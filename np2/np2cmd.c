/* np2cmd.c - Notepad2's Block / Convert / Insert / Special transforms,
 * converted from src/Edit.c.
 *
 * Same story as np2edit.c: these are Scintilla-message code, and the only Win32
 * in them is string handling, so most of the conversion is deleting the UTF-16
 * round trip. Three exceptions worth naming:
 *
 *   - UrlEscape / UrlUnescape are shlwapi, with no OS/2 counterpart at any layer.
 *     Written out here as RFC-3986 percent coding.
 *   - GetDateFormat / GetTimeFormat -> DosGetDateTime plus formatting. (The
 *     locale-aware route would be UniStrftime; this uses fixed forms, which is
 *     what Notepad2's "short/long form" defaults amount to anyway.)
 *   - Case conversion is ASCII-only, matching the rest of this port's byte
 *     orientation. The locale-correct route is UniTransUpper / UniTransLower and
 *     belongs with the encoding work, not scattered here.
 */
#define INCL_WIN
#define INCL_DOS
#define INCL_DOSERRORS
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "Scintilla.h"
#include "np2cmd.h"

static LONG SciMsg(HWND h, unsigned int msg, LONG wp, const void *lp)
{
    return LONGFROMMR(WinSendMsg(h, msg, MPFROMLONG(wp), MPFROMP((PVOID)lp)));
}
#define SciL(h, m, wp)      SciMsg((h), (m), (LONG)(wp), NULL)
#define SciP(h, m, wp, lp)  SciMsg((h), (m), (LONG)(wp), (const void *)(lp))

static BOOL IsRect(HWND h) { return (BOOL)(SciL(h, SCI_GETSELECTIONMODE, 0) == SC_SEL_RECTANGLE); }

static void WarnRect(HWND h)
{
    WinMessageBox(HWND_DESKTOP, h,
                  (PSZ)"This operation is not available for a rectangular selection.",
                  (PSZ)"Notepad2", 0, MB_OK | MB_WARNING | MB_MOVEABLE);
}

/* Fetch the selection, or the whole document when nothing is selected. Caller
 * frees. pStart and pEnd receive the range the text came from. */
static char *GetRangeText(HWND h, LONG *pStart, LONG *pEnd, BOOL bWholeDocIfEmpty)
{
    LONG s = SciL(h, SCI_GETSELECTIONSTART, 0);
    LONG e = SciL(h, SCI_GETSELECTIONEND, 0);
    char *p;

    if (s == e) {
        if (!bWholeDocIfEmpty)
            return NULL;
        s = 0;
        e = SciL(h, SCI_GETLENGTH, 0);
    }
    p = (char *)malloc(e - s + 1);
    if (!p)
        return NULL;
    {
        struct Sci_TextRange tr;
        tr.chrg.cpMin = s;
        tr.chrg.cpMax = e;
        tr.lpstrText  = p;
        SciP(h, SCI_GETTEXTRANGE, 0, &tr);
    }
    p[e - s] = '\0';
    *pStart = s;
    *pEnd   = e;
    return p;
}

/* Replace [s,e) with psz as one undo action, then reselect the result. */
static void ReplaceRange(HWND h, LONG s, LONG e, const char *psz)
{
    LONG cch = (LONG)strlen(psz);
    SciL(h, SCI_BEGINUNDOACTION, 0);
    SciL(h, SCI_SETTARGETSTART, s);
    SciL(h, SCI_SETTARGETEND,   e);
    SciP(h, SCI_REPLACETARGET, cch, psz);
    SciL(h, SCI_ENDUNDOACTION, 0);
    SciMsg(h, SCI_SETSEL, s, (const void *)(s + cch));
}

/*--------------------------------------------------------------------------
 * Convert - case
 *------------------------------------------------------------------------*/

typedef char (*CHARFN)(char, int *);

static char fnInvert(char c, int *st)
{
    (void)st;
    if (islower((unsigned char)c)) return (char)toupper((unsigned char)c);
    if (isupper((unsigned char)c)) return (char)tolower((unsigned char)c);
    return c;
}

/* *st tracks "at a word start". */
static char fnTitle(char c, int *st)
{
    if (isalpha((unsigned char)c) || c == '\'') {
        char r = *st ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c);
        *st = 0;
        return r;
    }
    *st = 1;
    return c;
}

/* *st tracks "at a sentence start". */
static char fnSentence(char c, int *st)
{
    if (isalpha((unsigned char)c)) {
        char r = *st ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c);
        *st = 0;
        return r;
    }
    if (c == '.' || c == '!' || c == '?' || c == '\r' || c == '\n')
        *st = 1;
    return c;
}

static void ConvertCase(HWND h, CHARFN fn)
{
    LONG s, e;
    char *p;
    int  st = 1;
    size_t i;

    if (IsRect(h)) { WarnRect(h); return; }
    p = GetRangeText(h, &s, &e, FALSE);       /* case ops need a selection */
    if (!p)
        return;
    for (i = 0; p[i]; i++)
        p[i] = fn(p[i], &st);
    ReplaceRange(h, s, e, p);
    free(p);
}

void EditInvertCase(HWND h)   { ConvertCase(h, fnInvert); }
void EditTitleCase(HWND h)    { ConvertCase(h, fnTitle); }
void EditSentenceCase(HWND h) { ConvertCase(h, fnSentence); }

/*--------------------------------------------------------------------------
 * Convert - tabs and spaces
 *------------------------------------------------------------------------*/

void EditTabsToSpaces(HWND h, int iTabWidth, BOOL bOnlyIndent)
{
    LONG s, e;
    char *in, *out, *ci, *co;
    LONG col = 0;
    BOOL bAtIndent = TRUE;
    size_t cap;

    if (IsRect(h)) { WarnRect(h); return; }
    if (iTabWidth < 1) iTabWidth = 4;
    in = GetRangeText(h, &s, &e, TRUE);
    if (!in) return;

    cap = strlen(in) * (size_t)iTabWidth + 2;
    out = (char *)malloc(cap);
    if (!out) { free(in); return; }

    for (ci = in, co = out; *ci; ci++) {
        if (*ci == '\t' && (!bOnlyIndent || bAtIndent)) {
            int n = iTabWidth - (int)(col % iTabWidth);
            while (n--) { *co++ = ' '; col++; }
        } else {
            if (*ci == '\n' || *ci == '\r') { col = 0; bAtIndent = TRUE; }
            else { col++; if (*ci != ' ' && *ci != '\t') bAtIndent = FALSE; }
            *co++ = *ci;
        }
    }
    *co = '\0';
    ReplaceRange(h, s, e, out);
    free(in); free(out);
}

void EditSpacesToTabs(HWND h, int iTabWidth, BOOL bOnlyIndent)
{
    LONG s, e;
    char *in, *out, *ci, *co;
    LONG col = 0;
    BOOL bAtIndent = TRUE;

    if (IsRect(h)) { WarnRect(h); return; }
    if (iTabWidth < 1) iTabWidth = 4;
    in = GetRangeText(h, &s, &e, TRUE);
    if (!in) return;
    out = (char *)malloc(strlen(in) + 2);
    if (!out) { free(in); return; }

    ci = in; co = out;
    while (*ci) {
        if (*ci == ' ' && (!bOnlyIndent || bAtIndent)) {
            /* Collapse a run of spaces that crosses a tab stop. */
            char *run = ci;
            LONG  startCol = col;
            while (*ci == ' ') { ci++; col++; }
            {
                LONG firstStop = ((startCol / iTabWidth) + 1) * iTabWidth;
                if (col >= firstStop) {
                    LONG c = startCol;
                    while (((c / iTabWidth) + 1) * iTabWidth <= col) {
                        *co++ = '\t';
                        c = ((c / iTabWidth) + 1) * iTabWidth;
                    }
                    while (c++ < col) *co++ = ' ';
                } else {
                    while (run < ci) { *co++ = *run++; }
                }
            }
        } else {
            if (*ci == '\n' || *ci == '\r') { col = 0; bAtIndent = TRUE; }
            else if (*ci == '\t') { col = ((col / iTabWidth) + 1) * iTabWidth; }
            else { col++; bAtIndent = FALSE; }
            *co++ = *ci++;
        }
    }
    *co = '\0';
    ReplaceRange(h, s, e, out);
    free(in); free(out);
}

/*--------------------------------------------------------------------------
 * Block
 *------------------------------------------------------------------------*/

/* Notepad2's convention: with no selection these act on the whole document. */
static void LineRange(HWND h, LONG *pFirst, LONG *pLast)
{
    LONG s = SciL(h, SCI_GETSELECTIONSTART, 0);
    LONG e = SciL(h, SCI_GETSELECTIONEND, 0);
    if (s == e) { s = 0; e = SciL(h, SCI_GETLENGTH, 0); }
    *pFirst = SciL(h, SCI_LINEFROMPOSITION, s);
    *pLast  = SciL(h, SCI_LINEFROMPOSITION, e);
    if (e <= SciL(h, SCI_POSITIONFROMLINE, *pLast) && *pLast > *pFirst)
        (*pLast)--;
}

void EditStripFirstCharacter(HWND h)
{
    LONG iLine, first, last;
    if (IsRect(h)) { WarnRect(h); return; }
    LineRange(h, &first, &last);
    SciL(h, SCI_BEGINUNDOACTION, 0);
    for (iLine = first; iLine <= last; iLine++) {
        LONG pos = SciL(h, SCI_POSITIONFROMLINE, iLine);
        if (SciL(h, SCI_GETLINEENDPOSITION, iLine) - pos > 0) {
            SciL(h, SCI_SETTARGETSTART, pos);
            SciL(h, SCI_SETTARGETEND, SciL(h, SCI_POSITIONAFTER, pos));
            SciP(h, SCI_REPLACETARGET, 0, "");
        }
    }
    SciL(h, SCI_ENDUNDOACTION, 0);
}

void EditStripLastCharacter(HWND h)
{
    LONG iLine, first, last;
    if (IsRect(h)) { WarnRect(h); return; }
    LineRange(h, &first, &last);
    SciL(h, SCI_BEGINUNDOACTION, 0);
    for (iLine = first; iLine <= last; iLine++) {
        LONG st  = SciL(h, SCI_POSITIONFROMLINE, iLine);
        LONG end = SciL(h, SCI_GETLINEENDPOSITION, iLine);
        if (end - st > 0) {
            SciL(h, SCI_SETTARGETSTART, SciL(h, SCI_POSITIONBEFORE, end));
            SciL(h, SCI_SETTARGETEND, end);
            SciP(h, SCI_REPLACETARGET, 0, "");
        }
    }
    SciL(h, SCI_ENDUNDOACTION, 0);
}

void EditStripTrailingBlanks(HWND h)
{
    LONG iLine, first, last;
    if (IsRect(h)) { WarnRect(h); return; }
    LineRange(h, &first, &last);
    SciL(h, SCI_BEGINUNDOACTION, 0);
    for (iLine = first; iLine <= last; iLine++) {
        LONG st  = SciL(h, SCI_POSITIONFROMLINE, iLine);
        LONG end = SciL(h, SCI_GETLINEENDPOSITION, iLine);
        LONG p   = end;
        while (p > st) {
            char c = (char)SciL(h, SCI_GETCHARAT, p - 1);
            if (c != ' ' && c != '\t') break;
            p--;
        }
        if (p < end) {
            SciL(h, SCI_SETTARGETSTART, p);
            SciL(h, SCI_SETTARGETEND, end);
            SciP(h, SCI_REPLACETARGET, 0, "");
        }
    }
    SciL(h, SCI_ENDUNDOACTION, 0);
}

void EditCompressSpaces(HWND h)
{
    LONG s, e;
    char *in, *out, *ci, *co;
    BOOL bLineStart, bModified = FALSE;

    if (IsRect(h)) { WarnRect(h); return; }
    in = GetRangeText(h, &s, &e, TRUE);
    if (!in) return;
    out = (char *)malloc(strlen(in) + 2);
    if (!out) { free(in); return; }

    bLineStart = (s == SciL(h, SCI_POSITIONFROMLINE, SciL(h, SCI_LINEFROMPOSITION, s)));

    for (ci = in, co = out; *ci; ci++) {
        if (*ci == ' ' || *ci == '\t') {
            if (*ci == '\t') bModified = TRUE;
            while (ci[1] == ' ' || ci[1] == '\t') { ci++; bModified = TRUE; }
            /* Runs at a line start, or immediately before a line end, vanish. */
            if (!bLineStart && ci[1] != '\n' && ci[1] != '\r' && ci[1] != '\0')
                *co++ = ' ';
            else
                bModified = TRUE;
        } else {
            bLineStart = (BOOL)(*ci == '\n' || *ci == '\r');
            *co++ = *ci;
        }
    }
    *co = '\0';
    if (bModified)
        ReplaceRange(h, s, e, out);
    free(in); free(out);
}

void EditRemoveBlankLines(HWND h, BOOL bMerge)
{
    LONG iLine, first, last;
    if (IsRect(h)) { WarnRect(h); return; }
    LineRange(h, &first, &last);

    SciL(h, SCI_BEGINUNDOACTION, 0);
    for (iLine = first; iLine <= last; ) {
        LONG nBlanks = 0;
        while (iLine + nBlanks <= last &&
               SciL(h, SCI_POSITIONFROMLINE, iLine + nBlanks) ==
               SciL(h, SCI_GETLINEENDPOSITION, iLine + nBlanks))
            nBlanks++;

        if (nBlanks == 0 || (nBlanks == 1 && bMerge)) {
            iLine += nBlanks + 1;
        } else {
            LONG ts, te;
            if (bMerge) nBlanks--;
            ts = SciL(h, SCI_POSITIONFROMLINE, iLine);
            te = SciL(h, SCI_POSITIONFROMLINE, iLine + nBlanks);
            SciL(h, SCI_SETTARGETSTART, ts);
            SciL(h, SCI_SETTARGETEND,   te);
            SciP(h, SCI_REPLACETARGET, 0, "");
            if (bMerge) iLine++;
            last -= nBlanks;
        }
    }
    SciL(h, SCI_ENDUNDOACTION, 0);
}

/* Pad every line in the range out to the longest line's length. */
void EditPadWithSpaces(HWND h)
{
    LONG iLine, first, last, maxCol = 0;
    if (IsRect(h)) { WarnRect(h); return; }
    LineRange(h, &first, &last);

    for (iLine = first; iLine <= last; iLine++) {
        LONG c = SciL(h, SCI_GETCOLUMN, SciL(h, SCI_GETLINEENDPOSITION, iLine));
        if (c > maxCol) maxCol = c;
    }
    if (maxCol <= 0) return;

    SciL(h, SCI_BEGINUNDOACTION, 0);
    for (iLine = first; iLine <= last; iLine++) {
        LONG end = SciL(h, SCI_GETLINEENDPOSITION, iLine);
        LONG c   = SciL(h, SCI_GETCOLUMN, end);
        if (c < maxCol) {
            char *pad = (char *)malloc(maxCol - c + 1);
            if (pad) {
                memset(pad, ' ', maxCol - c);
                pad[maxCol - c] = '\0';
                SciL(h, SCI_SETTARGETSTART, end);
                SciL(h, SCI_SETTARGETEND,   end);
                SciP(h, SCI_REPLACETARGET, maxCol - c, pad);
                free(pad);
            }
        }
    }
    SciL(h, SCI_ENDUNDOACTION, 0);
}

/*--------------------------------------------------------------------------
 * Lines - split / join
 *------------------------------------------------------------------------*/

static void GetEOL(HWND h, char *sz)
{
    LONG m = SciL(h, SCI_GETEOLMODE, 0);
    if (m == SC_EOL_CR)      strcpy(sz, "\r");
    else if (m == SC_EOL_LF) strcpy(sz, "\n");
    else                     strcpy(sz, "\r\n");
}

/* Split at the long-line edge column. */
void EditSplitLines(HWND h)
{
    SciL(h, SCI_BEGINUNDOACTION, 0);
    SciL(h, SCI_TARGETFROMSELECTION, 0);
    SciL(h, SCI_LINESSPLIT, 0);
    SciL(h, SCI_ENDUNDOACTION, 0);
}

/* bParagraph = Notepad2's "Join Paragraphs": blank lines are kept as separators. */
void EditJoinLines(HWND h, BOOL bParagraph)
{
    LONG s, e;
    char *in, *out, *ci, *co;
    char szEOL[3];

    if (IsRect(h)) { WarnRect(h); return; }
    if (!bParagraph) {
        SciL(h, SCI_BEGINUNDOACTION, 0);
        SciL(h, SCI_TARGETFROMSELECTION, 0);
        SciL(h, SCI_LINESJOIN, 0);
        SciL(h, SCI_ENDUNDOACTION, 0);
        return;
    }

    GetEOL(h, szEOL);
    in = GetRangeText(h, &s, &e, FALSE);
    if (!in) return;
    out = (char *)malloc(strlen(in) + 2);
    if (!out) { free(in); return; }

    for (ci = in, co = out; *ci; ) {
        if (*ci == '\r' || *ci == '\n') {
            /* Count how many line breaks in a row. Two or more = paragraph
             * boundary and is preserved; a single one becomes a space. */
            int breaks = 0;
            while (*ci == '\r' || *ci == '\n') {
                if (*ci == '\r' && ci[1] == '\n') ci++;
                ci++;
                breaks++;
            }
            if (breaks >= 2) {
                strcpy(co, szEOL); co += strlen(szEOL);
                strcpy(co, szEOL); co += strlen(szEOL);
            } else if (*ci) {
                *co++ = ' ';
            }
        } else {
            *co++ = *ci++;
        }
    }
    *co = '\0';
    ReplaceRange(h, s, e, out);
    free(in); free(out);
}

/*--------------------------------------------------------------------------
 * Special
 *------------------------------------------------------------------------*/

/* UrlEscape / UrlUnescape are shlwapi and have no OS/2 counterpart at any
 * layer, so this is RFC 3986 unreserved-set percent coding, written out. */
static int IsUnreserved(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

void EditURLEncode(HWND h)
{
    LONG s, e;
    char *in, *out, *co;
    unsigned char *ci;

    if (IsRect(h)) { WarnRect(h); return; }
    in = GetRangeText(h, &s, &e, FALSE);
    if (!in) return;
    out = (char *)malloc(strlen(in) * 3 + 1);
    if (!out) { free(in); return; }

    for (ci = (unsigned char *)in, co = out; *ci; ci++) {
        if (IsUnreserved(*ci)) {
            *co++ = (char)*ci;
        } else {
            sprintf(co, "%%%02X", *ci);
            co += 3;
        }
    }
    *co = '\0';
    ReplaceRange(h, s, e, out);
    free(in); free(out);
}

static int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void EditURLDecode(HWND h)
{
    LONG s, e;
    char *in, *out, *ci, *co;

    if (IsRect(h)) { WarnRect(h); return; }
    in = GetRangeText(h, &s, &e, FALSE);
    if (!in) return;
    out = (char *)malloc(strlen(in) + 1);
    if (!out) { free(in); return; }

    for (ci = in, co = out; *ci; ) {
        if (*ci == '%' && HexVal(ci[1]) >= 0 && HexVal(ci[2]) >= 0) {
            *co++ = (char)(HexVal(ci[1]) * 16 + HexVal(ci[2]));
            ci += 3;
        } else if (*ci == '+') {
            *co++ = ' '; ci++;
        } else {
            *co++ = *ci++;
        }
    }
    *co = '\0';
    ReplaceRange(h, s, e, out);
    free(in); free(out);
}

/* Escape / unescape backslash, double and single quote - the set Notepad2 does. */
static void EscapeWork(HWND h, BOOL bEscape)
{
    LONG s, e;
    char *in, *out, *ci, *co;

    if (IsRect(h)) { WarnRect(h); return; }
    in = GetRangeText(h, &s, &e, FALSE);
    if (!in) return;
    out = (char *)malloc(strlen(in) * 2 + 1);
    if (!out) { free(in); return; }

    for (ci = in, co = out; *ci; ) {
        if (bEscape) {
            if (*ci == '\\' || *ci == '"' || *ci == '\'')
                *co++ = '\\';
            *co++ = *ci++;
        } else {
            if (*ci == '\\' && (ci[1] == '\\' || ci[1] == '"' || ci[1] == '\'')) {
                ci++;
                *co++ = *ci++;
            } else {
                *co++ = *ci++;
            }
        }
    }
    *co = '\0';
    ReplaceRange(h, s, e, out);
    free(in); free(out);
}

void EditEscapeCChars(HWND h)   { EscapeWork(h, TRUE); }
void EditUnescapeCChars(HWND h) { EscapeWork(h, FALSE); }

void EditChar2Hex(HWND h)
{
    LONG s, e;
    char ch[32], outbuf[32];

    if (IsRect(h)) { WarnRect(h); return; }
    s = SciL(h, SCI_GETSELECTIONSTART, 0);
    e = SciL(h, SCI_GETSELECTIONEND, 0);
    if (e - s <= 0 || e - s > 8)
        return;
    {
        struct Sci_TextRange tr;
        tr.chrg.cpMin = s; tr.chrg.cpMax = e; tr.lpstrText = ch;
        SciP(h, SCI_GETTEXTRANGE, 0, &tr);
        ch[e - s] = '\0';
    }
    sprintf(outbuf, "\\x%02X", (unsigned char)ch[0]);
    ReplaceRange(h, s, e, outbuf);
}

void EditHex2Char(HWND h)
{
    LONG s, e;
    char ch[32];
    unsigned int v;

    if (IsRect(h)) { WarnRect(h); return; }
    s = SciL(h, SCI_GETSELECTIONSTART, 0);
    e = SciL(h, SCI_GETSELECTIONEND, 0);
    if (e - s <= 0 || e - s > 10)
        return;
    {
        struct Sci_TextRange tr;
        tr.chrg.cpMin = s; tr.chrg.cpMax = e; tr.lpstrText = ch;
        SciP(h, SCI_GETTEXTRANGE, 0, &tr);
        ch[e - s] = '\0';
    }
    if (ch[0] == '\\' && (ch[1] == 'x' || ch[1] == 'X' || ch[1] == 'u' || ch[1] == 'U')) {
        ch[0] = '0'; ch[1] = 'x';
    } else if (ch[0] == 'x' || ch[0] == 'X' || ch[0] == 'u' || ch[0] == 'U') {
        ch[0] = '0';
        memmove(ch + 2, ch + 1, strlen(ch));
        ch[1] = 'x';
    }
    if (sscanf(ch, "%x", &v) == 1 && v > 0 && v <= 0xFF) {
        char out[2];
        out[0] = (char)v;
        out[1] = '\0';
        ReplaceRange(h, s, e, out);
    }
}

/* Toggle a line comment on every line in the range. Removes if the first
 * non-blank line already starts with the marker, otherwise adds. */
void EditToggleLineComments(HWND h, const char *pszComment, BOOL bInsertAtStart)
{
    LONG iLine, first, last;
    int  cchComment = (int)strlen(pszComment);
    BOOL bRemove = TRUE;
    LONG minIndentPos = -1;

    if (IsRect(h)) { WarnRect(h); return; }
    LineRange(h, &first, &last);

    /* Decide add-vs-remove from the first non-blank line, and find the shallowest
     * indent so added markers line up. */
    for (iLine = first; iLine <= last; iLine++) {
        LONG ip  = SciL(h, SCI_GETLINEINDENTPOSITION, iLine);
        LONG end = SciL(h, SCI_GETLINEENDPOSITION, iLine);
        char buf[64];
        if (ip == end)
            continue;
        if (minIndentPos < 0)
            minIndentPos = SciL(h, SCI_GETCOLUMN, ip);
        else {
            LONG c = SciL(h, SCI_GETCOLUMN, ip);
            if (c < minIndentPos) minIndentPos = c;
        }
        if (bRemove) {
            struct Sci_TextRange tr;
            LONG want = ip + cchComment;
            if (want > end) want = end;
            tr.chrg.cpMin = ip; tr.chrg.cpMax = want; tr.lpstrText = buf;
            SciP(h, SCI_GETTEXTRANGE, 0, &tr);
            buf[want - ip] = '\0';
            if (strcmp(buf, pszComment) != 0)
                bRemove = FALSE;
        }
    }
    if (minIndentPos < 0)
        return;

    SciL(h, SCI_BEGINUNDOACTION, 0);
    for (iLine = first; iLine <= last; iLine++) {
        LONG ip  = SciL(h, SCI_GETLINEINDENTPOSITION, iLine);
        LONG end = SciL(h, SCI_GETLINEENDPOSITION, iLine);
        if (ip == end)
            continue;
        if (bRemove) {
            SciL(h, SCI_SETTARGETSTART, ip);
            SciL(h, SCI_SETTARGETEND,   ip + cchComment);
            SciP(h, SCI_REPLACETARGET, 0, "");
        } else {
            LONG at = bInsertAtStart
                    ? SciL(h, SCI_POSITIONFROMLINE, iLine)
                    : SciMsg(h, SCI_FINDCOLUMN, iLine, (const void *)minIndentPos);
            SciL(h, SCI_SETTARGETSTART, at);
            SciL(h, SCI_SETTARGETEND,   at);
            SciP(h, SCI_REPLACETARGET, cchComment, pszComment);
        }
    }
    SciL(h, SCI_ENDUNDOACTION, 0);
}

void EditFindMatchingBrace(HWND h, BOOL bSelect)
{
    LONG pos   = SciL(h, SCI_GETCURRENTPOS, 0);
    LONG match = SciL(h, SCI_BRACEMATCH, pos);

    /* The caret may sit just after the brace rather than on it. */
    if (match < 0 && pos > 0) {
        pos = SciL(h, SCI_POSITIONBEFORE, pos);
        match = SciL(h, SCI_BRACEMATCH, pos);
    }
    if (match < 0)
        return;

    if (bSelect) {
        if (match > pos) SciMsg(h, SCI_SETSEL, pos, (const void *)(match + 1));
        else             SciMsg(h, SCI_SETSEL, match, (const void *)(pos + 1));
    } else {
        SciL(h, SCI_GOTOPOS, match);
    }
}

/*--------------------------------------------------------------------------
 * Insert
 *------------------------------------------------------------------------*/

void EditInsertString(HWND h, const char *psz)
{
    SciL(h, SCI_BEGINUNDOACTION, 0);
    SciP(h, SCI_REPLACESEL, 0, psz);
    SciL(h, SCI_ENDUNDOACTION, 0);
}

/* GetDateFormat / GetTimeFormat -> DosGetDateTime [os2ref/kernel-services.md].
 * Fixed forms rather than locale-driven; UniStrftime is the locale-aware route
 * and belongs with the encoding work. */
void EditInsertDateTime(HWND h, BOOL bShort)
{
    DATETIME dt;
    char sz[64];
    static const char *aMon[] = { "January","February","March","April","May","June",
                                  "July","August","September","October","November","December" };

    if (DosGetDateTime(&dt) != NO_ERROR)
        return;

    if (bShort)
        sprintf(sz, "%04d-%02d-%02d %02d:%02d",
                dt.year, dt.month, dt.day, dt.hours, dt.minutes);
    else
        sprintf(sz, "%02d %s %04d %02d:%02d:%02d",
                dt.day, aMon[(dt.month >= 1 && dt.month <= 12) ? dt.month - 1 : 0],
                dt.year, dt.hours, dt.minutes, dt.seconds);

    EditInsertString(h, sz);
}

/*--------------------------------------------------------------------------
 * Clipboard - Copy Add (append the selection to what is already there)
 *------------------------------------------------------------------------*/

void EditCopyAppend(HWND h)
{
    LONG s, e;
    char *sel, *combined;
    HAB  hab = WinQueryAnchorBlock(h);
    ULONG cbOld = 0;
    char *pOld = NULL;
    PVOID pShared;
    char szEOL[3];

    sel = GetRangeText(h, &s, &e, FALSE);
    if (!sel)
        return;
    GetEOL(h, szEOL);

    /* Read what is on the clipboard now, so this appends rather than replaces. */
    if (WinOpenClipbrd(hab)) {
        PSZ p = (PSZ)WinQueryClipbrdData(hab, CF_TEXT);
        if (p) {
            cbOld = (ULONG)strlen((char *)p);
            pOld = (char *)malloc(cbOld + 1);
            if (pOld) strcpy(pOld, (char *)p);
        }
        WinCloseClipbrd(hab);
    }

    combined = (char *)malloc(cbOld + strlen(sel) + 4);
    if (!combined) { free(sel); free(pOld); return; }
    combined[0] = '\0';
    if (pOld && pOld[0]) {
        strcpy(combined, pOld);
        strcat(combined, szEOL);
    }
    strcat(combined, sel);

    /* Clipboard memory must be shared and giveable - the process that reads it
     * is not this one [os2ref/clipboard-dde.md]. */
    if (DosAllocSharedMem(&pShared, NULL, strlen(combined) + 1,
                          PAG_COMMIT | PAG_READ | PAG_WRITE | OBJ_GIVEABLE) == NO_ERROR) {
        strcpy((char *)pShared, combined);
        if (WinOpenClipbrd(hab)) {
            WinEmptyClipbrd(hab);
            WinSetClipbrdData(hab, (ULONG)pShared, CF_TEXT, CFI_POINTER);
            WinCloseClipbrd(hab);
        } else {
            DosFreeMem(pShared);
        }
    }

    free(sel); free(pOld); free(combined);
}

/*--------------------------------------------------------------------------
 * Mark Occurrences - converted from Edit.c's EditMarkAll.
 *
 * Driven from SCN_UPDATEUI so the marks follow the selection. Indicator 1 is
 * used throughout; the colour formula is Notepad2's own, and it is correct
 * against Scintilla's 0xBBGGRR order once you know the index order is
 * 1=red, 2=green, 3=blue.
 *------------------------------------------------------------------------*/

void EditMarkAll(HWND h, int iMark, BOOL bMatchCase, BOOL bMatchWords)
{
    struct Sci_TextToFind ttf;
    LONG  iPos, iTextLen, iSelStart, iSelEnd, iSelCount;
    char *pszText;
    int   iMatches = 0;

    iTextLen = SciL(h, SCI_GETLENGTH, 0);

    /* Always clear first, so switching the feature off actually clears. */
    SciL(h, SCI_SETINDICATORCURRENT, 1);
    SciMsg(h, SCI_INDICATORCLEARRANGE, 0, (const void *)iTextLen);

    if (!iMark)
        return;

    iSelStart = SciL(h, SCI_GETSELECTIONSTART, 0);
    iSelEnd   = SciL(h, SCI_GETSELECTIONEND, 0);
    iSelCount = iSelEnd - iSelStart;

    /* Nothing selected, or a multi-line selection - nothing to mark. */
    if (iSelCount <= 0 ||
        SciL(h, SCI_LINEFROMPOSITION, iSelStart) != SciL(h, SCI_LINEFROMPOSITION, iSelEnd))
        return;

    pszText = (char *)malloc(iSelCount + 1);
    if (!pszText)
        return;
    {
        struct Sci_TextRange tr;
        tr.chrg.cpMin = iSelStart;
        tr.chrg.cpMax = iSelEnd;
        tr.lpstrText  = pszText;
        SciP(h, SCI_GETTEXTRANGE, 0, &tr);
        pszText[iSelCount] = '\0';
    }

    /* With whole-words on, a selection containing punctuation is not a word. */
    if (bMatchWords) {
        LONG i;
        for (i = 0; pszText[i]; i++) {
            if (strchr(" \t\r\n@#$%^&*~-=+()[]{}\\/:;'\"", pszText[i])) {
                free(pszText);
                return;
            }
        }
    }

    memset(&ttf, 0, sizeof(ttf));
    ttf.chrg.cpMin = 0;
    ttf.chrg.cpMax = iTextLen;
    ttf.lpstrText  = pszText;

    SciMsg(h, SCI_INDICSETALPHA, 1, (const void *)100);
    SciMsg(h, SCI_INDICSETFORE,  1, (const void *)(LONG)(0xffL << ((iMark - 1) << 3)));
    SciMsg(h, SCI_INDICSETSTYLE, 1, (const void *)INDIC_ROUNDBOX);

    /* The 2000 cap is Notepad2's, and it matters: this runs on every caret
     * move, so an unbounded scan on a large file would make typing crawl. */
    while ((iPos = SciP(h, SCI_FINDTEXT,
                        (bMatchCase ? SCFIND_MATCHCASE : 0) |
                        (bMatchWords ? SCFIND_WHOLEWORD : 0), &ttf)) != -1
           && ++iMatches < 2000) {
        SciMsg(h, SCI_INDICATORFILLRANGE, iPos, (const void *)iSelCount);
        ttf.chrg.cpMin = iPos + iSelCount;
        ttf.chrg.cpMax = iTextLen;
        if (ttf.chrg.cpMin >= ttf.chrg.cpMax)
            break;
    }

    free(pszText);
}

/* ---- Clipboard swap, word selection, word completion -----------------------
 * All three are plain Scintilla-message code; nothing here is platform-specific
 * beyond the WinSendMsg wrapper above.
 */

/* Exchange the selection with the clipboard. With nothing selected, Notepad2
 * pastes and selects what it pasted, then empties the clipboard. */
void EditSwapClipboard(HWND h)
{
    const LONG selStart = SciL(h, SCI_GETSELECTIONSTART, 0);
    const LONG selEnd   = SciL(h, SCI_GETSELECTIONEND, 0);

    if (selEnd == selStart) {
        const LONG pos = SciL(h, SCI_GETCURRENTPOS, 0);
        SciL(h, SCI_PASTE, 0);
        SciMsg(h, SCI_SETSEL, pos, (const void *)(LONG)SciL(h, SCI_GETCURRENTPOS, 0));
        return;
    }

    {
        /* Take a copy of the selection before the paste replaces it, then put
         * that copy back on the clipboard afterwards. */
        const LONG cch = selEnd - selStart;
        char *pOld = (char *)malloc((size_t)cch + 1);
        if (!pOld)
            return;
        SciP(h, SCI_GETSELTEXT, 0, pOld);
        pOld[cch] = '\0';

        SciL(h, SCI_BEGINUNDOACTION, 0);
        SciL(h, SCI_PASTE, 0);
        SciL(h, SCI_ENDUNDOACTION, 0);

        SciP(h, SCI_COPYTEXT, cch, pOld);
        free(pOld);
    }
}

/* Select the word under the caret, leaving the selection alone if there is one. */
void EditSelectWord(HWND h)
{
    const LONG pos = SciL(h, SCI_GETCURRENTPOS, 0);
    LONG s, e;
    if (SciL(h, SCI_GETSELECTIONEND, 0) != SciL(h, SCI_GETSELECTIONSTART, 0))
        return;
    s = SciMsg(h, SCI_WORDSTARTPOSITION, pos, (const void *)(LONG)TRUE);
    e = SciMsg(h, SCI_WORDENDPOSITION,   pos, (const void *)(LONG)TRUE);
    if (e > s)
        SciMsg(h, SCI_SETSEL, s, (const void *)e);
}

/* Copy the selection - or the word at the caret - into pszOut. Returns its
 * length, or 0 when there is nothing usable. */
LONG EditGetSelOrWord(HWND h, char *pszOut, LONG cchOut)
{
    LONG s = SciL(h, SCI_GETSELECTIONSTART, 0);
    LONG e = SciL(h, SCI_GETSELECTIONEND, 0);
    if (e == s) {
        EditSelectWord(h);
        s = SciL(h, SCI_GETSELECTIONSTART, 0);
        e = SciL(h, SCI_GETSELECTIONEND, 0);
    }
    if (e <= s || (e - s) >= cchOut)
        return 0;
    SciP(h, SCI_GETSELTEXT, 0, pszOut);
    pszOut[e - s] = '\0';
    /* A find string spanning a line break is not useful. */
    if (strpbrk(pszOut, "\r\n")) {
        pszOut[0] = '\0';
        return 0;
    }
    return e - s;
}

/* Autocompletion from words already in the document - Notepad2's "Complete Word".
 *
 * Collects every distinct word that begins with the partial word at the caret and
 * hands the list to Scintilla, which owns the popup. SCFIND_WORDSTART restricts
 * matches to word beginnings, so "co" offers "count" but not "iconv".
 */
static int CmpWord(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

#define NP2_MAXCOMPLETIONS 500
#define NP2_MAXWORDLEN     128

BOOL EditCompleteWord(HWND h)
{
    const LONG pos       = SciL(h, SCI_GETCURRENTPOS, 0);
    const LONG wordStart = SciMsg(h, SCI_WORDSTARTPOSITION, pos, (const void *)(LONG)TRUE);
    const LONG cchPrefix = pos - wordStart;
    const LONG cchDoc    = SciL(h, SCI_GETLENGTH, 0);
    char   szPrefix[NP2_MAXWORDLEN];
    char  *apWords[NP2_MAXCOMPLETIONS];
    int    cWords = 0, i;
    LONG   iStart = 0;
    struct Sci_TextRange tr;
    struct Sci_TextToFind ttf;
    char  *pszList;
    size_t cbList = 1;

    if (cchPrefix < 1 || cchPrefix >= (LONG)sizeof(szPrefix))
        return FALSE;

    tr.chrg.cpMin = wordStart;
    tr.chrg.cpMax = pos;
    tr.lpstrText  = szPrefix;
    SciP(h, SCI_GETTEXTRANGE, 0, &tr);
    szPrefix[cchPrefix] = '\0';

    while (iStart < cchDoc && cWords < NP2_MAXCOMPLETIONS) {
        LONG found, wordEnd, cchWord;
        char szWord[NP2_MAXWORDLEN];

        ttf.chrg.cpMin = iStart;
        ttf.chrg.cpMax = cchDoc;
        ttf.lpstrText  = szPrefix;
        found = SciMsg(h, SCI_FINDTEXT,
            (LONG)(SCFIND_WORDSTART | SCFIND_MATCHCASE), &ttf);
        if (found < 0)
            break;
        iStart = ttf.chrgText.cpMax;

        wordEnd = SciMsg(h, SCI_WORDENDPOSITION, ttf.chrgText.cpMin,
            (const void *)(LONG)TRUE);
        cchWord = wordEnd - ttf.chrgText.cpMin;
        /* Skip the word being typed, and anything too long to be useful. */
        if (cchWord <= cchPrefix || cchWord >= (LONG)sizeof(szWord))
            continue;
        if (ttf.chrgText.cpMin == wordStart)
            continue;

        tr.chrg.cpMin = ttf.chrgText.cpMin;
        tr.chrg.cpMax = wordEnd;
        tr.lpstrText  = szWord;
        SciP(h, SCI_GETTEXTRANGE, 0, &tr);
        szWord[cchWord] = '\0';

        for (i = 0; i < cWords; i++)
            if (strcmp(apWords[i], szWord) == 0)
                break;
        if (i < cWords)
            continue;                      /* already have it */

        apWords[cWords] = strdup(szWord);
        if (!apWords[cWords])
            break;
        cbList += strlen(szWord) + 1;
        cWords++;
    }

    if (cWords == 0)
        return FALSE;

    qsort(apWords, (size_t)cWords, sizeof(apWords[0]), CmpWord);

    pszList = (char *)malloc(cbList);
    if (pszList) {
        pszList[0] = '\0';
        for (i = 0; i < cWords; i++) {
            if (i)
                strcat(pszList, "\n");
            strcat(pszList, apWords[i]);
        }
        /* A space would split words that cannot contain one anyway, but newline
         * is unambiguous and matches what this list can never hold. */
        SciL(h, SCI_AUTOCSETSEPARATOR, (LONG)'\n');
        SciP(h, SCI_AUTOCSHOW, cchPrefix, pszList);
        free(pszList);
    }
    for (i = 0; i < cWords; i++)
        free(apWords[i]);
    return TRUE;
}

/* Build a one-line excerpt of the selection for the title bar: runs of white
 * space (including line breaks) collapse to a single space, the result is
 * trimmed, and anything too long is cut with an ellipsis. An empty or
 * rectangular selection yields an empty string, which the caller reads as
 * "nothing to show". */
void EditGetExcerpt(HWND h, char *pszOut, int cchOut)
{
    LONG  s, e, n;
    char *pRaw;
    int   o = 0;
    struct Sci_TextRange tr;

    if (cchOut < 8)
        return;
    pszOut[0] = '\0';

    if (IsRect(h))
        return;
    s = SciL(h, SCI_GETSELECTIONSTART, 0);
    e = SciL(h, SCI_GETSELECTIONEND, 0);
    if (e <= s)
        return;

    /* Read at most what can survive the collapse, not the whole selection. */
    n = e - s;
    if (n > cchOut * 4)
        n = cchOut * 4;
    pRaw = (char *)malloc((size_t)n + 1);
    if (!pRaw)
        return;
    tr.chrg.cpMin = s;
    tr.chrg.cpMax = s + n;
    tr.lpstrText  = pRaw;
    SciP(h, SCI_GETTEXTRANGE, 0, &tr);
    pRaw[n] = '\0';

    {
        const char *p = pRaw;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;                                   /* leading space */
        for (; *p && o < cchOut - 1; p++) {
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                pszOut[o++] = ' ';
                while (p[1] == ' ' || p[1] == '\t' || p[1] == '\r' || p[1] == '\n')
                    p++;
            } else {
                pszOut[o++] = *p;
            }
        }
        while (o > 0 && pszOut[o - 1] == ' ')
            o--;                                   /* trailing space */
        pszOut[o] = '\0';
    }
    free(pRaw);

    /* Truncated, or more selection than was read: say so rather than implying
     * the excerpt is the whole of it. */
    if (o >= cchOut - 1 || (e - s) > n)
        strcpy(pszOut + (cchOut - 4 < o ? cchOut - 4 : o), "...");
}

/* Column Wrap: re-flow the selection so no line exceeds nCol columns.
 *
 * Breaks only at existing white space, so a word longer than the column limit
 * overhangs rather than being cut in half. Blank lines are preserved, since
 * they usually separate paragraphs and losing them would change the text's
 * structure rather than just its line breaks.
 */
void EditWrapToColumn(HWND h, int nCol)
{
    LONG  s, e, cch;
    char *pIn, *pOut;
    LONG  o = 0, col = 0;
    LONG  i;
    size_t cbOut;

    if (nCol < 1)
        return;
    if (IsRect(h)) { WarnRect(h); return; }

    s = SciL(h, SCI_GETSELECTIONSTART, 0);
    e = SciL(h, SCI_GETSELECTIONEND, 0);
    if (e <= s)
        return;
    cch = e - s;

    pIn = (char *)malloc((size_t)cch + 1);
    if (!pIn)
        return;
    SciP(h, SCI_GETSELTEXT, 0, pIn);
    pIn[cch] = '\0';

    /* Worst case one added line break per character. */
    cbOut = (size_t)cch * 2 + 2;
    pOut = (char *)malloc(cbOut);
    if (!pOut) { free(pIn); return; }

    for (i = 0; i < cch; i++) {
        const char c = pIn[i];

        if (c == '\r' || c == '\n') {
            /* Keep a blank line; otherwise treat the break as a space so the
             * paragraph re-flows. */
            LONG j = i;
            int  breaks = 0;
            while (j < cch && (pIn[j] == '\r' || pIn[j] == '\n')) {
                if (pIn[j] == '\n') breaks++;
                j++;
            }
            if (breaks > 1) {
                pOut[o++] = '\n'; pOut[o++] = '\n';
                col = 0;
            } else if (col > 0) {
                pOut[o++] = ' ';
                col++;
            }
            i = j - 1;
            continue;
        }

        if (c == ' ' || c == '\t') {
            if (col > 0 && pOut[o - 1] != ' ') { pOut[o++] = ' '; col++; }
            continue;
        }

        /* Start of a word: measure it, and break the line first if it will not
         * fit and the line already has something on it. */
        {
            LONG w = 0;
            while (i + w < cch && pIn[i + w] != ' ' && pIn[i + w] != '\t' &&
                   pIn[i + w] != '\r' && pIn[i + w] != '\n')
                w++;
            if (col + w > nCol && col > 0) {
                if (o > 0 && pOut[o - 1] == ' ') { o--; }
                pOut[o++] = '\n';
                col = 0;
            }
            memcpy(pOut + o, pIn + i, (size_t)w);
            o += w; col += w;
            i += w - 1;
        }
    }
    while (o > 0 && (pOut[o - 1] == ' ' || pOut[o - 1] == '\n'))
        o--;
    pOut[o] = '\0';

    SciL(h, SCI_BEGINUNDOACTION, 0);
    SciP(h, SCI_REPLACESEL, 0, pOut);
    SciL(h, SCI_ENDUNDOACTION, 0);
    SciMsg(h, SCI_SETSEL, s, (const void *)(LONG)(s + o));

    free(pOut);
    free(pIn);
}

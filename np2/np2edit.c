/* np2edit.c - Notepad2's buffer operations, converted from src/Edit.c.
 *
 * Converted: EditJumpTo, EditEncloseSelection, EditModifyLines, EditAlignText,
 * EditSortLines.
 *
 * These turned out to be the *easiest* part of the port, and the reason is worth
 * recording. All five are pure Scintilla-message code; the only Win32 in them is
 * string handling. In particular every one of them begins by calling
 * WideCharToMultiByte to turn the dialog's UTF-16 input into the editor's code
 * page - and in this port the dialogs hand back `char` already, so that call has
 * no counterpart to write. **The conversion is a deletion.** Same shape as the
 * modeless message loop: the Win32 original carries machinery that exists to
 * bridge a gap PM does not have.
 *
 * Three places the platform genuinely forced a change:
 *
 *   1. StrCmpLogicalW is a Windows SHELL API (shlwapi), not a C or Win32 API, and
 *      has no OS/2 analogue at any layer. Notepad2 even GetProcAddress()es it and
 *      degrades if absent. Hand-written below as CmpLogical.
 *   2. GetTickCount -> WinGetCurrentTime(hab) [os2ref/pm-window-messaging.md].
 *   3. LocalAlloc/LocalFree/LocalSize -> malloc/free with explicit sizes.
 *
 * Two deliberate limitations, both stated rather than papered over:
 *
 *   - Sorting and alignment work on BYTES, where the original converts to UTF-16
 *     first. For ASCII the result is identical. For a UTF-8 document, sort order
 *     becomes byte order rather than UTF-16 collation, and alignment counts bytes
 *     rather than characters. Doing better needs UniUconv against the editor's
 *     code page - the same dependency that blocks the encoding dialogs - so it is
 *     left for that work rather than guessed at now.
 *   - Rectangular (column) selection is not handled: it needs EditPadWithSpaces
 *     and the SCI_SETRECTANGULARSELECTION* API, neither of which this port has
 *     exercised. The operations say so instead of silently treating a rectangular
 *     selection as a stream one, which would quietly mangle the document.
 */
#define INCL_WIN
#define INCL_DOS
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "Scintilla.h"
#include "np2edit.h"
#include "np2find.h"    /* EditSelectEx */

/*--------------------------------------------------------------------------
 * Scintilla access
 *------------------------------------------------------------------------*/

static LONG SciMsg(HWND h, unsigned int msg, LONG wp, const void *lp)
{
    return LONGFROMMR(WinSendMsg(h, msg, MPFROMLONG(wp), MPFROMP((PVOID)lp)));
}

#define SciL(h, m, wp)      SciMsg((h), (m), (LONG)(wp), NULL)
#define SciP(h, m, wp, lp)  SciMsg((h), (m), (LONG)(wp), (const void *)(lp))

static void WarnRect(HWND hwndEdit)
{
    WinMessageBox(HWND_DESKTOP, hwndEdit,
                  (PSZ)"This operation is not available for a rectangular selection.",
                  (PSZ)"Notepad2", 0, MB_OK | MB_WARNING | MB_MOVEABLE);
}

static BOOL IsRectSel(HWND h)
{
    return (BOOL)(SciL(h, SCI_GETSELECTIONMODE, 0) == SC_SEL_RECTANGLE);
}

/*--------------------------------------------------------------------------
 * EditJumpTo - Go to line/column
 *------------------------------------------------------------------------*/

void EditJumpTo(HWND hwndEdit, LONG iNewLine, LONG iNewCol)
{
    LONG iMaxLine = SciL(hwndEdit, SCI_GETLINECOUNT, 0);

    if (iNewLine == -1) {                 /* -1 means "end of document" */
        SciL(hwndEdit, SCI_DOCUMENTEND, 0);
        return;
    }

    if (iNewLine > iMaxLine) iNewLine = iMaxLine;
    if (iNewCol  < 1)        iNewCol  = 1;

    if (iNewLine > 0 && iNewLine <= iMaxLine) {
        LONG iNewPos     = SciL(hwndEdit, SCI_POSITIONFROMLINE,   iNewLine - 1);
        LONG iLineEndPos = SciL(hwndEdit, SCI_GETLINEENDPOSITION, iNewLine - 1);

        /* Walk forward by COLUMN, not by byte - a tab is one byte and several
         * columns, so byte arithmetic lands in the wrong place on indented text. */
        while (iNewCol - 1 > SciL(hwndEdit, SCI_GETCOLUMN, iNewPos)) {
            if (iNewPos >= iLineEndPos)
                break;
            iNewPos = SciL(hwndEdit, SCI_POSITIONAFTER, iNewPos);
        }

        if (iNewPos > iLineEndPos)
            iNewPos = iLineEndPos;
        EditSelectEx(hwndEdit, -1, iNewPos);   /* SCI_SETSEL(-1,pos) == goto pos */
        SciL(hwndEdit, SCI_CHOOSECARETX, 0);
    }
}

/*--------------------------------------------------------------------------
 * EditEncloseSelection - wrap the selection in an opening/closing string
 *------------------------------------------------------------------------*/

void EditEncloseSelection(HWND hwndEdit, const char *pszOpen, const char *pszClose)
{
    LONG iSelStart, iSelEnd;
    int  cchOpen, cchClose;

    if (IsRectSel(hwndEdit)) {
        WarnRect(hwndEdit);
        return;
    }

    iSelStart = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
    iSelEnd   = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);
    cchOpen   = pszOpen  ? (int)strlen(pszOpen)  : 0;
    cchClose  = pszClose ? (int)strlen(pszClose) : 0;

    SciL(hwndEdit, SCI_BEGINUNDOACTION, 0);

    if (cchOpen) {
        SciL(hwndEdit, SCI_SETTARGETSTART, iSelStart);
        SciL(hwndEdit, SCI_SETTARGETEND,   iSelStart);
        SciP(hwndEdit, SCI_REPLACETARGET,  cchOpen, pszOpen);
    }
    if (cchClose) {
        /* The opening insert shifted everything after it - the close goes in at
         * iSelEnd + cchOpen, not iSelEnd. */
        SciL(hwndEdit, SCI_SETTARGETSTART, iSelEnd + cchOpen);
        SciL(hwndEdit, SCI_SETTARGETEND,   iSelEnd + cchOpen);
        SciP(hwndEdit, SCI_REPLACETARGET,  cchClose, pszClose);
    }

    SciL(hwndEdit, SCI_ENDUNDOACTION, 0);

    if (iSelStart == iSelEnd) {
        SciMsg(hwndEdit, SCI_SETSEL, iSelStart + cchOpen,
               (const void *)(iSelStart + cchOpen));
    } else {
        LONG iCurPos    = SciL(hwndEdit, SCI_GETCURRENTPOS, 0);
        LONG iAnchorPos = SciL(hwndEdit, SCI_GETANCHOR, 0);
        if (iCurPos < iAnchorPos) {
            iCurPos    = iSelStart + cchOpen;
            iAnchorPos = iSelEnd   + cchOpen;
        } else {
            iAnchorPos = iSelStart + cchOpen;
            iCurPos    = iSelEnd   + cchOpen;
        }
        SciMsg(hwndEdit, SCI_SETSEL, iAnchorPos, (const void *)iCurPos);
    }
}

/*--------------------------------------------------------------------------
 * EditModifyLines - prefix and/or append text to each selected line,
 * with Notepad2's $(L) $(0L) $(N) $(0N) $(I) $(0I) numbering tokens.
 *------------------------------------------------------------------------*/

/* Split psz at the first numbering token. Returns TRUE if one was found; on
 * success psz is truncated at the token, pszTail receives the text after it, and
 * the numbering parameters are filled in. Mirrors the original's cascade. */
static BOOL SplitNumToken(char *psz, char *pszTail, int cchTail,
                          LONG iLineStart, LONG iLineEnd,
                          LONG *piNum, int *piWidth, const char **ppszPad)
{
    static const struct { const char *tok; int base; const char *pad; } toks[] = {
        { "$(I)",  0, ""  }, { "$(0I)", 0, "0" },
        { "$(N)",  1, ""  }, { "$(0N)", 1, "0" },
        { "$(L)",  2, ""  }, { "$(0L)", 2, "0" },
    };
    char *p = psz;

    while ((p = strstr(p, "$(")) != NULL) {
        size_t i;
        for (i = 0; i < sizeof(toks) / sizeof(toks[0]); i++) {
            size_t len = strlen(toks[i].tok);
            if (strncmp(p, toks[i].tok, len) == 0) {
                LONG span;
                int  w = 1;

                *p = '\0';
                strncpy(pszTail, p + len, cchTail - 1);
                pszTail[cchTail - 1] = '\0';

                switch (toks[i].base) {
                case 0:  *piNum = 0;               span = iLineEnd - iLineStart;     break;
                case 1:  *piNum = 1;               span = iLineEnd - iLineStart + 1; break;
                default: *piNum = iLineStart + 1;  span = iLineEnd + 1;              break;
                }
                for (; span >= 10; span /= 10)
                    w++;
                *piWidth  = w;
                *ppszPad  = toks[i].pad;
                return TRUE;
            }
        }
        p += 2;                      /* past the "$(" that did not match */
    }
    return FALSE;
}

void EditModifyLines(HWND hwndEdit, const char *pszPrefix, const char *pszAppend)
{
    char szPrefix1[768] = "", szPrefix2[768] = "";
    char szAppend1[768] = "", szAppend2[768] = "";
    BOOL bPrefixNum = FALSE, bAppendNum = FALSE;
    LONG iPrefixNum = 0, iAppendNum = 0;
    int  iPrefixNumWidth = 1, iAppendNumWidth = 1;
    const char *pszPrefixNumPad = "", *pszAppendNumPad = "";
    LONG iSelStart, iSelEnd, iLineStart, iLineEnd, iLine;

    if (IsRectSel(hwndEdit)) {
        WarnRect(hwndEdit);
        return;
    }

    iSelStart = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
    iSelEnd   = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);

    if (pszPrefix) { strncpy(szPrefix1, pszPrefix, sizeof(szPrefix1) - 1); }
    if (pszAppend) { strncpy(szAppend1, pszAppend, sizeof(szAppend1) - 1); }

    iLineStart = SciL(hwndEdit, SCI_LINEFROMPOSITION, iSelStart);
    iLineEnd   = SciL(hwndEdit, SCI_LINEFROMPOSITION, iSelEnd);

    /* A selection ending exactly at the start of a line does not include that
     * line - otherwise dragging through three lines modifies four. */
    if (iSelEnd <= SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineEnd)) {
        if (iLineEnd - iLineStart >= 1)
            iLineEnd--;
    }

    if (szPrefix1[0])
        bPrefixNum = SplitNumToken(szPrefix1, szPrefix2, sizeof(szPrefix2),
                                   iLineStart, iLineEnd,
                                   &iPrefixNum, &iPrefixNumWidth, &pszPrefixNumPad);
    if (szAppend1[0])
        bAppendNum = SplitNumToken(szAppend1, szAppend2, sizeof(szAppend2),
                                   iLineStart, iLineEnd,
                                   &iAppendNum, &iAppendNumWidth, &pszAppendNumPad);

    SciL(hwndEdit, SCI_BEGINUNDOACTION, 0);

    for (iLine = iLineStart; iLine <= iLineEnd; iLine++) {
        LONG iPos;

        if (pszPrefix && pszPrefix[0]) {
            char szInsert[1600];
            strcpy(szInsert, szPrefix1);
            if (bPrefixNum) {
                char szFmt[64], szNum[64];
                sprintf(szFmt, "%%%s%dld", pszPrefixNumPad, iPrefixNumWidth);
                sprintf(szNum, szFmt, (long)iPrefixNum);
                strcat(szInsert, szNum);
                strcat(szInsert, szPrefix2);
                iPrefixNum++;
            }
            iPos = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLine);
            SciL(hwndEdit, SCI_SETTARGETSTART, iPos);
            SciL(hwndEdit, SCI_SETTARGETEND,   iPos);
            SciP(hwndEdit, SCI_REPLACETARGET, (LONG)strlen(szInsert), szInsert);
        }

        if (pszAppend && pszAppend[0]) {
            char szInsert[1600];
            strcpy(szInsert, szAppend1);
            if (bAppendNum) {
                char szFmt[64], szNum[64];
                sprintf(szFmt, "%%%s%dld", pszAppendNumPad, iAppendNumWidth);
                sprintf(szNum, szFmt, (long)iAppendNum);
                strcat(szInsert, szNum);
                strcat(szInsert, szAppend2);
                iAppendNum++;
            }
            iPos = SciL(hwndEdit, SCI_GETLINEENDPOSITION, iLine);
            SciL(hwndEdit, SCI_SETTARGETSTART, iPos);
            SciL(hwndEdit, SCI_SETTARGETEND,   iPos);
            SciP(hwndEdit, SCI_REPLACETARGET, (LONG)strlen(szInsert), szInsert);
        }
    }

    SciL(hwndEdit, SCI_ENDUNDOACTION, 0);

    if (iSelStart != iSelEnd) {
        LONG iCurPos    = SciL(hwndEdit, SCI_GETCURRENTPOS, 0);
        LONG iAnchorPos = SciL(hwndEdit, SCI_GETANCHOR, 0);
        LONG a = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineStart);
        LONG b = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineEnd + 1);
        if (iCurPos < iAnchorPos) { iCurPos = a; iAnchorPos = b; }
        else                      { iAnchorPos = a; iCurPos = b; }
        SciMsg(hwndEdit, SCI_SETSEL, iAnchorPos, (const void *)iCurPos);
    }
}

/*--------------------------------------------------------------------------
 * EditAlignText - left / right / centre / justify the selected lines
 *------------------------------------------------------------------------*/

#define BUFSIZE_ALIGN 1024

static void TrimEnds(char *psz, const char *pszSet)
{
    char *p = psz, *end;
    while (*p && strchr(pszSet, *p)) p++;
    if (p != psz) memmove(psz, p, strlen(p) + 1);
    end = psz + strlen(psz);
    while (end > psz && strchr(pszSet, end[-1])) end--;
    *end = '\0';
}

void EditAlignText(HWND hwndEdit, int nMode)
{
    BOOL bModified = FALSE;
    LONG iSelStart, iSelEnd, iCurPos, iAnchorPos;
    LONG iLine, iLineStart, iLineEnd;
    LONG iMinIndent = BUFSIZE_ALIGN, iMaxLength = 0;

    if (IsRectSel(hwndEdit)) {
        WarnRect(hwndEdit);
        return;
    }

    iSelStart  = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
    iSelEnd    = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);
    iCurPos    = SciL(hwndEdit, SCI_GETCURRENTPOS, 0);
    iAnchorPos = SciL(hwndEdit, SCI_GETANCHOR, 0);

    iLineStart = SciL(hwndEdit, SCI_LINEFROMPOSITION, iSelStart);
    iLineEnd   = SciL(hwndEdit, SCI_LINEFROMPOSITION, iSelEnd);
    if (iSelEnd <= SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineEnd)) {
        if (iLineEnd - iLineStart >= 1)
            iLineEnd--;
    }

    /* Pass 1: the common indent and the longest line decide the target width. */
    for (iLine = iLineStart; iLine <= iLineEnd; iLine++) {
        LONG iLineEndPos    = SciL(hwndEdit, SCI_GETLINEENDPOSITION, iLine);
        LONG iLineIndentPos = SciL(hwndEdit, SCI_GETLINEINDENTPOSITION, iLine);

        if (iLineIndentPos != iLineEndPos) {
            LONG iIndentCol = SciL(hwndEdit, SCI_GETLINEINDENTATION, iLine);
            LONG iEndCol, iTail;
            char ch;

            iTail = iLineEndPos - 1;
            ch = (char)SciL(hwndEdit, SCI_GETCHARAT, iTail);
            while (iTail >= iLineStart && (ch == ' ' || ch == '\t')) {
                iTail--;
                ch = (char)SciL(hwndEdit, SCI_GETCHARAT, iTail);
                iLineEndPos--;
            }
            iEndCol = SciL(hwndEdit, SCI_GETCOLUMN, iLineEndPos);

            if (iIndentCol < iMinIndent) iMinIndent = iIndentCol;
            if (iEndCol    > iMaxLength) iMaxLength = iEndCol;
        }
    }

    if (iMaxLength >= BUFSIZE_ALIGN) {
        WinMessageBox(HWND_DESKTOP, hwndEdit,
                      (PSZ)"A line in the selection is too long to align.",
                      (PSZ)"Align Lines", 0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
        return;
    }

    /* Pass 2: rebuild each line. */
    for (iLine = iLineStart; iLine <= iLineEnd; iLine++) {
        LONG iIndentPos = SciL(hwndEdit, SCI_GETLINEINDENTPOSITION, iLine);
        LONG iEndPos    = SciL(hwndEdit, SCI_GETLINEENDPOSITION, iLine);

        if (iIndentPos == iEndPos && iEndPos > 0) {
            /* Blank-but-for-whitespace: empty it. */
            if (!bModified) {
                SciL(hwndEdit, SCI_BEGINUNDOACTION, 0);
                bModified = TRUE;
            }
            SciL(hwndEdit, SCI_SETTARGETSTART, SciL(hwndEdit, SCI_POSITIONFROMLINE, iLine));
            SciL(hwndEdit, SCI_SETTARGETEND,   iEndPos);
            SciP(hwndEdit, SCI_REPLACETARGET, 0, "");
            continue;
        }

        {
            char  szLine[BUFSIZE_ALIGN * 3];
            char  szNew[BUFSIZE_ALIGN * 3];
            char *pWords[BUFSIZE_ALIGN];
            char *p;
            int   iWords = 0, iWordsLength = 0, i;
            LONG  iPos;

            if (SciL(hwndEdit, SCI_LINELENGTH, iLine) >= (LONG)sizeof(szLine))
                continue;

            if (!bModified) {
                SciL(hwndEdit, SCI_BEGINUNDOACTION, 0);
                bModified = TRUE;
            }

            szLine[0] = '\0';
            SciP(hwndEdit, SCI_GETLINE, iLine, szLine);
            szLine[SciL(hwndEdit, SCI_LINELENGTH, iLine)] = '\0';
            TrimEnds(szLine, "\r\n\t ");

            /* Split into words in place, exactly as the original does. */
            p = szLine;
            while (*p) {
                if (*p != ' ' && *p != '\t') {
                    pWords[iWords++] = p++;
                    iWordsLength++;
                    while (*p && *p != ' ' && *p != '\t') {
                        p++;
                        iWordsLength++;
                    }
                    if (iWords >= (int)(sizeof(pWords) / sizeof(pWords[0])))
                        break;
                } else {
                    *p++ = '\0';
                }
            }

            if (iWords == 0)
                continue;

            if (nMode == ALIGN_JUSTIFY || nMode == ALIGN_JUSTIFY_EX) {
                BOOL bNextLineIsBlank = FALSE;

                if (nMode == ALIGN_JUSTIFY_EX) {
                    if (SciL(hwndEdit, SCI_GETLINECOUNT, 0) <= iLine + 1) {
                        bNextLineIsBlank = TRUE;
                    } else {
                        LONG e = SciL(hwndEdit, SCI_GETLINEENDPOSITION, iLine + 1);
                        LONG n = SciL(hwndEdit, SCI_GETLINEINDENTPOSITION, iLine + 1);
                        if (n == e)
                            bNextLineIsBlank = TRUE;
                    }
                }

                if (iWords > 1 && iWordsLength >= 2 &&
                    ((nMode != ALIGN_JUSTIFY_EX || !bNextLineIsBlank || iLineStart == iLineEnd) ||
                     (bNextLineIsBlank &&
                      iWordsLength > (iMaxLength - iMinIndent) * 0.75))) {
                    /* Full justification: spread the slack across the gaps, the
                     * leftovers going to the rightmost gaps. */
                    int iGaps = iWords - 1;
                    int iSpacesPerGap = (int)((iMaxLength - iMinIndent - iWordsLength) / iGaps);
                    int iExtraSpaces  = (int)((iMaxLength - iMinIndent - iWordsLength) % iGaps);
                    int j;

                    strcpy(szNew, pWords[0]);
                    p = szNew + strlen(szNew);
                    for (i = 1; i < iWords; i++) {
                        for (j = 0; j < iSpacesPerGap; j++)
                            *p++ = ' ';
                        if (i > iGaps - iExtraSpaces)
                            *p++ = ' ';
                        *p = '\0';
                        strcpy(p, pWords[i]);
                        p += strlen(p);
                    }
                } else {
                    /* Last line of a paragraph: single-space it, do not stretch. */
                    strcpy(szNew, pWords[0]);
                    p = szNew + strlen(szNew);
                    for (i = 1; i < iWords; i++) {
                        *p++ = ' ';
                        *p = '\0';
                        strcpy(p, pWords[i]);
                        p += strlen(p);
                    }
                }

                iPos = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLine);
                SciL(hwndEdit, SCI_SETTARGETSTART, iPos);
                iPos = SciL(hwndEdit, SCI_GETLINEENDPOSITION, iLine);
                SciL(hwndEdit, SCI_SETTARGETEND, iPos);
                SciP(hwndEdit, SCI_REPLACETARGET, (LONG)strlen(szNew), szNew);
                SciMsg(hwndEdit, SCI_SETLINEINDENTATION, iLine, (const void *)iMinIndent);
            } else {
                int iExtraSpaces = (int)(iMaxLength - iMinIndent - iWordsLength - iWords + 1);
                int iOddSpaces   = iExtraSpaces % 2;

                p = szNew;
                *p = '\0';
                if (nMode == ALIGN_RIGHT) {
                    for (i = 0; i < iExtraSpaces; i++)
                        *p++ = ' ';
                    *p = '\0';
                }
                if (nMode == ALIGN_CENTER) {
                    for (i = 1; i < iExtraSpaces - iOddSpaces; i += 2)
                        *p++ = ' ';
                    *p = '\0';
                }
                for (i = 0; i < iWords; i++) {
                    strcpy(p, pWords[i]);
                    p += strlen(p);
                    if (i < iWords - 1) {
                        *p++ = ' ';
                        *p = '\0';
                    }
                    if (nMode == ALIGN_CENTER && iWords > 1 && iOddSpaces > 0 &&
                        i + 1 >= iWords / 2) {
                        *p++ = ' ';
                        *p = '\0';
                        iOddSpaces--;
                    }
                }

                if (nMode == ALIGN_RIGHT || nMode == ALIGN_CENTER) {
                    SciMsg(hwndEdit, SCI_SETLINEINDENTATION, iLine, (const void *)iMinIndent);
                    iPos = SciL(hwndEdit, SCI_GETLINEINDENTPOSITION, iLine);
                } else {
                    iPos = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLine);
                }
                SciL(hwndEdit, SCI_SETTARGETSTART, iPos);
                iPos = SciL(hwndEdit, SCI_GETLINEENDPOSITION, iLine);
                SciL(hwndEdit, SCI_SETTARGETEND, iPos);
                SciP(hwndEdit, SCI_REPLACETARGET, (LONG)strlen(szNew), szNew);

                if (nMode == ALIGN_LEFT)
                    SciMsg(hwndEdit, SCI_SETLINEINDENTATION, iLine, (const void *)iMinIndent);
            }
        }
    }

    if (bModified)
        SciL(hwndEdit, SCI_ENDUNDOACTION, 0);

    {
        LONG a = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineStart);
        LONG b = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineEnd + 1);
        if (iCurPos < iAnchorPos) { iCurPos = a; iAnchorPos = b; }
        else                      { iAnchorPos = a; iCurPos = b; }
        SciMsg(hwndEdit, SCI_SETSEL, iAnchorPos, (const void *)iCurPos);
    }
}

/*--------------------------------------------------------------------------
 * EditSortLines
 *------------------------------------------------------------------------*/

typedef struct _sortline {
    char *pszLine;        /* owned */
    char *pszSortEntry;   /* points INTO pszLine - never freed separately */
} SORTLINE;

static int g_iSortFlags;

static int CmpStr(const char *a, const char *b)
{
    return (g_iSortFlags & SORT_NOCASE) ? strcasecmp(a, b) : strcmp(a, b);
}

/* StrCmpLogicalW replacement: compare runs of digits by VALUE, everything else
 * by character, so "file10" sorts after "file9". Windows gets this from the
 * shell (shlwapi); OS/2 has no equivalent at any layer, so it is written out. */
static int CmpLogicalStr(const char *a, const char *b)
{
    for (;;) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            long va = 0, vb = 0;
            while (*a == '0') a++;            /* leading zeros do not add value */
            while (*b == '0') b++;
            while (isdigit((unsigned char)*a)) { va = va * 10 + (*a - '0'); a++; }
            while (isdigit((unsigned char)*b)) { vb = vb * 10 + (*b - '0'); b++; }
            if (va != vb)
                return (va < vb) ? -1 : 1;
        } else {
            char ca = *a, cb = *b;
            if (g_iSortFlags & SORT_NOCASE) {
                ca = (char)tolower((unsigned char)ca);
                cb = (char)tolower((unsigned char)cb);
            }
            if (ca != cb)
                return (unsigned char)ca < (unsigned char)cb ? -1 : 1;
            if (ca == '\0')
                return 0;
            a++; b++;
        }
    }
}

static int CmpEntry(const void *p1, const void *p2)
{
    const SORTLINE *s1 = (const SORTLINE *)p1;
    const SORTLINE *s2 = (const SORTLINE *)p2;
    return (g_iSortFlags & SORT_LOGICAL)
         ? CmpLogicalStr(s1->pszSortEntry, s2->pszSortEntry)
         : CmpStr(s1->pszSortEntry, s2->pszSortEntry);
}

static int CmpEntryRev(const void *p1, const void *p2)
{
    return -CmpEntry(p1, p2);
}

void EditSortLines(HWND hwndEdit, int iSortFlags)
{
    LONG iCurPos, iAnchorPos, iSelStart, iSelEnd;
    LONG iLineStart, iLineEnd, iLine;
    int  iLineCount, i;
    long cchTotal = 0;
    SORTLINE *pLines;
    char *pszResult;
    char  szEOL[3] = "\r\n";
    LONG  cEOLMode;
    LONG  iTabWidth, iSortColumn;
    BOOL  bLastDup = FALSE;

    if (IsRectSel(hwndEdit)) {
        WarnRect(hwndEdit);
        return;
    }

    iCurPos    = SciL(hwndEdit, SCI_GETCURRENTPOS, 0);
    iAnchorPos = SciL(hwndEdit, SCI_GETANCHOR, 0);
    if (iCurPos == iAnchorPos)
        return;

    iSelStart = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
    iSelEnd   = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);

    /* Sorting always works on whole lines - snap the start back to a line start. */
    iLine     = SciL(hwndEdit, SCI_LINEFROMPOSITION, iSelStart);
    iSelStart = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLine);

    iLineStart = SciL(hwndEdit, SCI_LINEFROMPOSITION, iSelStart);
    iLineEnd   = SciL(hwndEdit, SCI_LINEFROMPOSITION, iSelEnd);
    if (iSelEnd <= SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineEnd))
        iLineEnd--;

    iSortColumn = SciL(hwndEdit, SCI_GETCOLUMN, SciL(hwndEdit, SCI_GETCURRENTPOS, 0));

    iLineCount = (int)(iLineEnd - iLineStart + 1);
    if (iLineCount < 2)
        return;

    cEOLMode = SciL(hwndEdit, SCI_GETEOLMODE, 0);
    if (cEOLMode == SC_EOL_CR)      { szEOL[1] = '\0'; }
    else if (cEOLMode == SC_EOL_LF) { szEOL[0] = '\n'; szEOL[1] = '\0'; }

    iTabWidth = SciL(hwndEdit, SCI_GETTABWIDTH, 0);
    if (iTabWidth <= 0)
        iTabWidth = 8;

    pLines = (SORTLINE *)calloc(iLineCount, sizeof(SORTLINE));
    if (!pLines)
        return;

    i = 0;
    for (iLine = iLineStart; iLine <= iLineEnd; iLine++) {
        LONG  cchm = SciL(hwndEdit, SCI_LINELENGTH, iLine);
        char *psz  = (char *)malloc(cchm + 1);
        if (!psz) { pLines[i].pszLine = NULL; i++; continue; }

        SciP(hwndEdit, SCI_GETLINE, iLine, psz);
        psz[cchm] = '\0';
        TrimEnds(psz, "\r\n");
        cchTotal += (long)strlen(psz);

        pLines[i].pszLine      = psz;
        pLines[i].pszSortEntry = psz;

        /* Column sort: skip forward to the column the caret was in, counting
         * tab stops rather than bytes. */
        if (iSortFlags & SORT_COLUMN) {
            LONG col = 0, tabs = iTabWidth;
            char *q = pLines[i].pszSortEntry;
            while (*q) {
                if (*q == '\t') {
                    if (col + tabs <= iSortColumn) {
                        col += tabs;
                        tabs = iTabWidth;
                        q++;
                    } else {
                        break;
                    }
                } else if (col < iSortColumn) {
                    col++;
                    if (--tabs == 0)
                        tabs = iTabWidth;
                    q++;
                } else {
                    break;
                }
            }
            pLines[i].pszSortEntry = q;
        }
        i++;
    }

    g_iSortFlags = iSortFlags;

    if (iSortFlags & SORT_SHUFFLE) {
        /* GetTickCount -> WinGetCurrentTime [os2ref/pm-window-messaging.md]. */
        srand((unsigned)WinGetCurrentTime(WinQueryAnchorBlock(hwndEdit)));
        for (i = iLineCount - 1; i > 0; i--) {
            int j = rand() % i;
            SORTLINE t = pLines[i];
            pLines[i] = pLines[j];
            pLines[j] = t;
        }
    } else if (iSortFlags & SORT_DESCENDING) {
        qsort(pLines, iLineCount, sizeof(SORTLINE), CmpEntryRev);
    } else {
        qsort(pLines, iLineCount, sizeof(SORTLINE), CmpEntry);
    }

    pszResult = (char *)malloc(cchTotal + 2 * iLineCount + 1);
    if (!pszResult) {
        for (i = 0; i < iLineCount; i++) free(pLines[i].pszLine);
        free(pLines);
        return;
    }
    pszResult[0] = '\0';

    for (i = 0; i < iLineCount; i++) {
        BOOL bDropLine = FALSE;

        if (!pLines[i].pszLine)
            continue;
        if (!(iSortFlags & SORT_SHUFFLE) && pLines[i].pszLine[0] == '\0')
            continue;

        if (!(iSortFlags & SORT_SHUFFLE) &&
            (iSortFlags & (SORT_MERGEDUP | SORT_UNIQDUP | SORT_UNIQUNIQ))) {
            if (i < iLineCount - 1 && pLines[i + 1].pszLine &&
                CmpStr(pLines[i].pszLine, pLines[i + 1].pszLine) == 0) {
                bLastDup = TRUE;
                bDropLine = (BOOL)((iSortFlags & (SORT_MERGEDUP | SORT_UNIQDUP)) != 0);
            } else {
                bDropLine = (BOOL)((!bLastDup && (iSortFlags & SORT_UNIQUNIQ)) ||
                                   (bLastDup && (iSortFlags & SORT_UNIQDUP)));
                bLastDup = FALSE;
            }
        }

        if (!bDropLine) {
            strcat(pszResult, pLines[i].pszLine);
            strcat(pszResult, szEOL);
        }
    }

    for (i = 0; i < iLineCount; i++)
        free(pLines[i].pszLine);
    free(pLines);

    if (iAnchorPos > iCurPos) {
        iCurPos    = iSelStart;
        iAnchorPos = iSelStart + (LONG)strlen(pszResult);
    } else {
        iAnchorPos = iSelStart;
        iCurPos    = iSelStart + (LONG)strlen(pszResult);
    }

    SciL(hwndEdit, SCI_BEGINUNDOACTION, 0);
    SciL(hwndEdit, SCI_SETTARGETSTART, SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineStart));
    SciL(hwndEdit, SCI_SETTARGETEND,   SciL(hwndEdit, SCI_POSITIONFROMLINE, iLineEnd + 1));
    SciP(hwndEdit, SCI_REPLACETARGET, (LONG)strlen(pszResult), pszResult);
    SciL(hwndEdit, SCI_ENDUNDOACTION, 0);

    free(pszResult);

    SciMsg(hwndEdit, SCI_SETSEL, iAnchorPos, (const void *)iCurPos);
}

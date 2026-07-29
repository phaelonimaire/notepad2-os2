/* np2find.c - Notepad2's Find / Replace, converted to Presentation Manager.
 *
 * Converted from Notepad2-mod's src/Edit.c (EditFindNext / EditFindPrev /
 * EditReplace / EditReplaceAll / EditReplaceAllInSelection / EditFindReplaceDlgProc).
 *
 * The searching is entirely Scintilla's, unchanged: SCI_FINDTEXT with a
 * Sci_TextToFind, and SCI_SETTARGETSTART/END + SCI_REPLACETARGET(RE) to substitute.
 * Those messages already reach the control - ScintillaPM.cxx forwards anything
 * >= SCI_START to ScintillaBase::WndProc - so nothing in the platform layer changed
 * to make this work.
 *
 * What DID have to change is Win32 dialog plumbing:
 *   - CreateDialogParam(modeless) -> WinLoadDlg + WinShowWindow
 *     [DOC-IBM - os2ref/resources-and-dialogs.md 4]
 *   - WM_INITDIALOG returning TRUE -> WM_INITDLG returning FALSE (inverted!)
 *   - MessageBox -> WinMessageBox
 *   - IsDlgButtonChecked -> WinQueryButtonCheckstate
 *   - CB_ADDSTRING -> LM_INSERTITEM (a PM combo box accepts its list box's LM_*)
 *     [DOC-IBM - os2ref/pm-controls.md 10]
 */
#define INCL_WIN
#define INCL_DOS
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Scintilla.h"
#include "np2.h"
#include "np2find.h"
#include "pmhelpers.h"

/*--------------------------------------------------------------------------
 * Scintilla access
 *------------------------------------------------------------------------*/

static LONG SciMsg(HWND h, unsigned int msg, LONG wp, const void *lp)
{
    return LONGFROMMR(WinSendMsg(h, msg, MPFROMLONG(wp), MPFROMP((PVOID)lp)));
}

#define SciL(h, m, wp)      SciMsg((h), (m), (LONG)(wp), NULL)
#define SciP(h, m, wp, lp)  SciMsg((h), (m), (LONG)(wp), (const void *)(lp))

/*--------------------------------------------------------------------------
 * State
 *------------------------------------------------------------------------*/

static HWND             hwndFindReplaceDlg = NULLHANDLE;
static HWND             hwndFindEdit       = NULLHANDLE;
static EDITFINDREPLACE *lpefrGlobal        = NULL;
static BOOL             bDlgIsReplace      = FALSE;

/* Search / replace history, the MRU lists behind the two combo boxes. */
#define MRU_MAX 16
static char szFindMRU[MRU_MAX][NP2_FINDTEXT_MAX];
static char szReplMRU[MRU_MAX][NP2_FINDTEXT_MAX];
static int  cFindMRU = 0;
static int  cReplMRU = 0;

static void MRUAdd(char (*list)[NP2_FINDTEXT_MAX], int *pCount, const char *psz)
{
    int i, j;
    if (!psz || !psz[0])
        return;
    for (i = 0; i < *pCount; i++) {
        if (strcmp(list[i], psz) == 0) {          /* already present - promote it */
            char tmp[NP2_FINDTEXT_MAX];
            strcpy(tmp, list[i]);
            for (j = i; j > 0; j--)
                strcpy(list[j], list[j - 1]);
            strcpy(list[0], tmp);
            return;
        }
    }
    if (*pCount < MRU_MAX)
        (*pCount)++;
    for (j = *pCount - 1; j > 0; j--)
        strcpy(list[j], list[j - 1]);
    strncpy(list[0], psz, NP2_FINDTEXT_MAX - 1);
    list[0][NP2_FINDTEXT_MAX - 1] = '\0';
}

/*--------------------------------------------------------------------------
 * Reporting
 *
 * These are the honest-failure path: a search that finds nothing must SAY so.
 * A silent no-op reads as "the editor ignored me", which is the wrong diagnosis
 * and the exact failure mode c-guide.md Rule 2.1a warns about.
 *------------------------------------------------------------------------*/

static void FindNotFound(HWND hwndOwner, const char *pszFind)
{
    char szMsg[NP2_FINDTEXT_MAX + 64];
    sprintf(szMsg, "Not found: \"%s\"", pszFind);
    WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szMsg, (PSZ)"Find",
                  0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
}

/* Returns TRUE if the user agreed to wrap. */
static BOOL AskWrap(HWND hwndOwner, BOOL bForward)
{
    return (WinMessageBox(HWND_DESKTOP, hwndOwner,
                (PSZ)(bForward
                      ? "Search reached the end of the document.\nContinue from the beginning?"
                      : "Search reached the beginning of the document.\nContinue from the end?"),
                (PSZ)"Find", 0,
                MB_OKCANCEL | MB_QUERY | MB_MOVEABLE) == MBID_OK);
}

/*--------------------------------------------------------------------------
 * Backslash transformation - Notepad2's UnSlash / UnSlashLowOctal, ported.
 *
 * \u is DELIBERATELY not implemented for values above 255: the Win32 original
 * calls WideCharToMultiByte, and the OS/2 equivalent needs UniUconv against the
 * *editor's* code page, which is one of three independent code-page scopes
 * [os2ref/unicode-conversion.md 9.1]. Rather than guess, \uXXXX above 255 is
 * left as literal text - a visible limitation instead of a silent mistranslation.
 *------------------------------------------------------------------------*/

static int GetHexDigit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static BOOL IsOctalDigit(char ch) { return ch >= '0' && ch <= '7'; }

static void UnSlash(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '\\') {
            s++;
            switch (*s) {
            case 'a': *o = '\a'; break;
            case 'b': *o = '\b'; break;
            case 'f': *o = '\f'; break;
            case 'n': *o = '\n'; break;
            case 'r': *o = '\r'; break;
            case 't': *o = '\t'; break;
            case 'v': *o = '\v'; break;
            case 'x':
            case 'u': {
                BOOL bShort = (*s == 'x');
                int  hex, val = 0, digits = 0;
                int  maxDigits = bShort ? 2 : 4;
                while (digits < maxDigits && (hex = GetHexDigit(*(s + 1))) >= 0) {
                    s++;
                    val = val * 16 + hex;
                    digits++;
                }
                if (digits == 0 || val == 0 || val > 255) {
                    o--;                       /* nothing usable - drop the escape */
                } else {
                    *o = (char)val;
                }
                break;
            }
            default:
                *o = *s;                       /* \\ , \" and anything unknown */
                break;
            }
        } else {
            *o = *s;
        }
        o++;
        if (*s)
            s++;
    }
    *o = '\0';
}

/* \0oo -> the control character, so low control codes can reach the regex engine. */
static void UnSlashLowOctal(char *s)
{
    char *o = s;
    while (*s) {
        if (s[0] == '\\' && s[1] == '0' && IsOctalDigit(s[2]) && IsOctalDigit(s[3])) {
            *o = (char)(8 * (s[2] - '0') + (s[3] - '0'));
            s += 3;
        } else {
            *o = *s;
        }
        o++;
        if (*s)
            s++;
    }
    *o = '\0';
}

static void TransformBackslashes(char *psz, BOOL bRegEx)
{
    if (bRegEx)
        UnSlashLowOctal(psz);
    else
        UnSlash(psz);
}

/* Copy the pattern and apply the transform. Returns FALSE if nothing is left
 * to search for - which is a real condition, not an error to swallow. */
static BOOL PrepareFindText(EDITFINDREPLACE *lpefr, char *szOut, int cchOut)
{
    if (!lpefr->szFind[0])
        return FALSE;
    strncpy(szOut, lpefr->szFind, cchOut - 1);
    szOut[cchOut - 1] = '\0';
    if (lpefr->bTransformBS)
        TransformBackslashes(szOut, (BOOL)((lpefr->fuFlags & SCFIND_REGEXP) != 0));
    return szOut[0] != '\0';
}

/*--------------------------------------------------------------------------
 * Selection - Notepad2's EditSelectEx
 *------------------------------------------------------------------------*/

void EditSelectEx(HWND hwndEdit, LONG iAnchorPos, LONG iCurrentPos)
{
    LONG iNewLine    = SciL(hwndEdit, SCI_LINEFROMPOSITION, iCurrentPos);
    LONG iAnchorLine = SciL(hwndEdit, SCI_LINEFROMPOSITION, iAnchorPos);

    /* Unfold before selecting, not after - SCI_SETSEL on a folded line does not
     * reveal it. (Notepad2 has the same ordering, with the same comment.) */
    SciL(hwndEdit, SCI_ENSUREVISIBLE, iAnchorLine);
    if (iAnchorLine != iNewLine)
        SciL(hwndEdit, SCI_ENSUREVISIBLE, iNewLine);

    SciMsg(hwndEdit, SCI_SETXCARETPOLICY,
           CARET_SLOP | CARET_STRICT | CARET_EVEN, (const void *)50);
    SciMsg(hwndEdit, SCI_SETYCARETPOLICY,
           CARET_SLOP | CARET_STRICT | CARET_EVEN, (const void *)5);
    SciMsg(hwndEdit, SCI_SETSEL, iAnchorPos, (const void *)iCurrentPos);
    SciMsg(hwndEdit, SCI_SETXCARETPOLICY, CARET_SLOP | CARET_EVEN, (const void *)50);
    SciMsg(hwndEdit, SCI_SETYCARETPOLICY, CARET_EVEN, (const void *)0);
}

/*--------------------------------------------------------------------------
 * Find
 *------------------------------------------------------------------------*/

BOOL EditFindNext(HWND hwndEdit, EDITFINDREPLACE *lpefr, BOOL fExtendSelection)
{
    struct Sci_TextToFind ttf;
    LONG iPos, iSelPos, iSelAnchor;
    char szFind2[NP2_FINDTEXT_MAX];
    BOOL bSuppressNotFound = FALSE;
    HWND hwndOwner = (hwndFindReplaceDlg != NULLHANDLE) ? hwndFindReplaceDlg : hwndEdit;

    if (!PrepareFindText(lpefr, szFind2, sizeof(szFind2))) {
        FindNotFound(hwndOwner, lpefr->szFind);
        return FALSE;
    }

    iSelPos    = SciL(hwndEdit, SCI_GETCURRENTPOS, 0);
    iSelAnchor = SciL(hwndEdit, SCI_GETANCHOR, 0);

    memset(&ttf, 0, sizeof(ttf));
    ttf.chrg.cpMin = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);
    ttf.chrg.cpMax = SciL(hwndEdit, SCI_GETLENGTH, 0);
    ttf.lpstrText  = szFind2;

    iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf);

    if (iPos == -1 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap && !fExtendSelection) {
        if (AskWrap(hwndOwner, TRUE)) {
            ttf.chrg.cpMin = 0;
            iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf);
        } else {
            bSuppressNotFound = TRUE;
        }
    }

    if (iPos == -1) {
        if (!bSuppressNotFound)
            FindNotFound(hwndOwner, szFind2);
        return FALSE;
    }

    if (!fExtendSelection)
        EditSelectEx(hwndEdit, ttf.chrgText.cpMin, ttf.chrgText.cpMax);
    else
        EditSelectEx(hwndEdit,
                     (iSelAnchor < iSelPos) ? iSelAnchor : iSelPos,
                     ttf.chrgText.cpMax);
    return TRUE;
}

BOOL EditFindPrev(HWND hwndEdit, EDITFINDREPLACE *lpefr, BOOL fExtendSelection)
{
    struct Sci_TextToFind ttf;
    LONG iPos, iSelPos, iSelAnchor, iLength;
    char szFind2[NP2_FINDTEXT_MAX];
    BOOL bSuppressNotFound = FALSE;
    HWND hwndOwner = (hwndFindReplaceDlg != NULLHANDLE) ? hwndFindReplaceDlg : hwndEdit;

    if (!PrepareFindText(lpefr, szFind2, sizeof(szFind2))) {
        FindNotFound(hwndOwner, lpefr->szFind);
        return FALSE;
    }

    iSelPos    = SciL(hwndEdit, SCI_GETCURRENTPOS, 0);
    iSelAnchor = SciL(hwndEdit, SCI_GETANCHOR, 0);

    /* Backwards search is a forward search over a reversed range: Scintilla
     * returns the LAST match when cpMin > cpMax. */
    memset(&ttf, 0, sizeof(ttf));
    ttf.chrg.cpMin = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
    ttf.chrg.cpMax = 0;
    ttf.lpstrText  = szFind2;

    iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf);

    iLength = SciL(hwndEdit, SCI_GETLENGTH, 0);
    if (iPos == -1 && ttf.chrg.cpMin < iLength && !lpefr->bNoFindWrap && !fExtendSelection) {
        if (AskWrap(hwndOwner, FALSE)) {
            ttf.chrg.cpMin = iLength;
            iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf);
        } else {
            bSuppressNotFound = TRUE;
        }
    }

    if (iPos == -1) {
        if (!bSuppressNotFound)
            FindNotFound(hwndOwner, szFind2);
        return FALSE;
    }

    if (!fExtendSelection)
        EditSelectEx(hwndEdit, ttf.chrgText.cpMin, ttf.chrgText.cpMax);
    else
        EditSelectEx(hwndEdit,
                     (iSelAnchor > iSelPos) ? iSelAnchor : iSelPos,
                     ttf.chrgText.cpMin);
    return TRUE;
}

/*--------------------------------------------------------------------------
 * Replace
 *------------------------------------------------------------------------*/

/* Notepad2's rule, kept: Replace only substitutes when the CURRENT selection is
 * already the match. Otherwise it just selects the next match, so the first
 * click selects and the second replaces. */
BOOL EditReplace(HWND hwndEdit, EDITFINDREPLACE *lpefr)
{
    struct Sci_TextToFind ttf;
    LONG iPos, iSelStart, iSelEnd;
    unsigned int iReplaceMsg =
        (lpefr->fuFlags & SCFIND_REGEXP) ? SCI_REPLACETARGETRE : SCI_REPLACETARGET;
    char szFind2[NP2_FINDTEXT_MAX];
    char szReplace2[NP2_FINDTEXT_MAX];
    BOOL bSuppressNotFound = FALSE;
    HWND hwndOwner = (hwndFindReplaceDlg != NULLHANDLE) ? hwndFindReplaceDlg : hwndEdit;

    if (!PrepareFindText(lpefr, szFind2, sizeof(szFind2))) {
        FindNotFound(hwndOwner, lpefr->szFind);
        return FALSE;
    }

    strncpy(szReplace2, lpefr->szReplace, sizeof(szReplace2) - 1);
    szReplace2[sizeof(szReplace2) - 1] = '\0';
    if (lpefr->bTransformBS)
        TransformBackslashes(szReplace2, (BOOL)((lpefr->fuFlags & SCFIND_REGEXP) != 0));

    iSelStart = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
    iSelEnd   = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);

    memset(&ttf, 0, sizeof(ttf));
    ttf.chrg.cpMin = iSelStart;
    ttf.chrg.cpMax = SciL(hwndEdit, SCI_GETLENGTH, 0);
    ttf.lpstrText  = szFind2;

    iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf);

    if (iPos == -1 && ttf.chrg.cpMin > 0 && !lpefr->bNoFindWrap) {
        if (AskWrap(hwndOwner, TRUE)) {
            ttf.chrg.cpMin = 0;
            iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf);
        } else {
            bSuppressNotFound = TRUE;
        }
    }

    if (iPos == -1) {
        if (!bSuppressNotFound)
            FindNotFound(hwndOwner, szFind2);
        return FALSE;
    }

    /* Selection is not yet on the match - select it and stop. */
    if (iSelStart != ttf.chrgText.cpMin || iSelEnd != ttf.chrgText.cpMax) {
        EditSelectEx(hwndEdit, ttf.chrgText.cpMin, ttf.chrgText.cpMax);
        return FALSE;
    }

    SciL(hwndEdit, SCI_SETTARGETSTART, ttf.chrgText.cpMin);
    SciL(hwndEdit, SCI_SETTARGETEND,   ttf.chrgText.cpMax);
    SciP(hwndEdit, iReplaceMsg, -1, szReplace2);

    /* Advance to the next match so repeated Replace walks the document. */
    ttf.chrg.cpMin = SciL(hwndEdit, SCI_GETTARGETEND, 0);
    ttf.chrg.cpMax = SciL(hwndEdit, SCI_GETLENGTH, 0);
    iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf);
    if (iPos != -1)
        EditSelectEx(hwndEdit, ttf.chrgText.cpMin, ttf.chrgText.cpMax);
    else
        EditSelectEx(hwndEdit,
                     SciL(hwndEdit, SCI_GETTARGETEND, 0),
                     SciL(hwndEdit, SCI_GETTARGETEND, 0));
    return TRUE;
}

BOOL EditReplaceAll(HWND hwndEdit, EDITFINDREPLACE *lpefr, BOOL bInSelection)
{
    struct Sci_TextToFind ttf;
    LONG iPos, iSelStart = 0, iSelEnd = 0;
    int  iCount = 0;
    unsigned int iReplaceMsg =
        (lpefr->fuFlags & SCFIND_REGEXP) ? SCI_REPLACETARGETRE : SCI_REPLACETARGET;
    char szFind2[NP2_FINDTEXT_MAX];
    char szReplace2[NP2_FINDTEXT_MAX];
    char szMsg[128];
    BOOL bRegexStartOfLine;
    BOOL bRegexStartOrEndOfLine;
    HWND hwndOwner = (hwndFindReplaceDlg != NULLHANDLE) ? hwndFindReplaceDlg : hwndEdit;

    if (!PrepareFindText(lpefr, szFind2, sizeof(szFind2))) {
        FindNotFound(hwndOwner, lpefr->szFind);
        return FALSE;
    }

    strncpy(szReplace2, lpefr->szReplace, sizeof(szReplace2) - 1);
    szReplace2[sizeof(szReplace2) - 1] = '\0';
    if (lpefr->bTransformBS)
        TransformBackslashes(szReplace2, (BOOL)((lpefr->fuFlags & SCFIND_REGEXP) != 0));

    bRegexStartOfLine = (szFind2[0] == '^');
    bRegexStartOrEndOfLine =
        (BOOL)((lpefr->fuFlags & SCFIND_REGEXP) &&
               (!strcmp(szFind2, "$") || !strcmp(szFind2, "^") || !strcmp(szFind2, "^$")));

    if (bInSelection) {
        iSelStart = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
        iSelEnd   = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);
        if (iSelStart == iSelEnd) {
            WinMessageBox(HWND_DESKTOP, hwndOwner,
                          (PSZ)"Nothing is selected.", (PSZ)"Replace",
                          0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
            return FALSE;
        }
    }

    /* Hourglass while this runs - a long Replace All on a big file must not look
     * like a hang. WinSetPointer + SPTR_WAIT [os2ref/pm-window-messaging.md]. */
    WinSetPointer(HWND_DESKTOP, WinQuerySysPointer(HWND_DESKTOP, SPTR_WAIT, FALSE));

    memset(&ttf, 0, sizeof(ttf));
    ttf.chrg.cpMin = bInSelection ? iSelStart : 0;
    ttf.chrg.cpMax = bInSelection ? iSelEnd : SciL(hwndEdit, SCI_GETLENGTH, 0);
    ttf.lpstrText  = szFind2;

    while ((iPos = SciP(hwndEdit, SCI_FINDTEXT, lpefr->fuFlags, &ttf)) != -1) {
        LONG iReplacedLen;

        if (iCount == 0 && bRegexStartOrEndOfLine) {
            if (SciL(hwndEdit, SCI_GETLINEENDPOSITION, 0) == 0) {
                ttf.chrgText.cpMin = 0;
                ttf.chrgText.cpMax = 0;
            }
        }

        if (++iCount == 1)
            SciL(hwndEdit, SCI_BEGINUNDOACTION, 0);

        SciL(hwndEdit, SCI_SETTARGETSTART, ttf.chrgText.cpMin);
        SciL(hwndEdit, SCI_SETTARGETEND,   ttf.chrgText.cpMax);
        iReplacedLen = SciP(hwndEdit, iReplaceMsg, -1, szReplace2);

        /* The document just changed length under us; re-derive the bounds.
         * In-selection mode tracks the end by the same delta, or the tail of
         * the selection would drift out of range after the first substitution. */
        if (bInSelection)
            iSelEnd += iReplacedLen - (ttf.chrgText.cpMax - ttf.chrgText.cpMin);

        ttf.chrg.cpMin = ttf.chrgText.cpMin + iReplacedLen;
        ttf.chrg.cpMax = bInSelection ? iSelEnd : SciL(hwndEdit, SCI_GETLENGTH, 0);

        if (ttf.chrg.cpMin >= ttf.chrg.cpMax)
            break;

        /* A zero-length match would loop forever - step past it. */
        if (ttf.chrgText.cpMin == ttf.chrgText.cpMax &&
            !(bRegexStartOrEndOfLine && iReplacedLen > 0))
            ttf.chrg.cpMin = SciL(hwndEdit, SCI_POSITIONAFTER, ttf.chrg.cpMin);

        if (bRegexStartOfLine) {
            LONG iLine = SciL(hwndEdit, SCI_LINEFROMPOSITION, ttf.chrg.cpMin);
            LONG ilPos = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLine);
            if (ilPos == ttf.chrg.cpMin)
                ttf.chrg.cpMin = SciL(hwndEdit, SCI_POSITIONFROMLINE, iLine + 1);
            if (ttf.chrg.cpMin >= ttf.chrg.cpMax)
                break;
        }
    }

    if (iCount)
        SciL(hwndEdit, SCI_ENDUNDOACTION, 0);

    WinSetPointer(HWND_DESKTOP, WinQuerySysPointer(HWND_DESKTOP, SPTR_ARROW, FALSE));

    if (iCount > 0) {
        sprintf(szMsg, "%d occurrence%s replaced.", iCount, (iCount == 1) ? "" : "s");
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szMsg, (PSZ)"Replace",
                      0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
        if (bInSelection)
            EditSelectEx(hwndEdit, iSelStart, iSelEnd);
        return TRUE;
    }

    FindNotFound(hwndOwner, szFind2);
    return FALSE;
}

/*--------------------------------------------------------------------------
 * The dialog
 *------------------------------------------------------------------------*/

/* Read the controls back into the EDITFINDREPLACE. Called before every action,
 * so the dialog is the single source of truth and there is no shadow state to
 * drift. */
static void HarvestDialog(HWND hwnd, EDITFINDREPLACE *lpefr)
{
    lpefr->fuFlags = 0;

    WinQueryDlgItemText(hwnd, IDC_FINDTEXT, NP2_FINDTEXT_MAX, (PSZ)lpefr->szFind);
    if (bDlgIsReplace)
        WinQueryDlgItemText(hwnd, IDC_REPLACETEXT, NP2_FINDTEXT_MAX, (PSZ)lpefr->szReplace);
    else
        lpefr->szReplace[0] = '\0';

    if (WinQueryButtonCheckstate(hwnd, IDC_FINDCASE))   lpefr->fuFlags |= SCFIND_MATCHCASE;
    if (WinQueryButtonCheckstate(hwnd, IDC_FINDWORD))   lpefr->fuFlags |= SCFIND_WHOLEWORD;
    if (WinQueryButtonCheckstate(hwnd, IDC_FINDSTART))  lpefr->fuFlags |= SCFIND_WORDSTART;
    if (WinQueryButtonCheckstate(hwnd, IDC_FINDREGEXP)) lpefr->fuFlags |= SCFIND_REGEXP;

    lpefr->bTransformBS = (BOOL)(WinQueryButtonCheckstate(hwnd, IDC_FINDTRANSFORMBS) != 0);
    lpefr->bNoFindWrap  = (BOOL)(WinQueryButtonCheckstate(hwnd, IDC_NOWRAP) != 0);

    MRUAdd(szFindMRU, &cFindMRU, lpefr->szFind);
    if (bDlgIsReplace)
        MRUAdd(szReplMRU, &cReplMRU, lpefr->szReplace);
}

static void FillCombo(HWND hwnd, ULONG id,
                      char (*list)[NP2_FINDTEXT_MAX], int count, const char *pszCurrent)
{
    int i;
    /* A PM combo box is an entry field plus a list box, so it accepts the list
     * box's own LM_* messages [DOC-IBM - os2ref/pm-controls.md 10]. */
    WinSendDlgItemMsg(hwnd, id, LM_DELETEALL, 0, 0);
    for (i = 0; i < count; i++)
        WinSendDlgItemMsg(hwnd, id, LM_INSERTITEM,
                          MPFROMSHORT(LIT_END), MPFROMP(list[i]));
    WinSetDlgItemText(hwnd, id, (PSZ)(pszCurrent ? pszCurrent : ""));
}

static MRESULT EXPENTRY FindReplaceDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {

    case WM_INITDLG: {
        EDITFINDREPLACE *lpefr = (EDITFINDREPLACE *)PVOIDFROMMP(mp2);

        FillCombo(hwnd, IDC_FINDTEXT, szFindMRU, cFindMRU, lpefr->szFind);
        if (bDlgIsReplace)
            FillCombo(hwnd, IDC_REPLACETEXT, szReplMRU, cReplMRU, lpefr->szReplace);

        WinCheckButton(hwnd, IDC_FINDCASE,   (lpefr->fuFlags & SCFIND_MATCHCASE) ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_FINDWORD,   (lpefr->fuFlags & SCFIND_WHOLEWORD) ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_FINDSTART,  (lpefr->fuFlags & SCFIND_WORDSTART) ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_FINDREGEXP, (lpefr->fuFlags & SCFIND_REGEXP)    ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_FINDTRANSFORMBS, lpefr->bTransformBS ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_NOWRAP,          lpefr->bNoFindWrap  ? 1UL : 0UL);

        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        PMFocusDlgItem(hwnd, IDC_FINDTEXT);

        /* WM_INITDLG's return is INVERTED from Win32's WM_INITDIALOG: FALSE means
         * "I did not set the focus, PM please do it". Returning TRUE here leaves
         * the dialog drawn and keyboard-dead.
         * [DOC-IBM - os2ref/resources-and-dialogs.md 5] */
        return (MRESULT)FALSE;
    }

    case WM_COMMAND: {
        USHORT id = SHORT1FROMMP(mp1);

        if (id == DID_CANCEL) {
            WinDestroyWindow(hwnd);
            hwndFindReplaceDlg = NULLHANDLE;
            return (MRESULT)0;
        }

        if (lpefrGlobal == NULL || hwndFindEdit == NULLHANDLE)
            return (MRESULT)0;

        HarvestDialog(hwnd, lpefrGlobal);

        switch (id) {
        case DID_OK:            /* Find Next is the default button */
            EditFindNext(hwndFindEdit, lpefrGlobal, FALSE);
            break;
        case IDC_FINDPREVBTN:
            EditFindPrev(hwndFindEdit, lpefrGlobal, FALSE);
            break;
        case IDC_REPLACEBTN:
            EditReplace(hwndFindEdit, lpefrGlobal);
            break;
        case IDC_REPLACEALL:
            EditReplaceAll(hwndFindEdit, lpefrGlobal, FALSE);
            break;
        case IDC_REPLACEINSEL:
            EditReplaceAll(hwndFindEdit, lpefrGlobal, TRUE);
            break;
        default:
            return (MRESULT)0;
        }

        /* Refresh the MRU dropdown without disturbing what the user typed. */
        FillCombo(hwnd, IDC_FINDTEXT, szFindMRU, cFindMRU, lpefrGlobal->szFind);
        if (bDlgIsReplace)
            FillCombo(hwnd, IDC_REPLACETEXT, szReplMRU, cReplMRU, lpefrGlobal->szReplace);
        return (MRESULT)0;
    }

    case WM_CLOSE:
        WinDestroyWindow(hwnd);
        hwndFindReplaceDlg = NULLHANDLE;
        return (MRESULT)0;

    case WM_DESTROY:
        hwndFindReplaceDlg = NULLHANDLE;
        break;
    }

    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

HWND EditFindReplaceHwnd(void)
{
    return hwndFindReplaceDlg;
}

void EditFindReplaceDlg(HWND hwndOwner, HWND hwndEdit,
                        EDITFINDREPLACE *lpefr, BOOL bReplace)
{
    /* Seed the pattern from the current selection, as Notepad2 does - the common
     * case is "select a word, press Ctrl+F". */
    LONG iSelStart = SciL(hwndEdit, SCI_GETSELECTIONSTART, 0);
    LONG iSelEnd   = SciL(hwndEdit, SCI_GETSELECTIONEND, 0);
    if (iSelEnd > iSelStart && (iSelEnd - iSelStart) < NP2_FINDTEXT_MAX - 1) {
        struct Sci_TextRange tr;
        char szSel[NP2_FINDTEXT_MAX];
        tr.chrg.cpMin = iSelStart;
        tr.chrg.cpMax = iSelEnd;
        tr.lpstrText  = szSel;
        SciP(hwndEdit, SCI_GETTEXTRANGE, 0, &tr);
        if (!strchr(szSel, '\r') && !strchr(szSel, '\n'))
            strcpy(lpefr->szFind, szSel);
    }

    /* Switching between Find and Replace destroys and rebuilds, rather than
     * hiding: WinDismissDlg only HIDES a WinLoadDlg dialog, so reusing the handle
     * would leak a window per toggle [DOC-IBM - resources-and-dialogs.md 4]. */
    if (hwndFindReplaceDlg != NULLHANDLE) {
        WinDestroyWindow(hwndFindReplaceDlg);
        hwndFindReplaceDlg = NULLHANDLE;
    }

    hwndFindEdit  = hwndEdit;
    lpefrGlobal   = lpefr;
    bDlgIsReplace = bReplace;

    /* Modeless: WinLoadDlg creates and returns; the application's own message
     * loop drives it. (WinDlgBox would run it modally and block the editor.)
     * The dialog is created INVISIBLE unless the template says WS_VISIBLE, so
     * the WinShowWindow is required, not decorative. */
    hwndFindReplaceDlg = WinLoadDlg(HWND_DESKTOP, hwndOwner, FindReplaceDlgProc,
                                    NULLHANDLE,
                                    bReplace ? IDD_REPLACE : IDD_FIND,
                                    lpefr);
    if (hwndFindReplaceDlg == NULLHANDLE) {
        /* Say what actually failed. A dialog that silently does not appear is
         * indistinguishable from a dead menu item. */
        char szMsg[160];
        sprintf(szMsg, "WinLoadDlg failed for dialog %u - WinGetLastError = 0x%04lX",
                (unsigned)(bReplace ? IDD_REPLACE : IDD_FIND),
                (unsigned long)WinGetLastError(WinQueryAnchorBlock(hwndOwner)));
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szMsg, (PSZ)"Find",
                      0, MB_OK | MB_ERROR | MB_MOVEABLE);
        return;
    }
    WinShowWindow(hwndFindReplaceDlg, TRUE);
}

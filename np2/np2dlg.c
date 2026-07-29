/* np2dlg.c - Notepad2's dialogs, converted from src/Dialogs.c and src/Edit.c.
 *
 * Converted here: Goto (IDD_LINENUM), Tab Settings, Long Lines, Word Wrap
 * Settings, Modify Lines, Enclose Selection, Insert Tag, Align Lines, Sort Lines,
 * About.
 *
 * The mapping is the one in the toolkit's recipes/porting-a-windows-app.md, and by
 * this point it really is mechanical. What is NOT mechanical, and cost the time:
 *
 *   - Coordinates are laid out fresh. PM dialog units have a bottom-left origin,
 *     so translating the Win32 y values arithmetically produces an upside-down
 *     dialog that still compiles and still loads.
 *   - "~" is stripped from every LTEXT: a PM static has no mnemonic and would draw
 *     the tilde literally [DOC-IBM pm3.txt "Static Control Styles"].
 *   - WM_INITDLG returns FALSE (inverted from Win32's WM_INITDIALOG).
 *   - Combo boxes are CONTROL + WC_COMBOBOX, and take the list box's LM_* messages.
 *   - EnableWindow -> WinEnableWindow(WinWindowFromID(...)), since PM has no
 *     GetDlgItem-by-id shortcut on the enable call.
 *
 * One behavioural difference from the Win32 build, and it is an improvement:
 * Notepad2 GREYS OUT its "logical number comparison" checkbox when it cannot find
 * StrCmpLogicalW in shlwapi. This port implements that comparison itself
 * (np2edit.c), so the option is always available.
 */
#define INCL_WIN
#define INCL_DOS
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Scintilla.h"
#include "np2.h"
#include "np2dlg.h"
#include "np2edit.h"
#include "pmhelpers.h"

static LONG SciMsg(HWND h, unsigned int msg, LONG wp, const void *lp)
{
    return LONGFROMMR(WinSendMsg(h, msg, MPFROMLONG(wp), MPFROMP((PVOID)lp)));
}
#define SciL(h, m, wp) SciMsg((h), (m), (LONG)(wp), NULL)

static void EnableItem(HWND hwndDlg, ULONG id, BOOL fEnable)
{
    HWND h = WinWindowFromID(hwndDlg, id);
    if (h != NULLHANDLE)
        WinEnableWindow(h, fEnable);
}

static BOOL Checked(HWND hwndDlg, ULONG id)
{
    return (BOOL)(WinQueryButtonCheckstate(hwndDlg, id) != 0);
}

/*--------------------------------------------------------------------------
 * Settings
 *------------------------------------------------------------------------*/

void SettingsDefaults(NP2SETTINGS *s)
{
    memset(s, 0, sizeof(*s));
    s->iTabWidth           = 4;
    s->iIndentWidth        = 4;
    s->bTabsAsSpaces       = FALSE;
    s->bTabIndents         = TRUE;
    s->bBackspaceUnindents = TRUE;
    s->bMarkLongLines      = TRUE;
    s->iLongLinesLimit     = 80;
    s->bLongLineBackground = FALSE;
    s->fWordWrap           = FALSE;
    s->iWordWrapMode       = 0;
    s->iWordWrapIndent     = 0;
    s->bShowWordWrapSymbols = FALSE;
    s->iWordWrapSymbols    = 22;
    s->bLineNumbers        = TRUE;
}

/* The single place that pushes settings into Scintilla. Ported straight from
 * Notepad2.c's UpdateSettings block, including the wrap-indent cascade. */
void ApplySettings(HWND hwndEdit, const NP2SETTINGS *s)
{
    SciL(hwndEdit, SCI_SETTABWIDTH,    s->iTabWidth);
    SciL(hwndEdit, SCI_SETINDENT,      s->iIndentWidth);
    SciL(hwndEdit, SCI_SETUSETABS,     !s->bTabsAsSpaces);
    SciL(hwndEdit, SCI_SETTABINDENTS,  s->bTabIndents);
    SciL(hwndEdit, SCI_SETBACKSPACEUNINDENTS, s->bBackspaceUnindents);

    SciL(hwndEdit, SCI_SETWRAPMODE,
         !s->fWordWrap ? SC_WRAP_NONE
                       : ((s->iWordWrapMode == 0) ? SC_WRAP_WORD : SC_WRAP_CHAR));

    if (s->iWordWrapIndent == 5) {
        SciL(hwndEdit, SCI_SETWRAPINDENTMODE, SC_WRAPINDENT_SAME);
    } else if (s->iWordWrapIndent == 6) {
        SciL(hwndEdit, SCI_SETWRAPINDENTMODE, SC_WRAPINDENT_INDENT);
    } else {
        LONG i = 0;
        switch (s->iWordWrapIndent) {
        case 1: i = 1; break;
        case 2: i = 2; break;
        case 3: i = s->iIndentWidth ? s->iIndentWidth     : s->iTabWidth;     break;
        case 4: i = s->iIndentWidth ? 2 * s->iIndentWidth : 2 * s->iTabWidth; break;
        }
        SciL(hwndEdit, SCI_SETWRAPSTARTINDENT, i);
        SciL(hwndEdit, SCI_SETWRAPINDENTMODE,  SC_WRAPINDENT_FIXED);
    }

    if (s->bShowWordWrapSymbols) {
        LONG flags = 0, loc = 0;
        SHORT sym = s->iWordWrapSymbols ? s->iWordWrapSymbols : 22;
        switch (sym % 10) {
        case 1: flags |= SC_WRAPVISUALFLAG_END; loc |= SC_WRAPVISUALFLAGLOC_END_BY_TEXT; break;
        case 2: flags |= SC_WRAPVISUALFLAG_END; break;
        }
        switch (((sym % 100) - (sym % 10)) / 10) {
        case 1: flags |= SC_WRAPVISUALFLAG_START; loc |= SC_WRAPVISUALFLAGLOC_START_BY_TEXT; break;
        case 2: flags |= SC_WRAPVISUALFLAG_START; break;
        }
        SciL(hwndEdit, SCI_SETWRAPVISUALFLAGSLOCATION, loc);
        SciL(hwndEdit, SCI_SETWRAPVISUALFLAGS,         flags);
    } else {
        SciL(hwndEdit, SCI_SETWRAPVISUALFLAGS, 0);
    }

    if (s->bMarkLongLines) {
        SciL(hwndEdit, SCI_SETEDGEMODE,
             s->bLongLineBackground ? EDGE_BACKGROUND : EDGE_LINE);
        SciL(hwndEdit, SCI_SETEDGECOLUMN, s->iLongLinesLimit);
    } else {
        SciL(hwndEdit, SCI_SETEDGEMODE, EDGE_NONE);
    }

    SciMsg(hwndEdit, SCI_SETMARGINWIDTHN, 0,
           (const void *)(s->bLineNumbers ? 44 : 0));
}

/*--------------------------------------------------------------------------
 * Goto line / column
 *------------------------------------------------------------------------*/

static HWND hwndEditForGoto = NULLHANDLE;

static MRESULT EXPENTRY GotoDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
    case WM_INITDLG: {
        LONG iCurLine = SciL(hwndEditForGoto, SCI_LINEFROMPOSITION,
                             SciL(hwndEditForGoto, SCI_GETCURRENTPOS, 0)) + 1;
        WinSetDlgItemShort(hwnd, IDC_LINENUM, (USHORT)iCurLine, FALSE);
        WinSendDlgItemMsg(hwnd, IDC_LINENUM, EM_SETTEXTLIMIT, MPFROMSHORT(15), 0);
        WinSendDlgItemMsg(hwnd, IDC_COLNUM,  EM_SETTEXTLIMIT, MPFROMSHORT(15), 0);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;
    }

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK: {
            SHORT sLine = 0, sCol = 1;
            LONG  iMaxLine = SciL(hwndEditForGoto, SCI_GETLINECOUNT, 0);
            BOOL  fLineOK, fColOK = TRUE;

            fLineOK = WinQueryDlgItemShort(hwnd, IDC_LINENUM, &sLine, FALSE);
            /* An empty column field means column 1, not an error. */
            if (WinQueryDlgItemTextLength(hwnd, IDC_COLNUM) > 0)
                fColOK = WinQueryDlgItemShort(hwnd, IDC_COLNUM, &sCol, FALSE);
            else
                sCol = 1;

            if (!fLineOK || !fColOK) {
                PMFocusDlgItem(hwnd, !fLineOK ? IDC_LINENUM : IDC_COLNUM);
                return (MRESULT)0;
            }
            if (sLine > 0 && sLine <= iMaxLine && sCol > 0) {
                EditJumpTo(hwndEditForGoto, sLine, sCol);
                WinDismissDlg(hwnd, DID_OK);
            } else {
                PMFocusDlgItem(hwnd,
                    !(sLine > 0 && sLine <= iMaxLine) ? IDC_LINENUM : IDC_COLNUM);
            }
            return (MRESULT)0;
        }
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL EditGotoLineDlg(HWND hwndOwner, HWND hwndEdit)
{
    hwndEditForGoto = hwndEdit;
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, GotoDlgProc, NULLHANDLE,
                            IDD_GOTOLINE, NULL) == DID_OK);
}

/*--------------------------------------------------------------------------
 * Tab settings
 *------------------------------------------------------------------------*/

static MRESULT EXPENTRY TabSettingsDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static NP2SETTINGS *ps;

    switch (msg) {
    case WM_INITDLG:
        ps = (NP2SETTINGS *)PVOIDFROMMP(mp2);
        WinSetDlgItemShort(hwnd, IDC_TABWIDTH,    (USHORT)ps->iTabWidth, FALSE);
        WinSetDlgItemShort(hwnd, IDC_INDENTWIDTH, (USHORT)ps->iIndentWidth, FALSE);
        WinSendDlgItemMsg(hwnd, IDC_TABWIDTH,    EM_SETTEXTLIMIT, MPFROMSHORT(15), 0);
        WinSendDlgItemMsg(hwnd, IDC_INDENTWIDTH, EM_SETTEXTLIMIT, MPFROMSHORT(15), 0);
        WinCheckButton(hwnd, IDC_TABSASSPACES, ps->bTabsAsSpaces       ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_TABINDENTS,   ps->bTabIndents         ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_BSUNINDENTS,  ps->bBackspaceUnindents ? 1UL : 0UL);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK: {
            SHORT sTab = 0, sIndent = 0;
            if (!WinQueryDlgItemShort(hwnd, IDC_TABWIDTH, &sTab, FALSE) || sTab < 1) {
                PMFocusDlgItem(hwnd, IDC_TABWIDTH);
                return (MRESULT)0;
            }
            if (!WinQueryDlgItemShort(hwnd, IDC_INDENTWIDTH, &sIndent, FALSE) || sIndent < 0) {
                PMFocusDlgItem(hwnd, IDC_INDENTWIDTH);
                return (MRESULT)0;
            }
            ps->iTabWidth           = sTab;
            ps->iIndentWidth        = sIndent;
            ps->bTabsAsSpaces       = Checked(hwnd, IDC_TABSASSPACES);
            ps->bTabIndents         = Checked(hwnd, IDC_TABINDENTS);
            ps->bBackspaceUnindents = Checked(hwnd, IDC_BSUNINDENTS);
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;
        }
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL EditTabSettingsDlg(HWND hwndOwner, NP2SETTINGS *s)
{
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, TabSettingsDlgProc, NULLHANDLE,
                            IDD_TABSETTINGS, s) == DID_OK);
}

/*--------------------------------------------------------------------------
 * Long lines
 *------------------------------------------------------------------------*/

static MRESULT EXPENTRY LongLinesDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static NP2SETTINGS *ps;

    switch (msg) {
    case WM_INITDLG:
        ps = (NP2SETTINGS *)PVOIDFROMMP(mp2);
        WinSetDlgItemShort(hwnd, IDC_LONGLIMIT, (USHORT)ps->iLongLinesLimit, FALSE);
        WinSendDlgItemMsg(hwnd, IDC_LONGLIMIT, EM_SETTEXTLIMIT, MPFROMSHORT(15), 0);
        /* PM has no CheckRadioButton over an id range - set each button. */
        PMCheckRadioButton(hwnd, IDC_LONGEDGELINE, IDC_LONGEDGEBACK,
                           ps->bLongLineBackground ? IDC_LONGEDGEBACK : IDC_LONGEDGELINE);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK: {
            SHORT s = 0;
            if (!WinQueryDlgItemShort(hwnd, IDC_LONGLIMIT, &s, FALSE) || s < 1) {
                PMFocusDlgItem(hwnd, IDC_LONGLIMIT);
                return (MRESULT)0;
            }
            ps->iLongLinesLimit     = s;
            ps->bLongLineBackground = Checked(hwnd, IDC_LONGEDGEBACK);
            ps->bMarkLongLines      = TRUE;
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;
        }
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL EditLongLinesDlg(HWND hwndOwner, NP2SETTINGS *s)
{
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, LongLinesDlgProc, NULLHANDLE,
                            IDD_LONGLINES, s) == DID_OK);
}

/*--------------------------------------------------------------------------
 * Word wrap settings - four combo boxes
 *
 * The Win32 template hides the item lists inside invisible LTEXT controls with
 * "|" separators and splits them at run time. That trick is not carried over:
 * the strings live in this file, where they can be read.
 *------------------------------------------------------------------------*/

static const char *aWrapIndent[] = {
    "No wrap indent",
    "Wrap indent by 1 character",
    "Wrap indent by 2 characters",
    "Wrap indent by 1 level",
    "Wrap indent by 2 levels",
    "Wrap indent as first subline",
    "Wrap indent 1 level more than first subline",
    NULL
};
static const char *aWrapBefore[] = {
    "No visual indicators before wrap",
    "Show visual indicators before wrap (near text)",
    "Show visual indicators before wrap (near borders)",
    NULL
};
static const char *aWrapAfter[] = {
    "No visual indicators after wrap",
    "Show visual indicators after wrap (near text)",
    "Show visual indicators after wrap (near borders)",
    NULL
};
static const char *aWrapMode[] = {
    "Wrap text between words",
    "Wrap text between any glyphs",
    NULL
};

static void FillList(HWND hwnd, ULONG id, const char **items, SHORT sSelect)
{
    int i;
    WinSendDlgItemMsg(hwnd, id, LM_DELETEALL, 0, 0);
    for (i = 0; items[i]; i++)
        WinSendDlgItemMsg(hwnd, id, LM_INSERTITEM,
                          MPFROMSHORT(LIT_END), MPFROMP((PVOID)items[i]));
    WinSendDlgItemMsg(hwnd, id, LM_SELECTITEM, MPFROMSHORT(sSelect), MPFROMSHORT(TRUE));
}

static SHORT QueryList(HWND hwnd, ULONG id)
{
    SHORT s = (SHORT)LONGFROMMR(WinSendDlgItemMsg(hwnd, id, LM_QUERYSELECTION,
                                                  MPFROMSHORT(LIT_FIRST), 0));
    return (s == LIT_NONE) ? 0 : s;
}

static MRESULT EXPENTRY WordWrapDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static NP2SETTINGS *ps;

    switch (msg) {
    case WM_INITDLG: {
        SHORT sym;
        ps = (NP2SETTINGS *)PVOIDFROMMP(mp2);
        sym = ps->iWordWrapSymbols ? ps->iWordWrapSymbols : 22;

        FillList(hwnd, IDC_WWINDENT, aWrapIndent, ps->iWordWrapIndent);
        FillList(hwnd, IDC_WWBEFORE, aWrapBefore, (SHORT)(sym % 10));
        FillList(hwnd, IDC_WWAFTER,  aWrapAfter,  (SHORT)(((sym % 100) - (sym % 10)) / 10));
        FillList(hwnd, IDC_WWMODE,   aWrapMode,   ps->iWordWrapMode);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;
    }

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK: {
            SHORT sBefore = QueryList(hwnd, IDC_WWBEFORE);
            SHORT sAfter  = QueryList(hwnd, IDC_WWAFTER);
            ps->iWordWrapIndent = QueryList(hwnd, IDC_WWINDENT);
            ps->iWordWrapMode   = QueryList(hwnd, IDC_WWMODE);
            /* Repacked exactly as Notepad2 stores it - tens are the after-wrap
             * indicator, units the before-wrap one. */
            ps->iWordWrapSymbols     = (SHORT)(sAfter * 10 + sBefore);
            ps->bShowWordWrapSymbols = (BOOL)(sBefore != 0 || sAfter != 0);
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;
        }
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL EditWordWrapDlg(HWND hwndOwner, NP2SETTINGS *s)
{
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, WordWrapDlgProc, NULLHANDLE,
                            IDD_WORDWRAP, s) == DID_OK);
}

/*--------------------------------------------------------------------------
 * The two-entry-field dialogs: Modify Lines, Enclose Selection, Insert Tag
 *------------------------------------------------------------------------*/

typedef struct _twostr {
    char *psz1;
    char *psz2;
    int   cch;
    BOOL  bAutoCloseTag;    /* Insert Tag builds the closing tag as you type */
} TWOSTR;

/* Insert Tag's one piece of real behaviour: derive "</p>" from "<p class=x>".
 * Ported from EditInsertTagDlgProc's IDC_MODIFY handler. */
static void BuildCloseTag(const char *pszOpen, char *pszClose, int cch)
{
    const char *p = pszOpen;
    char *out = pszClose;
    int   n = 0;

    pszClose[0] = '\0';
    if (*p != '<')
        return;
    p++;
    if (*p == '/' || *p == '?' || *p == '!')     /* closing/PI/decl - no pair */
        return;

    if (n + 2 < cch) { *out++ = '<'; *out++ = '/'; n += 2; }
    while (*p && *p != '>' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (n + 1 >= cch) break;
        *out++ = *p++;
        n++;
    }
    /* A self-closing tag has no pair. */
    if (n == 2 || (*p == '\0')) { pszClose[0] = '\0'; return; }
    if (n + 1 < cch) { *out++ = '>'; n++; }
    *out = '\0';
}

static MRESULT EXPENTRY TwoStrDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static TWOSTR *pts;

    switch (msg) {
    case WM_INITDLG:
        pts = (TWOSTR *)PVOIDFROMMP(mp2);
        WinSendDlgItemMsg(hwnd, IDC_STR1, EM_SETTEXTLIMIT, MPFROMSHORT(255), 0);
        WinSendDlgItemMsg(hwnd, IDC_STR2, EM_SETTEXTLIMIT, MPFROMSHORT(255), 0);
        WinSetDlgItemText(hwnd, IDC_STR1, (PSZ)pts->psz1);
        WinSetDlgItemText(hwnd, IDC_STR2, (PSZ)pts->psz2);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        PMFocusDlgItem(hwnd, IDC_STR1);
        return (MRESULT)FALSE;

    /* A PM control reports to its owner through WM_CONTROL, not WM_COMMAND -
     * this is where a ported EN_CHANGE case has to move to. */
    case WM_CONTROL:
        if (pts && pts->bAutoCloseTag &&
            SHORT1FROMMP(mp1) == IDC_STR1 && SHORT2FROMMP(mp1) == EN_CHANGE) {
            char szOpen[256], szClose[256];
            WinQueryDlgItemText(hwnd, IDC_STR1, sizeof(szOpen), (PSZ)szOpen);
            BuildCloseTag(szOpen, szClose, sizeof(szClose));
            WinSetDlgItemText(hwnd, IDC_STR2, (PSZ)szClose);
        }
        return (MRESULT)0;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK:
            WinQueryDlgItemText(hwnd, IDC_STR1, pts->cch, (PSZ)pts->psz1);
            WinQueryDlgItemText(hwnd, IDC_STR2, pts->cch, (PSZ)pts->psz2);
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

static BOOL RunTwoStr(HWND hwndOwner, ULONG idDlg,
                      char *psz1, char *psz2, int cch, BOOL bAutoCloseTag)
{
    TWOSTR ts;
    ts.psz1 = psz1;
    ts.psz2 = psz2;
    ts.cch  = cch;
    ts.bAutoCloseTag = bAutoCloseTag;
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, TwoStrDlgProc, NULLHANDLE,
                            idDlg, &ts) == DID_OK);
}

BOOL EditModifyLinesDlg(HWND hwndOwner, char *pszPrefix, char *pszAppend, int cch)
{
    return RunTwoStr(hwndOwner, IDD_MODIFYLINES, pszPrefix, pszAppend, cch, FALSE);
}

BOOL EditEncloseSelectionDlg(HWND hwndOwner, char *pszOpen, char *pszClose, int cch)
{
    return RunTwoStr(hwndOwner, IDD_ENCLOSESEL, pszOpen, pszClose, cch, FALSE);
}

BOOL EditInsertTagDlg(HWND hwndOwner, char *pszOpen, char *pszClose, int cch)
{
    return RunTwoStr(hwndOwner, IDD_INSERTTAG, pszOpen, pszClose, cch, TRUE);
}

/*--------------------------------------------------------------------------
 * Align lines
 *------------------------------------------------------------------------*/

static MRESULT EXPENTRY AlignDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static int *piMode;

    switch (msg) {
    case WM_INITDLG:
        piMode = (int *)PVOIDFROMMP(mp2);
        PMCheckRadioButton(hwnd, IDC_ALIGNLEFT, IDC_ALIGNJUSTIFYEX,
                           IDC_ALIGNLEFT + *piMode);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK: {
            ULONG id;
            for (id = IDC_ALIGNLEFT; id <= IDC_ALIGNJUSTIFYEX; id++) {
                if (Checked(hwnd, id)) {
                    *piMode = (int)(id - IDC_ALIGNLEFT);
                    break;
                }
            }
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;
        }
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL EditAlignDlg(HWND hwndOwner, int *piAlignMode)
{
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, AlignDlgProc, NULLHANDLE,
                            IDD_ALIGN, piAlignMode) == DID_OK);
}

/*--------------------------------------------------------------------------
 * Sort lines
 *------------------------------------------------------------------------*/

static void SortSyncEnable(HWND hwnd)
{
    BOOL bShuffle = Checked(hwnd, IDC_SORTSHUFFLE);
    EnableItem(hwnd, IDC_SORTMERGEDUP, (BOOL)(!bShuffle && !Checked(hwnd, IDC_SORTUNIQDUP)));
    EnableItem(hwnd, IDC_SORTUNIQDUP,  !bShuffle);
    EnableItem(hwnd, IDC_SORTUNIQUNIQ, !bShuffle);
    EnableItem(hwnd, IDC_SORTNOCASE,   !bShuffle);
    EnableItem(hwnd, IDC_SORTLOGICAL,  !bShuffle);
}

static MRESULT EXPENTRY SortDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static int *piFlags;

    switch (msg) {
    case WM_INITDLG:
        piFlags = (int *)PVOIDFROMMP(mp2);

        PMCheckRadioButton(hwnd, IDC_SORTASC, IDC_SORTSHUFFLE,
            (*piFlags & SORT_DESCENDING) ? IDC_SORTDESC :
            (*piFlags & SORT_SHUFFLE)    ? IDC_SORTSHUFFLE : IDC_SORTASC);

        WinCheckButton(hwnd, IDC_SORTMERGEDUP, (*piFlags & SORT_MERGEDUP) ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_SORTUNIQDUP,  (*piFlags & SORT_UNIQDUP)  ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_SORTUNIQUNIQ, (*piFlags & SORT_UNIQUNIQ) ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_SORTNOCASE,   (*piFlags & SORT_NOCASE)   ? 1UL : 0UL);
        WinCheckButton(hwnd, IDC_SORTLOGICAL,  (*piFlags & SORT_LOGICAL)  ? 1UL : 0UL);

        /* Notepad2 greys this out when shlwapi has no StrCmpLogicalW. This port
         * implements the comparison itself, so it stays enabled. */

        /* Column sort needs a rectangular selection, which np2edit.c does not
         * handle yet - disabled rather than offered and ignored. */
        WinCheckButton(hwnd, IDC_SORTCOLUMN, 0UL);
        EnableItem(hwnd, IDC_SORTCOLUMN, FALSE);

        SortSyncEnable(hwnd);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK:
            *piFlags = 0;
            if (Checked(hwnd, IDC_SORTDESC))      *piFlags |= SORT_DESCENDING;
            if (Checked(hwnd, IDC_SORTSHUFFLE))   *piFlags |= SORT_SHUFFLE;
            if (Checked(hwnd, IDC_SORTMERGEDUP))  *piFlags |= SORT_MERGEDUP;
            if (Checked(hwnd, IDC_SORTUNIQDUP))   *piFlags |= SORT_UNIQDUP;
            if (Checked(hwnd, IDC_SORTUNIQUNIQ))  *piFlags |= SORT_UNIQUNIQ;
            if (Checked(hwnd, IDC_SORTNOCASE))    *piFlags |= SORT_NOCASE;
            if (Checked(hwnd, IDC_SORTLOGICAL))   *piFlags |= SORT_LOGICAL;
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;

        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;

        case IDC_SORTASC:
        case IDC_SORTDESC:
        case IDC_SORTSHUFFLE:
        case IDC_SORTUNIQDUP:
            SortSyncEnable(hwnd);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL EditSortDlg(HWND hwndOwner, HWND hwndEdit, int *piSortFlags)
{
    (void)hwndEdit;
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, SortDlgProc, NULLHANDLE,
                            IDD_SORT, piSortFlags) == DID_OK);
}

/*--------------------------------------------------------------------------
 * About
 *------------------------------------------------------------------------*/

static MRESULT EXPENTRY AboutDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
    case WM_INITDLG:
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;

    case WM_COMMAND:
        if (SHORT1FROMMP(mp1) == DID_OK || SHORT1FROMMP(mp1) == DID_CANCEL) {
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

void EditAboutDlg(HWND hwndOwner)
{
    WinDlgBox(HWND_DESKTOP, hwndOwner, AboutDlgProc, NULLHANDLE, IDD_ABOUT, NULL);
}

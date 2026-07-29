/* np2.c - a Notepad2-shaped PM frame hosting the ported Scintilla control.
 *
 * Architecture mirrors Notepad2's own: a frame window, a client window that owns the
 * layout, and the editor as a child of that client. Menu WM_COMMANDs arrive at the
 * client and are dispatched to the editor as Scintilla API messages - exactly the
 * Win32 idiom with WinSendMsg in place of SendMessage.
 */
#define INCL_WIN
#define INCL_GPI
#define INCL_DOS
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#define INCL_WINSTDFILE      /* WinFileDlg / FILEDLG - os2ref/resources-and-dialogs.md 10 */
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Scintilla.h"
#include "np2.h"
#include "np2find.h"
#include "np2edit.h"
#include "np2dlg.h"
#include "np2cmd.h"
#include "np2style.h"
#include "pmhelpers.h"

extern "C" void Scintilla_RegisterClasses(void *hab);

static HWND  hwndSci = NULLHANDLE;
static BOOL  bWordWrap    = FALSE;
static BOOL  bLineNumbers = TRUE;

/* Everything the Settings dialogs edit. ApplySettings is the only writer to
 * Scintilla for these, so there is one place to look when a setting does not
 * take effect. */
static NP2SETTINGS settings;

/* Search / Lines dialog inputs persist between invocations, as they do in
 * Notepad2 - reopening Modify Lines should show what you typed last time. */
static char szPrefix[256]  = "";
static char szAppend[256]  = "";
static char szEncOpen[256] = "";
static char szEncClose[256] = "";
static char szTagOpen[256] = "";
static char szTagClose[256] = "";
static int  iAlignMode = ALIGN_LEFT;
static int  iSortFlags = SORT_ASCENDING;

/* View toggles. Each is reflected into the menu by SyncMenu, so the check marks
 * and the editor never disagree. */
static BOOL bLongLineMarker = TRUE;
static BOOL bIndentGuides   = FALSE;
static BOOL bShowWhitespace = FALSE;
static BOOL bShowEOLs       = FALSE;
static BOOL bHiliteCurLine  = FALSE;
static BOOL bSelMargin      = FALSE;
static BOOL bFolding        = FALSE;
static BOOL bFoldsCollapsed = FALSE;
static BOOL bAutoIndent     = TRUE;
static BOOL bReadOnly       = FALSE;

/* Mark Occurrences: 0 off, 1 red, 2 green, 3 blue (Notepad2's numbering). */
static int  iMarkOccurrences  = 0;
static BOOL bMarkOccCase      = FALSE;
static BOOL bMarkOccWord      = FALSE;

/* Session-scoped "don't show this again" flags. Not persisted - settings
 * persistence is not written yet, and a checkbox that silently forgets is
 * better than one that silently does nothing. */
static BOOL bSuppressEOLChanged = FALSE;

/* Syntax scheme + the font every style inherits. */
static int  iScheme = 0;
static CHAR szFontFace[FACESIZE] = "Courier";
static int  iFontSize = 11;

/* Commands that are nothing but a Scintilla message. Keeping them in a table
 * rather than the switch is the difference between a readable dispatch and
 * eighty near-identical cases. */
static const struct { USHORT id; unsigned int msg; } aPassthrough[] = {
    { IDM_UNDO,            SCI_UNDO },
    { IDM_REDO,            SCI_REDO },
    { IDM_CUT,             SCI_CUT },
    { IDM_COPY,            SCI_COPY },
    { IDM_PASTE,           SCI_PASTE },
    { IDM_CLEAR,           SCI_CLEAR },
    { IDM_SELECTALL,       SCI_SELECTALL },
    { IDM_MOVELINEUP,      SCI_MOVESELECTEDLINESUP },
    { IDM_MOVELINEDOWN,    SCI_MOVESELECTEDLINESDOWN },
    { IDM_DUPLICATELINE,   SCI_LINEDUPLICATE },
    { IDM_CUTLINE,         SCI_LINECUT },
    { IDM_COPYLINE,        SCI_LINECOPY },
    { IDM_DELETELINE,      SCI_LINEDELETE },
    { IDM_INDENT,          SCI_TAB },
    { IDM_UNINDENT,        SCI_BACKTAB },
    { IDM_SELDUPLICATE,    SCI_SELECTIONDUPLICATE },
    { IDM_UPPERCASE,       SCI_UPPERCASE },
    { IDM_LOWERCASE,       SCI_LOWERCASE },
    { IDM_DELLINELEFT,     SCI_DELLINELEFT },
    { IDM_DELLINERIGHT,    SCI_DELLINERIGHT },
    { IDM_DELWORDLEFT,     SCI_DELWORDLEFT },
    { IDM_DELWORDRIGHT,    SCI_DELWORDRIGHT },
    { IDM_ZOOMIN,          SCI_ZOOMIN },
    { IDM_ZOOMOUT,         SCI_ZOOMOUT },
    { 0, 0 }
};

static CHAR szFileName[CCHMAXPATH] = "";
static CHAR szStatus[256] = "";
static HWND hwndFrameGlobal = NULLHANDLE;

/* Search state persists across dialog invocations, as it does in Notepad2 -
 * F3 must repeat the last search with the dialog closed. */
static EDITFINDREPLACE efrData;

/* The client area is entirely covered by the editor, so status goes in the title bar -
   which is also where a real editor shows the current file. */
static void ShowStatus(void)
{
    CHAR szTitle[400];
    sprintf(szTitle, "%s%s%s - Notepad2 for OS/2",
            szFileName[0] ? szFileName : "(untitled)",
            szStatus[0] ? "  |  " : "",
            szStatus);
    if (hwndFrameGlobal != NULLHANDLE)
        WinSetWindowText(hwndFrameGlobal, (PSZ)szTitle);
}

/* Forward declarations: LoadFile and ApplyScheme both need these, and both
   sit above them so the file reads file-I/O first, view second. */
static void ApplyView(void);
static void SyncMenu(HWND hwndFrame);

static MRESULT Sci(unsigned int msg, MPARAM mp1, MPARAM mp2)
{
    return WinSendMsg(hwndSci, msg, mp1, mp2);
}

/*--------------------------------------------------------------------------
 * File I/O.  DosOpen/DosRead/DosWrite/DosClose per os2ref/file-io.md 1.
 * Every APIRET is checked and reported - a silent failure here would look
 * like an empty document, which is exactly the wrong diagnosis.
 *------------------------------------------------------------------------*/

/* Ask for a file. WinFileDlg lives in PMCTLS.DLL, not PMWIN
   [OBS-RE - os2ref/resources-and-dialogs.md 10]. FILEDLG is both input and
   output; cbSize must be set before the call. */
static BOOL PickFile(HWND hwnd, BOOL fSave, PSZ pszResult)
{
    FILEDLG fild;
    memset(&fild, 0, sizeof(fild));
    fild.cbSize = sizeof(FILEDLG);
    fild.fl     = FDS_CENTER | (fSave ? FDS_SAVEAS_DIALOG : FDS_OPEN_DIALOG);
    fild.pszTitle = (PSZ)(fSave ? "Save as" : "Open");
    strcpy(fild.szFullFile, szFileName[0] ? szFileName : "*.txt");

    if (WinFileDlg(HWND_DESKTOP, hwnd, &fild) == NULLHANDLE)
        return FALSE;
    if (fild.lReturn != DID_OK)
        return FALSE;
    strcpy((char *)pszResult, fild.szFullFile);
    return TRUE;
}

static BOOL LoadFile(HWND hwnd, PSZ pszFile)
{
    HFILE   hf = NULLHANDLE;
    ULONG   ulAction = 0, cbRead = 0, cbFile = 0;
    APIRET  rc;
    FILESTATUS3 fs3;
    char   *pBuf;

    rc = DosOpen(pszFile, &hf, &ulAction, 0, FILE_NORMAL,
                 OPEN_ACTION_OPEN_IF_EXISTS,
                 OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL);
    if (rc != NO_ERROR) {
        sprintf(szStatus, "DosOpen failed on %s - rc=%lu", pszFile, (unsigned long)rc);
        return FALSE;
    }

    /* Size the buffer from the file, not from a guess. */
    if (DosQueryFileInfo(hf, FIL_STANDARD, &fs3, sizeof(fs3)) != NO_ERROR) {
        DosClose(hf);
        sprintf(szStatus, "DosQueryFileInfo failed on %s", pszFile);
        return FALSE;
    }
    cbFile = fs3.cbFile;

    pBuf = (char *)malloc(cbFile + 1);
    if (!pBuf) {
        DosClose(hf);
        sprintf(szStatus, "out of memory for %lu bytes", (unsigned long)cbFile);
        return FALSE;
    }

    rc = DosRead(hf, pBuf, cbFile, &cbRead);
    DosClose(hf);
    if (rc != NO_ERROR) {
        free(pBuf);
        sprintf(szStatus, "DosRead failed - rc=%lu", (unsigned long)rc);
        return FALSE;
    }
    pBuf[cbRead] = '\0';

    Sci(SCI_SETTEXT, 0, MPFROMP(pBuf));
    Sci(SCI_EMPTYUNDOBUFFER, 0, 0);
    Sci(SCI_SETSAVEPOINT, 0, 0);
    free(pBuf);

    strcpy(szFileName, (char *)pszFile);

    /* Pick the scheme from the extension, as Notepad2 does on open. */
    iScheme = Style_MatchFromFile(szFileName);
    Style_Apply(hwndSci, iScheme, szFontFace, iFontSize);
    if (!Style_SupportsFolding(iScheme))
        bFolding = FALSE;
    ApplyView();
    if (hwndFrameGlobal != NULLHANDLE)
        SyncMenu(hwndFrameGlobal);

    sprintf(szStatus, "Loaded %lu bytes  [%s]",
            (unsigned long)cbRead, Style_Name(iScheme));
    return TRUE;
}

static BOOL SaveFile(HWND hwnd, PSZ pszFile)
{
    HFILE  hf = NULLHANDLE;
    ULONG  ulAction = 0, cbWritten = 0;
    APIRET rc;
    LONG   cbText;
    char  *pBuf;

    cbText = (LONG)LONGFROMMR(Sci(SCI_GETLENGTH, 0, 0));
    pBuf = (char *)malloc(cbText + 1);
    if (!pBuf) {
        sprintf(szStatus, "out of memory for %ld bytes", (long)cbText);
        return FALSE;
    }
    Sci(SCI_GETTEXT, MPFROMLONG(cbText + 1), MPFROMP(pBuf));

    /* CREATE_IF_NEW | TRUNCATE_IF_EXISTS is the "save over" pair. */
    rc = DosOpen(pszFile, &hf, &ulAction, 0, FILE_NORMAL,
                 OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS,
                 OPEN_ACCESS_READWRITE | OPEN_SHARE_DENYWRITE, NULL);
    if (rc != NO_ERROR) {
        free(pBuf);
        sprintf(szStatus, "DosOpen(create) failed on %s - rc=%lu",
                pszFile, (unsigned long)rc);
        return FALSE;
    }

    rc = DosWrite(hf, pBuf, (ULONG)cbText, &cbWritten);
    DosClose(hf);
    free(pBuf);
    if (rc != NO_ERROR) {
        sprintf(szStatus, "DosWrite failed - rc=%lu", (unsigned long)rc);
        return FALSE;
    }
    if (cbWritten != (ULONG)cbText) {
        /* Short write is a real failure, not a rounding detail - say so. */
        sprintf(szStatus, "SHORT WRITE: %lu of %ld bytes to %s",
                (unsigned long)cbWritten, (long)cbText, pszFile);
        return FALSE;
    }

    Sci(SCI_SETSAVEPOINT, 0, 0);
    strcpy(szFileName, (char *)pszFile);
    sprintf(szStatus, "Saved %lu bytes to %s", (unsigned long)cbWritten, pszFile);
    return TRUE;
}

/* Save to the current file if there is one, otherwise fall through to Save as.
 * Returns TRUE if the document is now on disk. */
static BOOL DoSave(HWND hwnd, BOOL bForceSaveAs)
{
    CHAR szPick[CCHMAXPATH];

    if (!bForceSaveAs && szFileName[0])
        return SaveFile(hwnd, (PSZ)szFileName);

    if (!PickFile(hwnd, TRUE, (PSZ)szPick))
        return FALSE;
    return SaveFile(hwnd, (PSZ)szPick);
}

/* Ask before discarding unsaved work. SCI_GETMODIFY tracks against the save
 * point set by LoadFile/SaveFile, so this is accurate rather than a guess.
 * Returns FALSE if the user cancelled and the caller must abandon its action. */
static BOOL ConfirmDiscard(HWND hwnd)
{
    CHAR   szMsg[CCHMAXPATH + 64];
    ULONG  ulReply;

    if (!LONGFROMMR(Sci(SCI_GETMODIFY, 0, 0)))
        return TRUE;

    sprintf(szMsg, "%s has unsaved changes.\nSave them now?",
            szFileName[0] ? szFileName : "The document");
    ulReply = WinMessageBox(HWND_DESKTOP, hwnd, (PSZ)szMsg, (PSZ)"Notepad2",
                            0, MB_YESNOCANCEL | MB_QUERY | MB_MOVEABLE);

    if (ulReply == MBID_CANCEL)
        return FALSE;
    if (ulReply == MBID_YES)
        return DoSave(hwnd, FALSE);   /* a failed save must not discard either */
    return TRUE;
}

/* The ad-hoc "Long Line Column" dialog that used to live here has been replaced
 * by Notepad2's real Long Lines dialog (np2dlg.c), which also carries the edge
 * mode. Word wrap and line numbers now go through ApplySettings with everything
 * else instead of a private ApplyView. */
static void ApplyView(void)
{
    settings.fWordWrap       = bWordWrap;
    settings.bLineNumbers    = bLineNumbers;
    settings.bMarkLongLines  = bLongLineMarker;
    ApplySettings(hwndSci, &settings);

    Sci(SCI_SETINDENTATIONGUIDES, MPFROMLONG(bIndentGuides ? SC_IV_LOOKBOTH : SC_IV_NONE), 0);
    Sci(SCI_SETVIEWWS,   MPFROMLONG(bShowWhitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE), 0);
    Sci(SCI_SETVIEWEOL,  MPFROMLONG(bShowEOLs), 0);
    Sci(SCI_SETCARETLINEVISIBLE, MPFROMLONG(bHiliteCurLine), 0);

    /* Margin 1 is Scintilla's symbol margin; it carries the bookmark markers,
     * so it has to stay wide enough to show one when folding is off. */
    Sci(SCI_SETMARGINTYPEN,  MPFROMLONG(1), MPFROMLONG(SC_MARGIN_SYMBOL));
    Sci(SCI_SETMARGINMASKN,  MPFROMLONG(1), MPFROMLONG(~SC_MASK_FOLDERS));
    Sci(SCI_SETMARGINWIDTHN, MPFROMLONG(1), MPFROMLONG(bSelMargin ? 16 : 0));

    /* Margin 2 is the fold margin. */
    Sci(SCI_SETMARGINTYPEN,      MPFROMLONG(2), MPFROMLONG(SC_MARGIN_SYMBOL));
    Sci(SCI_SETMARGINMASKN,      MPFROMLONG(2), MPFROMLONG(SC_MASK_FOLDERS));
    Sci(SCI_SETMARGINSENSITIVEN, MPFROMLONG(2), MPFROMLONG(bFolding));
    Sci(SCI_SETMARGINWIDTHN,     MPFROMLONG(2), MPFROMLONG(bFolding ? 14 : 0));

    Sci(SCI_SETTABINDENTS, MPFROMLONG(bAutoIndent), 0);
    Sci(SCI_SETREADONLY,   MPFROMLONG(bReadOnly), 0);
}

/* Push the current scheme and font into the control. Called on load, on a
 * manual scheme change, and after a font change - all three have to go
 * through here, or the syntax styles keep the old font. */
static void ApplyScheme(HWND hwndFrame)
{
    Style_Apply(hwndSci, iScheme, szFontFace, iFontSize);
    /* Folding is only real when the lexer emits fold levels. */
    if (!Style_SupportsFolding(iScheme))
        bFolding = FALSE;
    ApplyView();
    if (hwndFrame != NULLHANDLE)
        SyncMenu(hwndFrame);
}

/* Fill the Syntax Scheme submenu from np2style.c's table. Doing it at run
 * time means adding a language touches one table and nothing else.
 * MM_QUERYITEM gets the submenu's window handle; MM_INSERTITEM needs a
 * MENUITEM plus the text as mp2 [DOC-IBM - os2ref/pm-controls.md menus]. */
static void BuildSchemeMenu(HWND hwndFrame)
{
    HWND     hwndMenu = WinWindowFromID(hwndFrame, FID_MENU);
    MENUITEM mi;
    int      i;

    if (hwndMenu == NULLHANDLE)
        return;
    if (!(BOOL)LONGFROMMR(WinSendMsg(hwndMenu, MM_QUERYITEM,
            MPFROM2SHORT(IDM_SCHEME_MENU, TRUE), MPFROMP(&mi))))
        return;
    if (mi.hwndSubMenu == NULLHANDLE)
        return;

    /* Drop the placeholder that keeps the .RC template valid. */
    WinSendMsg(mi.hwndSubMenu, MM_DELETEITEM,
               MPFROM2SHORT(IDM_SCHEME_BASE, FALSE), 0);

    for (i = 0; i < Style_Count(); i++) {
        MENUITEM item;
        memset(&item, 0, sizeof(item));
        item.iPosition   = MIT_END;
        item.afStyle     = MIS_TEXT;
        item.afAttribute = 0;
        item.id          = (USHORT)(IDM_SCHEME_BASE + i);
        item.hwndSubMenu = NULLHANDLE;
        item.hItem       = 0;
        WinSendMsg(mi.hwndSubMenu, MM_INSERTITEM,
                   MPFROMP(&item), MPFROMP((PSZ)Style_Name(i)));
    }
}

/* Line endings. SCI_SETEOLMODE changes what NEW lines use; SCI_CONVERTEOLS
 * rewrites the ones already there. Notepad2 does both, and doing only the
 * first is the classic half-fix: the file looks unchanged until you type. */
static void SetEOLMode(HWND hwnd, LONG mode)
{
    LONG cur = LONGFROMMR(Sci(SCI_GETEOLMODE, 0, 0));
    if (cur == mode)
        return;
    Sci(SCI_BEGINUNDOACTION, 0, 0);
    Sci(SCI_SETEOLMODE, MPFROMLONG(mode), 0);
    Sci(SCI_CONVERTEOLS, MPFROMLONG(mode), 0);
    Sci(SCI_ENDUNDOACTION, 0, 0);
    NP2InfoBox(hwnd,
        mode == SC_EOL_CRLF ? "Line endings converted to Windows (CR+LF)." :
        mode == SC_EOL_LF   ? "Line endings converted to Unix (LF)." :
                              "Line endings converted to Mac (CR).",
        "Line Endings", &bSuppressEOLChanged);
}

/* Bookmarks ride on marker 1, clear of SC_MASK_FOLDERS. */
#define NP2_BOOKMARK 1

static void BookmarkToggle(void)
{
    LONG line = LONGFROMMR(Sci(SCI_LINEFROMPOSITION,
                   MPFROMLONG(LONGFROMMR(Sci(SCI_GETCURRENTPOS, 0, 0))), 0));
    LONG mask = LONGFROMMR(Sci(SCI_MARKERGET, MPFROMLONG(line), 0));
    if (mask & (1 << NP2_BOOKMARK))
        Sci(SCI_MARKERDELETE, MPFROMLONG(line), MPFROMLONG(NP2_BOOKMARK));
    else
        Sci(SCI_MARKERADD, MPFROMLONG(line), MPFROMLONG(NP2_BOOKMARK));
}

static void BookmarkGoto(BOOL bNext)
{
    LONG line = LONGFROMMR(Sci(SCI_LINEFROMPOSITION,
                   MPFROMLONG(LONGFROMMR(Sci(SCI_GETCURRENTPOS, 0, 0))), 0));
    LONG found;
    if (bNext)
        found = LONGFROMMR(Sci(SCI_MARKERNEXT, MPFROMLONG(line + 1),
                               MPFROMLONG(1 << NP2_BOOKMARK)));
    else
        found = LONGFROMMR(Sci(SCI_MARKERPREVIOUS, MPFROMLONG(line - 1),
                               MPFROMLONG(1 << NP2_BOOKMARK)));
    /* Wrap, so Next from the last bookmark returns to the first. */
    if (found < 0)
        found = bNext
              ? LONGFROMMR(Sci(SCI_MARKERNEXT, 0, MPFROMLONG(1 << NP2_BOOKMARK)))
              : LONGFROMMR(Sci(SCI_MARKERPREVIOUS,
                    MPFROMLONG(LONGFROMMR(Sci(SCI_GETLINECOUNT, 0, 0))),
                    MPFROMLONG(1 << NP2_BOOKMARK)));
    if (found >= 0) {
        Sci(SCI_ENSUREVISIBLE, MPFROMLONG(found), 0);
        Sci(SCI_GOTOLINE, MPFROMLONG(found), 0);
    }
}

/* Reflect the toggles into the menu with MM_SETITEMATTR
 * [DOC-IBM - os2ref/pm-controls.md, menu messages]. */
static void SyncMenu(HWND hwndFrame)
{
    HWND hwndMenu = WinWindowFromID(hwndFrame, FID_MENU);
    if (hwndMenu == NULLHANDLE)
        return;
    WinSendMsg(hwndMenu, MM_SETITEMATTR,
               MPFROM2SHORT(IDM_WORDWRAP, TRUE),
               MPFROM2SHORT(MIA_CHECKED, bWordWrap ? MIA_CHECKED : 0));
    WinSendMsg(hwndMenu, MM_SETITEMATTR,
               MPFROM2SHORT(IDM_LINENUMBERS, TRUE),
               MPFROM2SHORT(MIA_CHECKED, bLineNumbers ? MIA_CHECKED : 0));
    {
        static const struct { USHORT id; const BOOL *pf; } aChecks[] = {
            { IDM_LONGLINEMARKER, &bLongLineMarker },
            { IDM_INDENTGUIDES,   &bIndentGuides   },
            { IDM_SHOWWHITESPACE, &bShowWhitespace },
            { IDM_SHOWEOLS,       &bShowEOLs       },
            { IDM_HILITECURLINE,  &bHiliteCurLine  },
            { IDM_SELMARGIN,      &bSelMargin      },
            { IDM_FOLDING,        &bFolding        },
            { IDM_AUTOINDENT,     &bAutoIndent     },
            { IDM_READONLY,       &bReadOnly       },
            { IDM_TABSASSPACES,   &settings.bTabsAsSpaces },
            { 0, NULL }
        };
        int i;
        for (i = 0; aChecks[i].id; i++)
            WinSendMsg(hwndMenu, MM_SETITEMATTR,
                       MPFROM2SHORT(aChecks[i].id, TRUE),
                       MPFROM2SHORT(MIA_CHECKED, *aChecks[i].pf ? MIA_CHECKED : 0));
    }

    /* Radio groups. PM has no CheckMenuRadioItem - tick one, clear the rest. */
    {
        LONG eol = LONGFROMMR(Sci(SCI_GETEOLMODE, 0, 0));
        /* Indexed by SC_EOL_* VALUE, not by menu order: the constants are
         * CRLF=0, CR=1, LF=2, so listing them in menu order puts the check
         * mark on the wrong item. */
        USHORT aEol[3] = { IDM_EOL_CRLF, IDM_EOL_CR, IDM_EOL_LF };
        USHORT aMark[4] = { IDM_MARKOCC_OFF, IDM_MARKOCC_RED,
                            IDM_MARKOCC_GREEN, IDM_MARKOCC_BLUE };
        int i;
        for (i = 0; i < 3; i++)
            WinSendMsg(hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(aEol[i], TRUE),
                       MPFROM2SHORT(MIA_CHECKED, (eol == i) ? MIA_CHECKED : 0));
        for (i = 0; i < 4; i++)
            WinSendMsg(hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(aMark[i], TRUE),
                       MPFROM2SHORT(MIA_CHECKED,
                                    (iMarkOccurrences == i) ? MIA_CHECKED : 0));
        WinSendMsg(hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(IDM_MARKOCC_CASE, TRUE),
                   MPFROM2SHORT(MIA_CHECKED, bMarkOccCase ? MIA_CHECKED : 0));
        WinSendMsg(hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(IDM_MARKOCC_WORD, TRUE),
                   MPFROM2SHORT(MIA_CHECKED, bMarkOccWord ? MIA_CHECKED : 0));

        for (i = 0; i < Style_Count(); i++)
            WinSendMsg(hwndMenu, MM_SETITEMATTR,
                       MPFROM2SHORT(IDM_SCHEME_BASE + i, TRUE),
                       MPFROM2SHORT(MIA_CHECKED, (iScheme == i) ? MIA_CHECKED : 0));

        /* Code Folding is meaningless without a lexer that emits fold levels;
         * grey it out rather than offer a switch that does nothing. */
        WinSendMsg(hwndMenu, MM_SETITEMATTR,
                   MPFROM2SHORT(IDM_FOLDING, TRUE),
                   MPFROM2SHORT(MIA_DISABLED,
                       Style_SupportsFolding(iScheme) ? 0 : MIA_DISABLED));
    }
}

MRESULT EXPENTRY ClientWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {

    case WM_CREATE:
        hwndSci = WinCreateWindow(hwnd, (PSZ)"Scintilla", (PSZ)"",
                                  WS_VISIBLE, 0, 0, 0, 0,
                                  hwnd, HWND_TOP, 2000, NULL, NULL);
        if (hwndSci != NULLHANDLE) {
            Sci(SCI_SETMARGINTYPEN, MPFROMLONG(0), MPFROMLONG(SC_MARGIN_NUMBER));
            Sci(SCI_STYLESETSIZE, MPFROMLONG(STYLE_DEFAULT), MPFROMLONG(11));
            Sci(SCI_STYLESETFONT, MPFROMLONG(STYLE_DEFAULT), MPFROMP((void *)"Courier"));
            Sci(SCI_STYLECLEARALL, 0, 0);
            Sci(SCI_SETTEXT, 0, MPFROMP((void *)
                "Notepad2 frame + Scintilla, both on OS/2 Presentation Manager.\r\n"
                "\r\n"
                "Edit menu drives SCI_UNDO / SCI_CUT / SCI_COPY / SCI_PASTE, so the\r\n"
                "clipboard path goes through DosAllocSharedMem + WinSetClipbrdData.\r\n"
                "\r\n"
                "Select a line, Edit/Copy, then Edit/Paste to exercise the clipboard.\r\n"
                "\r\n"
                "Search menu: Find (Ctrl+F), Replace (Ctrl+H), F3 / Shift+F3, Go To (Ctrl+G).\r\n"
                "Searching is Scintilla's own SCI_FINDTEXT - the dialog is the ported part.\r\n"
                "\r\n"
                "Edit/Lines has Modify, Align and Sort; Settings has Tabs, Long Lines and\r\n"
                "Word Wrap. All ten dialogs are Notepad2's own, converted to PM.\r\n"
                "\r\n"
                "banana\r\n"
                "Apple\r\n"
                "cherry\r\n"
                "apple\r\n"
                "item10\r\n"
                "item9\r\n"));
            Sci(SCI_EMPTYUNDOBUFFER, 0, 0);
            /* Without this the starter text counts as an unsaved change and the
               very first File/New would prompt to save it. */
            Sci(SCI_SETSAVEPOINT, 0, 0);
            ApplyView();
            WinSetFocus(HWND_DESKTOP, hwndSci);
        }
        return (MRESULT)0;

    case WM_SIZE:
        if (hwndSci != NULLHANDLE)
            WinSetWindowPos(hwndSci, HWND_TOP, 0, 0,
                            SHORT1FROMMP(mp2), SHORT2FROMMP(mp2),
                            SWP_SIZE | SWP_MOVE | SWP_SHOW);
        return (MRESULT)0;

    /* NOTE: do NOT forward focus to the editor from WM_SETFOCUS.
     * PM gives focus to the menu window while a pulled-down menu is being navigated;
     * a client that unconditionally bounces focus back to its child steals it from the
     * menu, and the menu then renders normally but ignores every mnemonic - the
     * keystrokes land in the editor instead. Focus is set once at WM_CREATE and again
     * after a command completes, which is enough. [OBS-RE] */

    /* Scintilla reports through WM_CONTROL, with the SCNotification in mp2
     * [scintilla/os2/ScintillaPM.cxx NotifyParent]. SCN_UPDATEUI fires on every
     * caret or selection change, which is exactly when the occurrence marks
     * need recomputing. */
    case WM_CONTROL:
        if (SHORT1FROMMP(mp1) == 2000 && iMarkOccurrences) {
            SCNotification *pscn = (SCNotification *)PVOIDFROMMP(mp2);
            if (pscn && pscn->nmhdr.code == SCN_UPDATEUI)
                EditMarkAll(hwndSci, iMarkOccurrences, bMarkOccCase, bMarkOccWord);
        }
        return (MRESULT)0;

    case WM_COMMAND: {
        HWND hwndFrame = WinQueryWindow(hwnd, QW_PARENT);
        /* Commands that open the modeless search dialog must NOT have focus
         * yanked back to the editor at the end of this handler. */
        BOOL bKeepFocus = FALSE;

        USHORT idCmd = SHORT1FROMMP(mp1);
        int i;

        /* Pure Scintilla commands first - see aPassthrough. */
        for (i = 0; aPassthrough[i].id; i++) {
            if (aPassthrough[i].id == idCmd) {
                Sci(aPassthrough[i].msg, 0, 0);
                WinSetFocus(HWND_DESKTOP, hwndSci);
                return (MRESULT)0;
            }
        }

        /* Scheme ids are a contiguous run generated from np2style.c's table. */
        if (idCmd >= IDM_SCHEME_BASE && idCmd < IDM_SCHEME_BASE + Style_Count()) {
            iScheme = idCmd - IDM_SCHEME_BASE;
            ApplyScheme(hwndFrame);
            WinSetFocus(HWND_DESKTOP, hwndSci);
            return (MRESULT)0;
        }

        switch (idCmd) {
        case IDM_NEW:
            if (!ConfirmDiscard(hwnd))
                break;
            Sci(SCI_CLEARALL, 0, 0);
            Sci(SCI_EMPTYUNDOBUFFER, 0, 0);
            Sci(SCI_SETSAVEPOINT, 0, 0);
            szFileName[0] = '\0';
            strcpy(szStatus, "New document");
            ShowStatus();
            break;

        case IDM_OPEN: {
            CHAR szPick[CCHMAXPATH];
            if (!ConfirmDiscard(hwnd))
                break;
            if (PickFile(hwnd, FALSE, (PSZ)szPick))
                LoadFile(hwnd, (PSZ)szPick);
            ShowStatus();
            /* Invalidate the EDITOR, not just the client: the client is fully
             * covered by its child, so invalidating it repaints nothing the
             * user can see and the area the dialog occupied stays stale. */
            WinInvalidateRect(hwndSci, NULL, TRUE);
            break;
        }

        case IDM_SAVE:
            DoSave(hwnd, FALSE);
            ShowStatus();
            break;

        case IDM_SAVEAS:
            DoSave(hwnd, TRUE);
            ShowStatus();
            WinInvalidateRect(hwndSci, NULL, TRUE);
            break;

        case IDM_FIND:
            EditFindReplaceDlg(hwnd, hwndSci, &efrData, FALSE);
            bKeepFocus = TRUE;
            break;

        case IDM_REPLACE:
            EditFindReplaceDlg(hwnd, hwndSci, &efrData, TRUE);
            bKeepFocus = TRUE;
            break;

        /* F3 / Shift+F3 repeat the last search with no dialog open. With an
         * empty pattern there is nothing to repeat, so open the dialog rather
         * than silently doing nothing. */
        case IDM_FINDNEXT:
            if (efrData.szFind[0])
                EditFindNext(hwndSci, &efrData, FALSE);
            else {
                EditFindReplaceDlg(hwnd, hwndSci, &efrData, FALSE);
                bKeepFocus = TRUE;
            }
            break;

        case IDM_FINDPREV:
            if (efrData.szFind[0])
                EditFindPrev(hwndSci, &efrData, FALSE);
            else {
                EditFindReplaceDlg(hwnd, hwndSci, &efrData, FALSE);
                bKeepFocus = TRUE;
            }
            break;
        case IDM_UNDO:      Sci(SCI_UNDO, 0, 0);                     break;
        case IDM_REDO:      Sci(SCI_REDO, 0, 0);                     break;
        case IDM_CUT:       Sci(SCI_CUT, 0, 0);                      break;
        case IDM_COPY:      Sci(SCI_COPY, 0, 0);                     break;
        case IDM_PASTE:     Sci(SCI_PASTE, 0, 0);                    break;
        case IDM_SELECTALL: Sci(SCI_SELECTALL, 0, 0);                break;

        case IDM_WORDWRAP:
            bWordWrap = !bWordWrap;
            ApplyView();
            SyncMenu(hwndFrame);
            break;

        case IDM_LINENUMBERS:
            bLineNumbers = !bLineNumbers;
            ApplyView();
            SyncMenu(hwndFrame);
            break;

        case IDM_GOTOLINE:
            EditGotoLineDlg(hwnd, hwndSci);
            break;

        /* --- Settings ------------------------------------------------------ */
        case IDM_TABSETTINGS:
            if (EditTabSettingsDlg(hwnd, &settings))
                ApplySettings(hwndSci, &settings);
            break;

        case IDM_LONGLINESET:
            if (EditLongLinesDlg(hwnd, &settings))
                ApplySettings(hwndSci, &settings);
            break;

        case IDM_WORDWRAPSET:
            if (EditWordWrapDlg(hwnd, &settings))
                ApplySettings(hwndSci, &settings);
            break;

        /* --- Lines and selection ------------------------------------------- */
        case IDM_MODIFYLINES:
            if (EditModifyLinesDlg(hwnd, szPrefix, szAppend, sizeof(szPrefix)))
                EditModifyLines(hwndSci, szPrefix, szAppend);
            break;

        case IDM_ALIGNLINES:
            if (EditAlignDlg(hwnd, &iAlignMode))
                EditAlignText(hwndSci, iAlignMode);
            break;

        case IDM_SORTLINES:
            if (EditSortDlg(hwnd, hwndSci, &iSortFlags))
                EditSortLines(hwndSci, iSortFlags);
            break;

        case IDM_ENCLOSESEL:
            if (EditEncloseSelectionDlg(hwnd, szEncOpen, szEncClose, sizeof(szEncOpen)))
                EditEncloseSelection(hwndSci, szEncOpen, szEncClose);
            break;

        case IDM_INSERTTAG:
            if (EditInsertTagDlg(hwnd, szTagOpen, szTagClose, sizeof(szTagOpen)))
                EditEncloseSelection(hwndSci, szTagOpen, szTagClose);
            break;

        case IDM_ABOUT:
            EditAboutDlg(hwnd);
            break;

        /* --- Edit: clipboard extras ---------------------------------------- */
        case IDM_COPYALL:
            Sci(SCI_SELECTALL, 0, 0);
            Sci(SCI_COPY, 0, 0);
            break;
        case IDM_COPYADD:
            EditCopyAppend(hwndSci);
            break;
        case IDM_CLEARCLIPBOARD:
            if (WinOpenClipbrd(WinQueryAnchorBlock(hwnd))) {
                WinEmptyClipbrd(WinQueryAnchorBlock(hwnd));
                WinCloseClipbrd(WinQueryAnchorBlock(hwnd));
            }
            break;

        /* --- Lines --------------------------------------------------------- */
        case IDM_SPLITLINES:      EditSplitLines(hwndSci);          break;
        case IDM_JOINLINES:       EditJoinLines(hwndSci, FALSE);    break;
        case IDM_JOINPARAGRAPHS:  EditJoinLines(hwndSci, TRUE);     break;

        /* --- Block --------------------------------------------------------- */
        case IDM_PADWITHSPACES:    EditPadWithSpaces(hwndSci);          break;
        case IDM_STRIP1STCHAR:     EditStripFirstCharacter(hwndSci);    break;
        case IDM_STRIPLASTCHAR:    EditStripLastCharacter(hwndSci);     break;
        case IDM_TRIMLINES:        EditStripTrailingBlanks(hwndSci);    break;
        case IDM_COMPRESSWS:       EditCompressSpaces(hwndSci);         break;
        case IDM_MERGEBLANKLINES:  EditRemoveBlankLines(hwndSci, TRUE); break;
        case IDM_REMOVEBLANKLINES: EditRemoveBlankLines(hwndSci, FALSE); break;

        /* --- Enclose shortcuts. These are EditEncloseSelection with fixed
         * strings, which is exactly how Notepad2 implements them too. ------- */
        case IDM_ENCLOSE_PAREN:    EditEncloseSelection(hwndSci, "(", ")");   break;
        case IDM_ENCLOSE_BRACE:    EditEncloseSelection(hwndSci, "{", "}");   break;
        case IDM_ENCLOSE_BRACKET:  EditEncloseSelection(hwndSci, "[", "]");   break;
        case IDM_ENCLOSE_SQUOTE:   EditEncloseSelection(hwndSci, "'", "'");   break;
        case IDM_ENCLOSE_DQUOTE:   EditEncloseSelection(hwndSci, "\"", "\""); break;
        case IDM_ENCLOSE_BACKTICK: EditEncloseSelection(hwndSci, "`", "`");   break;

        /* --- Convert ------------------------------------------------------- */
        case IDM_INVERTCASE:      EditInvertCase(hwndSci);    break;
        case IDM_TITLECASE:       EditTitleCase(hwndSci);     break;
        case IDM_SENTENCECASE:    EditSentenceCase(hwndSci);  break;
        case IDM_TABIFYSEL:       EditSpacesToTabs(hwndSci, settings.iTabWidth, FALSE); break;
        case IDM_UNTABIFYSEL:     EditTabsToSpaces(hwndSci, settings.iTabWidth, FALSE); break;
        case IDM_TABIFYINDENT:    EditSpacesToTabs(hwndSci, settings.iTabWidth, TRUE);  break;
        case IDM_UNTABIFYINDENT:  EditTabsToSpaces(hwndSci, settings.iTabWidth, TRUE);  break;

        /* --- Insert -------------------------------------------------------- */
        case IDM_INSERT_TIMESHORT: EditInsertDateTime(hwndSci, TRUE);  break;
        case IDM_INSERT_TIMELONG:  EditInsertDateTime(hwndSci, FALSE); break;
        case IDM_INSERT_FILENAME: {
            /* Filename only - walk back from the end to the last separator. */
            const char *p = szFileName;
            const char *q = strrchr(szFileName, '\\');
            const char *r = strrchr(szFileName, '/');
            if (r > q) q = r;
            if (q) p = q + 1;
            EditInsertString(hwndSci, p);
            break;
        }
        case IDM_INSERT_PATHNAME:
            EditInsertString(hwndSci, szFileName);
            break;

        /* --- Special ------------------------------------------------------- */
        case IDM_LINECOMMENT:
            /* No lexer is wired up yet, so the marker cannot come from the
             * language. "//" is the honest default until Styles.c lands. */
            EditToggleLineComments(hwndSci, "//", FALSE);
            break;
        case IDM_STREAMCOMMENT:
            EditEncloseSelection(hwndSci, "/* ", " */");
            break;
        case IDM_URLENCODE:      EditURLEncode(hwndSci);      break;
        case IDM_URLDECODE:      EditURLDecode(hwndSci);      break;
        case IDM_ESCAPECCHARS:   EditEscapeCChars(hwndSci);   break;
        case IDM_UNESCAPECCHARS: EditUnescapeCChars(hwndSci); break;
        case IDM_CHAR2HEX:       EditChar2Hex(hwndSci);       break;
        case IDM_HEX2CHAR:       EditHex2Char(hwndSci);       break;
        case IDM_FINDMATCHBRACE:  EditFindMatchingBrace(hwndSci, FALSE); break;
        case IDM_SELTOMATCHBRACE: EditFindMatchingBrace(hwndSci, TRUE);  break;

        /* --- Bookmarks ----------------------------------------------------- */
        case IDM_BOOKMARKTOGGLE: BookmarkToggle();      break;
        case IDM_BOOKMARKNEXT:   BookmarkGoto(TRUE);    break;
        case IDM_BOOKMARKPREV:   BookmarkGoto(FALSE);   break;
        case IDM_BOOKMARKCLEAR:
            Sci(SCI_MARKERDELETEALL, MPFROMLONG(NP2_BOOKMARK), 0);
            break;

        /* --- View toggles -------------------------------------------------- */
        case IDM_LONGLINEMARKER: bLongLineMarker = !bLongLineMarker; ApplyView(); SyncMenu(hwndFrame); break;
        case IDM_INDENTGUIDES:   bIndentGuides   = !bIndentGuides;   ApplyView(); SyncMenu(hwndFrame); break;
        case IDM_SHOWWHITESPACE: bShowWhitespace = !bShowWhitespace; ApplyView(); SyncMenu(hwndFrame); break;
        case IDM_SHOWEOLS:       bShowEOLs       = !bShowEOLs;       ApplyView(); SyncMenu(hwndFrame); break;
        case IDM_HILITECURLINE:  bHiliteCurLine  = !bHiliteCurLine;  ApplyView(); SyncMenu(hwndFrame); break;
        case IDM_SELMARGIN:      bSelMargin      = !bSelMargin;      ApplyView(); SyncMenu(hwndFrame); break;
        case IDM_AUTOINDENT:     bAutoIndent     = !bAutoIndent;     ApplyView(); SyncMenu(hwndFrame); break;
        case IDM_READONLY:       bReadOnly       = !bReadOnly;       ApplyView(); SyncMenu(hwndFrame); break;

        case IDM_FOLDING:
            bFolding = !bFolding;
            /* Folding needs the lexer to emit fold levels. Without a lexer the
             * margin would draw empty, so say so rather than show a dead margin. */
            Sci(SCI_SETPROPERTY, MPFROMP((void *)"fold"), MPFROMP((void *)(bFolding ? "1" : "0")));
            ApplyView();
            SyncMenu(hwndFrame);
            break;

        case IDM_TOGGLEFOLDS:
            bFoldsCollapsed = !bFoldsCollapsed;
            Sci(SCI_FOLDALL, MPFROMLONG(bFoldsCollapsed ? SC_FOLDACTION_CONTRACT
                                                        : SC_FOLDACTION_EXPAND), 0);
            break;

        case IDM_RESETZOOM:
            Sci(SCI_SETZOOM, MPFROMLONG(0), 0);
            break;

        case IDM_TABSASSPACES:
            settings.bTabsAsSpaces = !settings.bTabsAsSpaces;
            ApplySettings(hwndSci, &settings);
            SyncMenu(hwndFrame);
            break;

        /* --- Syntax scheme and font ---------------------------------------- */
        case IDM_VIEW_FONT:
            if (Style_ChooseFont(hwnd, szFontFace, sizeof(szFontFace), &iFontSize))
                ApplyScheme(hwndFrame);
            break;

        /* --- Line endings -------------------------------------------------- */
        case IDM_EOL_CRLF: SetEOLMode(hwnd, SC_EOL_CRLF); SyncMenu(hwndFrame); break;
        case IDM_EOL_LF:   SetEOLMode(hwnd, SC_EOL_LF);   SyncMenu(hwndFrame); break;
        case IDM_EOL_CR:   SetEOLMode(hwnd, SC_EOL_CR);   SyncMenu(hwndFrame); break;

        /* --- Mark Occurrences ---------------------------------------------- */
        case IDM_MARKOCC_OFF:
        case IDM_MARKOCC_RED:
        case IDM_MARKOCC_GREEN:
        case IDM_MARKOCC_BLUE:
            iMarkOccurrences = idCmd - IDM_MARKOCC_OFF;
            EditMarkAll(hwndSci, iMarkOccurrences, bMarkOccCase, bMarkOccWord);
            SyncMenu(hwndFrame);
            break;
        case IDM_MARKOCC_CASE:
            bMarkOccCase = !bMarkOccCase;
            EditMarkAll(hwndSci, iMarkOccurrences, bMarkOccCase, bMarkOccWord);
            SyncMenu(hwndFrame);
            break;
        case IDM_MARKOCC_WORD:
            bMarkOccWord = !bMarkOccWord;
            EditMarkAll(hwndSci, iMarkOccurrences, bMarkOccCase, bMarkOccWord);
            SyncMenu(hwndFrame);
            break;

        /* --- File ---------------------------------------------------------- */
        case IDM_REVERT:
            if (szFileName[0] && ConfirmDiscard(hwnd)) {
                CHAR szKeep[CCHMAXPATH];
                strcpy(szKeep, szFileName);
                LoadFile(hwnd, (PSZ)szKeep);
                ShowStatus();
                WinInvalidateRect(hwndSci, NULL, TRUE);
            }
            break;

        case IDM_EXIT:
            if (!ConfirmDiscard(hwnd))
                break;
            WinPostMsg(hwnd, WM_QUIT, 0, 0);
            break;
        }
        if (!bKeepFocus)
            WinSetFocus(HWND_DESKTOP, hwndSci);
        return (MRESULT)0;
    }
    }
    return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

int main(void)
{
    HAB   hab = WinInitialize(0);
    HMQ   hmq = WinCreateMsgQueue(hab, 0);
    HWND  hwndFrame, hwndClient = NULLHANDLE;
    ULONG flFrame = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER |
                    FCF_MINMAX | FCF_TASKLIST | FCF_MENU |
                    FCF_ACCELTABLE;
    QMSG  qmsg;

    SettingsDefaults(&settings);
    bWordWrap    = settings.fWordWrap;
    bLineNumbers = settings.bLineNumbers;

    Scintilla_RegisterClasses((void *)hab);
    WinRegisterClass(hab, (PSZ)"Notepad2Client", ClientWndProc, CS_SIZEREDRAW, 0);

    hwndFrame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flFrame,
                                   (PSZ)"Notepad2Client",
                                   (PSZ)"Notepad2 for OS/2 - Scintilla edition",
                                   0, NULLHANDLE, IDD_NP2MAIN, &hwndClient);
    if (hwndFrame == NULLHANDLE) {
        WinDestroyMsgQueue(hmq); WinTerminate(hab); return 1;
    }
    hwndFrameGlobal = hwndFrame;
    BuildSchemeMenu(hwndFrame);
    Style_Apply(hwndSci, iScheme, szFontFace, iFontSize);
    ApplyView();
    SyncMenu(hwndFrame);
    ShowStatus();
    WinSetWindowPos(hwndFrame, HWND_TOP, 40, 40, 720, 440,
                    SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);

    /* One loop drives both the frame and the modeless Find dialog. PM needs no
     * IsDialogMessage equivalent: the dialog is an ordinary window in this
     * queue, and WinDefDlgProc handles its tabbing and default-button logic
     * when WinDispatchMsg delivers to it. */
    while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0))
        WinDispatchMsg(hab, &qmsg);

    if (EditFindReplaceHwnd() != NULLHANDLE)
        WinDestroyWindow(EditFindReplaceHwnd());
    WinDestroyWindow(hwndFrame);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}

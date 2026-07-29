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
    sprintf(szStatus, "Loaded %lu bytes from %s", (unsigned long)cbRead, pszFile);
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
    settings.fWordWrap    = bWordWrap;
    settings.bLineNumbers = bLineNumbers;
    ApplySettings(hwndSci, &settings);
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

    case WM_COMMAND: {
        HWND hwndFrame = WinQueryWindow(hwnd, QW_PARENT);
        /* Commands that open the modeless search dialog must NOT have focus
         * yanked back to the editor at the end of this handler. */
        BOOL bKeepFocus = FALSE;

        switch (SHORT1FROMMP(mp1)) {
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
            WinInvalidateRect(hwnd, NULL, TRUE);
            break;
        }

        case IDM_SAVE:
            DoSave(hwnd, FALSE);
            ShowStatus();
            break;

        case IDM_SAVEAS:
            DoSave(hwnd, TRUE);
            ShowStatus();
            WinInvalidateRect(hwnd, NULL, TRUE);
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

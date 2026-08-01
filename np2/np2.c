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

/* kLIBC's extensions (_execname and friends) are declared in <stdlib.h> behind
 *   #if (!defined(__STRICT_ANSI__) && !defined(_POSIX_SOURCE)) || defined(__USE_EMX)
 * and -std=c++11 defines __STRICT_ANSI__ - so the declaration is present in the
 * header, visibly, and still not in scope. The symptom is "not declared in this
 * scope" for a function you can point at. __USE_EMX is the header's own opt-in;
 * -std=gnu++11 would work too but changes the dialect for everything. */
#define __USE_EMX
#include <stdlib.h>
#include <string.h>

#include "Scintilla.h"
#include "np2.h"
#include "np2find.h"
#include "np2edit.h"
#include "np2dlg.h"
#include "np2cmd.h"
#include "np2style.h"
#include "np2ini.h"
#include "np2run.h"
#include "np2browse.h"
#include "np2print.h"
#include "np2enc.h"
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

static CHAR szIniPath[CCHMAXPATH] = "";
static CHAR szExePath[CCHMAXPATH] = "";   /* for Launch > New Window */
static CHAR szRunCmd[300] = "";
static CHAR szBrowseDir[CCHMAXPATH] = "";
static CHAR aMru[NP2_MRU_MAX][CCHMAXPATH];
static int  cMru = 0;

/* An MRU entry must be fully qualified or it only reopens from the directory
 * it was first opened in - "demo.c" from a command line is a real example.
 * DosQueryPathInfo with FIL_QUERYFULLNAME is the documented OS/2 way to
 * resolve one [os2ref/file-io.md]. */
static void MruAddQualified(const char *pszFile)
{
    CHAR szFull[CCHMAXPATH];
    if (DosQueryPathInfo((PSZ)pszFile, FIL_QUERYFULLNAME,
                         szFull, sizeof(szFull)) == NO_ERROR && szFull[0])
        MruAdd(aMru, &cMru, szFull);
    else
        MruAdd(aMru, &cMru, pszFile);
}
static int  iEncoding = NP2ENC_ANSI;      /* the document's file encoding */
static int  iForceEncoding = -1;          /* Reload As: skip detection once */

/* Statusbar. PM has no statusbar control class - the WC_* set has 24 classes
 * and none of them is one - so it is composed from WC_STATIC panels
 * [os2ref/pm-controls.md 6]. */
static BOOL bStatusbar = TRUE;
static HWND hwndStatus[4] = { NULLHANDLE, NULLHANDLE, NULLHANDLE, NULLHANDLE };
static LONG cyStatus = 20;
static SWP  swpSaved;                 /* window position, restored at start */
static BOOL bHaveSavedPos = FALSE;

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

/* Refresh the statusbar from the editor's current state. Driven by
 * SCN_UPDATEUI, so it follows the caret without a timer. */
static void UpdateStatusbar(void)
{
    CHAR sz[128];
    LONG pos, line, col, selStart, selEnd, eol;

    if (!bStatusbar || hwndStatus[0] == NULLHANDLE || hwndSci == NULLHANDLE)
        return;

    pos  = LONGFROMMR(Sci(SCI_GETCURRENTPOS, 0, 0));
    line = LONGFROMMR(Sci(SCI_LINEFROMPOSITION, MPFROMLONG(pos), 0));
    col  = LONGFROMMR(Sci(SCI_GETCOLUMN, MPFROMLONG(pos), 0));
    sprintf(sz, "Ln %ld, Col %ld", (long)line + 1, (long)col + 1);
    WinSetWindowText(hwndStatus[0], (PSZ)sz);

    selStart = LONGFROMMR(Sci(SCI_GETSELECTIONSTART, 0, 0));
    selEnd   = LONGFROMMR(Sci(SCI_GETSELECTIONEND, 0, 0));
    if (selEnd > selStart) {
        LONG l1 = LONGFROMMR(Sci(SCI_LINEFROMPOSITION, MPFROMLONG(selStart), 0));
        LONG l2 = LONGFROMMR(Sci(SCI_LINEFROMPOSITION, MPFROMLONG(selEnd), 0));
        sprintf(sz, "Sel %ld ch, %ld ln", (long)(selEnd - selStart), (long)(l2 - l1 + 1));
    } else {
        sprintf(sz, "%ld lines", (long)LONGFROMMR(Sci(SCI_GETLINECOUNT, 0, 0)));
    }
    WinSetWindowText(hwndStatus[1], (PSZ)sz);

    eol = LONGFROMMR(Sci(SCI_GETEOLMODE, 0, 0));
    sprintf(sz, "%s  %s", EncName(iEncoding),
            eol == SC_EOL_CRLF ? "CR+LF" : (eol == SC_EOL_CR ? "CR" : "LF"));
    WinSetWindowText(hwndStatus[2], (PSZ)sz);

    sprintf(sz, "%s%s%s", Style_Name(iScheme),
            bReadOnly ? "  R/O" : "",
            LONGFROMMR(Sci(SCI_GETMODIFY, 0, 0)) ? "  *" : "");
    WinSetWindowText(hwndStatus[3], (PSZ)sz);
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

    /* Detect, then convert into the editor's UTF-8 representation. OS/2
     * converts from a code set you NAME - it does not guess one - so the
     * sniff is ours and the transcoding is UniUconv's
     * [os2ref/unicode-conversion.md 9.2]. */
    {
        char *pUtf8 = NULL;
        ULONG cbUtf8 = 0;
        CHAR  szEncErr[160] = "";

        iEncoding = (iForceEncoding >= 0) ? iForceEncoding
                                          : EncDetect(pBuf, cbRead);
        iForceEncoding = -1;

        if (EncToUtf8(iEncoding, pBuf, cbRead, &pUtf8, &cbUtf8,
                      szEncErr, sizeof(szEncErr))) {
            free(pBuf);
            pBuf = pUtf8;
            cbRead = cbUtf8;
            pBuf[cbRead] = '\0';
        } else {
            /* Report and fall back to the raw bytes rather than showing an
             * empty document - a failed conversion must not look like an
             * empty file. */
            sprintf(szStatus, "encoding conversion failed (%s) - showing raw bytes",
                    szEncErr);
            iEncoding = NP2ENC_ANSI;
        }
    }

    Sci(SCI_SETCODEPAGE, MPFROMLONG(SC_CP_UTF8), 0);
    Sci(SCI_SETTEXT, 0, MPFROMP(pBuf));
    Sci(SCI_EMPTYUNDOBUFFER, 0, 0);
    Sci(SCI_SETSAVEPOINT, 0, 0);
    free(pBuf);

    strcpy(szFileName, (char *)pszFile);
    MruAddQualified(szFileName);

    /* Pick the scheme from the extension, as Notepad2 does on open. */
    iScheme = Style_MatchFromFile(szFileName);
    Style_Apply(hwndSci, iScheme, szFontFace, iFontSize);
    if (!Style_SupportsFolding(iScheme))
        bFolding = FALSE;
    ApplyView();
    if (hwndFrameGlobal != NULLHANDLE)
        SyncMenu(hwndFrameGlobal);

    if (!szStatus[0])
        sprintf(szStatus, "%lu bytes  [%s]  %s",
                (unsigned long)cbRead, Style_Name(iScheme), EncName(iEncoding));
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

    /* Convert the editor's UTF-8 back out to the document's own encoding. */
    {
        char *pOut = NULL;
        ULONG cbOut = 0;
        CHAR  szEncErr[160] = "";
        if (EncFromUtf8(iEncoding, pBuf, (ULONG)cbText, &pOut, &cbOut,
                        szEncErr, sizeof(szEncErr))) {
            free(pBuf);
            pBuf = pOut;
            cbText = (LONG)cbOut;
        } else {
            sprintf(szStatus, "encoding conversion failed (%s) - NOT saved", szEncErr);
            free(pBuf);
            return FALSE;
        }
    }

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
    MruAddQualified(szFileName);
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

/*--------------------------------------------------------------------------
 * Settings persistence.  See np2ini.c - an app's own config file is text and
 * ordinary file I/O; Prf* is for OS2.INI/OS2SYS.INI only.
 *------------------------------------------------------------------------*/

#define SEC_SET  "Settings"
#define SEC_VIEW "View"
#define SEC_WIN  "Window"
#define SEC_FIND "Search"

static void LoadSettings(void)
{
    if (!IniLoad(szIniPath))
        return;                            /* first run - defaults stand */

    settings.iTabWidth           = (SHORT)IniGetInt(SEC_SET, "TabWidth", settings.iTabWidth);
    settings.iIndentWidth        = (SHORT)IniGetInt(SEC_SET, "IndentWidth", settings.iIndentWidth);
    settings.bTabsAsSpaces       = IniGetInt(SEC_SET, "TabsAsSpaces", settings.bTabsAsSpaces);
    settings.bTabIndents         = IniGetInt(SEC_SET, "TabIndents", settings.bTabIndents);
    settings.bBackspaceUnindents = IniGetInt(SEC_SET, "BackspaceUnindents", settings.bBackspaceUnindents);
    settings.iLongLinesLimit     = (SHORT)IniGetInt(SEC_SET, "LongLinesLimit", settings.iLongLinesLimit);
    settings.bLongLineBackground = IniGetInt(SEC_SET, "LongLineMode", settings.bLongLineBackground);
    settings.iWordWrapMode       = (SHORT)IniGetInt(SEC_SET, "WordWrapMode", settings.iWordWrapMode);
    settings.iWordWrapIndent     = (SHORT)IniGetInt(SEC_SET, "WordWrapIndent", settings.iWordWrapIndent);
    settings.iWordWrapSymbols    = (SHORT)IniGetInt(SEC_SET, "WordWrapSymbols", settings.iWordWrapSymbols);
    settings.bShowWordWrapSymbols = IniGetInt(SEC_SET, "ShowWordWrapSymbols", settings.bShowWordWrapSymbols);
    IniGetStr(SEC_SET, "FontFace", szFontFace, szFontFace, sizeof(szFontFace));
    IniGetStr(SEC_SET, "BrowseDir", "", szBrowseDir, sizeof(szBrowseDir));
    iFontSize = IniGetInt(SEC_SET, "FontSize", iFontSize);

    bWordWrap        = IniGetInt(SEC_VIEW, "WordWrap", bWordWrap);
    bLineNumbers     = IniGetInt(SEC_VIEW, "LineNumbers", bLineNumbers);
    bLongLineMarker  = IniGetInt(SEC_VIEW, "LongLineMarker", bLongLineMarker);
    bIndentGuides    = IniGetInt(SEC_VIEW, "IndentGuides", bIndentGuides);
    bShowWhitespace  = IniGetInt(SEC_VIEW, "ShowWhitespace", bShowWhitespace);
    bShowEOLs        = IniGetInt(SEC_VIEW, "ShowEOLs", bShowEOLs);
    bHiliteCurLine   = IniGetInt(SEC_VIEW, "HighlightCurrentLine", bHiliteCurLine);
    bSelMargin       = IniGetInt(SEC_VIEW, "SelectionMargin", bSelMargin);
    bFolding         = IniGetInt(SEC_VIEW, "CodeFolding", bFolding);
    bAutoIndent      = IniGetInt(SEC_VIEW, "AutoIndent", bAutoIndent);
    iMarkOccurrences = IniGetInt(SEC_VIEW, "MarkOccurrences", iMarkOccurrences);
    bMarkOccCase     = IniGetInt(SEC_VIEW, "MarkOccurrencesMatchCase", bMarkOccCase);
    bMarkOccWord     = IniGetInt(SEC_VIEW, "MarkOccurrencesMatchWords", bMarkOccWord);
    bSuppressEOLChanged = IniGetInt(SEC_VIEW, "SuppressEOLMessage", bSuppressEOLChanged);
    iEncoding           = IniGetInt(SEC_VIEW, "DefaultEncoding", iEncoding);
    bStatusbar          = IniGetInt(SEC_VIEW, "Statusbar", bStatusbar);

    swpSaved.x  = IniGetInt(SEC_WIN, "X",  -1);
    swpSaved.y  = IniGetInt(SEC_WIN, "Y",  -1);
    swpSaved.cx = IniGetInt(SEC_WIN, "CX", -1);
    swpSaved.cy = IniGetInt(SEC_WIN, "CY", -1);
    bHaveSavedPos = (BOOL)(swpSaved.cx > 0 && swpSaved.cy > 0);

    {
        int i;
        for (i = 0; i < Style_SlotCount(); i++) {
            CHAR szKey[64], szVal[80];
            unsigned long clr = 0;
            int bold = 0;
            sprintf(szKey, "Slot%d", i);
            IniGetStr("Colours", szKey, "", szVal, sizeof(szVal));
            if (szVal[0] && sscanf(szVal, "%lX,%d", &clr, &bold) == 2)
                Style_SetSlot(i, (LONG)clr, (BOOL)bold);
        }
    }

    cMru = 0;
    {
        int i;
        for (i = 0; i < NP2_MRU_MAX; i++) {
            CHAR szKey[32], szVal[CCHMAXPATH];
            sprintf(szKey, "File%d", i);
            IniGetStr("Recent", szKey, "", szVal, sizeof(szVal));
            if (!szVal[0])
                break;
            strcpy(aMru[cMru++], szVal);
        }
    }

    IniGetStr(SEC_FIND, "Find", "", efrData.szFind, sizeof(efrData.szFind));
    IniGetStr(SEC_FIND, "Replace", "", efrData.szReplace, sizeof(efrData.szReplace));
    efrData.fuFlags      = IniGetInt(SEC_FIND, "Flags", 0);
    efrData.bTransformBS = IniGetInt(SEC_FIND, "TransformBackslashes", 0);
    efrData.bNoFindWrap  = IniGetInt(SEC_FIND, "NoWrap", 0);

    IniFree();
}

static void SaveSettings(HWND hwndFrame)
{
    SWP swp;

    if (!IniBeginWrite(szIniPath))
        return;

    IniWriteSection(SEC_SET);
    IniWriteInt("TabWidth",            settings.iTabWidth);
    IniWriteInt("IndentWidth",         settings.iIndentWidth);
    IniWriteInt("TabsAsSpaces",        settings.bTabsAsSpaces);
    IniWriteInt("TabIndents",          settings.bTabIndents);
    IniWriteInt("BackspaceUnindents",  settings.bBackspaceUnindents);
    IniWriteInt("LongLinesLimit",      settings.iLongLinesLimit);
    IniWriteInt("LongLineMode",        settings.bLongLineBackground);
    IniWriteInt("WordWrapMode",        settings.iWordWrapMode);
    IniWriteInt("WordWrapIndent",      settings.iWordWrapIndent);
    IniWriteInt("WordWrapSymbols",     settings.iWordWrapSymbols);
    IniWriteInt("ShowWordWrapSymbols", settings.bShowWordWrapSymbols);
    IniWriteStr("FontFace",            szFontFace);
    IniWriteStr("BrowseDir",           szBrowseDir);
    IniWriteInt("FontSize",            iFontSize);

    IniWriteSection(SEC_VIEW);
    IniWriteInt("WordWrap",                  bWordWrap);
    IniWriteInt("LineNumbers",               bLineNumbers);
    IniWriteInt("LongLineMarker",            bLongLineMarker);
    IniWriteInt("IndentGuides",              bIndentGuides);
    IniWriteInt("ShowWhitespace",            bShowWhitespace);
    IniWriteInt("ShowEOLs",                  bShowEOLs);
    IniWriteInt("HighlightCurrentLine",      bHiliteCurLine);
    IniWriteInt("SelectionMargin",           bSelMargin);
    IniWriteInt("CodeFolding",               bFolding);
    IniWriteInt("AutoIndent",                bAutoIndent);
    IniWriteInt("MarkOccurrences",           iMarkOccurrences);
    IniWriteInt("MarkOccurrencesMatchCase",  bMarkOccCase);
    IniWriteInt("MarkOccurrencesMatchWords", bMarkOccWord);
    IniWriteInt("SuppressEOLMessage",        bSuppressEOLChanged);
    IniWriteInt("DefaultEncoding",           iEncoding);
    IniWriteInt("Statusbar",                 bStatusbar);

    /* Read the frame's position back rather than tracking it: WinQueryWindowPos
     * fills an SWP whose field order is (fl, cy, cx, y, x) - reversed from
     * WinSetWindowPos's arguments - so it is assigned by NAME here, never
     * positionally [os2ref/pm-window-messaging.md]. */
    if (hwndFrame != NULLHANDLE && WinQueryWindowPos(hwndFrame, &swp)) {
        IniWriteSection(SEC_WIN);
        IniWriteInt("X",  swp.x);
        IniWriteInt("Y",  swp.y);
        IniWriteInt("CX", swp.cx);
        IniWriteInt("CY", swp.cy);
    }

    /* The palette: one line per semantic slot. Written as name=colour,bold so
     * a hand edit is possible without a decoder ring. */
    IniWriteSection("Colours");
    {
        int i;
        for (i = 0; i < Style_SlotCount(); i++) {
            CHAR szKey[64], szVal[64];
            sprintf(szKey, "Slot%d", i);
            sprintf(szVal, "%06lX,%d,%s", (unsigned long)Style_SlotColour(i),
                    Style_SlotBold(i) ? 1 : 0, Style_SlotName(i));
            IniWriteStr(szKey, szVal);
        }
    }

    IniWriteSection("Recent");
    {
        int i;
        for (i = 0; i < cMru; i++) {
            CHAR szKey[32];
            sprintf(szKey, "File%d", i);
            IniWriteStr(szKey, aMru[i]);
        }
    }

    IniWriteSection(SEC_FIND);
    IniWriteStr("Find",                 efrData.szFind);
    IniWriteStr("Replace",              efrData.szReplace);
    IniWriteInt("Flags",                (int)efrData.fuFlags);
    IniWriteInt("TransformBackslashes", efrData.bTransformBS);
    IniWriteInt("NoWrap",               efrData.bNoFindWrap);

    IniEndWrite();
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
            { IDM_STATUSBAR,      &bStatusbar      },
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

        for (i = 0; i < NP2ENC_COUNT; i++)
            WinSendMsg(hwndMenu, MM_SETITEMATTR,
                       MPFROM2SHORT(IDM_ENC_ANSI + i, TRUE),
                       MPFROM2SHORT(MIA_CHECKED, (iEncoding == i) ? MIA_CHECKED : 0));

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
        {
            /* Four panels: caret position, selection, encoding + line endings,
             * and document mode. SS_TEXT takes its alignment from the DT_*
             * flags in the low style byte [os2ref/pm-controls.md 6]. */
            static const ULONG aId[4] = { IDC_STATUS_POS, IDC_STATUS_SEL,
                                          IDC_STATUS_ENC, IDC_STATUS_MODE };
            int i;
            for (i = 0; i < 4; i++)
                hwndStatus[i] = WinCreateWindow(hwnd, (PSZ)WC_STATIC, (PSZ)"",
                        WS_VISIBLE | SS_TEXT | DT_LEFT | DT_VCENTER,
                        0, 0, 0, 0, hwnd, HWND_TOP, aId[i], NULL, NULL);
            /* Size the bar from the font, not a guess: a bigger system font
             * must not clip the text. */
            {
                HPS hps = WinGetPS(hwnd);
                FONTMETRICS fm;
                if (hps != NULLHANDLE) {
                    if (GpiQueryFontMetrics(hps, sizeof(fm), &fm))
                        cyStatus = fm.lMaxBaselineExt + 6;
                    WinReleasePS(hps);
                }
                if (cyStatus < 14) cyStatus = 14;
            }
        }
        return (MRESULT)0;

    case WM_SIZE: {
        LONG cx = (LONG)SHORT1FROMMP(mp2);
        LONG cy = (LONG)SHORT2FROMMP(mp2);
        LONG cyBar = bStatusbar ? cyStatus : 0;
        int i;

        /* Bottom-left origin: the statusbar sits at y = 0 and the editor
         * ABOVE it, which is the opposite of the arithmetic a Win32 layout
         * would use [os2ref/gpi-drawing.md, coordinate origin]. */
        if (hwndSci != NULLHANDLE)
            WinSetWindowPos(hwndSci, HWND_TOP, 0, cyBar, cx, cy - cyBar,
                            SWP_SIZE | SWP_MOVE | SWP_SHOW);
        for (i = 0; i < 4; i++) {
            if (hwndStatus[i] == NULLHANDLE)
                continue;
            if (!bStatusbar) {
                WinShowWindow(hwndStatus[i], FALSE);
                continue;
            }
            {
                LONG w = cx / 4;
                LONG x = w * i;
                if (i == 3) w = cx - x;          /* last panel takes the rest */
                WinSetWindowPos(hwndStatus[i], HWND_TOP, x + 4, 2, w - 6, cyBar - 4,
                                SWP_SIZE | SWP_MOVE | SWP_SHOW);
            }
        }
        return (MRESULT)0;
    }

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
        /* ScintillaPM multiplexes TWO different notifications through
         * WM_CONTROL, and mp2 means something different in each:
         *
         *   NotifyParent()  code 0            mp2 = SCNotification *
         *   NotifyChange()  code SCEN_CHANGE  mp2 = the control's HWND
         *
         * So the notify code in mp1's high half must be checked BEFORE mp2 is
         * dereferenced - casting an HWND to SCNotification* and reading
         * ->nmhdr.code is a segfault. It stays dormant while the guarding
         * feature is off, which is how this survived until Mark Occurrences
         * was first restored from the settings file as non-zero. */
        if (SHORT1FROMMP(mp1) == 2000 && SHORT2FROMMP(mp1) == 0 && iMarkOccurrences) {
            SCNotification *pscn = (SCNotification *)PVOIDFROMMP(mp2);
            if (pscn && pscn->nmhdr.code == SCN_UPDATEUI) {
                EditMarkAll(hwndSci, iMarkOccurrences, bMarkOccCase, bMarkOccWord);
                UpdateStatusbar();
            }
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
        case IDM_STATUSBAR: {
            RECTL rcl;
            bStatusbar = !bStatusbar;
            /* Re-run the layout: WM_SIZE owns it, so ask for one rather than
             * duplicating the arithmetic here. */
            WinQueryWindowRect(hwnd, &rcl);
            WinSendMsg(hwnd, WM_SIZE, 0,
                       MPFROM2SHORT((SHORT)(rcl.xRight - rcl.xLeft),
                                    (SHORT)(rcl.yTop - rcl.yBottom)));
            UpdateStatusbar();
            SyncMenu(hwndFrame);
            break;
        }

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

        /* --- Encoding ------------------------------------------------------ */
        case IDM_ENC_ANSI:    case IDM_ENC_OEM:     case IDM_ENC_UTF8:
        case IDM_ENC_UTF8SIG: case IDM_ENC_UCS2LE:  case IDM_ENC_UCS2BE:
            /* Changes what the NEXT save writes; the buffer is already UTF-8
             * internally, so nothing needs re-decoding here. */
            iEncoding = idCmd - IDM_ENC_ANSI;
            sprintf(szStatus, "encoding set to %s", EncName(iEncoding));
            ShowStatus();
            SyncMenu(hwndFrame);
            break;

        /* Reload As: re-read the same file, overriding detection. */
        case IDM_RELOAD_ANSI: case IDM_RELOAD_OEM: case IDM_RELOAD_UTF8:
            if (!szFileName[0]) {
                WinMessageBox(HWND_DESKTOP, hwnd, (PSZ)"Nothing to reload.",
                              (PSZ)"Reload", 0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
                break;
            }
            if (ConfirmDiscard(hwnd)) {
                CHAR szKeep[CCHMAXPATH];
                strcpy(szKeep, szFileName);
                iForceEncoding = (idCmd == IDM_RELOAD_ANSI) ? NP2ENC_ANSI :
                                 (idCmd == IDM_RELOAD_OEM)  ? NP2ENC_OEM  : NP2ENC_UTF8;
                szStatus[0] = '\0';
                LoadFile(hwnd, (PSZ)szKeep);
                ShowStatus();
                SyncMenu(hwndFrame);
                WinInvalidateRect(hwndSci, NULL, TRUE);
            }
            break;

        /* --- Launch -------------------------------------------------------- */
        case IDM_NEWWINDOW:
            /* A second editor on the current file. Independent, not a child
             * session, so it outlives this one [os2ref/session-manager.md]. */
            RunProgram(hwnd, szExePath, szFileName[0] ? szFileName : NULL, TRUE);
            break;
        case IDM_EMPTYWINDOW:
            RunProgram(hwnd, szExePath, NULL, TRUE);
            break;
        case IDM_EXECDOC:
            RunOpenDocument(hwnd, szFileName);
            break;
        case IDM_RUNCMD:
            if (RunCommandDlg(hwnd, szRunCmd, sizeof(szRunCmd)) && szRunCmd[0]) {
                /* Split the command from its arguments at the first blank -
                 * DosStartSession takes them separately, unlike a shell. */
                CHAR szPgm[CCHMAXPATH];
                const char *pArgs = NULL;
                char *pSpace;
                strncpy(szPgm, szRunCmd, sizeof(szPgm) - 1);
                szPgm[sizeof(szPgm) - 1] = '\0';
                pSpace = strchr(szPgm, ' ');
                if (pSpace) {
                    *pSpace = '\0';
                    pArgs = szRunCmd + (pSpace - szPgm) + 1;
                }
                RunProgram(hwnd, szPgm, pArgs, FALSE);
            }
            break;

        /* --- Syntax scheme and font ---------------------------------------- */
        case IDM_PRINT:
            PrintDocument(hwnd, hwndSci,
                          szFileName[0] ? szFileName : "Untitled",
                          szFontFace, iFontSize);
            break;

        case IDM_PAGESETUP:
            PrintSetup(hwnd);
            break;

        case IDM_BROWSE: {
            CHAR szPick[CCHMAXPATH];
            if (BrowseDlg(hwnd, szBrowseDir, sizeof(szBrowseDir),
                          szPick, sizeof(szPick))) {
                if (ConfirmDiscard(hwnd)) {
                    szStatus[0] = '\0';
                    LoadFile(hwnd, (PSZ)szPick);
                    ShowStatus();
                    SyncMenu(hwndFrame);
                    WinInvalidateRect(hwndSci, NULL, TRUE);
                }
            }
            break;
        }

        case IDM_RECENT: {
            CHAR szPick[CCHMAXPATH];
            if (EditRecentDlg(hwnd, aMru, &cMru, szPick, sizeof(szPick))) {
                if (ConfirmDiscard(hwnd)) {
                    szStatus[0] = '\0';
                    LoadFile(hwnd, (PSZ)szPick);
                    ShowStatus();
                    SyncMenu(hwndFrame);
                    WinInvalidateRect(hwndSci, NULL, TRUE);
                }
            }
            break;
        }

        case IDM_SCHEMECONFIG:
            if (EditSchemeConfigDlg(hwnd))
                ApplyScheme(hwndFrame);      /* re-apply so it is visible now */
            break;

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

int main(int argc, char *argv[])
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

    /* Settings live beside the .EXE - OS/2 is single-seat and has no per-user
     * application-data directory [recipes/porting-a-windows-app.md 7.1]. */
    /* argv[0] is whatever the shell typed - "./np2.exe" from a kLIBC shell -
     * and DosStartSession rejects that with ERROR_SMG_INVALID_CALL (418)
     * rather than resolving it. kLIBC's _execname gives the real, fully
     * qualified path of the running program; argv[0] is only a fallback. */
    if (_execname(szExePath, sizeof(szExePath)) != 0) {
        szExePath[0] = '\0';
        if (argc > 0 && argv[0]) {
            strncpy(szExePath, argv[0], sizeof(szExePath) - 1);
            szExePath[sizeof(szExePath) - 1] = '\0';
        }
    }
    IniResolvePath(szExePath[0] ? szExePath : (argc > 0 ? argv[0] : NULL),
                   szIniPath, sizeof(szIniPath));
    LoadSettings();

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

    /* A file named on the command line replaces the starter text, and picks
     * the scheme the same way File/Open does. */
    if (argc > 1 && argv[1] && argv[1][0]) {
        if (LoadFile(hwndClient, (PSZ)argv[1]))
            iScheme = Style_MatchFromFile(szFileName);
    }

    Style_Apply(hwndSci, iScheme, szFontFace, iFontSize);
    ApplyView();
    SyncMenu(hwndFrame);
    ShowStatus();

    /* Restore the saved geometry if there is one. SWP is assigned by field
     * name throughout - its declaration order (fl, cy, cx, y, x) is the
     * reverse of WinSetWindowPos's arguments [os2ref/pm-window-messaging.md]. */
    if (bHaveSavedPos)
        WinSetWindowPos(hwndFrame, HWND_TOP,
                        swpSaved.x, swpSaved.y, swpSaved.cx, swpSaved.cy,
                        SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);
    else
        WinSetWindowPos(hwndFrame, HWND_TOP, 40, 40, 720, 440,
                        SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);

    /* One loop drives both the frame and the modeless Find dialog. PM needs no
     * IsDialogMessage equivalent: the dialog is an ordinary window in this
     * queue, and WinDefDlgProc handles its tabbing and default-button logic
     * when WinDispatchMsg delivers to it. */
    while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0))
        WinDispatchMsg(hab, &qmsg);

    /* Save before the frame is destroyed - WinQueryWindowPos needs it alive. */
    SaveSettings(hwndFrame);

    if (EditFindReplaceHwnd() != NULLHANDLE)
        WinDestroyWindow(EditFindReplaceHwnd());
    WinDestroyWindow(hwndFrame);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}

/* np2watch.c - file change notification by polling. See np2watch.h. */

#define INCL_WIN
#define INCL_DOS
#define INCL_DOSERRORS   /* NO_ERROR - INCL_DOS does not imply it */
#include <os2.h>
#include <string.h>
#include <stdio.h>

#include "np2.h"
#include "np2watch.h"

/* Notepad2's two defaults, both 2000ms: how often to look, and how long to let
 * a file settle before an automatic reload. The second one matters - a program
 * writing a large file is observed mid-write, and reloading immediately shows
 * a truncated document. Waiting for the size and timestamp to stop moving is
 * what makes auto-reload usable on a log file. */
#define WATCH_INTERVAL_MS   2000
#define WATCH_SETTLE_MS     2000

static int  iWatchMode        = FILEWATCH_NONE;
static BOOL bResetOnNewFile   = TRUE;

static BOOL bRunning          = FALSE;
static CHAR szWatched[CCHMAXPATH] = "";

/* The stamp we compare against, and whether the file existed when it was taken. */
static FDATE fdateLast;
static FTIME ftimeLast;
static ULONG cbLast;
static BOOL  bExistedLast;

/* Set when a change is first seen in AUTORELOAD mode; the reload is held until
 * the file has been quiet for WATCH_SETTLE_MS. 0 means "not pending". */
static ULONG ulChangeSeen;

int  FileWatchMode(void)          { return iWatchMode; }
BOOL FileWatchResetOnNewFile(void){ return bResetOnNewFile; }
BOOL FileWatchRunning(void)       { return bRunning; }

void FileWatchSetOptions(int iMode, BOOL bReset)
{
    if (iMode < FILEWATCH_NONE || iMode > FILEWATCH_AUTORELOAD)
        iMode = FILEWATCH_NONE;
    iWatchMode      = iMode;
    bResetOnNewFile = bReset;
}

/* Take a stamp. Returns FALSE when the file is not there, which is a state the
 * caller cares about rather than an error - a deleted file is a change. */
static BOOL Stamp(const char *pszFile, FDATE *pd, FTIME *pt, ULONG *pcb)
{
    FILESTATUS3 fs3;

    if (DosQueryPathInfo((PSZ)pszFile, FIL_STANDARD, &fs3, sizeof(fs3)) != NO_ERROR) {
        memset(pd, 0, sizeof(*pd));
        memset(pt, 0, sizeof(*pt));
        *pcb = 0;
        return FALSE;
    }
    *pd  = fs3.fdateLastWrite;
    *pt  = fs3.ftimeLastWrite;
    *pcb = fs3.cbFile;
    return TRUE;
}

/* FDATE and FTIME are bitfield structs, so they compare by bytes rather than
 * with an operator - the OS/2 equivalent of CompareFileTime. */
static BOOL SameStamp(FDATE d1, FTIME t1, ULONG cb1,
                      FDATE d2, FTIME t2, ULONG cb2)
{
    return (memcmp(&d1, &d2, sizeof(FDATE)) == 0 &&
            memcmp(&t1, &t2, sizeof(FTIME)) == 0 &&
            cb1 == cb2);
}

void FileWatchInstall(HWND hwndClient, const char *pszFile)
{
    HAB hab = WinQueryAnchorBlock(hwndClient);

    if (iWatchMode == FILEWATCH_NONE || pszFile == NULL || pszFile[0] == '\0') {
        if (bRunning) {
            WinStopTimer(hab, hwndClient, IDT_WATCH);
            bRunning = FALSE;
        }
        szWatched[0] = '\0';
        ulChangeSeen = 0;
        return;
    }

    if (!bRunning) {
        /* WinStartTimer returns FALSE when the system timer pool is exhausted -
         * PM has a finite number - so a silent failure here would look like a
         * feature that simply does nothing. */
        if (!WinStartTimer(hab, hwndClient, IDT_WATCH, WATCH_INTERVAL_MS))
            return;
        bRunning = TRUE;
    }

    strcpy(szWatched, pszFile);
    bExistedLast = Stamp(szWatched, &fdateLast, &ftimeLast, &cbLast);
    ulChangeSeen = 0;
}

void FileWatchOnNewFile(HWND hwndClient, const char *pszFile, BOOL bReload)
{
    if (!bReload && bResetOnNewFile)
        iWatchMode = FILEWATCH_NONE;
    FileWatchInstall(hwndClient, pszFile);
}

void FileWatchTick(HWND hwndClient)
{
    FDATE d;
    FTIME t;
    ULONG cb;
    BOOL  bExists;

    if (!bRunning || !szWatched[0])
        return;

    bExists = Stamp(szWatched, &d, &t, &cb);

    /* A reload is pending: hold it until the file stops moving. Any further
     * change restarts the clock, so a file being written continuously is
     * reloaded once it is finished rather than repeatedly while it grows. */
    if (ulChangeSeen != 0) {
        if (!SameStamp(d, t, cb, fdateLast, ftimeLast, cbLast)) {
            fdateLast = d; ftimeLast = t; cbLast = cb; bExistedLast = bExists;
            ulChangeSeen = WinGetCurrentTime(WinQueryAnchorBlock(hwndClient));
            return;
        }
        if (WinGetCurrentTime(WinQueryAnchorBlock(hwndClient)) - ulChangeSeen
                > WATCH_SETTLE_MS) {
            ulChangeSeen = 0;
            WinPostMsg(hwndClient, WM_CHANGENOTIFY, 0, 0);
        }
        return;
    }

    if (bExists == bExistedLast &&
        SameStamp(d, t, cb, fdateLast, ftimeLast, cbLast))
        return;

    fdateLast = d; ftimeLast = t; cbLast = cb; bExistedLast = bExists;

    /* Mode 1 asks, so it goes to the window at once. Mode 2 reloads without
     * asking, so it waits for the file to settle first. */
    if (iWatchMode == FILEWATCH_AUTORELOAD && bExists)
        ulChangeSeen = WinGetCurrentTime(WinQueryAnchorBlock(hwndClient));
    else
        WinPostMsg(hwndClient, WM_CHANGENOTIFY, 0, 0);
}

/* ---- the dialog ---------------------------------------------------------- */

static MRESULT EXPENTRY ChangeNotifyDlgProc(HWND hwnd, ULONG msg,
                                            MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
    case WM_INITDLG:
        WinCheckButton(hwnd, IDC_WATCH_NONE + iWatchMode, TRUE);
        WinCheckButton(hwnd, IDC_WATCH_RESET, bResetOnNewFile);
        return (MRESULT)FALSE;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK: {
            int i;
            for (i = 0; i <= FILEWATCH_AUTORELOAD; i++)
                if (WinQueryButtonCheckstate(hwnd, IDC_WATCH_NONE + i))
                    iWatchMode = i;
            bResetOnNewFile = WinQueryButtonCheckstate(hwnd, IDC_WATCH_RESET) ? TRUE : FALSE;
            WinDismissDlg(hwnd, DID_OK);
            return (MRESULT)0;
        }
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        break;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL ChangeNotifyDlg(HWND hwndOwner)
{
    HWND hwndDlg = WinLoadDlg(HWND_DESKTOP, hwndOwner, ChangeNotifyDlgProc,
                              NULLHANDLE, IDD_CHANGENOTIFY, NULL);
    if (hwndDlg == NULLHANDLE) {
        CHAR szMsg[128];
        sprintf(szMsg, "WinLoadDlg failed for dialog %d - WinGetLastError = 0x%lX",
                IDD_CHANGENOTIFY,
                (unsigned long)WinGetLastError(WinQueryAnchorBlock(hwndOwner)));
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szMsg, (PSZ)"Change Notify",
                      0, MB_OK | MB_ERROR);
        return FALSE;
    }
    {
        BOOL bOK = (WinProcessDlg(hwndDlg) == DID_OK);
        WinDestroyWindow(hwndDlg);
        return bOK;
    }
}

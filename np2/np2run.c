/* np2run.c - Launch: new window, execute document, run command.
 *
 * DosStartSession per os2ref/session-manager.md. Two details from there that
 * are easy to get wrong:
 *
 *  - The structure's size field is called `Length`, and a wrong value is
 *    rejected with ERROR_SMG_INVALID_DATA_LENGTH (461) rather than ignored.
 *  - `Related = SSF_RELATED_INDEPENDENT` means the session is *not* a child,
 *    and the session/process ids are then NOT returned. That is what we want
 *    here - a second editor should outlive the one that launched it - so the
 *    ids are deliberately unused rather than read back.
 *
 * STARTDATA also carries ObjectBuffer/ObjectBuffLen, which the system fills
 * with the name of the module that could not be loaded. Reporting that is the
 * difference between "it didn't start" and "DOSCALL1.DLL was missing".
 */
#define INCL_WIN
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_DOSSESMGR
#define INCL_WINWORKPLACE
#include <os2.h>
#include <stdio.h>
#include <string.h>

#include "np2.h"
#include "np2run.h"
#include "pmhelpers.h"

/* WinOpenObject's view constants live in the Toolkit's WPS header wpobject.h,
 * which kLIBC's os2emx.h does not ship - grepping os2emx.h alone finds nothing
 * and reads as "no such constant". Value and name from
 * <Toolkit>/h/wpobject.h:200; the documented view set (OPEN_SETTINGS,
 * OPEN_TREE, OPEN_DEFAULT, OPEN_CONTENTS, OPEN_DETAILS) is in pm2.txt under
 * WinOpenObject. */
#ifndef OPEN_DEFAULT
#define OPEN_DEFAULT  0
#endif
/* OPEN_SETTINGS is 2 - MEASURED, not guessed: wpobject.h is not on this system,
 * so a probe called WinOpenObject with successive view numbers and the one that
 * raised the "<file> - Properties" settings notebook was 2. IBM documents the
 * name and meaning ("Open Settings notebook") in wps2.txt under wpOpen, but not
 * the value. */
#ifndef OPEN_SETTINGS
#define OPEN_SETTINGS 2
#endif

BOOL RunProgram(HWND hwndOwner, const char *pszPgm, const char *pszArgs, BOOL bPM)
{
    STARTDATA sd;
    CHAR      szObj[CCHMAXPATH] = "";
    ULONG     ulSession = 0;
    PID       pid = 0;
    APIRET    rc;

    if (!pszPgm || !pszPgm[0])
        return FALSE;

    memset(&sd, 0, sizeof(sd));
    sd.Length        = sizeof(STARTDATA);
    sd.Related       = SSF_RELATED_INDEPENDENT;
    sd.FgBg          = SSF_FGBG_FORE;
    sd.TraceOpt      = SSF_TRACEOPT_NONE;
    sd.PgmTitle      = NULL;
    sd.PgmName       = (PSZ)pszPgm;
    sd.PgmInputs     = (PBYTE)(pszArgs && pszArgs[0] ? pszArgs : NULL);
    sd.TermQ         = NULL;
    sd.Environment   = NULL;
    sd.InheritOpt    = SSF_INHERTOPT_PARENT;
    sd.SessionType   = bPM ? SSF_TYPE_PM : SSF_TYPE_WINDOWABLEVIO;
    sd.IconFile      = NULL;
    sd.PgmHandle     = 0;
    sd.PgmControl    = SSF_CONTROL_VISIBLE;
    sd.ObjectBuffer  = (PSZ)szObj;
    sd.ObjectBuffLen = sizeof(szObj);

    rc = DosStartSession(&sd, &ulSession, &pid);

    /* 457 is "started, but in the background because nothing in this chain is
     * in the foreground" - a success, not a failure. */
    if (rc == NO_ERROR || rc == ERROR_SMG_START_IN_BACKGROUND)
        return TRUE;

    {
        CHAR szMsg[CCHMAXPATH + 160];
        sprintf(szMsg, "Could not start:\n%s\n\nDosStartSession rc=%lu%s%s",
                pszPgm, (unsigned long)rc,
                szObj[0] ? "\nFailing module: " : "",
                szObj[0] ? szObj : "");
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szMsg, (PSZ)"Launch",
                      0, MB_OK | MB_ERROR | MB_MOVEABLE);
    }
    return FALSE;
}

/* The Workplace Shell is OS/2's "associated application": WinQueryObject
 * accepts a file-system path as an object id and returns its HOBJECT, and
 * WinOpenObject then opens it in its default view - exactly what double
 * clicking it on the desktop does [os2ref/wps-classes.md]. */
/* The Workplace Shell's settings notebook is OS/2's answer to a Win32 shell
 * property sheet: same information, same role, reached the same way - by asking
 * the shell for the object and telling it which view to open. */
BOOL RunObjectSettings(HWND hwndOwner, const char *pszFile)
{
    HOBJECT hobj;
    if (!pszFile || !pszFile[0])
        return FALSE;
    hobj = WinQueryObject((PCSZ)pszFile);
    if (hobj == NULLHANDLE)
        return FALSE;
    (void)hwndOwner;
    return WinOpenObject(hobj, OPEN_SETTINGS, TRUE);
}

BOOL RunOpenDocument(HWND hwndOwner, const char *pszFile)
{
    HOBJECT hobj;

    if (!pszFile || !pszFile[0]) {
        WinMessageBox(HWND_DESKTOP, hwndOwner,
                      (PSZ)"Save the document first - there is no file to execute.",
                      (PSZ)"Launch", 0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
        return FALSE;
    }

    hobj = WinQueryObject((PCSZ)pszFile);
    if (hobj != NULLHANDLE && WinOpenObject(hobj, OPEN_DEFAULT, TRUE))
        return TRUE;

    /* No WPS object, or it refused: fall back to running it as a program,
     * which is right for .EXE and .CMD and reports honestly for anything
     * else rather than failing silently. */
    return RunProgram(hwndOwner, pszFile, NULL, FALSE);
}

/*--------------------------------------------------------------------------
 * The Run dialog
 *------------------------------------------------------------------------*/

typedef struct _runarg { char *psz; int cch; } RUNARG;

static MRESULT EXPENTRY RunDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static RUNARG *pra;

    switch (msg) {
    case WM_INITDLG:
        pra = (RUNARG *)PVOIDFROMMP(mp2);
        WinSendDlgItemMsg(hwnd, IDC_RUNCMD, EM_SETTEXTLIMIT, MPFROMSHORT(255), 0);
        WinSetDlgItemText(hwnd, IDC_RUNCMD, (PSZ)pra->psz);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        PMFocusDlgItem(hwnd, IDC_RUNCMD);
        return (MRESULT)FALSE;      /* inverted from Win32 - PM sets the focus */

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK:
            WinQueryDlgItemText(hwnd, IDC_RUNCMD, pra->cch, (PSZ)pra->psz);
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

BOOL RunCommandDlg(HWND hwndOwner, char *pszCmd, int cchCmd)
{
    RUNARG ra;
    ra.psz = pszCmd;
    ra.cch = cchCmd;
    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, RunDlgProc, NULLHANDLE,
                            IDD_RUN, &ra) == DID_OK);
}

/* ---- One running instance -------------------------------------------------
 *
 * Win32 finds the existing window with FindWindow and hands it the filename in
 * a WM_COPYDATA. PM has neither: WinPostMsg carries two MPARAMs and nothing
 * else, and a pointer in one of them is meaningless in another process.
 *
 * The OS/2 route is the SYSTEM ATOM TABLE, which "can be accessed by any
 * process in the system ... created at boot time and cannot be destroyed"
 * [DOC-IBM - pm2.txt, WinQuerySystemAtomTable Remarks]. An atom is a string
 * with a system-wide integer name, so the filename goes in as an atom and only
 * the ATOM travels in the message. The receiver reads the name back and deletes
 * the atom, which is what releases it.
 */

/* Find a running instance's client window, or NULLHANDLE. Must be called
 * BEFORE this process creates its own frame, so there is nothing to skip. */
HWND Np2FindInstance(const char *pszClientClass)
{
    HENUM  henum;
    HWND   hwndFrame, hwndFound = NULLHANDLE;
    CHAR   szClass[64];

    henum = WinBeginEnumWindows(HWND_DESKTOP);
    while ((hwndFrame = WinGetNextWindow(henum)) != NULLHANDLE) {
        HWND hwndClient = WinWindowFromID(hwndFrame, FID_CLIENT);
        if (hwndClient == NULLHANDLE)
            continue;
        if (WinQueryClassName(hwndClient, sizeof(szClass), (PCH)szClass) > 0 &&
            strcmp(szClass, pszClientClass) == 0) {
            hwndFound = hwndClient;
            break;
        }
    }
    WinEndEnumWindows(henum);
    return hwndFound;
}

/* Hand a file to that instance and raise it. The atom is deleted by the
 * receiver, not here: this process is about to exit. */
BOOL Np2HandOffFile(HWND hwndClient, ULONG msg, const char *pszFile)
{
    HATOMTBL hatomtbl = WinQuerySystemAtomTable();
    ATOM     atom;

    if (hwndClient == NULLHANDLE || !pszFile || !pszFile[0])
        return FALSE;
    atom = WinAddAtom(hatomtbl, (PCSZ)pszFile);
    if (atom == 0)
        return FALSE;
    if (!WinPostMsg(hwndClient, msg, MPFROMLONG((LONG)atom), 0)) {
        WinDeleteAtom(hatomtbl, atom);
        return FALSE;
    }
    WinSetActiveWindow(HWND_DESKTOP, WinQueryWindow(hwndClient, QW_PARENT));
    return TRUE;
}

/* Receiver side: recover the filename from the atom and release it. */
BOOL Np2TakeHandOff(ULONG atomValue, char *pszOut, int cchOut)
{
    HATOMTBL hatomtbl = WinQuerySystemAtomTable();
    ATOM     atom = (ATOM)atomValue;
    ULONG    cch;

    if (atom == 0)
        return FALSE;
    cch = WinQueryAtomName(hatomtbl, atom, (PSZ)pszOut, (ULONG)cchOut);
    WinDeleteAtom(hatomtbl, atom);
    return (cch > 0);
}

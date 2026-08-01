/* np2browse.c - a file browser on WC_CONTAINER.
 *
 * The container model, from os2ref/pm-controls.md 15:
 *
 *  - Every item is a RECORD whose first member is a RECORDCORE, or a smaller
 *    MINIRECORDCORE when the container has CCS_MINIRECORDCORE. An application
 *    subclasses it by asking the CONTAINER to allocate the extra bytes -
 *    CM_ALLOCRECORD, not malloc - because the container owns the memory and
 *    frees it with CM_FREERECORD.
 *  - Records are linked in with CM_INSERTRECORD plus a RECORDINSERT.
 *  - The view comes from CNRINFO.flWindowAttr (CV_* | CA_*), set with
 *    CM_SETCNRINFO.
 *
 * The two things that are easy to get wrong, both consequences of the
 * container owning the storage:
 *
 *  - The PSZ fields in a record (pszIcon here) must point at memory that
 *    OUTLIVES the insert. Pointing them at a local buffer leaves the
 *    container displaying freed stack. The extra bytes allocated past the
 *    core are exactly the place to put the text.
 *  - cb in MINIRECORDCORE must be sizeof(MINIRECORDCORE), not the size of the
 *    subclassed record.
 */
#define INCL_WIN
#define INCL_DOS
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#define INCL_WINSTDCNR
#define INCL_WINWORKPLACE
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "np2.h"
#include "np2browse.h"
#include "pmhelpers.h"

/* Our record: the core, then the data we need per entry. */
typedef struct _BROWSEREC {
    MINIRECORDCORE core;
    CHAR  szName[CCHMAXPATH];   /* pszIcon points here - see the header note */
    BOOL  bDir;
} BROWSEREC;

typedef struct _BROWSEARG {
    char *pszDir;
    int   cchDir;
    char *pszPick;
    int   cchPick;
} BROWSEARG;

static BROWSEARG *pbaCur;

/*--------------------------------------------------------------------------
 * Filling the container
 *------------------------------------------------------------------------*/

static void BrowseClear(HWND hwndCnr)
{
    /* NULL + CMA_FREE removes and frees every record in one call. */
    WinSendMsg(hwndCnr, CM_REMOVERECORD, NULL,
               MPFROM2SHORT(0, CMA_FREE | CMA_INVALIDATE));
}

static BROWSEREC *BrowseAlloc(HWND hwndCnr)
{
    /* CM_ALLOCRECORD: mp1 = extra bytes BEYOND the core, mp2 = how many. */
    return (BROWSEREC *)WinSendMsg(hwndCnr, CM_ALLOCRECORD,
               MPFROMLONG(sizeof(BROWSEREC) - sizeof(MINIRECORDCORE)),
               MPFROMSHORT(1));
}

static void BrowseInsert(HWND hwndCnr, BROWSEREC *prec, const char *pszText,
                         BOOL bDir, const char *pszFullPath)
{
    RECORDINSERT ri;

    strncpy(prec->szName, pszText, sizeof(prec->szName) - 1);
    prec->szName[sizeof(prec->szName) - 1] = '\0';
    prec->bDir = bDir;

    prec->core.cb = sizeof(MINIRECORDCORE);      /* the CORE's size, not ours */
    prec->core.flRecordAttr = 0;
    prec->core.pszIcon = (PSZ)prec->szName;      /* lives as long as the record */
    prec->core.hptrIcon = NULLHANDLE;

    /* WinLoadFileIcon gives the WPS icon for a path. fPrivate = FALSE asks for
     * a SHARED pointer, which is what you want when only displaying it
     * [DOC-IBM pm2.txt WinLoadFileIcon]. */
    if (pszFullPath && pszFullPath[0])
        prec->core.hptrIcon = WinLoadFileIcon((PSZ)pszFullPath, FALSE);

    memset(&ri, 0, sizeof(ri));
    ri.cb                = sizeof(RECORDINSERT);
    ri.pRecordOrder      = (PRECORDCORE)CMA_END;
    ri.pRecordParent     = NULL;
    ri.fInvalidateRecord = FALSE;                /* one invalidate at the end */
    ri.zOrder            = (ULONG)CMA_TOP;
    ri.cRecordsInsert    = 1;

    WinSendMsg(hwndCnr, CM_INSERTRECORD, MPFROMP(prec), MPFROMP(&ri));
}

static void BrowseFill(HWND hwnd, const char *pszDir)
{
    HWND    hwndCnr = WinWindowFromID(hwnd, IDC_BROWSECNR);
    CHAR    szMask[CCHMAXPATH];
    CHAR    szFull[CCHMAXPATH];
    HDIR    hdir = HDIR_CREATE;
    ULONG   cFind = 1;
    FILEFINDBUF3 ffb;
    APIRET  rc;

    if (hwndCnr == NULLHANDLE)
        return;

    BrowseClear(hwndCnr);
    WinSetDlgItemText(hwnd, IDC_BROWSEPATH, (PSZ)pszDir);

    /* Parent entry first, unless we are at a drive root. */
    if (strlen(pszDir) > 3) {
        BROWSEREC *prec = BrowseAlloc(hwndCnr);
        if (prec)
            BrowseInsert(hwndCnr, prec, "..", TRUE, NULL);
    }

    sprintf(szMask, "%s%s*", pszDir,
            (pszDir[0] && pszDir[strlen(pszDir) - 1] == '\\') ? "" : "\\");

    /* FILE_DIRECTORY in the attribute mask means "directories AS WELL",
     * not "directories only" [os2ref/file-io.md, DosFindFirst]. */
    rc = DosFindFirst((PSZ)szMask, &hdir,
                      FILE_NORMAL | FILE_DIRECTORY | FILE_READONLY | FILE_ARCHIVED,
                      &ffb, sizeof(ffb), &cFind, FIL_STANDARD);

    while (rc == NO_ERROR) {
        const BOOL bDir = (ffb.attrFile & FILE_DIRECTORY) != 0;
        if (strcmp(ffb.achName, ".") != 0 && strcmp(ffb.achName, "..") != 0) {
            BROWSEREC *prec = BrowseAlloc(hwndCnr);
            if (prec) {
                CHAR szDisplay[CCHMAXPATH];
                sprintf(szDisplay, bDir ? "[%s]" : "%s", ffb.achName);
                sprintf(szFull, "%s%s%s", pszDir,
                        (pszDir[strlen(pszDir) - 1] == '\\') ? "" : "\\",
                        ffb.achName);
                BrowseInsert(hwndCnr, prec, szDisplay, bDir, szFull);
            }
        }
        cFind = 1;
        rc = DosFindNext(hdir, &ffb, sizeof(ffb), &cFind);
    }
    DosFindClose(hdir);

    WinSendMsg(hwndCnr, CM_INVALIDATERECORD, NULL,
               MPFROM2SHORT(0, CMA_ERASE | CMA_REPOSITION));
}

/* Strip the display decoration a directory entry carries. */
static void PlainName(const BROWSEREC *prec, char *pszOut, int cchOut)
{
    const char *p = prec->szName;
    int n;
    if (prec->bDir && p[0] == '[')
        p++;
    strncpy(pszOut, p, cchOut - 1);
    pszOut[cchOut - 1] = '\0';
    n = (int)strlen(pszOut);
    if (prec->bDir && n > 0 && pszOut[n - 1] == ']')
        pszOut[n - 1] = '\0';
}

/* Enter or double-click on a record. */
static void BrowseActivate(HWND hwnd, BROWSEREC *prec)
{
    CHAR szName[CCHMAXPATH], szNew[CCHMAXPATH];

    if (!prec)
        return;
    PlainName(prec, szName, sizeof(szName));

    if (!prec->bDir) {
        sprintf(pbaCur->pszPick, "%s%s%s", pbaCur->pszDir,
                (pbaCur->pszDir[strlen(pbaCur->pszDir) - 1] == '\\') ? "" : "\\",
                szName);
        WinDismissDlg(hwnd, DID_OK);
        return;
    }

    if (strcmp(szName, "..") == 0) {
        char *pSep;
        strcpy(szNew, pbaCur->pszDir);
        {
            int n = (int)strlen(szNew);
            if (n > 3 && szNew[n - 1] == '\\')
                szNew[n - 1] = '\0';
        }
        pSep = strrchr(szNew, '\\');
        if (pSep) {
            if (pSep - szNew <= 2)
                pSep[1] = '\0';          /* keep "C:\" */
            else
                *pSep = '\0';
        }
    } else {
        sprintf(szNew, "%s%s%s", pbaCur->pszDir,
                (pbaCur->pszDir[strlen(pbaCur->pszDir) - 1] == '\\') ? "" : "\\",
                szName);
    }

    strncpy(pbaCur->pszDir, szNew, pbaCur->cchDir - 1);
    pbaCur->pszDir[pbaCur->cchDir - 1] = '\0';
    BrowseFill(hwnd, pbaCur->pszDir);
}

static BROWSEREC *BrowseSelected(HWND hwnd)
{
    HWND hwndCnr = WinWindowFromID(hwnd, IDC_BROWSECNR);
    return (BROWSEREC *)WinSendMsg(hwndCnr, CM_QUERYRECORDEMPHASIS,
               MPFROMP(CMA_FIRST), MPFROMSHORT(CRA_CURSORED));
}

/*--------------------------------------------------------------------------
 * Dialog
 *------------------------------------------------------------------------*/

static MRESULT EXPENTRY BrowseDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
    case WM_INITDLG: {
        HWND    hwndCnr = WinWindowFromID(hwnd, IDC_BROWSECNR);
        CNRINFO ci;

        pbaCur = (BROWSEARG *)PVOIDFROMMP(mp2);

        memset(&ci, 0, sizeof(ci));
        ci.cb = sizeof(CNRINFO);
        /* Name view with mini icons and flowed columns: the closest thing to
         * the listview Notepad2 uses, and it needs no FIELDINFO columns. */
        ci.flWindowAttr = CV_NAME | CV_MINI | CV_FLOW;
        WinSendMsg(hwndCnr, CM_SETCNRINFO, MPFROMP(&ci),
                   MPFROMLONG(CMA_FLWINDOWATTR));

        BrowseFill(hwnd, pbaCur->pszDir);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;
    }

    /* The container reports through WM_CONTROL. CN_ENTER carries a
     * NOTIFYRECORDENTER* in mp2 - a different payload from every other
     * notification, so the code must be checked before dereferencing
     * [os2ref/pm-window-messaging.md]. */
    case WM_CONTROL:
        if (SHORT1FROMMP(mp1) == IDC_BROWSECNR && SHORT2FROMMP(mp1) == CN_ENTER) {
            PNOTIFYRECORDENTER pnre = (PNOTIFYRECORDENTER)PVOIDFROMMP(mp2);
            if (pnre && pnre->pRecord)
                BrowseActivate(hwnd, (BROWSEREC *)pnre->pRecord);
        }
        return (MRESULT)0;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK:
            BrowseActivate(hwnd, BrowseSelected(hwnd));
            return (MRESULT)0;
        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

BOOL BrowseDlg(HWND hwndOwner, char *pszDir, int cchDir, char *pszPick, int cchPick)
{
    BROWSEARG ba;

    if (!pszDir[0]) {
        ULONG ulDrive = 0, ulMap = 0;
        CHAR  szCur[CCHMAXPATH] = "";
        ULONG cb = sizeof(szCur);
        DosQueryCurrentDisk(&ulDrive, &ulMap);
        DosQueryCurrentDir(0, (PBYTE)szCur, &cb);
        sprintf(pszDir, "%c:\\%s", (char)('A' + ulDrive - 1), szCur);
    }

    ba.pszDir  = pszDir;  ba.cchDir  = cchDir;
    ba.pszPick = pszPick; ba.cchPick = cchPick;
    pszPick[0] = '\0';

    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, BrowseDlgProc, NULLHANDLE,
                            IDD_BROWSE, &ba) == DID_OK && pszPick[0] != '\0');
}

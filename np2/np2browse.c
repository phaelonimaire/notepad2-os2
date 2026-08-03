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

/* ---- Resizable-dialog layout -------------------------------------------
 *
 * Each control records the MARGIN between its edges and the dialog's edges as
 * the template left it, and every WM_SIZE recomputes absolute positions from
 * those margins. An edge that is anchored keeps its margin; the opposite edge
 * then either holds the control's size (it rides) or stretches to follow.
 *
 * This replaces a delta-based version that accumulated state across WM_SIZE and
 * could not survive being re-baselined - the symptom was a vertical resize that
 * MOVED the controls (nothing had resized them, so they kept their offset from
 * the moving bottom-left origin) and a horizontal one that CLIPPED them.
 * Margins are absolute, so every WM_SIZE lands in the same place regardless of
 * how many were missed or in what order they arrived. */
#define ANC_L  0x01
#define ANC_B  0x02
#define ANC_R  0x04
#define ANC_T  0x08

typedef struct _ANCHOR {
    ULONG id;
    ULONG fl;                  /* which dialog edges this control follows */
    LONG  l, b, r, t;          /* margins to each dialog edge, at baseline */
    LONG  cx, cy;              /* size at baseline, kept when not stretching */
} ANCHOR;

/* Container takes all the slack. The path row rides the top and widens. The
 * buttons keep their size and hold the bottom-right corner. */
static ANCHOR aAnchor[] = {
    { IDC_BROWSECNR,     ANC_L | ANC_B | ANC_R | ANC_T, 0,0,0,0, 0,0 },
    { IDC_BROWSEPATH,    ANC_L | ANC_R | ANC_T,         0,0,0,0, 0,0 },
    { IDC_BROWSEPATHLBL, ANC_L | ANC_T,                 0,0,0,0, 0,0 },
    { DID_OK,            ANC_R | ANC_B,                 0,0,0,0, 0,0 },
    { DID_CANCEL,        ANC_R | ANC_B,                 0,0,0,0, 0,0 }
};
#define NANCHOR ((int)(sizeof(aAnchor) / sizeof(aAnchor[0])))
static BOOL bAnchorsTaken = FALSE;
/* The dialog the margins were measured against. Anchors are only valid for that
 * window: bAnchorsTaken is static and would otherwise survive into the next
 * invocation, and a stale margin set applied to a half-built dialog is what
 * turns a layout bug into "the border and the buttons are gone". */
static HWND hwndAnchored = NULLHANDLE;
/* Size the controls were last laid out for, so a pure move does no work. */
static LONG cxLaidOut, cyLaidOut;


/* Record each control's margins against the dialog as it stands right now. */
static void AnchorsCapture(HWND hwnd)
{
    SWP swpDlg;
    int i;

    bAnchorsTaken = FALSE;
    hwndAnchored  = NULLHANDLE;
    cxLaidOut = cyLaidOut = 0;      /* force the first layout after capture */
    if (!WinQueryWindowPos(hwnd, &swpDlg) || swpDlg.cx <= 0 || swpDlg.cy <= 0)
        return;

    for (i = 0; i < NANCHOR; i++) {
        HWND h = WinWindowFromID(hwnd, aAnchor[i].id);
        SWP  swp;
        if (h == NULLHANDLE || !WinQueryWindowPos(h, &swp))
            continue;
        aAnchor[i].l  = swp.x;
        aAnchor[i].b  = swp.y;
        aAnchor[i].r  = swpDlg.cx - (swp.x + swp.cx);
        aAnchor[i].t  = swpDlg.cy - (swp.y + swp.cy);
        aAnchor[i].cx = swp.cx;
        aAnchor[i].cy = swp.cy;
        /* A negative margin means this was measured before the dialog settled.
         * Refuse the whole set rather than lay out from it. */
        if (aAnchor[i].l < 0 || aAnchor[i].b < 0 ||
            aAnchor[i].r < 0 || aAnchor[i].t < 0)
            return;
    }
    bAnchorsTaken = TRUE;
    hwndAnchored  = hwnd;
}

static void AnchorsApplyImpl(HWND hwnd, LONG cxDlg, LONG cyDlg);

/* Lay the dialog out at its CURRENT size and repaint.
 *
 * The size is queried rather than taken from the message, and the positions are
 * absolute margins rather than accumulated deltas, so this is idempotent: it can
 * be called from any message, any number of times, and lands in the same place.
 * That matters because the delivery path is not one message - WM_WINDOWPOSCHANGED
 * is what the frame actually receives, and WM_SIZE is generated from it by the
 * default window procedure [DOC-IBM - pm3.txt, WM_WINDOWPOSCHANGED - Default
 * Processing: "SWP_SIZE A WM_SIZE with the new window size"]. */
static void BrowseLayout(HWND hwnd)
{
    HWND hwndCnr = WinWindowFromID(hwnd, IDC_BROWSECNR);
    SWP  swp;

    /* Only ever lay out from margins measured in WM_INITDLG for THIS dialog.
     * Capturing lazily from here was a trap: a size change can arrive while the
     * dialog is still being built, and capturing then measured a half-placed
     * layout - which the next capture would then lock in. If the margins are not
     * ready, do nothing and leave the template's own layout alone. */
    if (!bAnchorsTaken || hwnd != hwndAnchored)
        return;
    if (!WinQueryWindowPos(hwnd, &swp) || swp.cx <= 0 || swp.cy <= 0)
        return;

    /* WM_WINDOWPOSCHANGED reports moves as well as resizes, and dragging the
     * dialog by its title bar sends one per mouse-move. Re-laying out for a
     * move is pure churn - the margins produce the same answer - so compare
     * against the size last laid out and do nothing when only x/y changed. */
    if (swp.cx == cxLaidOut && swp.cy == cyLaidOut)
        return;
    cxLaidOut = swp.cx;
    cyLaidOut = swp.cy;

    AnchorsApplyImpl(hwnd, swp.cx, swp.cy);

    /* The records must re-flow into the container's new width; without this
     * they keep the column layout they had at the old size. */
    if (hwndCnr != NULLHANDLE)
        WinSendMsg(hwndCnr, CM_INVALIDATERECORD, NULL,
                   MPFROM2SHORT(0, CMA_ERASE | CMA_REPOSITION));
}

/* Reapply the margins against the dialog's current size.
 *
 * Every control is moved in ONE WinSetMultWindowPos call rather than a
 * WinSetWindowPos each. Positioned one at a time they are laid out - and
 * repainted - in sequence, so a drag shows each control briefly against the
 * others' old positions: the labels and the container's border visibly jump
 * about before settling. The batch form applies the whole layout as a unit,
 * which is what it exists for [DOC-IBM - pm2.txt, WinSetMultWindowPos]. */
static void AnchorsApplyImpl(HWND hwnd, LONG cxDlg, LONG cyDlg)
{
    SWP aswp[NANCHOR];
    ULONG cswp = 0;
    int i;

    if (!bAnchorsTaken || cxDlg <= 0 || cyDlg <= 0)
        return;

    for (i = 0; i < NANCHOR; i++) {
        const ANCHOR *pa = &aAnchor[i];
        HWND h = WinWindowFromID(hwnd, pa->id);
        LONG x, y, cx, cy;
        if (h == NULLHANDLE)
            continue;

        /* Horizontal: both edges anchored means stretch, one means ride. */
        if ((pa->fl & ANC_L) && (pa->fl & ANC_R)) {
            x  = pa->l;
            cx = cxDlg - pa->l - pa->r;
        } else if (pa->fl & ANC_R) {
            cx = pa->cx;
            x  = cxDlg - pa->r - cx;
        } else {
            x  = pa->l;
            cx = pa->cx;
        }

        /* Vertical: bottom-left origin, so ANC_T is the HIGH edge. */
        if ((pa->fl & ANC_B) && (pa->fl & ANC_T)) {
            y  = pa->b;
            cy = cyDlg - pa->b - pa->t;
        } else if (pa->fl & ANC_T) {
            cy = pa->cy;
            y  = cyDlg - pa->t - cy;
        } else {
            y  = pa->b;
            cy = pa->cy;
        }

        if (cx < 8)  cx = 8;      /* a dialog dragged tiny must not invert */
        if (cy < 8)  cy = 8;

        /* SWP is assigned by FIELD NAME: its declaration order is (fl, cy, cx,
         * y, x), the reverse of WinSetWindowPos's arguments, so a positional
         * initialiser here would silently swap width with height
         * [os2ref/pm-window-messaging.md]. */
        memset(&aswp[cswp], 0, sizeof(aswp[cswp]));
        aswp[cswp].fl               = SWP_SIZE | SWP_MOVE;
        aswp[cswp].cy               = cy;
        aswp[cswp].cx               = cx;
        aswp[cswp].y                = y;
        aswp[cswp].x                = x;
        aswp[cswp].hwndInsertBehind = NULLHANDLE;   /* no SWP_ZORDER: unused */
        aswp[cswp].hwnd             = h;
        cswp++;
    }

    if (cswp > 0)
        WinSetMultWindowPos(WinQueryAnchorBlock(hwnd), aswp, cswp);
}

/* Join a directory and a leaf into pszOut, supplying the separator only when
 * the directory lacks one.
 *
 * This was open-coded at four sites as sprintf(... pszDir[strlen(pszDir)-1] ...),
 * which indexes [-1] on an empty directory and, more seriously, could not be
 * bounded: a CCHMAXPATH directory plus a CCHMAXPATHCOMP leaf is ~515 bytes and
 * every destination was CCHMAXPATH. Both problems belong in one place.
 * Returns FALSE if the result did not fit. */
static BOOL JoinPath(char *pszOut, int cchOut, const char *pszDir,
                     const char *pszLeaf)
{
    const size_t cchDir = strlen(pszDir);
    const BOOL   bSep   = (cchDir > 0 && pszDir[cchDir - 1] != '\\');
    const int    n      = snprintf(pszOut, (size_t)cchOut, "%s%s%s",
                                   pszDir, bSep ? "\\" : "", pszLeaf);
    return (BOOL)(n >= 0 && n < cchOut);
}

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
    BOOL    bFound;

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

    /* Then the drives, so the browser can leave the volume it started on -
     * there was previously no way to do that at all. DosQueryCurrentDisk
     * returns a bitmask of the drives that exist, bit 0 = A:
     * [os2ref/file-io.md, DosQueryCurrentDisk]. "[-C-]" is the convention the
     * OS/2 file dialogs use, and it cannot collide with a real directory name
     * because ':' and '-' bracketing is not a legal name here. */
    {
        ULONG ulDrive = 0, ulMap = 0;
        if (DosQueryCurrentDisk(&ulDrive, &ulMap) == NO_ERROR) {
            int d;
            for (d = 0; d < 26; d++) {
                if (ulMap & (1UL << d)) {
                    BROWSEREC *prec = BrowseAlloc(hwndCnr);
                    if (prec) {
                        CHAR szDrv[8];
                        snprintf(szDrv, sizeof(szDrv), "[-%c-]", (char)('A' + d));
                        BrowseInsert(hwndCnr, prec, szDrv, TRUE, NULL);
                    }
                }
            }
        }
    }

    if (!JoinPath(szMask, sizeof(szMask), pszDir, "*"))
        return;

    /* FILE_DIRECTORY in the attribute mask means "directories AS WELL",
     * not "directories only" [os2ref/file-io.md, DosFindFirst].
     *
     * FILEFINDBUF3 is the right record for FIL_STANDARD: the struct suffix is
     * the API generation, not the info level (FIL_STANDARD is 1, and level 3 is
     * FIL_QUERYEASFROMLIST). Confirmed against bsedos.h and against klibc, which
     * pairs FIL_STANDARD with PFILEFINDBUF3 in fs.c. */
    rc = DosFindFirst((PSZ)szMask, &hdir,
                      FILE_NORMAL | FILE_DIRECTORY | FILE_READONLY | FILE_ARCHIVED,
                      &ffb, sizeof(ffb), &cFind, FIL_STANDARD);
    bFound = (BOOL)(rc == NO_ERROR);

    while (rc == NO_ERROR) {
        const BOOL bDir = (ffb.attrFile & FILE_DIRECTORY) != 0;
        if (strcmp(ffb.achName, ".") != 0 && strcmp(ffb.achName, "..") != 0 &&
            JoinPath(szFull, sizeof(szFull), pszDir, ffb.achName)) {
            /* Build the path first: a record allocated and then not inserted
             * would have to be handed back with CM_FREERECORD. */
            BROWSEREC *prec = BrowseAlloc(hwndCnr);
            if (prec) {
                CHAR szDisplay[CCHMAXPATH + 2];
                snprintf(szDisplay, sizeof(szDisplay), bDir ? "[%s]" : "%s",
                         ffb.achName);
                BrowseInsert(hwndCnr, prec, szDisplay, bDir, szFull);
            }
        }
        cFind = 1;
        rc = DosFindNext(hdir, &ffb, sizeof(ffb), &cFind);
    }
    /* Only a successful DosFindFirst leaves a handle to close; on failure hdir
     * is still HDIR_CREATE. */
    if (bFound)
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
        /* cchPick is the caller's buffer size; it was carried in the BROWSEARG
         * and never consulted, so this wrote a directory plus a leaf - up to
         * ~515 bytes - into the caller's CCHMAXPATH stack array. Refuse the
         * pick rather than dismiss the dialog with a truncated path. */
        if (!JoinPath(pbaCur->pszPick, pbaCur->cchPick, pbaCur->pszDir, szName)) {
            pbaCur->pszPick[0] = '\0';
            WinAlarm(HWND_DESKTOP, WA_WARNING);
            return;
        }
        WinDismissDlg(hwnd, DID_OK);
        return;
    }

    /* A drive entry: PlainName has already stripped the [ ], leaving "-C-". */
    if (szName[0] == '-' && szName[2] == '-' && szName[3] == '\0') {
        snprintf(szNew, sizeof(szNew), "%c:\\", szName[1]);
        strncpy(pbaCur->pszDir, szNew, pbaCur->cchDir - 1);
        pbaCur->pszDir[pbaCur->cchDir - 1] = '\0';
        BrowseFill(hwnd, pbaCur->pszDir);
        return;
    }

    if (strcmp(szName, "..") == 0) {
        char *pSep;
        strncpy(szNew, pbaCur->pszDir, sizeof(szNew) - 1);
        szNew[sizeof(szNew) - 1] = '\0';
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
    } else if (!JoinPath(szNew, sizeof(szNew), pbaCur->pszDir, szName)) {
        /* Descending would exceed CCHMAXPATH - stay put rather than navigate to
         * a truncated path that names a different directory. */
        WinAlarm(HWND_DESKTOP, WA_WARNING);
        return;
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
        /* Take the margins from the template's own layout, before the user can
         * have resized anything. Centring only moves the dialog, so the size
         * here is still the one the template asked for. */
        AnchorsCapture(hwnd);
        return (MRESULT)FALSE;
    }

    /* A PM dialog does not reflow its controls, so a sizeable one lays itself
     * out - from the margins captured in WM_INITDLG.
     *
     * THE SIZE ARRIVES AS WM_WINDOWPOSCHANGED, NOT WM_SIZE. A message trace of
     * this dialog through a full resize shows WM_ADJUSTWINDOWPOS, then
     * WM_WINDOWPOSCHANGED, then WM_FORMATFRAME and WM_PAINT - and WM_SIZE not
     * once. The books say the default window procedure turns SWP_SIZE into a
     * WM_SIZE [pm3.txt, WM_WINDOWPOSCHANGED - Default Processing], but that is
     * WinDefWindowProc; a dialog runs WinDefDlgProc, and the frame consumes the
     * position change without ever producing one. Three earlier attempts here
     * hung the layout off WM_SIZE and therefore never ran at all - which is why
     * a vertical drag appeared to MOVE the controls (they kept their offset from
     * a moving bottom-left origin) and a horizontal one CLIPPED them.
     *
     * Both are handled: WM_SIZE costs nothing if it never comes, and neither is
     * swallowed - WinDefDlgProc runs first so the frame formats itself (that is
     * what positions the title bar and the sizing border), then the controls go
     * on top of the geometry it settled on. */
    case WM_SIZE:
    case WM_WINDOWPOSCHANGED: {
        MRESULT mr = WinDefDlgProc(hwnd, msg, mp1, mp2);
        BrowseLayout(hwnd);
        return mr;
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
        /* DosQueryCurrentDir returns the path WITHOUT the leading backslash and
         * without the drive, so both are supplied here. */
        DosQueryCurrentDir(0, (PBYTE)szCur, &cb);
        snprintf(pszDir, (size_t)cchDir, "%c:\\%s",
                 (char)('A' + ulDrive - 1), szCur);
    }

    ba.pszDir  = pszDir;  ba.cchDir  = cchDir;
    ba.pszPick = pszPick; ba.cchPick = cchPick;
    pszPick[0] = '\0';

    return (BOOL)(WinDlgBox(HWND_DESKTOP, hwndOwner, BrowseDlgProc, NULLHANDLE,
                            IDD_BROWSE, &ba) == DID_OK && pszPick[0] != '\0');
}

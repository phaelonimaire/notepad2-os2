/* np2print.c - printing, via the queued print DC.
 *
 * The sequence is the one in os2ref/printing-spooler.md 7:
 *
 *   DevOpenDC(hab, OD_QUEUED, "*", 5, &dop, NULL)
 *   GpiCreatePS(..., GPIA_ASSOC)
 *   DevEscape(DEVESC_STARTDOC)
 *     ... GPI drawing for a page ...
 *   DevEscape(DEVESC_NEWFRAME)      -- eject and continue
 *   DevEscape(DEVESC_ENDDOC)        -- one STARTDOC..ENDDOC pair = one job
 *   GpiDestroyPS / DevCloseDC
 *
 * Two contract points from that reference that are easy to get wrong:
 *
 *  - pdriv must NOT be NULL. It is "a programming error to pass NULL, because
 *    the DRIVDATA carries the specific device name", so the queue's own
 *    pDriverData is used.
 *  - A DC opened with DevOpenDC must be closed with DevCloseDC - never with
 *    anything else, and never left open.
 *  - The lCount argument is the number of DEVOPENSTRUC fields supplied, not a
 *    fixed constant: 4 is the documented MINIMUM for a queued context, and this
 *    file supplies 5 (through pszComment), so it passes 5.
 *
 * Rendering is deliberately plain: the document's text, in the editor's font,
 * paginated by the printable height. Notepad2's header/footer and colour
 * modes are settings this port does not carry yet.
 */
#define INCL_WIN
#define INCL_GPI
#define INCL_DEV
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_SPL
#define INCL_SPLDOSPRINT
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Scintilla.h"
#include "np2.h"
#include "np2print.h"

/* Find a queue to print to: the one flagged as the default if there is one,
 * otherwise the first. Returns FALSE when the system has no queues at all,
 * which is a normal condition on a machine with no printer installed and is
 * reported as such rather than as a failure to print. */
static BOOL FindQueue(char *pszQueue, int cchQueue,
                      char *pszDriver, int cchDriver,
                      PDRIVDATA *ppDriverData, char *pszErr, int cchErr)
{
    ULONG   cReturned = 0, cTotal = 0, cbNeeded = 0;
    SPLERR  rc;
    PVOID   pBuf;
    PPRQINFO3 pq;
    ULONG   i, iBest;

    rc = SplEnumQueue(NULL, 3, NULL, 0, &cReturned, &cTotal, &cbNeeded, NULL);
    if (cTotal == 0 || cbNeeded == 0) {
        sprintf(pszErr, "No print queues are defined on this system.");
        return FALSE;
    }

    pBuf = malloc(cbNeeded);
    if (!pBuf) {
        sprintf(pszErr, "out of memory");
        return FALSE;
    }
    rc = SplEnumQueue(NULL, 3, pBuf, cbNeeded, &cReturned, &cTotal, &cbNeeded, NULL);
    if (rc != NO_ERROR || cReturned == 0) {
        sprintf(pszErr, "SplEnumQueue rc=%lu", (unsigned long)rc);
        free(pBuf);
        return FALSE;
    }

    pq = (PPRQINFO3)pBuf;
    iBest = 0;
    for (i = 0; i < cReturned; i++)
        if (pq[i].fsType & PRQ3_TYPE_APPDEFAULT) { iBest = i; break; }

    /* PSZ is unsigned char* here (it flips with OS2EMX_PLAIN_CHAR), so the
     * cast is required rather than cosmetic [c-guide.md 0.5]. */
    strncpy(pszQueue,
            pq[iBest].pszName ? (const char *)pq[iBest].pszName : "", cchQueue - 1);
    pszQueue[cchQueue - 1] = '\0';

    /* A queue's pszDriverName is "driver.device"; DEVOPENSTRUC wants only the
     * part before the period [os2ref/printing-spooler.md 3]. */
    strncpy(pszDriver,
            pq[iBest].pszDriverName ? (const char *)pq[iBest].pszDriverName : "",
            cchDriver - 1);
    pszDriver[cchDriver - 1] = '\0';
    {
        char *pDot = strchr(pszDriver, '.');
        if (pDot)
            *pDot = '\0';
    }

    /* Copy the driver data - the enumeration buffer is about to be freed. */
    *ppDriverData = NULL;
    if (pq[iBest].pDriverData && pq[iBest].pDriverData->cb) {
        *ppDriverData = (PDRIVDATA)malloc(pq[iBest].pDriverData->cb);
        if (*ppDriverData)
            memcpy(*ppDriverData, pq[iBest].pDriverData, pq[iBest].pDriverData->cb);
    }

    free(pBuf);
    return (BOOL)(pszQueue[0] != '\0');
}

BOOL PrintDocument(HWND hwndOwner, HWND hwndEdit, const char *pszTitle,
                   const char *pszFontFace, int iFontSize)
{
    CHAR      szQueue[128] = "", szDriver[128] = "", szErr[400] = "";
    PDRIVDATA pdd = NULL;
    DEVOPENSTRUC dop;
    HAB   hab = WinQueryAnchorBlock(hwndOwner);
    HDC   hdc = NULLHANDLE;
    HPS   hps = NULLHANDLE;
    SIZEL sizl = { 0, 0 };
    LONG  lHorzRes = 0, lVertRes = 0;
    LONG  cy, yTop, y, x;
    LONG  cLines, iLine;
    FONTMETRICS fm;
    BOOL  bOK = FALSE;

    if (!FindQueue(szQueue, sizeof(szQueue), szDriver, sizeof(szDriver),
                   &pdd, szErr, sizeof(szErr))) {
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szErr, (PSZ)"Print",
                      0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
        return FALSE;
    }

    memset(&dop, 0, sizeof(dop));
    dop.pszLogAddress    = (PSZ)szQueue;      /* OD_QUEUED: the QUEUE name */
    dop.pszDriverName    = (PSZ)szDriver;
    dop.pdriv            = pdd;               /* never NULL - see the header */
    dop.pszDataType      = (PSZ)"PM_Q_STD";
    dop.pszComment       = (PSZ)pszTitle;

    hdc = DevOpenDC(hab, OD_QUEUED, (PSZ)"*", 5, (PDEVOPENDATA)&dop, NULLHANDLE);
    if (hdc == DEV_ERROR) {
        sprintf(szErr, "DevOpenDC failed for queue \"%s\" (driver \"%s\")",
                szQueue, szDriver);
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szErr, (PSZ)"Print",
                      0, MB_OK | MB_ERROR | MB_MOVEABLE);
        free(pdd);
        return FALSE;
    }

    /* Work in pels so the font metrics and the page size share units. */
    DevQueryCaps(hdc, CAPS_WIDTH,  1, &lHorzRes);
    DevQueryCaps(hdc, CAPS_HEIGHT, 1, &lVertRes);
    sizl.cx = lHorzRes;
    sizl.cy = lVertRes;

    hps = GpiCreatePS(hab, hdc, &sizl, PU_PELS | GPIF_DEFAULT | GPIT_NORMAL | GPIA_ASSOC);
    if (hps == GPI_ERROR) {
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)"GpiCreatePS failed for the printer.",
                      (PSZ)"Print", 0, MB_OK | MB_ERROR | MB_MOVEABLE);
        DevCloseDC(hdc);
        free(pdd);
        return FALSE;
    }

    /* A printer PS starts in colour-index mode like any other; text would
     * otherwise draw in whatever index the RGB value happens to be
     * [os2ref/gpi-drawing.md]. */
    GpiCreateLogColorTable(hps, LCOL_RESET, LCOLF_RGB, 0, 0, NULL);

    if (DevEscape(hdc, DEVESC_STARTDOC, (LONG)strlen(pszTitle), (PBYTE)pszTitle,
                  NULL, NULL) != DEV_OK) {
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)"DEVESC_STARTDOC was refused.",
                      (PSZ)"Print", 0, MB_OK | MB_ERROR | MB_MOVEABLE);
        GpiDestroyPS(hps);
        DevCloseDC(hdc);
        free(pdd);
        return FALSE;
    }

    GpiQueryFontMetrics(hps, sizeof(fm), &fm);
    cy = fm.lMaxBaselineExt;
    if (cy <= 0)
        cy = 20;

    /* A margin of roughly a third of an inch on each side, in pels. */
    {
        LONG lVertPels = 0;
        DevQueryCaps(hdc, CAPS_VERTICAL_FONT_RES, 1, &lVertPels);
        if (lVertPels <= 0)
            lVertPels = 300;
        x    = lVertPels / 3;
        yTop = lVertRes - (lVertPels / 3);
    }

    cLines = (LONG)LONGFROMMR(WinSendMsg(hwndEdit, SCI_GETLINECOUNT, 0, 0));
    y = yTop;

    for (iLine = 0; iLine < cLines; iLine++) {
        CHAR  szLine[1024];
        LONG  cch;
        POINTL pt;

        cch = (LONG)LONGFROMMR(WinSendMsg(hwndEdit, SCI_LINELENGTH,
                                          MPFROMLONG(iLine), 0));
        if (cch >= (LONG)sizeof(szLine))
            cch = sizeof(szLine) - 1;
        szLine[0] = '\0';
        WinSendMsg(hwndEdit, SCI_GETLINE, MPFROMLONG(iLine), MPFROMP(szLine));
        szLine[cch] = '\0';
        /* Strip the line ending - the page break is ours to place. */
        while (cch > 0 && (szLine[cch - 1] == '\r' || szLine[cch - 1] == '\n'))
            szLine[--cch] = '\0';

        if (y < cy * 2) {                      /* out of page - eject */
            DevEscape(hdc, DEVESC_NEWFRAME, 0, NULL, NULL, NULL);
            y = yTop;
        }

        if (cch > 0) {
            pt.x = x;
            pt.y = y;
            GpiCharStringPosAt(hps, &pt, NULL, 0, cch, (PCH)szLine, NULL);
        }
        y -= cy;
    }

    bOK = (DevEscape(hdc, DEVESC_ENDDOC, 0, NULL, NULL, NULL) == DEV_OK);

    GpiAssociate(hps, NULLHANDLE);
    GpiDestroyPS(hps);
    DevCloseDC(hdc);
    free(pdd);

    if (bOK) {
        CHAR szMsg[256];
        sprintf(szMsg, "Sent %ld lines to queue \"%s\".", (long)cLines, szQueue);
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szMsg, (PSZ)"Print",
                      0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
    } else {
        WinMessageBox(HWND_DESKTOP, hwndOwner,
                      (PSZ)"DEVESC_ENDDOC failed - no job was created.",
                      (PSZ)"Print", 0, MB_OK | MB_ERROR | MB_MOVEABLE);
    }
    return bOK;
}

/* Job properties: DevPostDeviceModes is the OS/2 analogue of PrintDlg's
 * setup half [os2ref/printing-spooler.md 5.3]. Called with a NULL buffer it
 * reports the size it needs; called with one it shows the driver's own
 * dialog. */
BOOL PrintSetup(HWND hwndOwner)
{
    CHAR   szQueue[128] = "", szDriver[128] = "", szErr[256] = "";
    PDRIVDATA pdd = NULL;
    HAB    hab = WinQueryAnchorBlock(hwndOwner);
    LONG   cb;

    if (!FindQueue(szQueue, sizeof(szQueue), szDriver, sizeof(szDriver),
                   &pdd, szErr, sizeof(szErr))) {
        WinMessageBox(HWND_DESKTOP, hwndOwner, (PSZ)szErr, (PSZ)"Page Setup",
                      0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
        return FALSE;
    }

    cb = DevPostDeviceModes(hab, NULL, (PSZ)szDriver, NULL, NULL, DPDM_POSTJOBPROP);
    if (cb > 0) {
        PDRIVDATA p = (PDRIVDATA)malloc(cb);
        if (p) {
            memset(p, 0, cb);
            p->cb = cb;
            DevPostDeviceModes(hab, p, (PSZ)szDriver, NULL, (PSZ)szQueue,
                               DPDM_POSTJOBPROP);
            free(p);
        }
    } else {
        WinMessageBox(HWND_DESKTOP, hwndOwner,
                      (PSZ)"This printer driver has no job-properties dialog.",
                      (PSZ)"Page Setup", 0, MB_OK | MB_INFORMATION | MB_MOVEABLE);
    }
    free(pdd);
    return TRUE;
}

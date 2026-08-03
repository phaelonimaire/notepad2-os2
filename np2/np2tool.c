/* np2tool.c - the toolbar. See np2tool.h for the model and its provenance. */

#define INCL_WIN
#define INCL_GPI
#include <os2.h>

#include <stdio.h>
#include <string.h>

#include "np2.h"
#include "np2tool.h"

#define TB_MAXITEMS   32

/* Margins, in pels. The gap either side of a separator is what makes a group
 * read as a group, so it is wider than the gap between buttons. */
#define TB_MARGIN     4     /* bar edge to the first item */
#define TB_GAP        2     /* between adjacent buttons */
#define TB_SEPWIDTH   8     /* a separator's own width */
#define TB_VPAD       2     /* above and below the buttons */
/* The toolbar bitmaps are 16x16, cut from Notepad2's own strip. */
#define TB_BITMAPCX  16
#define TB_BITMAPCY  16

static const TOOLITEM *pItems;
static int   cItems;
static HWND  ahwnd[TB_MAXITEMS];        /* NULLHANDLE for separators */
static HBITMAP ahbm[TB_MAXITEMS];       /* the button images, kept for cleanup */
static LONG  cyBar    = 24;             /* measured in ToolbarMeasure */
static LONG  cxButton = 46;
/* Set when a bitmap button had to fall back to its text label - the bar is then
 * sized for text, or the labels are clipped to the width of an icon. */
static BOOL  bAnyTextFallback = FALSE;

BOOL ToolbarCreate(HWND hwndParent, const TOOLITEM *pItemsIn, int cItemsIn)
{
    HPS hpsLoad;
    int i;

    if (!pItemsIn || cItemsIn <= 0)
        return FALSE;
    if (cItemsIn > TB_MAXITEMS)
        cItemsIn = TB_MAXITEMS;

    pItems = pItemsIn;
    cItems = cItemsIn;
    memset(ahwnd, 0, sizeof(ahwnd));
    memset(ahbm,  0, sizeof(ahbm));
    bAnyTextFallback = FALSE;

    /* One PS for the whole load - GpiLoadBitmap needs one to bind the bitmap to
     * a device, and the bitmaps outlive it. */
    hpsLoad = WinGetPS(hwndParent);

    for (i = 0; i < cItems; i++) {
        const TOOLITEM *p = &pItems[i];
        BTNCDATA bcd;
        PVOID    pCtlData = NULL;
        ULONG    flStyle  = WS_VISIBLE | BS_PUSHBUTTON | BS_NOPOINTERFOCUS;

        if (p->kind == TBI_SEPARATOR)
            continue;                   /* a gap, not a window */

        /* A button takes its image as a HANDLE in BTNCDATA.hImage, not as a
         * resource id in its window text. The "#300" text form documented under
         * the WC_BUTTON styles works for a control in a DIALOG TEMPLATE, where
         * PM knows which module to load from; WinCreateWindow has no module
         * parameter, and passing "#id" there fails the whole call with
         * PMERR_PARAMETER_OUT_OF_RANGE (0x1003) rather than merely drawing no
         * image. GpiLoadBitmap with a NULLHANDLE module reads "the .EXE file of
         * the application" [DOC-IBM - gpi2.txt, GpiLoadBitmap Parameter -
         * Resource], which is where wrc bound them. */
        if (p->idBitmap && hpsLoad != NULLHANDLE) {
            ahbm[i] = GpiLoadBitmap(hpsLoad, NULLHANDLE, p->idBitmap, 0, 0);
            if (ahbm[i] != NULLHANDLE && ahbm[i] != (HBITMAP)GPI_ERROR) {
                memset(&bcd, 0, sizeof(bcd));
                bcd.cb          = sizeof(BTNCDATA);
                bcd.fsCheckState = 0;
                bcd.fsHiliteState = 0;
                bcd.hImage      = (LHANDLE)ahbm[i];
                pCtlData        = &bcd;
                flStyle |= BS_BITMAP;
            } else {
                ahbm[i] = NULLHANDLE;
                bAnyTextFallback = TRUE;   /* fall back to the label */
            }
        }

        /* WC_BUTTON is a PSZ macro while pszClass is const char *, so both arms
         * are made the same type before the cast rather than after it. */
        ahwnd[i] = WinCreateWindow(hwndParent,
                       (PSZ)(p->pszClass ? p->pszClass : (const char *)WC_BUTTON),
                       (PSZ)(p->pszText ? p->pszText : ""),
                       p->flStyle ? p->flStyle : flStyle,
                       0, 0, 0, 0, hwndParent, HWND_TOP, p->id, pCtlData, NULL);
    }

    if (hpsLoad != NULLHANDLE)
        WinReleasePS(hpsLoad);
    return TRUE;
}

void ToolbarMeasure(HWND hwndParent)
{
    HPS hps = WinGetPS(hwndParent);
    FONTMETRICS fm;

    if (hps != NULLHANDLE) {
        if (GpiQueryFontMetrics(hps, sizeof(fm), &fm)) {
            LONG cxWidest = 0;
            LONG cyText   = fm.lMaxBaselineExt;
            int  i;
            for (i = 0; i < cItems; i++) {
                POINTL apt[TXTBOX_COUNT];
                LONG   cch;
                if (pItems[i].kind != TBI_BUTTON)
                    continue;
                if (pItems[i].idBitmap && !bAnyTextFallback) {
                    /* A bitmap button shows no text, so it is sized from the
                     * image instead. */
                    if (TB_BITMAPCX > cxWidest)
                        cxWidest = TB_BITMAPCX;
                    continue;
                }
                if (!pItems[i].pszText)
                    continue;
                cch = (LONG)strlen(pItems[i].pszText);
                if (GpiQueryTextBox(hps, cch, (PCH)pItems[i].pszText,
                                    TXTBOX_COUNT, apt)) {
                    /* One width for every button: a toolbar of ragged buttons
                     * reads as broken rather than as tightly packed. */
                    if (apt[TXTBOX_CONCAT].x > cxWidest)
                        cxWidest = apt[TXTBOX_CONCAT].x;
                }
            }
            /* The bar must clear whichever is taller, the label or the image. */
            if (cyText < TB_BITMAPCY)
                cyText = TB_BITMAPCY;
            cyBar = cyText + 10;                  /* content + button border */
            if (cxWidest > 0)
                cxButton = cxWidest + 14;         /* content + button margins */
        }
        WinReleasePS(hps);
    }
    if (cyBar    < 18) cyBar    = 18;
    if (cxButton < 24) cxButton = 24;
}

LONG ToolbarHeight(void)
{
    return cyBar;
}

int ToolbarButtonCount(void)
{
    int i, n = 0;
    for (i = 0; i < cItems; i++)
        if (pItems[i].kind == TBI_BUTTON)
            n++;
    return n;
}

int ToolbarButtonLabels(const char **ppszOut, int cMax)
{
    int i, n = 0;
    for (i = 0; i < cItems && n < cMax; i++)
        if (pItems[i].kind == TBI_BUTTON)
            ppszOut[n++] = pItems[i].pszText ? pItems[i].pszText : "";
    return n;
}

/* The width an item occupies, separators included. */
static LONG ItemWidth(const TOOLITEM *p)
{
    return (p->kind == TBI_SEPARATOR) ? TB_SEPWIDTH : cxButton;
}

/* Which mask bit an item uses: its ordinal among the BUTTONS. Counting items
 * instead would move every bit the first time a separator is added, silently
 * re-pointing a saved ToolbarButtons value at the wrong buttons. */
static int ButtonIndexOf(int iItem)
{
    int i, n = 0;
    for (i = 0; i < iItem; i++)
        if (pItems[i].kind == TBI_BUTTON)
            n++;
    return n;
}

void ToolbarLayout(HWND hwndParent, LONG cxClient, LONG cyClient,
                   BOOL bVisible, ULONG flMask)
{
    LONG yBar   = cyClient - cyBar;     /* bottom-left origin: the bar is at the TOP */
    LONG xBegin = TB_MARGIN;
    LONG xEnd   = cxClient - TB_MARGIN;
    int  iButton = 0;
    int  i;

    /* Hidden bar: hide every window and leave the geometry alone. */
    if (!bVisible) {
        for (i = 0; i < cItems; i++)
            if (ahwnd[i] != NULLHANDLE)
                WinShowWindow(ahwnd[i], FALSE);
        return;
    }

    /* Two passes so that end-anchored items claim their space before the
     * begin-anchored ones are allowed to fill the middle. */
    for (i = cItems - 1; i >= 0; i--) {
        const TOOLITEM *p = &pItems[i];
        LONG w;
        if (p->align != TBI_END)
            continue;
        if (p->kind == TBI_BUTTON && !(flMask & (1UL << ButtonIndexOf(i))))
            continue;
        w = ItemWidth(p);
        if (xEnd - w < xBegin)
            continue;                   /* no room - handled in the pass below */
        xEnd -= w;
        if (ahwnd[i] != NULLHANDLE)
            WinSetWindowPos(ahwnd[i], HWND_TOP, xEnd, yBar + TB_VPAD,
                            cxButton, cyBar - TB_VPAD * 2,
                            SWP_SIZE | SWP_MOVE | SWP_SHOW);
        xEnd -= TB_GAP;
    }

    for (i = 0; i < cItems; i++) {
        const TOOLITEM *p = &pItems[i];
        LONG w;

        if (p->kind == TBI_BUTTON) {
            const BOOL bShown = (flMask & (1UL << iButton)) != 0;
            iButton++;
            if (!bShown) {
                if (ahwnd[i] != NULLHANDLE)
                    WinShowWindow(ahwnd[i], FALSE);
                continue;
            }
        }
        if (p->align == TBI_END)
            continue;                   /* already placed */

        w = ItemWidth(p);
        /* Out of room: hide rather than half-draw. A clipped button looks like
         * a rendering bug; an absent one looks like a narrow window. */
        if (xBegin + w > xEnd) {
            if (ahwnd[i] != NULLHANDLE)
                WinShowWindow(ahwnd[i], FALSE);
            continue;
        }
        if (ahwnd[i] != NULLHANDLE)
            WinSetWindowPos(ahwnd[i], HWND_TOP, xBegin, yBar + TB_VPAD,
                            cxButton, cyBar - TB_VPAD * 2,
                            SWP_SIZE | SWP_MOVE | SWP_SHOW);
        xBegin += w + TB_GAP;
    }
}

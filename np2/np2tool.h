/* np2tool.h - the toolbar.
 *
 * PM has no toolbar window class - the WC_* set has 24 classes and none of them
 * is a tool bar - so one is built here out of ordinary child windows plus a
 * layout engine. The structure follows the model used by Asu'a Custom Controls'
 * WC_TOOLBAR (Hobbes, dev/samples/PM/AsuaCtl_1-0.zip), which is the closest
 * thing OS/2 has to a documented toolbar control. Written from its DOCUMENTED
 * INTERFACE only - that library is GPL v2 and this port is BSD, so none of its
 * code is used here.
 *
 * The three ideas worth taking from it:
 *
 *   - An item OWNS A WINDOW OF ANY CLASS rather than being a button. The bar
 *     positions whatever it is handed, so a future entry field or combo needs no
 *     change to the engine.
 *   - Each item declares WHICH END OF THE BAR IT ANCHORS TO, and the engine
 *     resolves the layout. Computing x from a running slot index - which is what
 *     this port did before - cannot express a right-aligned item at all.
 *   - A SEPARATOR IS NOT A WINDOW. It is a sized gap the engine skips over.
 *
 * Deliberately NOT taken: the floating/docking half of that library (drag,
 * undock, rotate). Notepad2 has no such feature, and it is where most of
 * AsuaCtl's complexity and all of its unfinished code lives.
 */
#ifndef NP2TOOL_H
#define NP2TOOL_H

#define INCL_WIN
#define INCL_GPI
#include <os2.h>

/* Item kinds. */
#define TBI_BUTTON      0
#define TBI_SEPARATOR   1

/* Which end of the bar the item anchors to. */
#define TBI_BEGIN       0
#define TBI_END         1

typedef struct _TOOLITEM {
    USHORT      id;         /* WM_COMMAND id - a menu id, so a press is
                               indistinguishable from choosing the menu item.
                               0 for a separator. */
    UCHAR       kind;       /* TBI_BUTTON / TBI_SEPARATOR */
    UCHAR       align;      /* TBI_BEGIN / TBI_END */
    const char *pszText;    /* label; also the Customize dialog's name for the
                               item, so it is wanted even on a bitmap button */
    USHORT      idBitmap;   /* BITMAP resource id, or 0 for a text button */
    const char *pszClass;   /* window class, NULL for WC_BUTTON */
    ULONG       flStyle;    /* window style, 0 for the default push button */
} TOOLITEM;

/* Create the item windows as children of hwndParent. Call once, from WM_CREATE.
 * Separators create nothing. Returns FALSE only if the table is unusable. */
BOOL ToolbarCreate(HWND hwndParent, const TOOLITEM *pItems, int cItems);

/* Measure the bar from the system font. Call after ToolbarCreate, and again if
 * the font ever changes. Height and button width both come from the font: a
 * literal here clips every label the moment the system font is larger than the
 * one the literal was chosen for. */
void ToolbarMeasure(HWND hwndParent);

/* Height in pels, for the parent's own layout arithmetic. */
LONG ToolbarHeight(void);

/* Position everything for a client area of cxClient x cyClient. The bar sits at
 * the TOP, which in PM's bottom-left origin is the largest y. flMask has one bit
 * per BUTTON in table order - separators are not counted, so the mask keeps its
 * meaning when separators are added or moved. */
void ToolbarLayout(HWND hwndParent, LONG cxClient, LONG cyClient,
                   BOOL bVisible, ULONG flMask);

/* The button labels, in table order, for the Customize dialog. Separators are
 * skipped so the dialog's list lines up with the mask bits. Returns the count. */
int ToolbarButtonLabels(const char **ppszOut, int cMax);

/* How many buttons (not items) the bar has - the number of meaningful mask
 * bits. */
int ToolbarButtonCount(void);

#endif /* NP2TOOL_H */

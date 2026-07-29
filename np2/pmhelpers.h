/* pmhelpers.h - PM-idiomatic helpers for Win32 dialog idioms that have no 1:1 PM call.
 *
 * This is deliberately NOT a Win32 emulation layer. Emulating Win32 would mean testing
 * the emulation rather than the port, and would hide exactly the platform differences
 * that need to be got right. Each helper below exists because the Win32 original is a
 * *composite* operation that PM expresses differently, not because PM is missing a call.
 *
 * Every OS/2 API used is cited to os2ref/ or IBM's book.
 */
#ifndef PMHELPERS_H
#define PMHELPERS_H

#define INCL_WIN
#include <os2.h>

/* Win32 CheckRadioButton(hDlg, first, last, check) checks one button in an ID RANGE and
 * clears the others. PM has WinCheckButton per button [os2ref/resources-and-dialogs.md],
 * so the range walk is the caller's job. */
static void PMCheckRadioButton(HWND hwndDlg, ULONG idFirst, ULONG idLast, ULONG idCheck)
{
    ULONG id;
    for (id = idFirst; id <= idLast; id++)
        WinCheckButton(hwndDlg, id, (id == idCheck) ? 1UL : 0UL);
}

/* Centre a dialog over its owner.
 *
 * The y arithmetic is bottom-left-origin throughout: WinQueryWindowPos gives each
 * window's origin as its BOTTOM-left corner relative to its parent, so centring is
 * ordinary arithmetic in that space - no flip is needed as long as you never mix in a
 * top-left value. [DOC-IBM - pm2.txt, WinSetWindowPos/WinQueryWindowPos] */
static void PMCenterDlgInParent(HWND hwndDlg, HWND hwndOwner)
{
    SWP swpDlg, swpOwner;
    LONG x, y, cxDesk, cyDesk;

    if (hwndOwner == NULLHANDLE)
        hwndOwner = HWND_DESKTOP;
    if (!WinQueryWindowPos(hwndDlg, &swpDlg))
        return;
    if (!WinQueryWindowPos(hwndOwner, &swpOwner))
        return;

    if (hwndOwner == HWND_DESKTOP) {
        swpOwner.x = 0;
        swpOwner.y = 0;
    } else {
        POINTL ptl;
        ptl.x = 0;
        ptl.y = 0;
        WinMapWindowPoints(hwndOwner, HWND_DESKTOP, &ptl, 1);
        swpOwner.x = ptl.x;
        swpOwner.y = ptl.y;
    }

    x = swpOwner.x + (swpOwner.cx - swpDlg.cx) / 2;
    y = swpOwner.y + (swpOwner.cy - swpDlg.cy) / 2;

    /* Keep it on screen. SV_CXSCREEN/SV_CYSCREEN [os2ref/pm-window-messaging.md 11]. */
    cxDesk = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN);
    cyDesk = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    if (x + swpDlg.cx > cxDesk) x = cxDesk - swpDlg.cx;
    if (y + swpDlg.cy > cyDesk) y = cyDesk - swpDlg.cy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    /* NOTE: SWP's field order is (fl, cy, cx, y, x) - the REVERSE of the argument order
     * of WinSetWindowPos(x, y, cx, cy). Assigning positionally silently swaps both
     * pairs [DOC-IBM - pm4.txt, SWP; see os2ref/pm-window-messaging.md]. */
    WinSetWindowPos(hwndDlg, HWND_TOP, x, y, 0, 0, SWP_MOVE);
}

/* Win32 PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)hCtl, 1) moves focus to a specific
 * control. PM has no WM_NEXTDLGCTL; focus is set directly. */
static void PMFocusDlgItem(HWND hwndDlg, ULONG idItem)
{
    HWND hwndItem = WinWindowFromID(hwndDlg, idItem);
    if (hwndItem != NULLHANDLE)
        WinSetFocus(HWND_DESKTOP, hwndItem);
}

#endif /* PMHELPERS_H */

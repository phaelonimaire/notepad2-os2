/* rctest.c - load a menu, a dialog and a string from bound resources. */
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <string.h>
#include "res.h"

static CHAR szFromTable[128] = "";

MRESULT EXPENTRY AboutDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
    case WM_COMMAND:
        if (SHORT1FROMMP(mp1) == DID_OK)
            WinDismissDlg(hwnd, TRUE);
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

MRESULT EXPENTRY ClientWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
    case WM_PAINT: {
        RECTL rcl;
        POINTL ptl;
        HPS hps = WinBeginPaint(hwnd, NULLHANDLE, &rcl);
        WinQueryWindowRect(hwnd, &rcl);
        WinFillRect(hps, &rcl, CLR_WHITE);
        GpiSetColor(hps, CLR_BLACK);
        ptl.x = 20; ptl.y = rcl.yTop - 40;
        GpiCharStringAt(hps, &ptl, (LONG)strlen(szFromTable), (PCH)szFromTable);
        ptl.y -= 20;
        GpiCharStringAt(hps, &ptl, 34, (PCH)"Use the File / Help menus above.");
        WinEndPaint(hps);
        return (MRESULT)0;
    }
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case IDM_ABOUT:
            WinDlgBox(HWND_DESKTOP, hwnd, AboutDlgProc, NULLHANDLE,
                      ID_ABOUTDLG, NULL);
            return (MRESULT)0;
        case IDM_EXIT:
            WinPostMsg(hwnd, WM_QUIT, 0, 0);
            return (MRESULT)0;
        }
        break;
    }
    return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

int main(void)
{
    HAB   hab = WinInitialize(0);
    HMQ   hmq = WinCreateMsgQueue(hab, 0);
    HWND  hwndFrame, hwndClient = NULLHANDLE;
    ULONG flFrame = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER |
                    FCF_MINMAX | FCF_TASKLIST | FCF_MENU;
    QMSG  qmsg;

    /* WinLoadString(hab, hmod, id, cchMax, pchBuffer) - hmod 0 = the .EXE itself. */
    WinLoadString(hab, NULLHANDLE, IDS_TITLE, sizeof(szFromTable), (PSZ)szFromTable);

    WinRegisterClass(hab, (PSZ)"RcTest", ClientWndProc, CS_SIZEREDRAW, 0);

    /* FCF_MENU makes WinCreateStdWindow load the menu resource whose id matches
       the window id passed as the last-but-one argument. */
    hwndFrame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flFrame,
                                   (PSZ)"RcTest", (PSZ)"Resource test",
                                   0, NULLHANDLE, ID_MAINWIN, &hwndClient);
    if (hwndFrame == NULLHANDLE) {
        WinDestroyMsgQueue(hmq);
        WinTerminate(hab);
        return 1;
    }
    WinSetWindowPos(hwndFrame, HWND_TOP, 60, 60, 560, 260,
                    SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);

    while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0))
        WinDispatchMsg(hab, &qmsg);

    WinDestroyWindow(hwndFrame);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}

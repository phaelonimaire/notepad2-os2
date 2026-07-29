/* colwrap.c - Notepad2's ColumnWrapDlgProc, converted from Win32 to PM.
 *
 * The Win32 original is src/Dialogs.c:1447 in Notepad2-mod. Mapping applied:
 *
 *   WM_INITDIALOG            -> WM_INITDLG        (0x003b; PM dialogs get this, not WM_CREATE)
 *   lParam create param      -> mp2               (WinDlgBox pCreateParams)
 *   SetDlgItemInt            -> WinSetDlgItemShort
 *   GetDlgItemInt(&fTrans)   -> WinQueryDlgItemShort  (returns BOOL = "was a number")
 *   SendDlgItemMessage(EM_LIMITTEXT) -> WinSendDlgItemMsg(EM_SETTEXTLIMIT, 0x0143)
 *   LOWORD(wParam)           -> SHORT1FROMMP(mp1)
 *   IDOK / IDCANCEL          -> DID_OK (1) / DID_CANCEL (2)
 *   EndDialog                -> WinDismissDlg
 *   PostMessage(WM_NEXTDLGCTL) -> WinSetFocus  (PM has no WM_NEXTDLGCTL)
 *   DialogBoxParam           -> WinDlgBox
 *   return TRUE/FALSE        -> return 0 / WinDefDlgProc(...)
 *   WM_INITDIALOG return TRUE -> WM_INITDLG return FALSE   (INVERTED - see below)
 */
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <stdio.h>
#include <string.h>
#include "colwrap.h"
#include "pmhelpers.h"

static CHAR szStatus[128] = "No dialog run yet.";

MRESULT EXPENTRY ColumnWrapDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static SHORT *piNumber;

    switch (msg) {

    case WM_INITDLG:
        piNumber = (SHORT *)mp2;
        WinSetDlgItemShort(hwnd, IDC_COLUMNWRAP, (USHORT)*piNumber, FALSE);
        WinSendDlgItemMsg(hwnd, IDC_COLUMNWRAP, EM_SETTEXTLIMIT,
                          MPFROMSHORT(15), 0);
        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        /* !! INVERTED FROM WIN32 !!  PM's WM_INITDLG return is a "focus set indicator":
         * TRUE means "the dialog procedure HAS changed the focus itself", FALSE means
         * "focus not changed - PM, assign the default" [DOC-IBM - pm3.txt, WM_INITDLG
         * Return Value].  Win32's WM_INITDIALOG is the opposite: TRUE asks the system to
         * set the default focus.  Returning Win32's TRUE here leaves NO control focused:
         * the dialog paints perfectly and ignores every keystroke. */
        return (MRESULT)FALSE;

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {

        case DID_OK: {
            SHORT sNewNumber = 0;
            /* WinQueryDlgItemShort returns FALSE if the text is not a number - the
             * direct equivalent of Win32's fTranslated out-parameter. */
            if (WinQueryDlgItemShort(hwnd, IDC_COLUMNWRAP, &sNewNumber, FALSE)) {
                *piNumber = sNewNumber;
                WinDismissDlg(hwnd, DID_OK);
            } else {
                PMFocusDlgItem(hwnd, IDC_COLUMNWRAP);
            }
            return (MRESULT)0;
        }

        case DID_CANCEL:
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)0;
        }
        return (MRESULT)0;
    }

    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

MRESULT EXPENTRY ClientWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    static SHORT sColumn = 72;

    switch (msg) {
    case WM_PAINT: {
        RECTL rcl;
        POINTL ptl;
        HPS hps = WinBeginPaint(hwnd, NULLHANDLE, &rcl);
        WinQueryWindowRect(hwnd, &rcl);
        WinFillRect(hps, &rcl, CLR_WHITE);
        GpiSetColor(hps, CLR_BLACK);
        ptl.x = 16; ptl.y = rcl.yTop - 40;
        GpiCharStringAt(hps, &ptl, (LONG)strlen(szStatus), (PCH)szStatus);
        ptl.y -= 22;
        GpiCharStringAt(hps, &ptl, 41,
                        (PCH)"File / Column Wrap... to run the dialog.");
        WinEndPaint(hps);
        return (MRESULT)0;
    }

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case IDM_WRAP: {
            ULONG rc = (ULONG)WinDlgBox(HWND_DESKTOP, hwnd, ColumnWrapDlgProc,
                                        NULLHANDLE, IDD_COLUMNWRAP, &sColumn);
            if (rc == DID_OK)
                sprintf(szStatus, "OK pressed - column is now %d", (int)sColumn);
            else
                sprintf(szStatus, "Cancelled - column unchanged at %d", (int)sColumn);
            WinInvalidateRect(hwnd, NULL, FALSE);
            return (MRESULT)0;
        }
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

    WinRegisterClass(hab, (PSZ)"ColWrap", ClientWndProc, CS_SIZEREDRAW, 0);
    hwndFrame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flFrame,
                                   (PSZ)"ColWrap", (PSZ)"Notepad2 dialog, ported to PM",
                                   0, NULLHANDLE, IDD_MAINWIN, &hwndClient);
    if (hwndFrame == NULLHANDLE) {
        WinDestroyMsgQueue(hmq);
        WinTerminate(hab);
        return 1;
    }
    WinSetWindowPos(hwndFrame, HWND_TOP, 70, 70, 520, 220,
                    SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);

    while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0))
        WinDispatchMsg(hab, &qmsg);

    WinDestroyWindow(hwndFrame);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}

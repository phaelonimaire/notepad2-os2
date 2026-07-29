/* ww.c - Notepad2's WordWrapSettingsDlgProc combo-box handling, converted to PM.
 *
 * Win32 original: src/Dialogs.c:1547. Combo mapping - PM's WC_COMBOBOX is an entry
 * field plus a list box, and "because it is composed of the two, it also accepts the
 * EM_* and LM_* messages of its parts" [DOC-IBM - os2ref/pm-controls.md 10]:
 *
 *   CB_ADDSTRING       -> LM_INSERTITEM     (mp1 = LIT_END, mp2 = text)
 *   CB_SETCURSEL       -> LM_SELECTITEM     (mp1 = index,   mp2 = TRUE)
 *   CB_GETCURSEL       -> LM_QUERYSELECTION (mp1 = LIT_FIRST)
 *   CB_SETEXTENDEDUI   -> (no equivalent; a Win32 drop-down UI tweak, dropped)
 *   WM_INITDIALOG ret TRUE -> WM_INITDLG ret FALSE   (INVERTED - see the recipe)
 */
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <stdio.h>
#include <string.h>
#include "ww.h"
#include "pmhelpers.h"

static CHAR szStatus[160] = "No dialog run yet.";

/* The settings the dialog edits, exactly as Notepad2 keeps them. */
static SHORT iWordWrapIndent  = 0;
static SHORT iWordWrapSymbols = 0;
static SHORT iWordWrapMode    = 0;

static void FillCombo(HWND hwnd, ULONG id, const char *items)
{
    /* items is a '|'-separated list, the same shape Notepad2 stores in its
       string resources and splits with StrChr. */
    char buf[512];
    char *p1, *p2;
    strncpy(buf, items, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    p1 = buf;
    while ((p2 = strchr(p1, '|')) != NULL) {
        *p2++ = '\0';
        if (*p1)
            WinSendDlgItemMsg(hwnd, id, LM_INSERTITEM,
                              MPFROMSHORT(LIT_END), MPFROMP(p1));
        p1 = p2;
    }
    if (*p1)
        WinSendDlgItemMsg(hwnd, id, LM_INSERTITEM,
                          MPFROMSHORT(LIT_END), MPFROMP(p1));
}

static SHORT ComboSel(HWND hwnd, ULONG id)
{
    return (SHORT)SHORT1FROMMR(
        WinSendDlgItemMsg(hwnd, id, LM_QUERYSELECTION,
                          MPFROMSHORT(LIT_FIRST), 0));
}

MRESULT EXPENTRY WordWrapDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {

    case WM_INITDLG:
        FillCombo(hwnd, IDC_CB_INDENT, "No indent|Same as line|One more level|Two more levels");
        FillCombo(hwnd, IDC_CB_SYMBOL, "None|At end of line|At start of line|Both");
        FillCombo(hwnd, IDC_CB_MODE,   "Wrap at window|Wrap at column|No wrap");

        WinSendDlgItemMsg(hwnd, IDC_CB_INDENT, LM_SELECTITEM,
                          MPFROMSHORT(iWordWrapIndent), MPFROMSHORT(TRUE));
        WinSendDlgItemMsg(hwnd, IDC_CB_SYMBOL, LM_SELECTITEM,
                          MPFROMSHORT(iWordWrapSymbols), MPFROMSHORT(TRUE));
        WinSendDlgItemMsg(hwnd, IDC_CB_MODE, LM_SELECTITEM,
                          MPFROMSHORT(iWordWrapMode), MPFROMSHORT(TRUE));

        PMCenterDlgInParent(hwnd, WinQueryWindow(hwnd, QW_OWNER));
        return (MRESULT)FALSE;   /* FALSE = let PM set the focus (inverted vs Win32) */

    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case DID_OK:
            iWordWrapIndent  = ComboSel(hwnd, IDC_CB_INDENT);
            iWordWrapSymbols = ComboSel(hwnd, IDC_CB_SYMBOL);
            iWordWrapMode    = ComboSel(hwnd, IDC_CB_MODE);
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
        ptl.x = 16; ptl.y = rcl.yTop - 40;
        GpiCharStringAt(hps, &ptl, (LONG)strlen(szStatus), (PCH)szStatus);
        ptl.y -= 22;
        GpiCharStringAt(hps, &ptl, 44,
                        (PCH)"File / Word Wrap Settings... to run dialog.");
        WinEndPaint(hps);
        return (MRESULT)0;
    }
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1)) {
        case IDM_WW: {
            ULONG rc = (ULONG)WinDlgBox(HWND_DESKTOP, hwnd, WordWrapDlgProc,
                                        NULLHANDLE, IDD_WORDWRAP, NULL);
            if (rc == DID_OK)
                sprintf(szStatus, "OK: indent=%d symbols=%d mode=%d",
                        (int)iWordWrapIndent, (int)iWordWrapSymbols,
                        (int)iWordWrapMode);
            else if (rc == DID_CANCEL)
                sprintf(szStatus, "Cancelled - settings unchanged (%d/%d/%d)",
                        (int)iWordWrapIndent, (int)iWordWrapSymbols,
                        (int)iWordWrapMode);
            else
                /* Do NOT report a load failure as a user cancel - DID_ERROR (0xFFFF)
                   means WinDlgBox could not build the dialog at all. */
                sprintf(szStatus, "WinDlgBox FAILED: rc=0x%lX  WinGetLastError=0x%lX",
                        (unsigned long)rc,
                        (unsigned long)WinGetLastError(
                            WinQueryAnchorBlock(hwnd)));
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

    WinRegisterClass(hab, (PSZ)"WWTest", ClientWndProc, CS_SIZEREDRAW, 0);
    hwndFrame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flFrame,
                                   (PSZ)"WWTest", (PSZ)"Notepad2 combo dialog on PM",
                                   0, NULLHANDLE, IDD_WWMAIN, &hwndClient);
    if (hwndFrame == NULLHANDLE) { WinDestroyMsgQueue(hmq); WinTerminate(hab); return 1; }
    WinSetWindowPos(hwndFrame, HWND_TOP, 70, 70, 540, 220,
                    SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);
    while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0))
        WinDispatchMsg(hab, &qmsg);
    WinDestroyWindow(hwndFrame);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}

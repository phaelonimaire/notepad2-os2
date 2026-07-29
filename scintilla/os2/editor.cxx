// editor.cxx - a minimal real editor: a PM frame hosting the Scintilla control.
// If this runs and you can type into it, the port works end to end.

#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <cstring>
#include <cstdio>

#include "Scintilla.h"

extern "C" void Scintilla_RegisterClasses(void *hab);

static HWND hwndSci = NULLHANDLE;

// Talking to the control is exactly the Win32 idiom, with WinSendMsg in place of
// SendMessage - the message numbers and parameters are Scintilla's own.
static MRESULT SciMsg(unsigned int msg, MPARAM mp1, MPARAM mp2) {
	return WinSendMsg(hwndSci, msg, mp1, mp2);
}

MRESULT EXPENTRY FrameWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2) {
	switch (msg) {
	case WM_CREATE:
		hwndSci = WinCreateWindow(hwnd, (PSZ)"Scintilla", (PSZ)"",
			WS_VISIBLE, 0, 0, 0, 0, hwnd, HWND_TOP, 2000, nullptr, nullptr);
		if (hwndSci != NULLHANDLE) {
			SciMsg(SCI_SETMARGINWIDTHN, MPFROMLONG(0), MPFROMLONG(40));
			SciMsg(SCI_SETMARGINTYPEN, MPFROMLONG(0), MPFROMLONG(SC_MARGIN_NUMBER));
			SciMsg(SCI_STYLESETSIZE, MPFROMLONG(STYLE_DEFAULT), MPFROMLONG(11));
			SciMsg(SCI_STYLESETFONT, MPFROMLONG(STYLE_DEFAULT),
				MPFROMP((void *)"Courier"));
			SciMsg(SCI_STYLECLEARALL, 0, 0);
			SciMsg(SCI_SETTEXT, 0, MPFROMP((void *)
				"Scintilla on OS/2 Presentation Manager - scroll bar test.\r\n"
				"\r\n"
				"Line 01: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 02: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 03: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 04: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 05: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 06: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 07: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 08: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 09: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 10: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 11: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 12: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 13: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 14: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 15: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 16: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 17: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 18: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 19: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 20: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 21: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 22: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 23: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 24: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 25: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 26: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 27: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 28: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 29: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 30: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 31: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 32: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 33: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 34: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 35: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 36: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 37: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 38: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 39: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"
				"Line 40: the quick brown fox jumps over the lazy dog, and keeps going well past the right edge of the window.\r\n"));
			WinSetFocus(HWND_DESKTOP, hwndSci);
		}
		return 0;

	case WM_SIZE:
		if (hwndSci != NULLHANDLE)
			WinSetWindowPos(hwndSci, HWND_TOP, 0, 0,
				SHORT1FROMMP(mp2), SHORT2FROMMP(mp2), SWP_SIZE | SWP_MOVE | SWP_SHOW);
		return 0;

	case WM_SETFOCUS:
		if (hwndSci != NULLHANDLE && LONGFROMMP(mp2))
			WinSetFocus(HWND_DESKTOP, hwndSci);
		return 0;
	}
	return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

int main(void) {
	HAB hab = WinInitialize(0);
	HMQ hmq = WinCreateMsgQueue(hab, 0);

	Scintilla_RegisterClasses(reinterpret_cast<void *>(hab));

	WinRegisterClass(hab, (PSZ)"SciEditorFrame", FrameWndProc, CS_SIZEREDRAW, 0);

	ULONG flFrame = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER |
		FCF_MINMAX | FCF_TASKLIST;
	HWND hwndClient = NULLHANDLE;
	HWND hwndFrame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flFrame,
		(PSZ)"SciEditorFrame", (PSZ)"Scintilla for OS/2", 0, NULLHANDLE, 0,
		&hwndClient);
	if (hwndFrame == NULLHANDLE) {
		WinDestroyMsgQueue(hmq);
		WinTerminate(hab);
		return 1;
	}
	WinSetWindowPos(hwndFrame, HWND_TOP, 30, 30, 720, 420,
		SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);

	QMSG qmsg;
	while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0))
		WinDispatchMsg(hab, &qmsg);

	WinDestroyWindow(hwndFrame);
	WinDestroyMsgQueue(hmq);
	WinTerminate(hab);
	return 0;
}

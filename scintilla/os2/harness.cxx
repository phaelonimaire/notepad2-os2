// harness.cxx - minimal PM app that drives Scintilla's Surface through PlatPM.cxx.
//
// This exists to test the things that compile fine and can still be silently wrong:
//   1. the y-flip      - the ORIENTATION marker must land where the label says it does
//   2. the font path   - FATTRS -> GpiCreateLogFont -> GpiSetCharBox -> GpiSetCharSet
//   3. text metrics    - the measured box is drawn around the string
//   4. alpha + byte order - a 50% RED wash must look PINK, not BLUE
//
// Build (on the OS/2 box):
//   g++ -std=c++11 -Zomf -O1 -I../include -I../src -I../lexlib \
//       harness.cxx PlatPM.o -o harness.exe

#define INCL_WIN
#define INCL_GPI
#define INCL_DEV
#include <os2.h>

#include <cstring>
#include <cstdio>
#include "Platform.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

static Font g_font;
static bool g_fontMade = false;
static ListBox *g_list = nullptr;
static char g_listReport[160] = {0};
static Window g_editorWin;

// Exercise Window + ListBox: create the popup, fill it, size it from GetDesiredRect(),
// place it with SetPositionRelative() (which flips against the PARENT height, not the
// surface height), select an item, then read back what the control says.
static void MakeList(HWND hwndClient, char *report, size_t reportLen) {
	g_editorWin = reinterpret_cast<WindowID>(hwndClient);
	g_list = ListBox::Allocate();
	g_list->SetVisibleRows(5);
	g_list->SetAverageCharWidth(8);
	g_list->Create(g_editorWin, 1001, Point(0, 0), 16, false, 0);
	if (!g_fontMade) {
		FontParameters fp("Courier", 12.0f, 400, false);
		g_font.Create(fp);
		g_fontMade = true;
	}
	g_list->SetFont(g_font);
	g_list->SetList("WinCreateWindow WinSendMsg GpiCharStringPosAt "
		"GpiQueryTextBox DosAllocMem", ' ', '\0');

	PRectangle rcDesired = g_list->GetDesiredRect();
	// Place it under the sample text, in Scintilla (y-down) coordinates.
	g_list->SetPositionRelative(
		PRectangle(430, 150, 430 + (rcDesired.right - rcDesired.left),
			150 + (rcDesired.bottom - rcDesired.top)), g_editorWin);
	g_list->Select(2);
	g_list->Show(true);

	char sel[64];
	g_list->GetValue(g_list->GetSelection(), sel, sizeof(sel));
	snprintf(report, reportLen,
		"ListBox: Length()=%d  GetSelection()=%d  GetValue()=\"%s\"",
		g_list->Length(), g_list->GetSelection(), sel);
}

static void PaintTest(HPS hps, HWND hwnd, int cx, int cy) {
	Surface *surf = Surface::Allocate(0);
	if (!surf)
		return;
	// Init(SurfaceID, WindowID) takes an existing HPS - exactly what WinBeginPaint gave us.
	surf->Init(reinterpret_cast<SurfaceID>(hps), reinterpret_cast<WindowID>(hwnd));

	if (!g_fontMade) {
		FontParameters fp("Courier", 12.0f, 400, false);
		g_font.Create(fp);
		g_fontMade = true;
	}

	const ColourDesired white(255, 255, 255);
	const ColourDesired black(0, 0, 0);
	const ColourDesired red(255, 0, 0);
	const ColourDesired blue(0, 0, 255);
	const ColourDesired green(0, 160, 0);
	const ColourDesired grey(200, 200, 200);

	// Background.
	surf->FillRectangle(PRectangle(0, 0, static_cast<XYPOSITION>(cx),
		static_cast<XYPOSITION>(cy)), white);

	// ---- 1. ORIENTATION -----------------------------------------------------
	// In Scintilla coordinates y grows DOWNWARD, so this rectangle is at the TOP.
	// If the flip is wrong it will appear at the BOTTOM of the window instead.
	surf->FillRectangle(PRectangle(10, 10, 210, 40), red);
	surf->DrawTextNoClip(PRectangle(220, 10, 620, 40), g_font, 32,
		"^ RED BAR MUST BE AT TOP", 24, black, white);

	// A green bar at the bottom in Scintilla coordinates, as the paired control.
	surf->FillRectangle(PRectangle(10, static_cast<XYPOSITION>(cy - 40),
		210, static_cast<XYPOSITION>(cy - 10)), green);
	surf->DrawTextNoClip(PRectangle(220, static_cast<XYPOSITION>(cy - 40), 620,
		static_cast<XYPOSITION>(cy - 10)), g_font,
		static_cast<XYPOSITION>(cy - 18), "v GREEN BAR MUST BE AT BOTTOM", 29, black, white);

	// ---- 2/3. TEXT + MEASURED BOX -------------------------------------------
	const char *msg = "Scintilla Surface on OS/2 Presentation Manager";
	const int msgLen = static_cast<int>(strlen(msg));
	const XYPOSITION baseline = 110;
	surf->DrawTextNoClip(PRectangle(20, 90, static_cast<XYPOSITION>(cx), 120),
		g_font, baseline, msg, msgLen, black, white);

	// Draw the measured extent around it. If WidthText/Ascent/Descent are right the
	// box hugs the string; if the metrics are wrong the box will be visibly off.
	const XYPOSITION w = surf->WidthText(g_font, msg, msgLen);
	const XYPOSITION asc = surf->Ascent(g_font);
	const XYPOSITION desc = surf->Descent(g_font);
	surf->PenColour(blue);
	surf->MoveTo(20, static_cast<int>(baseline - asc));
	surf->LineTo(static_cast<int>(20 + w), static_cast<int>(baseline - asc));
	surf->LineTo(static_cast<int>(20 + w), static_cast<int>(baseline + desc));
	surf->LineTo(20, static_cast<int>(baseline + desc));
	surf->LineTo(20, static_cast<int>(baseline - asc));

	// ---- 4. ALPHA -----------------------------------------------------------
	// Grey block, then a 50% RED wash over its right half.
	// Correct  -> the washed half looks PINK.
	// B/R swap -> it looks BLUE/violet, and the [unverified] byte-order comment in
	//             PlatPM.cxx is what needs changing.
	surf->FillRectangle(PRectangle(20, 150, 420, 210), grey);
	surf->AlphaRectangle(PRectangle(220, 150, 420, 210), 0, red, 128, red, 128, 0);
	surf->DrawTextNoClip(PRectangle(20, 215, 620, 245), g_font, 237,
		"left=grey  right=grey+50% RED (must look PINK)", 45, black, white);

	// ---- 5. WINDOW + LISTBOX readback ---------------------------------------
	if (g_listReport[0])
		surf->DrawTextNoClip(PRectangle(20, 250, 700, 280), g_font, 272,
			g_listReport, static_cast<int>(strlen(g_listReport)), black, white);

	surf->Release();
	delete surf;
}

MRESULT EXPENTRY ClientWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2) {
	switch (msg) {
	case WM_PAINT: {
		// Deferred to the first paint: at WM_CREATE the frame has not been sized yet,
		// so any position computed from the client rectangle would use a zero geometry.
		if (!g_listReport[0])
			MakeList(hwnd, g_listReport, sizeof(g_listReport));
		RECTL rcl;
		HPS hps = WinBeginPaint(hwnd, NULLHANDLE, &rcl);
		RECTL rclWin;
		WinQueryWindowRect(hwnd, &rclWin);
		PaintTest(hps, hwnd,
			static_cast<int>(rclWin.xRight - rclWin.xLeft),
			static_cast<int>(rclWin.yTop - rclWin.yBottom));
		WinEndPaint(hps);
		return 0;
	}
	case WM_ERASEBACKGROUND:
		return MRFROMLONG(TRUE);
	}
	return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

int main(void) {
	HAB hab = WinInitialize(0);
	HMQ hmq = WinCreateMsgQueue(hab, 0);

	static const char *szClass = "ScintillaPMHarness";
	WinRegisterClass(hab, (PSZ)szClass, ClientWndProc, CS_SIZEREDRAW, 0);

	ULONG flFrame = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER |
		FCF_MINMAX | FCF_TASKLIST;
	HWND hwndClient = NULLHANDLE;
	HWND hwndFrame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flFrame,
		(PSZ)szClass, (PSZ)"Scintilla PM Surface - render test", 0, NULLHANDLE, 0,
		&hwndClient);

	if (hwndFrame == NULLHANDLE) {
		WinDestroyMsgQueue(hmq);
		WinTerminate(hab);
		return 1;
	}

	WinSetWindowPos(hwndFrame, HWND_TOP, 40, 40, 760, 380,
		SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);

	QMSG qmsg;
	while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0))
		WinDispatchMsg(hab, &qmsg);

	WinDestroyWindow(hwndFrame);
	WinDestroyMsgQueue(hmq);
	WinTerminate(hab);
	return 0;
}

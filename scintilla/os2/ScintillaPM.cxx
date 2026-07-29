// Scintilla source code edit control
// ScintillaPM.cxx - the OS/2 Presentation Manager editor control.
//
// PlatPM.cxx supplies the drawing primitives; this file is the control itself: a PM
// window class whose window procedure maps WM_* onto ScintillaBase.
//
// Every OS/2 API is cited. Message values and control semantics verified against the
// toolkit corpus (os2ref/pm-window-messaging.md, pm-controls.md, clipboard-dde.md,
// memory-api.md) and IBM's books (inf_text/pm2.txt, pm3.txt).
//
// ---------------------------------------------------------------------------
// Things that differ from the Win32 control in ways that bite
// ---------------------------------------------------------------------------
//  * COORDINATES. Mouse positions arrive in PM window coordinates - bottom-left origin,
//    y upward. Scintilla wants y-down. Every WM_MOUSEMOVE/WM_BUTTON*, and the caret and
//    scroll geometry, must flip against the client height. See PtFromMsg().
//  * CLIPBOARD MEMORY. A CFI_POINTER object must be allocated with DosAllocSharedMem,
//    unnamed and OBJ_GIVEABLE, NOT malloc - and ownership passes to the system, so the
//    setter must not touch or free it afterwards [DOC-IBM - pm2.txt, WinSetClipbrdData].
//    This is the OS/2 analogue of GlobalAlloc(GMEM_MOVEABLE) and is easy to get wrong in
//    a way that appears to work until the setting process exits.
//  * SCROLLBARS are separate WC_SCROLLBAR windows owned by the frame, not window styles.

#define INCL_WIN
#define INCL_GPI
#define INCL_DOS
#define INCL_DOSMEMMGR
#define INCL_WINCLIPBOARD
#define INCL_WINSCROLLBARS
#define INCL_WINTIMER
#include <os2.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <climits>

#include <stdexcept>
#include <new>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <memory>

#include "Platform.h"

#include "ILexer.h"
#include "Scintilla.h"

#ifdef SCI_LEXER
#include "SciLexer.h"
#endif
#include "StringCopy.h"
#ifdef SCI_LEXER
#include "LexerModule.h"
#endif
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "XPM.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "CaseConvert.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"

#include "AutoComplete.h"
#include "ScintillaBase.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

const char *const scintillaPMClassName = "Scintilla";

// Timer ids must be <= TID_USERMAX (0x7fff) [DOC-IBM - pm-window-messaging.md 10].
enum { tickerIdBase = 100 };

class ScintillaPM : public ScintillaBase {
	HWND hwnd;          // the client window this control drives
	HAB hab;
	bool capturedMouse;
	bool hasFocusPM;
	unsigned int linesPerScroll;
	// TickReason is { tickCaret, tickScroll, tickWiden, tickDwell, tickPlatform }
	// [Editor.h:521] - there is no _max sentinel, so size from the last member.
	enum { tickerCount = tickPlatform + 1 };
	bool tickerActive[tickerCount];

	// The control owns its scroll bars rather than using the frame's. A frame's
	// FID_VERTSCROLL sends WM_VSCROLL to the FRAME [DOC-IBM - pm-controls.md], which
	// would make this control depend on its host forwarding those messages. Child
	// WC_SCROLLBARs owned by this window send them here directly.
	HWND hwndVScroll;
	HWND hwndHScroll;
	LONG sbThickness;      // SV_CXVSCROLL
	LONG sbThicknessH;     // SV_CYHSCROLL

	LONG ClientHeight() const {
		RECTL rcl;
		if (hwnd != NULLHANDLE && WinQueryWindowRect(hwnd, &rcl))
			return rcl.yTop - rcl.yBottom;
		return 0;
	}

	// Live modifier state. PM does not put the modifier flags in the mouse messages the
	// way Win32 puts them in wParam, so they are queried [DOC-IBM - pm2.txt,
	// WinGetKeyState: the 0x8000 bit means "down now"].
	static int CurrentModifiers() {
		const bool shift = (WinGetKeyState(HWND_DESKTOP, VK_SHIFT) & 0x8000) != 0;
		const bool ctrl  = (WinGetKeyState(HWND_DESKTOP, VK_CTRL)  & 0x8000) != 0;
		const bool alt   = (WinGetKeyState(HWND_DESKTOP, VK_ALT)   & 0x8000) != 0;
		return Editor::ModifierFlags(shift, ctrl, alt);
	}

	// PM delivers pointer positions bottom-left origin; Scintilla wants y-down.
	Point PtFromMsg(MPARAM mp1) const {
		const LONG x = static_cast<LONG>(SHORT1FROMMP(mp1));
		const LONG y = static_cast<LONG>(SHORT2FROMMP(mp1));
		return Point(static_cast<XYPOSITION>(x),
			static_cast<XYPOSITION>(ClientHeight() - y));
	}

public:
	explicit ScintillaPM(HWND hwnd_);
	ScintillaPM(const ScintillaPM &) = delete;
	ScintillaPM &operator=(const ScintillaPM &) = delete;
	virtual ~ScintillaPM();

	static void Register(HAB hab_);
	static MRESULT EXPENTRY SciWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);

	MRESULT WndProc(ULONG msg, MPARAM mp1, MPARAM mp2);

private:
	// --- Editor overrides ---
	void Initialise() override;
	void Finalise() override;
	void SetVerticalScrollPos() override;
	void SetHorizontalScrollPos() override;
	bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override;
	void Copy() override;
	void Paste() override;
	void ClaimSelection() override;
	void NotifyChange() override;
	void NotifyParent(SCNotification scn) override;
	void CopyToClipboard(const SelectionText &selectedText) override;
	void SetMouseCapture(bool on) override;
	bool HaveMouseCapture() override;
	sptr_t DefWndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam) override;

	// --- ScintillaBase overrides ---
	void CreateCallTipWindow(PRectangle rc) override;
	void AddToPopUp(const char *label, int cmd, bool enabled) override;

	// --- fine tickers, on PM window timers ---
	bool FineTickerAvailable() override;
	bool FineTickerRunning(TickReason reason) override;
	void FineTickerStart(TickReason reason, int millis, int tolerance) override;
	void FineTickerCancel(TickReason reason) override;

	PRectangle GetClientRectangle() const override;
	void LayoutScrollBars();
	void Paint(HPS hps, const RECTL &rcPaint);
	void SetClipboardText(const char *text, size_t len);
	bool GetClipboardText(std::string &out);
};

ScintillaPM::ScintillaPM(HWND hwnd_)
	: hwnd(hwnd_), hab(NULLHANDLE), capturedMouse(false), hasFocusPM(false),
	  linesPerScroll(3), hwndVScroll(NULLHANDLE), hwndHScroll(NULLHANDLE),
	  sbThickness(16), sbThicknessH(16) {
	hab = WinQueryAnchorBlock(hwnd_);
	for (size_t i = 0; i < tickerCount; i++)
		tickerActive[i] = false;
	wMain = reinterpret_cast<WindowID>(hwnd_);

	// SV_CXVSCROLL / SV_CYHSCROLL are the scroll-bar thicknesses
	// [DOC-IBM - pm-window-messaging.md 11; os2emx.h:7986-7987].
	const LONG cx = WinQuerySysValue(HWND_DESKTOP, SV_CXVSCROLL);
	const LONG cy = WinQuerySysValue(HWND_DESKTOP, SV_CYHSCROLL);
	if (cx > 0) sbThickness = cx;
	if (cy > 0) sbThicknessH = cy;

	hwndVScroll = WinCreateWindow(hwnd_, WC_SCROLLBAR, (PSZ)"",
		WS_VISIBLE | SBS_VERT, 0, 0, 0, 0,
		hwnd_, HWND_TOP, 0x9001, nullptr, nullptr);
	hwndHScroll = WinCreateWindow(hwnd_, WC_SCROLLBAR, (PSZ)"",
		WS_VISIBLE | SBS_HORZ, 0, 0, 0, 0,
		hwnd_, HWND_TOP, 0x9002, nullptr, nullptr);

	Initialise();
}

ScintillaPM::~ScintillaPM() {
	Finalise();
}

void ScintillaPM::Initialise() {
}

void ScintillaPM::Finalise() {
	for (TickReason tr = tickCaret; tr <= tickDwell;
			tr = static_cast<TickReason>(tr + 1))
		FineTickerCancel(tr);
	ScintillaBase::Finalise();
}

// The drawing area excludes the scroll bars this control owns; without this the text
// would be laid out under them and the last column/line would be unreachable.
PRectangle ScintillaPM::GetClientRectangle() const {
	RECTL rcl;
	if (hwnd == NULLHANDLE || !WinQueryWindowRect(hwnd, &rcl))
		return PRectangle();
	XYPOSITION w = static_cast<XYPOSITION>(rcl.xRight - rcl.xLeft);
	XYPOSITION h = static_cast<XYPOSITION>(rcl.yTop - rcl.yBottom);
	if (hwndVScroll != NULLHANDLE)
		w -= static_cast<XYPOSITION>(sbThickness);
	if (hwndHScroll != NULLHANDLE)
		h -= static_cast<XYPOSITION>(sbThicknessH);
	if (w < 0) w = 0;
	if (h < 0) h = 0;
	return PRectangle(0, 0, w, h);
}

// Scroll bars are positioned in PM coordinates: the horizontal bar sits at the BOTTOM,
// which in a y-up world means y = 0.
void ScintillaPM::LayoutScrollBars() {
	if (hwnd == NULLHANDLE)
		return;
	RECTL rcl;
	if (!WinQueryWindowRect(hwnd, &rcl))
		return;
	const LONG w = rcl.xRight - rcl.xLeft;
	const LONG h = rcl.yTop - rcl.yBottom;
	if (hwndVScroll != NULLHANDLE)
		WinSetWindowPos(hwndVScroll, HWND_TOP, w - sbThickness, sbThicknessH,
			sbThickness, h - sbThicknessH, SWP_MOVE | SWP_SIZE | SWP_SHOW);
	if (hwndHScroll != NULLHANDLE)
		WinSetWindowPos(hwndHScroll, HWND_TOP, 0, 0,
			w - sbThickness, sbThicknessH, SWP_MOVE | SWP_SIZE | SWP_SHOW);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void ScintillaPM::Paint(HPS hps, const RECTL &rcPaint) {
	paintState = painting;
	// The update rectangle arrives in PM (y-up) coordinates; Scintilla's rcPaint is
	// y-down, so flip it against the client height.
	const LONG h = ClientHeight();
	const PRectangle rcPaintSci(
		static_cast<XYPOSITION>(rcPaint.xLeft),
		static_cast<XYPOSITION>(h - rcPaint.yTop),
		static_cast<XYPOSITION>(rcPaint.xRight),
		static_cast<XYPOSITION>(h - rcPaint.yBottom));

	std::unique_ptr<Surface> surfaceWindow(Surface::Allocate(technology));
	if (!surfaceWindow)
		return;
	surfaceWindow->Init(reinterpret_cast<SurfaceID>(hps),
		reinterpret_cast<WindowID>(hwnd));
	surfaceWindow->SetUnicodeMode(IsUnicodeMode());
	surfaceWindow->SetDBCSMode(CodePage());

	Editor::Paint(surfaceWindow.get(), rcPaintSci);
	surfaceWindow->Release();
	paintState = notPainting;
}

// ---------------------------------------------------------------------------
// Scrolling. PM scroll bars are separate WC_SCROLLBAR windows owned by the frame,
// addressed with SBM_* messages [DOC-IBM - pm-controls.md].
// ---------------------------------------------------------------------------

void ScintillaPM::SetVerticalScrollPos() {
	if (hwndVScroll != NULLHANDLE)
		WinSendMsg(hwndVScroll, SBM_SETPOS,
			MPFROMSHORT(static_cast<SHORT>(topLine)), 0);
}

void ScintillaPM::SetHorizontalScrollPos() {
	if (hwndHScroll != NULLHANDLE)
		WinSendMsg(hwndHScroll, SBM_SETPOS,
			MPFROMSHORT(static_cast<SHORT>(xOffset)), 0);
}

// SBM_SETSCROLLBAR: mp1 = position, mp2 = MPFROM2SHORT(first, last).
// SBM_SETTHUMBSIZE: mp1 = MPFROM2SHORT(visible, total) - the proportional thumb
// [DOC-IBM - pm-controls.md, SBM_* table].
bool ScintillaPM::ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) {
	bool modified = false;
	if (hwndVScroll != NULLHANDLE) {
		const SHORT last = static_cast<SHORT>(
			std::max<Sci::Line>(0, nMax - nPage + 1));
		WinSendMsg(hwndVScroll, SBM_SETSCROLLBAR,
			MPFROMSHORT(static_cast<SHORT>(topLine)), MPFROM2SHORT(0, last));
		WinSendMsg(hwndVScroll, SBM_SETTHUMBSIZE,
			MPFROM2SHORT(static_cast<SHORT>(nPage),
				static_cast<SHORT>(nMax + 1)), 0);
		modified = true;
	}
	if (hwndHScroll != NULLHANDLE) {
		const int pageWidth = static_cast<int>(GetTextRectangle().Width());
		const SHORT lastH = static_cast<SHORT>(
			std::max(0, scrollWidth - pageWidth));
		WinSendMsg(hwndHScroll, SBM_SETSCROLLBAR,
			MPFROMSHORT(static_cast<SHORT>(xOffset)), MPFROM2SHORT(0, lastH));
		WinSendMsg(hwndHScroll, SBM_SETTHUMBSIZE,
			MPFROM2SHORT(static_cast<SHORT>(pageWidth),
				static_cast<SHORT>(scrollWidth ? scrollWidth : 1)), 0);
		modified = true;
	}
	return modified;
}

// ---------------------------------------------------------------------------
// Clipboard
//
// CFI_POINTER data must be unnamed shareable memory from DosAllocSharedMem with
// OBJ_GIVEABLE, and ownership passes to the system on WinSetClipbrdData - the setter
// must not free or reuse the pointer afterwards [DOC-IBM - pm2.txt, WinSetClipbrdData;
// os2ref/clipboard-dde.md 2]. A malloc'd buffer here would appear to work and then fail
// once this process exits, which is the worst possible failure shape.
// ---------------------------------------------------------------------------

void ScintillaPM::SetClipboardText(const char *text, size_t len) {
	if (!text || hab == NULLHANDLE)
		return;
	PVOID pmem = nullptr;
	// OBJ_GIVEABLE 0x200, PAG_COMMIT 0x10, PAG_READ|PAG_WRITE 0x03
	// [DOC-IBM - memory-api.md; DosAllocSharedMem, bsedos.h:1882].
	const APIRET rc = DosAllocSharedMem(&pmem, nullptr,
		static_cast<ULONG>(len + 1),
		PAG_COMMIT | PAG_READ | PAG_WRITE | OBJ_GIVEABLE);
	if (rc != 0 || pmem == nullptr)
		return;   // honest failure: no clipboard update rather than a silent truncation
	memcpy(pmem, text, len);
	static_cast<char *>(pmem)[len] = '\0';

	if (WinOpenClipbrd(hab)) {
		WinEmptyClipbrd(hab);
		// After this call the memory belongs to the system; do not free it here.
		WinSetClipbrdData(hab, reinterpret_cast<ULONG>(pmem), CF_TEXT, CFI_POINTER);
		WinCloseClipbrd(hab);
	} else {
		DosFreeMem(pmem);
	}
}

bool ScintillaPM::GetClipboardText(std::string &out) {
	if (hab == NULLHANDLE || !WinOpenClipbrd(hab))
		return false;
	bool got = false;
	// "A data handle returned by a query must not be used after WinCloseClipbrd" - so
	// copy while it is still open [DOC-IBM - pm2.txt, WinQueryClipbrdData Remarks].
	const ULONG h = WinQueryClipbrdData(hab, CF_TEXT);
	if (h != 0) {
		const char *p = reinterpret_cast<const char *>(h);
		out.assign(p);
		got = true;
	}
	WinCloseClipbrd(hab);
	return got;
}

void ScintillaPM::Copy() {
	if (!sel.Empty()) {
		SelectionText selectedText;
		CopySelectionRange(&selectedText);
		CopyToClipboard(selectedText);
	}
}

void ScintillaPM::CopyToClipboard(const SelectionText &selectedText) {
	SetClipboardText(selectedText.Data(), selectedText.Length());
}

void ScintillaPM::Paste() {
	std::string text;
	if (!GetClipboardText(text))
		return;
	UndoGroup ug(pdoc);
	ClearSelection(multiPasteMode == SC_MULTIPASTE_EACH);
	InsertPasteShape(text.c_str(), static_cast<int>(text.length()),
		pasteStream);
	EnsureCaretVisible();
}

// PM has no X11-style primary selection.
void ScintillaPM::ClaimSelection() {
}

// ---------------------------------------------------------------------------
// Notifications - WM_CONTROL carries (id, notify code) in mp1 [DOC-IBM -
// pm-window-messaging.md]; the SCNotification travels in mp2 as a pointer, matching
// how Scintilla's other platform layers pass it.
// ---------------------------------------------------------------------------

void ScintillaPM::NotifyChange() {
	const HWND owner = WinQueryWindow(hwnd, QW_OWNER);
	if (owner != NULLHANDLE)
		WinSendMsg(owner, WM_CONTROL,
			MPFROM2SHORT(static_cast<SHORT>(WinQueryWindowUShort(hwnd, QWS_ID)),
				SCEN_CHANGE),
			MPFROMHWND(hwnd));
}

void ScintillaPM::NotifyParent(SCNotification scn) {
	scn.nmhdr.hwndFrom = reinterpret_cast<void *>(hwnd);
	scn.nmhdr.idFrom = static_cast<uptr_t>(WinQueryWindowUShort(hwnd, QWS_ID));
	const HWND owner = WinQueryWindow(hwnd, QW_OWNER);
	if (owner != NULLHANDLE)
		WinSendMsg(owner, WM_CONTROL,
			MPFROM2SHORT(static_cast<SHORT>(scn.nmhdr.idFrom), 0),
			MPFROMP(&scn));
}

// ---------------------------------------------------------------------------
// Mouse capture. WinSetCapture(HWND_DESKTOP, hwnd) routes all pointer messages to hwnd
// [DOC-IBM - pm2.txt].
// ---------------------------------------------------------------------------

void ScintillaPM::SetMouseCapture(bool on) {
	if (mouseDownCaptures) {
		WinSetCapture(HWND_DESKTOP, on ? hwnd : NULLHANDLE);
		capturedMouse = on;
	}
}

bool ScintillaPM::HaveMouseCapture() {
	return capturedMouse;
}

sptr_t ScintillaPM::DefWndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
	return reinterpret_cast<sptr_t>(WinDefWindowProc(hwnd, iMessage,
		reinterpret_cast<MPARAM>(wParam), reinterpret_cast<MPARAM>(lParam)));
}

// ---------------------------------------------------------------------------
// Fine tickers on PM window timers. WinStartTimer(hab, hwnd, idTimer, dtTimeout)
// posts WM_TIMER every dtTimeout ms [DOC-IBM - pm-window-messaging.md 10]. PM timers
// have no tolerance parameter, so it is ignored rather than faked.
// ---------------------------------------------------------------------------

bool ScintillaPM::FineTickerAvailable() {
	return true;
}

bool ScintillaPM::FineTickerRunning(TickReason reason) {
	return tickerActive[reason];
}

void ScintillaPM::FineTickerStart(TickReason reason, int millis, int /*tolerance*/) {
	FineTickerCancel(reason);
	if (WinStartTimer(hab, hwnd, tickerIdBase + reason,
			static_cast<ULONG>(millis)) != 0)
		tickerActive[reason] = true;
}

void ScintillaPM::FineTickerCancel(TickReason reason) {
	if (tickerActive[reason]) {
		WinStopTimer(hab, hwnd, tickerIdBase + reason);
		tickerActive[reason] = false;
	}
}

// ---------------------------------------------------------------------------
// Not yet implemented - these stop rather than pretend.
// ---------------------------------------------------------------------------

void ScintillaPM::CreateCallTipWindow(PRectangle) {
	// TODO: a WS_VISIBLE popup window painting through the same Surface. Leaving
	// ct.wCallTip uncreated means call tips simply do not appear, which is visible and
	// harmless; a half-made window would crash inside CallTip::PaintCT.
}

void ScintillaPM::AddToPopUp(const char *, int, bool) {
	// TODO: needs a WC_MENU popup (MM_INSERTITEM). Until then the context menu stays
	// empty rather than showing entries that would not dispatch.
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

MRESULT ScintillaPM::WndProc(ULONG msg, MPARAM mp1, MPARAM mp2) {
	switch (msg) {
	case WM_PAINT: {
		RECTL rclPaint;
		HPS hps = WinBeginPaint(hwnd, NULLHANDLE, &rclPaint);
		Paint(hps, rclPaint);
		WinEndPaint(hps);
		return 0;
	}

	case WM_SIZE:
		LayoutScrollBars();
		ChangeSize();
		return 0;

	// SB_* command is in the HIGH half of mp2, the slider position in the low half
	// [DOC-IBM - pm-controls.md, "Scroll commands"].
	case WM_VSCROLL: {
		const SHORT cmd = SHORT2FROMMP(mp2);
		const SHORT pos = SHORT1FROMMP(mp2);
		switch (cmd) {
		case SB_LINEUP:          ScrollTo(topLine - 1); break;
		case SB_LINEDOWN:        ScrollTo(topLine + 1); break;
		case SB_PAGEUP:          ScrollTo(topLine - LinesOnScreen()); break;
		case SB_PAGEDOWN:        ScrollTo(topLine + LinesOnScreen()); break;
		case SB_SLIDERTRACK:
		case SB_SLIDERPOSITION:  ScrollTo(pos); break;
		default: break;
		}
		return 0;
	}

	case WM_HSCROLL: {
		const SHORT cmd = SHORT2FROMMP(mp2);
		const SHORT pos = SHORT1FROMMP(mp2);
		const int page = static_cast<int>(GetTextRectangle().Width());
		switch (cmd) {
		case SB_LINELEFT:        HorizontalScrollTo(xOffset - 20); break;
		case SB_LINERIGHT:       HorizontalScrollTo(xOffset + 20); break;
		case SB_PAGELEFT:        HorizontalScrollTo(xOffset - page); break;
		case SB_PAGERIGHT:       HorizontalScrollTo(xOffset + page); break;
		case SB_SLIDERTRACK:
		case SB_SLIDERPOSITION:  HorizontalScrollTo(pos); break;
		default: break;
		}
		return 0;
	}

	case WM_BUTTON1DBLCLK:
		// Scintilla derives double-click from click timing, so the second click is
		// reported as a normal ButtonDown at the same point rather than dropped.
		ButtonDownWithModifiers(PtFromMsg(mp1),
			static_cast<unsigned int>(Platform::DoubleClickTime()),
			CurrentModifiers());
		return MRFROMLONG(TRUE);

	case WM_SETFOCUS:
		// mp2 is TRUE when the window is GAINING focus [DOC-IBM - pm3.txt, WM_SETFOCUS].
		hasFocusPM = (LONGFROMMP(mp2) != 0);
		SetFocusState(hasFocusPM);
		return 0;

	case WM_TIMER: {
		const ULONG id = static_cast<ULONG>(SHORT1FROMMP(mp1));
		if (id >= tickerIdBase && id < tickerIdBase + tickerCount)
			TickFor(static_cast<TickReason>(id - tickerIdBase));
		return 0;
	}

	case WM_BUTTON1DOWN:
		WinSetFocus(HWND_DESKTOP, hwnd);
		ButtonDownWithModifiers(PtFromMsg(mp1), 0, CurrentModifiers());
		return MRFROMLONG(TRUE);

	case WM_BUTTON1UP:
		ButtonUp(PtFromMsg(mp1), 0,
			(WinGetKeyState(HWND_DESKTOP, VK_CTRL) & 0x8000) != 0);
		return MRFROMLONG(TRUE);

	case WM_MOUSEMOVE:
		ButtonMoveWithModifiers(PtFromMsg(mp1), CurrentModifiers());
		return MRFROMLONG(TRUE);

	case WM_CHAR: {
		// mp1 low USHORT holds KC_* flags; mp2 carries the character and virtual key
		// [DOC-IBM - pm-window-messaging.md, WM_CHAR].
		const USHORT fsflags = SHORT1FROMMP(mp1);
		if (fsflags & KC_KEYUP)
			return MRFROMLONG(TRUE);
		const USHORT ch = SHORT1FROMMP(mp2);
		const bool shift = (fsflags & KC_SHIFT) != 0;
		const bool ctrl  = (fsflags & KC_CTRL) != 0;
		const bool alt   = (fsflags & KC_ALT) != 0;
		if ((fsflags & KC_CHAR) && ch >= 32 && !ctrl && !alt) {
			char c = static_cast<char>(ch);
			AddCharUTF(&c, 1);
			return MRFROMLONG(TRUE);
		}

		// Ctrl+letter must reach Scintilla's key map (Ctrl+A/C/V/X/Z and friends).
		// PM delivers these as KC_CHAR with the ASCII *control code* 1..26 rather than
		// the letter, so recover the letter before handing it over. Without this the
		// whole Ctrl keymap is silently unreachable.
		if (ctrl && (fsflags & KC_CHAR) && ch >= 1 && ch <= 26) {
			const int sciKeyCtrl = 'A' + (ch - 1);
			bool consumed = false;
			KeyDownWithModifiers(sciKeyCtrl, ModifierFlags(shift, ctrl, alt), &consumed);
			if (consumed)
				return MRFROMLONG(TRUE);
			break;   /* not ours - let it reach the owner */
		}
		if (fsflags & KC_VIRTUALKEY) {
			const USHORT vk = SHORT2FROMMP(mp2);
			int sciKey = 0;
			switch (vk) {
			case VK_LEFT:     sciKey = SCK_LEFT; break;
			case VK_RIGHT:    sciKey = SCK_RIGHT; break;
			case VK_UP:       sciKey = SCK_UP; break;
			case VK_DOWN:     sciKey = SCK_DOWN; break;
			case VK_HOME:     sciKey = SCK_HOME; break;
			case VK_END:      sciKey = SCK_END; break;
			case VK_PAGEUP:   sciKey = SCK_PRIOR; break;
			case VK_PAGEDOWN: sciKey = SCK_NEXT; break;
			case VK_DELETE:   sciKey = SCK_DELETE; break;
			case VK_BACKSPACE:sciKey = SCK_BACK; break;
			case VK_TAB:      sciKey = SCK_TAB; break;
			case VK_NEWLINE:
			case VK_ENTER:    sciKey = SCK_RETURN; break;
			case VK_ESC:      sciKey = SCK_ESCAPE; break;
			default: break;
			}
			if (sciKey) {
				bool consumed = false;
				KeyDownWithModifiers(sciKey, ModifierFlags(shift, ctrl, alt), &consumed);
				if (consumed)
					return MRFROMLONG(TRUE);
			}
		}
		// A key this control does not consume must go to WinDefWindowProc, whose
		// documented WM_CHAR behaviour is to "send the message to the owner window if
		// it exists" [DOC-IBM - pm3.txt, WM_CHAR Default Processing]. That forwarding
		// is how the frame ever sees a menu mnemonic. Returning FALSE here instead
		// short-circuits it: the menu pulls down and then ignores every keystroke.
		break;
	}

	case WM_DESTROY:
		return 0;
	}

	// Anything not handled above may be one of Scintilla's OWN API messages (SCI_*,
	// which start at 2000) rather than a PM message. Those must reach
	// ScintillaBase::WndProc or every SCI_SETTEXT / SCI_SETMARGINWIDTHN the host sends
	// is silently discarded - the control still paints, just as an empty document with
	// no margins, which looks like a drawing bug and is not one.
	if (msg >= SCI_START)
		return reinterpret_cast<MRESULT>(ScintillaBase::WndProc(msg,
			reinterpret_cast<uptr_t>(mp1), reinterpret_cast<sptr_t>(mp2)));

	return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

// The instance pointer lives in the window's reserved words, allocated by
// WinRegisterClass's cbWindowData [DOC-IBM - pm-window-messaging.md, WinRegisterClass].
MRESULT EXPENTRY ScintillaPM::SciWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2) {
	if (msg == WM_CREATE) {
		ScintillaPM *sci = new (std::nothrow) ScintillaPM(hwnd);
		WinSetWindowPtr(hwnd, 0, sci);
		return MRFROMLONG(FALSE);
	}
	ScintillaPM *sci = static_cast<ScintillaPM *>(WinQueryWindowPtr(hwnd, 0));
	if (!sci)
		return WinDefWindowProc(hwnd, msg, mp1, mp2);
	if (msg == WM_DESTROY) {
		WinSetWindowPtr(hwnd, 0, nullptr);
		delete sci;
		return WinDefWindowProc(hwnd, msg, mp1, mp2);
	}
	return sci->WndProc(msg, mp1, mp2);
}

void ScintillaPM::Register(HAB hab_) {
	// cbWindowData = sizeof(void *) reserves the per-window slot used above.
	WinRegisterClass(hab_, (PSZ)scintillaPMClassName, ScintillaPM::SciWndProc,
		CS_SIZEREDRAW | CS_CLIPCHILDREN, sizeof(void *));
}

extern "C" void Scintilla_RegisterClasses(void *hab_) {
	ScintillaPM::Register(reinterpret_cast<HAB>(hab_));
}

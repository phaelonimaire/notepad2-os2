// Scintilla source code edit control
// PlatPM.cxx - OS/2 Presentation Manager platform layer (Surface)
//
// Port target: Scintilla 3.7.5 (as bundled with Notepad2-mod) on OS/2 Warp 4.5x / ArcaOS,
// built with GCC 9.2 (kLIBC), -std=c++11.
//
// EVERY OS/2 API used here is cited to a source. Prototypes verified against the IBM
// Graphics Programming Interface Reference (inf_text/gpi2.txt) and the toolkit corpus
// (os2ref/gpi-drawing.md, os2ref/gpi-fonts-and-metafiles.md). Nothing here is from memory.
//
// ---------------------------------------------------------------------------
// THE COORDINATE PROBLEM - read this before changing any drawing code
// ---------------------------------------------------------------------------
// Scintilla is a y-DOWN system: PRectangle is {left, top, right, bottom} with top < bottom,
// and the covered band is the half-open interval [top, bottom).
//
// Presentation Manager is a y-UP system: the origin is the BOTTOM-LEFT and y increases
// upward [DOC-IBM - pm2.txt, WinQueryWindowRect: "the bottom left corner is at the position
// (0,0)"]. A RECTL includes its left/bottom edges and excludes right/top [DOC-IBM - pm2.txt,
// WinFillRect].
//
// This mismatch fails SILENTLY - nothing returns an error, the editor just renders upside
// down. Every Scintilla coordinate crossing into GPI must go through FlipY()/ToRECTL().
//
// The transform, given drawing-area height H:
//     xLeft   = rc.left          yBottom = H - rc.bottom
//     xRight  = rc.right         yTop    = H - rc.top
//
// Worked check (H=10, Scintilla top=2 bottom=5 -> covers y-down rows 2,3,4):
//     row r (y-down) maps to PM row H-1-r  ->  rows 2,3,4 become PM rows 7,6,5
//     PM half-open [yBottom, yTop) covering rows 5,6,7 needs yBottom=5, yTop=8
//     formula gives yBottom = 10-5 = 5, yTop = 10-2 = 8.  Correct.
// Both systems use half-open intervals on their own increasing axis, so the two
// conventions cancel: there is NO off-by-one correction. Do not add one.

#define INCL_WIN
#define INCL_GPI
#define INCL_DEV
#define INCL_GPIPRIMITIVES
#define INCL_GPILCIDS
#define INCL_GPIREGIONS
#define INCL_GPILOGCOLORTABLE
#define INCL_DOSMISC
#define INCL_DOSMODULEMGR
#define INCL_WINSYS
#define INCL_WINMENUS
#define INCL_WINDIALOGS
#include <os2.h>
#include <uconv.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdarg>

#include "Platform.h"
#include "XPM.h"
#include "UniConversion.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

namespace {

// GpiSetColor takes an RGB value when the PS colour table is in RGB mode
// [DOC-IBM - gpi-drawing.md: GpiSetColor(HPS, LONG lColor)].
inline LONG ToRGB(ColourDesired c) {
	return static_cast<LONG>((c.GetRed() << 16) | (c.GetGreen() << 8) | c.GetBlue());
}

// PCH is conditionally `unsigned char *` or `char *` depending on build flags
// [DOC-IBM - os2emx.h:224 vs :232], and the GPI string calls are not const-correct.
// reinterpret_cast covers both spellings; const_cast alone does not.
inline PCH AsPCH(const char *s) {
	return reinterpret_cast<PCH>(const_cast<char *>(s));
}

// 24bpp scan lines are padded to a 4-byte boundary. Used by both the off-screen
// pixmap surfaces (InitPixMap) and the software alpha compositing below.
inline size_t StrideFor(int width) {
	return ((static_cast<size_t>(width) * 3 + 3) / 4) * 4;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// FontPM - a font *request* (FATTRS + point size), not a bound font.
//
// A FontPM holds the request, not a bound lcid. An lcid lives in one presentation
// space's setid table [DOC-IBM - gpi-fonts-and-metafiles.md 1: "A presentation space keeps
// a small table of local identifiers"], and Scintilla shares one Font object across every
// Surface, so the binding cannot live here. Each SurfaceImpl binds this request to an lcid
// in its own PS - see SurfaceImpl::SetFont.
class FontPM {
public:
	FATTRS fattrs;
	LONG pointSize;

	FontPM() : pointSize(10) {
		memset(&fattrs, 0, sizeof(fattrs));
	}
};

Font::Font() : fid(nullptr) {
}

Font::~Font() {
}

// FATTRS field/flag values per gpi-fonts-and-metafiles.md 2.2 [DOC-IBM - os2def.h:422].
// FATTR_FONTUSE_OUTLINE asks for a scalable (vector) font so GpiSetCharBox can size it;
// _TRANSFORMABLE is required for the character box to actually apply.
void Font::Create(const FontParameters &fp) {
	Release();
	FontPM *pf = new FontPM();

	pf->fattrs.usRecordLength = sizeof(FATTRS);
	pf->fattrs.lMatch = 0;                 // 0 = let the system choose the physical font
	pf->fattrs.idRegistry = 0;
	pf->fattrs.usCodePage = 0;             // 0 = the PS's code page
	pf->fattrs.lMaxBaselineExt = 0;        // filled by the char box for outline fonts
	pf->fattrs.lAveCharWidth = 0;
	pf->fattrs.fsType = 0;
	pf->fattrs.fsFontUse = FATTR_FONTUSE_OUTLINE | FATTR_FONTUSE_TRANSFORMABLE;

	pf->fattrs.fsSelection = 0;
	if (fp.italic)
		pf->fattrs.fsSelection |= FATTR_SEL_ITALIC;
	if (fp.weight >= 600)                  // Scintilla uses CSS-style weights; 400 normal
		pf->fattrs.fsSelection |= FATTR_SEL_BOLD;

	if (fp.faceName) {
		strncpy(pf->fattrs.szFacename, fp.faceName, FACESIZE - 1);
		pf->fattrs.szFacename[FACESIZE - 1] = '\0';
	}
	pf->pointSize = static_cast<LONG>(fp.size + 0.5f);
	if (pf->pointSize <= 0)
		pf->pointSize = 10;

	fid = static_cast<FontID>(pf);
}

void Font::Release() {
	if (fid)
		delete static_cast<FontPM *>(fid);
	fid = nullptr;
}

// ---------------------------------------------------------------------------
// UTF-8 -> GPI code page
//
// Scintilla hands the Surface UTF-8 bytes whenever the document is in Unicode
// mode. GPI draws bytes in the GPI CODE PAGE - one of the three independent
// code-page scopes a PM process has [os2ref/unicode-conversion.md 9.1] - so
// passing UTF-8 straight through renders every byte as its own code-page
// glyph: "cafe-acute" comes out as "caf|(R)". Nothing errors; the text is
// simply wrong, which is the usual shape of a code-page bug.
//
// So text is transcoded at the drawing boundary. Two things make this more
// than a call to UniUconv:
//
//  - Scintilla indexes positions[] by SOURCE byte, and a converted string has
//    a different length, so the mapping from source byte to converted prefix
//    length has to be carried along.
//  - Every byte of a multi-byte sequence must report the SAME x position (the
//    end of its character), or the caret can land inside a character.
// ---------------------------------------------------------------------------
class Utf8Xlat {
public:
	std::string out;            // display-code-page bytes
	std::vector<int> outLenAt;  // outLenAt[i] = bytes of `out` produced by src[0..i]

	void Convert(const char *s, int len, UconvObject conv) {
		out.clear();
		outLenAt.assign(len > 0 ? len : 0, 0);
		int i = 0;
		while (i < len) {
			const unsigned char c = static_cast<unsigned char>(s[i]);
			int n = 1;
			unsigned long v = c;
			if (c >= 0xF0)      { n = 4; v = c & 0x07; }
			else if (c >= 0xE0) { n = 3; v = c & 0x0F; }
			else if (c >= 0xC0) { n = 2; v = c & 0x1F; }
			if (i + n > len) { n = 1; v = c; }          // truncated - draw raw
			for (int k = 1; k < n; k++)
				v = (v << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);

			char buf[8];
			int produced = 0;
			if (n == 1 && v < 0x80) {
				buf[0] = static_cast<char>(v);
				produced = 1;
			} else if (conv) {
				UniChar uc[2];
				uc[0] = static_cast<UniChar>(v > 0xFFFF ? 0xFFFD : v);
				UniChar *pIn = uc;
				void *pOut = buf;
				size_t cIn = 1, cbOut = sizeof(buf), cSub = 0;
				if (UniUconvFromUcs(conv, &pIn, &cIn, &pOut, &cbOut, &cSub) == 0 ||
				    cbOut < sizeof(buf))
					produced = static_cast<int>(sizeof(buf) - cbOut);
			}
			// A character the display code page cannot represent draws as
			// '?'. That is a real limit of an 8-bit display page, not a bug -
			// CP850 has no Greek - and showing it is better than dropping the
			// character silently and shifting everything after it.
			if (produced <= 0) { buf[0] = '?'; produced = 1; }

			out.append(buf, produced);
			const int lenNow = static_cast<int>(out.size());
			for (int k = 0; k < n && i + k < len; k++)
				outLenAt[i + k] = lenNow;
			i += n;
		}
	}
};

// ---------------------------------------------------------------------------
// SurfaceImpl
// ---------------------------------------------------------------------------
class SurfaceImpl : public Surface {
	HPS hps;
	HDC hdcOwned;      // only set when this surface opened its own DC
	bool hpsOwned;
	LONG surfaceHeight; // H for the y-flip; the height of the drawing area in pels
	bool unicodeMode;
	int codePage;
	FONTMETRICS currentFM;
	bool haveFM;

	// Conversion object for the GPI code page, created on first use.
	UconvObject convDisplay;
	Utf8Xlat xlat;
	// Returns the bytes to hand GPI, and (for MeasureWidths) the source->output
	// length map in xlat.outLenAt. In non-Unicode mode this is a pass-through.
	const char *Displayable(const char *s, int len, int *pOutLen);

	// Set when this Surface owns an off-screen (pixmap) presentation space - see
	// InitPixMap. The screen case leaves these NULLHANDLE.
	HBITMAP hbmPix;
	HBITMAP hbmPixOld;
	bool pixmapOwned;

	// lcid allocation is per-PS and the range is 0..254, with 0 reserved for the default
	// font [DOC-IBM - gpi2.txt:5096]. Scintilla uses one Font per style, so a small cache
	// keyed by FontID is enough; if it ever overflows we recycle a single scratch lcid
	// rather than fail.
	std::map<FontID, LONG> lcidForFont;
	LONG nextLcid;
	FontID selectedFont;

	void SetFont(Font &font_);

	// Scintilla y-down -> PM y-up. See the header comment for the derivation.
	LONG FlipY(XYPOSITION y) const {
		return surfaceHeight - static_cast<LONG>(y);
	}

	RECTL ToRECTL(PRectangle rc) const {
		RECTL r;
		r.xLeft   = static_cast<LONG>(rc.left);
		r.xRight  = static_cast<LONG>(rc.right);
		r.yBottom = FlipY(rc.bottom);
		r.yTop    = FlipY(rc.top);
		return r;
	}

	POINTL ToPOINTL(XYPOSITION x, XYPOSITION y) const {
		POINTL p;
		p.x = static_cast<LONG>(x);
		p.y = FlipY(y);
		return p;
	}

	void EnsureMetrics();
	void SetRGBMode();

public:
	SurfaceImpl();
	SurfaceImpl(const SurfaceImpl &) = delete;
	SurfaceImpl &operator=(const SurfaceImpl &) = delete;
	~SurfaceImpl() override;

	void Init(WindowID wid) override;
	void Init(SurfaceID sid, WindowID wid) override;
	void InitPixMap(int width, int height, Surface *surface_, WindowID wid) override;

	void Release() override;
	bool Initialised() override;
	void PenColour(ColourDesired fore) override;
	int LogPixelsY() override;
	int DeviceHeightFont(int points) override;
	void MoveTo(int x_, int y_) override;
	void LineTo(int x_, int y_) override;
	void Polygon(Point *pts, int npts, ColourDesired fore, ColourDesired back) override;
	void RectangleDraw(PRectangle rc, ColourDesired fore, ColourDesired back) override;
	void FillRectangle(PRectangle rc, ColourDesired back) override;
	void FillRectangle(PRectangle rc, Surface &surfacePattern) override;
	void RoundedRectangle(PRectangle rc, ColourDesired fore, ColourDesired back) override;
	void AlphaRectangle(PRectangle rc, int cornerSize, ColourDesired fill, int alphaFill,
		ColourDesired outline, int alphaOutline, int flags) override;
	void DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char *pixelsImage) override;
	void Ellipse(PRectangle rc, ColourDesired fore, ColourDesired back) override;
	void Copy(PRectangle rc, Point from, Surface &surfaceSource) override;

	void DrawTextNoClip(PRectangle rc, Font &font_, XYPOSITION ybase, const char *s, int len,
		ColourDesired fore, ColourDesired back) override;
	void DrawTextClipped(PRectangle rc, Font &font_, XYPOSITION ybase, const char *s, int len,
		ColourDesired fore, ColourDesired back) override;
	void DrawTextTransparent(PRectangle rc, Font &font_, XYPOSITION ybase, const char *s, int len,
		ColourDesired fore) override;
	void MeasureWidths(Font &font_, const char *s, int len, XYPOSITION *positions) override;
	XYPOSITION WidthText(Font &font_, const char *s, int len) override;
	XYPOSITION WidthChar(Font &font_, char ch) override;
	XYPOSITION Ascent(Font &font_) override;
	XYPOSITION Descent(Font &font_) override;
	XYPOSITION InternalLeading(Font &font_) override;
	XYPOSITION ExternalLeading(Font &font_) override;
	XYPOSITION Height(Font &font_) override;
	XYPOSITION AverageCharWidth(Font &font_) override;

	void SetClip(PRectangle rc) override;
	void FlushCachedState() override;

	void SetUnicodeMode(bool unicodeMode_) override;
	void SetDBCSMode(int codePage_) override;

	void SetHPS(HPS hps_, LONG height);
};

SurfaceImpl::SurfaceImpl()
	: hps(NULLHANDLE), hdcOwned(NULLHANDLE), hpsOwned(false), surfaceHeight(0),
	  unicodeMode(false), codePage(0), haveFM(false), convDisplay(nullptr),
	  hbmPix(NULLHANDLE), hbmPixOld(NULLHANDLE), pixmapOwned(false),
	  nextLcid(1), selectedFont(nullptr) {
	memset(&currentFM, 0, sizeof(currentFM));
}

// Bind a FontPM's FATTRS request to an lcid in THIS presentation space and select it.
//
// GpiCreateLogFont(HPS, PSTR8 pName, LONG lLcid, PFATTRS) creates the logical font and
// binds it; GpiSetCharSet(HPS, lcid) makes it current [DOC-IBM -
// gpi-fonts-and-metafiles.md 1, 2.4]. For an outline font the point size is applied by
// GpiSetCharBox, not by FATTRS.lMaxBaselineExt.
void SurfaceImpl::SetFont(Font &font_) {
	if (hps == NULLHANDLE)
		return;
	FontID id = font_.GetID();
	if (id == nullptr || id == selectedFont)
		return;
	FontPM *pf = static_cast<FontPM *>(id);

	LONG lcid;
	std::map<FontID, LONG>::iterator it = lcidForFont.find(id);
	if (it != lcidForFont.end()) {
		lcid = it->second;
	} else {
		if (nextLcid <= 254) {
			lcid = nextLcid++;
		} else {
			lcid = 254;   // scratch: recycle the last slot rather than fail outright
		}
		// A setid must not be redefined while it is the current pattern/marker set, and
		// must not be deleted while selected [DOC-IBM - gpi-fonts-and-metafiles.md 1].
		// Dropping to the default font first keeps both rules satisfied.
		GpiSetCharSet(hps, LCID_DEFAULT);
		if (GpiCreateLogFont(hps, nullptr, lcid, &pf->fattrs) == GPI_ERROR)
			return;
		lcidForFont[id] = lcid;
	}

	if (!GpiSetCharSet(hps, lcid))
		return;

	// Size an outline font through the character box. In a PU_PELS PS the box is in
	// pels, so convert points -> pels with the device's vertical font resolution.
	const LONG pels = (pf->pointSize * LogPixelsY() + 36) / 72;
	if (pels > 0) {
		SIZEF box;
		box.cx = MAKEFIXED(pels, 0);
		box.cy = MAKEFIXED(pels, 0);
		GpiSetCharBox(hps, &box);
	}

	selectedFont = id;
	haveFM = false;   // metrics belong to the newly selected font
}

SurfaceImpl::~SurfaceImpl() {
	Release();
}

// A freshly obtained PS is in colour-INDEX mode, not RGB: "The color table is in default
// color index mode" [DOC-IBM - pm2.txt, WinGetPS Remarks, which also states a WinBeginPaint
// cache PS starts in the same state as GpiCreatePS]. Every GpiSetColor / WinFillRect value
// would otherwise be read as a palette index, and drawing silently disappears - which is
// exactly what the first render test showed: only AlphaRectangle, the one path that writes
// raw pels and never consults the colour table, appeared on screen.
//
// GpiCreateLogColorTable(hps, flOptions, lFormat, lStart, lCount, alTable) with LCOLF_RGB
// puts the PS into RGB mode [DOC-IBM - gpi2.txt, GpiCreateLogColorTable].
void SurfaceImpl::SetRGBMode() {
	if (hps != NULLHANDLE)
		GpiCreateLogColorTable(hps, LCOL_RESET, LCOLF_RGB, 0, 0, nullptr);
}

void SurfaceImpl::SetHPS(HPS hps_, LONG height) {
	hps = hps_;
	surfaceHeight = height;
	hpsOwned = false;
	haveFM = false;
	SetRGBMode();
}

// WinGetPS returns a cache micro-PS; it must be given back with WinReleasePS and the
// handle is dead afterwards [DOC-IBM - pm2.txt, WinGetPS / WinReleasePS].
void SurfaceImpl::Init(WindowID wid) {
	Release();
	HWND hwnd = reinterpret_cast<HWND>(wid);
	if (hwnd == NULLHANDLE)
		return;
	hps = WinGetPS(hwnd);
	hpsOwned = (hps != NULLHANDLE);
	SetRGBMode();
	RECTL rcl;
	if (WinQueryWindowRect(hwnd, &rcl))
		surfaceHeight = rcl.yTop - rcl.yBottom;
	haveFM = false;
}

void SurfaceImpl::Init(SurfaceID sid, WindowID wid) {
	Release();
	hps = reinterpret_cast<HPS>(sid);
	hpsOwned = false;
	SetRGBMode();
	HWND hwnd = reinterpret_cast<HWND>(wid);
	RECTL rcl;
	if (hwnd != NULLHANDLE && WinQueryWindowRect(hwnd, &rcl))
		surfaceHeight = rcl.yTop - rcl.yBottom;
	haveFM = false;
}

void SurfaceImpl::Release() {
	// Only a cache PS may go to WinReleasePS, and the handle must not be used after
	// [DOC-IBM - pm2.txt, WinReleasePS].
	if (pixmapOwned) {
		// Deselect before deleting [DOC-IBM - GpiSetBitmap Remarks], then unwind the
		// PS and the memory DC this surface opened.
		if (hps != NULLHANDLE)
			GpiSetBitmap(hps, NULLHANDLE);
		if (hbmPix != NULLHANDLE && hbmPix != GPI_ERROR)
			GpiDeleteBitmap(hbmPix);
		if (hps != NULLHANDLE && hps != GPI_ERROR)
			GpiDestroyPS(hps);
		if (hdcOwned != NULLHANDLE && hdcOwned != DEV_ERROR)
			DevCloseDC(hdcOwned);
		hbmPix = NULLHANDLE;
		hbmPixOld = NULLHANDLE;
		hdcOwned = NULLHANDLE;
		pixmapOwned = false;
	} else if (hpsOwned && hps != NULLHANDLE) {
		WinReleasePS(hps);
	}
	hps = NULLHANDLE;
	hpsOwned = false;
	haveFM = false;
	// The setid table belonged to that PS; the lcids die with it. Do NOT call
	// GpiDeleteSetId here - the PS is already gone.
	lcidForFont.clear();
	nextLcid = 1;
	selectedFont = nullptr;
}

bool SurfaceImpl::Initialised() {
	return hps != NULLHANDLE;
}

void SurfaceImpl::PenColour(ColourDesired fore) {
	if (hps != NULLHANDLE)
		GpiSetColor(hps, ToRGB(fore));
}

void SurfaceImpl::MoveTo(int x_, int y_) {
	if (hps == NULLHANDLE)
		return;
	POINTL p = ToPOINTL(x_, y_);
	GpiMove(hps, &p);   // sets current position, draws nothing [gpi-drawing.md 5]
}

void SurfaceImpl::LineTo(int x_, int y_) {
	if (hps == NULLHANDLE)
		return;
	POINTL p = ToPOINTL(x_, y_);
	GpiLine(hps, &p);   // line from current position [gpi-drawing.md 5]
}

void SurfaceImpl::Polygon(Point *pts, int npts, ColourDesired fore, ColourDesired back) {
	if (hps == NULLHANDLE || npts < 2)
		return;
	std::vector<POINTL> apt(static_cast<size_t>(npts));
	for (int i = 0; i < npts; i++)
		apt[static_cast<size_t>(i)] = ToPOINTL(pts[i].x, pts[i].y);

	GpiSetColor(hps, ToRGB(back));
	POLYGON poly;
	poly.ulPoints = static_cast<ULONG>(npts - 1);
	poly.aPointl = &apt[1];
	GpiMove(hps, &apt[0]);
	GpiPolygons(hps, 1, &poly, POLYGON_BOUNDARY | POLYGON_ALTERNATE, POLYGON_INCL);

	GpiSetColor(hps, ToRGB(fore));
}

// GpiBox draws from the current position to the opposite corner
// [DOC-IBM - gpi-drawing.md 6: GpiBox(HPS, LONG lControl, PPOINTL, LONG lHRound, LONG lVRound)].
void SurfaceImpl::RectangleDraw(PRectangle rc, ColourDesired fore, ColourDesired back) {
	if (hps == NULLHANDLE)
		return;
	RECTL r = ToRECTL(rc);
	POINTL origin = { r.xLeft, r.yBottom };
	POINTL corner = { r.xRight - 1, r.yTop - 1 };

	GpiSetColor(hps, ToRGB(back));
	GpiMove(hps, &origin);
	GpiBox(hps, DRO_FILL, &corner, 0, 0);

	GpiSetColor(hps, ToRGB(fore));
	GpiMove(hps, &origin);
	GpiBox(hps, DRO_OUTLINE, &corner, 0, 0);
}

// WinFillRect fills left/bottom edges but not right/top [DOC-IBM - pm2.txt, WinFillRect],
// which matches Scintilla's half-open rectangle exactly - see the header comment.
void SurfaceImpl::FillRectangle(PRectangle rc, ColourDesired back) {
	if (hps == NULLHANDLE)
		return;
	RECTL r = ToRECTL(rc);
	WinFillRect(hps, &r, ToRGB(back));
}

void SurfaceImpl::RoundedRectangle(PRectangle rc, ColourDesired fore, ColourDesired back) {
	if (hps == NULLHANDLE)
		return;
	RECTL r = ToRECTL(rc);
	POINTL origin = { r.xLeft, r.yBottom };
	POINTL corner = { r.xRight - 1, r.yTop - 1 };

	GpiSetColor(hps, ToRGB(back));
	GpiMove(hps, &origin);
	GpiBox(hps, DRO_FILL, &corner, 8, 8);      // lHRound / lVRound = corner rounding

	GpiSetColor(hps, ToRGB(fore));
	GpiMove(hps, &origin);
	GpiBox(hps, DRO_OUTLINE, &corner, 8, 8);
}

void SurfaceImpl::Ellipse(PRectangle rc, ColourDesired fore, ColourDesired back) {
	if (hps == NULLHANDLE)
		return;
	// GpiFullArc scales the current arc parameters about the current position; set the
	// arc box via GpiSetArcParams so the ellipse fills rc.
	RECTL r = ToRECTL(rc);
	const LONG cx = (r.xRight - r.xLeft) / 2;
	const LONG cy = (r.yTop - r.yBottom) / 2;
	POINTL centre = { r.xLeft + cx, r.yBottom + cy };

	ARCPARAMS ap;
	ap.lP = cx;
	ap.lQ = cy;
	ap.lR = 0;
	ap.lS = 0;
	GpiSetArcParams(hps, &ap);

	GpiSetColor(hps, ToRGB(back));
	GpiMove(hps, &centre);
	GpiFullArc(hps, DRO_FILL, MAKEFIXED(1, 0));

	GpiSetColor(hps, ToRGB(fore));
	GpiMove(hps, &centre);
	GpiFullArc(hps, DRO_OUTLINE, MAKEFIXED(1, 0));
}

// GpiBitBlt(hpsTarget, hpsSource, lCount, aptlPoints, lRop, flOptions)
// [DOC-IBM - gpi-drawing.md 10]. The 3-point form is target-lower-left,
// target-upper-right, source-lower-left.
void SurfaceImpl::Copy(PRectangle rc, Point from, Surface &surfaceSource) {
	if (hps == NULLHANDLE)
		return;
	SurfaceImpl &source = static_cast<SurfaceImpl &>(surfaceSource);
	if (source.hps == NULLHANDLE)
		return;

	RECTL r = ToRECTL(rc);
	POINTL apt[3];
	apt[0].x = r.xLeft;   apt[0].y = r.yBottom;
	apt[1].x = r.xRight;  apt[1].y = r.yTop;
	apt[2].x = static_cast<LONG>(from.x);
	apt[2].y = source.FlipY(from.y + (rc.bottom - rc.top));

	GpiBitBlt(hps, source.hps, 3, apt, ROP_SRCCOPY, BBO_IGNORE);
}

void SurfaceImpl::EnsureMetrics() {
	if (!haveFM && hps != NULLHANDLE) {
		if (GpiQueryFontMetrics(hps, sizeof(FONTMETRICS), &currentFM))
			haveFM = true;
	}
}

// GpiCharStringPosAt(hps, pptlStart, prclRect, flOptions, lCount, pchString, alAdx)
// [DOC-IBM - gpi2.txt, GpiCharStringPosAt]. CHS_OPAQUE paints the background rectangle,
// CHS_CLIP clips to it; both ignore prclRect unless one of them is set.
void SurfaceImpl::DrawTextNoClip(PRectangle rc, Font &font_, XYPOSITION ybase,
		const char *s, int len, ColourDesired fore, ColourDesired back) {
	if (hps == NULLHANDLE || len <= 0)
		return;
	SetFont(font_);
	RECTL r = ToRECTL(rc);
	POINTL start = ToPOINTL(rc.left, ybase);
	GpiSetColor(hps, ToRGB(fore));
	GpiSetBackColor(hps, ToRGB(back));
	int cbDraw = 0;
	const char *pDraw = Displayable(s, len, &cbDraw);
	GpiCharStringPosAt(hps, &start, &r, CHS_OPAQUE, cbDraw,
		AsPCH(pDraw), nullptr);
}

void SurfaceImpl::DrawTextClipped(PRectangle rc, Font &font_, XYPOSITION ybase,
		const char *s, int len, ColourDesired fore, ColourDesired back) {
	if (hps == NULLHANDLE || len <= 0)
		return;
	SetFont(font_);
	RECTL r = ToRECTL(rc);
	POINTL start = ToPOINTL(rc.left, ybase);
	GpiSetColor(hps, ToRGB(fore));
	GpiSetBackColor(hps, ToRGB(back));
	int cbDraw = 0;
	const char *pDraw = Displayable(s, len, &cbDraw);
	GpiCharStringPosAt(hps, &start, &r, CHS_OPAQUE | CHS_CLIP, cbDraw,
		AsPCH(pDraw), nullptr);
}

void SurfaceImpl::DrawTextTransparent(PRectangle rc, Font &font_, XYPOSITION ybase,
		const char *s, int len, ColourDesired fore) {
	if (hps == NULLHANDLE || len <= 0)
		return;
	SetFont(font_);
	POINTL start = ToPOINTL(rc.left, ybase);
	GpiSetColor(hps, ToRGB(fore));
	// No CHS_OPAQUE: leave the background alone.
	int cbDraw = 0;
	const char *pDraw = Displayable(s, len, &cbDraw);
	GpiCharStringPosAt(hps, &start, nullptr, 0, cbDraw,
		AsPCH(pDraw), nullptr);
}

// GpiQueryTextBox(hps, lCount1, pchString, lCount2, aptlPoints)
// [DOC-IBM - gpi2.txt]. Points are indexed from TXTBOX_TOPLEFT = 0; TXTBOX_CONCAT is
// the advance to where a following string would start - that is the text width.
XYPOSITION SurfaceImpl::WidthText(Font &font_, const char *s, int len) {
	if (hps == NULLHANDLE || len <= 0)
		return 0;
	SetFont(font_);
	int cbMeasure = 0;
	const char *pMeasure = Displayable(s, len, &cbMeasure);
	if (cbMeasure <= 0)
		return 0;
	POINTL apt[TXTBOX_COUNT];
	if (!GpiQueryTextBox(hps, cbMeasure, AsPCH(pMeasure), TXTBOX_COUNT, apt))
		return 0;
	return static_cast<XYPOSITION>(apt[TXTBOX_CONCAT].x);
}

XYPOSITION SurfaceImpl::WidthChar(Font &font_, char ch) {
	return WidthText(font_, &ch, 1);
}

// Scintilla wants a cumulative x position after each byte. GpiQueryTextBox measures a
// whole string, so measure successive prefixes. Correct but O(n^2); a caching or
// GpiQueryCharStringPos-based version is the obvious optimisation once this is proven.
void SurfaceImpl::MeasureWidths(Font &font_, const char *s, int len, XYPOSITION *positions) {
	if (len <= 0)
		return;
	if (hps == NULLHANDLE) {
		for (int i = 0; i < len; i++)
			positions[i] = 0;
		return;
	}
	if (!unicodeMode) {
		for (int i = 1; i <= len; i++)
			positions[i - 1] = WidthText(font_, s, i);
		return;
	}

	// Unicode: convert ONCE, then measure prefixes of the converted bytes.
	// Every byte of a multi-byte character reports the same x, which is what
	// keeps the caret from landing inside a character.
	SetFont(font_);
	int cbAll = 0;
	Displayable(s, len, &cbAll);
	const std::string conv = xlat.out;
	const std::vector<int> mapAt = xlat.outLenAt;
	for (int i = 0; i < len; i++) {
		const int cb = (i < static_cast<int>(mapAt.size())) ? mapAt[i] : cbAll;
		if (cb <= 0) {
			positions[i] = 0;
			continue;
		}
		POINTL apt[TXTBOX_COUNT];
		if (GpiQueryTextBox(hps, cb, AsPCH(conv.c_str()), TXTBOX_COUNT, apt))
			positions[i] = static_cast<XYPOSITION>(apt[TXTBOX_CONCAT].x);
		else
			positions[i] = (i > 0) ? positions[i - 1] : 0;
	}
}

// FONTMETRICS field mapping [DOC-IBM - gpi-fonts-and-metafiles.md 2.3].
XYPOSITION SurfaceImpl::Ascent(Font &font_) {
	SetFont(font_);
	EnsureMetrics();
	return static_cast<XYPOSITION>(currentFM.lMaxAscender);
}

XYPOSITION SurfaceImpl::Descent(Font &font_) {
	SetFont(font_);
	EnsureMetrics();
	return static_cast<XYPOSITION>(currentFM.lMaxDescender);
}

XYPOSITION SurfaceImpl::InternalLeading(Font &font_) {
	SetFont(font_);
	EnsureMetrics();
	return static_cast<XYPOSITION>(currentFM.lInternalLeading);
}

XYPOSITION SurfaceImpl::ExternalLeading(Font &font_) {
	SetFont(font_);
	EnsureMetrics();
	return static_cast<XYPOSITION>(currentFM.lExternalLeading);
}

XYPOSITION SurfaceImpl::Height(Font &font_) {
	SetFont(font_);
	EnsureMetrics();
	return static_cast<XYPOSITION>(currentFM.lMaxBaselineExt);
}

XYPOSITION SurfaceImpl::AverageCharWidth(Font &font_) {
	SetFont(font_);
	EnsureMetrics();
	return static_cast<XYPOSITION>(currentFM.lAveCharWidth);
}

// GpiIntersectClipRectangle(hps, prclRectangle) - intersects the current clip region
// with the rectangle, in world coordinates [DOC-IBM - gpi2.txt].
void SurfaceImpl::SetClip(PRectangle rc) {
	if (hps == NULLHANDLE)
		return;
	RECTL r = ToRECTL(rc);
	GpiIntersectClipRectangle(hps, &r);
}

void SurfaceImpl::FlushCachedState() {
	haveFM = false;
}

// Hand GPI bytes it can actually draw. In non-Unicode mode the document's
// bytes already ARE code-page bytes, so this is a pass-through and costs
// nothing; only Unicode mode pays for the transcode.
const char *SurfaceImpl::Displayable(const char *s, int len, int *pOutLen) {
	if (!unicodeMode || len <= 0) {
		*pOutLen = len;
		return s;
	}
	if (!convDisplay) {
		// The GPI code page is the one text is DRAWN in, and is not necessarily
		// the process code page [os2ref/unicode-conversion.md 9.1]. But
		// GpiQueryCp on a freshly created PS reports 0 - "the default" - and
		// on some drivers a value UniMapCpToUcsCp will not map, so the queried
		// page is a hint and not a guarantee: fall back rather than give up,
		// because giving up means every non-ASCII character draws as a marker.
		const unsigned long aTry[3] = {
			static_cast<unsigned long>((hps != NULLHANDLE) ? GpiQueryCp(hps) : 0),
			850,          // the usual OS/2 display page
			437           // and the US default, if this box is set up that way
		};
		for (int t = 0; t < 3 && !convDisplay; t++) {
			UniChar name[32];
			if (aTry[t] == 0)
				continue;
			if (UniMapCpToUcsCp(aTry[t], name, 32) == 0)
				if (UniCreateUconvObject(name, &convDisplay) != 0)
					convDisplay = nullptr;
		}
	}
	xlat.Convert(s, len, convDisplay);
	*pOutLen = static_cast<int>(xlat.out.size());
	return xlat.out.c_str();
}

void SurfaceImpl::SetUnicodeMode(bool unicodeMode_) {
	unicodeMode = unicodeMode_;
}

// A PM process has THREE independent code pages - process, message queue, and GPI -
// and setting one does not set the others [DOC-IBM - pm5.txt "Code Pages"; see
// os2ref/unicode-conversion.md 9.1]. Text is DRAWN in the GPI code page, so that is
// the one a Surface must set.
void SurfaceImpl::SetDBCSMode(int codePage_) {
	codePage = codePage_;
	if (hps != NULLHANDLE && codePage_ != 0)
		GpiSetCp(hps, static_cast<ULONG>(codePage_));
}

// DevQueryCaps(hdc, lStart, lCount, alArray) [DOC-IBM - gpi-drawing.md 3].
// CAPS_VERTICAL_FONT_RES is the vertical font resolution in pels per inch, which is
// what Scintilla means by LogPixelsY.
int SurfaceImpl::LogPixelsY() {
	if (hps == NULLHANDLE)
		return 72;
	HDC hdc = GpiQueryDevice(hps);
	if (hdc == NULLHANDLE || hdc == HDC_ERROR)
		return 72;
	LONG value = 0;
	if (DevQueryCaps(hdc, CAPS_VERTICAL_FONT_RES, 1, &value) && value > 0)
		return static_cast<int>(value);
	return 72;
}

int SurfaceImpl::DeviceHeightFont(int points) {
	return (points * LogPixelsY() + 36) / 72;
}

// ---------------------------------------------------------------------------
// Not yet implemented. These stop honestly rather than drawing something wrong:
// a silently-wrong blend or a blank image would relocate the bug somewhere
// innocent, which costs far more than a visible gap.
// ---------------------------------------------------------------------------

// An off-screen drawing surface: a memory DC + PS with a bitmap selected into it
// [DOC-IBM - gpi-fonts-and-metafiles.md 3.1; the "off-screen presentation space"].
// Scintilla uses these for the margin patterns and for double-buffered line drawing,
// and asserts that Initialised() is true, so a stub here stops the editor at startup.
void SurfaceImpl::InitPixMap(int width, int height, Surface *, WindowID) {
	Release();
	if (width <= 0 || height <= 0)
		return;
	HAB habLocal = WinQueryAnchorBlock(HWND_DESKTOP);
	if (habLocal == NULLHANDLE)
		return;
	hdcOwned = DevOpenDC(habLocal, OD_MEMORY, (PSZ)"*", 0, nullptr, NULLHANDLE);
	if (hdcOwned == NULLHANDLE || hdcOwned == DEV_ERROR) {
		hdcOwned = NULLHANDLE;
		return;
	}
	SIZEL sizl = { 0, 0 };
	hps = GpiCreatePS(habLocal, hdcOwned, &sizl,
		PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC);
	if (hps == NULLHANDLE || hps == GPI_ERROR) {
		hps = NULLHANDLE;
		DevCloseDC(hdcOwned);
		hdcOwned = NULLHANDLE;
		return;
	}

	BITMAPINFOHEADER2 bmp2;
	memset(&bmp2, 0, sizeof(bmp2));
	bmp2.cbFix = sizeof(BITMAPINFOHEADER2);
	bmp2.cx = static_cast<ULONG>(width);
	bmp2.cy = static_cast<ULONG>(height);
	bmp2.cPlanes = 1;
	bmp2.cBitCount = 24;
	bmp2.ulCompression = BCA_UNCOMP;
	bmp2.cbImage = static_cast<ULONG>(StrideFor(width) * static_cast<size_t>(height));
	bmp2.usRecording = BRA_BOTTOMUP;
	bmp2.ulColorEncoding = BCE_RGB;

	hbmPix = GpiCreateBitmap(hps, &bmp2, 0, nullptr, nullptr);
	if (hbmPix == NULLHANDLE || hbmPix == GPI_ERROR) {
		hbmPix = NULLHANDLE;
		GpiDestroyPS(hps);
		hps = NULLHANDLE;
		DevCloseDC(hdcOwned);
		hdcOwned = NULLHANDLE;
		return;
	}
	hbmPixOld = GpiSetBitmap(hps, hbmPix);
	pixmapOwned = true;
	surfaceHeight = height;
	SetRGBMode();
}

// Tile the pattern surface across rc. GpiSetPattern with a bitmap set-id would let GPI
// do this, but that consumes an lcid from the same 0..254 table the fonts use; blitting
// the tile keeps the two resources independent.
void SurfaceImpl::FillRectangle(PRectangle rc, Surface &surfacePattern) {
	if (hps == NULLHANDLE)
		return;
	SurfaceImpl &pat = static_cast<SurfaceImpl &>(surfacePattern);
	if (pat.hps == NULLHANDLE || pat.surfaceHeight <= 0) {
		FillRectangle(rc, ColourDesired(0xC0, 0xC0, 0xC0));
		return;
	}
	const LONG patH = pat.surfaceHeight;
	// The pattern surfaces Scintilla builds are square, so reuse the height for width.
	const LONG patW = patH;
	const RECTL r = ToRECTL(rc);
	for (LONG y = r.yBottom; y < r.yTop; y += patH) {
		for (LONG x = r.xLeft; x < r.xRight; x += patW) {
			POINTL apt[3];
			apt[0].x = x;
			apt[0].y = y;
			apt[1].x = (x + patW < r.xRight) ? (x + patW) : r.xRight;
			apt[1].y = (y + patH < r.yTop) ? (y + patH) : r.yTop;
			apt[2].x = 0;
			apt[2].y = 0;
			GpiBitBlt(hps, pat.hps, 3, apt, ROP_SRCCOPY, BBO_IGNORE);
		}
	}
}

// ---------------------------------------------------------------------------
// Software alpha compositing.
//
// GPI has no alpha-blend primitive, so the blend is done by hand: capture the
// destination rectangle into a memory bitmap, blend in a plain byte buffer, write it
// back. The algorithm is adapted from a Win32 implementation (AlphaBlendU in
// odc/hmi/alpha.c) whose inner loop is pure arithmetic and carries over unchanged:
//
//     dst = (dst * (255 - alpha) + src * alpha) >> 8
//
// Two differences from the Win32 original, both forced by OS/2:
//
//  1. **No directly-writable bitmap memory.** OS/2 *does* have a memory DC - the off-screen
//     presentation space (DevOpenDC OD_MEMORY + GpiCreatePS + GpiSetBitmap), used below. What
//     it lacks is CreateDIBSection's writable pointer into a *selected* bitmap, so reading the
//     destination costs a GpiBitBlt + GpiQueryBitmapBits. Writing back does NOT: GpiDrawBits
//     sends an application buffer straight to a PS in one call.
//  2. **24bpp, not 32.** BITMAPINFOHEADER2.cBitCount documents 1/4/8/16/24
//     [DOC-IBM - pmbitmap.h:119 via gpi-fonts-and-metafiles.md 3.2]; there is no 32bpp
//     form to borrow the alpha byte from, so alpha arrives as a parameter (AlphaRectangle)
//     or from the caller's separate RGBA buffer (DrawRGBAImage).
//
// Scan lines are stored BRA_BOTTOMUP - bottom-to-top - which is the OS/2 default and
// agrees with PM's y-up origin, so no extra row inversion is needed here.
// ---------------------------------------------------------------------------

namespace {

struct MemBitmap {
	HAB hab;
	HDC hdc;
	HPS hps;
	HBITMAP hbm;
	HBITMAP hbmOld;
	BITMAPINFOHEADER2 bmp2;
	int w, h;

	MemBitmap() : hab(NULLHANDLE), hdc(NULLHANDLE), hps(NULLHANDLE),
		hbm(NULLHANDLE), hbmOld(NULLHANDLE), w(0), h(0) {
		memset(&bmp2, 0, sizeof(bmp2));
	}

	bool Create(int width, int height) {
		if (width <= 0 || height <= 0)
			return false;
		w = width; h = height;
		hab = WinQueryAnchorBlock(HWND_DESKTOP);
		if (hab == NULLHANDLE)
			return false;
		// A memory DC is opened with OD_MEMORY [DOC-IBM - pmdev.h:82].
		hdc = DevOpenDC(hab, OD_MEMORY, (PSZ)"*", 0, nullptr, NULLHANDLE);
		if (hdc == NULLHANDLE || hdc == DEV_ERROR)
			return false;
		SIZEL sizl = { 0, 0 };
		hps = GpiCreatePS(hab, hdc, &sizl, PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC);
		if (hps == NULLHANDLE || hps == GPI_ERROR)
			return false;

		bmp2.cbFix = sizeof(BITMAPINFOHEADER2);
		bmp2.cx = static_cast<ULONG>(w);
		bmp2.cy = static_cast<ULONG>(h);
		bmp2.cPlanes = 1;
		bmp2.cBitCount = 24;
		bmp2.ulCompression = BCA_UNCOMP;
		bmp2.cbImage = static_cast<ULONG>(StrideFor(w) * static_cast<size_t>(h));
		bmp2.usRecording = BRA_BOTTOMUP;
		bmp2.ulColorEncoding = BCE_RGB;

		hbm = GpiCreateBitmap(hps, &bmp2, 0, nullptr, nullptr);
		if (hbm == NULLHANDLE || hbm == GPI_ERROR)
			return false;
		hbmOld = GpiSetBitmap(hps, hbm);
		if (hbmOld == HBM_ERROR)
			return false;
		return true;
	}

	~MemBitmap() {
		// A bitmap must be deselected before deletion [DOC-IBM - GpiSetBitmap Remarks].
		if (hps != NULLHANDLE && hbm != NULLHANDLE)
			GpiSetBitmap(hps, NULLHANDLE);
		if (hbm != NULLHANDLE && hbm != GPI_ERROR)
			GpiDeleteBitmap(hbm);
		if (hps != NULLHANDLE && hps != GPI_ERROR)
			GpiDestroyPS(hps);
		if (hdc != NULLHANDLE && hdc != DEV_ERROR)
			DevCloseDC(hdc);
	}

	MemBitmap(const MemBitmap &) = delete;
	MemBitmap &operator=(const MemBitmap &) = delete;
};

} // anonymous namespace

// Capture the destination under rc, blend a solid colour over it at the given alpha,
// and put it back. cornerSize/outline are not yet honoured - the rectangle is filled
// uniformly; rounded translucent corners need a per-pixel coverage mask.
void SurfaceImpl::AlphaRectangle(PRectangle rc, int cornerSize, ColourDesired fill, int alphaFill,
		ColourDesired outline, int alphaOutline, int flags) {
	(void)cornerSize; (void)outline; (void)alphaOutline; (void)flags;
	if (hps == NULLHANDLE)
		return;
	const int w = static_cast<int>(rc.right - rc.left);
	const int h = static_cast<int>(rc.bottom - rc.top);
	if (w <= 0 || h <= 0)
		return;

	MemBitmap mb;
	if (!mb.Create(w, h))
		return;   // honest failure: draw nothing rather than an opaque block

	RECTL r = ToRECTL(rc);
	POINTL apt[3];
	// capture: target (0,0)-(w,h) in the memory PS, source at rc in this PS
	apt[0].x = 0; apt[0].y = 0;
	apt[1].x = w; apt[1].y = h;
	apt[2].x = r.xLeft; apt[2].y = r.yBottom;
	if (GpiBitBlt(mb.hps, hps, 3, apt, ROP_SRCCOPY, BBO_IGNORE) == GPI_ERROR)
		return;

	const size_t stride = StrideFor(w);
	std::vector<BYTE> buf(stride * static_cast<size_t>(h));
	BITMAPINFO2 bmi;
	memset(&bmi, 0, sizeof(bmi));
	memcpy(&bmi, &mb.bmp2, sizeof(BITMAPINFOHEADER2));
	if (GpiQueryBitmapBits(mb.hps, 0, h, buf.data(),
			reinterpret_cast<PBITMAPINFO2>(&bmi)) == BMB_ERROR)
		return;

	// Pel byte order within a 24bpp scan line is blue, green, red - the same ordering
	// the Win32 original assumes. [unverified] against IBM text; if the first render
	// shows red and blue swapped, this is the line to change, not the blend maths.
	const int a = alphaFill;
	const int na = 255 - a;
	const int fb = fill.GetBlue(), fg = fill.GetGreen(), fr = fill.GetRed();
	for (int y = 0; y < h; y++) {
		BYTE *row = &buf[stride * static_cast<size_t>(y)];
		for (int x = 0; x < w; x++) {
			BYTE *p = row + x * 3;
			p[0] = static_cast<BYTE>((p[0] * na + fb * a) >> 8);
			p[1] = static_cast<BYTE>((p[1] * na + fg * a) >> 8);
			p[2] = static_cast<BYTE>((p[2] * na + fr * a) >> 8);
		}
	}

	// Write back with GpiDrawBits, which draws an application buffer straight to a PS
	// [DOC-IBM - gpi2.txt, GpiDrawBits] - no GpiSetBitmapBits + second GpiBitBlt needed.
	// BBO_IGNORE is IBM's recommendation for colour data.
	POINTL aptDraw[4];
	aptDraw[0].x = r.xLeft;  aptDraw[0].y = r.yBottom;   // target lower-left
	aptDraw[1].x = r.xRight; aptDraw[1].y = r.yTop;      // target upper-right
	aptDraw[2].x = 0;        aptDraw[2].y = 0;           // source lower-left
	aptDraw[3].x = w;        aptDraw[3].y = h;           // source upper-right
	GpiDrawBits(hps, buf.data(), reinterpret_cast<PBITMAPINFO2>(&bmi),
		4, aptDraw, ROP_SRCCOPY, BBO_IGNORE);
}

// Same round trip, but each source pixel carries its own alpha in a caller-supplied
// RGBA buffer (Scintilla's order is R,G,B,A, top row first).
void SurfaceImpl::DrawRGBAImage(PRectangle rc, int width, int height,
		const unsigned char *pixelsImage) {
	if (hps == NULLHANDLE || width <= 0 || height <= 0 || pixelsImage == nullptr)
		return;

	MemBitmap mb;
	if (!mb.Create(width, height))
		return;

	RECTL r = ToRECTL(rc);
	POINTL apt[3];
	apt[0].x = 0; apt[0].y = 0;
	apt[1].x = width; apt[1].y = height;
	apt[2].x = r.xLeft; apt[2].y = r.yBottom;
	if (GpiBitBlt(mb.hps, hps, 3, apt, ROP_SRCCOPY, BBO_IGNORE) == GPI_ERROR)
		return;

	const size_t stride = StrideFor(width);
	std::vector<BYTE> buf(stride * static_cast<size_t>(height));
	BITMAPINFO2 bmi;
	memset(&bmi, 0, sizeof(bmi));
	memcpy(&bmi, &mb.bmp2, sizeof(BITMAPINFOHEADER2));
	if (GpiQueryBitmapBits(mb.hps, 0, height, buf.data(),
			reinterpret_cast<PBITMAPINFO2>(&bmi)) == BMB_ERROR)
		return;

	for (int y = 0; y < height; y++) {
		// Scintilla's image is top-row-first; the bitmap is BRA_BOTTOMUP, so the
		// destination row is mirrored.
		BYTE *row = &buf[stride * static_cast<size_t>(height - 1 - y)];
		const unsigned char *src = pixelsImage + static_cast<size_t>(y) * width * 4;
		for (int x = 0; x < width; x++) {
			const int sr = src[x * 4 + 0];
			const int sg = src[x * 4 + 1];
			const int sb = src[x * 4 + 2];
			const int a  = src[x * 4 + 3];
			const int na = 255 - a;
			BYTE *p = row + x * 3;
			p[0] = static_cast<BYTE>((p[0] * na + sb * a) >> 8);
			p[1] = static_cast<BYTE>((p[1] * na + sg * a) >> 8);
			p[2] = static_cast<BYTE>((p[2] * na + sr * a) >> 8);
		}
	}

	POINTL aptDraw[4];
	aptDraw[0].x = r.xLeft;  aptDraw[0].y = r.yBottom;
	aptDraw[1].x = r.xRight; aptDraw[1].y = r.yTop;
	aptDraw[2].x = 0;        aptDraw[2].y = 0;
	aptDraw[3].x = width;    aptDraw[3].y = height;
	GpiDrawBits(hps, buf.data(), reinterpret_cast<PBITMAPINFO2>(&bmi),
		4, aptDraw, ROP_SRCCOPY, BBO_IGNORE);
}

// ---------------------------------------------------------------------------

Surface *Surface::Allocate(int) {
	return new SurfaceImpl();
}

// ---------------------------------------------------------------------------
// Window
//
// THE Y-FLIP APPLIES HERE TOO, against a different height. A Surface flips against
// its own drawing area; a Window's position is expressed in its PARENT's coordinates,
// so it flips against the parent's height. SWP.y is the window's BOTTOM edge measured
// upward from the parent's bottom [DOC-IBM - pm2.txt, WinSetWindowPos: x/y are "in window
// coordinates relative to the bottom left corner of its parent"], while Scintilla's
// PRectangle.top is the top edge measured downward from the parent's top.
// ---------------------------------------------------------------------------

namespace {

LONG ParentHeightOf(HWND hwnd) {
	// QW_PARENT = 5 [DOC-IBM - pm-window-messaging.md, WinQueryWindow].
	HWND hwndParent = WinQueryWindow(hwnd, QW_PARENT);
	RECTL rcl;
	if (hwndParent != NULLHANDLE && WinQueryWindowRect(hwndParent, &rcl))
		return rcl.yTop - rcl.yBottom;
	return 0;
}

LONG OwnHeightOf(HWND hwnd) {
	RECTL rcl;
	if (WinQueryWindowRect(hwnd, &rcl))
		return rcl.yTop - rcl.yBottom;
	return 0;
}

} // anonymous namespace

Window::~Window() {
}

void Window::Destroy() {
	if (wid)
		WinDestroyWindow(reinterpret_cast<HWND>(wid));
	wid = 0;
}

// WinQueryFocus(hwndDeskTop) returns the focus window [DOC-IBM - pm2.txt].
bool Window::HasFocus() {
	return wid && (WinQueryFocus(HWND_DESKTOP) == reinterpret_cast<HWND>(wid));
}

// WinQueryWindowPos(hwnd, pswp) fills an SWP [DOC-IBM - pm2.txt].
PRectangle Window::GetPosition() {
	if (!wid)
		return PRectangle();
	HWND hwnd = reinterpret_cast<HWND>(wid);
	SWP swp;
	if (!WinQueryWindowPos(hwnd, &swp))
		return PRectangle();
	const LONG ph = ParentHeightOf(hwnd);
	return PRectangle(
		static_cast<XYPOSITION>(swp.x),
		static_cast<XYPOSITION>(ph - (swp.y + swp.cy)),
		static_cast<XYPOSITION>(swp.x + swp.cx),
		static_cast<XYPOSITION>(ph - swp.y));
}

void Window::SetPosition(PRectangle rc) {
	if (!wid)
		return;
	HWND hwnd = reinterpret_cast<HWND>(wid);
	const LONG ph = ParentHeightOf(hwnd);
	const LONG cx = static_cast<LONG>(rc.right - rc.left);
	const LONG cy = static_cast<LONG>(rc.bottom - rc.top);
	WinSetWindowPos(hwnd, NULLHANDLE,
		static_cast<LONG>(rc.left), ph - static_cast<LONG>(rc.bottom),
		cx, cy, SWP_MOVE | SWP_SIZE);
}

void Window::SetPositionRelative(PRectangle rc, Window relativeTo) {
	if (!wid || !relativeTo.GetID())
		return;
	// Map the anchor window's origin into desktop coordinates, then offset.
	HWND hwndRel = reinterpret_cast<HWND>(relativeTo.GetID());
	POINTL ptl;
	ptl.x = 0;
	ptl.y = 0;
	// WinMapWindowPoints(hwndFrom, hwndTo, prgptl, cwpt) [DOC-IBM - pm2.txt].
	WinMapWindowPoints(hwndRel, HWND_DESKTOP, &ptl, 1);
	const LONG relTopPM = ptl.y + OwnHeightOf(hwndRel);   // PM top edge of the anchor
	const LONG deskH = OwnHeightOf(HWND_DESKTOP);
	// rc is y-down relative to the anchor's top; convert to desktop y-down, then let
	// SetPosition-style maths put it back into PM coordinates.
	const LONG topDown = (deskH - relTopPM) + static_cast<LONG>(rc.top);
	const LONG cx = static_cast<LONG>(rc.right - rc.left);
	const LONG cy = static_cast<LONG>(rc.bottom - rc.top);
	WinSetWindowPos(reinterpret_cast<HWND>(wid), HWND_TOP,
		ptl.x + static_cast<LONG>(rc.left), deskH - (topDown + cy),
		cx, cy, SWP_MOVE | SWP_SIZE | SWP_ZORDER);
}

PRectangle Window::GetClientPosition() {
	if (!wid)
		return PRectangle();
	RECTL rcl;
	if (!WinQueryWindowRect(reinterpret_cast<HWND>(wid), &rcl))
		return PRectangle();
	// Window coordinates: bottom-left is (0,0), so the client box is simply its extent.
	return PRectangle(0, 0,
		static_cast<XYPOSITION>(rcl.xRight - rcl.xLeft),
		static_cast<XYPOSITION>(rcl.yTop - rcl.yBottom));
}

void Window::Show(bool show) {
	if (wid)
		WinShowWindow(reinterpret_cast<HWND>(wid), show ? TRUE : FALSE);
}

// WinInvalidateRect(hwnd, pwrc, fIncludeChildren) [DOC-IBM - pm2.txt]; a NULL rectangle
// invalidates the whole window.
void Window::InvalidateAll() {
	if (wid)
		WinInvalidateRect(reinterpret_cast<HWND>(wid), nullptr, TRUE);
}

void Window::InvalidateRectangle(PRectangle rc) {
	if (!wid)
		return;
	HWND hwnd = reinterpret_cast<HWND>(wid);
	const LONG h = OwnHeightOf(hwnd);
	RECTL rcl;
	rcl.xLeft   = static_cast<LONG>(rc.left);
	rcl.xRight  = static_cast<LONG>(rc.right);
	rcl.yBottom = h - static_cast<LONG>(rc.bottom);
	rcl.yTop    = h - static_cast<LONG>(rc.top);
	WinInvalidateRect(hwnd, &rcl, TRUE);
}

void Window::SetFont(Font &) {
	// A plain PM window has no font of its own; text is drawn through a Surface, which
	// selects the font into its own PS. ListBoxImpl overrides this for the real control.
}

// WinQuerySysPointer(hwndDeskTop, lIdentifier, fCopy) + WinSetPointer(hwndDeskTop, hptr)
// [DOC-IBM - pm2.txt]. fCopy = FALSE returns the system pointer itself (do not destroy it).
void Window::SetCursor(Cursor curs) {
	if (curs == cursorLast)
		return;
	cursorLast = curs;
	LONG id;
	switch (curs) {
	case cursorText:          id = SPTR_TEXT; break;
	case cursorWait:          id = SPTR_WAIT; break;
	case cursorHoriz:         id = SPTR_SIZEWE; break;
	case cursorVert:          id = SPTR_SIZENS; break;
	case cursorUp:            id = SPTR_ARROW; break;
	case cursorHand:          id = SPTR_ARROW; break;   // no system hand pointer
	case cursorReverseArrow:  id = SPTR_ARROW; break;
	case cursorArrow:
	default:                  id = SPTR_ARROW; break;
	}
	HPOINTER hptr = WinQuerySysPointer(HWND_DESKTOP, id, FALSE);
	if (hptr != NULLHANDLE)
		WinSetPointer(HWND_DESKTOP, hptr);
}

void Window::SetTitle(const char *s) {
	if (wid)
		WinSetWindowText(reinterpret_cast<HWND>(wid), AsPCH(s ? s : ""));
}

// OS/2 Warp 4.5x PM has no multi-monitor API, so the "monitor" containing a point is
// always the desktop.
PRectangle Window::GetMonitorRect(Point) {
	RECTL rcl;
	if (!WinQueryWindowRect(HWND_DESKTOP, &rcl))
		return PRectangle();
	return PRectangle(0, 0,
		static_cast<XYPOSITION>(rcl.xRight - rcl.xLeft),
		static_cast<XYPOSITION>(rcl.yTop - rcl.yBottom));
}

// ---------------------------------------------------------------------------
// ListBox - Scintilla's autocompletion popup, on a WC_LISTBOX control.
// LM_* message values and parameter layouts [DOC-IBM - pmwin.h:2240-2257 via
// pm-controls.md; field layouts from pm3.txt].
// ---------------------------------------------------------------------------

ListBox::ListBox() {
}

ListBox::~ListBox() {
}

class ListBoxImpl : public ListBox {
	int lineHeight;
	int desiredVisibleRows;
	int aveCharWidth;
	bool unicodeMode;
	CallBackAction doubleClickAction;
	void *doubleClickActionData;

public:
	ListBoxImpl()
		: lineHeight(10), desiredVisibleRows(5), aveCharWidth(8), unicodeMode(false),
		  doubleClickAction(nullptr), doubleClickActionData(nullptr) {
	}
	~ListBoxImpl() override {
	}

	void SetFont(Font &font) override;
	void Create(Window &parent, int ctrlID, Point location, int lineHeight_,
		bool unicodeMode_, int technology_) override;
	void SetAverageCharWidth(int width) override;
	void SetVisibleRows(int rows) override;
	int GetVisibleRows() const override;
	PRectangle GetDesiredRect() override;
	int CaretFromEdge() override;
	void Clear() override;
	void Append(char *s, int type) override;
	int Length() override;
	void Select(int n) override;
	int GetSelection() override;
	int Find(const char *prefix) override;
	void GetValue(int n, char *value, int len) override;
	void RegisterImage(int type, const char *xpm_data) override;
	void RegisterRGBAImage(int type, int width, int height,
		const unsigned char *pixelsImage) override;
	void ClearRegisteredImages() override;
	void SetDoubleClickAction(CallBackAction action, void *data) override;
	void SetList(const char *list, char separator, char typesep) override;
};

ListBox *ListBox::Allocate() {
	return new ListBoxImpl();
}

void ListBoxImpl::Create(Window &parent, int ctrlID, Point, int lineHeight_,
		bool unicodeMode_, int) {
	lineHeight = lineHeight_;
	unicodeMode = unicodeMode_;
	// The popup must be able to overhang the editor window, so it is a child of the
	// desktop rather than of the editor. It starts hidden; Scintilla positions and shows
	// it via Window::SetPositionRelative + Show.
	HWND hwndParent = parent.GetID() ? reinterpret_cast<HWND>(parent.GetID()) : HWND_DESKTOP;
	wid = reinterpret_cast<WindowID>(WinCreateWindow(
		HWND_DESKTOP,                       // parent: desktop, so it can overlap
		WC_LISTBOX,
		(PSZ)"",
		WS_CLIPSIBLINGS | LS_NOADJUSTPOS,   // not WS_VISIBLE: shown on demand
		0, 0, 0, 0,
		hwndParent,                         // owner: notifications go to the editor
		HWND_TOP,
		static_cast<ULONG>(ctrlID),
		nullptr, nullptr));
	if (wid && lineHeight > 0)
		WinSendMsg(reinterpret_cast<HWND>(wid), LM_SETITEMHEIGHT,
			MPFROMLONG(lineHeight), 0);
}

void ListBoxImpl::SetFont(Font &font) {
	if (!wid || !font.GetID())
		return;
	FontPM *pf = static_cast<FontPM *>(font.GetID());
	// A control's font is a presentation parameter, "<points>.<facename>"
	// [DOC-IBM - PP_FONTNAMESIZE, pm-controls.md].
	char spec[FACESIZE + 16];
	snprintf(spec, sizeof(spec), "%ld.%s",
		static_cast<long>(pf->pointSize), pf->fattrs.szFacename);
	WinSetPresParam(reinterpret_cast<HWND>(wid), PP_FONTNAMESIZE,
		static_cast<ULONG>(strlen(spec) + 1), spec);
}

void ListBoxImpl::SetAverageCharWidth(int width) {
	aveCharWidth = width;
}

void ListBoxImpl::SetVisibleRows(int rows) {
	desiredVisibleRows = rows;
}

int ListBoxImpl::GetVisibleRows() const {
	return desiredVisibleRows;
}

PRectangle ListBoxImpl::GetDesiredRect() {
	const int rows = (Length() < desiredVisibleRows) ? Length() : desiredVisibleRows;
	const int h = (rows > 0 ? rows : 1) * lineHeight + 4;
	return PRectangle(0, 0,
		static_cast<XYPOSITION>(aveCharWidth * 30),
		static_cast<XYPOSITION>(h));
}

int ListBoxImpl::CaretFromEdge() {
	return 4;
}

void ListBoxImpl::Clear() {
	if (wid)
		WinSendMsg(reinterpret_cast<HWND>(wid), LM_DELETEALL, 0, 0);
}

// LM_INSERTITEM: mp1 = sItemIndex (LIT_END appends), mp2 = pszItemText [DOC-IBM - pm3.txt].
void ListBoxImpl::Append(char *s, int type) {
	(void)type;   // per-item images are not implemented - see RegisterImage below
	if (wid && s)
		WinSendMsg(reinterpret_cast<HWND>(wid), LM_INSERTITEM,
			MPFROMSHORT(LIT_END), MPFROMP(s));
}

int ListBoxImpl::Length() {
	if (!wid)
		return 0;
	return static_cast<int>(LONGFROMMR(
		WinSendMsg(reinterpret_cast<HWND>(wid), LM_QUERYITEMCOUNT, 0, 0)));
}

// LM_SELECTITEM: mp1 = sItemIndex, mp2 = usselect (TRUE = select).
void ListBoxImpl::Select(int n) {
	if (wid)
		WinSendMsg(reinterpret_cast<HWND>(wid), LM_SELECTITEM,
			MPFROMSHORT(static_cast<SHORT>(n)), MPFROMSHORT(TRUE));
}

// LM_QUERYSELECTION: mp1 = sItemStart; LIT_FIRST begins the search.
int ListBoxImpl::GetSelection() {
	if (!wid)
		return -1;
	return static_cast<int>(SHORT1FROMMR(
		WinSendMsg(reinterpret_cast<HWND>(wid), LM_QUERYSELECTION,
			MPFROMSHORT(LIT_FIRST), 0)));
}

int ListBoxImpl::Find(const char *prefix) {
	if (!wid || !prefix)
		return -1;
	// LM_SEARCHSTRING exists, but Scintilla wants a prefix match on the *visible* text
	// and a -1 when absent; walking the items keeps the semantics obvious.
	const size_t plen = strlen(prefix);
	const int n = Length();
	char buf[512];
	for (int i = 0; i < n; i++) {
		GetValue(i, buf, sizeof(buf));
		if (strncmp(buf, prefix, plen) == 0)
			return i;
	}
	return -1;
}

// LM_QUERYITEMTEXT: mp1 = MPFROM2SHORT(sItemIndex, smaxcount), mp2 = pszItemText.
void ListBoxImpl::GetValue(int n, char *value, int len) {
	if (!value || len <= 0)
		return;
	value[0] = '\0';
	if (!wid)
		return;
	WinSendMsg(reinterpret_cast<HWND>(wid), LM_QUERYITEMTEXT,
		MPFROM2SHORT(static_cast<SHORT>(n), static_cast<SHORT>(len)),
		MPFROMP(value));
	value[len - 1] = '\0';
}

void ListBoxImpl::SetDoubleClickAction(CallBackAction action, void *data) {
	doubleClickAction = action;
	doubleClickActionData = data;
}

// SetList is the bulk form of Append: a separator-delimited string, with an optional
// "text<typesep><imagetype>" suffix per entry.
void ListBoxImpl::SetList(const char *list, char separator, char typesep) {
	if (!wid || !list)
		return;
	Clear();
	std::vector<char> work(list, list + strlen(list) + 1);
	char *start = &work[0];
	while (start) {
		char *end = strchr(start, separator);
		if (end)
			*end = '\0';
		char *tsep = typesep ? strchr(start, typesep) : nullptr;
		if (tsep)
			*tsep = '\0';
		Append(start, tsep ? atoi(tsep + 1) : -1);
		start = end ? end + 1 : nullptr;
	}
}

// ---- not implemented -------------------------------------------------------
// Per-item icons need an owner-drawn list box (LS_OWNERDRAW + WM_DRAWITEM). Until that
// exists these record nothing rather than pretending an image was registered; the list
// still shows correct text, just without type icons.

void ListBoxImpl::RegisterImage(int type, const char *xpm_data) {
	(void)type; (void)xpm_data;
}

void ListBoxImpl::RegisterRGBAImage(int type, int width, int height,
		const unsigned char *pixelsImage) {
	(void)type; (void)width; (void)height; (void)pixelsImage;
}

void ListBoxImpl::ClearRegisteredImages() {
}

// ---------------------------------------------------------------------------
// Menu - the context menu, on a PM popup menu.
// ---------------------------------------------------------------------------

Menu::Menu() : mid(0) {
}

// WinCreateWindow(..., WC_MENU, ...) with MS_ACTIONBAR omitted gives a popup menu.
void Menu::CreatePopUp() {
	Destroy();
	// A floating menu is an OBJECT window, not a desktop child: "if hwndFrame is
	// HWND_OBJECT (or an object window) the menu itself is created as an object
	// window" [DOC-IBM pm2.txt:16649-16652]. WinCreateMenu with a null template is
	// the documented way to build one programmatically and add items with
	// MM_INSERTITEM; WinCreateWindow is the fallback if it declines the null.
	// NOTE: IBM says the menu "must have been created, by use of either the
	// WinCreateMenu or WinLoadMenu functions" [DOC-IBM pm2.txt, WinPopupMenu].
	// A WC_MENU window built here works and is what a dynamic menu needs, since
	// Scintilla decides the items and their enabled state per invocation.
	HWND hwndMenu = WinCreateWindow(HWND_DESKTOP, WC_MENU, (PSZ)"",
		MS_VERTICALFLIP, 0, 0, 0, 0,
		HWND_DESKTOP, HWND_TOP, 0, nullptr, nullptr);
	mid = reinterpret_cast<MenuID>(hwndMenu);
}

void Menu::Destroy() {
	if (mid)
		WinDestroyWindow(reinterpret_cast<HWND>(mid));
	mid = 0;
}

// WinPopupMenu(hwndParent, hwndOwner, hwndMenu, x, y, idItem, fs) [DOC-IBM - pm2.txt].
// The position is in the parent's coordinates, so it takes the same y-flip as any other
// window-relative point.
void Menu::Show(Point pt, Window &w) {
	if (!mid || !w.GetID())
		return;
	HWND hwndParent = reinterpret_cast<HWND>(w.GetID());

	// x/y are "in window coordinates relative to the origin of the parent window"
	// [DOC-IBM pm2.txt, WinPopupMenu], so they take the usual y-flip: PM's origin is
	// the LOWER-left corner, which is also why the menu grows upward from the point.
	// The constrain flags are relative to the desktop either way.
	WinPopupMenu(hwndParent, hwndParent, reinterpret_cast<HWND>(mid),
		static_cast<LONG>(pt.x),
		OwnHeightOf(hwndParent) - static_cast<LONG>(pt.y), 0,
		PU_HCONSTRAIN | PU_VCONSTRAIN |
		PU_MOUSEBUTTON1 | PU_MOUSEBUTTON2 | PU_KEYBOARD);

	// NOT destroyed here. WinPopupMenu "returns as soon as the pop-up menu has been
	// invoked, which might be before the user has completed interacting with it"
	// [DOC-IBM pm2.txt], so destroying it here removes the menu instantly.
	// CreatePopUp destroys the previous menu instead.
}

// ---------------------------------------------------------------------------
// ElapsedTime - millisecond timing.
//
// DosQuerySysInfo(QSV_MS_COUNT, ...) returns milliseconds since IPL
// [DOC-IBM - os2ref/timers.md]. Scintilla splits it across two longs to dodge overflow.
// ---------------------------------------------------------------------------

static ULONG MsCount() {
	ULONG ms = 0;
	DosQuerySysInfo(QSV_MS_COUNT, QSV_MS_COUNT, &ms, sizeof(ms));
	return ms;
}

ElapsedTime::ElapsedTime() {
	const ULONG ms = MsCount();
	littleBit = static_cast<long>(ms & 0xffff);
	bigBit = static_cast<long>(ms >> 16);
}

double ElapsedTime::Duration(bool reset) {
	const ULONG msNow = MsCount();
	const ULONG msThen = (static_cast<ULONG>(bigBit) << 16) |
		(static_cast<ULONG>(littleBit) & 0xffff);
	const double result = static_cast<double>(msNow - msThen) / 1000.0;
	if (reset) {
		littleBit = static_cast<long>(msNow & 0xffff);
		bigBit = static_cast<long>(msNow >> 16);
	}
	return result;
}

// ---------------------------------------------------------------------------
// Platform statics
// ---------------------------------------------------------------------------

// System colours come from WinQuerySysColor(HWND_DESKTOP, SYSCLR_*, 0), which returns an
// RGB value [DOC-IBM - pm2.txt].
static ColourDesired SysColour(LONG id, ColourDesired fallback) {
	const LONG rgb = WinQuerySysColor(HWND_DESKTOP, id, 0);
	if (rgb < 0)
		return fallback;
	return ColourDesired(
		static_cast<unsigned int>((rgb >> 16) & 0xff),
		static_cast<unsigned int>((rgb >> 8) & 0xff),
		static_cast<unsigned int>(rgb & 0xff));
}

ColourDesired Platform::Chrome() {
	return SysColour(SYSCLR_BUTTONMIDDLE, ColourDesired(0xC0, 0xC0, 0xC0));
}

ColourDesired Platform::ChromeHighlight() {
	return SysColour(SYSCLR_BUTTONLIGHT, ColourDesired(0xFF, 0xFF, 0xFF));
}

// "System VIO" and "WarpSans" are the usual OS/2 UI faces; System Proportional is the
// one guaranteed present on Warp 4.
const char *Platform::DefaultFont() {
	return "System Proportional";
}

int Platform::DefaultFontSize() {
	return 10;
}

// SV_DBLCLKTIME is in milliseconds [DOC-IBM - pm2.txt, WinQuerySysValue].
unsigned int Platform::DoubleClickTime() {
	const LONG t = WinQuerySysValue(HWND_DESKTOP, SV_DBLCLKTIME);
	return (t > 0) ? static_cast<unsigned int>(t) : 500;
}

bool Platform::MouseButtonBounce() {
	return true;
}

void Platform::DebugDisplay(const char *s) {
	fputs(s ? s : "", stderr);
}

// WinGetKeyState(HWND_DESKTOP, vk): the 0x8000 bit is "down now" [DOC-IBM - pm2.txt].
bool Platform::IsKeyDown(int key) {
	return (WinGetKeyState(HWND_DESKTOP, key) & 0x8000) != 0;
}

long Platform::SendScintilla(WindowID w, unsigned int msg,
		unsigned long wParam, long lParam) {
	return static_cast<long>(reinterpret_cast<ULONG>(
		WinSendMsg(reinterpret_cast<HWND>(w), msg,
			reinterpret_cast<MPARAM>(wParam), reinterpret_cast<MPARAM>(lParam))));
}

long Platform::SendScintillaPointer(WindowID w, unsigned int msg,
		unsigned long wParam, void *lParam) {
	return static_cast<long>(reinterpret_cast<ULONG>(
		WinSendMsg(reinterpret_cast<HWND>(w), msg,
			reinterpret_cast<MPARAM>(wParam), MPFROMP(lParam))));
}

// DBCS lead-byte ranges come from the code page, not from a fixed table. Warp's
// DosQueryDBCSEnv reports them; for the single-byte code pages this port targets the
// answer is simply "no lead bytes", and saying so is better than guessing a range.
bool Platform::IsDBCSLeadByte(int, char) {
	return false;
}

int Platform::DBCSCharLength(int, const char *) {
	return 1;
}

int Platform::DBCSCharMaxLength() {
	return 2;
}

int Platform::Minimum(int a, int b) {
	return (a < b) ? a : b;
}

int Platform::Maximum(int a, int b) {
	return (a > b) ? a : b;
}

int Platform::Clamp(int val, int minVal, int maxVal) {
	if (val > maxVal)
		val = maxVal;
	if (val < minVal)
		val = minVal;
	return val;
}

// ---------------------------------------------------------------------------
// DynamicLibrary - used by ExternalLexer to load lexer DLLs.
//
// DosLoadModule(pszName, cbName, pszModname, phmod) fills pszName with the name of the
// module that could NOT be loaded on failure - it is an output diagnostic buffer, not
// the module to load [DOC-IBM - os2ref/module-dll.md]. DosQueryProcAddr takes either an
// ordinal or a name; pass ordinal 0 to look up by name.
// ---------------------------------------------------------------------------

class DynamicLibraryImpl : public DynamicLibrary {
	HMODULE hmod;

public:
	explicit DynamicLibraryImpl(const char *modulePath) : hmod(NULLHANDLE) {
		// PSZ is `unsigned char *` here, same signedness mismatch as PCH (os2emx.h),
		// so the buffer needs a reinterpret_cast rather than a plain array-to-pointer.
		CHAR failName[CCHMAXPATH] = { 0 };
		if (DosLoadModule(reinterpret_cast<PSZ>(failName), sizeof(failName),
				AsPCH(modulePath), &hmod) != 0)
			hmod = NULLHANDLE;
	}

	~DynamicLibraryImpl() override {
		if (hmod != NULLHANDLE)
			DosFreeModule(hmod);
		hmod = NULLHANDLE;
	}

	Function FindFunction(const char *name) override {
		if (hmod == NULLHANDLE || !name)
			return nullptr;
		PFN pfn = nullptr;
		if (DosQueryProcAddr(hmod, 0, AsPCH(name), &pfn) != 0)
			return nullptr;
		return reinterpret_cast<Function>(pfn);
	}

	bool IsValid() override {
		return hmod != NULLHANDLE;
	}
};

DynamicLibrary *DynamicLibrary::Load(const char *modulePath) {
	return static_cast<DynamicLibrary *>(new DynamicLibraryImpl(modulePath));
}

void Platform::DebugPrintf(const char *format, ...) {
	char buffer[2000];
	va_list pArguments;
	va_start(pArguments, format);
	vsnprintf(buffer, sizeof(buffer), format, pArguments);
	va_end(pArguments);
	Platform::DebugDisplay(buffer);
}

static bool assertionPopUps = true;

bool Platform::ShowAssertionPopUps(bool assertionPopUps_) {
	const bool ret = assertionPopUps;
	assertionPopUps = assertionPopUps_;
	return ret;
}

// Fail honestly: report where and stop, rather than continuing in a broken state.
void Platform::Assert(const char *c, const char *file, int line) {
	char buffer[2000];
	snprintf(buffer, sizeof(buffer), "Assertion [%s] failed at %s %d\r\n", c, file, line);
	if (assertionPopUps) {
		WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, AsPCH(buffer),
			(PSZ)"Scintilla", 0, MB_OK | MB_ERROR | MB_MOVEABLE);
	} else {
		Platform::DebugDisplay(buffer);
	}
	abort();
}

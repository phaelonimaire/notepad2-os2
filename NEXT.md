# Notepad2 for OS/2 — status and next steps

Checkpoint: 2026-07-29 (Find/Replace, ten dialogs, the command surface, syntax highlighting).

## Where this stands

The **platform layer is done**; the **application is not**.

| | Windows Notepad2 | this port |
|---|---|---|
| Menu commands | 245 | 155 (~63%) |
| Dialogs | 27 | 13 |
| App-layer code | 26,796 lines | 5,560 lines |

The 143 commands still missing are whole menus rather than scattered gaps, and the text-editing
surface itself is essentially complete. See "What is left" below for what each actually requires —
most of it is ordinary unwritten work, not a platform limitation.

## What is left, and what it actually needs

Nothing below is "blocked" except where explicitly said. Earlier revisions of this file used that
word for work that simply had not been written, which is a different thing and makes tractable work
look impossible. Sized honestly:

| Remaining | Needs | Size |
|---|---|---|
| ~~Line Endings~~ | **Done** — commit `1d75221` | |
| ~~Mark Occurrences~~ | **Done** — same commit | |
| ~~Info Box~~ | **Done** — `NP2InfoBox`, session-scoped suppression | |
| **Page Setup + Print** | `DevOpenDC` + `DevEscape` brackets + `DevPostDeviceModes`, fully documented in `os2ref/printing-spooler.md` | **Medium** |
| ~~Encoding / Reload~~ | **Done** — `np2/np2enc.c` + a UTF-8 drawing path in `PlatPM.cxx`, commit `aa7922e` | |
| ~~Statusbar~~ | **Done** — four `WC_STATIC` panels | |
| **Toolbar** | No PM control class — owner-drawn buttons on a composed bar. Lower value than the statusbar was | **Medium** |
| ~~Settings persistence~~ | **Done** — `np2/np2ini.c`, commit `f3e018f` | |
| ~~Launch / Run~~ | **Done** — `np2/np2run.c`, commit `caec1fd` | |
| **Scheme editor** | Settings persistence first; the schemes themselves are done | **Medium** |
| **`Dlapi.c`** (1,586 lines) → Favorites, Open With, File MRU | A `WC_CONTAINER` file browser. Routes all identified (step 6) but nothing here has exercised the control yet | **Large** |
| **Change Notify** | **Genuinely blocked.** OS/2 has no file-change notification at any layer — verified across all of `/usr/include`. The dialog is trivial; the feature must poll `DosQueryPathInfo` timestamps | **The only real platform limit** |

`scintilla/os2/` (2,655 lines) is the finished part: all 36 `Surface` virtuals, `Font`, `Window`,
`ListBox`, `Menu`, `ElapsedTime`, `DynamicLibrary`, the `Platform` statics, and `ScintillaPM.cxx` —
the control itself, with scroll bars, clipboard, and PM-timer fine tickers. Scintilla's 144
portable translation units compile unmodified. All of it verified on screen, not just at `-Wall`.

`np2/` is a working editor: open, edit, save (byte-exact) with an unsaved-changes guard, clipboard,
undo, word wrap, line numbers, and Find / Replace with match-case / whole-word / word-start / regex,
Replace All, In Selection, a search MRU and F3 repeat. Go To Line; Modify Lines with `$(...)`
numbering, Align, Sort (byte or logical, dedup, shuffle), Enclose Selection and Insert Tag; and
Settings for tabs, long lines and word wrap; and **syntax highlighting** — 21 schemes selected
automatically from the file extension, with `View > Default Font` through `WinFontDlg`.

Plus the full command surface: Lines (move/duplicate/cut/copy/delete, split, join, join paragraphs),
Block (indent, pad, strip first/last char, trim, compress whitespace, merge/remove blank lines),
Enclose shortcuts, Convert (five case modes, tabify/untabify by selection or indent), Insert
(date/time, filename, path), Special (line/stream comment, URL and C escaping, char↔hex, matching
brace, delete line/word left/right), Bookmarks, and the View toggles with zoom.

What is genuinely missing now is **encodings**, a **file browser**, and **settings persistence** —
not editing commands, and no longer highlighting.

## Build

```sh
# on the OS/2 box - see the toolkit's recipes/build-pm-app.md
export EMXOMFLD_TYPE=wlink EMXOMFLD_LINKER=wl.exe

# Scintilla core + lexers (once).  -DSCI_LEXER IS REQUIRED - see below.
cd scintilla && for f in src/*.cxx lexlib/*.cxx; do
    g++ -std=c++11 -DSCI_LEXER -O1 -c -Iinclude -Ilexlib -Isrc "$f" \
        -o "/tmp/obj/$(basename $f .cxx).o"; done
for f in lexers/*.cxx; do
    g++ -std=c++11 -O1 -c -Iinclude -Ilexlib -Isrc "$f" \
        -o "/tmp/objlex/$(basename $f .cxx).o"; done

# platform layer
g++ -std=c++11 -DSCI_LEXER -c -Iinclude -Ilexlib -Isrc os2/PlatPM.cxx     -o /tmp/platpm.o
g++ -std=c++11 -DSCI_LEXER -c -Iinclude -Ilexlib -Isrc os2/ScintillaPM.cxx -o /tmp/scipm.o

# the app
cd ../np2 && wrc -r -i=C:/usr/include np2.rc
g++ -std=c++11 -Zomf -O1 -I../scintilla/include -I../scintilla/src \
    np2.c np2find.c np2edit.c np2dlg.c np2cmd.c np2style.c np2ini.c np2run.c np2.def /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o /tmp/objlex/*.o -o np2.exe
wrc np2.res np2.exe
```

> **`np2.def` is not optional either.** Without `NAME np2 WINDOWAPI` the executable is not marked
> as a PM application, and it then runs from some sessions and exits instantly and silently from
> others — it worked over SSH for an entire session before anyone started it from `CMD.EXE`.
>
> **`-DSCI_LEXER` is not optional, and omitting it fails silently.** `case SCI_SETLEXER` lives
> inside `#ifdef SCI_LEXER` in `ScintillaBase.cxx`. Without the define, all 106 lexer objects still
> compile, still link, `Catalogue.o` still links, `SCI_SETLEXER` still returns cleanly — and every
> file renders in the default style with no error anywhere. The tell is `SCI_GETLEXER` returning 0
> after you set it to something else.

## Next steps, in order

~~1. Find / Replace.~~ **Done** — `np2/np2find.c`, commit `8270e1d`.
~~2. Save + modified-flag check.~~ **Done** — same commit.

~~3. Bulk-convert the self-contained dialogs.~~ **Done** — `np2/np2dlg.c` + `np2/np2edit.c`,
   commit `1bb2fe2`. Everything still unconverted is blocked on one of the subsystems below.

~~4. `Styles.c` — syntax highlighting.~~ **Done** — `np2/np2style.c`, commit `3395f74`. 21 schemes
   auto-detected from the file extension, plus `View > Default Font` through `WinFontDlg`.
   Still open from that area: the **scheme editor** (`IDD_STYLECONFIG` / `IDD_STYLESELECT`), which
   needs settings persistence first — schemes are compiled in today.
5. **Encoding conversion.** (Line *endings* are not part of this — they are `SCI_SETEOLMODE` /
   `SCI_CONVERTEOLS` and need nothing new; do them first, they are an afternoon.) Runs straight
   into the three independent code pages
   (process / message queue / GPI) — see the toolkit's `os2ref/unicode-conversion.md` §9.1. Also
   the prerequisite for four dialogs, for `\uXXXX` in Find/Replace, and for making sort and
   alignment character-correct rather than byte-correct.

   **Less work than it looks.** OS/2 ships `UCONV.DLL` with a full UCS-2 API, so
   `MultiByteToWideChar`/`WideCharToMultiByte` → `UniUconvToUcs`/`FromUcs`, `LCMapString` →
   `UniStrxfrm`, `CompareString` → `UniStrcoll`, `CharUpper` → `UniTransUpper`. The **only** piece
   with no counterpart is `IsTextUnicode` — OS/2 converts but does not *detect*, so the BOM sniff /
   UTF-8 well-formedness check has to be hand-written. (`unidef.h` and `uconv.h`, **not**
   `os2emx.h` — grepping the latter alone reports no Unicode support, which is false.)
6. **`Dlapi.c` (1,586 lines)** — the file browser. Needs `WC_CONTAINER`, which nothing here has
   exercised yet, so it is still the least-charted piece — but **not** the API void it was first
   called. The routes exist:
   - `ListView_*` / `TreeView_*` → one `WC_CONTAINER` in `CV_DETAIL` / `CV_TREE` view
   - `IShellFolder::EnumObjects` → `_wpQueryContent(somSelf, prev, QC_FIRST/QC_NEXT)`
     (worked loops in `wps2.txt:9634`)
   - `SHGetFileInfo` icons → `WinLoadFileIcon(pszFile, fPrivate)` / `WinFreeFileIcon`; pass
     `fPrivate = FALSE` for a shared pointer, which is the caching story
   - `SHBrowseForFolder` → `WinFileDlg` with `FDS_CUSTOM`, reading `DID_DIRECTORY_SELECTED` (270)
   - `ImageList_*` → nothing needed; container records carry `HPOINTER` directly

   Genuinely absent, so design around them: `SHAutoComplete` (no alternative) and the tray
   (WarpCenter has none; XWorkplace's taskbar does, which would be an add-on dependency).

### Suggested order from here

1. **Scheme editor** — unblocked now that settings persistence and schemes both exist.
2. **`Dlapi.c` on `WC_CONTAINER`** — the one genuinely uncharted control; unblocks Favorites,
   Open With and File MRU (4 dialogs).
3. **Print / Page Setup** — fully documented in `os2ref/printing-spooler.md`, just unwritten.
4. **Toolbar** — owner-drawn; lower value than the statusbar was.
5. **Change Notify** — the dialog is trivial; the feature must poll (no OS/2 notification API).

### Command-surface notes

- `EditToggleLineComments` uses `"//"` because no lexer is wired up; the marker should come from
  the language once `Styles.c` lands.
- Code Folding turns the margin on but a lexer has to emit fold levels for it to do anything - it
  is honest-but-inert until step 4.
- Bookmarks live on marker 1, deliberately clear of `SC_MASK_FOLDERS` so folding can use 2.
- Case conversion, tabify/untabify and the hex commands are byte-oriented like the rest of the
  port; `UniTransUpper`/`UniTransLower` is the locale-correct route and belongs with step 5.
- This Scintilla build defaults to **`SC_EOL_LF`**, not CRLF — so "convert to Unix" on a fresh
  document is correctly a no-op, which looks like a dead menu item until you check.
- `SC_EOL_*` is CRLF=0, **CR=1, LF=2** — not menu order. Any array indexed by it must follow the
  constant, not the display order.
- `NP2InfoBox` suppression is session-scoped. When settings persistence lands it should move into
  the same store as everything else.

### Conversion notes for whoever does the next batch

Every one of these cost real time at least once, and none is catchable by the compiler:

- Lay dialog coordinates out **fresh**. PM dialog units are bottom-left; converting the Win32 y
  values arithmetically yields an upside-down dialog that compiles and loads.
- Strip `~` from `LTEXT` — a PM static has no mnemonic and draws the tilde literally.
- Check each menu's mnemonics as a **set**; a duplicate silently kills the second item.
- `CONTROL` + explicit `WC_*` for composite controls. `AUTOCHECKBOX`, `AUTORADIOBUTTON`, `LTEXT`,
  `CTEXT`, `ENTRYFIELD`, `DEFPUSHBUTTON`, `PUSHBUTTON` are all fine as shorthand.
- Control change notifications (`EN_CHANGE`, `CBN_*`, `LBN_*`) arrive in **`WM_CONTROL`**, not
  `WM_COMMAND`. A case left in `WM_COMMAND` is never reached and nothing looks broken.
- `WM_INITDLG` returns `FALSE` to let PM assign the focus — inverted from Win32.

### Deliberately not doing

- **Converting Notepad2's settings to `Prf*`.** It writes its own text `.ini`, which works unchanged
  on OS/2. `Prf*` is for `OS2.INI`/`OS2SYS.INI` — the registry equivalent — not for an app's own
  config file. Change only what the platform forces.

### Known gaps in the platform layer

- `CreateCallTipWindow` and `AddToPopUp` are no-ops (call tips absent, context menu empty) — both
  visible absences rather than silent corruption, by design.
- `ListBox` image registration is unimplemented (needs `LS_OWNERDRAW` + `WM_DRAWITEM`).
- Inbound scroll-bar clicks (`WM_VSCROLL` → `ScrollTo`) are compiled and symmetric but never
  exercised — no mouse injection available. Keyboard-driven scrolling is verified.
- No drag-drop, no printing, no DBCS lead-byte handling (`IsDBCSLeadByte` returns false rather than
  consulting `DosQueryDBCSEnv`).
- Sort and alignment compare bytes, not characters; rectangular selection is refused rather than
  mishandled, so the column-sort option is disabled.
- Find/Replace does not transform `\uXXXX` above 255 (needs `UniUconv` against the editor's code
  page — see step 5), and Notepad2's `^c` "replace with clipboard" token is not wired up.

### Not applicable — OS/2 is single-seat

Notepad2 carries Win32 code that has no OS/2 counterpart because the question does not arise: the
OS/2 desktop is always one person's. LAN Server and HPFS386 carry multi-user *file permissions*, but
there is no interactive multi-user model. So `GetTokenInformation` / privilege checks,
`SHGetFolderPath(CSIDL_APPDATA)` and per-user profile paths, and any per-user-vs-machine settings
split are all N/A rather than unported. The app's `.ini` goes beside the `.EXE`.

Likewise `MonitorFromRect` / `GetMonitorInfo`: every call in Notepad2 is clamping a dialog to the
work area, and one desktop answers that completely — `pmhelpers.h` already does it with
`SV_CXSCREEN` / `SV_CYSCREEN`. See the toolkit's `recipes/porting-a-windows-app.md` §7.

### Testing note: SSH cannot exercise everything

A program started over SSH is a **detached** process on OS/2 — no keyboard, mouse or screen — and
`DosStartSession` returns `ERROR_SMG_INVALID_CALL` (418) from a detached process. So the whole
Launch menu is untestable that way and will look broken. Drive a real `CMD.EXE` on the guest
(`Alt+Esc` to it, then `keyboardputscancode`) for anything that starts another program, integrates
with the Workplace Shell, or cares about session type. See the toolkit's `recipes/setup-test-vm.md`.

### Encoding notes

- The document is held as UTF-8 inside Scintilla whatever the file was, so search, sort and case
  behave identically across encodings. Round-trip verified byte-identical.
- **`PlatPM.cxx` transcodes at the drawing boundary.** GPI draws in the GPI code page, so UTF-8
  handed straight to `GpiCharStringPosAt` renders each byte as its own 8-bit glyph. Characters the
  display page cannot represent draw as `?` — CP850 has no Greek, and that is a real limit rather
  than a defect.
- `GpiQueryCp` is a hint: it can report a value `UniMapCpToUcsCp` will not map, so the code falls
  back through the queried page, 850, then 437.
- Sort/align are still byte-oriented (`np2edit.c`); `UniStrcoll`/`UniTransUpper` would make them
  character-correct now that the conversion plumbing exists.

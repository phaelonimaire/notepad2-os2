# Notepad2 for OS/2 — status and next steps

Checkpoint: 2026-08-02 (a read-through audit of the port: one systemic buffer-overflow class fixed,
and one error found in the toolkit reference itself).

Previously: 2026-08-01 (toolbar, file change notification, and a mouse-capable test harness).

## Where this stands

The **platform layer is done**; the **application is not**.

| | Windows Notepad2 | this port |
|---|---|---|
| Menu commands (distinct) | 146 | 143 — every one that applies |
| Dialogs | 27 | 18 |
| App-layer code | 26,796 lines | 8,455 lines |

Counted reproducibly, so the number cannot drift into optimism:

```sh
# Notepad2's distinct main-menu commands - NOT a raw MENUITEM count
iconv -f UTF-16LE -t UTF-8 src/Notepad2.rc > /tmp/n2.rc   # the original is UTF-16
# then count non-SEPARATOR MENUITEM labels between IDR_MAINWND MENU and its END
```

> **The old "245" was wrong, and flattered nothing — it undersold the port.** It counted every
> `MENUITEM` line in the whole `.rc`: separators, and `IDR_POPUPMENU`, which repeats main-menu
> commands. Notepad2's actual main menu holds **146 distinct commands**. Comparing by *label*
> (ids were renamed, so a name diff finds nothing) this port covers **143 — every one that has an
> OS/2 meaning**.

**The command surface is complete.** The three below are not unwritten work; each is absent because
the platform gives the feature nothing to mean, and each was checked rather than assumed:

| Not covered | Why |
|---|---|
| Minimize to tray | **N/A.** WarpCenter has no system tray. XWorkplace's taskbar does, which would be an add-on dependency. |
| Encoding `More...` | **N/A.** Notepad2 needs a picker because it offers every installed code page; this port supports six and lists them all on the menu. Revisit if that changes. |
| Transparent mode | **N/A, measured.** No per-window alpha exists: zero matches for `AlphaBlend`/`SetWindowAlpha`/`WS_EX_LAYERED`-style flags and no `Gpi*Alpha*` entry point in `os2emx.h` — against positive controls (`WS_VISIBLE`, `SWP_MOVE`) that match, so the probe works. |

What is still missing is whole menus rather than scattered gaps, and the text-editing surface is
complete. See "What is left" for what each remaining item actually requires.

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
| ~~Favorites / Open With / Desktop Link~~ | **Done** — commit `fc2e14e`; desktop link is a real WPS shadow | |
| ~~Window title / Esc key / misc preferences~~ | **Done** — commit `793f6e4` | |
| ~~Toolbar~~ | **Done** — commit `780c5e5`; composed from `WC_BUTTON`, there being no PM toolbar class. Button presses verified by mouse | |
| ~~Settings persistence~~ | **Done** — `np2/np2ini.c`, commit `f3e018f` | |
| ~~Launch / Run~~ | **Done** — `np2/np2run.c`, commit `caec1fd` | |
| ~~Scheme editor~~ | **Done** — Customize Colours, commit `11fc683` | |
| ~~Recent Files~~ | **Done** — `WC_LISTBOX` MRU, commit `04f3171`. Per-file icons still want the container | |
| ~~File browser on `WC_CONTAINER`~~ | **Done** — `np2/np2browse.c`, commit `caff65e`. Favorites and Open With can now be built on the same code | |
| ~~Print / Page Setup~~ | **Written** — commit `1b4ddc0`. **Not fully verified**: this VM has no printer driver, so only the queue-discovery half has ever run. See "not seen working" below | |
| ~~**Change Notify**~~ | **Done** — `np2/np2watch.c`, commit `614268d`. OS/2 has no file-change notification at any layer, and this remains the only genuine platform absence found in the whole port. It cost less than the label implied: Notepad2 already polls, and only used the Win32 API as a gate in front of the timestamp comparison that does the real work | **Was the only real platform limit** |

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

Encodings, the file browser, settings persistence, the toolbar and change notification have all
landed since that paragraph was first written. What is left is **printing verification** (needs a
guest with a printer driver), the **scheme editor**, and the **long tail** of command variants —
no whole subsystem, and nothing blocked.

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

# the app.  Keep this source list complete - a file left off links cleanly right
# up until something calls into it.
cd ../np2 && wrc -r -i=C:/usr/include np2.rc
g++ -std=c++11 -Zomf -O1 -I../scintilla/include -I../scintilla/src \
    np2.c np2find.c np2edit.c np2dlg.c np2cmd.c np2style.c np2ini.c np2run.c \
    np2enc.c np2browse.c np2print.c np2watch.c np2tool.c \
    np2.def /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o /tmp/objlex/*.o -o np2.exe
wrc np2.res np2.exe        # binds the resources INTO the .exe - not optional
```

> **`wrc np2.res np2.exe` fails with `Permission denied` while the app is still running** on the
> guest — OS/2 locks a running executable, and the error names a temporary file
> (`Error! E007: Error renaming temporary file "__RCTMP89__.tmp"`) rather than saying so. Close the
> app first. Because the link step *before* it succeeded, the on-disk `.exe` is left stale rather
> than missing, so the next test silently runs the old build.

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

1. **Verify printing on a target that has a printer.** The code follows the documented sequence;
   nothing has ever reached `DevOpenDC`. Install any driver on the VM and re-test.
~~2. **Toolbar.**~~ **Done** — commit `780c5e5`.
~~3. **Change Notify.**~~ **Done** — `np2/np2watch.c`, commit `614268d`. Polls `DosQueryPathInfo`
   on a `WinStartTimer` tick, keeping Notepad2's modes, its .ini key names and its settle delay.
4. **The long tail — now enumerated, not guessed at.** Diff the two command surfaces by menu
   **label** (ids were renamed, so a name diff is useless):

   ```sh
   iconv -f UTF-16LE -t UTF-8 src/Notepad2.rc > /tmp/n2.rc   # the original is UTF-16
   # then compare MENUITEM/POPUP labels against np2/np2.rc, normalising & ~ \t and "..."
   ```

   That gives **40 differences**, of which several are only naming (their "Goto" against our
   "Go To Line...", their "&?" menu against our "Help", their "Customize Schemes" against our
   "Customize Colours"). ~~Five are done~~ — commit `6a02dc8`: Select to Next / Previous, Replace
   Next, Use Selection as Find Text, Swap with Clipboard, Complete Word.

   ~~Six more done~~ — commit `c80836d`: Save Copy, Auto-Complete Words as You Type, Save Settings
   on Exit, Remember Recent Files, Remember Search Strings, Sticky Window Position.

   Genuinely outstanding, roughly in order of value:
   - ~~**File**: Properties~~ — **done**, commit `dfb2da7`, on the WPS settings notebook. The
     Favorites trio was a **false positive**: the port already has Open (`IDM_FAVORITES`, whose pick
     dialog also removes entries, covering "Manage") and Add (`IDM_ADDTOFAV`). Notepad2 splits them
     across a submenu; this port folds them into two items.
   - ~~**Encoding**~~ — **done / not applicable**, commit `5b2d826`. *Recode* is this port's
     **Reload As**, now covering all six supported encodings. *Unicode* is already the
     `Unicode (UCS-2 LE)` item on the Encoding submenu. A `More...` selection dialog adds nothing
     over a six-item menu — Notepad2 needs one because it offers every installed code page; revisit
     if this port ever does. Fixing Reload As found a decoder bug: the byte-order mark was skipped
     unconditionally, so every BOM-less UTF-16 file lost its first character.
   - ~~**View**: Text excerpt~~ — **done**, commit `7a75c8f`. Still open: **2nd default scheme**,
     which needs a design decision rather than typing — Notepad2 keeps two default style sets and
     toggles between them, whereas this port compiles schemes in and shares one semantic palette
     (`np2style.c`). Decide whether the palette gains a second saved copy, or drop it as an artefact
     of the per-style model this port deliberately did not follow.
   - **Probably not applicable**: Transparent mode — PM on Warp 4.5x has no per-window alpha.
     Confirm against the `WS_*`/`SWP_*` set before implementing or dismissing it.
   - ~~**Reuse window**~~ — **done**, commit `c28f431`. PM has no `FindWindow` and no
     `WM_COPYDATA`; the filename travels through the **system atom table**, which any process can
     read. Written up in the toolkit as `os2ref/clipboard-dde.md` §9.
   - ~~**Single file instance**~~ — **done**, commit `79882fa`. Same machinery, opposite direction:
     it *asks* each instance whether it holds the file (`WinSendMsg`, which PM delivers across
     processes) instead of telling one to take it.
   - **Still needs design, not just work**: Customize toolbar (the toolbar is a composed row of
     `WC_BUTTON`s, so "customize" needs a model for what is configurable before a dialog can edit
     it), and Command line help.
   - **Not applicable**: Minimize to tray — WarpCenter has no tray. See "Not applicable" below.

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

- ~~`CreateCallTipWindow`~~ **Done** — commit `ec6ebf2`. A desktop-child window owned by the editor,
  painting through the same `Surface`. Verified on screen including the highlight range and a clean
  repaint on dismissal.
- ~~The context menu~~ **Done** — commit `7c74970`. `AddToPopUp` builds it with `MM_INSERTITEM`,
  `WM_BUTTON2DOWN` and Shift+F10 both raise it, and choosing an item dispatches. Verified on screen
  including the per-item enabled states.
- ~~`ListBox` image registration~~ **Done** — commit `c2f7df9`, on an owner-drawn list box. **One
  cosmetic defect**: the icon on the *highlighted* row draws in the complement of its colour — a
  `(191,0,0)` icon measures `(64,255,255)` there and `(191,0,0)` on every other row. Background,
  text, icon shape and position are all correct. Ruled out by measurement: the alpha blend
  (pre-compositing against the known background so every pixel is opaque changes nothing), the
  selection query, and mixing colour-index with RGB drawing. Left to test: whether PM applies its own
  emphasis inversion over an owner-drawn row.

**The platform layer now has no stubs.** Every `Surface`, `Window`, `ListBox`, `Menu` and `Font`
entry point is implemented.
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

### Platform-layer note

`ScintillaPM::Paint` must honour `paintState == paintAbandoned` and repaint — Scintilla uses it to
say "the rectangle you gave me was not enough". Ignoring it left the first line of a newly loaded
document correct with the rest of the screen showing the *previous* file, which reads as a load bug.
Fixed in `04f3171`; see the toolkit's `recipes/porting-a-windows-app.md` §5.0.

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

### Audit — 2026-08-02

A read-through of the whole port, checking each OS/2 convention against the Toolkit headers on the
build host and against klibc as shipping code, rather than against memory. It found **one systemic
defect, not scattered ones**: `sprintf` into a fixed buffer whose input is a filename, a directory
or the user's selection. **32 call sites**, three of them reachable in ordinary use. Fixed in
`cf1a918`:

| Was | Where | Worst case |
|---|---|---|
| `cchPick` carried and never consulted | `BrowseDlg`, `np2browse.c` | ~515 bytes into the **caller's** `CHAR[CCHMAXPATH]` |
| `sprintf(szStatus, "find text: %s", …)` | `IDM_SAVEFIND`, `np2.c` | 511-char selection → 267 bytes past a 256-byte static |
| `sprintf(szStatus, "Saved %lu bytes to %s", …)` | ordinary save path, `np2.c` | ~295 into 256, on any long path |
| `szTitle[400]` | `ShowStatus`, `np2.c` | 539 worst case |

`ShowStatus`'s two locals were **resized, not just bounded** — `snprintf` alone would have traded a
stack smash for a silent truncation of the *tail*, quietly costing `" - Notepad2 for OS/2"` off the
title bar on a deep path. `np2browse.c` grew a single `JoinPath()` because four sites had open-coded
the same `dir[strlen(dir)-1]` idiom, which also reads index `[-1]` on an empty directory; only one
of the four guarded it.

Also fixed: a stale `lcid` cache entry in `PlatPM.cxx` that would render a font in the wrong face
once a PS passed 254 setids (`6d39236`; latent — Notepad2 styles nothing like that many), and an
error string in `np2enc.c` that reported `for utf-80` (`f00da76`).

**The toolkit was wrong and the port was right**, which is the whole point of building this thing:
`os2ref/file-io.md` §5.1 paired the find-buffer records with info levels by their suffix, but the
digit in `FILEFINDBUF3` is the **API generation**, not the level — `FIL_STANDARD` is 1 and level 3
is `FIL_QUERYEASFROMLIST`. `bsedos.h:433-439` gives the values and klibc pairs `FIL_STANDARD` with
`PFILEFINDBUF3` in `fs.c:1302-1306`. Following the old text would have been silent: `FILEFINDBUF`
has no `oNextEntryOffset` and a 16-bit `attrFile`, so every directory flag and filename would come
out wrong with `NO_ERROR` returned. Corrected on the `notepad2` branch of this project's toolkit
clone (`491a02b`), not yet pushed to the hub.

One method note worth keeping: **the app sources type-check on the Linux side.**

```sh
EMX=<path to>/libc/src/emx/include
g++ -std=c++11 -fsyntax-only -m32 -I"$EMX" -Iscintilla/include -Iscintilla/src -Inp2 np2/np2.c
```

That is not a build — it cannot see the OS/2 libraries and `PlatPM.cxx` fails on `<map>` for want of
32-bit libstdc++ headers — but it catches undeclared identifiers and type errors before a file ever
reaches the guest, and it caught two mistakes in the audit fixes themselves.

### Verification status — what has NOT been seen working

Everything else described here has been exercised on screen. These have not, and should not be
read as working:

- **Printing.** The test VM has no printer driver installed and no `\spool` directory, so
  `SplEnumQueue` correctly reports zero queues and the code stops at its honest-failure message.
  The queue-discovery path and that message are verified; **`DevOpenDC` onwards has never run.**

- **Every fix from the 2026-08-02 audit.** Static verification only: the app files type-check
  against the emx headers, `JoinPath` and the `lcid` eviction were exercised as standalone
  programs, and both pre-commit checkers pass — but **nothing has been rebuilt on the guest or
  watched on screen**, and `PlatPM.cxx` has not been compiled at all since the change. Three
  cheap confirmations, in the order they are worth doing:
  - Open a file with a line of 246+ characters, select it, invoke *Use Selection As Find Text*.
    The status text should truncate; before the fix this wrote 267 bytes past `szStatus`.
  - Save a file whose full path is near `CCHMAXPATH` and check the title bar still ends in
    `" - Notepad2 for OS/2"` — that is the resize, not just the bound, being right.
  - Browse into a deep directory. A pick that cannot fit should now beep and refuse rather than
    return a truncated path naming a different file.

A status file that says "done" for something nobody has watched work is the same failure as calling
unwritten work "blocked" — it stops the next person from checking.

#### The mouse harness stopped responding mid-session

Worth knowing before trusting it: after a stretch of working clicks, the guest pointer froze and
stopped tracking `xdotool` entirely — absolute and relative motion both. The host pointer was
provably moving (`xdotool getmouselocation` confirmed it reached the intended screen position) and
the guest simply did not follow, so it is below the application. Re-focusing the VM window did not
recover it; a power-cycle is the next thing to try. Keyboard injection through `VBoxManage` kept
working throughout, which is the argument for the toolkit recipe's advice to prefer keyboard-
reachable paths for anything you need to be able to re-test.

Also: **`np2` instances accumulate.** Several launches over SSH left more than one running, and the
extra one holds `np2.exe` locked so `wrc` cannot bind resources — while Alt+F4 appears not to work,
because it closes the window you can see and not the other one. `ps | grep np2` and kill by pid
before rebuilding.

#### Now verified, and what it cost to find out

The harness *can* click. `VBoxManage` has no mouse command, but the VM's window can be driven with
`xdotool` on a nested `Xvfb` — see the toolkit's `recipes/setup-test-vm.md`, which now carries the
working rig. Three things that had been recorded here as unverifiable are verified on screen:

- **Toolbar buttons.** Pressing *Find* raises the Find dialog, so the buttons really do reach the
  same `WM_COMMAND` dispatch as the menu.
- **Inbound scroll-bar clicks** (`WM_VSCROLL` → `ScrollTo`). Three clicks on the down arrow scroll
  exactly three lines, and dragging the thumb to the top of the trough scrolls to line 1 — so the
  arrow, `SB_SLIDERTRACK` and `SB_SLIDERPOSITION` paths all work.
- **A dialog driven end to end by mouse**: click the entry field, type, click *Find Next*, and the
  match is selected with the view scrolled to it.

The first of those clicks immediately found a live regression — `IDD_FIND` and `IDD_REPLACE` had
been dropped from the `.RC` and Find had been broken for several commits (fixed in `6cb4717`). That
is the lesson worth keeping: **"the harness cannot test this" was true when it was written and
stopped being true without anyone noticing.** A capability limit is a claim with a date on it, and
this one was hiding a real bug behind it. `tools/rc-ids/check-rc-ids.py` in the toolkit now catches
the specific regression at build time.

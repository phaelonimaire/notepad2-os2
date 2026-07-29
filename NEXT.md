# Notepad2 for OS/2 — status and next steps

Checkpoint: 2026-07-29 (Find/Replace, ten dialogs, then the command surface).

## Where this stands

The **platform layer is done**; the **application is not**.

| | Windows Notepad2 | this port |
|---|---|---|
| Menu commands | 245 | 102 (~42%) |
| Dialogs | 27 | 13 |
| App-layer code | 26,796 lines | ~4,200 lines |

Of the 143 commands still missing, the great majority are the *menus* listed below rather than
scattered gaps: Encoding (8), Line Endings (4), Reload (5), Launch (7), Favorites (4), Mark
Occurrences (6), plus window-title/Esc-key/toolbar/statusbar preference groups. The text-editing
surface itself is essentially complete.

The remaining 14 dialogs are **not** more of the same work — every one of them is blocked on a
subsystem this port does not have yet, which is why the count stops here rather than at 27:

| Blocked on | Dialogs |
|---|---|
| `Styles.c` (step 4) | Style Select, Style Configure |
| encoding / `UniUconv` (step 5) | Default Encoding, Encoding, Recode, Default Line Ending |
| a file browser on `WC_CONTAINER` (step 6) | Open With, Favorites, Add To Favorites, File MRU |
| printing, unimplemented here | Page Setup |
| file-change monitoring | Change Notify |
| `DosStartSession` | Run |
| INI-backed "don't show again" | Info Box ×3 |

"Blocked" means *this port has not built it yet*, not that OS/2 lacks the API. Printing is fully
documented (`os2ref/printing-spooler.md`) and `DosStartSession` is a normal call. The one genuine
platform absence in that table is **file-change monitoring** — OS/2 has no notification API at any
layer, so Change Notify would have to poll `DosQueryPathInfo` timestamps.

`scintilla/os2/` (2,655 lines) is the finished part: all 36 `Surface` virtuals, `Font`, `Window`,
`ListBox`, `Menu`, `ElapsedTime`, `DynamicLibrary`, the `Platform` statics, and `ScintillaPM.cxx` —
the control itself, with scroll bars, clipboard, and PM-timer fine tickers. Scintilla's 144
portable translation units compile unmodified. All of it verified on screen, not just at `-Wall`.

`np2/` is a working editor: open, edit, save (byte-exact) with an unsaved-changes guard, clipboard,
undo, word wrap, line numbers, and Find / Replace with match-case / whole-word / word-start / regex,
Replace All, In Selection, a search MRU and F3 repeat. Go To Line; Modify Lines with `$(...)`
numbering, Align, Sort (byte or logical, dedup, shuffle), Enclose Selection and Insert Tag; and
Settings for tabs, long lines and word wrap.

Plus the full command surface: Lines (move/duplicate/cut/copy/delete, split, join, join paragraphs),
Block (indent, pad, strip first/last char, trim, compress whitespace, merge/remove blank lines),
Enclose shortcuts, Convert (five case modes, tabify/untabify by selection or indent), Insert
(date/time, filename, path), Special (line/stream comment, URL and C escaping, char↔hex, matching
brace, delete line/word left/right), Bookmarks, and the View toggles with zoom.

What is genuinely missing is **syntax highlighting and encodings** — not more editing commands.

## Build

```sh
# on the OS/2 box - see the toolkit's recipes/build-pm-app.md
export EMXOMFLD_TYPE=wlink EMXOMFLD_LINKER=wl.exe

# Scintilla core + lexers (once)
cd scintilla && for f in src/*.cxx lexlib/*.cxx lexers/*.cxx; do
    g++ -std=c++11 -O1 -c -Iinclude -Ilexlib -Isrc "$f" -o "/tmp/obj/$(basename $f .cxx).o"; done

# platform layer
g++ -std=c++11 -c -Iinclude -Ilexlib -Isrc os2/PlatPM.cxx     -o /tmp/platpm.o
g++ -std=c++11 -c -Iinclude -Ilexlib -Isrc os2/ScintillaPM.cxx -o /tmp/scipm.o

# the app
cd ../np2 && wrc -r -i=C:/usr/include np2.rc
g++ -std=c++11 -Zomf -O1 -I../scintilla/include -I../scintilla/src \
    np2.c np2find.c np2edit.c np2dlg.c np2cmd.c /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o /tmp/objlex/*.o -o np2.exe
wrc np2.res np2.exe
```

## Next steps, in order

~~1. Find / Replace.~~ **Done** — `np2/np2find.c`, commit `8270e1d`.
~~2. Save + modified-flag check.~~ **Done** — same commit.

~~3. Bulk-convert the self-contained dialogs.~~ **Done** — `np2/np2dlg.c` + `np2/np2edit.c`,
   commit `1bb2fe2`. Everything still unconverted is blocked on one of the subsystems below.

4. **`Styles.c` (5,169 lines)** — syntax-highlighting schemes, and the largest single win left:
   the lexers are already compiled and linked, so this is what turns the port into a *programmer's*
   editor. Needs Scintilla styling wired to an OS/2 font and colour story, then unblocks the two
   Style dialogs.
5. **Encoding / line-ending conversion.** Runs straight into the three independent code pages
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

### Command-surface notes

- `EditToggleLineComments` uses `"//"` because no lexer is wired up; the marker should come from
  the language once `Styles.c` lands.
- Code Folding turns the margin on but a lexer has to emit fold levels for it to do anything - it
  is honest-but-inert until step 4.
- Bookmarks live on marker 1, deliberately clear of `SC_MASK_FOLDERS` so folding can use 2.
- Case conversion, tabify/untabify and the hex commands are byte-oriented like the rest of the
  port; `UniTransUpper`/`UniTransLower` is the locale-correct route and belongs with step 5.

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

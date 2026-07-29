# Notepad2 for OS/2 — status and next steps

Checkpoint: 2026-07-29 (Find/Replace, then ten more dialogs).

## Where this stands

The **platform layer is done**; the **application is not**.

| | Windows Notepad2 | this port |
|---|---|---|
| Menu commands | 245 | 33 (~13%) |
| Dialogs | 27 | 13 |
| App-layer code | 26,796 lines | ~2,400 lines |

The remaining 14 dialogs are **not** more of the same work — every one of them is blocked on a
subsystem this port does not have yet, which is why the count stops here rather than at 27:

| Blocked on | Dialogs |
|---|---|
| `Styles.c` (step 4) | Style Select, Style Configure |
| encoding / `UniUconv` (step 5) | Default Encoding, Encoding, Recode, Default Line Ending |
| `Dlapi.c` shell listviews (step 6) | Open With, Favorites, Add To Favorites, File MRU |
| printing | Page Setup |
| file-change monitoring | Change Notify |
| `DosStartSession` | Run |
| INI-backed "don't show again" | Info Box ×3 |

`scintilla/os2/` (2,655 lines) is the finished part: all 36 `Surface` virtuals, `Font`, `Window`,
`ListBox`, `Menu`, `ElapsedTime`, `DynamicLibrary`, the `Platform` statics, and `ScintillaPM.cxx` —
the control itself, with scroll bars, clipboard, and PM-timer fine tickers. Scintilla's 144
portable translation units compile unmodified. All of it verified on screen, not just at `-Wall`.

`np2/` is a working editor: open, edit, save (byte-exact) with an unsaved-changes guard, clipboard,
undo, word wrap, line numbers, and Find / Replace with match-case / whole-word / word-start / regex,
Replace All, In Selection, a search MRU and F3 repeat. Go To Line; Modify Lines with `$(...)`
numbering, Align, Sort (byte or logical, dedup, shuffle), Enclose Selection and Insert Tag; and
Settings for tabs, long lines and word wrap. It is no longer only a demo, but it is a long way from
Notepad2 — the missing half is syntax highlighting and encodings, not more dialogs.

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
    np2.c np2find.c np2edit.c np2dlg.c /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o /tmp/objlex/*.o -o np2.exe
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
6. **`Dlapi.c` (1,586 lines)** — the file browser. The only piece with *no* PM equivalent: it drives
   Win32 shell listviews and would need `WC_CONTAINER`, which nothing has exercised yet. Treat as
   research, not translation. Unblocks four dialogs.

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

# Notepad2 for OS/2 — status and next steps

Checkpoint: 2026-07-29 (Find/Replace landed).

## Where this stands

The **platform layer is done**; the **application is not**.

| | Windows Notepad2 | this port |
|---|---|---|
| Menu commands | 245 | 23 (~9%) |
| Dialogs | 27 | 3 |
| App-layer code | 26,796 lines | ~1,000 lines |

`scintilla/os2/` (2,655 lines) is the finished part: all 36 `Surface` virtuals, `Font`, `Window`,
`ListBox`, `Menu`, `ElapsedTime`, `DynamicLibrary`, the `Platform` statics, and `ScintillaPM.cxx` —
the control itself, with scroll bars, clipboard, and PM-timer fine tickers. Scintilla's 144
portable translation units compile unmodified. All of it verified on screen, not just at `-Wall`.

`np2/` is a working editor: open, edit, save (byte-exact) with an unsaved-changes guard, clipboard,
undo, word wrap, line numbers, and Find / Replace with match-case / whole-word / word-start / regex,
Replace All, In Selection, a search MRU and F3 repeat. It is no longer only a demo, but it is a long
way from Notepad2.

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
    np2.c np2find.c /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o /tmp/objlex/*.o -o np2.exe
wrc np2.res np2.exe
```

## Next steps, in order

~~1. Find / Replace.~~ **Done** — `np2/np2find.c`, commit `8270e1d`.
~~2. Save + modified-flag check.~~ **Done** — same commit.

3. **Bulk-convert the remaining dialogs** in `src/Dialogs.c`. Now mechanical: 12 of its 13 Win32
   APIs map one-to-one, and the traps are documented in the toolkit's
   `recipes/porting-a-windows-app.md`. Watch `WM_INITDLG`'s inverted return, use `CONTROL` with an
   explicit `WC_*` class for composite controls (`AUTOCHECKBOX` shorthand is fine), lay the
   coordinates out fresh rather than converting the Win32 y values, and strip `~` from `LTEXT`
   labels — a static has no mnemonic and draws the tilde literally.
4. **`Styles.c` (5,169 lines)** — syntax-highlighting schemes. Needs Scintilla styling wired to an
   OS/2 font and colour story; the lexers are already compiled and available.
5. **Encoding / line-ending conversion.** Runs straight into the three independent code pages
   (process / message queue / GPI) — see the toolkit's `os2ref/unicode-conversion.md` §9.1.
6. **`Dlapi.c` (1,586 lines)** — the file browser. The only piece with *no* PM equivalent: it drives
   Win32 shell listviews and would need `WC_CONTAINER`, which nothing has exercised yet. Treat as
   research, not translation.

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
- Find/Replace does not transform `\uXXXX` above 255 (needs `UniUconv` against the editor's code
  page — see step 5), and Notepad2's `^c` "replace with clipboard" token is not wired up.

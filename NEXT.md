# Notepad2 for OS/2 — status and next steps

Checkpoint: 2026-07-29.

## Where this stands

The **platform layer is done**; the **application is not**.

| | Windows Notepad2 | this port |
|---|---|---|
| Menu commands | 245 | 17 (~7%) |
| Dialogs | 27 | 1 |
| App-layer code | 26,796 lines | 372 lines |

`scintilla/os2/` (2,655 lines) is the finished part: all 36 `Surface` virtuals, `Font`, `Window`,
`ListBox`, `Menu`, `ElapsedTime`, `DynamicLibrary`, the `Platform` statics, and `ScintillaPM.cxx` —
the control itself, with scroll bars, clipboard, and PM-timer fine tickers. Scintilla's 144
portable translation units compile unmodified. All of it verified on screen, not just at `-Wall`.

`np2/` is a working editor: open, edit, save (byte-exact), clipboard, undo, word wrap, line numbers,
one converted dialog. It is a demo of the approach, not a Notepad2.

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
    np2.c /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o -o np2.exe
wrc np2.res np2.exe
```

## Next steps, in order

1. **Find / Replace.** The cheapest real win — one dialog against Scintilla's own
   `SCI_FINDTEXT`/`SCI_SEARCHINTARGET`, no new platform work. Moves this from demo to tool.
2. **Save (not just Save as)** reusing the current filename, and a modified-flag check before
   New/Open (`SCI_GETMODIFY`, and the `SCI_SETSAVEPOINT` calls already in place).
3. **Bulk-convert the remaining dialogs** in `src/Dialogs.c`. Now mechanical: 12 of its 13 Win32
   APIs map one-to-one, and the traps are documented in the toolkit's
   `recipes/porting-a-windows-app.md`. Watch `WM_INITDLG`'s inverted return and use `CONTROL` with
   an explicit `WC_*` class for anything past text and buttons.
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

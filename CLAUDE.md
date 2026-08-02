# Notepad2 for OS/2 — working notes

Notepad2-mod + Scintilla 3.7.5 ported to OS/2 Presentation Manager. The port exists to
**validate `claude-os2-toolkit`** by building something real against it, so findings matter as much
as features: when something here costs an hour, the lesson belongs in the toolkit, not just in a
fix.

Commit 1 vendors pristine upstream, so the entire port is one additive diff against it.

## Layout

| Path | What |
|---|---|
| `scintilla/os2/` | the PM platform layer — `PlatPM.cxx` (Surface/Font/Window/ListBox/Menu), `ScintillaPM.cxx` (the control). **No stubs left.** |
| `np2/` | the application: frame, menus, dialogs, file I/O, settings |
| `src/`, `scintilla/` (rest) | pristine upstream — Scintilla's 144 portable units compile unmodified |
| `NEXT.md` | **live status and what remains.** Read it first; keep it honest. |

## Build

Builds happen **on the OS/2 guest**, not here. Copy sources over with `scp`, build in `/tmp/np2`.

```sh
export EMXOMFLD_TYPE=wlink        # -Zomf links via emxomfld, which otherwise wants a
export EMXOMFLD_LINKER=wl.exe     # missing ilink.exe on the netlabs/Arca RPM toolchain

# Scintilla core + lexers + platform layer (once; objects cached in /tmp)
#   /tmp/obj/*.o  core+lexlib   /tmp/objlex/*.o  lexers
#   /tmp/platpm.o  /tmp/scipm.o  platform layer      tree at /tmp/sci/scintilla
g++ -std=c++11 -DSCI_LEXER -c -Iinclude -Ilexlib -Isrc os2/PlatPM.cxx     -o /tmp/platpm.o
g++ -std=c++11 -DSCI_LEXER -c -Iinclude -Ilexlib -Isrc os2/ScintillaPM.cxx -o /tmp/scipm.o

# the app — keep this source list complete
cd /tmp/np2
wrc -r -i=C:/usr/include np2.rc     # wrc does NOT inherit the compiler include path
g++ -std=c++11 -Zomf -O1 -I/tmp/sci/scintilla/include -I/tmp/sci/scintilla/src \
    np2.c np2find.c np2edit.c np2dlg.c np2cmd.c np2style.c np2ini.c np2run.c \
    np2enc.c np2browse.c np2print.c np2watch.c \
    np2.def /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o /tmp/objlex/*.o -o np2.exe
wrc np2.res np2.exe                 # binds resources INTO the exe
```

**Three things are not optional and all fail quietly:**

- **`-DSCI_LEXER`.** `case SCI_SETLEXER` lives inside `#ifdef SCI_LEXER`. Without it every lexer
  still compiles and links, `SCI_SETLEXER` returns cleanly, and nothing highlights. Tell:
  `SCI_GETLEXER` returns 0 after you set it.
- **`np2.def`** (`NAME np2 WINDOWAPI`). Without it the exe is not marked PM; it runs from some
  sessions and exits instantly and silently from others.
- **A complete source list.** A file left off links fine right up until something calls into it.

**`wrc np2.res np2.exe` fails with `Permission denied` while the app is still running** on the
guest — OS/2 locks a running exe, and the error names a temp file rather than saying so. The link
step before it already succeeded, so the on-disk exe is left **stale**, and the next test silently
runs the old build. Kill the app first: `ps | grep " np2$"` then `kill -9`. Instances accumulate
across `nohup` launches, and the extra one holds the lock while Alt+F4 appears not to work.

## Testing — on screen or it isn't done

Compiling proves almost nothing here. Every bug this port has had was a **convention between
symbols**, never a wrong prototype: the toolkit was correct on every symbol looked up.

- Screenshots: `VBoxManage controlvm "<vm>" screenshotpng /tmp/shot.png`
- Keyboard: `VBoxManage controlvm "<vm>" keyboardputscancode …` — works headlessly.
  **Use a scancode table, don't hand-write them**; a wrong code doesn't fail, it types a different
  character into your document (`0x16` is `u`, not `v`).
- Mouse: `xdotool` over the VM window on a nested `Xvfb`. Coordinates read off a screenshot **are**
  guest coordinates; add the window origin and the chrome height to click them.
- **Pick your own display number.** Another session may hold `:99`. If the guest pointer "freezes",
  check whose X server is on the display before blaming the guest.

Verify a claim the way it can actually fail: count processes for a one-instance feature (a
screenshot can't tell a hand-off from a second window), sample pixels rather than judging colour by
eye, and read a trace log rather than a title-bar snapshot when you need a sequence.

## Before committing

```sh
tools/rc-mnemonics/check-mnemonics.py np2/np2.rc          # duplicate ~mnemonics (silently dead items)
tools/rc-ids/check-rc-ids.py np2/np2.h np2/np2.rc np2/*.c # missing DLGTEMPLATEs, colliding ids
```
(both in `claude-os2-toolkit`)

The id checker exists because a whole-file rewrite of a `.rc` silently dropped two dialog templates
and nothing complained until a button was pressed months later. **Prefer targeted edits to a `.rc`
and count the templates afterwards.**

Menu scopes are close to saturated. Enumerate the free letters per `BEGIN`/`END` scope rather than
guessing; where nothing natural is free, ship without a mnemonic rather than a contrived one.

## Conventions this port follows

- **Change only what the platform forces.** Don't rewrite for idiom, don't add features Notepad2
  lacks. Temporary test commands get removed in the same commit that used them.
- **Commit messages carry the reasoning** — what was measured, what was ruled out and why. Several
  sessions' worth of diagnosis lives there and nowhere else.
- **Say what has not been seen working.** A status file that claims "done" for something nobody
  watched work is the same failure as calling unwritten work "blocked".
- **"Blocked" means a capability that does not exist and you can cite the probe.** Everything else
  is unwritten work, which you then size. A capability limit is a claim with a date on it —
  re-probe before repeating it.
- **Negative results are about the probe.** Prove it works on a known-positive case before
  believing the negative; `os2emx.h` not mentioning something usually means the header isn't there,
  not that the API isn't.

## Known-good facts worth not rediscovering

- `PSZ` is `unsigned char *` — casts needed.
- `_execname` needs `#define __USE_EMX`; `-std=c++11` sets `__STRICT_ANSI__` and hides it.
- `stricmp` is not declared; use `strcasecmp`.
- `NO_ERROR` needs `INCL_DOSERRORS`; `INCL_DOS` does not imply it.
- A program started over SSH is **detached**: `DosStartSession` answers `ERROR_SMG_INVALID_CALL`
  (418), so the whole Launch menu looks broken when driven that way. Test it from a real `CMD.EXE`.
- PM never delivers **F10** to the focus window — a keyboard route to a context menu has to be a
  frame accelerator.
- A pop-up menu raised from inside a `WM_COMMAND` handler is dismissed as PM unwinds; post yourself
  a message and show it from a clean dispatch.

## Related

`claude-os2-toolkit` — the verified OS/2 reference corpus this port validates. **Another Claude
session works in it concurrently and has rewritten its history**; check `git log` before any history
operation there, never amend, never force, and stage only your own files.

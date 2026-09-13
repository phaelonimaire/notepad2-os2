# Notepad2 for OS/2 — working notes

Notepad2-mod + Scintilla 3.7.5 ported to OS/2 Presentation Manager. The port exists to
**validate `claude-os2-toolkit`** by building something real against it, so findings matter as much
as features: when something here costs an hour, the lesson belongs in the toolkit, not just in a
fix.

Commit 1 vendors pristine upstream, so the entire port is one additive diff against it.

**The repository is public** (`github.com/phaelonimaire/notepad2-os2`), with releases. See
"Releasing" below.

## Layout

| Path | What |
|---|---|
| `scintilla/os2/` | the PM platform layer — `PlatPM.cxx` (Surface/Font/Window/ListBox/Menu), `ScintillaPM.cxx` (the control). **No stubs left.** |
| `np2/` | the application: frame, menus, dialogs, file I/O, settings |
| `src/`, `scintilla/` (rest) | pristine upstream — Scintilla's 144 portable units compile unmodified |
| `build.sh`, `BUILD.md` | the public build: one command on OS/2, and the packages and traps behind it |
| `.github/README.md` | the GitHub front page. GitHub shows it in place of upstream's `Readme.md`, which stays pristine |
| `NEXT.md` | **live status and what remains.** Read it first; keep it honest. |

## Build

Builds happen **on the OS/2 guest**, not here. Move sources through the guest's shared folder, not
`scp` (see Testing), and build in a directory of your own on the guest's `C:`.

**`sh build.sh` is the build.** It is what users run and what releases are built with, and it was
checked from a fresh GitHub download on a clean VM. It globs `np2/*.c` but names the platform files,
because `scintilla/os2/` also holds `editor.cxx` and `harness.cxx`, test programs with their own
`main()`. A clean build is 146 units, about five minutes. Objects rebuild only when their own source
is newer, so **after editing a header, build with `CLEAN=1`**.

**Give every guest path its drive letter.** After `cd F:/…`, a bare `/tmp` means `F:\tmp`, not
`C:\tmp`. A build script that `cd`s to the shared folder and then writes to `/tmp` fails with
"not a directory" before compiling anything.

The dev loop earlier sessions used, with objects cached across trees. Keep it in step with
`build.sh`:

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
    np2enc.c np2browse.c np2print.c np2watch.c np2tool.c \
    np2.def /tmp/scipm.o /tmp/platpm.o /tmp/obj/*.o /tmp/objlex/*.o -o np2.exe
wrc np2.res np2.exe                 # binds resources INTO the exe
```

**Three things are not optional and all fail quietly:**

- **`-DSCI_LEXER`.** `case SCI_SETLEXER` lives inside `#ifdef SCI_LEXER`. Without it every lexer
  still compiles and links, `SCI_SETLEXER` returns cleanly, and nothing highlights. Tell:
  `SCI_GETLEXER` returns 0 after you set it.
- **`np2.def`** (`NAME np2 WINDOWAPI`). Without it the exe is not marked PM; it runs from some
  sessions and exits instantly and silently from others.
- **A complete source list** (dev loop only; `build.sh` globs). A file left off links fine right up
  until something calls into it.

**`wrc np2.res np2.exe` fails with `Permission denied` while the app is still running** on the
guest — OS/2 locks a running exe, and the error names a temp file rather than saying so. The link
step before it already succeeded, so the on-disk exe is left **stale**, and the next test silently
runs the old build. Kill the app first: `ps | grep " np2$"` then `kill -9`. Instances accumulate
across `nohup` launches, and the extra one holds the lock while Alt+F4 appears not to work.
`build.sh` refuses to link over a running exe instead.

**`yum` installs nothing if any one repository is unreachable**, even when every package is in
another. Rerun with `--disablerepo=<repo>`; `BUILD.md` documents it for users.

## Testing — on screen or it isn't done

Compiling proves almost nothing here. Every bug this port has had was a **convention between
symbols**, never a wrong prototype: the toolkit was correct on every symbol looked up.

- Screenshots: `VBoxManage controlvm "<vm>" screenshotpng /tmp/shot.png`
- Keyboard: `VBoxManage controlvm "<vm>" keyboardputscancode …` — works headlessly.
  **Use a scancode table, don't hand-write them**; a wrong code doesn't fail, it types a different
  character into your document (`0x16` is `u`, not `v`). Generate the codes from a table in a
  script, and check its output against a known string before relying on it.
- Mouse: `xdotool` over the VM window on a nested `Xvfb`. Coordinates read off a screenshot **are**
  guest coordinates; add the window origin and the chrome height to click them.
- **Pick your own display number.** Another session may hold `:99`. If the guest pointer "freezes",
  check whose X server is on the display before blaming the guest.

Verify a claim the way it can actually fail: count processes for a one-instance feature (a
screenshot can't tell a hand-off from a second window), sample pixels rather than judging colour by
eye, and read a trace log rather than a title-bar snapshot when you need a sequence. For a small
status-bar change, crop and enlarge the panel at each step, and note whether the caret moved,
because a caret move redraws the status bar anyway.

**Test a release on a machine that has never seen the build.** The dev guest has every package and
every earlier build on it, so it cannot tell you what a user is missing. Full-clone a VM, install
from what GitHub actually serves (the release zip, or the source zip fetched on the guest), test,
and delete the clone. Clone rather than boot someone's own VM, so the original is never modified.

Measured traps when testing on fresh machines:

- **Moving files:** `ssh host 'cat > file' < local` **truncated a 994 KB zip at exactly 12,288
  bytes** and exited 0. Use `scp -O` or a shared folder, and compare checksums at both ends.
- **eComStation's boot logo stays on screen until the first keypress,** even with the desktop
  running and the app already up. A screenshot shows only the logo. Tap Shift (`2a aa`) first.
- **eComStation has no `D:`,** and its `UNZIP.EXE` is in `C:\ECS\BIN`. That folder is on the
  CONFIG.SYS PATH but not on the SSH shell's, so `unzip` "not found" over SSH is not what a user sees.
- **"Has it halted?"**: two identical screenshots a minute apart, with the taskbar clock in frame.
  One pair can differ while the screen is still redrawing; take a second pair before you conclude.

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

**The repository is public, and so is this file.** Keep machine-specific details out of anything
committed: IP addresses, VM names, SSH key names, home-directory paths. Before a push that adds a
new kind of file, scan for them, and prove the scan finds a planted example first.

## Releasing

1. **Bump the version** in the About box (`np2/np2.rc`, the `Version x.y.z` line), then run the two
   checkers above.
2. **Build with `build.sh`** from the commit you will tag, on the guest. Then run the exe on screen:
   highlighting, toolbar icons, About showing the new version.
3. **Check the runtime DLLs** from the exe's LX import table, not from memory. The README lists
   `LIBCN0`, `GCC1` and `STDCPP6`; if the import list changes, the README changes.
4. **Zip layout:** `notepad2-os2/np2.exe`, `License.txt`, `License-Scintilla.txt`. BSD clause 2
   requires the notice to ship with the binary. The asset must be named exactly
   **`notepad2-os2.zip`**, because the README links to `releases/latest/download/notepad2-os2.zip`.
5. **Tag the commit the zip was built from** (annotated, with the zip and exe checksums in the
   message) and push the tag.
6. **The release itself is created in GitHub's web UI** by the maintainer. Our SSH key can push code
   and tags but cannot create a release, and there is no API token or `gh` here. It must be a
   normal release: GitHub never counts a pre-release as "latest", and the README link would break.
7. **Keep each version's zip in its own folder,** and **after the upload, download the README link
   and compare checksums.** Release 0.1.1 was first published with the 0.1 zip attached: both files
   had the same name and sat side by side, and only the checksum showed it.

`License.txt` is **Latin-1 with CRLF**, and its © is the single byte `0xA9`. Edit it as bytes, not
through a UTF-8 editor. GitHub reports its licence as `NOASSERTION`, and did so before the port's
copyright line was added: upstream's third clause names Florian Balmer where the SPDX text is
generic.

## Conventions this port follows

- **Change only what the platform forces.** Don't rewrite for idiom, don't add features Notepad2
  lacks. Temporary test commands get removed in the same commit that used them. Where the port
  diverges in behaviour, check what upstream does first: the save-marker bug was a Scintilla
  notification that upstream handles and the port had not wired up.
- **Commit messages carry the reasoning** — what was measured, what was ruled out and why. Several
  sessions' worth of diagnosis lives there and nowhere else.
- **Say what has not been seen working.** A status file that claims "done" for something nobody
  watched work is the same failure as calling unwritten work "blocked". The same goes for
  public text: the README and release notes list printing as never seen working.
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
- The statusbar redraws only on `SCN_UPDATEUI` and on `SCN_SAVEPOINTREACHED`/`SCN_SAVEPOINTLEFT`.
  A state change that neither moves the caret nor passes the save point needs its own
  `UpdateStatusbar()` call.
- `ScintillaPM::NotifyParent` forwards **every** notification to the owner, unfiltered. A missing
  reaction to one is a missing `case` in `np2.c`, not a platform-layer gap.

## Related

`claude-os2-toolkit` — the verified OS/2 reference corpus this port validates. **Another Claude
session works in it concurrently and has rewritten its history**; check `git log` before any history
operation there, never amend, never force, and stage only your own files.

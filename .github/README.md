# Notepad2 for OS/2

[Notepad2-mod](https://github.com/XhmikosR/notepad2-mod) and Scintilla 3.7.5, ported to the OS/2
Presentation Manager.

The first commit vendors the Windows sources unmodified, so the whole port is one additive diff
against upstream. The upstream readme is still at [`Readme.md`](../Readme.md); it describes the
Windows program this was ported from, not this port.

## Download

**[notepad2-os2.zip](https://github.com/phaelonimaire/notepad2-os2/releases/latest/download/notepad2-os2.zip)**
contains `np2.exe` and the licences. Unzip it anywhere and run `np2.exe`. Settings are saved to
`np2.ini` beside it. All releases are on the [releases page](https://github.com/phaelonimaire/notepad2-os2/releases).

## Requirements

- **OS/2 with the Presentation Manager desktop.** Tested on OS/2 Warp Server for e-business 4.5
  (Convenience Package). eComStation and ArcaOS have not been tried yet.
- **Three runtime DLLs from [bitwise works](https://github.com/bitwiseworks):** their kLIBC C
  library ([bitwiseworks/libc](https://github.com/bitwiseworks/libc)) and the GCC 9.2 runtime.
  Many systems already have them, because other GCC-built OS/2 software uses them too:

  | DLL | RPM package |
  |---|---|
  | `LIBCN0.DLL` | `libc` (kLIBC) |
  | `GCC1.DLL` | `libgcc` |
  | `STDCPP6.DLL` | `libstdc++` |

  They are in the netlabs RPM repositories. Install them from the command line:

  ```sh
  yum install libc libgcc libstdc++
  ```

  or search for the same package names in Arca Noae Package Manager.

  If one is missing, OS/2 refuses to start `np2.exe` and names the missing DLL.

## Status

The **Scintilla platform layer is complete**: every `Surface`, `Font`, `Window`, `ListBox` and
`Menu` entry point is implemented, with no stubs. Scintilla's 144 portable units compile unmodified.

The **editor covers 143 of Notepad2's 146 menu commands**. The other three (minimize to tray,
transparent mode, the encoding `More...` picker) have no meaning on OS/2. It has open/save
(byte-exact), Find/Replace with regex, syntax highlighting for 21 schemes, six encodings, the file
browser, the toolbar, settings persistence, change notification, Reuse Window and Single File
Instance, and the Lines / Block / Convert / Insert / Special command surface.

Not seen working yet:

- **Printing.** The code is written, but the test VM has no printer driver, so nothing has ever
  reached `DevOpenDC`.
- Sort and alignment compare bytes rather than characters, and no DBCS lead-byte handling exists.
  Neither has been tried with a proportional font.
- The highlighted row of the autocomplete list draws its icon in the complement of its colour.

[`NEXT.md`](../NEXT.md) is the live status: what remains, what was measured, and what was ruled out.

## Layout

| Path | What |
|---|---|
| `scintilla/os2/` | the PM platform layer: `PlatPM.cxx` and `ScintillaPM.cxx` |
| `np2/` | the application: frame, menus, dialogs, file I/O, settings |
| `src/`, rest of `scintilla/` | upstream, unmodified |

## Building

On OS/2, with the netlabs RPM GCC toolchain installed:

```sh
sh build.sh        # produces out-os2/np2.exe
```

[`BUILD.md`](../BUILD.md) lists the packages to install and the build options, and explains the
steps that fail silently if you build by hand.

## Why

This port was built to validate
[claude-os2-toolkit](https://github.com/phaelonimaire/claude-os2-toolkit), a reference corpus for
writing OS/2 software, by building something real against it. Where the port found the toolkit
wrong, the correction went back into the toolkit.

## Licence

Notepad2 and Notepad2-mod are BSD 3-clause ([`License.txt`](../License.txt)). Scintilla has its own
permissive licence ([`scintilla/License.txt`](../scintilla/License.txt)).

Credit for Notepad2 goes to Florian Balmer, and for Notepad2-mod to XhmikosR, Kai Liu and the
contributors listed in [`Readme-mod.txt`](../Readme-mod.txt).

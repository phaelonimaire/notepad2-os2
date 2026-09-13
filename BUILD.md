# Building Notepad2 for OS/2

The build runs **on OS/2**. There is no cross-build.

## 1. Install the toolchain

Install these packages from the netlabs RPM repositories:

```sh
yum install gcc gcc-c++ gcc-wlink gcc-wrc libstdc++ libstdc++-devel
```

If `yum` stops with `Cannot retrieve repository metadata` for one repository, it installs nothing at
all, even when the packages are in another repository. Every package above is in `netlabs-rel`, so
switch off the unreachable one for that run (`yum repolist` names them):

```sh
yum install --disablerepo=arcanoae-rel gcc gcc-c++ gcc-wlink gcc-wrc libstdc++ libstdc++-devel
```

Check what is installed with `rpm -qa | grep -E "^(gcc|libstdc)"`. `command -v` is not a reliable
test on OS/2. The port is built with GCC 9.2.0.

## 2. Build

From a copy of this repository on a local drive:

```sh
sh build.sh
```

This produces `out-os2/np2.exe`. A clean build compiles about 150 files, and running it again only
recompiles sources that have changed.

| Variable | Default | Use |
|---|---|---|
| `CLEAN=1` | off | Rebuild everything. **Use this after editing a header**, because a changed header does not trigger a rebuild. |
| `OUT=dir` | `out-os2` | Where objects and the exe go |
| `OS2INC=dir` | `C:/usr/include` | The directory holding `os2.h`, if your toolchain is on another drive |

Settings are written to `np2.ini` beside the exe.

## Traps

`build.sh` already handles each of these. They matter if you build by hand, or change the script.

- **`-DSCI_LEXER` is required.** Without it, every lexer compiles and links, `SCI_SETLEXER` returns
  cleanly, and **nothing is ever highlighted**. You can tell because `SCI_GETLEXER` returns 0 after
  you set a lexer.
- **`np2/np2.def` must be on the link line.** Its `NAME np2 WINDOWAPI` marks the exe as a PM
  application. Without it, the exe runs from some sessions and exits instantly, with no message,
  from others.
- **The resources must be bound into the exe** (`wrc np2.res np2.exe`). Without that step the exe
  has no menus, dialogs or toolbar icons.
- **Close Notepad2 before rebuilding.** OS/2 locks a running exe. Done by hand, the link succeeds
  and binding the resources then fails with `E007: Error renaming temporary file ... Permission
  denied`. That message doesn't mention the lock, and it leaves the old exe in place, so the next
  test runs the old build. `build.sh` stops with an error instead.
- **`EMXOMFLD_TYPE=wlink` and `EMXOMFLD_LINKER=wl.exe`.** `-Zomf` otherwise looks for IBM's
  `ilink.exe`, which the RPM toolchain does not include.
- **`wrc` does not use the compiler's include path.** It needs the `os2.h` directory passed to it
  with `-i=`.

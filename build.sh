#!/bin/sh
# Build Notepad2 for OS/2.  Run ON OS/2, from anywhere:  sh build.sh
#
#   OUT=dir      where objects and np2.exe go         (default: out-os2)
#   CLEAN=1      rebuild everything.  Objects are only rebuilt when their own source is newer,
#                so after changing a HEADER, build with CLEAN=1.
#   OS2INC=dir   the directory holding os2.h           (default: C:/usr/include)
#
# Needs the netlabs RPMs: gcc gcc-c++ gcc-wlink gcc-wrc libstdc++ libstdc++-devel.  See BUILD.md.

set -e
cd "$(dirname "$0")"

OUT=${OUT:-out-os2}
OS2INC=${OS2INC:-C:/usr/include}

# -Zomf links through emxomfld, which otherwise wants IBM's ilink.exe; the RPM toolchain ships
# OpenWatcom's linker as wl.exe instead.
export EMXOMFLD_TYPE=wlink
export EMXOMFLD_LINKER=wl.exe

# -DSCI_LEXER is REQUIRED.  Without it every lexer compiles and links, SCI_SETLEXER returns
# cleanly, and nothing is ever highlighted.
CXXFLAGS="-std=c++11 -O1 -DSCI_LEXER"
SCIINC="-Iscintilla/include -Iscintilla/lexlib -Iscintilla/src"

[ -n "$CLEAN" ] && rm -rf "$OUT"
mkdir -p "$OUT/core" "$OUT/lexers" "$OUT/os2"

built=0
compile() {    # compile SOURCE OBJDIR
    obj="$2/$(basename "$1" .cxx).o"
    if [ ! -f "$obj" ] || [ "$1" -nt "$obj" ]; then
        echo "  g++ $1"
        g++ $CXXFLAGS -c $SCIINC "$1" -o "$obj"
        built=$((built + 1))
    fi
}

echo "Scintilla core"
for f in scintilla/src/*.cxx scintilla/lexlib/*.cxx; do compile "$f" "$OUT/core"; done
echo "Scintilla lexers"
for f in scintilla/lexers/*.cxx; do compile "$f" "$OUT/lexers"; done
echo "PM platform layer"
# Named, not globbed: scintilla/os2 also holds editor.cxx and harness.cxx, standalone test
# programs with their own main().
for f in scintilla/os2/PlatPM.cxx scintilla/os2/ScintillaPM.cxx; do compile "$f" "$OUT/os2"; done
echo "  ($built objects rebuilt)"

# OS/2 locks a running exe.  Linking over it would succeed and binding the resources would then
# fail, leaving a stale exe that the next test silently runs, so refuse up front.
if ! rm -f "$OUT/np2.exe"; then
    echo "build.sh: cannot replace $OUT/np2.exe - close the running Notepad2 first" >&2
    exit 1
fi

echo "Notepad2"
# wrc does not inherit the compiler's include path, so it is given os2.h's directory itself.
(cd np2 && wrc -q -r -i="$OS2INC" np2.rc)

# np2.def (NAME np2 WINDOWAPI) marks the exe as a PM application.  Without it the exe runs from
# some sessions and exits instantly and silently from others.
g++ -std=c++11 -Zomf -O1 -Iscintilla/include -Iscintilla/src \
    np2/*.c np2/np2.def \
    "$OUT"/os2/PlatPM.o "$OUT"/os2/ScintillaPM.o "$OUT"/core/*.o "$OUT"/lexers/*.o \
    -o "$OUT/np2.exe"

# Bind the resources (menus, dialogs, toolbar bitmaps) into the exe.  Not optional.
wrc -q np2/np2.res "$OUT/np2.exe"

echo "Built $OUT/np2.exe"

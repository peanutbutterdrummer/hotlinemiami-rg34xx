#!/bin/bash
# Cross-compiles the native helper libraries. Run on Linux/WSL (tested: Ubuntu 22.04 under WSL2).
#   apt install gcc-arm-linux-gnueabihf gcc-aarch64-linux-gnu   (set CC64= if your aarch64 gcc has a version suffix)
# SYSROOT must contain:
#   include/            SDL2, X11, GL, EGL, KHR headers (any recent distro's -dev packages; copy /usr/include/{SDL2,X11,GL,EGL,KHR})
#   libSDL2-2.0.so.0.12.0   from the device (/usr/lib32/ on stock firmware)
#   gl4es/libGL.so.1        the armhf gl4es shipped in the release (hotlinemiami/box86/native/libGL.so.1)
set -e
cd "$(dirname "$0")"
SYSROOT=${SYSROOT:-sysroot}
CC=arm-linux-gnueabihf-gcc
I="-I$SYSROOT/include"
mkdir -p out
# fake Xlib + GLX on SDL2/EGL
$CC -O2 -fPIC -shared -fvisibility=hidden $I -Wall -Wno-unused-parameter -Wno-misleading-indentation \
  -Wl,-soname,libX11.so.6 -o out/libX11.so.6 src/x11shim.c $SYSROOT/libSDL2-2.0.so.0.12.0 -ldl -lpthread
# GL fixes layered over gl4es (box86 BOX86_LIBGL)
$CC -O2 -fPIC -shared $I -Wl,-soname,hm1gl.so -Wl,--no-as-needed -Wall -Wno-unused-parameter -Wno-misleading-indentation \
  -o out/hm1gl.so src/glspy.c out/libX11.so.6 $SYSROOT/gl4es/libGL.so.1 -ldl
# virtual Xbox 360 pad (LD_PRELOAD on box86)
$CC -O2 -fPIC -shared -fvisibility=hidden $I -Wall -Wno-format-truncation -o out/padfix.so src/padfix.c -ldl -lpthread
# framebuffer mode save/restore helper (aarch64, runs natively)
${CC64:-aarch64-linux-gnu-gcc} -O2 -s -static -o out/fbmode src/fbmode.c
# optional: Cg call logger (i386, BOX86_LD_PRELOAD) - debugging only, not shipped
if command -v i686-linux-gnu-gcc >/dev/null; then i686-linux-gnu-gcc -O2 -fPIC -shared -Wall -o out/cgspy.so src/cgspy.c -ldl; fi
ls -l out

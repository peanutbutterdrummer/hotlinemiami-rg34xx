#!/bin/bash
# package.sh <runtime-dir> [version]: builds dist/hotlinemiami-rg34xx-stock-<version>.zip
# <runtime-dir> = a working PORTS/hotlinemiami folder (box86/, x86/, shim/) to take the third-party runtime from.
# Never packages gamedata/, conf/ (saves) or logs.
set -e
cd "$(dirname "$0")"
RT=${1:?runtime dir}; VER=${2:-dev}
S=dist/stage; rm -rf dist; mkdir -p $S/hotlinemiami/{shim,x86,gamedata}
cp "port/Hotline Miami.sh" $S/
cp port/hotlinemiami/hm1.gptk out/fbmode $S/hotlinemiami/
cp port/hotlinemiami/gamedata/PUT_GAME_FILES_HERE.txt $S/hotlinemiami/gamedata/
cp out/libX11.so.6 out/hm1gl.so out/padfix.so $S/hotlinemiami/shim/
cp "$RT"/shim/libGLU* $S/hotlinemiami/shim/ 2>/dev/null || true
cp -r "$RT"/box86 $S/hotlinemiami/
cp "$RT"/x86/libCg*.so* "$RT"/x86/libstdc++.so.6 "$RT"/x86/libgcc_s.so.1 $S/hotlinemiami/x86/
cp THIRD_PARTY.md LICENSE README.md $S/hotlinemiami/
sed -i 's/\r$//' "$S/Hotline Miami.sh"
(cd $S && zip -qr9 ../hotlinemiami-rg34xx-stock-$VER.zip .)
unzip -l dist/hotlinemiami-rg34xx-stock-$VER.zip

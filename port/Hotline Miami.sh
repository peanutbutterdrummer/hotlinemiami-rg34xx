#!/bin/bash
# Hotline Miami - custom RG34XXSP stock-firmware port  # [rg34xxsp-stock]
echo "$(date) launcher start pid $$ shell=$(readlink /proc/$$/exe) args=$*" >> /tmp/hm1-launch.log 2>/dev/null
# GOG Linux i386 build (PhyreEngine, raw X11 + GLX + Cg) run through box86. There is no X server:
# shim/libX11.so.6 is a small fake Xlib/GLX (x11shim.c) on SDL2 + Mali EGL, with gl4es as the GL.
PORTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
GAMEDIR="$PORTS_DIR/hotlinemiami"
cd "$GAMEDIR/gamedata" || exit 1

exec > >(tee "$GAMEDIR/log.txt") 2>&1
echo "[rg34xx] start $(date)"
if [ ! -f Hotline ] || [ ! -f HotlineMiami_GL.wad ]; then
  echo "[rg34xx] game files missing: copy the GOG Linux build's game/ folder into $GAMEDIR/gamedata (see README)"
  exit 1
fi
# the shim switches fb0 to 720x480@32; the menu needs its own mode back (else it draws a black screen)
FBMODE=$("$GAMEDIR/fbmode" | sed -n 's/^was \([0-9]*\)x\([0-9]*\) .*@\([0-9]*\)$/\1 \2 \3/p')
# a previous run that died without restoring leaves the game's mode behind: fall back to the menu's known mode
case "$FBMODE" in ""|"720 480 32") FBMODE="1280 1024 16" ;; esac
echo "[rg34xx] menu fb mode: $FBMODE"

for c in /roms/ports/PortMaster /opt/system/Tools/PortMaster /opt/tools/PortMaster; do
  [ -f "$c/control.txt" ] && { controlfolder="$c"; break; }
done
if [ -n "$controlfolder" ]; then
  source "$controlfolder/control.txt"
  get_controls 2>/dev/null
fi

# saves + hotline.cfg live in $XDG_DATA_HOME/HotlineMiami
export HOME="$GAMEDIR/conf"
export XDG_DATA_HOME="$GAMEDIR/conf"
export XDG_CONFIG_HOME="$GAMEDIR/conf"
mkdir -p "$HOME/HotlineMiami"
# The game only creates its gamepad device when hotline.cfg exists (GameConfig::Load -> InitPad(0, joystick)).
# Format 3 = version line + key=value lines; joystick=N means /dev/input/jsN (padfix redirects it to the virtual pad).
CFG="$HOME/HotlineMiami/hotline.cfg"
if ! head -c1 "$CFG" 2>/dev/null | grep -q 3; then
  printf '3\r\nwidth=720\r\nheight=480\r\nwindowed=0\r\nlanguage=0\r\nalienware=0\r\nrumble=0\r\nvsync=1\r\ntriggerswap=0\r\njoystick=0' > "$CFG"
fi

# gl4es: SDL owns the EGL surface
export LIBGL_FB=0
export LIBGL_ES=2
export LIBGL_GL=21

# native (armhf) side: the fake libX11 must be found before any real one
# gl4es build to use: HM1_GL4ES=<dir holding libGL.so.1> (default: box86/native = gl4es 1.1.5)
GL4ES_DIR="${HM1_GL4ES:-$GAMEDIR/box86/native}"
export LD_LIBRARY_PATH="$GAMEDIR/shim:$GL4ES_DIR:$GAMEDIR/box86/native:/usr/lib32:/usr/lib/arm-linux-gnueabihf:$LD_LIBRARY_PATH"
export BOX86_LD_LIBRARY_PATH="$GAMEDIR/x86:$GAMEDIR/gamedata"
export BOX86_LIBGL="$GAMEDIR/shim/hm1gl.so"
export BOX86_LOG=${BOX86_LOG:-1}
export SDL_VIDEO_GL_DRIVER="$GL4ES_DIR/libGL.so.1"
[ -n "$sdl_controllerconfig" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

if [ -n "$GPTOKEYB" ]; then
  $GPTOKEYB "Hotline" -c "$GAMEDIR/hm1.gptk" &
fi

# padfix.so: virtual Xbox 360 pad for the game's own js0 pad support (see src/padfix.c)
LD_PRELOAD="$GAMEDIR/shim/padfix.so" "$GAMEDIR/box86/box86" ./Hotline $HM1_ARGS
echo "[rg34xx] exit status $?"

pkill -9 gptokeyb 2>/dev/null
[ -n "$FBMODE" ] && "$GAMEDIR/fbmode" $FBMODE
sync
printf "\033c" > /dev/tty0 2>/dev/null

#!/bin/bash
# shoot.sh <out dir> <stop frame> <interp 0|1> [<first>-<last>] [VAR=value ...]
# One headless run that saves every picture (exact and in-between) of game frames first..last as
# <out>/shots/interp_<frame>_<i>of<k>.ppm, its log in <out>/port.log. Extra VAR=value pairs go to the game
# (PORT_BOOT=demo:0x11, PORT_BATTLE=<case>, PORT_REPLAY=<rec>, PORT_INTERP_LOG=1, ...).
# Settings (environment): ASPECT (16:9), RES (1280x720), SCALE (8: pictures at 1/8 size; a Retina window doubles
# them, keep runs small), SPEED (max; 1 = real time), IMM (1: PORT_INTERP_IMMEDIATE=1, in-between pictures drawn at
# once; 0 = on the refresh clock, for real-time runs), BACKEND (metal / opengl / vulkan / directx), FLASH (a save).
source "$(dirname "$0")/common.sh"
OUT=$1; STOP=$2; INTERP=$3; RANGE=0-$STOP; shift 3
if [[ "$1" =~ ^[0-9]+-[0-9]+$ ]]; then RANGE=$1; shift; fi
rm -rf "$OUT"; mkdir -p "$OUT/shots" "$OUT/home"
cp "${FLASH:-$ROOT/replays/default.flash}" "$OUT/port.flash"
case "$BACKEND" in directx) BID=1; BN="DirectX 11";; opengl) BID=2; BN=OpenGL;; metal) BID=3; BN=Metal;; vulkan) BID=4; BN=Vulkan;; *) BID=;; esac
[ -n "$BID" ] && echo "{\"Window\": {\"Backend\": {\"Id\": $BID, \"Name\": \"$BN\"}}}" > "$OUT/home/pokestadium.cfg.json"
IMMV=(); [ "${IMM:-1}" = 1 ] && IMMV=(PORT_INTERP_IMMEDIATE=1)
env "${IMMV[@]}" PORT_STOP_AT=$STOP PORT_SPEED=${SPEED:-max} PORT_FLASH_FILE="$OUT/port.flash" PORT_HIDDEN_WINDOW=1 \
  PORT_RECORD=/dev/null PORT_INTERPOLATE=$INTERP PORT_RENDER_EVERY=1 PORT_ASPECT=${ASPECT:-16:9} \
  PORT_DEBUG_RESIZE=1:${RES:-1280x720} PORT_INTERP_SHOTS=$RANGE PORT_SHOT_SCALE=${SCALE:-8} \
  PORT_SCREENSHOT_DIR="$OUT/shots" PORT_SCREENSHOT_STOP_ONLY=1 PORT_FREEZE_CHECK=0 SHIP_HOME="$OUT/home" \
  "$@" "$BIN" "$ROM" > "$OUT/port.log" 2>&1
echo "exit $? $(ls "$OUT/shots" | wc -l | tr -d ' ') pictures"

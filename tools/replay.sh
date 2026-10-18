#!/bin/bash
# Usage: tools/replay.sh <recording> [stop frame] [rom]
# Replays an input recording (written by every run to build/last_input.rec; a crash also ends it there) as fast as
# possible. Runs are deterministic in lockstep mode, so this reaches exactly the same situation every time.
# The run ends at the stop frame (default: the last frame of the recording, e.g. where it crashed) and that frame is
# saved as build/shots/stop_<frame>.png. STOP=live keeps the game running with live input after the recording ends.
# UNTIL='<regex>' ends the run (capturing that frame) as soon as a log line matches, e.g. UNTIL='step 11' with
# PORT_TRACE_BATTLE=1 stops once the battle menu is up, instead of running on to the recording's end.
# Extra PORT_* variables pass through (PORT_TRACE_STATE=1, PORT_SPEED=1 to watch at normal speed, ...).
REC="$1"; STOP="$2"; ROM="${3:-/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64}"
# <recording> can be "-" for no input (e.g. with PORT_BOOT=battle, which needs none); then give a stop frame or UNTIL.
if [ "$REC" != "-" ]; then
    [ -f "$REC" ] || { echo "usage: tools/replay.sh <recording>|- [stop frame] [rom]"; exit 1; }
    REC=$(cd "$(dirname "$REC")" && pwd)/$(basename "$REC")
    [ -z "$STOP" ] && STOP=$(grep -o "^# end [0-9]*" "$REC" | tail -1 | awk '{print $3}')
fi
[ "$STOP" = "live" ] && STOP=""
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/shots
# only the stop frame is wanted: clear the previous run's pictures, and drop change-detection frames afterwards
python3 -I -c 'import os,sys; d=sys.argv[1]; [os.remove(os.path.join(d,f)) for f in os.listdir(d) if f.endswith((".ppm",".png"))]' build/shots
cd build || exit 1
export PORT_EXIT_ON_FREEZE="${PORT_EXIT_ON_FREEZE:-1}" PORT_SPEED="${PORT_SPEED:-max}" PORT_RECORD="${PORT_RECORD:-last_replay.rec}"
[ "$REC" != "-" ] && export PORT_REPLAY="$REC"
[ -n "$STOP" ] && export PORT_STOP_AT="$STOP" PORT_SCREENSHOT_DIR=shots
[ -n "$UNTIL" ] && export PORT_SCREENSHOT_DIR=shots
./pokestadium "$ROM" > run.log 2>&1 &
P=$!
if [ -n "$UNTIL" ]; then
    # stop the game as soon as the condition appears in its log
    while kill -0 $P 2>/dev/null; do
        if grep -qE "$UNTIL" run.log 2>/dev/null; then
            echo "[until] matched: $(grep -m1 -E "$UNTIL" run.log)"
            kill -USR1 $P
            break
        fi
        sleep 0.05
    done
fi
wait $P
grep -E "^\[state\]|^\[stop\]|^\*\*\* crash|^\[input\]|^\[stall\]" run.log
cd ..
if [ -n "$STOP" ] || [ -n "$UNTIL" ]; then
    python3 -I -c 'import os,sys; d=sys.argv[1]; [os.remove(os.path.join(d,f)) for f in os.listdir(d) if f.startswith("frame_")]' build/shots
    for f in build/shots/stop_*.ppm; do [ -f "$f" ] && python3 -I tools/ppm2png.py "$f" && python3 -I -c 'import os,sys; os.remove(sys.argv[1])' "$f"; done
    ls build/shots | grep stop_
fi
grep -qE "^\*\*\* crash|^\[stall\]" build/run.log && echo "(symbolize with: tools/symbolize.sh build/run.log)"
exit 0

#!/bin/bash
# Usage: tools/shot_run.sh <rom> [seconds]
# Runs the port with the framebuffer capture aid (saves only frames that changed), converts them to PNG and
# builds sheet.png (contact sheet of the saved frames; STEP=n keeps every nth), deletes the intermediate PPMs and any previous run's output, so nothing is left behind.
ROM="$1"; SECS="${2:-30}"
cd "$(dirname "$0")/.." || exit 1
OUT=build/shots
python3 -I - "$OUT" <<'PY'
import os, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
for f in os.listdir(d):
    if f.endswith((".ppm", ".png")):
        os.remove(os.path.join(d, f))
PY
cd build || exit 1
PORT_SCREENSHOT_DIR=../$OUT ./pokestadium "$ROM" > run.log 2>&1 &
P=$!   # pid of the game itself (no wrapper subshell), so the kill below really ends it
# SECS is only an upper bound: the game exits by itself once the picture has not changed for ~6 s
for ((i = 0; i < SECS; i++)); do kill -0 $P 2>/dev/null || break; sleep 1; done
kill $P 2>/dev/null; sleep 2; kill -9 $P 2>/dev/null; wait $P 2>/dev/null
cd ..
python3 -I tools/contact_sheet.py "$OUT/sheet.png" 6 $(ls "$OUT"/*.ppm | sort | awk -v s="${STEP:-1}" 'NR%s==0')
python3 -I tools/ppm2png.py $(ls "$OUT"/*.ppm | sort | tail -n "${KEEP:-1}")   # the last KEEP frames at full size
python3 -I - "$OUT" <<'PY'
import os, sys
d = sys.argv[1]
for f in os.listdir(d):
    if f.endswith(".ppm"):
        os.remove(os.path.join(d, f))
print("\n".join(sorted(os.listdir(d))))
PY

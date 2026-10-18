#!/bin/bash
# Runs ares_save: run.sh <script> [--flash-in f] [--flash-out f] [--load state]
# Like tools/ares_ref/ref.sh (same script language, .ppm pictures converted to .png), plus the FlashRAM options of
# ares_save.cpp. Flash paths may contain spaces: they are passed through symlinks in a temporary directory.
# ARES_SAVE_BUILD: directory with the ares_save binary; ARES_SAVE_SRC: the ares source tree (PIF firmware).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$1"; shift
ROM="${ROM:-/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64}"
: "${ARES_SAVE_BUILD:?set ARES_SAVE_BUILD to the directory containing ares_save}"
ARES_SAVE_SRC="${ARES_SAVE_SRC:-$ARES_SAVE_BUILD/../ares}"
export ARES_REF_FIRMWARE="$ARES_SAVE_SRC/mia/Firmware/Nintendo 64"
export DYLD_LIBRARY_PATH=/Applications/ares.app/Contents/Frameworks  # MoltenVK for paraLLEl-RDP
TMP="$(mktemp -d /tmp/ares_save.XXXXXX)"
ln -s "$ROM" "$TMP/rom.z64"
ARGS=("$TMP/rom.z64" "$SCRIPT")
OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --flash-in) ln -s "$(cd "$(dirname "$2")" && pwd)/$(basename "$2")" "$TMP/in.flash"; ARGS+=(--flash-in "$TMP/in.flash"); shift 2 ;;
        --flash-out) OUT="$2"; ARGS+=(--flash-out "$TMP/out.flash"); shift 2 ;;
        --load) ARGS+=(--load "$2"); shift 2 ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
done
"$ARES_SAVE_BUILD/ares_save" "${ARGS[@]}" 2>&1 | grep -E "^\[ref\]|error|failed|must be" || true
if [ -n "$OUT" ] && [ -f "$TMP/out.flash" ]; then mv "$TMP/out.flash" "$OUT"; fi
unlink "$TMP/rom.z64"; [ -L "$TMP/in.flash" ] && unlink "$TMP/in.flash"; rmdir "$TMP" 2>/dev/null || true
# convert pictures named by shot lines (SHEET=<out.png>: also tile them into one contact sheet, 4 per row)
python3 -I - "$SCRIPT" "$HERE/../../ppm2png.py" "$HERE/../../contact_sheet.py" "$SHEET" <<'EOF'
import os, subprocess, sys
script, conv, sheet_tool, sheet = sys.argv[1:5]
shots = []
for line in open(script):
    p = line.split()
    if len(p) >= 3 and p[0] == 'shot' and os.path.exists(p[2]):
        shots.append(p[2])
if sheet and shots:
    subprocess.run([sys.executable, '-I', sheet_tool, sheet, '4'] + shots, check=True)
for f in shots:
    subprocess.run([sys.executable, '-I', conv, f], check=True)
    os.remove(f)
EOF

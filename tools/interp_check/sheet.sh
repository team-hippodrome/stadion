#!/bin/bash
# sheet.sh <out.png> <columns> <tile width> <picture>...: the pictures tiled in order (contact sheet; needs ffmpeg)
OUT=$1; COLS=$2; W=$3; shift 3
ROWS=$(( ($# + COLS - 1) / COLS )); L=$(mktemp -t sheetXXXX)
for f in "$@"; do echo "file '$(cd "$(dirname "$f")" && pwd)/$(basename "$f")'"; done > "$L"
ffmpeg -v error -y -f concat -safe 0 -i "$L" -vf "scale=$W:-1,tile=${COLS}x${ROWS}" -frames:v 1 "$OUT"
rm -f "$L"

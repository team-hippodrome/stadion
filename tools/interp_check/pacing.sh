#!/bin/bash
# pacing.sh <out dir> [trials]
# Real time: what a run puts on screen (PORT_INTERP_LOG=3 logs every picture with its game frame and wall-clock time),
# 15 s of the cup-clear ceremony, with Smooth motion on and off, on this GPU and with a simulated slow one
# (PORT_DEBUG_DRAW_MS: every picture takes that much longer; an Intel HD 520 needs ~25 ms). Counts freezes (no new
# picture for 50 ms) and jumps (the game frame shown advances by more than 4 retraces: frames skipped to catch up).
# "old25" is the design before 2026-11 (no picture budget, n-th pairing). Other builds and sessions running on the
# machine make the numbers noisy: compare on/off in the same run, and repeat (trials).
source "$(dirname "$0")/common.sh"
OUT=$1; TRIALS=${2:-1}; mkdir -p "$OUT"
for trial in $(seq "$TRIALS"); do
    for cfg in "on0 1 0" "off0 0 0" "on25 1 25" "off25 0 25" "on14 1 14" "old25 1 25 PORT_INTERP_BUDGET=0 PORT_INTERP_MATCH=index"; do
        set -- $cfg; n=$1; i=$2; ms=$3; shift 3
        RES=320x180 SPEED=1 IMM=0 "$HERE/shoot.sh" "$OUT/$n" 900 "$i" 99999-99999 PORT_BOOT=demo:0x11 \
            PORT_INTERP_LOG=3 PORT_DEBUG_DRAW_MS="$ms" "$@" > /dev/null
        grep '^\[picture\]' "$OUT/$n/port.log" | awk -v c="$n#$trial" '{f = $3; t = $6;
            if (NR > 1) {if (t - pt > 50) fr++; if (f - pf > 4) j++} pf = f; pt = t; n++}
            END {printf "%-8s %4d pictures, freezes over 50 ms %3d, jumps over 4 retraces %3d\n", c, n, fr, j}'
        rm -rf "$OUT/$n/shots" "$OUT/$n/home"
    done
done | tee "$OUT/summary.txt"

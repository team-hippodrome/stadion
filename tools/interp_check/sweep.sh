#!/bin/bash
# sweep.sh <out dir> [16:9|4:3]
# Smooth motion over a set of scenes: the demo scenes (ceremonies), the Gym Leader Castle ending, battles with sleep
# Zs, Psychic and Fire Blast in arenas 2, 11 (Koga) and 15 (Elite Four), and eight replays. Every picture is saved
# small and each in-between picture is scored against its two exact neighbours (tri.c); the three worst per scene
# are kept as <scene>/worst_<frame>.png (previous, in-between, current), the pictures deleted. Also per scene
# (PORT_INTERP_LOG=1): blended matrices, ambiguous partners, pairs the old n-th rule would have got wrong, scene cuts.
# SCENES="demo0x11 battle_arena11 ..." runs only those. Add VAR=value pairs in EXTRA="..." (e.g. EXTRA="PORT_INTERP_MATCH=index" for the old pairing). Needs ffmpeg.
source "$(dirname "$0")/common.sh"
OUT=$1; AS=${2:-16:9}; mkdir -p "$OUT/cases"; tri_build
RES=1280x720; [ "$AS" = 4:3 ] && RES=960x720
cp "$HERE/cases/sleep_fx.txt" "$OUT/cases/"
for a in 2 11 15; do sed "s/^arena ARENA/arena $a/" "$HERE/cases/arena_fx.txt" > "$OUT/cases/arena$a.txt"; done
run() {
    local name=$1 stop=$2 d=$OUT/$1; shift 2
    [ -n "$SCENES" ] && [[ " $SCENES " != *" $name "* ]] && return
    ASPECT=$AS RES=$RES "$HERE/shoot.sh" "$d" "$stop" 1 0-"$stop" PORT_INTERP_LOG=1 $EXTRA "$@" > /dev/null
    "$TRI" "$d/shots" > "$d/tri.txt"
    sort -k6 -nr "$d/tri.txt" | awk '$4 > 0.002' | head -3 | while read t i k mo no sc md; do
        p=$(ls "$d/shots" | awk -F_ -v t="$t" '$2 + 0 < t' | tail -1)
        m=$(printf "interp_%05d_%dof%d.ppm" "$t" "$i" "$k"); c=$(printf "interp_%05d_%dof%d.ppm" "$t" "$k" "$k")
        ffmpeg -v error -y -i "$d/shots/$p" -i "$d/shots/$m" -i "$d/shots/$c" \
            -filter_complex "[0][1][2]hstack=3,scale=960:-1" "$d/worst_$t.png"
    done
    rm -rf "$d/shots" "$d/home"
}
J=${JOBS:-4}
jobs_wait() { while [ "$(jobs -rp | wc -l)" -ge "$J" ]; do sleep 1; done; }
for x in 0x11 0x15 0x16 0x17 0x1A 0x18; do jobs_wait; run demo$x 2400 PORT_BOOT=demo:$x & done
jobs_wait; run castle_clear 2400 PORT_BOOT=castle_clear &
jobs_wait; run battle_sleep 4200 PORT_BATTLE="$OUT/cases/sleep_fx.txt" &
for a in 2 11 15; do jobs_wait; run battle_arena$a 4200 PORT_BATTLE="$OUT/cases/arena$a.txt" & done
for r in to_battle tour_attract tour_fb tour_mg_dynamo tour_galmenu2 tour_play tour_vp1 tour_sandtr; do
    jobs_wait; run rep_$r 4000 PORT_REPLAY="$ROOT/replays/$r.rec" &
done
wait
printf "%-20s %6s %8s %6s %8s %9s %8s %6s\n" scene pics "score>1.5" max blended ambiguous misindex cuts
for d in "$OUT"/*/; do
    n=$(basename "$d"); [ "$n" = cases ] || [ ! -f "$d/tri.txt" ] && continue
    s=$(awk '$4 > 0.002 {c++; if ($6 > 1.5) bad++; if ($6 > m) m = $6} END {printf "%6d %8d %6.2f", c, bad, m}' "$d/tri.txt")
    l=$(grep '^\[interp\] frame' "$d/port.log" | awk '{for (i = 1; i <= NF; i++) {if ($i == "blended") b += $(i+1);
        if ($i == "ambiguous") a += $(i+1); if ($i == "misindexed") x += $(i+1)} if (/SCENE CUT/) sc++}
        END {printf "%8d %9d %8d %6d", b, a, x, sc}')
    printf "%-20s %s %s\n" "$n" "$s" "$l"
done | tee "$OUT/summary.txt"

#!/bin/sh
# The runs behind routes/particles/README.md, from the worktree root: OUT=<dir> sh evidence/whole_game/routes/particles/runs.sh
# (the Game Boy cartridge and the base save are the user's own: GB=<yellow rom>, BASE=<base.sav from gb_save.py>)
set -e
B=build-evidence-all2/pokestadium
OUT=${OUT:?out dir}
GB=${GB:-/Users/michael/Projects/Pokemon/pokestadium-port/gb/yellow_de.gb}
mkdir -p "$OUT"
r() { n=$1; shift; python3 -I tools/evidence/run.py "$OUT/$n" --bin $B --seq-max 2 "$@" > "$OUT/$n.log" 2>&1
      rm -f "$OUT/$n/seq.txt" "$OUT/$n/calls.txt"; tail -1 "$OUT/$n.log"; }

# Ice Punch: its effect model sets the func_84345448 node callback (mode 0) but the node is never drawn (mode 2)
r mv8 --seed 7 --frames 14000 --moves 8,8,8,8:1,1,1,1
# Spike Cannon with the battle_xcheck route's Charmander: effect 62 runs 20-21 frames per playback
r mv131 --seed 7 --frames 14000 --moves 131,131,131,131:1,1,1,1
# Spike Cannon from its real users (Cloyster, Omastar) in a Game Pak Free Battle: 21-26 frames per playback
python3 -I tools/evidence/gb_party.py "${BASE:?base save}" "$OUT/spike.sav" \
  "CLOYSTER:50,moves=131/131/131/131;OMASTAR:50,moves=131/131/131/131;CLOYSTER:50,moves=131/131/131/131;OMASTAR:50,moves=131/131/131/131;CLOYSTER:50,moves=131/131/131/131;OMASTAR:50,moves=131/131/131/131"
r fb_spike --seed 7 --frames 30000 --rec evidence/battle_engine/routes/free_battle_gamepak.rec --gb "$GB:$OUT/spike.sav" \
  --watch BattleAnim_StartEffect62OwnerLinkedFourStream_Init,OwnerLinkedFourStream_IsComplete

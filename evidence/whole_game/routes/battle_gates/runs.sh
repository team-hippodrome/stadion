#!/bin/sh
# runs.sh <out dir> <base.sav>: the battle_gates runs (README.md in this folder says what each reaches).
# <base.sav>: a save from tools/gb_save.py --got-pokedex (saves stay out of the repository).
set -e
cd "$(dirname "$0")/../../../.."
O=$1; BASE=$2; B=build-evidence-all2/pokestadium; D=evidence/whole_game/routes/battle_gates
CART=/Users/michael/Projects/Pokemon/pokestadium-port/gb/yellow_de.gb
FB=evidence/battle_engine/routes/free_battle_gamepak.rec
T="BLASTOISE:70;MEW:100;MAGIKARP:5"
mkdir -p "$O"
party() { python3 -I tools/evidence/gb_party.py "$BASE" "$O/$1.sav" "$2" > /dev/null; }
run() { n=$1; shift; python3 -I tools/evidence/run.py "$O/$n" --bin $B --seq-max 2 --dump-every 600 "$@" > "$O/$n.log" 2>&1; }

party six "PIKACHU:25;VENUSAUR:50;CHARIZARD:60;$T"
# AI trainer flag 0x400000: Free Battle 1P vs COM with "COM's Level" Hard (one UP on the level dialog)
run com_hard --frames 30000 --rec $D/free_battle_com_hard.rec --gb $CART:"$O/six.sav"
# six entries and six picks in order select, then B (POKeMON) and R (Check) in the first turn: the six-row list
run six_check --frames 14500 --natural --rec $D/free_battle_six_check.rec --gb $CART:"$O/six.sav"
# B, the active Pokemon's own button (CUP: "already out"), A, L (cancel) in the party menu
run party_menu --frames 13300 --natural --rec $D/free_battle_party_menu.rec --gb $CART:"$O/six.sav"

# forced mode (the player's first move with PP left): both teams come from the cartridge (same three leads)
party wrap "ARBOK:50,moves=43;VENUSAUR:50;CHARIZARD:60;$T"
run wrap --frames 26000 --rec $FB --gb $CART:"$O/wrap.sav" --moves 92,35,0,0:1,20,0,0      # Toxic, then Wrap
party fly "PIDGEOT:50,moves=92;ELECTRODE:50,moves=92;VENUSAUR:50,moves=92;$T"
run fly --frames 34000 --rec $FB --gb $CART:"$O/fly.sav" --moves 19,0,0,0                  # Fly vs Toxic
party subrest "TAUROS:50,moves=49;VENUSAUR:50;CHARIZARD:60;$T"
run subrest --frames 26000 --rec $FB --gb $CART:"$O/subrest.sav" --moves 164,156,150,0:1,1,20,0  # Substitute, Rest
party parrest "TAUROS:50,moves=86/33;VENUSAUR:50,moves=86/33;CHARIZARD:60,moves=86/33;$T"
run parrest --frames 30000 --rec $FB --gb $CART:"$O/parrest.sav" --moves 150,156,0,0:3,10,0,0     # Splash x3, Rest

# the battle_xcheck battle (Poke Cup round 1, seed 7) with one move
for m in 12 60 100 154 155; do run mv$m --seed 7 --frames 24000 --moves $m,0,0,0:40,0,0,0; done
run burn7 --seed 7 --frames 24000 --moves 126,0,0,0:40,0,0,0                               # Fire Blast: a burn

#!/bin/bash
# runs.sh <out dir> <name>...: the gate_trials2 runs (../../gate_trials2.md says what each opens and how).
# Saves stay out of the repository: <out>/saves/ is made from replays/*.flash, the scratchpad flash4/ saves and a
# tools/gb_save.py --got-pokedex base (GBBASE), as each run's comment says.
set -e
cd "$(dirname "$0")/../../../.."
O=$1; shift; B=build-evidence-all2/pokestadium; D=evidence/whole_game/routes/gate_trials2
S=/private/tmp/claude-501/-Users-michael-Projects-Pokemon/1fd4be59-42a7-4591-b1e0-223ce98f21f6/scratchpad
CART=/Users/michael/Projects/Pokemon/pokestadium-port/gb/yellow_de.gb; GBBASE=${GBBASE:-$S/gbv_sl/base_in.sav}
FB=evidence/battle_engine/routes/free_battle_gamepak.rec; CM=evidence/whole_game/routes/castle_mewtwo; P=$S/par_castle_mewtwo
T="BLASTOISE:70;MEW:100;MAGIKARP:5"
mkdir -p "$O/saves"; V=$O/saves
ss() { PYTHONPATH=tools/save_editor python3 -m stadium_save "$@" > /dev/null; }
party() { python3 -I tools/evidence/gb_party.py "$GBBASE" "$V/$1.sav" "$2" > /dev/null; }
run() { n=$1; shift; rm -rf "$O/$n"; nice -n 15 python3 -I tools/evidence/run.py "$O/$n" --bin $B --seq-max 2 --dump-every 600 "$@" > "$O/$n.log" 2>&1 || true
  rm -f "$O/$n"/{seq.txt,port_last.rec,port.flash,gb.sav,port.trace,calls.txt}; rm -rf "$O/$n/shots"; echo "$n: $(tail -1 "$O/$n.log")"; }
for n in "$@"; do case $n in
# Round 2, real: an unlocked save with Round 2 progress and the Round-2 choice stored (options +0x02 bit 2, what
# the main menu's C-Right "STADIUM Switch" writes), then the stored Poke Cup recordings
r2_cup)    ss unlock --round2 --mewtwo replays/default.flash -o "$V/r2.flash"; ss set "$V/r2.flash" options.sound=4
           run r2_cup --frames 4800 --natural --rec replays/cup_giveup.rec --flash "$V/r2.flash" ;;
r2_cupwin) ss unlock --round2 --mewtwo replays/cup_win_badge.flash -o "$V/cwb_r2.flash"; ss set "$V/cwb_r2.flash" options.sound=4
           run r2_cupwin --frames 20500 --natural --rec replays/cup_win_badge.rec --flash "$V/cwb_r2.flash" ;;
# Round 2 Castle / Mewtwo: these routes enter their state from the title screen, past the main menu, so the
# menu's inRound2 (gSessionContext+0x1296) is set with the state (forced, the value the menu sets from this save)
r2_castle) ss unlock --round2 $S/flash4/teams.flash -o "$V/teams_r2.flash"; ss set "$V/teams_r2.flash" options.sound=4
           run r2_castle --frames 64000 --natural --rec $CM/castle_gym_clear.rec --flash "$V/teams_r2.flash" --gb $CART:$P/op6.sav \
             --set "State_TitleScreen#1@exit:gCurrentGameState:4=39;State_TitleScreen#1@exit:gSessionContext+0x1296:1=1" ;;
r2_mewtwo) ss unlock --round2 --mewtwo $S/flash4/unlocked.flash -o "$V/unl_r2.flash"; ss set "$V/unl_r2.flash" options.sound=4
           run r2_mewtwo --frames 30000 --natural --rec $CM/mewtwo_win.rec --flash "$V/unl_r2.flash" --gb $CART:$P/op100b.sav \
             --set "State_TitleScreen#1@exit:gCurrentGameState:4=34;State_TitleScreen#1@exit:gSessionContext+0x1296:1=1" ;;
# Victory Palace, real: statues of 1-150 (no Mew: the species grid is on, no all-caught finale), A/B/START/L/R
vp)        ss statue replays/default.flash set all -o "$V/st.flash"; ss statue "$V/st.flash" clear 151 -o "$V/st150.flash"
           run vp_overlays --frames 4800 --natural --rec $D/victory_palace_overlays.rec --flash "$V/st150.flash" ;;
# species-specific particle offsets, real movesets: Onix with Dig (TM28), Vileplume with Reflect (TM33)
onix_dig)  party onix_dig "ONIX:50,moves=91;VENUSAUR:50;CHARIZARD:60;$T"
           run onix_dig --frames 20000 --rec $FB --gb $CART:"$V/onix_dig.sav" --moves 91,0,0,0 ;;
vile_reflect) party vile_reflect "VILEPLUME:50,moves=115;VENUSAUR:50;CHARIZARD:60;$T"
           run vile_reflect --frames 20000 --rec $FB --gb $CART:"$V/vile_reflect.sav" --moves 115,0,0,0 ;;
# the completion gift scene (every statue, Victory Palace) with a bonus Pokemon already waiting at the Lab (real:
# options +0x1C, what an earlier gift left): the swap prompt, kept and swapped
gift_keep|gift_swap) ss statue replays/default.flash set all -o "$V/st.flash"; ss options "$V/st.flash" --bonus MEWTWO -o "$V/st_bonus.flash"
           r=completion_gift_${n#gift_}; run $r --frames 7200 --natural --rec $D/$r.rec --flash "$V/st_bonus.flash" ;;
*) echo "unknown run $n"; exit 1 ;;
esac; done

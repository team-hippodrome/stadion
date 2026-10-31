#!/bin/sh
# runs.sh <out dir> <scratch dir>: the engine_left runs (README.md in this folder says what each reaches and which
# conditions are forced). <scratch dir> holds the saves that stay out of the repository: flash4/labfull.flash,
# flash4/unlocked.flash, gbv_sl/boxes_full.sav, par_castle_mewtwo/opR.sav (README, castle_mewtwo).
set -e
cd "$(dirname "$0")/../../../.."
O=$1; S=$2; B=build-evidence-all2/pokestadium; D=evidence/whole_game/routes/engine_left; R=evidence/whole_game/routes
G=/Users/michael/Projects/Pokemon/pokestadium-port/gb/yellow_de.gb
FB=evidence/battle_engine/routes/free_battle_gamepak.rec
mkdir -p "$O"
run() { n=$1; shift; nice -n 15 python3 -I tools/evidence/run.py "$O/$n" --bin $B --seq-max 2 --dump-every 300 "$@" > "$O/$n.log" 2>&1; }
python3 -I tools/gb_save.py "$O/base.sav" --got-pokedex > /dev/null
python3 -I tools/evidence/gb_party.py "$O/base.sav" "$O/moltres.sav" "MOLTRES:60;ZAPDOS:60;ARTICUNO:60;BLASTOISE:70;MEW:100;MAGIKARP:5" > /dev/null
python3 -I tools/evidence/gb_party.py "$O/base.sav" "$O/six.sav" "PIKACHU:25;VENUSAUR:50;CHARIZARD:60;BLASTOISE:70;MEW:100;MAGIKARP:5" > /dev/null

# 33FE0.c: the type-11 slot model (objectType 0x92) on screen
run moltres --frames 16000 --rec $FB --gb $G:"$O/moltres.sav"
# battle_prep: a Poke Cup loss (winner flags swapped, three continues) and a draw (both flags equal), first option
L="BattlePrep_RunRulesScreen#2:gSessionContext"
run cup_loss_opt0 --frames 26000 --natural --rec $D/cup_loss_opt0.rec --flash replays/cup_win_badge.flash \
    --set "$L+0x1220:1=0;$L+0x1270:1=1;$L+0x1297:1=3"
run cup_draw0 --frames 26000 --natural --rec $D/cup_loss_opt0.rec --flash replays/cup_win_badge.flash \
    --set "$L+0x1220:1=0;$L+0x1270:1=0;$L+0x1297:1=3"
run cup_draw1 --frames 26000 --natural --rec $D/cup_loss_opt0.rec --flash replays/cup_win_badge.flash \
    --set "$L+0x1220:1=1;$L+0x1270:1=1;$L+0x1297:1=3"
# battle_prep: the two-trainer roster (side 1 marked as two trainers; Anything Goes is rule category 0)
run roster_double --frames 10400 --rec $FB --gb $G:"$O/six.sav" --set "BattlePrepRoster_Init:gSessionContext+0x1239:1=2"
# battle_order_select: slot detail (natural) and the level-cap notice (rule category forced to 3, Poke Cup)
run order_detail_cap --frames 11300 --natural --rec $D/order_detail_cap.rec --gb $G:"$O/six.sav" \
    --set "BattleMenu_InitOrderSelectPanels@exit:gSessionContext+0x1:1=3"
# rental_rules: the qualifying-species window scrolled
run rules_species_scroll --frames 5600 --natural --rec $D/rules_species_scroll.rec
# lab_pc_list: HM into a free move slot, the level-range filter, the item box written back
run hm_squirtle --frames 9000 --rec $D/hm_squirtle.rec --flash $S/flash4/labfull.flash --gb $G:$S/gbv_sl/boxes_full.sav
run lab_level_group --frames 9000 --rec $D/lab_level_group.rec --flash $S/flash4/labfull.flash --gb $G:$S/gbv_sl/boxes_full.sav
run tm24_save --frames 11000 --rec $D/tm24_save.rec --flash $S/flash4/labfull.flash --gb $G:$S/gbv_sl/boxes_full.sav
# scheduler / reset / dp_intro: the reset button's PRE-NMI event (the port never posts it), after the title screen
run prenmi --frames 3000 --natural --rec $D/title_a.rec --call "State_TitleScreen#1@exit:port_PostEvent:v:i14"
# gym_leader_castle_clear: the castle clear with the Round 2 flag (gSessionContext.inRound2) set
run glc_round2 --frames 126000 --natural --rec $R/castle_mewtwo/castle_e4_rival_clear.rec --flash $S/flash4/unlocked.flash \
    --gb $G:$S/par_castle_mewtwo/opR.sav --set "State_TitleScreen#1@exit:gCurrentGameState:4=39;Glc_ClearInitState:gSessionContext+0x1296:1=1"

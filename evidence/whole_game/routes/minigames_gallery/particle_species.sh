#!/bin/sh
# particle_species.sh <out dir> <base.sav>: species-specific particle offsets (Particle_Field38_AddXZFromField94YOffsetMinus90_0901C)
# behind BattleAnim_GetOwnerSpecies checks: the species leads a Game Pak Free Battle and uses a window of four moves
# (PP 1 each, the forced-mode pick takes them in turn) whose animations call the species-offset helper (from the
# batch4 move sweep: which mvN window reached each helper). Saves stay out of the repository.
cd "$(dirname "$0")/../../../.."
O=$1; BASE=$2; B=build-evidence-all2/pokestadium
CART=/Users/michael/Projects/Pokemon/pokestadium-port/gb/yellow_de.gb
FB=evidence/battle_engine/routes/free_battle_gamepak.rec
T="BLASTOISE:70;MEW:100;MAGIKARP:5"
mkdir -p "$O"
one() { n=$1 mon=$2 w=$3
  python3 -I tools/evidence/gb_party.py "$BASE" "$O/$n.sav" "$mon:60;VENUSAUR:50;CHARIZARD:60;$T" > /dev/null
  nice -n 15 python3 -I tools/evidence/run.py "$O/$n" --bin $B --seq-max 2 --dump-every 600 --frames 30000 --rec $FB \
    --gb $CART:"$O/$n.sav" --moves $w:1,1,1,1 > "$O/$n.log" 2>&1; rm -f "$O/$n.sav"; }
one onix145 ONIX 145,146,147,148 &        # BattleAnim_ApplyMove146SpeciesOffset (0x5F)
one onix141 ONIX 141,142,143,144 &        # BattleAnim_GetSpeciesScaleAndOffset (0x5F)
one onix133 ONIX 133,134,135,136 &        # BattleAnim_ApplySpeciesYOffsetVariantA (0x5F)
one onix21 ONIX 21,22,23,24; wait         # BattleAnim_ApplySpeciesXZOffsetVariantB, BattleAnim_GetSpeciesScaleAdjust (0x5F)
one dewgong133 DEWGONG 133,134,135,136 &  # BattleAnim_ApplySpeciesYOffsetVariantA (0x57)
one vileplume109 VILEPLUME 109,110,111,112 &  # BattleAnim_ApplySpeciesMotionOffset (0x2D)
one vaporeon45 VAPOREON 45,46,47,48 &     # BattleAnim_ApplyModelSpeciesOffset (0x86)
one dragonair9 DRAGONAIR 9,10,11,12; wait # BattleAnim_ApplyEffect125ModelScaleOffset (0x94)
one dragonair121 DRAGONAIR 121,122,123,124 &
one vileplume113 VILEPLUME 113,114,115,116 &
one vaporeon37 VAPOREON 37,38,39,40; wait

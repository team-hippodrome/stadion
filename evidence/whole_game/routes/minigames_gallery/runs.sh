#!/bin/sh
# runs.sh <out dir> <gbv dir>: the minigames_gallery runs (README.md in this folder says what each reaches).
# <gbv dir>: Game Boy saves from tools/evidence/gb_variants.py (base.sav, boxes_full.sav, bad_checksum.sav).
# The Stadium flashes are made here from replays/default.flash (tools/save_editor); the album flash is the one the
# gal_album run writes. Saves and flashes stay out of the repository.
set -e
cd "$(dirname "$0")/../../../.."
O=$1; V=$2; B=build-evidence-all2/pokestadium; D=evidence/whole_game/routes/minigames_gallery
Y=/Users/michael/Projects/Pokemon/pokestadium-port/gb/yellow_de.gb
mkdir -p "$O"
run() { n=$1; shift; nice -n 15 python3 -I tools/evidence/run.py "$O/$n" --bin $B --natural "$@" > "$O/$n.log" 2>&1
        rm -f "$O/$n/seq.txt" "$O/$n/calls.txt" "$O/$n/port.trace"; }
se() { (cd tools/save_editor && python3 -m stadium_save "$@" > /dev/null); }
st() { echo "State_TitleScreen#1@exit:gCurrentGameState:4=$1"; }
A=$(cd "$O" && pwd)

# Victory Palace: all 151 statues, Nos. 1 and 3 won with rentals (OT = the player label, ID 0). The first entry gives
# the Hall of Fame gift and leaves; the second run starts from the flash the game wrote after it.
se statue ../../replays/default.flash set all -o "$A/vp_all.flash"
se statue "$A/vp_all.flash" set 1 --ot "<PLAYER>" --trainer-id 0
se statue "$A/vp_all.flash" set 3 --ot "<PLAYER>" --trainer-id 0
run vp_gift --frames 6200 --rec $D/vp_overlays.rec --flash "$O/vp_all.flash" --set "$(st 36)"
run vp_overlays --frames 6200 --rec $D/vp_overlays.rec --flash "$O/vp_gift/port.flash" --set "$(st 36)"

# Gallery, Snap photo with two Game Paks (controller 2 connected): Pokemon select, box picker, Surfing Pikachu,
# camera, 24 photos and the exit sequence; then film, album, Pick photo, paste, saving (the flash keeps the album).
G2="--gb $Y:$V/base.sav --gb2 $Y:$V/boxes_full.sav"
C2="Cont_InitControllers:gControllerBits:1|=3"
run gal_camera --frames 8800 --rec $D/gal_camera.rec $G2 --set "$(st 17);$C2"
run gal_album --frames 16000 --rec $D/gal_album.rec $G2 --set "$(st 17);$C2"
run gal_tutorial --frames 6500 --rec $D/gal_tutorial.rec --set "$(st 17)"
run gal_busy --frames 3400 --rec $D/gal_busy.rec --gb $Y:$V/base.sav --gb2 $Y:$V/bad_checksum.sav --set "$(st 17);$C2"
run gal_busy_boot --frames 3800 --rec $D/gal_busy_boot.rec --gb $Y:$V/bad_checksum.sav
run gal_reselect --frames 5600 --rec $D/gal_reselect.rec --set "$(st 17)"

# All stages cleared (round 2 cleared_modes 0x1F8) + the album: L+R on Print opens the Bonus stickers screen.
se unlock "$A/gal_album/port.flash" --mewtwo --round2 -o "$A/photos_unlocked.flash"
run gal_print --frames 6000 --rec $D/gal_print.rec --flash "$O/photos_unlocked.flash" --set "$(st 17)"
# Forced (Snap Station hardware): the gallery's accessory flag set, Print and its save prompt, L+R Bonus confirm.
SS="Gallery_SnapUpdateMenuIconStates:D_8330343C:4=1"
run gal_ss_print --frames 6000 --rec $D/gal_ss_print.rec --flash "$O/gal_album/port.flash" --set "$(st 17);$SS"
run gal_ss_bonus --frames 6000 --rec $D/gal_ss_bonus.rec --flash "$O/photos_unlocked.flash" --set "$(st 17);$SS"
# The Snap Station boot state (STATE_FAST_N64_LOGO 0x40) prints the four stickers as sixteen seals.
run seals --frames 5000 --rec $D/seals.rec --flash "$O/gal_album/port.flash" --set "$(st 64)"

# Kids Club: Pick a game, 1P, Ready, COM level, then B on the game grid (the round banner closes).
run kc_grid_cancel --frames 4800 --rec $D/kc_grid_cancel.rec --set "$(st 35)"

# Species-specific battle particle offsets (its own script: Game Pak Free Battles).
sh $D/particle_species.sh "$O/pm" "$V/base.sav"

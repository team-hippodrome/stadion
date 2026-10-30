#!/bin/bash
# The error-path routes (README.md): every run, with the cartridge, save and flash variants it needs.
# Usage: evidence/whole_game/routes/error_paths/runs.sh <out dir> [run name ...]   (default: all; from the repo root)
# Needs: S = the evidence scratchpad (gbv_sl/*.sav, flash4/*.flash), G = the Game Boy ROM folder; variants are written
# to <out dir>/var (scratch: never commit ROMs or saves). Each run leaves <out dir>/<name>/coverage.{json,profraw}.
set -e
OUT=$1; shift
S=${S:-/private/tmp/claude-501/-Users-michael-Projects-Pokemon/1fd4be59-42a7-4591-b1e0-223ce98f21f6/scratchpad}
G=${G:-/Users/michael/Projects/Pokemon/pokestadium-port/gb}
B=${B:-build-evidence-all2/pokestadium}
R=evidence/whole_game/routes
E=$R/error_paths
V=$OUT/var
mkdir -p $V

# Game Boy cartridges: refused (Japanese header), no working battery RAM, wrong global checksum
python3 -I $E/gb_rom_variant.py $G/yellow_de_us_title.gb $V/yellow_jp.gb country=0
python3 -I $E/gb_rom_variant.py $G/yellow_de_us_title.gb $V/yellow_noram.gb ram=0
python3 -I $E/gb_rom_variant.py $G/yellow_de.gb $V/yellow_badsum.gb flip=0xFFFF0
cp $S/gbv_sl/party6.sav $V/port4.sav
# Stadium flash: the options record's backup copy broken (its POKE footer), the primary valid
python3 -I -c "
import sys; d = bytearray(open('replays/default.flash', 'rb').read()); d[0x19EA0 + 0x28 - 3] ^= 0xFF
open(sys.argv[1], 'wb').write(d)" $V/options_bak_bad.flash

# The port's Transfer Pak keeps a 'present' flag per controller (src/port/transfer_pak.cpp, gCarts[ch].present, a
# local symbol in __MergedGlobals): controller 1 at osMemSize+0x2a4, controller 2 at +0x30c (Cart is 0x68 bytes;
# offsets for build-evidence-all2, from the disassembly of osPfsIsPlug). Clearing it is a pulled cartridge: every
# osGbpak* call then fails with PFS_ERR_NOPACK.
CART1="osMemSize+0x2a4:1"
CART2="osMemSize+0x30c:1"
# GbSave_PollCartRemoval's state (D_8007364C = D_80073230 + 0x41C): 2 = "cartridge seen off, wait for it on"
POLL="D_80073230+0x41C:1"
# Battery RAM test failing while the cartridge answers: the cart-valid mask D_800A82A5 cleared (GbPak_MbcWrite returns
# 1) and the probe latch D_800A82A0[0] cleared (GbTower_ProbePak probes again)
ramfail() { echo "$1:D_800A82A0:1=0;$1:D_800A82A5:1=0"; }

LAB="$R/lab_pc2/boxes_ck_moves_save.rec --gb $G/yellow_de.gb:$S/gbv_sl/boxes_full.sav --flash $S/flash4/labfull.flash"
PIKA="$R/pikachu_reward_3moves.rec --gb $G/yellow_de.gb:$S/gbv_sl/base_in.sav --call BattlePrep_RunRulesScreen#2@exit:Fragment_LoadAndCall:i:i34,i3891504,i3900720,i0,i0"
GBT="replays/gbtower_quit.rec --flash replays/gbtower_quit.flash"
T=Team_LoadOpponentPreset#1

run() {
    n=$1; fr=$2; shift 2
    if [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $n "; then return; fi
    rm -rf $OUT/$n
    nice -n 15 python3 -I tools/evidence/run.py $OUT/$n --bin $B --seq-max 2 --dump-every 600 --frames $fr --rec "$@" \
        > $OUT/$n.log 2>&1
    rm -f $OUT/$n/{seq.txt,port_last.rec,port.trace}
    echo "$n: $(tail -1 $OUT/$n.log)"
}
ONLY="$*"

# saved cup run: delete prompt (No, then Yes)
run continue_delete 3800 $E/continue_delete.rec --gb $G/yellow_de.gb:$S/gbv_sl/party6.sav --flash $S/flash4/continue.flash
# Stadium save: backup copy broken -> primary copied over it
run flash_options_backup_bad 2500 $R/idle.rec --flash $V/options_bak_bad.flash
# cartridges the game refuses or cannot use (no forcing)
PORT_GB_REGION_COMPAT=0 run cart_region_refused 4600 replays/tour_labgb3.rec --gb $G/yellow_de.gb:$G/yellow_de.sav
run cart_japanese 4600 replays/tour_labgb3.rec --gb $V/yellow_jp.gb:$G/yellow_de.sav
run cart_no_ram 4600 replays/tour_labgb3.rec --gb $V/yellow_noram.gb:$G/yellow_de.sav
PORT_GB_ROM4=$G/yellow_de_us_title.gb PORT_GB_SAVE4=$V/port4.sav run cart_port4 4600 replays/tour_labgb3.rec
run gbtower_bad_rom_sum 8200 $GBT --gb $V/yellow_badsum.gb:$G/gbtower_quit.sav
# cartridge pulled (the port's present flag cleared)
run poll_removed 3000 $R/idle.rec --gb $G/yellow_de.gb:$S/gbv_sl/party6.sav --set "GbSave_ScanAllPorts#1@exit:$CART1=0"
run cart_port2_removed 3000 $R/idle.rec --gb2 $G/yellow_de.gb:$S/gbv_sl/party6.sav --set "GbSave_ScanAllPorts#1@exit:$CART2=0"
run lab_probe_removed 9200 $LAB --set "GbSave_ValidateBeforeWrite#1:$CART1=0"
run lab_cart_removed_after_read 5000 $LAB --set "GbSave_ReadMainData#2@exit:$CART1=0"
run gbtower_cart_pulled 8200 $GBT --gb $G/yellow_de.gb:$G/gbtower_quit.sav --set "GbTower_PollPakConnection#3300:$CART1=0"
# save to the cartridge fails once (contact lost during the checksum write), the reinsert prompt, the retry succeeds
GLITCH="GbSave_InvalidateMainDataChecksum#1:$CART1=0;GbSave_InvalidateMainDataChecksum#1@exit:$CART1=1;GbSave_PollCartRemoval#2:$POLL=2"
run lab_save_retry 9800 $LAB --set "$GLITCH"
run pikachu_save_retry 37000 $PIKA --set "$GLITCH"
# battery RAM test fails (forced) in each caller that probes the cartridge; each ends on the fatal error screen
run ramfail_openmain_boot 2000 $R/idle.rec --gb $G/yellow_de.gb:$S/gbv_sl/party6.sav --set "$(ramfail GbSave_OpenMainData#1)"
run ramfail_loadport 5000 $LAB --set "$(ramfail GbSave_LoadPort#1)"
run ramfail_openbox 5000 $LAB --set "$(ramfail GbSave_OpenBoxData#1)"
run ramfail_readmain_lab 5000 $LAB --set "GbSave_ReadMainData#2:D_800A82A5:1=0"
run ramfail_save_validate 9200 $LAB --set "$(ramfail GbSave_ValidateBeforeWrite#1)"
run ramfail_save_rebuild 9200 $LAB --set "$(ramfail GbSave_RebuildAndWriteSave#1)"
run ramfail_save_writebox 9200 $LAB --set "$(ramfail GbSave_WriteBoxBanks#1)"
run ramfail_save_commitmain 9200 $LAB --set "$(ramfail GbSave_CommitMainData#1)"
# the cartridge's save changed since it was read (forced: the stored checksum byte, GbSavePort.unk_04)
run cart_swapped_before_save 9200 $LAB --set "GbSave_ValidateBeforeWrite#1:D_800AC910+0x4:1=0x5A"
# mode 2 (Nintendo Cup '98, not selectable in the US game) in round 2, second battle (forced; restored on return)
run mode2_round2_opponent 4200 replays/cup_giveup.rec --flash replays/default.flash --set "$T:gSessionContext:1=2;$T:gSessionContext+0x3:1=2;$T:gSessionContext+0x1289:1=0xFF;$T:gSessionContext+0x1296:1=1;$T@exit:gSessionContext:1=3;$T@exit:gSessionContext+0x3:1=1;$T@exit:gSessionContext+0x1296:1=0"
# debug profiler and its text renderer (no code in the game sets the flags), low and high resolution
run debug_profiler_title 2400 $R/idle.rec --set "StageLoader_SetupFrame#1:gShowCPUProfiler:4=1;StageLoader_SetupFrame#1:gShowMEMProfiler:4=1"
run debug_profiler_lab 5200 $LAB --set "StageLoader_SetupFrame#1:gShowCPUProfiler:4=2;StageLoader_SetupFrame#1:gShowMEMProfiler:4=1"

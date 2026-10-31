#!/bin/bash
# The debug and diagnostic routes (README.md): every run of this directory, each into <out>/<name>.
# Usage: evidence/whole_game/routes/debug_crash/routes.sh <out dir> [name...]   (from the repository root)
# Needs build-evidence-all2 (the instrumented whole-game build). Each run takes a few seconds; the "parked" ones
# (whose last call waits forever for a 64DD reply) are ended after 20 s.
set -u
OUT=$(cd "${1:?out dir}" && pwd); shift
R=$(cd "$(dirname "$0")" && pwd)
BIN=build-evidence-all2/pokestadium
T="State_TitleScreen#1@exit"   # every call / state entry is made when the title screen returns (A at frame 1720)

# run <name> <rec> [run.py args]  (FR: frames, default 2100; DE: dump every DE frames, default 30)
run() {
    local n=$1 rec=$2; shift 2
    python3 -I tools/evidence/run.py "$OUT/$n" --bin $BIN --frames "${FR:-2100}" --seq-max 4 --dump-every "${DE:-30}" \
        --rec "$R/$rec" --natural "$@" > "$OUT/$n.log" 2>&1
}
# parked <name> <rec> [run.py args]: the run's last call parks the game thread on a 64DD reply that never comes. The
# stop frame is checked on the game thread, so the run would never end, while the other threads tick on at full
# speed (a million frames a minute): dump only every 20000 frames (each dump rewrites the 25-65 MB profile; at the
# default 30 a parked run filled the disk) and end the port after 20 s (run.py then writes coverage.json).
parked() {
    DE=20000 run "$@" &
    local py=$!
    sleep 20
    local p
    for p in $(pgrep -P $py); do pkill -P "$p" -x pokestadium; done
    wait $py
}
want() { [ -z "$WANT" ] || [[ " $WANT " == *" $1 "* ]]; }
WANT="$*"
set --

# --- debug menus and states -------------------------------------------------------------------------------------
# STATE_STUBBED_DEBUG (0x80) through the game's loop; State_StubbedDebug calls the debug_menu_exit fragment, and the
# nine debug_menu_slot fragments (referenced by nothing) are called at its entry: each queues debug_menu_exit again
# and returns 0x80.
S=""
for f in DebugMenu_ForwardSlot68:i0,i0,i0 DebugMenu_ForwardSlot69:i0,i0,i0 DebugMenu_ForwardSlot70:i0,i0 \
         DebugMenu_ForwardSlot71:i0,i0 DebugMenu_ForwardSlot72:i0,i0,i0 DebugMenu_ForwardSlot73:i0,i0,i0 \
         DebugMenu_ForwardSlot76:i0,i0,i0 func_8FB00020:i0,i0 func_8FC00020:i0,i0; do
    S="$S;DebugMenu_Exit#1:${f%%:*}:i:${f#*:}"
done
want slots && run slots title.rec --set "$T:gCurrentGameState:4=128" --call "${S#;}"
# STATE_N64DD_BOOT_UNUSED (3): State_N64ddBootUnused -> LeoBootGame, which faults on the host (no disk boot code).
want dd_boot && run dd_boot title.rec --set "$T:gCurrentGameState:4=3"

# --- CPU / memory profiler --------------------------------------------------------------------------------------
# Profiler_SetDisplayModes (referenced by nothing) turns the profiler on; StageLoader_SetupFrame then draws it every
# frame. (3,2) is rejected by its range checks, (1,1) is bar mode 0 with the memory line, (2,0) bar mode 1.
want prof_m0 && run prof_m0 title.rec --shots 2300 --call "$T:Profiler_SetDisplayModes:v:i3,i2;$T:Profiler_SetDisplayModes:v:i1,i1;$T:clear_profiler_data:v:;$T:set_get_time_diff:i:i1;$T:set_get_time_diff:i:i2;$T:set_get_time_diff:i:i0"
want prof_m1 && run prof_m1 title.rec --shots 2300 --call "$T:Profiler_SetDisplayModes:v:i2,i0"

# --- gallery debugger -------------------------------------------------------------------------------------------
# GalleryDebugger_Main (fragment 48, referenced by nothing) as a state of its own; gldb.rec walks its menu: Up at the
# top, A on Load Controller Pak, Down, A on Save Controller Pak, Down twice (clamped at Quit), A on Quit.
want gldb && FR=2400 run gldb gldb.rec --shots 1970 --call "$T:GalleryDebugger_Main:i:i0,i0"

# --- minigame / gallery camera debug helpers --------------------------------------------------------------------
# The ranking debug test (Minigame_RankingDebugTest, the minigame test scene) with its own debug toggle: A/B/C-Left
# spawn the three ranking actors, Z turns miniDebugMode on (showDebuggCameraInfo), C buttons while it is on, Z off,
# START, C-Down leaves (miniUnkFadeOutLoop). The dead helpers run inside its HUD draw, where the font is loaded:
# showDebuggJoystickInfo (Ekans/Sandshrew draw it with miniDebugMode set), minigameDebuggVectorEditor and
# miniApplyCameraDebugOffset (referenced by nothing), GalleryCamera_DebugAdjustZoomOffset (referenced by nothing,
# reads controller 2's stick: connected, pushed up then down in rank2.rec).
want rank2 && FR=2600 run rank2 rank2.rec --set "Cont_InitControllers:gControllerBits:1|=3" \
    --call "$T:Minigame_RankingDebugTest:i:i0,i0;miniUnkDrawHUB:showDebuggJoystickInfo:v:;miniUnkDrawHUB:minigameDebuggVectorEditor:v:;miniUnkDrawHUB:miniApplyCameraDebugOffset:v:;miniUnkDrawHUB:GalleryCamera_DebugAdjustZoomOffset:v:"

# --- crash screen -----------------------------------------------------------------------------------------------
# The port never enters the game's crash screen (host faults go to the port's crash report; __osGetActiveQueue is
# NULL), and its frame buffer is an N64 KSEG1 address (osMemSize | 0xA0000000). crashfb.dylib maps a host buffer at
# 0x70A0000000, whose low word survives crash_screen_set_draw_info's "| 0xA0000000", and saves it as a PPM.
# Build: clang -O1 -dynamiclib -o crashfb.dylib crashfb.c. (run.py lowers its own priority; nothing SIP-protected
# like /usr/bin/time or nice may sit in front of it, or DYLD_INSERT_LIBRARIES is dropped.)
FB=483720691712    # 0x70A0000000
# scratch buffer: a faulted OSThread at p0 (priority 10, state 1, flags OS_FLAG_FAULT, id 5, registers
# 0x800000xx), the text "CRASH SCREEN TEST" at p128, float words inf / 1.0 / a denormal at p192..p200
CBUF=$(python3 -c "
import struct
b=bytearray(256)
struct.pack_into('<i',b,0x08,10)
struct.pack_into('<HHi',b,0x20,1,2,5)
for i in range(0x38,0x38+0xC8,8): struct.pack_into('<Q',b,i,0x80000000+i)
s=b'CRASH SCREEN TEST'; b[128:128+len(s)]=s
struct.pack_into('<III',b,192,0x7F800000,0x3F800000,0x00000001)
print(b.hex())")
DYLIB="$OUT/crashfb.dylib"
[ -f "$DYLIB" ] || clang -O1 -dynamiclib -o "$DYLIB" "$R/crashfb.c"
# crash_screen_draw on the fake thread: the whole screen (crash_screen.png); its last line reads the instruction at
# the N64 PC, a 32-bit address in the host's __PAGEZERO, so the run ends there with SIGSEGV (dumped on the way out).
want crash_draw && CRASHFB_K=0x70 DYLD_INSERT_LIBRARIES="$DYLIB" CRASHFB_OUT="$OUT/crash_draw.ppm" \
    run crash_draw title.rec --call-buf "$CBUF" --call "$T:crash_screen_set_draw_info:v:i$FB,i320,i240;$T:crash_screen_printf_with_bg:v:i40,i8,p128;$T:crash_screen_print_fpr:v:i200,i8,i1,p192;$T:crash_screen_print_fpr:v:i260,i8,i3,p196;$T:crash_screen_print_fpr:v:i200,i0,i5,p200;$T:crash_screen_print_fpcsr:v:i131072;$T:crash_screen_draw:v:p0"
# the 640-wide glyph path, then crash_screen_get_faulted_thread: NULL active queue on the port, SIGSEGV
want crash_thread && CRASHFB_K=0x70 DYLD_INSERT_LIBRARIES="$DYLIB" CRASHFB_OUT="$OUT/crash_thread.ppm" CRASHFB_W=640 \
    run crash_thread title.rec --call-buf "$CBUF" --call "$T:crash_screen_set_draw_info:v:i$FB,i640,i480;$T:crash_screen_printf_with_bg:v:i40,i20,p128;$T:crash_screen_sleep:v:i10;$T:crash_screen_get_faulted_thread:i:"
# the unlock combo: the loop busy-waits on osGetTime without blocking, so in the port's lockstep clock no frame
# passes and recorded presses never arrive; each of the ten buttons is forced into gPlayer1Controller->buttonPressed
# (+8) as Cont_ReadInputs returns on the loop's iterations (entries 845-854: 844 before the title screen returns)
S=""; i=845
for v in 2048 1024 512 256 8 4 2 1 16384 32768; do S="$S;Cont_ReadInputs#$i@exit:gPlayer1Controller*+8:2=$v"; i=$((i + 1)); done
want crash_combo && run crash_combo title.rec --set "${S#;}" --call "$T:crash_screen_wait_for_button_combo:v:;$T:crash_screen_sleep:v:i100"

# --- audio ------------------------------------------------------------------------------------------------------
# Audio_DispatchSoundBankStopCommand (referenced by nothing) for banks 2-0xB, Audio_SeedRandom (referenced by
# nothing), Audio_PlayCategory12SoundCommand with the gallery album's delete sound. title_audio.rec carries
# "# game audio": the sound engine runs (otherwise every audio command returns at PORT_AUDIO_OFF).
S=""
for b in 2 3 4 5 6 7 8 9 10 11; do S="$S;$T:Audio_DispatchSoundBankStopCommand:v:i$((b << 16)),i0,i0"; done
want audio && run audio title_audio.rec --call "${S#;};$T:Audio_SeedRandom:v:i12345,i6789;$T:Audio_PlayCategory12SoundCommand:i:i18874380,i0,i0"

# --- storage: SRAM, ROM write, 64DD (rsp.c, jpeg_stream.c, dma.c) -----------------------------------------------
# The request structs are the zeroed scratch buffer (vaddr NULL: the port's PI completes the transfer without
# copying). SRAM/ROM-write requests complete; every 64DD request parks the calling thread (no drive: libleo
# returns at once and the reply never comes), so each 64DD call is the last of its run.
want sram && parked sram title.rec --call "$T:Storage_GetSramHandle:i:;$T:Storage_ReadSram:i:p0;$T:Storage_WriteSram:i:p0;$T:Storage_WriteRom:i:p0;$T:Storage_ResetDisk:v:;$T:Storage_ReadDisk:i:p0"
for c in leo_wd:Storage_WriteDisk leo_id:Storage_ReadDiskId leo_sk:Storage_SeekDisk leo_mo:Storage_SetDiskMotor \
         leo_rr:Storage_ReadRtc leo_wr:Storage_WriteRtc; do
    want "${c%%:*}" && parked "${c%%:*}" title.rec --call "$T:${c#*:}:i:p0"
done
# jpeg_stream.c: a BinArchive with one file at p0, an empty one at p128; Dma_ReadChunks / Dma_CopyChunks go through
# the storage thread's SRAM read and ROM write; Asset_CopyUncompressed with size 0 (its argument must be a pool block
# or NULL); then the 64DD loaders, each parked on its disk read.
JBUF=$(python3 -c "b=bytearray(256); b[0x0C]=1; print(b.hex())")
want jpeg && parked jpeg title.rec --call-buf "$JBUF" --call "$T:Storage_SetLbaRange:i:p64,i0,i4;$T:Storage_SetByteRange:i:p64,i0,i100,i5000;$T:Dma_ReadChunks:v:p192,i0,i16,i0;$T:Dma_CopyChunks:v:p192,i0,i16;$T:Asset_CopyUncompressed:i:i0,i0,i0,i0;$T:BinArchive_Free:v:i0;$T:BinArchive_GetCachedFile:i:p0,i0;$T:BinArchive_GetCachedFile:i:p128,i0;$T:Storage_PrefetchLbaRange:v:i0,i1,i0"
want lba_seg && parked lba_seg title.rec --call "$T:Asset_LoadToSegmentByLbaRange:i:i0,i0,i1,i0"
want lba_frag && parked lba_frag title.rec --call "$T:Fragment_LoadByLbaRange:i:i0,i0,i1"
want lba_arch && parked lba_arch title.rec --call "$T:BinArchive_LoadByLbaRange:i:i0,i1,i0,i0"
want lba_file && parked lba_file title.rec --call-buf "$JBUF" --call "$T:BinArchive_LoadFileBuffered:i:p0,p128"
exit 0

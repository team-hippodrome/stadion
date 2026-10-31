# Debug and diagnostic code: crash screen, profiler, debug menus, gallery debugger, storage leftovers

`routes.sh <out dir> [name...]` (from the repository root) makes every run below with build-evidence-all2. Each
run boots, presses A on the title screen at frame 1720 (`title.rec`) and makes its calls or state entry when
`State_TitleScreen` returns (`--call "State_TitleScreen#1@exit:..."`, `--set "...:gCurrentGameState:4=<state>"`).
None of this code runs in the US retail game, so every route forces something: a state entry, a direct call of a
function nothing calls, or a memory value. The table says which. The script has the exact `--call`/`--set` strings
and the scratch buffers.

Whole game with these runs: **6859 of 7490 functions (91.6%)**, lines 82.7%, branches 73.7% (before: 6763, 90.3%,
81.9%, 73.1%; batch4-batch14).

| area | before | after | left |
|---|---|---|---|
| crash_screen.c | 3 / 14 | 14 / 14 | |
| profiler.c | 5 / 14 | 14 / 14 | |
| gallery_debugger | 0 / 12 | 12 / 12 | |
| debug_menu_slot68 / 71 / 76 (the other slots were already counted) | 0 / 3 | 3 / 3 | |
| rsp.c | 7 / 20 | 20 / 20 | |
| jpeg_stream.c | 26 / 37 | 37 / 37 | |
| audio_commands_category2.c | 18 / 28 | 28 / 28 | |
| stage_loader.c | 31 / 39 | 37 / 39 | |
| util.c | 7 / 9 | 9 / 9 | |
| ranking_debug_test | 7 / 8 | 8 / 8 | |
| minigame_actor_framework | 64 / 78 | 67 / 78 | not debug code |
| game_state.c | 34 / 38 | 35 / 38 | |
| dma.c | 3 / 12 | 6 / 12 | |
| memmap.c, memory_main.c | 25 / 28 | 27 / 28 | |
| libleo (readwrite, seek, readdiskid, readrtc, setrtc, spdlmotor, bytetolba, lbatobyte, bootdisk) | 0 / 11 | 9 / 11 | |

Still unreached under these names: `LeoTestUnitReady` and `leoTest_unit_rdy` (the 64DD drive's "test unit
ready" command, not test code: only the 64DD start-up path reaches it, and it fails without a drive).

## Routes

| run | what it reaches | forced |
|---|---|---|
| `slots` | `STATE_STUBBED_DEBUG` (0x80) through the game loop: `State_StubbedDebug`, the `debug_menu_exit` fragment, and the nine `debug_menu_slot` fragments' entries (each queues `debug_menu_exit` and returns 0x80) | state entry; the slot fragments are referenced by nothing, so they are called directly at `DebugMenu_Exit`'s entry |
| `dd_boot` | `STATE_N64DD_BOOT_UNUSED` (3): `State_N64ddBootUnused`, `LeoBootGame` | state entry. `LeoBootGame` then faults on the host, because there is no disk boot code to jump to (on the N64 without a drive it would fail too; the state is unused in the US game) |
| `prof_m0`, `prof_m1` | the CPU/RCP profiler bars (both modes), its CPU/RCP line, the memory line (`Util_DrawMemProfiler`), `clear_profiler_data`, `set_get_time_diff` | `Profiler_SetDisplayModes(1,1)` / `(2,0)` called directly (nothing calls it: `gShowCPUProfiler` stays 0 in retail). On the port the bars are near zero: the profiler's `osGetTime` stamps are the lockstep clock's. `profiler.jpg` |
| `gldb` | the gallery debugger (fragment 48): menu, its Controller Pak load/save (empty stubs in this build), the deck entries' load and save, the fade states; `gldb.rec` walks the menu (Up at the top, A on Load, Down, A on Save, Down twice to the Quit clamp, A on Quit) | `GalleryDebugger_Main` called directly (referenced by nothing). `gallery_debugger.jpg` |
| `rank2` | the ranking debug test (`Minigame_RankingDebugTest`) with its own Z toggle of `miniDebugMode` (`showDebuggCameraInfo`), C-Down to leave (`miniUnkFadeOutLoop`); and inside its HUD draw, where the font is loaded: `showDebuggJoystickInfo`, `minigameDebuggVectorEditor` (C buttons in `rank2.rec`), `miniApplyCameraDebugOffset`, `GalleryCamera_DebugAdjustZoomOffset` (controller 2 connected, its stick pushed up and down) | the test scene called directly (the minigame dispatcher's case 14 loads it, an id no menu picks) and the four helpers too (the helpers are referenced by nothing, except `showDebuggJoystickInfo`, which Ekans and Sandshrew draw when `miniDebugMode` is set) |
| `crash_draw` | `crash_screen_draw` on a faulted thread: the whole register screen, the FPCSR cause, valid and invalid float registers, `crash_screen_printf_with_bg`. `crash_screen.png` is the frame buffer the game drew | `crashfb.dylib` (see below), a fake `OSThread` in the scratch buffer. The last line reads the instruction at the N64 PC: on the host that 32-bit address is in `__PAGEZERO`, so the run ends with SIGSEGV there, dumped by the evidence runtime's crash hook |
| `crash_thread` | the 640-wide glyph path, `crash_screen_sleep`, `crash_screen_get_faulted_thread` | as above. `__osGetActiveQueue()` is NULL on the port, so the walk faults at once (SIGSEGV, dumped) |
| `crash_combo` | `crash_screen_wait_for_button_combo` through the whole unlock sequence (Up, Down, Left, Right, C-Up, C-Down, C-Left, C-Right, B, A) | the ten presses are forced into `gPlayer1Controller->buttonPressed` (+8) as `Cont_ReadInputs` returns on the loop's iterations (entries 845-854). The loop busy-waits on `osGetTime` without ever blocking, so in the port's lockstep clock no frame passes and recorded presses never arrive (`PORT_REALTIME=1` does not replay a recording) |
| `audio` | `Audio_DispatchSoundBankStopCommand` for banks 2-0xB (all nine category stop commands), `Audio_SeedRandom`, `Audio_PlayCategory12SoundCommand` with the gallery album's delete sound | direct calls (the dispatcher and the seed are referenced by nothing). `title_audio.rec` carries `# game audio`: without it the replay runs with the sound engine off and every audio command returns at `PORT_AUDIO_OFF` |
| `sram` | `Storage_GetSramHandle`, `Storage_ReadSram`, `Storage_WriteSram`, `Storage_WriteRom`, `Storage_ResetDisk`, `Storage_ReadDisk`, `Storage_WaitForIoMessage` | direct calls with a zeroed request (NULL buffer: the port's PI completes it without copying); the US game makes no SRAM or ROM-write requests (it saves to flash) |
| `leo_wd`, `leo_id`, `leo_sk`, `leo_mo`, `leo_rr`, `leo_wr` | `Storage_WriteDisk`, `ReadDiskId`, `SeekDisk`, `SetDiskMotor`, `ReadRtc`, `WriteRtc` with their libleo entry points | direct calls; each parks the game thread (see below) |
| `jpeg` | `Dma_ReadChunks` and `Dma_CopyChunks` (through the storage thread's SRAM read and ROM write), `Asset_CopyUncompressed`, `Storage_SetLbaRange`, `Storage_SetByteRange`, `BinArchive_Free`, `BinArchive_GetCachedFile` (both sides of its count check), `Storage_PrefetchLbaRange` -> `Storage_LoadAssetByLbaRange` | direct calls (all referenced by nothing, or only by the 64DD loaders) |
| `lba_seg`, `lba_frag`, `lba_arch`, `lba_file` | the 64DD loaders `Asset_LoadToSegmentByLbaRange`, `Fragment_LoadByLbaRange`, `BinArchive_LoadByLbaRange`, `BinArchive_LoadFileBuffered` | direct calls; parked on the disk read |

## What the N64 shows vs what the port can

- Crash screen: on the N64 a CPU fault wakes `crash_screen_thread_entry` (`OS_EVENT_FAULT`), which finds the faulted
  thread, waits for the unlock combo, then draws the registers into the frame buffer at the top of RDRAM. The port
  never gets there: host signals go to the port's own crash report, no fault event is ever sent, the active
  thread queue is NULL, and the frame buffer address is an N64 KSEG1 address. The drawing code itself runs
  unchanged once it has a buffer: `crashfb.c` (`DYLD_INSERT_LIBRARIES`) allocates one at 0x70A0000000 (the first
  free `0x<k>A0000000`: its low word survives `crash_screen_set_draw_info`'s `| 0xA0000000`) and saves it as a
  PPM whenever it changes. Build it with `clang -O1 -dynamiclib -o crashfb.dylib crashfb.c`. Nothing
  SIP-protected may start run.py (`/usr/bin/time`, `nice`): macOS drops the `DYLD_` variable. run.py lowers its
  own priority.
- Profiler: drawn as on the N64, but its timings are the port's virtual clock.
- 64DD: there is no drive. libleo refuses every command, and the request's reply never comes.

## Parked runs (64DD calls): read before changing them

A 64DD call parks the calling thread for good. The stop frame is checked on the game thread, so such a run never
ends, while the other threads tick on at full speed (about a million frames a minute). With a dump every 30
frames, each rewriting the 25-65 MB profile, four parked runs grew their coverage.profraw to 38 GB each in ten
minutes and filled the disk. `routes.sh` runs them with `--dump-every 20000` and ends the port after 20 s. run.py
then writes coverage.json from the last dump.

## Dead code (no reference in the source, or never enabled in the US build)

No reference anywhere: the nine `debug_menu_slot` fragments, `GalleryDebugger_Main` (fragment 48, nothing loads
it), `Profiler_SetDisplayModes` (so `gShowCPUProfiler`/`gShowMEMProfiler` stay 0 and the whole profiler is dead),
`set_get_time_diff`, `clear_profiler_data`, `crash_screen_printf_with_bg`, `Audio_DispatchSoundBankStopCommand`
(and with it the nine category stops), `Audio_SeedRandom`, `miniApplyCameraDebugOffset`,
`minigameDebuggVectorEditor`, `GalleryCamera_DebugAdjustZoomOffset`, `Dma_ReadChunks`, `Dma_CopyChunks`,
`BinArchive_Free`, `BinArchive_GetCachedFile`, `Storage_PrefetchLbaRange`, `Asset_LoadToSegmentByLbaRange`,
`Fragment_LoadByLbaRange`, `BinArchive_LoadByLbaRange`.

Never enabled in the US retail build:
- `STATE_STUBBED_DEBUG` and `STATE_N64DD_BOOT_UNUSED`: no state change leads to them.
- The SRAM and ROM-write requests: the game saves to flash.
- The 64DD request types and the LBA loaders: the archive flag 0x80 is set only by the dead `BinArchive_LoadByLbaRange`.
- The crash screen: it needs a CPU fault.
- `miniDebugMode`: only the ranking debug test toggles it. The minigame dispatcher loads that test only for
  minigame id 14, which no menu selects.

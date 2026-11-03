# Error and edge paths: save data, Game Boy cartridges, the Transfer Pak

`runs.sh <out dir> [names]` runs every route below (variants generated into `<out dir>/var`; set `S` to the evidence
scratchpad with `gbv_sl/` and `flash4/`, `G` to the Game Boy ROM folder). Recordings are the stored ones
(`replays/`, `../lab_pc2/boxes_ck_moves_save.rec`, `../pikachu_reward_3moves.rec`, `../idle.rec`) plus
`continue_delete.rec`; ROM variants come from `gb_rom_variant.py` (header edits; never commit ROMs or saves).

Result (whole game, against the union of batches 4-14 and every sibling session's runs at the time): **functions
6882 -> 6902 of 7490, lines 125735 -> 126121, branches 47980 -> 48178**. Of the 46 gates the frontier classes as
"session / save / mode", "result of a call" or "hardware", 15 are open now; 32 are left (one new behind a newly
reached caller, `Storage_ResetDisk -> LeoReset`): 64DD, the Snap Station, the reset button, RSP and display timing,
audio streaming, battle-animation, Game Boy music and JPEG branches the shipped data never takes, and dead calls.

## What forcing means here

Three kinds of runs:
- **No forcing**: the condition is presented the way the hardware presents it (a cartridge the game refuses, a
  cartridge without working battery RAM, a ROM whose global checksum is wrong, a broken flash record).
- **Pulled cartridge**: the port's Transfer Pak (`src/port/transfer_pak.cpp`) keeps a `present` flag per controller;
  clearing it with `--set` (controller 1: `osMemSize+0x2a4`, controller 2: `+0x30c`, in `build-evidence-all2`) makes
  every `osGbpak*` call fail with `PFS_ERR_NOPACK`, as a pulled cartridge does. Setting it back is a reinsert.
- **Forced game state** where the port has no stand-in for what the hardware reports (named in each row): the
  Transfer Pak's "cartridge off" status bit, a battery RAM that stops answering while the cartridge still answers,
  a cartridge swapped for one with other save data, and two flags no code in the shipped game sets.

## Routes

| route | setup | forced | reaches |
|---|---|---|---|
| `continue_delete` | `continue.flash` (a saved Poké Cup run), `party6` cartridge; `continue_delete.rec`: START, A, A to the "Saved battle data exists" menu, DOWN DOWN A (delete), A (No), A again, DOWN (toggle to Yes), A | none | `TransferPak_ConfirmPrompt` both answers ("Saved battle data will be deleted. Is this OK?") |
| `flash_options_backup_bad` | `default.flash` with the options record's backup copy (0x19EA0) broken | none | `Save_LoadWithRepair` case 1: `Save_WriteRecord` (primary copied over the backup) |
| `cart_region_refused` | `tour_labgb3.rec`, `yellow_de.gb` with `PORT_GB_REGION_COMPAT=0` (the European header as it is) | none | `GbPak_IsPokemonCartridge` title mismatch: "The Game Pak can't be used." at the Game Pak pick (as the US N64 game treats a European cartridge) |
| `cart_japanese` | the same with a US-titled ROM whose country byte is 0 | none | `GbPak_IsPokemonCartridge` country check refused |
| `cart_no_ram` | a ROM whose header declares no battery RAM | none | `GbSave_ScanAllPorts` -> `GbPak_DetectRam` fails -> fatal screen code 2 ("There is a problem with the GB Game Pak's connector") at boot |
| `cart_port4` | cartridge only on controller 4 (`PORT_GB_ROM4`) | none | port 3 probing through `Controller_CheckAccessoryEeprom` |
| `gbtower_bad_rom_sum` | `gbtower_quit.rec`/`.flash`, Yellow with one ROM byte changed (global checksum no longer matches) | none | the GB Tower emulator's ROM checksum check -> exit code 1 -> `State_GbTower` -> `Game_ShutdownAndLoadFragment` |
| `poll_removed` | `idle.rec`, `party6` cartridge | cartridge 1 pulled after the boot scan | `GbTower_PollPakConnection` three failed polls -> fatal screen code 1 ("The Transfer Pak is not set properly"), `GbTower_PowerOffAllPaks`, `Game_ResetStorageDevice` |
| `cart_port2_removed` | the cartridge on controller 2 | cartridge 2 pulled after the boot scan | the same for port 2 (the port diagram's other branch) |
| `lab_probe_removed` | Lab box save (`boxes_ck_moves_save.rec`, `boxes_full` cartridge, `labfull.flash`) | cartridge pulled as the save starts | `GbTower_ProbePak` -> `osGbpakReadId` fails -> fatal |
| `lab_cart_removed_after_read` | Lab entry | cartridge pulled right after the Lab reads the main data | `GbTower_CheckPakRemoved` -> `osGbpakGetStatus` fails -> fatal |
| `gbtower_cart_pulled` | `gbtower_quit.rec` with Yellow running in GB Tower | cartridge pulled at about frame 6270 | the emulator's disconnect path -> exit code 1 -> fatal from `State_GbTower` |
| `lab_save_retry` | Lab box save | contact lost only during `GbSave_InvalidateMainDataChecksum` (pulled at its entry, back at its return), and the poll state set to "seen off" on the second poll (the port cannot report the cartridge-off status bit) | the save fails, `LabUI_ShowSavingStatusWidget`/`SetSavingStatusMessage`, `GbSave_PollCartRemoval` (all four states), `GbPak_IsCartOff`, `GbPak_IsCartOn`, `GbTower_ReinitPak`, `GbTower_VerifyPakStillInserted`, `Ui_SendNullMessage`; the retry succeeds, `GbSave_RequirePakPresent` |
| `pikachu_save_retry` | the Pikachu reward's cartridge save (`pikachu_reward_3moves.rec` with its `Fragment_LoadAndCall`) | as `lab_save_retry` | the same retry loop from `PikachuReward_SaveToCartridge` |
| `ramfail_openmain_boot`, `ramfail_loadport`, `ramfail_openbox`, `ramfail_save_validate`, `ramfail_save_rebuild`, `ramfail_save_writebox`, `ramfail_save_commitmain` | boot, Lab entry, Lab box save | battery RAM test fails while the cartridge answers: `D_800A82A5` (cart-valid mask) and `D_800A82A0[0]` (probe latch) cleared at the named function's entry | `GbTower_ProbePak` returns 0 in that caller -> fatal code 2 from `GbSave_OpenMainData`, `GbSave_LoadPort`, `GbSave_OpenBoxData`, `GbSave_ValidateBeforeWrite`, `GbSave_RebuildAndWriteSave`, `GbSave_WriteBoxBanks`, `GbSave_CommitMainData` |
| `ramfail_readmain_lab` | Lab entry | `D_800A82A5` cleared as the Lab reads the main data (the probe passes, the read fails) | `GbSave_EnsureMainDataLoaded` -> fatal |
| `cart_swapped_before_save` | Lab box save | the stored checksum of the loaded save (`GbSavePort.unk_04`) changed: the cartridge's data no longer matches what was read | `GbSave_CheckMainDataUnchanged` fails -> fatal code 1 from `GbSave_ValidateBeforeWrite` |
| `mode2_round2_opponent` | `cup_giveup.rec` | at `Team_LoadOpponentPreset` entry: mode 2, round 2, `inRound2`, opponent history slot 0xFF; restored on return | `Team_PickRandomOpponentIndex`, `Team_IsOpponentIndexUsed` (mode 2 is the Nintendo Cup '98 entry, not selectable in the US game: dead there) |
| `debug_profiler_title`, `debug_profiler_lab` | title screen (low resolution), Lab (high resolution) | `gShowCPUProfiler` 1 / 2, `gShowMEMProfiler` 1 (only `Profiler_SetDisplayModes` sets them, and nothing calls it) | `HAL_Printf`, `TextRenderer_DrawString`, `TextRenderer_DrawGlyphSmall`/`Large`, the memory profiler line |

Host offsets: `gSessionContext` `inRound2` 0x1296, `opponentHistory` 0x1288 (`offsetof` with `session.c`'s flags);
`GbSavePort.unk_04` 0x4 (before any pointer, same as the N64).

## Left in these areas, and why

- Dead (nothing calls them): `pokemon_stats.c` `GbSave_CopyPortSnapshot`, `GbSave_GetPortAvailability`,
  `GbSave_Get/SetSecondaryName`, `GbSave_SetPlayerName`, `GbSave_SetTrainerId`, `GbSave_HasProgressFlag1000`,
  `func_80022A88/A98/AA8/ACC`; `save_data.c` `Save_RestoreTypedRecord` (and `Save_RestoreRecord` behind it),
  `Save_CommitAllBankRecords`, `Save_EnsureBank3Loaded`, `Deck_GetSaveEntryNameWrapper`, `func_8002667C`;
  `flash.c` `Flash_EraseChip`, `Flash_Reinitialize`; `controller.c` `Cont_IsEepromPresent`,
  `Cont_AttemptRead/WriteEeprom`; `gb_save.c` `Ui_*Selection*`, `Ui_GetSelectionDirection`,
  `Color_InterpolateFromGray`; `stage_loader.c` `TextRenderer_QueuePersistentString` (and
  `TextRenderer_ClearQueuedStrings`, which only its strings reach); `dma.c` `Storage_QueueRomWrite` and
  `Storage_QueueSramTransfer` (their callers `Dma_CopyChunks`, `Dma_ReadChunks` have none).
- Dead with the shipped calls: `GbPak_SelectRomBank`/`GbPak_RawWrite` (the only `GbPak_ReadRom` calls read the
  header at 0x120, inside bank 0).
- 64DD (no drive): the rest of `dma.c`, the LBA archive branches in `BinArchive_*`, `Storage_ResetDisk -> LeoReset`.
- Hardware the port has no stand-in for: the Snap Station (`controller_ram.c`: `Controller_ReadAccessoryStatus`,
  the busy wait `System_DelayMilliseconds`; the port's `__osContRamRead` is a stub, and the station is only used by the
  print-club fragments); `GbTower_ScanPaks`' "reset status bit missing" fatal (the port always reports it);
  the reset button (`SoftReset_Thread`); RSP task yield (`Sched_TryYieldActiveTask`).
- Not these areas: `Save_SetOptionsField1E` (Kids Club pause menu's hyper-mode unlock), the battle animation
  slots, Game Boy music commands, display-timing branches (`Display_IsFrameReady() == 0`).
- No Controller Pak file or Rumble Pak code exists in the game (no `osPfs*File`/`osMotor*` calls); the Transfer Pak
  is the only pak it uses.

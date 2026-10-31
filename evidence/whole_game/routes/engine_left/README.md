# Engine and screens left over: routes, forced conditions and proofs

This folder covers 110 functions that `../../dead_classes.md` listed as live, outside audio, particles and the battle
animation code. Results: **27 reached, 58 proven dead, 25 left**.

* The 27 reached functions come from 12 runs: `runs.sh <out> <scratch>` replays all of them on
  `build-evidence-all2`, game at origin/main e3d3f40.
* The 29 geo and slot-model proofs are in `../../geo_commands.md` and `../../data_dead_geo.json`. `geo_scan.py`
  writes the command counts that proof uses.
* The other 29 dead functions have their proof in the table below.

Runs that set something in memory say so in the "forced" column. The other runs use input only.

## Runs

| run | route | forced | reaches |
|---|---|---|---|
| `moltres` | Game Pak Free Battle (`battle_engine/routes/free_battle_gamepak.rec`), party led by Moltres | - | `func_80035E2C`: the type-11 slot-model template (`objectType == 0x92`) |
| `cup_loss_opt0` | `cup_loss_opt0.rec`: Poke Cup round lost, A on the first option of the continue prompt (no route had taken it) | winner flags swapped (`+0x1220`=0, `+0x1270`=1), three continues (`+0x1297`) | `BattlePrep_FadeInBallBurstTransition`, `BattlePrep_FadeInBallBurstObjects`, `BattlePrep_ResetAllDisplayObjects` |
| `cup_draw0`, `cup_draw1` | the same route | both winner flags equal (0 and 1): a draw | `BattlePrepStarBurst_UpdateExitSlide` (state 5), `BattlePrepStarBurst_UpdateFirstClearExit` (state 7), `BattlePrepStarBurst_DrawTrailIcon` (`BattlePrep_InitScene`: equal flags give `unk_0003` 1 and star-burst slot -1) |
| `roster_double` | Game Pak Free Battle, Anything Goes (rule category 0) | `battleSides[1].unk_01` = 2 at `BattlePrepRoster_Init` (`+0x1239`: a two-trainer side, which needs controller 2) | `BattlePrepRoster_LoadDexIdsDouble`, `BattlePrepRoster_PositionIconsDouble`, `BattlePrepRoster_DrawIconVariantB` (the double layouts' icon callback). The loader skips a missing second trainer. |
| `order_detail_cap` | `order_detail_cap.rec`: order select, R+B (slot detail), B, then A and C-down (Blastoise L70 + Mew L100) | rule category 3 (Poke Cup) at `BattleMenu_InitOrderSelectPanels` exit; the detail view needs nothing | `BattleMenu_DrawOrderSelectPanel_MonDetail`, `..._MonDetailInput` (state 4: R held with a pick button); `..._LevelCapNotice`, `..._LevelCapNoticeInput` (state 2: two picks summing ≥ 106 under Poke Cup rules) |
| `rules_species_scroll` | `rules_species_scroll.rec`: the mash route `tour_fb` up to the qualifying-species window, then DOWN held, UP held | - | all four `rental_rules` functions: `RentalRules_ScrollPageDown`, `RentalRules_ScrollPageUp`, `RentalRules_AdvancePageNode`, `RentalRules_QualifyingSpeciesWindowUpdateScrolling` |
| `hm_squirtle` | `hm_squirtle.rec`: Lab PC list, Squirtle (three moves), Use item, HM3 | - | `LabPCList_ClearMovesFrom` (a TM/HM into a free slot; the stored `hm_item.rec` tries Pikachu, for whom HM3 is greyed) |
| `lab_level_group` | `lab_level_group.rec`: LEVEL column, "Group by LEVEL", RIGHT ×3 to "Group", A | - | `LabPCList_FilterByLevelRange`: A on a number spinner is swallowed (`BattleInfoWidget_LevelRangeSelectionInput` returns 1), only child 2 ("Group") passes the confirm |
| `tm24_save` | `tm24_save.rec`: TM24 used (consumed), then exit with Save | - | `LabPCList_WriteDeckCategory` (`LabPCList_ConsumeUsedTmItem` marks the item box dirty) |
| `prenmi` | `title_a.rec` | `port_PostEvent(14)` called when the title screen returns: the reset button's PRE-NMI event, which the port never posts | `Sched_HandlePreNMI`, `Display_GetWorkerStatus`, `Display_IsCurrentFramebuffer`, `SoftReset_ClearLines320` |
| `glc_round2` | `castle_mewtwo/castle_e4_rival_clear.rec` (the castle clear) | `gSessionContext.inRound2` (`+0x1296`) = 1 at `Glc_ClearInitState` | `Glc_DrawTrackingIcon`, `Glc_DrawScisRectangle` (the Round 2 clear shows the tracking icon) |

Host offsets come from `offsetof` with `session.c`'s flags. On the host, `BattleSessionTeams` is 0x50 bytes and
`battleSides` starts at `+0x11E8`. The N64 comment gives 0x28 and `+0x1194`, which do not apply here.

## Proven dead (29, besides `geo_commands.md`)

| function | proof |
|---|---|
| BinArchive_LoadFileFromLbaRange, Fragment_LoadFromLbaRange | Only archive flag 0x80 selects them. No US archive header sets it: the headers at 0x820000, 0x920000, 0x8CC000, 0x898000 (trainers), 0x783760 (text), 0x56E7D0 (stadium models) and 0x3CB130 (backgrounds) all start with flags 0. Only `BinArchive_LoadByLbaRange` sets the flag, and nothing references it (it is the 64DD disk loader). |
| Jpeg_AllocAndDecodeFragment | Only for `PRESJPEG` files in `Fragment_LoadFromRomRange`. That loader gets the model archives 0x920000 (214 PERS-SZP files and one raw file) and 0x8CC000 (63 PERS-SZP files), and the Pokédex area model (PERS-SZP). The ROM's 40 PRESJPEG files are all in other archives, which other loaders read. |
| PokeIcon_WaitBackgroundLoad | Needs `assetIsBusy & 0x20`. The only writers of `assetIsBusy` set 0x80 (`battle_engine_315D50.c`), 0x40 and 0x10 (`model_renderer.c`), or 0 (`session.c`). |
| WidgetTree_HandleHorizontalMenuInput, WidgetTree_SetHorizontalMenuState, WidgetTree_UpdateHorizontalMenuItems, WidgetTree_DrawHorizontalFrame | They are registered only by `WidgetTree_InitHorizontalMenu` and `WidgetTree_InitHorizontalFrame`. Nothing in the decomp calls either one: their only "reach" is the forced direct-call sweep, which registers the callbacks but never runs them. |
| MiniFx_TryEmit, MiniFx_EmitParticles, MiniFx_ApplyEmitterToParticle, MiniFx_FreeEmitter | The emitter list stays empty because `MiniFx_AllocEmitter` is unreferenced (`../minigames_gallery/README.md`). |
| miniTutoScreenShow, miniDrawResultIcons | No minigame sets `miniTutoScreenState` to 5 (`../minigames_gallery/README.md`). |
| KidsClub_DrawWavyText, MiniGameSelect_UpdateCursor | Dead by code (`../minigames_gallery/README.md`): the banner timer is reset to 300 before the test, and the cursor callback runs only for active nodes. |
| BattlePrepBadgeAward_ScatterStep | Badge-award state 4 is set only by `BattlePrepBadgeAward_StartScatter`, which nothing references. |
| BattlePrep_TrophyModelIdle | Trophy state 1 is never set: the writers set 0, 2 (grow) and 3 (shrink). The function body is empty. |
| GbEmu_PixelOpAverage555, GbEmu_PixelOpDitherB | Blit modes 2 and 1. Every `GbEmu_BlitWithPixelOp` call passes 0 or 0xFFFF. |
| GbPak_VerifyProbeBlock | Needs `unk_5DC4 != 0`. The flag is set to 1 only inside the branch it guards: `GbPak_ProbeCartridge` after the check, and `GbPak_VerifyWritableProbeBlock`, which is called only in that branch. |
| GbPak_SelectRomBank, GbPak_RawWrite | The only `GbPak_ReadRom` calls read the header at 0x120, which is in bank 0 (`../menus_trade/README.md`). |
| Storage_QueueDiskIdRead, Storage_QueueDiskReset | Called only from the 64DD boot state machine (`Leo_QueueDiskIdRead` and `Leo_QueueDiskReset`), and the port has no drive. |
| Storage_QueueRtcRead, Mtx_BuildDirectionRotation, func_82600C00 | No C reference. The ROM words equal to their N64 addresses (0x80001304, 0x80031910, 0x82600C00) are unaligned, or (0x10c9d78) inside the compressed `pokemon_models` archive. No such word can be a function pointer. |
| Save_RestoreRecord | Called only by `Save_RestoreTypedRecord`, which nothing calls (`../error_paths/README.md`). |

## Left (25) and why

| function | gate | status |
|---|---|---|
| BattleInfoWidget_SetWidgetState, `_SetLevelRangeState`, `_SetTypePanelState`, `_SetMovePanelState`, `_SetMoveDestinationState`, `LabPCList_SetRowMenuState` | `unk_24` set-state slots of the lab list's info panels | No parent broadcasts a state to these panels on the screens driven so far (`WidgetTree_SetStateDefault`, or a direct `->unk_24`). The sibling slots `_SetSelectionState` and `_SetItemMenuState` do run. Not proven dead. |
| BattleInfoWidget_HandleInput | `unk_20` of the info panel built by `BattleInfoWidget_Initialize` | Its screen drives input through its own proxies. Not proven dead. |
| BattleInfoWidget_SyncParentSelection | `BattleInfoWidget_HandleSelectionProxyInput` case 2 with A | Child 2 of the battle-info selection proxy, confirmed with A. A route is still to be written. |
| WidgetTree_HandleVerticalMenuInput | default `unk_10` of `WidgetTree_InitVerticalMenu` | The one user that keeps the default (`LabItem_InitMenuController`) is driven by `LabItem_BoxPairMenu_HandleInput` directly. The other three override it. Not proven dead. |
| WidgetTree_GetAnimatedPanelInputState | `unk_20` of `WidgetTree_InitAnimatedPanel` | The lab screens own their loops, and no animated panel was seen as a grid item or message panel. Not proven dead. |
| LabItem_BoxScreen_SetMenuState, LabItem_ConfirmDialog_SetSelectedIndex, LabPC_BoxScreen_HandleInput | set-state and input slots | As in `evidence-labpc2.md`: the game sets the children directly. |
| TeamSelection_RegisteredTeam_ApplySelection | going back to a player whose registered team is already chosen | Needs two human players in team selection. Controller 2 can be forced (`Cont_InitControllers:gControllerBits:1\|=3`), but no route exists yet. |
| TeamSelection_TrainerSlots_HandleRulePrompt, TeamSelection_RulePrompt_DrawBinaryLabel | prompts 0x11/0x12: A on a team source that is listed but unusable | No save tried so far gives such a source. |
| Trade_UpdateBoxSlotShrink | three or four Game Paks in the trade machine | `run.py` has two cartridge ports. |
| Trade_DrawSaveSeqMessageBackdrop | a failed Game Pak write | The port's Transfer Pak never fails a write. |
| Save_SetOptionsField1E | the Kids Club hyper-mode unlock at the tournament-end pause menu | A forced try (`Widget_PauseMenuCheckHyperUnlock` conditions) did not reach the menu: the mash route `tour_mg_dynamo` went another way in this binary. |
| func_81203C58 | the GB Tower emulator's state test (`unk_549C`, `unk_559C`, `unk_53E8 == 0x100` in emulated memory) | Not traced. |
| Sched_TryYieldActiveTask, Sched_RequestTaskYield | at retrace, a pending task while another task is still on the RSP (`unk_A14 != NULL`) | The port runs RSP tasks to completion when they are started, so none is active at retrace. Timing the port does not reproduce. |
| SoftReset_ClearLines640, func_8000559C, DisplayWorker_DrainEvents | after PRE-NMI: a 640-wide framebuffer, no display status, the display worker looping with the reset flag set | The forced PRE-NMI run cleared one line in 320 mode. The worker stayed blocked on its queue. |

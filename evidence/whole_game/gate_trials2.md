# Gate trials, second round

The memory-condition trials re-run on the current reached union (batch4 ... batch14, 6763 of 7490 functions), then
the gates of the four classes "other state", "table index", "callback registration" and "battle scene / presentation
state" (246 gates in `frontier.md`) read one by one: what in the game really sets the condition, then a route that
gets there, or a forced value at the right function where no route can. Game at origin/main c946919, binary
`build-evidence-all2`. Routes and the command file: `routes/gate_trials2/` (`runs.sh <out> <run>...` replays every
run below; `offs.py` gives host struct offsets for `--set` with a source file's own flags).

**Result: 6763 → 6785 functions (90.3% → 90.6%); 6775 with the real runs alone.** Lines 81.9% → 82.5%, branches
73.1% → 73.8%. The four classes: 246 → 224 gates.

"Real" below means the game produced the state itself from input and a save it can write (save files from
`tools/save_editor`, Game Boy parties from `tools/evidence/gb_party.py`, all legal); "forced" means a value written
into memory by `run.py --set` (port-only, the game did not produce that state).

## 1. Automatic proposals (`frontier.py --build`, `try_gates.py`)

`frontier.py` now proposes only 11 settings (the other gates are not a plain test of a global). Each replayed on a
run where its caller ran: `runs6/cry_audio` (audio), `runs4/fl_cup_giveup__continue` (cup), `runs4/rp_tour_galenl3`
(gallery), `runs4/rp_tour_fm_noreset` (Gym Leader Castle map), `runs9/sc17` (demo scene 17), `runs4/st_fastlogo`.
New = functions outside the whole union (the earlier round counted only over its baselines). All forced.

| gate | set (at the caller's entry) | replayed on | callee reached | new over the union |
|---|---|---|---|---|
| Audio_CreateFrameTask -> LeoTestUnitReady | `D_80077DAC:4=1` | cry_audio | no | 0 (64DD: the previous round reached it on mv1; hardware) |
| GalleryCamera_HandleDPadInput -> GalleryCamera_OrbitAtPoint | `D_8690B390+0x6:2\|=1` | rp_tour_galenl3 | yes | 2: `GalleryCamera_OrbitAtPoint`, `GalleryCamera_RotateVecByMatrix` |
| Team_LoadOpponentPreset -> Team_PickRandomOpponentIndex | `gSessionContext+0x1296:1=1` | fl_cup_giveup__continue | no | 2: `BattlePrep_AdjustBannerTextureOffset`, `func_81002830` (the gate also needs `mode == 2`: dead, section 3) |
| Anim_PackedBitsReadSigned -> PackedBits_ReadSigned | `sRaw:4=0` | cry_audio | yes | 1 |
| BattlePrep_LoadBannerGeoLayouts -> BattlePrep_AdjustBannerTextureOffset | `gSessionContext+0x1296:1=1` | fl_cup_giveup__continue | yes | 1 (also real: section 2) |
| DisplayWorker_ThreadMain -> DisplayWorker_DrainEvents | `D_800A62E0+0xab8:2=1` | cry_audio | - | run failed (an event count with no events) |
| Gallery_DrawBackgroundSelectMenu -> Gallery_DrawHelpBalloonFrame | `D_8690B380:4=1` | rp_tour_galenl3 | yes | 1 |
| Gallery_DrawTutorialTextBox -> Gallery_DrawHelpBalloonFrame | `D_8690B3C0:4=256` | rp_tour_galenl3 | yes | (same function) |
| Glc_Draw -> Glc_DrawBackgroundCrossFade | `glc_to_e4_transition:2=256` | rp_tour_fm_noreset | yes | 1 |
| Intro_DrawTextPanels -> UI_DrawTextureRgba16 | `gSessionContext+0x1296:1=1` | sc17 | yes | 1 (Round 2's panel) |
| Snap_PrintClubPrintSeals -> Snap_PrintClubDrawSealMargin | `D_8FA00A00*:2=3` | st_fastlogo | no | 0 (`try_gates.py` does not replay a baseline's own `set`, which this state needs; debug state, left to the debug session) |

7 of 11 reached their callee; 8 functions new over the union. The gallery ones are input/presentation flags the
minigames-gallery session can reach with a route.

## 2. Gates opened by hand

| gate (class) | condition | what really sets it | how | run | opened (new over the union) |
|---|---|---|---|---|---|
| BattlePrep_LoadBannerGeoLayouts -> BattlePrep_AdjustBannerTextureOffset (session) and the Round 2 AI | `gSessionContext.inRound2 == 0` false | Round 2: on the main menu C-Right ("STADIUM Switch", once all Round 1 modes are cleared) toggles `inRound2` and stores it in options +0x02 bit 2; `TransferPak_CheckMain` reads it back on every visit | **real**: `stadium_save unlock --round2 --mewtwo` and `set options.sound=4` (the stored choice, what the menu writes), then the stored Poke Cup recordings (`replays/cup_giveup.rec`, `replays/cup_win_badge.rec`) | `r2_cup`, `r2_cupwin` | `BattlePrep_AdjustBannerTextureOffset`, `BattleAI_HasOtherCandidateAtLeastScore` (the Round 2 opponents' AI) |
| func_80018C40 -> Color_AdjustRGBA8 (other state) | `j < n` | the Gym Leader Castle in Round 2 recolours its banners | **forced** `inRound2` with the state (`State_TitleScreen#1@exit:gSessionContext+0x1296:1=1`; the Castle routes enter from the title screen, past the menu that would set it from the same save) on `castle_mewtwo/castle_gym_clear.rec` with a Round 2 save | `r2_castle` | `Color_AdjustRGBA8`, `Color_HSVToRGBA8`, `Color_RGBA8ToHSV`, `BattlePrep_AdjustBannerTextureOffset` |
| (Round 2 VS Mewtwo) | | | the same on `castle_mewtwo/mewtwo_win.rec` | `r2_mewtwo` | 0 |
| VictoryPalace_DrawOverlay -> VictoryPalace_DrawFrameBorder, VictoryPalace_IsCurrentSpeciesOwnedByPlayer; VictoryPalace_HandleInput -> ShowSpeciesInfo / HideOverlays / ShowSpeciesGrid / HideSpeciesGrid; VictoryPalace_ResetPillarGlow -> HideOverlays (other state) | `D_82608542 == 1`, `D_82608544 == 0`, ... | A on an obtained statue shows its info (`D_82608542`), START shows the species grid (`D_82608544`) when statues 1-150 are all there (`D_8267E4E8`); with all 151 the first visit is the all-caught finale, which leaves after 160 frames | **real**: statues of 1-150 (`stadium_save statue set all`, `clear 151`), `replays/tour_vp1.rec` then A, A, START, A, START, START, B, R, A, B, START, B, L, A, START, B, B (`victory_palace_overlays.rec`) | `vp_overlays` | `VictoryPalace_DrawFrameBorder`, `VictoryPalace_HideOverlays`, `VictoryPalace_HideSpeciesGrid`, `VictoryPalace_IsCurrentSpeciesOwnedByPlayer`, `VictoryPalace_ShowSpeciesGrid`, `VictoryPalace_ShowSpeciesInfo` |
| Completion_DrawDialog -> Completion_UpdateGiftDialogWithExisting; Completion_HandleAcceptDeclineInput -> Completion_ResetDialogText (other state) | `D_86B0EBDC->unk_2D8 == 1`, `unk_2F0 == 1` | the completion gift (every statue) when a bonus Pokemon is already waiting at the Lab (`Save_GetSelectedPokemonId`, options +0x1C): the scene asks whether to swap | **real**: every statue and `stadium_save options --bonus MEWTWO`, `tour_vp1.rec` then A every 30 frames (keep) or LEFT, A (swap) | `completion_gift_keep`, `completion_gift_swap` | `Completion_UpdateGiftDialogWithExisting`, `Completion_ResetDialogText`, `Completion_DrawSpeciesRewardText` |
| BattleAnim_ApplySpeciesXZOffsetVariantB / BattleAnim_GetSpeciesScaleAdjust -> Particle_Field38_AddXZFromField94YOffsetMinus90_0901C (battle scene) | `BattleAnim_GetOwnerSpecies(...) == 0x5F` | Dig's animation (move 91) has an Onix-specific offset | **real**: Game Pak party led by ONIX L50 with Dig (TM28), the Free Battle route, `--moves 91,0,0,0` (the player's moves: the time-limit fallback picks Dig) | `onix_dig` | `Particle_Field38_AddXZFromField94YOffsetMinus90_0901C` |
| BattleAnim_ApplySpeciesMotionOffset -> the same (battle scene) | `case 0x2D` | Reflect (115) with Vileplume | **real**: VILEPLUME L50 with Reflect (TM33), the same route | `vile_reflect` | (the same function) |

## 3. Decided: not openable in the shipped game

| gates | why |
|---|---|
| Team_LoadOpponentPreset -> Team_PickRandomOpponentIndex | needs `mode == 2` as well as Round 2; `StadiumSelect_ConfirmSelection` maps the cursor through `{3, 1, 2, 6, 4, 5}`, but entries 1 and 2 have no neighbours in `D_84102450` and no entry leads to them: the cursor cannot reach session modes 1 and 2 in this version |
| all battle-animation "table index" gates (`BattleAnim_RunEffectInitStart/InitEnd/RunEffectList` -> start slots 20, 21, 77, 122, 126, 133, end slots 19, 20, 28, 49, 51 and the default/no-op draws) and the `BattleScene_EnterStateRow` rows 5, 6, 7, 10, 20 | dead data: `battle_anim_slots.md` (no selectable list names them; rows never entered) |
| BattleScene_UpdatePhase -> BattleScene_PhaseCheckFaintedEndTurn / BattleScene_PhaseSendOutAfterFaint | phase 33 is never set (every `SetPhase`/`TransitionToPhase*` passes a literal: 1, 2, 5, 0x22, 0x24 and the listed ones; `unk_78` is always 3); 34 is set only from phase 33 (also `routes/battle_gates/README.md`) |
| BattleScene_SelectNextIntroCameraShot -> IntroCameraStep12Or14, BattleScene_UpdateIntroCameraSequence -> IntroCameraWideHoldStep, BattleScene_RunFinalPresentation / BattleScene_Update camera modes 0 and 3 | values no code produces (`routes/battle_gates/README.md`) |
| BattleScene_UpdateIntroSequence -> BattleScene_IntroStepNop | step 10 is skipped: in step 9 `BattleScene_BeginBattlePhase` adds 1 to `unk_20` and `BattleScene_IntroSecondMonSettle` adds another, 9 -> 11 |
| BattleAnim_CallbackDescriptor26BurstEmitter / 50BurstEmitter -> their burst children; WidgetTree_InitHorizontalMenu / InitHorizontalFrame -> their callbacks | the callers ran only as direct calls (`dead_calls.md`); no natural run reaches them |
| BinArchive_GetFile -> BinArchive_LoadFileBuffered, BinArchive_LoadFileToPool -> BinArchive_LoadFileFromLbaRange | flag 0x80 is set only by the LBA (64DD disk) archive opener: hardware |
| LeoCJCreateLeoManager -> __leoSetReset / leoCommand / __osLeoInterrupt, Storage_ReadRom -> osDriveRomInit | 64DD hardware |
| BattlePrepRoster / BattlePrepTeamIcons / BattlePrep_UpdateCarousel `...PAL` and `...Double` | PAL and two-on-two variants: dead in the US NTSC game (README, previous rounds) |
| Audio_InitBanks / Gsm_Decode_Stream / PortA_Swap* -> PortA_BE16/BE32, Audio_GetActivityScore -> Audio_PortDumpChannels | port-only helpers (byte order, debug dump) |

## 4. Left, and why

| gates | what it needs |
|---|---|
| Battle_HasUsableSleepStatusTarget / Battle_HasUsableMajorStatusTarget / func_84372C40 ... / Battle_CheckSideDefeatAfterResidualStatus -> BattleScene_GetOpposingSelectedSideTeamSize (7 gates, 1 function); TeamSelection_ApplySlotSelection -> TeamSelection_RegisteredTeam_ApplySelection | a two-trainer side (Free Battle "1P & 2P", `sessionTeams->unk_01 == 2`) or a second human player going back in team selection: both need controller 2, and the port reports only pad 1 (`osContInit` in libultraship sets the bits from the ControlDeck, so `FreeBattle_IsRuleAvailable` greys the rows out). A forced `gControllerBits` plus pad-2 input in the recording would be the next try |
| the lab widget SetState callbacks (`BattleInfoWidget_Set*State`, `LabItem_BoxScreen_SetMenuState`, `LabPCList_SetRowMenuState`, `LabPC_BoxScreen_HandleInput`, `LabPC_Draw*`, `WidgetTree_HandleVerticalMenuInput`, `WidgetTree_GetAnimatedPanelInputState`, ...) | the widgets are built in many natural Lab PC runs, but no parent calls their `unk_24`/`unk_20` slot on that run; each needs the one screen whose parent forwards focus to that child (per-callback reading of the Lab PC list code), and `--set` cannot store a host function pointer |
| GbEmu_BlitWithPixelOp -> GbEmu_PixelOpAverage555 / PixelOpDitherB | GB Tower presentation modes (`gGbEmuPresentationMode`) no reached run selected; not traced further |
| Audio_DecodeVoiceCommandStream / Audio_ProcessCommandQueue jump-table commands (16), process_geo_layout / Geo_* node commands (clear depth, cull distance, group, jump, noop, skip, types 3/9/12) | data-dependent: no sequence or geo layout the reached game loads uses them; a scan of all sequence and geo-layout data would decide whether any ROM data does |
| crash screen, profilers (`gShowCPUProfiler`), `miniDebugMode`, TextRenderer queued strings, soft reset, MiniFx, gallery/minigame flags, GB save cart-removal and pak errors, Options, trade, Pokedex map | the debug-crash, minigames-gallery, error-paths and menus-trade sessions' areas |
| ModelAnim_Evaluate*Curve -> ModelAnim_InterpolateKeyframeTangent, func_800361C4 -> Model_ApplyTransformCommands / func_80034BD4 | keyframe and model flags in asset data (same as the data-dependent row) |
| BattleScene_PhaseSendOut -> func_84321D34, func_84345448 -> func_843453E4, OwnerLinkedFourStream_IsComplete -> BattleAnim_MarkParticleInactive, BattleAnim_SixSlotTrail_DrawAll -> ...ComputeCameraRelativeOffset | not decided (`routes/battle_gates/README.md` lists them too) |

## Reproduce

```
evidence/whole_game/routes/gate_trials2/runs.sh <out> r2_cup r2_cupwin r2_castle r2_mewtwo vp gift_keep gift_swap onix_dig vile_reflect
python3 -I tools/evidence/frontier.py <scratchpad>/cg_all4.json build-evidence-all2/pokestadium <out>/front <batch4..14> <out>/* --files "^src/" --build build-evidence-all
python3 -I tools/evidence/try_gates.py <front>/proposals.json <baselines as in section 1> --out <out>/auto --bin build-evidence-all2/pokestadium --jobs 2
```

# Memory-set gate trials

| gate | set | replayed on | callee reached | new functions |
|---|---|---|---|---|
| Audio_CreateFrameTask -> GbMusic_UpdateChannels | `Audio_CreateFrameTask:D_80077D90:1=1` | mv1 | no | 0 (stalled (timeout)) |
| Audio_CreateFrameTask -> func_8003D32C | `Audio_CreateFrameTask:D_80077D9C:1=1` | mv1 | yes | 9 |
| BattleAI_ChooseAction -> BattleAI_DecideAction | `BattleAI_ChooseAction:D_843C5564*:4|=4194304` | fl_cup_giveup__corrupt | yes | 6 |
| Audio_CreateFrameTask -> LeoTestUnitReady | `Audio_CreateFrameTask:D_80077DAC:4=1` | mv1 | yes | 1 |
| Cry_PlayFromPokedex -> func_800498A8 | `Cry_PlayFromPokedex:D_80077D90:1=0` | gb_dex | no | 0 |
| BattleAI_ChooseAction -> BattleAI_ResolveTiedActionChoice | `BattleAI_ChooseAction:D_843C60AC*+0x4:1=1` | fl_cup_giveup__corrupt | no | 0 |
| GalleryCamera_HandleDPadInput -> GalleryCamera_OrbitAtPoint | `GalleryCamera_HandleDPadInput:D_8690B390+0x6:2|=1` | fl_tour_galmenu2__continue | yes | 3 |
| Team_LoadOpponentPreset -> Team_PickRandomOpponentIndex | `Team_LoadOpponentPreset:gSessionContext+0x1296:1=1` | fl_cup_giveup__continue | no | 4 |
| TransferPak_CheckMain -> Save_SetOptionsField02 | `TransferPak_CheckMain:gSessionContext+0x1296:1=0` | fl_cup_giveup__continue | no | 0 |
| Anim_PackedBitsReadSigned -> PackedBits_ReadSigned | `Anim_PackedBitsReadSigned:sRaw:4=0` | fl_cup_giveup__continue | yes | 1 |
| Audio_StartMusicTrack -> Audio_PlayMusicSequenceLooped | `Audio_StartMusicTrack:D_80078EC8:4=1` | fl_cup_giveup__continue | no | 0 |
| BattleAnim_CallbackSharedDescriptor19RecolorAnimatedFade -> ParticleGfx_RecolorEnvFromPaletteSetB | `BattleAnim_CallbackSharedDescriptor19RecolorAnimatedFade:gParticleFrameMod8:4=0` | mv61 | no | 0 |
| BattleAnim_CallbackSharedDescriptor19RecolorAnimatedFade -> ParticleGfx_RecolorPrimFromPaletteSetB | `BattleAnim_CallbackSharedDescriptor19RecolorAnimatedFade:gParticleFrameMod8:4=0` | mv61 | no | 0 |
| BattleAnim_CallbackUnusedGrowFadeVariant -> ParticleGfx_RecolorPrimFromPaletteSetB | `BattleAnim_CallbackUnusedGrowFadeVariant:gParticleFrameMod4:4=0` | battle_xcheck | no | 0 |
| BattlePrep_LoadBannerGeoLayouts -> BattlePrep_AdjustBannerTextureOffset | `BattlePrep_LoadBannerGeoLayouts:gSessionContext+0x1296:1=1` | fl_cup_giveup__continue | yes | 1 |
| BattleScene_HandlePlayerPartySelection -> BattleAnim_PlaySoundEffect1 | `BattleScene_HandlePlayerPartySelection:D_8439014C*+0x8:2|=16` | gb_fb_illegal | no | 0 |
| BattleScene_HandlePlayerPartySelection -> BattleAnim_PlaySoundEffect3 | `BattleScene_HandlePlayerPartySelection:D_8439014C*+0x6:2=0` | gb_fb_illegal | no | 0 |
| BattleScene_HandlePlayerPartySelection -> BattleScene_SelectActiveController | `BattleScene_HandlePlayerPartySelection:D_8439014C*+0x6:2=0` | gb_fb_illegal | no | 0 |
| BattleScene_TickHudTimers -> BattleHud_DrawBattleTimer | `BattleScene_TickHudTimers:sActivePokemon*+0x7f8+0x198*+0x30+0x8:2=23` | fl_cup_giveup__corrupt | no | 0 |
| Battle_Effect_Heal -> Battle_ApplyStatStageMultiplier | `Battle_Effect_Heal:D_8438AC60:1=1` | gb_fb_illegal | no | 0 |
| DisplayList_InitSelectedTextureSegment -> DisplayList_BuildSelectedTextureSegment | `DisplayList_InitSelectedTextureSegment:D_8006F09C*+0x58:2=2` | battle_xcheck | no | 0 |
| DisplayWorker_ThreadMain -> DisplayWorker_DrainEvents | `DisplayWorker_ThreadMain:D_800A62E0+0xab8:2=1` | fl_cup_giveup__continue | no | 0 (run failed) |
| Gallery_DrawBackgroundSelectMenu -> Gallery_DrawHelpBalloonFrame | `Gallery_DrawBackgroundSelectMenu:D_8690B380:4=1` | fl_tour_galmenu2__continue | yes | 3 |
| Gallery_DrawTutorialTextBox -> Gallery_DrawHelpBalloonFrame | `Gallery_DrawTutorialTextBox:D_8690B3C0:4=256` | fl_tour_galmenu2__continue | yes | 1 |
| Gallery_RentalViewerHandleInput -> Gallery_OpenConfirmPrompt | `Gallery_RentalViewerHandleInput:D_83402E20:4=1` | fl_tour_galmenu2__continue | yes | 13 |
| Glc_Draw -> Glc_DrawBackgroundCrossFade | `Glc_Draw:glc_to_e4_transition:2=255` | rp_tour_fm_noreset | no | 0 |
| RentalHub_MenuLoop -> RentalHub_FadeOutWait | `RentalHub_MenuLoop:gSessionContext:1=1` | fl_cup_giveup__corrupt | no | 0 |
| Rental_Main -> Team_SelectRegisteredTeams | `Rental_Main:gSessionContext+0x129a:2|=32` | fl_cup_giveup__corrupt | yes | 1 |
| Snap_PrintClubPrintSeals -> Snap_PrintClubDrawSealMargin | `Snap_PrintClubPrintSeals:D_8FA00A00*:2=3` | st_fastlogo | no | 0 |

10 of 29 proposals reached their callee; 41 functions new over the baselines.

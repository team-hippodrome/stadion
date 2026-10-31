# Direct calls of the functions nothing references

Each of the 44 functions with no reference in the whole decomp and no ROM word with its address was called directly once, in a forked child (`run.py --call-file ... --call-fork`, specs from `tools/evidence/dead_calls.py`: integers 1, pointers to a zeroed 256-byte buffer, floats 1.0), from the main loop after the N64 logo. 41 skipped (struct arguments by value).

Returned: **504**; crashed or hung: 123 (signal14 63, exit139 59, exit138 1). A crash or a hang with placeholder arguments says nothing about the name; a return gives a first behaviour record (its return value, the code it ran), and these functions are dead in the shipped game.

| area | returned | crashed/hung |
|---|---|---|
| battle_engine | 133 | 35 |
| particle_math | 154 | 7 |
| particle_data_library | 21 | 0 |
| minigame_actor_framework | 11 | 2 |
| matrix.c | 12 | 0 |
| pokemon_stats.c | 11 | 0 |
| demo_scenes | 9 | 1 |
| gb_tower_emulator | 9 | 1 |
| widget_tree | 6 | 1 |
| jpeg_stream.c | 2 | 5 |
| ekans_game | 6 | 1 |
| victory_palace | 6 | 1 |
| 33FE0.c | 4 | 2 |
| gallery_camera | 4 | 2 |
| lab_pc_list | 5 | 1 |
| table_view.c | 6 | 0 |
| gb_save.c | 1 | 4 |
| DDC0.c | 5 | 0 |
| leo.c | 5 | 0 |
| save_data.c | 4 | 1 |
| graphics_textures.c | 2 | 2 |
| battle_prep | 2 | 2 |
| minigame_shared_graphics | 3 | 0 |
| 3D140.c | 3 | 0 |
| audio_sound_state.c | 3 | 0 |
| controller.c | 3 | 0 |
| dma.c | 0 | 3 |
| jigglypuff_doodle | 2 | 1 |
| trade_machine | 2 | 1 |
| gb_data.c | 3 | 0 |
| player.c | 3 | 0 |
| poke_icon.c | 2 | 1 |
| geo_render.c | 2 | 1 |
| rattata_game | 2 | 1 |
| 33E30.c | 2 | 0 |
| gallery_backgrounds | 1 | 1 |
| vector_math.c | 2 | 0 |
| flash.c | 2 | 0 |
| 61 | 1 | 1 |
| cup_select | 2 | 0 |
| gallery_enlarge | 1 | 1 |
| gym_leader_castle | 1 | 1 |
| lab_pc | 1 | 1 |
| lab_pc_icons | 1 | 1 |
| minigame_select | 1 | 1 |
| snap_print_club_background_select | 1 | 1 |
| title_screen | 1 | 1 |
| geo_node.c | 1 | 1 |
| model_renderer.c | 1 | 1 |
| util.c | 2 | 0 |
| 3D140_2.c | 2 | 0 |
| audio_commands_category2.c | 2 | 0 |
| ui_graphics.c | 2 | 0 |
| area_select | 1 | 1 |
| clefairy_game | 1 | 1 |
| metapod_game | 1 | 1 |
| sushi_game | 1 | 1 |
| profiler.c | 1 | 0 |
| audio_commands.c | 1 | 0 |
| battle_hud.c | 1 | 0 |
| cry.c | 1 | 0 |
| display_object_textures.c | 0 | 1 |
| credits | 0 | 1 |
| debug_menu_exit | 1 | 0 |
| debug_menu_slot69 | 1 | 0 |
| debug_menu_slot70 | 1 | 0 |
| debug_menu_slot72 | 1 | 0 |
| debug_menu_slot73 | 1 | 0 |
| debug_menu_slot74 | 1 | 0 |
| debug_menu_slot77 | 1 | 0 |
| drowzee_game | 0 | 1 |
| dynamo_game | 0 | 1 |
| fatal_error_screen | 0 | 1 |
| gallery_album | 0 | 1 |
| gallery_debugger | 0 | 1 |
| gallery_film_select | 0 | 1 |
| gallery_rental_viewer | 0 | 1 |
| gb_tower_team_prep | 0 | 1 |
| gb_tower_unused | 0 | 1 |
| gym_leader_castle_clear | 0 | 1 |
| minigame_sample2_viewer | 0 | 1 |
| minigame_sample7_viewer | 0 | 1 |
| minigame_sample8_viewer | 0 | 1 |
| n64_logo | 0 | 1 |
| options | 0 | 1 |
| pikachu_reward | 0 | 1 |
| pokedex | 0 | 1 |
| pokemon_lab_unused | 1 | 0 |
| ranking_debug_test | 0 | 1 |
| rental_hub | 0 | 1 |
| snap_print_club_print_seals | 0 | 1 |
| transfer_pak_check | 0 | 1 |
| widget_toolkit | 1 | 0 |
| game_state.c | 1 | 0 |
| gfx_buffer.c | 1 | 0 |
| gsm_decoder.c | 1 | 0 |
| hal_libc.c | 1 | 0 |
| jpegutils.c | 1 | 0 |
| leomecha.c | 0 | 1 |
| n_resample.c | 1 | 0 |
| n_sl.c | 1 | 0 |
| memmap.c | 1 | 0 |
| memory.c | 1 | 0 |
| memory_main.c | 1 | 0 |
| gallery.c | 1 | 0 |
| gb_audio_render.c | 1 | 0 |
| 1 | 1 | 0 |
| battle_order_select | 0 | 1 |
| event_battle | 0 | 1 |
| gallery_snap | 0 | 1 |
| magikarp_game | 1 | 0 |
| pokemon_lab | 0 | 1 |
| rental_rules | 0 | 1 |
| sandshrew_game | 0 | 1 |
| stadium_select | 0 | 1 |
| transfer_pak_select | 0 | 1 |

## Per function

| function | area | call | result |
|---|---|---|---|
| AreaSelect_Main | area_select | `` | signal14 |
| Asset_LoadToSegmentByLbaRange | jpeg_stream.c | `` | signal14 |
| Audio_DispatchSoundBankStopCommand | audio_commands_category2.c | `` | returned - |
| Audio_GetActiveVoiceCount | audio_sound_state.c | `` | returned 0 |
| Audio_GetDiskReadyStatus | 3D140.c | `` | returned 0 |
| Audio_GetFreeVoiceCount | audio_sound_state.c | `` | returned 0 |
| Audio_IsSoundPlayingWrapper | audio_sound_state.c | `` | returned - |
| Audio_QueueSequenceIdWrapper | 3D140_2.c | `` | returned - |
| Audio_QueueVoicePan | player.c | `` | returned - |
| Audio_QueueVoiceVolumeByte | player.c | `` | returned - |
| Audio_RelocatePointerArray | player.c | `` | returned - |
| Audio_ResetCurrentMusic | DDC0.c | `` | returned - |
| Audio_SeedRandom | audio_commands_category2.c | `` | returned - |
| Audio_SetDiskReadyPolling | 3D140.c | `` | returned - |
| BattleAI_InitTrainerRecords | battle_engine | `` | exit139 |
| BattleAnim_AllocateEffect125DisplayList | battle_engine | `` | returned - |
| BattleAnim_AllocateEffect49DisplayList | battle_engine | `` | returned - |
| BattleAnim_AllocateEffect63DisplayList | battle_engine | `` | returned - |
| BattleAnim_AllocateEffect75DisplayList | battle_engine | `` | returned - |
| BattleAnim_AllocateReflectiveTextureDisplayList | battle_engine | `` | returned - |
| BattleAnim_ApproachS16 | battle_engine | `` | returned - |
| BattleAnim_ApproachU16 | battle_engine | `` | returned - |
| BattleAnim_Atan2sFromPoints | battle_engine | `` | returned 0 |
| BattleAnim_CallbackDescriptor26BurstEmitter | battle_engine | `` | returned - |
| BattleAnim_CallbackDescriptor50BurstEmitter | battle_engine | `` | returned - |
| BattleAnim_CallbackOwnerModelTintPaletteApproach | battle_engine | `` | returned - |
| BattleAnim_CallbackOwnerModelTintPaletteApproachRampDown | battle_engine | `` | returned - |
| BattleAnim_CallbackUnregisteredStraightDirectionalStreamFade | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedAimedSparkVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedDescriptor4FColorCycleScaleFadeVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedFallingTrailVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedGenericFadeApproach | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedGlitterVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedGrowFadeVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedGrowingSparkVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedIndexedFadeVariantA | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedIndexedFadeVariantB | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedRiseImpulseVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedRotatingSparkVariant | battle_engine | `` | returned - |
| BattleAnim_CallbackUnusedVariant2 | battle_engine | `` | returned - |
| BattleAnim_DrawStoredColorOverlay | battle_engine | `` | returned - |
| BattleAnim_EndEffectUnusedVerticalFadeAndDelayedFrameSequence | battle_engine | `` | exit139 |
| BattleAnim_GetCameraLookYaw | battle_engine | `` | exit139 |
| BattleAnim_GetCameraStoredAngles70 | battle_engine | `` | exit139 |
| BattleAnim_GetCameraStoredAngles76 | battle_engine | `` | exit139 |
| BattleAnim_GetCameraStoredYaw72 | battle_engine | `` | exit139 |
| BattleAnim_GetCameraStoredYaw78 | battle_engine | `` | exit139 |
| BattleAnim_GetEffectStateFlag | battle_engine | `` | returned 0 |
| BattleAnim_GetOwnerModelBoundsScaled | battle_engine | `` | returned - |
| BattleAnim_GetSecondaryOwnerSpeciesBaseScale | battle_engine | `` | returned - |
| BattleAnim_GetSpeciesBySideIndex | battle_engine | `` | exit139 |
| BattleAnim_GetTertiaryOwnerSpecies | battle_engine | `` | exit139 |
| BattleAnim_GetTertiaryOwnerSpeciesBaseScale | battle_engine | `` | returned - |
| BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence | battle_engine | `` | exit139 |
| BattleAnim_OrphanDescriptor34Emitter | battle_engine | `` | exit139 |
| BattleAnim_QueueEffectList5 | battle_engine | `` | returned - |
| BattleAnim_QueueEffectList6Alt | battle_engine | `` | returned - |
| BattleAnim_RenderNop1 | battle_engine | `` | returned - |
| BattleAnim_RenderNop2 | battle_engine | `` | returned - |
| BattleAnim_SetOwnerSecondaryStatusFlags | battle_engine | `` | returned - |
| BattleAnim_SpawnDescriptor25AlphaHoldDetach | battle_engine | `` | exit139 |
| BattleAnim_SpawnDescriptor25AlphaHoldDetachFullAlpha | battle_engine | `` | exit139 |
| BattleAnim_SpawnDescriptor25AlphaHoldDetachRepeating | battle_engine | `` | exit139 |
| BattleAnim_SpawnDescriptor25PaletteFadeIn | battle_engine | `` | exit139 |
| BattleAnim_SpawnDescriptor25PaletteFadeOut | battle_engine | `` | exit139 |
| BattleAnim_SpawnOwnerModelAlphaFadeFullPreset | battle_engine | `` | exit139 |
| BattleAnim_SpawnOwnerModelTintHoldFadeDetach | battle_engine | `` | exit139 |
| BattleAnim_SpawnOwnerModelTintPaletteCycleVariant | battle_engine | `` | exit139 |
| BattleAnim_SpawnOwnerModelTintRandomPaletteCycle | battle_engine | `` | exit139 |
| BattleAnim_TriggerStatusAfflictionCry | battle_engine | `` | exit139 |
| BattleAnim_UpdateOwnerSecondaryStatusAnimation | battle_engine | `` | exit139 |
| BattleAnim_Vec3fOffsetByPolarXZ | battle_engine | `` | returned 4329533056 |
| BattleAnim_Vec3fScaleComponents | battle_engine | `` | returned 4329533056 |
| BattleAnim_Vec3fSetZero | battle_engine | `` | returned 4329533056 |
| BattleHud_DrawForfeitConfirmDialog | battle_engine | `` | exit139 |
| BattleHud_DrawHpBar16x5 | ui_graphics.c | `` | returned - |
| BattleHud_DrawMessageBox | battle_engine | `` | exit139 |
| BattleMenu_RunOrderSelectScreen | battle_order_select | `` | signal14 |
| BattlePrepBadgeAward_StartScatter | battle_prep | `` | exit139 |
| BattlePrep_FormatString | battle_prep | `` | returned 4307081276 |
| BattlePrep_RunRulesScreen | battle_prep | `` | signal14 |
| BattleScene_AdvanceCameraSubstepCounter | battle_engine | `` | exit139 |
| BattleScene_ApproachCameraRollNoEyeUpdate | battle_engine | `` | exit139 |
| BattleScene_ClearParticipantOwnerActiveFlag | battle_engine | `` | returned - |
| BattleScene_ComputeParticipantModelOffset | battle_engine | `` | returned - |
| BattleScene_GetCombinedTeamSize | battle_engine | `` | exit139 |
| BattleScene_IntroInitNop | battle_engine | `` | returned - |
| BattleScene_IsCameraAngleSettled | battle_engine | `` | returned 1 |
| BattleScene_IsCameraFrameCounterAtLeast | battle_engine | `` | exit139 |
| BattleScene_IsParticipantModelVisible | battle_engine | `` | returned 0 |
| BattleScene_ReloadMoveResourceList | battle_engine | `` | exit139 |
| BattleScene_ResetOpponentStateFrameCounter | battle_engine | `` | exit139 |
| BattleScene_SetCameraEyeAndStoreAnglesFromAt | battle_engine | `` | exit139 |
| BattleScene_SetParticipantModelScale | battle_engine | `` | returned - |
| BattleScene_SetParticipantOwnerActiveFlag | battle_engine | `` | returned - |
| BattleScene_TickClearAnimRecordIfLoaded | battle_engine | `` | exit139 |
| Battle_CheckSideDefeatAfterResidualStatus | battle_engine | `` | returned 0 |
| Battle_ComputeSplineWeightsVariantA | battle_engine | `` | returned - |
| Battle_ComputeSplineWeightsVariantB | battle_engine | `` | returned - |
| Battle_ComputeSplineWeightsVariantC | battle_engine | `` | returned - |
| Battle_ComputeSplineWeightsVariantD | battle_engine | `` | returned - |
| Battle_ComputeSplineWeightsVariantE | battle_engine | `` | returned - |
| Battle_GetScaleRatio | battle_engine | `` | returned 0 |
| Battle_GetTertiaryOwnerBonePositionUnused | battle_engine | `` | returned - |
| Battle_IsPartyMemberAlive | battle_engine | `` | exit138 |
| Battle_Main | battle_engine | `` | signal14 |
| Battle_QueueStatusText | battle_engine | `` | exit139 |
| Battle_SetActiveMoveFromSlotIndex | battle_engine | `` | returned - |
| Battle_SetModelToStatusAnim | battle_engine | `` | returned - |
| BinArchive_Free | jpeg_stream.c | `` | exit139 |
| BinArchive_GetCachedFile | jpeg_stream.c | `` | returned 0 |
| BinArchive_LoadByLbaRange | jpeg_stream.c | `` | signal14 |
| ByteMatrix_AppendRow | table_view.c | `` | returned 1 |
| ByteMatrix_CopyRow | table_view.c | `` | returned 1 |
| ByteMatrix_CopyRowAt | table_view.c | `` | returned 1 |
| ByteMatrix_Free | table_view.c | `` | returned - |
| ClefairyGame_Main | clefairy_game | `` | signal14 |
| Cont_AttemptReadEeprom | controller.c | `` | returned 4294967295 |
| Cont_AttemptWriteEeprom | controller.c | `` | returned 0 |
| Cont_IsEepromPresent | controller.c | `` | returned 1 |
| Credits_Main | credits | `` | signal14 |
| Cry_PlayVariant4 | cry.c | `` | returned - |
| Cry_SetEnabled | 3D140.c | `` | returned - |
| CupSelect_Main | cup_select | `` | returned 1 |
| DebugMenu_Exit | debug_menu_exit | `` | returned 2 |
| DebugMenu_ForwardSlot69 | debug_menu_slot69 | `` | returned 128 |
| DebugMenu_ForwardSlot70 | debug_menu_slot70 | `` | returned 128 |
| DebugMenu_ForwardSlot72 | debug_menu_slot72 | `` | returned 128 |
| DebugMenu_ForwardSlot73 | debug_menu_slot73 | `` | returned 128 |
| DebugMenu_ForwardSlot76 | debug_menu_slot74 | `` | returned 128 |
| Deck_GetSaveEntryNameWrapper | save_data.c | `` | returned - |
| DisplayList_InitAnimatedFrameSegment | particle_data_library | `` | returned - |
| DisplayList_InitAnimatedModelTexture | particle_data_library | `` | returned - |
| DisplayList_InitColorCycleBoxSegment | particle_data_library | `` | returned - |
| DisplayList_InitConditionalSegment | particle_data_library | `` | returned - |
| DisplayList_InitDigitDisplayBackground | particle_data_library | `` | returned - |
| DisplayList_InitDigitOrGlyphSegment | particle_data_library | `` | returned - |
| DisplayList_InitIndexedTextureSegment | particle_data_library | `` | returned - |
| DisplayList_InitPulsingRectSegment | particle_data_library | `` | returned - |
| DisplayList_InitRotationMatrixSegment | particle_data_library | `` | returned - |
| DisplayList_InitRotationMatrixSegmentAlt | particle_data_library | `` | returned - |
| DisplayList_InitRotationMatrixSegmentAlt2 | particle_data_library | `` | returned - |
| DisplayList_InitScrollingDualPanel | particle_data_library | `` | returned - |
| DisplayList_InitScrollingIconSegment0 | particle_data_library | `` | returned - |
| DisplayList_InitScrollingIconSegment1 | particle_data_library | `` | returned - |
| DisplayList_InitScrollingIconSegment2 | particle_data_library | `` | returned - |
| DisplayList_InitScrollingIconSegment3 | particle_data_library | `` | returned - |
| DisplayList_InitSelectedTextureSegment | particle_data_library | `` | returned - |
| DisplayList_InitToggledBlendModeSegment | particle_data_library | `` | returned - |
| DisplayList_InitTwoSegmentChain | particle_data_library | `` | returned - |
| DisplayList_SetFlagInRange | particle_data_library | `` | returned - |
| DisplayList_SetFlagOutOfRange | particle_data_library | `` | returned - |
| Dma_CopyChunks | jpeg_stream.c | `` | returned - |
| Dma_ReadChunks | jpeg_stream.c | `` | signal14 |
| DrowzeeGame_Main | drowzee_game | `` | signal14 |
| DynamoGame_Main | dynamo_game | `` | signal14 |
| EventBattle_Main | event_battle | `` | signal14 |
| FatalError_Main | fatal_error_screen | `` | signal14 |
| Flash_EraseChip | flash.c | `` | returned 4294967295 |
| Flash_Reinitialize | flash.c | `` | returned 4328590696 |
| Font_DisableLineBreak | graphics_textures.c | `` | exit139 |
| Font_EnableLineBreak | graphics_textures.c | `` | exit139 |
| Font_GetSetInfo | graphics_textures.c | `` | returned 0 |
| Font_UnloadSizes | graphics_textures.c | `` | returned - |
| Fragment_LoadByLbaRange | jpeg_stream.c | `` | signal14 |
| GalleryCamera_DebugAdjustZoomOffset | gallery_camera | `` | returned - |
| GalleryCamera_ResetFreeView | gallery_camera | `` | exit139 |
| GalleryDebugger_Main | gallery_debugger | `` | signal14 |
| Gallery_AlbumMain | gallery_album | `` | signal14 |
| Gallery_CameraMain | gallery_camera | `` | signal14 |
| Gallery_CopyCommonUiString | gallery_backgrounds | `` | returned 505145703432 |
| Gallery_EnlargeMain | gallery_enlarge | `` | signal14 |
| Gallery_FilmSelectMain | gallery_film_select | `` | signal14 |
| Gallery_LoadUnlockedBackgrounds | gallery_backgrounds | `` | signal14 |
| Gallery_RentalViewerMain | gallery_rental_viewer | `` | signal14 |
| Gallery_SnapMain | gallery_snap | `` | signal14 |
| GbApu_AllocZeroed | gb_tower_emulator | `` | returned 0 |
| GbApu_ClearActiveFlag | gb_tower_emulator | `` | returned - |
| GbApu_SetActiveFlag | gb_tower_emulator | `` | returned - |
| GbApu_SetNoisePeriodBounds | gb_tower_emulator | `` | returned - |
| GbSave_CopyPortSnapshot | pokemon_stats.c | `` | returned 0 |
| GbSave_GetPortAvailability | pokemon_stats.c | `` | returned 1 |
| GbSave_GetSecondaryName | pokemon_stats.c | `` | returned 0 |
| GbSave_HasProgressFlag1000 | pokemon_stats.c | `` | returned 0 |
| GbSave_SetPlayerName | pokemon_stats.c | `` | returned 0 |
| GbSave_SetSecondaryName | pokemon_stats.c | `` | returned 0 |
| GbSave_SetTrainerId | pokemon_stats.c | `` | returned - |
| GbTower_TeamPrep | gb_tower_team_prep | `` | signal14 |
| GeoRender_GetMatrix | geo_render.c | `` | returned 4329194816 |
| GeoRender_IsModelFrameCurrent | geo_render.c | `` | returned 1 |
| Geo_ProcessCallbacks | geo_render.c | `` | exit139 |
| Gfx_GetDisplayListRemaining | gfx_buffer.c | `` | returned 130512 |
| GraphNode_GetChildAtIndex | geo_node.c | `` | returned 0 |
| GraphNode_RemoveChild | geo_node.c | `` | exit139 |
| Gsm_Decode | gsm_decoder.c | `` | returned 0 |
| GymLeaderCastleClear_Main | gym_leader_castle_clear | `` | signal14 |
| GymLeaderCastle_Main | gym_leader_castle | `` | signal14 |
| HAL_Strcpy2 | hal_libc.c | `` | returned 4329533057 |
| HallOfFame_Main | victory_palace | `` | signal14 |
| JpegUtils_ProcessHuffmanTableImplOld | jpegutils.c | `` | returned 1 |
| LabPCList_ShowListScreen | lab_pc_list | `` | signal14 |
| LabPC_Dispatch | lab_pc | `` | signal14 |
| LabPC_IconListDispatch | lab_pc_icons | `` | exit139 |
| Lab_Main | pokemon_lab | `` | signal14 |
| Leo_AbortPendingBootRequest | leo.c | `` | returned - |
| Leo_InitializeBootState | leo.c | `` | returned 0 |
| Leo_ResetBootStateIfReady | leo.c | `` | returned - |
| Leo_RunBootStateMachine | leo.c | `` | returned 0 |
| MainPool_ResetState | memory.c | `` | returned - |
| Memmap_GetFragmentVaddrMask | memmap.c | `` | returned 0 |
| MiniFx_GraphNodeBuildColorRampList | minigame_actor_framework | `` | returned - |
| MiniFx_GraphNodeBuildParticleRenderListVariantA | minigame_actor_framework | `` | returned - |
| MiniFx_GraphNodeBuildParticleRenderListVariantB | minigame_actor_framework | `` | returned - |
| MiniGameSelect_Entry | minigame_select | `` | signal14 |
| MiniGfx_DrawBackgroundStrips | minigame_shared_graphics | `` | returned - |
| MiniGfx_DrawScaledSprite | minigame_shared_graphics | `` | returned - |
| Minigame2SampleViewer_PollInput | gb_tower_unused | `` | signal14 |
| MinigameGetVec3Distance_2d | minigame_actor_framework | `` | returned 0 |
| MinigameGetVec3Distance_3d | minigame_actor_framework | `` | returned - |
| Minigame_JigglypuffDoodle | jigglypuff_doodle | `` | signal14 |
| Minigame_RankingDebugTest | ranking_debug_test | `` | signal14 |
| Minigame_Sample7Viewer | minigame_sample7_viewer | `` | signal14 |
| Minigame_Sample8Viewer | minigame_sample8_viewer | `` | signal14 |
| ModelAnim_GetAnimationRecord | model_renderer.c | `` | returned 0 |
| ModelRenderer_GetObjectType | battle_hud.c | `` | returned 0 |
| Model_LoadWithScale | display_object_textures.c | `` | signal14 |
| MtxF_PackToFixed | demo_scenes | `` | returned - |
| MtxF_ScaleUpperRows | demo_scenes | `` | returned - |
| MtxF_SetRotationTranslation | matrix.c | `` | returned - |
| MtxF_SetTranslation | matrix.c | `` | returned - |
| MtxF_TransformVec3s | matrix.c | `` | returned - |
| MtxF_TransposeScaled | demo_scenes | `` | returned - |
| N64Logo_Main | n64_logo | `` | signal14 |
| Options_Main | options | `` | signal14 |
| ParticleGfx_ApproachEnvPalette | particle_math | `` | returned 0 |
| ParticleGfx_DrawBillboardDefaultQuad | particle_math | `` | exit139 |
| ParticleGfx_DrawBillboardPrimEnvRotatedTriangle | particle_math | `` | exit139 |
| ParticleGfx_DrawI4TextureAtlasTiled | particle_math | `` | returned - |
| ParticleGfx_DrawI8TextureAtlas | particle_math | `` | returned - |
| ParticleGfx_DrawIa16TextureAtlas | particle_math | `` | returned - |
| ParticleGfx_DrawIa8TextureAtlas | particle_math | `` | returned - |
| ParticleGfx_GraphNodeBuildDescriptorMaterialList | particle_math | `` | returned - |
| ParticleGfx_LoadTextureCallbackI4TileFrameDiv2Mod8_24x24 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureCallbackRgba16_32x32 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureCi8 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureCi8WrapSWithMask | particle_math | `` | returned - |
| ParticleGfx_LoadTextureDualRgba16I8 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureI4ToList | particle_math | `` | returned - |
| ParticleGfx_LoadTextureI4WrapS | particle_math | `` | signal14 |
| ParticleGfx_LoadTextureI8WrapST | particle_math | `` | signal14 |
| ParticleGfx_LoadTextureIa4 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureRgba16Dual | particle_math | `` | returned - |
| ParticleGfx_LoadTextureRgba16WrapS | particle_math | `` | signal14 |
| ParticleGfx_LoadTextureRgba16WrapST | particle_math | `` | signal14 |
| ParticleGfx_LoadTextureRgba16WrapSWithMask | particle_math | `` | returned - |
| ParticleGfx_LoadTextureRgba32 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureTileCi8 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureTileI8Alias | particle_math | `` | returned - |
| ParticleGfx_LoadTextureTileIa4 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureTileIa8 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureTileRgba16 | particle_math | `` | returned - |
| ParticleGfx_LoadTextureTileRgba32 | particle_math | `` | returned - |
| ParticleGfx_NoopStub1 | particle_math | `` | returned - |
| ParticleGfx_NoopStub10 | particle_math | `` | returned - |
| ParticleGfx_NoopStub2 | particle_math | `` | returned - |
| ParticleGfx_NoopStub3 | particle_math | `` | returned - |
| ParticleGfx_NoopStub4 | particle_math | `` | returned - |
| ParticleGfx_NoopStub5 | particle_math | `` | returned - |
| ParticleGfx_NoopStub6 | particle_math | `` | returned - |
| ParticleGfx_NoopStub7 | particle_math | `` | returned - |
| ParticleGfx_NoopStub8 | particle_math | `` | returned - |
| ParticleGfx_NoopStub9 | particle_math | `` | returned - |
| ParticleGfx_RecolorPrimEnvFromPaletteSetA | particle_math | `` | returned - |
| ParticleGfx_RecolorPrimFromPaletteSetA | particle_math | `` | returned - |
| ParticleGfx_SetTextureState | particle_math | `` | returned - |
| ParticleMath_AddVec3s | particle_math | `` | returned - |
| ParticleMath_AdvanceRandomOffset | particle_math | `` | returned - |
| ParticleMath_ApproachAngleCoarse | particle_math | `` | returned 1 |
| ParticleMath_ApproachFloatDown | particle_math | `` | returned 0 |
| ParticleMath_ApproachFloatDownByPointer | particle_math | `` | returned 0 |
| ParticleMath_ApproachFloatUp | particle_math | `` | returned 1 |
| ParticleMath_ApproachFloatUpByPointer | particle_math | `` | returned 0 |
| ParticleMath_ApproachS16Down | particle_math | `` | returned 0 |
| ParticleMath_ApproachS16DownByPointer | particle_math | `` | returned 0 |
| ParticleMath_ApproachS16Up | particle_math | `` | returned 1 |
| ParticleMath_ApproachS16UpByPointer | particle_math | `` | returned 0 |
| ParticleMath_ApproachS32 | particle_math | `` | returned 1 |
| ParticleMath_ApproachS32Down | particle_math | `` | returned 0 |
| ParticleMath_ApproachS32DownByPointer | particle_math | `` | returned 0 |
| ParticleMath_ApproachS32Up | particle_math | `` | returned 1 |
| ParticleMath_ApproachS32UpByPointer | particle_math | `` | returned 0 |
| ParticleMath_ApproachS8 | particle_math | `` | returned 1 |
| ParticleMath_ApproachU16Down | particle_math | `` | returned 0 |
| ParticleMath_ApproachU16Up | particle_math | `` | returned 1 |
| ParticleMath_BitwiseOr | particle_math | `` | returned - |
| ParticleMath_BounceFloat | particle_math | `` | returned - |
| ParticleMath_BounceS16 | particle_math | `` | returned - |
| ParticleMath_HasBits | particle_math | `` | returned 0 |
| ParticleMath_LacksBits | particle_math | `` | returned 1 |
| ParticleMath_NoopStub | particle_math | `` | returned - |
| ParticleMath_Rotate2DX | particle_math | `` | returned 1 |
| ParticleMath_Rotate2DY | particle_math | `` | returned 1 |
| ParticleMath_ScaleVec3f | particle_math | `` | returned 4329533056 |
| Particle_AddPositionFromRotationTemplate2 | particle_math | `` | returned - |
| Particle_AddRotationRandomSignedXWithFixedYQuarterTurn | particle_math | `` | returned - |
| Particle_AddRotationRandomSignedXY | particle_math | `` | returned - |
| Particle_AddRotationRandomSignedXYWithXHalfTurn | particle_math | `` | returned - |
| Particle_AddRotationRandomSignedY | particle_math | `` | returned - |
| Particle_AddRotationRandomSignedYWithHalfTurn | particle_math | `` | returned - |
| Particle_AddVelocityFromRotationTemplate4 | particle_math | `` | returned - |
| Particle_AddVelocityFromRotationTemplate6 | particle_math | `` | returned - |
| Particle_AddVelocityFromRotationTemplate7A | particle_math | `` | returned - |
| Particle_AddVelocityXYFromRotationZViaImpulse | particle_math | `` | returned - |
| Particle_AddVelocityXZFromRotationY | particle_math | `` | returned - |
| Particle_AnimateFramesPingPongAndAdvanceLifecycle | particle_math | `` | returned 0 |
| Particle_AnimateFramesReverseAndAdvanceLifecycle | particle_math | `` | returned 0 |
| Particle_AnimateFramesReverseFirstPingPongAndAdvanceLifecycle | particle_math | `` | returned 0 |
| Particle_ApproachRotationTargetXY | particle_math | `` | returned - |
| Particle_ApproachRotationTargetXYZ | particle_math | `` | returned - |
| Particle_ApproachRotationYTo | particle_math | `` | returned - |
| Particle_ComputeVectorFromRotationXY | particle_math | `` | returned - |
| Particle_ComputeVectorFromRotationXZ | particle_math | `` | returned - |
| Particle_CountActiveAlias | particle_math | `` | exit139 |
| Particle_DisableFlag40 | particle_math | `` | returned - |
| Particle_EnableFlag40 | particle_math | `` | returned - |
| Particle_Field38_AddXScaled_08E70 | particle_math | `` | returned - |
| Particle_Field38_AddZScaled_08EB0 | particle_math | `` | returned - |
| Particle_Field38_SetVec_08C68 | particle_math | `` | returned - |
| Particle_Field50_AddVecScaled_09968 | particle_math | `` | returned - |
| Particle_Field50_AddXFromField7C_09C58 | particle_math | `` | returned - |
| Particle_Field50_AddXScaled_099C0 | particle_math | `` | returned - |
| Particle_Field50_AddZFromField7C_09C80 | particle_math | `` | returned - |
| Particle_Field50_AddZScaled_09A00 | particle_math | `` | returned - |
| Particle_Field50_AdvanceField7CWithTimer_0B854 | particle_math | `` | returned 0 |
| Particle_Field50_ApproachXToField5C_09AD8 | particle_math | `` | returned 1 |
| Particle_Field50_ApproachZToField5C_09B58 | particle_math | `` | returned 1 |
| Particle_Field50_SetVecFromField7C_09B98 | particle_math | `` | returned - |
| Particle_Field50_SetXScaled_098E8 | particle_math | `` | returned - |
| Particle_Field50_SetXYFromField7C_09BC8 | particle_math | `` | returned - |
| Particle_Field50_SetZScaled_09918 | particle_math | `` | returned - |
| Particle_Field50_SubVecScaled_09A20 | particle_math | `` | returned - |
| Particle_Field50_SubXScaled_09A78 | particle_math | `` | returned - |
| Particle_Field50_SubYScaled_09A98 | particle_math | `` | returned - |
| Particle_Field50_SubZScaled_09AB8 | particle_math | `` | returned - |
| Particle_Field5C_ApproachY_0A0C4 | particle_math | `` | returned 1 |
| Particle_Field5C_SetXScaled_0A07C | particle_math | `` | returned - |
| Particle_Field5C_SetZScaled_0A0AC | particle_math | `` | returned - |
| Particle_Field7C_DecrementXUntilAtMost_0B050 | particle_math | `` | returned 1 |
| Particle_Field7C_DecrementZUntilAtMost_0B0F8 | particle_math | `` | returned 1 |
| Particle_Field7C_IncrementXUntilAtLeast_0AF54 | particle_math | `` | returned 1 |
| Particle_Field7C_IncrementYUntilAtLeast_0AFA8 | particle_math | `` | returned 1 |
| Particle_Field7C_IncrementZUntilAtLeast_0AFFC | particle_math | `` | returned 1 |
| Particle_Field7C_SetVecFromField74AnglesYZVariant_0B700 | particle_math | `` | returned - |
| Particle_Field7C_SetXFromField74AngleY_0B1EC | particle_math | `` | returned - |
| Particle_Field7C_SetXScaled_0AF0C | particle_math | `` | returned - |
| Particle_Field7C_SetYFromField74AngleX_0B1C4 | particle_math | `` | returned - |
| Particle_Field7C_SetZFromField74AngleY_0B214 | particle_math | `` | returned - |
| Particle_Field7C_SetZScaled_0AF3C | particle_math | `` | returned - |
| Particle_Field94_AddConstantAll_0A2E0 | particle_math | `` | returned - |
| Particle_Field94_AddVecFromField9A_0A480 | particle_math | `` | returned - |
| Particle_Field94_AddXConstant_0A30C | particle_math | `` | returned - |
| Particle_Field94_AddYConstant_0A320 | particle_math | `` | returned - |
| Particle_Field94_DecrementYTo_0A810 | particle_math | `` | returned 0 |
| Particle_Field94_DecrementZTo_0A844 | particle_math | `` | returned 0 |
| Particle_Field94_SubX_0A63C | particle_math | `` | returned - |
| Particle_Field94_SubY_0A658 | particle_math | `` | returned - |
| Particle_Field94_SubZ_0A674 | particle_math | `` | returned - |
| Particle_FieldA6_DecrementToStep1_0BD80 | particle_math | `` | returned 1 |
| Particle_FieldA6_DecrementToZeroAndStepState_0BD34 | particle_math | `` | returned 1 |
| Particle_FieldA6_DecrementToZero_0BD08 | particle_math | `` | returned 1 |
| Particle_FieldA6_GetS16_0BCBC | particle_math | `` | returned 0 |
| Particle_FieldA8_DecrementToZeroStep1AndStepState_0BE8C | particle_math | `` | returned 1 |
| Particle_FieldA8_GetS16_0BE28 | particle_math | `` | returned 0 |
| Particle_FieldAE_GetS16_0C050 | particle_math | `` | returned 0 |
| Particle_HasFlag40 | particle_math | `` | returned - |
| Particle_SetFieldOffsetA6IfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetA8IfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetAAIfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetACIfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetAEIfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetB0IfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetCDIfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetCEIfNotSentinel | particle_math | `` | returned - |
| Particle_SetFieldOffsetCFIfNotSentinel | particle_math | `` | returned - |
| Particle_SetPositionFromRotationTemplate2 | particle_math | `` | returned - |
| Particle_SetPrimAlphaIfNotSentinel | particle_math | `` | returned - |
| Particle_SetRotationTargetFromCurrent | particle_math | `` | returned - |
| Particle_ToggleFlags | particle_math | `` | returned - |
| PointerList_Free | table_view.c | `` | returned - |
| PokeIcon_PollFrameLoadForObject | model_renderer.c | `` | exit139 |
| PokeIcon_RedrawSlot | poke_icon.c | `` | exit139 |
| PokeIcon_RenderNextSlotBlocking | poke_icon.c | `` | returned 4294967295 |
| PokeIcon_ResetRenderList | poke_icon.c | `` | returned - |
| Pokedex_Main | pokedex | `` | exit139 |
| Pokemon_StatExpForTarget | gb_data.c | `` | returned 1 |
| RattataGame_Main | rattata_game | `` | signal14 |
| RentalHub_Main | rental_hub | `` | signal14 |
| RentalRules_Main | rental_rules | `` | signal14 |
| Rental_Main | 61 | `` | signal14 |
| Save_CommitAllBankRecords | save_data.c | `` | returned - |
| Save_EnsureBank3Loaded | save_data.c | `` | signal14 |
| Save_RestoreTypedRecord | save_data.c | `` | returned - |
| Snap_PrintClubBackgroundSelect | snap_print_club_background_select | `` | signal14 |
| Snap_PrintClubPrintSeals | snap_print_club_print_seals | `` | exit139 |
| StadiumSelect_Main | stadium_select | `` | signal14 |
| Stadium_BonusPikachuReward | pikachu_reward | `` | exit139 |
| Stage_DispatchDemoScene | demo_scenes | `` | signal14 |
| Storage_QueueDiskMotorOff | dma.c | `` | signal14 |
| Storage_QueueDiskSeek | dma.c | `` | signal14 |
| Storage_QueueRtcWrite | dma.c | `` | signal14 |
| SushiGame_Main | sushi_game | `` | signal14 |
| Text_CompareU16Terminated | table_view.c | `` | returned 0 |
| Text_GetTmHmName | gb_data.c | `` | returned 4329533056 |
| Title_Main | title_screen | `` | signal14 |
| TradeCable_GetSequenceState | trade_machine | `` | returned 0 |
| Trade_Main | trade_machine | `` | signal14 |
| TransferPak_CheckMain | transfer_pak_check | `` | signal14 |
| TransferPak_SelectMain | transfer_pak_select | `` | signal14 |
| Ui_AdjustSteppedRangeSelection | gb_save.c | `` | exit139 |
| Ui_AdjustSteppedScrollSelection | gb_save.c | `` | exit139 |
| Ui_Draw20x20Icon | ui_graphics.c | `` | returned - |
| Ui_GetSelectionDirection | gb_save.c | `` | returned 0 |
| Ui_HandleHorizontalSelectionInput | gb_save.c | `` | exit139 |
| Ui_HandleVerticalSelectionInput | gb_save.c | `` | exit139 |
| Util_CheckStubMemArea | util.c | `` | returned 0 |
| Util_Memclear | util.c | `` | returned - |
| Vec3f_Add | matrix.c | `` | returned 4329533056 |
| Vec3f_CrossProductArrays | vector_math.c | `` | returned - |
| Vec3f_NormalizeSafe | vector_math.c | `` | returned - |
| Vec3f_SetComponents | matrix.c | `` | returned 4329533056 |
| Vec3f_SubtractInPlace | matrix.c | `` | returned 4329533056 |
| Vec3f_TriangleNormal | matrix.c | `` | returned 4329533056 |
| Vec3f_Zero | demo_scenes | `` | returned - |
| Vec3s_Add | matrix.c | `` | returned 4329533056 |
| Vec3s_AddInPlace | matrix.c | `` | returned 4329533056 |
| Vec3s_Copy | demo_scenes | `` | returned 4329533056 |
| Vec3s_FromVec3f | matrix.c | `` | returned 4329533056 |
| Vec3s_Subtract | matrix.c | `` | returned 4329533056 |
| Vec3s_SubtractInPlace | matrix.c | `` | returned 4329533056 |
| WidgetTree_AppendHorizontalMenuItem | widget_tree | `` | returned - |
| WidgetTree_FreeChildren | widget_tree | `` | returned - |
| WidgetTree_FreeTree | widget_tree | `` | signal14 |
| WidgetTree_InitHorizontalFrame | widget_tree | `` | returned - |
| WidgetTree_InitHorizontalMenu | widget_tree | `` | returned - |
| WidgetTree_InitTextMessageBuffer | widget_tree | `` | returned - |
| WidgetTree_PrependChild | widget_tree | `` | returned - |
| Widget_DrawMeterBar | widget_toolkit | `` | returned - |
| clear_profiler_data | profiler.c | `` | returned - |
| ekansMinigameLoad | ekans_game | `` | signal14 |
| func_8000D1C0 | DDC0.c | `` | returned - |
| func_8000D1C8 | DDC0.c | `` | returned - |
| func_8000D1D0 | DDC0.c | `` | returned - |
| func_8000D1D8 | DDC0.c | `` | returned - |
| func_8000E5B4 | leo.c | `` | returned - |
| func_800220C4 | gb_data.c | `` | returned - |
| func_80022A88 | pokemon_stats.c | `` | returned 23 |
| func_80022A98 | pokemon_stats.c | `` | returned 115 |
| func_80022AA8 | pokemon_stats.c | `` | returned 85 |
| func_80022ACC | pokemon_stats.c | `` | returned 119 |
| func_8002667C | save_data.c | `` | returned - |
| func_8002B244 | game_state.c | `` | returned - |
| func_800318F0 | gallery.c | `` | returned - |
| func_80033230 | 33E30.c | `` | returned - |
| func_80033264 | 33E30.c | `` | returned - |
| func_800333E0 | 33FE0.c | `` | returned 516 |
| func_80033410 | 33FE0.c | `` | returned 16 |
| func_800338B0 | 33FE0.c | `` | returned - |
| func_80033D1C | 33FE0.c | `` | exit139 |
| func_80034254 | 33FE0.c | `` | exit139 |
| func_80035208_empty | 33FE0.c | `` | returned - |
| func_8003733C | audio_commands.c | `` | returned - |
| func_800420F0_empty | 3D140_2.c | `` | returned - |
| func_8004AD2C | gb_audio_render.c | `` | returned - |
| func_81207330 | gb_tower_emulator | `` | returned - |
| func_81207354 | gb_tower_emulator | `` | returned - |
| func_81207690 | gb_tower_emulator | `` | returned - |
| func_81207C5C_Empty | gb_tower_emulator | `` | returned 0 |
| func_81208E4C | gb_tower_emulator | `` | exit139 |
| func_81209688 | gb_tower_emulator | `` | returned - |
| func_81209690 | 1 | `` | returned - |
| func_81409288 | particle_math | `` | returned - |
| func_81409308 | particle_math | `` | returned - |
| func_814093B0 | particle_math | `` | returned - |
| func_8140948C | particle_math | `` | returned - |
| func_81409634 | particle_math | `` | returned - |
| func_814097D8 | particle_math | `` | returned - |
| func_8140DA40 | particle_math | `` | returned 1 |
| func_82100020 | title_screen | `` | returned - |
| func_822014C0 | area_select | `` | returned - |
| func_825057A0 | minigame_select | `` | returned - |
| func_82600C08 | victory_palace | `` | returned - |
| func_82600CFC | victory_palace | `` | returned - |
| func_82600D34 | victory_palace | `` | returned - |
| func_82600D3C | victory_palace | `` | returned - |
| func_82602570 | victory_palace | `` | returned - |
| func_826028CC | victory_palace | `` | returned - |
| func_82900390 | minigame_sample2_viewer | `` | signal14 |
| func_82E00050 | cup_select | `` | returned - |
| func_82F0ECA4 | trade_machine | `` | returned - |
| func_83700090 | gallery_enlarge | `` | returned - |
| func_83A009C0 | snap_print_club_background_select | `` | returned - |
| func_84209CEC | 61 | `` | returned - |
| func_84300E78 | battle_engine | `` | returned - |
| func_84300E80 | battle_engine | `` | returned - |
| func_84301A24 | battle_engine | `` | returned - |
| func_843046BC | battle_engine | `` | returned - |
| func_84307414 | battle_engine | `` | returned - |
| func_8430746C | battle_engine | `` | returned - |
| func_84307474 | battle_engine | `` | returned - |
| func_84307480 | battle_engine | `` | returned - |
| func_8430FC10 | battle_engine | `` | returned - |
| func_8430FC18 | battle_engine | `` | returned - |
| func_8430FC20 | battle_engine | `` | returned - |
| func_84310360 | battle_engine | `` | returned - |
| func_843109EC | battle_engine | `` | returned - |
| func_84311420 | battle_engine | `` | returned - |
| func_84317B38 | battle_engine | `` | returned - |
| func_8431FCC4 | battle_engine | `` | returned - |
| func_84324404 | battle_engine | `` | returned - |
| func_8432440C | battle_engine | `` | returned - |
| func_84326A70 | battle_engine | `` | returned - |
| func_8432A414 | battle_engine | `` | returned - |
| func_8433209C | battle_engine | `` | returned - |
| func_84334B04 | battle_engine | `` | returned - |
| func_84337664 | battle_engine | `` | returned - |
| func_84340AC4 | battle_engine | `` | returned - |
| func_84341830 | battle_engine | `` | returned - |
| func_84341F08 | battle_engine | `` | returned - |
| func_84342304 | battle_engine | `` | returned - |
| func_84344CE8 | battle_engine | `` | returned - |
| func_843452F0 | battle_engine | `` | returned - |
| func_8434539C | battle_engine | `` | returned - |
| func_84345448 | battle_engine | `` | returned - |
| func_84345490 | battle_engine | `` | returned - |
| func_843467B0 | battle_engine | `` | returned - |
| func_84346960 | battle_engine | `` | returned - |
| func_84346B10 | battle_engine | `` | returned - |
| func_84347254 | battle_engine | `` | returned - |
| func_84347374 | battle_engine | `` | returned - |
| func_84347D2C | battle_engine | `` | returned - |
| func_84348C6C | battle_engine | `` | returned - |
| func_84348C78 | battle_engine | `` | returned - |
| func_84349010 | battle_engine | `` | returned - |
| func_84349264 | battle_engine | `` | returned - |
| func_8434933C | battle_engine | `` | returned - |
| func_84349F80 | battle_engine | `` | returned - |
| func_8434A18C | battle_engine | `` | returned - |
| func_8434A6D8 | battle_engine | `` | returned - |
| func_8434A794 | battle_engine | `` | returned - |
| func_8434AC44 | battle_engine | `` | returned - |
| func_8434BA28 | battle_engine | `` | returned - |
| func_8434C548 | battle_engine | `` | returned - |
| func_8434CA48 | battle_engine | `` | returned - |
| func_8434D890 | battle_engine | `` | returned - |
| func_8434E6AC | battle_engine | `` | returned - |
| func_843553B4 | battle_engine | `` | returned - |
| func_843592E8 | battle_engine | `` | returned - |
| func_843592F0 | battle_engine | `` | returned - |
| func_843592F8 | battle_engine | `` | returned - |
| func_84359300 | battle_engine | `` | returned - |
| func_84359308 | battle_engine | `` | returned - |
| func_84359310 | battle_engine | `` | returned - |
| func_84359318 | battle_engine | `` | returned - |
| func_84359320 | battle_engine | `` | returned - |
| func_84359328 | battle_engine | `` | returned - |
| func_84359330 | battle_engine | `` | returned - |
| func_84359338 | battle_engine | `` | returned - |
| func_84359340 | battle_engine | `` | returned - |
| func_8436EED4 | battle_engine | `` | returned - |
| func_8436EEDC | battle_engine | `` | returned - |
| func_8436EEE4 | battle_engine | `` | returned 1 |
| func_8436F6C0 | battle_engine | `` | returned - |
| func_8436F6D0 | battle_engine | `` | returned - |
| func_84370090 | battle_engine | `` | returned - |
| func_84370E70 | battle_engine | `` | returned - |
| func_84370E78 | battle_engine | `` | returned - |
| func_843718DC | battle_engine | `` | returned - |
| func_843767DC | battle_engine | `` | returned - |
| func_843779C0 | battle_engine | `` | returned - |
| func_8437921C | battle_engine | `` | returned - |
| func_84A00630 | gym_leader_castle | `` | returned - |
| func_84B0B054 | battle_prep | `` | returned - |
| func_86000C10 | magikarp_game | `` | returned - |
| func_86100C28 | clefairy_game | `` | returned - |
| func_862002EC | rattata_game | `` | returned - |
| func_86200938 | rattata_game | `` | returned - |
| func_86300F60 | jigglypuff_doodle | `` | returned - |
| func_863015EC | jigglypuff_doodle | `` | returned - |
| func_86806860 | sushi_game | `` | returned - |
| func_8690232C | gallery_camera | `` | returned - |
| func_869043F4 | gallery_camera | `` | returned - |
| func_86904BFC | gallery_camera | `` | returned - |
| func_86B00680 | demo_scenes | `` | returned - |
| func_86B01004 | demo_scenes | `` | returned - |
| func_86B0A81C | demo_scenes | `` | returned - |
| func_86B0B4C8 | demo_scenes | `` | returned - |
| func_86C01748 | ekans_game | `` | returned - |
| func_86C0250C | ekans_game | `` | returned - |
| func_86C027A4 | ekans_game | `` | returned - |
| func_86C027AC | ekans_game | `` | returned - |
| func_86C027B4 | ekans_game | `` | returned - |
| func_86C034F8 | ekans_game | `` | returned - |
| func_86E0296C | metapod_game | `` | returned - |
| func_87900020 | minigame_actor_framework | `` | returned 4303909540 |
| func_87A010C8 | minigame_shared_graphics | `` | returned 4305587392 |
| func_88210DA8 | lab_pc | `` | returned - |
| func_88306FE4 | lab_pc_list | `` | returned - |
| func_883097E4 | lab_pc_list | `` | returned - |
| func_88309808 | lab_pc_list | `` | returned - |
| func_8830FA10 | lab_pc_list | `` | returned - |
| func_8830FA18 | lab_pc_list | `` | returned - |
| func_88600240 | lab_pc_icons | `` | returned - |
| func_88A00020 | pokemon_lab_unused | `` | returned 37 |
| func_8FC00020 | debug_menu_slot77 | `` | returned 128 |
| leoRecal_i | leomecha.c | `` | exit139 |
| main_pool_search | memory_main.c | `` | returned 0 |
| metapodMinigameLoad | metapod_game | `` | signal14 |
| miniActorAllPositionsToZero | minigame_actor_framework | `` | returned - |
| miniActorCubeOverlapCheckUnused | minigame_actor_framework | `` | returned 0 |
| miniActorPosition2ToZero | minigame_actor_framework | `` | returned - |
| miniApplyCameraDebugOffset | minigame_actor_framework | `` | returned - |
| miniChangeActorAnim_alt1 | minigame_actor_framework | `` | returned - |
| miniShowDifficultyText | minigame_actor_framework | `` | exit139 |
| minigameDebuggVectorEditor | minigame_actor_framework | `` | exit139 |
| n_alClose | n_sl.c | `` | returned - |
| n_alResampleParam | n_resample.c | `` | returned 0 |
| sandshrewMinigameLoad | sandshrew_game | `` | signal14 |

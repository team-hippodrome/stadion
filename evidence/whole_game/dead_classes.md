# Unreached code by class

Functions 7490, reached 6763 (90.3%). Unreached 727: proven dead 229, 64DD hardware 83, live 415.

**Coverage of the code the shipped game can run: 6763 of 7261 (93.1%); without the 64DD library: 6763 of 7178 (94.2%).**

Classes (`tools/evidence/dead_classes.py` docstring: the tests and their order):

| class | functions |
|---|---|
| live | 415 |
| referenced by nothing | 113 |
| 64DD hardware (no drive in the port) | 83 |
| reachable only from dead code | 59 |
| data never selects it | 52 |
| PAL TV mode only | 5 |

| subclass | functions |
|---|---|
| data never selects it: Game Boy music commands no cry sequence uses (every cry 1-151 played with the sound engine on: README, Game Boy cries) | 18 |
| data never selects it: battle animation effect slots no selectable effect list names (battle_anim_slots.md) | 34 |
| live: AI decision | 3 |
| live: battle scene / presentation state | 13 |
| live: below other unreached code | 124 |
| live: callback registration | 22 |
| live: hardware (64DD, Controller Pak, Rumble Pak) | 1 |
| live: input | 8 |
| live: menu or flow step (a switch on a selection or state) | 94 |
| live: other state | 95 |
| live: result of a call (a confirmation, a load, a check) | 14 |
| live: session / save / mode | 9 |
| live: table index (which move / effect / animation) | 32 |

## Live, per area

| area | functions | names |
|---|---|---|
| battle_engine | 54 | BattleAI_AllCandidatesResistType, BattleAI_HasOtherCandidateAtLeastScore, BattleAI_ScoreCategorySetReady, BattleAnim_CallbackDescriptor26BurstChild, BattleAnim_CallbackDescriptor26BurstChild_InitParticle, BattleAnim_CallbackDescriptor26BurstEmitter_InitParticle, BattleAnim_CallbackDescriptor50BurstChild, BattleAnim_CallbackDescriptor50BurstChild_InitParticle, BattleAnim_CallbackDescriptor50BurstEmitter_InitParticle, BattleAnim_DefaultEndEffect_Draw, BattleAnim_DefaultStartEffect_Draw, BattleAnim_DrawAsset13FrameDiv2I4Quad, BattleAnim_DrawAsset13FrameI4YScaledQuadB, BattleAnim_DrawAsset18FrameI4ModelQuad, BattleAnim_DrawAsset1BI4TriangleB, BattleAnim_DrawAsset1DRotatedI4Quad, BattleAnim_DrawAsset87AnimatedI4Quad, BattleAnim_DrawAsset95Ia8Billboard, BattleAnim_DrawAssetA9I4XYScaledQuad, BattleAnim_DrawParticleNoop, BattleAnim_EndEffect16_NoOpDraw, BattleAnim_MarkParticleInactive, BattleAnim_MotionUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_MotionUnusedRotatingSparkVariant, BattleAnim_SetupOwnerModelTintPaletteApproach, BattleAnim_SetupOwnerModelTintPaletteApproachRampDown, BattleAnim_SetupUnregisteredStraightDirectionalStreamFade, BattleAnim_SetupUnusedAimedSparkVariant, BattleAnim_SetupUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_SetupUnusedGlitterVariant, BattleAnim_SetupUnusedGrowingSparkVariant, BattleAnim_SetupUnusedIndexedFadeVariantA, BattleAnim_SetupUnusedIndexedFadeVariantB, BattleAnim_SetupUnusedRiseImpulseVariant, BattleAnim_SetupUnusedRotatingSparkVariant, BattleAnim_SetupUnusedVariant2, BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset, BattleAnim_SpawnBatchedParticles, BattleAnim_UnusedResetAppearanceAndAdvance, BattleScene_CompleteSendOutFollowup, BattleScene_FinalPresentationFrameActiveOwner, BattleScene_FinalPresentationFrameOwner0, BattleScene_GetOpposingSelectedSideTeamSize, BattleScene_IntroCameraStep12Or14, BattleScene_IntroCameraWideHoldStep, BattleScene_IntroStepNop, BattleScene_PhaseCheckFaintedEndTurn, BattleScene_PhaseSendOutAfterFaint, BattleScene_SetCameraSendOutShot, BattleScene_UpdatePausedFrame, func_84321D34, func_843297E8, func_843297F0, func_843453E4 |
| particle_math | 26 | ParticleGfx_BuildTransformMatrixXYScaled, ParticleGfx_DrawBillboardOffsetQuad, ParticleGfx_DrawBillboardPrim, ParticleGfx_DrawBillboardPrimEnvQuadVariantB, ParticleGfx_DrawBillboardPrimEnvQuadVariantC, ParticleGfx_DrawTransformedQuadXYScaled, ParticleGfx_LoadTextureCallbackI4ParticleFrame_32x64, ParticleGfx_LoadTextureCallbackIa8Frame_32x64, ParticleGfx_LoadTextureRgba16WrapT, ParticleGfx_LoadTextureTileIa16, ParticleGfx_PushTransformMatrixXYScaled, ParticleGfx_RecolorPrimEnvFromPaletteSetB, ParticleGfx_RecolorPrimRandomPalette, ParticleMath_AngleXBetweenPoints, ParticleMath_BitwiseAndNot, ParticleMath_BitwiseXor, ParticleMath_RandomDirection, ParticleMath_RotateVec3fXY, ParticleMath_ScaleDirectionToLength, Particle_AddVelocityFromRotationTemplate2, Particle_AddVelocityFromRotationTemplate7B, Particle_AllocChain, Particle_Field38_AddXZFromField94YOffsetMinus90_0901C, Particle_Field7C_ScaleVec_0B14C, Particle_Field7C_SetVecFromField74AnglesXZ_0B330, Particle_SetVelocityRandomDirection |
| player.c | 19 | Audio_ApplyVoiceDetuneOnce, Audio_ApplyVoiceFxMixCommand, Audio_ApplyVoicePanCommand, Audio_ClearVoiceFlagED, Audio_ClearVoiceFlagEE, Audio_ClearVoiceSubEnvelopePointer, Audio_CountActiveSoundsByCategory, Audio_DisableVoicePeriodicPitchModulation, Audio_DisableVoicePitchSlide, Audio_EnableVoiceVariableDuration, Audio_EnableVoiceVibratoDown, Audio_PortDumpChannels, Audio_SetRandomVoiceVolume, Audio_SetVoiceEnvelopePointers, Audio_SetVoiceFlagED, Audio_SetVoiceFlagEE, Audio_SetVoiceSubEnvelopePointer, Audio_SkipVoiceCommandByte, Audio_SkipVoiceCommandWord |
| gallery_camera | 16 | GalleryCamera_ClampAtToBoundaryCircle, GalleryCamera_ClampEyeToPanBounds, GalleryCamera_ClampHeight, GalleryCamera_ClampToBoundaryCircle, GalleryCamera_Distance3D, GalleryCamera_OrbitAtPoint, GalleryCamera_RotateVecByMatrix, Gallery_CameraExitState_CountDown, Gallery_CameraExitState_ExpandBox, Gallery_CameraExitState_FadeOut, Gallery_CameraExitState_SpinOut, Gallery_DrawHelpBalloonFrame, Gallery_FinalizeBackgroundLoad, Gallery_LoadSurfingPikachuIcon, Gallery_SwitchBackgroundSet, func_86900A14 |
| 33FE0.c | 15 | Model_AccumulateVertexOffset, Model_ApplyTransformCommands, Model_ApplyVertexTransforms, Model_CopyPositionBuffer, Model_ExtrapolateVertexPosition, Model_GetVertexMatrixClass, Model_GetVertexMergeClass, Model_GetVertexSourceClass, Model_GetVertexTargetClass, Model_OffsetVertexHeights, func_80033568, func_80034348, func_80034824, func_80034BD4, func_80035E2C |
| trade_machine | 15 | TradeEvo_DrawEvolutionCaption, TradeEvo_UpdateApplyEvolution, TradeEvo_UpdateFlashIn, TradeEvo_UpdateShowCaption, Trade_ApplyEvolution, Trade_DrawSaveSeqMessageBackdrop, Trade_LoadPickBufferIcon, Trade_NicknameIsSpeciesDefault, Trade_UpdateBoxMachineFlowConfirmDone, Trade_UpdateBoxMachineFlowConfirmWait, Trade_UpdateBoxMachineFlowFrameMove, Trade_UpdateBoxMachineFlowFrameReturn, Trade_UpdateBoxSlotFadeOut, Trade_UpdateBoxSlotShrink, Trade_UpdatePickScreenPageSwipe |
| gallery_rental_viewer | 13 | Gallery_RentalViewerAdvanceClosePanel, Gallery_RentalViewerBoxPickerMoveDown, Gallery_RentalViewerBoxPickerMoveLeft, Gallery_RentalViewerBoxPickerMoveUp, Gallery_RentalViewerCancelSelection, Gallery_RentalViewerCloseBoxPicker, Gallery_RentalViewerMoveCursorLeft, Gallery_RentalViewerMoveCursorRight, Gallery_RentalViewerMoveCursorUp, Gallery_RentalViewerNextBox, Gallery_RentalViewerPrevBox, Gallery_RentalViewerUpdateBusyConfirm, Gallery_RentalViewerUpdateLoadConfirm |
| rsp.c | 13 | Storage_GetSramHandle, Storage_ReadDisk, Storage_ReadDiskId, Storage_ReadRtc, Storage_ReadSram, Storage_ResetDisk, Storage_SeekDisk, Storage_SetDiskMotor, Storage_WaitForIoMessage, Storage_WriteDisk, Storage_WriteRom, Storage_WriteRtc, Storage_WriteSram |
| battle_prep | 12 | BattlePrepBadgeAward_ScatterStep, BattlePrepRoster_DrawIconVariantB, BattlePrepRoster_LoadDexIdsDouble, BattlePrepRoster_PositionIconsDouble, BattlePrepStarBurst_DrawTrailIcon, BattlePrepStarBurst_UpdateExitSlide, BattlePrepStarBurst_UpdateFirstClearExit, BattlePrep_AdjustBannerTextureOffset, BattlePrep_FadeInBallBurstObjects, BattlePrep_FadeInBallBurstTransition, BattlePrep_ResetAllDisplayObjects, BattlePrep_TrophyModelIdle |
| lab_pc_list | 11 | BattleInfoWidget_HandleInput, BattleInfoWidget_SetLevelRangeState, BattleInfoWidget_SetMoveDestinationState, BattleInfoWidget_SetMovePanelState, BattleInfoWidget_SetTypePanelState, BattleInfoWidget_SetWidgetState, BattleInfoWidget_SyncParentSelection, LabPCList_ClearMovesFrom, LabPCList_FilterByLevelRange, LabPCList_SetRowMenuState, LabPCList_WriteDeckCategory |
| audio_commands_category2.c | 10 | Audio_PlayCategory12SoundCommand, Audio_StopCategory2SoundCommand, Audio_StopCategory3SoundCommand, Audio_StopCategory4SoundCommand, Audio_StopCategory5SoundCommand, Audio_StopCategory6SoundCommand, Audio_StopCategory7SoundCommand, Audio_StopCategory8SoundCommand, Audio_StopCategory9SoundCommand, Audio_StopCategoryASoundCommand |
| crash_screen.c | 10 | crash_screen_copy_to_buf, crash_screen_draw, crash_screen_draw_glyph, crash_screen_draw_rect, crash_screen_get_faulted_thread, crash_screen_print_fpcsr, crash_screen_print_fpr, crash_screen_printf, crash_screen_sleep, crash_screen_wait_for_button_combo |
| victory_palace | 9 | VictoryPalace_DrawFrameBorder, VictoryPalace_HideOverlays, VictoryPalace_HideSpeciesGrid, VictoryPalace_IsCurrentSpeciesOwnedByPlayer, VictoryPalace_LoadCurrentCarouselIcon, VictoryPalace_ShowSpeciesGrid, VictoryPalace_ShowSpeciesInfo, VictoryPalace_StringsEqual, func_82600C00 |
| minigame_actor_framework | 8 | MiniFx_ApplyEmitterToParticle, MiniFx_EmitParticles, MiniFx_FreeEmitter, MiniFx_TryEmit, miniDrawResultIcons, miniTutoScreenShow, showDebuggCameraInfo, showDebuggJoystickInfo |
| profiler.c | 8 | draw_profiler, draw_profiler_bar_cpu, draw_profiler_bar_cpu_keep_max, draw_profiler_mode_0, draw_profiler_mode_1, draw_profiler_rect, draw_reference_profiler_bars, print_profiler_metrics |
| gb_tower.c | 7 | GbPak_IsCartOff, GbPak_IsCartOn, GbPak_RawWrite, GbPak_SelectRomBank, GbTower_PowerOffAllPaks, GbTower_ReinitPak, GbTower_VerifyPakStillInserted |
| stage_loader.c | 7 | Profiler_SetDisplayModes, TextRenderer_ClearQueuedStrings, TextRenderer_DrawGlyphLarge, TextRenderer_DrawGlyphSmall, TextRenderer_DrawString, TextRenderer_RestoreState, TextRenderer_SetupGlyphState |
| geo_layout.c | 6 | geo_layout_cmd_create_clear_depth, geo_layout_cmd_create_cull_distance, geo_layout_cmd_create_group, geo_layout_cmd_jump, geo_layout_cmd_noop, geo_layout_cmd_skip |
| geo_render.c | 6 | Geo_NodeClearDepth, Geo_NodeCullDistance, Geo_NodeGroup, Geo_NodeType12Empty, Geo_NodeType3Pass, Geo_NodeType9Empty |
| jpeg_stream.c | 6 | Asset_CopyUncompressed, BinArchive_LoadFileBuffered, Storage_LoadAssetByLbaRange, Storage_PrefetchLbaRange, Storage_SetByteRange, Storage_SetLbaRange |
| options | 6 | Options_ApplySelection, Options_ApplySoundSettings, Options_ConfirmPrompt, Options_DrawEraseCompleteMessage, Options_EraseSaveData, Options_ShowEraseCompleteScreen |
| widget_tree | 6 | WidgetTree_DrawHorizontalFrame, WidgetTree_GetAnimatedPanelInputState, WidgetTree_HandleHorizontalMenuInput, WidgetTree_HandleVerticalMenuInput, WidgetTree_SetHorizontalMenuState, WidgetTree_UpdateHorizontalMenuItems |
| dma.c | 5 | Storage_QueueDiskIdRead, Storage_QueueDiskReset, Storage_QueueDiskTransfer, Storage_QueueRomWrite, Storage_QueueRtcRead |
| dp_intro.c | 5 | DisplayWorker_DrainEvents, Display_DisableBlackout, Display_EnableBlackout, Display_GetWorkerStatus, Display_IsCurrentFramebuffer |
| matrix.c | 5 | Color_AdjustRGBA8, Color_HSVToRGBA8, Color_RGBA8ToHSV, PackedBits_ReadSigned, Vec3f_Subtract |
| battle_order_select | 4 | BattleMenu_DrawOrderSelectPanel_LevelCapNotice, BattleMenu_DrawOrderSelectPanel_MonDetail, BattleMenu_HandleOrderSelectPanel_LevelCapNoticeInput, BattleMenu_HandleOrderSelectPanel_MonDetailInput |
| demo_scenes | 4 | Completion_DrawSpeciesRewardText, Completion_ResetDialogText, Completion_UpdateGiftDialogWithExisting, UI_DrawTextureRgba16 |
| display_object_textures.c | 4 | BinArchive_LoadFileFromLbaRange, Fragment_LoadFromLbaRange, Jpeg_AllocAndDecodeFragment, PokeIcon_WaitBackgroundLoad |
| gallery_album | 4 | Gallery_AlbumAdvanceDeleteAnimation, Gallery_AlbumComputeEnlargeMotion, Gallery_AlbumConfirmAndExit, func_83501718 |
| gallery_backgrounds | 4 | Gallery_ClearPhotoRecord, Gallery_DrawSaveOptions, Gallery_DrawYesNoOptions, Gallery_SwapPhotoRecords |
| gb_tower_emulator | 4 | GbEmu_PixelOpAverage555, GbEmu_PixelOpDitherB, GbPak_VerifyProbeBlock, func_81203C58 |
| lab_pc_icons | 4 | LabPC_DrawArrowLabelA, LabPC_DrawMoveDetailWidget, LabPC_GetDetailPanelMode, LabPC_ShowMoveDetailPage |
| pokedex | 4 | Pokedex_DrawMapPinIcon, Pokedex_FindPrevMapArea, Pokedex_GetCountTier, Pokedex_ResetCameraEase |
| rental_rules | 4 | RentalRules_AdvancePageNode, RentalRules_QualifyingSpeciesWindowUpdateScrolling, RentalRules_ScrollPageDown, RentalRules_ScrollPageUp |
| gallery_snap | 3 | Gallery_SnapResetToMenu, Gallery_SnapUpdatePasteConfirm, Gallery_SnapUpdateSaveConfirm |
| game_state.c | 3 | Game_ResetStorageDevice, Game_ShutdownAndLoadFragment, State_N64ddBootUnused |
| geo_node.c | 3 | GeoNode_CreateClearDepth, GeoNode_CreateCullDistance, GeoNode_CreateGroup |
| lab_pc | 3 | LabItem_BoxScreen_SetMenuState, LabItem_ConfirmDialog_SetSelectedIndex, LabPC_BoxScreen_HandleInput |
| lab_ui_support | 3 | LabUI_HideSavingStatusWidget, LabUI_SetSavingStatusMessage, LabUI_ShowSavingStatusWidget |
| minigame_select | 3 | KidsClub_AdvanceRoundBannerClose, KidsClub_DrawWavyText, MiniGameSelect_UpdateCursor |
| particle_data_library | 3 | DisplayList_BuildIndexedTextureSegment, DisplayList_BuildTwoSegmentChain, func_81002830 |
| rental_team_selection | 3 | TeamSelection_RegisteredTeam_ApplySelection, TeamSelection_RulePrompt_DrawBinaryLabel, TeamSelection_TrainerSlots_HandleRulePrompt |
| reset.c | 3 | SoftReset_ClearLines320, SoftReset_ClearLines640, func_8000559C |
| save_data.c | 3 | Save_RestoreRecord, Save_SetOptionsField1E, Save_WriteRecord |
| scheduler.c | 3 | Sched_HandlePreNMI, Sched_RequestTaskYield, Sched_TryYieldActiveTask |
| controller_ram.c | 2 | Controller_ReadAccessoryStatus, System_DelayMilliseconds |
| gym_leader_castle_clear | 2 | Glc_DrawScisRectangle, Glc_DrawTrackingIcon |
| memmap.c | 2 | Memmap_GetLoadedFragmentVaddr, Memmap_GetSegmentVaddrMask |
| port_audio.h | 2 | PortA_BE16, PortA_BE32 |
| session.c | 2 | Team_IsOpponentIndexUsed, Team_PickRandomOpponentIndex |
| snap_print_club_print_seals | 2 | Snap_PrintClubDrawSealMargin, Snap_PrintClubWaitMs |
| util.c | 2 | Util_DrawMemProfiler, Util_DrawRect |
| DDC0.c | 1 | Audio_FadeOutForShutdown |
| area_select | 1 | AreaSelect_UpdateMewtwoExit |
| audio_category_fade.c | 1 | Audio_FadeCategoriesAndStreamsWithDuration |
| audio_sfx_wrapper.c | 1 | Audio_PlaySoundEffectByIdWrapper |
| audio_sound_state.c | 1 | Audio_CountCategorySounds |
| bytetolba.c | 1 | LeoByteToLBA |
| debug_menu_slot68 | 1 | DebugMenu_ForwardSlot68 |
| debug_menu_slot71 | 1 | DebugMenu_ForwardSlot71 |
| debug_menu_slot76 | 1 | func_8FB00020 |
| driverominit.c | 1 | osDriveRomInit |
| ekans_game | 1 | EkansGame_GetPeakStickMagnitude |
| event_battle | 1 | EventBattle_DrawSpinnerArrows |
| free_battle_rules | 1 | func_84001AD8 |
| gb_save.c | 1 | Ui_SendNullMessage |
| gym_leader_castle | 1 | Glc_DrawBackgroundCrossFade |
| jpegutils.c | 1 | JpegUtils_SetHuffmanTableOld |
| lbatobyte.c | 1 | LeoLBAToByte |
| memory_main.c | 1 | main_pool_get_pool |
| metapod_game | 1 | MetapodGame_LockPlayerInputOnPress |
| model_animation.c | 1 | ModelAnim_InterpolateKeyframeTangent |
| model_animation_events.c | 1 | ModelAnim_IsEventTrackDone |
| model_renderer.c | 1 | Model_ComputeSizeVariantFromRecord |
| n_reverb.c | 1 | n_alFxParamHdl |
| n_syndelete.c | 1 | n_alSynDelete |
| n_synsetfxmix.c | 1 | n_alSynSetFXMix |
| poke_icon.c | 1 | PokeIcon_AdjustImageColorBuffer |
| pokemon_stats.c | 1 | GbSave_PollCartRemoval |
| ranking_debug_test | 1 | miniUnkFadeOutLoop |
| rattata_game | 1 | RattataGame_AdvanceFinishLineApproach |
| readdiskid.c | 1 | LeoReadDiskID |
| readrtc.c | 1 | LeoReadRTC |
| spdlmotor.c | 1 | LeoSpdlMotor |
| transfer_pak_check | 1 | TransferPak_ConfirmPrompt |
| vector_math.c | 1 | Mtx_BuildDirectionRotation |

## Proven dead, per area

| area | functions | names |
|---|---|---|
| battle_engine | 96 | BattleAI_InitTrainerRecords, BattleAnim_CallbackDescriptor25AlphaHoldDetach, BattleAnim_CallbackDescriptor25PaletteFadeIn, BattleAnim_CallbackDescriptor25PaletteFadeOut, BattleAnim_CallbackEndEffect28Descriptor10UpwardBurstFade, BattleAnim_CallbackEndEffect28Descriptor10WideScatterBurstFade, BattleAnim_CallbackEndEffect49Model33Anim34WaitCleanup, BattleAnim_CallbackOrphanDescriptor2BSpinFade, BattleAnim_CallbackOrphanDescriptor2EScatterFade, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach_InitParticle, BattleAnim_CallbackOwnerModelTintHoldFadeDetach, BattleAnim_CallbackOwnerModelTintRandomPaletteCycle, BattleAnim_ClearOwnerSecondaryStatusFlags, BattleAnim_EndEffect19RibbonMesh_Draw, BattleAnim_EndEffect19RibbonMesh_Init, BattleAnim_EndEffect19RibbonMesh_Update, BattleAnim_EndEffectUnusedVerticalFadeAndDelayedFrameSequence, BattleAnim_GetCameraLookYaw, BattleAnim_GetCameraStoredAngles70, BattleAnim_GetCameraStoredAngles76, BattleAnim_GetCameraStoredYaw72, BattleAnim_GetCameraStoredYaw78, BattleAnim_GetSpeciesBySideIndex, BattleAnim_GetTertiaryOwnerSpecies, BattleAnim_InitSecondarySwayOffset, BattleAnim_InitializeParticleChain, BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence, BattleAnim_OrphanDescriptor34Emitter, BattleAnim_SecondaryStatusCaseB, BattleAnim_SecondaryStatusCaseBSub0, BattleAnim_SecondaryStatusCaseBSub1, BattleAnim_SecondaryStatusDefault, BattleAnim_SetupDescriptor25AlphaHoldDetach, BattleAnim_SetupDescriptor25PaletteFadeIn, BattleAnim_SetupDescriptor25PaletteFadeOut, BattleAnim_SetupEndEffect28Descriptor10UpwardBurstFade, BattleAnim_SetupEndEffect28Descriptor10WideScatterBurstFade, BattleAnim_SetupOrphanDescriptor2BSpinFade, BattleAnim_SetupOrphanDescriptor2EScatterFade, BattleAnim_SetupOwnerModelTintRandomPaletteCycle, BattleAnim_SpawnDescriptor25AlphaFadeInHoldRepeating, BattleAnim_SpawnDescriptor25AlphaHoldDetach, BattleAnim_SpawnDescriptor25AlphaHoldDetachFullAlpha, BattleAnim_SpawnDescriptor25AlphaHoldDetachRepeating, BattleAnim_SpawnDescriptor25AlphaHoldFadeOutRepeating, BattleAnim_SpawnDescriptor25PaletteFadeIn, BattleAnim_SpawnDescriptor25PaletteFadeOut, BattleAnim_SpawnOwnerModelAlphaFadeFullPreset, BattleAnim_SpawnOwnerModelTintHoldFadeDetach, BattleAnim_SpawnOwnerModelTintPaletteCycleVariant, BattleAnim_SpawnOwnerModelTintRandomPaletteCycle, BattleAnim_StartEffect122GlobalAndOwnerTintFade, BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup, BattleAnim_StartEffect77DoubleTextureTrail_Draw, BattleAnim_StartEffect77DoubleTextureTrail_Init, BattleAnim_StartEffect77DoubleTextureTrail_Seed, BattleAnim_StartEffect77DoubleTextureTrail_Update, BattleAnim_TriggerStatusAfflictionCry, BattleAnim_UpdateOwnerSecondaryStatusAnimation, BattleAnim_UpdateSecondarySwayPhysics, BattleHud_DrawForfeitConfirmDialog, BattleHud_DrawMessageBox, BattleScene_AdvanceCameraSubstepCounter, BattleScene_ApproachCameraRollNoEyeUpdate, BattleScene_GetCombinedTeamSize, BattleScene_IsCameraFrameCounterAtLeast, BattleScene_ReloadMoveResourceList, BattleScene_ResetOpponentStateFrameCounter, BattleScene_SetCameraEyeAndStoreAnglesFromAt, BattleScene_StateRow56EnterNop, BattleScene_StateRow5Slot3Nop, BattleScene_StateRow5Slot4Nop, BattleScene_StateRow5Slot5Nop, BattleScene_StateRow5Slot6Nop, BattleScene_StateRow6Slot3Nop, BattleScene_StateRow6Slot4Nop, BattleScene_StateRow6Slot5Nop, BattleScene_StateRow6Slot6Nop, BattleScene_StateRow7Slot4Nop, BattleScene_StateRow7Slot5Nop, BattleScene_TickClearAnimRecordIfLoaded, BattleScene_TickRow10CleanupAndEnterState18, BattleScene_TickRow20CleanupAndEnterState18, Battle_IsPartyMemberAlive, Battle_QueueStatusText, Radial20_VariantB, Radial20_VariantE, func_84317940, func_8433765C, func_843378CC, func_8433BF54, func_8433E33C, func_84345B28, func_8434C590, func_8434C5F0 |
| particle_math | 32 | ParticleGfx_BuildBillboardMatrixYScaled, ParticleGfx_BuildDirectionMatrix, ParticleGfx_DrawBillboardDefaultQuad, ParticleGfx_DrawBillboardPrimEnvRotatedTriangle, ParticleGfx_DrawModelQuadYScaled, ParticleGfx_DrawModelTriangle, ParticleGfx_DrawModelTriangleRotated, ParticleGfx_LoadTextureI4WrapS, ParticleGfx_LoadTextureI8WrapST, ParticleGfx_LoadTextureRgba16WrapS, ParticleGfx_LoadTextureRgba16WrapST, ParticleGfx_PushBillboardMatrixYScaled, ParticleGfx_PushDirectionMatrix, ParticleGfx_PushTransformMatrixWithHalfTurnXY, ParticleMath_AngleZBetweenPoints, ParticleMath_Atan2XY, ParticleMath_Atan2YZ, ParticleMath_DistanceScaled, ParticleMath_DotVec3f, ParticleMath_IsWithinRadius2D, ParticleMath_ScaleApproachStep, ParticleMath_Vec3ToAngles, Particle_ApproachPosition, Particle_CountActive, Particle_CountActiveAlias, Particle_EmitBurstAtTransformWithMetadata, Particle_EmitBurstFromObjectWithMetadata, Particle_Field38_AddVecFromVec3f_08DD8, Particle_Field7C_SetVecScaled_0AE84, Particle_Field94_SubVecFromVec3s_0A600, Particle_Field9A_SetVecFromVec3s_0A37C, func_81409708 |
| gb_audio.c | 20 | GbMusic_AdvancePitchSlide, GbMusic_CmdCall, GbMusic_CmdEnableEnvelope, GbMusic_CmdLoop, GbMusic_CmdPitchSlide, GbMusic_CmdPlayNote, GbMusic_CmdPlaySequence, GbMusic_CmdSetEnvelope, GbMusic_CmdSetMasterVolume, GbMusic_CmdSetNoteLength, GbMusic_CmdSetOctave, GbMusic_CmdSetPanMask, GbMusic_CmdSetTempo, GbMusic_CmdToggleFinePitch, GbMusic_GetNoteFrequency, GbMusic_StartMusic, GbMusic_StartPitchSlide, GbMusic_StartSequenceById, GbMusic_TriggerChannelNote, GbMusic_TriggerNoteFromByte |
| gallery_debugger | 12 | GalleryDebugger_DrawFrame, GalleryDebugger_DrawMenu, GalleryDebugger_HandleMenuInput, GalleryDebugger_LoadControllerPak, GalleryDebugger_LoadDeckEntries, GalleryDebugger_Main, GalleryDebugger_MenuLoop, GalleryDebugger_PollInput, GalleryDebugger_SaveControllerPak, GalleryDebugger_SaveDeckEntries, GalleryDebugger_UpdateFadeState, func_83900020 |
| gb_save.c | 7 | Color_InterpolateFromGray, Ui_AdjustHorizontalSelection, Ui_AdjustSteppedRangeSelection, Ui_AdjustSteppedScrollSelection, Ui_AdjustVerticalSelection, Ui_HandleHorizontalSelectionInput, Ui_HandleVerticalSelectionInput |
| battle_prep | 6 | BattlePrepBadgeAward_StartScatter, BattlePrepRoster_ZoomStepPAL, BattlePrepTeamIcons_HpDrainStepDoublePAL, BattlePrepTeamIcons_HpDrainStepSinglePAL, BattlePrep_CarouselStepFinalPAL, BattlePrep_CarouselStepPAL |
| minigame_actor_framework | 6 | MiniFx_AllocEmitter, MiniFx_ConfigureEmitter, MiniFx_ConfigureEmitterWithPeriodAndRepeat, MiniFx_ConfigureOneShotEmitter, miniShowDifficultyText, minigameDebuggVectorEditor |
| minigame_sample2_viewer | 6 | Minigame2SampleViewer_DrawBackgroundTiles, Minigame2SampleViewer_DrawFrame, Minigame2SampleViewer_Init, Minigame2SampleViewer_Loop, func_82900020, func_82900390 |
| 33FE0.c | 5 | Math_CrossProduct, func_800334C0, func_80033D1C, func_80033D44, func_80034254 |
| jpeg_stream.c | 5 | Asset_LoadToSegmentByLbaRange, BinArchive_Free, BinArchive_LoadByLbaRange, Dma_ReadChunks, Fragment_LoadByLbaRange |
| widget_tree | 5 | WidgetTree_DrawAnimatedFrameVariantB, WidgetTree_FreeTree, WidgetTree_InitAnimatedFrameVariantB, WidgetTree_SetAnimatedFrameVariantBState, WidgetTree_UpdateAnimatedFrameVariantB |
| dma.c | 4 | Storage_QueueDiskMotorOff, Storage_QueueDiskSeek, Storage_QueueRtcWrite, Storage_QueueSramTransfer |
| graphics_textures.c | 4 | Font_DisableLineBreak, Font_EnableLineBreak, Gfx_DrawTextureI16, Gfx_DrawTextureI8 |
| demo_scenes | 3 | Completion_AbortDialogToIdle, Vec3f_Distance, func_86B09F64 |
| 33E30.c | 1 | Gfx_DrawTriangleMarker |
| crash_screen.c | 1 | crash_screen_printf_with_bg |
| display_object_textures.c | 1 | Model_LoadWithScale |
| gallery_backgrounds | 1 | Gallery_DrawRoundedFrame |
| gallery_camera | 1 | GalleryCamera_ResetFreeView |
| game_state.c | 1 | Trainer_GetSlots |
| gb_tower_emulator | 1 | func_81208E4C |
| gb_tower_unused | 1 | Minigame2SampleViewer_PollInput |
| geo_node.c | 1 | GraphNode_RemoveChild |
| geo_render.c | 1 | Geo_ProcessCallbacks |
| leomecha.c | 1 | leoRecal_i |
| minigame_shared_graphics | 1 | MiniGfx_DrawVertexColoredQuad |
| model_renderer.c | 1 | PokeIcon_PollFrameLoadForObject |
| poke_icon.c | 1 | PokeIcon_RedrawSlot |
| profiler.c | 1 | set_get_time_diff |
| save_data.c | 1 | Save_EnsureBank3Loaded |
| stage_loader.c | 1 | TextRenderer_QueuePersistentString |
| vector_math.c | 1 | Math_ProjectPointOntoLine |

# Unreached code by class

Functions 7490, reached 7005 (93.5%). Unreached 485: proven dead 365, 64DD hardware 79, live 41.

**Coverage of the code the shipped game can run: 7005 of 7125 (98.3%); without the 64DD library: 7005 of 7046 (99.4%).**

Classes (`tools/evidence/dead_classes.py` docstring: the tests and their order):

| class | functions |
|---|---|
| never selected (data or code proof) | 208 |
| referenced by nothing | 105 |
| 64DD hardware (no drive in the port) | 79 |
| reachable only from dead code | 47 |
| live | 41 |
| PAL TV mode only | 5 |

| subclass | functions |
|---|---|
| live: AI decision | 2 |
| live: below other unreached code | 2 |
| live: callback registration | 12 |
| live: hardware (64DD, Controller Pak, Rumble Pak) | 1 |
| live: menu or flow step (a switch on a selection or state) | 4 |
| live: other state | 14 |
| live: result of a call (a confirmation, a load, a check) | 2 |
| live: session / save / mode | 4 |
| never selected (data or code proof): Badge-award state 4 is set only by `BattlePrepBadgeAward_StartScatter`, which nothing references. (routes/engine_left/README.md, "Proven dead") | 1 |
| never selected (data or code proof): Blit modes 2 and 1. Every `GbEmu_BlitWithPixelOp` call passes 0 or 0xFFFF. (routes/engine_left/README.md, "Proven dead") | 2 |
| never selected (data or code proof): Dead by code (`../minigames_gallery/README.md`): the banner timer is reset to 300 before the test, and the cursor callback runs only for active nodes. (routes/engine_left/README.md, "Proven dead") | 2 |
| never selected (data or code proof): Game Boy music commands no cry sequence uses (every cry 1-151 played with the sound engine on: README, Game Boy cries) | 18 |
| never selected (data or code proof): Needs `assetIsBusy & 0x20`. The only writers of `assetIsBusy` set 0x80 (`battle_engine_315D50.c`), 0x40 and 0x10 (`model_renderer.c`), or 0 (`session.c`). (routes/engine_left/README.md, "Proven dead") | 1 |
| never selected (data or code proof): Needs `unk_5DC4 != 0`. The flag is set to 1 only inside the branch it guards: `GbPak_ProbeCartridge` after the check, and `GbPak_VerifyWritableProbeBlock`, whic... (routes/engine_left/README.md, "Proven dead") | 1 |
| never selected (data or code proof): No C reference. The ROM words equal to their N64 addresses (0x80001304, 0x80031910, 0x82600C00) are unaligned, or (0x10c9d78) inside the compressed `pokemon_mod... (routes/engine_left/README.md, "Proven dead") | 1 |
| never selected (data or code proof): No minigame sets `miniTutoScreenState` to 5 (`../minigames_gallery/README.md`). (routes/engine_left/README.md, "Proven dead") | 2 |
| never selected (data or code proof): Only archive flag 0x80 selects them. No US archive header sets it: the headers at 0x820000, 0x920000, 0x8CC000, 0x898000 (trainers), 0x783760 (text), 0x56E7D0 (... (routes/engine_left/README.md, "Proven dead") | 2 |
| never selected (data or code proof): Only for `PRESJPEG` files in `Fragment_LoadFromRomRange`. That loader gets the model archives 0x920000 (214 PERS-SZP files and one raw file) and 0x8CC000 (63 PE... (routes/engine_left/README.md, "Proven dead") | 1 |
| never selected (data or code proof): The only `GbPak_ReadRom` calls read the header at 0x120, which is in bank 0 (`../menus_trade/README.md`). (routes/engine_left/README.md, "Proven dead") | 2 |
| never selected (data or code proof): Trophy state 1 is never set: the writers set 0, 2 (grow) and 3 (shrink). The function body is empty. (routes/engine_left/README.md, "Proven dead") | 1 |
| never selected (data or code proof): battle animation effect slots no selectable effect list names (battle_anim_slots.md) | 34 |
| never selected (data or code proof): callers reached only by dead_calls.md's forced calls, or no aligned ROM word with the address (particle_descriptors.md, other areas) | 13 |
| never selected (data or code proof): geo layout commands no shipped layout uses, and the node types only they create (geo_commands.md) | 15 |
| never selected (data or code proof): libmus API commands nothing queues: command 5 only Audio_QueueVoicePan writes (nothing calls it), command 6 no function writes (audio_commands.md) | 2 |
| never selected (data or code proof): libmus voice commands no shipped song or sound effect uses (every song and FX bank the game's loaders reach, 76 songs and 331 FX banks, decoded along every path: audio_commands.md, routes/audio_seq/decode_seq.py) | 15 |
| never selected (data or code proof): n_alFxParamHdl: nothing in the C code refers to it; the one ROM word equal to its address (ROM 0xa6e778) lies inside the compressed bytes of the Yay0 blob at ROM 0xa54408-0xa7b4a5 (audio_commands.md) | 1 |
| never selected (data or code proof): n_alSynSetFXMix runs only for a voice whose reverb_intensity is not 0, which only command 6 (Audio_ApplyVoiceFxMixCommand) sets (audio_commands.md) | 1 |
| never selected (data or code proof): particle descriptors no code selects, callbacks only dead callers register, and call sites no shipped value reaches (particle_descriptors.md) | 79 |
| never selected (data or code proof): slot-model segment types and vertex classes the two built-in templates never produce (geo_commands.md, 33FE0.c) | 14 |

## Live, per area

| area | functions | names |
|---|---|---|
| lab_pc_list | 8 | BattleInfoWidget_HandleInput, BattleInfoWidget_SetLevelRangeState, BattleInfoWidget_SetMoveDestinationState, BattleInfoWidget_SetMovePanelState, BattleInfoWidget_SetTypePanelState, BattleInfoWidget_SetWidgetState, BattleInfoWidget_SyncParentSelection, LabPCList_SetRowMenuState |
| battle_engine | 4 | BattleAI_AllCandidatesResistType, BattleAI_ScoreCategorySetReady, BattleAnim_MarkParticleInactive, func_843453E4 |
| lab_pc | 3 | LabItem_BoxScreen_SetMenuState, LabItem_ConfirmDialog_SetSelectedIndex, LabPC_BoxScreen_HandleInput |
| rental_team_selection | 3 | TeamSelection_RegisteredTeam_ApplySelection, TeamSelection_RulePrompt_DrawBinaryLabel, TeamSelection_TrainerSlots_HandleRulePrompt |
| reset.c | 2 | SoftReset_ClearLines640, func_8000559C |
| scheduler.c | 2 | Sched_RequestTaskYield, Sched_TryYieldActiveTask |
| trade_machine | 2 | Trade_DrawSaveSeqMessageBackdrop, Trade_UpdateBoxSlotShrink |
| widget_tree | 2 | WidgetTree_GetAnimatedPanelInputState, WidgetTree_HandleVerticalMenuInput |
| controller_ram.c | 1 | System_DelayMilliseconds |
| dp_intro.c | 1 | DisplayWorker_DrainEvents |
| driverominit.c | 1 | osDriveRomInit |
| ekans_game | 1 | EkansGame_GetPeakStickMagnitude |
| event_battle | 1 | EventBattle_DrawSpinnerArrows |
| free_battle_rules | 1 | func_84001AD8 |
| gallery_backgrounds | 1 | Gallery_GetCommonUiString |
| gb_tower_emulator | 1 | func_81203C58 |
| lab_pc_icons | 1 | LabPC_GetDetailPanelMode |
| metapod_game | 1 | MetapodGame_LockPlayerInputOnPress |
| model_animation.c | 1 | ModelAnim_InterpolateKeyframeTangent |
| poke_icon.c | 1 | PokeIcon_AdjustImageColorBuffer |
| rattata_game | 1 | RattataGame_AdvanceFinishLineApproach |
| save_data.c | 1 | Save_SetOptionsField1E |
| stage_loader.c | 1 | TextRenderer_ClearQueuedStrings |

## Proven dead, per area

| area | functions | names |
|---|---|---|
| battle_engine | 144 | BattleAI_InitTrainerRecords, BattleAnim_CallbackDescriptor25AlphaHoldDetach, BattleAnim_CallbackDescriptor25PaletteFadeIn, BattleAnim_CallbackDescriptor25PaletteFadeOut, BattleAnim_CallbackDescriptor26BurstChild, BattleAnim_CallbackDescriptor26BurstChild_InitParticle, BattleAnim_CallbackDescriptor26BurstEmitter_InitParticle, BattleAnim_CallbackDescriptor50BurstChild, BattleAnim_CallbackDescriptor50BurstChild_InitParticle, BattleAnim_CallbackDescriptor50BurstEmitter_InitParticle, BattleAnim_CallbackEndEffect28Descriptor10UpwardBurstFade, BattleAnim_CallbackEndEffect28Descriptor10WideScatterBurstFade, BattleAnim_CallbackEndEffect49Model33Anim34WaitCleanup, BattleAnim_CallbackOrphanDescriptor2BSpinFade, BattleAnim_CallbackOrphanDescriptor2EScatterFade, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach_InitParticle, BattleAnim_CallbackOwnerModelTintHoldFadeDetach, BattleAnim_CallbackOwnerModelTintRandomPaletteCycle, BattleAnim_ClearOwnerSecondaryStatusFlags, BattleAnim_DefaultEndEffect_Draw, BattleAnim_DefaultStartEffect_Draw, BattleAnim_DrawAsset13FrameDiv2I4Quad, BattleAnim_DrawAsset13FrameI4YScaledQuadB, BattleAnim_DrawAsset18FrameI4ModelQuad, BattleAnim_DrawAsset1BI4TriangleB, BattleAnim_DrawAsset1DRotatedI4Quad, BattleAnim_DrawAsset87AnimatedI4Quad, BattleAnim_DrawAsset95Ia8Billboard, BattleAnim_DrawAssetA9I4XYScaledQuad, BattleAnim_DrawParticleNoop, BattleAnim_EndEffect16_NoOpDraw, BattleAnim_EndEffect19RibbonMesh_Draw, BattleAnim_EndEffect19RibbonMesh_Init, BattleAnim_EndEffect19RibbonMesh_Update, BattleAnim_EndEffectUnusedVerticalFadeAndDelayedFrameSequence, BattleAnim_GetCameraLookYaw, BattleAnim_GetCameraStoredAngles70, BattleAnim_GetCameraStoredAngles76, BattleAnim_GetCameraStoredYaw72, BattleAnim_GetCameraStoredYaw78, BattleAnim_GetSpeciesBySideIndex, BattleAnim_GetTertiaryOwnerSpecies, BattleAnim_InitSecondarySwayOffset, BattleAnim_InitializeParticleChain, BattleAnim_MotionUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_MotionUnusedRotatingSparkVariant, BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence, BattleAnim_OrphanDescriptor34Emitter, BattleAnim_SecondaryStatusCaseB, BattleAnim_SecondaryStatusCaseBSub0, BattleAnim_SecondaryStatusCaseBSub1, BattleAnim_SecondaryStatusDefault, BattleAnim_SetupDescriptor25AlphaHoldDetach, BattleAnim_SetupDescriptor25PaletteFadeIn, BattleAnim_SetupDescriptor25PaletteFadeOut, BattleAnim_SetupEndEffect28Descriptor10UpwardBurstFade, BattleAnim_SetupEndEffect28Descriptor10WideScatterBurstFade, BattleAnim_SetupOrphanDescriptor2BSpinFade, BattleAnim_SetupOrphanDescriptor2EScatterFade, BattleAnim_SetupOwnerModelTintPaletteApproach, BattleAnim_SetupOwnerModelTintPaletteApproachRampDown, BattleAnim_SetupOwnerModelTintRandomPaletteCycle, BattleAnim_SetupUnregisteredStraightDirectionalStreamFade, BattleAnim_SetupUnusedAimedSparkVariant, BattleAnim_SetupUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_SetupUnusedGlitterVariant, BattleAnim_SetupUnusedGrowingSparkVariant, BattleAnim_SetupUnusedIndexedFadeVariantA, BattleAnim_SetupUnusedIndexedFadeVariantB, BattleAnim_SetupUnusedRiseImpulseVariant, BattleAnim_SetupUnusedRotatingSparkVariant, BattleAnim_SetupUnusedVariant2, BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset, BattleAnim_SpawnBatchedParticles, BattleAnim_SpawnDescriptor25AlphaFadeInHoldRepeating, BattleAnim_SpawnDescriptor25AlphaHoldDetach, BattleAnim_SpawnDescriptor25AlphaHoldDetachFullAlpha, BattleAnim_SpawnDescriptor25AlphaHoldDetachRepeating, BattleAnim_SpawnDescriptor25AlphaHoldFadeOutRepeating, BattleAnim_SpawnDescriptor25PaletteFadeIn, BattleAnim_SpawnDescriptor25PaletteFadeOut, BattleAnim_SpawnOwnerModelAlphaFadeFullPreset, BattleAnim_SpawnOwnerModelTintHoldFadeDetach, BattleAnim_SpawnOwnerModelTintPaletteCycleVariant, BattleAnim_SpawnOwnerModelTintRandomPaletteCycle, BattleAnim_StartEffect122GlobalAndOwnerTintFade, BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup, BattleAnim_StartEffect77DoubleTextureTrail_Draw, BattleAnim_StartEffect77DoubleTextureTrail_Init, BattleAnim_StartEffect77DoubleTextureTrail_Seed, BattleAnim_StartEffect77DoubleTextureTrail_Update, BattleAnim_TriggerStatusAfflictionCry, BattleAnim_UnusedResetAppearanceAndAdvance, BattleAnim_UpdateOwnerSecondaryStatusAnimation, BattleAnim_UpdateSecondarySwayPhysics, BattleHud_DrawForfeitConfirmDialog, BattleHud_DrawMessageBox, BattleScene_AdvanceCameraSubstepCounter, BattleScene_ApproachCameraRollNoEyeUpdate, BattleScene_CompleteSendOutFollowup, BattleScene_FinalPresentationFrameActiveOwner, BattleScene_FinalPresentationFrameOwner0, BattleScene_GetCombinedTeamSize, BattleScene_IntroCameraStep12Or14, BattleScene_IntroCameraWideHoldStep, BattleScene_IntroStepNop, BattleScene_IsCameraFrameCounterAtLeast, BattleScene_PhaseCheckFaintedEndTurn, BattleScene_PhaseSendOutAfterFaint, BattleScene_ReloadMoveResourceList, BattleScene_ResetOpponentStateFrameCounter, BattleScene_SetCameraEyeAndStoreAnglesFromAt, BattleScene_SetCameraSendOutShot, BattleScene_StateRow56EnterNop, BattleScene_StateRow5Slot3Nop, BattleScene_StateRow5Slot4Nop, BattleScene_StateRow5Slot5Nop, BattleScene_StateRow5Slot6Nop, BattleScene_StateRow6Slot3Nop, BattleScene_StateRow6Slot4Nop, BattleScene_StateRow6Slot5Nop, BattleScene_StateRow6Slot6Nop, BattleScene_StateRow7Slot4Nop, BattleScene_StateRow7Slot5Nop, BattleScene_TickClearAnimRecordIfLoaded, BattleScene_TickRow10CleanupAndEnterState18, BattleScene_TickRow20CleanupAndEnterState18, BattleScene_UpdatePausedFrame, Battle_IsPartyMemberAlive, Battle_QueueStatusText, Radial20_VariantB, Radial20_VariantE, func_84317940, func_84321D34, func_843297E8, func_843297F0, func_8433765C, func_843378CC, func_8433BF54, func_8433E33C, func_84345B28, func_8434C590, func_8434C5F0 |
| particle_math | 57 | ParticleGfx_BuildBillboardMatrixYScaled, ParticleGfx_BuildDirectionMatrix, ParticleGfx_BuildTransformMatrixXYScaled, ParticleGfx_DrawBillboardDefaultQuad, ParticleGfx_DrawBillboardOffsetQuad, ParticleGfx_DrawBillboardPrim, ParticleGfx_DrawBillboardPrimEnvQuadVariantB, ParticleGfx_DrawBillboardPrimEnvQuadVariantC, ParticleGfx_DrawBillboardPrimEnvRotatedTriangle, ParticleGfx_DrawModelQuadYScaled, ParticleGfx_DrawModelTriangle, ParticleGfx_DrawModelTriangleRotated, ParticleGfx_DrawTransformedQuadXYScaled, ParticleGfx_LoadTextureCallbackI4ParticleFrame_32x64, ParticleGfx_LoadTextureCallbackIa8Frame_32x64, ParticleGfx_LoadTextureI4WrapS, ParticleGfx_LoadTextureI8WrapST, ParticleGfx_LoadTextureRgba16WrapS, ParticleGfx_LoadTextureRgba16WrapST, ParticleGfx_LoadTextureRgba16WrapT, ParticleGfx_LoadTextureTileIa16, ParticleGfx_PushBillboardMatrixYScaled, ParticleGfx_PushDirectionMatrix, ParticleGfx_PushTransformMatrixWithHalfTurnXY, ParticleGfx_PushTransformMatrixXYScaled, ParticleGfx_RecolorPrimEnvFromPaletteSetB, ParticleGfx_RecolorPrimRandomPalette, ParticleMath_AngleXBetweenPoints, ParticleMath_AngleZBetweenPoints, ParticleMath_Atan2XY, ParticleMath_Atan2YZ, ParticleMath_BitwiseAndNot, ParticleMath_BitwiseXor, ParticleMath_DistanceScaled, ParticleMath_DotVec3f, ParticleMath_IsWithinRadius2D, ParticleMath_RandomDirection, ParticleMath_RotateVec3fXY, ParticleMath_ScaleApproachStep, ParticleMath_ScaleDirectionToLength, ParticleMath_Vec3ToAngles, Particle_AddVelocityFromRotationTemplate2, Particle_AddVelocityFromRotationTemplate7B, Particle_AllocChain, Particle_ApproachPosition, Particle_CountActive, Particle_CountActiveAlias, Particle_EmitBurstAtTransformWithMetadata, Particle_EmitBurstFromObjectWithMetadata, Particle_Field38_AddVecFromVec3f_08DD8, Particle_Field7C_ScaleVec_0B14C, Particle_Field7C_SetVecFromField74AnglesXZ_0B330, Particle_Field7C_SetVecScaled_0AE84, Particle_Field94_SubVecFromVec3s_0A600, Particle_Field9A_SetVecFromVec3s_0A37C, Particle_SetVelocityRandomDirection, func_81409708 |
| gb_audio.c | 20 | GbMusic_AdvancePitchSlide, GbMusic_CmdCall, GbMusic_CmdEnableEnvelope, GbMusic_CmdLoop, GbMusic_CmdPitchSlide, GbMusic_CmdPlayNote, GbMusic_CmdPlaySequence, GbMusic_CmdSetEnvelope, GbMusic_CmdSetMasterVolume, GbMusic_CmdSetNoteLength, GbMusic_CmdSetOctave, GbMusic_CmdSetPanMask, GbMusic_CmdSetTempo, GbMusic_CmdToggleFinePitch, GbMusic_GetNoteFrequency, GbMusic_StartMusic, GbMusic_StartPitchSlide, GbMusic_StartSequenceById, GbMusic_TriggerChannelNote, GbMusic_TriggerNoteFromByte |
| 33FE0.c | 19 | Math_CrossProduct, Model_AccumulateVertexOffset, Model_ApplyTransformCommands, Model_ApplyVertexTransforms, Model_CopyPositionBuffer, Model_ExtrapolateVertexPosition, Model_GetVertexMatrixClass, Model_GetVertexMergeClass, Model_GetVertexSourceClass, Model_GetVertexTargetClass, Model_OffsetVertexHeights, func_800334C0, func_80033568, func_80033D1C, func_80033D44, func_80034254, func_80034348, func_80034824, func_80034BD4 |
| player.c | 17 | Audio_ApplyVoiceDetuneOnce, Audio_ApplyVoiceFxMixCommand, Audio_ApplyVoicePanCommand, Audio_ClearVoiceFlagED, Audio_ClearVoiceFlagEE, Audio_ClearVoiceSubEnvelopePointer, Audio_DisableVoicePeriodicPitchModulation, Audio_DisableVoicePitchSlide, Audio_EnableVoiceVariableDuration, Audio_EnableVoiceVibratoDown, Audio_SetRandomVoiceVolume, Audio_SetVoiceEnvelopePointers, Audio_SetVoiceFlagED, Audio_SetVoiceFlagEE, Audio_SetVoiceSubEnvelopePointer, Audio_SkipVoiceCommandByte, Audio_SkipVoiceCommandWord |
| minigame_actor_framework | 11 | MiniFx_AllocEmitter, MiniFx_ApplyEmitterToParticle, MiniFx_ConfigureEmitter, MiniFx_ConfigureEmitterWithPeriodAndRepeat, MiniFx_ConfigureOneShotEmitter, MiniFx_EmitParticles, MiniFx_FreeEmitter, MiniFx_TryEmit, miniDrawResultIcons, miniShowDifficultyText, miniTutoScreenShow |
| widget_tree | 9 | WidgetTree_DrawAnimatedFrameVariantB, WidgetTree_DrawHorizontalFrame, WidgetTree_FreeTree, WidgetTree_HandleHorizontalMenuInput, WidgetTree_InitAnimatedFrameVariantB, WidgetTree_SetAnimatedFrameVariantBState, WidgetTree_SetHorizontalMenuState, WidgetTree_UpdateAnimatedFrameVariantB, WidgetTree_UpdateHorizontalMenuItems |
| battle_prep | 8 | BattlePrepBadgeAward_ScatterStep, BattlePrepBadgeAward_StartScatter, BattlePrepRoster_ZoomStepPAL, BattlePrepTeamIcons_HpDrainStepDoublePAL, BattlePrepTeamIcons_HpDrainStepSinglePAL, BattlePrep_CarouselStepFinalPAL, BattlePrep_CarouselStepPAL, BattlePrep_TrophyModelIdle |
| gb_save.c | 7 | Color_InterpolateFromGray, Ui_AdjustHorizontalSelection, Ui_AdjustSteppedRangeSelection, Ui_AdjustSteppedScrollSelection, Ui_AdjustVerticalSelection, Ui_HandleHorizontalSelectionInput, Ui_HandleVerticalSelectionInput |
| geo_render.c | 7 | Geo_NodeClearDepth, Geo_NodeCullDistance, Geo_NodeGroup, Geo_NodeType12Empty, Geo_NodeType3Pass, Geo_NodeType9Empty, Geo_ProcessCallbacks |
| dma.c | 6 | Storage_QueueDiskIdRead, Storage_QueueDiskMotorOff, Storage_QueueDiskReset, Storage_QueueDiskSeek, Storage_QueueRtcRead, Storage_QueueRtcWrite |
| geo_layout.c | 6 | geo_layout_cmd_create_clear_depth, geo_layout_cmd_create_cull_distance, geo_layout_cmd_create_group, geo_layout_cmd_jump, geo_layout_cmd_noop, geo_layout_cmd_skip |
| minigame_sample2_viewer | 6 | Minigame2SampleViewer_DrawBackgroundTiles, Minigame2SampleViewer_DrawFrame, Minigame2SampleViewer_Init, Minigame2SampleViewer_Loop, func_82900020, func_82900390 |
| display_object_textures.c | 5 | BinArchive_LoadFileFromLbaRange, Fragment_LoadFromLbaRange, Jpeg_AllocAndDecodeFragment, Model_LoadWithScale, PokeIcon_WaitBackgroundLoad |
| gb_tower_emulator | 4 | GbEmu_PixelOpAverage555, GbEmu_PixelOpDitherB, GbPak_VerifyProbeBlock, func_81208E4C |
| geo_node.c | 4 | GeoNode_CreateClearDepth, GeoNode_CreateCullDistance, GeoNode_CreateGroup, GraphNode_RemoveChild |
| graphics_textures.c | 4 | Font_DisableLineBreak, Font_EnableLineBreak, Gfx_DrawTextureI16, Gfx_DrawTextureI8 |
| demo_scenes | 3 | Completion_AbortDialogToIdle, Vec3f_Distance, func_86B09F64 |
| gallery_backgrounds | 2 | Gallery_CopyCommonUiString, Gallery_DrawRoundedFrame |
| gb_tower.c | 2 | GbPak_RawWrite, GbPak_SelectRomBank |
| minigame_select | 2 | KidsClub_DrawWavyText, MiniGameSelect_UpdateCursor |
| particle_data_library | 2 | DisplayList_BuildIndexedTextureSegment, DisplayList_BuildTwoSegmentChain |
| save_data.c | 2 | Save_EnsureBank3Loaded, Save_RestoreRecord |
| vector_math.c | 2 | Math_ProjectPointOntoLine, Mtx_BuildDirectionRotation |
| 33E30.c | 1 | Gfx_DrawTriangleMarker |
| gallery_camera | 1 | GalleryCamera_ResetFreeView |
| game_state.c | 1 | Trainer_GetSlots |
| gb_tower_unused | 1 | Minigame2SampleViewer_PollInput |
| jpegutils.c | 1 | JpegUtils_SetHuffmanTableOld |
| leomecha.c | 1 | leoRecal_i |
| matrix.c | 1 | Vec3f_Subtract |
| memmap.c | 1 | Memmap_GetSegmentVaddrMask |
| minigame_shared_graphics | 1 | MiniGfx_DrawVertexColoredQuad |
| model_animation_events.c | 1 | ModelAnim_IsEventTrackDone |
| model_renderer.c | 1 | PokeIcon_PollFrameLoadForObject |
| n_reverb.c | 1 | n_alFxParamHdl |
| n_synsetfxmix.c | 1 | n_alSynSetFXMix |
| poke_icon.c | 1 | PokeIcon_RedrawSlot |
| stage_loader.c | 1 | TextRenderer_QueuePersistentString |
| victory_palace | 1 | func_82600C00 |

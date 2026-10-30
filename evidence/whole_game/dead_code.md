# Functions nothing references

Checked: 1017. With a C reference (caller, address taken or pointer table): 421. Referenced only by a raw address word in the ROM: 18. **No reference at all: 578.** Not in the port's function table: 0.

## No reference at all, per area

| area | functions | names |
|---|---|---|
| particle_math | 178 | ParticleGfx_ApproachEnvPalette, ParticleGfx_DrawBillboardDefaultQuad, ParticleGfx_DrawBillboardPrimEnvRotatedTriangle, ParticleGfx_DrawI4TextureAtlasTiled, ParticleGfx_DrawI8TextureAtlas, ParticleGfx_DrawIa16TextureAtlas, ParticleGfx_DrawIa8TextureAtlas, ParticleGfx_DrawModelQuadYScaled, ParticleGfx_DrawModelTriangle, ParticleGfx_LoadTextureCallbackI4TileFrameDiv2Mod8_24x24, ParticleGfx_LoadTextureCallbackRgba16_32x32, ParticleGfx_LoadTextureCi8, ParticleGfx_LoadTextureCi8WrapSWithMask, ParticleGfx_LoadTextureDualRgba16I8, ParticleGfx_LoadTextureI4ToList, ParticleGfx_LoadTextureI4WrapS, ParticleGfx_LoadTextureI8WrapST, ParticleGfx_LoadTextureIa4, ParticleGfx_LoadTextureRgba16Dual, ParticleGfx_LoadTextureRgba16WrapS, ParticleGfx_LoadTextureRgba16WrapST, ParticleGfx_LoadTextureRgba16WrapSWithMask, ParticleGfx_LoadTextureRgba32, ParticleGfx_LoadTextureTileCi8, ParticleGfx_LoadTextureTileI8Alias, ParticleGfx_LoadTextureTileIa4, ParticleGfx_LoadTextureTileIa8, ParticleGfx_LoadTextureTileRgba16, ParticleGfx_LoadTextureTileRgba32, ParticleGfx_NoopStub1, ParticleGfx_NoopStub10, ParticleGfx_NoopStub2, ParticleGfx_NoopStub3, ParticleGfx_NoopStub4, ParticleGfx_NoopStub5, ParticleGfx_NoopStub6, ParticleGfx_NoopStub7, ParticleGfx_NoopStub8, ParticleGfx_NoopStub9, ParticleGfx_PushDirectionMatrix, ParticleGfx_PushTransformMatrixWithHalfTurnXY, ParticleGfx_RecolorPrimEnvFromPaletteSetA, ParticleGfx_RecolorPrimFromPaletteSetA, ParticleGfx_SetTextureState, ParticleMath_AddVec3s, ParticleMath_AdvanceRandomOffset, ParticleMath_AngleZBetweenPoints, ParticleMath_ApproachAngleCoarse, ParticleMath_ApproachFloatDown, ParticleMath_ApproachFloatDownByPointer, ParticleMath_ApproachFloatUp, ParticleMath_ApproachFloatUpByPointer, ParticleMath_ApproachS16Down, ParticleMath_ApproachS16DownByPointer, ParticleMath_ApproachS16Up, ParticleMath_ApproachS16UpByPointer, ParticleMath_ApproachS32, ParticleMath_ApproachS32Down, ParticleMath_ApproachS32DownByPointer, ParticleMath_ApproachS32Up, ParticleMath_ApproachS32UpByPointer, ParticleMath_ApproachS8, ParticleMath_ApproachU16Down, ParticleMath_ApproachU16Up, ParticleMath_Atan2XY, ParticleMath_Atan2YZ, ParticleMath_BitwiseOr, ParticleMath_BounceFloat, ParticleMath_BounceS16, ParticleMath_DistanceScaled, ParticleMath_DotVec3f, ParticleMath_HasBits, ParticleMath_IsWithinRadius2D, ParticleMath_LacksBits, ParticleMath_NoopStub, ParticleMath_Rotate2DX, ParticleMath_Rotate2DY, ParticleMath_ScaleVec3f, ParticleMath_Vec3ToAngles, Particle_AddPositionFromRotationTemplate2, Particle_AddRotationRandomSignedXWithFixedYQuarterTurn, Particle_AddRotationRandomSignedXY, Particle_AddRotationRandomSignedXYWithXHalfTurn, Particle_AddRotationRandomSignedY, Particle_AddRotationRandomSignedYWithHalfTurn, Particle_AddVelocityFromRotationTemplate4, Particle_AddVelocityFromRotationTemplate6, Particle_AddVelocityFromRotationTemplate7A, Particle_AddVelocityXYFromRotationZViaImpulse, Particle_AddVelocityXZFromRotationY, Particle_AnimateFramesPingPongAndAdvanceLifecycle, Particle_AnimateFramesReverseAndAdvanceLifecycle, Particle_AnimateFramesReverseFirstPingPongAndAdvanceLifecycle, Particle_ApproachPosition, Particle_ApproachRotationTargetXY, Particle_ApproachRotationTargetXYZ, Particle_ApproachRotationYTo, Particle_ComputeVectorFromRotationXY, Particle_ComputeVectorFromRotationXZ, Particle_CountActiveAlias, Particle_DisableFlag40, Particle_EmitBurstAtTransformWithMetadata, Particle_EmitBurstFromObjectWithMetadata, Particle_EnableFlag40, Particle_Field38_AddVecFromVec3f_08DD8, Particle_Field38_AddXScaled_08E70, Particle_Field38_AddZScaled_08EB0, Particle_Field38_SetVec_08C68, Particle_Field50_AddVecScaled_09968, Particle_Field50_AddXFromField7C_09C58, Particle_Field50_AddXScaled_099C0, Particle_Field50_AddZFromField7C_09C80, Particle_Field50_AddZScaled_09A00, Particle_Field50_AdvanceField7CWithTimer_0B854, Particle_Field50_ApproachXToField5C_09AD8, Particle_Field50_ApproachZToField5C_09B58, Particle_Field50_SetVecFromField7C_09B98, Particle_Field50_SetXScaled_098E8, Particle_Field50_SetXYFromField7C_09BC8, Particle_Field50_SetZScaled_09918, Particle_Field50_SubVecScaled_09A20, Particle_Field50_SubXScaled_09A78, Particle_Field50_SubYScaled_09A98, Particle_Field50_SubZScaled_09AB8, Particle_Field5C_ApproachY_0A0C4, Particle_Field5C_SetXScaled_0A07C, Particle_Field5C_SetZScaled_0A0AC, Particle_Field7C_DecrementXUntilAtMost_0B050, Particle_Field7C_DecrementZUntilAtMost_0B0F8, Particle_Field7C_IncrementXUntilAtLeast_0AF54, Particle_Field7C_IncrementYUntilAtLeast_0AFA8, Particle_Field7C_IncrementZUntilAtLeast_0AFFC, Particle_Field7C_SetVecFromField74AnglesYZVariant_0B700, Particle_Field7C_SetVecScaled_0AE84, Particle_Field7C_SetXFromField74AngleY_0B1EC, Particle_Field7C_SetXScaled_0AF0C, Particle_Field7C_SetYFromField74AngleX_0B1C4, Particle_Field7C_SetZFromField74AngleY_0B214, Particle_Field7C_SetZScaled_0AF3C, Particle_Field94_AddConstantAll_0A2E0, Particle_Field94_AddVecFromField9A_0A480, Particle_Field94_AddXConstant_0A30C, Particle_Field94_AddYConstant_0A320, Particle_Field94_DecrementYTo_0A810, Particle_Field94_DecrementZTo_0A844, Particle_Field94_SubVecFromVec3s_0A600, Particle_Field94_SubX_0A63C, Particle_Field94_SubY_0A658, Particle_Field94_SubZ_0A674, Particle_Field9A_SetVecFromVec3s_0A37C, Particle_FieldA6_DecrementToStep1_0BD80, Particle_FieldA6_DecrementToZeroAndStepState_0BD34, Particle_FieldA6_DecrementToZero_0BD08, Particle_FieldA6_GetS16_0BCBC, Particle_FieldA8_DecrementToZeroStep1AndStepState_0BE8C, Particle_FieldA8_GetS16_0BE28, Particle_FieldAE_GetS16_0C050, Particle_HasFlag40, Particle_SetFieldOffsetA6IfNotSentinel, Particle_SetFieldOffsetA8IfNotSentinel, Particle_SetFieldOffsetAAIfNotSentinel, Particle_SetFieldOffsetACIfNotSentinel, Particle_SetFieldOffsetAEIfNotSentinel, Particle_SetFieldOffsetB0IfNotSentinel, Particle_SetFieldOffsetCDIfNotSentinel, Particle_SetFieldOffsetCEIfNotSentinel, Particle_SetFieldOffsetCFIfNotSentinel, Particle_SetPositionFromRotationTemplate2, Particle_SetPrimAlphaIfNotSentinel, Particle_SetRotationTargetFromCurrent, Particle_ToggleFlags, func_81409288, func_81409308, func_814093B0, func_8140948C, func_81409634, func_814097D8, func_8140DA40 |
| battle_engine | 142 | BattleAI_InitTrainerRecords, BattleAnim_ApproachS16, BattleAnim_ApproachU16, BattleAnim_Atan2sFromPoints, BattleAnim_CallbackDescriptor26BurstEmitter, BattleAnim_CallbackDescriptor50BurstEmitter, BattleAnim_CallbackOwnerModelTintPaletteApproach, BattleAnim_CallbackOwnerModelTintPaletteApproachRampDown, BattleAnim_CallbackUnregisteredStraightDirectionalStreamFade, BattleAnim_CallbackUnusedAimedSparkVariant, BattleAnim_CallbackUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_CallbackUnusedFallingTrailVariant, BattleAnim_CallbackUnusedGenericFadeApproach, BattleAnim_CallbackUnusedGlitterVariant, BattleAnim_CallbackUnusedGrowFadeVariant, BattleAnim_CallbackUnusedGrowingSparkVariant, BattleAnim_CallbackUnusedIndexedFadeVariantA, BattleAnim_CallbackUnusedIndexedFadeVariantB, BattleAnim_CallbackUnusedRiseImpulseVariant, BattleAnim_CallbackUnusedRotatingSparkVariant, BattleAnim_CallbackUnusedVariant2, BattleAnim_DrawStoredColorOverlay, BattleAnim_EndEffectUnusedVerticalFadeAndDelayedFrameSequence, BattleAnim_GetCameraLookYaw, BattleAnim_GetCameraStoredAngles70, BattleAnim_GetCameraStoredAngles76, BattleAnim_GetCameraStoredYaw72, BattleAnim_GetCameraStoredYaw78, BattleAnim_GetEffectStateFlag, BattleAnim_GetOwnerModelBoundsScaled, BattleAnim_GetSecondaryOwnerSpeciesBaseScale, BattleAnim_GetSpeciesBySideIndex, BattleAnim_GetTertiaryOwnerSpecies, BattleAnim_GetTertiaryOwnerSpeciesBaseScale, BattleAnim_InitializeParticleChain, BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence, BattleAnim_OrphanDescriptor34Emitter, BattleAnim_QueueEffectList5, BattleAnim_QueueEffectList6Alt, BattleAnim_RenderNop1, BattleAnim_RenderNop2, BattleAnim_SetOwnerSecondaryStatusFlags, BattleAnim_SpawnDescriptor25AlphaFadeInHoldRepeating, BattleAnim_SpawnDescriptor25AlphaHoldDetach, BattleAnim_SpawnDescriptor25AlphaHoldDetachFullAlpha, BattleAnim_SpawnDescriptor25AlphaHoldDetachRepeating, BattleAnim_SpawnDescriptor25AlphaHoldFadeOutRepeating, BattleAnim_SpawnDescriptor25PaletteFadeIn, BattleAnim_SpawnDescriptor25PaletteFadeOut, BattleAnim_SpawnOwnerModelAlphaFadeFullPreset, BattleAnim_SpawnOwnerModelTintHoldFadeDetach, BattleAnim_SpawnOwnerModelTintPaletteCycleVariant, BattleAnim_SpawnOwnerModelTintRandomPaletteCycle, BattleAnim_TriggerStatusAfflictionCry, BattleAnim_UpdateOwnerSecondaryStatusAnimation, BattleAnim_Vec3fOffsetByPolarXZ, BattleAnim_Vec3fScaleComponents, BattleAnim_Vec3fSetZero, BattleHud_DrawForfeitConfirmDialog, BattleHud_DrawMessageBox, BattleScene_AdvanceCameraSubstepCounter, BattleScene_ApproachCameraRollNoEyeUpdate, BattleScene_ClearParticipantOwnerActiveFlag, BattleScene_ComputeParticipantModelOffset, BattleScene_GetCombinedTeamSize, BattleScene_IntroInitNop, BattleScene_IsCameraAngleSettled, BattleScene_IsCameraFrameCounterAtLeast, BattleScene_IsParticipantModelVisible, BattleScene_ReloadMoveResourceList, BattleScene_ResetOpponentStateFrameCounter, BattleScene_SetCameraEyeAndStoreAnglesFromAt, BattleScene_SetParticipantModelScale, BattleScene_SetParticipantOwnerActiveFlag, BattleScene_TickClearAnimRecordIfLoaded, Battle_CheckSideDefeatAfterResidualStatus, Battle_ComputeSplineWeightsVariantA, Battle_ComputeSplineWeightsVariantB, Battle_ComputeSplineWeightsVariantC, Battle_ComputeSplineWeightsVariantD, Battle_ComputeSplineWeightsVariantE, Battle_GetScaleRatio, Battle_GetTertiaryOwnerBonePositionUnused, Battle_IsPartyMemberAlive, Battle_QueueStatusText, Battle_SetActiveMoveFromSlotIndex, Battle_SetModelToStatusAnim, Radial20_VariantB, Radial20_VariantE, func_84300E78, func_84300E80, func_84301A24, func_843046BC, func_84307414, func_8430746C, func_84307474, func_84307480, func_8430FC10, func_8430FC18, func_8430FC20, func_84310360, func_843109EC, func_84311420, func_84317B38, func_8431FCC4, func_84324404, func_8432440C, func_84326A70, func_8432A414, func_8433209C, func_84334B04, func_84337664, func_84340AC4, func_84341830, func_84345490, func_84348C6C, func_84348C78, func_843553B4, func_843592E8, func_843592F0, func_843592F8, func_84359300, func_84359308, func_84359310, func_84359318, func_84359320, func_84359328, func_84359330, func_84359338, func_84359340, func_8436EED4, func_8436EEDC, func_8436EEE4, func_8436F6C0, func_8436F6D0, func_84370090, func_84370E70, func_84370E78, func_843718DC, func_843767DC, func_843779C0, func_8437921C |
| minigame_actor_framework | 13 | MiniFx_ConfigureEmitterWithPeriodAndRepeat, MiniFx_ConfigureOneShotEmitter, MiniFx_GraphNodeBuildColorRampList, MinigameGetVec3Distance_2d, MinigameGetVec3Distance_3d, func_87900020, miniActorAllPositionsToZero, miniActorCubeOverlapCheckUnused, miniActorPosition2ToZero, miniApplyCameraDebugOffset, miniChangeActorAnim_alt1, miniShowDifficultyText, minigameDebuggVectorEditor |
| demo_scenes | 12 | Completion_AbortDialogToIdle, MtxF_PackToFixed, MtxF_ScaleUpperRows, MtxF_TransposeScaled, Vec3f_Distance, Vec3f_Zero, Vec3s_Copy, func_86B00680, func_86B01004, func_86B09F64, func_86B0A81C, func_86B0B4C8 |
| matrix.c | 12 | MtxF_SetRotationTranslation, MtxF_SetTranslation, MtxF_TransformVec3s, Vec3f_Add, Vec3f_SetComponents, Vec3f_SubtractInPlace, Vec3f_TriangleNormal, Vec3s_Add, Vec3s_AddInPlace, Vec3s_FromVec3f, Vec3s_Subtract, Vec3s_SubtractInPlace |
| pokemon_stats.c | 11 | GbSave_CopyPortSnapshot, GbSave_GetPortAvailability, GbSave_GetSecondaryName, GbSave_HasProgressFlag1000, GbSave_SetPlayerName, GbSave_SetSecondaryName, GbSave_SetTrainerId, func_80022A88, func_80022A98, func_80022AA8, func_80022ACC |
| gb_tower_emulator | 10 | GbApu_AllocZeroed, GbApu_ClearActiveFlag, GbApu_SetActiveFlag, GbApu_SetNoisePeriodBounds, func_81207330, func_81207354, func_81207690, func_81207C5C_Empty, func_81208E4C, func_81209688 |
| widget_tree | 8 | WidgetTree_AppendHorizontalMenuItem, WidgetTree_FreeChildren, WidgetTree_FreeTree, WidgetTree_InitAnimatedFrameVariantB, WidgetTree_InitHorizontalFrame, WidgetTree_InitHorizontalMenu, WidgetTree_InitTextMessageBuffer, WidgetTree_PrependChild |
| jpeg_stream.c | 7 | Asset_LoadToSegmentByLbaRange, BinArchive_Free, BinArchive_GetCachedFile, BinArchive_LoadByLbaRange, Dma_CopyChunks, Dma_ReadChunks, Fragment_LoadByLbaRange |
| 33FE0.c | 7 | func_800333E0, func_80033410, func_800338B0, func_80033D1C, func_80033D44, func_80034254, func_80035208_empty |
| table_view.c | 6 | ByteMatrix_AppendRow, ByteMatrix_CopyRow, ByteMatrix_CopyRowAt, ByteMatrix_Free, PointerList_Free, Text_CompareU16Terminated |
| gb_save.c | 6 | Color_InterpolateFromGray, Ui_AdjustSteppedRangeSelection, Ui_AdjustSteppedScrollSelection, Ui_GetSelectionDirection, Ui_HandleHorizontalSelectionInput, Ui_HandleVerticalSelectionInput |
| graphics_textures.c | 6 | Font_DisableLineBreak, Font_EnableLineBreak, Font_GetSetInfo, Font_UnloadSizes, Gfx_DrawTextureI16, Gfx_DrawTextureI8 |
| victory_palace | 6 | func_82600C08, func_82600CFC, func_82600D34, func_82600D3C, func_82602570, func_826028CC |
| ekans_game | 6 | func_86C01748, func_86C0250C, func_86C027A4, func_86C027AC, func_86C027B4, func_86C034F8 |
| DDC0.c | 5 | Audio_ResetCurrentMusic, func_8000D1C0, func_8000D1C8, func_8000D1D0, func_8000D1D8 |
| save_data.c | 5 | Deck_GetSaveEntryNameWrapper, Save_CommitAllBankRecords, Save_EnsureBank3Loaded, Save_RestoreTypedRecord, func_8002667C |
| gallery_camera | 5 | GalleryCamera_DebugAdjustZoomOffset, GalleryCamera_ResetFreeView, func_8690232C, func_869043F4, func_86904BFC |
| leo.c | 5 | Leo_AbortPendingBootRequest, Leo_InitializeBootState, Leo_ResetBootStateIfReady, Leo_RunBootStateMachine, func_8000E5B4 |
| lab_pc_list | 5 | func_88306FE4, func_883097E4, func_88309808, func_8830FA10, func_8830FA18 |
| particle_data_library | 4 | DisplayList_InitConditionalSegment, DisplayList_InitIndexedTextureSegment, DisplayList_InitSelectedTextureSegment, DisplayList_InitTwoSegmentChain |
| minigame_shared_graphics | 4 | MiniGfx_DrawBackgroundStrips, MiniGfx_DrawScaledSprite, MiniGfx_DrawVertexColoredQuad, func_87A010C8 |
| audio_sound_state.c | 3 | Audio_GetActiveVoiceCount, Audio_GetFreeVoiceCount, Audio_IsSoundPlayingWrapper |
| 3D140.c | 3 | Audio_GetDiskReadyStatus, Audio_SetDiskReadyPolling, Cry_SetEnabled |
| player.c | 3 | Audio_QueueVoicePan, Audio_QueueVoiceVolumeByte, Audio_RelocatePointerArray |
| battle_prep | 3 | BattlePrepBadgeAward_StartScatter, BattlePrep_FormatString, func_84B0B054 |
| controller.c | 3 | Cont_AttemptReadEeprom, Cont_AttemptWriteEeprom, Cont_IsEepromPresent |
| geo_render.c | 3 | GeoRender_GetMatrix, GeoRender_IsModelFrameCurrent, Geo_ProcessCallbacks |
| 33E30.c | 3 | Gfx_DrawTriangleMarker, func_80033230, func_80033264 |
| vector_math.c | 3 | Math_ProjectPointOntoLine, Vec3f_CrossProductArrays, Vec3f_NormalizeSafe |
| jigglypuff_doodle | 3 | Minigame_JigglypuffDoodle, func_86300F60, func_863015EC |
| poke_icon.c | 3 | PokeIcon_RedrawSlot, PokeIcon_RenderNextSlotBlocking, PokeIcon_ResetRenderList |
| gb_data.c | 3 | Pokemon_StatExpForTarget, Text_GetTmHmName, func_800220C4 |
| dma.c | 3 | Storage_QueueDiskMotorOff, Storage_QueueDiskSeek, Storage_QueueRtcWrite |
| audio_commands_category2.c | 2 | Audio_DispatchSoundBankStopCommand, Audio_SeedRandom |
| 3D140_2.c | 2 | Audio_QueueSequenceIdWrapper, func_800420F0_empty |
| ui_graphics.c | 2 | BattleHud_DrawHpBar16x5, Ui_Draw20x20Icon |
| flash.c | 2 | Flash_EraseChip, Flash_Reinitialize |
| gallery_backgrounds | 2 | Gallery_CopyCommonUiString, Gallery_DrawRoundedFrame |
| gallery_enlarge | 2 | Gallery_EnlargeMain, func_83700090 |
| geo_node.c | 2 | GraphNode_GetChildAtIndex, GraphNode_RemoveChild |
| model_renderer.c | 2 | ModelAnim_GetAnimationRecord, PokeIcon_PollFrameLoadForObject |
| snap_print_club_background_select | 2 | Snap_PrintClubBackgroundSelect, func_83A009C0 |
| trade_machine | 2 | TradeCable_GetSequenceState, func_82F0ECA4 |
| util.c | 2 | Util_CheckStubMemArea, Util_Memclear |
| profiler.c | 2 | clear_profiler_data, set_get_time_diff |
| rattata_game | 2 | func_862002EC, func_86200938 |
| credits | 1 | Credits_Main |
| cry.c | 1 | Cry_PlayVariant4 |
| debug_menu_slot69 | 1 | DebugMenu_ForwardSlot69 |
| debug_menu_slot70 | 1 | DebugMenu_ForwardSlot70 |
| debug_menu_slot72 | 1 | DebugMenu_ForwardSlot72 |
| debug_menu_slot73 | 1 | DebugMenu_ForwardSlot73 |
| debug_menu_slot74 | 1 | DebugMenu_ForwardSlot76 |
| event_battle | 1 | EventBattle_Main |
| fatal_error_screen | 1 | FatalError_Main |
| gallery_debugger | 1 | GalleryDebugger_Main |
| gfx_buffer.c | 1 | Gfx_GetDisplayListRemaining |
| gsm_decoder.c | 1 | Gsm_Decode |
| gym_leader_castle_clear | 1 | GymLeaderCastleClear_Main |
| hal_libc.c | 1 | HAL_Strcpy2 |
| jpegutils.c | 1 | JpegUtils_ProcessHuffmanTableImplOld |
| memory.c | 1 | MainPool_ResetState |
| memmap.c | 1 | Memmap_GetFragmentVaddrMask |
| gb_tower_unused | 1 | Minigame2SampleViewer_PollInput |
| ranking_debug_test | 1 | Minigame_RankingDebugTest |
| minigame_sample7_viewer | 1 | Minigame_Sample7Viewer |
| minigame_sample8_viewer | 1 | Minigame_Sample8Viewer |
| model_animation_events.c | 1 | ModelAnim_IsEventFrame |
| battle_hud.c | 1 | ModelRenderer_GetObjectType |
| display_object_textures.c | 1 | Model_LoadWithScale |
| rental_rules | 1 | RentalRules_Main |
| pikachu_reward | 1 | Stadium_BonusPikachuReward |
| stage_loader.c | 1 | TextRenderer_QueuePersistentString |
| widget_toolkit | 1 | Widget_DrawMeterBar |
| crash_screen.c | 1 | crash_screen_printf_with_bg |
| game_state.c | 1 | func_8002B244 |
| gallery.c | 1 | func_800318F0 |
| audio_commands.c | 1 | func_8003733C |
| gb_audio_render.c | 1 | func_8004AD2C |
| 1 | 1 | func_81209690 |
| title_screen | 1 | func_82100020 |
| area_select | 1 | func_822014C0 |
| minigame_select | 1 | func_825057A0 |
| minigame_sample2_viewer | 1 | func_82900390 |
| cup_select | 1 | func_82E00050 |
| 61 | 1 | func_84209CEC |
| gym_leader_castle | 1 | func_84A00630 |
| magikarp_game | 1 | func_86000C10 |
| clefairy_game | 1 | func_86100C28 |
| sushi_game | 1 | func_86806860 |
| metapod_game | 1 | func_86E0296C |
| lab_pc | 1 | func_88210DA8 |
| lab_pc_icons | 1 | func_88600240 |
| pokemon_lab_unused | 1 | func_88A00020 |
| debug_menu_slot77 | 1 | func_8FC00020 |
| leomecha.c | 1 | leoRecal_i |
| memory_main.c | 1 | main_pool_search |
| n_sl.c | 1 | n_alClose |
| n_resample.c | 1 | n_alResampleParam |

## Referenced only from data (18)

DebugMenu_ForwardSlot68, DebugMenu_ForwardSlot71, Memmap_GetSegmentVaddrMask, ModelAnim_IsEventTrackDone, Mtx_BuildDirectionRotation, ParticleGfx_LoadTextureRgba16WrapT, ParticleGfx_LoadTextureTileIa16, ParticleMath_BitwiseAndNot, ParticleMath_BitwiseXor, Particle_AddVelocityFromRotationTemplate2, Profiler_SetDisplayModes, Storage_PrefetchLbaRange, Storage_QueueRtcRead, Vec3f_Subtract, func_82600C00, func_84001AD8, func_8FB00020, n_alFxParamHdl

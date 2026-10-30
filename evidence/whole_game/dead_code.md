# Functions nothing references

Checked: 368. With a C reference (caller, address taken or pointer table): 232. Referenced only by a raw address word in the ROM: 18. **No reference at all: 118.** Not in the port's function table: 0.

## No reference at all, per area

| area | functions | names |
|---|---|---|
| battle_engine | 39 | BattleAI_InitTrainerRecords, BattleAnim_EndEffectUnusedVerticalFadeAndDelayedFrameSequence, BattleAnim_GetCameraLookYaw, BattleAnim_GetCameraStoredAngles70, BattleAnim_GetCameraStoredAngles76, BattleAnim_GetCameraStoredYaw72, BattleAnim_GetCameraStoredYaw78, BattleAnim_GetSpeciesBySideIndex, BattleAnim_GetTertiaryOwnerSpecies, BattleAnim_InitializeParticleChain, BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence, BattleAnim_OrphanDescriptor34Emitter, BattleAnim_SpawnDescriptor25AlphaFadeInHoldRepeating, BattleAnim_SpawnDescriptor25AlphaHoldDetach, BattleAnim_SpawnDescriptor25AlphaHoldDetachFullAlpha, BattleAnim_SpawnDescriptor25AlphaHoldDetachRepeating, BattleAnim_SpawnDescriptor25AlphaHoldFadeOutRepeating, BattleAnim_SpawnDescriptor25PaletteFadeIn, BattleAnim_SpawnDescriptor25PaletteFadeOut, BattleAnim_SpawnOwnerModelAlphaFadeFullPreset, BattleAnim_SpawnOwnerModelTintHoldFadeDetach, BattleAnim_SpawnOwnerModelTintPaletteCycleVariant, BattleAnim_SpawnOwnerModelTintRandomPaletteCycle, BattleAnim_TriggerStatusAfflictionCry, BattleAnim_UpdateOwnerSecondaryStatusAnimation, BattleHud_DrawForfeitConfirmDialog, BattleHud_DrawMessageBox, BattleScene_AdvanceCameraSubstepCounter, BattleScene_ApproachCameraRollNoEyeUpdate, BattleScene_GetCombinedTeamSize, BattleScene_IsCameraFrameCounterAtLeast, BattleScene_ReloadMoveResourceList, BattleScene_ResetOpponentStateFrameCounter, BattleScene_SetCameraEyeAndStoreAnglesFromAt, BattleScene_TickClearAnimRecordIfLoaded, Battle_IsPartyMemberAlive, Battle_QueueStatusText, Radial20_VariantB, Radial20_VariantE |
| particle_math | 25 | ParticleGfx_DrawBillboardDefaultQuad, ParticleGfx_DrawBillboardPrimEnvRotatedTriangle, ParticleGfx_DrawModelQuadYScaled, ParticleGfx_DrawModelTriangle, ParticleGfx_LoadTextureI4WrapS, ParticleGfx_LoadTextureI8WrapST, ParticleGfx_LoadTextureRgba16WrapS, ParticleGfx_LoadTextureRgba16WrapST, ParticleGfx_PushDirectionMatrix, ParticleGfx_PushTransformMatrixWithHalfTurnXY, ParticleMath_AngleZBetweenPoints, ParticleMath_Atan2XY, ParticleMath_Atan2YZ, ParticleMath_DistanceScaled, ParticleMath_DotVec3f, ParticleMath_IsWithinRadius2D, ParticleMath_Vec3ToAngles, Particle_ApproachPosition, Particle_CountActiveAlias, Particle_EmitBurstAtTransformWithMetadata, Particle_EmitBurstFromObjectWithMetadata, Particle_Field38_AddVecFromVec3f_08DD8, Particle_Field7C_SetVecScaled_0AE84, Particle_Field94_SubVecFromVec3s_0A600, Particle_Field9A_SetVecFromVec3s_0A37C |
| jpeg_stream.c | 5 | Asset_LoadToSegmentByLbaRange, BinArchive_Free, BinArchive_LoadByLbaRange, Dma_ReadChunks, Fragment_LoadByLbaRange |
| gb_save.c | 5 | Color_InterpolateFromGray, Ui_AdjustSteppedRangeSelection, Ui_AdjustSteppedScrollSelection, Ui_HandleHorizontalSelectionInput, Ui_HandleVerticalSelectionInput |
| graphics_textures.c | 4 | Font_DisableLineBreak, Font_EnableLineBreak, Gfx_DrawTextureI16, Gfx_DrawTextureI8 |
| minigame_actor_framework | 4 | MiniFx_ConfigureEmitterWithPeriodAndRepeat, MiniFx_ConfigureOneShotEmitter, miniShowDifficultyText, minigameDebuggVectorEditor |
| demo_scenes | 3 | Completion_AbortDialogToIdle, Vec3f_Distance, func_86B09F64 |
| dma.c | 3 | Storage_QueueDiskMotorOff, Storage_QueueDiskSeek, Storage_QueueRtcWrite |
| 33FE0.c | 3 | func_80033D1C, func_80033D44, func_80034254 |
| widget_tree | 2 | WidgetTree_FreeTree, WidgetTree_InitAnimatedFrameVariantB |
| battle_prep | 1 | BattlePrepBadgeAward_StartScatter |
| event_battle | 1 | EventBattle_Main |
| gallery_camera | 1 | GalleryCamera_ResetFreeView |
| gallery_debugger | 1 | GalleryDebugger_Main |
| gallery_backgrounds | 1 | Gallery_DrawRoundedFrame |
| gallery_enlarge | 1 | Gallery_EnlargeMain |
| geo_render.c | 1 | Geo_ProcessCallbacks |
| 33E30.c | 1 | Gfx_DrawTriangleMarker |
| geo_node.c | 1 | GraphNode_RemoveChild |
| vector_math.c | 1 | Math_ProjectPointOntoLine |
| minigame_shared_graphics | 1 | MiniGfx_DrawVertexColoredQuad |
| gb_tower_unused | 1 | Minigame2SampleViewer_PollInput |
| model_animation_events.c | 1 | ModelAnim_IsEventFrame |
| display_object_textures.c | 1 | Model_LoadWithScale |
| model_renderer.c | 1 | PokeIcon_PollFrameLoadForObject |
| poke_icon.c | 1 | PokeIcon_RedrawSlot |
| rental_rules | 1 | RentalRules_Main |
| save_data.c | 1 | Save_EnsureBank3Loaded |
| pikachu_reward | 1 | Stadium_BonusPikachuReward |
| stage_loader.c | 1 | TextRenderer_QueuePersistentString |
| crash_screen.c | 1 | crash_screen_printf_with_bg |
| gb_tower_emulator | 1 | func_81208E4C |
| minigame_sample2_viewer | 1 | func_82900390 |
| leomecha.c | 1 | leoRecal_i |
| profiler.c | 1 | set_get_time_diff |

## Referenced only from data (18)

DebugMenu_ForwardSlot68, DebugMenu_ForwardSlot71, Memmap_GetSegmentVaddrMask, ModelAnim_IsEventTrackDone, Mtx_BuildDirectionRotation, ParticleGfx_LoadTextureRgba16WrapT, ParticleGfx_LoadTextureTileIa16, ParticleMath_BitwiseAndNot, ParticleMath_BitwiseXor, Particle_AddVelocityFromRotationTemplate2, Profiler_SetDisplayModes, Storage_PrefetchLbaRange, Storage_QueueRtcRead, Vec3f_Subtract, func_82600C00, func_84001AD8, func_8FB00020, n_alFxParamHdl

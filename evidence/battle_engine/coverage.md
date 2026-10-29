# Battle engine: static call graph and runtime coverage

Runs: s1, s2, s3, s5, s7, s9, mv1, mv101, mv105, mv109, mv113, mv117, mv121, mv125, mv129, mv13, mv133, mv137, mv141, mv145, mv149, mv153, mv157, mv161, mv165, mv17, mv21, mv25, mv29, mv33, mv37, mv41, mv45, mv49, mv5, mv53, mv57, mv61, mv65, mv69, mv73, mv77, mv81, mv85, mv89, mv9, mv93, mv97, rules0, rules2, counter, rest, spect, call, g000, g001, g002, g003, g004, g005, g006, g007.

| | functions | reached | % |
|---|---|---|---|
| all | 2055 | 1704 | 82.9 |
| named | 1737 | 1466 | 84.4 |
| func_XXXXXXXX | 318 | 238 | 74.8 |
| PORT: changed | 32 | 27 | 84.4 |

Static graph: 8942 edges (call 8139, table 381, ref 422), 14 pointer tables, 206 roots (no caller in these files).

## Pointer tables

| table | functions | reached |
|---|---|---|
| gBattleAnimParticleDescriptors | 61 | 20 |
| gMoveAnimEffectEndDrawFuncs | 8 | 4 |
| gMoveAnimEffectEndFuncs | 8 | 6 |
| gMoveAnimEffectEndInitFuncs | 79 | 69 |
| gMoveAnimEffectEndUpdateFuncs | 8 | 6 |
| gMoveAnimEffectStartDrawFuncs | 23 | 21 |
| gMoveAnimEffectStartFuncs | 23 | 22 |
| gMoveAnimEffectStartInitFuncs | 124 | 111 |
| gMoveAnimEffectStartUpdateFuncs | 23 | 22 |
| gMoveEffectHandlers | 34 | 33 |
| gParticleDescriptorChildren | 33 | 0 |
| sBattleSceneStateRowTable | 51 | 37 |
| sBattleSceneStateTickSentinel | 1 | 1 |
| sStageGeoLayout | 4 | 4 |

## Per file

| file | functions | reached | not reached (first 12) |
|---|---|---|---|
| battle_engine_2EC3C0.c | 121 | 97 | BattleAnim_TriggerStatusAfflictionCry, BattleAnim_TrySetModelEventTrackFromRow, BattleScene_ReloadMoveResourceList, BattleScene_ResetOpponentStateFrameCounter, BattleScene_StateRow56EnterNop, BattleScene_StateRow5Slot3Nop, BattleScene_StateRow5Slot4Nop, BattleScene_StateRow5Slot5Nop, BattleScene_StateRow5Slot6Nop, BattleScene_StateRow6Slot3Nop, BattleScene_StateRow6Slot4Nop, BattleScene_StateRow6Slot5Nop ... |
| battle_engine_2F1D60.c | 17 | 15 | BattleScene_ClearParticipantOwnerActiveFlag, BattleScene_SetParticipantOwnerActiveFlag |
| battle_engine_2F23A0.c | 59 | 51 | BattleAnim_GetSpeciesBySideIndex, BattleScene_ComputeParticipantModelOffset, BattleScene_GetCombinedTeamSize, BattleScene_GetOpposingSelectedSideTeamSize, BattleScene_IsParticipantModelVisible, BattleScene_SetParticipantModelScale, Battle_GetScaleRatio, Battle_SetModelToStatusAnim |
| battle_engine_2F3CB0.c | 74 | 65 | BattleAnim_ClearOwnerSecondaryStatusFlags, BattleAnim_InitSecondarySwayOffset, BattleAnim_SecondaryStatusCaseB, BattleAnim_SecondaryStatusCaseBSub0, BattleAnim_SecondaryStatusCaseBSub1, BattleAnim_SecondaryStatusDefault, BattleAnim_SetOwnerSecondaryStatusFlags, BattleAnim_UpdateOwnerSecondaryStatusAnimation, BattleAnim_UpdateSecondarySwayPhysics |
| battle_engine_2F6930.c | 22 | 11 | BattleAnim_ApproachS16, BattleAnim_ApproachU16, BattleAnim_Atan2sFromPoints, BattleAnim_Vec3fOffsetByPolarXZ, BattleAnim_Vec3fScaleComponents, BattleAnim_Vec3fSetZero, Battle_ComputeSplineWeightsVariantA, Battle_ComputeSplineWeightsVariantB, Battle_ComputeSplineWeightsVariantC, Battle_ComputeSplineWeightsVariantD, Battle_ComputeSplineWeightsVariantE |
| battle_engine_2F74E0.c | 54 | 43 | BattleScene_DetermineWinner, BattleScene_ForfeitPrompt, BattleScene_HandleNoUsableMoveSelection, BattleScene_HideParticipantOwnerAndSecondaryModels, BattleScene_QueuePartySelectionConfirmMessage, BattleScene_SelectActiveController, BattleScene_SelectMoveFromButtons, BattleScene_ShowParticipantOwnerAndSecondaryModels, BattleScene_ValidateSelectedMove, BattleScene_ValidateSelectedPartyMember, Battle_ClassifyHpTier |
| battle_engine_2FA4D0.c | 56 | 31 | BattleHud_DrawBattleTimer, BattleHud_DrawDoublesRosterRow, BattleHud_DrawDoublesTeamStatusList, BattleHud_DrawExpandingCaptionBoxBlack, BattleHud_DrawForfeitConfirmDialog, BattleHud_DrawMessageBox, BattleHud_DrawMovePpTextLTR, BattleHud_DrawMovePpTextRTL, BattleHud_DrawMoveSelectMenu, BattleHud_DrawParticipantTimer, BattleHud_DrawPartyRosterRow, BattleHud_DrawPlainText ... |
| battle_engine_3020D0.c | 23 | 19 | BattleScene_BuildParticipantMessage, Battle_QueueStatusText, func_84317940, func_84317B38 |
| battle_engine_304060.c | 23 | 16 | BattleScene_IntroCameraHeightShotStep, BattleScene_IntroCameraHeightShotSwapStep, BattleScene_IntroCameraStep12Or14, BattleScene_IntroCameraStep16, BattleScene_IntroCameraStep18, BattleScene_IntroCameraWideHoldStep, BattleScene_IsEitherActivePokemonFainted |
| battle_engine_3055E0.c | 81 | 73 | BattleScene_AdvanceCameraSubstepCounter, BattleScene_ApproachCameraActivePokemonHeightShot, BattleScene_ApproachCameraRollNoEyeUpdate, BattleScene_IsCameraAngleSettled, BattleScene_IsCameraFrameCounterAtLeast, BattleScene_SetCameraActivePokemonHeightShot, BattleScene_SetCameraEyeAndStoreAnglesFromAt, BattleScene_SetCameraSendOutShot |
| battle_engine_309ED0.c | 118 | 105 | BattleScene_AdvanceSendOutFollowupCamera, BattleScene_ApplyModelAnimationSlot5A, BattleScene_CompleteSendOutExtendedFade, BattleScene_CompleteSendOutFollowup, BattleScene_Phase16, BattleScene_PhaseAttackHitRecoil, BattleScene_PhaseCheckFaintedEndTurn, BattleScene_PhaseSendOutAfterFaint, func_8431FCC4, func_84321D34, func_84324404, func_8432440C ... |
| battle_engine_313250.c | 13 | 11 | BattleScene_IntroInitNop, BattleScene_IntroStepNop |
| battle_engine_313D60.c | 23 | 18 | BattleScene_FinalPresentationFrameActiveOwner, BattleScene_FinalPresentationFrameOwner0, func_843297E8, func_843297F0, func_8432A414 |
| battle_engine_315830.c | 8 | 7 | BattleAnim_RestoreBaseModelAnimation |
| battle_engine_315D50.c | 69 | 59 | BattleAnim_GetCameraLookYaw, BattleAnim_GetCameraStoredAngles70, BattleAnim_GetCameraStoredAngles76, BattleAnim_GetCameraStoredYaw72, BattleAnim_GetCameraStoredYaw78, BattleAnim_PlaySoundEffect1, BattleAnim_PlaySoundEffect3, BattleAnim_PlaySoundEffect8, BattleAnim_QueueEffectList5, BattleAnim_QueueEffectList6Alt |
| battle_engine_317E70.c | 98 | 78 | BattleAnim_GetEffectStateFlag, BattleAnim_GetOwnerModelBoundsScaled, BattleAnim_GetOwnerSpeciesBaseScaleAlias, BattleAnim_GetSecondaryOwnerSpeciesBaseScale, BattleAnim_GetTertiaryOwnerSpecies, BattleAnim_GetTertiaryOwnerSpeciesBaseScale, BattleAnim_InitializeParticleChain, BattleAnim_MarkParticleInactive, BattleAnim_SpawnBatchedParticles, BattleAnim_SpawnDescriptor25AlphaFadeInHoldRepeating, BattleAnim_SpawnDescriptor25AlphaHoldDetach, BattleAnim_SpawnDescriptor25AlphaHoldDetachFullAlpha ... |
| battle_engine_31A7E0.c | 5 | 5 |  |
| battle_engine_31AA30.c | 31 | 22 | BattleAnim_DrawAsset13FrameDiv2I4Quad, BattleAnim_DrawAsset13FrameI4YScaledQuadB, BattleAnim_DrawAsset18FrameI4ModelQuad, BattleAnim_DrawAsset1BI4TriangleB, BattleAnim_DrawAsset1DRotatedI4Quad, BattleAnim_DrawAsset87AnimatedI4Quad, BattleAnim_DrawAsset95Ia8Billboard, BattleAnim_DrawAssetA9I4XYScaledQuad, BattleAnim_DrawParticleNoop |
| battle_engine_31C660.c | 38 | 24 | BattleAnim_CallbackDescriptor25AlphaHoldDetach, BattleAnim_CallbackDescriptor25PaletteFadeIn, BattleAnim_CallbackDescriptor25PaletteFadeOut, BattleAnim_CallbackOwnerModelTintHoldFadeDetach, BattleAnim_CallbackOwnerModelTintPaletteApproach, BattleAnim_CallbackOwnerModelTintPaletteApproachRampDown, BattleAnim_CallbackOwnerModelTintRandomPaletteCycle, BattleAnim_SetupDescriptor25AlphaHoldDetach, BattleAnim_SetupDescriptor25PaletteFadeIn, BattleAnim_SetupDescriptor25PaletteFadeOut, BattleAnim_SetupOwnerModelTintPaletteApproach, BattleAnim_SetupOwnerModelTintPaletteApproachRampDown ... |
| battle_engine_31D9F0.c | 77 | 63 | BattleAnim_CallbackEndEffect29Descriptor02RotatingScaleGrow, BattleAnim_CallbackEndEffect29Descriptor02RotatingScaleGrow_InitParticle, BattleAnim_CallbackEndEffect68Descriptor1BExpandingFadeDrift, BattleAnim_CallbackUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_CallbackUnusedGrowFadeVariant, BattleAnim_CallbackUnusedVariant2, BattleAnim_MotionUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_SetupUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_SetupUnusedVariant2, func_84334B04, func_843375B0, func_8433765C ... |
| battle_engine_3223B0.c | 91 | 78 | BattleAnim_CallbackEndEffect68Descriptor54FlickerExpandingFade, BattleAnim_CallbackEndEffect68Descriptor54FlickerExpandingFade_InitParticle, BattleAnim_CallbackUnusedAimedSparkVariant, BattleAnim_CallbackUnusedIndexedFadeVariantA, BattleAnim_CallbackUnusedIndexedFadeVariantB, BattleAnim_CallbackUnusedRotatingSparkVariant, BattleAnim_MotionUnusedRotatingSparkVariant, BattleAnim_SetupUnusedAimedSparkVariant, BattleAnim_SetupUnusedIndexedFadeVariantA, BattleAnim_SetupUnusedIndexedFadeVariantB, BattleAnim_SetupUnusedRotatingSparkVariant, func_8433BF54 ... |
| battle_engine_3274D0.c | 28 | 21 | BattleAnim_CallbackEndEffect28Descriptor10UpwardBurstFade, BattleAnim_CallbackEndEffect28Descriptor10WideScatterBurstFade, BattleAnim_CallbackUnregisteredStraightDirectionalStreamFade, BattleAnim_SetupEndEffect28Descriptor10UpwardBurstFade, BattleAnim_SetupEndEffect28Descriptor10WideScatterBurstFade, BattleAnim_SetupUnregisteredStraightDirectionalStreamFade, func_8433E33C |
| battle_engine_328F10.c | 24 | 21 | BattleAnim_StartEffect122GlobalAndOwnerTintFade, BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup, BattleAnim_StartEffect143ModeOrbitAndMetadataBurst |
| battle_engine_32A640.c | 14 | 6 | BattleAnim_CallbackOrphanDescriptor2BSpinFade, BattleAnim_CallbackOrphanDescriptor2EScatterFade, BattleAnim_CallbackUnusedGenericFadeApproach, BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence, BattleAnim_SetupOrphanDescriptor2BSpinFade, BattleAnim_SetupOrphanDescriptor2EScatterFade, BattleAnim_UnusedResetAppearanceAndAdvance, func_84340AC4 |
| battle_engine_32B680.c | 280 | 262 | BattleAnim_ApplySpeciesYOffsetVariantB, BattleAnim_CallbackEndEffect49Model33Anim34WaitCleanup, BattleAnim_CallbackStartEffect86ModelFadeCycle, BattleAnim_InitEffect86Model, func_84341830, func_843453E4, func_84345490, func_84345B28, func_84345EC0, func_84348C6C, func_84348C78, func_8434917C ... |
| battle_engine_339CA0.c | 153 | 126 | BattleAnim_CallbackDescriptor26BurstChild, BattleAnim_CallbackDescriptor26BurstChild_InitParticle, BattleAnim_CallbackDescriptor26BurstEmitter, BattleAnim_CallbackDescriptor26BurstEmitter_InitParticle, BattleAnim_CallbackDescriptor50BurstChild, BattleAnim_CallbackDescriptor50BurstChild_InitParticle, BattleAnim_CallbackDescriptor50BurstEmitter, BattleAnim_CallbackDescriptor50BurstEmitter_InitParticle, BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade, BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade_InitParticle, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach_InitParticle ... |
| battle_engine_3438B0.c | 4 | 2 | BattleAnim_RenderNop1, BattleAnim_RenderNop2 |
| battle_engine_343BA0.c | 126 | 99 | BattleAnim_DefaultEndEffect_Draw, BattleAnim_DefaultStartEffect_Draw, BattleAnim_DrawStoredColorOverlay, BattleAnim_EndEffect16_NoOpDraw, BattleAnim_EndEffect19RibbonMesh_Draw, BattleAnim_EndEffect19RibbonMesh_Init, BattleAnim_EndEffect19RibbonMesh_Update, BattleAnim_EndEffect33Radial20_Draw, BattleAnim_EndEffect33Radial20_Init, BattleAnim_EndEffect33Radial20_Update, BattleAnim_StartEffect77DoubleTextureTrail_Draw, BattleAnim_StartEffect77DoubleTextureTrail_Init ... |
| battle_engine_347330.c | 14 | 14 |  |
| battle_engine_34A420.c | 14 | 12 | Radial20_VariantB, Radial20_VariantE |
| battle_engine_34E1A0.c | 6 | 6 |  |
| battle_engine_34F1A0.c | 7 | 7 |  |
| battle_engine_3502D0.c | 6 | 6 |  |
| battle_engine_3512D0.c | 6 | 6 |  |
| battle_engine_351F20.c | 1 | 1 |  |
| battle_engine_352570.c | 6 | 6 |  |
| battle_engine_353370.c | 6 | 6 |  |
| battle_engine_3540B0.c | 6 | 6 |  |
| battle_engine_354CD0.c | 6 | 6 |  |
| battle_engine_355A00.c | 6 | 6 |  |
| battle_engine_356730.c | 8 | 8 |  |
| battle_engine_357C30.c | 9 | 8 | BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset |
| battle_engine_3591E0.c | 9 | 6 | func_8436EED4, func_8436EEDC, func_8436EEE4 |
| battle_engine_3597B0.c | 5 | 5 |  |
| battle_engine_359F80.c | 1 | 0 | func_8436F6C0 |
| battle_engine_359F90.c | 54 | 45 | Battle_CheckSideDefeatAfterResidualStatus, Battle_IsPartyMemberAlive, Battle_SetActiveMoveFromSlotIndex, func_8436F6D0, func_8436FDF4, func_84370090, func_84370E70, func_84370E78, func_843718DC |
| battle_engine_35DF70.c | 38 | 37 | Battle_Effect_Recharge |
| battle_engine_361050.c | 91 | 63 | BattleAI_AllCandidatesResistType, BattleAI_GroupSharesTypeWeakness, BattleAI_HasOtherCandidateAtLeastScore, BattleAI_InitTrainerRecords, BattleAI_InsertRankedCandidateGroup, BattleAI_IsBannedRandomMove, BattleAI_IsCandidateEligible, BattleAI_IsDiscouragedRepeatCategory, BattleAI_IsMoveExcludedForSpecies, BattleAI_IsMoveIneffective, BattleAI_IsRestrictedRandomMove, BattleAI_IsSpeciesInList ... |
| battle_engine_main.c | 26 | 22 | BattleScene_UpdatePausedFrame, func_84300E78, func_84300E80, func_84301A24 |

## Functions reached from more than one caller (378)

| function | runtime callers (count) | static callers not seen |
|---|---|---|
| BattleAnim_CreateEffectSlotForCurrentOwner | BattleAnim_StartEffect135ExpandingFlashAndRisingScaleFade (762), func_84356020 (330), BattleAnim_StartEffect141GridRiseAndFallingBurst (193), func_843568C8 (178), BattleAnim_StartEffect134ModeOrbitAndMetadataBurst (143), func_84344B04 (96) | BattleAnim_EndEffectUnusedVerticalFadeAndDelayedFrameSequence, BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence, Battle |
| BattleScene_GetParticipantSideIndex | Battle_AdvanceSecondaryModelAnimIfFinished (1954068), BattleScene_GetParticipantModelRadius (1534074), func_843060EC (651356), BattleAnim_StartFreezeStatusAnimation (651356), BattleHud_DrawMonInfoPanel (584906), Battle_ResetModelToIdleAnim (502494) | BattleAnim_RestoreBaseModelAnimation, BattleAnim_TrySetModelEventTrackFromRow, BattleScene_ApplyModelAnimationSlot5A, Ba |
| BattleAnim_CreateRepeatingEffectSlotForCurrentOwner | BattleAnim_StartEffect134ModeOrbitAndMetadataBurst (715), func_84336EE8 (501), func_843378D4 (497), func_8435839C (351), func_84337000 (310), BattleAnim_StartEffect135ExpandingFlashAndRisingScaleFade (254) | BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup, BattleAnim_StartEffect143ModeOrbitAndMetadataBurst, func_84 |
| BattleAnim_SpawnGlobalTintColorFadeDetach | BattleAnim_StartEffect134ModeOrbitAndMetadataBurst (286), BattleAnim_StartEffect135ExpandingFlashAndRisingScaleFade (254), func_84336EE8 (167), func_843378D4 (71), BattleAnim_StartEffect18ScatterRadialAndColumnSequence (12), func_84345038 (10) | BattleAnim_StartEffect122GlobalAndOwnerTintFade, BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup, BattleAnim |
| BattleScene_ResetCameraFrameCounter | BattleScene_ActionSetupAnimationDefault (1381), BattleScene_UpdateDisplayedHpAndTransition (880), BattleScene_ActionSetupCameraStep (859), BattleScene_AdvanceActionPresentationSetup (857), BattleScene_SelectNextIntroCameraShot (800), BattleScene_CompleteHpUpdateTransition (609) | BattleScene_AdvanceSendOutFollowupCamera, BattleScene_CompleteSendOutFollowup, BattleScene_PhaseAttackHitRecoil, BattleS |
| Battle_QueueMessage | Battle_QueueMoveCategoryUsedMessage (767), func_8436FA80 (349), func_8436F838 (255), func_84371010 (157), func_84371080 (148), Battle_HandleFaint (146) | Battle_Effect_ConfuseChance, Battle_TryLoadFollowupMoveFromTarget |
| BattleAnim_GetOwnerDefaultAnchorPosition | BattleScene_FrameCamera (63658), BattleScene_CameraShotTargetClose (3385), BattleScene_SetCameraOwnerStandardShot (3087), BattleScene_GetOwnerBoneOrDefaultAnchor (2335), BattleScene_SetCameraPresetWide (948), BattleAnim_InitializeParticleAnchor (888) | BattleScene_AdvanceOwnerAnimationAndClearFlags, BattleScene_ApproachCameraActivePokemonHeightShot, BattleScene_FinalPres |
| Battle_GetActiveMonNickname | BattleHud_DrawMonInfoPanel (1169812), Battle_QueueMoveCategoryUsedMessage (767), Battle_HandleFaint (146), Battle_CheckMonCanAct (78), Battle_ApplyResidualEffects (63), Battle_QueueMoveFailureAndApplyRecoil (62) | Battle_Effect_ConfuseChance |
| BattleAnim_SpawnOwnerModelTintRampDetach | BattleAnim_StartEffect135ExpandingFlashAndRisingScaleFade (508), BattleAnim_StartEffect134ModeOrbitAndMetadataBurst (143), func_84337000 (62), func_8434A384 (12), func_8434E870 (11), func_84348630 (9) | BattleAnim_StartEffect122GlobalAndOwnerTintFade, BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup, BattleAnim |
| BattleScene_AdvanceCameraFrameCounter | BattleScene_IsRowWaitCompleteWithEventCount (22736), BattleScene_ActionSetupCameraStep (14530), BattleScene_WaitForActionCameraDuration (13376), BattleScene_RunActionEffectHpTransition (11155), BattleScene_IntroCameraPresetHoldStep (9445), BattleScene_IntroCameraFinalHoldStep (5756) | BattleScene_CompleteSendOutExtendedFade, BattleScene_CompleteSendOutFollowup, BattleScene_IntroCameraWideHoldStep, Battl |
| BattleAnim_GetOwnerSpecies | BattleAnim_CallbackStartEffects71And74And111SpecialModelColorCycle (700), BattleAnim_InitSharedVerticalFadeParticle_ApplySpeciesOffset (150), BattleAnim_ApplyEffect31SpeciesOffset (96), BattleAnim_ApplySpeciesKeyedSpawnOffset (96), BattleAnim_ApplyModelSpeciesOffset (36), BattleAnim_ApplyMove86SpeciesOffset (36) | BattleAnim_ApplySpeciesYOffsetVariantB, BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade_InitParticle, B |
| BattleScene_ResetAnimationStep | BattleScene_RunActionEffectHpTransition (22882), BattleScene_TickCameraSubstateStripWipe (11928), BattleScene_PhaseDrainDamage (3138), BattleScene_TickCameraSubstateDefault (2550), BattleScene_CompleteActionPhase (693), BattleScene_InitializeIntroCamera (620) | BattleScene_AdvanceBothOwnerAnimations, BattleScene_AdvanceDoublesOwnerAnimation, BattleScene_AdvanceOwnerAnimation, Bat |
| BattleAnim_GetOwnerContextParticle | func_8434B2B8 (2626), func_84341E50 (2589), func_843467F8 (1530), func_843469A8 (1530), func_84349D10 (1225), func_843492AC (1200) |  |
| BattleAnim_ResetSharedEffectMatrix | BattleAnim_Effect5RibbonMesh_Init (30), BattleAnim_EndEffect1RibbonMesh_Init (12), BattleAnim_EndEffect9Radial20_Init (10), BattleAnim_StartEffect11Trail50Alt_Init (8), BattleAnim_StartEffect67OwnerAnchoredFourStream_Init (8), BattleAnim_StartEffect19Radial20_Init (5) | BattleAnim_EndEffect19RibbonMesh_Init, BattleAnim_EndEffect33Radial20_Init, BattleAnim_StartEffect77DoubleTextureTrail_I |
| BattleScene_SetCameraEyeFromPosition | BattleScene_FrameCamera (127316), BattleScene_CameraShotTargetClose (3385), BattleScene_SetCameraOwnerStandardShot (3087), BattleScene_SetCameraPresetWide (948), BattleScene_IntroPanCamera (854), BattleScene_ApplyCameraPreset (842) | BattleScene_SetCameraActivePokemonHeightShot, BattleScene_SetCameraSendOutShot |
| BattleScene_AnimateDisplayedHpToRuntime | BattleScene_RunActionEffectHpTransition (108998), BattleScene_PhaseDrainDamage (6276), BattleScene_PhaseAttackHit (5944), BattleScene_Phase7 (5942), BattleScene_RunRepeatedHitHpTransition (4004), BattleScene_RunConditionalHpTransition (2042) | BattleScene_Phase16, BattleScene_PhaseAttackHitRecoil, BattleScene_PhaseCheckFaintedEndTurn |
| BattleAnim_AllocOwnerDisplayContext | BattleAnim_CallbackStartEffect8ModelBurstEmitter (450), BattleAnim_SelectParticleModelFrames (334), BattleAnim_InitEffect31Model (96), BattleAnim_InitModelAnimationSlot (36), BattleAnim_InitEffects10And50Model (30), BattleAnim_CallbackEndEffect7ModelScaleAlphaFade_InitParticle (24) | BattleAnim_InitEffect86Model |
| BattleScene_ScaleCameraDistance | BattleScene_FrameCamera (63658), BattleScene_ApproachCameraZoom (12642), BattleScene_CameraShotTargetClose (3385), BattleScene_SetCameraOwnerStandardShot (3087), BattleScene_LoadCameraPreset (2912), BattleScene_SetCameraPresetWide (948) | BattleScene_FinalPresentationFrameActiveOwner, BattleScene_FinalPresentationFrameOwner0, BattleScene_SetCameraSendOutSho |
| BattleAnim_IsAssetLoadBusy | BattleScene_AdvanceSendOutCamera (16839), BattleScene_TickQueueList3Selector2OrC (6651), BattleScene_Phase13 (1830), BattleScene_TickRow3PlayMoveEffectScript (782), BattleScene_TickRow3PostEffectCamera (782), BattleScene_TickRow3QueueEffectList6 (778) | BattleScene_AdvanceSendOutFollowupCamera, BattleScene_Row12Slot1CameraCleanup, BattleScene_TickQueueList3Selector2OrCFro |
| BattleBox_Open | BattleScene_ActionSetupCameraStep (859), BattleScene_RunActionEffectHpTransition (563), BattleScene_AdvanceSendOutCamera (134), BattleScene_PhaseWithdrawSwitch (130), BattleScene_PhaseMoveFailedMessage (114), BattleScene_PhaseSelectMoveAnim (103) | BattleScene_AdvanceSendOutFollowupCamera, BattleScene_CompleteSendOutPresentation, BattleScene_Phase16, BattleScene_Phas |
| BattleAnim_Vec3fCopy | BattleScene_ApplyOwnerFramingAdjustments (229515), BattleScene_AnimateCameraPreset (127788), BattleScene_FrameCamera (127316), BattleScene_UpdateCameraTargetFromOwner (100074), BattleScene_ApproachCameraTarget (20848), BattleScene_UpdateCameraModelOffset (18790) | BattleScene_AdvanceOwnerAnimationAndClearFlags, BattleScene_ApproachCameraActivePokemonHeightShot, BattleScene_SetCamera |
| BattleAnim_CreateEffectSlot | BattleAnim_CreateRepeatingEffectSlotForCurrentOwner (3956), BattleAnim_CreateEffectSlotForCurrentOwner (2872), BattleAnim_SpawnGlobalTintColorFadeDetach (948), BattleAnim_SpawnOwnerModelTintRampDetach (801), BattleAnim_SpawnOwnerModelAlphaFade (402), BattleAnim_SpawnDescriptor25AlphaHoldFadeOutFullAlpha (280) | BattleAnim_SpawnDescriptor25AlphaFadeInHoldRepeating, BattleAnim_SpawnDescriptor25AlphaHoldDetach, BattleAnim_SpawnDescr |
| BattleScene_SetCameraAtFromAnchor | BattleScene_CameraShotTargetClose (3385), BattleScene_SetCameraOwnerStandardShot (3087), BattleScene_SetCameraPresetWide (948), BattleScene_CameraShotTargetStandard (715), BattleScene_SetCameraFollowPresetNear (359), BattleScene_SetCameraPresetAngledClose (165) | BattleScene_FinalPresentationFrameActiveOwner, BattleScene_FinalPresentationFrameOwner0, BattleScene_SetCameraActivePoke |
| BattleAnim_CleanupEffects | BattleScene_PrepareMoveResourcePresentation (859), BattleScene_CleanupActionPresentation (743), BattleScene_TickPlayMoveEffectEnd (623), BattleScene_TickCameraSubstateStripWipe (375), BattleScene_TickCameraSubstateDefault (255), BattleScene_AdvanceSendOutPreparation (143) | BattleScene_Phase16, BattleScene_PhaseAttackHitRecoil, BattleScene_PhaseCheckFaintedEndTurn |
| BattleAnim_ApproachF | BattleScene_ApplyCameraShake (325678), BattleScene_FrameCamera (127316), BattleScene_AnimateCameraPreset (83946), BattleScene_ApplyOwnerFramingAdjustments (82391), BattleScene_ActionSetupCameraStep (36350), BattleScene_ApproachCameraZoom (12642) | BattleScene_ApproachCameraActivePokemonHeightShot |
| BattleScene_GetParticipantFacingAngle | BattleScene_FrameCamera (63658), BattleScene_AnimateCameraIntro (7168), BattleScene_CameraShotTargetClose (6770), BattleScene_SetCameraOwnerStandardShot (6174), BattleScene_LoadCameraPreset (2912), BattleScene_SetCameraPresetWide (1896) | BattleScene_FinalPresentationFrameActiveOwner, BattleScene_FinalPresentationFrameOwner0, BattleScene_SetCameraSendOutSho |
| Battle_Random | Battle_ApplyDamageRandomFactor (3542), Battle_CheckCriticalHit (642), Battle_Effect_SecondaryStatusChance (79), Battle_CheckMonCanAct (46), Battle_Effect_Thrash (30), Battle_Effect_MultiHit (19) | Battle_QueueMetronomeMoveMessage |
| BattleScene_EnterStateRow | BattleScene_AdvanceActionPresentationSetup (751), BattleScene_RunActionEffectHpTransition (563), BattleScene_AdvanceSendOutPreparation (143), BattleScene_IntroCameraStep4Or6 (132), BattleScene_PhaseWithdrawSwitch (130), BattleScene_PhaseMoveFailedMessage (114) | BattleScene_Phase16, BattleScene_PhaseAttackHitRecoil, BattleScene_PhaseCheckFaintedEndTurn, BattleScene_SetCameraSendOu |
| func_84370ADC | BattleScene_ActionSetupFinalizeStep (63830), Battle_ResolveMonAction (1736), BattleScene_RunActionEffectHpTransition (563), BattleScene_Phase7 (43), BattleScene_RunRepeatedHitHpTransition (21), BattleScene_RunBasicHpTransition (15) |  |
| BattleScene_ClearPhaseFlag | BattleScene_UpdateDisplayedHpAndTransition (880), BattleScene_AdvanceActionPresentationSetup (751), BattleScene_ActionSetupAnimationDefault (688), BattleScene_InitializeBattlePresentation (620), BattleScene_CompleteHpUpdateTransition (609), BattleScene_TransitionToPhaseFull (333) | BattleScene_CompleteSendOutFollowup, BattleScene_PhaseAttackHitRecoil, BattleScene_PhaseCheckFaintedEndTurn |
| BattleScene_UpdateCameraModelOffset | BattleScene_CameraShotTargetClose (3385), BattleScene_SetCameraOwnerStandardShot (3087), BattleScene_SetCameraPresetWide (948), BattleScene_CameraShotTargetStandard (715), BattleScene_SetCameraFollowPresetNear (359), BattleScene_SetCameraPresetAngledClose (165) | BattleScene_AdvanceSendOutFollowupCamera |
| BattleAnim_AddSpawnDelay | BattleAnim_StartEffect18ScatterRadialAndColumnSequence (12), BattleAnim_StartEffect14ModelAnimAndMoveKeyedTintParticles (6), BattleAnim_EndEffect26DelayedDescriptor29ScatterSequence (3), func_8434C0C4 (2), func_8434C720 (2), func_8434C618 (2) | BattleAnim_OrphanDescriptor2BAnd2EEmitterSequence |
| BattleAnim_SpawnDescriptor25AlphaHoldFadeOutFullAlpha | BattleAnim_StartEffect135ExpandingFlashAndRisingScaleFade (254), BattleAnim_StartEffect13SecondaryOwnerSpeciesBranch (3), func_8434C0C4 (2), func_8434C144 (2), func_8434C720 (2), func_8434C618 (2) | func_8433E33C, func_8435727C |
| BattleScene_SetParticipantModelFlags | BattleScene_TickRow3PlayMoveEffectScript (762), BattleScene_ResetParticipantModelFlags (384), BattleScene_PhaseWithdrawSwitch (130), BattleScene_InitializePresentation (122), BattleScene_IntroSendOutSecondMon (61), BattleScene_IntroNarrowFov (61) | BattleScene_SendOutPresentationFinalize, func_8430506C |
| BattleScene_ByteArrayContains | BattleAnim_GetOwnerDefaultAnchorPosition (311468), BattleScene_GetOwnerCameraAnchor (262119), BattleScene_RecomputeCameraDistance (50231), BattleScene_UpdateCameraModelOffset (37580), Battle_QueueMoveCategoryUsedMessage (3027), BattleScene_Phase13 (1830) | BattleScene_SetCameraSendOutShot, func_8430506C |
| BattleAnim_SelectParticleModelFrames | BattleAnim_CallbackInitModelAnimWait (108), BattleAnim_CallbackInitModelAnim (55), BattleAnim_CallbackStartEffects118And119ModelPalettePulse (40), BattleAnim_CallbackEndEffect79DefaultDescriptorPaletteFade (28), BattleAnim_CallbackInitModelAnimMode2Update (24), BattleAnim_CallbackStartEffect69ModelPhaseWait (20) | func_84341830 |
| BattleScene_SelectCameraFollowPreset | BattleScene_ActionSetupCameraStep (376), BattleScene_PhaseWithdrawSwitch (130), BattleScene_IntroFirstMonSettle (61), BattleScene_IntroRevealFirstMon (61), BattleScene_IntroSecondMonSettle (61), BattleScene_IntroRevealSecondMon (61) | BattleScene_Phase30 |
| BattleAnim_GetQueuedMoveId | BattleAnim_BuildReflectiveTextureDisplayList (523), BattleAnim_RecolorParticleByQueuedMoveId (430), BattleAnim_InitEffect65Overlay (28), BattleAnim_GetMoveKeyedModelVariantIndex (10), BattleAnim_StartEffect14ModelAnimAndMoveKeyedTintParticles (6), BattleAnim_GetMoveKeyedTintRampParam (6) |  |
| BattleAnim_InitModelWithAnim | BattleAnim_SelectParticleModelFrames (270), BattleAnim_InitEffect31Model (96), BattleAnim_InitModelAnimationSlot (36), BattleAnim_InitEffects10And50Model (30), BattleAnim_CallbackEndEffect7ModelScaleAlphaFade_InitParticle (24), BattleAnim_InitEffects63And75Model (6) |  |
| BattleAnim_SpawnDescriptor25AlphaHoldFadeOut | BattleAnim_StartEffect18ScatterRadialAndColumnSequence (12), func_8433DECC (7), func_8433DFF4 (5), func_84356EF4 (3), func_84348630 (3), func_84357110 (3) | func_8435742C, func_843574FC |
| BattleScene_SaveCameraFov | BattleScene_ActionSetupAnimationVariantA (54), BattleScene_Phase7 (39), BattleScene_Phase13 (20), BattleScene_Phase17 (15), BattleScene_Phase14 (7), BattleScene_Phase31 (6) |  |
| BattleScene_GetParticipantModelHeight | BattleAnim_GetOwnerDefaultAnchorPosition (77867), BattleScene_TickQueueRowEffectList3AtAnimEnd (14139), BattleScene_TickPlaySpeciesSequence33 (14139), BattleScene_UpdateCameraModelOffset (9395), BattleScene_TickQueueList3Selector2OrC (6508), BattleScene_PositionParticipantModel (597) | BattleAnim_SecondaryStatusCaseBSub1, BattleScene_CleanupOpponentAnimationFlags, BattleScene_ComputeParticipantModelOffse |
| BattleAnim_CalcVectorAtToDm | BattleAnim_StartEffect19Radial20_EmitVariantD (35), BattleAnim_StartEffect47SixSlotTrail_Seed (32), BattleAnim_StartEffect39SixSlotTrail_Seed (26), BattleAnim_StartEffect33Radial20_EmitVariantC (16), BattleAnim_StartEffect67OwnerAnchoredFourStream_Init (8), BattleAnim_StartEffect98Radial20_EmitVariantD (7) | BattleAnim_StartEffect77DoubleTextureTrail_Seed |
| BattleAnim_SpawnOwnerModelTintPulseDetach | func_8435839C (117), func_84358288 (8), BattleAnim_StartEffect14ModelAnimAndMoveKeyedTintParticles (5), func_84357CC8 (5), BattleAnim_StartEffect3ModelTintAndVerticalFade (2), func_8434D938 (2) |  |
| Battle_ResetModelToIdleAnim | BattleScene_HideOpponentOwner (90046), BattleScene_HideActiveOwner (70484), BattleScene_UpdateOwnerVisibilitySingle (3033), BattleScene_SelectNextIntroCameraShot (1600), BattleScene_InitializeBattlePresentation (1240), BattleScene_CompleteHpUpdateTransition (609) | BattleScene_Phase16, BattleScene_PhaseAttackHitRecoil, BattleScene_PhaseCheckFaintedEndTurn |
| BattleScene_SelectCameraShot | BattleScene_RunActionEffectHpTransition (563), BattleScene_AdvanceSendOutPreparation (143), BattleScene_AdvanceSendOutCamera (140), BattleScene_PhaseMoveFailedMessage (114), BattleScene_RunRepeatedHitHpTransition (68), BattleScene_PhaseDrainDamage (45) | BattleScene_AdvanceSendOutFollowupCamera, BattleScene_FrameCamera, BattleScene_Phase16 |
| BattleAnim_InitModel | BattleAnim_CallbackStartEffect8ModelBurstEmitter (450), BattleAnim_SelectParticleModelFrames (64), BattleAnim_CallbackStartEffects95To97EndEffects62And63ModelFadeIn_InitParticle (14), BattleAnim_CallbackStartEffects71And74And111SpecialModelColorCycle_InitParticle (10), BattleAnim_CallbackStartEffect34SpecialModelFadeIn_InitParticle (10), BattleAnim_InitEffects63And75Model38 (6) | BattleAnim_InitEffect86Model |
| Battle_ClearRuntimeFlags | BattleAnim_TickParticipantStatusAnimation (26687), BattleScene_CleanupActionPresentation (743), BattleScene_AdvanceSendOutPreparation (143), BattleScene_PrepareReplacementTransition (132), BattleScene_CleanupParticipantHideFlag (27), BattleScene_CleanupOpponentSpecialAnimFlag (20) | BattleAnim_ClearFreezeStatusAnimation, BattleAnim_ClearFreezeStatusAnimationIfThawed, BattleAnim_ClearSleepStatusAnimati |
| BattleAnim_CalculateDistanceAngles | BattleScene_ApplyCameraShake (325678), BattleScene_AnimateCameraPreset (127788), BattleScene_FrameCamera (127316), BattleScene_IsOwnerInViewWedgeAlt (107802), BattleScene_ApproachCameraTarget (20848), BattleScene_ApproachCameraZoom (12642) | BattleScene_ApproachCameraActivePokemonHeightShot |
| BattleScene_ClearCameraOffsetVectors | BattleScene_SelectCameraShot (1121), BattleScene_SelectCameraPreset (1033), BattleScene_SelectCameraFollowPreset (834), BattleScene_LoadCameraPreset (728), BattleScene_CameraShotTargetStandard (715), BattleScene_LoadCameraPresetUnscaled (620) | BattleScene_SetCameraActivePokemonHeightShot |
| func_843708CC | func_84372670 (650), Battle_Effect_LeechSeed (45), Battle_Effect_LowerStat (12), Battle_Effect_Sleep (8), Battle_Effect_Counter (6), Battle_Effect_Paralyze (4) |  |
| BattleAnim_GetCameraContext | BattleAnim_GetCameraEyeAngles (325678), BattleAnim_GetCameraLookAngles (325678), DoubleTextureTrail_DrawAll (802), BattleAnim_SetupReflectiveCamera (776), BattleAnim_SixSlotTrail_DrawAll (559), Trail40_Reset (476) | BattleAnim_GetCameraLookYaw, BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset |
| BattleScene_ResetParticipantModelFlags | BattleAnim_StatusIdleCase2Sub1 (193), BattleAnim_StatusIdleCase1Sub1 (122), BattleAnim_StatusIdleCase15Sub1 (26), BattleAnim_StatusIdleCase5Sub2 (20), BattleAnim_StatusIdleCase4Sub2 (8), BattleAnim_StatusIdleCase13Sub2 (8) | BattleAnim_StatusIdleCase10Sub1, BattleAnim_StatusIdleCase9Sub0, BattleAnim_StatusIdleCase9Sub1, BattleAnim_UpdateOwnerS |
| BattleScene_PositionParticipantModel | BattleScene_InitializeParticipantSecondaryModels (244), BattleScene_ReinitParticipantBodyModel (136), BattleScene_InitializeParticipantModel (122), BattleAnim_StatusIdleCase6Sub2 (45), BattleAnim_StatusIdleCase6Sub1 (38), BattleAnim_StatusIdleCase8Sub0 (4) |  |
| BattleScene_ReframeAndRecomputeDistance | BattleScene_ApplyActionCameraMode (47266), BattleScene_ActionSetupAnimationVariantA (1798), BattleScene_SetCameraPresetWide (948), BattleScene_RunRepeatedHitHpTransition (686), BattleScene_ActionSetupAnimationVariantB (507), BattleScene_Phase30 (259) |  |
| BattleScene_WaitForDisplayedHpAnimations | BattleScene_Phase7 (38), BattleScene_Phase13 (20), BattleScene_Phase17 (15), BattleScene_Phase14 (7), BattleScene_Phase31 (6), BattleScene_Phase27 (6) |  |
| BattleAnim_SpawnDescriptor25AlphaFadeInHoldOut | BattleAnim_StartEffect14ModelAnimAndMoveKeyedTintParticles (5), func_8433BECC (2), func_8433B988 (2), func_8433BD08 (2), func_8433BC38 (2), func_8434DA94 (2) | BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup |
| BattleScene_SelectActionCameraPresetFromTiming | BattleScene_ActionSetupAnimationDefault (693), BattleScene_Phase7 (39), BattleScene_Phase17 (12), BattleScene_Phase14 (7), BattleScene_Phase27 (6), BattleScene_Phase30 (3) |  |
| BattleAnim_QueueEffectList3 | BattleScene_PhaseWithdrawSwitch (260), BattleScene_TickQueueList3Selector2OrC (141), BattleScene_QueueRowEffectList3 (141), BattleScene_IntroSendOutSecondMon (61), BattleScene_IntroNarrowFov (61), BattleScene_IntroRevealFirstMon (61) | BattleScene_TickQueueList3Selector2OrCFromB10, func_843060EC |
| Battle_SetRuntimeFlags | BattleScene_AdvanceSendOutCamera (139), func_84372670 (28), BattleScene_Phase13 (20), BattleAnim_UpdateSleepStatusAnimation (6), BattleAnim_StartFreezeStatusAnimation (2), Battle_ClearOpponentHopFlagAndSyncMove (2) | BattleScene_AdvanceSendOutFollowupCamera, BattleScene_EnterRow21StartAnimRecord0, BattleScene_EnterRow3StartMoveAnim, Ba |
| BattleAnim_EmitParticleBurst | BattleAnim_CallbackStartEffect8ModelBurstEmitter (14768), BattleAnim_CallbackStartEffects29And60And104And110DefaultDescriptorBurstEmitter_InitParticle (288), BattleAnim_CallbackStartEffect101ModelBurstEmitter (65), BattleAnim_CallbackEndEffects37And38Descriptor34BurstEmitter (54), BattleAnim_CallbackMove140EndEffect65EmitDescriptor4A (35), BattleAnim_SetupEndEffect52DefaultBurstChild (16) | BattleAnim_CallbackDescriptor26BurstEmitter, BattleAnim_CallbackDescriptor50BurstEmitter |
| BattleAnim_BuildAnimatedTileDisplayListFromStruct | func_8434729C (538), func_8434719C (538), func_84345338 (516), func_8434523C (516), func_84347BF4 (438), func_84348F24 (350) |  |
| BattleScene_SelectNextIntroCameraShot | BattleScene_UpdateIntroCameraSequence (630), BattleScene_SkipIntroIfFainted (113), BattleScene_IntroCameraFinalHoldStep (17), BattleScene_IntroCameraPresetHoldStep (12), BattleScene_IntroCameraAnimationHoldStep (10), BattleScene_IntroCameraRollStep (7) | BattleScene_IntroCameraHeightShotStep, BattleScene_IntroCameraStep16, BattleScene_IntroCameraStep18, BattleScene_IntroCa |
| BattleAI_RandomBelowInclusive | func_8437B0CC (3519), BattleAI_ChooseAction (502), BattleAI_SelectBestScoredMove (432), BattleAI_SelectRandomFallbackOrder (409), BattleAI_SelectBestAttackTargetIndex (112), BattleAI_SimulateStatBoostGuess (12) | BattleAI_BuildMoveCandidateScores, BattleAI_InsertRankedCandidateGroup, BattleAI_IsRestrictedRandomMove, BattleAI_Resolv |
| BattleScene_SetPhaseFlag | BattleScene_ActionSetupCameraStep (859), BattleScene_ActionSetupAnimationDefault (693), BattleScene_RunActionEffectHpTransition (563), BattleScene_ActionSetupAnimationVariantC (20), BattleScene_RunBasicHpTransition (15), BattleScene_ActionSetupAnimationVariantB (11) |  |
| BattleAnim_GetOwnerModelHeight | BattleAnim_GetOwnerDefaultAnchorPosition (78074), BattleAnim_StatusIdleCase5Sub2 (2240), BattleScene_Phase13 (1830), BattleScene_UpdateCameraVerticalFraming (1140), BattleScene_SetCameraPresetLowClose (140), BattleScene_IntroCameraStep4Or6 (133) | BattleScene_UpdateCameraModelOffset |
| BattleScene_IsPartnerPikachu | BattleScene_EnterRow3StartMoveAnim (786), BattleScene_EnterRow4StartHitReactionAnim (484), BattleScene_EnterRowAnim166WithCry (387), BattleScene_TickRow18PlayCryAndSpeciesSound (266), BattleScene_TickRow16PlayCryAndSpeciesSound (244), BattleScene_AdvanceSendOutPreparation (143) |  |
| BattleScene_ApplyActionCameraMode | BattleScene_ActionSetupAnimationDefault (50889), BattleScene_Phase7 (3602), BattleScene_PhaseSelectMoveAnim (1848), BattleScene_Phase14 (490), BattleScene_Phase27 (246), BattleScene_Phase30 (180) |  |
| Battle_IsRowAnimationSettled | BattleScene_ActionSetupAnimationDefault (37513), BattleScene_Phase7 (3032), BattleScene_ActionSetupAnimationVariantA (1775), BattleScene_ActionSetupAnimationVariantB (507), BattleScene_Phase30 (180), BattleScene_Phase29 (174) |  |
| Battle_GetMoveEffectListEntry | BattleScene_ReinitParticipantBodyModel (136), BattleScene_ReinitParticipantSecondaryModels (136), BattleScene_ReinitParticipantOwnerModel (133), Battle_LoadOwnerModelForSwitchIn (132), BattleScene_InitializeParticipantModel (122), BattleScene_InitializeParticipantOwnerModel (122) |  |
| BattleAnim_SetModelEventTrackFromRow | BattleAnim_TickParticipantStatusAnimation (4986), BattleScene_EnterRow3StartMoveAnim (786), BattleScene_SetOpponentEventTrackA6 (387), func_84303E58 (45), func_8430506C (32), BattleScene_EnterRow8StartMappedMoveAnim (28) | BattleAnim_EndSleepStatusAnimation, BattleAnim_UpdateSleepStatusAnimation |
| BattleAnim_SpawnOwnerModelTintPaletteApproachInactive | func_8433766C (2), func_8434CC68 (2), func_8434E02C (2), func_8433C604 (2), func_8433C3A0 (2), BattleAnim_EndEffect25Descriptor13DelegateSequence (1) | func_8433BF54 |
| BattleScene_AdvanceOwnerAnimation98 | BattleScene_RunActionEffectHpTransition (565), BattleScene_PhaseMoveFailedMessage (114), BattleScene_RunRepeatedHitHpTransition (47), BattleScene_RunBasicHpTransition (15), BattleScene_PhaseFaint (11), BattleScene_RunConditionalHpTransition (10) | BattleScene_Phase16 |
| BattleScene_CleanupParticipantEffectMode | BattleScene_TickCameraSubstateStripWipe (1996), BattleScene_ActionSetupCameraStep (1982), BattleScene_TickCameraSubstateDefault (510), BattleScene_ResetOwnerStatusPresentation (143), BattleScene_PhaseMoveFailedMessage (114), BattleScene_PhaseAttackHit (56) | BattleScene_PhaseAttackHitRecoil, BattleScene_PhaseCheckFaintedEndTurn |
| BattleAnim_PlaySpeciesReactionSound | BattleScene_TickRow3PostEffectCamera (782), BattleScene_TickRow4PostEndCamera (606), BattleScene_TickRow19PostEffectCamera (31), BattleScene_TickRow8PlayMoveEffectScriptB (28), BattleScene_TickRow11PostEndCamera (17), BattleScene_TickRow14PlayMoveEffectEnd (15) | BattleScene_Row12Slot1CameraCleanup |
| BattleAnim_EffectSecondaryOwnerModelScale | BattleAnim_StartEffect11Trail50Alt_Update (574), BattleAnim_StartEffect57Trail50_Update (246), BattleAnim_StartEffect7TexturedRibbonSheet_Emit (107), BattleAnim_StartEffect19Radial20_EmitVariantD (35), BattleAnim_Effect5RibbonMesh_Init (30), BattleAnim_StartEffect33Radial20_EmitVariantC (16) |  |
| BattleScene_UpdateDisplayedHpAndTransition | BattleScene_CompleteHpUpdateTransition (609), BattleScene_PhaseMoveFailedMessage (114), BattleScene_WaitForDisplayedHpAnimations (100), BattleScene_PhaseDrainDamage (45), BattleScene_PhaseFaint (7), BattleScene_Phase10 (3) | BattleScene_Phase16 |
| BattleAnim_EffectSecondaryOwnerAnchorPosition | BattleAnim_StartEffect11Trail50Alt_Update (574), BattleAnim_StartEffect57Trail50_Update (246), BattleAnim_CalcVectorAtToDm (138), BattleAnim_CalcVectorAtToDm2 (107), BattleAnim_StartEffect42Trail40_Init (4), BattleAnim_StartEffect44Trail40GridAlt_Init (2) |  |
| BattleAnim_PlayBattleSequenceById | BattleScene_PhaseDrainDamage (45), BattleScene_Row13PlaySequences14And21 (13), BattleScene_TickRow11QueueEffectList6 (12), BattleScene_EnterRow15StartAnimA80Seq30 (11), BattleAnim_UpdateSleepStatusAnimation (6), BattleScene_TickRow14PlayMoveEffectEnd (3) | BattleAnim_TriggerStatusAfflictionCry |
| BattleAnim_RandomRange | BattleAnim_TickParticipantStatusAnimation (212623), BattleScene_InitializeIntroCamera (620), BattleScene_IntroCameraStep4Or6 (399), BattleScene_IntroCameraStep8Or10 (161), BattleScene_PhaseMoveFailedMessage (113), BattleScene_Phase30 (3) | BattleAnim_SetModelEventTrackFromRow, Battle_AdvanceSecondaryModelAnimIfFinished, Battle_ResetModelToIdleAnim, func_8430 |

## Runtime edges the static graph does not have (365)

Calls through pointers stored at run time, or entries from main code (fragment entry points, process callbacks).

| caller | via | callee | count |
|---|---|---|---|
| BattleScene_TickStateCallbacks |  | BattleScene_StateCallbackNop | 2150907 |
| BattleScene_UpdateFrame | Geo_NodePerspective | Battle_StageMaterialTintCallback | 976269 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects133And134And143Descriptor44ModeOrbitSwirl | 906954 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAsset1BI4TriangleA | 437568 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect135Descriptor1CRisingScaleFade | 366776 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackSharedDescriptors42And44ScaleFall | 352321 |
| BattleScene_UpdateFrame | Geo_NodeModelRoot | Battle_StageGeoDispatchCallback | 325423 |
| BattleScene_UpdateFrame | Geo_NodeModelRoot | Battle_StageLightAngleCallbackB | 325423 |
| BattleScene_UpdateFrame | Geo_NodeModelRoot | Battle_StageLightAngleCallbackA | 325423 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAsset76Ia8FrameWithAsset77I4OverlayPipelineB | 323036 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect141Descriptor1CGridRiseFade | 267112 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect8Descriptor4ABurstChild | 236532 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackSharedDescriptors3EAnd3FAnimatedRiseFade | 226768 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffect59Descriptor4FRotatingColorScaleFade | 207404 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect139Descriptor42FallingScaleFade | 87780 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects23And35And84Descriptor4FColorCycleScaleFade | 85176 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffects15And22Descriptor4ERotatingColorFadeVariant1 | 69224 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect139Descriptor22SpinFadeSpark | 65880 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAsset76Ia8FrameWithAsset77I4OverlayPipelineA | 61952 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects29And60And104And110Descriptor44BurstChild | 51861 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffects15And22And59Descriptor3DRotatingAlphaScaleFade | 41846 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackSharedDescriptor3DAnimatedColorMotion | 34160 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects23And35And84Descriptor3DAlphaScaleFade | 31310 |
| BattleScene_TickStateCallbacks |  | BattleScene_TickRow3PlayMoveEffectScript | 30771 |
| BattleScene_TickStateCallbacks |  | BattleScene_TickRow3PostEffectCamera | 30771 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffects15And22Descriptor4ERotatingColorFadeVariant0 | 30720 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects23And35And84Descriptor4FRotatingFallColorCycle | 30156 |
| BattleScene_TickStateCallbacks |  | BattleScene_TickRow3QueueEffectList6 | 26883 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffect59Descriptor4FColorScaleFade | 26197 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackSharedAnimatedVerticalFade | 22127 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect135ExpandingFlashFade | 21336 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackOwnerModelTintRampDetach | 21277 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect69EndEffect78Descriptor4AFallFade | 20800 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffect26Descriptor2CGravityScatterFall | 16965 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAsset1ERgba16Quad | 16059 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect8ModelBurstEmitter | 15443 |
| BattleScene_TickStateCallbacks |  | BattleScene_TickPlaySpeciesSequence33 | 14139 |
| BattleScene_TickStateCallbacks |  | BattleScene_TickQueueRowEffectList3AtAnimEnd | 14139 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect69EndEffect78Descriptor30YThresholdFall | 12892 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects77And120Descriptor45AimedSparkFade | 12857 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackGlobalTintColorFadeDetach | 11822 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackMove73EndEffect31SpawnDescriptor42 | 11304 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffects39And41And45And46And83Descriptor3DMetadataBurstChild | 10540 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAssetBcRgba16WithAssetBdDisplayList | 10472 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects28And53And65And76Descriptor47AnimatedScaleMotion | 10255 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect69ModelPhaseWait | 9423 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffect132Descriptor39DelayedRiseFade | 9374 |
| BattleAnim_RenderEffectsFrame | Particle_RenderOrthoParticles | BattleAnim_DrawParticleFullScreenColorOverlay | 9125 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAsset13FrameI4YScaledQuadA | 9072 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffects34And53Descriptor45RotatingBounceFade | 7608 |
| BattleScene_TickStateCallbacks |  | BattleScene_TickQueueList3Selector2OrC | 6651 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects18And73Descriptor11ScatterRiseFade | 6648 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackStartEffects133And134And143Descriptor38MetadataBurstEmitter | 6203 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAsset13FrameI4YScaledQuadC | 6188 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffects39And41And45And46And83Descriptor34BurstEmitter | 6008 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackDescriptor25AlphaHoldFadeOut | 5790 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackSharedDescriptor34DelayedFrameMotion | 5703 |
| BattleAnim_RenderEffectsFrame | Particle_RenderWorldChains | BattleAnim_DrawAsset13FrameI4ModelQuadB | 5327 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackEndEffect23Descriptor44ScalePulse | 5192 |
| BattleAnim_UpdateParticlePool |  | BattleAnim_CallbackSharedDescriptor19DelayedAnimatedGrowFade | 5192 |

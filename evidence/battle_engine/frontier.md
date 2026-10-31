# Battle engine: what keeps code unreached

Runs: s1, s2, s3, s5, s7, s9, mv1, mv101, mv105, mv109, mv113, mv117, mv121, mv125, mv129, mv13, mv133, mv137, mv141, mv145, mv149, mv153, mv157, mv161, mv165, mv17, mv21, mv25, mv29, mv33, mv37, mv41, mv45, mv49, mv5, mv53, mv57, mv61, mv65, mv69, mv73, mv77, mv81, mv85, mv89, mv9, mv93, mv97, rules0, rules2, counter, rest, spect, call, g000, g001, g002, g003, g004, g005, g006, g007.

| | covered | total | % |
|---|---|---|---|
| functions | 1704 | 2055 | 82.9 |
| lines | 28868 | 39104 | 73.8 |
| branches | 10585 | 16746 | 63.2 |
| regions | 15628 | 22112 | 70.7 |

127 gates (a reached caller, an unreached callee); 154 unreached functions are behind them, 197 have no reached caller anywhere above them (entry points or callbacks that nothing in these runs registered).

## Gates by what decides them

| class | gates | unreached functions behind them |
|---|---|---|
| table index (which move / effect / animation) | 50 | 65 |
| other state | 28 | 37 |
| battle scene / presentation state | 27 | 28 |
| AI decision | 8 | 16 |
| input | 8 | 9 |
| battle state (moves, status, HP, flags) | 6 | 4 |

### table index (which move / effect / animation)

| unlocks | caller -> callee | condition never taken | line |
|---|---|---|---|
| 5 | BattleAnim_RunEffectInitEnd -> func_8433E33C | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 4 | BattleAnim_RunEffectInitEnd -> func_8433C284 | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 3 | BattleAnim_RunEffectInitEnd -> func_843375B0 | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 3 | BattleAnim_RunEffectInitEnd -> func_843574FC | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 3 | BattleAnim_RunEffectInitStart -> func_84345EC0 | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 2 | BattleAnim_RunEffectInitEnd -> func_84345B28 | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 2 | BattleAnim_RunEffectList -> BattleAnim_StartEffect77DoubleTextureTrail_Init | `index into gMoveAnimEffectStartFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitEnd -> func_8433BF54 | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitEnd -> func_8434917C | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitEnd -> func_8434BCD4 | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitEnd -> func_8434C5F0 | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitEnd -> func_8434EA10 | `index into gMoveAnimEffectEndInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> BattleAnim_StartEffect122GlobalAndOwnerTintFade | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> BattleAnim_StartEffect133ModeOrbitBurstAndEffect135Followup | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> BattleAnim_StartEffect143ModeOrbitAndMetadataBurst | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_8433765C | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_843378CC | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_84349A14 | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_8434C590 | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_8434EFBC | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_84356D04 | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_8435727C | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_8435742C | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectInitStart -> func_84358EDC | `index into gMoveAnimEffectStartInitFuncs` (- side) |  |
| 1 | BattleAnim_RunEffectList -> BattleAnim_DefaultEndEffect_Draw | `index into gMoveAnimEffectEndDrawFuncs` (- side) |  |
| ... | 25 more | | |

### other state

| unlocks | caller -> callee | condition never taken | line |
|---|---|---|---|
| 7 | BattleScene_TickAttractModeInput -> BattleAI_ResolveRandomMove | `sp24->unk_2A == 0` (true side) | 1208 |
| 4 | BattleHud_DrawOpponentSideStatus -> BattleHud_DrawMoveSelectMenu | `arg1->unk_1C == 1` (true side) | 1900 |
| 4 | BattleHud_DrawOwnSideStatus -> BattleHud_DrawMoveSelectMenu | `arg1->unk_1C == 1` (true side) | 1841 |
| 3 | BattleHud_DrawOpponentSideStatus -> BattleHud_DrawDoublesTeamStatusList | `arg0->unk_724->numberOfPokemonChosen < 4` (true side) | 1887 |
| 3 | BattleHud_DrawOpponentSideStatus -> BattleHud_DrawTeamStatusList | `arg0->sessionTeams->teamMember[1]->unk_001 < arg0->sessionTeams->teamMember[0]->unk_001` (true side) | 1878 |
| 3 | BattleHud_DrawOwnSideStatus -> BattleHud_DrawDoublesTeamStatusList | `arg0->unk_724->numberOfPokemonChosen < 4` (true side) | 1828 |
| 3 | BattleHud_DrawOwnSideStatus -> BattleHud_DrawTeamStatusList | `arg0->sessionTeams->teamMember[1]->unk_001 < arg0->sessionTeams->teamMember[0]->unk_001` (true side) | 1819 |
| 3 | BattleScene_HandlePlayerMoveSelection -> BattleScene_HandleNoUsableMoveSelection | `?` (true side) | 20 |
| 2 | BattleBox_Update -> BattleHud_InitCaptionBoxPopupBlack | `temp_s0->unk_04 == 0xD` (true side) | 636 |
| 2 | BattleBox_Update -> BattleHud_UpdateCaptionBoxPopupBlack | `BattleHud_UpdateCaptionBoxPopupBlack(temp_s0, 0xA0, 0x78) != 0` (true side) | 648 |
| 2 | BattleScene_UpdateResultPresentation -> BattleScene_DetermineWinner | `var_v0 != 0` (true side) | 157 |
| 1 | BattleAI_ScoreAttackVsSwitch -> BattleAI_ScoreCategorySetLowPP | `(sp58 != 0)` (true side) | 4761 |
| 1 | BattleAnim_DispatchParticleEmission -> BattleAnim_SpawnBatchedParticles | `switch (arg0->unk_16) case 18` (true side) | 432 |
| 1 | BattleAnim_SelectParticleModelFrames -> BattleAnim_ApplySpeciesYOffsetVariantB | `switch (Particle_FieldCF_GetS16_0C058(arg0)) case 2` (true side) | 114 |
| 1 | BattleAnim_SixSlotTrail_DrawAll -> BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset | `var_s0->unk_00 <= 0.0` (true side) | 331 |
| 1 | BattleHud_DrawOpponentSideStatus -> BattleHud_DrawTwoOptionCursorMenu | `switch (arg1->unk_10) case 4` (true side) | 1907 |
| 1 | BattleHud_DrawOpponentSideStatus -> BattleHud_DrawTwoOptionLabelBar | `arg1->unk_2D == 0x10` (false side) | 1892 |
| 1 | BattleHud_DrawOwnSideStatus -> BattleHud_DrawTwoOptionCursorMenu | `switch (arg1->unk_10) case 4` (true side) | 1848 |
| 1 | BattleHud_DrawOwnSideStatus -> BattleHud_DrawTwoOptionLabelBar | `arg1->unk_2D == 0x10` (false side) | 1833 |
| 1 | BattleScene_AdvanceDoublesOwnerAnimation -> BattleScene_ApplyModelAnimationSlot5A | `arg0->sessionTeams->uniqueStatusFlags & 0x10` (true side) | 198 |
| 1 | BattleScene_AdvanceOwnerAnimation -> BattleScene_ApplyModelAnimationSlot5A | `arg0->sessionTeams->uniqueStatusFlags & 0x10` (true side) | 90 |
| 1 | BattleScene_HandleActionSelectionCancel -> BattleAnim_PlaySoundEffect3 | `sp2C->unk_04 == 2` (true side) | 366 |
| 1 | BattleScene_HandleActionSelectionCancel -> BattleScene_SelectActiveController | `sp2C->unk_04 == 2` (true side) | 366 |
| 1 | BattleScene_RevealParticipantMessageChars -> BattleHud_DrawTextBox | `arg0 == sSide1ActivePokemon` (true side) | 721 |
| 1 | BattleScene_TickHudTimers -> BattleHud_DrawParticipantTimer | `sp1C->unk_10 != 3` (true side) | 737 |
| ... | 3 more | | |

### battle scene / presentation state

| unlocks | caller -> callee | condition never taken | line |
|---|---|---|---|
| 5 | BattleScene_UpdateIntroCameraSequence -> BattleScene_IntroCameraHeightShotSwapStep | `switch (sBattleScene.unk_00->unk_20) case 17` (true side) | 602 |
| 4 | BattleScene_UpdatePhase -> BattleScene_PhaseSendOutAfterFaint | `switch (sBattleScene.unk_00->unk_38) case 34` (true side) | 3552 |
| 3 | BattleScene_HandlePartySlotCursorInput -> BattleScene_ValidateSelectedPartyMember | `BattleScene_ValidateSelectedPartyMember(arg0) != 0` (true side) | 791 |
| 3 | BattleScene_SelectNextIntroCameraShot -> BattleScene_IntroCameraStep16 | `switch (sBattleScene.unk_00->unk_20) case 16` (true side) | 444 |
| 3 | BattleScene_SelectNextIntroCameraShot -> BattleScene_IntroCameraStep18 | `switch (sBattleScene.unk_00->unk_20) case 18` (true side) | 448 |
| 2 | BattleScene_HandlePartySlotCursorInput -> BattleScene_QueuePartySelectionConfirmMessage | `BattleScene_ValidateSelectedPartyMember(arg0) != 0` (true side) | 791 |
| 2 | BattleScene_SelectNextIntroCameraShot -> BattleScene_IntroCameraStep12Or14 | `switch (sBattleScene.unk_00->unk_20) case 12` (true side) | 436 |
| 2 | BattleScene_UpdateIntroCameraSequence -> BattleScene_IntroCameraHeightShotStep | `switch (sBattleScene.unk_00->unk_20) case 19` (true side) | 606 |
| 1 | BattleScene_HandlePartySlotCursorInput -> BattleAnim_PlaySoundEffect8 | `BattleScene_ValidateSelectedPartyMember(arg0) != 0` (true side) | 791 |
| 1 | BattleScene_PhaseSendOut -> BattleScene_AdvanceSendOutFollowupCamera | `switch (sBattleScene.unk_00->unk_20) case 10` (true side) | 1130 |
| 1 | BattleScene_PhaseSendOut -> BattleScene_CompleteSendOutExtendedFade | `switch (sBattleScene.unk_00->unk_20) case 8` (true side) | 1126 |
| 1 | BattleScene_PhaseSendOut -> func_84321D34 | `switch (sBattleScene.unk_00->unk_20) case 5` (true side) | 1114 |
| 1 | BattleScene_RunFinalPresentation -> BattleScene_FinalPresentationFrameActiveOwner | `switch (sBattleScene.unk_00->unk_1C) case 0` (true side) | 53 |
| 1 | BattleScene_RunFinalPresentation -> BattleScene_FinalPresentationFrameOwner0 | `switch (sBattleScene.unk_00->unk_1C) case 3` (true side) | 66 |
| 1 | BattleScene_Update -> func_843297E8 | `switch (sBattleScene.unk_00->unk_1C) case 0` (true side) | 710 |
| 1 | BattleScene_Update -> func_843297F0 | `switch (sBattleScene.unk_00->unk_1C) case 3` (true side) | 723 |
| 1 | BattleScene_UpdateIntroCameraSequence -> BattleScene_IntroCameraWideHoldStep | `switch (sBattleScene.unk_00->unk_20) case 13` (true side) | 594 |
| 1 | BattleScene_UpdateIntroSequence -> BattleScene_IntroStepNop | `switch (sBattleScene.unk_00->unk_20) case 10` (true side) | 228 |
| 1 | BattleScene_UpdatePhase -> BattleScene_Phase16 | `switch (sBattleScene.unk_00->unk_38) case 16` (true side) | 3540 |
| 1 | BattleScene_UpdatePhase -> BattleScene_PhaseAttackHitRecoil | `switch (sBattleScene.unk_00->unk_38) case 35` (true side) | 3534 |
| 1 | BattleScene_UpdatePhase -> BattleScene_PhaseCheckFaintedEndTurn | `switch (sBattleScene.unk_00->unk_38) case 33` (true side) | 3531 |
| 1 | Battle_HasUsableMajorStatusTarget -> BattleScene_GetOpposingSelectedSideTeamSize | `i < BattleScene_GetOpposingSelectedSideTeamSize(active_pokemon)` (true side) | 433 |
| 1 | Battle_HasUsableSleepStatusTarget -> BattleScene_GetOpposingSelectedSideTeamSize | `i < BattleScene_GetOpposingSelectedSideTeamSize(active_pokemon)` (true side) | 406 |
| 1 | func_84372C40 -> BattleScene_GetOpposingSelectedSideTeamSize | `i < BattleScene_GetOpposingSelectedSideTeamSize(arg0)` (true side) | 1640 |
| 1 | func_84372D88 -> BattleScene_GetOpposingSelectedSideTeamSize | `i < BattleScene_GetOpposingSelectedSideTeamSize(arg0)` (true side) | 1670 |
| ... | 2 more | | |

### AI decision

| unlocks | caller -> callee | condition never taken | line |
|---|---|---|---|
| 9 | func_843831A0 -> BattleAI_TryBuildScoredOrder | `BattleAI_TryBuildScoredOrder(D_843C60A8, D_843C60A4, sp38, NULL, rule_category) == 0` (true side) | 5455 |
| 2 | BattleAI_ChooseAction -> BattleAI_ResolveTiedActionChoice | `(D_843C60AC->unk_04 == 1)` (true side) | 5665 |
| 1 | BattleAI_BuildMoveCandidateScores -> BattleAI_AllCandidatesResistType | `BattleAI_AllCandidatesResistType(gMoveData[var_s2->unk_00 - 1].unk_03, D_843C60A8) == 0` (true side) | 1702 |
| 1 | BattleAI_ChooseAction -> BattleAI_SelectRandomUsableMoveSlot | `BattleAI_HasExactlyOneUsableCandidate(D_843C60A4) != 0` (true side) | 5676 |
| 1 | func_8437B0CC -> BattleAI_HasOtherCandidateAtLeastScore | `BattleAI_HasOtherCandidateAtLeastScore(arg3, i, arg2, temp_s7->unk_0C * 2) != 0` (true side) | 2263 |
| 1 | func_8437B0CC -> BattleAI_ScaleStatHigh | `temp_s3->unk_2E < BattleAI_ScaleStatHigh(temp_s6)` (true side) | 2584 |
| 1 | func_8437B0CC -> BattleAI_ScaleStatLow | `BattleAI_ScaleStatLow(temp_s3) < temp_s6->unk_2E` (true side) | 2766 |
| 1 | func_8437B0CC -> BattleAI_ScoreCategorySetReady | `BattleAI_ScoreCategorySetReady(temp_fp, temp_s4) != 0` (true side) | 3260 |

### input

| unlocks | caller -> callee | condition never taken | line |
|---|---|---|---|
| 4 | BattleScene_HandlePlayerMoveSelection -> BattleScene_SelectMoveFromButtons | `!BTN_IS_DOWN` (true side) | 996 |
| 1 | BattleScene_HandlePlayerMoveSelection -> BattleAnim_PlaySoundEffect1 | `!BTN_IS_DOWN` (true side) | 996 |
| 1 | BattleScene_HandlePlayerMoveSelection -> BattleAnim_PlaySoundEffect3 | `!BTN_IS_DOWN` (true side) | 996 |
| 1 | BattleScene_HandlePlayerPartySelection -> BattleAnim_PlaySoundEffect1 | `(D_8439014C->buttonPressed & 0x10)` (true side) | 833 |
| 1 | BattleScene_HandlePlayerPartySelection -> BattleAnim_PlaySoundEffect3 | `!(D_8439014C->buttonDown & 0x10)` (true side) | 826 |
| 1 | BattleScene_HandlePlayerPartySelection -> BattleScene_SelectActiveController | `!(D_8439014C->buttonDown & 0x10)` (true side) | 826 |
| 1 | BattleScene_TickAttractModeInput -> BattleScene_HideParticipantOwnerAndSecondaryModels | `cont->buttonPressed & 0x10` (true side) | 1200 |
| 1 | BattleScene_TickAttractModeInput -> BattleScene_ShowParticipantOwnerAndSecondaryModels | `cont->buttonPressed & 0x20` (true side) | 1194 |

### battle state (moves, status, HP, flags)

| unlocks | caller -> callee | condition never taken | line |
|---|---|---|---|
| 1 | BattleAnim_ClearFreezeStatusAnimation -> BattleAnim_RestoreBaseModelAnimation | `arg0->state.unk_34 & 2` (true side) | 43 |
| 1 | BattleAnim_ClearFreezeStatusAnimationIfThawed -> BattleAnim_RestoreBaseModelAnimation | `!(arg0->state.runtime.unk_15 & 0x20)` (true side) | 64 |
| 1 | BattleAnim_ResetFreezeStatusAndRestoreAnimation -> BattleAnim_RestoreBaseModelAnimation | `arg0->state.unk_34 & 2` (true side) | 54 |
| 1 | BattleScene_HandlePlayerActionInput -> BattleScene_ForfeitPrompt | `switch (arg0->state.unk_04) case 3` (true side) | 1085 |
| 1 | BattleScene_TickHudTimers -> BattleHud_DrawBattleTimer | `(sActivePokemon[0]->unk_728.unk_168->viewport.x == 0x17)` (true side) | 734 |
| 1 | Battle_ResetEventTrack -> BattleAnim_TrySetModelEventTrackFromRow | `active_pokemon->state.unk_34 & 1` (true side) | 552 |

## Unreached with no reached caller above (197, first 60)

BattleAI_InitTrainerRecords, BattleAnim_ApproachS16, BattleAnim_ApproachU16, BattleAnim_Atan2sFromPoints, BattleAnim_CallbackDescriptor25AlphaHoldDetach, BattleAnim_CallbackDescriptor25PaletteFadeIn, BattleAnim_CallbackDescriptor25PaletteFadeOut, BattleAnim_CallbackDescriptor26BurstChild, BattleAnim_CallbackDescriptor26BurstChild_InitParticle, BattleAnim_CallbackDescriptor26BurstEmitter, BattleAnim_CallbackDescriptor26BurstEmitter_InitParticle, BattleAnim_CallbackDescriptor50BurstChild, BattleAnim_CallbackDescriptor50BurstChild_InitParticle, BattleAnim_CallbackDescriptor50BurstEmitter, BattleAnim_CallbackDescriptor50BurstEmitter_InitParticle, BattleAnim_CallbackOrphanDescriptor2BSpinFade, BattleAnim_CallbackOrphanDescriptor2EScatterFade, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach, BattleAnim_CallbackOrphanDescriptor34ColorFadeDetach_InitParticle, BattleAnim_CallbackOwnerModelTintHoldFadeDetach, BattleAnim_CallbackOwnerModelTintPaletteApproach, BattleAnim_CallbackOwnerModelTintPaletteApproachRampDown, BattleAnim_CallbackOwnerModelTintRandomPaletteCycle, BattleAnim_CallbackUnregisteredStraightDirectionalStreamFade, BattleAnim_CallbackUnusedAimedSparkVariant, BattleAnim_CallbackUnusedDescriptor4FColorCycleScaleFadeVariant, BattleAnim_CallbackUnusedFallingTrailVariant, BattleAnim_CallbackUnusedGenericFadeApproach, BattleAnim_CallbackUnusedGlitterVariant, BattleAnim_CallbackUnusedGrowFadeVariant, BattleAnim_CallbackUnusedGrowingSparkVariant, BattleAnim_CallbackUnusedIndexedFadeVariantA, BattleAnim_CallbackUnusedIndexedFadeVariantB, BattleAnim_CallbackUnusedRiseImpulseVariant, BattleAnim_CallbackUnusedRotatingSparkVariant, BattleAnim_CallbackUnusedVariant2, BattleAnim_ClearOwnerSecondaryStatusFlags, BattleAnim_DrawAsset13FrameDiv2I4Quad, BattleAnim_DrawAsset13FrameI4YScaledQuadB, BattleAnim_DrawAsset18FrameI4ModelQuad, BattleAnim_DrawAsset1BI4TriangleB, BattleAnim_DrawAsset1DRotatedI4Quad, BattleAnim_DrawAsset87AnimatedI4Quad, BattleAnim_DrawAsset95Ia8Billboard, BattleAnim_DrawAssetA9I4XYScaledQuad, BattleAnim_DrawParticleNoop, BattleAnim_DrawStoredColorOverlay, BattleAnim_EndEffectUnusedVerticalFadeAndDelayedFrameSequence, BattleAnim_GetCameraLookYaw, BattleAnim_GetCameraStoredAngles70, BattleAnim_GetCameraStoredAngles76, BattleAnim_GetCameraStoredYaw72, BattleAnim_GetCameraStoredYaw78, BattleAnim_GetEffectStateFlag, BattleAnim_GetOwnerModelBoundsScaled, BattleAnim_GetOwnerSpeciesBaseScaleAlias, BattleAnim_GetSecondaryOwnerSpeciesBaseScale, BattleAnim_GetSpeciesBySideIndex, BattleAnim_GetTertiaryOwnerSpecies, BattleAnim_GetTertiaryOwnerSpeciesBaseScale

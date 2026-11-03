# Coverage added by Game Pak parties (Free Battle, modified Game Boy saves)

Baseline: 48 runs; new: gb1, gb2, gb3, gb4, gb5.

Functions: baseline 1702, new runs 1264, **12 only in the new runs**.
Lines: 624 lines only in the new runs.

BattleAI_SelectRandomUsableMoveSlot, BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade, BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade_InitParticle, BattleAnim_ClearSleepStatusAnimation, BattleAnim_StartEffect143ModeOrbitAndMetadataBurst, BattleScene_ClearMonStatusAnimations, BattleScene_PhaseWithdrawFainted, BattleScene_TickAssetLoadQueueRowEffectList3, func_8435727C, func_8435742C, func_843574FC, func_84358EDC

## Source lines only the new runs reached

### src/fragments/battle_engine/battle_engine_2EC3C0.c (48 lines)

```
  188          gBattleSceneStateTickSlots[sBattleSceneStateTickSlotIndex].unk_04 = sBattleSceneStateTickSentinel;
...
  281              return lookup_result;
  282          }
...
  328                      temp_v1_2 = i / 8;
  329                      if (temp_v1_2 == D_843845CC[temp_v0_3][0]) {
  330                          var_a0 = species_data->tmHmLearnset[temp_v1_2] | D_843845CC[temp_v0_3][1];
  331                      } else {
  332                          var_a0 = species_data->tmHmLearnset[temp_v1_2];
  333                      }
...
  415          var_a1 = (uintptr_t)_70D3A0_ROM_START + ((u32)D_80075BD0[152] & 0xFFFFFF);
  416      }
...
  594                      BattleAnim_PlayMoveEffectScriptB(active_pokemon, pokemon_runtime->usedMove);
...
  740          var_v0 = sCurrentAnimation->unk_04 + 0x1E;
...
  776          var_v0 = sCurrentAnimation->unk_04 + 0x1E;
...
  843              Cry_Play(0xC8, 5);
  844          }
...
  987              func_80048060(BattleScene_GetParticipantSideIndex(active_pokemon) & 0xFF, 0, 0xC8, 0xA);
...
 1025              Cry_Play(0xC8, 3);
...
 1037              func_80048060(BattleScene_GetParticipantSideIndex(active_pokemon) & 0xFF, 0, 0xC8, 0xB);
...
 1060              Cry_Play(0xC8, 4);
...
 1072              func_80048060(BattleScene_GetParticipantSideIndex(active_pokemon) & 0xFF, 0, 0xC8, 0xB);
...
 1081  void BattleScene_TickAssetLoadQueueRowEffectList3(ActivePokemon* active_pokemon) {
 1082      UNUSED s32 pad[2];
 1083      s32 sp24 = active_pokemon->animationState;
 1084      ActivePokemonState* temp_v1 = &active_pokemon->state;
...
 1086      if (BattleAnim_TryRegisterAssetTables(active_pokemon) != 0) {
 1087          active_pokemon->sessionTeams->displayModel = active_pokemon->state.runtime.id;
 1088          active_pokemon->animationTimer = 0;
 1089          BattleScene_QueueRowEffectList3(active_pokemon, sBattleSceneStateRowTable[sp24].mode);
 1090          Audio_PlayCommand(3, 0, 0);
 1091          active_pokemon->sessionTeams->unk_04 = &active_pokemon->sessionTeams->teamMember[temp_v1->unk_2B]->pok
 1092          gBattleSceneStateTickSlots[sBattleSceneStateTickSlotIndex].unk_04 = sBattleSceneStateTickSentinel;
 1093      }
 1094  }
...
 1242          gBattleSceneStateTickSlots[sBattleSceneStateTickSlotIndex].unk_04 = sBattleSceneStateTickSentinel;
...
 1256          sp24 = sCurrentAnimation->unk_0A - 0x31;
 1257      }
...
 1261              if ((sp24 <= 0) || (sp24 == active_pokemon->animationTimer)) {
 1262                  if (sCurrentAnimation->unk_09 == 1) {
...
 1264                  } else {
 1265                      BattleAnim_QueueEffectList3(0xC, active_pokemon, active_pokemon, 0xFF, 0xFF);
 1266                  }
 1267                  Audio_PlayCommand(3, 0, 0);
 1268                  gBattleSceneStateTickSlots[sBattleSceneStateTickSlotIndex].unk_04 = sBattleSceneStateTickSenti
 1269              }
...
 2133          case 0:
 2134          case 1:
 2135          case 2:
```

### src/fragments/battle_engine/battle_engine_2F23A0.c (8 lines)

```
   29          return 1;
   30      }
...
  213          arg1->x = arg0->displayObject.position.x;
  214          arg1->y = arg0->displayObject.position.y + var_fs0;
  215          arg1->z = arg0->displayObject.position.z;
...
  310                      idx = BattleAnim_RandomRange(5);
  311                      ModelAnim_SetAnimation(&arg1->displayObject, idx + 0xE);
...
  349                  ModelAnim_SetAnimation(&active_pokemon->displayObject, BattleAnim_RandomRange(5) + 0xE);
```

### src/fragments/battle_engine/battle_engine_2F74E0.c (30 lines)

```
  273          for (i = 0; i < 4; i++) {
  274              if (((temp_v0->unk_52 >> 4) != i) && (temp_v0->moves[i] != 0)) {
  275                  var_v1 |= temp_v0->unk_32[i] & 0x3F;
  276              }
  277          }
...
  303          return 1;
  304      }
...
  414          return 1;
  415      }
...
  525                  sp18->unk_14 = 4;
  526                  sp18->unk_0C = 4;
...
  553              BattleAnim_PlaySoundEffect2();
  554              sp1C->unk_1C = 0;
  555              sp1C->unk_18 = D_84390138 - 4;
  556              sp1C->unk_1E = 0;
  557              sp1C->unk_10 = 3;
  558              sp1C->unk_04 = 2;
  559          }
...
  563          return 1;
  564      }
...
  603                  return 0;
  604              }
...
  622                      BattleScene_CompleteCpuMoveAction(arg0, arg1);
  623                  }
...
  631          BattleScene_CompleteCpuForcedAction(arg0, arg1);
  632      }
...
  694          temp_v0->unk_2D = 0xF;
  695      }
...
 1250                                  BattleScene_HandlePlayerMoveSelectionLocked(arg0, arg0->state.unk_C8);
 1251                                  sp20->usedMove = 0xFF;
```

### src/fragments/battle_engine/battle_engine_2FA4D0.c (9 lines)

```
  317          arg0 -= 2;
  318          Gfx_DrawTextureRgba16(arg0, arg1, 8, 6, PORT_BE_ADDR(temp_s4, sp58[0] - '0'), 8, 0);
  319          arg0 += 8;
  320          var_s2 = &sp58[1];
...
  377          Gfx_SetEnvColor(0xFF, 0x64, 0xFF, 0xFF);
  378      }
...
  405          Gfx_SetEnvColor(0xFF, 0x64, 0xFF, (s8)sp20);
...
  427              var_t0 = 1;
  428          }
```

### src/fragments/battle_engine/battle_engine_3020D0.c (20 lines)

```
  270          do {
  271              tmp = Battle_Random();
  272          } while ((tmp == 0) || (tmp >= 0xA5) || tmp == 0x76);
...
  274          sp24->usedMove = tmp;
...
  276          Text_SetStringToken(0x19, Battle_GetActiveMonNickname(sActivePokemon[sBattleScene.unk_00->unk_2C]));
  277          Text_SetStringToken(0x1D, Battle_GetActiveMoveName(sActivePokemon[sBattleScene.unk_00->unk_2C]));
...
  279          switch (sp2C) {
...
  292              case 3:
  293                  Battle_QueueMessage(gBattleMessageQueues->unk_0C8, 0x17);
  294                  break;
...
  299          }
...
  301          sp24->usedMove = 0x76;
...
  303          Text_SetStringToken(0x19, Battle_GetActiveMonNickname(sActivePokemon[sBattleScene.unk_00->unk_2C]));
  304          Text_SetStringToken(0x1D, Battle_GetActiveMoveName(sActivePokemon[sBattleScene.unk_00->unk_2C]));
...
  306          switch (sp2C) {
...
  319              case 3:
  320                  Battle_QueueMessage(gBattleMessageQueues->unk_388, 0x17);
  321                  break;
...
  326          }
...
  380          Audio_QueuePendingSoundId(0xBC);
```

### src/fragments/battle_engine/battle_engine_309ED0.c (97 lines)

```
  474              BattleAnim_GetOwnerDefaultAnchorPosition(sActivePokemon[sBattleScene.unk_00->unk_2C], &D_843901B0.
  475              BattleScene_LoadCameraPreset(&D_843848F0, sBattleScene.unk_00->unk_DC, sActivePokemon[sBattleScene
  476              sBattleScene.unk_00->unk_38 = 0xB;
  477          }
...
  905              Cry_Play(0xC8, 1);
...
 1356                      BattleScene_ResetOwnerPresentationFlags(D_84390200);
 1357                  }
...
 3254  void BattleScene_ClearMonStatusAnimations(ActivePokemon* arg0) {
 3255      BattleAnim_ClearFreezeStatusAnimation(arg0);
 3256      BattleAnim_ClearSleepStatusAnimation(arg0);
 3257  }
...
 3259  void BattleScene_PhaseWithdrawFainted(CameraNode* arg0) {
 3260      ActivePokemonState* sp34 = &sActivePokemon[sBattleScene.unk_00->unk_2C]->state;
 3261      ActivePokemon* temp_s0 = sActivePokemon[sBattleScene.unk_00->unk_2C];
 3262      BattleMonRuntime* sp2C = &sActivePokemon[sBattleScene.unk_00->unk_2C]->state.runtime;
...
 3264      if (sBattleScene.unk_00->unk_2C) {}
...
 3266      switch (sBattleScene.unk_00->unk_20) {
 3267          case 0:
 3268              if (sBattleScene.unk_00->unk_34 == 1) {
...
 3273              } else if (BattleScene_AnimateCameraPreset(arg0, temp_s0, 1, 1) != 0) {
 3274                  BattleBox_Open(0xA);
 3275                  func_8003F4C0(BattleScene_GetParticipantDisplayPlayerNumber(temp_s0));
 3276                  sBattleScene.unk_00->unk_20 += 1;
 3277              }
 3278              break;
...
 3280          case 1:
 3281              BattleScene_ApproachCameraZoom(arg0, temp_s0);
 3282              BattleScene_EnterStateRow(temp_s0, 2);
 3283              BattleScene_ClearMonStatusAnimations(temp_s0);
 3284              sBattleScene.unk_00->unk_20 += 1;
 3285              break;
...
 3287          case 2:
 3288              BattleScene_ApproachCameraZoom(arg0, temp_s0);
 3289              if (BattleScene_AdvanceCameraFrameCounter(0x69) != 0) {
 3290                  Vec3f_SetComponentsDuplicate(&arg0->view.targetPosition, temp_s0->unk_4B0 * -150.0f, 0.0, 0.0f
 3291                  if (sBattleScene.unk_00 && sBattleScene.unk_00) {}
 3292                  Vec3f_SetComponentsDuplicate(&arg0->view.eyePosition, temp_s0->unk_4B0 * -50.0f, 5.0f, 0.0f);
 3293                  BattleScene_SetParticipantModelFlags(temp_s0, 2, 0);
 3294                  BattleAnim_QueueEffectList3(0xD, temp_s0, temp_s0, 0xFF, 0xFF);
 3295                  sBattleScene.unk_00->unk_20 += 1;
 3296              }
 3297              break;
...
 3299          case 3:
 3300              if (temp_s0->unk_4EC.position.y == 2.5f) {
 3301                  temp_s0->unk_4EC.node.renderFlags |= 0x20;
 3302              }
...
 3304              if (ModelAnim_IsAnimationDone(&temp_s0->unk_4EC) == 1) {
 3305                  sBattleScene.unk_00->unk_20 += 1;
 3306              }
 3307              break;
...
 3309          case 4:
 3310              temp_s0->unk_4EC.node.flags &= ~1;
 3311              temp_s0->sessionTeams->unk_04 = &temp_s0->sessionTeams->teamMember[sp34->unk_2C]->pokemonPicked[sp
 3312              temp_s0->sessionTeams->uniqueStatusFlags |= 0x80;
 3313              sBattleScene.unk_00->unk_20 += 1;
 3314              break;
...
 3316          case 5:
 3317              if (temp_s0->sessionTeams->uniqueStatusFlags & 0x10) {
 3318                  temp_s0->sessionTeams->uniqueStatusFlags &= ~0x90;
 3319                  Battle_LoadOwnerModelForSwitchIn(temp_s0);
... 37 more
```

### src/fragments/battle_engine/battle_engine_313D60.c (11 lines)

```
  506                          sActivePokemon[sBattleScene.unk_00->unk_2C]->displayObject.node.flags &= ~1;
  507                      }
...
  522                  case 11:
  523                      sBattleScene.unk_00->unk_08 = 4;
  524                      if (sBattleScene.unk_00->unk_34 == 1) {
...
  526                      } else if (sBattleScene.unk_00->unk_20 == 0) {
  527                          BattleScene_UpdateBothOwnersVisibilityCulling(arg0);
  528                      } else {
  529                          BattleScene_HideOpponentOwner();
  530                      }
  531                      break;
```

### src/fragments/battle_engine/battle_engine_315830.c (3 lines)

```
  114  void BattleAnim_ClearSleepStatusAnimation(ActivePokemon* arg0) {
  115      if (arg0->state.unk_34 & 1) {
...
  119  }
```

### src/fragments/battle_engine/battle_engine_315D50.c (16 lines)

```
 4282          case 0:
 4283              switch (gSessionContext.ruleCategory) {
 4284                  case 0:
 4285                      Audio_SelectModeMusic(0, 0xE);
 4286                      break;
...
 4319              }
 4320              break;
...
 4322          case 1:
...
 4444          case 0:
 4445              switch (gSessionContext.ruleCategory) {
 4446                  case 0:
 4447                      Audio_StartTrackForCategory(0, 0xE);
 4448                      break;
...
 4481              }
 4482              break;
...
 4484          case 1:
```

### src/fragments/battle_engine/battle_engine_328F10.c (14 lines)

```
  366          case 0:
  367              var_v1 = 0xD;
  368              break;
...
  442  void BattleAnim_StartEffect143ModeOrbitAndMetadataBurst(void) {
  443      UNUSED s32 pad;
...
  445      BattleAnim_SpawnGlobalTintColorFadeDetach(0, 8, 6);
  446      BattleAnim_CreateRepeatingEffectSlotForCurrentOwner(0, 1, 4, BattleAnim_CallbackStartEffects133And134And14
  447      BattleAnim_CreateRepeatingEffectSlotForCurrentOwner(6, 1, 8, BattleAnim_CallbackStartEffects133And134And14
  448      BattleAnim_CreateRepeatingEffectSlotForCurrentOwner(6, 1, 8, BattleAnim_CallbackStartEffects133And134And14
  449      BattleAnim_SpawnOwnerModelTintRampDetach(0xE, 0xA, 0, 0xFF, 4);
  450      BattleAnim_CreateEffectSlotForCurrentOwner(0x14, BattleAnim_CallbackStartEffects133And134And143Descriptor3
  451      BattleAnim_SpawnOwnerModelAlphaFade(0x2D, 0x40, 0, 0xFF);
  452      BattleAnim_SpawnGlobalTintColorFadeDetach(0x32, 6, 0xA);
  453  }
```

### src/fragments/battle_engine/battle_engine_32B680.c (7 lines)

```
 1075          case 0x97:
 1076              var_fs0 = -13.0f;
 1077              break;
...
 1461          case 0x97:
 1462              break;
...
 3930          var_a3 = sSide2PokemonThumbnailImage + idx;
 3931      }
```

### src/fragments/battle_engine/battle_engine_339CA0.c (86 lines)

```
  369  void BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade_InitParticle(Particle* arg0) {
  370      s16 sp26 = Particle_FieldCE_GetS32_0C040(arg0);
  371      s16 sp24 = Particle_FieldCF_GetS16_0C058(arg0);
  372      s16 sp22 = BattleAnim_GetOwnerSpecies(gBattleAnimSecondaryOwner);
...
  374      Particle_Field1C_SetScaled_0B938(arg0, 1.0f);
...
  376      if (sp22 == 0x42) {
...
  379      } else {
  380          Particle_Field94_AddX_0A578(arg0, 0x2000);
  381          Particle_Field94_AddY_0A594(arg0, 0x4000);
  382      }
...
  384      switch (sp26) {
  385          case 0:
  386              Particle_Field38_AddYScaled_08E90(arg0, -10.0f);
  387              break;
...
  389          case 2:
  390              Particle_Field38_AddYScaled_08E90(arg0, 10.0f);
  391              break;
  392      }
...
  394      ParticleGfx_RecolorPrimEnvFromPalettePair(arg0, sp24);
  395      Particle_SetPrimAlpha(arg0, 0x80);
  396      Particle_AdvanceLifecycle(arg0);
  397  }
...
  399  void BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade(Particle* arg0) {
  400      switch (arg0->unk_B2) {
  401          case 1:
  402              BattleAnim_CallbackEndEffect88Descriptor2AOwnerKeyedAnimGrowFade_InitParticle(arg0);
  403              break;
...
  405          case 2:
  406              Particle_TickDelayAndAdvanceLifecycle(arg0, 8);
  407              break;
...
  409          case 3:
  410              Particle_PrimAlpha_ApproachAndStepState_0BC5C(arg0, 0, 0x10);
  411              break;
...
  413          case 4:
  414              Particle_DisableAndDetach(arg0);
  415              break;
  416      }
...
  418      Particle_AnimateFramesForward(arg0, 0, 7, 1, 2, 0);
  419      Particle_Field1C_Approach_0B98C(arg0, 5.0f, 0.4f);
  420  }
...
 1155          case 9:
 1156              Particle_AddPositionFromRotationTemplate1(arg0, ParticleMath_RandomRangeOffset(0x1E, 0x14));
 1157              Particle_SetScaleSpeed(arg0, 0.3f);
 1158              Particle_SetScaleTarget(arg0, 0.025f);
 1159              Particle_Field74_SetScaled_0AD8C(arg0, 0.0f);
 1160              Particle_Field78_SetScaled_0ADA4(arg0, 0.02f);
 1161              break;
...
 2871  void func_8435727C(void) {
 2872      UNUSED s16 pad;
 2873      s16 sp3C = BattleAnim_GetOwnerSpecies(gBattleAnimSecondaryOwner);
 2874      s16 sp3A = 3;
...
 2876      if (sp3C == 0x7B) {
...
 2880      BattleAnim_CreateEffectSlotForCurrentOwner(0, BattleAnim_CallbackSharedDescriptor29ScaleAnimFade, &gBattle
 2881      BattleAnim_CreateEffectSlotForCurrentOwner(4, BattleAnim_CallbackSharedDescriptor29ScaleAnimFade, &gBattle
 2882      BattleAnim_CreateEffectSlotForCurrentOwner(0xA, BattleAnim_CallbackSharedDescriptor29ScaleAnimFade, &gBatt
...
 2884      if (sp3C == 0x7B) {
...
 2886      } else {
 2887          BattleAnim_CreateEffectSlotForCurrentOwner(0x1E, BattleAnim_CallbackSharedOwnerKeyedGrowColorFade, &gB
 2888      }
 2889      BattleAnim_SpawnDescriptor25AlphaHoldFadeOutFullAlpha(0x23, 0, 0x10, 0x14);
... 26 more
```

### src/fragments/battle_engine/battle_engine_34A420.c (2 lines)

```
  674                          var_v0->unk_1C.x += 0.1;
  675                      }
```

### src/fragments/battle_engine/battle_engine_359F90.c (26 lines)

```
  300      return 1;
...
  736          sp24->unk_32[1] = 0;
  737      }
...
  740          sp24->unk_32[2] = 0;
  741      }
...
  744          sp24->unk_32[3] = 0;
  745      }
...
  749              for (j = i; j < 4; j++) {
  750                  sp24->unk_32[j] = 0;
  751                  sp24->moves[j] = sp34->unk_09[j] = 0;
  752              }
  753              break;
  754          }
...
 1074          gBattleMessageQueues->unk_388[0] = 0;
 1075          gBattleMessageQueues->unk_388[1] = 0;
 1076          sBattleScene.unk_00->unk_48 = 0x1C;
 1077          Battle_QueueMessage(gBattleMessageQueues->unk_088, 0x20);
 1078          return 0;
 1079      }
...
 1314              gBattleUser->unk_52 = 0;
 1315              gBattleUser->unk_57 = 0;
 1316              Text_SetStringToken(0x19, Battle_GetActiveMonNickname(sActivePokemon[sBattleScene.unk_00->unk_2C])
 1317              Battle_QueueMessage(gBattleMessageQueues->unk_388, 0xB);
 1318          }
...
 1520                              break;
 1521                          }
```

### src/fragments/battle_engine/battle_engine_35DF70.c (12 lines)

```
  129                      return;
  130                  }
...
  255              case 6:
...
  518                      case 3:
  519                          sp18 = &gBattleTarget->unk_3E;
  520                          sp1C = &gBattleTarget->unk_30;
  521                          break;
...
 1054          sBattleScene.unk_00->unk_48 = 0x1C;
 1055          Text_SetStringToken(0x1B, Battle_GetActiveMonNickname(sActivePokemon[!sBattleScene.unk_00->unk_2C]));
 1056          Battle_QueueMessage(gBattleMessageQueues->unk_088, -0x2F);
 1057          return;
 1058      }
```

### src/fragments/battle_engine/battle_engine_361050.c (224 lines)

```
  285              if (D_843C6140 & 0xFF00) {
...
  292          }
...
  305          D_843C6140 >>= 1;
  306          if (D_843C6140 == 0) {
...
  309      }
...
  312          D_843C6140 = 1;
  313      }
...
  318          D_843C613A = 0x3E4;
  319      }
...
 1079          case 0:
 1080          case 8:
 1081              if (arg1 == NULL) {
 1082                  arg3->unk_002 = 1;
 1083                  arg3->unk_004 = 0;
 1084                  arg3->unk_006 = arg0->numberOfPokemonChosen;
 1085                  arg3->unk_007 = 0;
 1086              } else {
...
 1093              arg3[0].unk_003 = arg0->rosterInfo->numberOfPokemonCanBePicked;
 1094              arg3[0].unk_005 = arg3[0].unk_003 + arg3[0].unk_004;
 1095              arg3[0].unk_008 = arg3[0].unk_006 + arg3[0].unk_007;
...
 1097              arg3[1].unk_002 = 1;
 1098              arg3[1].unk_006 = arg2->numberOfPokemonChosen;
 1099              arg3[1].unk_004 = 0;
 1100              arg3[1].unk_007 = 0;
...
 1102              arg3[1].unk_005 = arg3[1].unk_003 = arg3[1].unk_008 = arg3[1].unk_006;
 1103              break;
...
 1131          var_v0 = BattleAI_PredictDamage(arg0, arg1, arg2, 0);
...
 1582              if (var_s3->unk_58 != 0) {
 1583                  var_s2->unk_00 = var_s3->unk_58;
 1584              }
 1585          }
...
 1784                      if (var_s1 >= var_s3->unk_0C) {
 1785                          BattleAI_ApplyInverseWeightScale(spFB, &spFA);
 1786                      }
...
 1826              case 0x9:
 1827                  if (var_s0->unk_2E >= var_s3->unk_2E) {
 1828                      if ((var_s3->unk_58 == 0) || (var_s3->unk_58 == 0x77)) {
 1829                          var_s1 = 0;
 1830                          spFB = 0;
 1831                      }
 1832                  } else {
 1833                      var_s1 = BattleAI_ScoreMove(var_s0, var_s3, spF7);
 1834                      spFB = BattleAI_CalcStagedMovePower(var_s0, var_s3, spF7, 1);
 1835                      if ((arg2 != 0) && (sp102 >= spFC)) {
...
 1838                  }
...
 1840                  if (var_s0->unk_2E >= var_s3->unk_2E) {
 1841                      if (var_s1 >= var_s3->unk_0C) {
...
 1844                  } else if (var_s4 >= var_s0->unk_0C) {
 1845                      BattleAI_ApplyInverseWeightScale(spFA, &spFB);
 1846                  }
 1847                  break;
...
 1934              case 0x38:
 1935                  spFB = 0xFF;
 1936                  if (var_s0->unk_15 & 0x40) {
...
 1940                  if (var_s0->unk_4C & 0x80) {
...
 1944                  if (spFB == 0) {
...
 1948                  if ((arg2 != 0) && (sp102 >= spFC)) {
...
 1951                  break;
...
 1975                      spFB = 0xBF;
 1976                  }
... 164 more
```

### src/fragments/battle_engine/battle_engine_main.c (11 lines)

```
  158              sStageFog.node.flags |= 1;
  159          }
...
  559              if (gSessionContext.ruleCategory == RULE_CATEGORY_ANYTHING_GOES) {
  560                  number_of_pokemon_picked = com_roster->rosterInfo->numberOfPokemonCanBePicked;
  561              } else {
...
  564              func_843831A0(player_roster, player_teammate_roster, com_roster, gSessionContext.aiDifficulty, ord
...
  712          sStageFog.unk_18.unk_00 = (s16)((((u8*)&fog_parameters->unk_00)[0] << 8) | ((u8*)&fog_parameters->unk_
  713          sStageFog.unk_18.unk_02 = (s16)((((u8*)&fog_parameters->unk_02)[0] << 8) | ((u8*)&fog_parameters->unk_
  714          sStageFog.unk_18.unk_04.rgba = fog_parameters->unk_04.rgba;
  715          sStageFog.node.callbackArg = 1;
  716      }
```


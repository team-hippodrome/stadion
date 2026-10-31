# Game Boy save variants: source lines each one adds over the base save (same route)


---
# pc__not_at_pc

Baseline: 1 runs; new: pc__not_at_pc.

Functions: baseline 1049, new runs 826, **20 only in the new runs**.
Lines: 417 lines only in the new runs.

Audio_Disable, Audio_FadeCategoriesAndStreams, Audio_SetEnabled, GbTower_LoadPartyFromDeck, GbTower_TeamPrep, GbTower_TeamPrepFrameCallback, GeoRender_SetMode, ModelAnim_ClearTransformChannel, PokeIcon_CaptureGeoNode, PokeIcon_CreateRenderList, PokeIcon_InitRenderScene, PokeIcon_ProcessAllSlots, PokeIcon_RenderSlot, State_GbTower, Ui_DrawBorderStyleA, Ui_DrawGradientPanel, Ui_DrawGradientRectangle, Ui_DrawTexturedRect8x8, func_8001A46C, func_8130337C

### Source lines only the new runs reached

### src/3D140.c (4 lines)

```
  626  s32 Audio_SetEnabled(s32 arg0) {
  627      if (PORT_AUDIO_OFF()) {
  628          return 0;
  629      }
```

### src/DDC0.c (6 lines)

```
   71          Audio_FadeCategoriesAndStreams(2);
   72      }
...
   92  void Audio_Disable(void) {
   93      D_800A83A0 = 0;
   94      Audio_SetEnabled(0);
   95  }
```

### src/audio_category_fade.c (4 lines)

```
   31  void Audio_FadeCategoriesAndStreams(s32 arg0) {
   32      if (PORT_AUDIO_OFF()) {
   33          return;
   34      }
```

### src/battle_hud.c (1 lines)

```
  220              D_800AF7B4[arg0] = 0;
```

### src/fragments/gb_tower_team_prep/gb_tower_team_prep.c (40 lines)

```
   19  void GbTower_LoadPartyFromDeck(s32 arg0) {
   20      s32 i;
   21      DeckHandle* temp_v0;
...
   23      if (GbSave_EnsureMainDataLoaded(arg0) != 0) {
   24          temp_v0 = Deck_Open(0x20, arg0, 0, 0);
   25          D_82A00360 = Deck_ReadEntries(D_82A00378, 6, temp_v0);
   26          Deck_CloseAndFlush(temp_v0);
   27      }
...
   29      for (i = 0; i < D_82A00360; i++) {
   30          func_8001A46C(D_82A00374, i, &D_82A00378[i], 1);
   31      }
   32  }
...
   34  s32 GbTower_TeamPrepFrameCallback(u8 arg0) {
   35      Cont_StartReadInputs();
   36      Cont_ReadInputs();
   37      PokeIcon_ProcessAllSlots(D_82A00374, 0);
   38      BgStage_DrawFrame();
   39      GfxImage_FillCurrent(&gDisplayListHead, 1);
   40      return 0;
   41  }
...
   62  s32 GbTower_TeamPrep(s32 arg0, UnkInputStruct8000D738* arg1) {
   63      StageContext* sp24;
...
   65      main_pool_push_state('PREP');
...
   67      Gfx_InitDisplayListBuffers(0x10000, 0);
   68      sp24 = StageContext_Allocate(0, 1, 2, 0, 1, 1);
...
   70      ASSET_LOAD(D_1000000, common_menu1_ui, 0);
   71      FRAGMENT_LOAD(FRAGMENT_PARTICLE_DATA_LIBRARY);
...
   73      D_82A00370 = PokeIcon_AllocFramebuffers(1);
   74      D_82A00374 = PokeIcon_CreateRenderList(D_82A00370, 6, 0x28, 0x28);
   75      PokeIcon_OpenModelArchives();
   76      GbTower_LoadPartyFromDeck(arg0);
   77      Save_EnsureBankLoaded(2);
   78      StageContext_Activate(sp24);
   79      BgStage_RunUntilCondition(8, GbTower_TeamPrepFrameCallback);
   80      StageContext_Deactivate();
   81      GbTower_BuildPartyResult(arg1);
   82      Gfx_FreeDisplayListBuffers();
...
   84      main_pool_pop_state('PREP');
...
   86      return D_82A00360;
   87  }
```

### src/fragments/pokemon_lab/pokemon_lab.c (7 lines)

```
 2877              var_s1 = 0x80000003;
 2878              var_s6 = 0x11;
...
 2891                          ((func88506074)Memmap_GetFragmentVaddr(WidgetTree_SetTextList))(arg0->unk_34,
 2892                                                                                 Text_GetString(NULL, 0, D_88016
 2893                          var_s1 |= 0x80000101;
...
 3045          Audio_FadeOutAll(0x10);
 3046      }
```

### src/fragments/transfer_pak_check/transfer_pak_check.c (13 lines)

```
 1372          Gfx_FillRectRgb(4, 4, 0x74, 0x64, 0x64, 0x1E, 0x1E);
 1373          Gfx_FillRectRgb(4, 0x68, 0x74, 0x5C, 0, 0, 0);
...
 1386              PakUi_DrawPartyCardList(0x64);
...
 1461          Font_SetActive(4, 0);
 1462          Font_SetLineHeight(0x14);
...
 1464          switch (arg2) {
...
 1469              case 19:
 1470                  Font_Printf(0xE, 0x70, Text_GetString(NULL, 0, D_82D0AB9C, 0x21));
 1471                  break;
...
 1476          }
 1477      }
...
 1499                  PakUi_BuildPartyCard(arg0, 1, 0x13, (u16)D_82D0AB38[arg0].unk_06, D_82D0AB38[arg0].unk_08);
 1500              }
```

### src/fragments/transfer_pak_select/transfer_pak_select.c (70 lines)

```
  132              gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, 639, 202);
  133          }
...
  175                  var_v1 = 0x13;
  176              }
...
  197              switch (temp_v0) {
...
  202                  case 19:
  203                      sp18 = 3;
  204                      break;
...
  213              }
  214          }
...
  234          TransferPak_DrawPromptPanel(arg1, arg2, 0x78, 0x38, 0x64, 0x1E, 0x1E, tmp1);
  235      }
...
  238          Ui_DrawGradientPanel(arg1 - 4, arg2 + 0x9C, 0x80, 0x74, &D_81303F14, &D_81303F14);
  239          tmp1 = arg1 + 0x28;
  240          tmp2 = (SINS(D_81303CB0 * 2) * 2) + (arg2 + 0x8C);
  241          TransferPak_DrawSpinningPakIcon(D_3006060, tmp1, tmp2, 0x30, 0x10, 1.0f);
  242      }
...
  280              case 3:
  281                  Font_Printf(arg1 + 5, arg2 + 0xAC, Text_GetString(NULL, 0, D_81304658, 0x10));
  282                  break;
...
  590                      Audio_PlaySoundEffectById(SFX_PAK_TOWER_SELECT);
  591                  }
...
  617          func_8130337C();
  618      }
...
  936  void func_8130337C(void) {
  937      Gfx* gfx = gDisplayListHead;
  938      u8* sp30;
  939      s32 i;
...
  941      gDPPipeSync(gfx++);
...
  943      gDPSetCycleType(gfx++, G_CYC_1CYCLE);
  944      gSPClearGeometryMode(gfx++, G_ZBUFFER);
  945      gSPSetGeometryMode(gfx++, 0);
  946      gDPSetCombineMode(gfx++, G_CC_BLENDPEDECALA, G_CC_BLENDPEDECALA);
  947      gDPSetTextureLOD(gfx++, G_TL_TILE);
  948      gDPSetTextureFilter(gfx++, G_TF_BILERP);
  949      gDPSetTextureConvert(gfx++, G_TC_FILT);
  950      gDPSetTextureLUT(gfx++, G_TT_NONE);
  951      gDPSetTexturePersp(gfx++, G_TP_NONE);
...
  953      gDPPipeSync(gfx++);
...
  955      gDPLoadTextureBlock_4b(gfx++, D_3008670, G_IM_FMT_IA, 128, 64, 0, G_TX_NOMIRROR | G_TX_WRAP,
  956                             G_TX_NOMIRROR | G_TX_WRAP, 7, 7, G_TX_NOLOD, G_TX_NOLOD);
...
  958      for (i = 0; i < 8; i++) {
  959          gDPPipeSync(gfx++);
...
  961          gDPSetPrimColor(gfx++, 0, 0, (0xBE0 - (i * 0xBE)) / 16, (0xDC0 - (i * 0xDC)) / 16, (0x7D0 - (i * 0x7D)
  962                          255);
  963          gDPSetEnvColor(gfx++, (0x140 - (i * 0x14)) / 16, (0x5F0 - (i * 0x5F)) / 16, (0xA0 - (i * 10)) / 16, 25
  964          gSPTextureRectangle(gfx++, 0, (i * 0x40) << 2, 0x0A00, ((i + 1) * 0x40) << 2, G_TX_RENDERTILE, 0, 0, 0
  965                              0x0400);
  966      }
...
  968      switch (D_81304654) {
...
  973          case 0:
  974              sp30 = D_3009678;
  975              break;
...
  984      }
...
  986      gDPSetRenderMode(gfx++, AA_EN | CVG_DST_CLAMP | ZMODE_OPA | CVG_X_ALPHA | ALPHA_CVG_SEL | FORCE_BL | G_RM_
  987                       AA_EN | CVG_DST_CLAMP | ZMODE_OPA | CVG_X_ALPHA | ALPHA_CVG_SEL | FORCE_BL | G_RM_NOOP2);
  988      gDPSetCombineMode(gfx++, G_CC_DECALRGBA, G_CC_DECALRGBA);
...
  990      for (i = 0; i < 9; i++) {
  991          gDPPipeSync(gfx++);
...
  993          gDPLoadTextureBlock(gfx++, &sp30[i * 0x1000], G_IM_FMT_RGBA, G_IM_SIZ_16b, 128, 16, 0,
... 10 more
```

### src/fragments/widget_tree/widget_tree.c (16 lines)

```
 2699              case 0x100:
 2700                  var_a1 = SFX_MENU_ERROR;
 2701                  break;
...
 2975          for (i = 0, sp48 = sp50; i < arg0->unk_30 - 1; i++) {
 2976              if (sp48 == arg0->unk_30 - 1) {
 2977                  sp48 = 0;
 2978              } else {
...
 2982              for (j = 0, sp4C = arg0->unk_34; j < arg0->unk_2C;) {
 2983                  if ((arg0->unk_18[sp48 * arg0->unk_2C + sp4C] != NULL) &&
 2984                      !(arg0->unk_18[sp48 * arg0->unk_2C + sp4C]->unk_28 & 2)) {
...
 2989                  j++;
 2990                  sp4C = ((sp4C == 0) ? arg0->unk_2C : sp4C) - 1;
 2991              }
...
 2993              if (var_t4 != 0) {
...
 2996          }
...
 2998          if ((arg0->unk_28 & 0x200) && ((var_t4 == 0) || (sp48 < sp50))) {
```

### src/game_state.c (32 lines)

```
 1082  void State_GbTower(void) {
 1083      s32 sp34;
 1084      UnkInputStruct8000D738 sp2C;
 1085      GameOptions sp24;
...
 1087      main_pool_push_state('EMU0');
...
 1089      if ((FRAGMENT_LOAD_AND_CALL(FRAGMENT_TRANSFER_PAK_SELECT, 0, 0) == 0) && (D_80075674 >= 0) && (D_80075674 
 1090          Audio_StopMusic(4);
 1091          Audio_WaitForIdle(1);
 1092          Display_WaitForFrames(2);
 1093          Audio_Disable();
...
 1095          sp2C.unk_04 = main_pool_alloc(sizeof(*sp2C.unk_04), 0);
...
 1097          FRAGMENT_LOAD_AND_CALL(FRAGMENT_GB_TOWER_TEAM_PREP, D_80075674, &sp2C);
 1098          GbTower_Start(&sp2C);
...
 1100          sp34 = GbTower_RecvExitMesg();
...
 1102          GbTower_Stop(&sp2C);
 1103          Display_WaitForFrames(2);
 1104          Audio_Enable();
...
 1106          if (sp34 != 0) {
...
 1110          GbSave_ForceReloadPort(D_80075674);
 1111          Save_EnsureBankLoaded(2);
 1112          Save_GetOptions(&sp24);
...
 1114          sp24.unk_04 = sp2C.unk_00;
...
 1116          Save_SetOptions(&sp24);
 1117          Save_CommitTypedRecord(0x14, 0);
 1118          Save_FlushBank(2);
 1119      }
...
 1121      main_pool_pop_state('EMU0');
...
 1123      gCurrentGameState = STATE_AREA_SELECT;
 1124  }
...
 1429              case STATE_GB_TOWER:
 1430                  State_GbTower();
 1431                  break;
```

### src/geo_render.c (5 lines)

```
 1079  void GeoRender_SetMode(s32 arg0) {
 1080      D_8006F080 = arg0;
 1081  }
...
 1142          var_v0 = 0x107A58;
 1143      }
```

### src/input.c (3 lines)

```
   23          D_800AC888 = 0xA;
   24          return 0x400;
   25      }
```

### src/memmap.c (2 lines)

```
   37              sVBlobs[i].host = NULL;
   38          }
```

### src/model_animation.c (4 lines)

```
  392  void ModelAnim_ClearTransformChannel(DisplayObject* display_object) {
  393      display_object->transformAnim.animationId = -1;
  394      display_object->transformAnim.data = NULL;
  395  }
```

### src/poke_icon.c (143 lines)

```
   23  intptr_t PokeIcon_CaptureGeoNode(s32 arg0, GraphNode* arg1) {
   24      if (arg0 == 0) {
   25          D_800ABE10.unk_A04.unk_0C = arg1;
   26      }
   27      return 0;
   28  }
...
   30  void PokeIcon_InitRenderScene(unk_func_8001A024* arg0, s16 arg1, s16 arg2) {
   31      static uintptr_t D_8006F350[] = { // PORT64: holds addresses
   32          0x0C00FFFF,    0x05000000, 0x0B00001E, 0x00000000, 0x00300030, 0x00000000, 0x0000000A, 0x015404E2, 0x0
   33          0x0D000000,    0x05000000, 0x14000000, 0x002D0019, 0xFFFFFF32, 0x14000000, 0xFFD300CD, 0x80808032, 0x1
   34          0x0F000001,    0x05000000, 0x1F000000, 0x00000000, 0x00000000, 0xFF060000, 0x00640064, 0x00640000, 0x0
   35          PokeIcon_CaptureGeoNode, 0x00000000, 0x06000000, 0x06000000, 0x06000000, 0x06000000, 0x01000000, 0x000
   36      };
...
   38      CameraNode* sp24;
   39      MemoryBlock* sp20 = MainPool_AllocState(main_pool_get_available(), 0);
...
   41      arg0->unk_0C = process_geo_layout(sp20, D_8006F350);
   42      arg0->unk_10 = D_800ABE10.unk_A04.unk_0C;
   43      MainPool_FinalizeAllocation(sp20);
   44      sp24 = arg0->unk_0C->children;
   45      GeoCamera_SetViewport(sp24, 0, 0, arg1, arg2);
   46      GeoCamera_SetPerspective(sp24, 30.0f, 100.0f, 12800.0f);
   47  }
...
   49  unk_func_8001A024* PokeIcon_CreateRenderList(ModelLoadContext* arg0, s16 arg1, s16 arg2, s16 arg3) {
   50      s32 var_s0;
   51      DepthBuffer* temp_a1;
   52      s32 i;
   53      unk_func_8001A024* temp_v0 = main_pool_alloc(sizeof(unk_func_8001A024), 0);
...
   55      if (temp_v0 != NULL) {
   56          temp_v0->unk_00 = 0;
   57          temp_v0->unk_02 = 0;
   58          temp_v0->unk_03 = -1;
   59          temp_v0->unk_01 = arg1;
   60          temp_v0->unk_08 = main_pool_alloc(arg1 * sizeof(unk_func_8001A024_008), 0);
   61          temp_v0->unk_04 = arg0;
   62          temp_v0->unk_14 = NULL;
   63          temp_v0->unk_18 = D_8006FF00;
...
   65          if (arg0 != NULL) {
   66              temp_v0->unk_14 = GfxImage_Allocate(0, 2, arg2, arg3, 1);
   67          }
...
   69          for (i = 0; i < arg1; i++) {
   70              temp_v0->unk_08[i].unk_00 = 0;
   71              temp_v0->unk_08[i].unk_14.raw = 0;
   72              temp_v0->unk_08[i].unk_0C = GfxImage_Allocate(0, 2, arg2, arg3, 0);
   73              bzero(temp_v0->unk_08[i].unk_0C->img_p, arg2 * arg3 * 2);
   74              if (temp_v0->unk_14 != NULL) {
   75                  GfxImage_AttachDepthBuffer(temp_v0->unk_08[i].unk_0C, temp_v0->unk_14);
   76              }
   77          }
...
   79          PokeIcon_InitRenderScene(temp_v0, arg2, arg3);
   80      }
   81      return temp_v0;
   82  }
...
  151  void func_8001A46C(unk_func_8001A024* arg0, s32 arg1, BattleMon* arg2, u16 arg3) {
  152      s32 w;
  153      s32 h;
...
  155      if (arg1 < arg0->unk_01) {
  156          w = arg0->unk_08[arg1].unk_0C->width;
  157          h = arg0->unk_08[arg1].unk_0C->height;
  158          bzero(arg0->unk_08[arg1].unk_0C->img_p, w * h * 2);
  159          if (((s32) arg2->unk_00.unk_00 > 0) && ((s32) arg2->unk_00.unk_00 < 0x98)) {
... 83 more
```

### src/pokemon_stats.c (5 lines)

```
  842          ptr = D_800AC910 + (arg1 - 'BOX0');
...
  847          ptr->unk_54 = NULL;
  848  #endif
  849          ptr->unk_00 &= ~0x12;
  850      }
```

### src/ui_graphics.c (62 lines)

```
  238  void Ui_DrawTexturedRect8x8(s32 x1, s32 y1, s32 width, s32 height, u8* texture) {
  239      s32 texture_step_x = (width < 9) ? 0x400 : 0;
  240      s32 texture_step_y = (height < 9) ? 0x400 : 0;
...
  242      gDPLoadTextureBlock(gDisplayListHead++, texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, G_TX_NOMIRROR | G_T
  243                          G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
...
  245      Gfx_DrawTexturedRectClipped(x1, y1, width, height, 0, 0, texture_step_x, texture_step_y, 0);
  246  }
...
  248  void Ui_DrawBorderStyleA(s32 x1, s32 y1, s32 width, s32 height) {
  249      Ui_DrawTexturedRect8x8(x1, y1, 8, 8, D_10027D0);
  250      Ui_DrawTexturedRect8x8(x1, (y1 + height) - 8, 8, 8, D_1002850);
  251      Ui_DrawTexturedRect8x8((x1 + width) - 8, (y1 + height) - 8, 8, 8, D_10028D0);
  252      Ui_DrawTexturedRect8x8((x1 + width) - 8, y1, 8, 8, D_1002950);
...
  254      if (width > 16) {
  255          Ui_DrawTexturedRect8x8(x1 + 8, y1, width - 0x10, 8, D_10025D0);
  256          Ui_DrawTexturedRect8x8(x1 + 8, (y1 + height) - 8, width - 0x10, 8, D_1002650);
  257      }
...
  259      if (height > 16) {
  260          Ui_DrawTexturedRect8x8(x1, y1 + 8, 8, height - 0x10, D_10026D0);
  261          Ui_DrawTexturedRect8x8((x1 + width) - 8, y1 + 8, 8, height - 0x10, D_1002750);
  262      }
  263  }
...
  322                                Color_RGB8* bottom_color) {
  323      Vtx* temp_s2;
  324      DepthBuffer* temp_s1;
  325      Mtx* sp7C;
  326      Vp* sp78;
...
  328      temp_s1 = GfxImage_GetCurrent();
  329      temp_s2 = Gfx_AllocDisplayList(sizeof(Vtx) * 4);
  330      sp7C = Gfx_AllocDisplayList(sizeof(Mtx) * 1);
  331      sp78 = Gfx_AllocDisplayList(sizeof(Vp) * 1);
...
  333      Gfx_SetViewportDimensions(sp78, temp_s1->width, temp_s1->height);
...
  335      gSPViewport(gDisplayListHead++, (uintptr_t)sp78);
...
  337      guOrtho(sp7C, 0.5f, temp_s1->width - 0.5f, temp_s1->height - 0.5f, 0.5f, -2.0f, 2.0f, 1.0f);
  338      Gfx_SetVertexAttributes(&temp_s2[0], x1, y1, -1, 0, 0, top_color->r, top_color->g, top_color->b, 0xFF);
  339      Gfx_SetVertexAttributes(&temp_s2[1], x1 + width, y1, -1, 0, 0, top_color->r, top_color->g, top_color->b, 0
  340      Gfx_SetVertexAttributes(&temp_s2[2], x1, y1 + height, -1, 0, 0, bottom_color->r, bottom_color->g,
  341                              bottom_color->b, 0xFF);
  342      Gfx_SetVertexAttributes(&temp_s2[3], x1 + width, y1 + height, -1, 0, 0, bottom_color->r, bottom_color->g,
  343                              bottom_color->b, 0xFF);
...
  345      gDPPipeSync(gDisplayListHead++);
...
  347      gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
  348      gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER | G_LIGHTING);
  349      gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
  350      gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
  351      gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
...
  353      gSPMatrix(gDisplayListHead++, (uintptr_t)sp7C, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
  354      gSPMatrix(gDisplayListHead++, (uintptr_t)&gIdentityMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
...
  356      gSPVertex(gDisplayListHead++, (uintptr_t)temp_s2, 4, 0);
  357      gSP2Triangles(gDisplayListHead++, 0, 2, 3, 0, 0, 3, 1, 0);
  358  }
...
  376  void Ui_DrawGradientPanel(s32 x1, s32 y1, s32 width, s32 height, Color_RGB8* top_color, Color_RGB8* bottom_col
  377      if ((width >= 0xF) && (height >= 0xF)) {
  378          if (StageContext_IsHighResolution() != 0) {
  379              Ui_DrawGradientRectangle(x1 + 6, y1 + 6, width - 0xC, height - 0xC, top_color, bottom_color);
  380          } else {
...
  383      }
...
  385      gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);
...
  387      if (StageContext_IsHighResolution() != 0) {
  388          Ui_DrawBorderStyleA(x1, y1, width, height);
  389      } else {
... 2 more
```


---
# pc__boxes_full

Baseline: 1 runs; new: pc__boxes_full.

Functions: baseline 1049, new runs 1034, **8 only in the new runs**.
Lines: 105 lines only in the new runs.

GbSave_LoadBoxPokemon, Text_CountLines, WidgetTree_HandleMessagePanelInput, WidgetTree_OpenMessagePanel, WidgetTree_ResetMessagePanelAnimation, WidgetTree_RunMessagePanel, WidgetTree_RunMessagePanelWithSound, WidgetTree_SetMessagePanelText

### Source lines only the new runs reached

### src/battle_hud.c (1 lines)

```
  220              D_800AF7B4[arg0] = 0;
```

### src/fragments/lab_pc/lab_pc_1AE680.c (6 lines)

```
 1001                              Audio_PlaySoundEffectById(SFX_MENU_ERROR);
 1002                              ((func8850A10C)Memmap_GetFragmentVaddr(WidgetTree_SetMessagePanelText))(arg0->unk_
 1003                              ((func8850A3CC)Memmap_GetFragmentVaddr(WidgetTree_RunMessagePanelWithSound))(arg0-
 1004                              var_v1 = 1;
...
 2373              ptr2->unk_00[j] = var_s1++;
 2374          }
```

### src/fragments/widget_tree/widget_tree.c (59 lines)

```
 2143              var_t5 = 1;
 2144          }
...
 2429  void WidgetTree_SetMessagePanelText(unk_func_88509F48* arg0, unk_func_8850878C* arg1, s32 arg2, s32 arg3) {
 2430      s16 temp_v1;
...
 2432      *arg0->unk_34 = *arg1;
 2433      arg0->unk_34->unk_00.unk_28 |= 1;
 2434      Font_SetActive(arg1->unk_2C, arg1->unk_38);
 2435      Font_SetLineHeight(arg1->unk_3C);
...
 2437      if (arg3 == 0) {
 2438          arg3 = Font_MeasureTextExtent(0, 0, arg1->unk_40);
 2439      }
...
 2441      if (arg2 == 0) {
 2442          arg2 = Text_CountLines(arg1->unk_40);
 2443      }
...
 2445      temp_v1 = arg0->unk_38->unk_00[arg0->unk_38->unk_50].unk_02;
...
 2447      arg0->unk_2C->unk_00.unk_14.unk_00 = temp_v1 + arg3;
...
 2449      arg0->unk_2C->unk_00.unk_14.unk_02 = (((arg1->unk_3C * arg2) + temp_v1) - arg1->unk_3C) + temp_v1;
 2450      arg0->unk_2C->unk_00.unk_10.unk_00 = (arg0->unk_00.unk_14.unk_00 - arg0->unk_2C->unk_00.unk_14.unk_00) / 2
 2451      arg0->unk_2C->unk_00.unk_10.unk_02 = (arg0->unk_00.unk_14.unk_02 - arg0->unk_2C->unk_00.unk_14.unk_02) / 2
...
 2453      arg0->unk_30->unk_00.unk_14 = arg0->unk_2C->unk_00.unk_14;
...
 2455      arg0->unk_34->unk_00.unk_10.unk_00 = temp_v1 / 2;
 2456      arg0->unk_34->unk_00.unk_10.unk_02 = temp_v1 / 2;
 2457  }
...
 2459  s32 WidgetTree_HandleMessagePanelInput(unk_func_88509F48* arg0, Controller* arg1) {
 2460      s32 var_v1 = 0;
...
 2462      if (arg0->unk_2C->unk_30 & 2) {
 2463          if (arg1->buttonPressed & 0xC000) {
 2464              var_v1 = 0x80000002;
 2465          }
 2466      } else {
 2467          var_v1 = 1;
 2468      }
...
 2470      return var_v1;
 2471  }
...
 2473  void WidgetTree_OpenMessagePanel(unk_func_88509F48* arg0) {
 2474      WidgetTree_OpenAnimatedPanel(arg0->unk_2C);
 2475  }
...
 2477  void WidgetTree_ResetMessagePanelAnimation(unk_func_88509F48* arg0) {
 2478      arg0->unk_2C->unk_2C = 0xB;
 2479  }
...
 2481  void WidgetTree_RunMessagePanel(unk_func_88509F48* arg0, Controller* arg1, s32 arg2) {
 2482      s32 temp_v0;
 2483      intptr_t var_s0 = 0;
...
 2485      WidgetTree_OpenMessagePanel(arg0);
...
 2487      while (var_s0 == 0) {
 2488          Ui_SendMessageAndPollInput(var_s0);
...
 2490          temp_v0 = arg0->unk_00.unk_20(arg0, arg1);
 2491          if (!(temp_v0 & 1) && (temp_v0 & 2)) {
 2492              var_s0 = 1;
 2493          }
...
 2495          if (arg2 != 0) {
 2496              ((func8850BC94)Memmap_GetFragmentVaddr(Ui_PlayInputActionSound))(temp_v0);
 2497          }
 2498      }
...
 2500      WidgetTree_ResetMessagePanelAnimation(arg0);
 2501  }
...
 2503  void WidgetTree_RunMessagePanelWithSound(unk_func_88509F48* arg0, Controller* arg1) {
 2504      WidgetTree_RunMessagePanel(arg0, arg1, 1);
 2505  }
```

### src/gb_data.c (6 lines)

```
 1049          case 4:
 1050              sp4 = (arg1 * arg1 * arg1 * 4) / 5;
 1051              break;
...
 1053          case 5:
 1054              sp4 = (arg1 * arg1 * arg1 * 5) / 4;
 1055              break;
```

### src/gb_save.c (1 lines)

```
  457          var_v1 = gSpeciesData[temp_v0].type2;
```

### src/pokemon_stats.c (18 lines)

```
  462                  break;
  463              }
...
 1372  void GbSave_LoadBoxPokemon(BattleMon* arg0, s32 arg1, s32 arg2, s32 arg3) {
 1373      GbSaveCurrentBox* sp2C = (GbSaveCurrentBox*)GbSave_GetBoxEntry(arg1, arg2);
...
 1375      if (sp2C != NULL) {
 1376          Pokemon_FromGbRecord(arg0, &sp2C->unk_016[arg3]);
 1377          Pokemon_RecalcStats(arg0);
...
 1379          arg0->unk_52 = 0;
 1380          arg0->unk_53 = 0;
 1381          arg0->unk_25 = 0;
...
 1383          _bcopy(sp2C->unk_2AA[arg3], arg0->unk_46, 0xB);
 1384          Text_UntranscodeFixedName(arg0->unk_30, sp2C->unk_386[arg3]);
 1385          Text_UntranscodeFixedName(arg0->unk_3B, sp2C->unk_2AA[arg3]);
 1386      }
 1387  }
...
 1656              case 33:
 1657                  GbSave_LoadBoxPokemon(arg4, arg1, arg2, arg3);
 1658                  break;
```

### src/text_system.c (14 lines)

```
  152  s32 Text_CountLines(s8* arg0) {
  153      s32 last_chr = 0;
  154      s32 line_count = 0;
  155      s32 chr = *arg0++;
...
  157      while (chr != 0) {
  158          last_chr = chr;
  159          if (chr == '\n') {
...
  162          chr = *arg0++;
  163      }
...
  165      if (last_chr != '\n') {
  166          line_count++;
  167      }
...
  169      return line_count;
  170  }
```


---
# pc__party1

Baseline: 1 runs; new: pc__party1.

Functions: baseline 1049, new runs 1009, **6 only in the new runs**.
Lines: 81 lines only in the new runs.

Text_CountLines, WidgetTree_HandleMessagePanelInput, WidgetTree_OpenMessagePanel, WidgetTree_RunMessagePanel, WidgetTree_RunMessagePanelWithSound, WidgetTree_SetMessagePanelText

### Source lines only the new runs reached

### src/fragments/lab_pc/lab_pc_1AE680.c (11 lines)

```
 1044                              Audio_PlaySoundEffectById(SFX_MENU_ERROR);
 1045                              temp_v1 = ((func8850A10C)Memmap_GetFragmentVaddr(WidgetTree_SetMessagePanelText));
 1046                              if (arg0->unk_48->unk_18 != 0) {
...
 1048                              } else {
 1049                                  var_v0 = 3;
 1050                              }
 1051                              temp_v1(arg0->unk_3C, arg0->unk_5C[var_v0], 0, 0);
 1052                              ((func8850A3CC)Memmap_GetFragmentVaddr(WidgetTree_RunMessagePanelWithSound))(arg0-
 1053                              var_v1 = 1;
...
 1129          return NULL;
 1130      }
```

### src/fragments/widget_tree/widget_tree.c (54 lines)

```
 1889          return (arg1->buttonPressed & 0xF00) ? 8 : 0;
 1890      }
...
 1910                  var_t4 = 1;
 1911              }
...
 2429  void WidgetTree_SetMessagePanelText(unk_func_88509F48* arg0, unk_func_8850878C* arg1, s32 arg2, s32 arg3) {
 2430      s16 temp_v1;
...
 2432      *arg0->unk_34 = *arg1;
 2433      arg0->unk_34->unk_00.unk_28 |= 1;
 2434      Font_SetActive(arg1->unk_2C, arg1->unk_38);
 2435      Font_SetLineHeight(arg1->unk_3C);
...
 2437      if (arg3 == 0) {
 2438          arg3 = Font_MeasureTextExtent(0, 0, arg1->unk_40);
 2439      }
...
 2441      if (arg2 == 0) {
 2442          arg2 = Text_CountLines(arg1->unk_40);
 2443      }
...
 2445      temp_v1 = arg0->unk_38->unk_00[arg0->unk_38->unk_50].unk_02;
...
 2447      arg0->unk_2C->unk_00.unk_14.unk_00 = temp_v1 + arg3;
...
 2449      arg0->unk_2C->unk_00.unk_14.unk_02 = (((arg1->unk_3C * arg2) + temp_v1) - arg1->unk_3C) + temp_v1;
 2450      arg0->unk_2C->unk_00.unk_10.unk_00 = (arg0->unk_00.unk_14.unk_00 - arg0->unk_2C->unk_00.unk_14.unk_00) / 2
 2451      arg0->unk_2C->unk_00.unk_10.unk_02 = (arg0->unk_00.unk_14.unk_02 - arg0->unk_2C->unk_00.unk_14.unk_02) / 2
...
 2453      arg0->unk_30->unk_00.unk_14 = arg0->unk_2C->unk_00.unk_14;
...
 2455      arg0->unk_34->unk_00.unk_10.unk_00 = temp_v1 / 2;
 2456      arg0->unk_34->unk_00.unk_10.unk_02 = temp_v1 / 2;
 2457  }
...
 2459  s32 WidgetTree_HandleMessagePanelInput(unk_func_88509F48* arg0, Controller* arg1) {
 2460      s32 var_v1 = 0;
...
 2462      if (arg0->unk_2C->unk_30 & 2) {
 2463          if (arg1->buttonPressed & 0xC000) {
...
 2466      } else {
 2467          var_v1 = 1;
 2468      }
...
 2470      return var_v1;
 2471  }
...
 2473  void WidgetTree_OpenMessagePanel(unk_func_88509F48* arg0) {
 2474      WidgetTree_OpenAnimatedPanel(arg0->unk_2C);
 2475  }
...
 2481  void WidgetTree_RunMessagePanel(unk_func_88509F48* arg0, Controller* arg1, s32 arg2) {
 2482      s32 temp_v0;
 2483      intptr_t var_s0 = 0;
...
 2485      WidgetTree_OpenMessagePanel(arg0);
...
 2487      while (var_s0 == 0) {
 2488          Ui_SendMessageAndPollInput(var_s0);
...
 2490          temp_v0 = arg0->unk_00.unk_20(arg0, arg1);
 2491          if (!(temp_v0 & 1) && (temp_v0 & 2)) {
...
 2495          if (arg2 != 0) {
 2496              ((func8850BC94)Memmap_GetFragmentVaddr(Ui_PlayInputActionSound))(temp_v0);
 2497          }
 2498      }
...
 2500      WidgetTree_ResetMessagePanelAnimation(arg0);
 2501  }
...
 2503  void WidgetTree_RunMessagePanelWithSound(unk_func_88509F48* arg0, Controller* arg1) {
 2504      WidgetTree_RunMessagePanel(arg0, arg1, 1);
 2505  }
```

### src/gb_save.c (2 lines)

```
  428              break;
  429          }
```

### src/text_system.c (14 lines)

```
  152  s32 Text_CountLines(s8* arg0) {
  153      s32 last_chr = 0;
  154      s32 line_count = 0;
  155      s32 chr = *arg0++;
...
  157      while (chr != 0) {
  158          last_chr = chr;
  159          if (chr == '\n') {
...
  162          chr = *arg0++;
  163      }
...
  165      if (last_chr != '\n') {
  166          line_count++;
  167      }
...
  169      return line_count;
  170  }
```


---
# pc__blue

Baseline: 1 runs; new: pc__blue.

Functions: baseline 1049, new runs 1049, **0 only in the new runs**.
Lines: 10 lines only in the new runs.

(none)

### Source lines only the new runs reached

### src/fragments/transfer_pak_check/transfer_pak_check.c (4 lines)

```
 1321          case 3:
...
 1326              var_t0 = D_3004E20;
 1327  #endif
 1328              break;
```

### src/fragments/transfer_pak_select/transfer_pak_select.c (3 lines)

```
  833              case 3:
  834                  D_81304008[temp_s1].unk_01C = D_2022420;
  835                  break;
```

### src/gb_tower.c (2 lines)

```
  133              return 1;
  134          }
```

### src/pokemon_stats.c (1 lines)

```
  638                  var_s0 = POKEMON_ROM_BLUE;
```


---
# fb__illegal

Baseline: 1 runs; new: fb__illegal.

Functions: baseline 1956, new runs 1920, **53 only in the new runs**.
Lines: 1107 lines only in the new runs.

AuxiliaryEffect_Draw, BattleAI_ScaleStatHigh, BattleAnim_AllocOwnerDisplayContext, BattleAnim_CallbackDescriptor25AlphaFadeInHoldOut, BattleAnim_CallbackEndEffects73And74DefaultDescriptorFade, BattleAnim_CallbackInitModelAnimMode2Update, BattleAnim_CallbackOwnerModelTintPulseDetach, BattleAnim_EffectEndNop, BattleAnim_EffectSecondaryOwnerBonePosition, BattleAnim_EffectTertiaryOwnerModelScale, BattleAnim_EndEffect1RibbonMesh_Draw, BattleAnim_EndEffect1RibbonMesh_Init, BattleAnim_EndEffect1RibbonMesh_Update, BattleAnim_FreeOwnerDisplayContext, BattleAnim_GetOwnerContextParticle, BattleAnim_GetSecondaryOwnerBonePosition, BattleAnim_GetTertiaryOwnerModelScale, BattleAnim_InitModel, BattleAnim_PlayBattleSequenceById, BattleAnim_QueueEffectList6, BattleAnim_ResetSharedEffectMatrix, BattleAnim_SelectParticleModelFrames, BattleAnim_SetupDescriptor25AlphaFadeInHoldOut, BattleAnim_SetupOwnerModelTintPulseDetach, BattleAnim_SpawnDescriptor25AlphaFadeInHoldOut, BattleAnim_SpawnOwnerModelTintPulseDetach, BattleScene_AnimateCameraPresetToOwner, BattleScene_SetCameraPresetFromTable, BattleScene_TickRow11PostEndCamera, BattleScene_TickRow11QueueEffectList6, Battle_Effect_LowerStat, ParticleGfx_SetAnimatedAuxTileSizeOnList, ParticleGfx_SetAnimatedTileSizeOnList, ParticleMath_AddVec3f, ParticleMath_MakeXZVectorFromAngle, Particle_FieldCD_GetU8_0C048, Port_CbTexture, RibbonMesh_Configure, RibbonMesh_Draw, RibbonMesh_DrawPool, RibbonMesh_Init, RibbonMesh_Spawn, RibbonMesh_Update, RibbonMesh_UpdateEnvAlpha, RibbonMesh_UpdatePool, func_81409248, func_814092C8, func_8140935C, func_8430506C, func_8434B2B8, func_8434D890, func_8434DA94, func_8434DBEC

### Source lines only the new runs reached

### src/33FE0.c (2 lines)

```
 1202          dz = 0.0f;
 1203      }
```

### src/fragments/61/fragment61_2DFB60.c (2 lines)

```
  765              break;
  766          }
```

### src/fragments/battle_engine/battle_engine_2EC3C0.c (73 lines)

```
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
  431              sp6E = ptr2->unk_10;
...
  433              var_a1 = ((uintptr_t)_70D3A0_ROM_START) + (((uintptr_t)(D_6E910 + ((ptr2->unk_10 - (tmp = 2)) * 0x
  434              Dma_WriteChunks(ptr2, var_a1, var_a1 + 0x20, 0);
  435              Port_SwapSpeciesModelTransform(ptr2);
...
  437              D_84384578[side_index]->unk_10 = sp6E;
  438          }
...
 1303  void func_8430506C(ActivePokemon* active_pokemon) {
 1304      s32 pad3;
 1305      s32 sp48;
 1306      s32 sp44;
 1307      s32 sp40;
 1308      ActivePokemonState* sp3C;
 1309      s32 pad[2];
 1310      ActivePokemon* sp30;
 1311      BattleMonRuntime* sp28;
...
 1313      sp48 = 0xA5;
 1314      sp40 = BattleScene_GetParticipantSideIndex(active_pokemon) == 0;
 1315      sp3C = &sActivePokemon[sp40]->state;
 1316      sp30 = sActivePokemon[sp40];
 1317      sp28 = &active_pokemon->state.runtime;
...
 1319      sCurrentAnimation = &D_84384570[sp40]->unk_A80;
...
 1321      if (sp3C->unk_34 & 1) {
...
 1327      if ((sp3C->unk_34 & 1) && BattleScene_ByteArrayContains(sp30->displayObject.objectType, D_84384590, 8)) {
...
 1331      sActivePokemon[sp40]->animationTimer = 0;
...
 1333      if (!(sp3C->unk_34 & 0x4000) && !(sp3C->unk_34 & 2)) {
 1334          if ((sp28->usedMove == BUBBLE) && (D_843C4DEC < 0xA)) {
...
 1338          if ((D_843C4E44 == 0) || (sBattleMoveFailed != 0)) {
...
 1344          } else if (!(sp3C->unk_34 & 1)) {
 1345              BattleAnim_SetModelEventTrackFromRow(sActivePokemon[sp40], 0xA8);
 1346          }
...
 1348          ModelAnim_SetFrame(&sActivePokemon[sp40]->displayObject, 0);
 1349          ModelAnim_SetAnimation(&sActivePokemon[sp40]->displayObject, sCurrentAnimation->id);
...
 1351          if ((sp28->moveCache.lastMove == STOMP) || (sp28->moveCache.lastMove == BODY_SLAM) || (sp28->moveCache
...
 1354      }
 1355  }
...
 1403  void BattleScene_TickRow11QueueEffectList6(ActivePokemon* active_pokemon) {
 1404      BattleMonRuntime* ptr = &active_pokemon->state.runtime;
 1405      s32 sp28;
...
 1407      sp28 = BattleScene_GetParticipantSideIndex(active_pokemon) == 0;
 1408      sCurrentAnimation = &D_84384570[BattleScene_GetParticipantSideIndex(active_pokemon) == 0]->unk_000[ptr->us
 1409      if (sActivePokemon[sp28]->animationTimer == (sCurrentAnimation->unk_08 + 1)) {
 1410          if (BattleAnim_IsAssetLoadBusy(active_pokemon) == 0) {
 1411              if (D_843C4E45 == 1) {
 1412                  BattleAnim_QueueEffectList6(1, sActivePokemon[sp28]);
 1413                  BattleAnim_PlayBattleSequenceById(active_pokemon, 0x20);
 1414              }
 1415              gBattleSceneStateTickSlots[sBattleSceneStateTickSlotIndex].unk_04 = sBattleSceneStateTickSentinel;
 1416          }
 1417      }
 1418  }
...
 1420  void BattleScene_TickRow11PostEndCamera(ActivePokemon* active_pokemon) {
 1421      BattleMonRuntime* sp20;
... 13 more
```

### src/fragments/battle_engine/battle_engine_2F1D60.c (3 lines)

```
   82          arg0->unk_728.unk_186 = D_84384578[temp_v0]->unk_0E;
   83          tmp = D_84384578[temp_v0]->unk_00.z;
   84      }
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

### src/fragments/battle_engine/battle_engine_3020D0.c (15 lines)

```
  102          if ((tmp - 0x80) >= 0xB) {
  103              Text_GetString(arg0 + 1, 0x40, sBattleMessagesTable, tmp - 0x48);
  104          } else {
...
  381      } else if (temp_v0 == 'R') {
...
  383      } else if (temp_v0 == 'S') {
...
  385      } else if (temp_v0 == 'T') {
...
  387      } else if (temp_v0 == 'U') {
...
  389      } else if (temp_v0 == 'V') {
...
  391      } else if (temp_v0 == 'W') {
...
  393      } else if (temp_v0 == 'X') {
...
  395      } else {
  396          Audio_QueuePendingSoundId(temp_v0 + 0x64);
  397      }
...
  450                  arg0->unk_10 += 1;
  451              }
```

### src/fragments/battle_engine/battle_engine_304060.c (9 lines)

```
  162      return 0;
...
  193          BattleScene_SetCameraAtFromPosition(arg0, D_84385B90, 0, 0, 0.0f, 0.0f);
  194          BattleScene_SetCameraEyeAndStoreAngles(arg0, D_84385B90, 0x5B0, 0, 550.0f, 0.0f);
  195      }
...
  207          BattleScene_SetCameraAtFromPosition(arg0, D_84385B90, 0, 0, 0.0f, 0.0f);
  208          BattleScene_SetCameraEyeAndStoreAngles(arg0, D_84385B90, 0x1A4E, 0, 1000.0f, 0.0f);
  209      }
...
  239              sp50 = 325.0f;
  240          }
```

### src/fragments/battle_engine/battle_engine_3055E0.c (77 lines)

```
   16          return arg2;
   17      }
...
  130          sBattleScene.unk_00->unk_68 = 50.0f;
  131      }
...
  368                  *arg1 = *arg0;
  369              }
...
  427          case 0x96:
  428              if (sp3C < BattleScene_GetParticipantModelRadius(arg1)) {
  429                  BattleAnim_Vec3fCopy(&sp64, &arg1->swayState.unk_18);
  430              }
  431              break;
...
  473          case 0x96:
  474              var_fv1 = sp3C / (BattleScene_GetParticipantModelRadius(arg1) * 1.5f);
  475              break;
...
  834  s32 BattleScene_AnimateCameraPresetToOwner(CameraNode* arg0, ActivePokemon* arg1) {
  835      Vec3f sp6C;
  836      Vec3f sp60;
  837      Vec3f sp54;
  838      Vec3f sp48;
  839      f32 sp44;
  840      f32 sp40;
  841      f32 sp3C;
  842      f32 var_fa1;
  843      s16 sp36;
  844      s16 sp34;
...
  846      if (D_843901B0.unk_3C != 200.0f) {
...
  848      } else {
  849          arg0->perspective.verticalFieldOfView = 30.0f;
  850      }
...
  852      BattleAnim_ApproachF(&sBattleScene.unk_00->unk_94.z, D_843901B0.unk_34, D_843901B0.unk_38);
  853      BattleScene_GetOwnerCameraAnchor(arg1, &sp6C);
  854      Vec3f_CalculateDistanceAngles(&D_843901B0.unk_00, &sp6C, &sp44, &sp36, &sp34);
...
  856      var_fa1 = sp44 / (BattleScene_GetParticipantModelRadius(arg1) * 40.0f);
  857      if (var_fa1 >= 0.6f) {
...
  861      sp44 *= var_fa1;
  862      sp44 += D_843901B0.unk_30;
...
  864      Camera_ComputeEyeFromAngles(&D_843901B0.unk_00, &sp60, sp44, D_843901B0.unk_26, D_843901B0.unk_28);
  865      BattleAnim_Vec3fCopy(&sp54, &arg0->view.eyePosition);
  866      BattleAnim_CalculateDistanceAngles(&sp54, &sp60, &sp40, &sp36, &sp34);
  867      Camera_ComputeEyeFromAngles(&sp54, &arg0->view.eyePosition, sBattleScene.unk_00->unk_94.z * sp40, sp36, sp
  868      BattleScene_GetOwnerCameraAnchor(arg1, &sp6C);
  869      Vec3f_CalculateDistanceAngles(&D_843901B0.unk_00, &sp6C, &sp44, &sp36, &sp34);
...
  871      var_fa1 = sp44 / (BattleScene_GetParticipantModelRadius(arg1) * 40.0f);
  872      if (var_fa1 >= 0.6f) {
...
  875      sp44 *= var_fa1;
  876      sp44 += D_843901B0.unk_2C;
...
  878      Camera_ComputeEyeFromAngles(&D_843901B0.unk_00, &sp48, sp44, D_843901B0.unk_20, D_843901B0.unk_22);
  879      sp48.y += BattleScene_GetParticipantEyeHeight(arg1);
  880      BattleAnim_Vec3fCopy(&sp6C, &arg0->view.targetPosition);
  881      BattleAnim_CalculateDistanceAngles(&sp6C, &sp48, &sp3C, &sp36, &sp34);
  882      Camera_ComputeEyeFromAngles(&sp6C, &arg0->view.targetPosition, sBattleScene.unk_00->unk_94.z * sp3C, sp36,
...
  884      if ((sp40 <= 1.0f) && (sp3C <= 1.0f)) {
  885          return 1;
  886      }
  887      return 0;
  888  }
...
 1227  void BattleScene_SetCameraPresetFromTable(CameraNode* arg0, ActivePokemon* arg1, s32 arg2, s32 arg3) {
 1228      BattleAnim_GetOwnerDefaultAnchorPosition(arg1, &D_843901B0.unk_00);
 1229      BattleScene_LoadCameraPreset(&D_843849C0[arg2], arg0, arg1);
 1230      BattleScene_ApplyCameraPreset(arg0, arg1, 1);
... 17 more
```

### src/fragments/battle_engine/battle_engine_309ED0.c (13 lines)

```
 1300          case 24:
 1301              BattleScene_SelectCameraPreset(D_84390204, arg0,
 1302                            sBattleScene.unk_00->unk_50 = D_84385B88[BattleAnim_RandomIntWithWarmup(ARRAY_COUNT(
 1303              break;
...
 1561          case 5:
 1562          case 8:
 1563          case 9:
 1564          case 10:
 1565          case 11:
 1566              BattleScene_AnimateCameraPresetToOwner(arg0, D_84390204);
 1567              sp18 = 1;
 1568              break;
...
 1889                      BattleScene_EnterStateRow(D_84390204, 0xB);
```

### src/fragments/battle_engine/battle_engine_313D60.c (2 lines)

```
  344          sp5C = 0;
  345      }
```

### src/fragments/battle_engine/battle_engine_315D50.c (67 lines)

```
 4600  void BattleAnim_PlayBattleSequenceById(ActivePokemon* arg0, s32 arg1) {
 4601      s16 sp1E = arg0->displayObject.objectType;
...
 4603      if ((BattleScene_IsPartnerPikachu(arg0) != 0) && (arg1 == 0x32)) {
...
 4605      } else if (arg1 == 0x14) {
...
 4607      } else if (arg1 == 0x12) {
...
 4609      } else if (arg1 == 0x32) {
...
 4611      } else if (arg1 == 0x21) {
...
 4613      } else if (arg1 == 0x13) {
...
 4615      } else if (arg1 == 0x1E) {
...
 4617      } else if (arg1 == 0x1F) {
...
 4619      } else if (arg1 == 0x20) {
 4620          Audio_PlayCommand(0x20, 0, 0);
 4621      }
 4622  }
...
 4690              case 6:
 4691                  sp2C = D_84386DEC[arg0];
 4692                  break;
...
 4712          BattleAnim_RunEffectList(gBattleAnimQueuedAlternateMoveId, gBattleAnimQueuedAlternateList, gBattleAnim
 4713          gBattleAnimQueuedAlternatePending = 0;
 4714      }
...
 4787  void BattleAnim_QueueEffectList6(s32 arg0, ActivePokemon* arg1) {
 4788      gBattleAnimQueuedAlternatePending = 2;
 4789      gBattleAnimQueuedAlternateMoveId = arg0;
 4790      gBattleAnimQueuedAlternateList = 6;
 4791      gBattleAnimDefaultAnchor = 0xFF;
 4792      gBattleAnimAlternateAnchor = 0xFF;
 4793      gBattleAnimCurrentOwner = arg1;
 4794      gBattleAnimSecondaryOwner = arg1;
 4795      gBattleAnimTertiaryOwner = arg1;
 4796  }
...
 4930  Particle* BattleAnim_GetOwnerContextParticle(DisplayObject* arg0) {
 4931      return arg0->node.callbackArg;
 4932  }
...
 4934  DisplayObject* BattleAnim_AllocOwnerDisplayContext(Particle* arg0) {
 4935      s32 i;
 4936      s32 var_v0 = -1;
 4937      DisplayObject* temp_a0;
...
 4939      for (i = 0; i < 16; i++) {
 4940          if (D_84391A50[i] == 0) {
 4941              var_v0 = i;
 4942              break;
 4943          }
 4944      }
...
 4946      if (var_v0 >= 0) {
 4947          temp_a0 = &D_843903D0[var_v0];
 4948          D_84391A50[var_v0] = 1;
 4949          temp_a0->node.callbackArg = arg0;
 4950      } else {
...
 4953      return temp_a0;
 4954  }
...
 4956  void BattleAnim_FreeOwnerDisplayContext(DisplayObject* arg0) {
 4957      s32 i;
...
 4959      for (i = 0; i < 16; i++) {
 4960          if (arg0 == &D_843903D0[i]) {
 4961              break;
 4962          }
 4963      }
...
 4965      if (i != 16) {
 4966          D_84391A50[i] = 0;
 4967          ModelRenderer_ClearDisplayObject(arg0);
... 7 more
```

### src/fragments/battle_engine/battle_engine_317E70.c (26 lines)

```
  143          if (Particle_HasFlags(arg0, 0x400) != 0) {
...
  145          } else {
  146              BattleAnim_FreeOwnerDisplayContext(arg0->unk_14);
  147          }
  148      }
...
  194          case 12:
  195              BattleAnim_GetOwnerBonePosition(arg0->unk_10, 0x64, &arg0->unk_68);
  196              BattleScene_GetParticipantModelRotation(arg0->unk_10, &sp28);
  197              BattleScene_GetParticipantModelBoundsScaled(arg0->unk_10, &sp30);
  198              ParticleMath_MakeXZVectorFromAngle(&sp3C, sp30.x * 25.0f, sp28.y + 0x8000);
  199              ParticleMath_AddVec3f(&arg0->unk_68, &sp3C);
  200              break;
...
  676  BattleAnimEffectSlot* BattleAnim_SpawnDescriptor25AlphaFadeInHoldOut(s32 arg0, s16 arg1, s16 arg2, u8 arg3, u8
  677      return BattleAnim_CreateEffectSlot(arg0, 0, 1, BattleAnim_CallbackDescriptor25AlphaFadeInHoldOut, &gBattle
  678  }
...
  728  BattleAnimEffectSlot* BattleAnim_SpawnOwnerModelTintPulseDetach(s32 arg0, s16 arg1, u8 arg2, s16 arg3, s16 arg
  729      return BattleAnim_CreateEffectSlot(arg0, 0, 1, BattleAnim_CallbackOwnerModelTintPulseDetach, &gDefaultPart
  730  }
...
  787  void BattleAnim_EffectEndNop(void) {
  788  }
...
  851  void BattleAnim_GetSecondaryOwnerBonePosition(Vec3f* arg0) {
  852      BattleAnim_GetOwnerBonePosition(gBattleAnimSecondaryOwner, 0x64, arg0);
  853  }
...
  897  f32 BattleAnim_GetTertiaryOwnerModelScale(void) {
  898      return BattleAnim_GetOwnerModelScale(gBattleAnimTertiaryOwner);
  899  }
```

### src/fragments/battle_engine/battle_engine_31C660.c (54 lines)

```
   57  void BattleAnim_SetupOwnerModelTintPulseDetach(Particle* arg0) {
   58      ParticleGfx_RecolorPrim(arg0, Particle_FieldCF_GetS16_0C058(arg0));
   59      Particle_FieldAE_SetS16_0BF4C(arg0, arg0->unk_CE);
   60      Particle_AdvanceLifecycle(arg0);
   61  }
...
   63  void BattleAnim_CallbackOwnerModelTintPulseDetach(Particle* arg0) {
   64      switch (arg0->unk_B2) {
   65          case 1:
   66              BattleAnim_SetupOwnerModelTintPulseDetach(arg0);
   67              break;
...
   69          case 2:
   70              if (Particle_FieldAE_Approach_0BF60(arg0, arg0->unk_CD, arg0->unk_AA) != 0) {
   71                  Particle_AdvanceLifecycle(arg0);
   72              }
   73              break;
...
   75          case 3:
   76              Particle_FieldA6_DecrementToZeroStep1AndStepState_0BDD0(arg0);
   77              break;
...
   79          case 4:
   80              if (Particle_FieldAE_Approach_0BF60(arg0, 0, arg0->unk_AA) != 0) {
   81                  Particle_AdvanceLifecycle(arg0);
   82              }
   83              break;
...
   85          case 5:
   86              Particle_DisableAndDetach(arg0);
   87              break;
   88      }
...
   90      if (Particle_IsRunning(arg0) != 0) {
   91          Model_SetMaterialColor(&arg0->unk_10->displayObject, arg0->prim_r, arg0->prim_g, arg0->prim_b, arg0->u
   92      }
   93  }
...
  410  void BattleAnim_SetupDescriptor25AlphaFadeInHoldOut(Particle* arg0) {
  411      ParticleGfx_RecolorPrim(arg0, Particle_FieldCF_GetS16_0C058(arg0));
  412      Particle_SetPrimAlpha(arg0, 0);
  413      Particle_AdvanceLifecycle(arg0);
  414  }
...
  416  void BattleAnim_CallbackDescriptor25AlphaFadeInHoldOut(Particle* arg0) {
  417      switch (arg0->unk_B2) {
  418          case 1:
  419              BattleAnim_SetupDescriptor25AlphaFadeInHoldOut(arg0);
  420              break;
...
  422          case 2:
  423              Particle_PrimAlpha_ApproachAndStepState_0BC5C(arg0, arg0->unk_CD, arg0->unk_AA);
  424              break;
...
  426          case 3:
  427              Particle_FieldA6_DecrementToZeroStep1AndStepState_0BDD0(arg0);
  428              break;
...
  430          case 4:
  431              if (Particle_PrimAlpha_Approach_0BC2C(arg0, 0, arg0->unk_AA) != 0) {
  432                  Particle_MarkInactive(arg0);
  433              }
  434              break;
  435      }
  436  }
```

### src/fragments/battle_engine/battle_engine_32B680.c (131 lines)

```
   17  static void* Port_CbTexture(void* arg) {
   18      return (void*)Util_ConvertAddrToVirtAddr((uintptr_t)arg);
   19  }
...
   87  void BattleAnim_SelectParticleModelFrames(Particle* arg0, s32 arg1) {
   88      f32 sp2C = 1.0f;
   89      s16 sp2A;
   90      s16 sp28;
   91      s16 sp26;
   92      s16 sp24 = Particle_FieldCD_GetU8_0C048(arg0);
...
   94      switch (Particle_FieldCF_GetS16_0C058(arg0)) {
...
  283          case 24:
  284              sp2A = 0xAE;
  285              Particle_SetPrimAlpha(arg0, 0);
  286              break;
...
  404      }
...
  406      arg0->unk_14 = BattleAnim_AllocOwnerDisplayContext(arg0);
  407      if (arg0->unk_14 != NULL) {
  408          switch (arg1) {
...
  418              case 2:
  419                  BattleAnim_InitModel(arg0->unk_14, sp2A);
  420                  break;
  421          }
  422          Particle_Field1C_SetScaled_0B938(arg0, sp2C);
  423          Particle_AdvanceLifecycle(arg0);
  424      } else {
...
  427  }
...
  485  void BattleAnim_CallbackInitModelAnimMode2Update(Particle* arg0) {
  486      switch (arg0->unk_B2) {
  487          case 1:
  488              BattleAnim_SelectParticleModelFrames(arg0, 2);
  489              break;
...
  491          case 2:
  492              Particle_PrimAlpha_ApproachAndStepState_0BC5C(arg0, arg0->unk_A6, Particle_FieldCE_GetS32_0C040(ar
  493              break;
  494      }
  495  }
...
 4111  void func_8434B2B8(Gfx* arg0, u8* arg1, UNUSED u16 arg2) {
 4112      u8* a1;
 4113      Particle* spC8;
 4114      u8 var_t0;
 4115      s16 spC4;
 4116      s16 spC2;
 4117      s16 spC0;
 4118      s16 spBE;
 4119      s16 var_a1;
 4120      s16 var_a2;
 4121      s16 var_a3;
 4122      s16 var_s1;
 4123      UNUSED s16 pad2;
 4124      s16 spB2;
 4125      s16 spB0;
 4126      s16 spAE;
 4127      s16 spAC;
 4128      u16 temp_v0_2;
 4129      s16 var_t2;
 4130      s16 var_t1;
...
 4132      spC8 = BattleAnim_GetOwnerContextParticle(D_8006F09C);
 4133      temp_v0_2 = Particle_FieldCD_GetU8_0C048(spC8);
 4134      var_s1 = 0x20;
 4135      var_t1 = 5;
... 71 more
```

### src/fragments/battle_engine/battle_engine_339CA0.c (7 lines)

```
 1375          case 13:
 1376              Particle_Field1C_SetScaled_0B938(arg0, 1.0f);
 1377              Particle_Field74_SetScaled_0AD8C(arg0, 0.0f);
 1378              func_81409248(arg0, 0x1E);
 1379              func_8140935C(arg0, 0x14, 0xA);
 1380              func_814092C8(arg0, 0x1E);
 1381              break;
```

### src/fragments/battle_engine/battle_engine_343BA0.c (25 lines)

```
  110  void BattleAnim_EffectSecondaryOwnerBonePosition(Vec3f* arg0) {
  111      BattleAnim_GetSecondaryOwnerBonePosition(arg0);
  112  }
...
  114  f32 BattleAnim_EffectTertiaryOwnerModelScale(void) {
  115      return BattleAnim_GetTertiaryOwnerModelScale();
  116  }
...
  204  void BattleAnim_ResetSharedEffectMatrix(void) {
  205      guRotateRPY(&D_84392588, 0.0f, 0.0f, 0.0f);
  206  }
...
  229  void BattleAnim_EndEffect1RibbonMesh_Init(void) {
  230      Vec3f sp24;
  231      f32 sp20;
...
  233      BattleAnim_ResetSharedEffectMatrix();
  234      BattleAnim_EffectSecondaryOwnerBonePosition(&sp24);
  235      sp20 = BattleAnim_EffectTertiaryOwnerModelScale();
  236      RibbonMesh_Init();
  237      RibbonMesh_Spawn(sp24.x, sp24.y, sp24.z, 40.0f, sp20, 1);
  238  }
...
  240  s32 BattleAnim_EndEffect1RibbonMesh_Update(void) {
  241      return RibbonMesh_UpdatePool();
  242  }
...
  244  void BattleAnim_EndEffect1RibbonMesh_Draw(void) {
  245      gSPMatrix(gDisplayListHead++, &D_84392588, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
...
  247      gDisplayListHead = RibbonMesh_DrawPool(gDisplayListHead);
  248  }
```

### src/fragments/battle_engine/battle_engine_347330.c (311 lines)

```
  179  void RibbonMesh_Init(void) {
  180      RibbonMesh* ptr = &D_843925E0[0];
  181      s32 i;
...
  183      for (i = 0; i < 4; i++) {
  184          ptr->unk_018 = ptr->unk_000 = 0;
  185          ptr++;
  186      }
  187  }
...
  190                     s32 arg9) {
  191      arg0->unk_028 = arg1;
  192      arg0->unk_02C = arg2;
  193      arg0->unk_030 = arg3;
  194      arg0->unk_038 = arg4;
  195      arg0->unk_03C = arg5;
  196      arg0->unk_040 = arg6;
  197      arg0->unk_024 = arg7;
  198      arg0->unk_034 = arg8;
  199      arg0->unk_044 = arg9;
  200  }
...
  202  void RibbonMesh_Spawn(f32 arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4, s32 arg5) {
  203      RibbonMesh* ptr;
  204      RibbonMeshVertexState* ptr1;
  205      RibbonMeshVertexState* ptr2;
  206      RibbonMeshVertexState* ptr3;
  207      s32 i;
  208      u8 temp_lo;
  209      u8 temp_lo_2;
  210      s32 temp_v0_2;
  211      s32 var_v1;
  212      u8 temp_a0;
  213      u8 temp_a1;
  214      u8 temp_v1;
...
  216      var_v1 = -1;
...
  218      for (i = 0; i < 4; i++) {
  219          if (D_843925E0[i].unk_000 == 0) {
  220              var_v1 = i;
  221              break;
  222          }
  223      }
...
  225      if (var_v1 == -1) {
...
  229      ptr = &D_843925E0[var_v1];
...
  231      ptr->unk_000 = 1;
  232      ptr->unk_018 = 0;
  233      ptr->unk_004 = arg0;
  234      ptr->unk_00C = arg2;
  235      ptr->unk_010 = arg3 * arg4;
  236      ptr->unk_014 = arg4;
  237      ptr->unk_01E = -1;
...
  239      switch (arg5) {
...
  249          case 1:
  250              ptr->unk_01C = -1;
  251              ptr->unk_01A = 0x78;
  252              ptr->unk_008 = (arg4 * 100.0) + arg1;
  253              ptr->unk_020 = 1.5f;
  254              RibbonMesh_Configure(&D_843925E0[var_v1], 0xFF, 0xFF, 0xC8, 0xFF, 0x20, 0x20, 0x80, 0x80, 0);
  255              break;
...
  264      }
...
  266      ptr1 = &ptr->unk_430[0];
  267      for (i = 0; i < 31; i++) {
  268          ptr2 = ptr1++;
... 251 more
```

### src/fragments/battle_engine/battle_engine_3591E0.c (6 lines)

```
  228  Gfx* AuxiliaryEffect_Draw(s16 arg0, Gfx* arg1) {
  229      UNUSED s32 pad;
  230      AuxiliaryEffect* temp_s3 = &D_843C49C0[arg0];
...
  232      if (arg0 < 0) { // PORT: no free effect slot (the caller keeps -1); element -1 is unrelated memory on the 
  233          return arg1;
  234      }
```

### src/fragments/battle_engine/battle_engine_359F90.c (18 lines)

```
  243          Battle_QueueMessage(gBattleMessageQueues->unk_1C8, 0x25);
...
  300      return 1;
...
  465              if (D_843C4DCA & 0xFF00) {
  466                  D_843C4DC8 >>= 2;
  467                  D_843C4DCA >>= 2;
  468                  if (D_843C4DC8 == 0) {
...
  471              }
  472          }
...
  503          gBattleDamage = 0x3E4;
  504      }
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
```

### src/fragments/battle_engine/battle_engine_35DF70.c (57 lines)

```
  446  void Battle_Effect_LowerStat(void) {
  447      s32 sp2C;
  448      s32 sp28;
  449      s32 sp24;
  450      s32 sp20;
  451      u16* sp1C;
  452      u16* sp18;
...
  454      if (gBattleUser->moveCache.unk_01 < 0x44) {
...
  456      } else {
  457          sBattleScene.unk_00->unk_1A = 3;
  458      }
...
  460      if (gBattleTarget->unk_4D & 0x10) {
...
  464      sp24 = gBattleUser->moveCache.unk_01;
  465      sp20 = 1;
...
  467      if (sp24 >= 0x44) {
  468          if (Battle_Random() < 0x55) {
  469              sp24 -= 0x44;
  470              goto block_14;
  471          }
  472      } else {
...
  489      block_14:
  490          if (gBattleTarget->unk_5C[sp24] != 1) {
  491              gBattleTarget->unk_5C[sp24] -= sp20;
  492              if (gBattleTarget->unk_5C[sp24] <= 0) {
...
  496              sp28 = 0;
  497              sp2C = 0;
...
  499              if (sp24 < 4) {
  500                  switch (sp24) {
...
  518                      case 3:
  519                          sp18 = &gBattleTarget->unk_3E;
  520                          sp1C = &gBattleTarget->unk_30;
  521                          break;
  522                  }
...
  524                  if (*sp1C != 1) {
  525                      *sp1C = (*sp18 * gStatStageMultipliers[gBattleTarget->unk_5C[sp24] - 1].unk_00) /
  526                              gStatStageMultipliers[gBattleTarget->unk_5C[sp24] - 1].unk_01;
  527                      if (*sp1C == 0) {
...
  530                  } else {
...
  533              }
...
  535              if (sp20 == 2) {
...
  538              } else {
  539                  Text_SetStringToken(0x19, Battle_GetActiveMonNickname(sActivePokemon[!sBattleScene.unk_00->unk
  540                  Battle_QueueMessage(gBattleMessageQueues->unk_208, sp24 + 0xA0);
  541              }
...
  543              if ((sp28 != 0) || (D_8438AC60[0] != 1)) {
...
  547              if ((sp2C != 0) || (D_8438AC60[0] != 1)) {
...
  550              return;
  551          }
  552      }
...
  554  block_39:
  555      if (gBattleUser->moveCache.unk_01 < 0x44) {
...
  562  end:
  563      if (gBattleUser->moveCache.unk_01 < 0x44) {
...
  569      } else {
  570          D_843C4E45 = 0;
  571      }
  572  }
```

### src/fragments/battle_engine/battle_engine_361050.c (142 lines)

```
  198                  default:
  199                      var_v0 = arg0->unk_26;
  200                      var_v0 = (var_v0 >> 1) + (var_v0 >> 2);
...
  285              if (D_843C6140 & 0xFF00) {
  286                  D_843C613E >>= 2;
  287                  D_843C6140 >>= 2;
  288                  if (D_843C613E == 0) {
...
  291              }
  292          }
...
  305          D_843C6140 >>= 1;
  306          if (D_843C6140 == 0) {
...
  309      }
...
  318          D_843C613A = 0x3E4;
  319      }
...
  467  u16 BattleAI_ScaleStatHigh(BattleMonRuntime* arg0) {
  468      u16 var_v1 = arg0->unk_3C << 2;
...
  470      if (arg0->unk_15 & 0x40) {
...
  474      if (var_v1 == 0) {
...
  478      return var_v1;
  479  }
...
  670          case 1:
  671              *arg3 = 0xC;
  672              temp_v0 = BattleAI_ClassifyMoveAvailability(arg0, arg1, *arg3);
  673              if (temp_v0 != 0) {
...
  677              *arg3 = 0x20;
  678              temp_v0 = BattleAI_ClassifyMoveAvailability(arg0, arg1, *arg3);
  679              if (temp_v0 != 0) {
...
  683              *arg3 = 0x5A;
  684              temp_v0 = BattleAI_ClassifyMoveAvailability(arg0, arg1, *arg3);
  685              if (temp_v0 != 0) {
...
  688              break;
...
 1781              case 0x47:
 1782                  var_s2->unk_04 = 0x55;
 1783                  if (var_s0->unk_2E >= var_s3->unk_2E) {
 1784                      if (var_s1 >= var_s3->unk_0C) {
 1785                          BattleAI_ApplyInverseWeightScale(spFB, &spFA);
 1786                      }
 1787                  } else if (var_s4 >= var_s0->unk_0C) {
...
 1790                  break;
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
 1964              case 0x34:
 1965              case 0x35:
 1966              case 0x39:
 1967              case 0x40:
 1968              case 0x41:
 1969              case 0x52:
 1970              case 0x53:
 1971              case 0x55:
 1972              case 0x56:
 1973                  spFB = 0xFF;
 1974                  if (var_s0->unk_15 & 0x40) {
...
 1978                  if (var_s0->unk_4C & 0x80) {
...
 1982                  if (spFB == 0) {
...
 1986                  if ((arg2 != 0) && (sp102 >= spFC)) {
... 82 more
```

### src/fragments/particle_math/particle_math_261F90.c (13 lines)

```
  493  void ParticleMath_AddVec3f(Vec3f* arg0, Vec3f* arg1) {
  494      arg0->x += arg1->x;
  495      arg0->y += arg1->y;
  496      arg0->z += arg1->z;
  497  }
...
  597  Vec3f* ParticleMath_MakeXZVectorFromAngle(Vec3f* arg0, f32 arg1, s16 arg2) {
  598      f32 temp_fv0 = SINS(arg2);
  599      f32 temp_fv1 = COSS(arg2);
...
  601      arg0->x = arg1 * temp_fv0;
  602      arg0->y = 0.0f;
  603      arg0->z = arg1 * temp_fv1;
...
  605      return arg0;
  606  }
```

### src/fragments/particle_math/particle_math_264380.c (8 lines)

```
  958  void ParticleGfx_SetAnimatedTileSizeOnList(Gfx* gfx, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
  959      gDPSetTileSize(gfx, G_TX_RENDERTILE, arg1 * gParticleFrameCounter, -arg2 * gParticleFrameCounter, (((arg1 
  960                     (((-arg2 * gParticleFrameCounter) + arg4) - 1) << 2);
  961  }
...
  963  void ParticleGfx_SetAnimatedAuxTileSizeOnList(Gfx* gfx, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
  964      gDPSetTileSize(gfx, 1, arg1 * gParticleFrameCounter, -arg2 * gParticleFrameCounter, (((arg1 * gParticleFra
  965                     (((-arg2 * gParticleFrameCounter) + arg4) - 1) << 2);
  966  }
```

### src/fragments/particle_math/particle_math_269820.c (25 lines)

```
  576          arg0->unk_14->position.x = arg0->unk_2C.x;
  577          arg0->unk_14->position.y = arg0->unk_2C.y;
  578          arg0->unk_14->position.z = arg0->unk_2C.z;
...
  580          arg0->unk_14->scale.x = arg0->unk_1C;
  581          arg0->unk_14->scale.y = arg0->unk_1C;
  582          arg0->unk_14->scale.z = arg0->unk_1C;
...
  584          if (Particle_LacksFlags(arg0, 0x200) != 0) {
  585              arg0->unk_14->rotation.x = arg0->unk_94.x;
  586              arg0->unk_14->rotation.y = arg0->unk_94.y;
  587              arg0->unk_14->rotation.z = arg0->unk_94.z;
  588          }
  589          arg0->unk_14->lodFraction = arg0->prim_a;
  590      }
...
  717  void func_81409248(Particle* arg0, s32 arg1) {
  718      arg0->unk_38.x = ParticleMath_RandomSigned(arg1) * gParticleRenderContext.unk_00.y;
  719  }
...
  725  void func_814092C8(Particle* arg0, s32 arg1) {
  726      arg0->unk_38.z = ParticleMath_RandomSigned(arg1) * gParticleRenderContext.unk_00.y;
  727  }
...
  733  void func_8140935C(Particle* arg0, s32 arg1, s32 arg2) {
  734      arg0->unk_38.y = (ParticleMath_RandomSigned(arg1) * gParticleRenderContext.unk_00.y) + arg2;
  735  }
...
 1887  u8 Particle_FieldCD_GetU8_0C048(Particle* arg0) {
 1888      return arg0->unk_CD;
 1889  }
```

### src/fragments/rental_team_selection/rental_team_selection_2E2C20.c (4 lines)

```
  351                          break;
  352                      }
...
  365                          break;
  366                      }
```

### src/gb_data.c (3 lines)

```
 1053          case 5:
 1054              sp4 = (arg1 * arg1 * arg1 * 5) / 4;
 1055              break;
```

### src/matrix.c (5 lines)

```
  715          var_fv0 = b;
  716          var_v0 = 0x42;
  717      }
...
  750          dest->x = ((r - g) / var_ft5) + 4.0;
  751      }
```


---
# fb__blue

Baseline: 1 runs; new: fb__blue.

Functions: baseline 1956, new runs 1955, **0 only in the new runs**.
Lines: 13 lines only in the new runs.

(none)

### Source lines only the new runs reached

### src/fragments/battle_engine/battle_engine_317E70.c (3 lines)

```
  831                  if (GeoRender_FindAnchorPosition(&arg0->displayObject, 0x64, arg2) != NULL) {
  832                      sp24 = 3;
  833                  }
```

### src/fragments/rental_team_selection/rental_team_selection_2DB900.c (3 lines)

```
  711                  case 3:
  712                      D_8423E508[i] = D_2022420;
  713                      break;
```

### src/fragments/transfer_pak_check/transfer_pak_check.c (4 lines)

```
 1321          case 3:
...
 1326              var_t0 = D_3004E20;
 1327  #endif
 1328              break;
```

### src/gb_tower.c (2 lines)

```
  133              return 1;
  134          }
```

### src/pokemon_stats.c (1 lines)

```
  638                  var_s0 = POKEMON_ROM_BLUE;
```


---
# trade__two_base

Baseline: 1 runs; new: trade__two_base.

Functions: baseline 775, new runs 969, **194 only in the new runs**.
Lines: 3631 lines only in the new runs.

BattleHud_DrawHpBarTexture, BinArchive_LoadFileFromRomRange, BinArchive_LoadFileToPool, CalculateStatValue, Color_AdjustRGB5551, Color_HSVInterpolate, Color_HSVToRGB, Color_HSVToRGB5551, Color_RGB5551ToHSV, Color_RGBToHSV, Deck_CloseAndFlush, Deck_GetCapacity, Deck_GetEntrySize, Deck_InitializeBoxBackend, Deck_Open, Deck_ReadEntries, Deck_ReadEntry, Fragment_LoadFromRomRange, Fragment_RelocateAndCommit, GbData_ReadU24BE, GbSave_LoadMainPokemon, GetPokemonDV, GfxImage_ClearDepthRectangle, MainPool_ResetStateToOffset, Math_StepToS, Model_ComputeSizeVariant, MtxF_SetRotationAndTransformTranslation, PokeIcon_AdjustImageColors, PokeIcon_AdvancePreviewMotion, PokeIcon_AllocFramebuffers, PokeIcon_ApplyLoadResult, PokeIcon_CapturePreviewGeoNode, PokeIcon_CreateModelPreview, PokeIcon_DrawPreview, PokeIcon_InitPreviewScene, PokeIcon_LoadModelArchive, PokeIcon_LoadModelTexture, PokeIcon_LoadModelTextureForMon, PokeIcon_RenderPreview, PokeIcon_RequestFrameLoad, PokeIcon_SetPreviewMon, PokeIcon_UpdatePreview, PokeIcon_WaitFrameLoad, PokemonType_ToDisplayIndex, Pokemon_ExpForLevel, Pokemon_FromGbRecord, Pokemon_FromGbRecordExt, Pokemon_LevelFromExp, Pokemon_RecalcStats, StatExpSqrt, Text_CopySpeciesName, Text_CopyString, TradeCable_CheckSequenceCondition, TradeCable_DrawBalls, TradeCable_DrawLightSpark, TradeCable_DrawLights, TradeCable_DrawScissoredTexRect, TradeCable_DrawSequence, TradeCable_DrawTubeCenterBar, TradeCable_DrawTubeEndCap, TradeCable_DrawTubeEndCapReflective, TradeCable_DrawTubeJoint, TradeCable_DrawTubeRailBar, TradeCable_DrawTubeSparkBandBright, TradeCable_DrawTubeSparkBandDim, TradeCable_InitSequence, TradeCable_IsSequenceActive, TradeCable_RenderReflectionTexture, TradeCable_ResetBalls, TradeCable_ResetLights, TradeCable_SetLightState, TradeCable_SetSequenceState, TradeCable_SetSequenceTimer, TradeCable_UpdateBalls, TradeCable_UpdateLightIdlePulse, TradeCable_UpdateLightShrinkToIdle, TradeCable_UpdateLights, TradeCable_UpdateSequence, TradeCable_UpdateSequenceIdle, TradeCable_UpdateSequenceOpen, TradeCable_UpdateSequenceSlideHorizontal, TradeCable_UpdateSequenceSlideVertical, TradeCable_UpdateSequenceWaitLight0, TradeCable_UpdateSequenceWaitLight1, TradeEvo_Draw, TradeEvo_ResetState, TradeEvo_Update, Trade_ChangeTopic, Trade_ConfirmPickSelection, Trade_Draw, Trade_DrawBox3DModel, Trade_DrawBoxMachine, Trade_DrawBoxMachineBanner, Trade_DrawBoxMachineConfirm, Trade_DrawBoxMachineCursor, Trade_DrawBoxMachineSlots, Trade_DrawBoxMachineTitle, Trade_DrawBoxMachineTopBar, Trade_DrawBoxMachineTransferArrows, Trade_DrawBoxTabBar, Trade_DrawBoxTabIcon, Trade_DrawDigitStrip, Trade_DrawFadeRect, Trade_DrawFilledFrame, Trade_DrawInfoBox, Trade_DrawInfoBoxIfActive, Trade_DrawMovingSheen, Trade_DrawNumberTwoDigits, Trade_DrawPickScreenFrame, Trade_DrawPickScreens, Trade_DrawPickSlotBackdrop, Trade_DrawPlayerBanner, Trade_DrawPortInfoPanel, Trade_DrawRoundedFrameLarge, Trade_DrawRoundedFrameSmall, Trade_DrawSaveSeq, Trade_DrawSelectionCorners, Trade_DrawSolidFillRect, Trade_DrawTextPrinter, Trade_DrawTitleBarStrip, Trade_FillRect, Trade_GetBoxMachineFlowState, Trade_GetBoxSlotType, Trade_GetInfoBoxState, Trade_GetMoveName, Trade_GetPickBufferSlot, Trade_GetPickScreenSnapshot, Trade_GetSaveSeqState, Trade_GetSelectedBox, Trade_GetSpeciesName, Trade_GetTextPrinterState, Trade_GetTypeName, Trade_Init, Trade_InitBoxMachine, Trade_InitPickScreen, Trade_IsMoveInvalid, Trade_IsPickScreenActive, Trade_IsTextMenuAtEnd, Trade_Loop, Trade_Main, Trade_OpenInfoBox, Trade_ReadDeckEntries, Trade_ReadInput, Trade_ResetBoxMachineFlow, Trade_ResetFade, Trade_ResetInfoBox, Trade_ResetPickScreens, Trade_ResetSaveSeq, Trade_ResetTextMenu, Trade_ResetTextPrinter, Trade_ScanPorts, Trade_SetBoxSlotAnimState, Trade_SetFadeRect, Trade_SetState, Trade_SetTextPrinter, Trade_SetupBoxSlots, Trade_ShowTextMenu, Trade_Update, Trade_UpdateBoxMachine, Trade_UpdateBoxMachineFlowClose, Trade_UpdateBoxMachineFlowConfirmInput, Trade_UpdateBoxMachineFlowConfirmOpen, Trade_UpdateBoxMachineFlowOpen, Trade_UpdateBoxMachineFlowPanelsFull, Trade_UpdateBoxMachineFlowSelect, Trade_UpdateBoxMachineRect, Trade_UpdateBoxMachineSlots, Trade_UpdateBoxSlotBob, Trade_UpdateBoxSlotBobIdle, Trade_UpdateBoxSlotFlyIn, Trade_UpdateBoxSlotFlyOut, Trade_UpdateBoxSlotTransforms, Trade_UpdateInfoBox, Trade_UpdateInfoBoxChoose, Trade_UpdateInfoBoxCloseOut, Trade_UpdateInfoBoxOpenIn, Trade_UpdateMainMenu, Trade_UpdatePickScreenClose, Trade_UpdatePickScreenInput, Trade_UpdatePickScreenOpen, Trade_UpdatePickScreens, Trade_UpdateSaveSeq, Trade_UpdateTextPrinter, Trade_UpdateWaitForBoxConfirm, Trade_UpdateWaitForCableCondition, Trade_UpdateWaitForPickSelection, Ui_DrawBorderStyleA, Ui_DrawGradientPanel, Ui_DrawGradientRectangle, Ui_DrawTexturedRect8x8, Yay0_AllocAndDecompressFragment, func_82F00960, func_82F04604, func_82F10BB4

### Source lines only the new runs reached

### src/battle_hud.c (1 lines)

```
  220              D_800AF7B4[arg0] = 0;
```

### src/display_object_textures.c (141 lines)

```
  113  Fragment* Yay0_AllocAndDecompressFragment(MainPoolState* arg0, PERSSZP* arg1, PERSSZP* arg2) {
  114      u8* sp1C = MainPool_AllocAligned(arg0, arg2->decompressed_size2, 0x10);
...
  116      if (sp1C != NULL) {
  117          Yay0_DecompressAndRelocate(arg2, sp1C);
  118      }
  119      return sp1C;
  120  }
...
  132  Fragment* Fragment_LoadFromRomRange(MainPoolState* arg0, s32 start, s32 end, PERSSZP* arg3, s32 arg4) {
  133      UNUSED s32 pad;
  134      u32 size = ALIGN16(end - start);
  135      Fragment* var_v1 = NULL;
...
  137      if (arg4 >= size) {
  138          Dma_WriteChunks(arg3, start, end, 0);
...
  140          if (PORT_BE32(((u32*)arg3->magic)[0]) == 'PERS' && (PORT_BE32(((u32*)arg3->magic)[1]) == '-SZP')) {
  141              Port_FixPersHeader(arg3);
  142              var_v1 = Yay0_AllocAndDecompressFragment(arg0, arg3, arg3);
  143          } else if (PORT_BE32(((u32*)arg3->magic)[0]) == 'PRES' && (PORT_BE32(((u32*)arg3->magic)[1]) == 'JPEG'
...
  149      }
...
  151      return var_v1;
  152  }
...
  182  void Fragment_RelocateAndCommit(MainPoolState* arg0, u32 arg1, Fragment* arg2) {
  183      Memmap_RelocateFragment(arg1, arg2);
  184      Port_RelocateDataBlob(arg2); // PORT: before the relocation table is released by the reset below
  185      MainPool_ResetStateToOffset(arg0, ((uintptr_t)arg2 + arg2->sizeInRam) - (uintptr_t)arg0->listHeadR);
  186  }
...
  188  Fragment* BinArchive_LoadFileFromRomRange(MainPoolState* arg0, BinArchive* arg1, BinArchiveFile* arg2, uintptr
  189      s32 temp_a1 = arg1->unk_04 + arg2->offset;
...
  191      return Fragment_LoadFromRomRange(arg0, temp_a1, arg2->size + temp_a1, arg3, arg4);
  192  }
...
  204  Fragment* BinArchive_LoadFileToPool(MainPoolState* arg0, BinArchive* arg1, s32 arg2, uintptr_t arg3, s32 arg4)
  205      s32 pad[4];
  206      Fragment* var_s0 = NULL;
  207      BinArchiveFile* sp2C = (u8*)arg1 + sizeof(BinArchive) + (arg2 * sizeof(BinArchiveFile));
...
  209      if (arg2 < arg1->num_files) {
  210          if (arg1->unk_00 & 1) {
...
  212          } else if (!(arg1->unk_00 & 4) && (sp2C->unk_08 != 0)) {
...
  214          } else {
  215              if (arg1->unk_00 & 0x80) {
...
  217              } else {
  218                  var_s0 = BinArchive_LoadFileFromRomRange(arg0, arg1, sp2C, arg3, arg4);
  219              }
...
  221              if (var_s0 != NULL) {
  222                  if ((PORT_BE32(((u32*)var_s0->magic)[0]) == 'FRAG') && (PORT_BE32(((u32*)var_s0->magic)[1]) ==
  223                      Fragment_RelocateAndCommit(arg0, arg1->unk_02, var_s0);
  224                  }
  225                  sp2C->unk_08 = Port_PtrToHandle(var_s0);
  226              }
  227          }
  228      }
...
  230      return var_s0;
  231  }
...
  323  void PokeIcon_LoadModelArchive(unk_func_80019600* arg0) {
  324      UNUSED s32 pad;
  325      MemoryBlock* sp28;
  326      Fragment* sp24;
...
  328      sp28 = MainPool_InitState(arg0->pool, arg0->size);
  329      sp24 = BinArchive_LoadFileToPool(sp28, D_800ABE10.unk_A04.unk_00, arg0->unk_02, D_800ABE10.unk_A04.unk_08,
...
  331      if (sp24 != NULL) {
  332          arg0->unk_08 = Fragment_ProcessGeoLayout(sp28, sp24, arg0->unk_18);
  333      } else {
... 81 more
```

### src/fragments/pokemon_lab/pokemon_lab.c (8 lines)

```
 2943                          Audio_PlaySoundEffectById(0x1C);
 2944                          var_s6 = 0x10001;
 2945                          var_s1 = 1;
 2946                      }
...
 3045          Audio_FadeOutAll(0x10);
 3046      }
...
 3050          Ui_SendMessageAndPollInput(NULL);
 3051      }
```

### src/fragments/trade_machine/trade_machine_287ED0.c (350 lines)

```
   65  char* Trade_GetSpeciesName(s32 arg0) {
   66      return Text_GetString(NULL, 0, D_82F144C4, arg0 - 1);
   67  }
...
   69  char* Trade_GetMoveName(s32 arg0) {
   70      return Text_GetString(NULL, 0, D_82F144C8, arg0 - 1);
   71  }
...
   73  char* Trade_GetTypeName(s32 arg0) {
   74      return Text_GetString(NULL, 0, D_82F144CC, PokemonType_ToDisplayIndex(arg0));
   75  }
...
   77  void Trade_SetFadeRect(s16 arg0) {
   78      unk_D_82F144B0* ptr = &D_82F144B0;
...
   80      switch (arg0) {
   81          case 0:
   82              ptr->unk_02 = 0;
   83              ptr->unk_04 = 0;
   84              ptr->unk_06 = 0x280;
   85              ptr->unk_08 = 0x1E0;
   86              break;
...
   88          case 1:
   89              ptr->unk_02 = 0;
   90              ptr->unk_04 = 0x154;
   91              ptr->unk_06 = 0x280;
   92              ptr->unk_08 = 0x8C;
   93              break;
   94      }
   95  }
...
   97  void Trade_ResetFade(void) {
   98      D_82F144B0.unk_00 = -1;
   99      Trade_SetFadeRect(0);
  100  }
...
  102  void Trade_DrawFadeRect(void) {
  103      unk_D_82F144B0* ptr = &D_82F144B0;
...
  105      if ((ptr->unk_00 != 0) && (TradeCable_IsSequenceActive() != 0)) {
  106          if (ptr->unk_00 > 0) {
...
  110          gSPDisplayList(gDisplayListHead++, D_8006F4C0);
  111          gDPSetFillColor(gDisplayListHead++, 0x00010001);
  112          gDPFillRectangle(gDisplayListHead++, ptr->unk_02, ptr->unk_04, (ptr->unk_02 + ptr->unk_06) - 1,
  113                           (ptr->unk_04 + ptr->unk_08) - 1);
  114      }
  115  }
...
  117  void Trade_ResetTextPrinter(void) {
  118      D_82F1444C = &D_82F14438;
  119      D_82F14438.unk_00 = 0;
  120      D_82F14438.unk_02 = 0;
  121      D_82F14438.unk_08 = 0;
  122      D_82F14438.unk_0A = 2;
  123      D_82F14438.unk_04 = 0;
  124      D_82F14438.unk_06 = 0;
  125      D_82F14438.unk_0C = 0;
  126      D_82F14438.unk_10 = NULL;
  127  }
...
  129  void Trade_SetTextPrinter(s16 arg0, s16 arg1, s8* arg2, s32 arg3) {
  130      s8* ptr;
...
  132      D_82F1444C->unk_00 = 0;
  133      D_82F1444C->unk_02 = 0;
...
  135      ptr = arg2;
  136      if (ptr != NULL) {
  137          while (*ptr != '\x00') {
  138              D_82F1444C->unk_02++;
  139              ptr++;
... 290 more
```

### src/fragments/trade_machine/trade_machine_289240.c (612 lines)

```
   20  s16 Trade_ReadDeckEntries(unk_D_82F144D0* arg0, s16 arg1) {
   21      UNUSED s32 pad;
   22      DeckHandle* var_v0;
   23      unk_func_82F00838_sp30_00C sp3C;
   24      unk_D_82F144D0_0CA8* sp34 = &arg0->unk_0CA8[arg1];
...
   26      sp34->unk_000 = 0;
   27      if (arg1 == 0) {
   28          var_v0 = Deck_Open(0x20, arg0->unk_000E, 0, 0);
   29      } else {
   30          var_v0 = Deck_Open(0x21, arg0->unk_000E, arg1 - 1, 0);
   31      }
...
   33      if (var_v0 != NULL) {
   34          while (Deck_ReadEntries(&sp3C, 1, var_v0) == 1) {
   35              sp34->unk_00C[sp34->unk_000++] = sp3C;
   36          }
   37          Deck_CloseAndFlush(var_v0);
   38      }
   39      return sp34->unk_000;
   40  }
...
   42  u16* Trade_GetPickBufferSlot(u8* arg0, s16 arg1, s16 arg2) {
   43      return arg0 + (arg1 * 0xFA00) + (arg2 * 0xC80);
   44  }
...
   46  void Trade_InitPickScreen(s32 arg0, s16 arg1) {
   47      MainPoolState* temp_v0_2;
   48      s32 var_s2;
   49      unk_D_82F144D0* temp_s1;
   50      s32 i;
   51      s32 j;
   52      unk_D_82F144D0_0CA8* ptr1;
   53      unk_D_82F144D0_0CA8* ptr2;
   54      unk_func_82F00838_sp30_00C* temp_a3;
...
   56      if ((arg0 < 0) || (arg0 >= 2)) {
...
   60      if (GbSave_GetBoxCount(arg1) == 0) {
...
   64      D_82F20A04 = arg0;
...
   66      main_pool_push_state('PICK');
...
   68      temp_s1 = &D_82F144D0[arg0];
   69      temp_s1->unk_0000 = 1;
   70      temp_s1->unk_0002 = -1;
   71      temp_s1->unk_0004 = 0x140;
   72      temp_s1->unk_0006 = 0xA0;
   73      temp_s1->unk_0008 = 0;
   74      temp_s1->unk_000A = 0;
   75      temp_s1->unk_000C = 0xA;
   76      temp_s1->unk_0010 = 0;
   77      temp_s1->unk_0012 = 0;
   78      temp_s1->unk_0014 = 0;
   79      temp_s1->unk_0016 = 0;
   80      temp_s1->unk_0018 = 0;
   81      temp_s1->unk_001A = 0;
   82      temp_s1->unk_001C = 0;
   83      temp_s1->unk_001E = 0;
   84      temp_s1->unk_0020 = 0;
   85      temp_s1->unk_000E = arg1;
...
   87      var_s2 = 0;
...
   89      for (i = 12; i >= 0; i--) {
   90          if (Trade_ReadDeckEntries(temp_s1, i) > 0) {
   91              temp_s1->unk_0010 = i;
   92              var_s2++;
   93          }
   94          temp_s1->unk_0CA8[i].unk_008 = NULL;
... 552 more
```

### src/fragments/trade_machine/trade_machine_28D1A0.c (333 lines)

```
   14  void Trade_OpenInfoBox(unk_D_82F144D0* arg0, u8* arg1, s16 arg2, s16 arg3) {
   15      unk_D_82F20A10* ptr = &D_82F20A10;
...
   17      main_pool_push_state('BBRD');
...
   19      ptr->unk_00 = 1;
   20      ptr->unk_02 = -1;
   21      ptr->unk_04 = arg2;
   22      ptr->unk_06 = arg3;
   23      ptr->unk_0C = 0;
   24      ptr->unk_0E = 0;
   25      ptr->unk_08 = arg2;
   26      ptr->unk_0A = arg3;
   27      ptr->unk_10 = 6;
   28      ptr->unk_14 = arg1;
   29      ptr->unk_18 = arg0;
   30      ptr->unk_1C = NULL;
   31      ptr->unk_20 = PokeIcon_AllocFramebuffers(1);
   32      ptr->unk_24 = PokeIcon_CreateModelPreview(ptr->unk_20, 0xE4, 0xA0, 0, 0, 0xE0, 0xA0, D_3014468);
   33      ptr->unk_24->unk_00 &= ~2;
   34      PokeIcon_SetPreviewMon(ptr->unk_24, ptr->unk_14, 0x108D);
   35      Audio_PlaySoundEffectById(SFX_PANEL_OPEN);
   36  }
...
   38  void Trade_UpdateInfoBoxOpenIn(unk_D_82F20A10* arg0) {
   39      arg0->unk_10--;
   40      arg0->unk_04 = 0x38 - (((0x38 - arg0->unk_08) * arg0->unk_10) / 6);
   41      arg0->unk_06 = 0x70 - (((0x70 - arg0->unk_0A) * arg0->unk_10) / 6);
   42      arg0->unk_0C = 0x210 - ((arg0->unk_10 * 0x210) / 6);
   43      arg0->unk_0E = 0x148 - ((arg0->unk_10 * 0x148) / 6);
   44      if (arg0->unk_10 <= 0) {
   45          arg0->unk_00 = 2;
   46          arg0->unk_02 = 4;
   47          D_82F20A38 = 0;
   48      }
   49  }
...
   51  void Trade_UpdateInfoBoxChoose(unk_D_82F20A10* arg0) {
   52      static Controller** D_82F13C10[] = {
   53          &gPlayer1Controller,
   54          &gPlayer1Controller,
   55          &gPlayer1Controller,
   56          &gPlayer1Controller,
   57      };
...
   59      s16 tmp;
   60      s32 var_t0;
   61      Controller* temp_a3;
...
   63      arg0->unk_1C = PokeIcon_RenderPreview(arg0->unk_24);
   64      temp_a3 = *D_82F13C10[arg0->unk_18->unk_000E];
   65      var_t0 = 0;
   66      tmp = D_82F20A38;
...
   68      if (temp_a3->buttonPressed & 0x200) {
...
   75      if (temp_a3->buttonPressed & 0x100) {
...
   82      if (D_82F20A38 != tmp) {
...
   87      if (temp_a3->buttonPressed & 0x8000) {
   88          var_t0 = 1;
   89      } else if (temp_a3->buttonPressed & 0x4000) {
...
   94      if (var_t0 != 0) {
   95          arg0->unk_00 = 3;
   96          arg0->unk_02 = -1;
   97          arg0->unk_10 = 6;
   98          arg0->unk_1C = 0;
   99          if (D_82F20A38 == 0) {
  100              arg0->unk_18->unk_0020 = 1;
... 273 more
```

### src/fragments/trade_machine/trade_machine_28ED00.c (652 lines)

```
  117  s32 Trade_DrawBox3DModel(s32 arg0, GraphNode* arg1) {
  118      if (arg0 == 5) {
  119          s32 idx = D_8006F09C->node.callbackArg;
...
  121          gDPPipeSync(gDisplayListHead++);
  122          gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, D_8006F09C->lodFraction);
...
  124          if (D_82F20A88[idx].unk_024 != NULL) {
  125              gSPSegment(gDisplayListHead++, 0x0F, Memmap_GetSegmentVaddr(D_82F20A88[idx].unk_024));
  126              gSPSegment(gDisplayListHead++, 0x0E, Memmap_GetSegmentVaddr(D_82F13F2C[D_82F20A88[idx].unk_002]));
  127              gSPDisplayList(gDisplayListHead++, arg1->callbackArg);
  128          }
  129          GeoRender_ApplyMaterialState();
  130      }
  131      return 0;
  132  }
...
  134  s16 Trade_GetBoxSlotType(unk_D_82F14450* arg0) {
  135      s16 ret;
...
  137      switch (arg0->unk_00) {
  138          case 0:
  139              if (arg0->unk_04 == 0) {
...
  141              } else if (arg0->unk_03 == 0) {
...
  143              } else {
  144                  ret = 0;
  145              }
  146              break;
...
  156          default:
  157              ret = -1;
  158              break;
  159      }
...
  161      return ret;
  162  }
...
  164  void Trade_SetupBoxSlots(s16 arg0) {
  165      s16 temp_v0;
  166      unk_D_82F20A88* var_s0;
  167      s32 i;
...
  169      var_s0 = &D_82F20A88[0];
  170      for (i = 0; i < 4; i++, var_s0++) {
  171          var_s0->unk_000 = 0;
  172          var_s0->unk_002 = -1;
  173          var_s0->unk_004 = 0;
  174          var_s0->unk_024 = NULL;
  175      }
...
  177      D_82F210D0 = 1;
...
  179      for (i = 0; i < 4; i++) {
  180          temp_v0 = Trade_GetBoxSlotType(&D_82F14450[i]);
  181          if (temp_v0 == -1) {
  182              continue;
  183          }
...
  185          if (i != D_82F210D8[0]) {
  186              var_s0 = &D_82F20A88[D_82F210D0++];
  187          } else {
  188              var_s0 = &D_82F20A88[0];
  189          }
...
  191          var_s0->unk_002 = i;
  192          var_s0->unk_004 = temp_v0;
...
  194          switch (var_s0->unk_004) {
  195              case 0:
  196              case 2:
  197              case 3:
  198              case 4:
  199                  switch (D_82F14450[i].unk_01) {
... 592 more
```

### src/fragments/trade_machine/trade_machine_291D60.c (40 lines)

```
  707  s32 Trade_GetSaveSeqState(void) {
  708      return D_82F210E0.unk_00;
  709  }
...
  719  void Trade_ResetSaveSeq(void) {
  720      unk_D_82F210E0* ptr = &D_82F210E0;
  721      s32 i;
...
  723      ptr->unk_00 = 0;
  724      ptr->unk_02 = 0;
  725      ptr->unk_04 = 0;
...
  727      for (i = 0; i < 2; i++) {
  728          D_82F210E0.unk_14[i].unk_00 = D_82F210E0.unk_14[i].unk_02 = 0;
  729          D_82F210E0.unk_14[i].unk_04 = 0xC8;
  730          D_82F210E0.unk_14[i].unk_06 = 0;
  731      }
...
  733      ptr->unk_24.unk_00 = 0;
  734      ptr->unk_24.unk_04 = 0xE8;
  735      ptr->unk_24.unk_06 = 0;
  736      ptr->unk_2C.unk_00 = 0;
  737      ptr->unk_2C.unk_04 = 0x210;
  738      ptr->unk_2C.unk_06 = 0;
  739      ptr->unk_34.unk_00 = 0;
  740      ptr->unk_34.unk_04 = 0xE8;
  741      ptr->unk_34.unk_06 = 0;
...
  743      ptr->unk_24.unk_02 = 0;
  744      ptr->unk_2C.unk_02 = ptr->unk_34.unk_02 = 0;
...
  746      for (i = 0; i < 2; i++) {
  747          D_82F21120[i] = NULL;
  748      }
  749  }
...
  751  void Trade_UpdateSaveSeq(void) {
  752      unk_D_82F210E0* ptr = &D_82F210E0;
...
  754      if (ptr->unk_00 == 0) {
  755          return;
  756      }
...
  793  void Trade_DrawSaveSeq(void) {
...
  797      static s16 D_82F13F8C[] = { 0x32, 0x14A };
...
  799      unk_D_82F210E0* ptr = &D_82F210E0;
  800      s32 i;
...
  802      if (ptr->unk_00 != 0) {
...
  822  }
```

### src/fragments/trade_machine/trade_machine_2942C0.c (754 lines)

```
   22  void TradeCable_DrawScissoredTexRect(s16 arg0, s16 arg1, s16 arg2, s16 arg3, s16 arg4, s16 arg5, s16 arg6, s16
   23      gSPScisTextureRectangle(gDisplayListHead++, arg0 << 2, arg1 << 2, (arg0 + arg2) << 2, (arg1 + arg3) << 2, 
   24                              arg5, arg6, arg7);
   25  }
...
   27  void TradeCable_ResetBalls(void) {
   28      unk_D_82F211A0* var_v1;
   29      s32 i;
...
   31      for (i = 0, var_v1 = &D_82F211A0[0]; i < 2; i++, var_v1++) {
   32          var_v1->unk_00 = 0;
   33      }
   34  }
...
  167  void TradeCable_UpdateBalls(void) {
  168      s32 i;
  169      unk_D_82F211A0* var_s0 = &D_82F211A0[0];
...
  171      for (i = 0; i < 2; i++, var_s0++) {
  172          switch (var_s0->unk_00) {
...
  184          }
  185      }
  186  }
...
  188  void TradeCable_DrawBalls(void) {
  189      s32 i;
  190      s32 j;
  191      s32 sp84 = 0;
  192      s16 var_s0;
  193      u16* temp_s0;
  194      unk_D_82F211A0* var_s4;
  195      s16 v;
  196      s16 tmp;
...
  198      for (i = 0, var_s4 = &D_82F211A0[0]; i < 2; i++, var_s4++) {
  199          if (var_s4->unk_00 != 0) {
...
  246      }
...
  248      if (sp84 != 0) {
...
  251  }
...
  253  void TradeEvo_ResetState(void) {
  254      D_82F21238.unk_02 = 0;
  255  }
...
  547  void TradeEvo_Update(void) {
  548      switch (D_82F21238.unk_02) {
  549          case 0:
  550              break;
...
  575      }
  576  }
...
  749  void TradeEvo_Draw(void) {
  750      unk_D_82F21238* ptr = &D_82F21238;
  751      UNUSED s32 pad;
  752      s32 sp24;
...
  754      if (ptr->unk_02 != 0) {
...
  777  }
...
  779  void TradeCable_ResetLights(void) {
  780      s32 i;
  781      unk_D_82F21160* var_v1;
...
  783      for (i = 0, var_v1 = &D_82F21160[0]; i < 2; i++, var_v1++) {
  784          var_v1->unk_00 = 0;
  785          var_v1->unk_02 = 0;
  786          var_v1->unk_04 = 0;
  787          var_v1->unk_06 = 0;
  788          var_v1->unk_08 = 0;
  789          var_v1->unk_0A = 0;
  790          var_v1->unk_0C = 0;
  791          var_v1->unk_0E = 0;
... 694 more
```

### src/gb_data.c (124 lines)

```
  654  u32 GbData_ReadU24BE(u8* arg0) {
  655      return (arg0[0] << 16) | (arg0[1] << 8) | arg0[2];
  656  }
...
  812  char* Text_CopyString(char* arg0, char* arg1) {
  813      HAL_Strcpy(arg0, arg1);
  814      return arg0;
  815  }
...
  819  char* Text_CopySpeciesName(char* arg0, s32 arg1) {
  820      Text_CopyString(arg0, D_8006FF00[arg1 - 1].name); // PORT: was a raw read at 0x8006FEE8 + 0x1C * id (the n
  821      return arg0;
  822  }
...
  845  void Pokemon_FromGbRecord(BattleMon* arg0, GbPokemonRecord* arg1) {
  846      arg0->unk_00.unk_00 = Species_FromInternalIndex(arg1->unk_00);
  847      arg0->unk_00.unk_01 = arg1->unk_00;
  848      arg0->unk_02 = GbData_ReadU16BE(arg1->unk_01);
...
  850      arg0->unk_04 = arg1->unk_03;
  851      arg0->unk_05 = arg1->unk_04;
  852      arg0->unk_06 = arg1->unk_05;
  853      arg0->unk_07 = arg1->unk_06;
  854      arg0->unk_08 = arg1->unk_07;
...
  856      arg0->unk_0E = GbData_ReadU16BE(arg1->unk_0C);
  857      arg0->unk_10 = GbData_ReadU24BE(arg1->unk_0E);
  858      arg0->unk_14 = GbData_ReadU16BE(arg1->unk_11);
  859      arg0->unk_16 = GbData_ReadU16BE(arg1->unk_13);
  860      arg0->unk_18 = GbData_ReadU16BE(arg1->unk_15);
  861      arg0->unk_1A = GbData_ReadU16BE(arg1->unk_17);
  862      arg0->unk_1C = GbData_ReadU16BE(arg1->unk_19);
  863      arg0->unk_1E = GbData_ReadU16BE(arg1->unk_1B);
...
  865      memcpy(arg0->unk_09, arg1->unk_08, 4);
  866      memcpy(arg0->unk_20, arg1->unk_1D, 4);
  867  }
...
  869  void Pokemon_FromGbRecordExt(BattleMon* arg0, GbPokemonRecordExt* arg1) {
  870      Pokemon_FromGbRecord(arg0, &arg1->unk_00);
  871      arg0->unk_24 = arg1->unk_21;
  872      arg0->unk_26 = GbData_ReadU16BE(arg1->unk_22);
  873      arg0->unk_28 = GbData_ReadU16BE(arg1->unk_24);
  874      arg0->unk_2A = GbData_ReadU16BE(arg1->unk_26);
  875      arg0->unk_2C = GbData_ReadU16BE(arg1->unk_28);
  876      arg0->unk_2E = GbData_ReadU16BE(arg1->unk_2A);
  877  }
...
  917  u16 StatExpSqrt(u16 square) {
  918      u16 i;
...
  920      for (i = 1; i < 255; i++) {
  921          if (SQ(i) >= square) {
  922              break;
  923          }
  924      }
  925      return i;
  926  }
...
  931  u16 GetPokemonDV(s32 statId, u16 dvBits) { //GetPokemonDV
  932      u16 dvValue;
...
  934      switch (statId) {
  935          case STAT_HP: // HP DV (derived from the LSBs of other DVs)
  936              dvValue = ((dvBits & 0x1000) >> 9)  // Special DV bit 0 → HP DV bit 0
  937                      | ((dvBits & 0x0100) >> 6)  // Speed DV bit 0 → HP DV bit 1
  938                      | ((dvBits & 0x0010) >> 3)  // Defense DV bit 0 → HP DV bit 2
  939                      | (dvBits & 0x0001);        // Attack DV bit 0 → HP DV bit 3
  940              break;
...
  942          case STAT_SPECIAL: // Special DV
  943              dvValue = (dvBits >> 12) & 0xF;
... 64 more
```

### src/gb_data.h (6 lines)

```
    9  #define STAT_HP 1
   10  #define STAT_SPECIAL 2
   11  #define STAT_SPEED 3
   12  #define STAT_DEFENSE 4
   13  #define STAT_ATTACK 5
...
   58  #define gBaseStats ((PokemonStats*)((u8*)gSpeciesData - 0x1C))
```

### src/gb_save.c (5 lines)

```
  561                  case 0x10000:
  562                      FRAGMENT_LOAD_AND_CALL(FRAGMENT_TRADE_MACHINE, 0, 0);
  563                      var_s4 = 2;
  564                      game_state = STATE_POKEMON_LAB;
  565                      break;
```

### src/geo_render.c (6 lines)

```
  621          lights->a.l.colc[0] = lights->a.l.col[0] = arg->unk_18.r;
  622          lights->a.l.colc[1] = lights->a.l.col[1] = arg->unk_18.g;
  623          lights->a.l.colc[2] = lights->a.l.col[2] = arg->unk_18.b;
  624      }
...
  791              MtxF_SetRotationAndTransformTranslation(&sp38, &arg->position, &arg->rotation);
  792              MtxF_Multiply(&sp38, &sp38, &D_800AA8C8.unk_0000[D_800AA8C8.unk_10A0]);
```

### src/gfx_rect.c (22 lines)

```
   12          arg0->x1 = x2;
   13          arg0->y1 = x1;
   14      }
...
   20          arg0->y2 = y2;
   21          arg0->x2 = y1;
   22      }
...
  180  void GfxImage_ClearDepthRectangle(Gfx** gfx_p, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
  181      if (D_80068BB0 != NULL && D_80068BB0->depth_p != NULL) {
  182          Gfx* gfx = *gfx_p;
  183          DepthBuffer* depth = D_80068BB0->depth_p;
...
  185          gDPPipeSync(gfx++);
  186          gDPSetRenderMode(gfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
  187          gDPSetCycleType(gfx++, G_CYC_FILL);
  188          gDPSetColorImage(gfx++, depth->fmt, depth->size, depth->width, (uintptr_t)depth->img_p);
...
  190          gDPSetFillColor(gfx++, 0xFFFCFFFC);
  191          gDPFillRectangle(gfx++, arg1, arg2, (arg1 + arg3) - 1, (arg2 + arg4) - 1);
...
  193          gDPPipeSync(gfx++);
  194          gDPSetColorImage(gfx++, D_80068BB0->fmt, D_80068BB0->size, D_80068BB0->width,
  195                           (uintptr_t)D_80068BB0->img_p);
...
  197          *gfx_p = gfx;
  198      }
  199  }
```

### src/matrix.c (161 lines)

```
  364  void MtxF_SetRotationAndTransformTranslation(MtxF* out, Vec3f* translate, Vec3s* rotate) {
  365      f32 sx = SINS(rotate->x);
  366      f32 cx = COSS(rotate->x);
  367      f32 sy = SINS(rotate->y);
  368      f32 cy = COSS(rotate->y);
  369      f32 sz = SINS(rotate->z);
  370      f32 cz = COSS(rotate->z);
...
  372      out->mf[0][0] = (cy * cz) + ((sx * sy) * sz);
  373      out->mf[0][1] = (-cy * sz) + ((sx * sy) * cz);
  374      out->mf[0][2] = cx * sy;
...
  376      out->mf[1][0] = cx * sz;
  377      out->mf[1][1] = cx * cz;
  378      out->mf[1][2] = -sx;
...
  380      out->mf[2][0] = (-sy * cz) + ((sx * cy) * sz);
  381      out->mf[2][1] = (sy * sz) + ((sx * cy) * cz);
  382      out->mf[2][2] = cx * cy;
...
  384      out->mf[3][0] = (out->mf[0][0] * translate->x) + (out->mf[1][0] * translate->y) + (out->mf[2][0] * transla
  385      out->mf[3][1] = (out->mf[0][1] * translate->x) + (out->mf[1][1] * translate->y) + (out->mf[2][1] * transla
  386      out->mf[3][2] = (out->mf[0][2] * translate->x) + (out->mf[1][2] * translate->y) + (out->mf[2][2] * transla
...
  388      out->mf[0][3] = out->mf[1][3] = out->mf[2][3] = 0.0f;
  389      out->mf[3][3] = 1.0f;
  390  }
...
  635  s16 Math_StepToS(s16 current, s16 target, s16 delta) {
  636      s16 temp_v0 = target - current;
...
  638      if (temp_v0 < 0) {
...
  643      } else {
  644          temp_v0 -= delta;
  645          if (temp_v0 < 0) {
  646              temp_v0 = 0;
  647          }
  648      }
...
  650      return target - temp_v0;
  651  }
...
  699  void Color_RGBToHSV(f32 r, f32 g, f32 b, Vec3f* dest) {
  700      f32 temp_ft4;
  701      f32 var_ft5;
  702      f32 var_fv0;
  703      f32 var_fv1;
  704      s32 var_v0;
...
  706      var_v0 = 0x47;
  707      if (g <= r) {
  708          var_fv0 = r;
  709          var_v0 = 0x52;
  710      } else {
  711          var_fv0 = g;
  712      }
...
  714      if (var_fv0 < b) {
...
  719      if (r <= g) {
  720          var_fv1 = r;
  721      } else {
  722          var_fv1 = g;
  723      }
...
  725      if (b < var_fv1) {
  726          var_fv1 = b;
  727      }
...
  729      temp_ft4 = var_fv0 + var_fv1;
  730      dest->z = temp_ft4 - 1.0f;
  731      if (var_fv0 == var_fv1) {
  732          dest->y = 0.0f;
  733          dest->x = 0.0f;
... 101 more
```

### src/memmap.c (2 lines)

```
   37              sVBlobs[i].host = NULL;
   38          }
```

### src/memory.c (6 lines)

```
  187  void MainPool_ResetStateToOffset(struct MainPoolState* state, s32 size) {
  188      if ((s32)state->freeSpace >= size) {
  189          state->listHeadL = (void*)(uintptr_t)size;
  190          state->prev = (void*)((uintptr_t)state->listHeadR + (uintptr_t)size);
  191      }
  192  }
```

### src/model_renderer.c (16 lines)

```
  174  arg1_func_80010CA8* Model_ComputeSizeVariant(arg1_func_80010CA8* arg0, BattleMon* arg1) {
  175      UNUSED s32 pad;
  176      char sp38[0xB];
  177      arg1_func_80010CA8 sp34;
  178      unk_D_8006FF00* sp30;
  179      UNUSED s32 pad2;
  180      u8 var_a0;
  181      s8* var_v1;
...
  183      sp30 = &D_8006FF00[arg1->unk_00.unk_00 - 1];
  184      sp34.raw = 0;
...
  186      Text_CopySpeciesName(sp38, arg1->unk_00.unk_00);
...
  188      if (HAL_Strcmp(arg1->unk_30, sp38) != 0) {
...
  208      if (PORT_GETENV_ONCE("PORT_TRACE_VARIANT")) {
...
  213      *arg0 = sp34;
  214      return arg0;
  215  }
```

### src/poke_icon.c (161 lines)

```
  181  void PokeIcon_AdjustImageColors(u16* arg0, s32 a1, arg1_func_80010CA8 arg2) {
  182      s32 cond;
  183      s32 arg1 = a1;
...
  185      if (!arg0) {}
...
  187      arg1 *= 2;
  188      osInvalDCache(arg0, arg1);
...
  190      cond = arg1 >= 2;
  191      arg1 -= 2;
...
  193      while (cond != 0) {
...
  195          *arg0 = Port_Swap16(Color_AdjustRGB5551(Port_Swap16(*arg0), arg2));        cond = arg1 >= 2;        ar
  196          arg0++;
  197      }
...
  199  }
...
  453  void PokeIcon_LoadModelTexture(u16* arg0, s32 arg1) {
  454      BinArchive* temp_v1;
  455      u8* temp_a1;
...
  457      if ((arg1 > 0) && (arg1 < 0x99)) {
  458          temp_v1 = D_800ABE10.unk_A04.unk_10;
  459          temp_a1 = temp_v1->unk_04 + temp_v1[arg1].raw;
  460          Dma_WriteChunks(arg0, temp_a1, temp_v1[arg1].unk_04 + temp_a1, 0);
  461      }
  462  }
...
  467  void PokeIcon_LoadModelTextureForMon(u16* arg0, s32 arg1, BattleMon* arg2) {
  468      arg1_func_80010CA8 sp1C;
...
  470      if (arg2 == NULL) {
...
  475      Model_ComputeSizeVariant(&sp1C, arg2);
  476      PokeIcon_LoadModelTexture(arg0, arg2->unk_00.unk_00);
  477      PokeIcon_AdjustImageColors(arg0, 0x640, sp1C);
  478  }
...
  481  intptr_t PokeIcon_CapturePreviewGeoNode(s32 arg0, GraphNode* arg1) {
  482      if (arg0 == 0) {
  483          D_800AC830 = arg1;
  484      }
  485      return 0;
  486  }
...
  488  void PokeIcon_InitPreviewScene(unk_func_8001B1FC* arg0, s16 arg1, s16 arg2, s16 arg3, s16 arg4) {
  489      static uintptr_t D_8006F3E0[] = { // PORT64: holds addresses
  490          0x0C00FFFF,    0x05000000, 0x0B00001E, 0x00000000, 0x010000E0, 0x00000000, 0x0000000A, 0x015404E2, 0x0
  491          0x0D000000,    0x05000000, 0x14000000, 0x002D013B, 0xFFFFFF32, 0x14000000, 0xFFD30087, 0x80808032, 0x1
  492          0x0F000001,    0x05000000, 0x1F000000, 0x00000000, 0x00000000, 0xFF060000, 0x00640064, 0x00640000, 0x0
  493          PokeIcon_CapturePreviewGeoNode, 0x00000000, 0x06000000, 0x06000000, 0x06000000, 0x06000000, 0x01000000
  494      };
  495      MemoryBlock* sp24;
  496      MemoryBlock* temp_v0;
...
  498      sp24 = MainPool_AllocState(main_pool_get_available(), 0);
  499      arg0->unk_20 = process_geo_layout(sp24, D_8006F3E0);
  500      arg0->unk_24 = D_800AC830;
  501      MainPool_FinalizeAllocation(sp24);
  502      GeoCamera_SetViewport(arg0->unk_20->children, arg1, arg2, arg3, arg4);
  503      GeoCamera_SetPerspective(arg0->unk_20->children, 30.0f, 100.0f, 12800.0f);
  504  }
...
  507                                   s16 arg6, u8* arg7) {
  508      unk_func_8001B1FC* temp_v0 = main_pool_alloc(sizeof(unk_func_8001B1FC), 0);
...
  510      if (temp_v0 != NULL) {
  511          temp_v0->unk_00 = 6;
  512          temp_v0->unk_1C = arg0;
  513          temp_v0->unk_14 = 0;
  514          temp_v0->unk_04 = arg1;
  515          temp_v0->unk_06 = arg2;
  516          temp_v0->unk_08 = arg5;
... 101 more
```

### src/pokemon_stats.c (132 lines)

```
  171  u16 Pokemon_LevelFromExp(s32 arg0, u32 arg1) {
  172      s32 i;
...
  174      for (i = 1; i < 100; i++) {
  175          if (arg1 < Pokemon_ExpForLevel(arg0, i + 1)) {
  176              break;
  177          }
  178      }
...
  180      return i;
  181  }
...
  335  void Deck_InitializeBoxBackend(DeckHandle* arg0) {
  336      if (arg0->unk_00 == 1) {
...
  341      arg0->unk_05 = Deck_GetEntrySize(arg0->unk_01);
  342      arg0->unk_06 = Deck_GetCapacity(arg0->unk_01);
  343      arg0->unk_08 = Deck_GetCount(arg0->unk_01, arg0->unk_02, arg0->unk_03);
...
  345      if (arg0->unk_00 == 2) {
...
  347      } else {
  348          arg0->unk_0A = 0;
  349      }
  350  }
...
  352  DeckHandle* Deck_Open(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
  353      DeckHandle* sp1C;
...
  355      sp1C = Util_Malloc(sizeof(DeckHandle));
  356      if (sp1C != NULL) {
  357          sp1C->unk_00 = arg3;
  358          sp1C->unk_01 = arg0;
  359          sp1C->unk_02 = arg1;
  360          sp1C->unk_03 = arg2;
  361          sp1C->unk_04 = 0;
...
  363          switch (arg0 & 0xF0) {
...
  368              case 32:
  369                  Deck_InitializeBoxBackend(sp1C);
  370                  break;
  371          }
  372      }
...
  374      return sp1C;
  375  }
...
  402  s32 Deck_CloseAndFlush(DeckHandle* arg0) {
  403      s32 sp1C = 0;
...
  405      if ((arg0->unk_01 & 0x10) && (arg0->unk_04 & 1)) {
...
  410      Util_Free(arg0);
...
  412      return sp1C;
  413  }
...
  426  s32 Deck_ReadEntries(u8* arg0, s32 arg1, DeckHandle* arg2) {
  427      s32 var_s3;
  428      s32 var_s4;
  429      s32 var_v1;
  430      s32 tmp;
...
  432      var_s4 = 0;
  433      var_s3 = arg1 - 1;
  434      tmp = arg1 > 0;
...
  436      if (tmp) {}
...
  438      if (tmp != 0) {
  439          for (; arg2->unk_0A < arg2->unk_08;) {
  440              var_v1 = 0;
  441              switch (arg2->unk_01 & 0xF0) {
...
  446                  case 32:
  447                      var_v1 = Deck_ReadEntry(arg2->unk_01, arg2->unk_02, arg2->unk_03, arg2->unk_0A, arg0);
  448                      break;
  449              }
...
  451              tmp = var_s3 > 0;
... 72 more
```

### src/status_icons.c (7 lines)

```
 3607  u32 PokemonType_ToDisplayIndex(s32 arg0) {
 3608      if (arg0 >= 0x1B) {
...
 3610      } else if (arg0 >= 0x14) {
 3611          arg0 -= 0xB;
 3612      }
 3613      return arg0;
 3614  }
```

### src/ui_graphics.c (92 lines)

```
  238  void Ui_DrawTexturedRect8x8(s32 x1, s32 y1, s32 width, s32 height, u8* texture) {
  239      s32 texture_step_x = (width < 9) ? 0x400 : 0;
  240      s32 texture_step_y = (height < 9) ? 0x400 : 0;
...
  242      gDPLoadTextureBlock(gDisplayListHead++, texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, G_TX_NOMIRROR | G_T
  243                          G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
...
  245      Gfx_DrawTexturedRectClipped(x1, y1, width, height, 0, 0, texture_step_x, texture_step_y, 0);
  246  }
...
  248  void Ui_DrawBorderStyleA(s32 x1, s32 y1, s32 width, s32 height) {
  249      Ui_DrawTexturedRect8x8(x1, y1, 8, 8, D_10027D0);
  250      Ui_DrawTexturedRect8x8(x1, (y1 + height) - 8, 8, 8, D_1002850);
  251      Ui_DrawTexturedRect8x8((x1 + width) - 8, (y1 + height) - 8, 8, 8, D_10028D0);
  252      Ui_DrawTexturedRect8x8((x1 + width) - 8, y1, 8, 8, D_1002950);
...
  254      if (width > 16) {
  255          Ui_DrawTexturedRect8x8(x1 + 8, y1, width - 0x10, 8, D_10025D0);
  256          Ui_DrawTexturedRect8x8(x1 + 8, (y1 + height) - 8, width - 0x10, 8, D_1002650);
  257      }
...
  259      if (height > 16) {
  260          Ui_DrawTexturedRect8x8(x1, y1 + 8, 8, height - 0x10, D_10026D0);
  261          Ui_DrawTexturedRect8x8((x1 + width) - 8, y1 + 8, 8, height - 0x10, D_1002750);
  262      }
  263  }
...
  322                                Color_RGB8* bottom_color) {
  323      Vtx* temp_s2;
  324      DepthBuffer* temp_s1;
  325      Mtx* sp7C;
  326      Vp* sp78;
...
  328      temp_s1 = GfxImage_GetCurrent();
  329      temp_s2 = Gfx_AllocDisplayList(sizeof(Vtx) * 4);
  330      sp7C = Gfx_AllocDisplayList(sizeof(Mtx) * 1);
  331      sp78 = Gfx_AllocDisplayList(sizeof(Vp) * 1);
...
  333      Gfx_SetViewportDimensions(sp78, temp_s1->width, temp_s1->height);
...
  335      gSPViewport(gDisplayListHead++, (uintptr_t)sp78);
...
  337      guOrtho(sp7C, 0.5f, temp_s1->width - 0.5f, temp_s1->height - 0.5f, 0.5f, -2.0f, 2.0f, 1.0f);
  338      Gfx_SetVertexAttributes(&temp_s2[0], x1, y1, -1, 0, 0, top_color->r, top_color->g, top_color->b, 0xFF);
  339      Gfx_SetVertexAttributes(&temp_s2[1], x1 + width, y1, -1, 0, 0, top_color->r, top_color->g, top_color->b, 0
  340      Gfx_SetVertexAttributes(&temp_s2[2], x1, y1 + height, -1, 0, 0, bottom_color->r, bottom_color->g,
  341                              bottom_color->b, 0xFF);
  342      Gfx_SetVertexAttributes(&temp_s2[3], x1 + width, y1 + height, -1, 0, 0, bottom_color->r, bottom_color->g,
  343                              bottom_color->b, 0xFF);
...
  345      gDPPipeSync(gDisplayListHead++);
...
  347      gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
  348      gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER | G_LIGHTING);
  349      gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
  350      gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
  351      gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
...
  353      gSPMatrix(gDisplayListHead++, (uintptr_t)sp7C, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
  354      gSPMatrix(gDisplayListHead++, (uintptr_t)&gIdentityMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
...
  356      gSPVertex(gDisplayListHead++, (uintptr_t)temp_s2, 4, 0);
  357      gSP2Triangles(gDisplayListHead++, 0, 2, 3, 0, 0, 3, 1, 0);
  358  }
...
  376  void Ui_DrawGradientPanel(s32 x1, s32 y1, s32 width, s32 height, Color_RGB8* top_color, Color_RGB8* bottom_col
  377      if ((width >= 0xF) && (height >= 0xF)) {
  378          if (StageContext_IsHighResolution() != 0) {
  379              Ui_DrawGradientRectangle(x1 + 6, y1 + 6, width - 0xC, height - 0xC, top_color, bottom_color);
  380          } else {
...
  383      }
...
  385      gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);
...
  387      if (StageContext_IsHighResolution() != 0) {
  388          Ui_DrawBorderStyleA(x1, y1, width, height);
  389      } else {
... 32 more
```


---
# tower__bad_checksum

Baseline: 1 runs; new: tower__bad_checksum.

Functions: baseline 1149, new runs 1107, **0 only in the new runs**.
Lines: 30 lines only in the new runs.

(none)

### Source lines only the new runs reached

### src/fragments/gb_tower_emulator/gb_tower_emulator_86CB0.c (1 lines)

```
 7577                  gGbApuNoise.unk_48 = gGbApuNoise.unk_0C - gGbApuNoise.unk_40;
```

### src/fragments/transfer_pak_check/transfer_pak_check.c (16 lines)

```
 1372          Gfx_FillRectRgb(4, 4, 0x74, 0x64, 0x64, 0x1E, 0x1E);
 1373          Gfx_FillRectRgb(4, 0x68, 0x74, 0x5C, 0, 0, 0);
...
 1386              PakUi_DrawPartyCardList(0x64);
...
 1450              Font_Printf(0x3E - (Font_MeasureTextExtent(4, 0, "ID  *****") / 2), 0x50, "ID  *****");
 1451          }
...
 1461          Font_SetActive(4, 0);
 1462          Font_SetLineHeight(0x14);
...
 1464          switch (arg2) {
 1465              case 20:
 1466                  Font_Printf(0xE, 0x70, Text_GetString(NULL, 0, D_82D0AB9C, 0x20));
 1467                  break;
...
 1476          }
 1477      }
...
 1486  #define PAKUI_UNKNOWN_NAME "?????"
...
 1502              PakUi_BuildPartyCard(arg0, 1, 0x14, -1, PAKUI_UNKNOWN_NAME);
 1503          }
```

### src/fragments/transfer_pak_select/transfer_pak_select.c (7 lines)

```
  178              var_v1 = 0x14;
  179          }
...
  315          Font_SetActive(8, 0);
  316          Font_Printf((arg1 - (Font_MeasureTextExtent(0, 0, "?????") / 2)) + 0x3C, arg2 + 0xA, "?????");
  317          Font_SetActive(4, 0);
  318          Font_Printf((arg1 - (Font_MeasureTextExtent(0, 0, "ID ?????") / 2)) + 0x3C, arg2 + 0x1E, "ID ?????");
  319      }
```

### src/pokemon_stats.c (6 lines)

```
  706      return sp30;
...
  909              main_pool_try_free(D_800AC910[arg0].unk_50);
  910  #endif
  911          }
...
 1100          D_800AC910[arg0].unk_03 = 3;
 1101      }
```


---
# dex__dex_full

Baseline: 1 runs; new: dex__dex_full.

Functions: baseline 875, new runs 875, **0 only in the new runs**.
Lines: 2 lines only in the new runs.

(none)

### Source lines only the new runs reached

### src/fragments/pokedex/pokedex_214920.c (2 lines)

```
  207          Pokedex_DrawCheckboxIcon(arg0 + 0x52, arg1 + 8, 0);
  208      }
```


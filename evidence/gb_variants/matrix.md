# Game Boy save variants: what each one reaches

45 runs over 6 routes (dex, fb, pc, pcmenu, tower, trade). Whole game instrumented (all game sources).

All runs: 2990 functions; with a cartridge 2960; **1877 only with a cartridge** (in no run without one).

Cartridge-only functions per area: battle_engine 743, particle_math 190, trade_machine 137, widget_tree 108, lab_pc 82, 61 77, gb_tower_emulator 63, lab_pc_icons 46, pokemon_stats.c 45, rental_team_selection 42, pokedex 40, battle_order_select 30, battle_prep 27, poke_icon.c 26, pokemon_lab 22, display_object_textures.c 17, rental_hub 14, save_data.c 14, matrix.c 13, gb_data.c 12, gb_save.c 10, gb_tower.c 10, geo_render.c 9, table_view.c 8, 3D140_2.c 8, model_renderer.c 7, gb_tower_audio.c 5, geo_node.c 4, session.c 4, text_system.c 4, ui_graphics.c 4, gb_tower_team_prep 4, graphics_textures.c 4, transfer_pak_select 3, geo_layout.c 3, pokemon_lab_support 3, model_renderer_callbacks.c 3, model_animation_events.c 3, gb_mbc.c 3, game_state.c 2, lab_ui_support 2, audio_fade.c 2, stage_loader.c 2, hal_libc.c 2, DDC0.c 2, status_icons.c 2, 3D140.c 2, free_battle_rules 2, math_util.c 1, battle_hud.c 1, audio_stored_fade.c 1, transfer_pak_check 1, audio_commands.c 1, audio_loop_point.c 1, scheduler.c 1, macros.h 1, dp_intro.c 1, audio_commands_category2.c 1, cry.c 1, memory.c 1.

## dex

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| bad_checksum | 887 | 58 | 92 | KidsClub_AdvanceMatchBannerClose, KidsClub_AdvanceMatchBannerOpen, KidsClub_DrawMatchBanner, KidsClub_DrawRoundBanner, KidsClub_DrawRoundedFrame, KidsClub_GetMatchBannerState ... |
| base | 948 | - | 207 |  |
| dex_empty | 945 | 0 | 204 |  |
| dex_full | 948 | 0 | 207 |  |
| no_pokedex | 905 | 58 | 110 | KidsClub_AdvanceMatchBannerClose, KidsClub_AdvanceMatchBannerOpen, KidsClub_DrawMatchBanner, KidsClub_DrawRoundBanner, KidsClub_DrawRoundedFrame, KidsClub_GetMatchBannerState ... |
| nocart | 795 | 54 | - | KidsClub_AdvanceMatchBannerClose, KidsClub_AdvanceMatchBannerOpen, KidsClub_DrawMatchBanner, KidsClub_DrawRoundBanner, KidsClub_DrawRoundedFrame, KidsClub_GetMatchBannerState ... |
| not_at_pc | 952 | 4 | 211 | Ui_DrawBorderStyleA, Ui_DrawGradientPanel, Ui_DrawGradientRectangle, Ui_DrawTexturedRect8x8 |

## fb

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| base | 2030 | - | - |  |
| blue | 2029 | 0 | - |  |
| boxes_full | 2031 | 1 | - | GbSave_LoadBoxPokemon |
| fainted | 1942 | 0 | - |  |
| illegal | 1994 | 53 | - | AuxiliaryEffect_Draw, BattleAI_ScaleStatHigh, BattleAnim_AllocOwnerDisplayContext, BattleAnim_CallbackDescriptor25AlphaFadeInHoldOut, BattleAnim_CallbackEndEffects73And74DefaultDescriptorFade, BattleAnim_CallbackInitModelAnimMode2Update ... |
| party6 | 1984 | 0 | - |  |

## pc

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| bad_box_sum | 1123 | 0 | 383 |  |
| bad_checksum | 847 | 16 | 105 | Audio_Disable, Audio_FadeCategoriesAndStreams, Audio_SetEnabled, GbTower_LoadPartyFromDeck, GbTower_TeamPrep, GbTower_TeamPrepFrameCallback ... |
| base | 1123 | - | 383 |  |
| blue | 1123 | 0 | 383 |  |
| boxes_full | 1108 | 8 | 368 | GbSave_LoadBoxPokemon, Text_CountLines, WidgetTree_HandleMessagePanelInput, WidgetTree_OpenMessagePanel, WidgetTree_ResetMessagePanelAnimation, WidgetTree_RunMessagePanel ... |
| boxes_mixed | 1108 | 8 | 368 | GbSave_LoadBoxPokemon, Text_CountLines, WidgetTree_HandleMessagePanelInput, WidgetTree_OpenMessagePanel, WidgetTree_ResetMessagePanelAnimation, WidgetTree_RunMessagePanel ... |
| dex_empty | 1123 | 0 | 383 |  |
| dex_full | 1123 | 0 | 383 |  |
| fainted | 1123 | 0 | 383 |  |
| hof | 1123 | 0 | 383 |  |
| illegal | 1123 | 0 | 383 |  |
| no_pokedex | 899 | 20 | 157 | Audio_Disable, Audio_FadeCategoriesAndStreams, Audio_SetEnabled, GbTower_LoadPartyFromDeck, GbTower_TeamPrep, GbTower_TeamPrepFrameCallback ... |
| nocart | 742 | 2 | - | State_GbTower, func_8130337C |
| not_at_pc | 899 | 20 | 157 | Audio_Disable, Audio_FadeCategoriesAndStreams, Audio_SetEnabled, GbTower_LoadPartyFromDeck, GbTower_TeamPrep, GbTower_TeamPrepFrameCallback ... |
| party1 | 1083 | 6 | 343 | Text_CountLines, WidgetTree_HandleMessagePanelInput, WidgetTree_OpenMessagePanel, WidgetTree_RunMessagePanel, WidgetTree_RunMessagePanelWithSound, WidgetTree_SetMessagePanelText |
| party6 | 1123 | 0 | 383 |  |

## pcmenu

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| bad_box_sum | 893 | 0 | - |  |
| base | 893 | - | - |  |
| boxes_full | 893 | 0 | - |  |
| boxes_mixed | 893 | 0 | - |  |
| dex_full | 893 | 0 | - |  |
| party1 | 893 | 0 | - |  |

## tower

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| bad_checksum | 1180 | 0 | 115 |  |
| base | 1222 | - | 157 |  |
| blue | 1222 | 0 | 157 |  |
| nocart | 1099 | 34 | - | CupSelect_BuildDivisionList, CupSelect_DividerGeoPostCallback, CupSelect_DrawSelectionCorners, CupSelect_HandleInput, CupSelect_IconGeoPostCallback, CupSelect_InitGeoLayouts ... |
| party1 | 1222 | 0 | 157 |  |

## trade

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| one | 848 | 848 | - | Anim_PackedBitsReadSigned, Anim_S16, Anim_U16, AreaSelect_AdvanceMagnemiteTrail, AreaSelect_BuildExitFadeVertices, AreaSelect_BuildMagnemiteQuadVerts ... |
| two_base | 1043 | 1043 | - | Anim_PackedBitsReadSigned, Anim_S16, Anim_U16, AreaSelect_AdvanceMagnemiteTrail, AreaSelect_BuildExitFadeVertices, AreaSelect_BuildMagnemiteQuadVerts ... |
| two_boxes | 1044 | 1044 | - | Anim_PackedBitsReadSigned, Anim_S16, Anim_U16, AreaSelect_AdvanceMagnemiteTrail, AreaSelect_BuildExitFadeVertices, AreaSelect_BuildMagnemiteQuadVerts ... |
| two_party | 1043 | 1043 | - | Anim_PackedBitsReadSigned, Anim_S16, Anim_U16, AreaSelect_AdvanceMagnemiteTrail, AreaSelect_BuildExitFadeVertices, AreaSelect_BuildMagnemiteQuadVerts ... |
| yellow_blue | 1043 | 1043 | - | Anim_PackedBitsReadSigned, Anim_S16, Anim_U16, AreaSelect_AdvanceMagnemiteTrail, AreaSelect_BuildExitFadeVertices, AreaSelect_BuildMagnemiteQuadVerts ... |

## Functions only one variant reaches

| variant | functions | list (first 15) |
|---|---|---|
| bad_box_sum | 0 |  |
| bad_checksum | 0 |  |
| base | 0 |  |
| blue | 0 |  |
| boxes_full | 0 |  |
| boxes_mixed | 0 |  |
| dex_empty | 0 |  |
| dex_full | 0 |  |
| fainted | 0 |  |
| hof | 0 |  |
| illegal | 53 | AuxiliaryEffect_Draw, BattleAI_ScaleStatHigh, BattleAnim_AllocOwnerDisplayContext, BattleAnim_CallbackDescriptor25AlphaFadeInHoldOut, BattleAnim_CallbackEndEffects73And74DefaultDescriptorFade, BattleAnim_CallbackInitModelAnimMode2Update, BattleAnim_CallbackOwnerModelTintPulseDetach, BattleAnim_EffectEndNop, BattleAnim_EffectSecondaryOwnerBonePosition, BattleAnim_EffectTertiaryOwnerModelScale, BattleAnim_EndEffect1RibbonMesh_Draw, BattleAnim_EndEffect1RibbonMesh_Init, BattleAnim_EndEffect1RibbonMesh_Update, BattleAnim_FreeOwnerDisplayContext, BattleAnim_GetOwnerContextParticle ... |
| no_pokedex | 0 |  |
| nocart | 30 | CupSelect_BuildDivisionList, CupSelect_DividerGeoPostCallback, CupSelect_DrawSelectionCorners, CupSelect_HandleInput, CupSelect_IconGeoPostCallback, CupSelect_InitGeoLayouts, CupSelect_Loop, CupSelect_Main, CupSelect_PollController, CupSelect_RenderFrame, Gfx_DrawTiledImageMesh, RuleWizard_StadiumMenu, StadiumSelect_BuildCupIconGrid, StadiumSelect_BuildDivisionCupIcon, StadiumSelect_BuildSimpleCupIcon ... |
| not_at_pc | 0 |  |
| one | 0 |  |
| party1 | 0 |  |
| party6 | 0 |  |
| two_base | 0 |  |
| two_boxes | 0 |  |
| two_party | 0 |  |
| yellow_blue | 0 |  |

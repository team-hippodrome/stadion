# Stadium flash variants: what each one reaches (column "new vs no cartridge" does not apply)

72 runs over 8 routes (cup_giveup, tour_attract, tour_fb, tour_galmenu2, tour_labgb3, tour_play, tour_r2_opts, tour_vp1). Whole game instrumented (all game sources).

All runs: 2733 functions; with a cartridge 2733; **2733 only with a cartridge** (in no run without one).

Cartridge-only functions per area: battle_engine 670, particle_math 164, battle_prep 128, 61 74, rental_team_selection 65, gb_tower_emulator 59, minigame_select 59, geo_render.c 56, gallery_rental_viewer 52, gallery_camera 50, transfer_pak_check 47, pokemon_stats.c 45, demo_scenes 45, save_data.c 45, graphics_textures.c 39, particle_data_library 39, gallery_backgrounds 38, jpeg_stream.c 35, geo_layout.c 34, matrix.c 33, widget_tree 32, battle_order_select 31, geo_node.c 30, stage_loader.c 29, gallery.c 29, free_battle_rules 29, area_select 27, gallery_album 26, gallery_film_select 26, transfer_pak_select 25, widget_toolkit 25, display_object_textures.c 25, rattata_game 24, poke_icon.c 24, 33FE0.c 23, model_animation.c 23, gallery_snap 22, pokemon_lab 22, game_state.c 21, victory_palace 20, memmap.c 19, scheduler.c 19, stadium_select 18, title_screen 18, model_renderer.c 16, ui_graphics.c 16, gb_data.c 15, rental_hub 14, dp_intro.c 14, model_animation_events.c 13, gfx_rect.c 13, memory_main.c 13, gb_tower.c 12, session.c 12, model_renderer_callbacks.c 12, cup_select 10, magikarp_game 10, controller.c 9, memory.c 9, 3D140_2.c 9, flash.c 8, options 8, battle_hud.c 7, rsp.c 7, DDC0.c 7, jpegutils.c 6, text_system.c 6, gb_save.c 5, minigame_shared_graphics 5, n64_logo 5, gfx_buffer.c 5, math_util.c 4, gb_tower_team_prep 4, jpeg_decoder.c 4, crash_screen.c 4, util.c 4, gb_tower_audio.c 4, audio_loop_point.c 3, input.c 3, profiler.c 3, gb_mbc.c 3, audio_commands_category2.c 3, hal_libc.c 3, audio_channel.c 3, audio_data.c 2, audio_fade.c 2, main.c 2, sushi_game 2, vector_math.c 2, dma.c 2, macros.h 2, minigame_dispatch.c 2, reset.c 2, status_icons.c 2, controller_ram.c 2, lab_pc 2, event_battle 1, 1 1, n_env.c 1, dynamo_game 1, audio_sfx.c 1, libgsm.c 1, lab_item.c 1, pokemon_lab_support 1, 3D140.c 1, player.c 1, audio_sound_state.c 1, audio_commands.c 1, sandshrew_game 1, leomseq_tbl.c 1, lab_pc_list 1, table_view.c 1, credits 1, leo_tbl.c 1, gb_audio_render.c 1, metapod_game 1, drowzee_game 1, rental_rules 1, n_drvrNew.c 1, pokedex 1, port_audio.c 1, audio_category_fade.c 1, ekans_game 1, audio_stored_fade.c 1, minigame_actor_framework 1, driveexist.c 1, clefairy_game 1, trade_machine 1, cjcreateleomanager.c 1, cry.c 1.

## cup_giveup

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 981 | 48 | - | Deck_CloseAndFlush, Deck_GetSaveEntryCount, Deck_GetSaveEntryName, Deck_GetSaveEntryTrainerId, Deck_GetSaveRecordCapacity, Deck_GetSaveRecordSize ... |
| base | 1737 | - | - |  |
| boxes | 1737 | 0 | - |  |
| continue | 818 | 28 | - | BattlePrep_RenderBadgeGlowTexture, Flash_ClearStatus, Flash_EraseSector, Flash_EraseSectorByIndex, Flash_ProgramPage, Flash_StartWriteBuffer ... |
| corrupt | 1743 | 6 | - | Save_ClearRecord, Save_ClearTypedRecord, Save_CommitRecord, Save_CommitTypedRecord, Save_ResetAndCommitTypedRecord, Save_WriteFooter |
| new | 1737 | 0 | - |  |
| statues | 1751 | 15 | - | BattleAI_ApplyStatStageModifier, DisplayList_InitScrollingDualPanel, GeoNode_CreateScale, GeoRender_SetMatrix, Geo_NodeScale, MtxF_ApplyScaleTransform ... |
| teams | 966 | 36 | - | Deck_CloseAndFlush, Deck_GetSaveEntryCount, Deck_GetSaveEntryName, Deck_GetSaveEntryTrainerId, Deck_GetSaveRecordCapacity, Deck_GetSaveRecordSize ... |
| unlocked | 1751 | 15 | - | BattleAI_ApplyStatStageModifier, DisplayList_InitScrollingDualPanel, GeoNode_CreateScale, GeoRender_SetMatrix, Geo_NodeScale, MtxF_ApplyScaleTransform ... |

## tour_attract

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 1379 | 0 | - |  |
| base | 1379 | - | - |  |
| boxes | 1379 | 0 | - |  |
| continue | 1379 | 0 | - |  |
| corrupt | 1379 | 0 | - |  |
| new | 1379 | 0 | - |  |
| statues | 1379 | 0 | - |  |
| teams | 1379 | 0 | - |  |
| unlocked | 1379 | 0 | - |  |

## tour_fb

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 751 | 0 | - |  |
| base | 751 | - | - |  |
| boxes | 751 | 0 | - |  |
| continue | 764 | 44 | - | CupSelect_BuildDivisionList, CupSelect_DividerGeoPostCallback, CupSelect_DrawSelectionCorners, CupSelect_HandleInput, CupSelect_IconGeoPostCallback, CupSelect_InitGeoLayouts ... |
| corrupt | 751 | 0 | - |  |
| new | 751 | 0 | - |  |
| statues | 751 | 0 | - |  |
| teams | 751 | 0 | - |  |
| unlocked | 751 | 0 | - |  |

## tour_galmenu2

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 990 | 0 | - |  |
| base | 990 | - | - |  |
| boxes | 990 | 0 | - |  |
| continue | 990 | 0 | - |  |
| corrupt | 990 | 0 | - |  |
| new | 990 | 0 | - |  |
| statues | 990 | 0 | - |  |
| teams | 990 | 0 | - |  |
| unlocked | 990 | 0 | - |  |

## tour_labgb3

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 961 | 0 | - |  |
| base | 961 | - | - |  |
| boxes | 961 | 0 | - |  |
| continue | 911 | 161 | - | Audio_PlayCategory11SoundCommand, BattleMenu_AddOrderSelectPanel, BattleMenu_AppendOrderSlotIcon, BattleMenu_DrawBorderedPanel, BattleMenu_DrawBottomActionButtons, BattleMenu_DrawConfirmButtonLabel ... |
| corrupt | 961 | 0 | - |  |
| new | 961 | 0 | - |  |
| statues | 961 | 0 | - |  |
| teams | 961 | 0 | - |  |
| unlocked | 961 | 0 | - |  |

## tour_play

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 828 | 0 | - |  |
| base | 828 | - | - |  |
| boxes | 828 | 0 | - |  |
| continue | 926 | 207 | - | BinArchive_LoadFileFromRomRange, BinArchive_LoadFileToPool, Color_AdjustRGB5551, Color_HSVInterpolate, Color_HSVToRGB, Color_HSVToRGB5551 ... |
| corrupt | 828 | 0 | - |  |
| new | 828 | 0 | - |  |
| statues | 828 | 0 | - |  |
| teams | 828 | 0 | - |  |
| unlocked | 828 | 0 | - |  |

## tour_r2_opts

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 700 | 0 | - |  |
| base | 700 | - | - |  |
| boxes | 700 | 0 | - |  |
| continue | 700 | 0 | - |  |
| corrupt | 713 | 13 | - | Flash_ClearStatus, Flash_EraseSector, Flash_EraseSectorByIndex, Flash_ProgramPage, Flash_StartWriteBuffer, Save_ClearRecord ... |
| new | 700 | 0 | - |  |
| statues | 700 | 0 | - |  |
| teams | 700 | 0 | - |  |
| unlocked | 700 | 0 | - |  |

## tour_vp1

| variant | functions | new vs base | new vs no cartridge | examples (new vs base) |
|---|---|---|---|---|
| all | 766 | 8 | - | Text_SetNumberToken, Text_SubstituteTokens, Text_UntranscodeFixedName, Ui_DrawBorderStyleC, Ui_DrawBorderedPanelNoFill, Ui_DrawTexturedRect8x8 ... |
| base | 759 | - | - |  |
| boxes | 759 | 0 | - |  |
| continue | 722 | 14 | - | PakUi_DrawDataPanelBorder, PakUi_DrawDataSummaryPanel, PakUi_DrawMenuOptionText, PakUi_DrawRulesPanel, PakUi_UpdateConfirmDisplayFrame, Text_SetNumberToken ... |
| corrupt | 759 | 0 | - |  |
| new | 759 | 0 | - |  |
| statues | 766 | 8 | - | Text_SetNumberToken, Text_SubstituteTokens, Text_UntranscodeFixedName, Ui_DrawBorderStyleC, Ui_DrawBorderedPanelNoFill, Ui_DrawTexturedRect8x8 ... |
| teams | 759 | 0 | - |  |
| unlocked | 759 | 0 | - |  |

## Functions only one variant reaches

| variant | functions | list (first 15) |
|---|---|---|
| all | 0 |  |
| base | 0 |  |
| boxes | 0 |  |
| continue | 8 | BattlePrep_RenderBadgeGlowTexture, PakUi_DrawDataPanelBorder, PakUi_DrawDataSummaryPanel, PakUi_DrawMenuOptionText, PakUi_DrawRulesPanel, PakUi_UpdateConfirmDisplayFrame, Session_LoadContinueData, TransferPak_MainMenu |
| corrupt | 0 |  |
| new | 0 |  |
| statues | 0 |  |
| teams | 0 |  |
| unlocked | 0 |  |

# Findings to send back to the decomp (pokestadium)

Facts learned while porting that are true for the original game too: types, layouts, names, data formats and real
bugs. Each entry names the decomp symbol/file, what the port found, and the evidence. None of these change matching
code; they are type/name/comment improvements (or documentation of original bugs).

## Types: fields that hold pointers but are declared as integers
| Symbol | Declared | Actually holds | Evidence |
|---|---|---|---|
| `DisplayNodeHeader.callbackArg` (global.h) | `s32` | display-list / callback-data pointers | battle_prep carousel callbacks pass it to `gSPDisplayList` |
| `BattleSessionTeams.unk_20` (game_state.h) | `s32` | `gBattleAnimResourceList` (pointer to the resource id list) | `BattleAnim_LoadModeResourceList`, passed on as the list to load |
| `BattleSceneStateTickSlot.unk_00` (battle_engine.h) | `s32` | the `Battler*` passed to `unk_04` | `BattleScene_RegisterStateTickCallback`, `BattleScene_TickStateCallbacks` |
| `PokeIcon_RequestBackgroundLoad` arg1 | `s32` | resource list pointer | caller passes `BattleSessionTeams.unk_20` |
| `PokeIcon_LoadMultipleModels` `temp_s6` | `s32*` | array of table pointers | stores `fragment + 1` (the table) per entry |
| `BattleSessionTeams.unk_04` (game_state.h) | `TeamRoster*` | `BattleMon*` (the Pokémon whose model to load) | all six writers store `&roster->unk_01C[i]`; `Trainer_RequestPokeIcon` reads byte 0x52 via `unk_01C[0].unk_30[6]` |
| `Model_LoadWithScale` return | `s32` | model table pointer | returns `Fragment_ProcessGeoLayout` result |
| particle callbacks `unk_D_8140E728_01C_004_1_04/08` arg 2 | `s32` | pointer (`unk_D_8140E728_01C_00C.arg`) | the stored functions take `u8*` / node pointers |
| `FontContext.unk_4C` (variables.h) | `s32` | address of the loaded glyph source | graphics_textures.c compares/stores `temp_s0->unk_08 + offset` |
| `unk_D_82508B30.unk_034/unk_038` (minigame_select.h) | `s32` | `unk_D_8250A228*` / `unk_D_80068BB0*` | same slots as `unk_D_8250A228.unk_34/38` |

## Types: two names for the same memory
- `unk_func_8001C248` (model_renderer.h) is a view of `BattleSessionTeams`: every caller of
  `PokeIcon_{Request,Poll}BackgroundForObject` / `PokeIcon_WaitFrameLoadForObject` passes `&D_8438E798[i]` or
  `Battler.unk_720`. Its `s16 unk_018, unk_01A` are `BattleSessionTeams.unk_18` (a `ModelNodeView*`); `unk_01D` are the
  load flags (0x80 requested, 0x40 in flight, 0x10 ready). Suggest merging the two types.
- `ret_func_unk_D_800ABE10` (display_object_textures.c), `unk_D_86002F30` (global.h) and `ModelNodeView` used as
  `->unk_00[2]` (`Battle_GetMoveEffectListEntry`) are the same loaded-model table: `u8 count @3`, node list @8.
- `unk_D_80070F84` / `PokemonStats`: `gBaseStats` = `(u8*)D_80070FA0 - 0x1C` (record stride 0x17, 1-based species id);
  `gDexToInternalIndex` = `D_80073230 + 0x37`, `gInternalIndexToDex` = `D_80073230 + 0xCF`.

- `unk_D_82508B30_03C` (minigame_select.h) and `unk_D_86808808_018` (sushi_game.h) are `DisplayObject`s (passed to
  `ModelRenderer_AttachDisplayObject` / `Model_InitDisplayObject`); the containing structs pad them to 0x168.
- `unk_D_8122B1E8` (gb_tower_emulator) is `{OSMesg; OSMesgQueue; u32; OSTask; u8[8]}` = the scheduler's
  `UnkStruct80001380`.
- Lab PC widgets: two node classes share `WidgetLinkHeader` (0x10): drawable `WidgetNode` (0x10/0x14 = position/size
  `WidgetPoint`s, callbacks at 0x18-0x24) and controllers (0x10 input callback, 0x14 set-state callback; vertical/
  horizontal menus, grid menu, child-selection proxy, input proxies). Menu children are controllers, so the
  `WidgetNode*` parameters of `WidgetTree_AppendVerticalMenuItem`/`AppendChildSelectionProxyItem` callers are really
  `WidgetLinkHeader*`. `WidgetGridMenu.unk_0C` is the header's destructor slot (`WidgetGridMenuItem` is unused).
- The scrollable-grid helpers typed `unk_func_8820BE14_02C_038*` / `unk_func_8830867C_02C_034*` only use the 0x70-byte
  core `unk_func_8821421C_038_034`; 0x70+ differs per user (lab_pc callbacks, lab_pc_list owner pointer, pokedex
  `PointerList`s).
- `LabPC_ReleaseSwapWidgets` gets the swap screen (`unk_func_8820BE14_02C`) and flips its tabs `unk_44[2,4,5,3,7]`;
  the decomp typed the argument as `unk_func_8820B12C_02C` (fields 0x4C/0x54/0x58/0x50/0x60).
- `unk_func_88316128` (PC list sort records) is a view of `unk_func_8830867C_02C_0CC_000`: 0x00 pointer, 0x04 s32.

## Offset-comment errors
`tools/layout_check.py` writes build/layout/comment_errors.txt: 84 offset comments that disagree with the layout the
compiler computes for the N64 (32-bit MIPS) target, e.g. `GbSaveBoxEntry.unk_001` (comment 0x0, real 0x1),
`unk_D_82508B30.unk_006` (0x4 / 0x6), `unk_D_82508B30.unk_038` (0x34 / 0x38).

## Data formats (ROM-resident, big-endian)
- Trainer preset files (`TRAINER_DATA` archive): `PresetTrainer` with `u16 unk_34` (flags; high byte = trainer
  class), `u16 unk_36` (team size), then `BattleMon[]` (0x54 bytes, u16/u32 fields big-endian: `unk_10` = EXP).
- Rental rosters (same archive, `RentalRoster`): `u32 count` + `BattleMon[count]`.
- Resource tables in model/animation fragments: 8-byte records `{s8 type; pad; s16; u32 addr}`, type 0 ends the list
  (`arg1_func_80019420`, read by `Fragment_ProcessGeoLayoutList`).
- `stadium_models` archive entries (and model blobs) are FRAGMENTs whose entry point is a small leaf lookup function:
  `switch (arg0)` returning link addresses of geo layouts/tables in the same fragment (e.g. file 3: 0/1/3 → layouts,
  2 → 1, 4 → fog colour). Worth naming/documenting in the yaml.
- Geo command 0x17 (shadow/vertex context): the vertex count spans several consecutive `Vtx` symbols; those symbols
  are one array (e.g. `D_84B103B0` + `D_84B10410`, `D_84B12498`..`D_84B13AD8`). Splitting them hides that.

- Per-species battle records in the `_70D3A0` segment, DMA'd by `func_84302658`: `D_6E910` (0x20/species,
  `SpeciesModelTransform`: Vec3f offset, s16 yaw, s16 pitch, s16 alternate-record index or 0xFF, u16 animation frame;
  copied into both `D_84384578` (portrait camera framing, `BattleScene_ApplyOwnerCameraFraming`) and `D_84384580`
  (move camera)), `D_70110` (0x10: four floats, `unk_08` = model height offset), `D_70B10` (0x20: `SwayLayoutConfig`).
  The battle portraits are a per-side second camera (`PresentationState.unk_168`, geo layers 1/2) rendering a second
  `DisplayObject` of the same model (`PresentationState.unk_000`, rootId 1/2).
- `fragments/31/fragment31_unk_table` (ROM 0x2552E0-0x2554C0, "what is this?") is a jump table: 60 trampolines
  `j target; nop` at 0x81000020.. that ROM data uses as stable entry points into code fragments (geo-layout callbacks
  in stadium models, e.g. 0x810001D0 -> `ParticleGfx_GraphNodeBuildDescriptorMaterialList`, 0x81000030 ->
  `BattleAnim_AllocateEffect125DisplayList`). It should be split as code (`.text`) with named entries.
- Material records read by `ParticleGfx_GraphNodeBuildDescriptorMaterialList` (`arg1_func_81407874_014_000` and
  `_004`) are one 0x20-byte record: texture, tile parameters, tile size, texture animation, combine mode, prim
  colour/animation, env colour/animation, texture generation. The list is `{record* primary; record* secondary}`; the
  arena floor uses it for a detail texture (tile 1 = the same texture, shift 2/3).
- Geo callbacks receive the node; several fragments view it as `{char[0x14]; T* data; Gfx* gfx}` where `data` is
  `GraphNode.callbackArg` and `gfx` is the display-list node's field (`unk_D_86002F34_alt7.unk_18`).
- Model blobs: the entry function returns the model table, which is also the animation bank
  (`ModelAnimationBank`): count of model nodes at +3, transform-animation count at +4, event-track count at +5, node
  list at +8, transform-track and event-track address arrays at +0xC / +0x10. `ModelAssetNode.getAnimationBank` is the
  root node's `unk_D_86002F34.unk_28`.

- `StadiumModel` (33FE0.h): `unkSegment` at 0x138 is a `ModelSegment` (0x20 bytes, `func_80033B2C` copies a whole
  segment there and points `modelSegment` at it), not `u32` + `pad13C[0x1C]`.
- `unk_func_8001B1FC` (poke_icon.h): `unk_0C` is a `u8*` (background image), `unk_14` a `BattleMon*` or species id;
  the preview image returned by `PokeIcon_RenderPreview` is stored in `u8*` fields (rental `unk_D_8423D3A8.unk_20`,
  trade machine `unk_1C` / `unk_20`), declared `s32`.
- `DisplayList_SetFlagInRange/OutOfRange` (particle_data_library): the node view's `unk_04` is `GraphNode.prevSibling`
  (they toggle the previous sibling's render flag for a frame range of an animation), `unk_14` the callback data.
- `D_800777A8` / `D_80077950` and their arrays (33FE0.c) are slot-model templates: s16 tables, `Vtx[10]`, F3DEX2
  display lists; they should be typed as such instead of `u32[]`.

## Original bugs (harmless on N64, real on other platforms)
- `DisplayList_InitAnimatedModelTexture` calls `ModelRenderer_GetActiveMode()` before checking the callback event; at
  installation no model part is current and it reads through a NULL `D_8006F09C` (low RDRAM on the N64).
- `BattleHud_DrawLevelNumber`: `s8 sp58[2]` receives `sprintf("%d", level)` (3–4 bytes).
- `BattleHud_DrawHpFractionText` and two siblings: `s8 sp70[7]` receives `"%3d/%3d"` (8 bytes).
- lab_pc_icons.c: `sp50[4]` receives `"%d/"` (5 bytes for 3 digits).
- battle_engine_361050.c `func_843831A0`: `bzero(D_843C5568, sizeof(D_843C5568) + 2)` clears 2 bytes past the array.
- `gsSPVertex(D_84B12498, 32, 0)` loads 32 vertices from the 31-entry symbol (fine only because the arrays are contiguous).

## Names suggested by behaviour
- `Model_ComputeSizeVariant` (model_renderer.c) computes the *nickname colour variant*, not a size: 0 unless the
  nickname (`BattleMon.unk_30`) differs from the species name; otherwise a value in `D_8006FF00[].unk_10..unk_12`
  chosen by a checksum of `unk_0E` (OT ID), nickname and `unk_3B` (OT name). Suggested name: `Model_ComputeNicknameColorVariant`;
  `unk_10/unk_12` = colour variant min/max.
- `sPokemonNames` holds placeholders ("pmname_NNN") until `Text_LoadNameTable` (called from the N64 logo intro) copies
  the real names in; anything that runs before it compares against placeholders.
- `BattleScene_UpdateIntroSequence` steps (battle_engine_313250.c) are the send-out intro; step 1 waits for
  `BattleSessionTeams.unk_1D & 0xC0` (background asset load) to clear.
- `BattleMenu_PicksExceedLevelCap`: `ruleCategory` 3 = Poké Cup (2 picks < 106, 3 < 156 levels), 4 = Petit Cup
  (< 56 / < 81), 5 = Pika Cup (< 36 / < 51).
- GB Tower (gb_tower_emulator_7F9A0.c): `unk_D_8122B2C0.unk_5A2C[]` is the save RAM page state (0xFF clean, 0 written,
  1 being copied to the cartridge, 2 written back) and `unk_582C[]` the per-page write-back delay (0xFE = page not
  tracked; below 0x500 for Pokémon carts, see the loop after the work-area allocation). `unk_5DA0`/`unk_5D80` are the
  cartridge probe block (32 bytes at save RAM offset `unk_5DA0`, re-read to detect a pulled cartridge).
  `unk_5DC7` is the request to the pak service thread (1 read ROM, 2/3 read/write RAM, 4 power off, 5 write one page
  and verify, 6 load the game's ROM banks, 7 status, 8 probe), `unk_5DC9` its connection state.
- `GbCpu_InitOpcodeTables(emu, image)` does not build opcode tables: it renders the game's SGB border (tiles at context
  +0xDDF0, map +0xFDF0, palettes +0x105F0) into the tower frame image `D_8122B2C8[0]` (palette entries 64-127, then
  256x224 indices), which `unk_58` then shows. `unk_5404` = "SGB border received". `D_8122B2C8[1..2]` are Stadium's own
  frames (Yay0 at ROM 0xF4920 Game Boy device, 0xF5450 / 0xFDE40 Stadium borders, by presentation mode).
- The emulator's CHR_TRN/PCT_TRN handlers (asm func_8120D164) take the border from `unk_5C58` (= `D_8122C748`, the UI
  asset from ROM 0x102BA0: 0x800 bytes of 2bpp tiles, then 0x900 bytes map/palettes = Pokémon Yellow's border) when
  `unk_5DC5 == 4` (Yellow) and the cart runs in GBC mode (`unk_5485 == 0`): a workaround for Yellow's broken border
  transfer on a GBC.
- `unk_5485` = 0 for Game Boy Color carts (header 0x143 bit 7), selecting the colour microcode.
- `D_81228654`/`D_81228678` (gb_tower_emulator_86CB0.c) are the tower menu's PCM streams: start addresses (N64 pointers
  into `D_8120EB94`...`D_81226964`) and byte sizes; `func_81209374` mixes them.

## Data formats: Game Boy saves (for tools, not the decomp)
- Gen-1 main data mirrors WRAM from wPokedexOwned at save offset 0x25A3; a continued game reuses the saved map header
  (script pointer included) and tileset state, so a save needs more than map id and coordinates to be continued.
- area_select.c `AreaSelect_AdvanceSparkleTrail`: shifts `unk_0C[j + 1]` for j = 4..0 in a 4-element array; points 4 and
  5 overwrite the next trail's header and, for the last trail, 12 bytes past `gAreaSelectSparkleTrails`.
- stadium_select.c: `D_84102510` and `D_8410251C` are entries 10 and 11 of `D_84102498` (12 sparkle records; the
  loops run to 12). rental_team_selection_2DB900.c: `D_84210DD8` is entries 4 and 5 of `D_84210DA0` (the Registered
  Pokémon / Rental Only icons).
- victory_palace.c `VictoryPalace_UpdateLeftPillarGlow`: `sp30` receives an f32 from `Vec3f_CalculateDistanceAngles`
  (declared s16 to match the stack); gym_leader_castle.c: `TrainerData.gfx_file_index` is a big-endian u16 read
  directly from ROM data; pokemon_stats.c `GbSave_WriteBoxBank` verifies a 0x1A60-byte box bank in the smaller
  `D_800ACA78` main-data buffer (an overrun on the N64 too).
- Fragment entry points declared void whose callers use the result (left in v0 by their last call): Gallery_SnapMain,
  Gallery_AlbumMain, Gallery_FilmSelectMain, MagikarpGame_Main, ClefairyGame_Main, RattataGame_Main,
  Minigame_JigglypuffDoodle, DrowzeeGame_Main, DynamoGame_Main, SushiGame_Main.
- Geo layouts with raw link addresses instead of symbols: gallery_camera D_86908ABC (D_8690A610/A630/A648/A660 and the
  three GalleryCamera_*Callback functions), pokedex D_888265DC (D_888269C0..D_88826A20), sushi_game D_86807078
  (D_86806FD0, SushiGame_NameTagGeoCallback).
- Gallery background sets (archive D_8690A698) are code fragments: entry `s32 f(s32 index)` returning the geo layouts
  (0, 1, 3), the fill colour (2) and the fog record (4).

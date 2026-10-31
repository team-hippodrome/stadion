# Gallery, Victory Palace, Kids Club and the battle particle offsets

Routes written from each screen's input code (`runs.sh <out> <gb_variants dir>` replays them on
`build-evidence-all2`, game at origin/main c946919; `mkrec4.py` writes the recordings: held buttons, combinations,
the stick and pads 2-4, which `tools/evidence/mkrec.py` does not do). States are entered through the game's loop
(`--set "State_TitleScreen#1@exit:gCurrentGameState:4=<state>"` after A at 1720); the `*_boot.rec` routes start
from boot for the ares comparison.

| area | before | after | left |
|---|---|---|---|
| victory_palace | 35 | 43 / 44 | `func_82600C00` (empty, referenced by nothing) |
| gallery_camera | 55 | 71 / 72 | `GalleryCamera_ResetFreeView` (referenced by nothing) |
| gallery_rental_viewer | 54 | 67 / 67 | |
| gallery_album | 26 | 30 / 30 | |
| gallery_snap | 24 | 27 / 27 | (`Gallery_SnapUpdateSaveConfirm` only with the forced Snap Station flag) |
| gallery_backgrounds | 51 | 55 / 56 | `Gallery_DrawRoundedFrame` (referenced by nothing) |
| snap_print_club_print_seals | 2 | 4 / 4 | |
| minigame_select | 69 | 70 / 72 | dead, see below |
| particle_math | 466 | 467 / 524 | battle-effect callers, see below |
| minigame_actor_framework | 64 | 64 / 78 | dead, see below |

## Routes

| run | save / setting | input | reaches |
|---|---|---|---|
| `vp_gift`, `vp_overlays` | `stadium_save statue ... set all`, Nos. 1 and 3 again with `--ot "<PLAYER>" --trainer-id 0` (a win with rentals); state 36. The first entry with all 151 gives the Hall of Fame gift (Psyduck) and leaves; `vp_overlays` starts from the flash written after it | A on the "151 represented" grid, A (info), A, START, START (grid), A, B, START, B, A, R, A on No. 2, LEFT, L, B | `VictoryPalace_ShowSpeciesInfo` / `HideOverlays` / `ShowSpeciesGrid` / `HideSpeciesGrid`, `DrawFrameBorder`, `IsCurrentSpeciesOwnedByPlayer`, `StringsEqual` (the "RENTAL" frame), `LoadCurrentCarouselIcon` |
| `gal_camera` | Game Paks `base.sav` (pad 1: party Pikachu with the player's OT) and `boxes_full.sav` (pad 2), `gControllerBits |= 3`; state 17 | Snap photo; Pokemon select: RIGHT LEFT DOWN RIGHT LEFT UP RIGHT DOWN UP RIGHT, A on 2P; box picker R R L, DOWN x7, UP UP, RIGHT LEFT, B; LEFT, A on 1P, A on Pikachu; camera: stick forward, C-right/C-left/C-up/C-down held, D-pad DOWN 250 frames, RIGHT, UP, LEFT, stick back, L (background menu) UP DOWN DOWN UP A, Z x3, A every 30 frames | the slot cursor moves, every box-picker move, next/prev box, close panel; `Gallery_LoadSurfingPikachuIcon` (a Yellow Pikachu whose OT/ID match the cartridge: species 0x99); the orbit, pan and height clamps; `Gallery_SwitchBackgroundSet` (UP on set 0 jumps to the last set); the help balloon; 24 photos: `Gallery_CameraExitState_CountDown` / `SpinOut` / `ExpandBox` / `FadeOut` |
| `gal_album` | as `gal_camera`; leaves with "Save and quit", so its `port.flash` holds an album (5 photos) | the camera route, film: A on six photos, DOWN A (Go to Album), B, RIGHT A (Go to Gallery) A; Move photo: A RIGHT A (swap), A DOWN DOWN A (trash); B; Pick photo: A; on the grid RIGHT A A (paste), DOWN A A RIGHT A B ... B A | `Gallery_SwapPhotoRecords`, `Gallery_AlbumAdvanceDeleteAnimation`, `Gallery_ClearPhotoRecord`, `Gallery_AlbumComputeEnlargeMotion`, `func_83501718` (a picked photo over the trash), `Gallery_AlbumConfirmAndExit`, `Gallery_SnapUpdatePasteConfirm`, `Gallery_SnapResetToMenu` |
| `gal_tutorial` | state 17 | Snap photo, DOWN RIGHT A (How to snap) | `func_86900A14` (the tutorial's scripted pad) |
| `gal_busy`, `gal_busy_boot` | a Game Pak with a bad checksum | A on its slot, A, B | `Gallery_RentalViewerUpdateBusyConfirm`, `Gallery_DrawYesNoOptions`, `Gallery_RentalViewerCancelSelection` |
| `gal_reselect` | state 17 | Rental Pokemon only, A, three photos, B (camera), B on the Pokemon select | `Gallery_RentalViewerUpdateLoadConfirm`, `Gallery_DrawSaveOptions` |
| `gal_print` | `gal_album`'s flash + `stadium_save unlock --mewtwo --round2` (round 2 `cleared_modes` 0x1F8) | UP UP to Print, hold L+R, A | the Bonus stickers screen (`Snap_PrintClub*` background select); confirming returns "only available at a Pokemon Snap Station" |
| `gal_ss_print`, `gal_ss_bonus` | **forced**: `Gallery_SnapUpdateMenuIconStates:D_8330343C:4=1` (the Snap Station accessory, `Controller_CheckAccessoryEeprom`) | Print, A, A / the Bonus route | `Gallery_SnapUpdateSaveConfirm`, `Gallery_SaveUnlockedBackgrounds` |
| `seals` | `gal_album`'s flash; state 64 (`STATE_FAST_N64_LOGO`, the Snap Station boot) | none | `Snap_PrintClubPrintSeals` in photo mode: `Snap_PrintClubDrawSealMargin`, `Snap_PrintClubWaitMs` |
| `kc_grid_cancel` | state 35 | Pick a game, A (1P), DOWN A (Ready), A (COM level), RIGHT, B | `KidsClub_AdvanceRoundBannerClose` |
| `particle_species.sh` | `gb_party.py`: the species leads a Game Pak Free Battle, `--moves` with PP 1 (each move once in turn); the move windows are the batch4 sweep runs that reached each helper | | `Particle_Field38_AddXZFromField94YOffsetMinus90_0901C` through the species checks for Onix (0x5F: moves 133-136, 141-144, 145-148), Dewgong (0x57), Vaporeon (0x86), Vileplume (0x2D). Dragonair (0x94, moves 9-12 and 121-124) and Onix with 21-24 reached nothing new |

## Port differences found (checked in ares on the same input)

- Pokemon select, a Game Pak that cannot be read: the prompt box was empty; ares shows "Save file not found.".
  `Gallery_GetCommonUiString` / `Gallery_CopyCommonUiString` had no `return`. Fixed in PR #72.
- The Bonus stickers screen (`gal_bonus_boot.rec`): ares draws all 16 background thumbnails on white, the port 5 on
  black. The game renders each thumbnail into a CPU image (`GfxImage_SetRenderTarget`) and draws that image back as
  480 one-line texture loads; the renderer does not return the rendered pixels to those loads. Not fixed (renderer).
- Not a difference: the album opened from the film screen is empty in both (marked photos are placed when the film
  screen is left through "Go to Gallery").

## Left, and why

- Dead (referenced by nothing): `func_82600C00`, `GalleryCamera_ResetFreeView`, `Gallery_DrawRoundedFrame`,
  `MiniGfx_DrawVertexColoredQuad`, `miniShowDifficultyText`, `minigameDebuggVectorEditor`,
  `MiniFx_ConfigureOneShotEmitter`, `MiniFx_ConfigureEmitterWithPeriodAndRepeat`, `minigame_sample2_viewer` (no
  dispatch case), `gallery_debugger`.
- Dead (reachable only from dead code): `MiniFx_ConfigureEmitter`, `MiniFx_AllocEmitter`; the emitter list stays
  empty, so `MiniFx_TryEmit`, `MiniFx_EmitParticles`, `MiniFx_ApplyEmitterToParticle`, `MiniFx_FreeEmitter` never run.
- Dead (the code never selects it): `miniTutoScreenShow` and `miniDrawResultIcons` (tutorial case 5: no minigame
  sets `miniTutoScreenState` to 5; `D_87903DD0 == 0x18` is set only by `miniTutoScreenShow`);
  `KidsClub_DrawWavyText` (`KidsClub_UpdateMatchBannerIdle` resets its timer to 300 before testing it for <= 0, so
  the wave amplitude stays 0); `MiniGameSelect_UpdateCursor` (callback arg 2 runs only for active nodes; the cursor
  object is created with its active flag cleared and nothing sets it); `showDebuggCameraInfo` /
  `showDebuggJoystickInfo` in Ekans / Metapod / Sandshrew (`miniDebugMode` is toggled only inside the debug ranking
  test, which can only be left with it off).
- particle_math: the other live leftovers' callers are battle animation callbacks that did not run (descriptor 26
  burst child, asset A9 quad, the `Unused*` setups, descriptor 19 case 2), `BattleAnim_SpawnEffectParticles` with
  `unk_00 == 1` and the dead MiniFx emitter (`Particle_AllocChain`), or table entries with no static caller; see
  `../../battle_anim_slots.md` for the slots no move script names.

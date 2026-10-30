# Menus and the Trade Machine: trade evolution, cancelled trades, Options, Pokédex area map

Each route is a full recording from boot (`run.py --rec R --natural --frames N`, N from the table: coverage is
written every 600 frames, so N leaves a dump after the last press). Game Boy cartridge `yellow_de.gb` on both
ports; saves are not in the repository, the recipes are below (`$S` = the evidence scratchpad with `gbv4/`,
`gbv_sl/` and `flash4/`). The trade routes start from `evidence/gb_variants/routes/lab_trade.rec` (Lab with a
Game Pak, Trade Machine, both Game Paks chosen, "Are these OK?" Yes at 8062); every pick screen is driven by
controller 1, the 2P cartridge is on controller 2 (`--gb2`).

| route | frames | GB saves (`--gb`, `--gb2`) / flash / state | what it does | reaches |
|---|---|---|---|---|
| `trade_evolution.rec` | 16600 | `evo1.sav`, `evo2.sav` | MACHOKE (1P) for KADABRA (2P, OT GARY): A on the evolution preview of each side, both evolve, the caption, "The trade was successful", Save and continue, a second trade | `TradeEvo_UpdateFlashIn`, `TradeEvo_UpdateApplyEvolution`, `TradeEvo_UpdateShowCaption`, `TradeEvo_DrawEvolutionCaption`, `Trade_ApplyEvolution`, `Trade_NicknameIsSpeciesDefault`, `Trade_LoadPickBufferIcon` |
| `trade_box_pages.rec` | 14600 | `gbv4/boxes_mixed.sav`, `evo2.sav` | R, R, L on the 1P pick screen (box 1 full, box 2 one Pokémon), a box Pokémon for KADABRA | `Trade_UpdatePickScreenPageSwipe` and the evolution above |
| `trade_cancel.rec` | 9600 | `evo1.sav`, `evo2.sav` | "Are these OK?": DOWN, A (No), the machine reopens; A on 2P, B (No); B, B closes the machine | `Trade_UpdateBoxMachineFlowConfirmDone`, `Trade_UpdateBoxSlotFadeOut` |
| `trade_cart_error.rec` | 9000 | `evo1.sav`, `gbv4/not_at_pc.sav` | A on the 2P slot of a cartridge saved outside a Pokémon Center: its message frame, A, B, B | `Trade_UpdateBoxMachineFlowFrameMove`, `Trade_UpdateBoxMachineFlowConfirmWait`, `Trade_UpdateBoxMachineFlowFrameReturn` |
| `options.rec` | 3800 | flash `flash4/unlocked.flash`, `--set "State_TitleScreen#1@exit:gCurrentGameState:4=19"` | SOUND (mono), VOICE (off), DELETE SAVED DATA: B; Yes then No; Yes, Yes (erase, "complete", A); both toggles again; QUIT (writes the options record) | all of `options` (14/14): `Options_ApplySelection`, `Options_ConfirmPrompt`, `Options_EraseSaveData`, `Options_ShowEraseCompleteScreen`, `Options_DrawEraseCompleteMessage`, `Options_ApplySoundSettings` |
| `pokedex_area.rec` | 9800 | `gbv4/dex_full.sav` | Pokédex: #016 PIDGEY (wild areas in Yellow; the party's starters are "AREA UNKNOWN"), AREA: UP/DOWN through the areas, R (area list) and back, A (zoom), B | all of `pokedex` (68/68): `Pokedex_FindPrevMapArea`, `Pokedex_ResetCameraEase`, `Pokedex_DrawMapPinIcon`, `Pokedex_GetCountTier` |
| `labpc_check_move_page.rec` | 9000 | `gbv_sl/boxes_full.sav`, flash `flash4/labfull.flash` | Lab PC BOXes, Check BOX, A on SQUIRTLE, A: the move detail page, DOWN/UP through the moves, B, START, B out | `LabPC_ShowMoveDetailPage`, `LabPC_DrawMoveDetailWidget`, `LabPC_DrawArrowLabelA` |
| `area_select_mewtwo.rec` | 3000 | flash `flash4/unlocked.flash`, `--set "State_TitleScreen#1@exit:gCurrentGameState:4=16"` | area select: UP to VS Mewtwo, A | `AreaSelect_UpdateMewtwoExit` |

Save recipes (`tools/evidence/gb_party.py`, base `$S/gbv4/base.sav`):

```
python3 -I tools/evidence/gb_party.py $S/gbv4/base.sav evo1.sav "MACHOKE:30;PIKACHU:25;GRAVELER:30"
python3 -I tools/evidence/gb_party.py $S/gbv4/base.sav evo2.sav \
  "KADABRA:30,ot=GARY,otid=54321;HAUNTER:30,nick=GEIST,ot=GARY,otid=54321;GRAVELER:30;MACHOKE:30,nick=MUSKEL"
```

Input patterns: the Trade Machine reads controller 1 for both pick screens, the evolution preview and the save menu,
so A every 120 frames after the second "Yes" walks the whole trade (cable, both previews, save, the next trade). The
species that evolve on a trade are the dex numbers 64, 67, 75 and 93 (`Trade_SpeciesEvolvesOnTransfer`); a
nickname that is not the species name is kept (`Trade_NicknameIsSpeciesDefault`). The Options screen polls every
frame: presses 50-60 frames apart are enough, the prompt opens in 7 frames. The Pokédex area map needs a species
with wild encounters in the cartridge's game (`D_88826A54` stays 1 for "AREA UNKNOWN", and then the map has no
areas to step through and no pin).

Left in these areas and why:
- `Trade_UpdateBoxSlotShrink`: the slots of the cartridges not chosen shrink away; needs three or four Game Paks
  (`run.py` has two ports with a cartridge).
- `Trade_DrawSaveSeqMessageBackdrop` (and `GbSave_PollCartRemoval`'s cases, `LabUI_ShowSavingStatusWidget`,
  `LabUI_SetSavingStatusMessage`, `LabUI_HideSavingStatusWidget`): shown only when a Game Pak write fails and the
  game asks for the cartridge to be taken out and put back; the port's Transfer Pak never fails a write.
- `TradeCable_GetSequenceState`, `func_82F0ECA4`: nothing references them (dead).
- `LabPC_GetDetailPanelMode`: `LabPC_IconListDispatch` command 3; the Lab PC issues commands 0, 1, 2 and 4 only
  (dead). `func_88600240`: empty, no reference.
- Area select `func_822014C0`, title screen `func_82100020`, free battle rules `func_84001AD8`: empty, no reference.

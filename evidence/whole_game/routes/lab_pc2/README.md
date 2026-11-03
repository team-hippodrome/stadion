# Lab PC, second round: Game Pak item box, saving, prize Pokémon

Each route is a full recording (run it with `--frames` = its `# end` line; coverage is only written every 600 frames,
so the end leaves at least one dump after the last press). Game Boy cartridge `yellow_de.gb`; saves are not in the
repository, the recipes are below (`$S` = the evidence scratchpad with `gbv_sl/` and `flash4/labfull.flash`).

| route | GB save | Stadium flash | what it does | reaches (lab_pc) |
|---|---|---|---|---|
| `items_gp_new_box.rec` | `gbv_sl/items.sav` | `full4.flash` | Items: UP to the right box icon, A, DOWN to "N64 Game Pak", A; the "no space" message (all four item boxes belong to other trainers), A; box picker: A on GARY, A (check items), B, DOWN to the bin, A, LEFT (Yes), A: the box is deleted and becomes MICHI's; Move: one MASTER BALL into it; exit, Save and quit | `LabItem_RunBoxScreenModal`, `LabItem_BoxScreen_HandleInput`, `func_88210380` (box picker menu), `LabItem_LoadBoxScreenSlotItems`, `LabItem_GamePakLabelWidget_SetSlot`, `LabItem_DrawGamePakLabel`, `LabItem_DrawGamePakInfoCard`, `LabItem_RunConfirmDialog`, `LabItem_ConfirmDialog_HandleInput`, `LabItem_SetConfirmDialogText`, `LabItem_CommitChanges`, `LabItem_WriteDeckBack` (new box: `Deck_OpenAndSetName` path) |
| `items_gp_merge.rec` | `gbv_sl/items.sav` | `dup.flash` | Items: right box = MICHI's Game Pak box (exists, no picker); R makes the right side active; Re-order: HP UP x3 onto HP UP x4; exit, Save and quit | `LabItem_CombineStackQuantities`, `LabItem_RemoveGridEntryAt`, `LabItem_CommitChanges`, `LabItem_WriteDeckBack` (existing box) |
| `boxes_ck_moves_save.rec` | `gbv_sl/boxes_full.sav` | `labfull.flash` | BOXes: Check BOX on GB BOX 1, A on SQUIRTLE, START (re-order moves), C-Up, C-Right (TACKLE and TAIL WHIP swap), B out, Return to Menu, Save and quit | `LabPC_BoxHasUnsavedChanges`, `LabPC_SaveModifiedBoxes`, `LabPC_WriteBoxEntries`, `LabPC_MarkBoxSeen` |
| `boxes_sides.rec` | `gbv_sl/boxes_full.sav` | `labfull.flash` | BOXes: R, L on the icon column | `LabPC_BoxPairSelector_SwapSides`, `LabPC_BoxMenu_ClearButtonHighlights` |
| `poke_reorder_release.rec` | `gbv_sl/boxes_full.sav` | `labfull.flash` | Pokémon: RIGHT, DOWN (Re-order), A, R/L while idle, PIKACHU onto BULBASAUR; B, DOWN (Release, the hand), A on CHARMANDER, UP/LEFT to Yes, A | `LabPC_SwapMonsInBox`, `LabPC_SwapMenu_IsIdle`, `LabPC_SwapMenu_SwapSides`, `LabPC_ReleaseSwapWidgets`, `LabPC_RemoveMonFromBox` |
| `prize_pokemon.rec` | `gbv_sl/items.sav` | `prize.flash` | the start of the Items route to the Lab menu, whose cursor now opens on the prize ball: A, Oak's lines, the prize screen (PROF. OAK box with BULBASAUR L5), A, A into the party, exit, Save and quit | `LabPC_ExitToLabMain`, `LabPC_PrizePokemonThread`, `func_88206110` (builds the prize mon), `LabPC_FindDirtyDeck` |

Flash recipes (`python3 -m stadium_save` from `tools/save_editor`):

```
cp $S/flash4/labfull.flash full4.flash
for i in 2 3 4; do python3 -m stadium_save items full4.flash add $i POTION:3 ANTIDOTE:2 --name ASH$i --trainer-id 1000$i; done
python3 -m stadium_save items full4.flash clear 1
python3 -m stadium_save items full4.flash add 1 "MASTER BALL:1" --name GARY --trainer-id 22222

python3 -m stadium_save items $S/flash4/labfull.flash -o dup.flash clear 1
python3 -m stadium_save items dup.flash add 1 "HP UP:3" "HP UP:4" TM01:2 --name MICHI --trainer-id 12345

python3 -m stadium_save set $S/flash4/labfull.flash -o prize.flash options.bonus_pokemon=1
```

ares (`ARES_REF_FLASH_OUT` set): `items_gp_new_box` matches the port at 7100, 7330, 7880, 8100, 8700, 9000, and both
item routes leave a FlashRAM byte-identical to the port's (box 1 MICHI: MASTER BALL x1; HP UP x7, TM01 x2).

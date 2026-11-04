# Gym Leader Castle, VS Mewtwo and Battle Now routes

Every route enters its state through the game's own loop from the title screen (`mkrec` "1720 A", then
`--set "State_TitleScreen#1@exit:gCurrentGameState:4=<state>"`: 39 `STATE_GYM_LEADER_CASTLE`, 34 `STATE_VS_MEWTWO`,
40 `STATE_BATTLE_NOW_1P`) and is otherwise played with real input: every battle is won or lost by the moves the
recording picks, and nothing in memory is forced. Run with
`tools/evidence/run.py <out> --bin build-evidence-all2/stadion --frames N --rec <route> --natural --flash F --gb yellow_de.gb:SAVE --set "State_TitleScreen#1@exit:gCurrentGameState:4=<state>"`.

## Saves (not in the repository)

Stadium flash: `teams` (one registered team), `unlocked` (`stadium_save unlock`: all eight gyms cleared, Mewtwo open),
`continue` (a saved Poke Cup run, for the overwrite prompt). Game Boy saves from the `party6` base
(`tools/gb_save.py ... --got-pokedex`) with a party of six written by `tools/evidence/gb_party.py`, all with
`moves=94/85/58/53,dv=0xFFFF` (Psychic, Thunderbolt, Ice Beam, Flamethrower; max DVs):

| save | party |
|---|---|
| `op6` | MEWTWO, MEW, DRAGONITE, ALAKAZAM, GENGAR, ZAPDOS, all L55 |
| `opR` | ZAPDOS, GENGAR, MEW, MEWTWO, DRAGONITE, ALAKAZAM, all L55 (the order select's A, C-Up, C-Right picks land on Mewtwo, Mew, Alakazam) |
| `op100b` | as `op6`, L100, with `moves=63/89/85/58` (Hyper Beam, Earthquake, Thunderbolt, Ice Beam) |
| `party6` | the registration routes' cartridge (for the rental routes, which do not use it) |

Stadium recomputes the stats from level and DVs (a `stats=` override has no effect), so these are strong but
legal Pokemon, not 999s. Psychic at L55 takes most Castle trainers in one hit.

## Input patterns

- Entry from the cartridge ("1P", top-left of "Select entry Pokemon"): the cartridge grid is 3 x 2 and the cursor
  moves on by itself after a pick, so six plain picks (A for the details, A on "Yes"), then START and A on "OK".
- Battles: A every 40 frames and a C-button 20 frames after it (C-Up, C-Right, C-Up, C-Down in turn). A opens the
  moves, C-Up/Right/Down/Left are moves 1/2/3/4; after a faint, with three chosen, C-Up/C-Right/C-Down pick the
  slot (with six chosen: C-Up, C-Left, B, C-Right, C-Down, A). B is left out of the battle mash: it opens the
  switch menu and a fainted slot loops on "There's no will to fight!".
- The order select for VS Mewtwo wants all six (B, C-Left, C-Up, A, C-Down, C-Right) and takes input only once its
  text has run; a cycle of all six buttons every 30 frames covers both.
- Battle prep's prompts are vertical (D-Down moves, A picks). For a non-default option the battle mash stops just
  before the frame the prompt is first drawn (from a run with the mash), then Down/A.

## Routes

| route | flash / GB save / frames | reaches |
|---|---|---|
| `castle_rules.rec` | teams / op6 / 6500 | Castle hub, Rules: each rule's detail (A, A, Down), out with B |
| `mewtwo_rules.rec` | unlocked / op6 / 5500 | the same for VS Mewtwo |
| `castle_gym_clear.rec` | teams / op6 / 64000 | Pewter Gym from the cartridge: four real wins (the clear at frame 59656) with "Continue" (A) between rounds, the gym clear (cup cleared, round record, Victory Palace records, `BattlePrep_ShowCastleClearOutro`), back to the Castle map |
| `castle_round_save_quit.rec` | teams / op6 / 20000 | first round won, "Keep battling?": "Save and Quit" (no saved run: saves and goes to the title) |
| `castle_round_overwrite_save.rec` | continue / op6 / 20000 | the same with a saved Poke Cup run: the overwrite prompt (`BattlePrep_DrawStreakGiveUpPrompt`), "Save" |
| `castle_round_continue_no_save.rec` | continue / op6 / 26000 | the overwrite prompt, "Continue w/o saving": the next round |
| `castle_loss_reenter.rec` | teams / party6 / 90000 | rentals (`../registration/castle_rental_entry.rec`, then order select and the battle mash): three Pewter trainers won, lost to Brock; the loss prompt (`BattlePrep_DrawCastleRuleSummary`, `BattlePrep_DrawGiveUpPrompt`) "Re-enter" |
| `castle_loss_restart_gym.rec` | teams / party6 / 90000 | the same, "Re-start at this GYM" (back to the entry screen) |
| `castle_loss_quit.rec` | teams / party6 / 90000 | the same, "Quit" (area select) |
| `castle_e4_rival_clear.rec` | unlocked / opR / 150000 | the map to the Elite Four (Right, Up x4, Right, Down, Right), four Elite Four wins (frame ~109000), `Glc_AdvanceRoom` opening the rival's room, the rival battle won (~123800), then `FRAGMENT_GYM_LEADER_CASTLE_CLEAR` (124136) and demo scenes 0x11 (124336) and 0x15 (125064) |
| `mewtwo_win.rec` | unlocked / op100b / 30000 | VS Mewtwo with six L100: Mewtwo beaten (`BattlePrep_ConfirmMewtwoRules` win side, 17054), the credits (`State_RunCredits`, 17630; `Credits_OutroLoop`, 28046) |
| `mewtwo_loss_battle_again.rec` | unlocked / op6 / 24000 | six L55 lose; the loss prompt (`BattlePrep_DrawQuitPrompt`, 20280) "Battle again" (the entry screen) |
| `mewtwo_loss_quit.rec` | unlocked / op6 / 24000 | the same, "Quit" (area select) |
| `battle_now.rec` | teams / party6 / 22000 | Battle Now (1P) with its preset teams, the battle to its end and `BattlePrep_ConfirmSimpleRules` (18822) |

Frames are the recording's (the evidence runtime's VI tick); `--shots` frames run behind them while the game loads.

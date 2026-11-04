# Rental team grid (fragment 61 `Rental_*`) and the registered-team screens around it

The grid opens from the team tray's confirmation prompt, "Reselect some Pokémon" (`Rental_HandleTeamConfirmation`
case 3, or `Rental_HandleRegisteredTeamConfirmation` case 2 while registering), and from the level-cap warning.
The cup routes start from the stored cup recording's menu path (Stadium → Poké Cup, `cuploss0.rec` up to the tray);
the Castle routes from the state shortcut. All runs: `--bin build-evidence-all2/stadion --natural`.

| route | run options | reaches |
|---|---|---|
| `cup_grid.rec` | `--frames 7700 --flash replays/cup_win_badge.flash` | six rentals, "Reselect some": grid cursor up/down/left/right (`Rental_TeamCursor*`, `Rental_TeamTraySlideTick`), A on a slot (`Rental_OpenSlotAssignmentDialog`, "Exchange? No" → `Rental_ResolveSlotPick`), B (`Rental_ConfirmTeam`) back to the prompt, OK |
| `cup_grid_exchange.rec` | `--frames 8200 --flash replays/cup_win_badge.flash` | "Exchange? Yes": the carousel resumes and the slot gets a new rental |
| `cup_level_cap.rec` | `--frames 7600 --flash replays/cup_win_badge.flash --gb yellow_de.gb:L55.sav` | six L55 Game Pak Pokémon, grid, B: "combined levels of the three lowest … too high" (`Rental_HandleLevelCapWarning`), A (back to the grid) and B (replace the last slot) |
| `cup_register_full.rec` | `--frames 7400 --flash CUP10.flash` | rentals, "OK to Register" with 10 Poké Cup teams: "File full -- registration failed" (`Rental_HandleTeamCancel`) |
| `cup_back_to_registered.rec` | `--frames 5600 --flash CUP10X6.flash` | Rental Only, B out of the carousel with no picks, then Registered Pokémon: the tray update runs idle (`Rental_TeamTrayHide`); list scroll down/up (`..._GetNext`), "Check": Pokémon cursor (`..._MovePokemonRight/Up`, `TeamSelection_Preview_StartCompact`) |
| `castle_register_grid.rec` | `--frames 9200 --flash teams.flash --gb yellow_de.gb:party6.sav --set "State_TitleScreen#1@exit:gCurrentGameState:4=39"` | Castle registration of six, "Reselect some": grid with the registering tray, slot dialog, B: registered-team prompt again, "Reselect all" |
| `castle_delete_team.rec` | `--frames 6200 --flash CASTLE9.flash`, gb and set as above | Delete registered Pokémon, first option: `..._EditTeam`, `RegisteredTeam_DeleteAndCompact`, "The transformed DITTO changed back to normal" |
| `../registration/reg6.rec` | `--frames 9620 --flash CASTLE9.flash`, gb and set as above | registering the 10th team: `Rental_TeamTrayResume` (freezes without PR #71) |
| `../registration/reg6.rec` | `--frames 9620 --flash CASTLE10.flash`, gb and set as above | Register with a full file: `TeamSelection_WaitForConfirm` |

Saves (not in the repository): `L55.sav` = `tools/gb_save.py base.sav --got-pokedex --rom yellow_de_us_title.gb
--party PIKACHU:25`, then `tools/evidence/gb_party.py base.sav L55.sav "NIDOKING:55;LAPRAS:55;SNORLAX:55;TAUROS:55;STARMIE:55;ALAKAZAM:55"`.
`CUP10.flash` / `CUP10X6.flash`: `replays/cup_win_badge.flash` with `python3 -m stadium_save team F set poke_cup <0..9>
SNORLAX:50 PIKACHU:50 LAPRAS:50 [TAUROS:50 STARMIE:50 ALAKAZAM:50]`. `CASTLE9/10.flash`: the `teams` flash (one Castle
team) with `gym_leader_castle` slots 1..8 (1..9) set the same way.


The exchange route returns from the slot dialog to the carousel through
`Rental_ResolveSlotPick -> Rental_CarouselResumeInput`, then selects the replacement.
Its final input is at frame7800 (released at7808); capture after it. Unlike
`cup_grid.rec`, this route does not press B from the team grid, so
`Rental_ConfirmTeam` is not an expected function. Validate the exchange-specific
call edge and at least seven `Rental_CarouselConfirmMon` calls (six initial picks
plus a replacement).

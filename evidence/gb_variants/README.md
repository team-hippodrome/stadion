# Game Boy save variants: the code only cartridge contents reach

Whole game instrumented (`cmake ... -DCMAKE_PROJECT_INCLUDE=cmake/evidence.cmake -DPORT_EVID_FILES=game/src/`,
351 sources), 45 runs: 14 save variants and two cartridges over six routes, plus runs without a cartridge.
ROMs and saves are the user's own and not in the repository; the variants are generated
(`tools/evidence/gb_variants.py <base.sav> <dir>`, base from `tools/gb_save.py --got-pokedex`).

| route | what it does |
|---|---|
| `routes/lab_pc.rec` (= replays/tour_labgb3.rec) | Lab → Game Pak → PC → Pokémon: move one to a GB box, open a data page |
| `routes/lab_pcmenu.rec` | Lab PC → Items, BOXes, See List |
| `routes/lab_dex.rec` | Lab → Pokédex (LEFT): list, data page, cry, area, scroll |
| `routes/lab_trade.rec` | Lab → Trade Machine (RIGHT), a second cartridge on controller 2 (`run.py --gb2`) |
| replays/gbtower_quit.rec (+ .flash) | GB Tower, load the cartridge, menu, quit |
| ../battle_engine/routes/free_battle_gamepak.rec | Free Battle with Game Pak teams |

Files: `matrix.md` (per route and variant: functions, new over the base save, new over no cartridge, functions
only one variant reaches), `lines.md` (the source lines each variant adds over the base save on the same route).

## Results

- 45 runs reach **2990 functions** (40% of 7474); **1877 of them only with a cartridge** (in no run without one):
  battle engine 743, particle math 190, Trade Machine 137, widget tree 108, Lab PC 82, rental carousel (fragment 61)
  77, GB Tower 63, Lab PC icons 46, pokemon_stats.c 45, rental team selection 42, Pokédex 40, ...
- What each variant adds over the base save (same route):

| variant | adds | what the code is |
|---|---|---|
| two cartridges (Trade Machine) | 194 functions, 3631 lines | the whole trade flow (one cartridge: "You need to have two Game Paks") |
| illegal party (Free Battle) | 53 functions, 1107 lines | moves the species cannot learn (animations, effects), stats 999 (`BattleAI_ScaleStatHigh`) |
| saved outside a Pokémon Center | 20 functions, 417 lines | the Lab's refusal, then the route continues elsewhere (see the caveat) |
| boxes full | 8 functions, 105 lines | `GbSave_LoadBoxPokemon`, moving into a full box: error sound and message (lab_pc_1AE680.c 1001-1004) |
| one Pokémon in the party | 6 functions, 81 lines | moving the last Pokémon away refused (lab_pc_1AE680.c 1044-1053) |
| bad checksum (GB Tower) | 30 lines | the corrupt-save card ("ID *****") in transfer_pak_check.c 1372-1476 |
| Pokémon Blue instead of Yellow | 10-13 lines | game id 3 branches: `POKEMON_ROM_BLUE` (pokemon_stats.c 638), transfer_pak_check / transfer_pak_select / rental case 3; no partner Pikachu |
| Pokédex full / empty | 0-2 lines | same code, other data (the list is data-driven) |
| Hall of Fame entry | nothing | these routes do not read it (it needs the Hall of Fame route) |

- **Two port crashes found** (both on screens only a cartridge opens, both also in the build where that code is not
  instrumented, so not caused by the instrumentation):
  1. Lab PC → Items: segfault in `LabItem_DrawItemRow` (called from `LabItem_DrawItemGridCell`, widget tree draw), a
     garbage pointer (`0x…2865b565b5`), every variant, frame 6248 of `routes/lab_pcmenu.rec`.
  2. Pokédex → AREA: signal 5 (a libc check trap) in `Pokedex_DrawAreaMapFrame` (from `Pokedex_EnterAreaMapScreen`),
     frame 9340 of `routes/lab_dex.rec`, with every save that reaches the Pokédex.
  The earlier ares tours covered the Pokédex list and data page and the Lab PC's Pokémon pages, not these two.
- Caveat: a refusing variant (no Pokédex flag, saved outside a Pokémon Center, bad checksum) leaves the rest of the
  route's presses to act on other screens; their "new" functions include where those presses went (GB Tower, Kids
  Club). The refusal code itself is in `lines.md`. Routes per refusal would separate them.

## Next variants

Hall of Fame (needs its route), the Pikachu reward (Yellow conditions), Stadium-side saves (registered teams, cup
progress) combined with Game Pak teams, Red, Japanese cartridges (Stadium's own region check), more PC actions
(release, items move), and the ares side for each (with the Game Boy core, `build-ares-gb`).

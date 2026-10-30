# Whole game: how far the levers reach, what is left and why

**Current (origin/main 8309990 merged: the Lab PC Items / See List and Pokédex AREA fixes, PR #51):** 155 runs
(the same levers; the Lab PC menu and Pokédex routes now run to their end) reach **5146 of 7487 functions
(68.7%)**, lines 62.2%, branches 54.5% (before the fixes: 65.7%, 58.8%, 53.2%). Gained: Lab PC list 0 -> 101 of 281,
Lab PC 66 -> 153 of 269, Pokédex 40 -> 62 of 68, widget tree +20. Left: 1324 functions behind 837 gates, 1017 with no
reached caller above them, 578 of those referenced by nothing (`dead_code.md`). `frontier.md`, `gate_trials.md` and
`dead_code.md` are this run's; the tables below are the first whole-game round.

Two more port crashes on paths these runs reach (not fixed here):
1. Victory Palace with every statue (flash: `stadium_save statue set all`, `replays/tour_vp1.rec`, frame 3506):
   segfault in `Completion_InitScene` (from `Stage_ShowCompletionScene`, `Stage_DispatchDemoScene`).
2. Free Battle after a Game Pak battle ends (`../battle_engine/routes/free_battle_gamepak.rec`, the Metronome
   party, seed 7, frame 29642): segfault in `BattlePrep_LoadBadgeTextures` (from `BattlePrep_InitBadgeCarouselScene`,
   the rules screen after the battle).

Build: every game source instrumented (`cmake ... -DCMAKE_PROJECT_INCLUDE=cmake/evidence.cmake
-DPORT_EVID_FILES=game/src/`, 351 sources), function entry hooks plus clang source coverage. Call graph:
`tools/evidence/callgraph.py <build> cg.json --files "game/src/|/generated/wrap/"` (7608 functions, 102 pointer
tables). 217 runs, all reproducible (recorded input, generated saves, forced values); no ROMs or saves in the
repository.

| step | runs | functions reached (of 7483) |
|---|---|---|
| the 29 stored recordings (`replays/*.rec`, their own flash and cartridge) | 29 | 3465 (46.3%) |
| + Game Boy routes (Lab PC, Pokédex, Trade Machine with two cartridges, Free Battle with Game Pak teams), 3 seeded battles, the 165-move sweep | +51 | 4805 (64.2%) |
| + Stadium flash variants over 8 recordings (blank, corrupt records, unlocked, statues, registered teams, Lab boxes and items, a saved cup run, combined) | +72 | 4840 |
| + forced game states (direct calls of `State_*` the retail flow never selects) | +6 | 4870 (65.1%) |
| + automatic memory proposals replayed on a run where their caller ran: 11 of 32 opened their gate | +32 | **4919 (65.7%)**; lines 58.8%, branches 53.2% |

Files: `frontier.md` (per area, gates by class with their conditions), `dead_code.md` (what nothing references),
`gate_trials.md`, `flash_variants.md`, `routes/mash.rec` (a generic input for forced states). The Game Boy save
variants are in `../gb_variants/`, the battle engine in `../battle_engine/`.

## What is left (2564 functions)

- **581 referenced by nothing** (`dead_code.md`): no caller or address-taking in the C code of the whole decomp and
  no ROM word with their address: particle callbacks (179), battle animation callbacks (173), mini-game actor and
  demo leftovers, matrix/vector helpers, ... Dead in the shipped game unless asm reaches them; evidence only by a
  direct call (`run.py --call`), and their records must say so. 18 more are referenced only by raw addresses in data.
- **712 below other unreached code** (whole fragments never entered): debug fragments (debug menu slots, gallery
  debugger, mini-game sample viewers, ranking debug test), the 64DD library (`leo*.c`, RTC: only with a 64DD drive),
  credits and Gym Leader Castle clear (beating the game), event battles, the fatal error screen.
- **1253 behind 666 gates**, by class (`frontier.md`): menu or flow steps (193 gates, 533 functions: a route that
  picks the entry; the largest are Lab PC → Items (186 functions, blocked by the port crash there) and Boxes (118)),
  "other state" (215 / 312), session/save/mode (30 / 212), table indices (94 / 99: more moves and species on both
  sides), results of calls such as save confirmations (30 / 96: routes that confirm and write), hardware (3 / 59:
  64DD), input (27 / 32), callbacks (27 / 35).

## Levers and what each reached here

| lever | how | reached here |
|---|---|---|
| recorded routes | `run.py --rec R --natural [--flash F] [--gb ROM:SAV]` | 46% from 29 recordings |
| Game Boy cartridges | generated saves (`gb_party.py`, `gb_variants.py`), `--gb`, `--gb2` | Lab, Pokédex, Trade Machine, GB Tower, Game Pak battles: 1877 functions only with a cartridge |
| Stadium flash | `tools/save_editor` (`stadium_save unlock / team / box / statue / continue`), `--flash` | the saved cup run's Continue and Transfer Pak flow (+207 on one tour), statues, corrupt-record repair |
| forced randomness / moves | `--queue`, `--moves`, the move sweep | the battle engine to 83% (see `../battle_engine/`) |
| forced states | `--set "Game_DoCopyProtection#2:gCurrentGameState:4=0x80" --call "Game_DoCopyProtection#2:State_StubbedDebug:v:"` | the stubbed debug state (`DebugMenu_Exit`), kiosk Kids Club, fast logo, fast battle |
| memory proposals | `frontier.py --build`, `try_gates.py` (replays each on a run where its caller ran; 15-minute limit) | 11 of 32: AI decision, gallery camera, help balloons, rental confirm, registered teams, a 64DD probe |
| direct calls | `--call` | dead code with return values (`BattleAnim_Atan2sFromPoints` ...) |

Limits seen: forcing can produce states the game cannot handle (the Game Boy music flag on every audio frame
stalls the game: stopped by the time limit; `State_N64ddBootUnused` and `State_EventBattle` crash when called
without their setup); a refused or diverted route sends the rest of its input elsewhere; input gates need input,
not memory writes.

## Next

1. Routes per menu entry for the big menu gates (Lab PC Boxes, save confirmations, Trade save sequence, Gallery
   snap save), and for Items/AREA once the two port crashes are fixed (separate task).
2. Beating content with saved runs: `stadium_save continue` at the last battle plus forced battle wins (memory
   set on the HP) for credits, Castle clear, event battles.
3. Forced fragment entries for the debug fragments (a direct call of the entry with its fragment loaded).
4. The 64DD: ares emulates it; evidence for `libleo` would come from ares with a disk image, not from the port.
5. Direct calls for the 581 unreferenced functions, typed from their prototypes, with the verdict "dead in the
   shipped game".
6. The ares side for every lever that is real game input (routes, saves, cartridges), with the Game Boy core.

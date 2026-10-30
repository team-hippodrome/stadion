# Whole game: how far the levers reach, what is left and why

**Current: 6537 of 7490 functions (87.3%), lines 77.8%, branches 68.9%.** Battle prep 165 of 205: a real cup
win from the stored Poke Cup recording (`routes/cup_win_tail.rec`, replayed as recorded; it wins the first battle)
with the round set to the last (`--set "BattlePrep_RunRulesScreen#2:gSessionContext+0x3:1=8"`, `roundOrDemoId`):
the cup-clear bookkeeping (Victory Palace records, cup cleared, first-clear bonus, new round record, the Pikachu
check) and the post-cup showcase with the real six-Pokemon roster (all six models, with `ruleCategory` 1: the
free-battle crash before was the forced roster). A loss (the winner flags swapped, `+0x1220`/`+0x1270`) with three
continues (`+0x1297`) and each option of the continue prompt (`routes/cup_loss_from_start.rec`: "From the start"
uses a continue token). Left in battle prep: the PAL and two-on-two ("Double") variants (dead in the US NTSC game),
the Gym Leader Castle and Mewtwo rule screens (their states stop at team selection without a registered team).

**Previous round: 6529 of 7490 functions (87.2%), lines 77.7%, branches 68.8%.** Battle engine 1868 of 2055: battles with
time limits (`--set "BattlePrep_RunRulesScreen#1@exit:gSessionContext+0x1291:1=1;...+0x1292:1=<minutes>;...+0x1293:1=10"`
on the Game Pak free battle: `timeLimitsEnabled`, `battleLimitMinutes`, `moveLimitSeconds` as the event battle's
settings panel sets them) reach the battle and turn timers, the time-out caption and the judge's decision
(`BattleScene_DetermineWinner`, `Battle_ClassifyHpTier`). The event battle's two-player fight needs controller 2
connected (`--set "Cont_InitControllers:gControllerBits:1|=3"`: libultraship's control deck reports only controller
1) and both pads in the recording (order select: three picks each, then A on "All ready"). Most of the battle
engine that is left are animation setups and callbacks reached only through effect-table slots: every one of the
165 moves' animations ran (the move sweep), so these are slots no move script uses (code the shipped data cannot
run), plus empty scene states and stubs; `BattleHud_DrawForfeitConfirmDialog` is referenced by nothing. Still open
with a reachable gate: the AI's 0x400000 trainer-flag path (`BattleAI_DecideAction`), the six-Pokemon team status
list (a side with four or more chosen), the send-out-after-faint scene phase (case 34).

**Previous round: 6521 of 7490 functions (87.1%), lines 77.5%, branches 68.7%.** New: the **demo
scenes** 170 of 177 (was 74). The scenes the Stadium menu state plays after a cup (`game_state.c`: scene 17 on
`transitionFlags` 0x2, 22/23 on 0x8/0x10 for the first clear that raises the GB Tower tier, 21/26 the completion
scene) entered at the point the game enters them: `Stage_RunDemoScene(id)` when battle prep's post-battle screen
returns, after the Game Pak free battle (`run.py --rec routes/pikachu_reward_3moves.rec --gb ... --call
"BattlePrep_RunRulesScreen#2@exit:Stage_RunDemoScene:i:i<id>"`); the stored cup-win recording no longer replays (the
battles have changed since it was recorded). The Mewtwo intro (24) through `STATE_VS_MEWTWO` from the title screen
(`--set "State_TitleScreen#1@exit:gCurrentGameState:4=34"`, unlocked flash). Scene 17's model showcase walks a cup
roster of six (`ruleCategory` 1, 2, 4, 5 or 7, set with `--set "...@exit:gSessionContext+0x1:1=1"`); with the free
battle's three it shows three and then reads past them (a forced-condition artefact, not a port bug: in the game
the showcase only follows a cup). Left: the completion gift dialog's decline and "already have one" branches,
unused math helpers and stubs.

The evidence runtime now also dumps when the game crashes (it hooks the crash signals at its first periodic dump
and hands them on to the port's crash report afterwards): before, a crash lost everything since the last dump,
often the code that crashed.

**Previous round (origin/main ec92ad5 merged: the Lab PC list fixes, PRs #68 and #69): 6415 of 7490 functions (85.6%),
lines 76.4%, branches 67.6%** (previous: 83.5%, 74.1%, 65.8%). New: the **Lab PC list** 271 of 282 (was 129), driven
screen by screen from its input code with screenshots (`run.py --shots`) and checked against the N64 in ares:
- Routes `routes/lab_list/*.rec`, each from the list screen with both box lists (Lab with a Game Pak, the
  `boxes_full` Game Boy save, a Stadium save with Lab boxes 1-3 filled and item box 1 for the cartridge's trainer:
  `stadium_save box ... add 1 PIKACHU:30 ...`, `stadium_save items ... add 1 TM01:2 TM24 TM13 198 "PP UP:5" "HP UP:3"
  PROTEIN:3 IRON:3 CARBOS:3 CALCIUM:3 --name MICHI --trainer-id 12345`): the type filter (one type, a pair, cancel),
  the move filter, the level sorts and level-range dialog, the stat sorts, the Pokemon header's menu (sort, Group by
  CUP, item list, Select Pokemon to be listed, cancel selections), the row menu (group by species / type, Move to a
  64 box, Check), leaving with and without saving, and item use (`*_item.rec`: vitamins, an ineligible vitamin,
  PP Up, a TM over a known move, TM24, an HM the Pokemon cannot learn, backing out of each step).
- Four port bugs found on the way: the TM/HM item list counted 16-byte N64 entries (crash when the list opens with
  any TM, HM, vitamin or PP Up in the item box), empty box-grid cells drawn from a NULL entry (crash on "64 BOXes"),
  the column header's cells declared as 8 bytes of padding (one pointer on the host: every column one cell off, the
  type filter unreachable; ares opens "Group by TYPE" on the same input), all fixed in PR #68; and using an item
  (a vitamin went down the TM path because `func_88305F28` read the item entry through an N64-offset view), fixed
  in PR #69. Left in the list (11): the TM into a free move slot (`LabPCList_ClearMovesFrom`), the level-range
  filter's apply step, widget state setters and empty `func_` stubs.

**Previous round (origin/main 9f9343c merged: the See List fix, PR #64, and the Pikachu reward and event battle fixes,
PR #67): 6255 of 7490 functions (83.5%), lines 74.1%, branches 65.8%** (previous round 79.1%, 68.1%, 59.4%).
Four batches, each compacted into one profile (`tools/evidence/compact_runs.py`; `frontier.py` and `covdiff.py` read the compacted `merged.profdata`): the
baseline levers below again (142 runs and the forked direct calls), the 28 stored recordings extended with menu
mashing plus an idle attract run, each also with the game's sound engine on (61 runs), and the routes and forced
conditions of this round (21 runs) and their reruns after the two crash fixes (4 runs). Binary: the instrumented build of the merged sources (the baseline batch ran
on the build before the merge; the profiles of the few functions the merge changed count only from the later
batches). Gate trials (`gate_trials.md`) were not repeated this round.

New in this round. The gated areas were worked out from their code (input handling, state machines, what each
prompt waits for) and driven with routes written for them (`tools/evidence/mkrec.py`: an input recording from
timed presses), instead of mashing:
- **Event battle** 56/57 (was 1), its rental-rules screen 52/56 (was 1): `STATE_EVENT_BATTLE` is entered through
  the game's own state loop (`run.py --set "State_TitleScreen#1@exit:gCurrentGameState:4=18"`: the next state is
  changed when the title screen returns, a new `@exit` form of `--set` and `--call`), with two cartridges of six
  Pokémon. `routes/event_battle.rec`: mode 2, the team check and the action menu, back, the settings panel, the
  rules screen (every entry), the return to the mode select (`EventBattle_ValidateInitialSelection`) and the battle
  start (`State_BattleFromEvent`). After the short-party fix (PR #67) a cartridge with three Pokémon
  is rejected by the rule: the notice (`EventBattle_DrawSlicedFrame`) and leaving through the mode select. Left:
  the spinner arrows (the time limit on with the cursor on its minutes or seconds row); the rental-rules species
  window's scrolling.
- **Pikachu reward** 29/29 (was 0): a Game Pak Free Battle (`../battle_engine/routes/free_battle_gamepak.rec`, the
  base save's Pikachu L25 without Surf), then `Fragment_LoadAndCall` of the reward when the post-battle screen
  returns (`--call "BattlePrep_RunRulesScreen#2@exit:Fragment_LoadAndCall:..."`), as `State_StadiumMenu` enters it
  after a cup win; every prompt of the reward takes A (Yes is the default, the move list starts on the first
  move): `routes/pikachu_reward_3moves.rec`, `routes/pikachu_reward_4moves.rec` (a Pikachu with four moves:
  `gb_party.py ... "PIKACHU:25,moves=84/45/86/98;..."`). The declined ending was reached by an earlier mashing run;
  teaching Surf, saving to the cartridge and the success ending after the crash fix (PR #67).
- **Game Boy cries and music** (`gb_audio.c`, `gb_audio_render.c`) 36/57 (was 1): with the sound engine on
  (`PORT_GAME_AUDIO=1`), every Pokédex cry played once (`routes/all_cries.calls`: `Cry_PlayFromPokedex` for 1-151,
  150 frames apart, over `routes/idle.rec`). Left (21): the Game Boy music sequence commands (notes, octave, tempo,
  loops, calls, pitch slides, panning, master volume), `GbMusic_StartSequenceById` and `GbMusic_StartMusic` (nothing
  references either) and the empty `func_8004AD2C`: no cry's data uses those commands, so with the shipped data
  this is code the game cannot run.
- **Lab PC See List**: the route runs to its end with the fix (`lab_pc_list` 129 of 282).
- Recordings made before the sound engine was ported desynchronise with it on (its fades and waits change the
  timing): the audio-on copies of the stored recordings only reach what they reach before the first desync.

Two new port crashes, fixed in PR #67:
1. **Pikachu reward, learning Surf**: segfault in `ModelAnim_EvaluateScaleChannel` (from `Geo_RenderRootNode`,
   `PikachuReward_ShowTextWaitForA`, `PikachuReward_ResolveMoveSlot`) right after the follower model switches to
   animation 9, fault address `0x8811208e`; the same with three or four moves, and with the reward entered at the
   point the game enters it (not an artefact of the forced call).
2. **Event battle with a cartridge whose party has fewer than six Pokémon**: `EventBattle_InitTrainerCard` reads six
   entries per side, an empty slot keeps species 0 and `Text_CopySpeciesName(0)` reads the name pointer before the
   species table (`D_8006FF00[-1]`): harmless on the N64, a wild pointer on the host (`HAL_Strcpy`).

Runner: runs no longer open a window or take the focus (`PORT_HIDDEN_WINDOW`, and the evidence runtime makes the
game a macOS accessory app with `PORT_EVID_BACKGROUND`); `PORT_HEADLESS`, which `run.py` set before, is read by
nothing.

Largest areas left (`frontier.md`): battle engine 194, Lab PC list 153, demo scenes 103, Lab PC 70, particle math
62, battle prep 46, rental team selection 40, the 64DD library (`leo*.c`, no drive).

The sections below are the earlier rounds.

**Previous round (origin/main 7936e2e + #61 merged): 5926 of 7490 functions (79.1%), lines 68.1%, branches 59.4%** over 159
runs plus 627 forked direct calls. New in this round, on top of the levers below:
- **Menu mash routes** (`routes/lab_*_mash.rec`): each Lab screen (Pokémon, Items, BOXes, See List, Pokédex, Trade
  Machine) entered with a Game Pak, then a fixed cycle of A, directions, L/R, Z and C-buttons; an `items` Game Boy
  save variant (full bag and PC item storage) for the Items screen.
- **Forced fragment entries** (`tools/evidence/fragment_calls.py`): a direct call of `Fragment_LoadAndCall(id, ROM
  start, ROM end, 0, 0)` for each fragment no run entered (the absolute `fragmentNN_*` symbols give the arguments).
  Ran to the end: credits 26/27, Gym Leader Castle clear 21/23, Jigglypuff Doodle 16/18, mini-game sample viewers 7
  and 8 10/10 each, snap print-club background select 9/10, ranking debug test 7/8, fatal error screen 6/6. Crashed
  without their parent state's setup: event battle, gallery enlarge, rental rules, Pikachu reward (next: call them
  from inside the state that normally enters them).
- **Forked direct calls** (`tools/evidence/dead_calls.py`, `run.py --call-file F --call-fork`): every function that
  nothing references (668) called once with placeholder arguments, each in a forked child (2 s alarm, own coverage
  file), so crashes and hangs cost one call: 627 called (41 skipped: struct arguments by value), 504 returned
  (`dead_calls.md`: return value per function), 59 crashed, 63 hung, 1 bus error. Integer and float arguments in any
  order (arm64 passes them in separate registers).
- 10 of 29 new memory proposals opened their gate (+41 functions).

Left (1564): 1196 behind 821 gates (`frontier.md`: menu steps 376, other state 398, session/save/mode 187, callbacks
137, results of calls 117, table indices 115, hardware 59), 368 with no reached caller (`dead_code.md`: 118
referenced by nothing: the direct calls that crashed, hung or were skipped; 18 referenced only from data; 232 below
other unreached code). Largest areas: event battle 56, rental rules 55, Game Boy music (gb_audio.c) 44, Pikachu
reward 29, the 64DD library, the Lab PC list 168 (much of it behind a new crash, below), demo scenes 103.

New port crash (not fixed here): Lab PC → See List, `routes/lab_seelist_mash.rec` with the `boxes_full` save, frame
7804: segfault in `func_88304850` (lab_pc_list, drawn from `LabPCList_RunWidgetLoop` through the widget tree), a
garbage pointer (`0x136b5573d15ad36e`).

The sections below are the earlier rounds.

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

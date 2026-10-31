# Naming every function and variable with runtime proof

This guide is for people and AI agents who want to give each function and variable of the Pokémon Stadium decomp a
name backed by what the game does when it runs that code, not by a guess from reading it. It uses the evidence work
in `evidence/` and `tools/evidence/`: an instrumented build of the port, recorded routes that drive the game to
every screen and situation reached so far, and the reports that say which code ran, which code cannot run and why.

State at the time of writing (`evidence/whole_game/dead_classes.md`): 7005 of 7490 game functions ran in at least
one recorded run (93.5%); counting only the code the shipped game can run, 98.3% (99.4% without the 64DD library).
Of the 485 that did not run, 365 are proven dead, 79 are the 64DD library (no drive in the port) and 41 are still
to reach.

## What is here

| path | what it is |
|---|---|
| `cmake/evidence.cmake`, `src/port/evidence/evidence_rt.c` | the instrumented build: every game source gets function-entry hooks and clang source coverage; the runtime records callers, call paths, call order, watched state, and can force memory values or make direct calls |
| `tools/evidence/` | the tools (`README.md` there has the full table): `run.py` (one run), `mkrec.py` (input recordings from timed presses), `callgraph.py`, `frontier.py` (what keeps code unreached), `dead_classes.py` (why each unreached function did not run), `deadcode.py`, `dead_calls.py`, `fragment_calls.py`, `try_gates.py`, `covdiff.py`, `compact_runs.py`, `analyze.py` (evidence records), `watch.py`, `gb_party.py`, `gb_variants.py` |
| `evidence/whole_game/routes/<area>/` | the recorded routes, one folder per area, each with a README: what every route reaches, the saves it needs (recipes, never the files), the input patterns, and which conditions were forced |
| `evidence/whole_game/README.md` | how each lever was used, round by round, and the current numbers |
| `evidence/whole_game/frontier.md` | per area, the gates that still hold code back (the branch that never went the other way, with its source text) |
| `evidence/whole_game/dead_classes.md` / `.json` | every unreached function with its class: referenced by nothing, never selected (data or code proof), PAL only, 64DD, reachable only from dead code, or live with its gate class |
| `evidence/whole_game/data_dead*.json` and the proofs they cite | the code the shipped data or code never selects: `battle_anim_slots.md`, `particle_descriptors.md`, `audio_commands.md`, `geo_commands.md`, the engine leftovers in `routes/engine_left/README.md` |
| `evidence/whole_game/dead_calls.md` | what a forced direct call of each unreferenced function returned |
| `evidence/battle_engine/` | the pilot: evidence records for the battle engine (`records.md`, `reviews.json`), the format to follow |

## Setup

1. Build the port as usual once (ROM path, `deps`), then an instrumented build in its own directory:
   `cmake -S . -B build-evidence-all -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_INCLUDE=$PWD/cmake/evidence.cmake -DPORT_EVID_FILES=game/src/`
   then `nice -n 15 cmake --build build-evidence-all -j 4`. Never build into `build/` if others use it.
2. Saves are not in the repository. Game Boy saves: `tools/gb_save.py <out> --got-pokedex --rom <US-titled cart> --party PIKACHU:25,...`,
   then `tools/evidence/gb_party.py` (any party) and `tools/evidence/gb_variants.py <base> <dir>` (full boxes, items,
   bad checksums, ...). Stadium saves: `python3 -m stadium_save` from `tools/save_editor` (`unlock`, `team`, `box`,
   `items`, `statue`, `continue`, `set`) on a copy of `replays/default.flash`. Each route README lists the recipe.
3. For comparisons with the real console: ares with the N64 and Game Boy cores (`tools/ares_ref`, built with
   `-DARES_CORES="n64;gb"`). A route converts to an ares script with screenshots at the same frames (ares frame =
   port frame + 1).

## One run

```
python3 -I tools/evidence/run.py <out dir> --bin build-evidence-all/pokestadium --frames N --rec <route.rec> \
    [--natural] [--gb ROM:SAVE] [--gb2 ROM:SAVE] [--flash STADIUM.flash] [--shots F1,F2] \
    [--set "Fn[#n][@exit]:symbol+offset:size=value;..."] [--call "Fn[#n][@exit]:Target:ret:args"] [--watch A,B]
```

Runs are hidden (no window, no focus) and low priority. The output folder has:

- `coverage.json`: for every function that ran, its call count, first frame, and its runtime callers with the frame
  and a context string; the call paths; the call order (`seq`); the watched functions' state.
- `coverage.profraw`: line, region and branch counts for llvm-cov (`frontier.py`, `covdiff.py`).
- `shots/stop_<frame>.ppm`: the screen at the frames given to `--shots`.

`--natural` replays a recording as made (the game's own randomness); without it, battles run in the lockstep mode
(seeded randomness, the player's move timer picks the first move). `--set` writes a value at a function's entry (or
when that call returns, with `@exit`), e.g. to enter a game state through the game's own loop:
`--set "State_TitleScreen#1@exit:gCurrentGameState:4=39"` after `mkrec.py r.rec "1720 A"`. Host struct offsets
differ from the N64 offsets in the decomp's comments: compute them with `offsetof` using a game source's flags from
`compile_commands.json`.

## Naming a function

For each function, the goal is a record that says what it does, shown by runs, and a name that says the same.

1. **Find its class.** Look it up in `dead_classes.json`:
   - reached: continue with step 2;
   - proven dead (referenced by nothing, never selected, reachable only from dead code, PAL only): there is no run to
     observe. Name it from its code and its proof, and say in the record that it is dead and why (the proof file);
     `dead_calls.md` has what a forced direct call returned for the unreferenced ones;
   - 64DD: name it from the 64DD library's documented interface (libleo), marked as not observable in the port;
   - live: reach it first (step 6), then continue.
2. **Find the runs that reach it.** Search the batch or run folders' `coverage.json` for the name (the parent
   session's union of all runs, or re-run the routes of the area). `first_frame` and `sites` tell you which route,
   when, and from which caller. Prefer a run where it is reached by a real route without forced conditions.
3. **Observe it.**
   - Re-run that route with `--shots` at and after the first frame: what is on screen when it runs.
   - Read its callers and callees in the call graph (`callgraph.py` output: `call`, `table` and `ref` edges) and in
     the run's call paths: what calls it, in which situation, what it calls.
   - Read the branch counts (`llvm-cov show build-evidence-all/pokestadium -instr-profile=<merged.profdata> <file>`):
     which of its branches ran and how often, which never did.
   - For battle code, `--watch Fn` records the battle state at entry and exit; `watch.py diff <run> Fn` lists what
     changed.
4. **Test the hypothesis.** Change one input and see the effect:
   - another route, save or party (e.g. a status move, a full box, a corrupt save) that should or should not reach
     it; compare with `covdiff.py` and screenshots;
   - `--set` a variable it reads to another value and watch the screen or the call order change;
   - for a pure function, a direct call with chosen arguments (`--call ... --call-fork`) and its return value.
5. **Check it is the same function everywhere.** When several routes reach it, the record lists them; behaviour
   must agree. Compare with ares when a difference could be a port bug (port-only changes are marked `PORT:` in the
   decomp; evidence from changed lines is weaker).
6. **Reach a live function.** Read its gate in `frontier.md` (the condition that never held), then write a route
   with `mkrec.py` screen by screen, checking each step with `--shots`; or find the save or state that sets the
   condition; or force it with `--set` and say so in the record. If the condition can never hold in the shipped game,
   write the proof (which data or which code would have to set it, and that none does) and add the function to a
   `data_dead_<area>.json` file, as the existing proofs do.
7. **Write the record** (format in `tools/evidence/README.md`, examples in `evidence/battle_engine/records.md`):
   the proposed name, the observed behaviour, the runs with frames (route, saves, forced conditions), the callers,
   screenshots where they show something, the ares comparison if made, and a confidence. `analyze.py` builds the
   records from the call graph and runs; the hand-checked part goes into a `reviews.json`.

## Naming a variable or struct field

Variables have no call counts, so the evidence is what reads and writes them and what changes when they change.

1. Find the readers and writers in the code (grep, the call graph's functions around them), then which of those
   functions ran (step 2 above) and when.
2. Observe the value: a direct call of a small accessor at a hook, `--watch` in battle code, or screenshots before
   and after a frame where a writer runs.
3. Change it: `--set "Fn#n:Variable+offset:size=value"` at a function that runs before the readers, and watch the
   effect on screen and in the call order. A field whose change moves a cursor, changes a menu page or flips a mode
   names itself. Record the host offset and the N64 offset.
4. For tables (moves, species, effects, animation lists), decode the data the ROM ships and cross-check with what
   runs: the proofs in `battle_anim_slots.md`, `particle_descriptors.md`, `audio_commands.md` and `geo_commands.md`
   show the method and give decoders.
5. Rename struct fields only together with the struct's other users, and check the host layout (`offsetof`): many
   port bugs found on the way were structs whose N64 layout did not hold on the host.

## Applying the names

The names live in the decomp (`game/`, a submodule with the port's changes as patches in `patches/game/`). Rename in
the decomp repository (sources, headers, symbol files) and update the submodule and patches, or carry renames as a
game patch (`tools/game_patches.py apply`, edit, `export`) until they go upstream. Keep each rename next to its
record, so a reviewer can follow the proof. After a rename, rebuild the instrumented build and re-run the area's
routes: coverage profiles are tied to the build (changed functions drop out of old profiles), and the records must
still point at functions that ran.

## Working with AI agents

The coverage was reached by several agents working in parallel, one area each. The same works for naming.

- **One area per agent** (a fragment or a group of files), with a brief that names the area, the tools, the rules
  below and what done looks like (records, renames, routes).
- **Shared rules**: each agent keeps its runs in its own folder and keeps each run's `coverage.json` and
  `coverage.profraw`; it commits only its own files (`git add` of its paths, never everything); the shared
  instrumented binary is not rebuilt by agents; tools are extended by adding files, not editing shared ones; builds
  run at low priority; no ROMs or saves are committed; one agent merges the results (`compact_runs.py`, then
  `frontier.py` over all batches and runs, then `dead_classes.py` with every `data_dead*.json`).
- **Port bugs on the way** (crashes, wrong screens) are fixed in their own worktree from `main` and their own pull
  request, without stopping the naming work; compare with ares first. The common class so far: structs with an
  N64-only layout (padding that stands for pointers, hard-coded entry sizes or strides, N64-offset views), and
  functions that fell off their end without a return.
- **Evidence over guesses**: a name without a run or a proof is a proposal, not a result. Records say which
  conditions were forced.

## Pitfalls

- Recordings replay frame-exact only on the build they were made with. A fix that changes timing (a scene that now
  plays, a battle outcome) can make later inputs land elsewhere: re-check the route's milestones (first frames of
  its key functions) after merging fixes.
- Coverage profiles are tied to the binary: after a code change, the changed functions' counts from older profiles
  no longer apply. Re-run the routes that reach them.
- Same-named static functions (inline helpers in headers) exist in several copies; `frontier.py` counts the copy
  that ran.
- 64DD calls park the game thread forever (no drive), so such runs never reach `--frames`: end them by time and keep
  the dump interval large, or the profile grows without bound.
- Screens that read the controller several times per frame, wait for an animation, or take input only once a panel
  is open need presses held for a few frames and spaced out; check with `--shots`.
- Two-player screens need controllers 2-4 plugged in (`PORT_CONTROLLERS=1,2`, or the launcher's Controls panel) and
  inputs for those pads in the recording.
- Watch the disk: frame dumps and profiles of long runs add up.

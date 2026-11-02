# Function evidence (pilot: the battle engine)

Goal: every decomp function name backed by runtime evidence of what the function does, reached through its real
call paths, and recognised as the same function when several paths reach it.

## Pieces

| step | tool | output |
|---|---|---|
| static call graph | `callgraph.py <build dir> <out.json> [--files REGEX]` | functions (file:line, comment), edges `call` / `table` (dispatch through a function-pointer table such as `gMoveEffectHandlers`) / `ref` (address taken: callback), pointer tables |
| instrumented build | `cmake -S . -B build-evidence -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_INCLUDE=$PWD/cmake/evidence.cmake` | the port with the files matching `PORT_EVID_FILES` compiled with `-finstrument-functions`, plus the hook runtime `src/port/evidence/evidence_rt.c`; no shared source changes |
| one battle | `run.py <out> --bin build-evidence/pokestadium --seed N [--queue F] [--watch A,B] [--moves ...]` | `coverage.json`: per function its count, runtime callers (instrumented parent on the shadow stack, plus the return-address function, which names main-code dispatchers such as `Geo_NodeModelRoot`), call paths, the call sequence with battle context, the watched functions' state at entry and exit |
| forced situations | `watch.py queue <baseline run> <out> <fn> <k> <value>` | a `PORT_XCHECK_RNG` queue: the baseline's values up to call k of that random function, then the forced value; later calls keep the seeded stream |
| ares side | `watch.py ares <out> <seed> <case> <frames>` | the same battle in ares with a lockstep case file (`rule Battle_Random@Battle_CheckCriticalHit 34`) and its caller-tagged random trace |
| state diff | `watch.py diff <run> <fn>` | per watched call: caller, battle fields changed between entry and exit |
| forced conditions | `run.py ... --set "Fn[#n]:sym[*][+off]...:size[\|]=value;..."` | at entry of `Fn` (its n-th entry), write a value through a symbol and pointer path (`gBattleUser*+0x15:1=8`: the user's status); also for constants the shipped game never changes (`D_8438AC60:1=0`, the rules mode). Port-only, marked as forced in records |
| branch coverage | built in (`PORT_EVID_COVERAGE`, `-fprofile-instr-generate -fcoverage-mapping`) | `coverage.profraw` per run: line, region and branch counts (llvm-cov) |
| what keeps code unreached | `frontier.py <callgraph.json> <binary> <out> <runs>... [--build build-evidence]` | gates (reached caller, unreached callee), the branch side never taken before the call with its source text, its class (random, rules constant, battle state, session/save, input, AI, table index), how many unreached functions each gate holds back; with `--build`, proposed `--set` values for plain conditions (offsets from a probe compiled with the caller's flags) |
| trying them | `try_gates.py <proposals.json> <baseline runs>... --out DIR` | one run per proposal: did the callee run, what became reachable |
| Game Boy data | `gb_party.py <base.sav> <out.sav> "<mon>;..."`, then `run.py ... --gb ROM:SAVE --rec ROUTE` / `watch.py ares ... --gb ROM:SAVE --rec ROUTE` | any party in a Red/Blue/Yellow save (species, moves incl. illegal ones, DVs, status, HP, OT, nickname); a battle from another route (e.g. `evidence/battle_engine/routes/free_battle_gamepak.rec`) with that cartridge. ROMs and saves stay out of the repository; ares needs the Game Boy core (`-DARES_CORES="n64;gb"`) |
| save variants | `gb_variants.py <base.sav> <dir>`, then runs named `<route>__<variant>`, `variant_matrix.py <runs> <callgraph.json> <out.md>` | 14 variants (party, boxes, Pokédex, flags, map, checksums, Hall of Fame, illegal, fainted); per route what each adds over the base save and over no cartridge (`evidence/gb_variants/`) |
| whole game | `cmake ... -DPORT_EVID_FILES=game/src/` (own build dir), `run.py --dump-every 600 --gb2 ROM:SAVE --flash F` | every game source instrumented; second cartridge; Stadium flash per route |
| dead code | `deadcode.py <whole-game callgraph.json> <rom.z64> <out.md> <names or ->` | functions with no C reference in the whole decomp and no ROM word with their N64 address (`port_func_table.c`) |
| forced fragment entries | `fragment_calls.py <binary> <callgraph.json> <out.json> <runs>...`, then `run.py --call "<spec>"` | a direct call of `Fragment_LoadAndCall` for each fragment no run entered |
| calls for unreferenced code | `dead_calls.py <callgraph.json> <names> <out.txt>`, then `run.py --call-file out.txt --call-fork` | one placeholder call per function, each in a forked child (crashes and hangs cost one call); results in the sequence file (`K` lines) |
| what a lever adds | `covdiff.py <binary> <out.md> --base <runs>... --new <runs>...` | functions and source lines only the new runs reached |
| records | `analyze.py <callgraph.json> <out> <runs>... --reviews reviews.json` | `records.json` (all functions), `coverage.md`, `records.md` (the reviewed ones) |

The instrumented build behaves like the plain one: seed 7 gives the same 71 events and the same 6387 forced random
calls (per function) as the lockstep reference run.

On Windows, `whole_game_windows.py --bin EXE --rom STADIUM.z64 --out DIR --backend opengl --gb-rom YELLOW.gb`
runs the full stored-route catalog with the user's German Yellow cartridge. Use `--plan` to inspect the inventory
first, and repeat with `vulkan` and `directx` for the other renderers. Run Python with `-X utf8 -I`. The catalog
currently covers all 144 stored recordings in 163 scenarios; without `--gb-rom`, only the 37 cartridge-independent
recipes are available. Generated Game Boy saves and Stadium flashes remain under the output directory, while
each run receives its own writable copies. Cartridge ROM and save contents participate in cache fingerprints.
The album/printing cases wait for the album-producing route, including with multiple jobs. A failed case stops
new launches; completed matching cases are reused on resumption. Lab and registration cases require evidence
that their target screen was reached, in addition to a clean exit and a coverage profile.

The cartridge fixtures reproduce the documented party, box, item, and progress conditions. They are synthetic
saves for Stadium's cartridge-reading screens; continuing an overworld in GB Tower requires a save written by
the Game Boy game itself (see `tools/gb_save.py`). Deliberately hanging 64DD probes and macOS crash-buffer injection
from the diagnostic scripts are separate hardware/platform experiments, rather than stored-route passes.
The title-screen audio probe exercises bank-stop dispatch and random seeding. Category 12 playback is checked
through the album's real delete-photo caller: injecting it at title exit lets the following menu replace the
shared FX bank while its command stream is active.

## Evidence record

```json
{
  "name": "Battle_CheckCriticalHit",
  "comment": "",                                   // the comment above the definition
  "file": "src/fragments/battle_engine/battle_engine_359F90.c:1183",
  "static": false, "port_changed": false,           // PORT: lines in the body: port-only evidence is weaker there
  "static_callers": [{"caller": "func_84372670", "kind": "call", "via": ""}],
  "static_paths": [["BattleScene_CollectActions", "Battle_StartTurn", "Battle_ResolveMonAction", "func_84372670",
                    "Battle_CheckCriticalHit"]],
  "runtime": {"count": 83, "runs": ["s1", "..."],
              "callers": [{"caller": "func_84372670", "via": "", "count": 83,
                           "first": {"run": "s1", "frame": 2951, "ctx": "1/1/106:96/4/0"}}],
              "paths": [["...outermost", "...", "Battle_CheckCriticalHit"]]},
  "scenario": "seed 7, queue Battle_Random#0 = 34 / 35 (ares: rule Battle_Random@Battle_CheckCriticalHit 34 / 35)",
  "observed": "...",                                // state before/after, return value or its visible consequence
  "ares": "...",                                    // same scenario in ares: same / different observable effect
  "verdict": "confirmed | name wrong | unclear | not reached",
  "notes": "..."
}
```

ctx is `<BattleScene phase>/<attacker side>/<user species>:<used move>/<target species>/<gBattleDamage>`.

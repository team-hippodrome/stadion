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
| records | `analyze.py <callgraph.json> <out> <runs>... --reviews reviews.json` | `records.json` (all functions), `coverage.md`, `records.md` (the reviewed ones) |

The instrumented build behaves like the plain one: seed 7 gives the same 71 events and the same 6387 forced random
calls (per function) as the lockstep reference run.

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

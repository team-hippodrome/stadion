# Native Windows validation

For a visible window smoke test, run this on an interactive Windows desktop
with Pillow installed. It uses F11 and Alt-Tab, resizes the game, and moves the
window to another display when one is available. Keep the desktop idle while
the test runs. Captures and results are written to the output directory.

```powershell
python tools/windows_window_smoke.py --bin build-win/pokestadium.exe --rom 'C:/path/to/Pokemon Stadium (USA).z64' --out build-win/validation/window-smoke
```

`run_replays.py` accepts `--port-bin`, `--backend directx|opengl|vulkan`, `--out`, and
`--reference-dir` in addition to its existing arguments. Each case uses a fresh save
copy, isolated settings, and a hidden rendered window. Missing Game Boy inputs are
reported as skipped. A different selected backend is a failure, including a Vulkan
fallback. Windows timeouts allow for slower framebuffer readback.

Example (PowerShell, from the repository root):

```powershell
python -X utf8 -I tools/run_replays.py --all --port-bin build-win/pokestadium.exe --rom 'C:/path/to/Pokemon Stadium (USA).z64' --backend directx --out build-win/validation/directx/replays -j 2
python -X utf8 -I tools/run_replays.py --all --port-bin build-win/pokestadium.exe --rom 'C:/path/to/Pokemon Stadium (USA).z64' --backend opengl --out build-win/validation/opengl/replays --reference-dir build-win/validation/directx/replays/ref -j 2
```

Without an existing reference the runner creates a local baseline; this checks
completion and creates a capture, rather than proving fidelity to the original.
`results.json` records each status. A supplied reference directory is read-only:
missing baseline images produce `NOREF`.

The existing animation suite also runs on Windows using the patched `ares_ref.exe`:

```powershell
$env:ROM = 'C:/path/to/Pokemon Stadium (USA).z64'
$env:ARES_REF_BUILD = 'C:/path/to/ares-reference/build'
$env:ARES_REF_SRC = 'C:/path/to/ares-reference/ares'
$env:PORT_TEST_BACKEND = 'directx'
$env:XCHECK_SWEEP_DIR = "$PWD/build-win/validation/directx/animations"
python -X utf8 -I tools/battle_sweep.py run --port-bin build-win/pokestadium.exe --jobs 2
```

Repeat with `opengl` and `vulkan`, choosing a distinct output directory for each.
The standard sweep covers the base move paths; `--all-paths` expands the catalog.
Inspect `report.md` and per-case `result.json`: a completed runner process does not
mean every animation, logic, or sound comparison matched. Finished cases are cached;
`--open` retries unresolved cases. The Windows reference runner copies its private
ROM alias instead of requiring privileged symbolic links. The original ROM and saves
are never modified.

Windows cross-check caller names come from a generated index of the committed game
function table, replacing `dladdr`. Rebuild after updating the port before comparing
animations: older Windows executables report `caller=?` and cannot apply rules
targeting a particular caller. Such forced-path comparisons are invalid.

`XCHECK_REFERENCE_CACHE` optionally shares compressed Ares captures between renderers.
Its key includes the ROM, reference executable, symbols, firmware, initial state,
battle case and route, and capture limits. Port frames are always rerun and compared
with the same oracle captures; corrupt reference archives are regenerated.


## Menu-return and GB Tower regressions

Windows fragment sections must use Clang section pragmas and COFF subsections:
`.data$fdNN$...` for initialized data and `.bss$fbNN$...` for zero-initialized data.
The BSS boundary markers must themselves be BSS. Ignored MSVC pragmas or DATA
markers around BSS leave state from the previous visit, including the hub exit fade.
The subsections merge into ordinary output sections, keeping PE section count small.

GB cartridge variant detection must address each title array explicitly. Red/Green/Blue
live within `D_8120DD2C`, while Yellow lives in a separate array; stepping backward
from Yellow relies on a linker layout that Windows does not preserve.

Run the real menu routes rather than `PORT_BOOT`, which skips the initial hub visit:

```powershell
python -I tools/windows_menu_regressions.py --bin build-win/pokestadium.exe --rom "C:/path/Stadium.z64" --gb-rom "C:/path/Red.gb" --gb-save "C:/path/Red.sav" --out build-win/validation/menu-regressions --backends directx opengl vulkan --jobs 2
```

The runner checks backend selection, exit status, completion frame, and unchanged
input hashes. It copies battery saves before running and retains screenshots for
visual review. A captured run is not automatically a functional pass. On the Free
Battle route, the final image should show the Stadium island and text after canceling.
The GB route should boot the cartridge and continue the save; review its final image.
`PORT_TRACE_GBPAK=1` logs banked reads and power transitions for diagnosis.


## Whole-game route evidence

Configure a separate Windows build with the normal toolchain/dependency options plus
`-DCMAKE_PROJECT_INCLUDE=<absolute-path>/cmake/evidence.cmake`,
`-DPORT_EVID_FILES=game/src/` and `-DPORT_EVID_COVERAGE=ON`.
Use `llvm-nm`, `llvm-profdata` and `llvm-cov` from the compiler's LLVM distribution.
Set `NM` to the full `llvm-nm.exe` path if it is not on PATH.

```powershell
foreach ($backend in @('opengl', 'vulkan', 'directx')) {
    python -X utf8 -I tools/evidence/whole_game_windows.py --bin build-evidence-win/pokestadium.exe --rom 'C:/path/to/Pokemon Stadium (USA).z64' --out build-win/validation/whole-game --backend $backend --jobs 2
}
```

The runner currently ports 37 source-documented recipes. `--plan` inventories all
144 stored route recordings, distinguishing those recipes from recordings that still
need fixtures or recipe porting. It does not claim complete whole-game coverage.
Each result records the executable hash, input fingerprint, selected backend,
expected-function reachability and exit status. Missing expected functions, crashes
and timeouts fail the aggregate run. Profiles and call evidence stay local.
Windows cannot run fork-isolated probes or the original ARM64 mixed-float probes;
those unsupported requests fail explicitly rather than being simulated.

Animation `--open` and reports supplied `--port-bin` reject stale executable hashes
and case specifications. Evidence-mode cache reuse also requires the raw profile.


## Audio comparison timing

The animation runner replays the reference's observed `V <game-frame> <retrace>`
schedule on its second port pass, in addition to matching asset-load waits. N64
rendering can take more than two retraces per battle frame. Without matching that
cadence, later sound commands run early on the native port and an otherwise matching
sound is compared against a different instant of the reference's mix.

`PORT_XCHECK_VI_TIMING` is a comparison-only file option; normal play is unchanged.
The mixer keeps its original sample clock, and audio thresholds are unchanged.
Case results record `timing_mode` and the schedule's SHA256. Set
`XCHECK_REFERENCE_TIMING=0` to diagnose native-speed differences; those results
cannot reuse reference-paced cache entries. Malformed or discontinuous schedules
fail explicitly. `python -X utf8 -I tools/test_reference_timing.py` checks schedule
handling and confirms that missing audio still fails comparison.

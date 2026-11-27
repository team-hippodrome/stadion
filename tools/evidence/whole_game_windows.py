"""Run source-documented whole-game scenarios on Windows, preserving fixtures and coverage.

Usage: python -X utf8 -I tools/evidence/whole_game_windows.py --bin EXE --rom ROM --out DIR
       [--backend directx|opengl|vulkan] [--jobs 2] [--case ID]
Only explicit recipes are run; route recordings without their required recipe/fixtures are inventoried separately.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import battle_xcheck as bx
from runner_platform import process_options
from run import cartridge_paths


def recipes(fixtures):
    cases = []
    base = ROOT / 'evidence/whole_game/routes'
    def add(name, rec, frames, source, *extra, expect=(), expect_edges=(), min_calls=None):
        cases.append(dict(name=name, rec=str(base / rec) if rec else None, frames=frames,
                          source=source, extra=list(extra), expect=list(expect),
                          expect_edges=list(expect_edges), min_calls=min_calls or {}))
    flash = str(ROOT / 'replays/cup_win_badge.flash')
    for name, win0, win1 in [('cup_loss_opt0', 0, 1), ('cup_draw0', 0, 0), ('cup_draw1', 1, 1)]:
        add('engine_' + name, 'engine_left/cup_loss_opt0.rec', 26000, 'engine_left/runs.sh', '--natural',
            '--flash', flash, '--set', f'BattlePrep_Main#2:gSessionContext+0x1220:1={win0};'
            f'BattlePrep_Main#2:gSessionContext+0x1270:1={win1};BattlePrep_Main#2:gSessionContext+0x1297:1=3')
    add('rules_species_scroll', 'engine_left/rules_species_scroll.rec', 5600, 'engine_left/runs.sh', '--natural',
        expect=('RentalRules_ScrollPageDown', 'RentalRules_ScrollPageUp'))
    add('prenmi', 'engine_left/title_a.rec', 3000, 'engine_left/runs.sh', '--natural', '--call',
        'State_TitleScreen#1@exit:port_PostEvent:v:i14;Sched_HandleRetrace#3000@exit:Port_EvidenceExit:v:i0',
        expect=('Sched_HandlePreNMI',), min_calls={'func_8000559C': 16})
    for name in ('cup_grid', 'cup_grid_exchange'):
        add(name, 'rental_grid/' + name + '.rec', 8200 if name == 'cup_grid_exchange' else 7700, 'rental_grid/README.md', '--natural', '--flash', flash,
            expect=(('Rental_ResolveSlotPick', 'Rental_CarouselConfirmMon') if name == 'cup_grid_exchange' else ('Rental_ConfirmTeam',)),
            expect_edges=(('Rental_ResolveSlotPick', 'Rental_CarouselResumeInput'),) if name == 'cup_grid_exchange' else (),
            min_calls={'Rental_CarouselConfirmMon': 7} if name == 'cup_grid_exchange' else {})
    for name, frames, state in [('gal_tutorial', 6500, 17), ('gal_reselect', 5600, 17), ('kc_grid_cancel', 4800, 35)]:
        add(name, 'minigames_gallery/' + name + '.rec', frames, 'minigames_gallery/runs.sh', '--natural', '--set',
            f'State_TitleScreen#1@exit:gCurrentGameState:4={state}')
    for name, frames, state in [('options', 3800, 19), ('area_select_mewtwo', 3000, 16)]:
        add(name, 'menus_trade/' + name + '.rec', frames, 'menus_trade/README.md', '--natural', '--flash',
            str(fixtures / 'unlocked.flash'), '--set', f'State_TitleScreen#1@exit:gCurrentGameState:4={state}')
    add('flash_options_backup_bad', 'idle.rec', 2500, 'error_paths/runs.sh', '--flash',
        str(fixtures / 'options_bak_bad.flash'))
    add('debug_profiler_title', 'idle.rec', 2400, 'error_paths/runs.sh', '--set',
        'StageLoader_SetupFrame#1:gShowCPUProfiler:4=1;StageLoader_SetupFrame#1:gShowMEMProfiler:4=1')
    for name, frames in [('music1', 81500), ('music2', 81500), ('music3', 81500), ('music4', 81500),
                         ('system', 25200), ('moves1', 62400), ('moves2', 63500), ('species', 26500)]:
        add('audio_' + name, 'audio_seq/options.rec', frames, 'audio_seq/runs.sh', '--natural', '--set',
            'State_TitleScreen#1@exit:gCurrentGameState:4=19', '--call-file', str(base / 'audio_seq' / (name + '.calls')))
    for move in (12, 60, 100, 154, 155, 126):
        add('battle_gate_mv' + str(move), None, 24000, 'battle_gates/runs.sh', '--seed', '7', '--moves',
            f'{move},0,0,0:40,0,0,0')
    for move in (8, 131):
        add('particle_mv' + str(move), None, 14000, 'particles/runs.sh', '--seed', '7', '--moves',
            ','.join([str(move)] * 4) + ':1,1,1,1')
    for name, frames, recipe, flashname in [
        ('vp_gift', 6200, 'minigames_gallery/vp_overlays.rec', 'vp_all.flash'),
        ('gate_vp', 4800, 'gate_trials2/victory_palace_overlays.rec', 'st150.flash'),
        ('gift_keep', 7200, 'gate_trials2/completion_gift_keep.rec', 'st_bonus.flash'),
        ('gift_swap', 7200, 'gate_trials2/completion_gift_swap.rec', 'st_bonus.flash')]:
        extra = ['--natural', '--flash', str(fixtures / flashname)]
        if name == 'vp_gift':
            extra += ['--set', 'State_TitleScreen#1@exit:gCurrentGameState:4=36']
        add(name, recipe, frames, 'gate_trials2/runs.sh' if name != 'vp_gift' else 'minigames_gallery/runs.sh', *extra)
    title = 'State_TitleScreen#1@exit'
    for name, args in [('prof_m0', f'{title}:Profiler_SetDisplayModes:v:i3,i2;{title}:Profiler_SetDisplayModes:v:i1,i1;'
                       f'{title}:clear_profiler_data:v:;{title}:set_get_time_diff:i:i1;{title}:set_get_time_diff:i:i2;'
                       f'{title}:set_get_time_diff:i:i0'), ('prof_m1', f'{title}:Profiler_SetDisplayModes:v:i2,i0')]:
        add(name, 'debug_crash/title.rec', 2100, 'debug_crash/routes.sh', '--natural', '--call', args,
            expect=('Profiler_SetDisplayModes',))
    add('gldb', 'debug_crash/gldb.rec', 2400, 'debug_crash/routes.sh', '--natural', '--call',
        f'{title}:GalleryDebugger_Main:i:i0,i0', expect=('GalleryDebugger_Main',))
    return cases


def prepare(directory):
    directory.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONPATH=str(ROOT / 'tools/save_editor'))
    def edit(*args):
        subprocess.run([sys.executable, '-X', 'utf8', '-m', 'stadium_save', *map(str, args)],
                       cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
    default = ROOT / 'replays/default.flash'
    damaged = bytearray(default.read_bytes())
    damaged[0x19EA0 + 0x28 - 3] ^= 0xFF
    (directory / 'options_bak_bad.flash').write_bytes(damaged)
    edit('unlock', default, '--mewtwo', '--round2', '-o', directory / 'unlocked.flash')
    edit('statue', default, 'set', 'all', '-o', directory / 'vp_all.flash')
    edit('statue', directory / 'vp_all.flash', 'set', '1', '--ot', '<PLAYER>', '--trainer-id', '0')
    edit('statue', directory / 'vp_all.flash', 'set', '3', '--ot', '<PLAYER>', '--trainer-id', '0')
    edit('statue', default, 'set', 'all', '-o', directory / 'st.flash')
    edit('statue', directory / 'st.flash', 'clear', '151', '-o', directory / 'st150.flash')
    edit('options', directory / 'st.flash', '--bonus', 'MEWTWO', '-o', directory / 'st_bonus.flash')


def case_fingerprint(case, args):
    route = Path(getattr(args, 'route_dir', Path(args.out).resolve() / 'battle-route')) / 'port.rec'
    inputs = [args.rom, case['rec'] or str(route)]
    inputs += [case['extra'][i + 1] for i, value in enumerate(case['extra']) if value in ('--flash', '--call-file')]
    for i, value in enumerate(case['extra']):
        if value in ('--gb', '--gb2'):
            inputs.extend(cartridge_paths(case['extra'][i + 1]))
    fingerprint = hashlib.sha256((json.dumps(case, sort_keys=True) + args.binary_hash).encode())
    for source in inputs:
        with open(source, 'rb') as f:
            fingerprint.update(hashlib.file_digest(f, 'sha256').digest())
    return fingerprint.hexdigest()


def run_case(case, args):
    if case.get('derived_flash'):
        from whole_game_catalog_windows import materialize
        materialize(case)
    out = Path(args.out).resolve() / args.backend / case['name']
    out.mkdir(parents=True, exist_ok=True)
    key = case_fingerprint(case, args)
    result_path = out / 'result.json'
    if result_path.exists():
        cached = json.loads(result_path.read_text(encoding='utf-8'))
        if cached.get('spec') == key and cached.get('status') == 'COMPLETE':
            return cached
    # An interrupted new build must not inherit the previous build's diagnostics or profile.
    for artifact in ('coverage.profraw', 'coverage.json', 'calls.txt', 'seq.txt', 'run_status.json', 'port.trace'):
        (out / artifact).unlink(missing_ok=True)
    command = [sys.executable, '-X', 'utf8', '-I', str(ROOT / 'tools/evidence/run.py'), str(out),
               '--bin', args.bin, '--seq-max', '2', '--dump-every', '300', '--frames', str(case['frames'])]
    route = Path(getattr(args, 'route_dir', Path(args.out).resolve() / 'battle-route')) / 'port.rec'
    command += ['--rec', case['rec'] or str(route)]
    command += case['extra']
    env = dict(os.environ, ROM=args.rom, PORT_TEST_BACKEND=args.backend)
    env.update(case.get('env', {}))
    if case['name'] == 'prenmi':
        env['PORT_FREEZE_CHECK'] = '0'  # The fully faded reset screen is intentionally static.
    if case['name'].startswith('audio_'):
        env['PORT_GAME_AUDIO'] = '1'
    start = time.monotonic()
    result = dict(name=case['name'], backend=args.backend, binary_sha256=args.binary_hash, spec=key, source=case['source'], command=command)
    try:
        with open(out / 'runner.log', 'w', encoding='utf-8') as log:
            proc = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  timeout=max(360, case['frames'] / 30 + 30), **process_options())
        status = json.loads((out / 'run_status.json').read_text()) if (out / 'run_status.json').exists() else {}
        coverage = json.loads((out / 'coverage.json').read_text()) if (out / 'coverage.json').exists() else {}
        functions = coverage.get('functions', {})
        missing = sorted(set(case['expect']) - set(functions))
        missing_prefixes = [prefix for prefix in case.get('expect_prefixes', [])
                            if not any(name.startswith(prefix) for name in functions)]
        missing_edges = [f'{parent} -> {child}' for parent, child in case.get('expect_edges', ())
                         if not any(site[0] == parent and site[2] > 0 for site in functions.get(child, {}).get('sites', []))]
        insufficient_calls = {name: {'expected': count, 'actual': functions.get(name, {}).get('count', 0)}
                              for name, count in case.get('min_calls', {}).items()
                              if functions.get(name, {}).get('count', 0) < count}
        result.update(exit_code=proc.returncode, game_exit_code=status.get('exit_code'),
                      actual_backend=status.get('actual_backend'), functions=len(coverage.get('functions', {})),
                      missing_expected=missing, missing_prefixes=missing_prefixes, missing_edges=missing_edges,
                      insufficient_calls=insufficient_calls, profile=(out / 'coverage.profraw').exists())
        result['status'] = ('COMPLETE' if proc.returncode == 0 and status.get('exit_code') == 0 and
                            coverage.get('functions') and result['profile'] else 'ERROR')
        if (missing or missing_prefixes or missing_edges or insufficient_calls) and result['status'] == 'COMPLETE':
            result['status'] = 'NOT_REACHED'
    except subprocess.TimeoutExpired:
        result['status'] = 'TIMEOUT'
    except Exception as error:
        result.update(status='ERROR', error=str(error))
    result['seconds'] = round(time.monotonic() - start, 1)
    result_path.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('%s %s: %s, %s functions (%.1f s)' % (args.backend, case['name'], result['status'],
          result.get('functions', 0), result['seconds']), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', required=True)
    parser.add_argument('--rom', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--backend', choices=['directx', 'opengl', 'vulkan'], default='directx')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--case')
    parser.add_argument('--plan', action='store_true')
    parser.add_argument('--gb-rom', help='include the full stored-route catalog using this German Yellow cartridge')
    args = parser.parse_args()
    args.bin, args.rom = str(Path(args.bin).resolve()), str(Path(args.rom).resolve())
    output = Path(args.out).resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixtures = output / 'fixtures' / args.backend
    cases = recipes(fixtures)
    if args.gb_rom:
        from whole_game_catalog_windows import recipes as cartridge_recipes, ordered
        full_fixtures = output / 'cartridge-fixtures' / args.backend
        cases = ordered(cases + cartridge_recipes(full_fixtures, output, args.backend))
    covered_recordings = {Path(c['rec']).resolve() for c in cases if c['rec']}
    inventory = [dict(recording=str(p.relative_to(ROOT)), status='RECIPE_READY' if p.resolve() in covered_recordings
                      else 'REQUIRES_FIXTURES_OR_RECIPE_PORT') for p in sorted((ROOT / 'evidence/whole_game/routes').rglob('*.rec'))]
    backend_output = output / args.backend
    backend_output.mkdir(parents=True, exist_ok=True)
    (backend_output / 'plan.json').write_text(json.dumps(dict(cases=cases, recordings=inventory), indent=2), encoding='utf-8')
    if args.plan:
        print('%d source-documented scenarios; %d stored route recordings inventoried' % (len(cases), len(inventory)))
        return
    if args.case and not any(c['name'] == args.case for c in cases):
        parser.error('unknown case: ' + args.case)
    prepare(fixtures)
    if args.gb_rom:
        from whole_game_catalog_windows import prepare as prepare_cartridge
        prepare_cartridge(full_fixtures, args.gb_rom)
    route = output / 'battle-route' / args.backend
    route.mkdir(parents=True, exist_ok=True)
    args.route_dir = str(route)
    bx.write_inputs(str(route), 24000)
    args.binary_hash = hashlib.sha256(Path(args.bin).read_bytes()).hexdigest()
    selected = [c for c in cases if not args.case or c['name'] == args.case]
    # Dependency cases consume FlashRAM written by their prerequisite run. Queue
    # them only after that run completes, including when --jobs exceeds one.
    if args.case:
        required = set()
        by_name = {case['name']: case for case in cases}
        def require(case):
            for name in case.get('depends_on', []):
                require(by_name[name])
                required.add(name)
        require(selected[0])
        selected = [c for c in cases if c['name'] in required or c['name'] == args.case]
    pending = list(selected)
    results, completed, running = [], set(), {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while pending or running:
            for case in list(pending):
                if len(running) >= args.jobs:
                    break
                if set(case.get('depends_on', [])) <= completed:
                    running[pool.submit(run_case, case, args)] = case
                    pending.remove(case)
            if not running:
                raise ValueError('unresolved whole-game dependencies')
            ready, _ = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in ready:
                case = running.pop(future)
                result = future.result()
                results.append(result)
                if result['status'] == 'COMPLETE':
                    completed.add(case['name'])
                else:
                    pending.clear()  # preserve other in-flight evidence; stop launching after a failure
    (output / args.backend / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    if any(r['status'] != 'COMPLETE' for r in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()

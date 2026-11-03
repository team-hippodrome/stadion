"""Check portable launcher configuration and imported-save loading from unrelated working directories.

Usage: python -X utf8 -I tools/windows_launcher_regressions.py --bin EXE --rom ROM --out DIR
The executable and cartridge-save fixtures are copied into isolated folders; source files are not modified.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from runner_platform import actual_backend, BACKENDS, process_options


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--case', action='append', help='Run only this named check (may be repeated)')
    args = parser.parse_args()
    args.bin, args.rom, args.out = args.bin.resolve(), args.rom.resolve(), args.out.resolve()
    fixture = ROOT / 'tools/save_editor/fixtures/first_save.flash'
    originals = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.bin, args.rom, fixture)}
    args.out.mkdir(parents=True, exist_ok=True)

    def run(name, backend=None, home_override=False, relocated=False, unicode=False):
        case = args.out / name
        case.mkdir(parents=True, exist_ok=True)
        app = case / ('런처 Pokémon' if unicode else 'launcher folder')
        app.mkdir(exist_ok=True)
        shutil.copy2(args.bin, app / 'pokestadium.exe')
        working = case / 'unrelated cwd'
        working.mkdir(exist_ok=True)
        decoy = working / 'pokestadium.cfg.json'
        decoy.write_text(json.dumps({'Decoy': name}), encoding='utf-8')
        decoy_bytes = decoy.read_bytes()
        home = (working / 'relative home' if home_override == 'relative' else case / 'override home') if home_override else app
        home.mkdir(exist_ok=True)
        config = home / 'pokestadium.cfg.json'
        if backend:
            state = dict(Window=dict(Backend=dict(Id=BACKENDS[backend][0], Name=BACKENDS[backend][1])),
                         CVars=dict(gLauncher=dict(Debug=1, AutoPlay=int(relocated), Rom=str(args.rom),
                                                  Save='saves/imported.flash')))
            config.write_text(json.dumps(state), encoding='utf-8')
        if relocated:
            (home / 'saves').mkdir(exist_ok=True)
            shutil.copy2(fixture, home / 'saves/imported.flash')
            # Copying the folder models moving a portable installation without touching the original.
            moved = case / 'moved launcher'
            shutil.copytree(app, moved)
            app = home = moved
            config = home / 'pokestadium.cfg.json'
        env = {key: value for key, value in os.environ.items()
               if key != 'SHIP_HOME' and not key.startswith(('PORT_', 'XCHECK_', 'ARES_REF_'))}
        env.update(PORT_HIDDEN_WINDOW='1', PORT_SPEED='max')
        if home_override:
            env['SHIP_HOME'] = os.path.relpath(home, working) if home_override == 'relative' else str(home)
        if relocated:
            env.update(PORT_STOP_AT='2400', PORT_TRACE_FLASH='1', PORT_SHOT_AT='2400',
                       PORT_SCREENSHOT_DIR=str(case), PORT_SCREENSHOT_NOSTOP='1', PORT_SCREENSHOT_MAX_MB='1')
        else:
            env['PORT_LAUNCHER_SHOT'] = str(case / 'launcher.ppm')
        with (case / 'run.log').open('w', encoding='utf-8') as log:
            result = subprocess.run([str(app / 'pokestadium.exe')], cwd=working, env=env,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=180, **process_options())
        row = dict(case=name, backend=backend, exit_code=result.returncode, status='ERROR')
        try:
            assert result.returncode == 0, 'process failed'
            assert config.is_file(), 'config was not generated beside launcher/in explicit SHIP_HOME'
            saved = json.loads(config.read_text(encoding='utf-8'))
            assert 'Decoy' not in saved and decoy.read_bytes() == decoy_bytes, 'working-directory config was used'
            if backend:
                assert actual_backend(str(home)) == BACKENDS[backend][0], 'requested backend not active'
                assert saved['CVars']['gLauncher']['Debug'] == 1, 'existing launcher setting was lost'
            if relocated:
                log = (case / 'run.log').read_text(encoding='utf-8', errors='replace')
                assert '[stop] reached frame 2400' in log, 'saved ROM did not auto-play'
                expected = str(home / 'saves/imported.flash')
                assert '[flash] loaded 131072 bytes from ' + expected in log, 'imported save was not loaded'
                assert (home / 'saves/imported.flash').read_bytes() == fixture.read_bytes(), 'unexpected save mutation'
            else:
                assert (case / 'launcher.ppm').is_file(), 'launcher was not rendered'
                from PIL import Image, ImageStat
                image = Image.open(case / 'launcher.ppm')
                assert min(image.size) > 0 and max(ImageStat.Stat(image).stddev) > 5, 'launcher capture is blank'
            if home_override:
                assert not (app / 'pokestadium.cfg.json').exists(), 'explicit data-folder override ignored'
            if unicode:
                assert {p.name for p in case.iterdir() if p.is_dir()} == {app.name, working.name}, 'garbled data folder created'
            row['status'] = 'PASS'
        except (AssertionError, KeyError, ValueError) as error:
            row['detail'] = str(error)
        (case / 'result.json').write_text(json.dumps(row, indent=2), encoding='utf-8')
        print(row, flush=True)
        return row

    jobs = [('fresh', None, False, False, False), ('unicode-fresh', None, False, False, True),
            ('explicit-home', 'opengl', True, False, False), ('relative-home', 'opengl', 'relative', False, False)]
    for backend in ('opengl', 'vulkan', 'directx'):
        jobs += [(backend + '-settings', backend, False, False, False),
                 (backend + '-relocated-save', backend, False, True, False)]
    if args.case:
        unknown = set(args.case) - {job[0] for job in jobs}
        if unknown:
            parser.error('Unknown check: ' + ', '.join(sorted(unknown)))
        jobs = [job for job in jobs if job[0] in args.case]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run, *job) for job in jobs]
        results = [future.result() for future in concurrent.futures.as_completed(futures)]
    unchanged = all(hashlib.sha256(p.read_bytes()).hexdigest() == sha for p, sha in originals.items())
    summary = dict(inputs_unchanged=unchanged, results=results)
    (args.out / 'results.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    return int(not unchanged or any(row['status'] != 'PASS' for row in results))


if __name__ == '__main__':
    raise SystemExit(main())

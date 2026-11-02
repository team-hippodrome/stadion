"""Record Windows menu return and GB Tower regressions with isolated settings/saves."""
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
from runner_platform import actual_backend, backend_home, BACKENDS, process_options


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bin', required=True, type=Path)
    ap.add_argument('--rom', required=True, type=Path)
    ap.add_argument('--gb-rom', type=Path)
    ap.add_argument('--gb-save', type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--backends', nargs='+', choices=BACKENDS, default=list(BACKENDS))
    ap.add_argument('--jobs', type=int, default=2)
    a = ap.parse_args()
    if bool(a.gb_rom) != bool(a.gb_save):
        ap.error('--gb-rom and --gb-save must be supplied together')
    a.bin, a.rom, a.out = a.bin.resolve(), a.rom.resolve(), a.out.resolve()
    assets = [a.bin, a.rom] + ([a.gb_rom.resolve(), a.gb_save.resolve()] if a.gb_rom else [])
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in assets}
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / 'inputs.json').write_text(json.dumps(hashes, indent=2))

    def run(case, backend):
        d = a.out / backend / case
        d.mkdir(parents=True, exist_ok=True)
        # Follow the real title/menu route: direct PORT_BOOT skips state needed to reproduce the return bug.
        cutoff = 3459 if case == 'free_battle_return' else 2488
        lines = [line for line in (ROOT / 'replays/tour_fb.rec').read_text().splitlines()
                 if not line.startswith('# end') and (line.startswith('#') or int(line.split()[0]) < cutoff)]
        events = [(3600, 16384), (3610, 0)] if case == 'free_battle_return' else [
            (2652, 32768), (2662, 0), (3459, 32768), (3469, 0),
            (5400, 4096), (5410, 0), (5800, 32768), (5810, 0), (6200, 32768), (6210, 0), (6600, 1024), (6660, 0), (6800, 256), (6860, 0)]
        stop = 4400 if case == 'free_battle_return' else 7200
        lines.extend(f'{frame} {buttons} 0 0 0 0 0 0 0 0 0 0 0' for frame, buttons in events)
        rec = d / 'input.rec'
        rec.write_text('\n'.join(lines) + f'\n# end {stop}\n')
        env = os.environ.copy()
        # Keep each invocation independent even if the caller has debug options set.
        for key in list(env):
            if key.startswith(('PORT_', 'XCHECK_')):
                env.pop(key)
        backend_home(env, str(d), backend)
        env.update(PORT_REPLAY=str(rec), PORT_SPEED='max', PORT_STOP_AT=str(stop),
                   PORT_SHOT_AT='3200,4200,4400' if case == 'free_battle_return' else '3500,5300,6500,7200',
                   PORT_SCREENSHOT_DIR=str(d), PORT_SCREENSHOT_MAX_MB='4', PORT_SCREENSHOT_NOSTOP='1', PORT_HIDDEN_WINDOW='1',
                   PORT_TRACE_FRAGMENTS='1', PORT_TRACE_GBPAK='1', PORT_FLASH_FILE=str(d / 'test.flash'))
        if case == 'gb_tower':
            shutil.copyfile(a.gb_save, d / 'test.sav')
            shutil.copyfile(ROOT / 'replays/gbtower_quit.flash', d / 'test.flash')
            env.update(PORT_GB_ROM=str(a.gb_rom.resolve()), PORT_GB_SAVE=str(d / 'test.sav'))
        else:
            (d / 'test.flash').unlink(missing_ok=True)
        result = dict(case=case, backend=backend, binary_sha256=hashes[str(a.bin)], status='ERROR')
        try:
            with (d / 'run.log').open('w') as log:
                p = subprocess.run([str(a.bin), str(a.rom)], cwd=d, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, timeout=600, **process_options())
            result.update(exit_code=p.returncode, actual_backend=actual_backend(d))
            log = (d / 'run.log').read_text(errors='replace')
            result['status'] = ('CAPTURED_REQUIRES_VISUAL_REVIEW' if p.returncode == 0
                                and result['actual_backend'] == BACKENDS[backend][0]
                                and '[stop] reached frame' in log else 'ERROR')
        except subprocess.TimeoutExpired:
            result['status'] = 'TIMEOUT'
        # A successful process alone is not a functional pass: retain screenshots for explicit review.
        try:
            from PIL import Image
            for p in d.glob('stop*.ppm'):
                Image.open(p).save(p.with_suffix('.png'))
        except ImportError:
            pass
        (d / 'result.json').write_text(json.dumps(result, indent=2))
        print(f'{backend} {case}: {result["status"]}', flush=True)
        return result

    cases = ['free_battle_return'] + (['gb_tower'] if a.gb_rom else [])
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futures = [pool.submit(run, case, backend) for backend in a.backends for case in cases]
        results = [f.result() for f in concurrent.futures.as_completed(futures)]
    unchanged = all(hashlib.sha256(p.read_bytes()).hexdigest() == hashes[str(p)] for p in assets)
    (a.out / 'results.json').write_text(json.dumps(dict(inputs_unchanged=unchanged, results=results), indent=2))
    return int(not unchanged or any(r['status'] in ('ERROR', 'TIMEOUT') for r in results))


if __name__ == '__main__':
    raise SystemExit(main())

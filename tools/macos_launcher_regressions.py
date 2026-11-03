"""ROM-free AppKit launcher checks: fresh/existing config, bundle sidecar and explicit override."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Run these AppKit checks on macOS')
    args.bin, args.out = args.bin.resolve(), args.out.resolve()
    rows = []
    for name, bundle, existing, override in [('bare-fresh', False, False, False),
                                              ('bundle-fresh', True, False, False),
                                              ('bundle-existing', True, True, False),
                                              ('explicit-home', True, False, True)]:
        case = args.out / name
        launcher = case / 'launcher folder'
        launcher.mkdir(parents=True, exist_ok=True)
        working = case / 'unrelated cwd'
        working.mkdir(exist_ok=True)
        decoy = working / 'pokestadium.cfg.json'
        decoy.write_text('{"decoy": true}', encoding='utf-8')
        if bundle:
            app = launcher / 'Pokemon Stadium.app'
            executable = app / 'Contents/MacOS/pokestadium'
            resources = app / 'Contents/Resources'
            resources.mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({
                'CFBundleExecutable': 'pokestadium', 'CFBundlePackageType': 'APPL',
                'CFBundleIdentifier': 'tests.pokestadium.' + uuid.uuid4().hex,
                'NSHighResolutionCapable': True}))
            (resources / 'project-path.txt').write_text(str(ROOT), encoding='utf-8')
        else:
            executable = launcher / 'pokestadium'
        executable.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.bin, executable)
        home = case / 'custom data' if override else launcher
        home.mkdir(exist_ok=True)
        config = home / 'pokestadium.cfg.json'
        if existing:
            config.write_text(json.dumps({'Window': {'Backend': {'Id': 3}},
                'CVars': {'gLauncher': {'Rom': '', 'Save': 'saves/existing.flash', 'Debug': 1, 'AutoPlay': 0},
                          'gControllers': {'Existing': 1}}}), encoding='utf-8')
        env = {k: v for k, v in os.environ.items() if k != 'SHIP_HOME' and not k.startswith('LAUNCHER_')}
        env['LAUNCHER_SNAPSHOT'] = str(case / 'launcher.png')
        if override:
            env['SHIP_HOME'] = str(home)
        with (case / 'run.log').open('w') as log:
            process = subprocess.run([str(executable)], cwd=working, env=env, stdout=log,
                                     stderr=subprocess.STDOUT, timeout=45)
        assert process.returncode == 0, name + ': launcher failed'
        png = (case / 'launcher.png').read_bytes()
        assert png[:8] == b'\x89PNG\r\n\x1a\n' and min(struct.unpack('>II', png[16:24])) > 0, name + ': no launcher snapshot'
        assert len(png) > 1024, name + ': empty launcher snapshot'
        saved = json.loads(config.read_text(encoding='utf-8'))
        assert 'decoy' not in saved and decoy.read_text() == '{"decoy": true}', name + ': cwd config was used'
        if bundle:
            assert not (executable.parent / 'pokestadium.cfg.json').exists(), name + ': config inside bundle'
        if existing:
            assert saved['CVars']['gLauncher']['Debug'] == 1 and saved['Window']['Backend']['Id'] == 3
            assert saved['CVars']['gControllers']['Existing'] == 1
        if override:
            assert not (launcher / 'pokestadium.cfg.json').exists(), name + ': override ignored'
        row = dict(case=name, status='PASS', snapshot=str(case / 'launcher.png'))
        rows.append(row)
        print(row, flush=True)
    (args.out / 'results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Check RDRAM-to-render-target copies through the castle's defeated-trainer path.

Requires a build instrumenting game_state.c and battle_prep_ (cmake/evidence.cmake).
The test advances the castle's trainer-card display to round two to exercise the red X
without playing a complete battle. All saves and renderer settings are isolated under OUT.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out', type=Path)
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--backend', required=True, choices=('opengl', 'metal', 'vulkan', 'directx'))
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PORT_TEST_BACKEND=args.backend, PORT_CHECK_RTT_INIT='1',
               PORT_RENDER_EVERY='1', PORT_FREEZE_CHECK='0')
    command = [sys.executable, str(ROOT / 'tools/evidence/run.py'), str(output),
               '--bin', str(args.bin.resolve()), '--natural', '--frames', '9600',
               '--rec', str(ROOT / 'evidence/whole_game/routes/registration/castle_rental_entry.rec'),
               '--set', 'State_TitleScreen#1@exit:gCurrentGameState:4=39;'
                        'BattlePrep_LoadBadgeTextures:gSessionContext+3:1=2',
               '--shots', '9000,9100,9200,9300,9400']
    completed = subprocess.run(command, env=env, check=False)
    if completed.returncode:
        return completed.returncode
    log = (output / 'port.log').read_text(errors='replace')
    checks = re.findall(r'\[rtt-init-check\] (\d+)x(\d+): (\d+) mismatches of (\d+) pixels', log)
    reached = any(w == '64' and h == '64' for w, h, _, _ in checks)
    passed = reached and all(mismatches == '0' for _, _, mismatches, _ in checks)
    result = dict(backend=args.backend, portrait_reached=reached, passed=passed, checks=checks)
    (output / 'rtt_results.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Render-target initialization checks ' + ('passed.' if passed else 'failed; see rtt_results.json.'))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())

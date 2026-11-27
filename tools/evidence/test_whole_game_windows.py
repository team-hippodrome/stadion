"""Full route inventory, frame bounds, and cartridge-sensitive cache checks."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
import json
import subprocess

sys.path.insert(0, str(Path(__file__).resolve().parent))
import whole_game_windows as whole
import whole_game_catalog_windows as catalog


class WholeGameCatalogTests(unittest.TestCase):
    def test_documented_isolated_cli_can_plan_the_full_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, '-X', 'utf8', '-I', str(Path(whole.__file__)),
                                     '--bin', 'unused.exe', '--rom', 'unused.z64', '--gb-rom', 'unused.gb',
                                     '--out', directory, '--plan'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(Path(directory, 'directx/plan.json').read_text())
            self.assertTrue(all(r['status'] == 'RECIPE_READY' for r in plan['recordings']))

    def test_every_stored_recording_has_a_recipe_and_keeps_all_inputs(self):
        fixtures = Path('fixtures').resolve()
        cases = whole.recipes(fixtures) + catalog.recipes(fixtures)
        self.assertEqual(len({c['name'] for c in cases}), len(cases))
        recordings = {p.resolve() for p in catalog.ROUTES.rglob('*.rec')}
        self.assertTrue(recordings <= {Path(c['rec']).resolve() for c in cases if c['rec']})
        for case in cases:
            if not case['rec']:
                continue
            if Path(case['rec']).name == 'idle.rec':
                continue  # short diagnostic probes intentionally stop before idle's late exit button
            if case['name'] == 'champion_banner_probe_round':
                continue  # the diagnostic ends after drawing the Champion banner, before the recorded battle
            frames = [int(line.split()[0]) for line in Path(case['rec']).read_text().splitlines()
                      if line.strip() and not line.startswith('#')]
            with self.subTest(case=case['name']):
                self.assertGreaterEqual(case['frames'], max(frames, default=0))

    def test_champion_diagnostic_selects_and_draws_the_last_castle_banner(self):
        cases = catalog.recipes(Path('fixtures').resolve())
        case = next(c for c in cases if c['name'] == 'champion_banner_probe_round')
        self.assertEqual(case['frames'], 9000)
        self.assertIn('BattlePrepBanner_LoadTextures:gSessionContext+0x2:1=9', case['extra'][case['extra'].index('--set') + 1])
        self.assertIn('BattlePrepBanner_DrawTitle', case['expect'])

    def test_album_dependencies_precede_readers(self):
        cases = catalog.ordered(catalog.recipes(Path('fixtures').resolve()))
        done = set()
        for case in cases:
            self.assertTrue(set(case.get('depends_on', [])) <= done)
            done.add(case['name'])
        with self.assertRaises(ValueError):
            catalog.ordered([dict(name='reader', frames=1, depends_on=['missing'])])
        targets = [case['derived_flash']['target'] for case in cases if case.get('derived_flash')]
        self.assertEqual(len(set(targets)), len(targets))

    def test_changes_to_either_cartridge_invalidate_a_cached_route(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ['stadium.z64', 'recording.rec', 'yellow.gb', 'one.sav', 'two.sav']
            for name in names:
                (root / name).write_bytes(b'original')
            args = SimpleNamespace(rom=str(root / names[0]), out=str(root), binary_hash='binary')
            case = dict(rec=str(root / names[1]), extra=['--gb', str(root / names[2]) + '|' + str(root / names[3]),
                                                        '--gb2', str(root / names[2]) + '|' + str(root / names[4])])
            original = whole.case_fingerprint(case, args)
            for name in names:
                with self.subTest(input=name):
                    (root / name).write_bytes(b'changed')
                    self.assertNotEqual(whole.case_fingerprint(case, args), original)
                    (root / name).write_bytes(b'original')


if __name__ == '__main__':
    unittest.main()

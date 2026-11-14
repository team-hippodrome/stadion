"""The default sweep must exercise every move, including the no-PP fallback."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import battle_sweep as sweep


class SweepPlanTests(unittest.TestCase):
    def test_base_plan_covers_every_move_and_successful_healing(self):
        moves, species, chart = sweep.decomp_tables()
        cases, skipped = sweep.plan(moves, species, chart, only_path='base')
        uses = [use['move'] for case in cases if case['path'] != 'injured' for use in case['uses']]
        self.assertEqual(set(uses), set(moves))
        self.assertEqual(len(uses), len(moves))
        self.assertEqual(skipped, [])
        struggle = [case for case in cases if case['path'] == 'struggle']
        self.assertEqual(len(struggle), 1)
        self.assertTrue(struggle[0]['pp0'])
        injured = [case for case in cases if case['path'] == 'injured']
        self.assertEqual({case['uses'][0]['move'] for case in injured}, {105, 135, 156})
        for case in injured:
            self.assertEqual(len(case['uses']), 1)
            self.assertLess(case['user']['hp'], 300)
            self.assertTrue(case['uses'][0]['require_animation'])
        self.assertTrue(any(case['id'] == '104-105-106-107_base_597871bb' for case in cases))

    def test_required_healing_animation_cannot_pass_without_capture(self):
        self.assertTrue(sweep.classify({'used': True, 'require_animation': True}, 0).startswith('animation mismatch'))
        for score in ({}, {'frames': [0, 0]}, {'frames': [3, 3]}):
            use = {'used': True, 'require_animation': True, 'anim': {'port': {}, 'ares': {}}, 'score': score}
            self.assertTrue(sweep.classify(use, 0).startswith('animation mismatch'))
        self.assertEqual(sweep.classify({'used': True}, 0), 'match (no animation)')

    def test_bide_and_dream_eater_base_setups_can_play_their_animations(self):
        moves, species, chart = sweep.decomp_tables()
        cases, _ = sweep.plan(moves, species, chart, only_path='base')
        bide = next(c for c in cases if c['uses'][0]['move'] == 117)
        self.assertGreaterEqual(bide['pp'], bide['turns'])
        self.assertIn('"pp":', bide['spec'])
        self.assertIn('p1=5 ', sweep.case_file(bide, species))
        dream = next(c for c in cases if c['uses'][0]['move'] == 138)
        self.assertGreater(dream['target']['status'] & 7, 0)
        for case in (bide, dream):
            self.assertEqual(len(case['uses']), 1)
            self.assertTrue(case['uses'][0]['require_animation'])

    def test_copied_moves_capture_the_animation_the_game_actually_plays(self):
        moves, species, chart = sweep.decomp_tables()
        cases, _ = sweep.plan(moves, species, chart, only_path='base')
        mirror = next(c for c in cases if c['uses'][0]['move'] == 119)
        self.assertEqual(mirror['target']['moves'], [33])
        self.assertGreater(mirror['target']['spd'], 150)
        self.assertEqual(mirror['uses'][0]['animation_move'], 33)
        metronome = next(c for c in cases if c['uses'][0]['move'] == 118)
        self.assertEqual(metronome['rules']['Battle_Random@Battle_Effect_Metronome'], [85])
        self.assertEqual(metronome['uses'][0]['animation_move'], 85)
        for case in (mirror, metronome):
            self.assertIn('"animation_moves":', case['spec'])
            self.assertTrue(case['uses'][0]['require_animation'])

    def test_explicit_move_and_generic_path_filters_are_preserved(self):
        moves, species, chart = sweep.decomp_tables()
        for move in (1, 33, 165):
            cases, _ = sweep.plan(moves, species, chart, only_move=move, only_path='base')
            self.assertEqual([u['move'] for c in cases for u in c['uses']], [move])
        cases, _ = sweep.plan(moves, species, chart, only_path='user_paralyzed')
        self.assertTrue(cases)
        self.assertEqual({c['path'] for c in cases}, {'user_paralyzed'})


if __name__ == '__main__':
    unittest.main()

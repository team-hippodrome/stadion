"""The default sweep must exercise every move, including the no-PP fallback."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import battle_sweep as sweep


class SweepPlanTests(unittest.TestCase):
    def test_base_plan_covers_every_move_once(self):
        moves, species, chart = sweep.decomp_tables()
        cases, skipped = sweep.plan(moves, species, chart, only_path='base')
        uses = [use['move'] for case in cases for use in case['uses']]
        self.assertEqual(set(uses), set(moves))
        self.assertEqual(len(uses), len(moves))
        self.assertEqual(skipped, [])
        struggle = [case for case in cases if case['path'] == 'struggle']
        self.assertEqual(len(struggle), 1)
        self.assertTrue(struggle[0]['pp0'])

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

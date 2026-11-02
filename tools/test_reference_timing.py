"""Regression checks for comparison timing and unchanged audio failure detection."""
import array
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import battle_sweep as sweep


class ReferenceTimingTests(unittest.TestCase):
    def test_retrace_gaps_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {}, clear=True):
            Path(directory, 'ares.trace').write_text('V 1 100\nV 2 102\nV 3 108\n')
            schedule = sweep.reference_vi_schedule(directory)
            self.assertEqual(Path(schedule).read_text(), '1 100\n2 102\n3 108\n')

    def test_missing_or_discontinuous_timing_is_rejected(self):
        for trace in ('', 'V 1 100\nV 3 104\n', 'V 1 100\nV 2 99\n', 'V 1 -1\n', 'V 1 100\nV 2\n'):
            with self.subTest(trace=trace), tempfile.TemporaryDirectory() as directory:
                Path(directory, 'ares.trace').write_text(trace)
                with patch.dict(os.environ, {}, clear=True), self.assertRaises(ValueError):
                    sweep.reference_vi_schedule(directory)

    def test_native_speed_results_do_not_satisfy_reference_paced_cache(self):
        case = {'spec': 'case'}
        result = {'spec': 'case', 'timing_mode': 'native'}
        with patch.dict(os.environ, {}, clear=True):
            self.assertFalse(sweep.result_matches_build(result, case, None, '.'))
        with patch.dict(os.environ, {'XCHECK_REFERENCE_TIMING': '0'}, clear=True):
            self.assertTrue(sweep.result_matches_build(result, case, None, '.'))

    def test_matching_audio_passes_and_missing_audio_fails(self):
        def pcm(rate, damaged=False):
            data = array.array('h')
            for i in range(rate * 3):
                t = i / rate
                value = (0.1 + 0.04 * math.sin(2 * math.pi * 1.7 * t)) * (
                    math.sin(2 * math.pi * 250 * t) + 0.2 * math.sin(2 * math.pi * 1000 * t))
                sample = 0 if damaged and t > 1.5 else int(value * 32767)
                data.extend((sample, sample))
            return data

        reference = pcm(48000)
        index = array.array('I', (i * 800 for i in range(181)))
        port_index = [(i + 2, round(i * 8000 / 60)) for i in range(181)]
        for damaged, expected in ((False, 'sound match'), (True, 'sound mismatch')):
            with self.subTest(damaged=damaged):
                audio = {'port': (pcm(8000, damaged), 8000, port_index),
                         'ares': (reference, 48000, index)}
                result = sweep.score_audio('', audio, 2, 182, 2, 182, 75)
                self.assertTrue(sweep.audio_verdict(result).startswith(expected), result)


if __name__ == '__main__':
    unittest.main()

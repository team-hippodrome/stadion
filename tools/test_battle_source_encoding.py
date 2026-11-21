"""Battle source tables must parse independently of Windows' default code page."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


class BattleSourceEncodingTests(unittest.TestCase):
    def test_tables_and_names_with_utf8_mode_disabled(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # The Japanese comma includes byte 0x81, which is undefined in cp1252.
            comment = '// UTF-8 comment: Pok\u00e9mon\u3001\u65e5\u672c\u8a9e\n'
            fixtures = {
                'game/src/gb_data.c': (
                    'gMoveDataWithZeroth[166] = {\n{1, 0, 40, 0, 255, 35}\n};\n'
                    'SpeciesData gSpeciesData[152] = {\n'
                    '{1, 45, 49, 49, 45, 65, 0, 0, 0, 0, {33, 0, 0, 0}}\n};\n'),
                'game/src/fragments/battle_engine/battle_engine_mechanics.c':
                    'u8 sTypeEffects[] = {0, 0, 0x14, 0xFF, 0, 0};\n',
                'game/include/pokemon.h': '#define BULBASAUR 1\n',
                'game/include/moves.h': '#define POUND 1\n',
            }
            for name, content in fixtures.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(comment + content, encoding='utf-8')
            with self.assertRaises(UnicodeDecodeError):
                comment.encode('utf-8').decode('cp1252')
            code = textwrap.dedent('''
                import codecs, json, locale, os, pathlib, sys
                assert sys.flags.utf8_mode == 0
                if os.name != 'nt':
                    locale.setlocale(locale.LC_CTYPE, 'C')
                encoding = codecs.lookup(locale.getencoding()).name
                if encoding == 'utf-8':
                    print(json.dumps({'skip': 'System default is already UTF-8'}))
                    raise SystemExit(0)
                sys.path.insert(0, sys.argv[1])
                import battle_sweep as sweep
                import battle_xcheck as crosscheck
                sweep.ROOT = crosscheck.ROOT = sys.argv[2]
                # Prove that this real process reproduces the old implicit-read failure.
                try:
                    with open(pathlib.Path(sys.argv[2], 'game/src/gb_data.c')) as source:
                        source.read()
                except UnicodeDecodeError:
                    pass
                else:
                    raise AssertionError('Legacy default did not reproduce the decoding failure')
                moves, species, chart = sweep.decomp_tables()
                assert moves[1]['power'] == 40
                assert species[1]['hp'] == 45
                assert chart[(0, 0)] == 2.0
                species_names, move_names = crosscheck.names()
                assert species_names[1] == 'Bulbasaur'
                assert move_names[1] == 'Pound'
                print(json.dumps({'encoding': encoding, 'parsers': 'passed'}))
            ''')
            result = subprocess.run(
                [sys.executable, '-X', 'utf8=0', '-I', '-c', code,
                 str(Path(__file__).resolve().parent), str(root)],
                capture_output=True, text=True, encoding='utf-8', errors='replace')
            self.assertEqual(result.returncode, 0, result.stderr)
            parsed = json.loads(result.stdout)
            if 'skip' in parsed:
                self.skipTest(parsed['skip'])
            self.assertNotEqual(parsed['encoding'], 'utf-8')
            self.assertEqual(parsed['parsers'], 'passed')


if __name__ == '__main__':
    unittest.main()

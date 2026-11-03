"""Exercise the native save importer and portable paths with real cartridge-save fixtures."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LauncherFilesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('A C++17 compiler is needed for native launcher tests')
        cls.build = tempfile.TemporaryDirectory()
        source = Path(cls.build.name) / 'driver.cpp'
        source.write_text('''#include "launcher_files.h"
#include <iostream>
int main(int argc, char** argv) {
    std::string value, directory;
    std::getline(std::cin, value); std::getline(std::cin, directory);
    auto home = std::filesystem::u8path(directory);
    if (std::string(argv[1]) == "paths") {
        std::cout << Port::ResolveLauncherPath(value, home) << "\\n"
                  << Port::StoreLauncherPath(value, home) << "\\n";
        return 0;
    }
    std::string imported, error;
    if (!Port::ImportStadiumSave(std::filesystem::u8path(value), home, imported, error)) {
        std::cerr << error; return 1;
    }
    std::cout << imported;
}
''', encoding='utf-8')
        cls.exe = Path(cls.build.name) / ('driver.exe' if os.name == 'nt' else 'driver')
        subprocess.run([compiler, '-std=c++17', '-I' + str(ROOT / 'src/port'), str(source),
                        str(ROOT / 'src/port/launcher_files.cpp'), '-o', str(cls.exe)], check=True,
                       capture_output=True)
        cls.fixture = (ROOT / 'tools/save_editor/fixtures/first_save.flash').read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def invoke(self, source, home, mode='import'):
        return subprocess.run([str(self.exe), mode], input=str(source) + '\n' + str(home) + '\n',
                              text=True, encoding='utf-8', capture_output=True)

    def test_original_n64_dump_is_copied_without_modification(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'cartridge.fla'
            source.write_bytes(self.fixture)
            result = self.invoke(source, root / 'launcher')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout).read_bytes(), self.fixture)
            self.assertEqual(source.read_bytes(), self.fixture)
            self.assertTrue(Path(result.stdout).is_relative_to(root / 'launcher/saves'))

    def test_emulator_byte_orders_produce_the_same_n64_save(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for order in (1, 2, 3):
                with self.subTest(order=order):
                    swapped = bytes(self.fixture[i ^ order] for i in range(len(self.fixture)))
                    source = root / ('order%d.sra' % order)
                    source.write_bytes(swapped)
                    result = self.invoke(source, root / 'launcher')
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(Path(result.stdout).read_bytes(), self.fixture)
                    self.assertEqual(source.read_bytes(), swapped)

    def test_invalid_inputs_leave_existing_save_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            home = root / 'launcher'
            home.mkdir()
            (home / 'existing.flash').write_bytes(self.fixture)
            for data in (b'not a save state', bytes(32768), bytes(131072), bytes([0xaa]) * 131072):
                source = root / 'bad.sav'
                source.write_bytes(data)
                result = self.invoke(source, home)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(source.read_bytes(), data)
                self.assertEqual((home / 'existing.flash').read_bytes(), self.fixture)
                self.assertFalse((home / 'saves').exists())

    def test_reimport_never_overwrites_an_earlier_working_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'cartridge.flash'
            source.write_bytes(self.fixture)
            first = self.invoke(source, root / 'launcher')
            self.assertEqual(first.returncode, 0, first.stderr)
            Path(first.stdout).write_bytes(b'player made new progress')
            second = self.invoke(source, root / 'launcher')
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertNotEqual(first.stdout, second.stdout)
            self.assertEqual(Path(first.stdout).read_bytes(), b'player made new progress')
            self.assertEqual(Path(second.stdout).read_bytes(), self.fixture)

    def test_unicode_and_spaces_in_import_and_launcher_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'Pokémon 저장.fla'
            source.write_bytes(self.fixture)
            result = self.invoke(source, root / '런처 folder')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout).read_bytes(), self.fixture)

    def test_backup_only_and_erased_cartridges_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            partial = bytearray([0xff]) * 131072
            partial[0x10000:0x10160] = self.fixture[0x10000:0x10160]
            for data in (bytes(partial), bytes([0xff]) * 131072):
                source = root / 'recoverable.fla'
                source.write_bytes(data)
                result = self.invoke(source, root / 'launcher')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(Path(result.stdout).read_bytes(), data)

    def test_local_paths_follow_a_moved_launcher_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            old = root / 'old'
            moved = root / 'moved'
            local = old / 'saves/stadium.flash'
            stored = self.invoke(local, old, 'paths').stdout.splitlines()[1]
            self.assertEqual(stored, 'saves/stadium.flash')
            self.assertFalse(Path(stored).is_absolute())
            resolved = self.invoke(stored, moved, 'paths').stdout.splitlines()[0]
            self.assertEqual(Path(resolved), moved / 'saves/stadium.flash')
            external = root / 'external.fla'
            self.assertEqual(Path(self.invoke(external, old, 'paths').stdout.splitlines()[1]), external)


if __name__ == '__main__':
    unittest.main()

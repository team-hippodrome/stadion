"""Run the shared app-directory policy natively, including packaged and sandboxed launcher layouts."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LauncherDirectoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('A C++17 compiler is needed')
        cls.build = tempfile.TemporaryDirectory()
        source = Path(cls.build.name) / 'directory.cpp'
        source.write_text('''#include <ship/utils/AppDirectory.h>
#include <iostream>
int main() {
    std::string base, overridePath, home, image, sandbox, mac;
    std::getline(std::cin, base); std::getline(std::cin, overridePath); std::getline(std::cin, home);
    std::getline(std::cin, image); std::getline(std::cin, sandbox); std::getline(std::cin, mac);
    try {
        auto path = Ship::ResolveAppDirectory(std::filesystem::u8path(base), overridePath,
            std::filesystem::u8path(home), std::filesystem::u8path(image),
            std::filesystem::u8path(sandbox), mac == "1");
        std::cout << path.u8string();
    } catch (const std::exception& e) { std::cerr << e.what(); return 1; }
}
''', encoding='utf-8')
        cls.exe = Path(cls.build.name) / ('directory.exe' if os.name == 'nt' else 'directory')
        subprocess.run([compiler, '-std=c++17', '-I' + str(ROOT / 'libultraship/include'),
                        str(source), '-o', str(cls.exe)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def resolve(self, base='', override='', home='', image='', sandbox='', mac=False, cwd=None):
        values = [base, override, home, image, sandbox, int(mac)]
        return subprocess.run([str(self.exe)], input='\n'.join(map(str, values)) + '\n', cwd=cwd,
                              text=True, encoding='utf-8', capture_output=True)

    def test_bare_desktop_launcher_ignores_the_working_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app = root / '런처 Pokémon'
            result = self.resolve(base=app, cwd=root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout), app)

    def test_macos_bundle_uses_the_folder_beside_the_signed_app(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for bundle in ('Pokemon Stadium.app', 'Pokemon Stadium.APP'):
                for child in ('Contents/MacOS', 'Contents/Resources'):
                    with self.subTest(bundle=bundle, child=child):
                        result = self.resolve(base=root / bundle / child, mac=True)
                        self.assertEqual(Path(result.stdout), root)

    def test_appimage_uses_original_artifact_not_read_only_mount(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for folder in ('downloads', 'moved folder'):
                image = root / folder / 'Pokémon Stadium.AppImage'
                result = self.resolve(base=root / '.mount_readonly/usr/bin', image=image)
                self.assertEqual(Path(result.stdout), image.parent)

    def test_sandboxed_launcher_uses_its_writable_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = root / 'Android/data/com.teamhippodrome.pokestadium/files'
            result = self.resolve(base=root / 'readonly/apk/lib/arm64', sandbox=files)
            self.assertEqual(Path(result.stdout), files)
            result = self.resolve(sandbox=root / 'iOS/Documents')
            self.assertEqual(Path(result.stdout), root / 'iOS/Documents')

    def test_explicit_override_wins_over_every_packaged_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            selected = root / 'chosen data'
            result = self.resolve(base=root / 'Game.app/Contents/MacOS', mac=True,
                                  image=root / 'Game.AppImage', sandbox=root / 'app-files', override=selected)
            self.assertEqual(Path(result.stdout), selected)

    def test_relative_and_home_overrides_are_normalized(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            result = self.resolve(override='relative/../data', cwd=root)
            self.assertEqual(Path(result.stdout).resolve(), (root / 'data').resolve())
            result = self.resolve(override='~/launcher data', home=root)
            self.assertEqual(Path(result.stdout), root / 'launcher data')

    def test_unknown_executable_location_does_not_silently_use_cwd(self):
        result = self.resolve()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('SHIP_HOME', result.stderr)


if __name__ == '__main__':
    unittest.main()

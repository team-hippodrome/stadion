"""Native integration tests for preflight, protected persistence, exports and diagnostic privacy."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ReliabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('A C++17 compiler is needed')
        cls.build = tempfile.TemporaryDirectory()
        source = Path(cls.build.name) / 'driver.cpp'
        source.write_text(r"""#include "launcher_reliability.h"
#include <iostream>
#include <fstream>
int main(int argc, char** argv) {
    std::string path, directory, second; std::getline(std::cin, path); std::getline(std::cin, directory);
    auto home = std::filesystem::u8path(directory), file = std::filesystem::u8path(path);
    std::string mode = argv[1], output, error;
    if (mode == "write" || mode == "twice" || mode == "gbwrite") {
        std::vector<uint8_t> data(mode == "gbwrite" ? 32768 : 131072, uint8_t(argc > 2 ? atoi(argv[2]) : 7));
        bool ok = Port::WriteProtectedSave(file, data.data(), data.size(), error);
        if (ok && mode == "twice") { data[0] = 42; ok = Port::WriteProtectedSave(file, data.data(), data.size(), error); }
        if (!ok) { std::cerr << error; return 1; } return 0;
    }
    if (mode == "redact") { std::string text((std::istreambuf_iterator<char>(std::cin)), {}); std::cout << Port::RedactDiagnosticLog(text); return 0; }
    if (mode == "export" || mode == "diagnostics") {
        bool ok = mode == "export" ? Port::ExportStadiumSave(file, home, output, error) :
            Port::ExportLauncherDiagnostics(home, "test-build", "Vulkan", "OpenGL", output, error);
        if (!ok) { std::cerr << error; return 1; } std::cout << output; return 0;
    }
    std::array<Port::LauncherSlot, 4> slots;
    for (auto& slot : slots) { std::getline(std::cin, slot.rom); std::getline(std::cin, slot.save); slot.enabled = !slot.rom.empty(); }
    auto check = Port::CheckLauncherFiles(home, path, slots);
    for (const auto& e : check.errors) std::cout << "ERROR " << e << "\n";
    for (const auto& w : check.warnings) std::cout << "WARNING " << w << "\n";
    return check.errors.empty() ? 0 : 1;
}
""", encoding='utf-8')
        cls.exe = Path(cls.build.name) / ('driver.exe' if os.name == 'nt' else 'driver')
        subprocess.run([compiler, '-std=c++17', '-pthread', *(['-static'] if os.name == 'nt' else []), '-I' + str(ROOT / 'src/port'), str(source),
                        str(ROOT / 'src/port/launcher_files.cpp'), str(ROOT / 'src/port/launcher_reliability.cpp'),
                        '-o', str(cls.exe)], check=True, capture_output=True)
        cls.fixture = (ROOT / 'tools/save_editor/fixtures/first_save.flash').read_bytes()

    @classmethod
    def tearDownClass(cls): cls.build.cleanup()

    def invoke(self, mode, path, home, extra='', value=None):
        args = [str(self.exe), mode] + ([str(value)] if value is not None else [])
        return subprocess.run(args, input=str(path)+'\n'+str(home)+'\n'+extra,
                              text=True, encoding='utf-8', capture_output=True, timeout=30)

    def gb_rom(self, path):
        data = bytearray(0x100000)
        data[0x134:0x13f] = b'POKEMON RED'
        data[0x147:0x14a] = bytes([0x13, 5, 3])
        data[0x14d] = (-sum(data[0x134:0x14d])-25) & 255
        path.write_bytes(data)

    def test_preflight_missing_save_is_new_and_does_not_create_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp); save = home/'saves/new.flash'
            result = self.invoke('check', save, home)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertFalse(save.exists())
            self.assertFalse(list(home.rglob('.stadium-write-check-*')))

    def test_preflight_wrong_save_and_blocked_folder_preserve_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp); bad = home/'bad.flash'; bad.write_bytes(b'not a cartridge')
            result = self.invoke('check', bad, home)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('wrong size', result.stdout)
            self.assertEqual(bad.read_bytes(), b'not a cartridge')
            blocked = home/'not-a-folder'; blocked.write_bytes(b'keep')
            result = self.invoke('check', blocked/'save.flash', home)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(blocked.read_bytes(), b'keep')

    def test_preflight_rom_header_save_size_and_shared_slot_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp); rom = home/'red.gb'; self.gb_rom(rom)
            save = home/'stadium.flash'; save.write_bytes(self.fixture)
            gb = home/'red.sav'; gb.write_bytes(bytes(32768))
            result = self.invoke('check', save, home, f'{rom}\n{gb}\n')
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn('checksum', result.stdout)
            result = self.invoke('check', save, home, f'{rom}\n{gb}\n{rom}\n{gb}\n')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('share the same', result.stdout)
            gb.write_bytes(bytes(100))
            self.assertNotEqual(self.invoke('check', save, home, f'{rom}\n{gb}\n').returncode, 0)
            data = bytearray(rom.read_bytes()); data[0x14d] ^= 1; rom.write_bytes(data)
            self.assertIn('header', self.invoke('check', save, home, f'{rom}\n{gb}\n').stdout)

    def test_protected_write_backs_up_once_per_session(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp); save = home/'Pokémon 저장.flash'; save.write_bytes(self.fixture)
            result = self.invoke('twice', save, home)
            self.assertEqual(result.returncode, 0, result.stderr)
            backups = list(Path(str(save)+'.backups').glob('*.bak'))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), self.fixture)
            self.assertEqual(save.read_bytes()[0], 42)
            self.assertFalse(list(home.glob('.stadium-save-*')))

    def test_game_boy_battery_ram_keeps_its_original_size_and_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp); save = home/'battery.sav'; original = bytes([3])*32768
            save.write_bytes(original)
            result = self.invoke('gbwrite', save, home)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(save.read_bytes(), bytes([7])*32768)
            backups = list(Path(str(save)+'.backups').glob('*.bak'))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), original)

    def test_rotates_five_previous_sessions_and_handles_new_saves(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp); save = home/'new.flash'
            for value in range(8):
                result = self.invoke('write', save, home, value=value)
                self.assertEqual(result.returncode, 0, result.stderr)
            backups = sorted(Path(str(save)+'.backups').glob('*.bak'))
            self.assertEqual([p.read_bytes()[0] for p in backups], [2,3,4,5,6])
            self.assertEqual(save.read_bytes(), bytes([7])*131072)

    def test_backup_failure_prevents_overwriting_active_save(self):
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp); save=home/'stadium.flash'; save.write_bytes(self.fixture)
            Path(str(save)+'.backups').write_bytes(b'block backup folder')
            result=self.invoke('write',save,home)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(save.read_bytes(),self.fixture)
            self.assertFalse(list(home.glob('.stadium-save-*')))

    def test_export_normalizes_without_modifying_source_or_previous_export(self):
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp); save=home/'emulator.sra'
            source=bytes(self.fixture[i^3] for i in range(len(self.fixture))); save.write_bytes(source)
            first=self.invoke('export',save,home); second=self.invoke('export',save,home)
            self.assertEqual(first.returncode,0,first.stderr)
            self.assertNotEqual(first.stdout,second.stdout)
            self.assertEqual(Path(first.stdout).read_bytes(),self.fixture)
            self.assertEqual(Path(second.stdout).read_bytes(),self.fixture)
            self.assertEqual(save.read_bytes(),source)

    def test_diagnostics_redact_paths_and_game_filenames_and_are_bounded(self):
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp); (home/'logs').mkdir()
            secret='ROM C:\\Users\\Private Name\\Downloads\\Pokémon.z64\nSave /Users/private/person folder/progress.sav\nAndroid /storage/emulated/0/secret.gb\nrelative-person.sav failed\n'
            (home/'logs/pokestadium.log').write_text('old secret marker\n'+'x'*40000+'\n'+secret+'port_env.txt: PORT_PRIVATE=env-value-never-export\nAuthorization: Bearer credential-never-export\nVulkan failed to initialize\n',encoding='utf-8')
            (home/'pokestadium.cfg.json').write_text('{"private": "never export config"}')
            result=self.invoke('diagnostics','',home)
            self.assertEqual(result.returncode,0,result.stderr)
            text=Path(result.stdout).read_text(encoding='utf-8')
            for value in ['Private Name','private/person','secret.gb','relative-person','never export config','old secret marker','env-value-never-export','credential-never-export']:
                self.assertNotIn(value,text)
            self.assertIn('Vulkan failed',text)
            self.assertIn('Requested renderer: Vulkan',text)
            self.assertIn('Active renderer: OpenGL',text)
            self.assertLess(len(text),18000)

if __name__ == '__main__': unittest.main()

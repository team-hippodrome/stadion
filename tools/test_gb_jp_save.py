"""Japanese Red/Green/Blue/Yellow saves through the US Stadium's international view (src/port/gb_jp_save.cpp).

The Japanese save is built here from the layout (PKHeX SAV1Offsets JPN, game/src/gb_data.h), independently of the
C++ code: names in kana, box 1 with 25 Pokémon (more than an international box holds), box 2 full.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

DRIVER = r"""#include "gb_jp_save.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace Port;
static std::vector<uint8_t> Load(const char* p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void Store(const char* p, const std::vector<uint8_t>& d) { std::ofstream(p, std::ios::binary).write((const char*)d.data(), d.size()); }
int main(int argc, char** argv) {
    std::string mode = argv[1];
    if (mode == "intl") {             // intl <jp> <out>: the international view
        auto jp = Load(argv[2]); std::vector<uint8_t> intl(kGbSaveSize); GbJpSaveMap map;
        JapaneseSaveToInternational(jp.data(), intl.data(), map); Store(argv[3], intl); return 0;
    }
    if (mode == "back") {             // back <jp> <edited view> <out>: the view's changes in the Japanese save
        auto jp = Load(argv[2]); auto edited = Load(argv[3]); std::vector<uint8_t> intl(kGbSaveSize); GbJpSaveMap map;
        JapaneseSaveToInternational(jp.data(), intl.data(), map);
        InternationalSaveToJapanese(edited.data(), jp.data(), map); Store(argv[4], jp); return 0;
    }
    if (mode == "rom") {              // rom <in> <out>
        auto rom = Load(argv[2]); Store(argv[3], OverseasRomImage(rom)); return IsJapaneseGen1Rom(rom.data(), rom.size()) ? 0 : 3;
    }
    if (mode == "name") {             // name <hex jp name>: romaji, and back to kana (hex)
        std::vector<uint8_t> jp(6, 0x50);
        for (size_t i = 0; i < 6 && 2 * i < strlen(argv[2]); i++) jp[i] = (uint8_t)std::stoi(std::string(argv[2] + 2 * i, 2), nullptr, 16);
        auto intl = JapaneseNameToInternational(jp.data(), 10);
        auto back = InternationalNameToJapanese(intl.data());
        for (auto c : intl) printf("%02x", c); printf(" ");
        for (auto c : back) printf("%02x", c); printf("\n"); return 0;
    }
    return 2;
}
"""

KATA = "アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフホマミムメモヤユヨラルレロワヲン"
VOICED = "ガギグゲゴザジズゼゾダヂヅデド"
SMALL = {"バ": 0x19, "ビ": 0x1A, "ブ": 0x1B, "ボ": 0x1C, "パ": 0x40, "プ": 0x42, "ポ": 0x43, "ッ": 0xAC, "ャ": 0xAD, "ュ": 0xAE, "ョ": 0xAF, "ィ": 0xB0, "ー": 0xE3}


def kana(text, length=6):
    out = []
    for ch in text:
        if ch in KATA:
            out.append(0x80 + KATA.index(ch))
        elif ch in VOICED:
            out.append(0x05 + VOICED.index(ch))
        elif ch == "ピ":
            out.append(0x41)
        else:
            out.append(SMALL[ch])
    return bytes(out + [0x50] * (length - len(out)))


def intl(text, length=11):
    return bytes([0x80 + ord(c) - 65 for c in text] + [0x50] * (length - len(text)))


def checksum(data):
    return (~sum(data)) & 0xFF


# species (internal index), Japanese species name
PIKACHU = (0x54, "ピカチュウ")
MEW = (0x15, "ミュウ")


def mon(species, serial, size):
    data = bytearray(size)
    data[0] = species
    data[3] = 25                          # level (box structure)
    data[12:14] = bytes([0x12, serial])   # OT ID
    data[14:17] = bytes([0, 0x30, serial])  # experience
    data[27:29] = bytes([serial * 7 & 255, serial * 13 & 255])  # DVs
    if size == 44:
        data[33] = 25
    return bytes(data)


def pokemon_list(entries, cap, size):
    """entries: (species, serial, nickname, ot) -> a Japanese list (6-byte names)"""
    out = bytearray(1 + cap + 1 + cap * size + 2 * cap * 6)
    out[0] = len(entries)
    for i, (sp, serial, nick, ot) in enumerate(entries):
        out[1 + i] = sp
        base = 2 + cap
        out[base + i * size: base + (i + 1) * size] = mon(sp, serial, size)
        out[base + cap * size + i * 6: base + cap * size + (i + 1) * 6] = kana(ot)
        out[base + cap * size + cap * 6 + i * 6: base + cap * size + cap * 6 + (i + 1) * 6] = kana(nick)
    out[1 + len(entries)] = 0xFF
    return bytes(out)


def japanese_save():
    s = bytearray(0x8000)
    s[0x2598:0x259E] = kana("サトシ")
    s[0x25F1:0x25F7] = kana("シゲル")
    s[0x25FB:0x25FD] = bytes([0x12, 0x34])  # trainer ID
    s[0x25F8] = 0xFF                        # badges
    s[0x2842] = 0x80                        # current box 1, boxes in use
    party = [(PIKACHU[0], 1, PIKACHU[1], "サトシ"), (MEW[0], 2, "キュウビ", "サトシ")]
    s[0x2ED5:0x2ED5 + 0x158] = pokemon_list(party, 6, 44)
    box1 = [(PIKACHU[0] if i % 2 else MEW[0], 10 + i, "タロウ" if i == 22 else (PIKACHU[1] if i % 2 else MEW[1]),
             "ロケット") for i in range(25)]
    box2 = [(MEW[0], 40 + i, MEW[1], "サトシ") for i in range(30)]
    box1_list, box2_list = pokemon_list(box1, 30, 33), pokemon_list(box2, 30, 33)
    s[0x302D:0x302D + 0x566] = box1_list
    for b in range(8):
        base = (0x4000 if b < 4 else 0x6000) + (b % 4) * 0x566
        s[base:base + 0x566] = box1_list if b == 0 else box2_list if b == 1 else pokemon_list([], 30, 33)
    for bank in (0x4000, 0x6000):
        s[bank + 0x1598] = checksum(s[bank:bank + 0x1598])
        for b in range(4):
            s[bank + 0x1599 + b] = checksum(s[bank + b * 0x566:bank + (b + 1) * 0x566])
    s[0x3594] = checksum(s[0x2598:0x3594])
    return bytes(s)


def intl_list(data, offset, cap, size):
    count = data[offset]
    base = offset + 2 + cap
    return [(data[offset + 1 + i], data[base + i * size: base + (i + 1) * size],
             data[base + cap * size + i * 11: base + cap * size + (i + 1) * 11],
             data[base + cap * size + cap * 11 + i * 11: base + cap * size + cap * 11 + (i + 1) * 11]) for i in range(count)]


def intl_box(k):
    return (0x4000 if k < 6 else 0x6000) + (k % 6) * 0x462


class JapaneseSaveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('A C++17 compiler is needed')
        cls.tmp = tempfile.TemporaryDirectory()
        source = Path(cls.tmp.name) / 'driver.cpp'
        source.write_text(DRIVER, encoding='utf-8')
        cls.exe = Path(cls.tmp.name) / ('driver.exe' if os.name == 'nt' else 'driver')
        subprocess.run([compiler, '-std=c++17', '-include', 'cstring', '-I' + str(ROOT / 'src/port'), str(source),
                        str(ROOT / 'src/port/gb_jp_save.cpp'), '-o', str(cls.exe)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_driver(self, *args):
        return subprocess.run([str(self.exe), *map(str, args)], check=True, capture_output=True, text=True).stdout

    def view(self, jp):
        d = Path(self.tmp.name)
        (d / 'jp.sav').write_bytes(jp)
        self.run_driver('intl', d / 'jp.sav', d / 'view.sav')
        return bytearray((d / 'view.sav').read_bytes())

    def back(self, jp, view):
        d = Path(self.tmp.name)
        (d / 'jp.sav').write_bytes(jp)
        (d / 'edited.sav').write_bytes(bytes(view))
        self.run_driver('back', d / 'jp.sav', d / 'edited.sav', d / 'out.sav')
        return (d / 'out.sav').read_bytes()

    def test_view_is_a_valid_international_save(self):
        v = self.view(japanese_save())
        self.assertEqual(v[0x3523], checksum(v[0x2598:0x3523]))
        for bank in (0x4000, 0x6000):
            self.assertEqual(v[bank + 0x1A4C], checksum(v[bank:bank + 0x1A4C]))
        self.assertEqual(v[0x2598:0x25A3], intl("SATOSHI"))
        self.assertEqual(v[0x25F6:0x2601], intl("SHIGERU"))
        self.assertEqual(v[0x2605:0x2607], bytes([0x12, 0x34]))  # trainer ID after the shifted fields
        self.assertEqual(v[0x2602], 0xFF)                        # badges
        party = intl_list(v, 0x2F2C, 6, 44)
        self.assertEqual([p[3] for p in party], [intl("PIKACHU"), intl("KYUUBI")])  # species name; romaji
        self.assertEqual(party[0][2], intl("SATOSHI"))
        # box 1: the first 20 of 25 in box 1 (and as the current box), Pokémon 21-25 in box 9 ahead of box 2's 21-30
        self.assertEqual(len(intl_list(v, 0x30C0, 20, 33)), 20)
        self.assertEqual(intl_list(v, intl_box(0), 20, 33), intl_list(v, 0x30C0, 20, 33))
        self.assertEqual(len(intl_list(v, intl_box(1), 20, 33)), 20)
        nine = intl_list(v, intl_box(8), 20, 33)
        self.assertEqual(len(nine), 15)
        self.assertEqual([m[1][13] for m in nine], list(range(30, 35)) + list(range(60, 70)))
        self.assertEqual(nine[2][3], intl("TAROU"))
        self.assertEqual(nine[2][2], intl("ROKETTO"))

    def test_unchanged_view_converts_back_to_the_same_bytes(self):
        jp = japanese_save()
        self.assertEqual(self.back(jp, self.view(jp)), jp)

    def test_moving_pokemon_keeps_names_and_layout(self):
        jp = japanese_save()
        v = self.view(jp)
        party = intl_list(v, 0x2F2C, 6, 44)
        # Stadium moves KYUUBI from the party to box 3, and puts an English-named Pokémon into the party
        moved = party[1]
        box3 = bytearray(v[intl_box(2):intl_box(2) + 0x462])
        box3[0], box3[1], box3[2] = 1, moved[0], 0xFF
        box3[22:55] = moved[1][:33]
        box3[22 + 20 * 33:22 + 20 * 33 + 11] = moved[2]
        box3[22 + 20 * 33 + 20 * 11:22 + 20 * 33 + 20 * 11 + 11] = moved[3]
        v[intl_box(2):intl_box(2) + 0x462] = box3
        p = 0x2F2C
        v[p + 1 + 1] = MEW[0]
        v[p + 8 + 44:p + 8 + 88] = mon(MEW[0], 99, 44)
        v[p + 8 + 264 + 11:p + 8 + 264 + 22] = intl("ASH")
        v[p + 8 + 264 + 66 + 11:p + 8 + 264 + 66 + 22] = intl("SPARKY")
        out = self.back(jp, v)
        self.assertEqual(out[0x3594], checksum(out[0x2598:0x3594]))
        self.assertEqual(out[0x4000 + 0x1598], checksum(out[0x4000:0x4000 + 0x1598]))
        box3_jp = out[0x4000 + 2 * 0x566:]
        self.assertEqual(box3_jp[0], 1)
        self.assertEqual(box3_jp[2 + 30 + 30 * 33 + 30 * 6:2 + 30 + 30 * 33 + 30 * 6 + 6], kana("キュウビ"))
        self.assertEqual(box3_jp[2 + 30 + 30 * 33:2 + 30 + 30 * 33 + 6], kana("サトシ"))
        nick = out[0x2ED5 + 8 + 264 + 36 + 6:0x2ED5 + 8 + 264 + 36 + 12]
        self.assertNotEqual(nick[0], 0x50)  # SPARKY, spelled in katakana
        self.assertTrue(all(0x80 <= c <= 0xAC or 0x40 <= c <= 0x43 or c in (0xE3, 0x50) or 0x05 <= c <= 0x13 for c in nick))
        # the overflow boxes are as they were: box 1 still 25, box 2 still 30
        self.assertEqual(out[0x302D], 25)
        self.assertEqual(out[0x4000 + 0x566], 30)
        self.assertEqual(out[0x2598:0x259E], kana("サトシ"))

    def test_names(self):
        def name(text):
            return self.run_driver('name', kana(text).hex()).split()
        self.assertEqual(name("サトシ")[0], intl("SATOSHI").hex())
        self.assertEqual(name("サトシ")[1], kana("サトシ").hex())
        self.assertEqual(name("ロケット")[0], intl("ROKETTO").hex())
        self.assertEqual(name("ロケット")[1], kana("ロケット").hex())
        self.assertEqual(name("キュウビ")[1], kana("キュウビ").hex())
        self.assertEqual(name("ジュン")[0], intl("JUN").hex())

    def test_rom_image(self):
        rom = bytearray(0x80000)
        rom[0x134:0x141] = b'POKEMON GREEN'
        rom[0x146:0x14C] = bytes([3, 3, 4, 3, 0, 0x33])
        rom[0x200] = 0x42
        d = Path(self.tmp.name)
        (d / 'g.gb').write_bytes(rom)
        self.run_driver('rom', d / 'g.gb', d / 'o.gb')
        out = (d / 'o.gb').read_bytes()
        self.assertEqual(len(out), 0x100000)
        self.assertEqual((out[0x14A], out[0x148], out[0x147], out[0x80200]), (1, 5, 0x13, 0x42))
        self.assertEqual(out[0x14D], (-sum(out[0x134:0x14D]) - 25) & 255)
        self.assertEqual(int.from_bytes(out[0x14E:0x150], 'big'), (sum(out) - out[0x14E] - out[0x14F]) & 0xFFFF)


if __name__ == '__main__':
    unittest.main()

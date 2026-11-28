#!/usr/bin/env python3 -I
"""Checks src/port/generated/rom_layouts.c, the tables that map the game's US Rev 2 ROM offsets onto every other
Stadium ROM (tools/gen_rom_layouts.py).

Without ROMs: no row ends before it starts, and the main segment's rows ("text") never overlap another piece.
A main segment row inside another piece wins the lookup there (rom_version.cpp takes the innermost row) and moves that
piece's reads by the main segment's delta, which is how the European ROMs once read the GB Tower's border images
0x30 bytes late and crashed decompressing garbage.

With --rom-dir (a folder holding "Pokemon Stadium (<release>).z64" for each release): the Yay0 data the GB Tower
loads (its UI graphics and the three SGB borders) is found where the lookup sends each ROM's reads.

Usage: tools/test_rom_layouts.py [--rom-dir DIR]   (exit status 1 when a check fails)"""
import argparse, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TABLE = ROOT / "src/port/generated/rom_layouts.c"
ROW = re.compile(r"\{ (0x[0-9A-F]+), (0x[0-9A-F]+), (0x[0-9A-F]+), (0x[0-9A-F]+) \}, // (\w+)")
# releases: table name -> ROM file name inside --rom-dir
ROMS = {
    "us": "Pokemon Stadium (USA).z64",
    "us_rev1": "Pokemon Stadium (USA) (Rev 1).z64",
    "eu": "Pokemon Stadium (Europe).z64",
    "eu_rev1": "Pokemon Stadium (Europe) (Rev 1).z64",
    "fr": "Pokemon Stadium (France).z64",
    "de": "Pokemon Stadium (Germany).z64",
    "es": "Pokemon Stadium (Spain).z64",
    "it": "Pokemon Stadium (Italy).z64",
}
REV2 = "Pokemon Stadium (USA) (Rev 2).z64"
# Rev 2 offsets of the GB Tower's Yay0 files (gb_tower_emulator.h: fragment1_misc_yay0_ROM_START + offset)
FRAGMENT1_MISC_YAY0 = 0xADE70
GB_TOWER_YAY0 = {"border tier 0": 0x46CA0, "border tier 1": 0x477D0, "border tier 2": 0x501C0, "UI graphics": 0x54F20}


def tables():
    text = TABLE.read_text()
    out = {}
    for m in re.finditer(r"static const PortRomSegment sSegments_(\w+)\[\] = \{(.*?)\};", text, re.S):
        out[m.group(1)] = [(int(a, 16), int(b, 16), int(c, 16), int(d, 16), n) for a, b, c, d, n in ROW.findall(m.group(2))]
    return out


def lookup(rows, offset):
    """rom_version.cpp FindSegmentIn + OffsetIn: the innermost row holding offset, else the last one before it"""
    before = [r for r in rows if r[0] <= offset]
    if not before:
        return offset
    for r in reversed(before):
        if offset < r[1]:
            return offset - r[0] + r[2]
    r = before[-1]
    return offset - r[0] + r[2]


def check_table(name, rows):
    errors = []
    for gs, ge, vs, ve, n in rows:
        # zero-length rows are linker markers (heap); a piece may be longer in a translated ROM, so only the order
        if ge < gs or ve < vs or (n == "text" and ge == gs):
            errors.append(f"{name}: row 0x{gs:X}-0x{ge:X} ({n}) ends before it starts")
    pieces = [r for r in rows if r[4] != "text" and r[1] > r[0]]
    for gs, ge, _, _, n in rows:
        if n != "text":
            continue
        for ps, pe, _, _, pn in pieces:
            if gs < pe and ps < ge:
                errors.append(f"{name}: main segment row 0x{gs:X}-0x{ge:X} overlaps {pn} 0x{ps:X}-0x{pe:X}")
                break
    return errors


def check_roms(name, rows, rom_dir, rev2):
    path = rom_dir / ROMS[name]
    if not path.exists():
        return [], f"{name}: skipped ({path.name} not in {rom_dir})"
    rom = path.read_bytes()
    errors = []
    for what, off in GB_TOWER_YAY0.items():
        game = FRAGMENT1_MISC_YAY0 + off
        at = lookup(rows, game)
        # the same Yay0 header and unpacked size (the Spanish and Italian UI graphics are translated: other bytes)
        if rom[at:at + 8] != rev2[game:game + 8]:
            errors.append(f"{name}: GB Tower {what} (Rev 2 0x{game:X}) read at 0x{at:X}, which does not hold it")
    return errors, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rom-dir", type=pathlib.Path)
    args = parser.parse_args()
    all_tables = tables()
    if set(all_tables) != set(ROMS):
        print(f"tables found: {sorted(all_tables)}, expected {sorted(ROMS)}")
        return 1
    errors, notes = [], []
    for name, rows in all_tables.items():
        errors += check_table(name, rows)
    if args.rom_dir:
        rev2_path = args.rom_dir / REV2
        if not rev2_path.exists():
            print(f"{REV2} not in {args.rom_dir}: the ROM checks need it as the reference")
            return 1
        rev2 = rev2_path.read_bytes()
        for magic_off in GB_TOWER_YAY0.values():
            assert rev2[FRAGMENT1_MISC_YAY0 + magic_off:][:4] == b"Yay0", "the Rev 2 offsets above are stale"
        for name, rows in all_tables.items():
            e, note = check_roms(name, rows, args.rom_dir, rev2)
            errors += e
            if note:
                notes.append(note)
    for n in notes:
        print(n)
    for e in errors:
        print("FAIL", e)
    checked = "tables" + (" and ROMs" if args.rom_dir else "")
    print(f"{len(all_tables)} layouts, {checked}: " + ("OK" if not errors else f"{len(errors)} failures"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3 -I
"""A family of Game Boy save variants from one base save, each changing one thing Pokémon Stadium reads from the
cartridge (for tools/evidence: which code each condition reaches). Saves are written to <out dir>, never committed.

Usage:
  tools/evidence/gb_variants.py <base.sav> <out dir> [variant ...]     (default: all)
Variants (the base keeps its party, map and flags):
  base           the base unchanged (the control)
  party6         six Pokémon of different types, levels 5-100, full moves
  party1         one Pokémon only
  dex_full       all 151 owned and seen
  dex_empty      nothing owned or seen
  boxes_full     every one of the 12 boxes and the current box full (20 each), all 151 species spread over them
  boxes_mixed    box 1 full, box 2 one Pokémon, the others empty
  no_pokedex     the "got the Pokédex" event flag cleared
  not_at_pc      saved outside a Pokémon Center (Pallet Town)
  bad_checksum   main-data checksum wrong (bank 1)
  bad_box_sum    box bank 2 checksum wrong
  hof            a Hall of Fame entry
  illegal        party with moves the species cannot learn, level 100 Mewtwo, an egg-like id 0 slot
  fainted        party where every Pokémon but one has 0 HP, one poisoned, one asleep
Gen 1 save layout (pokered): main data 0x2598-0x3522 with its checksum at 0x3523; party 0x2F2C; current box
0x30C0 (0x462 bytes); current box number 0x284C; boxes 1-6 at 0x4000, 7-12 at 0x6000 (0x462 each), bank checksum at
bank + 0x1A4C, per-box checksums from bank + 0x1A4D; Pokédex owned 0x25A3 / seen 0x25B6 (19 bytes each); event
flags from 0x29F3 (0x29F7 bit 5: got the Pokédex); current map 0x260A; Hall of Fame at 0x0598 (0x60 per team).
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import gb_party  # noqa: E402
import gb_save  # noqa: E402

T = gb_party.tables()


def put_party(sav, specs, player, otid):
    mons = [gb_party.build(x, T, player, otid) for x in specs]
    b = 0x2F2C
    sav[b] = len(mons)
    for i in range(7):
        sav[b + 1 + i] = 0xFF
    for i, (data, sid, dex, ot, nick) in enumerate(mons):
        sav[b + 1 + i] = sid
        sav[b + 8 + 44 * i:b + 8 + 44 * (i + 1)] = data
        sav[b + 0x110 + 11 * i:b + 0x110 + 11 * (i + 1)] = ot
        sav[b + 0x152 + 11 * i:b + 0x152 + 11 * (i + 1)] = nick


def box_bytes(specs, player, otid):
    """one 0x462-byte box: count, species list, 20 x 33-byte entries, OT names, nicknames"""
    box = bytearray(0x462)
    mons = [gb_party.build(x, T, player, otid) for x in specs]
    box[0] = len(mons)
    for i in range(21):
        box[1 + i] = 0xFF
    for i, (data, sid, dex, ot, nick) in enumerate(mons):
        box[1 + i] = sid
        box[0x16 + 33 * i:0x16 + 33 * (i + 1)] = data[:33]
        box[0x2AA + 11 * i:0x2AA + 11 * (i + 1)] = ot
        box[0x386 + 11 * i:0x386 + 11 * (i + 1)] = nick
    if not mons:
        box[1] = 0xFF
    return bytes(box)


def put_boxes(sav, boxes, current, player, otid):
    """boxes: 12 lists of mon specs; current: the current box's list (in bank 1)"""
    for bank in (2, 3):
        start = bank * 0x2000
        for k in range(6):
            sav[start + k * 0x462:start + (k + 1) * 0x462] = box_bytes(boxes[(bank - 2) * 6 + k], player, otid)
        sav[start + 0x1A4C] = gb_save.checksum(sav[start:start + 0x1A4C])
        for k in range(6):
            o = start + k * 0x462
            sav[start + 0x1A4D + k] = gb_save.checksum(sav[o:o + 0x462])
    sav[0x30C0:0x30C0 + 0x462] = box_bytes(current, player, otid)
    sav[0x284C] = 0x80  # current box 1, boxes initialised


def dex(sav, owned, seen):
    for i in range(19):
        sav[0x25A3 + i] = 0
        sav[0x25B6 + i] = 0
    for d in owned:
        sav[0x25A3 + (d - 1) // 8] |= 1 << ((d - 1) % 8)
    for d in seen:
        sav[0x25B6 + (d - 1) // 8] |= 1 << ((d - 1) % 8)


def fix_main(sav):
    sav[0x3523] = gb_save.checksum(sav[0x2598:0x3523])


SIX = ["PIKACHU:25", "VENUSAUR:50", "CHARIZARD:60", "BLASTOISE:70", "MEW:100", "MAGIKARP:5"]


def variant(name, base, player, otid):
    sav = bytearray(base)
    if name == "base":
        return sav
    if name == "party6":
        put_party(sav, SIX, player, otid)
    elif name == "party1":
        put_party(sav, ["PIKACHU:25"], player, otid)
    elif name == "dex_full":
        dex(sav, range(1, 152), range(1, 152))
    elif name == "dex_empty":
        dex(sav, [], [])
    elif name == "boxes_full":
        species = [d for d in range(1, 152)]
        boxes = [["%d:%d" % (species[(b * 20 + i) % 151], 5 + (b * 20 + i) % 95) for i in range(20)] for b in range(12)]
        put_boxes(sav, boxes, boxes[0], player, otid)
        dex(sav, range(1, 152), range(1, 152))
    elif name == "boxes_mixed":
        boxes = [["%d:%d" % (d, 10 + d % 50) for d in range(1, 21)], ["MEWTWO:70"]] + [[] for _ in range(10)]
        put_boxes(sav, boxes, boxes[0], player, otid)
    elif name == "no_pokedex":
        sav[0x29F7] &= ~0x20
    elif name == "not_at_pc":
        sav[0x260A] = 0x00  # Pallet Town
    elif name == "bad_checksum":
        fix_main(sav)
        sav[0x3523] ^= 0x5A
        return sav
    elif name == "bad_box_sum":
        sav[0x4000 + 0x1A4C] ^= 0x5A
    elif name == "hof":
        team = [gb_party.build(x, T, player, otid) for x in SIX]
        sav[0x284E] = 1 if len(sav) > 0x284E else 0  # (number of Hall of Fame teams, in main data)
        o = 0x0598
        for i, (data, sid, d, ot, nick) in enumerate(team):
            e = o + 16 * i
            sav[e] = sid
            sav[e + 1] = data[3]
            sav[e + 2:e + 13] = nick
    elif name == "illegal":
        put_party(sav, ["CHARMANDER:5,moves=63/94/57/153", "MEWTWO:100,moves=94/105/97/0", "MAGIKARP:100,moves=150/63/0/0",
                        "PIKACHU:100,moves=57/19/0/0,stats=999/999/999/999/999"], player, otid)
    elif name == "fainted":
        put_party(sav, ["PIKACHU:25,hp=0", "MEW:100,hp=0", "GENGAR:60,status=0x08", "SNORLAX:60,status=0x02",
                        "CHARIZARD:60,hp=1"], player, otid)
    else:
        sys.exit("unknown variant %s" % name)
    fix_main(sav)
    return sav


ALL = ["base", "party6", "party1", "dex_full", "dex_empty", "boxes_full", "boxes_mixed", "no_pokedex", "not_at_pc",
       "bad_checksum", "bad_box_sum", "hof", "illegal", "fainted"]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    base = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    names = sys.argv[3:] or ALL
    raw = base[0x2598:0x25A3]
    inv = {v: k for k, v in gb_save.CHARS.items()}
    player = "".join(inv.get(c, "") for c in raw[:raw.index(0x50)] if c in inv) or "RED"
    otid = int.from_bytes(base[0x2605:0x2607], "big")
    for n in names:
        open(os.path.join(out, n + ".sav"), "wb").write(bytes(variant(n, base, player, otid)))
    print("%d variants in %s: %s" % (len(names), out, " ".join(names)))


if __name__ == "__main__":
    main()

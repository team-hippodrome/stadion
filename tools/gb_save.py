#!/usr/bin/env python3 -I
"""Writes a Pokémon Red/Blue/Yellow (international, incl. German) battery save with a party, for the Transfer Pak.

Usage: tools/gb_save.py <out.sav> [--player NAME] [--rival NAME] [--party SPECIES:LEVEL,...] [--rom game.gb]
       [--base real.sav] [--got-pokedex]
Species: PIKACHU BULBASAUR CHARMANDER SQUIRTLE PIDGEY RATTATA (Gen 1 internal ids below).
The save is 32 KB (bank 0..3). It holds what Pokémon Stadium reads: names, ID, party, Pokédex flags, money, badges,
an empty current box and empty box banks, with valid checksums. The overworld state is a fresh-game position
(Viridian City Pokémon Center), so the Game Boy game itself can also continue from it. The map header is read from
--rom (any Red/Blue/Yellow); without it the German Yellow values are used.
A save built from scratch only gives a partial overworld state (the game keeps tileset and sprite state in the save
too), so a continued Game Boy game may not draw its map. With --base, a save the game itself wrote is the starting
point instead: its map, flags and player ID stay, and only names, party and Pokédex flags are set.
"""
import sys

CHARS = {c: 0x80 + i for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ")}
CHARS.update({c: 0xA0 + i for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")})
CHARS.update({str(d): 0xF6 + d for d in range(10)})
CHARS[" "] = 0x7F

# name: (internal id, dex no, base hp, atk, def, spd, spc, type1, type2, catch rate, growth, moves[(id, pp)])
# growth: 0 medium fast (n^3), 3 medium slow
SPECIES = {
    "PIKACHU": (0x54, 25, 35, 55, 30, 90, 50, 0x17, 0x17, 190, 0, [(0x54, 30), (0x2D, 40), (0x56, 20), (0x62, 30)]),
    "BULBASAUR": (0x99, 1, 45, 49, 49, 45, 65, 0x16, 0x03, 45, 3, [(0x21, 35), (0x2D, 40), (0x49, 10), (0x16, 10)]),
    "CHARMANDER": (0xB0, 4, 39, 52, 43, 65, 50, 0x14, 0x14, 45, 3, [(0x0A, 35), (0x2D, 40), (0x34, 25), (0x2B, 30)]),
    "SQUIRTLE": (0xB1, 7, 44, 48, 65, 43, 50, 0x15, 0x15, 45, 3, [(0x21, 35), (0x27, 30), (0x91, 30), (0x37, 25)]),
    "PIDGEY": (0x24, 16, 40, 45, 40, 56, 35, 0x00, 0x02, 255, 3, [(0x21, 35), (0x1C, 15), (0x62, 30), (0x10, 35)]),
    "RATTATA": (0xA5, 19, 30, 56, 35, 72, 25, 0x00, 0x00, 255, 0, [(0x21, 35), (0x27, 30), (0x62, 30), (0x9E, 15)]),
}


def text(s, n=11):
    out = [CHARS[c] for c in s.upper()][: n - 1] + [0x50]
    return bytes(out + [0x50] * (n - len(out)))


def exp_for(level, growth):
    n = level
    if growth == 0:
        return n ** 3
    return max(0, int(6 * n ** 3 / 5 - 15 * n * n + 100 * n - 140))


def stat(base, dv, level, hp=False):
    v = ((base + dv) * 2) * level // 100
    return v + level + 10 if hp else v + 5


def mon(name, level, ot_id):
    sid, dex, bhp, batk, bdef, bspd, bspc, t1, t2, catch, growth, moves = SPECIES[name]
    atk, dfn, spd, spc = 10, 10, 10, 10
    hpdv = ((atk & 1) << 3) | ((dfn & 1) << 2) | ((spd & 1) << 1) | (spc & 1)
    hp = stat(bhp, hpdv, level, True)
    st = [stat(batk, atk, level), stat(bdef, dfn, level), stat(bspd, spd, level), stat(bspc, spc, level)]
    exp = exp_for(level, growth)
    b = bytearray(44)
    b[0] = sid
    b[1:3] = hp.to_bytes(2, "big")
    b[3] = level
    b[4] = 0
    b[5], b[6] = t1, t2
    b[7] = catch
    for i, (m, _) in enumerate(moves):
        b[8 + i] = m
    b[12:14] = ot_id.to_bytes(2, "big")
    b[14:17] = exp.to_bytes(3, "big")
    b[27] = (atk << 4) | dfn
    b[28] = (spd << 4) | spc
    for i, (_, pp) in enumerate(moves):
        b[29 + i] = pp
    b[33] = level
    b[34:36] = hp.to_bytes(2, "big")
    for i, v in enumerate(st):
        b[36 + 2 * i:38 + 2 * i] = v.to_bytes(2, "big")
    return bytes(b), sid, dex


# German Yellow, map 0x29: tileset 6, 4x7 blocks, blocks/text/script pointers (bank 0x11), no connections
YELLOW_DE_VIRIDIAN_PC_HEADER = bytes([0x06, 0x04, 0x07, 0xDF, 0x40, 0x63, 0x42, 0x5D, 0x42, 0x00])


def map_header(rom, map_id):
    """Finds the map header pointer table (Pallet Town and Viridian City headers as its first two entries) and returns
    the 10 header bytes of map_id."""
    def headers(pat):
        out, i = set(), rom.find(pat)
        while i >= 0:
            out.add(i % 0x4000 + (0x4000 if i >= 0x4000 else 0))
            i = rom.find(pat, i + 1)
        return out
    pallet, viridian = headers(bytes([0, 9, 10])), headers(bytes([0, 18, 20]))
    for t in range(len(rom) - 0x200):
        if rom[t] | rom[t + 1] << 8 in pallet and rom[t + 2] | rom[t + 3] << 8 in viridian:
            ptr = rom[t + 2 * map_id] | rom[t + 2 * map_id + 1] << 8
            for bank in range(len(rom) // 0x4000):
                o = bank * 0x4000 + ptr - 0x4000
                if bank and rom[o] == 6 and rom[o + 1] == 4 and rom[o + 2] == 7:  # a Pokémon Center header
                    return rom[o:o + 10]
    sys.exit("map header of map %#x not found" % map_id)


def checksum(data):
    return (~sum(data)) & 0xFF


def write_party(sav, party, player, ot_id):
    """Party at 0x2F2C (count, species list, 6 x 44-byte structs, OT names, nicknames) plus Pokédex flags."""
    mons = [(p.split(":")[0].upper(), int(p.split(":")[1])) for p in party.split(",") if p]
    base = 0x2F2C
    sav[base] = len(mons)
    for i in range(7):
        sav[base + 1 + i] = 0xFF
    for i, (name, level) in enumerate(mons):
        data, sid, dex = mon(name, level, ot_id)
        sav[base + 1 + i] = sid
        sav[base + 8 + 44 * i: base + 8 + 44 * (i + 1)] = data
        sav[base + 0x110 + 11 * i: base + 0x110 + 11 * (i + 1)] = text(player)
        sav[base + 0x152 + 11 * i: base + 0x152 + 11 * (i + 1)] = text(name)
        # Pokédex: owned + seen
        sav[0x25A3 + (dex - 1) // 8] |= 1 << ((dex - 1) % 8)
        sav[0x25B6 + (dex - 1) // 8] |= 1 << ((dex - 1) % 8)
    return mons


def main():
    args = sys.argv[1:]
    out = args[0]
    player, rival, party, rom, base_sav = "MICHI", "GARY", "PIKACHU:25,BULBASAUR:20,CHARMANDER:18", None, None
    for i, a in enumerate(args):
        if a == "--player":
            player = args[i + 1]
        if a == "--rival":
            rival = args[i + 1]
        if a == "--party":
            party = args[i + 1]
        if a == "--rom":
            rom = open(args[i + 1], "rb").read()
        if a == "--base":
            base_sav = open(args[i + 1], "rb").read()
    if base_sav:
        sav = bytearray(base_sav)
        ot_id = int.from_bytes(sav[0x2605:0x2607], "big")
        sav[0x2598:0x25A3] = text(player)
        sav[0x25F6:0x2601] = text(rival)
        mons = write_party(sav, party, player, ot_id)
        if "--got-pokedex" in args:
            sav[0x29F7] |= 0x20  # event flag "got the Pokédex" (pokered wd74b bit 5); Stadium's Lab needs it
        sav[0x3523] = checksum(sav[0x2598:0x3523])
        open(out, "wb").write(bytes(sav))
        print("%s: player %s, party %s (map state from the base save)" % (out, player, ", ".join("%s L%d" % m for m in mons)))
        return
    ot_id = 0x3039
    sav = bytearray([0x00] * 0x8000)
    # bank 1: main data
    sav[0x2598:0x25A3] = text(player)
    sav[0x25F6:0x2601] = text(rival)
    sav[0x25F3:0x25F6] = bytes([0x00, 0x30, 0x00])  # money 3000 (BCD)
    sav[0x2601] = 0x40  # options: text speed medium
    sav[0x2602] = 0x00  # badges
    sav[0x2605:0x2607] = ot_id.to_bytes(2, "big")
    sav[0x25C9] = 0  # bag: no items
    sav[0x25CA] = 0xFF
    sav[0x27E6] = 0  # PC items: none
    sav[0x27E7] = 0xFF
    # map: Viridian City Pokémon Center (0x29, 7x4 blocks, tileset 6), standing in front of the counter; "last map" =
    # Viridian City (0x01) for the exit warp. Main data mirrors WRAM from wPokedexOwned (0x25A3): wCurMap 0x260A,
    # wCurrentTileBlockMapViewPointer 0x260B, wYCoord/wXCoord 0x260D/E, wY/XBlockCoord 0x260F/10, wLastMap 0x2611,
    # wCurMapTileset/Height/Width 0x2613-15. The view pointer is what the warp tables hold:
    # wOverworldMap (0xC6E8) + 7 + width + (width + 6) * (y / 2) + x / 2.
    y, x, width, height = 4, 3, 7, 4
    sav[0x260A] = 0x29
    sav[0x260B:0x260D] = (0xC6E8 + 7 + width + (width + 6) * (y // 2) + x // 2).to_bytes(2, "little")
    sav[0x260D] = y
    sav[0x260E] = x
    sav[0x260F] = y & 1
    sav[0x2610] = x & 1
    sav[0x2611] = 0x01
    # wCurMapHeader (0x2613..0x261C): tileset, height, width, block/text/script pointers, connections. A continued game
    # runs the saved script pointer before it reloads the header, so it must be the real one for the map.
    sav[0x2613:0x261D] = map_header(rom, 0x29) if rom else YELLOW_DE_VIRIDIAN_PC_HEADER
    sav[0x2CED:0x2CF0] = bytes([1, 0, 0])  # play time 1:00
    mons = write_party(sav, party, player, ot_id)
    if "--got-pokedex" in args:
        sav[0x29F7] |= 0x20  # event flag "got the Pokédex"
    # current box (bank 1): empty
    sav[0x30C0] = 0
    sav[0x30C1] = 0xFF
    sav[0x284C] = 0x80  # current box number 1, "boxes initialised" flag
    sav[0x3523] = checksum(sav[0x2598:0x3523])
    # banks 2 and 3: six empty boxes each (0x462 bytes), all-boxes and per-box checksums
    for bank in (2, 3):
        start = bank * 0x2000
        for k in range(6):
            o = start + k * 0x462
            sav[o] = 0
            sav[o + 1] = 0xFF
        sav[start + 0x1A4C] = checksum(sav[start:start + 0x1A4C])
        for k in range(6):
            o = start + k * 0x462
            sav[start + 0x1A4D + k] = checksum(sav[o:o + 0x462])
    open(out, "wb").write(bytes(sav))
    print("%s: player %s, party %s" % (out, player, ", ".join("%s L%d" % m for m in mons)))


if __name__ == "__main__":
    main()

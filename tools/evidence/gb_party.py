#!/usr/bin/env python3 -I
"""Puts a fully specified party into a Pokémon Red/Blue/Yellow save for the Transfer Pak: Game Boy data that rentals
never produce (any species, moves the species cannot learn, levels, DVs, status, HP, nicknames, OT, the catch-rate /
held-item byte), to reach the code paths that only Game Pak Pokémon reach.

Usage:
  tools/evidence/gb_party.py <base.sav> <out.sav> "<mon>;<mon>;..." [--player NAME]
  <mon>: <species name or dex no>:<level>[,moves=a/b/c/d][,dv=0xAAAA][,status=N][,hp=N][,nick=NAME][,ot=NAME]
         [,otid=N][,cr=N][,pp=a/b/c/d][,stats=hp/atk/def/spd/spc]
  e.g. "PIKACHU:25,cr=0xA3;MEW:100,moves=94/105/118/0;CHARMANDER:5,moves=63/63/63/63"
The base save must be one Stadium accepts (made by tools/gb_save.py, with --got-pokedex for the Lab, or written by
the game). Species stats, types, growth rates, internal ids and move PP come from the decomp's tables
(game/src/gb_data.c gSpeciesData, gMoveDataWithZeroth; game/src/pokemon_stats.c gDexToInternalIndex). Stats are
computed the Game Boy way from the DVs (stat experience 0) unless stats= gives them. The main-data checksum (0x3523)
is rewritten. Saves and ROMs are the user's own and stay out of the repository.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(os.path.dirname(os.path.dirname(HERE)), "game")
sys.path.insert(0, os.path.dirname(HERE))
import gb_save  # noqa: E402  (its text encoding and checksum)


def tables():
    src = open(os.path.join(GAME, "src", "gb_data.c")).read()
    body = src[src.index("SpeciesData gSpeciesData[152] = {"):]
    body = body[:body.index("};")]
    species = {}
    num = r"\s*(0x[0-9A-Fa-f]+|\d+),"
    for m in re.finditer(r"\{" + num * 10 + r"\s*\{([^}]*)\},\s*(\d+),", body):
        g = [int(x, 0) for x in m.groups()[:10]]
        moves = [int(x, 0) for x in m.group(11).split(",")]
        species[g[0]] = {"hp": g[1], "atk": g[2], "def": g[3], "spd": g[4], "spc": g[5], "t1": g[6], "t2": g[7],
                         "cr": g[8], "moves": moves, "growth": int(m.group(12))}
    body = src[src.index("MoveData gMoveDataWithZeroth[166] = {"):]
    body = body[:body.index("};")]
    pp = {int(m.group(1)): int(m.group(2)) for m in
          re.finditer(r"\{(\d+),\s*\d+,\s*\d+,\s*\d+,\s*(?:0x[0-9A-Fa-f]+|\d+),\s*(\d+)\}", body)}
    ps = open(os.path.join(GAME, "src", "pokemon_stats.c")).read()
    arr = ps[ps.index("u8 D_80073230[0x420] = {"):]
    arr = arr[:arr.index("};")]
    words = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", re.sub(r"/\*.*?\*/", "", arr.split("{", 1)[1]))]
    internal = {d: words[0x37 + d] for d in range(1, 152)}
    names = {}
    for line in open(os.path.join(GAME, "include", "pokemon.h")):
        m = re.match(r"#define (\w+)\s+(\d+)\s*$", line)
        if m and 1 <= int(m.group(2)) <= 151:
            names.setdefault(m.group(1), int(m.group(2)))
    return species, pp, internal, names


def exp_for(level, growth):
    n = level
    return {0: n ** 3, 3: max(0, int(6 * n ** 3 / 5 - 15 * n * n + 100 * n - 140)),
            4: int(4 * n ** 3 / 5), 5: int(5 * n ** 3 / 4)}.get(growth, n ** 3)


def build(spec, T, player, ot_default):
    species, pp, internal, names = T
    head, *opts = spec.split(",")
    sp, level = head.split(":")
    dex = int(sp) if sp.isdigit() else names[sp.upper()]
    level = int(level)
    o = dict(x.split("=", 1) for x in opts)
    s = species[dex]
    moves = [int(x, 0) for x in o["moves"].split("/")] if "moves" in o else list(s["moves"])
    moves = (moves + [0, 0, 0, 0])[:4]
    dv = int(o.get("dv", "0xAAAA"), 0)
    atk, dfn, spd, spc = dv >> 12 & 15, dv >> 8 & 15, dv >> 4 & 15, dv & 15
    hpdv = ((atk & 1) << 3) | ((dfn & 1) << 2) | ((spd & 1) << 1) | (spc & 1)
    calc = lambda base, d: ((base + d) * 2) * level // 100 + 5
    stats = [((s["hp"] + hpdv) * 2) * level // 100 + level + 10, calc(s["atk"], atk), calc(s["def"], dfn),
             calc(s["spd"], spd), calc(s["spc"], spc)]
    if "stats" in o:
        stats = [int(x, 0) for x in o["stats"].split("/")]
    hp = int(o.get("hp", stats[0]), 0) if isinstance(o.get("hp"), str) else stats[0]
    pps = [int(x, 0) for x in o["pp"].split("/")] if "pp" in o else [pp.get(m, 0) if m else 0 for m in moves]
    otid = int(o.get("otid", str(ot_default)), 0)
    b = bytearray(44)
    b[0] = internal[dex]
    b[1:3] = hp.to_bytes(2, "big")
    b[3] = level
    b[4] = int(o.get("status", "0"), 0)
    b[5], b[6] = s["t1"], s["t2"]
    b[7] = int(o.get("cr", str(s["cr"])), 0)
    b[8:12] = bytes(moves)
    b[12:14] = otid.to_bytes(2, "big")
    b[14:17] = exp_for(level, s["growth"]).to_bytes(3, "big")
    b[27] = dv >> 8
    b[28] = dv & 0xFF
    b[29:33] = bytes(pps)
    b[33] = level
    b[34:36] = stats[0].to_bytes(2, "big")
    for i, v in enumerate(stats[1:]):
        b[36 + 2 * i:38 + 2 * i] = v.to_bytes(2, "big")
    nick = o.get("nick", [n for n, d in names.items() if d == dex][0].replace("_", ""))
    return bytes(b), internal[dex], dex, gb_save.text(o.get("ot", player)), gb_save.text(nick)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    base, out, party = sys.argv[1], sys.argv[2], sys.argv[3]
    player = sys.argv[sys.argv.index("--player") + 1] if "--player" in sys.argv else None
    sav = bytearray(open(base, "rb").read())
    if player is None:
        raw = sav[0x2598:0x25A3]
        inv = {v: k for k, v in gb_save.CHARS.items()}
        player = "".join(inv.get(c, "") for c in raw[:raw.index(0x50)] if c in inv) or "RED"
    ot_default = int.from_bytes(sav[0x2605:0x2607], "big")
    T = tables()
    mons = [build(x, T, player, ot_default) for x in party.split(";") if x]
    base_off = 0x2F2C
    sav[base_off] = len(mons)
    for i in range(7):
        sav[base_off + 1 + i] = 0xFF
    for i, (data, sid, dex, ot, nick) in enumerate(mons):
        sav[base_off + 1 + i] = sid
        sav[base_off + 8 + 44 * i: base_off + 8 + 44 * (i + 1)] = data
        sav[base_off + 0x110 + 11 * i: base_off + 0x110 + 11 * (i + 1)] = ot
        sav[base_off + 0x152 + 11 * i: base_off + 0x152 + 11 * (i + 1)] = nick
        sav[0x25A3 + (dex - 1) // 8] |= 1 << ((dex - 1) % 8)
        sav[0x25B6 + (dex - 1) // 8] |= 1 << ((dex - 1) % 8)
    sav[0x3523] = gb_save.checksum(sav[0x2598:0x3523])
    open(out, "wb").write(bytes(sav))
    print("%s: %d Pokémon (%s)" % (out, len(mons), ", ".join("#%d" % m[2] for m in mons)))


if __name__ == "__main__":
    main()

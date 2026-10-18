"""Gen 1 Pokémon records as stored in the Stadium save (GbPokemonNamedRecord, 0x37 bytes, src/gb_data.h).

0x00 u8   species, Gen 1 internal index (Species_FromInternalIndex -> dex)
0x01 u16  current HP
0x03 u8   level (GbSave_SaveBattleMonRecord copies the battle level here)
0x04 u8   status (bits 0-2 sleep turns, 3 poison, 4 burn, 5 freeze, 6 paralysis)
0x05 u8   type 1, 0x06 u8 type 2 (Gen 1 type ids)
0x07 u8   catch rate byte (Game Boy "held item" byte in Gold/Silver)
0x08 u8[4] moves
0x0C u16  original trainer ID
0x0E u24  experience
0x11 u16[5] stat experience: HP, Attack, Defense, Speed, Special
0x1B u16  DVs: Attack<<12 | Defense<<8 | Speed<<4 | Special
0x1D u8[4] PP: bits 0-5 current PP, bits 6-7 PP Ups used
0x21 gb[11] nickname, 0x2C gb[11] original trainer name

Stats are not stored: GbSave_LoadBattleMonRecord recomputes them with Pokemon_RecalcStats (Gen 1 formula).
"""
from . import gamedata
from .schema import Codec, Text

SIZE = 0x37
_NAME = Text(11, "gb")
STAT_KEYS = ("hp", "atk", "def", "spd", "spc")


def species_lookup(value):
    """Name or dex number -> dex number."""
    if isinstance(value, int):
        return value
    v = str(value).strip().upper()
    if v.isdigit():
        return int(v)
    for dex, name in enumerate(gamedata.SPECIES):
        if dex and name.upper() == v:
            return dex
    # tolerate missing accents / punctuation (POKé -> POKE, NIDORAN♀ etc.)
    norm = lambda s: "".join(ch for ch in s.upper().replace("é", "E").replace("É", "E") if ch.isalnum())
    for dex, name in enumerate(gamedata.SPECIES):
        if dex and norm(name) == norm(v):
            return dex
    raise ValueError("unknown species %r" % value)


def move_lookup(value):
    if isinstance(value, int):
        return value
    v = str(value).strip().upper()
    if v.isdigit():
        return int(v)
    norm = lambda s: "".join(ch for ch in s.upper() if ch.isalnum())
    for mid, name in enumerate(gamedata.MOVES):
        if mid and norm(name) == norm(v):
            return mid
    raise ValueError("unknown move %r" % value)


def species_name(dex):
    return gamedata.SPECIES[dex] if 0 < dex <= 151 else None


def index_to_dex(index):
    if 0 < index < len(gamedata.INDEX_TO_DEX):
        d = gamedata.INDEX_TO_DEX[index]
        return d if 0 < d <= 151 else None
    return None


def dex_to_index(dex):
    return gamedata.DEX_TO_INDEX[dex] if 0 < dex <= 151 else 0


def hp_dv(dvs):
    return ((dvs["atk"] & 1) << 3) | ((dvs["def"] & 1) << 2) | ((dvs["spd"] & 1) << 1) | (dvs["spc"] & 1)


def stat_exp_term(se):
    """StatExpSqrt: smallest i in 1..255 with i*i >= se."""
    i = 1
    while i < 255 and i * i < se:
        i += 1
    return i // 4


def calc_stats(dex, level, dvs, stat_exp):
    """Pokemon_RecalcStats / CalculateStatValue (src/gb_data.c), capped at 999."""
    base = gamedata.BASE[dex]
    out = {}
    dv = {"hp": hp_dv(dvs), "atk": dvs["atk"], "def": dvs["def"], "spd": dvs["spd"], "spc": dvs["spc"]}
    for k, b in zip(STAT_KEYS, base[:5]):
        v = (stat_exp_term(stat_exp[k]) + (b + dv[k]) * 2) * level // 100
        v += level + 10 if k == "hp" else 5
        out[k] = min(v, 999)
    return out


def exp_for_level(dex, level):
    """Pokemon_ExpForLevel growth formulas (growth byte from the species table)."""
    g = gamedata.BASE[dex][10]
    n = level
    if g == 0:
        return n ** 3
    if g == 1:
        return n ** 3 * 3 // 4 + 10 * n * n - 30
    if g == 2:
        return n ** 3 * 3 // 4 + 20 * n * n - 30
    if g == 3:
        return n ** 3 * 6 // 5 - 15 * n * n + 100 * n - 140
    if g == 4:
        return n ** 3 * 4 // 5
    return n ** 3 * 5 // 4


def level_for_exp(dex, exp):
    lv = 1
    while lv < 100 and exp_for_level(dex, lv + 1) <= exp:
        lv += 1
    return lv


def max_pp(move, pp_up=0):
    base = gamedata.MOVE_DATA[move][4] if 0 < move < len(gamedata.MOVE_DATA) else 0
    return base + (base // 5) * pp_up


class MonCodec(Codec):
    size = SIZE

    def decode(self, data):
        d = bytes(data)
        out = {}
        dex = index_to_dex(d[0])
        if dex is not None and dex_to_index(dex) == d[0]:
            out["species"] = gamedata.SPECIES[dex]
        else:
            out["species_index"] = d[0]
        out["level"] = d[3]
        out["hp"] = int.from_bytes(d[1:3], "big")
        out["status"] = d[4]
        out["type1"] = d[5]
        out["type2"] = d[6]
        out["catch_rate"] = d[7]
        moves = []
        for i in range(4):
            mid, pp = d[8 + i], d[0x1D + i]
            if mid == 0 and pp == 0:
                moves.append(None)
                continue
            name = gamedata.MOVES[mid] if 0 < mid <= 165 else mid
            moves.append({"move": name, "pp": pp & 0x3F, "pp_up": pp >> 6})
        out["moves"] = moves
        out["ot_id"] = int.from_bytes(d[0xC:0xE], "big")
        out["exp"] = int.from_bytes(d[0xE:0x11], "big")
        out["stat_exp"] = {k: int.from_bytes(d[0x11 + 2 * i:0x13 + 2 * i], "big") for i, k in enumerate(STAT_KEYS)}
        dvw = int.from_bytes(d[0x1B:0x1D], "big")
        out["dvs"] = {"atk": dvw >> 12, "def": (dvw >> 8) & 15, "spd": (dvw >> 4) & 15, "spc": dvw & 15}
        out["nickname"] = _NAME.decode(d[0x21:0x2C])
        out["ot_name"] = _NAME.decode(d[0x2C:0x37])
        if dex is not None:
            info = {"dex": dex, "hp_dv": hp_dv(out["dvs"]),
                    "stats": calc_stats(dex, d[3], out["dvs"], out["stat_exp"]),
                    "types": [gamedata.TYPES.get(d[5]), gamedata.TYPES.get(d[6])],
                    "level_from_exp": level_for_exp(dex, out["exp"])}
            out["_info"] = info
        return out

    def encode(self, v):
        b = bytearray(SIZE)
        if "species_index" in v:
            b[0] = v["species_index"]
        else:
            b[0] = dex_to_index(species_lookup(v["species"]))
        b[1:3] = int(v.get("hp", 0)).to_bytes(2, "big")
        b[3] = v.get("level", 1)
        b[4] = v.get("status", 0)
        b[5] = v.get("type1", 0)
        b[6] = v.get("type2", 0)
        b[7] = v.get("catch_rate", 0)
        moves = list(v.get("moves", [])) + [None] * 4
        for i, m in enumerate(moves[:4]):
            if m is None:
                continue
            if not isinstance(m, dict):
                m = {"move": m}
            mid = move_lookup(m["move"])
            pp_up = m.get("pp_up", 0)
            pp = m.get("pp", max_pp(mid, pp_up))
            b[8 + i] = mid
            b[0x1D + i] = (pp & 0x3F) | ((pp_up & 3) << 6)
        b[0xC:0xE] = int(v.get("ot_id", 0)).to_bytes(2, "big")
        b[0xE:0x11] = int(v.get("exp", 0)).to_bytes(3, "big")
        se = v.get("stat_exp", {})
        for i, k in enumerate(STAT_KEYS):
            b[0x11 + 2 * i:0x13 + 2 * i] = int(se.get(k, 0)).to_bytes(2, "big")
        dv = v.get("dvs", {})
        dvw = (dv.get("atk", 0) << 12) | (dv.get("def", 0) << 8) | (dv.get("spd", 0) << 4) | dv.get("spc", 0)
        b[0x1B:0x1D] = dvw.to_bytes(2, "big")
        b[0x21:0x2C] = _NAME.encode(v.get("nickname", ""))
        b[0x2C:0x37] = _NAME.encode(v.get("ot_name", ""))
        return bytes(b)


MON = MonCodec()


def make_mon(species, level, moves=None, nickname=None, ot_name="STADIUM", ot_id=0, dvs=None, stat_exp=None):
    """A consistent new Pokémon: experience for the level, full HP and PP, species types, default moves."""
    dex = species_lookup(species)
    base = gamedata.BASE[dex]
    dvs = dvs or {"atk": 15, "def": 15, "spd": 15, "spc": 15}
    stat_exp = stat_exp or {k: 0 for k in STAT_KEYS}
    if moves is None:
        moves = [m for m in base[9] if m]
    mlist = []
    for m in moves[:4]:
        mid = move_lookup(m)
        mlist.append({"move": gamedata.MOVES[mid], "pp": max_pp(mid), "pp_up": 0})
    stats = calc_stats(dex, level, dvs, stat_exp)
    return {
        "species": gamedata.SPECIES[dex], "level": level, "hp": stats["hp"], "status": 0,
        "type1": base[5], "type2": base[6], "catch_rate": base[7], "moves": mlist + [None] * (4 - len(mlist)),
        "ot_id": ot_id, "exp": exp_for_level(dex, level), "stat_exp": stat_exp, "dvs": dvs,
        "nickname": nickname if nickname is not None else gamedata.SPECIES[dex], "ot_name": ot_name,
    }


def validate(mon):
    """Problems the game would show or trip over; returns a list of strings."""
    errs = []
    try:
        dex = species_lookup(mon["species"]) if "species" in mon else index_to_dex(mon["species_index"])
    except ValueError as e:
        return [str(e)]
    if dex is None:
        return ["invalid species index"]
    if not 1 <= mon["level"] <= 100:
        errs.append("level %d outside 1..100" % mon["level"])
    stats = calc_stats(dex, mon["level"], mon["dvs"], mon["stat_exp"])
    if mon["hp"] > stats["hp"]:
        errs.append("current HP %d above max HP %d" % (mon["hp"], stats["hp"]))
    for m in mon["moves"]:
        if m and not 0 < move_lookup(m["move"]) <= 165:
            errs.append("invalid move %r" % m["move"])
    if not any(mon["moves"]):
        errs.append("no moves")
    return errs

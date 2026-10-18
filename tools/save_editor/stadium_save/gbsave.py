"""Pokémon Red / Blue / Yellow battery saves (.sav, 32 KiB), international layout (English, German, French, ...).

Layout from pret/pokered (sram.asm, wram.asm); Japanese saves differ and are refused by the main checksum check.
Bank 1 (0x2000..0x3FFF) holds the main data (player, bag, money, badges, Pokédex, party, the *current* PC box) with
one checksum over 0x2598..0x3522 at 0x3523. Banks 2 and 3 hold PC boxes 1-6 and 7-12 (0x462 bytes each), with a
checksum over the six boxes and one per box. Checksum = ~(sum of bytes) & 0xFF.

The current box lives at 0x30C0; its slot in bank 2/3 is a stale copy until the game switches boxes. Bit 7 of the
current-box byte (0x284C) says the bank boxes were initialized; before that the game empties them on the first box
switch, so they are shown as empty here and initialized when one is written.

Pokémon use the same dict format as the Stadium save (pokemon.MON: the 33-byte Gen 1 box structure plus nickname and
OT name), so they move between the two unchanged. Party Pokémon get their level and stats recomputed on writing.
"""
import copy

from . import gamedata, pokemon
from .schema import Text

SIZE = 0x8000
NAME = Text(11, "gb")

PLAYER_NAME = 0x2598
POKEDEX_OWNED = 0x25A3
POKEDEX_SEEN = 0x25B6
BAG = 0x25C9           # count, 20 x (item, quantity), 0xFF
MONEY = 0x25F3         # 3 bytes BCD
RIVAL_NAME = 0x25F6
BADGES = 0x2602
PLAYER_ID = 0x2605
PIKACHU_HAPPINESS = 0x271C  # Yellow only
PC_ITEMS = 0x27E6      # count, 50 x (item, quantity), 0xFF
CURRENT_BOX = 0x284C   # bits 0-6 box index, bit 7 boxes initialized
PLAYTIME = 0x2CED      # hours, maxed flag, minutes, seconds, frames
PARTY = 0x2F2C
CURRENT_BOX_DATA = 0x30C0
MAIN_CHECKSUM = 0x3523
BOX_SIZE = 0x462
BANK_BOXES = (0x4000, 0x6000)  # boxes 0-5, 6-11
BADGE_NAMES = ("Boulder", "Cascade", "Thunder", "Rainbow", "Soul", "Marsh", "Volcano", "Earth")


def checksum(data):
    return ~sum(data) & 0xFF


def box_offset(i):
    return BANK_BOXES[i // 6] + (i % 6) * BOX_SIZE


def _bcd(b):
    return int(b.hex()) if all(x >> 4 < 10 and x & 15 < 10 for x in b) else 0


def _to_bcd(v):
    return bytes.fromhex("%06d" % max(0, min(999999, int(v))))


class _MonList:
    """Party (capacity 6, 44-byte structures) or PC box (capacity 20, 33-byte structures)."""

    def __init__(self, capacity, mon_size):
        self.capacity, self.mon_size = capacity, mon_size
        self.mons = 2 + capacity
        self.ots = self.mons + capacity * mon_size
        self.nicks = self.ots + capacity * 11
        self.size = self.nicks + capacity * 11

    def decode(self, d):
        n = d[0] if d[0] <= self.capacity else 0
        out = []
        for i in range(n):
            core = d[self.mons + i * self.mon_size:self.mons + i * self.mon_size + 33]
            nick = d[self.nicks + i * 11:self.nicks + (i + 1) * 11]
            ot = d[self.ots + i * 11:self.ots + (i + 1) * 11]
            out.append(pokemon.MON.decode(bytes(core) + bytes(nick) + bytes(ot)))
        return out

    def encode(self, old, mons):
        if len(mons) > self.capacity:
            raise ValueError("at most %d Pokémon" % self.capacity)
        d = bytearray(old)
        d[0] = len(mons)
        for i in range(self.capacity + 1):
            d[1 + i] = 0xFF if i == len(mons) else (d[1 + i] if i > len(mons) else 0)
        for i, m in enumerate(mons):
            raw = pokemon.MON.encode(m)
            d[1 + i] = raw[0]
            core = bytearray(raw[:33])
            if self.mon_size == 44:  # party: level and stats
                dex = pokemon.index_to_dex(raw[0])
                st = pokemon.calc_stats(dex, m["level"], m["dvs"], m["stat_exp"])
                core += bytes([m["level"]]) + b"".join(st[k].to_bytes(2, "big") for k in pokemon.STAT_KEYS)
            d[self.mons + i * self.mon_size:self.mons + (i + 1) * self.mon_size] = core
            for off, name in ((self.ots + i * 11, raw[0x2C:0x37]), (self.nicks + i * 11, raw[0x21:0x2C])):
                n = name.index(0x50) + 1 if 0x50 in name else 11  # like the game: up to the terminator, the
                d[off:off + n] = name[:n]                          # bytes after it stay
        return bytes(d)


PARTY_LIST = _MonList(6, 44)
BOX_LIST = _MonList(20, 33)


def _items(d, cap):
    n = d[0] if d[0] <= cap else 0
    return [{"item": d[1 + 2 * i], "quantity": d[2 + 2 * i]} for i in range(n)]


def _items_encode(old, items, cap):
    if len(items) > cap:
        raise ValueError("there is room for %d items at most" % cap)
    d = bytearray(old)
    d[0] = len(items)
    for i, it in enumerate(items):
        d[1 + 2 * i] = int(it["item"])
        d[2 + 2 * i] = max(1, min(99, int(it["quantity"])))
    d[1 + 2 * len(items)] = 0xFF
    return bytes(d)


def _flags(d):
    return [dex for dex in range(1, 152) if d[(dex - 1) >> 3] & (1 << ((dex - 1) & 7))]


def _flags_encode(dexes):
    d = bytearray(19)
    for dex in dexes:
        d[(dex - 1) >> 3] |= 1 << ((dex - 1) & 7)
    return bytes(d)


class GBSave:
    def __init__(self, data):
        if len(data) != SIZE:
            raise ValueError("a Game Boy Pokémon save is %d bytes, got %d" % (SIZE, len(data)))
        self.data = bytearray(data)

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            return cls(f.read())

    def save(self, path):
        with open(path, "wb") as f:
            f.write(self.data)

    # ---- checks ----
    def main_valid(self):
        return checksum(self.data[PLAYER_NAME:MAIN_CHECKSUM]) == self.data[MAIN_CHECKSUM]

    def boxes_initialized(self):
        return bool(self.data[CURRENT_BOX] & 0x80)

    @property
    def current_box(self):
        return self.data[CURRENT_BOX] & 0x7F if (self.data[CURRENT_BOX] & 0x7F) < 12 else 0

    def fix_checksums(self, banks=(0, 1)):
        """Main checksum, and the box checksums of the given banks (0: boxes 1-6, 1: boxes 7-12)."""
        self.data[MAIN_CHECKSUM] = checksum(self.data[PLAYER_NAME:MAIN_CHECKSUM])
        for base in [BANK_BOXES[b] for b in banks]:
            for i in range(6):
                self.data[base + 0x1A4D + i] = checksum(self.data[base + i * BOX_SIZE:base + (i + 1) * BOX_SIZE])
            self.data[base + 0x1A4C] = checksum(self.data[base:base + 0x1A4C])

    # ---- structured access ----
    def box_bytes(self, i):
        if i == self.current_box:
            return self.data[CURRENT_BOX_DATA:CURRENT_BOX_DATA + BOX_SIZE]
        if not self.boxes_initialized():
            return bytes([0, 0xFF]) + bytes(BOX_SIZE - 2)
        return self.data[box_offset(i):box_offset(i) + BOX_SIZE]

    def to_dict(self):
        d = self.data
        ptime = d[PLAYTIME:PLAYTIME + 5]
        return {
            "format": "pokemon-gen1-sav", "valid": self.main_valid(),
            "trainer": {
                "name": NAME.decode(d[PLAYER_NAME:PLAYER_NAME + 11]),
                "rival": NAME.decode(d[RIVAL_NAME:RIVAL_NAME + 11]),
                "id": int.from_bytes(d[PLAYER_ID:PLAYER_ID + 2], "big"),
                "money": _bcd(d[MONEY:MONEY + 3]),
                "badges": d[BADGES],
                "pikachu_happiness": d[PIKACHU_HAPPINESS],
                "playtime": {"hours": ptime[0], "minutes": ptime[2], "seconds": ptime[3]},
            },
            "pokedex": {"owned": _flags(d[POKEDEX_OWNED:POKEDEX_OWNED + 19]),
                        "seen": _flags(d[POKEDEX_SEEN:POKEDEX_SEEN + 19])},
            "bag": _items(d[BAG:BAG + 42], 20),
            "pc_items": _items(d[PC_ITEMS:PC_ITEMS + 102], 50),
            "party": PARTY_LIST.decode(d[PARTY:PARTY + PARTY_LIST.size]),
            "current_box": self.current_box,
            "boxes": [BOX_LIST.decode(self.box_bytes(i)) for i in range(12)],
        }

    def _write_box(self, i, mons):
        if not self.boxes_initialized():  # what the game does on the first box switch: empty all bank boxes
            for j in range(12):
                o = box_offset(j)
                self.data[o:o + BOX_SIZE] = bytes([0, 0xFF]) + bytes(BOX_SIZE - 2)
            self.data[CURRENT_BOX] |= 0x80
            self._banks |= {0, 1}
        self._banks.add(i // 6)
        raw = BOX_LIST.encode(self.box_bytes(i), mons)
        if i == self.current_box:
            self.data[CURRENT_BOX_DATA:CURRENT_BOX_DATA + BOX_SIZE] = raw
        self.data[box_offset(i):box_offset(i) + BOX_SIZE] = raw

    def apply(self, base, edited):
        """Write the parts of `edited` that differ from `base` (both to_dict() results); untouched bytes stay."""
        d = self.data
        self._banks = set()
        bt, et = base["trainer"], edited["trainer"]
        for key, off in (("name", PLAYER_NAME), ("rival", RIVAL_NAME)):
            if et[key] != bt[key]:
                d[off:off + 11] = NAME.encode(et[key])
        if et["id"] != bt["id"]:
            d[PLAYER_ID:PLAYER_ID + 2] = int(et["id"]).to_bytes(2, "big")
        if et["money"] != bt["money"]:
            d[MONEY:MONEY + 3] = _to_bcd(et["money"])
        if et["badges"] != bt["badges"]:
            d[BADGES] = int(et["badges"]) & 0xFF
        if et.get("pikachu_happiness") != bt.get("pikachu_happiness"):
            d[PIKACHU_HAPPINESS] = max(0, min(255, int(et["pikachu_happiness"])))
        for key, off in (("owned", POKEDEX_OWNED), ("seen", POKEDEX_SEEN)):
            if sorted(edited["pokedex"][key]) != sorted(base["pokedex"][key]):
                d[off:off + 19] = _flags_encode(edited["pokedex"][key])
        if edited["bag"] != base["bag"]:
            d[BAG:BAG + 42] = _items_encode(d[BAG:BAG + 42], edited["bag"], 20)
        if edited["pc_items"] != base["pc_items"]:
            d[PC_ITEMS:PC_ITEMS + 102] = _items_encode(d[PC_ITEMS:PC_ITEMS + 102], edited["pc_items"], 50)
        if edited["party"] != base["party"]:
            if not edited["party"]:
                raise ValueError("the party needs at least one Pokémon")
            d[PARTY:PARTY + PARTY_LIST.size] = PARTY_LIST.encode(d[PARTY:PARTY + PARTY_LIST.size], edited["party"])
        for i in range(12):
            if edited["boxes"][i] != base["boxes"][i]:
                self._write_box(i, edited["boxes"][i])
        # a Pokémon that arrives counts as seen and caught, as after a trade
        dex_new = {pokemon.species_lookup(m["species"]) for m in edited["party"] + sum(edited["boxes"], [])
                   if "species" in m}
        dex_old = {pokemon.species_lookup(m["species"]) for m in base["party"] + sum(base["boxes"], [])
                   if "species" in m}
        arrived = dex_new - dex_old
        if arrived:
            for key, off in (("owned", POKEDEX_OWNED), ("seen", POKEDEX_SEEN)):
                cur = set(_flags(d[off:off + 19]))
                d[off:off + 19] = _flags_encode(cur | arrived)
        self.fix_checksums(sorted(self._banks))
        return self


def apply_edit(data, base, edited):
    """Bytes of a GB save after applying an edit (base/edited dicts)."""
    return bytes(GBSave(data).apply(copy.deepcopy(base), copy.deepcopy(edited)).data)


def game_info(rom_path=None, save_path=""):
    """Title and language of the cartridge, for labels and names (German saves use Ä Ö Ü ä ö ü)."""
    title, kind = "Pokémon (Game Boy)", ""
    german = any(s in (rom_path or "") + save_path for s in ("Germany", "(G)", "_de.", "_de/"))
    if rom_path:
        try:
            with open(rom_path, "rb") as f:
                head = f.read(0x150)
            t = head[0x134:0x144].decode("ascii", "replace").upper()
            for key, k, en, de in (("YEL", "yellow", "Pokémon Yellow", "Pokémon Gelb"),
                                   ("RED", "red", "Pokémon Red", "Pokémon Rot"),
                                   ("BLUE", "blue", "Pokémon Blue", "Pokémon Blau"),
                                   ("GREEN", "green", "Pokémon Green", "Pokémon Grün")):
                if key in t:
                    title, kind = (de if german else en), k
                    break
        except OSError:
            pass
    return {"title": title, "kind": kind, "lang": "de" if german else "en"}


ITEM_NAMES = gamedata.ITEMS

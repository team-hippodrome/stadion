"""Pokémon Stadium (US) FlashRAM save: banks, records, footers, JSON.

Layout (see SAVE_FORMAT.md): a 128 KiB FlashRAM. Four banks of up to 0x4000 bytes; bank n's primary copy is at
n * 0x4000 and its backup copy at 0x10000 + n * 0x4000 (Save_FlushBank / Save_AllocateBank in src/save_data.c).
Each bank holds fixed-size records; each record ends with a 6-byte footer:
    'POKE' + u16 checksum            committed record (Save_WriteFooter(..., 1))
    'XXXX' + u16 (checksum ^ 0x00FF) record mid-transaction (Save_InvalidateRecord); the game discards it
The checksum is the 16-bit sum of all record bytes except the last two (Save_Checksum), magic included.
"""
import copy
import json

from . import gamedata, pokemon
from .schema import Array, Bytes, F32, Raw, Struct, Text, U8, U16, S16, U32, raw_from_json, raw_to_json

FLASH_SIZE = 0x20000
BANK_STRIDE = 0x4000
BACKUP_BASE = 0x10000
BANK_SIZES = (0x3700, 0x3700, 0x3F80, 0x3880)  # Save_FlushBank D_80073650

MAGIC_VALID = b"POKE"
MAGIC_PENDING = b"XXXX"

# Registered-team categories = SessionContext.ruleCategory (src/game_state.h). Category c uses group c of the
# active record bank (Save_SetActiveRecordBank): bank c // 4, 10 slots of 0x160 bytes at (c % 4) * 0xDC0.
TEAM_CATEGORIES = (
    "free_battle",        # 0 RULE_CATEGORY_ANYTHING_GOES (Free Battle "ANYTHING GOES")
    "category_1",         # 1 RULE_CATEGORY_NINTENDO_CUP_97: listed as a hidden "POKé CUP" in stadium_select
    "category_2",         # 2 RULE_CATEGORY_NINTENDO_CUP_98: same
    "poke_cup",           # 3
    "petit_cup",          # 4
    "pika_cup",           # 5
    "prime_cup",          # 6
    "gym_leader_castle",  # 7
    "mewtwo",             # 8 RULE_CATEGORY_VS_MEWTWO
)

# SessionContext.mode values (text table 0x0E entries mode - 1; Session_SetMode callers)
MODES = {0: "Free Battle", 1: "Poké Cup (hidden copy 1)", 2: "Poké Cup (hidden copy 2)", 3: "Poké Cup",
         4: "Petit Cup", 5: "Pika Cup", 6: "Prime Cup", 7: "Gym Leader Castle", 8: "Vs. Mewtwo",
         10: "Event Battle"}
CUP_DIVISIONS = ("Poké Ball", "Great Ball", "Ultra Ball", "Master Ball")
GLC_STAGES = ("Pewter Gym", "Cerulean Gym", "Vermilion Gym", "Celadon Gym", "Fuchsia Gym", "Saffron Gym",
              "Cinnabar Gym", "Viridian Gym", "Elite Four", "Rival")

GBNAME = Text(11, "gb")

# ---- record layouts (offsets inside a record; the last 6 bytes are the footer and not part of these) ----

HEADER_FIELDS = [  # DeckRecordHeader
    (0x00, "flags", U8),          # bit0: slot in use (Deck_IsSaveEntryPresent), bit1: modified since commit
    (0x01, "name", GBNAME),       # trainer name (Deck_SetSaveEntryTrainerIdAndName)
    (0x0C, "trainer_id", U16),
    (0x0E, "count", S16),         # number of entries (Deck_GetSaveEntryCount)
]


def _photo_struct():
    vec = Array(F32(), 3)
    return Struct(0x8C, [  # unk_D_83403C60 / unk_D_83402EE0 (src/gallery.h)
        (0x00, "y_position", F32()),   # model Y offset (Gallery_InitializePhotoModel)
        (0x04, "subject", U8),         # 1..151 = species, 0x99 = drawn with Pikachu's model; else empty slot
        (0x05, "animation", U8),       # ModelAnim_SetAnimation
        (0x06, "anim_frame", S16),     # ModelAnim_SetFrame
        (0x08, "event_track", U8),     # ModelAnim_SetEventTrack
        (0x09, "event_frame", U8),     # ModelAnim_SetEventFrame
        (0x0A, "slot_data", Raw(0x46)),  # Particle31_UnpackSlotData input
        (0x50, "camera_a", vec),
        (0x5C, "camera_b", vec),
        (0x68, "word_68", U32),
        (0x6C, "flags", U8),           # record 20 of the photo set: bit0/bit1 gallery mode flags
        (0x70, "ot_id", U16),          # Gallery_CopyPhotoMonFromBattleMon
        (0x72, "species", U8),
        (0x73, "nickname", Text(11, "n64")),
        (0x7E, "ot_name", Text(11, "n64")),
    ], empty_is_null=True)


PHOTO = _photo_struct()

VICTORY_ENTRY = Struct(0x1A, [  # unk_D_800AE4E8_004_2_0DC0_002, VictoryPalace_SaveSpeciesRecord
    (0x00, "mode", U8),          # SessionContext.mode of the win, +0x80 when won in Round 2; 0 = no statue
    (0x01, "level", U8),
    (0x02, "ot_id", U16),
    (0x04, "nickname", GBNAME),
    (0x0F, "ot_name", GBNAME),
], empty_is_null=True)

CONTINUE_DATA = Struct(0x20, [  # SessionContinueData (Session_SaveContinueData, src/session.c)
    (0x00, "mode", U8),
    (0x01, "mode_selection", U8),  # cup division or Gym Leader Castle stage
    (0x02, "battle", U8),          # one-based round
    (0x03, "round2", U8),          # SessionContext.unk_11F2
    (0x04, "continues", U8),
    (0x05, "opponent_history", Bytes(8)),
    (0x0D, "gb_starter_choice", U8),
    (0x0E, "trainer_id", U16),
    (0x10, "player_name", Text(16, "gb")),
])

MODE_PROGRESS = Struct(8, [  # ModeSettings (BattlePrep_MarkCupCleared / BattlePrep_MarkNewRoundRecord)
    (0x00, "cleared_modes", U16),  # bit (1 << mode) per cleared mode; 0xF8 = all cups + Castle
    (0x04, "gym_leader_castle", U8),  # Castle stages cleared (0..8)
    (0x05, "poke_cup", U8),        # Poké Cup divisions cleared (0..4)
    (0x06, "prime_cup", U8),       # Prime Cup divisions cleared (0..4)
    (0x07, "mewtwo_unlocked", U8),  # set when cleared_modes reaches 0xF8
])

OPTIONS = Struct(0x22, [  # SaveOptionsRecord
    (0x00, "flags", U8),
    (0x02, "sound", U16),            # bit0 mono, bit1 voice off (Options_ApplySoundSettings)
    (0x04, "gb_tower_speed", S16),   # 0 normal, 1 Doduo, 2 Dodrio (BattlePrep_MarkFirstClearBonus)
    (0x08, "gb_tower_word", U32),    # value carried into/out of GB Tower (State_GbTower); role unknown
    (0x0C, "progress", Array(MODE_PROGRESS, 2)),  # [Round 1, Round 2]
    (0x1C, "bonus_pokemon", U16),    # dex | 0x100 (Round 2); collected at the Lab PC, then cleared
    (0x1E, "kids_club", U8),         # bit0: Hyper difficulty unlocked
    (0x1F, "bonus_flag", U8),        # 1 = bonus Pokémon came from session mode 0x1A
])


class RecordType:
    """A record kind: size, how to decode its body (size - 6 bytes) and its reset contents."""

    def __init__(self, key, size, body, reset_flags):
        self.key, self.size, self.body, self.reset_flags = key, size, body, reset_flags


class ListBody:
    """DeckRecordHeader + count entries of a codec + raw rest."""

    def __init__(self, size, codec, capacity):
        self.size, self.codec, self.capacity = size, codec, capacity
        self.header = Struct(0x10, HEADER_FIELDS)
        self.tail_off = 0x10 + codec.size * capacity

    def decode(self, data):
        h = self.header.decode(data[:0x10])
        count = h["count"]
        n = max(0, min(count, self.capacity))
        es = self.codec.size
        entries = [self.codec.decode(data[0x10 + i * es:0x10 + (i + 1) * es]) for i in range(n)]
        out = dict(h)
        del out["count"]
        out["entries"] = entries
        if n != count:
            out["count_raw"] = count
        unused = data[0x10 + n * es:self.tail_off]
        if any(unused):
            out["_unused_entries"] = raw_to_json(unused)
        tail = data[self.tail_off:]
        if any(tail):
            out["_tail"] = raw_to_json(tail)
        return out

    def encode(self, v):
        buf = bytearray(self.size)
        entries = v.get("entries", [])
        if len(entries) > self.capacity:
            raise ValueError("at most %d entries" % self.capacity)
        h = {k: v[k] for k in ("flags", "name", "trainer_id") if k in v}
        h["count"] = v.get("count_raw", len(entries))
        buf[:0x10] = self.header.encode(h)
        es = self.codec.size
        if "_unused_entries" in v:
            un = raw_from_json(v["_unused_entries"])
            start = 0x10 + len(entries) * es
            un = un[:max(0, self.tail_off - start)]  # more entries than when decoded: less room behind them
            buf[start:start + len(un)] = un
        for i, e in enumerate(entries):
            buf[0x10 + i * es:0x10 + (i + 1) * es] = self.codec.encode(e)
        if "_tail" in v:
            t = raw_from_json(v["_tail"], self.size - self.tail_off)
            buf[self.tail_off:] = t
        return bytes(buf)


class ItemCodec:
    size = 2

    def decode(self, d):
        name = gamedata.ITEMS[d[0]] if d[0] < len(gamedata.ITEMS) and gamedata.ITEMS[d[0]] else None
        out = {"item": d[0], "quantity": d[1]}
        if name:
            out["_name"] = name
        return out

    def encode(self, v):
        item = v["item"]
        if isinstance(item, str):
            item = next(i for i, n in enumerate(gamedata.ITEMS) if n and n.upper() == item.upper())
        return bytes([item, v["quantity"]])


class StructBody:
    def __init__(self, size, struct):
        self.size, self.struct = size, struct

    def decode(self, data):
        return self.struct.decode(data)

    def encode(self, v):
        return self.struct.encode(v)


def _flagged_array(size, entry, count):
    """u8 flags at 0, unknown byte at 1, entry array at 2 (Victory Palace, gallery photo sets)."""
    return StructBody(size, Struct(size, [(0x00, "flags", U8), (0x02, "entries", Array(entry, count))]))


TEAM = RecordType("team", 0x160, ListBody(0x15A, pokemon.MON, 6), 0)
BOX = RecordType("box", 0x468, ListBody(0x462, pokemon.MON, 20), 0)
ITEM_BOX = RecordType("item_box", 0xE0, ListBody(0xDA, ItemCodec(), 100), 0)
VICTORY = RecordType("victory_palace", 0xF60, _flagged_array(0xF5A, VICTORY_ENTRY, 151), 1)
CONTINUE = RecordType("continue", 0x180, StructBody(0x17A, Struct(0x17A, [
    (0x00, "flags", U8),            # bit0: a saved cup/Castle run exists (Save_ContinueRecordExists)
    (0x02, "session", CONTINUE_DATA),
    (0x22, "count", S16),
    (0x24, "pokemon", Array(Raw(0x37), 6)),
    (0x16E, "battle_byte_52", Bytes(6)),  # BattleMon.unk_52 per Pokémon
    (0x174, "battle_byte_53", Bytes(6)),  # BattleMon.unk_53 per Pokémon
])), 0)
OPTIONS_REC = RecordType("options", 0x28, StructBody(0x22, OPTIONS), 1)
ALBUM = RecordType("album", 0x13B8, _flagged_array(0x13B2, PHOTO, 36), 1)
PHOTO_SET = RecordType("photos", 0xB84, _flagged_array(0xB7E, PHOTO, 21), 1)


class RecordRef:
    def __init__(self, path, rtype, bank, offset):
        self.path, self.type, self.bank, self.offset = path, rtype, bank, offset

    @property
    def primary(self):
        return self.bank * BANK_STRIDE + self.offset

    @property
    def backup(self):
        return BACKUP_BASE + self.bank * BANK_STRIDE + self.offset


def _layout():
    recs = []
    for c, name in enumerate(TEAM_CATEGORIES):
        for s in range(10):
            recs.append(RecordRef(("teams", name, s), TEAM, c // 4, (c % 4) * 0xDC0 + s * 0x160))
    recs.append(RecordRef(("victory_palace",), VICTORY, 2, 0x0DC0))
    recs.append(RecordRef(("continue",), CONTINUE, 2, 0x1D20))
    recs.append(RecordRef(("options",), OPTIONS_REC, 2, 0x1EA0))
    recs.append(RecordRef(("album",), ALBUM, 2, 0x1EC8))
    recs.append(RecordRef(("photos",), PHOTO_SET, 2, 0x3280))
    for i in range(12):
        recs.append(RecordRef(("boxes", i), BOX, 3, i * 0x468))
    for i in range(4):
        recs.append(RecordRef(("item_boxes", i), ITEM_BOX, 3, 0x34E0 + i * 0xE0))
    return recs


RECORDS = _layout()
BANK_RECORD_END = (0x3700, 0x3700, 0x3E04, 0x3860)


def checksum(rec):
    return sum(rec[:-2]) & 0xFFFF


def footer_state(rec):
    m, s = bytes(rec[-6:-2]), int.from_bytes(rec[-2:], "big")
    if m == MAGIC_VALID and s == checksum(rec):
        return "valid"
    if m == MAGIC_PENDING and s == checksum(rec) ^ 0xFF:
        return "pending"
    return "invalid"


def write_footer(rec, state):
    """rec: bytearray of the whole record; state 'valid' or 'pending'."""
    rec[-6:-2] = MAGIC_VALID if state == "valid" else MAGIC_PENDING
    s = checksum(rec)
    rec[-2:] = (s if state == "valid" else s ^ 0xFF).to_bytes(2, "big")


def reset_record(rtype):
    """Save_ClearTypedRecord followed by Save_CommitTypedRecord."""
    rec = bytearray(rtype.size)
    if rtype in (TEAM, BOX, ITEM_BOX):
        rec[1] = 0x50  # empty name (terminator only)
    rec[0] = rtype.reset_flags
    write_footer(rec, "valid")
    return bytes(rec)


def get_path(root, path):
    node = root
    for p in path:
        node = node[p]
    return node


def set_path(root, path, value):
    node = root
    for p in path[:-1]:
        node = node[p]
    node[path[-1]] = value


class SaveFile:
    def __init__(self, data=None):
        if data is None:
            data = new_save_bytes()
        if len(data) != FLASH_SIZE:
            raise ValueError("a Pokémon Stadium FlashRAM image is %d bytes, got %d" % (FLASH_SIZE, len(data)))
        self.data = bytearray(data)

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            return cls(f.read())

    def save(self, path):
        with open(path, "wb") as f:
            f.write(self.data)

    def record_bytes(self, ref, copy_="primary"):
        off = ref.primary if copy_ == "primary" else ref.backup
        return bytes(self.data[off:off + ref.type.size])

    def loaded_copy(self, ref):
        """Which copy Save_LoadWithRepair keeps: 'primary', 'backup' or 'reset' (both invalid)."""
        p = footer_state(self.record_bytes(ref)) == "valid"
        b = footer_state(self.record_bytes(ref, "backup")) == "valid"
        if p:
            return "primary"
        return "backup" if b else "reset"

    # ---- validation / repair ----
    def check(self):
        """List of (path, primary state, backup state, copy the game loads)."""
        out = []
        for ref in RECORDS:
            out.append((ref.path, footer_state(self.record_bytes(ref)), footer_state(self.record_bytes(ref, "backup")),
                        self.loaded_copy(ref)))
        return out

    def repair(self):
        """Apply Save_LoadWithRepair to every record, then mirror primary to backup (Save_FlushBank)."""
        changed = []
        for ref in RECORDS:
            which = self.loaded_copy(ref)
            if which == "primary":
                good = self.record_bytes(ref)
            elif which == "backup":
                good = self.record_bytes(ref, "backup")
            else:
                good = reset_record(ref.type)
            if good != self.record_bytes(ref) or good != self.record_bytes(ref, "backup"):
                changed.append(ref.path)
            self._write_record(ref, good)
        return changed

    def fix_checksums(self, include_pending=False):
        """Rewrite footers of records whose contents were edited by other tools: magic POKE + correct checksum.
        'pending' (XXXX) records are left alone unless include_pending (the game discards them)."""
        changed = []
        for ref in RECORDS:
            for copy_ in ("primary", "backup"):
                off = ref.primary if copy_ == "primary" else ref.backup
                rec = bytearray(self.data[off:off + ref.type.size])
                st = footer_state(rec)
                if st == "valid" or (st == "pending" and not include_pending):
                    continue
                write_footer(rec, "valid")
                self.data[off:off + ref.type.size] = rec
                changed.append((ref.path, copy_))
        return changed

    def _write_record(self, ref, rec):
        self.data[ref.primary:ref.primary + ref.type.size] = rec
        self.data[ref.backup:ref.backup + ref.type.size] = rec

    # ---- structured access ----
    def decode_record(self, ref, copy_="primary"):
        rec = self.record_bytes(ref, copy_)
        body = ref.type.body.decode(rec[:-6])
        st = footer_state(rec)
        body["_footer"] = st if st != "invalid" else {"raw": rec[-6:].hex()}
        if ref.type is CONTINUE:
            mons = []
            for raw in body["pokemon"][:max(0, min(body["count"], 6))]:
                mons.append(pokemon.MON.decode(raw_from_json(raw, 0x37)))
            body["pokemon"] = mons + body["pokemon"][len(mons):]
        return body

    def encode_record(self, ref, body):
        if ref.type is CONTINUE:
            body = dict(body)
            mons = list(body["pokemon"])
            for i, m in enumerate(mons):
                if isinstance(m, dict):
                    mons[i] = raw_to_json(pokemon.MON.encode(m))
            body["pokemon"] = mons
        rec = bytearray(ref.type.size)
        rec[:-6] = ref.type.body.encode(body)
        foot = body.get("_footer", "valid")
        if isinstance(foot, dict):
            rec[-6:] = bytes.fromhex(foot["raw"])
        else:
            write_footer(rec, foot)
        return bytes(rec)

    def set_record(self, ref, body, commit=True):
        """Store a decoded record body in both copies. commit: footer becomes 'valid' and the
        modified flag (bit1 of byte 0) is cleared, as Save_CommitRecord does."""
        body = copy.deepcopy(body)
        if commit:
            body["_footer"] = "valid"
            if "flags" in body:
                body["flags"] &= ~2
        rec = self.encode_record(ref, body)
        if rec != self.record_bytes(ref) or rec != self.record_bytes(ref, "backup"):
            self._write_record(ref, rec)

    def to_dict(self):
        out = {"format": "pokestadium-us-flashram", "version": 1}
        for ref in RECORDS:
            body = self.decode_record(ref)
            if self.record_bytes(ref, "backup") != self.record_bytes(ref):
                body["_backup"] = self.record_bytes(ref, "backup").hex()
            node = out
            for i, p in enumerate(ref.path[:-1]):
                node = node.setdefault(p, [] if isinstance(ref.path[i + 1], int) else {})
            key = ref.path[-1]
            if isinstance(key, int):
                node.append(body)
            else:
                node[key] = body
        banks = []
        for b in range(4):
            end = BANK_RECORD_END[b]
            prim = self.data[b * BANK_STRIDE + end:(b + 1) * BANK_STRIDE]
            back = self.data[BACKUP_BASE + b * BANK_STRIDE + end:BACKUP_BASE + (b + 1) * BANK_STRIDE]
            e = {"bank": b, "tail": raw_to_json(prim)}
            if back != prim:
                e["backup_tail"] = raw_to_json(back)
            banks.append(e)
        out["_banks"] = banks
        return out

    @classmethod
    def from_dict(cls, d):
        s = cls(bytes([0xFF]) * FLASH_SIZE)
        for b, e in enumerate(d["_banks"]):
            end = BANK_RECORD_END[b]
            n = BANK_STRIDE - end
            prim = raw_from_json(e["tail"], n)
            back = raw_from_json(e.get("backup_tail", e["tail"]), n)
            s.data[b * BANK_STRIDE + end:(b + 1) * BANK_STRIDE] = prim
            s.data[BACKUP_BASE + b * BANK_STRIDE + end:BACKUP_BASE + (b + 1) * BANK_STRIDE] = back
        for ref in RECORDS:
            body = get_path(d, ref.path)
            rec = s.encode_record(ref, body)
            s.data[ref.primary:ref.primary + ref.type.size] = rec
            back = bytes.fromhex(body["_backup"]) if "_backup" in body else rec
            s.data[ref.backup:ref.backup + ref.type.size] = back
        return s

    def to_json(self, indent=1):
        return json.dumps(self.to_dict(), indent=indent, ensure_ascii=False)

    @classmethod
    def from_json(cls, text):
        return cls.from_dict(json.loads(text))

    # ---- convenience ----
    def record(self, *path):
        for ref in RECORDS:
            if ref.path == tuple(path):
                return ref
        raise KeyError(path)

    def edit(self, fn):
        """Decode every record, let fn modify the dict, write back records whose contents changed (committed)."""
        d = self.to_dict()
        before = copy.deepcopy(d)
        fn(d)
        for ref in RECORDS:
            new, old = get_path(d, ref.path), get_path(before, ref.path)
            if new != old:
                new = dict(new)
                new.pop("_backup", None)
                self.set_record(ref, new)
        return self


def new_save_bytes():
    """The image the game writes for a blank chip: every record reset and committed in both copies,
    the rest of each 0x4000-byte sector erased (0xFF). Matches a real first save (tests/test_save.py)."""
    data = bytearray([0xFF]) * FLASH_SIZE
    for ref in RECORDS:
        rec = reset_record(ref.type)
        data[ref.primary:ref.primary + len(rec)] = rec
        data[ref.backup:ref.backup + len(rec)] = rec
    return bytes(data)

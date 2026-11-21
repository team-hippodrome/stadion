"""The save editor built into the game (src/port/save_editor, C++) against this Python library.

Build the C++ driver, then run these tests with its path (from tools/save_editor):
    cmake --build <build dir> --target stadium_save_cli
    STADIUM_SAVE_CLI=<build dir>/stadium_save_cli python3 -m unittest tests.test_native -v

Every check compares the two implementations: the same JSON for a save (dump), the same bytes for a save built from a
dump, after a repair, after fixing checksums, after the editor's edits (records committed as the game commits them),
for a new Pokémon from the editor's dialog, and for Game Boy saves. Skipped when STADIUM_SAVE_CLI is not set.
"""
import copy
import glob
import json
import math
import os
import random
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from stadium_save import make_mon, web  # noqa: E402
from stadium_save.gbsave import GBSave, apply_edit  # noqa: E402
from stadium_save.save import FLASH_SIZE, RECORDS, SaveFile, new_save_bytes, write_footer  # noqa: E402
from stadium_save.text import decode_gb, decode_n64, encode_gb, encode_n64  # noqa: E402

CLI = os.environ.get("STADIUM_SAVE_CLI")
SAVES = sorted(glob.glob(os.path.join(ROOT, "fixtures", "*.flash")) + glob.glob(os.path.join(ROOT, "verify", "saves", "*.flash"))
               + glob.glob(os.path.join(ROOT, "fixtures", "lab_transfer", "*.flash")))
GB_SAVES = sorted(glob.glob(os.path.join(ROOT, "fixtures", "lab_transfer", "*.sav")))
FIRST_SAVE = os.path.join(ROOT, "fixtures", "first_save.flash")


def no_inf(v):
    """JSON has no infinity: Python writes Infinity, nlohmann null. Random bytes give such floats (photo records)."""
    if isinstance(v, float) and math.isinf(v):
        return None
    if isinstance(v, dict):
        return {k: no_inf(x) for k, x in v.items()}
    if isinstance(v, list):
        return [no_inf(x) for x in v]
    return v


@unittest.skipUnless(CLI, "set STADIUM_SAVE_CLI to the stadium_save_cli binary")
class Native(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.n = 0

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, data=None, ext=".bin"):
        self.n += 1
        p = os.path.join(self.tmp.name, "f%d%s" % (self.n, ext))
        if data is not None:
            with open(p, "wb" if isinstance(data, (bytes, bytearray)) else "w") as f:
                f.write(data if isinstance(data, (bytes, bytearray)) else json.dumps(data, ensure_ascii=False))
        return p

    def run_cli(self, *args, ok=True):
        r = subprocess.run([CLI] + [str(a) for a in args], capture_output=True, text=True, encoding="utf-8")
        if ok:
            self.assertEqual(r.returncode, 0, r.stderr)
        return r

    def cli_json(self, *args):
        return json.loads(self.run_cli(*args).stdout)

    def read(self, p):
        with open(p, "rb") as f:
            return f.read()

    # ---- saves to compare on ----
    def random_saves(self):
        out = []
        for seed in range(3):  # noise: every byte is decoded or kept raw
            rnd = random.Random(seed)
            out.append(bytes(rnd.getrandbits(8) for _ in range(FLASH_SIZE)))
        rnd = random.Random(7)  # plausible records: counts in range, valid or pending footers
        s = SaveFile()
        for ref in RECORDS:
            rec = bytearray(rnd.getrandbits(8) for _ in range(ref.type.size))
            if ref.type.key in ("team", "box", "item_box"):
                rec[0x0E:0x10] = rnd.randint(0, ref.type.body.capacity).to_bytes(2, "big")
            if ref.type.key == "continue":
                rec[0x22:0x24] = rnd.randint(0, 6).to_bytes(2, "big")
            write_footer(rec, rnd.choice(["valid", "pending"]))
            s.data[ref.primary:ref.primary + ref.type.size] = rec
            s.data[ref.backup:ref.backup + ref.type.size] = rec
        out.append(bytes(s.data))
        # a real save with broken copies: primary only, both, and a backup that differs
        s = SaveFile.load(FIRST_SAVE)
        s.edit(lambda d: d["options"].update(sound=3))
        opt, team, box = s.record("options"), s.record("teams", "poke_cup", 0), s.record("boxes", 2)
        s.data[opt.primary + 4] ^= 0xFF
        s.data[team.primary + 1] ^= 0xFF
        s.data[team.backup + 1] ^= 0xFF
        s.data[box.backup + 0x20] ^= 0x10
        s.data[0x3700 + 5] = 0x12  # a bank tail byte
        out.append(bytes(s.data))
        return out

    def all_saves(self):
        return [(os.path.relpath(p, ROOT), self.read(p)) for p in SAVES] + \
               [("random %d" % i, d) for i, d in enumerate(self.random_saves())]

    # ---- tests ----
    def test_new_save(self):
        p = self.path()
        self.run_cli("new", p)
        self.assertEqual(self.read(p), new_save_bytes())
        self.assertEqual(self.read(p), self.read(FIRST_SAVE))

    def test_dump_identical(self):
        for name, data in self.all_saves():
            with self.subTest(name):
                self.assertEqual(self.cli_json("dump", self.path(data)), no_inf(SaveFile(data).to_dict()))

    def test_build_byte_identical(self):
        for name, data in self.all_saves():
            with self.subTest(name):
                self.assertEqual(self.run_cli("roundtrip", self.path(data)).stdout.strip(), "same")
                d = SaveFile(data).to_dict()
                if no_inf(d) != d:
                    continue  # the Python dump has Infinity, which is not JSON
                out = self.path()
                self.run_cli("build", self.path(d), out)  # the C++ build of the Python dump
                self.assertEqual(self.read(out), bytes(SaveFile.from_dict(d).data))
                self.assertEqual(self.read(out), data)

    def test_check_repair_fix_checksums(self):
        for name, data in self.all_saves():
            with self.subTest(name):
                p = self.path(data)
                self.assertEqual(self.cli_json("check", p), [[list(c[0]), c[1], c[2], c[3]] for c in SaveFile(data).check()])
                out = self.path()
                changed = self.cli_json("repair", p, out)
                s = SaveFile(data)
                self.assertEqual(changed, [list(c) for c in s.repair()])
                self.assertEqual(self.read(out), bytes(s.data))
                for pending in (False, True):
                    out = self.path()
                    changed = self.cli_json("fixsum", p, out, *(["pending"] if pending else []))
                    s = SaveFile(data)
                    self.assertEqual(changed, [[list(c[0]), c[1]] for c in s.fix_checksums(pending)])
                    self.assertEqual(self.read(out), bytes(s.data))

    def edits(self):
        """(name, function editing a dump) the way the editor's pages edit it."""
        mew = web._make_mon({"species": "MEW", "level": 50, "moves": [{"move": "PSYCHIC", "pp_up": 3}, {"move": "SURF"}],
                             "ot_name": "ASH", "ot_id": 4242, "nickname": "MEWMEW",
                             "dvs": {"atk": 15, "def": 14, "spd": 13, "spc": 12}, "stat_exp": {"hp": 65535, "atk": 3, "def": 0, "spd": 9, "spc": 0}})["mon"]
        pika = web._make_mon({"species": "PIKACHU", "level": 15})["mon"]

        def unlock(d):
            for p in d["options"]["progress"]:
                p.update(cleared_modes=p["cleared_modes"] | 0xF8, gym_leader_castle=8, poke_cup=4, prime_cup=4, mewtwo_unlocked=1)

        def options(d):
            o = d["options"]
            o.update(sound=(o["sound"] & ~1) | 1, gb_tower_speed=2, kids_club=o["kids_club"] | 1, bonus_pokemon=0x100 | 25)

        def team(d):
            t = d["teams"]["poke_cup"][3]
            t.update(flags=1, name="ASH", trainer_id=4242, entries=[mew, pika], _footer="valid")
            t.pop("count_raw", None)
            t.pop("_unused_entries", None)

        def delete_team(d):
            for t in d["teams"]["poke_cup"]:
                if t["entries"]:
                    t.update(flags=0, name="", trainer_id=0, entries=[])

        def box(d):
            b = d["boxes"][0]
            if b["_footer"] != "valid":
                b.clear()
                b.update(flags=0, name="", trainer_id=0, entries=[], _footer="valid")
            b["entries"] = b["entries"][1:] if b["entries"] else [mew, pika, mew]

        def box_add(d):
            d["boxes"][11]["entries"].append(pika)

        def items(d):
            ib = d["item_boxes"][1]
            ib["entries"] = ib["entries"][:1] + [{"item": 40, "quantity": 5}, {"item": "MASTER BALL", "quantity": 99}]
            ib["flags"] |= 1
            if not ib["name"]:
                ib["name"] = "RED"

        def palace(d):
            ents = d["victory_palace"]["entries"]
            ents[24] = {"mode": 3 | 0x80, "level": 50, "ot_id": 7, "nickname": "PIKA", "ot_name": "RED"}
            ents[0] = None

        def run(d):
            c, se = d["continue"], d["continue"]["session"]
            c["flags"] = 1
            se.update(mode=7, mode_selection=3, battle=2, round2=1, continues=1, trainer_id=99, player_name="RED")
            c["pokemon"] = [mew, pika] + ["fill:00*55"] * 4
            c["count"] = 2

        def gallery(d):
            d["album"]["entries"] = [None] * 36
            d["photos"]["entries"] = [None] * 21

        def names(d):
            d["teams"]["free_battle"][0].update(flags=1, name="<PLAYER>é", entries=[pika], _footer="valid")

        def everything(d):
            for f in (unlock, options, team, box, box_add, items, palace, run, gallery, names):
                f(d)

        return [(f.__name__, f) for f in (unlock, options, team, delete_team, box, box_add, items, palace, run, gallery,
                                           names, everything, lambda d: None)]

    def test_edits_identical(self):
        for save in SAVES:
            data = self.read(save)
            base = json.loads(json.dumps(SaveFile(data).to_dict()))
            for name, fn in self.edits():
                with self.subTest(save=os.path.relpath(save, ROOT), edit=name):
                    edited = copy.deepcopy(base)
                    fn(edited)
                    out = self.path()
                    self.run_cli("edit", self.path(data), self.path(edited), out)
                    expect = web._encode({"base": copy.deepcopy(base), "edited": copy.deepcopy(edited)})
                    self.assertEqual(self.read(out), expect)

    def test_lab_transfer_matches_game(self):
        # the Lab PC moves written by the game itself (tests/test_save.py GameBoy), through the C++ library
        lab = os.path.join(ROOT, "fixtures", "lab_transfer")
        fin = self.read(os.path.join(lab, "1_before.flash"))
        g = GBSave(self.read(os.path.join(lab, "1_before.sav"))).to_dict()
        mon = g["party"][0]
        mon["nickname"] = mon["nickname"]["text"] if isinstance(mon["nickname"], dict) else mon["nickname"]
        ed = json.loads(json.dumps(SaveFile(fin).to_dict()))
        ed["boxes"][0]["entries"] = [mon]
        out = self.path()
        self.run_cli("edit", self.path(fin), self.path(ed), out)
        self.assertEqual(self.read(out), self.read(os.path.join(lab, "1_after.flash")))
        fin = self.read(os.path.join(lab, "1_after.flash"))
        ed = json.loads(json.dumps(SaveFile(fin).to_dict()))
        ed["boxes"][0]["entries"].pop(0)
        self.run_cli("edit", self.path(fin), self.path(ed), out)
        self.assertEqual(self.read(out), self.read(os.path.join(lab, "2_after.flash")))

    def test_mon_requests(self):
        cases = [
            {"species": "MEWTWO", "level": 55, "moves": [{"move": "PSYCHIC", "pp_up": 3}], "ot_name": "ASH", "ot_id": 4242},
            {"species": "PIKACHU", "level": 15},
            {"species": "nidoran♀", "level": 1, "nickname": "", "moves": []},
            {"species": "NIDORAN♂", "level": 100, "moves": [{"move": "", "pp_up": 2}, {"move": "thunderbolt", "pp_up": 2}]},
            {"species": "Mr. Mime", "level": 30, "nickname": "MIMI", "ot_name": {"text": "RED", "raw": "9184835000000000000000"}},
            {"species": "25", "level": "40", "dvs": {"atk": 0, "def": 1, "spd": 2, "spc": 3}, "stat_exp": {"hp": 1, "atk": 2, "def": 3, "spd": 4, "spc": 5}},
            {"species": 151, "level": 70, "moves": [{"move": "SURF"}, {"move": 57, "pp_up": 1}, {"move": "STRUGGLE"}, {"move": "SPLASH"}]},
            {"species": "MEW", "level": 5, "nickname": "<PLAYER>"},
            {"species": "FARFETCH'D", "level": 50, "nickname": "lowercase"},
            # errors: both refuse
            {"species": "MEW", "level": 0},
            {"species": "MEW", "level": 50, "dvs": {"atk": 16, "def": 0, "spd": 0, "spc": 0}},
            {"species": "MEW", "level": 50, "stat_exp": {"hp": 70000, "atk": 0, "def": 0, "spd": 0, "spc": 0}},
            {"species": "MEW", "level": 50, "nickname": "MUCHTOOLONGNAME"},
            {"species": "NOTAMON", "level": 50},
            {"species": "MEW", "level": 50, "moves": [{"move": "NOTAMOVE"}]},
        ]
        for req in cases:
            with self.subTest(req=req):
                try:
                    expect = web._make_mon(copy.deepcopy(req))
                except Exception:  # noqa: BLE001 (the same requests must fail in C++)
                    expect = None
                r = self.run_cli("mon", self.path(req), ok=False)
                if expect is None:
                    self.assertEqual(r.returncode, 2, r.stdout)
                else:
                    self.assertEqual(r.returncode, 0, r.stderr)
                    self.assertEqual(json.loads(r.stdout), expect)

    def test_text(self):
        for kind, size, text in [("gb", 11, "RED"), ("gb", 11, "<PLAYER>"), ("gb", 11, "àéÄ♂♀<PK><MN>"), ("gb", 11, "a b-c?!"),
                                 ("gb", 11, "<A0><FF>"), ("gb", 16, "lowercase"), ("n64", 11, "Mew♂<PK>"), ("n64", 11, "a<3C>b"),
                                 ("n64", 11, "ÿé"), ("gb", 11, "ABCDEFGHIJK"), ("gb", 11, "{"), ("n64", 11, "€"),
                                 ("gb", 11, "NIDORAN<M>"), ("gb", 11, "NIDORAN<f>"), ("gb", 11, "<pk>MON"), ("gb", 11, "<mn>"),
                                 ("gb", 11, "a<b"), ("gb", 11, "<"), ("gb", 11, "<ZZ>"), ("gb", 11, "<1>"), ("gb", 11, "<>"),
                                 ("gb", 11, "DON\u2019T"), ("gb", 11, "A\u2013B"), ("gb", 11, "3\u00d74"), ("gb", 11, "ß"),
                                 ("gb", 11, "<e5><E8>")]:
            with self.subTest(kind=kind, text=text):
                try:
                    b = encode_gb(text, size) if kind == "gb" else encode_n64(text, size)
                    expect = [b.hex(), decode_gb(b) if kind == "gb" else decode_n64(b)]
                except ValueError:
                    expect = None
                r = self.run_cli("text", kind, size, text, ok=False)
                if expect is None:
                    self.assertEqual(r.returncode, 2, r.stdout)
                else:
                    self.assertEqual(r.stdout.splitlines(), expect)

    def test_text_every_byte(self):
        """Every Game Boy byte decodes and encodes back to itself in both implementations, and they agree."""
        for b in range(256):
            if b in (0x50,):
                continue
            raw = bytes([b, 0x80])
            with self.subTest(byte=b):
                shown = decode_gb(raw)
                self.assertEqual(encode_gb(shown, 11)[:2], raw if b != 0x7F else bytes([0x7F, 0x80]))
                r = self.run_cli("text", "gb", 11, shown)
                self.assertEqual(r.stdout.splitlines(), [encode_gb(shown, 11).hex(), decode_gb(encode_gb(shown, 11))])

    def test_game_boy(self):
        for p in GB_SAVES:
            raw = self.read(p)
            base = GBSave(raw).to_dict()
            with self.subTest(os.path.basename(p), edit="dump"):
                self.assertEqual(self.cli_json("gbdump", p), base)

            def party_to_box(e):
                e["boxes"][0].append(e["party"].pop(0))

            def edits_all(e):
                e["boxes"][7].append(make_mon("MEW", 50))  # boxes never initialized: the game's first-switch reset
                e["trainer"].update(money=123456, badges=0xFF, name="ASH", rival="GARY", id=777, pikachu_happiness=300)
                e["bag"].append({"item": 40, "quantity": 120})
                e["pc_items"] = e["pc_items"][1:]
                e["pokedex"]["owned"] = sorted(set(e["pokedex"]["owned"]) | {1, 2, 3})
                e["pokedex"]["seen"] = list(range(1, 152))

            def party_edit(e):
                e["party"][0]["level"] = 77
                e["party"].append(web._make_mon({"species": "SNORLAX", "level": 40})["mon"])

            def empty_party(e):
                e["party"] = []

            for fn in (party_to_box, edits_all, party_edit, empty_party, lambda e: None):
                with self.subTest(os.path.basename(p), edit=fn.__name__):
                    e = copy.deepcopy(base)
                    if fn is party_to_box and not e["party"]:
                        continue
                    fn(e)
                    try:
                        expect = apply_edit(raw, base, e)
                    except ValueError:
                        expect = None
                    out = self.path()
                    r = self.run_cli("gbedit", p, self.path(e), out, ok=False)
                    if expect is None:
                        self.assertEqual(r.returncode, 2)
                    else:
                        self.assertEqual(r.returncode, 0, r.stderr)
                        self.assertEqual(self.read(out), expect)


if __name__ == "__main__":
    unittest.main()

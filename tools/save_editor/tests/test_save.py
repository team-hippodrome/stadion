"""Tests for the Pokémon Stadium save editor: python3 -m unittest discover -s tests (from tools/save_editor)."""
import json
import os
import random
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from stadium_save import gamedata, make_mon, pokemon, text  # noqa: E402
from stadium_save.save import (BACKUP_BASE, FLASH_SIZE, RECORDS, SaveFile, checksum, footer_state,  # noqa: E402
                               new_save_bytes, reset_record, write_footer)

FIRST_SAVE = os.path.join(ROOT, "fixtures", "first_save.flash")
VERIFY_SAVES = os.path.join(ROOT, "verify", "saves")


def cli(*args):
    r = subprocess.run([sys.executable, "-m", "stadium_save"] + list(args), cwd=ROOT, capture_output=True, text=True)
    if r.returncode not in (0, 1):
        raise AssertionError(r.stderr)
    return r


class RoundTrip(unittest.TestCase):
    def test_new_save_matches_game(self):
        # fixtures/first_save.flash was written by the real game (ares) on its first save
        with open(FIRST_SAVE, "rb") as f:
            self.assertEqual(new_save_bytes(), f.read())

    def test_load_save_identical(self):
        s = SaveFile.load(FIRST_SAVE)
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "x.flash")
            s.save(out)
            with open(out, "rb") as f, open(FIRST_SAVE, "rb") as g:
                self.assertEqual(f.read(), g.read())

    def test_json_roundtrip_first_save(self):
        s = SaveFile.load(FIRST_SAVE)
        self.assertEqual(SaveFile.from_json(s.to_json()).data, s.data)

    def test_json_roundtrip_random_bytes(self):
        # every byte is either decoded into a field or kept raw, so even noise survives dump -> build
        for seed in range(3):
            rnd = random.Random(seed)
            s = SaveFile(bytes(rnd.getrandbits(8) for _ in range(FLASH_SIZE)))
            self.assertEqual(SaveFile.from_json(s.to_json()).data, s.data, seed)

    def test_json_roundtrip_random_records(self):
        # structurally plausible records (counts in range, valid footers, random Pokémon/photos/names)
        rnd = random.Random(7)
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
        self.assertEqual(SaveFile.from_json(s.to_json()).data, s.data)

    def test_edit_without_changes_is_identity(self):
        s = SaveFile.load(FIRST_SAVE)
        before = bytes(s.data)
        s.edit(lambda d: None)
        self.assertEqual(bytes(s.data), before)


class Footers(unittest.TestCase):
    def test_checksum_definition(self):
        # Save_Checksum: 16-bit sum of all bytes but the last two, magic included
        rec = bytearray(reset_record(RECORDS[0].type))
        self.assertEqual(rec[-6:-2], b"POKE")
        self.assertEqual(int.from_bytes(rec[-2:], "big"), (sum(rec[:-2])) & 0xFFFF)
        self.assertEqual(footer_state(rec), "valid")
        write_footer(rec, "pending")
        self.assertEqual(rec[-6:-2], b"XXXX")
        self.assertEqual(int.from_bytes(rec[-2:], "big"), checksum(rec) ^ 0xFF)
        self.assertEqual(footer_state(rec), "pending")
        rec[3] ^= 1
        self.assertEqual(footer_state(rec), "invalid")

    def test_all_records_valid_in_first_save(self):
        s = SaveFile.load(FIRST_SAVE)
        self.assertTrue(all(p == b == "valid" and w == "primary" for _, p, b, w in s.check()))
        self.assertEqual(len(RECORDS), 111)

    def test_repair_uses_backup_then_reset(self):
        s = SaveFile.load(FIRST_SAVE)
        s.edit(lambda d: d["options"].update(sound=3))
        good = bytes(s.data)
        opt = s.record("options")
        s.data[opt.primary + 4] ^= 0xFF          # primary broken -> game loads the backup
        self.assertEqual(s.loaded_copy(opt), "backup")
        team = s.record("teams", "poke_cup", 0)
        s.data[team.primary + 1] ^= 0xFF          # both copies broken -> game resets the record
        s.data[team.backup + 1] ^= 0xFF
        self.assertEqual(s.loaded_copy(team), "reset")
        changed = s.repair()
        self.assertIn(("options",), changed)
        self.assertEqual(bytes(s.data), good)

    def test_fix_checksums(self):
        s = SaveFile.load(FIRST_SAVE)
        opt = s.record("options")
        s.data[opt.primary + 3] = 1              # hex edit: sound (u16 at +2) = mono, footer now stale
        s.data[opt.backup + 3] = 1
        self.assertEqual(footer_state(s.record_bytes(opt)), "invalid")
        self.assertEqual(len(s.fix_checksums()), 2)
        self.assertEqual(s.to_dict()["options"]["sound"], 1)
        self.assertTrue(all(p == b == "valid" for _, p, b, _ in s.check()))

    def test_bank_layout(self):
        # primary at bank * 0x4000, backup 0x10000 above; records tile each bank without gaps
        seen = {}
        for ref in RECORDS:
            self.assertEqual(ref.backup - ref.primary, BACKUP_BASE)
            seen.setdefault(ref.bank, []).append((ref.offset, ref.type.size))
        for bank, recs in seen.items():
            pos = 0
            for off, size in sorted(recs):
                self.assertEqual(off, pos)
                pos += size
            self.assertEqual(pos, (0x3700, 0x3700, 0x3E04, 0x3860)[bank])


class Pokemon(unittest.TestCase):
    def test_species_tables(self):
        for dex in range(1, 152):
            idx = pokemon.dex_to_index(dex)
            self.assertEqual(pokemon.index_to_dex(idx), dex)
            mon = pokemon.make_mon(dex, 50)
            self.assertEqual(pokemon.MON.decode(pokemon.MON.encode(mon))["species"], gamedata.SPECIES[dex])

    def test_known_values(self):
        self.assertEqual(gamedata.SPECIES[25], "PIKACHU")
        self.assertEqual(pokemon.dex_to_index(25), 0x54)
        self.assertEqual(gamedata.MOVES[57], "SURF")
        self.assertEqual(gamedata.MOVE_DATA[57][4], 15)  # Surf base PP
        # Mewtwo L100, DVs 15, no stat exp: HP (106+15)*2+110 = 352, Special (154+15)*2+5 = 343
        st = pokemon.calc_stats(150, 100, {"atk": 15, "def": 15, "spd": 15, "spc": 15},
                                {k: 0 for k in pokemon.STAT_KEYS})
        self.assertEqual((st["hp"], st["spc"]), (352, 343))
        # max stat exp adds 63 (sqrt 65535 -> 255, // 4)
        st = pokemon.calc_stats(150, 100, {"atk": 15, "def": 15, "spd": 15, "spc": 15},
                                {k: 65535 for k in pokemon.STAT_KEYS})
        self.assertEqual(st["spc"], 406)
        self.assertEqual(pokemon.exp_for_level(150, 100), 1250000)   # slow
        self.assertEqual(pokemon.exp_for_level(1, 100), 1059860)     # medium slow
        self.assertEqual(pokemon.level_for_exp(1, 1059859), 99)

    def test_record_layout(self):
        m = pokemon.make_mon("PIKACHU", 50, moves=["THUNDERBOLT", "SURF"], nickname="SPARKY", ot_name="ASH",
                             ot_id=0x1234)
        b = pokemon.MON.encode(m)
        self.assertEqual(len(b), 0x37)
        self.assertEqual(b[0], 0x54)
        self.assertEqual(b[3], 50)
        self.assertEqual(b[8:10], bytes([85, 57]))
        self.assertEqual(b[0x0C:0x0E], b"\x12\x34")
        self.assertEqual(b[0x1B:0x1D], b"\xff\xff")
        self.assertEqual(b[0x1D] & 0x3F, 15)
        self.assertEqual(b[0x21:0x28], bytes([0x92, 0x8F, 0x80, 0x91, 0x8A, 0x98, 0x50]))  # SPARKY + terminator

    def test_text(self):
        for s in ("RED", "NIDORAN♀", "<PLAYER>", "Mr.Mime", "<PK><MN>"):
            self.assertEqual(text.decode_gb(text.encode_gb(s, 11)), s)
        self.assertEqual(text.decode_gb(bytes([0x80, 0x01, 0x50])), "A<01>")
        # the characters the Game Boy games allow in names: every byte survives decode + encode
        for b in range(256):
            if b == 0x50:
                continue
            self.assertEqual(text.encode_gb(text.decode_gb(bytes([b])), 11)[0], b, hex(b))
        # glyphs the game draws (D_80073030): gender signs, x, e acute, apostrophe, PK and MN
        for b, s in ((0xEF, "\u2642"), (0xF5, "\u2640"), (0xF1, "\u00d7"), (0xBC, "\u00e9"), (0xE0, "'"), (0xE1, "<PK>"),
                     (0xE2, "<MN>"), (0xE3, "-"), (0xE6, "?"), (0xE7, "!"), (0xF2, "."), (0xF3, "/"), (0xF4, ","), (0xF0, "\u00a5")):
            self.assertEqual(text.decode_gb(bytes([b, 0x50])), s)
            self.assertEqual(text.encode_gb(s, 3)[0], b)
        # easier spellings
        self.assertEqual(text.encode_gb("NIDORAN<M>", 11), text.encode_gb("NIDORAN\u2642", 11))
        self.assertEqual(text.encode_gb("<f><pk>", 11), text.encode_gb("\u2640<PK>", 11))
        self.assertEqual(text.encode_gb("DON\u2019T", 11), text.encode_gb("DON'T", 11))
        for bad in ("<", "a<b", "<ZZ>", "<1>", "<>", "\u20ac"):  # an error, never another exception type
            with self.assertRaises(ValueError):
                text.encode_gb(bad, 11)
        with self.assertRaises(ValueError):
            text.encode_gb("ABCDEFGHIJK", 11)  # 11 characters + terminator do not fit


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.save = os.path.join(self.tmp.name, "s.flash")
        with open(FIRST_SAVE, "rb") as f, open(self.save, "wb") as g:
            g.write(f.read())

    def tearDown(self):
        self.tmp.cleanup()

    def changed_records(self):
        a = SaveFile.load(FIRST_SAVE)
        b = SaveFile.load(self.save)
        return {ref.path for ref in RECORDS if a.record_bytes(ref) != b.record_bytes(ref)}

    def test_options(self):
        cli("options", self.save, "--sound", "mono", "--voice", "off", "--kids-club-hyper", "on")
        d = SaveFile.load(self.save).to_dict()
        self.assertEqual(d["options"]["sound"], 3)
        self.assertEqual(d["options"]["kids_club"], 1)
        self.assertEqual(self.changed_records(), {("options",)})
        self.assertEqual(cli("check", self.save).returncode, 0)

    def test_unlock(self):
        cli("unlock", self.save, "--round2", "--mewtwo")
        p = SaveFile.load(self.save).to_dict()["options"]["progress"]
        self.assertEqual([x["cleared_modes"] for x in p], [0x1F8, 0x1F8])
        self.assertEqual(p[0]["gym_leader_castle"], 8)

    def test_team_box_items_statue_continue(self):
        cli("team", self.save, "set", "prime_cup", "2", "MEWTWO:100", "MEW:100@nick=MEW", "--name", "ASH",
            "--trainer-id", "4242")
        cli("box", self.save, "add", "12", "PIKACHU:5", "EEVEE:25")
        cli("items", self.save, "add", "2", "MASTER BALL:1", "PP UP:9", "--name", "ASH", "--trainer-id", "4242")
        cli("statue", self.save, "set", "GENGAR", "--mode", "7", "--round2")
        cli("continue", self.save, "set", "SNORLAX:50", "--mode", "6", "--selection", "3", "--battle", "5")
        d = SaveFile.load(self.save).to_dict()
        t = d["teams"]["prime_cup"][2]
        self.assertEqual((t["flags"], t["name"], t["trainer_id"]), (1, "ASH", 4242))
        self.assertEqual([m["species"] for m in t["entries"]], ["MEWTWO", "MEW"])
        self.assertEqual([m["species"] for m in d["boxes"][11]["entries"]], ["PIKACHU", "EEVEE"])
        self.assertEqual([(e["item"], e["quantity"]) for e in d["item_boxes"][1]["entries"]], [(1, 1), (79, 9)])
        self.assertEqual(d["victory_palace"]["entries"][93]["mode"], 0x87)
        c = d["continue"]
        self.assertEqual((c["flags"], c["count"], c["session"]["mode"], c["session"]["battle"]), (1, 1, 6, 5))
        self.assertEqual(self.changed_records(), {("teams", "prime_cup", 2), ("boxes", 11), ("item_boxes", 1),
                                                  ("victory_palace",), ("continue",)})
        self.assertEqual(cli("check", self.save).returncode, 0)
        # both copies are written, like Save_FlushBank
        s = SaveFile.load(self.save)
        for ref in RECORDS:
            self.assertEqual(s.record_bytes(ref), s.record_bytes(ref, "backup"))

    def test_set_and_dump_build(self):
        cli("set", self.save, "options.gb_tower_speed=1", "teams.mewtwo.0.name=\"BLUE\"")
        d = SaveFile.load(self.save).to_dict()
        self.assertEqual(d["options"]["gb_tower_speed"], 1)
        self.assertEqual(d["teams"]["mewtwo"][0]["name"], "BLUE")
        js = os.path.join(self.tmp.name, "s.json")
        out = os.path.join(self.tmp.name, "t.flash")
        cli("dump", self.save, "-o", js)
        cli("build", js, "-o", out)
        with open(self.save, "rb") as f, open(out, "rb") as g:
            self.assertEqual(f.read(), g.read())

    def test_check_reports_corruption(self):
        s = SaveFile.load(self.save)
        s.data[s.record("album").primary + 10] ^= 1
        s.save(self.save)
        self.assertEqual(cli("check", self.save).returncode, 1)
        cli("repair", self.save)
        self.assertEqual(cli("check", self.save).returncode, 0)


class VerifiedSaves(unittest.TestCase):
    """The saves under verify/saves were booted in ares (verify/run_verify.py); they must stay reproducible."""

    def test_saves_reproducible(self):
        with open(os.path.join(ROOT, "verify", "cases.json")) as f:
            cases = json.load(f)
        with tempfile.TemporaryDirectory() as d:
            for name, case in cases.items():
                ref = os.path.join(VERIFY_SAVES, name + ".flash")
                if not os.path.exists(ref):
                    continue
                out = os.path.join(d, name + ".flash")
                with open(FIRST_SAVE, "rb") as f, open(out, "wb") as g:
                    g.write(f.read())
                for step in case["steps"]:
                    cli(*[a.replace("SAVE", out) for a in step])
                with open(out, "rb") as f, open(ref, "rb") as g:
                    self.assertEqual(f.read(), g.read(), name)

    def test_game_written_saves_valid(self):
        for fn in os.listdir(VERIFY_SAVES) if os.path.isdir(VERIFY_SAVES) else []:
            if fn.endswith(".game.flash"):
                s = SaveFile.load(os.path.join(VERIFY_SAVES, fn))
                self.assertTrue(all(p == b == "valid" for _, p, b, _ in s.check()), fn)
                self.assertEqual(SaveFile.from_json(s.to_json()).data, s.data)


LAB = os.path.join(ROOT, "fixtures", "lab_transfer")


class GameBoy(unittest.TestCase):
    """gbsave against what the game itself wrote (port, German Yellow, Lab PC; identical to ares): run 1 moved PIKACHU
    from the party to Lab box 1 (1_before -> 1_after), run 2 moved it back into party slot 2 (1_after -> 2_after).
    SRAM 0x00-0x1F is the Transfer Pak RAM probe (timing noise on hardware too) and is not compared."""

    def load(self, name):
        with open(os.path.join(LAB, name), "rb") as f:
            return f.read()

    def test_unchanged_write_is_identity(self):
        from stadium_save.gbsave import apply_edit, GBSave
        for name in ("1_before.sav", "1_after.sav", "2_after.sav"):
            raw = self.load(name)
            d = GBSave(raw).to_dict()
            self.assertTrue(d["valid"])
            self.assertEqual(apply_edit(raw, d, d), raw)

    def test_party_to_lab_box_matches_game(self):
        import copy
        from stadium_save.gbsave import apply_edit, GBSave
        from stadium_save import web
        gin, fin = self.load("1_before.sav"), self.load("1_before.flash")
        g = GBSave(gin).to_dict()
        e = copy.deepcopy(g)
        mon = e["party"].pop(0)
        self.assertEqual(apply_edit(gin, g, e)[0x20:], self.load("1_after.sav")[0x20:])
        base = json.loads(json.dumps(SaveFile(fin).to_dict()))
        ed = copy.deepcopy(base)
        mon["nickname"] = mon["nickname"]["text"] if isinstance(mon["nickname"], dict) else mon["nickname"]
        ed["boxes"][0]["entries"] = [mon]  # the page re-encodes the nickname like the Lab PC
        self.assertEqual(web._encode({"base": base, "edited": ed}), self.load("1_after.flash"))

    def test_lab_box_to_party_matches_game(self):
        import copy
        from stadium_save.gbsave import apply_edit, GBSave
        from stadium_save import web
        gin, fin = self.load("1_after.sav"), self.load("1_after.flash")
        g = GBSave(gin).to_dict()
        base = json.loads(json.dumps(SaveFile(fin).to_dict()))
        e, ed = copy.deepcopy(g), copy.deepcopy(base)
        e["party"].insert(1, ed["boxes"][0]["entries"].pop(0))
        self.assertEqual(apply_edit(gin, g, e)[0x20:], self.load("2_after.sav")[0x20:])
        self.assertEqual(web._encode({"base": base, "edited": ed}), self.load("2_after.flash"))

    def test_edits_keep_save_valid(self):
        import copy
        from stadium_save.gbsave import apply_edit, GBSave
        raw = self.load("1_before.sav")
        g = GBSave(raw).to_dict()
        e = copy.deepcopy(g)
        e["boxes"][7].append(make_mon("MEW", 50))  # boxes never initialized: the game's first-switch reset
        e["trainer"].update(money=123456, badges=0xFF, name="ASH")
        e["bag"].append({"item": 40, "quantity": 5})
        out = GBSave(apply_edit(raw, g, e))
        d = out.to_dict()
        self.assertTrue(d["valid"] and out.boxes_initialized())
        self.assertEqual([m["species"] for m in d["boxes"][7]], ["MEW"])
        self.assertEqual((d["trainer"]["money"], d["trainer"]["badges"], d["trainer"]["name"]), (123456, 0xFF, "ASH"))
        self.assertIn(151, d["pokedex"]["owned"])  # arrived Pokémon count as caught
        for base in (0x4000, 0x6000):  # bank checksums
            self.assertEqual(out.data[base + 0x1A4C], ~sum(out.data[base:base + 0x1A4C]) & 0xFF)
        with self.assertRaises(ValueError):
            e["party"] = []
            apply_edit(raw, g, e)


class Web(unittest.TestCase):
    def test_save_to_file(self):
        # the launcher's embedded editor: serve --write, POST /api/save writes the file and keeps a .bak
        import threading
        import urllib.request
        from http.server import ThreadingHTTPServer
        from stadium_save import web
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "s.flash")
            with open(FIRST_SAVE, "rb") as f, open(path, "wb") as g:
                g.write(f.read())
            web.Handler.initial, web.Handler.writable = path, True
            srv = ThreadingHTTPServer(("127.0.0.1", 0), web.Handler)
            threading.Thread(target=srv.serve_forever, daemon=True).start()
            try:
                url = "http://127.0.0.1:%d" % srv.server_address[1]
                init = json.load(urllib.request.urlopen(url + "/api/initial"))
                self.assertTrue(init["writable"])
                init["save"]["options"]["sound"] = 1
                r = urllib.request.urlopen(urllib.request.Request(url + "/api/save",
                                                                  data=json.dumps(init["save"]).encode()))
                self.assertTrue(all(p == b == "valid" for _, p, b, _ in json.load(r)["check"]))
                self.assertEqual(SaveFile.load(path).to_dict()["options"]["sound"], 1)
                with open(path + ".bak", "rb") as f, open(FIRST_SAVE, "rb") as g:
                    self.assertEqual(f.read(), g.read())
                # the friendly page: {"base", "edited"} commits only the records that changed
                post = lambda p, body: json.load(urllib.request.urlopen(urllib.request.Request(
                    url + p, data=json.dumps(body).encode())))
                meta = json.load(urllib.request.urlopen(url + "/api/meta"))
                self.assertEqual(len(meta["species"]), 151)
                mon = post("/api/mon", {"species": "MEWTWO", "level": 55, "moves": [{"move": "PSYCHIC", "pp_up": 3}],
                                        "ot_name": "ASH", "ot_id": 4242})["mon"]
                self.assertEqual((mon["moves"][0]["pp"], mon["moves"][0]["pp_up"], mon["nickname"]), (16, 3, "MEWTWO"))
                base = post("/api/save", init["save"])["save"]
                edited = json.loads(json.dumps(base))
                edited["teams"]["poke_cup"][0].update(flags=1, name="ASH", trainer_id=4242, entries=[mon])
                before = SaveFile.load(path)
                post("/api/save", {"base": base, "edited": edited})
                after = SaveFile.load(path)
                self.assertEqual(after.to_dict()["teams"]["poke_cup"][0]["entries"][0]["species"], "MEWTWO")
                for ref in RECORDS:  # only the edited record changed, and it is valid in both copies
                    if ref.path != ("teams", "poke_cup", 0):
                        self.assertEqual(after.record_bytes(ref), before.record_bytes(ref))
                self.assertTrue(all(p == b == "valid" for _, p, b, _ in after.check()))
                # an unreadable record is reset by /api/repair, like the game does on load
                broken = bytearray(after.data)
                broken[0:4] = b"\xff\xff\xff\xff"
                r = urllib.request.urlopen(urllib.request.Request(url + "/api/decode", data=bytes(broken)))
                dec = json.load(r)["save"]
                self.assertNotEqual(dec["teams"]["free_battle"][0]["_footer"], "valid")
                rep = post("/api/repair", {"base": dec, "edited": dec})["save"]
                self.assertEqual(rep["teams"]["free_battle"][0]["_footer"], "valid")
                # a save that changed on disk since the page loaded it (the game wrote it) is refused, nothing written
                cur = json.load(urllib.request.urlopen(url + "/api/initial"))["save"]
                with open(path, "r+b") as f:
                    f.seek(0x1EA2)  # any byte: a registered team slot in bank 0
                    f.write(b"\x00\x03")
                with open(path, "rb") as f:
                    on_disk = f.read()
                edited = json.loads(json.dumps(cur))
                edited["options"]["gb_tower_speed"] = 2
                with self.assertRaises(urllib.error.HTTPError) as err:
                    post("/api/save", {"base": cur, "edited": edited})
                self.assertIn(b"changed on disk", err.exception.read())
                with open(path, "rb") as f:
                    self.assertEqual(f.read(), on_disk)
                web.Handler.writable = False
                with self.assertRaises(urllib.error.HTTPError):
                    urllib.request.urlopen(urllib.request.Request(url + "/api/save", data=b"{}"))
            finally:
                srv.shutdown()
                srv.server_close()
                web.Handler.initial, web.Handler.writable = None, False


if __name__ == "__main__":
    unittest.main()

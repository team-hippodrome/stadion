"""UI test of the editor page (stadium_save/editor.html) in the macOS launcher's editor window.

Runs "Pokemon Stadium.app" (build it with tools/make_app.sh) with LAUNCHER_EDITOR_TEST=tests/ui_test.js: the launcher
opens the editor on a fixture save, runs the suite in the page (it clicks every button and changes every field),
answers the page's confirm() and the unsaved-changes prompt itself (LAUNCHER_AUTO_CONFIRM), closes the window and
quits. Then the written save is checked with the library. Skipped when the app is not built (or not on macOS).

python3 -m unittest tests.test_ui -v   (from tools/save_editor)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PROJECT = os.path.dirname(os.path.dirname(ROOT))
sys.path.insert(0, ROOT)

from stadium_save import make_mon  # noqa: E402
from stadium_save.gbsave import GBSave  # noqa: E402
from stadium_save.save import PHOTO, SaveFile, new_save_bytes  # noqa: E402

LAUNCHER = os.path.join(PROJECT, "Pokemon Stadium.app", "Contents", "MacOS", "launcher")


def fixture(path):
    """A save with something in every section the page edits, and one unreadable record (box 12) for Repair."""
    s = SaveFile(new_save_bytes())

    def fn(d):
        d["teams"]["poke_cup"][0].update(flags=1, name="RED", trainer_id=1, entries=[make_mon("PIKACHU", 50)])
        d["boxes"][0]["entries"] = [make_mon("EEVEE", 5)]
        d["victory_palace"]["entries"][0] = {"mode": 3, "level": 50, "ot_id": 1, "nickname": "BULBY", "ot_name": "RED"}
        c = d["continue"]
        c["flags"] = 1
        c["session"].update(mode=3, mode_selection=0, battle=3, trainer_id=1, player_name="RED")
        mons = [make_mon(x, 50) for x in ("MEWTWO", "PIKACHU", "SNORLAX")]
        c["count"] = 3
        c["pokemon"] = mons + ["fill:00*55"] * 3
        photo = PHOTO.decode(bytes([0, 0, 0, 0, 25]) + bytes(0x8C - 5))
        d["album"]["entries"][0] = photo
        d["photos"]["entries"][0] = photo

    s.edit(fn)
    box12 = s.record("boxes", 11)
    for off in (box12.primary, box12.backup):
        s.data[off + box12.type.size - 1] ^= 0xFF
    s.save(path)


GB_FIXTURE = os.path.join(PROJECT, "gb", "yellow_de.sav")  # German Yellow: MICHI, PIKACHU BULBASAUR CHARMANDER
GB_ROM = os.path.join(PROJECT, "gb", "yellow_de.gb")


def run_launcher(save, mode, gb):
    env = dict(os.environ, LAUNCHER_DEBUG="1", LAUNCHER_SAVE=save, LAUNCHER_AUTO_CONFIRM=mode,
               LAUNCHER_EDITOR_TEST=os.path.join(HERE, "ui_test.js"), LAUNCHER_GB=gb + "|" + GB_ROM)
    r = subprocess.run([LAUNCHER], env=env, capture_output=True, text=True, timeout=180)
    out = {"confirm": [], "alert": [], "close": [], "window": None, "result": None, "raw": r.stdout + r.stderr}
    for line in r.stdout.splitlines():
        if line.startswith("TEST-RESULT: "):
            try:
                out["result"] = json.loads(line[13:])
            except ValueError:
                out["result"] = {"error": line[13:]}
        elif line.startswith("CONFIRM: "):
            out["confirm"].append(line[9:])
        elif line.startswith("ALERT: "):
            out["alert"].append(line[7:])
        elif line.startswith("CLOSE-PROMPT -> "):
            out["close"].append(line[16:])
        elif line.startswith("WINDOW: "):
            out["window"] = line[8:]
    return out


def editor_processes(save):
    r = subprocess.run(["pgrep", "-f", "stadium_save serve " + save], capture_output=True, text=True)
    return r.stdout.split()


@unittest.skipUnless(sys.platform == "darwin" and os.access(LAUNCHER, os.X_OK) and os.path.exists(GB_FIXTURE),
                     "needs the built launcher app and gb/yellow_de.sav")
class EditorUI(unittest.TestCase):
    def check_steps(self, out):
        res = out["result"]
        self.assertIsNotNone(res, out["raw"])
        self.assertIn("steps", res, res)
        failed = ["%s: %s" % (n, info) for n, ok, info in res["steps"] if not ok]
        self.assertEqual(failed, [], "\n".join(failed))
        return res

    def test_every_control(self):
        with tempfile.TemporaryDirectory() as d:
            save, gb = os.path.join(d, "ui.flash"), os.path.join(d, "yellow.sav")
            fixture(save)
            shutil.copy(GB_FIXTURE, gb)
            with open(save, "rb") as f:
                original = f.read()
            out = run_launcher(save, "yes", gb)
            res = self.check_steps(out)
            print("\n%d UI steps passed; confirmations: %d" % (len(res["steps"]), len(out["confirm"])))
            self.assertEqual(out["alert"], [], "the page reported errors")
            # every confirmation went through the launcher's native handler
            for msg in ("Throw away all changes", "Delete this team?", "Empty box 5?", "Empty this item box?",
                        "Remove all statues?", "Delete the saved run?", "Delete all album photos?",
                        "Delete the current photo set?", "Empty PC box 4?", "Clear the whole Pokédex?"):
                self.assertTrue(any(c.startswith(msg) and c.endswith("-> ok") for c in out["confirm"]), msg)
            # the unsaved change at the end was confirmed in the close prompt and the window closed
            self.assertEqual(out["close"], ["close"])
            self.assertEqual(out["window"], "closed")
            self.assertEqual(editor_processes(save), [])

            s = SaveFile.load(save)
            self.assertTrue(all(p == b == "valid" for _, p, b, _ in s.check()), "a record is not valid")
            dd = s.to_dict()
            self.assertEqual(dd["options"]["sound"], 2)  # cmd+S saved stereo + voice off; the later Mono was dropped
            p = dd["options"]["progress"][0]
            self.assertEqual((p["cleared_modes"] & 0xF8, p["mewtwo_unlocked"], p["gym_leader_castle"]), (0xF8, 1, 8))
            team = dd["teams"]["poke_cup"][0]
            self.assertEqual((team["name"], team["trainer_id"]), ("ASH", 4242))
            self.assertEqual([(m["species"], m["level"]) for m in team["entries"]], [("PIKACHU", 50), ("MEWTWO", 55)])
            self.assertEqual(dd["teams"]["poke_cup"][2]["entries"], [])
            # EEVEE went to the Game Boy, PIKACHU and BULBASAUR came from it, MEW was added
            self.assertEqual([m["species"] for m in dd["boxes"][0]["entries"]], ["PIKACHU", "BULBASAUR", "MEW"])
            self.assertEqual(dd["boxes"][4]["entries"], [])
            self.assertEqual([(e["item"], e["quantity"]) for e in dd["item_boxes"][0]["entries"]], [(40, 1)])
            self.assertEqual([i + 1 for i, e in enumerate(dd["victory_palace"]["entries"]) if e], [25])
            c = dd["continue"]
            self.assertEqual((c["flags"] & 1, c["count"], c["pokemon"][0]["species"]), (1, 1, "MEWTWO"))
            self.assertFalse(any(dd["album"]["entries"]) or any(dd["photos"]["entries"]))
            self.assertEqual(dd["options"]["gb_tower_word"], 7)
            g = GBSave.load(gb).to_dict()
            self.assertTrue(g["valid"])
            t = g["trainer"]
            self.assertEqual((t["name"], t["rival"], t["id"], t["money"], t["pikachu_happiness"], t["badges"]),
                             ("ASH", "BLUE", 12345, 999999, 255, 2))
            self.assertEqual([m["species"] for m in g["party"]], ["CHARMANDER"])
            self.assertEqual([[m["species"] for m in b] for b in g["boxes"][:4]], [["EEVEE"], [], ["MEW"], []])
            self.assertEqual(g["bag"], [{"item": 20, "quantity": 5}])
            self.assertEqual(len(g["pokedex"]["owned"]), 151)
            self.assertTrue(os.path.exists(gb + ".bak"))
            # two saves (Save button, then cmd+S): .bak is the version before the last one (Mono, saved first)
            bak = SaveFile.load(save + ".bak")
            self.assertEqual(bak.to_dict()["options"]["sound"], 1)
            self.assertNotEqual(bytes(bak.data), original)

    def test_cancel_changes_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            save, gb = os.path.join(d, "ui.flash"), os.path.join(d, "yellow.sav")
            fixture(save)
            shutil.copy(GB_FIXTURE, gb)
            with open(save, "rb") as f:
                original = f.read()
            out = run_launcher(save, "no", gb)
            self.check_steps(out)
            self.assertEqual(len(out["confirm"]), 7, out["confirm"])
            self.assertTrue(all(c.endswith("-> cancel") for c in out["confirm"]))
            self.assertEqual(out["close"], ["cancel"])   # unsaved changes: closing was cancelled ...
            self.assertEqual(out["window"], "open")
            self.assertEqual(editor_processes(save), [])  # ... and quitting the launcher still stops the server
            with open(save, "rb") as f:
                self.assertEqual(f.read(), original)
            self.assertFalse(os.path.exists(save + ".bak"))
            with open(gb, "rb") as f, open(GB_FIXTURE, "rb") as g:
                self.assertEqual(f.read(), g.read())


if __name__ == "__main__":
    unittest.main()

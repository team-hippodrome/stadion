#!/usr/bin/env python3
"""Boots edited saves in ares and captures the screens that show the edits.

usage: ARES_SAVE_BUILD=<ares_save build dir> python3 verify/run_verify.py [case ...]

For each case in verify/cases.json: start from fixtures/first_save.flash, apply the CLI steps (SAVE = the file),
store the result as verify/saves/<case>.flash, boot it in ares with verify/scripts/<script> and write the pictures
to verify/shots/<case>_<name>.png. Scripts may end with 'flashsave <frame> OUT': the save the game itself wrote is
then stored as verify/saves/<case>.game.flash, checked with the editor, and compared record by record with the input.
A case with "blank": true boots with an erased flash chip (the input save is then only the comparison reference).
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from stadium_save.save import RECORDS, SaveFile  # noqa: E402


def main():
    with open(os.path.join(HERE, "cases.json")) as f:
        cases = json.load(f)
    names = sys.argv[1:] or list(cases)
    os.makedirs(os.path.join(HERE, "saves"), exist_ok=True)
    os.makedirs(os.path.join(HERE, "shots"), exist_ok=True)
    report = []
    for name in names:
        case = cases[name]
        save = os.path.join(HERE, "saves", name + ".flash")
        shutil.copyfile(os.path.join(ROOT, "fixtures", "first_save.flash"), save)
        for step in case["steps"]:
            subprocess.run([sys.executable, "-m", "stadium_save"] + [a.replace("SAVE", save) for a in step],
                           cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        with tempfile.TemporaryDirectory(prefix="stadium_verify_") as tmp:
            with open(os.path.join(HERE, "scripts", case["script"])) as f:
                script = f.read().replace("SHOT", tmp).replace("OUT", os.path.join(tmp, "out.flash"))
            sp = os.path.join(tmp, "script.txt")
            with open(sp, "w") as f:
                f.write(script)
            # "blank": boot with an erased chip instead of the edited save
            flash_args = [] if case.get("blank") else ["--flash-in", save]
            subprocess.run([os.path.join(ROOT, "ares", "run.sh"), sp] + flash_args, check=True)
            shots = []
            for fn in sorted(os.listdir(tmp)):
                if fn.endswith(".png"):
                    dst = os.path.join(HERE, "shots", "%s_%s" % (name, fn))
                    shutil.move(os.path.join(tmp, fn), dst)
                    shots.append(os.path.relpath(dst, HERE))
            line = "%s: %s -> %s" % (name, case["expect"], ", ".join(shots))
            out = os.path.join(tmp, "out.flash")
            if os.path.exists(out):
                game = os.path.join(HERE, "saves", name + ".game.flash")
                shutil.move(out, game)
                a, b = SaveFile.load(save), SaveFile.load(game)
                bad = [c for c in b.check() if c[1] != "valid" or c[2] != "valid"]
                diff = [ref.path for ref in RECORDS if a.record_bytes(ref) != b.record_bytes(ref)]
                line += "\n    game-written save: %d invalid records, %d records differ from the input%s" % (
                    len(bad), len(diff), (": " + ", ".join("/".join(map(str, p)) for p in diff)) if diff else "")
        print(line)
        report.append(line)
    with open(os.path.join(HERE, "last_run.txt"), "w") as f:
        f.write("\n".join(report) + "\n")


if __name__ == "__main__":
    main()

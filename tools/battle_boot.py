#!/usr/bin/env python3 -I
"""Boots the port straight into a battle (no menus, no replay) and writes its outputs to a directory.

Usage:
  tools/battle_boot.py --out DIR --p1 25,4,7 --p2 113 --arena 2 --moves 85,85 --seed 1 [options]
  tools/battle_boot.py --out DIR --case my_case.txt [options]          (a hand-written case file, see src/port/xcheck.cpp)

The case file (DIR/case.txt) is written from:
  --p1 / --p2 SPECIES[,SPECIES...]   each side's party (Pokedex numbers, up to 6; the first is the lead)
  --level N[,N...]                   levels of both sides (default 50; one value for the whole side or one per Pokemon);
                                     --level1 / --level2 for one side
  --moves M[,M...]                   move ids the player's lead uses on consecutive turns (e.g. 85,85): the lead
                                     knows those moves (up to 4 different ones) and a scripted decision uses each
  --moves2 M[,M...]                  the same for side 2 (default: side 2 is the computer)
  --known1 / --known2 M[,M...]       the lead's moves explicitly (a side 2 that is the computer needs its own)
  --name1 / --name2 NAME             trainer names (ASCII)
  --act1 / --act2 m0,m1,s2,...       explicit scripted decisions instead (m<slot> move slot, s<slot> switch to party slot)
  --hp1 / --hp2 N                    the lead's HP (max HP stays the stat; default full)
  --status1 / --status2 N            the lead's status byte (Gen 1: 8 poison, 16 burn, 32 freeze, 64 paralysis, 1-7 sleep)
  --arena N                          stadium_models file: 0 Free Battle, 1-3 Cups, 4 Petit, 5 Pika, 6 Prime, 7-16 Castle, 17 Mewtwo
  --seed N                           random seed (default 1)
  --intro 0|1                        1 (default): frames start with the send-out intro; 0: it runs undrawn, frames start after it
  --think N                          game frames a scripted side's menu is shown before its choice (default 20)
  --turns N                          stop after N turns (default: when the battle ends)
  --nomusic                          no battle music / announcer
Outputs (all into DIR):
  --frames-every N                   every N-th battle game frame as frames/f_<n>.ppm (n: frame since the start; 0: none, default 0)
  --res WxH                          size of those frames (default 640x480)
  --max-frames N                     stop after N game frames of the dump
  --trace                            DIR/trace.txt: the battle events and random values (the xcheck trace)
  --camlog / --partlog               DIR/cam.txt (camera per frame) / DIR/part.txt (drawn particles)
  --anim [--anim-frames N]           DIR/anim/m<k>_<move>/f_<n>.ppm: 160x120 frames of each move animation (xcheck)
  --twice                            run a second time into DIR_b and compare every output (determinism check)
Needs the built port (--bin, default build-macos/pokestadium or build/pokestadium) and the ROM (--rom or $ROM). One game
instance runs at a time; the window is hidden.
"""
import hashlib, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from runner_platform import process_options

ROM = os.environ.get("ROM", "/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64")
VALUE_OPTIONS = {"--out", "--case", "--p1", "--p2", "--level", "--moves", "--moves2", "--act1", "--act2", "--hp1", "--hp2",
                 "--status1", "--status2", "--arena", "--seed", "--intro", "--think", "--turns", "--frames-every", "--res",
                 "--max-frames", "--anim-frames", "--bin", "--rom", "--name1", "--name2", "--mode", "--known1", "--known2", "--level1", "--level2"}
FLAGS = {"--nomusic", "--trace", "--camlog", "--partlog", "--anim", "--twice", "--help"}


def ints(text):
    return [int(x, 0) for x in text.split(",") if x != ""]


def case_text(a):
    lines = ["# written by tools/battle_boot.py"]
    if "--arena" in a:
        lines.append("arena %d" % int(a["--arena"]))
    lines.append("seed %d" % int(a.get("--seed", 1)))
    lines.append("intro %d" % int(a.get("--intro", 1)))
    if "--think" in a:
        lines.append("think %d" % int(a["--think"]))
    if "--mode" in a:
        lines.append("mode %s" % a["--mode"].replace(",", " "))
    for side, key in ((0, "--p1"), (1, "--p2")):
        n = side + 1
        party = ints(a.get(key, "25" if side == 0 else "113"))
        levels = ints(a.get("--level%d" % n, a.get("--level", "50")))
        script = a.get("--moves%s" % ("" if side == 0 else "2"))
        moves = ints(a["--known%d" % n]) if "--known%d" % n in a else []
        acts = []
        if script:
            for m in ints(script):
                if m not in moves:
                    moves.append(m)
                acts.append("m%d" % moves.index(m))
        if "--act%d" % n in a:
            acts = a["--act%d" % n].split(",")
        if "--name%d" % n in a:
            lines.append("trainer %d %s" % (side, a["--name%d" % n]))
        for slot, species in enumerate(party[:6]):
            lv = levels[slot] if slot < len(levels) else levels[-1]
            mv = moves[:4] if slot == 0 and moves else [33]
            fields = "species=%d level=%d" % (species, lv)
            for i, m in enumerate(mv[:4]):
                fields += " m%d=%d" % (i + 1, m)
            if slot == 0 and "--hp%d" % n in a:
                fields += " hp=%d" % int(a["--hp%d" % n])
            if slot == 0 and "--status%d" % n in a:
                fields += " status=%d" % int(a["--status%d" % n])
            lines.append("mon %d %d %s" % (side, slot, fields))
        if acts:
            lines.append("act %d %s" % (side, " ".join(acts)))
    if "--turns" in a:
        lines.append("stop %d" % int(a["--turns"]))
    if "--nomusic" in a:
        lines.append("nomusic")
    return "\n".join(lines) + "\n"


def run(out, a):
    """one run into out; returns the seconds it took"""
    os.makedirs(out, exist_ok=True)
    case = os.path.join(out, "case.txt")
    if "--case" in a:
        shutil.copyfile(a["--case"], case)
    else:
        open(case, "w").write(case_text(a))
    home = os.path.join(out, "home")
    os.makedirs(home, exist_ok=True)
    binary = os.path.abspath(a.get("--bin") or next(
        (p for p in (os.path.join(ROOT, "build-macos", "pokestadium"), os.path.join(ROOT, "build", "pokestadium"))
         if os.path.exists(p)), os.path.join(ROOT, "build", "pokestadium")))
    env = dict(os.environ, PORT_HIDDEN_WINDOW="1", PORT_SPEED="max", PORT_INTERPOLATE="0", PORT_BATTLE=case,
               SHIP_HOME=home)  # (an own home: the window size and settings of this run only)
    # the options of this run alone, not the caller's environment
    for k in ("PORT_BATTLE_FRAMES", "PORT_BATTLE_EVERY", "PORT_BATTLE_RES", "PORT_BATTLE_MAX_FRAMES", "PORT_REPLAY",
              "PORT_XCHECK_CASE", "PORT_XCHECK_ANIM", "PORT_CAMLOG", "PORT_PARTLOG", "PORT_SCREENSHOT_DIR"):
        env.pop(k, None)
    every = int(a.get("--frames-every", 0))
    if every > 0:
        env["PORT_BATTLE_FRAMES"] = os.path.join(out, "frames")
        env["PORT_BATTLE_EVERY"] = str(every)
        env["PORT_BATTLE_RES"] = a.get("--res", "640x480")
        if "--max-frames" in a:
            env["PORT_BATTLE_MAX_FRAMES"] = a["--max-frames"]
    if "--trace" in a:
        env["PORT_XCHECK"] = os.path.join(out, "trace.txt")
    if "--camlog" in a:
        env["PORT_CAMLOG"] = os.path.join(out, "cam.txt")
    if "--partlog" in a:
        env["PORT_PARTLOG"] = os.path.join(out, "part.txt")
    if "--anim" in a:
        env["PORT_XCHECK_ANIM"] = os.path.join(out, "anim")
        env["PORT_RENDER_EVERY"] = "1"
        env["PORT_XCHECK_ANIM_FRAMES"] = a.get("--anim-frames", "200")
    for name in ("trace.txt", "cam.txt", "part.txt"):
        try:
            os.remove(os.path.join(out, name))  # the logs append
        except OSError:
            pass
    shutil.rmtree(os.path.join(out, "frames"), ignore_errors=True)
    start = time.time()
    with open(os.path.join(out, "run.log"), "w") as log:
        p = subprocess.Popen([binary, a.get("--rom", ROM), "--battle", case], cwd=os.path.dirname(binary), env=env,
                             stdout=log, stderr=subprocess.STDOUT, **process_options())
        code = p.wait()
    secs = time.time() - start
    print("%s: exit %d in %.1f s" % (out, code, secs))
    if code != 0:
        print("  see %s" % os.path.join(out, "run.log"))
    return secs


def digest(out):
    """name -> md5 of every output (the frames as one hash over the sorted files)"""
    res = {}
    for name in ("trace.txt", "cam.txt", "part.txt"):
        p = os.path.join(out, name)
        if os.path.exists(p):
            res[name] = hashlib.md5(open(p, "rb").read()).hexdigest()
    for sub in ("frames", "anim"):
        root = os.path.join(out, sub)
        if os.path.isdir(root):
            h = hashlib.md5()
            n = 0
            for d, _, files in sorted(os.walk(root)):
                for f in sorted(files):
                    h.update(f.encode())
                    h.update(open(os.path.join(d, f), "rb").read())
                    n += 1
            res["%s (%d files)" % (sub, n)] = h.hexdigest()
    return res


def main(argv):
    a = {}
    i = 0
    while i < len(argv):
        k = argv[i]
        if k in FLAGS:
            a[k] = True
            i += 1
        elif k in VALUE_OPTIONS and i + 1 < len(argv):
            a[k] = argv[i + 1]
            i += 2
        else:
            sys.exit("unknown option %s (--help)" % k)
    if "--help" in a or "--out" not in a:
        print(__doc__)
        return 0 if "--help" in a else 2
    out = os.path.abspath(a["--out"])
    run(out, a)
    if "--twice" in a:
        other = out.rstrip("/") + "_b"
        run(other, a)
        d1, d2 = digest(out), digest(other)
        same = d1 == d2
        for k in d1:
            print("  %-22s %s %s" % (k, d1[k], "==" if d1[k] == d2.get(k) else "!= " + str(d2.get(k))))
        print("deterministic: %s" % ("yes" if same else "NO"))
        return 0 if same else 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3 -I
"""Plays the stored input recordings (replays/*.rec) and reports which still reach their stop frame.

Usage: tools/run_replays.py [--all] [--update] [-j N] [--rom PATH] [name ...]   (or PORT_ROM=<path>)

Each recording runs at max speed with the game window hidden (PORT_HIDDEN_WINDOW=1), several at a time (-j, default:
the number of CPU cores minus two), from a fresh copy of its save
(replays/<name>.flash, else replays/default.flash), until its stop frame. replays/replays.txt lists the stop frame and
extra settings per recording (default: the recording's "# end" frame). The stop frame is saved as
build/replays/<name>.ppm and compared with build/replays/ref/<name>.ppm when that exists (a local reference picture:
game pictures are not committed); --update writes the references. Results: OK, DIFF (picture differs from the
reference), BADADDR (the renderer or a DMA met a wild address: "[timg] bad", "[dma-bad]"; usually unswapped big-endian ROM
data), FREEZE (the game stopped changing the picture), CRASH, TIMEOUT. Recordings marked "long" in the list run
only with --all or when named. Exit status 1 when any recording did not pass.
"""
import os, shutil, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPLAYS = os.path.join(ROOT, "replays")
BUILD = os.path.join(ROOT, "build")
OUT = os.path.join(BUILD, "replays")
DEFAULT_ROM = os.environ.get("PORT_ROM", "/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64")
DIFF_LIMIT = 2.0  # mean absolute difference per sampled channel value that still counts as the same picture


def manifest():
    """name -> (stop frame or None, {env}, long)"""
    entries = {}
    path = os.path.join(REPLAYS, "replays.txt")
    if os.path.exists(path):
        for line in open(path):
            parts = line.split("#", 1)[0].split()
            if not parts:
                continue
            name, rest = parts[0], parts[1:]
            stop, env, long_run = None, {}, False
            for p in rest:
                if p == "long":
                    long_run = True
                elif "=" in p:
                    k, v = p.split("=", 1)
                    env[k] = v
                elif p.isdigit():
                    stop = int(p)
            entries[name] = (stop, env, long_run)
    return entries


def end_frame(rec):
    end = None
    for line in open(rec):
        if line.startswith("# end"):
            end = int(line.split()[2])
    return end


def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][-w * h * 3:]


def mad(a, b):
    if a[0] != b[0] or a[1] != b[1]:
        return 255.0
    x, y = a[2][::13], b[2][::13]
    return sum(abs(p - q) for p, q in zip(x, y)) / max(1, len(x))


def run_one(name, stop, env_extra, rom, update):
    rec = os.path.join(REPLAYS, name + ".rec")
    save = os.path.join(REPLAYS, name + ".flash")
    if not os.path.exists(save):
        save = os.path.join(REPLAYS, "default.flash")
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ)
        env.update({
            "PORT_HIDDEN_WINDOW": "1", "PORT_SPEED": "max", "PORT_EXIT_ON_FREEZE": "1",
            "PORT_REPLAY": rec, "PORT_RECORD": os.path.join(tmp, "out.rec"),
            "PORT_STOP_AT": str(stop), "PORT_SCREENSHOT_DIR": tmp,
        })
        missing = [v for k, v in env_extra.items() if (k == "GBSAVE" or k == "PORT_GB_ROM")
                   and not os.path.exists(os.path.join(ROOT, v))]
        if missing:
            return name, "SKIPPED", 0.0, "missing %s (not in the repository: your own cartridge dump)" % ", ".join(missing)
        if os.path.exists(save):
            shutil.copyfile(save, os.path.join(tmp, "save.flash"))  # the run writes to its copy only
            env["PORT_FLASH_FILE"] = os.path.join(tmp, "save.flash")
        for k, v in env_extra.items():
            path = os.path.join(ROOT, v)
            if k == "GBSAVE":  # a Game Boy save: the run writes to a fresh copy
                shutil.copyfile(path, os.path.join(tmp, "gb.sav"))
                env["PORT_GB_SAVE"] = os.path.join(tmp, "gb.sav")
            else:  # values naming a file in the repository (gb/..., replays/...) become absolute
                env[k] = path if os.path.exists(path) else v
        start = time.time()
        log = open(os.path.join(OUT, name + ".log"), "w")
        try:
            # timeout: generous for max speed (a few thousand frames per second), at least 2 minutes
            code = subprocess.run([os.path.join(BUILD, "pokestadium"), rom], cwd=BUILD, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=max(120, stop / 200)).returncode
        except subprocess.TimeoutExpired:
            code = None
        log.close()
        secs = time.time() - start
        # the first frame drawn at or after the stop frame (the game may not draw on every retrace)
        shots = sorted(f for f in os.listdir(tmp) if f.startswith("stop_") and f.endswith(".ppm"))
        shot = os.path.join(tmp, shots[0]) if shots else ""
        if code is None:
            return name, "TIMEOUT", secs, ""
        if code == 3:
            return name, "FREEZE", secs, "see build/replays/%s.log" % name
        if code != 0 or not os.path.exists(shot):
            return name, "CRASH", secs, "exit %s, see build/replays/%s.log" % (code, name)
        bad = [l.strip()[:120] for l in open(os.path.join(OUT, name + ".log"), errors="replace")
               if l.startswith(("[timg] bad", "[dma-bad]"))]
        result = os.path.join(OUT, name + ".ppm")
        shutil.copyfile(shot, result)
    if bad:
        return name, "BADADDR", secs, "%d lines, first: %s" % (len(bad), bad[0])
    ref = os.path.join(OUT, "ref", name + ".ppm")
    if update or not os.path.exists(ref):
        shutil.copyfile(result, ref)
        return name, "OK", secs, "reference written" if update else "new reference"
    d = mad(read_ppm(result), read_ppm(ref))
    return name, ("OK" if d <= DIFF_LIMIT else "DIFF"), secs, "difference %.2f" % d


def main():
    args = sys.argv[1:]
    run_all = "--all" in args
    update = "--update" in args
    jobs, rom, names = max(1, (os.cpu_count() or 4) - 2), DEFAULT_ROM, []
    i = 0
    while i < len(args):
        if args[i] == "-j":
            jobs = int(args[i + 1]); i += 1
        elif args[i] == "--rom":
            rom = args[i + 1]; i += 1
        elif not args[i].startswith("--"):
            names.append(args[i][:-4] if args[i].endswith(".rec") else os.path.basename(args[i]))
        i += 1
    if not os.path.exists(os.path.join(BUILD, "pokestadium")):
        sys.exit("build/pokestadium is missing: build first")
    if not os.path.exists(rom):
        sys.exit("ROM not found: %s (use --rom)" % rom)
    os.makedirs(os.path.join(OUT, "ref"), exist_ok=True)
    listed = manifest()
    todo = []
    for rec in sorted(f for f in os.listdir(REPLAYS) if f.endswith(".rec")):
        name = rec[:-4]
        stop, env, long_run = listed.get(name, (None, {}, False))
        if names and name not in names:
            continue
        if long_run and not run_all and not names:
            continue
        todo.append((name, stop or end_frame(os.path.join(REPLAYS, rec)), env))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(lambda t: run_one(t[0], t[1], t[2], rom, update), todo))
    width = max([len(r[0]) for r in results] + [9])
    for name, status, secs, note in results:
        print("%-*s  %-7s %6.1f s  %s" % (width, name, status, secs, note))
    sys.exit(0 if all(r[1] in ("OK", "SKIPPED") for r in results) else 1)


if __name__ == "__main__":
    main()

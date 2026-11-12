#!/usr/bin/env python3 -I
"""Plays the stored input recordings (replays/*.rec) and reports which still reach their stop frame.

Usage: tools/run_replays.py [--all] [--update] [-j N] [--rom PATH] [name ...]   (or PORT_ROM=<path>)
       [--port-bin PATH] [--backend directx|opengl|metal|vulkan] [--out DIR] [--reference-dir DIR]
       [--shader-capture | --shader-verify DIR] [--gb-dir DIR] [--replay-dir DIR]

Each recording runs at max speed with the game window hidden (PORT_HIDDEN_WINDOW=1), several at a time (-j, default:
the number of CPU cores minus two), from a fresh copy of its save
(replays/<name>.flash, else replays/default.flash), until its stop frame. replays/replays.txt lists the stop frame and
extra settings per recording (default: the recording's "# end" frame). The stop frame is saved as
build/replays/<name>.ppm and compared with build/replays/ref/<name>.ppm when that exists (a local reference picture:
game pictures are not committed); --update writes the references. Results: OK, DIFF (picture differs from the
reference), BADADDR (the renderer or a DMA met a wild address: "[timg] bad", "[dma-bad]"; usually unswapped big-endian ROM
data), FREEZE (the game stopped changing the picture), CRASH, TIMEOUT. Recordings marked "long" in the list run
only with --all or when named. Exit status 1 when any recording did not pass.
--shader-capture disables warm-up and retains shader recipes from successful runs.
--shader-verify loads staged manifests and reports new recipes or first-draw builds.
Use --reference-dir with the capture run's ref directory to check rendered output.
"""
import os, shutil, subprocess, sys, tempfile, time, json, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runner_platform import BACKENDS, backend_home, actual_backend, process_options
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPLAYS = os.path.join(ROOT, "replays")
BUILD = os.path.join(ROOT, "build")
OUT = os.path.join(BUILD, "replays")
PORT_BIN = os.path.join(BUILD, "pokestadium.exe" if os.name == "nt" else "pokestadium")
BACKEND = None
REFERENCE_DIR = None
SHADER_CAPTURE = False
SHADER_VERIFY = None
GB_DIR = None
SHADER_FILES = ("shaders.txt", "vulkan_pipelines.txt")
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


def recipe_rows(path):
    if not os.path.isfile(path):
        return set()
    with open(path) as f:
        return {tuple(int(part, 16) for part in line.split("#", 1)[0].split())
                for line in f if line.split("#", 1)[0].strip()}


def run_one(name, stop, env_extra, rom, update):
    rec = os.path.join(REPLAYS, name + ".rec")
    save = os.path.join(REPLAYS, name + ".flash")
    if not os.path.exists(save):
        save = os.path.join(REPLAYS, "default.flash")
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=(os.name == "nt")) as tmp:
        env = dict(os.environ)
        env.update({
            "PORT_HIDDEN_WINDOW": "1", "PORT_SPEED": "max", "PORT_EXIT_ON_FREEZE": "1",
            "PORT_REPLAY": rec, "PORT_RECORD": os.path.join(tmp, "out.rec"),
            "PORT_STOP_AT": str(stop), "PORT_SCREENSHOT_DIR": tmp,
            # shader warm-up: the shipped list only; programs drawn are recorded in the run's own file
            "PORT_SHADER_LIST": os.path.join(tmp, "shaders.txt"),
        })
        def fixture(value):
            if GB_DIR and value.startswith("gb/"):
                return os.path.join(GB_DIR, value.removeprefix("gb/"))
            if REPLAYS != os.path.join(ROOT, "replays"):
                local = os.path.join(REPLAYS, value)
                if os.path.isfile(local):
                    return local
            return os.path.join(ROOT, value)

        missing = [v for k, v in env_extra.items() if (k == "GBSAVE" or k == "PORT_GB_ROM")
                   and not os.path.exists(fixture(v))]
        if missing:
            return name, "SKIPPED", 0.0, "missing %s (not in the repository: your own cartridge dump)" % ", ".join(missing)
        if os.path.exists(save):
            shutil.copyfile(save, os.path.join(tmp, "save.flash"))  # the run writes to its copy only
            env["PORT_FLASH_FILE"] = os.path.join(tmp, "save.flash")
        for k, v in env_extra.items():
            path = fixture(v)
            if k == "GBSAVE":  # a Game Boy save: the run writes to a fresh copy
                shutil.copyfile(path, os.path.join(tmp, "gb.sav"))
                env["PORT_GB_SAVE"] = os.path.join(tmp, "gb.sav")
            else:  # values naming a file in the repository (gb/..., replays/...) become absolute
                env[k] = path if os.path.exists(path) else v
        start = time.time()
        home = os.path.join(tmp, "home")
        backend_home(env, home, BACKEND)
        before = {}
        if SHADER_CAPTURE or SHADER_VERIFY:
            env["PORT_SHADER_LIST"] = os.path.join(home, "shaders.txt")
            env["PORT_SCREENSHOT_MAX_MB"] = "0"
            env["PORT_SCREENSHOT_STOP_ONLY"] = "1"
            # Max-speed replays normally render every tenth retrace; coverage
            # must include effects that appear between those frames.
            env["PORT_RENDER_EVERY"] = "1"
            env["PORT_FREEZE_CHECK"] = "0"
            env["PORT_TRACE_SHADERS"] = "1"
            if SHADER_CAPTURE:
                env["PORT_NO_SHADER_WARMUP"] = "1"
            else:
                env.pop("PORT_NO_SHADER_WARMUP", None)
                env.pop("PORT_VK_NO_PREBUILD", None)
                if BACKEND == "vulkan":
                    env["PORT_VK_STATS"] = "1"
                for file in SHADER_FILES:
                    dest = os.path.join(home, file)
                    shutil.copyfile(os.path.join(SHADER_VERIFY, file), dest)
                    before[file] = recipe_rows(dest)
        log = open(os.path.join(OUT, name + ".log"), "w")
        try:
            # Coverage draws every frame, which needs a larger budget than normal max-speed runs.
            frames_per_second = 40 if os.name == "nt" or SHADER_CAPTURE or SHADER_VERIFY else 200
            timeout = max(300 if os.name == "nt" else 120, stop / frames_per_second)
            # PORT_REPLAY_TIMEOUT=<s>: a longer budget (runs that draw every frame and its in-between frames)
            timeout = max(timeout, float(os.environ.get("PORT_REPLAY_TIMEOUT", "0")))
            code = subprocess.run([PORT_BIN, rom], cwd=tmp, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=timeout, **process_options()).returncode
        except subprocess.TimeoutExpired:
            code = None
        log.close()
        secs = time.time() - start
        selected = actual_backend(os.path.join(tmp, "home")) if BACKEND else None
        if BACKEND and selected != BACKENDS[BACKEND][0]:
            return name, "BACKEND", secs, "requested %s, actual backend id %s" % (BACKEND, selected)
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
        if SHADER_CAPTURE or SHADER_VERIFY:
            recipe_dir = os.path.join(OUT, "shader-recipes", name)
            os.makedirs(recipe_dir, exist_ok=True)
            counts = []
            for file in SHADER_FILES:
                path = os.path.join(home, file)
                saved = os.path.join(recipe_dir, file)
                if os.path.isfile(path):
                    shutil.copyfile(path, saved)
                elif os.path.isfile(saved):
                    os.remove(saved)
                if SHADER_VERIFY:
                    counts.append((file, len(recipe_rows(path) - before[file])))
            if SHADER_VERIFY:
                with open(os.path.join(OUT, name + ".log"), errors="replace") as f:
                    trace = f.read()
                if "[shader] startup warm-up complete; starting intro" not in trace:
                    return name, "SHADER_MISS", secs, "startup warm-up did not complete"
                if BACKEND == "metal":
                    builds = len(re.findall(r"^\[shader\] [0-9a-f]+/[0-9a-f]+ built in", trace, re.M))
                    counts.append(("first-draw programs", builds))
                    if re.search(r"failed to (?:compile shader library|create pipeline state)", trace, re.I):
                        return name, "SHADER_MISS", secs, "Metal shader or pipeline build failed"
                if BACKEND == "vulkan":
                    stats = re.findall(r"^\[vkstats\] 60 frames: (\d+) programs \((\d+) prebuilt\) [\d.]+ ms, (\d+) pipelines",
                                       trace, re.M)
                    if not stats:
                        return name, "SHADER_MISS", secs, "no Vulkan frame statistics"
                    counts.append(("first-draw programs", sum(int(total) - int(warm) for total, warm, _ in stats)))
                    counts.append(("first-draw pipelines", sum(int(pipelines) for _, _, pipelines in stats)))
                    if re.search(r"pipeline creation failed|glslang.*failed", trace, re.I):
                        return name, "SHADER_MISS", secs, "Vulkan shader or pipeline build failed"
                if any(count for _, count in counts):
                    return name, "SHADER_MISS", secs, ", ".join(f"{n}: {count}" for n, count in counts)
        result = os.path.join(OUT, name + ".ppm")
        shutil.copyfile(shot, result)
    if bad:
        return name, "BADADDR", secs, "%d lines, first: %s" % (len(bad), bad[0])
    ref = os.path.join(REFERENCE_DIR or os.path.join(OUT, "ref"), name + ".ppm")
    if REFERENCE_DIR and not os.path.exists(ref):
        return name, "NOREF", secs, "reference missing: %s" % ref
    if update or not os.path.exists(ref):
        shutil.copyfile(result, ref)
        return name, "OK", secs, "reference written" if update else "new reference"
    d = mad(read_ppm(result), read_ppm(ref))
    return name, ("OK" if d <= DIFF_LIMIT else "DIFF"), secs, "difference %.2f" % d


def main():
    global PORT_BIN, OUT, BACKEND, REFERENCE_DIR, SHADER_CAPTURE, SHADER_VERIFY, GB_DIR, REPLAYS
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
        elif args[i] == "--port-bin":
            PORT_BIN = os.path.abspath(args[i + 1]); i += 1
        elif args[i] == "--out":
            OUT = os.path.abspath(args[i + 1]); i += 1
        elif args[i] == "--backend":
            BACKEND = args[i + 1]; i += 1
            if BACKEND not in BACKENDS:
                sys.exit("backend must be directx, opengl, metal or vulkan")
        elif args[i] == "--shader-capture":
            SHADER_CAPTURE = True
        elif args[i] == "--shader-verify":
            SHADER_VERIFY = os.path.abspath(args[i + 1]); i += 1
        elif args[i] == "--gb-dir":
            GB_DIR = os.path.abspath(args[i + 1]); i += 1
        elif args[i] == "--replay-dir":
            REPLAYS = os.path.abspath(args[i + 1]); i += 1
        elif args[i] == "--reference-dir":
            REFERENCE_DIR = os.path.abspath(args[i + 1]); i += 1
        elif not args[i].startswith("--"):
            names.append(args[i][:-4] if args[i].endswith(".rec") else os.path.basename(args[i]))
        i += 1
    if SHADER_CAPTURE and SHADER_VERIFY:
        sys.exit("--shader-capture and --shader-verify are mutually exclusive")
    if (SHADER_CAPTURE or SHADER_VERIFY) and not BACKEND:
        sys.exit("shader coverage runs require --backend")
    if SHADER_VERIFY and (not REFERENCE_DIR or update):
        sys.exit("--shader-verify requires an unchanged --reference-dir from a capture run")
    if SHADER_VERIFY and BACKEND not in ("metal", "vulkan"):
        sys.exit("--shader-verify requires Metal or Vulkan first-draw statistics")
    if not os.path.exists(PORT_BIN):
        sys.exit("port binary is missing: %s" % PORT_BIN)
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
    if not todo:
        sys.exit("no matching replays")
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(run_one, t[0], t[1], t[2], rom, update): t[0] for t in todo}
        results = []
        for future in as_completed(futures):
            try:
                row = future.result()
            except Exception as error:
                row = (futures[future], "ERROR", 0.0, "%s: %s" % (type(error).__name__, error))
            results.append(row)
            n, s, t, v = row
            with open(os.path.join(OUT, n + ".result.json"), "w") as f:
                json.dump(dict(name=n, status=s, seconds=t, note=v), f, indent=2)
            print("%s: %s (%.1f s) %s" % row, flush=True)
    results.sort()
    width = max([len(r[0]) for r in results] + [9])
    for name, status, secs, note in results:
        print("%-*s  %-7s %6.1f s  %s" % (width, name, status, secs, note))
    with open(os.path.join(OUT, "results.json"), "w") as f:
        json.dump({"backend": BACKEND, "binary": PORT_BIN, "shader_capture": SHADER_CAPTURE,
                   "results": [dict(name=n, status=s, seconds=t, note=v)
                   for n, s, t, v in results]}, f, indent=2)
    sys.exit(0 if all(r[1] in ("OK", "SKIPPED") for r in results) else 1)


if __name__ == "__main__":
    main()

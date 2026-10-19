#!/usr/bin/env python3 -I
"""Writes src/port/shader_warmup.txt, the shader programs the port builds ahead while the launcher shows
(src/port/shader_warmup.cpp; packed into pokestadium.o2r), from the programs the stored recordings draw.

Usage: tools/shader_list.py [--run] [shaders.txt ...]
  --run         first plays all recordings (tools/run_replays.py --all) with PORT_TRACE_SHADERS=1
  shaders.txt   lists the port recorded while playing (next to pokestadium.cfg.json, or PORT_SHADER_LIST): added too
Reads the "[shader] <id0>/<id1> ..." lines of build/replays/*.log. Programs drawn by more recordings come first
(the boot and menu ones), then by their average position in the logs. Programs in the current list that no log
draws (e.g. from recordings skipped for want of a cartridge dump) stay, after those of the given files.
"""
import glob, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIST = os.path.join(ROOT, "src/port/shader_warmup.txt")
HEADER = "# Shader programs built ahead (src/port/shader_warmup.cpp): <id0> <id1>. Written by tools/shader_list.py.\n"


def main():
    if "--run" in sys.argv[1:]:
        env = dict(os.environ, PORT_TRACE_SHADERS="1")
        subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools/run_replays.py"), "--all"], env=env)
    seen = {}  # id -> [recordings, sum of positions]
    for log in sorted(glob.glob(os.path.join(ROOT, "build/replays/*.log"))):
        pos = 0
        for line in open(log, errors="replace"):
            parts = line.split()
            if len(parts) >= 3 and parts[0] == "[shader]" and "/" in parts[1] and parts[2] in ("built", "prebuilt"):
                key = tuple(parts[1].split("/"))
                entry = seen.setdefault(key, [0, 0])
                entry[0] += 1
                entry[1] += pos
                pos += 1
    if not seen:
        sys.exit("no [shader] lines in build/replays/*.log: run with --run (or PORT_TRACE_SHADERS=1)")
    order = sorted(seen, key=lambda k: (-seen[k][0], seen[k][1] / seen[k][0], k))
    old = []
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    for path in files + ([LIST] if os.path.exists(LIST) else []):
        for line in open(path):
            parts = line.split("#", 1)[0].split()
            if len(parts) == 2 and tuple(parts) not in seen and tuple(parts) not in old:
                old.append(tuple(parts))
    with open(LIST, "w") as f:
        f.write(HEADER)
        for k in order + old:
            f.write("%s %s\n" % k)
    print("wrote %s: %d programs (%d from logs, %d from lists)" % (LIST, len(order) + len(old), len(order), len(old)))


if __name__ == "__main__":
    main()

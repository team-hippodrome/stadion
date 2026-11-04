#!/usr/bin/env python3 -I
"""Writes src/port/shader_warmup.txt, the shader programs the port builds ahead while the launcher shows
(src/port/shader_warmup.cpp; packed into pokestadium.o2r), from the programs the stored recordings draw.

For coverage work, merge successful --shader-capture runs into a staging directory:
  tools/shader_list.py --capture-dir DIR [--capture-dir DIR ...] --candidate-dir DIR
This mode preserves the shipped warm-up lists and writes both candidate manifests.

Usage: tools/shader_list.py [--run] [shaders.txt ...]
  --run         first plays all recordings (tools/run_replays.py --all) with PORT_TRACE_SHADERS=1
  shaders.txt   lists the port recorded while playing (next to pokestadium.cfg.json, or PORT_SHADER_LIST): added too
Reads the "[shader] <id0>/<id1> ..." lines of build/replays/*.log. Programs drawn by more recordings come first
(the boot and menu ones), then by their average position in the logs. Programs in the current list that no log
draws (e.g. from recordings skipped for want of a cartridge dump) stay, after those of the given files.
"""
import argparse
import glob
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIST = os.path.join(ROOT, "src/port/shader_warmup.txt")
HEADER = "# Shader programs built ahead (src/port/shader_warmup.cpp): <id0> <id1>. Written by tools/shader_list.py.\n"


def read_recipes(path, fields):
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text().splitlines():
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        values = tuple(int(word, 16) for word in words)
        if len(values) != fields or any(not 0 <= value < 2**64 for value in values[:2]):
            raise ValueError(f"invalid shader recipe in {path}: {line}")
        if fields == 3 and not 0 <= values[2] <= 255:
            raise ValueError(f"invalid Vulkan pipeline state in {path}: {line}")
        rows.append(values)
    return rows


def stage_captures(args):
    shipped = Path(LIST).parent
    programs = dict.fromkeys(read_recipes(shipped / "shader_warmup.txt", 2))
    pipelines = dict.fromkeys(read_recipes(shipped / "vulkan_pipeline_warmup.txt", 3))
    original = len(programs), len(pipelines)
    accepted = excluded = 0
    for capture in args.capture_dir:
        data = json.loads((capture / "results.json").read_text())
        if not data.get("shader_capture"):
            raise ValueError(f"{capture} is not a shader capture")
        for result in data["results"]:
            if result["status"] != "OK":
                excluded += 1
                continue
            path = capture / "shader-recipes" / result["name"]
            if not path.is_dir():
                raise ValueError(f"missing shader capture for {result['name']} in {capture}")
            accepted += 1
            for program in read_recipes(path / "shaders.txt", 2):
                programs.setdefault(program)
            for recipe in read_recipes(path / "vulkan_pipelines.txt", 3):
                programs.setdefault(recipe[:2])
                pipelines.setdefault(recipe)
    if not accepted:
        raise ValueError("no successful replay captures")
    args.candidate_dir.mkdir(parents=True, exist_ok=True)
    (args.candidate_dir / "shaders.txt").write_text(
        "".join(f"{a:016x} {b:016x}\n" for a, b in programs))
    (args.candidate_dir / "vulkan_pipelines.txt").write_text(
        "".join(f"{a:016x} {b:016x} {state:x}\n" for a, b, state in pipelines))
    print(f"{accepted} successful captures; {excluded} excluded")
    print(f"{len(programs)} programs (+{len(programs) - original[0]}), "
          f"{len(pipelines)} pipelines (+{len(pipelines) - original[1]})")


def main():
    if "--capture-dir" in sys.argv or "--candidate-dir" in sys.argv:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--capture-dir", type=Path, action="append", required=True)
        parser.add_argument("--candidate-dir", type=Path, required=True)
        stage_captures(parser.parse_args())
        return
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

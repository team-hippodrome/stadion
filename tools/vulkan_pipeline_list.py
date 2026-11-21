#!/usr/bin/env python3 -I
"""Writes src/port/vulkan_pipeline_warmup.txt, the Vulkan pipelines the warm-up builds ahead (gfx_vulkan.cpp,
PrebuildShadersAsync; packed into stadion.o2r), from vulkan_pipelines.txt files the port recorded.

Usage: tools/vulkan_pipeline_list.py [--msaa N] FILE ... [--msaa N] FILE ... [--check FILE ...]
  FILE        a vulkan_pipelines.txt (a profile's, or a capture: run_replays.py --shader-capture, shader-recipes/*/)
  --msaa N    the anti-aliasing setting the following files were recorded with (default: unknown)
  --check     report how many pipelines of these files a fresh profile builds on first draw with the current list
              (--list FILE: another list), for static and dynamic depth state and for the old exact-key matching

A recorded line is "<shader id0> <shader id1> <render state>", the state's low four bits the sample count of the frame
buffer drawn to. The shipped list stores the sample count as 0 ("the MSAA setting": the game's frame buffers) or 1
(a single-sampled buffer whatever the setting), so one list serves every setting. A file recorded with 2x or more tells
the two apart; a 1x pipeline from a file recorded with 1x (or an unknown setting) could be either, and is listed both
ways unless another file settles it. The current list is kept (and normalised): entries are only added.
Capture at 2x: tools/run_replays.py --all --backend vulkan --shader-capture with PORT_TEST_MSAA=2.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIST = os.path.join(ROOT, "src/port/vulkan_pipeline_warmup.txt")
HEADER = ("# Vulkan pipelines built ahead (gfx_vulkan.cpp PrebuildShadersAsync): <id0> <id1> <render state>, the state's\n"
          "# low four bits 0 = the MSAA setting, 1 = single-sampled. Written by tools/vulkan_pipeline_list.py.\n")


def read(path):
    rows = []
    for line in open(path):
        words = line.split("#", 1)[0].split()
        if len(words) == 3:
            rows.append(tuple(int(w, 16) for w in words))
    return rows


def normalise(rows, msaa):
    """-> {(id0, id1, state without sample count): set of 0/1}, sure, and the same for ambiguous 1x rows"""
    sure, unsure = {}, {}
    for a, b, state in rows:
        samples, rest = state & 15, state & ~15
        if samples == 0 or samples > 1:
            sure.setdefault((a, b, rest), set()).add(0)
        elif msaa is not None and msaa > 1:
            sure.setdefault((a, b, rest), set()).add(1)
        else:
            unsure.setdefault((a, b, rest), set()).update((0, 1))
    return sure, unsure


def merge(groups):
    sure, unsure = {}, {}
    for rows, msaa in groups:
        s, u = normalise(rows, msaa)
        for k, v in s.items():
            sure.setdefault(k, set()).update(v)
        for k, v in u.items():
            unsure.setdefault(k, set()).update(v)
    out = set()
    for (a, b, rest), samples in sure.items():
        out.update((a, b, rest | n) for n in samples)
    for (a, b, rest), samples in unsure.items():
        if (a, b, rest) not in sure:
            out.update((a, b, rest | n) for n in samples)
    return out


def check(groups, list_path):
    shipped = read(list_path)
    by_program = {}
    for a, b, state in shipped:
        by_program.setdefault((a, b), []).append(state)
    for rows, msaa in groups:
        setting = msaa or 1
        exact = {(a, b, s) for a, b, s in shipped}
        for dynamic in (False, True):
            missed = 0
            for a, b, key in set(rows):
                slots = set()
                for state in by_program.get((a, b), ()):
                    samples = 1 if state & 15 == 1 else setting
                    full = (state & ~15) | samples
                    slots.add(full & 0x1F if dynamic else full)
                if (key & 0x1F if dynamic else key) not in slots:
                    missed += 1
            label = "dynamic depth" if dynamic else "static depth"
            print(f"  {label}: {missed} of {len(set(rows))} pipelines not built ahead")
        print(f"  exact-key matching (the v0.0.15 rule): {sum(1 for r in set(rows) if r not in exact)} not built ahead")


def main():
    args = sys.argv[1:]
    groups, msaa, checking, list_path = [], None, False, LIST
    i = 0
    while i < len(args):
        if args[i] == "--msaa":
            msaa = int(args[i + 1]); i += 1
        elif args[i] == "--check":
            checking = True
        elif args[i] == "--list":
            list_path = args[i + 1]; i += 1
        else:
            groups.append((read(args[i]), msaa, args[i]))
        i += 1
    if checking:
        for rows, m, path in groups:
            print(f"{path} (recorded with {m or 'unknown'}x):")
            check([(rows, m)], list_path)
        return
    old = read(LIST) if os.path.exists(LIST) else []
    rows = merge([(old, None)] + [(r, m) for r, m, _ in groups])
    with open(LIST, "w") as f:
        f.write(HEADER)
        for a, b, state in sorted(rows):
            f.write(f"{a:016x} {b:016x} {state:x}\n")
    print(f"{len(old)} -> {len(rows)} pipelines ({len({r[:2] for r in rows})} programs)")


if __name__ == "__main__":
    main()

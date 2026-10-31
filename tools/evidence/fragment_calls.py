#!/usr/bin/env python3 -I
"""Forced entries for game fragments no run entered: a direct call of Fragment_LoadAndCall(id, ROM start, ROM end,
arg0, arg1) as the game's FRAGMENT_LOAD_AND_CALL does, for run.py --call.

Usage:
  tools/evidence/fragment_calls.py <binary> <callgraph.json> <out.json> [<run dir>...] [--at "Fn#n"] [--args a0,a1]
For each fragment in src/port/generated/port_fragment_entries.c whose entry function did not run in any of the given
runs (coverage.json), writes {fragment, entry, file, call} with call = "<at>:Fragment_LoadAndCall:i:i<id>,i<rom start>,
i<relocs ROM end>,i<arg0>,i<arg1>". The id is (TEXT_START & 0x0FF00000) >> 20 - 0x10 (FRAGMENT_ID); the ROM values are
the absolute fragmentNN_* symbols in the binary (src/port/generated/abs_syms.S). Default hook: Game_DoCopyProtection#2
(the main loop's second state, after the N64 logo), args 0,0. A forced entry runs a mode without the state the retail
flow sets up for it: records from it say "forced fragment entry".
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def main():
    a = sys.argv[1:]
    opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
    binary, cg_path, out = a[0], a[1], a[2]
    runs = [x for x in a[3:] if not x.startswith("--") and os.path.isdir(x)]
    at = opt("--at", "Game_DoCopyProtection#2")
    args = opt("--args", "0,0").split(",")
    syms = {}
    for line in subprocess.run(["nm", binary], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] == "A" and p[2].startswith("_fragment"):
            syms[p[2][1:]] = int(p[0], 16)
    src = open(os.path.join(ROOT, "src", "port", "generated", "port_fragment_entries.c")).read()
    entries = re.findall(r"\{ (0x[0-9A-F]+), (0x[0-9A-F]+), \(void\*\)(\w+), \"(fragment\d+)\" \}", src)
    reached = set()
    for r in runs:
        try:
            reached |= set(json.load(open(os.path.join(r, "coverage.json")))["functions"])
        except (OSError, ValueError):
            pass
    F = json.load(open(cg_path))["functions"]
    res = []
    for rom, vaddr, entry, frag in entries:
        if entry in reached:
            continue
        text = syms.get(frag + "_TEXT_START", int(vaddr, 16))
        start, end = syms.get(frag + "_ROM_START"), syms.get(frag + "_relocs_ROM_END")
        if start is None or end is None:
            continue
        fid = ((text & 0x0FF00000) >> 20) - 0x10
        res.append({"fragment": frag, "entry": entry, "file": F.get(entry, {}).get("file", ""),
                    "call": "%s:Fragment_LoadAndCall:i:i%d,i%d,i%d,i%s,i%s" % (at, fid, start, end, args[0], args[1])})
    json.dump(res, open(out, "w"), indent=1)
    print("%d fragments not entered by the given runs (of %d)" % (len(res), len(entries)))
    for r in res:
        print("  %s %s (%s)" % (r["fragment"], r["entry"], r["file"]))


if __name__ == "__main__":
    main()

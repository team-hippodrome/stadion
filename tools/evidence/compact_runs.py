#!/usr/bin/env python3 -I
"""Compacts a batch of runs for later unions: merges every run's coverage.profraw(.k*) into <out dir>/merged.profdata
and keeps each run's coverage.json (the function data), dropping the large raw counter files.

Usage: tools/evidence/compact_runs.py <out dir> <run dir>...
frontier.py, covdiff.py and variant_matrix.py accept the out dir like a run dir (they read *.profdata there).
"""
import json, os, shutil, subprocess, sys


def main():
    out, runs = sys.argv[1], sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    raws = [os.path.join(r, f) for r in runs for f in sorted(os.listdir(r))
            if f == "coverage.profraw" or f.startswith("coverage.profraw.k") or f.endswith(".profdata")]
    tool = subprocess.run(["xcrun", "-f", "llvm-profdata"], capture_output=True, text=True).stdout.strip()
    prev = os.path.join(out, "merged.profdata")
    inputs = raws + ([prev] if os.path.exists(prev) else [])
    subprocess.run([tool, "merge", "-sparse", "-o", prev + ".new"] + inputs, check=True)
    os.replace(prev + ".new", prev)
    fns = {}
    for r in runs:
        p = os.path.join(r, "coverage.json")
        if os.path.exists(p):
            for name, f in json.load(open(p))["functions"].items():
                fns.setdefault(name, 0)
                fns[name] += f["count"]
    p = os.path.join(out, "coverage.json")
    if os.path.exists(p):
        for name, f in json.load(open(p))["functions"].items():
            fns[name] = fns.get(name, 0) + f["count"]
    json.dump({"functions": {n: {"count": c, "first_frame": 0, "sites": []} for n, c in fns.items()}, "paths": [],
               "seq": [], "watch": []}, open(p, "w"))
    for r in raws:
        if not r.endswith(".profdata"):
            os.remove(r)
    print("%s: %d raw files merged, %d functions" % (out, len(raws), len(fns)))


if __name__ == "__main__":
    main()

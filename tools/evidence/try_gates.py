#!/usr/bin/env python3 -I
"""Tries frontier.py's memory-setting proposals: one port run per proposal (the battle of tools/battle_xcheck.py,
seed and frames as given), and reports which ones reached the gate's callee and what else became reachable.

Usage:
  tools/evidence/try_gates.py <proposals.json> <baseline run dir>... --out DIR [--bin B] [--seed N] [--frames N]
                              [--jobs N] [--first N]
Writes DIR/<k>/ (run.py output) and DIR/gates.md / gates.json: per proposal: reached or not, new functions.
Runs are forced conditions (port-only): the game did not produce that state itself.
"""
import json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    a = sys.argv[1:]
    opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
    props = json.load(open(a[0]))[:int(opt("--first", "1000"))]
    baselines = []
    i = 1
    while i < len(a) and not a[i].startswith("--"):
        baselines.append(a[i])
        i += 1
    out = os.path.abspath(opt("--out"))
    os.makedirs(out, exist_ok=True)
    base = set()
    for b in baselines:
        base |= set(json.load(open(os.path.join(b, "coverage.json")))["functions"])

    def one(k):
        p = props[k]
        d = os.path.join(out, "g%03d" % k)
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "run.py"), d, "--bin", opt("--bin"), "--seed",
                        opt("--seed", "7"), "--frames", opt("--frames", "14000"), "--seq-max", "5", "--set", p["set"]],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            fns = set(json.load(open(os.path.join(d, "coverage.json")))["functions"])
        except (OSError, ValueError):
            return dict(p, reached=False, new=[], error="run failed")
        for f in ("seq.txt", "port_last.rec", "port.flash"):
            if os.path.exists(os.path.join(d, f)):
                os.remove(os.path.join(d, f))
        return dict(p, reached=p["callee"] in fns, new=sorted(fns - base), run=d)

    with ThreadPoolExecutor(int(opt("--jobs", "2"))) as ex:
        res = list(ex.map(one, range(len(props))))
    json.dump(res, open(os.path.join(out, "gates.json"), "w"), indent=1)
    md = ["# Memory-set gate trials", "", "| gate | set | callee reached | new functions |", "|---|---|---|---|"]
    for r in res:
        md.append("| %s | `%s` | %s | %d%s |" % (r["gate"], r["set"], "yes" if r["reached"] else "no",
                                                len(r["new"]), " (%s)" % r.get("error") if r.get("error") else ""))
    allnew = set()
    for r in res:
        allnew |= set(r["new"])
    md += ["", "%d of %d proposals reached their callee; %d functions new over the baselines." % (
        sum(r["reached"] for r in res), len(res), len(allnew))]
    open(os.path.join(out, "gates.md"), "w").write("\n".join(md) + "\n")
    print(md[-1])


if __name__ == "__main__":
    main()

#!/usr/bin/env python3 -I
"""Tries frontier.py's memory-setting proposals: one port run per proposal (the battle of tools/battle_xcheck.py,
seed and frames as given), and reports which ones reached the gate's callee and what else became reachable.

Usage:
  tools/evidence/try_gates.py <proposals.json> <baseline run dir>... --out DIR [--bin B] [--seed N] [--frames N]
                              [--jobs N] [--first N]
Each proposal is replayed on a baseline run in which its caller ran (that run's scenario.json: recording, cartridges,
flash, moves, forced values), so it works for any part of the game; without such a run, on the battle route.
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

    covs = {}
    for b in baselines:
        covs[b] = set(json.load(open(os.path.join(b, "coverage.json")))["functions"])

    def scenario_args(b):
        """run.py arguments that replay baseline run b"""
        sc = {}
        if os.path.exists(os.path.join(b, "scenario.json")):
            sc = json.load(open(os.path.join(b, "scenario.json")))
        a = ["--seed", str(sc.get("seed") or opt("--seed", "7")), "--frames", str(sc.get("frames") or opt("--frames", "14000"))]
        for k in ("moves", "gb", "gb2", "rec", "call", "call_buf"):
            if sc.get(k):
                a += ["--" + k.replace("_", "-"), sc[k]]
        rec = sc.get("rec") or ""
        flash = sc.get("flash")
        if not flash and rec:  # runs made before scenario.json kept these: a stored recording's own flash
            cand = os.path.splitext(rec)[0] + ".flash"
            flash = cand if os.path.exists(cand) else None
        if flash:
            a += ["--flash", flash]
        if sc.get("natural") or (rec and ("/replays/" in rec or rec.startswith("replays/") or rec.endswith("mash.rec"))):
            a.append("--natural")
        q = [l for l in sc.get("queue") or [] if l.strip()]
        if q:
            qf = os.path.join(out, "queue_%d.txt" % abs(hash(b)))
            open(qf, "w").write("\n".join(q) + "\n")
            a += ["--queue", qf]
        return a

    def one(k):
        try:
            return one_(k)
        except subprocess.TimeoutExpired:  # a forced condition the game cannot handle (it stalls): the run is lost
            subprocess.run(["pkill", "-f", os.path.join(out, "g%03d" % k)])
            return dict(props[k], reached=False, new=[], error="stalled (timeout)")

    def one_(k):
        p = props[k]
        d = os.path.join(out, "g%03d" % k)
        caller = p["gate"].split(" -> ")[0]
        host = next((b for b in baselines if caller in covs[b]), None)
        args = scenario_args(host) if host else ["--seed", opt("--seed", "7"), "--frames", opt("--frames", "14000")]
        p["route"] = os.path.basename(host.rstrip("/")) if host else "battle_xcheck"
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "run.py"), d, "--bin", opt("--bin")] + args +
                       ["--seq-max", "3", "--dump-every", "600", "--set", p["set"]],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=int(opt("--timeout", "900")))
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
    md = ["# Memory-set gate trials", "", "| gate | set | replayed on | callee reached | new functions |",
          "|---|---|---|---|---|"]
    for r in res:
        md.append("| %s | `%s` | %s | %s | %d%s |" % (r["gate"], r["set"], r.get("route", ""), "yes" if r["reached"] else "no",
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

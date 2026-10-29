#!/usr/bin/env python3 -I
"""Runs one reproducible battle in the instrumented port (build-evidence, cmake/evidence.cmake) and resolves its
call data to function names.

Usage:
  tools/evidence/run.py <out dir> --bin build-evidence/pokestadium [--seed N] [--queue FILE] [--frames N]
                        [--watch Name1,Name2] [--seq-max N] [--ares] [--moves m1,m2,m3,m4[:p1,p2,p3,p4]]

The battle is tools/battle_xcheck.py's (Poke Cup round 1, the same rentals, the player's moves picked by the
game's time-limit fallback; every battle random value forced from the seed, after the values queued in FILE:
lines "<random function> <value>"). With --ares the same battle also runs in ares (tools/ares_ref, ARES_REF_BUILD)
for the event-by-event comparison (tools/battle_xcheck.py compare <out dir>).
Output in <out dir>: calls.txt / seq.txt (raw, from src/port/evidence/evidence_rt.c), port.trace (xcheck events),
coverage.json: {"functions": {name: {"count", "first_frame",
                                     "sites": [[instrumented caller, return-address function, count, frame, ctx]]}},
                "paths": [[count, first_frame, [innermost..outer]]], "seq": [...], "watch": [...]}
"""
import bisect, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BASE = 0x100000000


def symbols(binary):
    """sorted [(offset, name)] of the text symbols (local ones too: nm, not dladdr)"""
    out = subprocess.run(["nm", "-n", binary], capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tT":
            name = p[2][1:] if p[2].startswith("_") else p[2]
            syms.append((int(p[0], 16) - BASE, name))
    return syms


class Resolver:
    def __init__(self, syms):
        self.syms = syms
        self.addrs = [a for a, _ in syms]
        self.by_name = {}
        for a, n in syms:
            self.by_name.setdefault(n, a)

    def fn(self, off):
        """the function containing offset off"""
        i = bisect.bisect_right(self.addrs, off) - 1
        return self.syms[i][1] if i >= 0 else "?"


def parse(out, res):
    funcs, paths = {}, []
    for line in open(os.path.join(out, "calls.txt")):
        p = line.split()
        if p[0] == "F":
            fn, parent, site = res.fn(int(p[1], 16)), int(p[2], 16), int(p[3], 16)
            count, frame = int(p[4]), int(p[5])
            ctx = p[6] if len(p) > 6 else ""
            f = funcs.setdefault(fn, {"count": 0, "first_frame": frame, "sites": []})
            f["count"] += count
            f["first_frame"] = min(f["first_frame"], frame)
            # [instrumented caller or None, function holding the return address (site-1: inside the call), count,
            #  first frame, ctx]
            f["sites"].append([res.fn(parent) if parent else None, res.fn(site - 1), count, frame, ctx])
        elif p[0] == "P":
            paths.append([int(p[1]), int(p[2]), [res.fn(int(x, 16)) for x in p[3:]]])
    seq, watch = [], []
    sp = os.path.join(out, "seq.txt")
    if os.path.exists(sp):
        for line in open(sp):
            p = line.split()
            if not p or not line.endswith("\n"):
                continue
            if p[0] == "C":
                seq.append([int(p[1]), int(p[2]), res.fn(int(p[3], 16)), res.fn(int(p[4], 16) - 1),
                            p[5] if len(p) > 5 else ""])
            elif p[0] == "W":
                kv = dict(x.split("=", 1) for x in p[5:] if "=" in x)
                watch.append({"what": p[1], "frame": int(p[2]), "fn": res.fn(int(p[3], 16)),
                              "caller": res.fn(int(p[4], 16) - 1), **kv})
    return {"functions": funcs, "paths": paths, "seq": seq, "watch": watch}


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        print(__doc__)
        sys.exit(1)
    out = os.path.abspath(args[0])
    opt = lambda k, d=None: args[args.index(k) + 1] if k in args else d
    binary = os.path.abspath(opt("--bin", os.path.join(ROOT, "build-evidence", "pokestadium")))
    os.makedirs(out, exist_ok=True)
    res = Resolver(symbols(binary))
    env = dict(os.environ, PORT_EVID=os.path.join(out, "calls.txt"), PORT_EVID_SEQ=os.path.join(out, "seq.txt"),
               PORT_EVID_SEQ_MAX=opt("--seq-max", "40"))
    if opt("--watch"):
        offs = []
        for name in opt("--watch").split(","):
            if name not in res.by_name:
                sys.exit("unknown function %s" % name)
            offs.append("0x%x" % res.by_name[name])
        env["PORT_EVID_WATCH"] = ",".join(offs)
    if opt("--moves"):  # port-only: the player's active Pokemon gets these moves (src/port/evidence/evidence_rt.c)
        env["PORT_EVID_MOVES"] = opt("--moves")
        env["PORT_EVID_INJECT_AT"] = "0x%x" % res.by_name[opt("--inject-at", "BattleScene_HandlePlayerActionInput")]
    seed = opt("--seed", "1")
    if opt("--queue"):
        seed += ":" + os.path.abspath(opt("--queue"))
    cmd = [sys.executable, "-I", os.path.join(ROOT, "tools", "battle_xcheck.py"), "run", out, "--seed", seed,
           "--frames", opt("--frames", "20000"), "--port-bin", binary]
    if "--ares" not in args:
        cmd.append("--port-only")
    json.dump({"seed": opt("--seed", "1"), "queue": open(opt("--queue")).read().split("\n") if opt("--queue") else [],
               "moves": opt("--moves"), "frames": opt("--frames", "20000"), "ares": "--ares" in args,
               "setup": "tools/battle_xcheck.py route(): Poke Cup round 1, Charmander/Venusaur/Ivysaur vs the Biker"},
              open(os.path.join(out, "scenario.json"), "w"), indent=1)
    subprocess.run(cmd, env=env, check=True)
    cov = parse(out, res)
    json.dump(cov, open(os.path.join(out, "coverage.json"), "w"))
    print("%d functions reached, %d call paths, %d sequence lines, %d watch records" % (
        len(cov["functions"]), len(cov["paths"]), len(cov["seq"]), len(cov["watch"])))


if __name__ == "__main__":
    main()

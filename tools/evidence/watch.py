#!/usr/bin/env python3 -I
"""Helpers for forced scenarios and for reading the watched functions' state.

  tools/evidence/watch.py queue <baseline run dir> <out file> <function> <call index> <value> [<function> <k> <v>]...
      a PORT_XCHECK_RNG queue that replays the baseline run's values of <function> up to the call index and then
      forces <value> at it (later calls continue the seeded stream unchanged: the stream index counts queued calls)
  tools/evidence/watch.py diff <run dir> <function> [--max N]
      each watched call of <function>: caller, frame, and the battle fields that changed from entry to exit
      (BattleMonRuntime of user and target, both ActivePokemonState, BattleScene, gBattleDamage; decoded with
      tools/battle_xcheck.py's layouts)
  tools/evidence/watch.py randoms <run dir> [--around FRAME]
      the forced random calls (function, call index, argument, value, frame), to pick the call to force
  tools/evidence/watch.py ares <out dir> <seed> <case file> <frames>
      the same battle in ares with a case file of the lockstep sweep's driver ("rule <function>@<caller> <v>,..."
      lines: forced values for the calls from that caller, the other calls keep the seeded stream). Needs an
      ares_ref built from the battle-xcheck work tree that reads ARES_REF_XCHECK_CASE (EVID_ARES_BIN, default
      build-ares-ref/ares_ref, with build-ares-ref/battle_engine.syms) and ARES_REF_SRC for the PIF firmware
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import battle_xcheck as bx  # noqa: E402  (its layouts and trace parser; used as is)


def randoms(run):
    return [r for r in bx.parse_trace(os.path.join(run, "port.trace")) if r["t"] == "R"]


def cmd_queue(args):
    run, out, rest = args[0], args[1], args[2:]
    forced = {}
    for i in range(0, len(rest), 3):
        forced.setdefault(rest[i], {})[int(rest[i + 1])] = int(rest[i + 2])
    lines = []
    rs = randoms(run)
    for fn, ks in forced.items():
        if fn not in bx.LOGIC_FNS:
            sys.exit("only the battle-logic functions can be queued: %s" % ", ".join(bx.LOGIC_FNS))
        base = [r["val"] for r in rs if r["fn"] == fn]
        last = max(ks)
        for k in range(last + 1):
            lines.append("%s %d\n" % (fn, ks[k] if k in ks else base[k]))
    open(out, "w").writelines(lines)
    print("wrote %d queued values to %s" % (len(lines), out))


def decode_all(w):
    d = {}
    for key, layout in (("user", bx.RUNTIME), ("target", bx.RUNTIME), ("st0", bx.STATE), ("st1", bx.STATE),
                        ("scene", bx.SCENE)):
        raw = bytes.fromhex(w.get(key, ""))
        if raw:
            for k, v in bx.decode(layout, raw, "<").items():
                d["%s.%s" % (key, k)] = v
    d["dmg"] = int(w.get("dmg", 0))
    return d


def watch_calls(run, fn):
    cov = json.load(open(os.path.join(run, "coverage.json")))
    stack, calls = [], []
    for w in cov["watch"]:
        if w["fn"] != fn:
            continue
        if w["what"] == "enter":
            stack.append(w)
        elif stack:
            e = stack.pop()
            a, b = decode_all(e), decode_all(w)
            calls.append({"frame": e["frame"], "caller": e["caller"], "frames": w["frame"] - e["frame"],
                          "before": a, "changed": {k: [a.get(k), b[k]] for k in b if a.get(k) != b[k]}})
    return calls


def cmd_diff(args):
    run, fn = args[0], args[1]
    mx = int(args[args.index("--max") + 1]) if "--max" in args else 20
    species, moves = bx.names()
    calls = watch_calls(run, fn)
    print("%s: %d watched calls" % (fn, len(calls)))
    for c in calls[:mx]:
        b = c["before"]
        print("frame %d from %s: user %s (%s) -> target %s, %d frames" % (
            c["frame"], c["caller"], species.get(b.get("user.species"), b.get("user.species")),
            moves.get(b.get("user.used_move"), b.get("user.used_move")),
            species.get(b.get("target.species"), b.get("target.species")), c["frames"]))
        for k, (x, y) in sorted(c["changed"].items()):
            if k.endswith("idle_anim") or k == "scene.unk_7A":
                continue
            print("    %s: %s -> %s" % (k, x, y))


def cmd_randoms(args):
    run = args[0]
    around = int(args[args.index("--around") + 1]) if "--around" in args else None
    for r in randoms(run):
        if r["fn"] not in bx.LOGIC_FNS:
            continue
        if around is None or abs(r["frame"] - around) < 400:
            print("%s %d arg=%d val=%d frame=%d" % (r["fn"], r["k"], r["arg"], r["val"], r["frame"]))


def cmd_ares(args):
    import shutil, subprocess
    out, seed, case, frames = os.path.abspath(args[0]), args[1], os.path.abspath(args[2]), int(args[3])
    root = os.path.dirname(os.path.dirname(HERE))
    binary = os.environ.get("EVID_ARES_BIN", os.path.join(root, "build-ares-ref", "ares_ref"))
    os.makedirs(out, exist_ok=True)
    bx.write_inputs(out, frames)
    shutil.copyfile(os.path.join(root, "replays", "default.flash"), os.path.join(out, "ares.flash"))
    link = os.path.join(out, "rom.z64")
    if os.path.lexists(link):
        os.remove(link)
    os.symlink(bx.ROM, link)
    env = dict(os.environ, DYLD_LIBRARY_PATH="/Applications/ares.app/Contents/Frameworks",
               ARES_REF_FIRMWARE=os.path.join(os.environ["ARES_REF_SRC"], "mia", "Firmware", "Nintendo 64"),
               ARES_REF_FLASH=os.path.join(out, "ares.flash"), ARES_REF_XCHECK=os.path.join(out, "ares.trace"),
               ARES_REF_XCHECK_RNG=seed, ARES_REF_XCHECK_COM="1", ARES_REF_XCHECK_CASE=case,
               ARES_REF_XCHECK_SYMS=os.path.join(os.path.dirname(binary), "battle_engine.syms"))
    with open(os.path.join(out, "ares.log"), "w") as log:
        r = subprocess.run([binary, link, os.path.join(out, "ares.txt")], env=env, stdout=log, stderr=subprocess.STDOUT,
                           preexec_fn=bx.low_priority)
    print("ares: exit %d" % r.returncode)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    {"queue": cmd_queue, "diff": cmd_diff, "randoms": cmd_randoms, "ares": cmd_ares}[sys.argv[1]](sys.argv[2:])

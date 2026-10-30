#!/usr/bin/env python3 -I
"""Classes every unreached function by why no run reached it, and gives coverage of the code the shipped game can run.

Usage:
  tools/evidence/dead_classes.py <whole-game callgraph.json> <frontier.json> <rom.z64> <data_dead.json> <out.md>
frontier.json: tools/evidence/frontier.py's output (its "unreached" list). data_dead.json: {"<reason>": [names]} for
code the shipped data never selects, each reason with its proof in the evidence (e.g. battle_anim_slots.md).

Classes, the first that applies:
  1. referenced by nothing: no caller, address-taking or pointer table in the C code of the whole decomp and no word in
     the ROM equal to the function's N64 address (tools/evidence/deadcode.py's test); dead_calls.md has what a
     forced direct call of each returned
  2. data never selects it: data_dead.json
  3. PAL TV mode only: the name says PAL (the US build runs NTSC; the PAL branch is chosen by the TV type)
  4. 64DD hardware: the 64DD library (leo*.c and the disk boot); the port has no drive. Counted apart: not dead code,
     hardware the port does not present
  5. reachable only from dead code: every caller is in classes 1-3 or 5 (repeated until nothing changes)
  6. live: the rest, still to reach, with the gate class from frontier.json where a reached caller holds a gate
Coverage of reachable code = reached / (functions - classes 1, 2, 3, 5); with the 64DD library also left out.
"""
import collections, json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deadcode  # noqa: E402  (addresses(): name -> N64 address from the port's function table)

LEO = re.compile(r"/leo[^/]*\.c$|/bootdisk\.c$|/leomecha\.c$|/n64dd|/readwrite\.c$|/seek\.c$|/testunitready\.c$|"
                 r"/setrtc\.c$|/leoc2ecc\.c$")


def main():
    cg_path, fr_path, rom_path, data_path, out = sys.argv[1:6]
    cg = json.load(open(cg_path))
    F = cg["functions"]
    fr = json.load(open(fr_path))
    unreached = set(fr["unreached"])
    data_dead = json.load(open(data_path))
    callers = collections.defaultdict(set)
    for a, b, k, v in cg["edges"]:
        if a != b:
            callers[b].add(a)
    for t, fns in cg["tables"].items():
        for f in fns:
            callers[f].add("table " + t)
    rom = open(rom_path, "rb").read()
    addr = deadcode.addresses()

    cls = {}
    for n in unreached:
        if not callers.get(n) and (n not in addr or rom.find(struct.pack(">I", addr[n])) < 0):
            cls[n] = "referenced by nothing"
    for reason, names in data_dead.items():
        for n in names:
            if n in unreached and n not in cls:
                cls[n] = "data never selects it: " + reason
    for n in unreached:
        if n not in cls and "PAL" in n:
            cls[n] = "PAL TV mode only"
    for n in unreached:
        if n not in cls and LEO.search("/" + F.get(n, {}).get("file", "")):
            cls[n] = "64DD hardware (no drive in the port)"
    dead = lambda n: n in cls and not cls[n].startswith(("64DD", "live"))
    changed = True
    while changed:
        changed = False
        for n in unreached:
            if n in cls:
                continue
            cs = callers.get(n, set())
            # a pointer table counts as a live reference: its slot may be selected (data_dead.json says otherwise)
            if cs and all(not c.startswith("table ") and dead(c) for c in cs):
                cls[n] = "reachable only from dead code"
                changed = True
    gate_class = {}
    for g in fr["gates"]:
        gate_class.setdefault(g["callee"], g["class"])
    for n in unreached:
        if n not in cls:
            cls[n] = "live: " + gate_class.get(n, "below other unreached code")

    total, reached = fr["functions"], fr["reached"]
    by = collections.Counter(c.split(":")[0] if c.startswith(("live", "data never")) else c for c in cls.values())
    n_dead = sum(1 for n in cls if dead(n))
    n_leo = sum(1 for c in cls.values() if c.startswith("64DD"))
    reach = total - n_dead
    md = ["# Unreached code by class", "",
          "Functions %d, reached %d (%.1f%%). Unreached %d: proven dead %d, 64DD hardware %d, live %d." % (
              total, reached, 100.0 * reached / total, len(unreached), n_dead, n_leo, len(unreached) - n_dead - n_leo),
          "",
          "**Coverage of the code the shipped game can run: %d of %d (%.1f%%); without the 64DD library: %d of %d "
          "(%.1f%%).**" % (reached, reach, 100.0 * reached / reach, reached, reach - n_leo,
                           100.0 * reached / (reach - n_leo)),
          "", "Classes (`tools/evidence/dead_classes.py` docstring: the tests and their order):", "",
          "| class | functions |", "|---|---|"]
    for k, v in by.most_common():
        md.append("| %s | %d |" % (k, v))
    sub = collections.Counter(c for c in cls.values() if c.startswith(("live", "data never")))
    md += ["", "| subclass | functions |", "|---|---|"] + ["| %s | %d |" % (k, v) for k, v in sorted(sub.items())]
    area = lambda n: (re.search(r"fragments/([^/]+)/", F.get(n, {}).get("file", "")) or
                      re.search(r"([^/]+)$", F.get(n, {}).get("file", "?"))).group(1)
    for title, pred in [("Live, per area", lambda c: c.startswith("live")),
                        ("Proven dead, per area", lambda c: not c.startswith(("live", "64DD")))]:
        per = collections.defaultdict(list)
        for n, c in cls.items():
            if pred(c):
                per[area(n)].append(n)
        md += ["", "## " + title, "", "| area | functions | names |", "|---|---|---|"]
        for a, ns in sorted(per.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            md.append("| %s | %d | %s |" % (a, len(ns), ", ".join(sorted(ns))))
    open(out, "w").write("\n".join(md) + "\n")
    json.dump(cls, open(os.path.splitext(out)[0] + ".json", "w"), indent=1, sort_keys=True)
    print(md[2])
    print(md[4])


if __name__ == "__main__":
    main()

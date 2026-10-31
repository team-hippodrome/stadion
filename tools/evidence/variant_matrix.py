#!/usr/bin/env python3 -I
"""Compares runs of the same routes with different save variants: what each variant reaches that the route's base
save (and the route without a cartridge) does not, and which functions only one variant reaches.

Usage:
  tools/evidence/variant_matrix.py <runs dir> <callgraph.json> <out.md> [--base base] [--nocart nocart]
Runs are named <route>__<variant> (tools/evidence/run.py output dirs, coverage.json). The call graph (callgraph.py
over the instrumented files) gives each function's file, for the per-area summary.
"""
import collections, json, os, re, sys


def area(path):
    m = re.search(r"fragments/([^/]+)/", path)
    return m.group(1) if m else os.path.basename(path) or "?"


def main():
    a = sys.argv[1:]
    runs_dir, cg_path, out = a[0], a[1], a[2]
    base = a[a.index("--base") + 1] if "--base" in a else "base"
    nocart = a[a.index("--nocart") + 1] if "--nocart" in a else "nocart"
    F = json.load(open(cg_path))["functions"]
    runs = {}
    for d in sorted(os.listdir(runs_dir)):
        p = os.path.join(runs_dir, d, "coverage.json")
        if "__" in d and os.path.exists(p):
            route, var = d.split("__", 1)
            runs[(route, var)] = set(json.load(open(p))["functions"]) & set(F)
    routes = sorted({r for r, _ in runs})
    nocart_all = set().union(*[s for (r, v), s in runs.items() if v == nocart]) if any(
        v == nocart for _, v in runs) else set()
    cart_all = set().union(*[s for (r, v), s in runs.items() if v != nocart])
    by_var = collections.defaultdict(set)
    for (r, v), s in runs.items():
        by_var[v] |= s
    md = ["# Game Boy save variants: what each one reaches", "",
          "%d runs over %d routes (%s). Whole game instrumented (all game sources)." % (len(runs), len(routes),
                                                                                     ", ".join(routes)), "",
          "All runs: %d functions; with a cartridge %d; **%d only with a cartridge** (in no run without one)." % (
              len(cart_all | nocart_all), len(cart_all), len(cart_all - nocart_all)), ""]
    only = collections.Counter(area(F[f]["file"]) for f in cart_all - nocart_all)
    md += ["Cartridge-only functions per area: " + ", ".join("%s %d" % kv for kv in only.most_common()) + ".", ""]
    for route in routes:
        b = runs.get((route, base), set())
        n = runs.get((route, nocart))
        md += ["## %s" % route, "", "| variant | functions | new vs %s | new vs no cartridge | examples (new vs %s) |" % (
            base, base), "|---|---|---|---|---|"]
        for (r, v), s in sorted(runs.items()):
            if r != route:
                continue
            nb = sorted(s - b) if v != base else []
            nn = len(s - n) if n is not None and v != nocart else "-"
            md.append("| %s | %d | %s | %s | %s |" % (v, len(s), len(nb) if v != base else "-", nn,
                                                       ", ".join(nb[:6]) + (" ..." if len(nb) > 6 else "")))
        md.append("")
    md += ["## Functions only one variant reaches", "", "| variant | functions | list (first 15) |", "|---|---|---|"]
    for v, s in sorted(by_var.items()):
        others = set().union(*[t for w, t in by_var.items() if w != v])
        u = sorted(s - others)
        md.append("| %s | %d | %s |" % (v, len(u), ", ".join(u[:15]) + (" ..." if len(u) > 15 else "")))
    open(out, "w").write("\n".join(md) + "\n")
    print("%d runs; %d functions with a cartridge only" % (len(runs), len(cart_all - nocart_all)))


if __name__ == "__main__":
    main()

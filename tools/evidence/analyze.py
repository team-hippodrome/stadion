#!/usr/bin/env python3 -I
"""Merges the static call graph with the runtime coverage of one or more runs into per-function evidence records.

Usage:
  tools/evidence/analyze.py <callgraph.json> <out dir> <run dir>... [--reviews reviews.json]

<run dir>: output of tools/evidence/run.py (coverage.json, plus scenario.json if the run was made for a record).
--reviews: the hand-checked part of the records (observed effects, ares comparison, verdict), keyed by function.
Writes <out dir>/records.json (one record per function, format in tools/evidence/README.md), coverage.md (numbers,
uncovered functions per file, functions with more than one runtime caller, dynamic-only edges) and records.md (the
reviewed records).
"""
import collections, json, os, re, sys

MAX_PATHS = 3


def static_paths(fn, callers_of, roots, limit=MAX_PATHS, depth=12):
    """up to limit shortest paths root -> ... -> fn through the static edges (BFS backwards)"""
    out, queue, seen = [], collections.deque([[fn]]), {fn}
    while queue and len(out) < limit:
        path = queue.popleft()
        head = path[0]
        if head in roots and len(path) > 1 or not callers_of.get(head):
            out.append(path)
            continue
        if len(path) >= depth:
            continue
        for c in sorted(callers_of[head]):
            if c not in seen:
                seen.add(c)
                queue.append([c] + path)
    return out


def port_changed(functions):
    """functions whose body carries a PORT: comment (the port changed them: port-only evidence is weaker there)"""
    by_file = collections.defaultdict(list)
    for n, f in functions.items():
        if f["line"] and not f["external"]:
            by_file[f["file"]].append((f["line"], n))
    changed = set()
    for rel, defs in by_file.items():
        path = None
        for base in ("game/", ""):
            if os.path.exists(base + rel):
                path = base + rel
        if path is None:
            continue
        lines = open(path, errors="replace").read().split("\n")
        defs.sort()
        for i, (line, n) in enumerate(defs):
            end = defs[i + 1][0] - 1 if i + 1 < len(defs) else len(lines)
            if any("PORT:" in l for l in lines[line - 1:end]):
                changed.add(n)
    return changed


def main():
    args = sys.argv[1:]
    reviews = {}
    if "--reviews" in args:
        i = args.index("--reviews")
        reviews = json.load(open(args[i + 1]))
        del args[i:i + 2]
    cg_path, out, runs = args[0], args[1], args[2:]
    cg = json.load(open(cg_path))
    F = cg["functions"]
    # the decomp's functions: defined in the selected files (not header inlines, generated wrappers or the port's
    # own additions such as Port_XcheckBattleEvent)
    internal = {n for n, f in F.items() if not f["external"] and "fragments/" in f["file"] and
                not n.startswith(("Port_", "PortRegister"))}
    callers_of = collections.defaultdict(set)
    static_callers = collections.defaultdict(list)
    for a, b, kind, via in cg["edges"]:
        if a == b:
            continue
        callers_of[b].add(a)
        static_callers[b].append({"caller": a, "kind": kind, "via": via})
    roots = {n for n in internal if not callers_of.get(n)}
    changed = port_changed(F)

    rt = collections.defaultdict(lambda: {"count": 0, "callers": collections.Counter(), "first": {}, "runs": set()})
    paths = collections.Counter()
    dyn_edges = collections.Counter()
    for r in runs:
        cov = json.load(open(os.path.join(r, "coverage.json")))
        name = os.path.basename(r.rstrip("/"))
        for fn, f in cov["functions"].items():
            d = rt[fn]
            d["count"] += f["count"]
            d["runs"].add(name)
            for parent, sitefn, count, frame, ctx in f["sites"]:
                if parent is None:
                    caller, via = sitefn, ""          # entered from code outside the instrumented files
                elif sitefn == parent or sitefn in internal:
                    caller, via = parent, ""          # direct (or the compiler inlined fn: the site is one level up)
                else:
                    caller, via = parent, sitefn      # through code outside the instrumented files
                key = (caller, via)
                d["callers"][key] += count
                if key not in d["first"]:
                    d["first"][key] = {"run": name, "frame": frame, "ctx": ctx}
                if caller not in callers_of.get(fn, ()) and caller != fn:
                    dyn_edges[(caller, via, fn)] += count
        for count, frame, p in cov["paths"]:
            paths[tuple(p)] += count

    by_fn_paths = collections.defaultdict(list)
    for p, c in paths.items():
        by_fn_paths[p[0]].append((c, list(reversed(p))))

    records = {}
    for n in sorted(internal):
        f = F[n]
        d = rt.get(n)
        rec = {"name": n, "comment": f["comment"], "file": "%s:%d" % (f["file"], f["line"]),
               "static": f["static"], "port_changed": n in changed,
               "static_callers": static_callers.get(n, []),
               "static_paths": static_paths(n, callers_of, roots),
               "runtime": None, "scenario": None, "observed": None, "ares": None,
               "verdict": "not reached" if not d else "reached, not reviewed", "notes": ""}
        if d:
            rec["runtime"] = {
                "count": d["count"], "runs": sorted(d["runs"]),
                "callers": [{"caller": c, "via": v, "count": k, "first": d["first"][(c, v)]}
                            for (c, v), k in d["callers"].most_common()],
                "paths": [p for _, p in sorted(by_fn_paths.get(n, []), reverse=True)[:5]]}
        if n in reviews:
            rec.update(reviews[n])
        records[n] = rec
    os.makedirs(out, exist_ok=True)
    json.dump(records, open(os.path.join(out, "records.json"), "w"), indent=1)
    json.dump({n: records[n] for n in reviews if n in records}, open(os.path.join(out, "records_reviewed.json"), "w"),
              indent=1, ensure_ascii=False)

    # ---- coverage.md ----
    reached = [n for n in internal if n in rt]
    named = [n for n in internal if not n.startswith("func_")]
    lines = ["# Battle engine: static call graph and runtime coverage", "",
             "Runs: %s." % ", ".join(os.path.basename(r.rstrip("/")) for r in runs), "",
             "| | functions | reached | % |", "|---|---|---|---|"]
    for label, group in (("all", internal), ("named", named), ("func_XXXXXXXX", [n for n in internal if n not in named]),
                         ("PORT: changed", [n for n in internal if n in changed])):
        group = list(group)
        r = sum(1 for n in group if n in rt)
        lines.append("| %s | %d | %d | %.1f |" % (label, len(group), r, 100.0 * r / max(1, len(group))))
    kinds = collections.Counter(e[2] for e in cg["edges"])
    lines += ["", "Static graph: %d edges (%s), %d pointer tables, %d roots (no caller in these files)." % (
        len(cg["edges"]), ", ".join("%s %d" % kv for kv in kinds.items()), len(cg["tables"]), len(roots)), ""]
    lines += ["## Pointer tables", "", "| table | functions | reached |", "|---|---|---|"]
    for t, fns in sorted(cg["tables"].items()):
        if t.startswith("llvm."):
            continue
        lines.append("| %s | %d | %d |" % (t, len(fns), sum(1 for x in fns if x in rt)))
    lines += ["", "## Per file", "", "| file | functions | reached | not reached (first 12) |", "|---|---|---|---|"]
    per_file = collections.defaultdict(list)
    for n in internal:
        per_file[F[n]["file"].split(":")[0]].append(n)
    for file, fns in sorted(per_file.items()):
        miss = sorted(n for n in fns if n not in rt)
        lines.append("| %s | %d | %d | %s |" % (os.path.basename(file), len(fns), len(fns) - len(miss),
                                                ", ".join(miss[:12]) + (" ..." if len(miss) > 12 else "")))
    multi = sorted((n for n in reached if len({c for c, _ in rt[n]["callers"]}) > 1),
                   key=lambda n: -len(rt[n]["callers"]))
    lines += ["", "## Functions reached from more than one caller (%d)" % len(multi), "",
              "| function | runtime callers (count) | static callers not seen |", "|---|---|---|"]
    for n in multi[:80]:
        seen = {c for c, _ in rt[n]["callers"]}
        lines.append("| %s | %s | %s |" % (n, ", ".join("%s%s (%d)" % (c, " via " + v if v else "", k)
                                                         for (c, v), k in rt[n]["callers"].most_common(6)),
                                           ", ".join(sorted(callers_of.get(n, set()) - seen))[:120]))
    lines += ["", "## Runtime edges the static graph does not have (%d)" % len(dyn_edges), "",
              "Calls through pointers stored at run time, or entries from main code (fragment entry points, process "
              "callbacks).", "", "| caller | via | callee | count |", "|---|---|---|---|"]
    for (c, v, fn), k in dyn_edges.most_common(60):
        lines.append("| %s | %s | %s | %d |" % (c, v, fn, k))
    open(os.path.join(out, "coverage.md"), "w").write("\n".join(lines) + "\n")

    # ---- records.md: reviewed records ----
    md = ["# Evidence records (reviewed)", ""]
    verdicts = collections.Counter(records[n]["verdict"] for n in reviews if n in records)
    md.append("Verdicts: " + ", ".join("%s %d" % kv for kv in verdicts.most_common()) + ".")
    md.append("")
    for n in reviews:
        if n not in records:
            md.append("## %s: not in the call graph" % n)
            continue
        r = records[n]
        md += ["## %s — %s%s" % (n, r["verdict"], " (proposed: `%s`)" % r["proposed_name"] if r.get("proposed_name")
                                   else ""), "",
               "- **File:** `%s`%s" % (r["file"], " (has `PORT:` changes)" if r["port_changed"] else ""),
               "- **Comment:** %s" % (r["comment"] or "(none)"),
               "- **Static callers:** %s" % (", ".join(sorted({"%s%s" % (c["caller"], " [%s %s]" % (
                   c["kind"], c["via"]) if c["kind"] != "call" else "") for c in r["static_callers"]})) or "(none)")]
        if r["runtime"]:
            md.append("- **Runtime callers:** %s" % ", ".join("%s%s ×%d" % (c["caller"], " via " + c["via"] if c["via"]
                                                                            else "", c["count"])
                                                            for c in r["runtime"]["callers"][:6]))
            if r["runtime"]["paths"]:
                md.append("- **Runtime path (most frequent):** %s" % " → ".join(r["runtime"]["paths"][0]))
        else:
            md.append("- **Runtime:** not reached")
        for key, label in (("scenario", "Scenario"), ("observed", "Observed"), ("ares", "ares"), ("notes", "Notes")):
            if r.get(key):
                v = r[key]
                md.append("- **%s:** %s" % (label, v if isinstance(v, str) else json.dumps(v)))
        md.append("")
    open(os.path.join(out, "records.md"), "w").write("\n".join(md) + "\n")
    print("%d functions, %d reached (%.1f%%), %d with several runtime callers, %d dynamic-only edges" % (
        len(internal), len(reached), 100.0 * len(reached) / len(internal), len(multi), len(dyn_edges)))


if __name__ == "__main__":
    main()

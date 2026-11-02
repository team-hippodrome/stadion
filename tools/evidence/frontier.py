#!/usr/bin/env python3 -I
"""What keeps code unreached: the frontier between the reached and the unreached part of the call graph, with the
branch condition that was never true at each frontier call, what controls it, and how much code it unlocks.

Usage:
  tools/evidence/frontier.py <callgraph.json> <binary> <out dir> <run dir>... [--build DIR] [--files REGEX]
--files: which functions count (regex on their source path; default "fragments/", the battle-engine call graph's
files; "src/" for the whole game with a whole-game call graph and build).

With --build (the instrumented build directory, for its compile_commands.json) it also proposes a memory setting
for each gate whose condition is a plain test of a global or of a field behind a global pointer
("gSessionContext.mode == 0x10", "gBattleUser->unk_15 & 8", "!(x & 2)"): the field's offset and size come from a
probe compiled with the caller's own flags that includes the caller's source (statics resolve too); the setting is
written at the caller's entry (PORT_EVID_SET through run.py --set). <out>/proposals.json: [{gate, set}].
tools/evidence/try_gates.py runs them and keeps the ones that reach the callee.

Needs the runs' coverage.profraw (build with PORT_EVID_COVERAGE, cmake/evidence.cmake) and llvm-profdata/llvm-cov
(xcrun). Writes <out>/frontier.md and <out>/frontier.json:
- a gate is a static edge caller -> callee where the caller ran and the callee never did; its condition is the
  nearest branch before the call site in the caller whose taken side never ran (llvm-cov branch counts), with the
  source text; the gate's "unlocks" are the unreached functions only reachable through it;
- each condition is classed by what decides it: a battle random value (force it: queue / case rule), the rules mode
  (D_8438AC60[0] is the constant 1: dead in the shipped game, reachable only by forcing memory), the battle state
  (moves, status, HP, flags: case mon lines or PORT_EVID_SET), session or save data (save editor, other routes),
  input, other globals;
- line and branch coverage of the instrumented files.
"""
import collections, json, os, re, subprocess, sys, shutil

CLASSES = [
    ("hardware (64DD, Controller Pak, Rumble Pak)", r"Leo[A-Z]|LeoDrive|osPfs|osMotor|Rumble|ControllerPak"),
    ("random", r"Random|D_843C4DA2"),
    ("rules mode (constant: dead code in the shipped game)", r"D_8438AC60\[0\]"),
    ("battle state (moves, status, HP, flags)", r"gBattleUser|gBattleTarget|sActivePokemon|usedMove|moveCache|"
                                               r"runtime|->unk_1[45]\b|state\.|BattleMon"),
    ("session / save / mode", r"gSessionContext|gSave|D_800[0-9A-F]{5}|gGameMode|D_8007|D_800A|D_800B"),
    ("input", r"[Cc]ont|[Bb]utton|[Pp]ad|Input|stick|BTN_"),
    ("AI decision", r"BattleAI|D_843C5564|D_843C60A[0-9A-F]"),
    ("battle scene / presentation state", r"sBattleScene|BattleScene|unk_00->|Camera|[Aa]nim"),
    ("menu or flow step (a switch on a selection or state)", r"^switch \("),
    ("result of a call (a confirmation, a load, a check)", r"^\(*!?\w+\(.*\)\s*(==|!=)?\s*\w*\)*$"),
]


def classify(text):
    for name, rx in CLASSES:
        if re.search(rx, text):
            return name
    return "other state"


RE_SIMPLE = re.compile(r"^\(*\s*(!?)\s*\(*\s*([A-Za-z_]\w*)((?:(?:->|\.)\w+|\[\d+\])+)?\s*"
                       r"(==|!=|&|>=|<=|>|<)?\s*(0x[0-9A-Fa-f]+|\d+)?\s*\)*\s*$")


def propose(gates, F, build, tmp):
    """memory settings that should open simple gates: [(gate, set spec)]"""
    cc = {e["file"]: e for e in json.load(open(os.path.join(build, "compile_commands.json")))}
    wanted = []
    for g in gates:
        c = g.get("condition") or {}
        m = RE_SIMPLE.match((c.get("text") or "").strip())
        if not m or g["kind"] != "call":
            continue
        neg, base, path, op, num = m.groups()
        path = path or ""
        if base[0].islower() and not base.startswith(("g", "s", "D_")):
            continue  # a local or an argument: not settable from outside
        num = int(num, 0) if num else None
        want_true = c.get("never") == "true"
        # the value that makes the whole condition take its never-taken side
        if op in (None, "&") and not neg:
            val = (num if op == "&" else 1) if want_true else 0
        elif op in (None, "&") and neg:
            val = 0 if want_true else (num if op == "&" else 1)
        elif op == "==":
            val = num if want_true else (num + 1)
        elif op == "!=":
            val = (num + 1) if want_true else num
        elif op in (">=", ">"):
            val = (num + (op == ">")) if want_true else max(0, num - 1)
        else:
            val = max(0, num - (op == "<")) if want_true else num + 1
        orin = op == "&" and val != 0  # set the tested bit, keep the others
        wanted.append((g, neg, base, path, val, orin))
    # one probe per proposal (a local or a bad expression fails alone): offsets and size with the caller's flags,
    # its source included so that its statics resolve
    out = []
    for n, (g, neg, base, path, val, orin) in enumerate(wanted):
        rel = F[g["caller"]]["file"]
        entry = next((e for f, e in cc.items() if f.endswith(rel)), None)
        if entry is None:
            continue
        lines = ['#include "%s"' % entry["file"]]
        acc, steps = base, []
        for k, p in enumerate(re.findall(r"->|\.\w+|\[\d+\]|\w+", path)):
            if p == "->":
                steps.append("*")
                acc = "(*(%s))" % acc
                continue
            if p.startswith("["):
                idx = int(p[1:-1])
                off = "%d * sizeof((%s)[0])" % (idx, acc)
                acc = "(%s)%s" % (acc, p)
            else:
                fld = p.lstrip(".")
                off = "__builtin_offsetof(__typeof__(%s), %s)" % (acc, fld)
                acc = "(%s).%s" % (acc, fld)
            lines.append("long probe_off_%d = %s;" % (k, off))
            steps.append(k)
        lines.append("long probe_size = sizeof(%s);" % acc)
        probe = os.path.join(tmp, "probe.c")
        open(probe, "w").write("\n".join(lines) + "\n")
        cmd = entry["command"].split(" -o ")[0].split() + ["-O1", "-S", "-emit-llvm", "-o", "-", probe]
        cmd = [a for a in cmd if not a.startswith(("-finstrument", "-fprofile-instr", "-fcoverage"))]
        r = subprocess.run(cmd, cwd=entry["directory"], capture_output=True, text=True)
        os.remove(probe)
        if r.returncode != 0:
            continue
        vals = dict(re.findall(r"^@(probe_\w+) = .*? i64 (-?\d+)", r.stdout, re.M))
        size = int(vals.get("probe_size", 0))
        if size not in (1, 2, 4):
            continue
        spec = base
        for st in steps:
            if st == "*":
                spec += "*"
            elif int(vals.get("probe_off_%d" % st, 0)):
                spec += "+0x%x" % int(vals["probe_off_%d" % st])
        out.append({"gate": "%s -> %s" % (g["caller"], g["callee"]), "callee": g["callee"],
                    "condition": g["condition"]["text"], "unlocks": g["unlocks"],
                    "set": "%s:%s:%d%s=%d" % (g["caller"], spec, size, "|" if orin else "",
                                              val & ((1 << (8 * size)) - 1))})
    return out


def tool(name):
    found = shutil.which(name)
    if found:
        return found
    return subprocess.run(["xcrun", "-f", name], capture_output=True, text=True, check=True).stdout.strip()


def main():
    args = sys.argv[1:]
    build = None
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    files_rx = "fragments/"
    if "--files" in args:
        i = args.index("--files")
        files_rx = args[i + 1]
        del args[i:i + 2]
    cg_path, binary, out, runs = args[0], args[1], args[2], args[3:]
    os.makedirs(out, exist_ok=True)
    raws = [os.path.join(r, f) for r in runs if os.path.isdir(r) for f in sorted(os.listdir(r))
            if f == "coverage.profraw" or f.startswith("coverage.profraw.k") or f.endswith(".profdata")]
    # (.k<i>: forked direct calls; *.profdata: a batch compacted with compact_runs.py)
    prof = os.path.join(out, "merged.profdata")
    subprocess.run([tool("llvm-profdata"), "merge", "-sparse", "-o", prof] + raws, check=True)
    exp = subprocess.run([tool("llvm-cov"), "export", "-format=text", "-skip-expansions", "-instr-profile", prof,
                          binary], capture_output=True, text=True, check=True).stdout
    os.remove(prof)
    data = json.loads(exp)["data"][0]
    cg = json.load(open(cg_path))
    F = cg["functions"]

    # per function: execution count, file, regions, branches (llvm-cov names static functions "file:name")
    cov = {}
    for fn in data["functions"]:
        name = fn["name"].split(":")[-1]
        if name in cov and cov[name]["count"] >= fn["count"]:
            continue  # same-named static copies (inline helpers in a header): the one that ran most counts
        cov[name] = {"count": fn["count"], "file": fn["filenames"][0], "regions": fn["regions"],
                     "branches": fn.get("branches", [])}
    internal = {n for n in F if not F[n]["external"] and re.search(files_rx, F[n]["file"])
                and not n.startswith(("Port_", "PortRegister"))}
    reached = {n for n in internal if cov.get(n, {}).get("count", 0) > 0}
    callees = collections.defaultdict(list)
    for a, b, kind, via in cg["edges"]:
        if a != b:
            callees[a].append((b, kind, via))

    lines_cache = {}

    def src(path):
        if path not in lines_cache:
            lines_cache[path] = open(path, errors="replace").read().split("\n")
        return lines_cache[path]

    def text(path, l0, c0, l1, c1):
        ls = src(path)
        if l0 == l1:
            return ls[l0 - 1][c0 - 1:c1 - 1]
        return " ".join([ls[l0 - 1][c0 - 1:]] + [x.strip() for x in ls[l0:l1 - 1]] + [ls[l1 - 1][:c1 - 1].strip()])

    def line_count(c, line):
        """the count of the innermost region of caller c that covers this line"""
        best = None
        for r in c["regions"]:
            l0, c0, l1, c1, n = r[:5]
            if r[7] != 0 or r[5] != 0 or not (l0 <= line <= l1):  # kind 0: code region, in the function's file
                continue
            size = (l1 - l0) * 1000 + (c1 - c0)
            if best is None or size < best[0]:
                best = (size, n)
        return best[1] if best else None

    def gate_condition(c, line):
        """the nearest branch at or before the line whose side was never taken"""
        cand = None
        for b in c["branches"]:
            l0, c0, l1, c1, t, f = b[:6]
            if l0 > line or (t and f) or b[6] != 0:
                continue
            if cand is None or (l0, c0) > (cand[0], cand[1]):
                cand = b
        if cand is None:
            return None
        l0, c0, l1, c1, t, f = cand[:6]
        cond = text(c["file"], l0, c0, l1, c1)
        if cond.strip().startswith(("case", "default")):  # a switch case: name the switch's expression
            body = src(c["file"])
            for ln in range(l0, 0, -1):
                m = re.search(r"\bswitch\s*\((.*)\)", body[ln - 1])
                if m:
                    cond = "switch (%s) %s" % (m.group(1), cond.strip())
                    break
        return {"line": l0, "text": cond, "true": t, "false": f, "never": "true" if t == 0 else "false"}

    # gates: reached caller -> unreached callee
    gates = []
    for c in sorted(reached):
        cc = cov[c]
        for callee, kind, via in callees.get(c, []):
            if callee not in internal or callee in reached:
                continue
            g = {"caller": c, "callee": callee, "kind": kind, "via": via, "call_line": None, "condition": None}
            if kind == "call":
                body = src(cc["file"])
                own = [r for r in cc["regions"] if r[5] == 0] or cc["regions"]  # (fileID 0: the function's file)
                start = min(r[0] for r in own)
                end = min(max(r[2] for r in own), len(body))
                for ln in range(start, end + 1):
                    if re.search(r"\b%s\s*\(" % re.escape(callee), body[ln - 1]):
                        n = line_count(cc, ln)
                        if n == 0 or g["call_line"] is None:
                            g["call_line"] = ln
                            g["condition"] = gate_condition(cc, ln)
                        if n == 0:
                            break
            elif kind == "table":
                g["condition"] = {"line": 0, "text": "index into %s" % via, "never": "-"}
            else:
                g["condition"] = {"line": 0, "text": "address stored for later (callback)", "never": "-"}
            cond = g["condition"]["text"] if g["condition"] else ""
            g["class"] = ("table index (which move / effect / animation)" if kind == "table" else
                          "callback registration" if kind == "ref" else classify(cond))
            gates.append(g)

    # unlocks: the unreached functions below each gate's callee (through unreached functions only), counted once
    # for the gate that reaches them first by gate order (most unlocked first)
    def below(fn):
        seen, stack = {fn}, [fn]
        while stack:
            x = stack.pop()
            for y, _, _ in callees.get(x, []):
                if y in internal and y not in reached and y not in seen:
                    seen.add(y)
                    stack.append(y)
        return seen
    for g in gates:
        g["unlocks"] = len(below(g["callee"]))
    unreached = internal - reached
    behind = set()
    for g in gates:
        behind |= below(g["callee"])
    no_gate = sorted(unreached - behind)

    # coverage totals (the instrumented files --files selects; llvm-cov paths are absolute, the call graph's relative
    # to game/, so the pattern is matched against the path from game/ on)
    tot = {"lines": [0, 0], "branches": [0, 0], "regions": [0, 0]}
    for f in data["files"]:
        if not re.search(files_rx, f["filename"].split("/game/", 1)[-1]):
            continue
        for k in tot:
            tot[k][0] += f["summary"][k]["covered"]
            tot[k][1] += f["summary"][k]["count"]

    by_class = collections.defaultdict(list)
    for g in gates:
        by_class[g["class"]].append(g)
    json.dump({"totals": tot, "reached": len(reached), "functions": len(internal), "gates": gates,
               "no_gate": no_gate, "unreached": sorted(unreached)},  # (dead_classes.py classes them)
              open(os.path.join(out, "frontier.json"), "w"), indent=1)

    md = ["# What keeps code unreached (%s)" % files_rx, "",
          "Runs: %s." % ", ".join(os.path.basename(r.rstrip("/")) for r in runs), "",
          "| | covered | total | % |", "|---|---|---|---|",
          "| functions | %d | %d | %.1f |" % (len(reached), len(internal), 100.0 * len(reached) / len(internal))]
    for k, (a, b) in tot.items():
        md.append("| %s | %d | %d | %.1f |" % (k, a, b, 100.0 * a / max(1, b)))
    # per area: the fragment, or the main-code file
    def area_of(n):
        f = F[n]["file"]
        m = re.search(r"fragments/([^/]+)/", f)
        return m.group(1) if m else os.path.basename(f)
    areas = collections.defaultdict(lambda: [0, 0, 0])
    for n in internal:
        a = areas[area_of(n)]
        a[0] += 1
        a[1] += n in reached
        a[2] += n.startswith("func_")
    md += ["", "## Per area", "", "| area | functions | reached | % | func_ |", "|---|---|---|---|---|"]
    for a, (tot_, r, fu) in sorted(areas.items(), key=lambda kv: (kv[1][1] / kv[1][0], -kv[1][0])):
        md.append("| %s | %d | %d | %.0f | %d |" % (a, tot_, r, 100.0 * r / tot_, fu))
    md += ["", "%d gates (a reached caller, an unreached callee); %d unreached functions are behind them, %d have no "
           "reached caller anywhere above them (entry points or callbacks that nothing in these runs registered)." % (
               len(gates), len(behind), len(no_gate)), "",
           "## Gates by what decides them", "", "| class | gates | unreached functions behind them |", "|---|---|---|"]
    for cls, gs in sorted(by_class.items(), key=lambda kv: -len(kv[1])):
        fns = set()
        for g in gs:
            fns |= below(g["callee"])
        md.append("| %s | %d | %d |" % (cls, len(gs), len(fns)))
    for cls, gs in sorted(by_class.items(), key=lambda kv: -len(kv[1])):
        md += ["", "### %s" % cls, "", "| unlocks | caller -> callee | condition never %s | line |" % "taken",
               "|---|---|---|---|"]
        for g in sorted(gs, key=lambda g: -g["unlocks"])[:25]:
            c = g["condition"] or {}
            md.append("| %d | %s -> %s | `%s` (%s side) | %s |" % (
                g["unlocks"], g["caller"], g["callee"], (c.get("text") or "?").replace("|", "\\|")[:110],
                c.get("never", "?"), c.get("line") or ""))
        if len(gs) > 25:
            md.append("| ... | %d more | | |" % (len(gs) - 25))
    md += ["", "## Unreached with no reached caller above (%d, first 60)" % len(no_gate), "",
           ", ".join(no_gate[:60])]
    open(os.path.join(out, "frontier.md"), "w").write("\n".join(md) + "\n")
    if build:
        props = propose(gates, F, build, out)
        props.sort(key=lambda p: -p["unlocks"])
        json.dump(props, open(os.path.join(out, "proposals.json"), "w"), indent=1)
        print("%d memory-setting proposals" % len(props))
    print("functions %d/%d; lines %d/%d; branches %d/%d; %d gates, %d functions behind them, %d without a gate" % (
        len(reached), len(internal), tot["lines"][0], tot["lines"][1], tot["branches"][0], tot["branches"][1],
        len(gates), len(behind), len(no_gate)))


if __name__ == "__main__":
    main()

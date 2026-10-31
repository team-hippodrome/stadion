#!/usr/bin/env python3 -I
"""Direct-call specs for functions nothing references (tools/evidence/deadcode.py), for run.py --call with
--call-fork (each call in a forked child: a crash or a hang costs only that call).

Usage:
  tools/evidence/dead_calls.py <callgraph.json> <names file> <out.txt> [--at "Fn#n"] [--int N]
The prototype comes from each function's definition (callgraph.json file:line). Arguments: integers (s8..u32, s64,
int, enums) get N (default 1), pointers p0 (a zeroed 256-byte scratch buffer), floats f1 (at most 6 integers/pointers
and 4 floats, in any order: arm64 registers per kind). Functions with struct arguments by value are skipped and listed.
Return: v (void), f (float/double), i (anything else). out.txt holds one spec per line ("<at>:<name>:<ret>:<args>")
and a "# skipped" section. The evidence from such a call is "dead in the shipped game; behaviour under a direct call
with placeholder arguments": records say so.
"""
import json, os, re, sys

INT = {"s8", "u8", "s16", "u16", "s32", "u32", "s64", "u64", "int", "unsigned", "long", "char", "short", "uintptr_t",
       "intptr_t", "size_t", "bool", "OSPri", "OSId", "OSMesg"}
FLOAT = {"f32", "f64", "float", "double"}


def prototype(src_lines, line, name):
    """the text from the definition line to its '{' (several lines possible)"""
    text = ""
    for k in range(line - 1, min(line + 8, len(src_lines))):
        text += " " + src_lines[k]
        if "{" in src_lines[k]:
            break
    m = re.search(r"([\w\s\*]+?)\b%s\s*\((.*?)\)\s*\{" % re.escape(name), text, re.S)
    return (m.group(1).strip(), m.group(2).strip()) if m else (None, None)


def kind(param):
    p = re.sub(r"\b(const|volatile|register|UNUSED|struct|union|enum)\b", " ", param).strip()
    if "*" in p or "[" in p:
        return "p"
    words = p.split()
    if not words:
        return None
    t = words[0]
    if t in FLOAT:
        return "f"
    if t in INT or t.startswith(("s", "u")) and t[1:].isdigit():
        return "i"
    if len(words) >= 2 and words[0] == "unsigned":
        return "i"
    return "struct:" + t


def main():
    a = sys.argv[1:]
    opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
    cg, names_path, out = a[0], a[1], a[2]
    at = opt("--at", "Game_DoCopyProtection#2")
    ival = opt("--int", "1")
    F = json.load(open(cg))["functions"]
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    cache = {}
    specs, skipped = [], []
    for name in [l.strip() for l in open(names_path) if l.strip() and not l.startswith("#")]:
        f = F.get(name)
        if not f or not f.get("file") or not f.get("line"):
            skipped.append((name, "no definition"))
            continue
        path = os.path.join(root, "game", f["file"]) if not f["file"].startswith("/") else f["file"]
        if path not in cache:
            try:
                cache[path] = open(path, errors="replace").read().split("\n")
            except OSError:
                cache[path] = []
        ret, params = prototype(cache[path], f["line"], name)
        if ret is None:
            skipped.append((name, "prototype not found"))
            continue
        plist = [] if params in ("", "void") else [x.strip() for x in params.split(",")]
        kinds = [kind(x) for x in plist]
        bad = [k for k in kinds if k is None or k.startswith("struct:")]
        if bad or len(plist) > 6:
            skipped.append((name, "argument " + (bad[0] if bad else "count")))
            continue
        if kinds.count("f") > 4 or len(kinds) - kinds.count("f") > 6:
            skipped.append((name, "more than 4 floats"))
            continue
        r = ret.split()[-1].replace("*", "") if ret else "void"
        rk = "v" if r == "void" and "*" not in ret else ("f" if r in FLOAT else "i")
        args = ",".join({"i": "i" + ival, "p": "p0", "f": "f1"}[k] for k in kinds)
        specs.append("%s:%s:%s:%s" % (at, name, rk, args))
    with open(out, "w") as fo:
        fo.write("\n".join(specs) + "\n# skipped\n")
        for n, why in skipped:
            fo.write("# %s: %s\n" % (n, why))
    print("%d call specs, %d skipped" % (len(specs), len(skipped)))


if __name__ == "__main__":
    main()

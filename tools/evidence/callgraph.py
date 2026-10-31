#!/usr/bin/env python3 -I
"""Static call graph of game sources from clang's LLVM IR, with dispatch through function-pointer tables.

Usage:
  tools/evidence/callgraph.py <build dir> <out.json> [--files REGEX] [--jobs N]

Each source matching REGEX (default the battle engine) is compiled with its own compile_commands.json flags to -O0
LLVM IR. From the IR:
- "call" edges: direct calls (including calls to code outside the selected files, kept as external nodes);
- "table" edges: a function that reads a global whose initializer holds function addresses (a pointer table, or an
  array of structs with function pointers, like the move-effect table) and makes an indirect call gets an edge to
  every function in that table;
- "ref" edges: a function takes another's address without calling it (a callback handed to a scheduler, a stored
  state handler); the real caller is then whatever runs the pointer, which the runtime trace shows;
- "unresolved": functions with indirect calls that no table explains.
Function file:line comes from the debug info; the comment block above each definition from the source.
Output: {"functions": {name: {file, line, comment, static, external, indirect_calls, tables}},
         "edges": [[caller, callee, kind, via]], "tables": {global: [functions]}}
"""
import json, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

RE_DEFINE = re.compile(r'^define\s+(.*?)@("?[\w.$]+"?)\(')
RE_DECLARE = re.compile(r'^declare\s+.*?@("?[\w.$]+"?)\(')
RE_GLOBAL = re.compile(r'^@("?[\w.$]+"?)\s*=\s*(.*)$')
RE_SYM = re.compile(r'@("?[\w.$]+"?)')
RE_CALL = re.compile(r'\b(?:call|invoke)\b[^@%]*?(?:\([^)]*\)\s*)?(@"?[\w.$]+"?|%[\w.]+)\(')
RE_SP = re.compile(r'!DISubprogram\(name: "([^"]+)".*?file: (![0-9]+), line: ([0-9]+)')
RE_FILE = re.compile(r'^(![0-9]+) = !DIFile\(filename: "([^"]+)", directory: "([^"]*)"')


def unq(s):
    return s.strip('"')


def ir_for(entry, tmpdir):
    cmd = entry["command"].split()
    out = os.path.join(tmpdir, os.path.basename(entry["file"]) + ".ll")
    args = []
    skip = False
    for i, a in enumerate(cmd):
        if skip:
            skip = False
            continue
        if a in ("-o", "-c"):
            skip = a == "-o"
            continue
        if a.startswith(("-O", "-finstrument", "-fprofile-instr", "-fcoverage")) or a == "-fno-optimize-sibling-calls":
            continue
        args.append(a)
    args = args[:-1] + ["-O0", "-Xclang", "-disable-O0-optnone", "-S", "-emit-llvm", "-o", out, args[-1]]
    r = subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write("IR failed for %s:\n%s\n" % (entry["file"], r.stderr[-2000:]))
        return None
    return out


def comment_above(lines, line):
    """the comment block that ends right above the definition line (1-based), if any"""
    i = min(line - 2, len(lines) - 1)
    while i >= 0 and lines[i].strip() == "":
        i -= 1
    if i < 0:
        return ""
    out = []
    s = lines[i].rstrip()
    if s.strip().endswith("*/"):
        while i >= 0:
            out.insert(0, lines[i].rstrip())
            if "/*" in lines[i]:
                break
            i -= 1
    else:
        while i >= 0 and lines[i].strip().startswith("//"):
            out.insert(0, lines[i].rstrip())
            i -= 1
    text = " ".join(re.sub(r"^\s*(//+|/\*+|\*+/?)\s?", "", l).replace("*/", "").strip() for l in out).strip()
    return text


_LINES = {}


def src_lines(path):
    if path not in _LINES:
        try:
            _LINES[path] = open(path, errors="replace").read().split("\n")
        except OSError:
            _LINES[path] = []
    return _LINES[path]


def parse_ir(path):
    funcs, decls, globals_, files, sps = {}, set(), {}, {}, {}
    cur = None
    for line in open(path, errors="replace"):
        if cur is not None:
            if line.startswith("}"):
                cur = None
                continue
            body = funcs[cur]
            for m in RE_CALL.finditer(line):
                t = m.group(1)
                if t.startswith("%"):
                    body["indirect"] += 1
                else:
                    body["calls"].add(unq(t[1:]))
            stripped = RE_CALL.sub("", line)
            for m in RE_SYM.finditer(stripped):
                body["refs"].add(unq(m.group(1)))
            continue
        m = RE_DEFINE.match(line)
        if m:
            cur = unq(m.group(2))
            dbg = re.search(r"!dbg (![0-9]+)", line)
            funcs[cur] = {"calls": set(), "refs": set(), "indirect": 0, "static": "internal" in m.group(1),
                          "dbg": dbg.group(1) if dbg else None}
            continue
        m = RE_DECLARE.match(line)
        if m:
            decls.add(unq(m.group(1)))
            continue
        m = RE_GLOBAL.match(line)
        if m:
            globals_[unq(m.group(1))] = set(unq(x) for x in RE_SYM.findall(m.group(2)))
            continue
        m = RE_FILE.match(line)
        if m:
            files[m.group(1)] = os.path.join(m.group(3), m.group(2))
            continue
        if "!DISubprogram(" in line and "DISPFlagDefinition" in line:
            num = line.split("=", 1)[0].strip()
            m = RE_SP.search(line)
            if m:
                sps[num] = (m.group(1), m.group(2), int(m.group(3)))
    return funcs, decls, globals_, files, sps


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    build, out = sys.argv[1], sys.argv[2]
    opts = sys.argv[3:]
    regex = opts[opts.index("--files") + 1] if "--files" in opts else r"fragments/battle_engine/"
    jobs = int(opts[opts.index("--jobs") + 1]) if "--jobs" in opts else 4
    cc = json.load(open(os.path.join(build, "compile_commands.json")))
    entries = [e for e in cc if re.search(regex, e["file"]) and e["file"].endswith(".c")]
    tmp = tempfile.mkdtemp(prefix="callgraph-", dir=os.environ.get("TMPDIR"))
    os.nice(15)
    with ThreadPoolExecutor(jobs) as ex:
        irs = list(ex.map(lambda e: (e, ir_for(e, tmp)), entries))

    functions, edges, tables = {}, [], {}
    parsed = []
    for e, ir in irs:
        if ir is None:
            continue
        p = parse_ir(ir)
        parsed.append((e, p))
        os.remove(ir)
    os.rmdir(tmp)
    # pass 1: definitions and pointer tables (globals whose initializer reaches function addresses)
    defined = {}
    for e, (funcs, decls, globals_, files, sps) in parsed:
        allfuncs = set(funcs) | decls
        for name, f in funcs.items():
            sp = sps.get(f["dbg"]) if f["dbg"] else None
            file, line = (files.get(sp[1], e["file"]), sp[2]) if sp else (e["file"], 0)
            rel = os.path.relpath(file, os.path.join(os.path.dirname(build.rstrip("/")) or ".", "game")) \
                if "/game/" in file else file
            rel = file.split("/game/", 1)[1] if "/game/" in file else rel
            functions[name] = {"file": rel, "line": line, "comment": comment_above(src_lines(file), line) if line else "",
                               "static": f["static"], "external": False, "indirect_calls": f["indirect"],
                               "tables": []}
            defined[name] = f
        # resolve tables through nested globals (struct arrays point at other globals rarely; one level is enough)
        for g, refs in globals_.items():
            if g.startswith(("llvm.", "__llvm", "__prof")):
                continue
            fns = set(r for r in refs if r in allfuncs)
            for r in refs:
                if r in globals_ and r not in allfuncs:
                    fns |= set(x for x in globals_[r] if x in allfuncs)
            if fns:
                tables.setdefault(g, set()).update(fns)
    # pass 2: edges
    for name, f in defined.items():
        for c in sorted(f["calls"]):
            edges.append([name, c, "call", ""])
        used_tables = [r for r in f["refs"] if r in tables]
        for r in sorted(f["refs"]):
            if r in tables or r == name:
                continue
            if r in defined or r in functions:
                edges.append([name, r, "ref", ""])
        functions[name]["tables"] = sorted(used_tables)
        if f["indirect"]:
            for t in sorted(used_tables):
                for c in sorted(tables[t]):
                    edges.append([name, c, "table", t])
    for _, c, _, _ in edges:
        if c not in functions:
            functions[c] = {"file": "", "line": 0, "comment": "", "static": False, "external": True,
                            "indirect_calls": 0, "tables": []}
    # functions whose address is taken but that are only ever reached through a pointer at runtime
    json.dump({"files": regex, "functions": functions, "edges": edges,
               "tables": {k: sorted(v) for k, v in tables.items()}}, open(out, "w"), indent=0)
    internal = [n for n, f in functions.items() if not f["external"]]
    kinds = {}
    for e in edges:
        kinds[e[2]] = kinds.get(e[2], 0) + 1
    unresolved = [n for n in internal if functions[n]["indirect_calls"] and not functions[n]["tables"]]
    print("%d functions in %d files, %d external callees, edges %s, %d pointer tables, %d functions with "
          "unresolved indirect calls" % (len(internal), len(parsed), len(functions) - len(internal), kinds,
                                         len(tables), len(unresolved)))


if __name__ == "__main__":
    main()

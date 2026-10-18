#!/usr/bin/env python3 -I
"""For each 'call to undeclared function' error, find the header in game/ that declares the function
and insert an #include for it into the offending source file."""
import re, subprocess, sys, pathlib, collections
root = pathlib.Path(__file__).resolve().parent.parent
unique = (root / "build/syntax/unique.txt").read_text().splitlines()
need = collections.defaultdict(set)
for line in unique:
    m = re.match(r"(game/[^:]+\.c):\d+:\d+: error: call to undeclared function '([^']+)'", line)
    if m:
        need[m.group(1)].add(m.group(2))
headers = [p for p in (root / "game").rglob("*.h") if "lib/ultralib" not in str(p) and "/port/" not in str(p)]
text = {p: p.read_text(errors="replace") for p in headers}
def declaring_header(fn):
    pat = re.compile(r"^[A-Za-z_][\w\s\*]*[\s\*]" + re.escape(fn) + r"\s*\([^;{]*\)\s*;", re.M)
    for p, t in text.items():
        if pat.search(t):
            return p
    return None
unresolved = collections.defaultdict(list)
for src, fns in need.items():
    path = root / src
    s = path.read_text(errors="replace")
    add = []
    for fn in sorted(fns):
        h = declaring_header(fn)
        if h is None:
            unresolved[fn].append(src); continue
        rel = h.relative_to(root / "game").as_posix()
        inc = f'#include "{rel}"'
        if inc not in s and inc not in add and not (h.name == path.with_suffix(".h").name and False):
            add.append(inc)
    if add:
        lines = s.split("\n")
        idx = max([i for i, l in enumerate(lines) if l.startswith("#include")] or [-1])
        lines[idx + 1:idx + 1] = add
        path.write_text("\n".join(lines))
        print(src, "+", len(add))
for fn, srcs in unresolved.items():
    print("UNRESOLVED", fn, srcs[:3])

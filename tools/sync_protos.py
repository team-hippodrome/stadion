#!/usr/bin/env python3 -I
"""For each 'conflicting types for F' error, copy the parameter list and return type of the definition (at the error
location) into every other single-statement declaration of F under game/."""
import re, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
errs = set()
for l in (root / "build/syntax").glob("*.log"):
    for line in l.read_text(errors="replace").splitlines():
        m = re.match(r"(game/[^:]+):(\d+):\d+: error: conflicting types for '(\w+)'", line)
        if m: errs.add((m.group(1), int(m.group(2)), m.group(3)))
n = 0
srcs = [p for p in (root / "game").rglob("*") if p.suffix in (".c", ".h") and "lib/ultralib/include" not in str(p)]
for f, ln, name in sorted(errs):
    L = (root / f).read_text(errors="replace").split("\n")
    text = " ".join(x.strip() for x in L[ln - 1:ln + 6])
    m = re.match(r"(.*?)\b" + re.escape(name) + r"\s*\(([^)]*)\)\s*\{", text)
    if not m: print("no def parse", f, ln, name); continue
    ret, params = m.group(1).strip(), m.group(2).strip()
    pat = re.compile(r"^([A-Za-z_][\w\s\*]*?)\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*;", re.M)
    for p in srcs:
        s = p.read_text(errors="replace")
        t = pat.sub(lambda mm: f"{ret} {name}({params});", s)
        if t != s:
            p.write_text(t); n += 1
print("prototypes updated in", n, "files for", len(errs), "functions")

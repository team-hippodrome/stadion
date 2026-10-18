#!/usr/bin/env python3 -I
"""Pointer cleanup, rule E: widen the return type of functions whose result is used as a pointer (or that return
one) while declared with a 32-bit integer type, in the definition and every prototype under game/."""
import re, pathlib, collections
root = pathlib.Path(__file__).resolve().parent.parent
diag = re.compile(r"^(game/[^:]+):(\d+):(\d+): (?:error|warning): incompatible (pointer to integer|integer to pointer) conversion (initializing|assigning to|passing|returning) (.*)$")
INTS = ("s32", "u32", "int", "unsigned int", "s16", "u16")
WIDE = {"s32": "intptr_t", "int": "intptr_t", "s16": "intptr_t", "u32": "uintptr_t", "unsigned int": "uintptr_t", "u16": "uintptr_t"}
names = set()
cache = {}
def lines(f):
    if f not in cache: cache[f] = (root / f).read_text(errors="replace").split("\n")
    return cache[f]
for l in (root / "build/ptr/intconv.txt").read_text().splitlines():
    m = diag.match(l)
    if not m: continue
    f, ln, col, kind, ctx, rest = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5), m.group(6)
    L = lines(f); s = L[ln - 1]
    if ctx == "returning" and kind == "pointer to integer":
        # enclosing definition
        i = ln - 1
        while i >= 0:
            dm = re.match(r"^(" + "|".join(INTS) + r")\s+(\w+)\s*\(", L[i])
            if dm: names.add(dm.group(2)); break
            i -= 1
    elif kind == "integer to pointer" and re.search(r"'(" + "|".join(INTS) + r")'", rest):
        cm = re.match(r"([A-Za-z_]\w*)\s*\(", s[col - 1:])
        if cm: names.add(cm.group(1))
# keep only functions that are defined (not macros) under game/src
srcs = [p for p in (p for d in ("src", "include", "lib") for p in (root / "game" / d).rglob("*")) if p.suffix in (".c", ".h") and "lib/ultralib/include" not in str(p)]
SKIP = {"Audio_WriteU32BE", "osRecvMesg", "osSendMesg"}
n = 0
for name in sorted(names - SKIP):
    pat = re.compile(r"^(" + "|".join(INTS) + r")(\s+" + re.escape(name) + r"\s*\()", re.M)
    for p in srcs:
        s = p.read_text(errors="replace")
        t = pat.sub(lambda m: WIDE[m.group(1)] + m.group(2), s)
        if t != s: p.write_text(t); n += 1
print("functions:", len(names), "files touched:", n)
print(sorted(names)[:60])

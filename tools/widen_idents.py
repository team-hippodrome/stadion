#!/usr/bin/env python3 -I
"""Pointer cleanup, rule B-D: widen local variables, parameters and file-scope variables (32-bit ints) that the
compiler reports as holding pointers (build/ptr/intconv.txt from tools/intconv.sh). Struct fields and function
return types are reported, not changed (return types are widened when the function is defined in the same file)."""
import re, pathlib, collections, sys
root = pathlib.Path(__file__).resolve().parent.parent
INT = r"(?:s8|u8|s16|u16|s32|u32|int|unsigned int|unsigned|long)"
WIDE = {"s32": "intptr_t", "int": "intptr_t", "s16": "intptr_t", "s8": "intptr_t", "long": "intptr_t",
        "u32": "uintptr_t", "unsigned int": "uintptr_t", "unsigned": "uintptr_t", "u16": "uintptr_t", "u8": "uintptr_t"}
diag = re.compile(r"^(game/[^:]+):(\d+):(\d+): (?:error|warning): incompatible (pointer to integer|integer to pointer) conversion "
                  r"(initializing|assigning to|passing|returning) (.*)$")
items = []
for l in (root / "build/ptr/intconv.txt").read_text().splitlines():
    m = diag.match(l)
    if m: items.append((m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5), m.group(6)))
files = {}
def lines_of(f):
    if f not in files: files[f] = (root / f).read_text(errors="replace").split("\n")
    return files[f]
def find_decl(L, ln, name):
    """Return (line_index, type) of the declaration of 'name' visible at 1-based line ln."""
    pat = re.compile(r"\b(" + INT + r")\s+((?:[^;(){}=]*?[\s,])?)(\*?)" + re.escape(name) + r"\b")
    i = ln - 1
    sigdone = False
    while i >= 0 and i > ln - 400:
        s = L[i]
        m = pat.search(s)
        if m and not m.group(3) and "(" not in s[:m.start()] .rstrip("(") or (m and not m.group(3) and re.search(r"\w\s*\([^)]*$", s[:m.start()])):
            return i, m.group(1), m.start(1)
        if re.match(r"^[A-Za-z_][\w\s\*]*\([^;]*$", s) or re.match(r"^[A-Za-z_][\w\s\*]*\(.*\)\s*\{\s*$", s):
            # function header reached; parameters may continue on following lines handled by the regexp above
            if sigdone: break
            sigdone = True
        i -= 1
    # file scope
    for j, s in enumerate(L):
        m = pat.match(s) if s and not s.startswith(" ") else None
        if m and not m.group(3) and "(" not in s: return j, m.group(1), m.start(1)
    return None
widened, skipped = 0, []
for f, ln, col, kind, ctx, rest in sorted(set(items)):
    L = lines_of(f)
    s = L[ln - 1]
    ints32 = re.search(r"(?:to|from|passing|returning)? ?'(s32|u32|int|unsigned int|s16|u16)'", rest)
    name = None
    if ctx == "initializing" and kind == "pointer to integer":
        name = re.match(r"\w+", s[col - 1:]).group(0) if re.match(r"\w+", s[col - 1:]) else None
    elif ctx == "assigning to" and kind == "pointer to integer":
        eq = s.rfind("=", 0, col - 1)
        lhs = s[:eq].strip() if eq > 0 else ""
        if re.fullmatch(r"[A-Za-z_]\w*", lhs.split()[-1] if lhs else ""):
            name = lhs.split()[-1]
    elif kind == "integer to pointer" and ints32:
        m = re.match(r"[A-Za-z_]\w*", s[col - 1:])
        if m and not re.match(r"[A-Za-z_]\w*\s*[\(\.\-\[]", s[col - 1:]): name = m.group(0)
    if not name or name in ("NULL",):
        skipped.append((f, ln, "complex", rest[:70])); continue
    d = find_decl(L, ln, name)
    if not d:
        skipped.append((f, ln, "no local/global decl for " + name, rest[:60])); continue
    di, typ, tcol = d
    line = L[di]
    L[di] = line[:tcol] + WIDE[typ] + line[tcol + len(typ):]
    widened += 1
for f, L in files.items():
    (root / f).write_text("\n".join(L))
print("widened", widened, "skipped", len(skipped))
c = collections.Counter(x[2][:30] for x in skipped)
for k, v in c.most_common(8): print(" ", v, k)
(root / "build/ptr/widen_skipped.txt").write_text("\n".join(f"{a}:{b}: {c}: {d}" for a, b, c, d in skipped))

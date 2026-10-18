#!/usr/bin/env python3 -I
"""Rule A for pointer cleanup: when clang reports "passing <pointer> to parameter of type u32/s32", widen that
parameter in every declaration/definition of the callee (u32 -> uintptr_t, s32 -> intptr_t).
Uses clang's follow-up note ("passing argument to parameter 'x' here") to locate the callee's declaration."""
import re, subprocess, pathlib, sys, concurrent.futures as cf, collections
root = pathlib.Path(__file__).resolve().parent.parent
flags = [l.strip() for l in (root / "tools/compile_flags.txt").read_text().split("\n") if l.strip()]
flags = [f for f in flags if f not in ("-w", "-Wno-int-conversion")]
files = sorted(str(p.relative_to(root)) for p in (root / "game/src").rglob("*.c"))
warn_re = re.compile(r"^(\S+?):(\d+):(\d+): (?:warning|error): incompatible pointer to integer conversion passing '[^']*' (?:\(aka '[^']*'\) )?to parameter of type '(u32|s32|u16|s16|int|unsigned int)'")
note_re = re.compile(r"^(\S+?):(\d+):(\d+): note: passing argument to parameter '(\w+)' here")
def run(f):
    out = subprocess.run(["clang", "-fsyntax-only", *flags, "-Wint-conversion", f], capture_output=True, text=True, cwd=root).stderr.split("\n")
    res = []
    for i, l in enumerate(out):
        m = warn_re.match(l)
        if m:
            for j in range(i + 1, min(i + 8, len(out))):
                n = note_re.match(out[j])
                if n:
                    res.append((n.group(1), int(n.group(2)), int(n.group(3)), n.group(4), m.group(4)))
                    break
                if warn_re.match(out[j]):
                    break
    return res
sites = set()
with cf.ThreadPoolExecutor(8) as ex:
    for r in ex.map(run, files):
        sites.update(r)
print("param sites:", len(sites))
# Group by declaration line; find function name on that line (or the previous lines for wrapped params).
MAP = {"u32": "uintptr_t", "s32": "intptr_t", "int": "intptr_t", "unsigned int": "uintptr_t", "u16": "uintptr_t", "s16": "intptr_t"}
by_file = collections.defaultdict(set)
for f, ln, col, pname, ty in sites:
    by_file[f].add((ln, col, pname, ty))
funcs = {}
for f, items in by_file.items():
    lines = (root / f).read_text(errors="replace").split("\n")
    for ln, col, pname, ty in items:
        # text of this declaration up to the parameter: walk back to the unmatched "(" and take the identifier before it
        text = " ".join(lines[max(0, ln - 6):ln - 1]) + " " + lines[ln - 1][:col]
        depth = 0
        k = len(text) - 1
        fname = None
        while k >= 0:
            c = text[k]
            if c == ")":
                depth += 1
            elif c == "(":
                if depth == 0:
                    m = re.search(r"(\w+)\s*$", text[:k])
                    fname = m.group(1) if m else None
                    break
                depth -= 1
            k -= 1
        if not fname:
            continue
        funcs.setdefault(fname, set()).add((pname, ty))
# Audio_WriteU32BE writes a 32-bit field of the audio command queue; its callers need offsets, not wider values.
for skip in ("Audio_WriteU32BE",):
    funcs.pop(skip, None)
print("functions:", len(funcs))
changed = 0
allsrc = [p for p in (p for d in ("src", "include", "lib") for p in (root / "game" / d).rglob("*")) if p.suffix in (".c", ".h") and "lib/ultralib/include" not in str(p.relative_to(root))]
for p in allsrc:
    s = p.read_text(errors="replace")
    o = s
    for fname, params in funcs.items():
        for pname, ty in params:
            # <type> <pname> inside a declaration/definition of fname
            pat = re.compile(r"(\b" + re.escape(fname) + r"\s*\([^;{)]*?)\b(u32|s32|int|unsigned int|u16|s16)(\s+" + re.escape(pname) + r"\b)")
            s = pat.sub(lambda m: m.group(1) + MAP[m.group(2)] + m.group(3), s)
    if s != o:
        p.write_text(s)
        changed += 1
print("files changed:", changed)

#!/usr/bin/env python3 -I
"""Rewrites explicit pointer->32-bit-integer casts reported by tools/ptr_warnings.sh to pointer-width casts
at the exact reported column: (u32)/(s32)/(int)/(unsigned int) -> (uintptr_t)/(intptr_t). Sites inside macro
expansions (column does not land on a cast) are left and printed."""
import re, collections, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
MAP = {"u32": "uintptr_t", "unsigned int": "uintptr_t", "unsigned": "uintptr_t", "s32": "intptr_t", "int": "intptr_t"}
warn = collections.defaultdict(list)
for l in (root / "build/ptr/warnings.txt").read_text().splitlines():
    m = re.match(r"([^:]+):(\d+):(\d+): warning: cast to smaller integer type", l)
    if m:
        warn[m.group(1)].append((int(m.group(2)), int(m.group(3))))
fixed = skipped = 0
left = []
for f, sites in warn.items():
    p = root / f
    lines = p.read_text(errors="replace").split("\n")
    # process right-to-left per line so earlier columns stay valid
    for ln, col in sorted(set(sites), key=lambda t: (t[0], -t[1])):
        s = lines[ln - 1]
        m = re.match(r"\(\s*(u32|s32|int|unsigned int|unsigned)\s*\)", s[col - 1:])
        if not m:
            left.append((f, ln)); skipped += 1; continue
        lines[ln - 1] = s[:col - 1] + "(" + MAP[m.group(1)] + ")" + s[col - 1 + m.end():]
        fixed += 1
    p.write_text("\n".join(lines))
print("fixed", fixed, "left", skipped)
for f, ln in left[:60]:
    print(" ", f, ln)

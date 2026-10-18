#!/usr/bin/env python3 -I
"""Widen u32/s32 data tables whose initializers contain addresses (not compile-time constants when the
address is 64-bit). Rewrites the element type to uintptr_t and tags the declaration with 'PORT64'."""
import re, pathlib, collections
root = pathlib.Path(__file__).resolve().parent.parent
errs = collections.defaultdict(set)
for l in (root/"build/syntax/unique.txt").read_text().splitlines():
    m = re.match(r"(game/[^:]+):(\d+):\d+: error: initializer element is not a compile-time constant", l)
    if m: errs[m.group(1)].add(int(m.group(2)))
decl = re.compile(r"^(\s*)((?:static\s+)?(?:const\s+)?)(u32|s32)(\s+\w+\s*\[[^\]]*\]\s*=\s*\{)")
manual = []
for f, lines_ in sorted(errs.items()):
    p = root/f
    L = p.read_text(errors="replace").split("\n")
    done = set()
    for ln in sorted(lines_):
        i = ln - 1
        while i >= 0 and not decl.match(L[i]):
            if re.match(r"^\S.*[;]\s*$", L[i]) and i < ln - 1 and "{" not in L[i]:
                i = -1; break
            i -= 1
        if i >= 0 and i not in done:
            m = decl.match(L[i])
            L[i] = decl.sub(r"\1\2uintptr_t\4 // PORT64: holds addresses", L[i], count=1)
            done.add(i)
        elif i < 0:
            manual.append((f, ln))
    p.write_text("\n".join(L))
print("manual:", len(manual))
for m in manual: print(*m)

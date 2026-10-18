#!/usr/bin/env python3 -I
"""Restores the N64 memory layout of runs of adjacent static vertex arrays.

The decomp splits vertex data into one array per symbol (static Vtx D_84B103B0[] = {...}; static Vtx D_84B10410[] = ...).
On the N64 they were contiguous, and the game relies on it: geo layouts give a vertex count spanning several arrays,
display lists load 32 vertices from a 31-entry array, code recolours "the next n vertices". On the host each array is
placed independently, so those accesses read or overwrite whatever follows (often a display list).

For every run of arrays whose names (= N64 addresses) are exactly back to back (address + 16 * entries == next address),
this keeps one array under the first name holding all entries and turns the others into macros pointing into it:
    #define D_84B10410 (D_84B103B0 + 6)
Usage: merge_vtx_arrays.py file.c [...]   (idempotent; prints what it merged)
"""
import re, sys

ARRAY = re.compile(r'^static Vtx (D_([0-9A-Fa-f]{8}))\[\] = \{\n(.*?)^\};\n', re.S | re.M)

def entries(body):
    return len(re.findall(r'\bVTX\(', body))

def merge(path):
    src = open(path).read()
    arrays = [m for m in ARRAY.finditer(src)]
    runs, cur = [], []
    for m in arrays:
        n = entries(m.group(3))
        if n == 0:
            if len(cur) > 1:
                runs.append(cur)
            cur = []
            continue
        addr = int(m.group(2), 16)
        if cur:
            pm, paddr, pn = cur[-1]
            # back to back in N64 memory, and nothing but whitespace between them in the source
            if paddr + 16 * pn == addr and src[pm.end():m.start()].strip() == "":
                cur.append((m, addr, n))
                continue
            if len(cur) > 1:
                runs.append(cur)
        cur = [(m, addr, n)]
    if len(cur) > 1:
        runs.append(cur)
    if not runs:
        return
    out, pos = [], 0
    for run in runs:
        first, faddr, _ = run[0]
        out.append(src[pos:first.start()])
        body = "".join(m.group(3) for m, _, _ in run)
        out.append("// PORT: one array (the N64 layout): the game indexes across these symbols\n")
        out.append("static Vtx %s[] = {\n%s};\n" % (first.group(1), body))
        for m, addr, _ in run[1:]:
            out.append("#define %s (%s + %d)\n" % (m.group(1), first.group(1), (addr - faddr) // 16))
        pos = run[-1][0].end()
        print("%s: merged %s (%d vertices)" % (path, ", ".join(m.group(1) for m, _, _ in run), sum(n for _, _, n in run)))
    out.append(src[pos:])
    text = "".join(out)
    # forward declarations of merged names would now expand to an invalid declaration
    for run in runs:
        for m, _, _ in run[1:]:
            text = re.sub(r'^(?:static |extern )?Vtx %s\[\];\n' % m.group(1), '', text, flags=re.M)
    open(path, "w").write(text)

for p in sys.argv[1:]:
    merge(p)

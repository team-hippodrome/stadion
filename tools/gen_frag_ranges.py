#!/usr/bin/env python3 -I
"""Web build: each fragment's static data, from the linker map, for Port_FragmentResetData (tools/gen_frag_sections.py).

The other builds find a fragment's writable data through its own sections (__DATA,__fdNN on macOS, fdNN with
__start_/__stop_ symbols on ELF and COFF). wasm-ld defines no such symbols: a fragment's data could not be reset, so it
kept what the previous visit left (the area-select screen stayed black, its cursor stuck, on the second visit). It
does keep each object's #pragma clang section data together, as one chunk of the map with an empty name; this script
writes those chunks as "<fragment index> <address> <size>" lines (hexadecimal), packed into pokestadium.o2r as
port/frag_ranges.txt and read at startup (Engine.cpp).

Usage: gen_frag_ranges.py <pokestadium.map> <game/src> <frag_sections.txt> <out.txt>"""
import pathlib
import re
import sys

mapfile, src, index_file, out = sys.argv[1:5]
index = {}  # fragment directory -> index (frag_sections.txt: "<index> <dir>")
for line in open(index_file):
    k, d = line.split()
    index[d] = int(k)
owner = {}  # source file name -> fragment index (the word-array wrappers keep the original file names)
for d, k in index.items():
    for f in (pathlib.Path(src) / "fragments" / d).glob("*.c"):
        owner[f.name + ".o"] = k
lines = []
for addr, size, obj in re.findall(r"^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+libstadium_game\.a\(([^)]+)\):\(\)$",
                                  open(mapfile).read(), re.M):
    if obj in owner and int(size, 16) > 0:
        lines.append("%d %s %s\n" % (owner[obj], addr, size))
if not lines:
    sys.exit("gen_frag_ranges.py: no fragment data chunks in " + mapfile)
open(out, "w").write("".join(lines))
print("fragment data ranges: %d" % len(lines))

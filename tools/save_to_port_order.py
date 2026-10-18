#!/usr/bin/env python3 -I
"""OBSOLETE: the port now reads and writes N64-order saves directly (PORTING_NOTES.md, "Save byte order").
Converts a Pokémon Stadium FlashRAM image in N64 order (ares save.flash, tools/save_editor output) to the order the
port currently writes: record footers and the options/progress record's 16/32-bit fields in host (little-endian) order.
Records other than options keep their bytes, which is enough for blank/edited-progress saves.

Usage: tools/save_to_port_order.py <in.fla> <out.fla> [tools/save_editor dir]
"""
import sys
import os
sys.path.insert(0, sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'save_editor'))
from stadium_save.save import RECORDS
d = bytearray(open(sys.argv[1], 'rb').read())
def sw(o, n):
    d[o:o + n] = d[o:o + n][::-1]
for ref in RECORDS:
    for base in (ref.primary, ref.backup):
        end = base + ref.type.size
        for k in (6, 4, 2):
            sw(end - k, 2)
        if ref.path == ("options",):
            for o, n in ((2, 2), (4, 2), (8, 4), (0x0C, 2), (0x14, 2), (0x1C, 2)):
                sw(base + o, n)
open(sys.argv[2], 'wb').write(d)

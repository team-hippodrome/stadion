#!/usr/bin/env python3 -I
"""An input recording from timed presses, for routes written from a screen's input code (each press held 8
frames, pad 1):
  tools/evidence/mkrec.py out.rec "1720 A" "1900 DOWN" ...
Buttons: A B Z START UP DOWN LEFT RIGHT L R CUP CDOWN CLEFT CRIGHT.
"""
import sys

B = {"A": 0x8000, "B": 0x4000, "Z": 0x2000, "START": 0x1000, "UP": 0x0800, "DOWN": 0x0400, "LEFT": 0x0200,
     "RIGHT": 0x0100, "L": 0x0020, "R": 0x0010, "CUP": 0x0008, "CDOWN": 0x0004, "CLEFT": 0x0002, "CRIGHT": 0x0001}
z = " 0 0 0 0 0 0 0 0 0"
out = ["# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4", "0 0 0 0" + z]
last = 0
for x in sys.argv[2:]:
    f, b = x.split()
    f = int(f)
    out += ["%d %d 0 0%s" % (f, B[b], z), "%d 0 0 0%s" % (f + 8, z)]
    last = f + 8
out.append("# end %d" % last)
open(sys.argv[1], "w").write("\n".join(out) + "\n")

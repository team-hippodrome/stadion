#!/usr/bin/env python3 -I
"""An input recording for up to four pads, with held buttons, combinations and the analog stick (mkrec.py holds one
button of pad 1 for 8 frames):
  mkrec4.py out.rec [--end N] "FRAME [pP:]BTN[+BTN...][@X,Y] [HOLD]" ...
  "1720 A"            pad 1 presses A for 8 frames
  "2000 p2:A 4"       pad 2 presses A for 4 frames
  "2100 Z+A"          pad 1 holds Z and A together
  "2200 @0,80 60"     pad 1 holds the stick at (0, 80) for 60 frames (no button: "-")
  "2300 B@-60,0 20"   B and the stick together
Buttons: A B Z START UP DOWN LEFT RIGHT L R CUP CDOWN CLEFT CRIGHT. Overlapping presses of different pads combine;
overlapping presses of one pad are OR-ed (buttons) and the later stick wins.
"""
import sys

B = {"A": 0x8000, "B": 0x4000, "Z": 0x2000, "START": 0x1000, "UP": 0x0800, "DOWN": 0x0400, "LEFT": 0x0200,
     "RIGHT": 0x0100, "L": 0x0020, "R": 0x0010, "CUP": 0x0008, "CDOWN": 0x0004, "CLEFT": 0x0002, "CRIGHT": 0x0001,
     "-": 0}
args = sys.argv[2:]
end = None
if args[:1] == ["--end"]:
    end, args = int(args[1]), args[2:]
ev = []  # (start, stop, pad, buttons, stick)
for x in args:
    p = x.split()
    f, spec = int(p[0]), p[1]
    hold = int(p[2]) if len(p) > 2 else 8
    pad = 0
    if spec[:1] == "p" and ":" in spec:
        pad, spec = int(spec[1]) - 1, spec[3:]
    stick = None
    if "@" in spec:
        spec, s = spec.split("@")
        stick = tuple(int(v) for v in s.split(","))
    btn = 0
    for b in (spec or "-").split("+"):
        btn |= B[b]
    ev.append((f, f + hold, pad, btn, stick))
cuts = sorted({0} | {e[0] for e in ev} | {e[1] for e in ev})
out = ["# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4"]
prev = None
for c in cuts:
    row = [[0, 0, 0] for _ in range(4)]
    for f0, f1, pad, btn, stick in ev:
        if f0 <= c < f1:
            row[pad][0] |= btn
            if stick:
                row[pad][1], row[pad][2] = stick
    if row != prev:
        out.append("%d %s" % (c, " ".join("%d %d %d" % tuple(r) for r in row)))
        prev = row
out.append("# end %d" % (end or max(cuts)))
open(sys.argv[1], "w").write("\n".join(out) + "\n")

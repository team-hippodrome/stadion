#!/usr/bin/env python3 -I
"""Builds a new input recording: an existing one cut at a frame, followed by scripted presses.

Usage: rec_script.py <in.rec|-> <out.rec> <start frame> "<script>"
  in.rec       recording to keep up to (not including) the start frame; "-" for none (script starts at boot)
  start frame  first frame of the script (default: where in.rec ended)
  script       whitespace-separated tokens, executed in order:
                 A B Z START L R UP DOWN LEFT RIGHT CU CD CL CR   tap that button (held 6 frames, then 1 frame idle)
                 A+CU ...                                         tap several buttons together
                 hold:A:30                                        hold for 30 frames
                 <number>                                         wait that many frames
Example (pick three Pokemon with C-up, C-left, B, then confirm with A):
  tools/rec_script.py replays/team_select_all_ready.rec replays/to_battle.rec 99000 "CU 30 CL 30 B 30 A 120 A"
Replay the result with tools/replay.sh (it stops where the script ends).
"""
import sys

BUTTONS = {"A": 0x8000, "B": 0x4000, "Z": 0x2000, "START": 0x1000, "UP": 0x0800, "DOWN": 0x0400, "LEFT": 0x0200,
           "RIGHT": 0x0100, "L": 0x0020, "R": 0x0010, "CU": 0x0008, "CD": 0x0004, "CL": 0x0002, "CR": 0x0001}

def mask(spec):
    m = 0
    for name in spec.split("+"):
        if name.upper() not in BUTTONS:
            sys.exit("unknown button %r (known: %s)" % (name, " ".join(BUTTONS)))
        m |= BUTTONS[name.upper()]
    return m

src, dst = sys.argv[1], sys.argv[2]
start = sys.argv[3] if len(sys.argv) > 3 else ""
script = sys.argv[4] if len(sys.argv) > 4 else ""
lines, end = [], 0
if src != "-":
    for line in open(src):
        if line.startswith("# end"):
            end = max(end, int(line.split()[2]))
        elif not line.startswith("#") and line.strip():
            lines.append(line)
start = int(start) if start else end
kept = [l for l in lines if int(l.split()[0]) < start]
out = ["# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4\n"] + kept
frame = start

def emit(f, buttons):
    out.append("%d %d 0 0 0 0 0 0 0 0 0 0 0\n" % (f, buttons))

emit(frame, 0)
for tok in script.split():
    if tok.isdigit():
        frame += int(tok)
    elif tok.lower().startswith("hold:"):
        _, spec, n = tok.split(":")
        emit(frame, mask(spec))
        frame += int(n)
        emit(frame, 0)
        frame += 1
    else:
        emit(frame, mask(tok))
        frame += 6
        emit(frame, 0)
        frame += 1
frame += 30
out.append("# end %d\n" % frame)
open(dst, "w").writelines(out)
print("%s: %d kept input changes before frame %d, script ends at frame %d" % (dst, len(kept), start, frame))

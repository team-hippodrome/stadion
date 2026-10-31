#!/usr/bin/env python3 -I
"""Compare the opcode handlers the runs reached with the opcodes the shipped data uses (decode_seq.py's JSON).

usage: check_runs.py decode.json <run dir or batch dir with coverage.json>...
A handler reached but unused by the data would mean a voice read bytes that are not a command stream (or a decoder
error); a handler used by the data but never reached is still to reach.
"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decode_seq as d  # noqa: E402

dec = json.load(open(sys.argv[1]))
used = {int(k.split()[0], 16) for k in dec["used"]}
count = {}
for r in sys.argv[2:]:
    for n, v in json.load(open(os.path.join(r, "coverage.json")))["functions"].items():
        count[n] = count.get(n, 0) + v.get("count", 0)
bad = 0
for op, (name, _) in sorted(d.OPS.items()):
    c = count.get(name, 0)
    state = ("used, reached" if c else "used, NOT reached") if op in used else ("unused, REACHED" if c else "unused, not reached")
    bad += state in ("used, NOT reached", "unused, REACHED")
    print("0x%02x %-44s %-20s %d" % (op, name, state, c))
print("mismatches:", bad)

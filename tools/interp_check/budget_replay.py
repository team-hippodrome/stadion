#!/usr/bin/env python3
"""budget_replay.py <trace.txt>...: what the Smooth motion budget's window rule (src/port/interp.cpp CountSeries) makes
of a PORT_INTERP_LOG=1 trace, for example a tester's smooth_motion_log.txt. Best on a trace taken with
PORT_INTERP_BUDGET=0, where every series of in-between pictures is drawn.

A series is a game frame with k >= 2. It counts as late when the trace has a "late picture" / "late at" line or a
"drop at ... clock lag" / "next game frame waiting" line for that frame. An overrun is at least LATE of the last
WINDOW series, or RUN late series in a row (the constants below, as in interp.cpp); the window starts empty again after
one. (The probation after a hold is not modelled: the replay does not know the holds the run itself had.)"""
import re
import sys

WINDOW, LATE, RUN = 16, 8, 5

for path in sys.argv[1:]:
    series, late = [], set()
    for line in open(path, errors="replace"):
        m = re.match(r"\[interp\] frame (\d+): \S+ k (\d+) repaint", line)
        if m and int(m[2]) >= 2:
            series.append(int(m[1]))
        m = re.search(r"(?:late picture|late) at frame (\d+)", line)
        if m:
            late.add(int(m[1]))
        m = re.search(r"drop at frame (\d+) picture \d+/\d+: (clock lag|next game frame waiting)", line)
        if m:
            late.add(int(m[1]))
    window, overruns, run, longest = [], [], 0, 0
    for f in series:
        window = (window + [f in late])[-WINDOW:]
        run = run + 1 if f in late else 0
        longest = max(longest, run)
        if sum(window) >= LATE or run >= RUN:
            overruns.append(f)
            window, run = [], 0
    worst = max((sum(s in late for s in series[i:i + WINDOW]) for i in range(max(1, len(series) - WINDOW + 1))),
                default=0)
    print("%s: %d series, %d late, at most %d late in any %d, %d in a row; overruns at frames: %s" % (
        path, len(series), sum(f in late for f in series), worst, WINDOW, longest, overruns or "none"))

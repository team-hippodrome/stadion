#!/bin/bash
# Runs the port under lldb for N seconds and prints the first crash backtrace (or the log tail if it survives).
cd "$(dirname "$0")/../build"
ROM=${ROM:-"/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64"}
SECS=${1:-20}
rm -f lldb.out
(lldb -b -o "run" -o "bt 16" -o "thread backtrace all" -o "kill" -o "quit" -- ./pokestadium "$ROM" > lldb.out 2>&1 &)
sleep "$SECS"
pkill -f "build/pokestadium" 2>/dev/null; pkill lldb 2>/dev/null
if grep -q "stop reason" lldb.out; then grep -A18 "stop reason" lldb.out | head -40; else echo "no crash in ${SECS}s"; tail -5 lldb.out; fi

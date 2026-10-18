#!/bin/bash
# Usage: tools/run_for.sh <seconds> <logfile> <rom>   (env vars pass through)
# Runs the port for a fixed time, then terminates it (SIGKILL fallback: the game can hang on SIGTERM).
S="$1"; LOG="$2"; ROM="$3"
cd "$(dirname "$0")/../build" || exit 1
./pokestadium "$ROM" > "$LOG" 2>&1 &
P=$!
sleep "$S"
kill $P 2>/dev/null; sleep 2; kill -9 $P 2>/dev/null; wait $P 2>/dev/null
exit 0

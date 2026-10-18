#!/bin/bash
# Rebuilds, runs the port for N seconds and symbolizes the crash stack if there is one.
cd "$(dirname "$0")/.."
cmake --build build --target pokestadium 2>&1 | grep -E " error:|FAILED|undefined" | head -5
cd build
ROM=${ROM:-"/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64"}
(PORT_TRACE=${PORT_TRACE:-} ./pokestadium "$ROM" > run.log 2>&1 & P=$!; sleep "${1:-12}"; if kill -0 $P 2>/dev/null; then echo "ALIVE after ${1:-12}s"; kill $P; else wait $P; echo "exit $?"; fi)
grep -q "^\*\*\* crash" run.log && ../tools/symbolize.sh "$PWD/run.log" | head -${2:-10}
grep "^\[os\]" run.log | tail -4

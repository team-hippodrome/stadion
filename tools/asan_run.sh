#!/bin/bash
# Rebuilds the AddressSanitizer variant and runs it for N seconds; prints the first ASAN report (or the crash stack).
cd "$(dirname "$0")/.."
cmake --build build_asan --target pokestadium 2>&1 | grep -E " error |FAILED" | head -3
cd build_asan
ROM=${ROM:-"/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64"}
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./pokestadium "$ROM" > run.log 2>&1 &
P=$!   # the game itself (no wrapper subshell) so the kill really ends it
for ((i = 0; i < ${1:-50}; i++)); do kill -0 $P 2>/dev/null || break; sleep 1; done
kill $P 2>/dev/null; sleep 2; kill -9 $P 2>/dev/null; wait $P 2>/dev/null
if grep -q "AddressSanitizer" run.log; then grep -A14 "ERROR: AddressSanitizer" run.log | cut -c1-200; grep "is located" run.log | head -3 | cut -c1-260
else echo "no ASAN report"; grep "crash:" run.log; fi

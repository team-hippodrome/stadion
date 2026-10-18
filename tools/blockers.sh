#!/bin/bash
# After a run with PORT_TRACE=2: shows where each game thread last blocked (first game frames of the stack).
cd "$(dirname "$0")/.."
LOG=${1:-build/run.log}
SLIDE=$(grep -o 'slide=0x[0-9a-f]*' "$LOG" | tail -1 | cut -d= -f2)
[ build/pokestadium.dSYM -nt build/pokestadium ] || dsymutil build/pokestadium 2>/dev/null
for t in $(grep '^@stack' "$LOG" | sed -E 's/.*thread=([0-9]+).*/\1/' | sort -un); do
  line=$(grep "^@stack .* thread=$t " "$LOG" | tail -1)
  echo "--- thread $t"
  echo "$line" | sed -E 's/.*:  *//' | tr ' ' '\n' | grep -E '^0x' | head -8 | xargs atos -i -o build/pokestadium -l 0x$(printf '%x' $((0x100000000 + SLIDE))) | grep -v "os_thread.cpp\|^0x" | head -5
done

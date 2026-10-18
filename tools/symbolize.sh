#!/bin/bash
# Usage: tools/symbolize.sh build/run.log   (resolves the addresses printed by the crash handler)
LOG=$(cd "$(dirname "${1:-build/run.log}")" && pwd)/$(basename "${1:-build/run.log}")
cd "$(dirname "$0")/.."
SLIDE=$(grep -o "image slide 0x[0-9a-f]*" "$LOG" | tail -1 | awk '{print $3}')
[ ${BIN:-build/pokestadium}.dSYM -nt ${BIN:-build/pokestadium} ] || dsymutil ${BIN:-build/pokestadium} 2>/dev/null
grep -E '^  0x[0-9a-f]+' "$LOG" | tail -48 | awk '{print $1}' | xargs atos -i -o ${BIN:-build/pokestadium} -l 0x$(printf '%x' $((0x100000000 + SLIDE)))

# Stall reports (os_thread.cpp port_DumpThreads): one "@stack thread=N slide=0x...: addr addr ..." line per blocked thread.
grep "^@stack" "$LOG" | tail -12 | while read -r _ th sl rest; do
    s=${sl#slide=}; s=${s%:}
    echo "== $th"
    echo $rest | xargs atos -i -o ${BIN:-build/pokestadium} -l 0x$(printf '%x' $((0x100000000 + s))) | grep -v "^$" | head -8
done

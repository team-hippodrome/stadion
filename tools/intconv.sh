#!/bin/bash
# Lists implicit integer<->pointer conversions (-Wint-conversion), the compiler's best signal for
# "an address is being carried in a 32-bit integer". Output: build/ptr/intconv.txt
cd "$(dirname "$0")/.."
export OUT=build/ptr; mkdir -p "$OUT"
export FLAGS=$(grep -v -e '^-w$' -e '^-Wno-int-conversion$' tools/compile_flags.txt | tr '\n' ' ')
one() { clang -fsyntax-only $FLAGS -Wint-conversion "$1" 2>&1 | grep "int-conversion\]"; }
export -f one
find game/src -name '*.c' | sort | xargs -P 8 -n 1 bash -c 'one "$0"' | sort -u > "$OUT/intconv.txt"
wc -l < "$OUT/intconv.txt"

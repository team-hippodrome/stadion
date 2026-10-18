#!/bin/bash
# Finds osRecvMesg/osJamMesg-style calls whose receive variable is not pointer-sized (the host writes 8 bytes).
cd "$(dirname "$0")/.."
export FLAGS=$(grep -v -e '^-w$' -e '^-Wno-incompatible-pointer-types$' tools/compile_flags.txt | tr '\n' ' ')
one() { clang -fsyntax-only $FLAGS -Wincompatible-pointer-types "$1" 2>&1 | grep "incompatible pointer types passing.*to parameter of type 'OSMesg \*'" ; }
export -f one
find game/src -name '*.c' | sort | xargs -P 8 -n 1 bash -c 'one "$0"' | sort -u

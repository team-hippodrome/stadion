#!/bin/bash
# Lists every place the compiler sees a pointer squeezed into / created from a 32-bit integer.
# These are the sites that corrupt addresses on a 64-bit host. Output: build/ptr/warnings.txt
cd "$(dirname "$0")/.."
export OUT=build/ptr; mkdir -p "$OUT"
export FLAGS=$(grep -v -e '^-w$' -e '^-Wno-int-conversion$' -e '^-Wno-incompatible' tools/compile_flags.txt | tr '\n' ' ')
one() { clang -fsyntax-only $FLAGS -Wint-conversion -Wpointer-to-int-cast -Wint-to-pointer-cast -Wshorten-64-to-32 "$1" 2>&1 | grep -E "warning: (cast to smaller integer|incompatible (integer to pointer|pointer to integer)|cast to .* from smaller|implicit conversion loses integer precision: .*(pointer|uintptr|long))" ; }
export -f one
{ find game/src -name '*.c'; grep -v '^#' cmake/ultralib_sources.txt | sed 's|^|game/lib/ultralib/src/|'; } | sort | xargs -P 8 -n 1 bash -c 'one "$0"' | grep "^game/src\|^game/lib" | sort -u > "$OUT/warnings.txt"
wc -l < "$OUT/warnings.txt"
cut -d: -f1 "$OUT/warnings.txt" | sort | uniq -c | sort -rn | head -${1:-15}

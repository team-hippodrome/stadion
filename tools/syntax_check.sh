#!/bin/bash
# Runs clang -fsyntax-only on every game source and writes per-file error counts.
# Usage: tools/syntax_check.sh [outdir]
cd "$(dirname "$0")/.."
export OUT=${1:-build/syntax}
mkdir -p "$OUT"
export FLAGS=$(tr '\n' ' ' < tools/compile_flags.txt)
check_one() {
  f=$1; log="$OUT/$(echo "$f" | tr / _).log"
  clang -fsyntax-only $FLAGS "$f" 2> "$log"
  echo "$(grep -c "error:" "$log") $f"
}
export -f check_one
find game/src -name '*.c' | sort | xargs -P 8 -n 1 bash -c 'check_one "$0"' | sort -rn > "$OUT/summary.txt"
echo "files with errors: $(awk '$1>0' "$OUT/summary.txt" | wc -l) / $(wc -l < "$OUT/summary.txt")"
echo "total errors: $(awk '{s+=$1} END{print s}' "$OUT/summary.txt")"

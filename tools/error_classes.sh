#!/bin/bash
# Groups clang errors across all syntax-check logs by unique source location (deduped, since headers repeat).
cat ${1:-build/syntax}/*.log | grep "error:" | sort -u | sed -E 's/^[^ ]+ error: //' | sed -E "s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn | head -${2:-25}

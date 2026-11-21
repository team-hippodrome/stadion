#!/bin/bash
# Fetches the submodule (the Team Hippodrome libultraship fork). The decompiled game is vendored in game/.
# Run once after cloning, then build with: cmake -S . -B build && cmake --build build -j
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
git submodule sync --recursive
git submodule update --init --recursive

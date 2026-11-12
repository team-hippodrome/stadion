#!/bin/bash
# Fetches the submodules (game, the Team Hippodrome libultraship fork) and applies
# the port's changes to the decompiled game (patches/game/*.patch).
# Run once after cloning, then build with: cmake -S . -B build && cmake --build build -j
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
git submodule sync --recursive
git submodule update --init --recursive
if python3 tools/game_patches.py check >/dev/null 2>&1; then
    python3 tools/game_patches.py apply
else
    echo "patches/game already applied (or does not apply cleanly); leaving game as it is"
fi

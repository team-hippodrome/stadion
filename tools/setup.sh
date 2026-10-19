#!/bin/bash
# Fetches the submodules (game, libultraship, ghostship-ref) and applies the port's changes to libultraship
# (patches/libultraship.patch) and to the decompiled game (patches/game/*.patch).
# Run once after cloning, then build with: cmake -S . -B build && cmake --build build -j
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
git submodule update --init --recursive
if git -C libultraship apply --check "$HERE/patches/libultraship.patch" 2>/dev/null; then
    git -C libultraship apply "$HERE/patches/libultraship.patch"
    echo "applied patches/libultraship.patch"
else
    echo "patches/libultraship.patch already applied (or does not apply cleanly); leaving libultraship as it is"
fi
if python3 tools/game_patches.py check >/dev/null 2>&1; then
    python3 tools/game_patches.py apply
else
    echo "patches/game already applied (or does not apply cleanly); leaving game as it is"
fi

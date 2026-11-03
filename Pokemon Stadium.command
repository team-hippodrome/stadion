#!/bin/bash
# Double-click the shared launcher. Its config and saves live beside this script.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -x "$HERE/build/pokestadium" ]; then
    echo "The game is not built yet: run 'cmake --build build' in $HERE first."
    read -r -p "Press Return to close." _
    exit 1
fi
export SHIP_HOME="${SHIP_HOME:-$HERE}"
exec "$HERE/build/pokestadium" "$@"

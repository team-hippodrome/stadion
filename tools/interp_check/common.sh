# Shared by the interp_check scripts (sourced). ROOT: the checkout; BIN / ROM: the build and the ROM to run.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="$ROOT/tools/interp_check"
BIN="${PORT_BIN:-$ROOT/build/pokestadium}"
ROM="${PORT_ROM:-/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64}"
TRI="$ROOT/build/interp_tri"
# the picture scorer, built next to the game
tri_build() {
    if [ ! -x "$TRI" ] || [ "$HERE/tri.c" -nt "$TRI" ]; then cc -O2 -o "$TRI" "$HERE/tri.c" || exit 1; fi
}

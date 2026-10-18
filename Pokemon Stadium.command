#!/bin/bash
# Double-click to play Pokémon Stadium (macOS port).
# - Stadium ROM:      ROM below
# - Transfer Pak:     German Pokémon Yellow (GB_ROM); set GB_ROM="" to play without a Game Boy cartridge
# - Saves (kept between sessions, in ./saves): stadium.flash (Stadium), yellow.sav (the Game Boy cartridge)
ROM="/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64"
GB_ROM="/Users/michael/Downloads/Pokemon - Gelbe Edition - Special Pikachu Edition (Germany) (GBC,SGB Enhanced).gb"

HERE="$(cd "$(dirname "$0")" && pwd)"
SAVES="$HERE/saves"
mkdir -p "$SAVES"

if [ ! -x "$HERE/build/pokestadium" ]; then
    echo "The game is not built yet: run 'cmake --build build' in $HERE first."
    read -r -p "Press Return to close." _
    exit 1
fi
if [ ! -f "$ROM" ]; then
    echo "Stadium ROM not found: $ROM"
    read -r -p "Press Return to close." _
    exit 1
fi

export PORT_FLASH_FILE="$SAVES/stadium.flash"
if [ -n "$GB_ROM" ] && [ -f "$GB_ROM" ]; then
    # first start: begin with the prepared Yellow save (party PIKACHU, BULBASAUR, CHARMANDER)
    if [ ! -f "$SAVES/yellow.sav" ] && [ -f "$HERE/gb/yellow_de.sav" ]; then
        cp "$HERE/gb/yellow_de.sav" "$SAVES/yellow.sav"
    fi
    export PORT_GB_ROM="$GB_ROM"
    export PORT_GB_SAVE="$SAVES/yellow.sav"
fi

cd "$HERE/build" || exit 1
exec ./pokestadium "$ROM"

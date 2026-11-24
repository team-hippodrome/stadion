#pragma once
// The launcher's Game Boy cartridge pictures, rebuilt from the cartridge ROM itself: the Pokémon each Gen 1 game shows
// first on its title screen (Red: Charmander, Green: Bulbasaur, Blue: Squirtle, Yellow/Pikachu: Pikachu), its front
// sprite decompressed from the ROM and coloured with the game's own Super Game Boy palette. Nothing is bundled: a ROM
// that is not a Gen 1 Pokémon game (or that the lookups below do not recognise) gets no picture.
//
// Every table is found by content, not by address, so the same code reads the Japanese (Red/Green/Blue/Pikachu, all
// revisions) and the Western releases (EN/DE/FR/ES/IT Red/Blue/Yellow):
// - the base stats table (28 bytes per Pokémon, Pokédex order) by Bulbasaur's entry: size byte and front pic pointer;
// - PokedexOrder (internal index -> Pokédex number) by its first 8 entries;
// - the pic bank by parsing UncompressMonSprite's "cp index / ld a, bank / jr c" chain (JP Red/Green split differ);
// - MonsterPalettes (Pokédex number -> SGB palette id) by its first 10 entries; SuperPalettes follows it.

#include <cstdint>
#include <string>
#include <vector>

namespace Port {

struct GbCartArt {
    std::string version;        // "red" | "green" | "blue" | "yellow" | "" (not a Gen 1 Pokémon cartridge)
    int dex = 0;                // Pokédex number of the title Pokémon
    int width = 0, height = 0;  // sprite size in pixels (a multiple of 8; 0 when no sprite was found)
    std::vector<uint8_t> shade; // width * height GB shades, 0 (white) .. 3 (black), row-major
    uint32_t rgb[4] = { 0xE0F8D0, 0x88C070, 0x346856, 0x081820 }; // 0xRRGGBB per shade; DMG green without SGB data
    bool sgbPalette = false;    // rgb[] came from the ROM's SGB palette
};

// version from the header title (0x134), as the launcher names carts
std::string GbCartVersion(const std::vector<uint8_t>& rom);

// decode the title Pokémon of a whole GB ROM image; false (art.width == 0) when it cannot be found
bool DecodeGbCartArt(const std::vector<uint8_t>& rom, GbCartArt& art);

} // namespace Port

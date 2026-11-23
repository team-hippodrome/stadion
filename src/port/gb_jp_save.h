#pragma once
// Japanese Pokémon Red/Green/Blue/Yellow cartridges for the US Stadium.
//
// The US Stadium only takes overseas cartridges: its header checks want destination code 1 and a 1 MB ROM, and it reads
// the battery RAM in the international layout (11-byte names, 12 boxes of 20 Pokémon). A Japanese cartridge has the
// Japanese layout (6-byte names in kana, 8 boxes of 30), so the Transfer Pak (transfer_pak.cpp) presents it as an
// overseas one: a 1 MB ROM image with an overseas header, and battery RAM in the international layout, built from the
// cartridge's own RAM. Writes to that view are converted back into the Japanese RAM. The GB Tower runs the Japanese
// game itself and so reads the cartridge's RAM as it is.
//
// The 240 box slots map one to one: Japanese box n (1-8) shows its first 20 Pokémon as box n, and Pokémon 21-30 of
// boxes 1+2, 3+4, 5+6 and 7+8 fill boxes 9, 10, 11 and 12. Names are written in romaji (a Pokémon that keeps its
// species name gets the English species name); converting back restores the original kana for every name that came
// from the cartridge, and spells any other name in katakana.
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Port {

constexpr size_t kGbSaveSize = 0x8000;

// Whether this ROM (its first 0x150 bytes at least) is a Japanese Red/Green/Blue/Yellow cartridge with 32 KB of
// battery RAM.
bool IsJapaneseGen1Rom(const uint8_t* rom, size_t size);
// The ROM image the US Stadium accepts: at least 1 MB (a smaller ROM repeats, as its address lines do on the real
// cartridge), destination code 1, ROM size code 5, an MBC3 instead of an MBC1, with the header and global checksums
// recomputed.
std::vector<uint8_t> OverseasRomImage(const std::vector<uint8_t>& rom);

// What a conversion remembers, so that converting back gives the cartridge's own bytes for what didn't change.
struct GbJpSaveMap {
    std::array<uint8_t, 11> player{}, rival{};             // the names as presented
    std::map<std::string, std::array<uint8_t, 12>> mons;   // Pokémon key + presented nickname/OT -> Japanese nickname+OT
    std::map<std::string, std::array<uint8_t, 6>> nicks, ots; // presented name -> Japanese name
    int overflowSplit[4] = {};                              // Pokémon of box 9+j that came from Japanese box 2j+1
    uint8_t currentBox = 0;                                 // Japanese current box byte
};

// Japanese battery RAM (32 KB) -> international layout (32 KB), with valid checksums.
void JapaneseSaveToInternational(const uint8_t* jp, uint8_t* intl, GbJpSaveMap& map);
// International layout -> Japanese battery RAM. 'jp' holds the cartridge's RAM and is updated in place; what has no
// counterpart in the international view (battle scratch data) stays as it is. Checksums are recomputed.
void InternationalSaveToJapanese(const uint8_t* intl, uint8_t* jp, const GbJpSaveMap& map);

// The trainer name and ID of a Japanese save in romaji, empty when its checksum is wrong.
bool JapaneseSaveTrainer(const uint8_t* jp, size_t size, std::string& name, unsigned& id);

// Name conversion (Game Boy character sets): a Japanese name (up to 5 kana, terminated by 0x50) to romaji, at most
// 'maxChars' letters, 0x50-terminated in 11 bytes; and an international name back to katakana.
std::array<uint8_t, 11> JapaneseNameToInternational(const uint8_t* jp, int maxChars);
std::array<uint8_t, 6> InternationalNameToJapanese(const uint8_t* intl);

} // namespace Port

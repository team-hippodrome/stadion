// Reading any accepted Stadium ROM with US Rev 2 (1.2) code.
//
// The port's game code is compiled as Rev 2 (CMakeLists.txt: VERSION_US_REV2), so every ROM offset it uses (the
// *_ROM_START constants in generated/abs_syms.S and the literals in the C, such as TRAINER_DATA_ROM_START) is where
// the Rev 2 cartridge keeps that piece. The 1.0 and Rev 1 cartridges hold the same pieces a little earlier or later.
// generated/rom_layouts.c lists, per version, where each piece of the Rev 2 ROM is in that version's ROM (from the
// decomp's link maps; tools/gen_rom_layouts.py), and every ROM read the game makes goes through Port_RomRead.
//
// What each ROM brings is its own data: textures, models, text, trainer and rental tables. The code, and so the
// behaviour (Rev 2's fixes, such as nicknames with spaces), is Rev 2's whatever the ROM.
#include "rom_version.h"
#include "rom_check.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace Port {
const uint8_t* RomData();
size_t RomSize();
} // namespace Port

namespace {

const PortRomLayout* gLayout = nullptr; // nullptr: read the ROM as laid out like Rev 2
PortRomVersion gVersion = PORT_ROM_US_REV2;

// the segment of the current layout that holds gameOffset, else the last one before it (its delta carries on
// through a gap), else nullptr
const PortRomSegment* FindSegment(uint32_t gameOffset) {
    if (gLayout == nullptr || gLayout->segments == nullptr || gLayout->segmentCount == 0) {
        return nullptr;
    }
    const PortRomSegment* begin = gLayout->segments;
    const PortRomSegment* end = begin + gLayout->segmentCount;
    // the last segment that starts at or before gameOffset; nested pieces (a fragment's sub-archives) start later
    // than their container, so walking back finds the innermost one that holds the offset
    const PortRomSegment* it =
        std::upper_bound(begin, end, gameOffset, [](uint32_t off, const PortRomSegment& s) { return off < s.gameStart; });
    if (it == begin) {
        return nullptr;
    }
    const PortRomSegment* before = it - 1;
    for (const PortRomSegment* s = before;; s--) {
        if (gameOffset < s->gameEnd) {
            return s;
        }
        if (s == begin) {
            break;
        }
    }
    return before;
}

} // namespace

namespace Port {

void SelectRomVersion(PortRomVersion version) {
    gVersion = version;
    gLayout = nullptr;
    for (unsigned i = 0; i < gPortRomLayoutCount; i++) {
        if (gPortRomLayouts[i].version == version) {
            gLayout = &gPortRomLayouts[i];
        }
    }
    fprintf(stderr, "[rom] %s\n", RomVersionName(version)); // (rom_check.cpp)
}

PortRomVersion LoadedRomVersion() {
    return gVersion;
}

} // namespace Port

extern "C" {

uint32_t Port_RomOffset(uint32_t gameOffset) {
    const PortRomSegment* s = FindSegment(gameOffset);
    if (s == nullptr) {
        return gameOffset;
    }
    return gameOffset - s->gameStart + s->romStart;
}

size_t Port_RomRead(uint32_t gameOffset, void* dst, size_t size) {
    const uint8_t* rom = Port::RomData();
    const size_t romSize = Port::RomSize();
    uint8_t* out = static_cast<uint8_t*>(dst);
    size_t done = 0;
    while (done < size) {
        const uint32_t off = gameOffset + (uint32_t)done;
        size_t piece = size - done;
        const PortRomSegment* s = FindSegment(off);
        if (s != nullptr && off < s->gameEnd) {
            piece = std::min<size_t>(piece, s->gameEnd - off); // the next piece may have moved by another amount
        }
        if (gLayout != nullptr && gLayout->segments != nullptr) {
            // and never past the start of the next segment
            const PortRomSegment* begin = gLayout->segments;
            const PortRomSegment* end = begin + gLayout->segmentCount;
            const PortRomSegment* next = std::upper_bound(
                begin, end, off, [](uint32_t o, const PortRomSegment& seg) { return o < seg.gameStart; });
            if (next < end) {
                piece = std::min<size_t>(piece, next->gameStart - off);
            }
        }
        const uint32_t src = Port_RomOffset(off);
        if (src >= romSize) {
            break;
        }
        const size_t n = std::min<size_t>(piece, romSize - src);
        memcpy(out + done, rom + src, n);
        done += n;
        if (n < piece) {
            break;
        }
    }
    return done;
}

uint32_t Port_RomCodeAddrToGame(uint32_t fragmentRom, uint32_t addr) {
    if (gLayout == nullptr || gLayout->codeMoves == nullptr) {
        return addr;
    }
    const PortCodeMove* begin = gLayout->codeMoves;
    const PortCodeMove* end = begin + gLayout->codeMoveCount;
    const PortCodeMove key = { fragmentRom, addr, 0 };
    const PortCodeMove* it = std::lower_bound(begin, end, key, [](const PortCodeMove& a, const PortCodeMove& b) {
        return a.fragmentRom != b.fragmentRom ? a.fragmentRom < b.fragmentRom : a.romAddr < b.romAddr;
    });
    if (it != end && it->fragmentRom == fragmentRom && it->romAddr == addr) {
        return it->gameAddr;
    }
    return addr;
}

} // extern "C"

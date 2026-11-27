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
#include "seg_syms.h"
#include "StadionPort.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace Port {
const uint8_t* RomData();
size_t RomSize();
} // namespace Port

namespace {

const PortRomLayout* gLayout = nullptr; // nullptr: read the ROM as laid out like Rev 2
PortRomVersion gVersion = PORT_ROM_US_REV2;

// Extra ROMs (rom_version.h): slot 1 to PORT_ROM_EXTRA_SLOTS; slot 0 is the loaded ROM (Port::RomData)
struct ExtraRom {
    std::vector<uint8_t> data;
    PortRomVersion version = PORT_ROM_VERSION_COUNT;
    const PortRomLayout* layout = nullptr;
};
ExtraRom gExtra[PORT_ROM_EXTRA_SLOTS + 1];

// Rev 2 ROM pieces read from an extra ROM instead of the loaded one (restored content)
struct Redirect {
    uint32_t gameStart, gameEnd;
    int slot;
};
std::vector<Redirect> gRedirects;

const PortRomLayout* LayoutOf(PortRomVersion version) {
    for (unsigned i = 0; i < gPortRomLayoutCount; i++) {
        if (gPortRomLayouts[i].version == version) {
            return &gPortRomLayouts[i];
        }
    }
    return nullptr;
}

// the segment of layout that holds gameOffset, else the last one before it (its delta carries on through a gap),
// else nullptr
const PortRomSegment* FindSegmentIn(const PortRomLayout* layout, uint32_t gameOffset) {
    if (layout == nullptr || layout->segments == nullptr || layout->segmentCount == 0) {
        return nullptr;
    }
    const PortRomSegment* begin = layout->segments;
    const PortRomSegment* end = begin + layout->segmentCount;
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

const PortRomSegment* FindSegment(uint32_t gameOffset) {
    return FindSegmentIn(gLayout, gameOffset);
}

uint32_t OffsetIn(const PortRomLayout* layout, uint32_t gameOffset) {
    const PortRomSegment* s = FindSegmentIn(layout, gameOffset);
    if (s == nullptr) {
        return gameOffset;
    }
    return gameOffset - s->gameStart + s->romStart;
}

// the extra ROM that keeps the Rev 2 offset, or 0 for the loaded ROM
int SlotOf(uint32_t gameOffset) {
    for (const Redirect& r : gRedirects) {
        if (r.gameStart <= gameOffset && gameOffset < r.gameEnd) {
            return r.slot;
        }
    }
    return 0;
}

const PortRomLayout* LayoutOfSlot(int slot) {
    return slot == 0 ? gLayout : gExtra[slot].layout;
}

uint32_t Mark(int slot, uint32_t romOffset) {
    return romOffset | PORT_ROM_RAW | ((uint32_t)slot << PORT_ROM_SLOT_SHIFT);
}

} // namespace

namespace Port {

void SelectRomVersion(PortRomVersion version) {
    gVersion = version;
    gLayout = LayoutOf(version);
    fprintf(stderr, "[rom] %s\n", RomVersionName(version)); // (rom_check.cpp)
}

PortRomVersion LoadedRomVersion() {
    return gVersion;
}

int AddExtraRom(std::vector<uint8_t> data, PortRomVersion version) {
    for (int slot = 1; slot <= PORT_ROM_EXTRA_SLOTS; slot++) {
        if (gExtra[slot].version == version) {
            return slot; // one of each release is enough
        }
    }
    for (int slot = 1; slot <= PORT_ROM_EXTRA_SLOTS; slot++) {
        if (gExtra[slot].data.empty()) {
            gExtra[slot].data = std::move(data);
            gExtra[slot].version = version;
            gExtra[slot].layout = LayoutOf(version);
            fprintf(stderr, "[rom] extra ROM %d: %s\n", slot, RomVersionName(version));
            return slot;
        }
    }
    return 0;
}

void ClearExtraRoms() {
    for (ExtraRom& rom : gExtra) {
        rom = ExtraRom();
    }
    gRedirects.clear();
}

int ExtraRomSlot(PortRomVersion version) {
    for (int slot = 1; slot <= PORT_ROM_EXTRA_SLOTS; slot++) {
        if (!gExtra[slot].data.empty() && gExtra[slot].version == version) {
            return slot;
        }
    }
    return 0;
}

void RedirectRomPiece(uint32_t gameStart, uint32_t gameEnd, int slot) {
    if (slot > 0 && slot <= PORT_ROM_EXTRA_SLOTS && !gExtra[slot].data.empty()) {
        gRedirects.push_back({ gameStart, gameEnd, slot });
    }
}

} // namespace Port

extern "C" {

uint32_t Port_RomOffset(uint32_t gameOffset) {
    return OffsetIn(gLayout, gameOffset);
}

uint32_t Port_RomToRaw(uint32_t gameOffset) {
    if (gameOffset & PORT_ROM_RAW) {
        return gameOffset;
    }
    const int slot = SlotOf(gameOffset);
    return Mark(slot, OffsetIn(LayoutOfSlot(slot), gameOffset));
}

uint32_t Port_ExtraRomRaw(int slot, uint32_t gameOffset) {
    if (slot <= 0 || slot > PORT_ROM_EXTRA_SLOTS || gExtra[slot].data.empty()) {
        return 0;
    }
    return Mark(slot, OffsetIn(gExtra[slot].layout, gameOffset));
}

uint32_t Port_RomRawIn(int slot, uint32_t romOffset) {
    return Mark(slot, romOffset);
}

uint32_t Port_RomRangeEndToRaw(uint32_t gameStart, uint32_t gameEnd) {
    if (gameEnd & PORT_ROM_RAW) {
        return gameEnd;
    }
    if (gameEnd <= gameStart) {
        return Port_RomToRaw(gameStart) + (gameEnd - gameStart);
    }
    const int slot = SlotOf(gameStart);
    const PortRomLayout* layout = LayoutOfSlot(slot);
    if (layout != nullptr && layout->segments != nullptr) {
        const PortRomSegment* begin = layout->segments;
        const PortRomSegment* end = begin + layout->segmentCount;
        // the innermost piece holding the start that ends exactly at gameEnd
        for (const PortRomSegment* s = begin; s < end && s->gameStart <= gameStart; s++) {
            if (gameStart < s->gameEnd && s->gameEnd == gameEnd) {
                return Mark(slot, s->romEnd);
            }
        }
        // a piece that starts at gameEnd (ranges such as "this fragment up to the next asset")
        for (const PortRomSegment* s = begin; s < end && s->gameStart <= gameEnd; s++) {
            if (s->gameStart == gameEnd) {
                return Mark(slot, s->romStart);
            }
        }
    }
    return Mark(slot, OffsetIn(layout, gameEnd - 1) + 1);
}

size_t Port_RomRead(uint32_t gameOffset, void* dst, size_t size) {
    const uint8_t* rom = Port::RomData();
    size_t romSize = Port::RomSize();
    uint8_t* out = static_cast<uint8_t*>(dst);
    if (!(gameOffset & PORT_ROM_RAW) && SlotOf(gameOffset) != 0) {
        gameOffset = Port_RomToRaw(gameOffset); // a piece read from an extra ROM (whole: pieces are not split)
    }
    if (gameOffset & PORT_ROM_RAW) {
        const int slot = (int)((gameOffset & PORT_ROM_SLOT_MASK) >> PORT_ROM_SLOT_SHIFT);
        const uint32_t src = gameOffset & ~(PORT_ROM_RAW | PORT_ROM_SLOT_MASK);
        if (slot != 0) {
            rom = gExtra[slot].data.data();
            romSize = gExtra[slot].data.size();
        }
        if (src >= romSize) {
            return 0;
        }
        const size_t n = std::min<size_t>(size, romSize - src);
        memcpy(out, rom + src, n);
        return n;
    }
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

int Port_RomVersion(void) {
    return (int)gVersion;
}

uintptr_t Port_SegSym(uintptr_t rev2) {
    for (unsigned t = 0; t < gPortSegSymTableCount; t++) {
        const PortSegSymTable& table = gPortSegSymTables[t];
        if (table.version != gVersion) {
            continue;
        }
        const PortSegSym* begin = table.syms;
        const PortSegSym* end = begin + table.count;
        const PortSegSym* it = std::lower_bound(begin, end, (uint32_t)rev2,
                                                [](const PortSegSym& s, uint32_t v) { return s.rev2 < v; });
        if (it != end && it->rev2 == (uint32_t)rev2) {
            return it->value;
        }
    }
    return rev2;
}

// Data the translated releases keep in their own fragment images (StadionPort.h). Fragments are stored uncompressed and
// link at the same address in every release, so the loaded ROM's copy of a fragment starts where its layout puts the
// Rev 2 fragment's start, and an address in it is that start plus its distance from the link address.
static std::map<std::tuple<uint32_t, uint32_t, bool>, std::vector<uint8_t>> sFragmentData;

static const uint8_t* FragmentRomBytes(uintptr_t fragmentRom, uint32_t fragmentVram, uint32_t vram, uint32_t size,
                                       bool words) {
    if (vram < fragmentVram || size == 0) {
        return nullptr;
    }
    const auto key = std::make_tuple(vram, size, words);
    auto it = sFragmentData.find(key);
    if (it == sFragmentData.end()) {
        std::vector<uint8_t> data(size);
        const uint32_t raw = Port_RomToRaw((uint32_t)fragmentRom) + (vram - fragmentVram);
        if (Port_RomRead(raw, data.data(), size) != size) {
            return nullptr;
        }
        if (words) {
            for (uint32_t i = 0; i + 4 <= size; i += 4) {
                const uint32_t w = ((uint32_t)data[i] << 24) | ((uint32_t)data[i + 1] << 16) |
                                   ((uint32_t)data[i + 2] << 8) | data[i + 3];
                memcpy(&data[i], &w, 4);
            }
        }
        it = sFragmentData.emplace(key, std::move(data)).first;
    }
    return it->second.data();
}

const void* Port_FragmentRomData(uintptr_t fragmentRom, uint32_t fragmentVram, uint32_t vram, uint32_t size) {
    return FragmentRomBytes(fragmentRom, fragmentVram, vram, size, false);
}

const uint32_t* Port_FragmentRomWords(uintptr_t fragmentRom, uint32_t fragmentVram, uint32_t vram, uint32_t count) {
    return reinterpret_cast<const uint32_t*>(FragmentRomBytes(fragmentRom, fragmentVram, vram, count * 4, true));
}

uint32_t Port_FragmentRomWord(uintptr_t fragmentRom, uint32_t fragmentVram, uint32_t vram) {
    const uint32_t* w = Port_FragmentRomWords(fragmentRom, fragmentVram, vram, 1);
    return w != nullptr ? *w : 0;
}

} // extern "C"

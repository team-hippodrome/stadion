// Where each accepted ROM keeps what the port's code (US Rev 2) expects: the tables tools/gen_rom_layouts.py writes
// into src/port/generated/rom_layouts.c, and the version list rom_version.cpp identifies ROMs by.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortRomVersion {
    PORT_ROM_US,      // USA 1.0
    PORT_ROM_US_REV1, // USA Rev 1 (1.1)
    PORT_ROM_US_REV2, // USA Rev 2 (1.2), retail and kiosk: the version the code is compiled as
    PORT_ROM_EU,      // Europe (English) 1.0
    PORT_ROM_EU_REV1, // Europe (English) Rev 1
    PORT_ROM_FR,      // France
    PORT_ROM_DE,      // Germany
    PORT_ROM_ES,      // Spain
    PORT_ROM_IT,      // Italy
    PORT_ROM_JP,      // Japan (ポケモンスタジアム２, CP2J): only as an extra ROM for restored content, never the one played
    PORT_ROM_VERSION_COUNT
} PortRomVersion;

// [gameStart, gameEnd) of the Rev 2 ROM is at [romStart, romEnd) in this ROM
typedef struct PortRomSegment {
    uint32_t gameStart;
    uint32_t gameEnd;
    uint32_t romStart;
    uint32_t romEnd;
} PortRomSegment;

// in the fragment the Rev 2 ROM keeps at fragmentRom (0: the main segment), this ROM's link address romAddr is the
// Rev 2 link address gameAddr
typedef struct PortCodeMove {
    uint32_t fragmentRom;
    uint32_t romAddr;
    uint32_t gameAddr;
} PortCodeMove;

typedef struct PortRomLayout {
    PortRomVersion version;
    const PortRomSegment* segments; // sorted by gameStart; NULL: the ROM is laid out like Rev 2
    uint32_t segmentCount;
    const PortCodeMove* codeMoves; // sorted by fragmentRom, then romAddr
    uint32_t codeMoveCount;
} PortRomLayout;

extern const PortRomLayout gPortRomLayouts[];
extern const unsigned gPortRomLayoutCount;

#ifdef __cplusplus
}
#endif

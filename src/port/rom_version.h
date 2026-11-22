// Which Stadium ROM is loaded, and how the port reads it. The game code is US Rev 2 (1.2) for every ROM: its ROM
// offsets (link-time constants, literals) are Rev 2 offsets, mapped here onto the loaded ROM. See rom_version.cpp.
#pragma once

#include "rom_layouts.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace Port {

// Sets the layout used to read the loaded ROM. Any version not listed (PORT_ANY_ROM) is read as a Rev 2 layout.
void SelectRomVersion(PortRomVersion version);
PortRomVersion LoadedRomVersion();

} // namespace Port

extern "C" {
#endif

// The loaded ROM's offset for a Rev 2 ROM offset.
uint32_t Port_RomOffset(uint32_t gameOffset);
// Copies size bytes of the Rev 2 ROM range starting at gameOffset out of the loaded ROM (piece by piece where the
// range crosses pieces that moved by different amounts). Returns the bytes copied.
size_t Port_RomRead(uint32_t gameOffset, void* dst, size_t size);
// A link address read from the loaded ROM's code (a fragment image: jump targets) as the Rev 2 link address the
// port's tables use. fragmentRom is the Rev 2 ROM offset of the fragment the address is in (0: main segment).
uint32_t Port_RomCodeAddrToGame(uint32_t fragmentRom, uint32_t addr);

#ifdef __cplusplus
}
#endif

// Restored content: Gallery, Nidoqueen's animation and Jynx's colour from other Stadium releases the player also
// provides (restored_content.cpp).
#pragma once

#include "rom_layouts.h"

#include <stdint.h>

// Port_Enh_JynxColor: the loaded ROM's own, or Original (USA 1.0, Japan) or Purple (later releases)
#define PORT_JYNX_ROM 0
#define PORT_JYNX_ORIGINAL 1
#define PORT_JYNX_PURPLE 2
// Port_Enh_NidoqueenAnimation: the loaded ROM's own, the international one or the Japanese one
#define PORT_NIDOQUEEN_ROM 0
#define PORT_NIDOQUEEN_INTERNATIONAL 1
#define PORT_NIDOQUEEN_JAPANESE 2

#ifdef __cplusplus
#include <vector>

namespace Port {

// Reads the extra ROMs (gLauncher.ExtraRom1..3, PORT_EXTRA_ROMS) and sets up what they restore. After the loaded ROM
// is selected (SelectRomVersion), before the game starts.
void LoadRestoredContent();
// The version of the extra ROM in slot (PORT_ROM_VERSION_COUNT: none)
PortRomVersion ExtraRomVersion(int slot);

// What the launcher offers for a ROM and the extra ROMs beside it
struct RestoredContentChoices {
    bool gallery = true;    // the Gallery can be entered
    bool jynx = false;      // both of Jynx's colours are there
    bool jynxOriginalDefault = false; // the loaded ROM's own colour is Original
    bool nidoqueen = false; // the Japanese animation is there
};
RestoredContentChoices RestoredChoicesFor(PortRomVersion loaded, const std::vector<PortRomVersion>& extras);

} // namespace Port

extern "C" {
#endif

int Port_Enh_JynxColor(void);
void Port_Enh_SetJynxColor(int color);
int Port_Enh_NidoqueenAnimation(void);
void Port_Enh_SetNidoqueenAnimation(int animation);
// A file of an archive read from the ROM (archiveRaw: the archive's start as Port_RomToRaw marks it, index: the file)
// that restored content takes from another ROM: sets *start (a marked offset) and *size and returns 1, else 0.
int Port_RestoredArchiveFile(uint32_t archiveRaw, int index, uint32_t* start, uint32_t* size);

#ifdef __cplusplus
}
#endif

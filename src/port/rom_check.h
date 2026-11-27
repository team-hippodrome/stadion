// The Stadium ROMs the port runs with: the original USA and European cartridge dumps, checked by SHA-1
// (rom_check.cpp). The port's code is Rev 2's, and rom_version.cpp maps its ROM offsets onto each of these from the
// decomp's link maps of exactly these dumps; any other dump (another region, a hack or translation, a trimmed or
// damaged file) would start but read its data from the wrong places and crash later.
#pragma once

#include "rom_layouts.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Port {

// The accepted dumps: the USA (1.0, Rev 1, Rev 2) and European (English 1.0 and Rev 1, French, German, Spanish,
// Italian) cartridges (32 MiB, big-endian .z64), as the decomp's matching builds. The Rev 2 kiosk cartridge is the
// same dump as the retail Rev 2.
struct KnownRom {
    const char* sha1;
    PortRomVersion version;
    const char* name;
};
constexpr KnownRom kKnownRoms[] = {
    { "ed7bef5a306f88c0a6e96b15e71fee2ef32058f3", PORT_ROM_US, "USA 1.0" },
    { "488bb6d112427823d77fcecdd9297fd86875263d", PORT_ROM_US_REV1, "USA Rev 1" },
    { "0d3b1d740c6ee6da6923550bf24ad827262fd8c0", PORT_ROM_US_REV2, "USA Rev 2" },
    { "6f26e3c8b35c3232058375e30d19665facb23123", PORT_ROM_EU, "Europe 1.0" },
    { "9470f899ad0107677b7dfd24e1dd567723a5f84f", PORT_ROM_EU_REV1, "Europe Rev 1" },
    { "4f7267ccaba4a8a9b0ae2cb89e191bc69b153ddc", PORT_ROM_FR, "France" },
    { "6a25903e733f071e7d6ea54529b7fd60b760cda3", PORT_ROM_DE, "Germany" },
    { "f39911ad0bedccc3e44d4338507c73bfa97856bf", PORT_ROM_ES, "Spain" },
    { "db5f2797c377fa8957b97465fc5738d6999ff240", PORT_ROM_IT, "Italy" },
};
// Accepted only as extra ROMs (restored content, rom_version.h), not as the ROM the game runs: the port cannot run
// their code yet.
constexpr KnownRom kExtraOnlyRoms[] = {
    { "074582744c71d7f0569ce960964a7bbe947efd3c", PORT_ROM_JP, "Japan" },
};
constexpr size_t kRomSize = 32 * 1024 * 1024;

// The version of a ROM by its SHA-1, or PORT_ROM_VERSION_COUNT when it is not one of the accepted dumps.
PortRomVersion RomVersionOfSha1(const std::string& sha1);
// The same for an extra ROM (rom_version.h), which may also be one of kExtraOnlyRoms
PortRomVersion ExtraRomVersionOfSha1(const std::string& sha1);
// "USA 1.0" etc., for messages
const char* RomVersionName(PortRomVersion version);
// The accepted SHA-1s, one per line, for messages
std::string KnownRomList();

// lowercase hex SHA-1 of a buffer, or of a file ("" when the file cannot be read)
std::string Sha1Hex(const uint8_t* data, size_t size);
std::string Sha1HexFile(const std::string& path);

// "" when the ROM is one of the accepted dumps, else a short reason. PORT_ANY_ROM=1 accepts any ROM (debugging; one
// that is not an accepted dump is read as laid out like Rev 2).
std::string CheckRomDump(const uint8_t* data, size_t size);
// version, when given: the file's version, or PORT_ROM_VERSION_COUNT
std::string CheckRomDumpFile(const std::string& path, PortRomVersion* version = nullptr);

} // namespace Port

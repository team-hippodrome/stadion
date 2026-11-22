// The Stadium ROMs the port runs with: the original USA cartridge dumps (1.0, Rev 1, Rev 2), checked by SHA-1
// (rom_check.cpp). The port's code is Rev 2's, and rom_version.cpp maps its ROM offsets onto each of these from the
// decomp's link maps of exactly these dumps; any other dump (another region, a hack or translation, a trimmed or
// damaged file) would start but read its data from the wrong places and crash later.
#pragma once

#include "rom_layouts.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Port {

// The accepted dumps: "Pokemon Stadium (USA)[ (Rev 1)| (Rev 2)].z64" (32 MiB, big-endian), as the decomp's matching
// builds. The Rev 2 kiosk cartridge is the same dump as the retail Rev 2.
struct KnownRom {
    const char* sha1;
    PortRomVersion version;
    const char* name;
};
constexpr KnownRom kKnownRoms[] = {
    { "ed7bef5a306f88c0a6e96b15e71fee2ef32058f3", PORT_ROM_US, "USA 1.0" },
    { "488bb6d112427823d77fcecdd9297fd86875263d", PORT_ROM_US_REV1, "USA Rev 1" },
    { "0d3b1d740c6ee6da6923550bf24ad827262fd8c0", PORT_ROM_US_REV2, "USA Rev 2" },
};
constexpr size_t kRomSize = 32 * 1024 * 1024;

// The version of a ROM by its SHA-1, or PORT_ROM_VERSION_COUNT when it is not one of the accepted dumps.
PortRomVersion RomVersionOfSha1(const std::string& sha1);
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

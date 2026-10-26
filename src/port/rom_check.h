// The Stadium ROM the port runs with: the original USA cartridge dump, checked by SHA-1 (rom_check.cpp). The port's
// tables (src/port/generated) come from the decomp's link map of exactly that ROM; any other dump (a revision, a hack
// or translation, a trimmed or damaged file) starts but reads its data from the wrong places and crashes later.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Port {

// SHA-1 of the original "Pokemon Stadium (USA).z64" (32 MiB, big-endian), as the decomp's matching build
constexpr const char* kRomSha1 = "ed7bef5a306f88c0a6e96b15e71fee2ef32058f3";
constexpr size_t kRomSize = 32 * 1024 * 1024;

// lowercase hex SHA-1 of a buffer, or of a file ("" when the file cannot be read)
std::string Sha1Hex(const uint8_t* data, size_t size);
std::string Sha1HexFile(const std::string& path);

// "" when the ROM is the original dump, else a short reason. PORT_ANY_ROM=1 accepts any ROM (debugging).
std::string CheckRomDump(const uint8_t* data, size_t size);
std::string CheckRomDumpFile(const std::string& path);

} // namespace Port

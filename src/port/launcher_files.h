#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace Port {
std::string PathUtf8(const std::filesystem::path& path);
std::string ResolveLauncherPath(const std::string& path, const std::filesystem::path& directory);
std::string StoreLauncherPath(const std::string& path, const std::filesystem::path& directory);

// Accept raw 128 KiB Stadium FlashRAM, preserving invalid records for the game's own backup repair.
bool NormalizeStadiumSave(const std::vector<uint8_t>& source, std::vector<uint8_t>& normalized,
                          std::string& error);
// Import a separate working copy; never overwrite the source or an earlier import.
bool ImportStadiumSave(const std::filesystem::path& source, const std::filesystem::path& directory,
                       std::string& imported, std::string& error);
} // namespace Port

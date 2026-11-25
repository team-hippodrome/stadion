#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Port {
struct LauncherSlot { bool enabled = false; std::string rom, save; };
struct LauncherCheck { std::vector<std::string> errors, warnings; };
// Why the US Stadium will not use the cartridge whose ROM header (0x150 bytes or more) this is; empty when it will.
std::string GameBoyCartridgeNotice(const uint8_t* rom);
LauncherCheck CheckLauncherFiles(const std::filesystem::path& directory, const std::string& save,
                                 const std::array<LauncherSlot, 4>& slots);
// Preserve the previous on-disk save once per process/session, then replace atomically.
// At most five previous session snapshots are retained beside each save.
bool WriteProtectedSave(const std::filesystem::path& path, const uint8_t* data, size_t size, std::string& error);
bool ExportStadiumSave(const std::filesystem::path& source, const std::filesystem::path& directory,
                        std::string& exported, std::string& error);
// Before the config is loaded: an empty or unreadable config is renamed to <config>.broken and replaced by
// <config>.bak when that holds a JSON object (else the game starts with default settings); a good config is copied
// to <config>.bak. Returns what was done for the log, empty when the config was fine or absent.
std::string RecoverConfigFile(const std::filesystem::path& path);
void ReportSaveFailure(const std::string& error);
std::string TakeSaveFailure();
std::string RedactDiagnosticLog(const std::string& text);
bool ExportLauncherDiagnostics(const std::filesystem::path& directory, const std::string& build,
                                const std::string& requested, const std::string& active,
                                std::string& exported, std::string& error);
} // namespace Port

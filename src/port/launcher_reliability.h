#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Port {
struct LauncherSlot { bool enabled = false; std::string rom, save; };
struct LauncherCheck { std::vector<std::string> errors, warnings; };
LauncherCheck CheckLauncherFiles(const std::filesystem::path& directory, const std::string& save,
                                 const std::array<LauncherSlot, 4>& slots);
// Preserve the previous on-disk save once per process/session, then replace atomically.
// At most five previous session snapshots are retained beside each save.
bool WriteProtectedSave(const std::filesystem::path& path, const uint8_t* data, size_t size, std::string& error);
bool ExportStadiumSave(const std::filesystem::path& source, const std::filesystem::path& directory,
                        std::string& exported, std::string& error);
void ReportSaveFailure(const std::string& error);
std::string TakeSaveFailure();
std::string RedactDiagnosticLog(const std::string& text);
bool ExportLauncherDiagnostics(const std::filesystem::path& directory, const std::string& build,
                                const std::string& requested, const std::string& active,
                                std::string& exported, std::string& error);
} // namespace Port

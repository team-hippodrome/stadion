#include "launcher_reliability.h"
#include "launcher_files.h"
#include "file_io.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <cctype>
#include <set>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace Port {
namespace {
std::atomic<unsigned> serial{0};
std::mutex saveMutex, failureMutex;
std::string saveFailure;
std::set<fs::path> backedUp;
std::string Unique() {
    return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(serial++);
}
bool Read(const fs::path& p, std::vector<uint8_t>& data, size_t limit) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || in.tellg() > static_cast<std::streamoff>(limit)) return false;
    data.resize(static_cast<size_t>(in.tellg())); in.seekg(0);
    return data.empty() || bool(in.read(reinterpret_cast<char*>(data.data()), data.size()));
}
bool WriteNew(const fs::path& p, const uint8_t* data, size_t size, std::string& error) {
    FILE* out = OpenFileUtf8(PathUtf8(p).c_str(), "wbx");
    if (!out) { error = "Cannot create file: " + PathUtf8(p); return false; }
    bool ok = (size == 0 || fwrite(data, 1, size, out) == size) && fflush(out) == 0;
#ifdef _WIN32
    if (ok) ok = _commit(_fileno(out)) == 0;
#else
    if (ok) ok = fsync(fileno(out)) == 0;
#endif
    if (fclose(out) != 0) ok = false;
    if (!ok) { std::error_code ec; fs::remove(p, ec); error = "Could not finish writing: " + PathUtf8(p); }
    return ok;
}
bool Writable(const fs::path& target, std::string& error) {
    std::error_code ec;
    auto parent = target.parent_path(); if (parent.empty()) parent = ".";
    fs::create_directories(parent, ec);
    if (ec) { error = "Cannot create folder: " + PathUtf8(parent); return false; }
    if (fs::exists(target, ec)) {
        FILE* f = OpenFileUtf8(PathUtf8(target).c_str(), "r+b");
        if (!f || !fs::is_regular_file(target, ec)) {
            if (f) fclose(f);
            error = "File is not writable: " + PathUtf8(target); return false;
        }
        fclose(f);
    }
    auto probe = parent / fs::u8path(".stadium-write-check-" + Unique());
    if (!WriteNew(probe, nullptr, 0, error)) return false;
    fs::remove(probe, ec);
    if (ec) { error = "Cannot remove temporary file in: " + PathUtf8(parent); return false; }
    return true;
}
}

LauncherCheck CheckLauncherFiles(const fs::path& directory, const std::string& save,
                                 const std::array<LauncherSlot, 4>& slots) {
    LauncherCheck result;
    std::string error;
    if (!Writable(directory / "pokestadium.cfg.json", error)) result.errors.push_back("Settings: " + error);
    auto checkSave = [&](const std::string& path, const std::string& label, bool stadium, size_t size) {
        if (path.empty()) { result.errors.push_back(label + "Choose a save location."); return; }
        const auto p = fs::u8path(path);
        if (!Writable(p, error)) result.errors.push_back(label + error);
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            if (!stadium) result.warnings.push_back(label + "No battery save yet; a new game will start. Transfer Pak features require saved progress.");
            return;
        }
        std::vector<uint8_t> bytes, normalized;
        if (!Read(p, bytes, size) || bytes.size() != size) {
            result.errors.push_back(label + "Save has the wrong size or cannot be read: " + path); return;
        }
        if (stadium && (!NormalizeStadiumSave(bytes, normalized, error) || bytes != normalized))
            result.errors.push_back(label + "Invalid or byte-swapped cartridge save. Use Import save to select a working copy. " + path);
        if (!stadium && size == 0x8000) {
            unsigned sum = 0; for (size_t i = 0x2598; i <= 0x3522; ++i) sum += bytes[i];
            if (static_cast<uint8_t>(~sum) != bytes[0x3523])
                result.warnings.push_back(label + "Trainer checksum is not valid; the save may be blank, damaged or incompatible with this cartridge.");
        }
    };
    checkSave(save, "Stadium save: ", true, 0x20000);
    std::vector<fs::path> selected;
    if (!save.empty()) selected.push_back(fs::absolute(fs::u8path(save)).lexically_normal());
    for (size_t i = 0; i < slots.size(); i++) {
        const auto& slot = slots[i]; if (!slot.enabled) continue;
        const auto label = "Player " + std::to_string(i + 1) + ": ";
        std::vector<uint8_t> rom;
        if (slot.rom.empty() || !Read(fs::u8path(slot.rom), rom, 8 * 1024 * 1024) || rom.size() < 0x8000) {
            result.errors.push_back(label + "Choose a readable Game Boy ROM."); continue;
        }
        uint8_t checksum = 0; for (size_t j = 0x134; j <= 0x14c; j++) checksum = checksum - rom[j] - 1;
        if (checksum != rom[0x14d] || rom[0x148] > 8 || rom.size() != (size_t(0x8000) << rom[0x148])) {
            result.errors.push_back(label + "Game Boy ROM header or size is invalid. Choose a complete cartridge dump."); continue;
        }
        std::string title(reinterpret_cast<const char*>(rom.data() + 0x134), 16);
        if (title.find("POKEMON RED") != 0 && title.find("POKEMON BLUE") != 0 && title.find("POKEMON YEL") != 0)
            result.warnings.push_back(label + "This cartridge is not a recognized Red, Blue or Yellow release; Stadium compatibility is not assured.");
        static const size_t sizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
        const size_t size = rom[0x149] < 6 ? sizes[rom[0x149]] : 0;
        if (size == 0) { result.errors.push_back(label + "Cartridge has no supported battery RAM."); continue; }
        checkSave(slot.save, label, false, size);
        if (!slot.save.empty()) {
            const auto battery = fs::absolute(fs::u8path(slot.save)).lexically_normal();
            for (const auto& other : selected) {
                std::error_code sameError;
                if (battery == other || fs::equivalent(battery, other, sameError)) {
                    result.errors.push_back(label + "Two active cartridges share the same save file. Choose separate saves."); break;
                }
            }
            selected.push_back(battery);
        }
        std::error_code ec;
        if (!slot.save.empty() && fs::equivalent(fs::u8path(slot.rom), fs::u8path(slot.save), ec))
            result.errors.push_back(label + "The save points to the ROM itself. Choose a separate .sav file.");
    }
    return result;
}

bool WriteProtectedSave(const fs::path& source, const uint8_t* data, size_t size, std::string& error) {
    std::lock_guard<std::mutex> guard(saveMutex);
    const auto path = fs::absolute(source).lexically_normal();
    const auto temporary = path.parent_path() / fs::u8path(".stadium-save-" + Unique());
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) { error = "Cannot create save folder: " + ec.message(); return false; }
    if (!WriteNew(temporary, data, size, error)) return false;
    if (!backedUp.count(path) && fs::exists(path, ec)) {
        const auto backups = fs::u8path(PathUtf8(path) + ".backups");
        fs::create_directories(backups, ec);
        if (!ec) fs::copy_file(path, backups / fs::u8path(Unique() + ".bak"), fs::copy_options::none, ec);
        if (ec) { fs::remove(temporary); error = "Cannot back up the existing save; it was kept unchanged: " + ec.message(); return false; }
        std::vector<fs::path> entries;
        for (fs::directory_iterator it(backups, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file() && it->path().extension() == ".bak") entries.push_back(it->path());
        std::sort(entries.begin(), entries.end());
        if (!ec) {
            while (entries.size() > 5) {
                fs::remove(entries.front(), ec);
                if (ec) break;
                entries.erase(entries.begin());
            }
        }
        if (ec) {
            std::error_code cleanup; fs::remove(temporary, cleanup);
            error = "Cannot maintain save backups; the active file was kept unchanged: " + ec.message(); return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec); error = "Could not replace save; the previous file was preserved."; return false;
    }
#else
    fs::rename(temporary, path, ec);
    if (ec) { fs::remove(temporary); error = "Could not replace save; the previous file was preserved."; return false; }
#endif
    backedUp.insert(path);
    return true;
}

bool ExportStadiumSave(const fs::path& source, const fs::path& directory, std::string& exported, std::string& error) {
    std::vector<uint8_t> data, normalized;
    if (!Read(source, data, 0x20000) || !NormalizeStadiumSave(data, normalized, error)) {
        if (error.empty()) error = "No readable Stadium cartridge save to export yet."; return false;
    }
    const auto folder = directory / "exports";
    std::error_code ec; fs::create_directories(folder, ec);
    if (ec) { error = "Cannot create exports folder: " + ec.message(); return false; }
    const auto path = folder / fs::u8path("stadium-" + Unique() + ".fla");
    if (!WriteNew(path, normalized.data(), normalized.size(), error)) return false;
    exported = PathUtf8(path); return true;
}

void ReportSaveFailure(const std::string& error) { std::lock_guard<std::mutex> guard(failureMutex); if (saveFailure.empty()) saveFailure = error; }
std::string TakeSaveFailure() { std::lock_guard<std::mutex> guard(failureMutex); auto error = saveFailure; saveFailure.clear(); return error; }

std::string RedactDiagnosticLog(const std::string& text) {
    // Conservative and bounded: never copy a path or game filename into a report.
    std::istringstream lines(text); std::string line, result;
    while (std::getline(lines, line)) {
        line = line.substr(0, 1024);
        auto slash = line.find_first_of("/\\");
        if (slash != std::string::npos) {
            size_t begin = slash;
            while (begin > 0 && !std::isspace(static_cast<unsigned char>(line[begin - 1]))) --begin;
            line = line.substr(0, begin) + "[path redacted]";
        }
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        // Android's environment-file loader can log KEY=value. Export neither assignments nor credentials.
        if (line.find('=') != std::string::npos || lower.find("port_env") != std::string::npos ||
            lower.find("password") != std::string::npos || lower.find("secret") != std::string::npos ||
            lower.find("authorization") != std::string::npos || lower.find("token") != std::string::npos ||
            lower.find("bearer") != std::string::npos) {
            line = "[environment or credential log line redacted]";
        }
        for (const auto* extension : {".z64", ".n64", ".v64", ".gb", ".sav", ".flash", ".fla", ".sra"}) {
            if (lower.find(extension) != std::string::npos) { line = "[game file log line redacted]"; break; }
        }
        result += line + "\n";
    }
    return result;
}

bool ExportLauncherDiagnostics(const fs::path& directory, const std::string& build,
                                const std::string& requested, const std::string& active,
                                std::string& exported, std::string& error) {
    std::string report = "Pokemon Stadium launcher diagnostics\nBuild: " + build + "\nRequested renderer: " + requested +
        "\nActive renderer: " + active + "\nNo ROM, save, config contents or environment variables included.\n";
    for (const auto& relative : {"logs/pokestadium.log", "saves/last_run.log"}) {
        std::ifstream log(directory / relative, std::ios::binary | std::ios::ate);
        if (!log) continue;
        const auto size = log.tellg(); if (size <= 0) continue;
        log.seekg(std::max(std::streamoff(0), static_cast<std::streamoff>(size) - 16384));
        std::string tail((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
        if (size > 16384) { const auto newline = tail.find('\n'); tail = newline == std::string::npos ? "" : tail.substr(newline + 1); }
        report += "\nRecent log (paths redacted):\n" + RedactDiagnosticLog(tail) + "\n";
    }
    const auto folder = directory / "diagnostics";
    std::error_code ec; fs::create_directories(folder, ec);
    if (ec) { error = "Cannot create diagnostics folder: " + ec.message(); return false; }
    const auto path = folder / fs::u8path("launcher-" + Unique() + ".txt");
    if (!WriteNew(path, reinterpret_cast<const uint8_t*>(report.data()), report.size(), error)) return false;
    exported = PathUtf8(path); return true;
}
} // namespace Port

#include "launcher_files.h"
#include "file_io.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cerrno>

namespace fs = std::filesystem;

namespace Port {
std::string PathUtf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::string ResolveLauncherPath(const std::string& path, const fs::path& directory) {
    if (path.empty()) return "";
    const auto value = fs::u8path(path);
    return PathUtf8((value.is_absolute() ? value : directory / value).lexically_normal());
}

std::string StoreLauncherPath(const std::string& path, const fs::path& directory) {
    if (path.empty()) return "";
    const auto value = fs::u8path(path).lexically_normal();
    const auto relative = value.lexically_relative(directory.lexically_normal());
    if (!relative.empty() && *relative.begin() != "..") {
        const auto stored = relative.generic_u8string();
        return {stored.begin(), stored.end()};
    }
    return PathUtf8(value);
}

namespace {
constexpr size_t kFlashSize = 0x20000;

int ValidRecords(const std::vector<uint8_t>& data) {
    int valid = 0;
    auto check = [&](size_t start, size_t size) {
        for (size_t copy : {size_t(0), size_t(0x10000)}) {
            const size_t end = start + copy + size;
            if (memcmp(data.data() + end - 6, "POKE", 4) != 0) continue;
            uint16_t sum = 0;
            for (size_t i = start + copy; i < end - 2; i++) sum += data[i];
            if (sum == ((data[end - 2] << 8) | data[end - 1])) valid++;
        }
    };
    // Save banks and record sizes: //@game/src/save_data.c (Save_RecordIsValid and bank record tables).
    for (size_t bank = 0; bank < 2; bank++)
        for (size_t slot = 0; slot < 40; slot++) check(bank * 0x4000 + slot * 0x160, 0x160);
    for (size_t slot = 0; slot < 10; slot++) check(0x8000 + slot * 0x160, 0x160);
    check(0x8dc0, 0xf60);
    check(0x9d20, 0x180);
    check(0x9ea0, 0x28);
    check(0x9ec8, 0x13b8);
    check(0xb280, 0xb84);
    for (size_t slot = 0; slot < 12; slot++) check(0xc000 + slot * 0x468, 0x468);
    for (size_t slot = 0; slot < 4; slot++) check(0xf4e0 + slot * 0xe0, 0xe0);
    return valid;
}
} // namespace

bool NormalizeStadiumSave(const std::vector<uint8_t>& source, std::vector<uint8_t>& normalized,
                          std::string& error) {
    normalized.clear();
    error.clear();
    if (source.size() != kFlashSize) {
        error = "Choose a raw 128 KiB Pokémon Stadium (USA) cartridge save, not a save state or Controller Pak file.";
        return false;
    }
    if (std::all_of(source.begin(), source.end(), [](uint8_t b) { return b == 0xff; })) {
        normalized = source;
        return true;
    }
    int best = 0;
    bool ambiguous = false;
    for (size_t order : {size_t(0), size_t(1), size_t(2), size_t(3)}) {
        std::vector<uint8_t> candidate(kFlashSize);
        for (size_t i = 0; i < kFlashSize; i++) candidate[i] = source[i ^ order];
        const int valid = ValidRecords(candidate);
        if (valid > best) {
            best = valid;
            normalized = std::move(candidate);
            ambiguous = false;
        } else if (valid == best && valid > 0 && candidate != normalized) {
            ambiguous = true;
        }
    }
    if (best == 0 || ambiguous) {
        normalized.clear();
        error = "No unambiguous valid Stadium save records found. The file may be damaged, a save state, or for another game.";
        return false;
    }
    return true;
}

bool ImportStadiumSave(const fs::path& source, const fs::path& directory,
                       std::string& imported, std::string& error) {
    imported.clear();
    std::ifstream file(source, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != static_cast<std::streamoff>(kFlashSize)) {
        error = "Cannot read a raw 128 KiB Stadium cartridge save from that file.";
        return false;
    }
    std::vector<uint8_t> data(kFlashSize), normalized;
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), data.size())) {
        error = "Could not read the complete save file.";
        return false;
    }
    if (!NormalizeStadiumSave(data, normalized, error)) return false;
    const auto saves = directory / "saves";
    std::error_code ec;
    fs::create_directories(saves, ec);
    if (ec) {
        error = "Cannot create the saves folder beside the launcher: " + ec.message();
        return false;
    }
    for (unsigned n = 1; n <= 10000; n++) {
        std::string name = PathUtf8(source.stem()) + "-imported" + (n == 1 ? "" : "-" + std::to_string(n)) + ".flash";
        const auto destination = saves / fs::u8path(name);
        FILE* out = OpenFileUtf8(PathUtf8(destination).c_str(), "wbx");
        if (out == nullptr) {
            if (errno == EEXIST) continue;
            error = "Cannot write the imported save beside the launcher.";
            return false;
        }
        const bool written = fwrite(normalized.data(), 1, normalized.size(), out) == normalized.size();
        const bool closed = fclose(out) == 0;
        if (!written || !closed) {
            fs::remove(destination, ec);
            error = "Could not finish writing the imported save.";
            return false;
        }
        imported = PathUtf8(destination);
        return true;
    }
    error = "Too many imports with this name. Choose another save filename.";
    return false;
}
} // namespace Port

// Standalone regression: compile with src/port/rom_check.cpp and -Isrc/port
// using C++17, then run natively or run the Windows build under Wine.
#include "rom_check.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <chrono>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("stadium-rom-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const char* contents = "Unicode ROM path regression\n";
    try {
        for (const char* relative : { "plain/Stadium.z64", u8"José/桌面/Pokémon.z64" }) {
            const auto file = root / fs::u8path(relative);
            fs::create_directories(file.parent_path());
            { std::ofstream out(file, std::ios::binary); out << contents; }
            const std::string path = file.u8string();
            if (Port::Sha1HexFile(path) != "693409023e7746e910500e61f1a6df47209d7be8" ||
                Port::CheckRomDumpFile(path) != "Not the original dump") {
                throw std::runtime_error("ROM checker could not read the UTF-8 path");
            }
        }
        if (Port::CheckRomDumpFile((root / "missing.z64").u8string()) != "Cannot open the file") {
            throw std::runtime_error("Missing ROM was not rejected");
        }
        fs::remove_all(root);
        std::puts("PASS: ASCII and accented/CJK paths readable; invalid and missing ROMs rejected");
        return 0;
    } catch (const std::exception& e) {
        fs::remove_all(root);
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}

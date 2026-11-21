// Exercises the disk cache without creating a renderer or GPU context.
#include "fast/backends/gfx_shader_cache.h"
#include "ship/core/Context.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
std::string scratch;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

// The cache's only application dependency. Keep test files in the supplied empty directory.
std::string Ship::Context::GetPathRelativeToAppDirectory(const std::string& path, const std::string&) {
    return scratch + "/" + path;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: shader_cache_check <empty scratch directory>\n");
        return 2;
    }
    scratch = argv[1];
    try {
        Require(std::filesystem::is_directory(scratch) && std::filesystem::is_empty(scratch),
                "scratch directory must exist and be empty");
        using Fast::ShaderDiskCache;
        const uint64_t key = ShaderDiskCache::Hash({"source", "options"});
        Require(key != ShaderDiskCache::Hash({"sourceoptions"}), "part lengths must affect the key");
        ShaderDiskCache cache;
        cache.Open("test");
        const std::string original = "compiled shader";
        cache.Store(key, original.data(), original.size());
        Require(cache.Load(key) == std::vector<char>(original.begin(), original.end()), "round trip failed");
        const auto directory = std::filesystem::path(scratch) / "shadercache/test";
        const auto file = std::filesystem::directory_iterator(directory)->path();
        {
            std::fstream stream(file, std::ios::in | std::ios::out | std::ios::binary);
            stream.seekp(-1, std::ios::end);
            stream.put('x');
        }
        Require(cache.Load(key).empty(), "corrupted payload was accepted");
        cache.Store(key, original.data(), original.size());
        Require(!cache.Load(key).empty(), "corrupted entry could not be replaced");
        std::filesystem::resize_file(file, 2);
        Require(cache.Load(key).empty(), "truncated entry was accepted");
        cache.Store(key, original.data(), original.size());
        std::filesystem::resize_file(file, 64 * 1024 * 1024);
        Require(cache.Load(key).empty(), "oversized entry was accepted");
        std::vector<std::thread> writers;
        for (int i = 0; i < 16; ++i) {
            writers.emplace_back([&, i] {
                const std::string data(1024, static_cast<char>('a' + i));
                for (int j = 0; j < 10; ++j) cache.Store(key, data.data(), data.size());
            });
        }
        for (auto& thread : writers) thread.join();
        const auto data = cache.Load(key);
        Require(data.size() == 1024, "concurrent write produced an incomplete entry");
        Require(std::all_of(data.begin(), data.end(), [&](char value) { return value == data.front(); }),
                "concurrent writers mixed their payloads");
        Require(std::distance(std::filesystem::directory_iterator(directory),
                              std::filesystem::directory_iterator{}) == 1, "temporary files were left behind");
#ifdef _WIN32
        _putenv_s("PORT_NO_SHADER_DISK_CACHE", "1");
#else
        setenv("PORT_NO_SHADER_DISK_CACHE", "1", 1);
#endif
        cache.Open("disabled");
        cache.Store(key, original.data(), original.size());
        Require(cache.Load(key).empty(), "disabled cache returned data");
#ifdef _WIN32
        _putenv_s("PORT_NO_SHADER_DISK_CACHE", "");
#else
        unsetenv("PORT_NO_SHADER_DISK_CACHE");
#endif
        const auto blocked = std::filesystem::path(scratch) / "blocked";
        std::ofstream(blocked).put('x');
        scratch = blocked.string();
        cache.Open("unwritable");
        cache.Store(key, original.data(), original.size());
        Require(cache.Load(key).empty(), "unwritable cache returned data");
        puts("Shader cache checks passed.");
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "Shader cache check failed: %s\n", error.what());
        return 1;
    }
}

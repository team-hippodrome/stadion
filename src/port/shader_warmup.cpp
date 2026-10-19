// Shader warm-up. Fast3D builds a shader program the first time a combiner/state combination is drawn, and that draw
// waits for it: 1-4 ms with a warm driver cache, 100+ ms on a fresh install or after an update (macOS keeps a Metal
// cache per binary), a visible hitch the first time each effect appears. So the programs are built ahead, while the
// launcher shows (or during the boot logos when Auto-play skips it): the list shipped in pokestadium.o2r
// (src/port/shader_warmup.txt, the programs the stored recordings draw: tools/shader_list.py) plus the ones this
// installation drew before (shaders.txt next to pokestadium.cfg.json, extended as new ones are drawn).
// PORT_NO_SHADER_WARMUP=1: no warm-up (still records). PORT_SHADER_LIST=<file>: record into that file instead.
// PORT_TRACE_SHADERS=1 (or PORT_TRACE_PACING=1): "[shader] <id0>/<id1> built|prebuilt in X ms" per first draw.
#include "Engine.h"

#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <libultraship/bridge/resourcebridge.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Port {
namespace {

using ShaderId = std::pair<uint64_t, uint64_t>;

std::set<ShaderId> sKnown; // render thread only (Record), after StartShaderWarmup
std::string sListPath;

// lines "<id0> <id1>" in hex; anything else is ignored
void Parse(const std::string& text, std::vector<ShaderId>& out) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        unsigned long long id0, id1;
        if (sscanf(line.c_str(), "%llx %llx", &id0, &id1) == 2 && sKnown.insert({ id0, id1 }).second) {
            out.push_back({ id0, id1 });
        }
    }
}

std::string ReadText(const std::string& path) {
    std::string text;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            text.append(buf, n);
        }
        fclose(f);
    }
    return text;
}

// a program drawn for the first time in this run (render thread)
void Record(uint64_t id0, uint64_t id1) {
    if (!sKnown.insert({ id0, id1 }).second) {
        return;
    }
    if (FILE* f = fopen(sListPath.c_str(), "a")) {
        fprintf(f, "%016" PRIx64 " %016" PRIx64 "\n", id0, id1);
        fclose(f);
    }
}

} // namespace

void StartShaderWarmup() {
    const char* listEnv = getenv("PORT_SHADER_LIST");
    sListPath = listEnv != nullptr ? listEnv : AppPath("shaders.txt");
    std::vector<ShaderId> ids;
    auto rm = ResourceGetResourceManager();
    if (rm != nullptr && rm->GetArchiveManager() != nullptr) {
        if (auto file = rm->GetArchiveManager()->LoadFile("port/shader_warmup.txt"); file && file->Buffer) {
            Parse(std::string(file->Buffer->begin(), file->Buffer->end()), ids);
        }
    }
    size_t shipped = ids.size();
    Parse(ReadText(sListPath), ids);
    Fast::gPortShaderCreated = Record;
    if (getenv("PORT_TRACE_SHADERS") || getenv("PORT_TRACE_PACING")) {
        fprintf(stderr, "[shader] warm-up: %zu programs (%zu shipped, %zu from %s)%s\n", ids.size(), shipped,
                ids.size() - shipped, sListPath.c_str(), getenv("PORT_NO_SHADER_WARMUP") ? ", off" : "");
    }
    if (getenv("PORT_NO_SHADER_WARMUP") == nullptr) {
        if (auto interpreter = GetWindow()->GetInterpreterWeak().lock()) {
            interpreter->QueueShaderWarmup(std::move(ids));
        }
    }
}

} // namespace Port

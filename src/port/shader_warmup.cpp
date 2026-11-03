// Shader warm-up. Fast3D builds a shader program the first time a combiner/state combination is drawn, and that draw
// waits for it: 1-4 ms with a warm driver cache, 100+ ms on a fresh install or after an update (macOS keeps a Metal
// cache per binary), a visible hitch the first time each effect appears. The programs are built before the intro.
// The queue uses the list shipped in pokestadium.o2r
// (src/port/shader_warmup.txt, the programs the stored recordings draw: tools/shader_list.py) plus the ones this
// installation drew before (shaders.txt next to pokestadium.cfg.json, extended as new ones are drawn).
// PORT_NO_SHADER_WARMUP=1: no warm-up (still records). PORT_SHADER_LIST=<file>: record into that file instead.
// PORT_TRACE_SHADERS=1 (or PORT_TRACE_PACING=1): "[shader] <id0>/<id1> built|prebuilt in X ms" per first draw.
#include "Engine.h"

#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <SDL3/SDL.h>
#include <libultraship/libultraship.h>
#include <libultraship/bridge/resourcebridge.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include <algorithm>
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

std::set<ShaderId> sKnown; // Render thread only.
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

class ShaderWarmupWindow : public Ship::GuiWindow {
  public:
    ShaderWarmupWindow() : GuiWindow("", true, "Shader warm-up") {}

    float progress = 0.0f;

    void Draw() override {
        auto* viewport = ImGui::GetMainViewport();
        auto* draw = ImGui::GetForegroundDrawList(viewport);
        const ImVec2 pos = viewport->Pos;
        const ImVec2 size = viewport->Size;
        const float width = std::min(320.0f, size.x * 0.7f);
        const float x = pos.x + (size.x - width) * 0.5f;
        const float y = pos.y + size.y * 0.5f;
        const char* label = "Preparing shaders";
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32_BLACK);
        draw->AddText(ImVec2(pos.x + (size.x - ImGui::CalcTextSize(label).x) * 0.5f, y - 24), IM_COL32_WHITE, label);
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + 8), IM_COL32(55, 55, 55, 255));
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width * progress, y + 8), IM_COL32(98, 162, 255, 255));
    }

  protected:
    void DrawElement() override {}
    void UpdateElement() override {}
};

std::shared_ptr<ShaderWarmupWindow> sScreen;

} // namespace

void BeginShaderWarmup() {
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
    const bool trace = getenv("PORT_TRACE_SHADERS") || getenv("PORT_TRACE_PACING");
    if (trace) {
        fprintf(stderr, "[shader] warm-up: %zu programs (%zu shipped, %zu from %s)%s\n", ids.size(), shipped,
                ids.size() - shipped, sListPath.c_str(), getenv("PORT_NO_SHADER_WARMUP") ? ", off" : "");
    }
    if (getenv("PORT_NO_SHADER_WARMUP")) return;

    auto window = GetWindow();
    auto interpreter = window->GetInterpreterWeak().lock();
    if (!interpreter) return;
    interpreter->QueueShaderWarmup(std::move(ids));
    if (interpreter->ShaderWarmupProgress() >= 1.0f) return;

    sScreen = std::make_shared<ShaderWarmupWindow>();
    sScreen->Init();
    window->GetGui()->AddGuiWindow(sScreen);
}

// True while warm-up still needs another frame.
bool PumpShaderWarmup() {
    if (!sScreen) return false;
    auto window = GetWindow();
    auto interpreter = window->GetInterpreterWeak().lock();
    Gfx empty[1];
    gSPEndDisplayList(&empty[0]);
    if (window->IsRunning() && interpreter) {
        window->HandleEvents();
        if (window->IsRunning()) {
            sScreen->progress = interpreter->ShaderWarmupProgress();
            const bool drawn = window->DrawAndRunGraphicsCommands(empty, {});
            if (!drawn || sScreen->progress < 1.0f) return true;
        }
    }
    window->GetGui()->RemoveGuiWindow(sScreen->GetName());
    sScreen.reset();
    if (window->IsRunning() && (getenv("PORT_TRACE_SHADERS") || getenv("PORT_TRACE_PACING"))) {
        fprintf(stderr, "[shader] startup warm-up complete; starting intro\n");
    }
    return false;
}

bool RunShaderWarmup() {
    BeginShaderWarmup();
    while (PumpShaderWarmup()) SDL_Delay(1);
    return GetWindow()->IsRunning();
}

} // namespace Port

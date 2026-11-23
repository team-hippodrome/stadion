// Recommended graphics settings (see graphics_advisor.h).
#include "graphics_advisor.h"
#include "Engine.h"

#include <SDL3/SDL.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <ship/config/Config.h>
#include <ship/core/Context.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace Port {

namespace {

// lower case, without the trademark marks drivers put in names ("Intel(R) Iris(R) Xe Graphics" -> "intel iris xe graphics")
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    for (const char* mark : { "(r)", "(tm)", "\xc2\xae", "\xe2\x84\xa2" }) {
        for (size_t i; (i = s.find(mark)) != std::string::npos;) s.erase(i, strlen(mark));
    }
    return s;
}
bool Has(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}
// the number right after 'after' in s ("adreno (tm) 650" -> 650), or -1
int NumberAfter(const std::string& s, const char* after) {
    size_t i = s.find(after);
    if (i == std::string::npos) return -1;
    i += strlen(after);
    while (i < s.size() && !std::isdigit((unsigned char)s[i])) {
        if (std::isalpha((unsigned char)s[i])) return -1;
        i++;
    }
    int n = 0, digits = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i]) && digits < 6) n = n * 10 + (s[i++] - '0'), digits++;
    return digits ? n : -1;
}

constexpr uint64_t kGiB = 1024ull * 1024 * 1024;

// Effective megapixels per picture a chip draws in about 12 ms (two pictures in a 33 ms game frame with room to spare,
// for Smooth motion). A picture's cost is its pixels times the anti-aliasing factor below.
// Measured on a quiet M5 MacBook Air with Metal (battle, Smooth motion on): 4.0 ms without anti-aliasing and 10.5 ms
// with 4x at 2560 x 1440, 8.5 and 17.0 ms at 3840 x 2160; about 1.05 ms per megapixel and a 4x factor of 2.0 to 2.6
// (2.4 here): a budget of 11. Other Apple
// generations scale with their graphics cores; the PC classes are set from that and from the Low-power preset, which an
// Intel HD 520 runs at full speed only without Smooth motion (2x, 854 x 480, no anti-aliasing: 0.41 at 30 pictures a
// second; 0.25 at 60 keeps that preset for it).
double Budget(const GpuFacts& gpu, GpuClass c) {
    const std::string n = Lower(gpu.name);
    const int apple = NumberAfter(n, "apple m");
    if (apple > 0) {
        const double base = apple >= 5 ? 11.0 : apple == 4 ? 9.5 : apple == 3 ? 7.5 : apple == 2 ? 6.0 : 4.5;
        return base * (Has(n, " ultra") ? 6.0 : Has(n, " max") ? 3.5 : Has(n, " pro") ? 1.8 : 1.0);
    }
    switch (c) {
        case kGpuVeryWeak: return 0.12;
        case kGpuWeak: return 0.25;
        case kGpuMedium: return 2.5;
        case kGpuStrong: return 8.0;
        case kGpuVeryStrong: return 18.0;
    }
    return 2.5;
}
// without Smooth motion half as many pictures are drawn; not quite twice the budget each, for headroom
constexpr double kNoSmoothBudget = 1.7;
double MsaaFactor(int msaa) {
    return msaa >= 8 ? 3.6 : msaa >= 4 ? 2.4 : msaa >= 2 ? 1.6 : 1.0;
}

struct Candidate {
    int mode, value, msaa;
    bool smooth;
    double pixels; // drawn per picture
};

// Graphics memory for the game's pictures: each frame buffer the game draws (about six) holds a colour picture, its
// multisampled twin and a resolved depth picture; one multisampled depth buffer is shared; Smooth motion keeps a copy;
// plus textures, shaders and the window's own buffers.
uint64_t MemoryFor(double pixels, int msaa, bool smooth) {
    const double perFb = 4 + (msaa > 1 ? 4.0 * msaa : 0) + 4;
    const double fbs = 6 + (smooth ? 1 : 0);
    return (uint64_t)(pixels * (perFb * fbs + 4.0 * std::max(1, msaa))) + 400ull * 1024 * 1024;
}
uint64_t MemoryAllowed(const GpuFacts& gpu) {
    if (gpu.memoryBytes > 0) return gpu.memoryBytes / 2;
    if (gpu.systemRamBytes > 0 && !gpu.discrete) return gpu.systemRamBytes / 4; // (shares the machine's memory)
    return 2 * kGiB;
}

} // namespace

const char* GpuClassName(GpuClass c) {
    switch (c) {
        case kGpuVeryWeak: return "very weak";
        case kGpuWeak: return "weak";
        case kGpuMedium: return "medium";
        case kGpuStrong: return "strong";
        case kGpuVeryStrong: return "very strong";
    }
    return "?";
}

GpuClass ClassifyGpu(const GpuFacts& gpu) {
    const std::string n = Lower(gpu.name + " " + gpu.vendor);
    // drawn by the processor
    if (Has(n, "llvmpipe") || Has(n, "softpipe") || Has(n, "swiftshader") || Has(n, "basic render") ||
        Has(n, "gdi generic") || Has(n, "software")) {
        return kGpuVeryWeak;
    }
    // Apple silicon (Metal's "Apple M2 Pro"; Apple's OpenGL names it the same)
    if (Has(n, "apple m") || Has(n, "apple a1") || Has(n, "apple a2")) {
        return Has(n, " pro") || Has(n, " max") || Has(n, " ultra") ? kGpuVeryStrong : kGpuStrong;
    }
    const bool intel = gpu.vendorId == 0x8086 || Has(n, "intel");
    const bool amd = gpu.vendorId == 0x1002 || Has(n, "amd") || Has(n, "radeon") || Has(n, "ati ");
    const bool nvidia = gpu.vendorId == 0x10DE || Has(n, "nvidia") || Has(n, "geforce") || Has(n, "quadro");
    if (intel) {
        if (gpu.discrete || Has(n, "arc a") || Has(n, "arc b")) return kGpuStrong; // Arc cards
        if (Has(n, "iris xe") || Has(n, "arc")) return kGpuMedium;               // 11th gen and later laptops
        return kGpuWeak;                                                          // HD, UHD, older Iris
    }
    if (nvidia) {
        if (Has(n, "rtx")) return kGpuVeryStrong;
        if (Has(n, " mx")) return kGpuMedium; // MX150..MX550 laptop chips
        if (Has(n, " gt ") || Has(n, "gt 7") || Has(n, "gt 10")) return kGpuWeak;
        return kGpuStrong;
    }
    if (amd) {
        // integrated Radeon (Ryzen APUs: "AMD Radeon(TM) Graphics", "Radeon Vega 8", 680M/780M)
        // (Vega 7/8 and later are several times an Intel HD 620; the R5/R7 of older APUs are not)
        if (gpu.integrated || Has(n, "vega") || Has(n, "radeon graphics") || Has(n, "m graphics") ||
            Has(n, "radeon r")) {
            return Has(n, "radeon r") ? kGpuWeak : kGpuMedium;
        }
        return gpu.memoryBytes >= 6 * kGiB ? kGpuVeryStrong : kGpuStrong;
    }
    // phones and tablets (Android's OpenGL ES renderer names)
    if (Has(n, "adreno")) {
        const int model = NumberAfter(n, "adreno");
        return model >= 700 ? kGpuMedium : model >= 600 ? kGpuWeak : kGpuVeryWeak;
    }
    if (Has(n, "immortalis") || Has(n, "xclipse")) return kGpuMedium;
    if (Has(n, "mali")) return Has(n, "mali-g7") || Has(n, "mali-g6") ? kGpuWeak : kGpuVeryWeak;
    if (Has(n, "powervr")) return kGpuVeryWeak;
    // unknown names: what the API says about the chip
    if (gpu.discrete) return kGpuStrong;
    if (gpu.integrated) return kGpuWeak;
    return kGpuMedium;
}

GraphicsPick PickGraphics(const GpuFacts& gpu, int targetW, int targetH, bool wide) {
    const GpuClass cls = ClassifyGpu(gpu);
    const double budget = Budget(gpu, cls);
    const uint64_t memory = MemoryAllowed(gpu);
    const double aspect = wide ? 16.0 / 9.0 : 4.0 / 3.0;
    if (targetW <= 0 || targetH <= 0) targetW = 1920, targetH = 1080;

    // resolutions from the sharpest down: the window's own pixels, the fixed heights below it, then the N64's sizes
    struct Res {
        int mode, value;
        double pixels;
    };
    std::vector<Res> res;
    res.push_back({ kResWindow, 0, (double)targetW * targetH });
    for (int h : { 2160, 1440, 1080, 720 }) {
        if (h < targetH * 0.9) res.push_back({ kResHeight, h, h * h * aspect });
    }
    const double n64w = wide ? 426 : 320;
    for (int k : { 2, 1 }) res.push_back({ kResN64, k, n64w * k * 240 * k });

    // Smooth motion on at 2x the N64's size or more, sharpest first and more samples first at each size; then the
    // same without Smooth motion (half as many pictures: kNoSmoothBudget times the budget per picture); the N64's own size last
    std::vector<Candidate> order;
    for (const Res& r : res) {
        if (r.mode == kResN64 && r.value < 2) continue;
        for (int msaa : { 4, 2, 1 }) order.push_back({ r.mode, r.value, msaa, true, r.pixels });
    }
    for (const Res& r : res) {
        for (int msaa : { 2, 1 }) order.push_back({ r.mode, r.value, msaa, false, r.pixels });
    }

    GraphicsPick pick;
    pick.gpuClass = cls;
    for (const Candidate& c : order) {
        const double cost = c.pixels / 1e6 * MsaaFactor(c.msaa);
        const uint64_t bytes = MemoryFor(c.pixels, c.msaa, c.smooth);
        if (cost <= budget * (c.smooth ? 1.0 : kNoSmoothBudget) && bytes <= memory) {
            pick.resMode = c.mode, pick.resValue = c.value, pick.msaa = c.msaa, pick.smooth = c.smooth;
            pick.cost = cost, pick.memoryBytes = bytes;
            return pick;
        }
    }
    // nothing fits: the cheapest setting there is
    pick.resMode = kResN64, pick.resValue = 1, pick.msaa = 1, pick.smooth = false;
    pick.cost = n64w * 240 / 1e6, pick.memoryBytes = MemoryFor(n64w * 240, 1, false);
    return pick;
}

GpuFacts QueryGpuFacts() {
    GpuFacts gpu;
    gpu.systemRamBytes = (uint64_t)std::max(0, SDL_GetSystemRAM()) * 1024 * 1024;
    auto window = GetWindow();
    auto interp = window ? window->GetInterpreterWeak().lock() : nullptr;
    if (interp && interp->mRapi) {
        const Fast::GfxDeviceInfo info = interp->mRapi->GetDeviceInfo();
        gpu.name = info.name;
        gpu.vendor = info.vendor;
        gpu.vendorId = info.vendorId;
        gpu.integrated = info.integrated;
        gpu.discrete = info.discrete;
        gpu.unifiedMemory = info.unifiedMemory;
        gpu.memoryBytes = info.memoryBytes;
    }
    // test hook: PORT_DEBUG_GPU=<name>[,integrated|discrete][,<memory MB>] stands in for the renderer's chip
    if (const char* debug = getenv("PORT_DEBUG_GPU")) {
        std::string spec = debug;
        const size_t comma = spec.find(',');
        gpu = GpuFacts{};
        gpu.systemRamBytes = (uint64_t)std::max(0, SDL_GetSystemRAM()) * 1024 * 1024;
        gpu.name = spec.substr(0, comma);
        const std::string rest = comma == std::string::npos ? "" : spec.substr(comma);
        gpu.integrated = Has(rest, "integrated");
        gpu.discrete = Has(rest, "discrete");
        const size_t mb = rest.find_last_of(',');
        if (mb != std::string::npos) gpu.memoryBytes = strtoull(rest.c_str() + mb + 1, nullptr, 10) * 1024 * 1024;
    }
    return gpu;
}

GraphicsPick RecommendedGraphics() {
    int w = 0, h = 0;
    int count = 0;
    SDL_Window* sdl = nullptr;
    if (SDL_Window** ws = SDL_GetWindows(&count)) {
        if (count > 0) sdl = ws[0];
        SDL_free(ws);
    }
    bool fullscreen = false;
    if (auto window = GetWindow()) {
        if (auto config = window->GetContext()->GetChildren().GetFirst<Ship::Config>()) {
            fullscreen = config->GetBool("Window.Fullscreen.Enabled", false);
        }
    }
    if (sdl != nullptr) {
        const SDL_DisplayMode* mode = fullscreen ? SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(sdl)) : nullptr;
        if (mode != nullptr && mode->w > 0 && mode->h > 0) {
            const float density = std::max(1.0f, mode->pixel_density);
            w = (int)std::lround(mode->w * density), h = (int)std::lround(mode->h * density);
        } else {
            SDL_GetWindowSizeInPixels(sdl, &w, &h);
        }
    }
    return PickGraphics(QueryGpuFacts(), w, h, AspectSetting() == 1);
}

std::string DescribePick(const GpuFacts& gpu, const GraphicsPick& pick) {
    char buf[256];
    const std::string res =
        pick.resMode == kResWindow ? std::string("Match window") : ResolutionLabel(pick.resMode, pick.resValue);
    snprintf(buf, sizeof(buf), "%s (%s): %s, %s, Smooth motion %s", gpu.name.empty() ? "Unknown graphics" : gpu.name.c_str(),
             GpuClassName(pick.gpuClass), res.c_str(),
             pick.msaa > 1 ? (std::to_string(pick.msaa) + "x anti-aliasing").c_str() : "no anti-aliasing",
             pick.smooth ? "on" : "off");
    return buf;
}

void PrintGraphicsAdviceTable() {
    struct Example {
        const char* name;
        bool integrated, discrete;
        uint64_t memory, ram;
    };
    const Example examples[] = {
        { "Intel(R) HD Graphics 520", true, false, 0, 8 * kGiB },
        { "Intel(R) UHD Graphics 620", true, false, 0, 8 * kGiB },
        { "Intel(R) HD Graphics 520 (4 GB machine)", true, false, 0, 4 * kGiB },
        { "Intel(R) Iris(R) Xe Graphics", true, false, 0, 16 * kGiB },
        { "Intel Iris Plus Graphics 645 (Metal, 1.5 GB)", false, false, 1536ull << 20, 8 * kGiB },
        { "AMD Radeon(TM) Graphics", true, false, 0, 16 * kGiB },
        { "AMD Radeon 780M", true, false, 0, 16 * kGiB },
        { "NVIDIA GeForce MX250", false, true, 2 * kGiB, 8 * kGiB },
        { "NVIDIA GeForce GTX 1060 6GB", false, true, 6 * kGiB, 16 * kGiB },
        { "NVIDIA GeForce RTX 3060", false, true, 12 * kGiB, 16 * kGiB },
        { "AMD Radeon RX 580", false, true, 8 * kGiB, 16 * kGiB },
        { "Apple M1", false, false, 5 * kGiB, 8 * kGiB },
        { "Apple M2 Pro", false, false, 21 * kGiB, 32 * kGiB },
        { "Apple M5", false, false, 11 * kGiB, 16 * kGiB },
        { "Apple M1 Max", false, false, 48 * kGiB, 64 * kGiB },
        { "Adreno (TM) 650", true, false, 0, 8 * kGiB },
        { "Mali-G52", true, false, 0, 4 * kGiB },
        { "llvmpipe (LLVM 15.0.7, 256 bits)", false, false, 0, 8 * kGiB },
    };
    const int sizes[][2] = { { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 }, { 3024, 1964 } };
    for (const Example& e : examples) {
        GpuFacts gpu;
        gpu.name = e.name, gpu.integrated = e.integrated, gpu.discrete = e.discrete;
        gpu.memoryBytes = e.memory, gpu.systemRamBytes = e.ram;
        for (const auto& s : sizes) {
            const GraphicsPick p = PickGraphics(gpu, s[0], s[1], true);
            printf("%-46s %4dx%-4d %s  (%.2f MP-eq, %.0f MB)\n", e.name, s[0], s[1], DescribePick(gpu, p).c_str() + gpu.name.size(),
                   p.cost, p.memoryBytes / 1048576.0);
        }
    }
}

} // namespace Port

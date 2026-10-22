#include "Engine.h"
#include "touch_controls.h"
#include "pacing.h"

#include <libultraship/libultraship.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <ship/config/Config.h>
#include <ship/config/ConsoleVariable.h>
#include <ship/debug/Console.h>
#include <ship/debug/CrashHandler.h>
#include <ship/events/Events.h>
#include <ship/log/Logger.h>
#include <ship/resource/ResourceManager.h>
#include <ship/thread/ThreadPool.h>
#include <controller/controldeck/ControlDeck.h>
#include <libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h>
#include <stdexcept>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <libultraship/bridge/windowbridge.h>
#include <libultraship/bridge/controllerbridge.h>
#include <libultraship/bridge/eventsbridge.h>
#include <libultraship/bridge/crashhandlerbridge.h>
#include <libultraship/bridge/gfxbridge.h>

#include <algorithm>
#include <unistd.h>
#include <chrono>
#include <SDL3/SDL.h>
#include <thread>
#include <vector>

namespace Fast {
extern void* (*gPortAddressResolver)(uintptr_t);
}
extern "C" void* Port_ResolveN64Address(uintptr_t addr);

extern "C" double Port_Speed();
extern "C" bool Port_DisplayPaced();
extern "C" bool Port_Lockstep();
extern "C" uint64_t Port_ViTick();
extern "C" bool Port_FreezeCheckEnabled();
extern "C" void Port_NotePictureHash(uint64_t hash);
extern "C" void Port_GfxFrameDone(void);

// PORT_STOP_AT: the frame to save regardless of the change detection (as stop_<frame>.ppm).
static uint64_t sCaptureFrame = 0;
namespace Fast {
extern std::vector<uint8_t> gPortScreenBgra; // gfx_metal.cpp: last screen readback, BGRA8
extern uint32_t gPortScreenW, gPortScreenH;
}
using Fast::gPortScreenBgra;
using Fast::gPortScreenW;
using Fast::gPortScreenH;
extern bool gPortDrawLog;
extern "C" void Port_RequestCapture(uint64_t frame) { sCaptureFrame = frame; }
namespace {
std::shared_ptr<Ship::Context> sContext;
std::shared_ptr<Fast::Fast3dWindow> sWindow;

// Default bindings, the same as pokemonStadiumRecomp's (src/main/input_bindings.cpp). Keyboard: X/Z = A/B, Shift or
// Space = Z, Return (and keypad Enter, which libultraship reads as Return) = Start, arrows = D-pad, Q/E = L/R,
// I/K/J/L = C-buttons, WASD = stick. Controller: A/B, either trigger = Z, Start, D-pad, shoulders = L/R, right stick =
// C-buttons, left stick = stick.
std::shared_ptr<LUS::ControllerDefaultMappings> DefaultMappings() {
    using Ship::KbScancode;
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> keys = {
        { BTN_A, { KbScancode::LUS_KB_X } },
        { BTN_B, { KbScancode::LUS_KB_Z } },
        { BTN_Z, { KbScancode::LUS_KB_SHIFT, KbScancode::LUS_KB_SPACE } },
        { BTN_START, { KbScancode::LUS_KB_ENTER } },
        { BTN_DUP, { KbScancode::LUS_KB_ARROWKEY_UP } },
        { BTN_DDOWN, { KbScancode::LUS_KB_ARROWKEY_DOWN } },
        { BTN_DLEFT, { KbScancode::LUS_KB_ARROWKEY_LEFT } },
        { BTN_DRIGHT, { KbScancode::LUS_KB_ARROWKEY_RIGHT } },
        { BTN_L, { KbScancode::LUS_KB_Q } },
        { BTN_R, { KbScancode::LUS_KB_E } },
        { BTN_CUP, { KbScancode::LUS_KB_I } },
        { BTN_CDOWN, { KbScancode::LUS_KB_K } },
        { BTN_CLEFT, { KbScancode::LUS_KB_J } },
        { BTN_CRIGHT, { KbScancode::LUS_KB_L } },
    };
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, KbScancode>>> keyStick = {
        { Ship::LEFT_STICK,
          { { Ship::UP, KbScancode::LUS_KB_W },
            { Ship::DOWN, KbScancode::LUS_KB_S },
            { Ship::LEFT, KbScancode::LUS_KB_A },
            { Ship::RIGHT, KbScancode::LUS_KB_D } } },
    };
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>> pad = {
        { BTN_A, { SDL_GAMEPAD_BUTTON_SOUTH } },
        { BTN_B, { SDL_GAMEPAD_BUTTON_EAST } },
        { BTN_START, { SDL_GAMEPAD_BUTTON_START } },
        { BTN_DUP, { SDL_GAMEPAD_BUTTON_DPAD_UP } },
        { BTN_DDOWN, { SDL_GAMEPAD_BUTTON_DPAD_DOWN } },
        { BTN_DLEFT, { SDL_GAMEPAD_BUTTON_DPAD_LEFT } },
        { BTN_DRIGHT, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT } },
        { BTN_L, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER } },
        { BTN_R, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER } },
    };
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>> padAxes = {
        { BTN_Z, { { SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1 }, { SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1 } } },
        { BTN_CUP, { { SDL_GAMEPAD_AXIS_RIGHTY, -1 } } },
        { BTN_CDOWN, { { SDL_GAMEPAD_AXIS_RIGHTY, 1 } } },
        { BTN_CLEFT, { { SDL_GAMEPAD_AXIS_RIGHTX, -1 } } },
        { BTN_CRIGHT, { { SDL_GAMEPAD_AXIS_RIGHTX, 1 } } },
    };
    // the right stick drives the C-buttons only (no right-stick axis mapping)
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
        padStick = {
            { Ship::LEFT_STICK,
              { { Ship::UP, { SDL_GAMEPAD_AXIS_LEFTY, -1 } },
                { Ship::DOWN, { SDL_GAMEPAD_AXIS_LEFTY, 1 } },
                { Ship::LEFT, { SDL_GAMEPAD_AXIS_LEFTX, -1 } },
                { Ship::RIGHT, { SDL_GAMEPAD_AXIS_LEFTX, 1 } } } },
        };
    return std::make_shared<LUS::ControllerDefaultMappings>(
        keys, keyStick, pad, std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GamepadButton>>>(),
        padAxes, padStick);
}

// Configs written before these defaults hold libultraship's own ones (and nothing else: there was no way to change
// them); they are dropped once so the defaults above apply.
constexpr int32_t kControlsVersion = 1;
void MigrateControls(const std::shared_ptr<Ship::ConsoleVariable>& cvars) {
    if (cvars->GetInteger("gPort.ControlsVersion", 0) >= kControlsVersion) {
        return;
    }
    cvars->ClearBlock("gControllers");
    cvars->SetInteger("gPort.ControlsVersion", kControlsVersion);
    cvars->Save();
}
} // namespace

namespace Port {

bool Init() {
    Fast::gPortAddressResolver = Port_ResolveN64Address;
    sContext = Ship::Context::CreateInstance("Pokemon Stadium", "pokestadium");
    sContext->Init();

    auto logger = std::make_shared<Ship::Logger>(
        "Pokemon Stadium", Ship::Context::GetPathRelativeToAppDirectory("logs/pokestadium.log"));
    sContext->GetChildren().Add(logger);
    logger->Init();

    auto config = std::make_shared<Ship::Config>(
        Ship::Context::GetPathRelativeToAppDirectory("pokestadium.cfg.json"), nullptr);
    sContext->GetChildren().Add(config);

    auto cvars = std::make_shared<Ship::ConsoleVariable>(config);
    sContext->GetChildren().Add(cvars);
    CVarSetConsoleVariable(cvars);
    // Vsync would cap the speed-up at the display rate: PORT_SPEED > 1 turns it off for that run. The config is saved
    // with it (on the first run, from the settings menu), and later runs at normal speed then presented without vsync
    // (frames shown whenever they were done, out of step with the display); the player's own value is therefore kept
    // in gPort.VsyncSaved and restored by the next run at normal speed.
    if (Port_Speed() > 1.0) {
        if (!cvars->GetInteger("gPort.VsyncOverridden", 0)) {
            cvars->SetInteger("gPort.VsyncSaved", cvars->GetInteger("gVsyncEnabled", 1));
            cvars->SetInteger("gPort.VsyncOverridden", 1);
        }
        cvars->SetInteger("gVsyncEnabled", 0);
    } else if (cvars->GetInteger("gPort.VsyncOverridden", 0)) {
        cvars->SetInteger("gVsyncEnabled", cvars->GetInteger("gPort.VsyncSaved", 1));
        cvars->ClearVariable("gPort.VsyncOverridden");
        cvars->ClearVariable("gPort.VsyncSaved");
        cvars->Save();
    }

    // ImGui windows (tooltips, menus) stay inside the game window: with multi-viewports on, one that reached past its
    // edge became an OS window of its own, and the Metal backend crashed creating it (the launcher's wide tooltips)
    cvars->SetInteger("gEnableMultiViewports", 0);
#if defined(__ANDROID__)
    // Android: always fullscreen. A plain SDL window leaves the status and navigation bars over the picture; a
    // fullscreen one puts the activity in immersive mode (SDLActivity). Windowed fullscreen: the display keeps its
    // mode (an exclusive mode would ask the system for a display mode switch).
    config->SetBool("Window.Fullscreen.Enabled", true);
    cvars->SetInteger("gSdlWindowedFullscreen", 1);
#endif

    sWindow = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({}), config, cvars);

    auto threadPool =
        std::make_shared<Ship::ThreadPool>(std::max(1, (int32_t)(std::thread::hardware_concurrency() - 3 - 1)));
    sContext->GetChildren().Add(threadPool);

    auto resourceManager = std::make_shared<Ship::ResourceManager>(threadPool);
    sContext->GetChildren().Add(resourceManager);
    ResourceSetResourceManager(resourceManager);

    MigrateControls(cvars);
    ApplyAspect();
    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>(), DefaultMappings(),
                                                          std::unordered_map<CONTROLLERBUTTONS_T, std::string>({
                                                              { BTN_A, "A" },
                                                              { BTN_B, "B" },
                                                              { BTN_L, "L" },
                                                              { BTN_R, "R" },
                                                              { BTN_Z, "Z" },
                                                              { BTN_START, "Start" },
                                                              { BTN_CLEFT, "CLeft" },
                                                              { BTN_CRIGHT, "CRight" },
                                                              { BTN_CUP, "CUp" },
                                                              { BTN_CDOWN, "CDown" },
                                                              { BTN_DLEFT, "DLeft" },
                                                              { BTN_DRIGHT, "DRight" },
                                                              { BTN_DUP, "DUp" },
                                                              { BTN_DDOWN, "DDown" },
                                                          }),
                                                          sWindow, cvars);
    sContext->GetChildren().Add(controlDeck);
    ControllerSetControlDeck(controlDeck);

    // Placeholder archive: libultraship's GUI needs a ResourceManager with at least one archive.
    // Game data is still read straight from the ROM at this milestone.
#if defined(__ANDROID__)
    // the archive ships as an app asset (android/app/build.gradle), which libzip cannot open in place: copy it to the
    // app's files on every start (a fraction of a megabyte, and always the version that came with the app)
    if (SDL_IOStream* in = SDL_IOFromFile("pokestadium.o2r", "rb")) {
        std::string dst = Ship::Context::GetPathRelativeToAppDirectory("pokestadium.o2r");
        if (SDL_IOStream* out = SDL_IOFromFile(dst.c_str(), "wb")) {
            char chunk[65536];
            size_t n;
            while ((n = SDL_ReadIO(in, chunk, sizeof chunk)) > 0) {
                SDL_WriteIO(out, chunk, n);
            }
            SDL_CloseIO(out);
        }
        SDL_CloseIO(in);
    }
#endif
    try {
        nlohmann::json rmArgs;
        rmArgs["archivePaths"] = std::vector<std::string>{ Ship::Context::LocateFileAcrossAppDirs("pokestadium.o2r") };
        rmArgs["validHashes"] = std::vector<uint32_t>{};
        resourceManager->Init(rmArgs);
    } catch (const std::exception& e) {
        SPDLOG_WARN("ResourceManager init deferred: {}", e.what());
    }

    auto console = std::make_shared<Ship::Console>();
    sContext->GetChildren().Add(console);
    console->Init();

    auto crashHandler = std::make_shared<Ship::CrashHandler>();
    sContext->GetChildren().Add(crashHandler);
    CrashHandlerSetComponent(crashHandler);

    sContext->GetChildren().Add(sWindow);
    WindowSetWindowComponent(sWindow);
    GfxSetFast3dWindow(sWindow);
    sWindow->Init();
    // PORT_WINDOW_ON_TOP=1: the window stays above other windows without taking the keyboard focus (measuring the
    // frame pacing with PORT_TRACE_PACING while working in another app: macOS stops presenting a covered window)
    if (getenv("PORT_WINDOW_ON_TOP")) {
        int n = 0;
        SDL_Window** wins = SDL_GetWindows(&n);
        for (int i = 0; wins && i < n; i++) {
            SDL_SetWindowFocusable(wins[i], false);
            SDL_SetWindowAlwaysOnTop(wins[i], true);
        }
        SDL_free(wins);
    }
    // PORT_HIDDEN_WINDOW=1: render without showing the window (tools/run_replays.sh; screenshots still work)
    if (getenv("PORT_HIDDEN_WINDOW")) {
        int n = 0;
        SDL_Window** wins = SDL_GetWindows(&n);
        for (int i = 0; wins && i < n; i++) SDL_HideWindow(wins[i]);
        SDL_free(wins);
    }

    auto events = std::make_shared<Ship::Events>();
    sContext->GetChildren().Add(events);
    EventSystemSetEvents(events);

    StartShaderWarmup();
    return true;
}

// Debug aid (PORT_VALIDATE_DL=1): walks a display list before it is submitted and reports commands whose data
// pointers are not readable, which is what crashes the renderer when raw N64 addresses leak through.
#include "port_platform.h"
static bool Readable(const void* p, size_t n) {
    return Port_Readable(p, n) != 0;
}
static int sValidateBudget = 20;
static void ValidateList(const Gfx* g, int depth, const char* path) {
    if (depth > 24) return;
    for (int n = 0; n < 200000; n++, g++) {
        uint32_t w0 = (uint32_t)g->words.w0;
        uintptr_t w1 = g->words.w1;
        uint8_t op = w0 >> 24;
        if (op == 0xDF) return;
        if (op > 0x08 && op < 0xD7) {
            if (sValidateBudget-- > 0) fprintf(stderr, "[dlcheck] invalid opcode %02x (w0=%08x w1=%p) at %p depth %d\n", op, w0, (void*)w1, (const void*)g, depth);
            return;
        }
        bool ptrOp = op == 0xDE || op == 0x01 || op == 0xDA || op == 0xDC || op == 0xFD || op == 0xFE || op == 0xFF;
        if (!ptrOp) continue;
        if (op == 0xDE && w1 == 0) {
            if (sValidateBudget-- > 0) fprintf(stderr, "[dlcheck] G_DL to NULL at %p depth %d\n", (const void*)g, depth);
            return;
        }
        if (w1 < 0x100000000ull) continue; // N64-style address, Fast3D resolves it
        if (!Readable((void*)w1, 8)) {
            if (sValidateBudget-- > 0)
                fprintf(stderr, "[dlcheck] unreadable pointer %p in cmd %02x (w0=%08x) at %p depth %d (%s)\n", (void*)w1, op, w0, (const void*)g, depth, path);
            return;
        }
        if (op == 0xDE) {
            ValidateList((const Gfx*)w1, depth + 1, path);
            if (((w0 >> 16) & 0xFF) == 1) return;
        }
    }
}

// Debug aid (PORT_SCREENSHOT_DIR=dir): writes the presented frame as a PPM every 120 frames (the Metal readback
// returns the previous frame as RGBA5551). Convert with tools/ppm2png.py.
static void MaybeScreenshot() {
    static const char* dir = getenv("PORT_SCREENSHOT_DIR");
    static int frame = 0;
    if (!dir && !Port_FreezeCheckEnabled()) return;
    ++frame;
    int w = sWindow->GetWidth(), h = sWindow->GetHeight();
    {   // the drawable can be larger than the window in points (Retina): read the full pixel size
        int count = 0;
        if (SDL_Window** ws = SDL_GetWindows(&count)) {
            int pw = 0, ph = 0;
            if (count > 0 && SDL_GetWindowSizeInPixels(ws[0], &pw, &ph) && pw > 0 && ph > 0) { w = pw; h = ph; }
            SDL_free(ws);
        }
    }
    if (w <= 0 || h <= 0) return;
    auto interp = sWindow->GetInterpreterWeak().lock();
    if (!interp) return;
    std::vector<uint16_t> buf((size_t)w * h);
    interp->mRapi->ReadFramebufferToCPU(0, (uint32_t)w, (uint32_t)h, buf.data());
    {   // freeze detection: hash of a sparse sample of the presented picture
        // channels quantized to 3 bits: the per-frame shader noise (N64 dither emulation) must not count as change
        uint64_t fh = 1469598103934665603ull;
        for (size_t i = 0; i < buf.size(); i += 7) fh = (fh ^ (buf[i] & 0xE738)) * 1099511628211ull;
        Port_NotePictureHash(fh);
    }
    if (!dir) return;
    // Only save when the picture changed since the last saved frame (hash of a sparse sample), checked every 15 frames.
    // The readback must still be requested every frame to stay in sync.
    static uint64_t lastHash = 0;
    static bool all = getenv("PORT_SCREENSHOT_ALL") != nullptr;
    // Files are named by game frame (VI retrace count) in lockstep mode, so they line up with PORT_STOP_AT and replays.
    int name = Port_Lockstep() ? (int)Port_ViTick() : frame;
    if (sCaptureFrame != 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/stop_%05d.ppm", dir, name);
        if (FILE* f = fopen(path, "wb")) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            // the 8-bit screen pixels when the readback has them (same frame as buf), else the RGBA5551 copy
            bool full = gPortScreenW == (uint32_t)w && gPortScreenH == (uint32_t)h && gPortScreenBgra.size() == buf.size() * 4;
            for (size_t i = 0; i < buf.size(); i++) {
                if (full) {
                    uint8_t px[3] = { gPortScreenBgra[i * 4 + 2], gPortScreenBgra[i * 4 + 1], gPortScreenBgra[i * 4 + 0] };
                    fwrite(px, 1, 3, f);
                    continue;
                }
                uint16_t v = buf[i];
                uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 6) & 31) * 255 / 31), (uint8_t)(((v >> 1) & 31) * 255 / 31) };
                fwrite(px, 1, 3, f);
            }
            fclose(f);
        }
        sCaptureFrame = 0; // one picture per request (PORT_SHOT_AT asks again for its next frame)
        return;
    }
    if (frame % (all ? 5 : 15) != 0) return;
    uint64_t hsh = 1469598103934665603ull;
    size_t nz = 0;
    for (size_t i = 0; i < buf.size(); i += 7) { hsh = (hsh ^ buf[i]) * 1099511628211ull; nz += buf[i] != 0; }
    static int lastChange = 0;
    if (hsh == lastHash && !all) {
        // Picture unchanged (black or stuck) for ~6 s: the capture run has nothing more to show, so end it.
        static int stuckFrames = getenv("PORT_SCREENSHOT_STUCK") ? atoi(getenv("PORT_SCREENSHOT_STUCK")) * 60 : 360;
        static const bool noStop = getenv("PORT_SCREENSHOT_NOSTOP") != nullptr;
        if (frame - lastChange > stuckFrames && !noStop) {
            fprintf(stderr, "[shot] screen unchanged since frame %d, stopping\n", lastChange);
            fflush(stderr);
            _exit(0);
        }
        return;
    }
    lastHash = hsh;
    lastChange = frame;
    // the change-detected frames are uncompressed (~6 MB each at 1920x1080): stop saving them past a size budget,
    // PORT_SCREENSHOT_MAX_MB (default 2048), so a long run cannot fill the disk; stop_ pictures are still saved
    static const uint64_t budget = (getenv("PORT_SCREENSHOT_MAX_MB") ? strtoull(getenv("PORT_SCREENSHOT_MAX_MB"), nullptr, 10) : 2048) << 20;
    static uint64_t written = 0;
    const uint64_t size = (uint64_t)buf.size() * 3;
    if (written + size > budget) {
        static bool told = false;
        if (!told) {
            fprintf(stderr, "[shot] PORT_SCREENSHOT_MAX_MB reached (%llu MB written), no more changed frames saved\n",
                    (unsigned long long)(written >> 20));
            told = true;
        }
        return;
    }
    written += size;
    fprintf(stderr, "[shot] frame %d changed (nonzero samples %zu)\n", name, nz);
    char path[1024];
    snprintf(path, sizeof path, "%s/frame_%05d.ppm", dir, name);
    if (FILE* f = fopen(path, "wb")) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (size_t i = 0; i < buf.size(); i++) {
            uint16_t v = buf[i];
            uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 6) & 31) * 255 / 31), (uint8_t)(((v >> 1) & 31) * 255 / 31) };
            fwrite(px, 1, 3, f);
        }
        fclose(f);
    }
}

// Saves the picture just drawn as an 8-bit PPM (interp.cpp: in-between frames).
void SaveScreen(const char* path) {
    int w = 0, h = 0;
    int count = 0;
    if (SDL_Window** ws = SDL_GetWindows(&count)) {
        if (count > 0) SDL_GetWindowSizeInPixels(ws[0], &w, &h);
        SDL_free(ws);
    }
    auto interp = sWindow->GetInterpreterWeak().lock();
    if (w <= 0 || h <= 0 || !interp) return;
    std::vector<uint16_t> buf((size_t)w * h);
    interp->mRapi->ReadFramebufferToCPU(0, (uint32_t)w, (uint32_t)h, buf.data());
    bool full = gPortScreenW == (uint32_t)w && gPortScreenH == (uint32_t)h && gPortScreenBgra.size() == buf.size() * 4;
    static const int scale = getenv("PORT_SHOT_SCALE") ? std::max(1, atoi(getenv("PORT_SHOT_SCALE"))) : 1; // 1/n size
    if (FILE* f = fopen(path, "wb")) {
        fprintf(f, "P6\n%d %d\n255\n", w / scale, h / scale);
        for (size_t i = 0; i < buf.size(); i++) {
            if (scale > 1 && ((i % w) % scale != 0 || (i / w) % scale != 0 || (int)(i % w) / scale >= w / scale ||
                              (int)(i / w) / scale >= h / scale)) {
                continue;
            }
            uint16_t v = buf[i];
            uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 6) & 31) * 255 / 31), (uint8_t)(((v >> 1) & 31) * 255 / 31) };
            if (full) {
                px[0] = gPortScreenBgra[i * 4 + 2];
                px[1] = gPortScreenBgra[i * 4 + 1];
                px[2] = gPortScreenBgra[i * 4 + 0];
            }
            fwrite(px, 1, 3, f);
        }
        fclose(f);
    }
}

// Shows the presented-frame rate in the window title (updated twice a second).
static void UpdateFpsTitle() {
    static auto last = std::chrono::steady_clock::now();
    static int frames = 0;
    frames++;
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last).count();
    if (dt < 0.5) return;
    char title[96];
    snprintf(title, sizeof title, "Pokemon Stadium - %.1f FPS", frames / dt);
    int count = 0;
    if (SDL_Window** ws = SDL_GetWindows(&count)) {
        for (int i = 0; i < count; i++) SDL_SetWindowTitle(ws[i], title);
        SDL_free(ws);
    }
    frames = 0;
    last = now;
}

// A frame that is not drawn still updates the RSP's persistent segment table, as on the N64; later frames rely on it.
bool TrackFrame(void* gfxCommands) {
    if (auto interp = sWindow->GetInterpreterWeak().lock()) {
        return interp->TrackSegmentsOnly(static_cast<Gfx*>(gfxCommands)); // true: renders off-screen, must be drawn
    }
    return false;
}

bool RunFrame(void* gfxCommands) {
    return RunFrameWith(gfxCommands, nullptr, false);
}

// mtxReplacements: interpolated matrices (interp.cpp); subFrame: an in-between frame, the display list drawn again
bool RunFrameWith(void* gfxCommands, const void* mtxReplacements, bool subFrame, bool readBack) {
    static const std::unordered_map<Mtx*, MtxF> sNone;
    const auto& replacements =
        mtxReplacements ? *static_cast<const std::unordered_map<Mtx*, MtxF>*>(mtxReplacements) : sNone;
    if (subFrame) {
        if (!WindowIsRunning()) {
            return false;
        }
        auto interp = sWindow->GetInterpreterWeak().lock();
        if (!interp) return false;
        sWindow->StartFrame();
        interp->mReplayPass = true;
        interp->mReplayReadback = readBack; // (Metal reads the screen back at the end of the next frame)
        sWindow->DrawAndRunGraphicsCommands(static_cast<Gfx*>(gfxCommands), replacements);
        interp->mReplayPass = false;
        interp->mReplayReadback = false;
        Port_PacingNote(PORT_PACE_DRAWN, Port_SteadyNs());
        UpdateFpsTitle();
        return true;
    }
    static const bool validateDL = getenv("PORT_VALIDATE_DL") != nullptr;
    if (validateDL) ValidateList(static_cast<Gfx*>(gfxCommands), 0, "frame");
    if (!WindowIsRunning()) {
        return false;
    }
    sWindow->HandleEvents();
    { // PORT debug: PORT_DEBUG_RESIZE=<frame>:<w>x<h> resizes the window once at that VI frame
        static const char* rs = getenv("PORT_DEBUG_RESIZE");
        static bool done = false;
        unsigned long long f = 0;
        int w = 0, h = 0;
        if (rs && !done && sscanf(rs, "%llu:%dx%d", &f, &w, &h) == 3 && Port_ViTick() >= f) {
            done = true;
            int n = 0;
            SDL_Window** wins = SDL_GetWindows(&n);
            if (wins && n > 0) SDL_SetWindowSize(wins[0], w, h);
            SDL_free(wins);
            fprintf(stderr, "[resize] window -> %dx%d at frame %llu\n", w, h, (unsigned long long)Port_ViTick());
        }
        // PORT debug: PORT_QUIT_AT=<frame> sends the quit event a window close sends, at that VI frame
        static const char* qa = getenv("PORT_QUIT_AT");
        static bool quitSent = false;
        if (qa && !quitSent && Port_ViTick() >= strtoull(qa, nullptr, 10)) {
            quitSent = true;
            SDL_Event e = {};
            e.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&e);
            fprintf(stderr, "[quit] window close at frame %llu\n", (unsigned long long)Port_ViTick());
        }
    }
    if (!sWindow->IsFrameReady()) {
        if (!TrackFrame(gfxCommands)) {
            Port_GfxFrameDone();
            return true;
        }
    }
    sWindow->StartFrame();
    // libultraship's frame limiter sleeps after a present until 1/60 s after the previous one; when retraces follow the
    // display's refresh (os_devices.cpp) it must not hold presents back (a display slightly faster than 60 Hz would
    // otherwise push every frame a little later in the refresh, until one missed it), so it only caps at 64 fps there
    // (in-between frames come once per refresh: the limit then follows the display's refresh rate)
    int targetFps = Port_DisplayPaced() ? 64 : 60;
    if (mtxReplacements != nullptr) {
        int64_t period = 0, vblank = 0;
        if (Port_DisplayVblank(&period, &vblank) && period > 1000000) targetFps = (int)(1e9 / period) + 8;
    }
    sWindow->SetTargetFps(targetFps);
    static const bool drawLog = getenv("PORT_DRAWLOG") != nullptr;
    gPortDrawLog = sCaptureFrame != 0 && drawLog; // debug: list the draw states of the captured frame
    static const bool tracePacing = getenv("PORT_TRACE_PACING") != nullptr;
    auto drawStart = std::chrono::steady_clock::now();
    Port_BeforePresent();
    sWindow->DrawAndRunGraphicsCommands(static_cast<Gfx*>(gfxCommands), replacements);
    Port_PacingNote(PORT_PACE_DRAWN, Port_SteadyNs());
    if (tracePacing) { // PORT_TRACE_PACING=1: presented frames more than 40 ms apart, and slow draws (os_devices.cpp)
        static auto lastPresent = drawStart;
        auto now = std::chrono::steady_clock::now();
        double gapMs = std::chrono::duration<double, std::milli>(now - lastPresent).count();
        double drawMs = std::chrono::duration<double, std::milli>(now - drawStart).count();
        if (gapMs > 40 || drawMs > 20) {
            fprintf(stderr, "[present] frame %llu: %.1f ms since the last present (draw %.1f ms)\n",
                    (unsigned long long)Port_ViTick(), gapMs, drawMs);
        }
        lastPresent = now;
    }
    gPortDrawLog = false;
    Port_GfxFrameDone();
    MaybeScreenshot();
    UpdateFpsTitle();
    return true;
}

bool PumpFrame() {
    if (!WindowIsRunning()) {
        return false;
    }
    sWindow->HandleEvents();
    { // the display the window is on sets the pacing (os_devices.cpp, LockstepClock)
        int n = 0;
        if (SDL_Window** ws = SDL_GetWindows(&n)) {
            if (n > 0) Port_DisplayFollowWindow(ws[0]);
            SDL_free(ws);
        }
    }
    // Poll the controllers here, on the window thread.
    if (auto deck = ControllerGetControlDeck()) {
        OSContPad pads[MAXCONTROLLERS] = {};
        // the deck is set up by the game's osContInit (game thread); polling before that throws (no wheel handler
        // yet). On macOS the game usually got there first; on Linux the window thread does.
        try {
            deck->WriteToPad(pads);
        } catch (const std::runtime_error&) {
        }
        // Debug aid: PORT_AUTOSTART=<period in ms> taps Start (A with PORT_AUTOA) every period, to skip intros without a keyboard.
        static const char* autoStart = getenv("PORT_AUTOSTART");
        if (autoStart && !Port_Lockstep()) { // in lockstep mode the clock applies it per frame (os_devices.cpp)
            static auto t0 = std::chrono::steady_clock::now();
            long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            long period = atol(autoStart) > 0 ? atol(autoStart) : 5000;
            static const bool autoA = getenv("PORT_AUTOA") != nullptr;
            if (ms > 8000 && (ms % period) < 150) pads[0].button |= autoA ? (CONT_A | CONT_START) : CONT_START;
        }
        // Debug aid: PORT_KEYTEST=1 pushes real SDL key events (arrows = C-buttons, X = A) through the window's event
        // queue every second, to check the keyboard path end to end; PORT_TRACE_PADS=1 logs pad state changes.
        static const bool keyTest = getenv("PORT_KEYTEST") != nullptr;
        if (keyTest) {
            static int n = 0;
            static const SDL_Scancode keys[] = { SDL_SCANCODE_UP, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_X };
            int phase = n++ % 120;
            if (phase == 60 || phase == 70) {
                SDL_Event ev = {};
                ev.type = phase == 60 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                ev.key.scancode = keys[(n / 120) % 5];
                ev.key.down = phase == 60;
                int count = 0;
                if (SDL_Window** ws = SDL_GetWindows(&count)) {
                    if (count > 0) ev.key.windowID = SDL_GetWindowID(ws[0]);
                    SDL_free(ws);
                }
                SDL_PushEvent(&ev);
            }
        }
        static const bool tracePads = getenv("PORT_TRACE_PADS") != nullptr;
        if (tracePads) {
            static uint32_t last = 0;
            if (pads[0].button != last) fprintf(stderr, "[pad] button 0x%04x\n", pads[0].button);
            last = pads[0].button;
        }
        Port_TouchControlsApply(&pads[0], sWindow->GetGui().get()); // the on-screen controller (touch_controls.cpp)
        PublishPads(pads);
    }
    return true;
}

// The game picture's shape: 4:3 (the N64's) or 16:9 (the 3D scenes show more at the sides); the rest of the window gets
// black bars (libultraship's fixed-aspect resolution settings), so only these two shapes need to look right.
// gPort.Aspect is the launcher's Screen option (0 = 4:3, 1 = 16:9; 16:9 by default on Android, whose screens are wide);
// PORT_ASPECT=4:3 or 16:9 overrides it (tests).
int AspectSetting() {
    if (const char* e = getenv("PORT_ASPECT")) {
        return strcmp(e, "16:9") == 0 ? 1 : 0;
    }
#if defined(__ANDROID__)
    return CVarGetInteger("gPort.Aspect", 1) != 0 ? 1 : 0;
#else
    return CVarGetInteger("gPort.Aspect", 0) != 0 ? 1 : 0;
#endif
}

} // namespace Port

// Port_WidenViewAngle (src/port/sdk/port_compat.h): the half-angle whose tangent is the 4:3 one's times the width ratio
extern "C" s16 Port_WidenViewAngle(s16 angle) {
    if (Port::AspectSetting() != 1) {
        return angle;
    }
    const double kBinToRad = 3.14159265358979323846 / 32768.0;
    const double widened = atan(tan(angle * kBinToRad) * ((16.0 / 9.0) / (4.0 / 3.0)));
    return (s16)lround(widened / kBinToRad);
}

namespace Port {

void ApplyAspect() {
    const bool wide = AspectSetting() == 1;
    CVarSetInteger("gAdvancedResolution.Enabled", 1);
    CVarSetFloat("gAdvancedResolution.AspectRatioX", wide ? 16.0f : 4.0f);
    CVarSetFloat("gAdvancedResolution.AspectRatioY", wide ? 9.0f : 3.0f);
    CVarSetInteger("gAdvancedResolution.VerticalResolutionToggle", 0); // the window's resolution
    CVarSetInteger("gAdvancedResolution.PixelPerfectMode", 0);
    CVarSetInteger("gAdvancedResolution.IgnoreAspectCorrection", 0);
}

std::string AppPath(const char* name) {
    return Ship::Context::GetPathRelativeToAppDirectory(name);
}

std::shared_ptr<Fast::Fast3dWindow> GetWindow() {
    return sWindow;
}

void Shutdown() {
    sWindow = nullptr;
    sContext = nullptr;
}

} // namespace Port

#include "Engine.h"
#include "launcher_reliability.h"
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

#include "sound_settings.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <chrono>
#include <SDL3/SDL.h>
#include <thread>
#include <vector>
#include "StadionPort.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace Fast {
extern void* (*gPortAddressResolver)(uintptr_t);
}
#if defined(__EMSCRIPTEN__)
extern "C" void Port_FragmentLoadRanges(const char* text);
#endif
extern "C" void* Port_ResolveN64Address(uintptr_t addr);

extern "C" double Port_Speed();
extern "C" bool Port_DisplayPaced();
extern "C" bool Port_Lockstep();
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

// libultraship's osContInit loads SDL controller mappings from gamecontrollerdb.txt and logs an error when there is
// none. SDL has mappings for the common controllers built in, so the port creates a placeholder (comments only) in
// the app's folder the first time; mappings pasted into it are used from then on.
static void EnsureControllerDb() {
    std::error_code ec;
    const std::string found = Ship::Context::LocateFileAcrossAppDirs("gamecontrollerdb.txt");
    if (std::filesystem::exists(std::filesystem::u8path(found), ec)) {
        return;
    }
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("gamecontrollerdb.txt");
    if (FILE* f = fopen(path.c_str(), "w")) {
        fputs("# SDL game controller mappings, one per line (https://github.com/mdqinc/SDL_GameControllerDB).\n"
              "# SDL knows the common controllers already: add a line here only for a controller it does not.\n",
              f);
        fclose(f);
    }
}

#if !defined(__EMSCRIPTEN__)
extern "C" const unsigned char gPortO2r[]; // generated/pokestadium_o2r.c
extern "C" const size_t gPortO2rSize;
#endif

bool Init() {
    Fast::gPortAddressResolver = Port_ResolveN64Address;
    sContext = Ship::Context::CreateInstance("Pokemon Stadium", "pokestadium");
    sContext->Init();

    auto logger = std::make_shared<Ship::Logger>(
        "Pokemon Stadium", Ship::Context::GetPathRelativeToAppDirectory("logs/pokestadium.log"));
    sContext->GetChildren().Add(logger);
    logger->Init();
    EnsureControllerDb();

    auto config = std::make_shared<Ship::Config>(
        Ship::Context::GetPathRelativeToAppDirectory("pokestadium.cfg.json"), nullptr);
    sContext->GetChildren().Add(config);
    if (const char* res = getenv("PORT_BATTLE_RES")) { // battle boot: the window (so the picture) at that size
        int rw = 0, rh = 0;
        if (sscanf(res, "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0) {
            config->SetInt("Window.Width", rw);
            config->SetInt("Window.Height", rh);
        }
    }

    auto cvars = std::make_shared<Ship::ConsoleVariable>(config);
    sContext->GetChildren().Add(cvars);
    CVarSetConsoleVariable(cvars);
    if (getenv("PORT_SAFE_MODE") != nullptr) {
#ifdef __APPLE__
        config->SetInt("Window.Backend.Id", Fast::FAST3D_SDL_METAL);
        config->SetString("Window.Backend.Name", "Metal");
        cvars->SetInteger("gLauncher.Renderer", Fast::FAST3D_SDL_METAL);
#else
        config->SetInt("Window.Backend.Id", Fast::FAST3D_SDL_OPENGL);
        config->SetString("Window.Backend.Name", "OpenGL");
        cvars->SetInteger("gLauncher.Renderer", Fast::FAST3D_SDL_OPENGL);
#endif
        cvars->SetInteger("gLauncher.AutoPlay", 0);
        cvars->Save();
    }

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

    // Graphics settings first seen by this version (the launcher's Graphics panel, Engine.h ResolutionSetting):
    // Internal resolution replaced the old Resolution percentage (gInternalResolution): below 100 % (the slow-chip
    // preset's 50 %) becomes 2x, the Low-power preset's size; anything else Match window, which now counts the
    // window's pixels (a HiDPI display's too, as the web build's devicePixelRatio scale did). New configurations get
    // the Recommended preset: 4x anti-aliasing and Smooth motion on. Fullscreen is borderless (SDL; DXGI always is).
    if (cvars->GetInteger("gPort.Resolution.Mode", -1) < 0) {
        const float old = cvars->GetFloat("gInternalResolution", 1.0f);
        cvars->SetInteger("gPort.Resolution.Mode", old > 0.0f && old < 0.99f ? kResN64 : kResWindow);
        cvars->SetInteger("gPort.Resolution.Multiple", 2);
        cvars->SetInteger("gPort.Resolution.Height", 1080);
        cvars->SetInteger("gPort.Resolution.Upscale", kUpscaleSmooth);
        cvars->SetFloat("gInternalResolution", 1.0f);
        if (cvars->GetInteger("gMSAAValue", 0) == 0) cvars->SetInteger("gMSAAValue", 4);
        if (cvars->GetInteger("gPort.Interpolate", -1) < 0) cvars->SetInteger("gPort.Interpolate", 1);
        cvars->Save();
    }
#if !defined(__ANDROID__)
    cvars->SetInteger("gSdlWindowedFullscreen", 1);
#endif
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
    ApplySoundSettings();
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

    // The port's archive (shaders, fonts, the shader warm-up list, launcher pictures; no game data, which is read from
    // the ROM) is built into the executable (CMakeLists.txt, tools/make_o2r.py --c). It used to be a separate
    // pokestadium.o2r next to the executable, and a damaged, unreadable or missing copy crashed the game a moment later
    // (an uncaught exception on Windows). A pokestadium.o2r left over from an older version is not read.
    std::string archiveError;
    try {
        nlohmann::json rmArgs;
#if defined(__EMSCRIPTEN__)
        rmArgs["archivePaths"] = std::vector<std::string>{ AppPath("pokestadium.o2r") };
#else
        resourceManager->SetBuiltInArchive("pokestadium.o2r (built in)", gPortO2r, gPortO2rSize);
        rmArgs["archivePaths"] = std::vector<std::string>{};
#endif
        rmArgs["validHashes"] = std::vector<uint32_t>{};
        resourceManager->Init(rmArgs);
        if (!resourceManager->GetArchiveManager()->HasFile("port/shader_warmup.txt")) {
            archiveError = "the resource archive has no shader list";
        }
    } catch (const std::exception& e) {
        archiveError = e.what();
    }
    if (!archiveError.empty()) { // a broken build: the renderers would fail on the missing shaders
        std::string message = "The game's resources could not be loaded (" + archiveError + ").";
        SPDLOG_CRITICAL("{}", message);
        fprintf(stderr, "%s\n", message.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pokémon Stadium", message.c_str(), nullptr);
        return false;
    }
#if defined(__EMSCRIPTEN__)
    // Web build: where each fragment's static data lies (tools/gen_frag_ranges.py), copied now, before the game runs, so
    // that loading a fragment starts it fresh as on the N64 (Port_FragmentResetData in the generated frag_sections.c)
    if (auto file = resourceManager->GetArchiveManager()->LoadFile("port/frag_ranges.txt"); file && file->Buffer) {
        std::string text(file->Buffer->begin(), file->Buffer->end());
        Port_FragmentLoadRanges(text.c_str());
    } else {
        SPDLOG_WARN("port/frag_ranges.txt missing: fragments keep their data between visits");
    }
#endif

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
    ApplyAspect(); // again: the renderer's Init read the scale from gInternalResolution (PORT_RESOLUTION=window:n)
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
    // PORT_HIDDEN_WINDOW=1: render without showing the window (tools/run_replays.sh; screenshots still work).
    // PORT_HEADLESS=1 (tools/battle_xcheck.py without --anim, tools/evidence/run.py --rec: nothing to look at) hides it too
    if (getenv("PORT_HIDDEN_WINDOW") || getenv("PORT_HEADLESS")) {
        int n = 0;
        SDL_Window** wins = SDL_GetWindows(&n);
        for (int i = 0; wins && i < n; i++) SDL_HideWindow(wins[i]);
        SDL_free(wins);
    }

    auto events = std::make_shared<Ship::Events>();
    sContext->GetChildren().Add(events);
    EventSystemSetEvents(events);

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
extern "C" const char* Port_BattleFramePath(int* w, int* h);
extern "C" void Port_BattleFrameWritten(void);

// the presented frame as a PPM: the 8-bit screen pixels when the readback has them (same frame as buf), else the
// RGBA5551 copy
static void WriteFramePpm(const char* path, int w, int h, const std::vector<uint16_t>& buf) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
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

// battle boot frames (xcheck.cpp): the presented frame as an 8-bit PPM, box-averaged to ow x oh when that is asked for
static void WriteFramePpmSized(const char* path, int w, int h, const std::vector<uint16_t>& buf, int ow, int oh) {
    if (ow <= 0 || oh <= 0 || (ow == w && oh == h)) return WriteFramePpm(path, w, h, buf);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    bool full = gPortScreenW == (uint32_t)w && gPortScreenH == (uint32_t)h && gPortScreenBgra.size() == buf.size() * 4;
    auto pixel = [&](size_t i, unsigned* rgb) {
        if (full) {
            rgb[0] += gPortScreenBgra[i * 4 + 2], rgb[1] += gPortScreenBgra[i * 4 + 1], rgb[2] += gPortScreenBgra[i * 4];
        } else {
            uint16_t v = buf[i];
            rgb[0] += ((v >> 11) & 31) * 255 / 31, rgb[1] += ((v >> 6) & 31) * 255 / 31, rgb[2] += ((v >> 1) & 31) * 255 / 31;
        }
    };
    std::vector<uint8_t> out((size_t)ow * oh * 3);
    for (int y = 0; y < oh; y++) {
        int ya = (int)((int64_t)y * h / oh), yb = std::max(ya + 1, (int)((int64_t)(y + 1) * h / oh));
        for (int x = 0; x < ow; x++) {
            int xa = (int)((int64_t)x * w / ow), xb = std::max(xa + 1, (int)((int64_t)(x + 1) * w / ow));
            unsigned sum[3] = { 0, 0, 0 }, n = 0;
            for (int yy = ya; yy < yb && yy < h; yy++) {
                for (int xx = xa; xx < xb && xx < w; xx++) pixel((size_t)yy * w + xx, sum), n++;
            }
            n = std::max(n, 1u);
            for (int c = 0; c < 3; c++) out[((size_t)y * ow + x) * 3 + c] = (uint8_t)(sum[c] / n);
        }
    }
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
}

// the same, 160x120 (box-averaged, the centre 4:3 of a wider picture): the battle cross-check's animation frames
static void WriteFramePpmSmall(const char* path, int w, int h, const std::vector<uint16_t>& buf) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    const int ow = 160, oh = 120;
    bool full = gPortScreenW == (uint32_t)w && gPortScreenH == (uint32_t)h && gPortScreenBgra.size() == buf.size() * 4;
    int cw = std::min(w, h * 4 / 3), x0 = (w - cw) / 2;
    std::vector<uint8_t> out((size_t)ow * oh * 3);
    for (int y = 0; y < oh; y++) {
        int ya = y * h / oh, yb = std::max(ya + 1, (y + 1) * h / oh);
        for (int x = 0; x < ow; x++) {
            int xa = x0 + x * cw / ow, xb = std::max(xa + 1, x0 + (x + 1) * cw / ow);
            unsigned sum[3] = { 0, 0, 0 }, n = 0;
            for (int yy = ya; yy < yb; yy++) {
                for (int xx = xa; xx < xb; xx++) {
                    size_t i = (size_t)yy * w + xx;
                    if (full) {
                        sum[0] += gPortScreenBgra[i * 4 + 2], sum[1] += gPortScreenBgra[i * 4 + 1], sum[2] += gPortScreenBgra[i * 4];
                    } else {
                        uint16_t v = buf[i];
                        sum[0] += ((v >> 11) & 31) * 255 / 31, sum[1] += ((v >> 6) & 31) * 255 / 31, sum[2] += ((v >> 1) & 31) * 255 / 31;
                    }
                    n++;
                }
            }
            for (int c = 0; c < 3; c++) out[((size_t)y * ow + x) * 3 + c] = (uint8_t)(sum[c] / n);
        }
    }
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
}

static void MaybeScreenshot() {
    static const char* dir = getenv("PORT_SCREENSHOT_DIR");
    static const bool xcheckAnim = Port_XcheckAnimActive(); // battle cross-check: move animation frames (xcheck.cpp)
    static const bool stopOnly = getenv("PORT_SCREENSHOT_STOP_ONLY") != nullptr;
    static const bool battleBoot = Port_BattleBootActive() != 0; // battle boot: frames of the whole battle (xcheck.cpp)
    if (stopOnly && !xcheckAnim && !battleBoot && !Port_FreezeCheckEnabled() && sCaptureFrame == 0) return;
    static int frame = 0;
    if (!dir && !xcheckAnim && !battleBoot && !Port_FreezeCheckEnabled()) return;
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
    if (const char* xpath = xcheckAnim ? Port_XcheckFramePath() : nullptr) WriteFramePpmSmall(xpath, w, h, buf);
    if (battleBoot) {
        int ow = 0, oh = 0;
        if (const char* bpath = Port_BattleFramePath(&ow, &oh)) {
            WriteFramePpmSized(bpath, w, h, buf, ow, oh);
            Port_BattleFrameWritten();
        }
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
        WriteFramePpm(path, w, h, buf);
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
    int w = static_cast<int>(sWindow->GetWidth()), h = static_cast<int>(sWindow->GetHeight());
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

static std::atomic<uint64_t> sFramesDrawn{ 0 };
uint64_t FramesDrawn() {
    return sFramesDrawn.load(std::memory_order_relaxed);
}

// PORT debug: PORT_DEBUG_DRAW_MS=<ms> makes every drawn frame (in-between frames too) take that much longer, as on a
// slow GPU (an Intel HD 520 needs ~20-25 ms per battle frame), to test pacing and sound when drawing falls behind
static void DebugDrawDelay() {
    static const double sDrawDelayMs = getenv("PORT_DEBUG_DRAW_MS") ? atof(getenv("PORT_DEBUG_DRAW_MS")) : 0.0;
    if (sDrawDelayMs > 0) std::this_thread::sleep_for(std::chrono::microseconds((long long)(sDrawDelayMs * 1000)));
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
        DebugDrawDelay();
        sFramesDrawn.fetch_add(1, std::memory_order_relaxed);
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
    Port::DebugMinimize(Port_ViTick());
    { // PORT debug: PORT_DEBUG_RESIZE=<frame>:<w>x<h>[,<frame>:<w>x<h>...] resizes the window at those VI frames
        static const char* rs = getenv("PORT_DEBUG_RESIZE");
        unsigned long long f = 0;
        int w = 0, h = 0, used = 0;
        if (rs && sscanf(rs, "%llu:%dx%d%n", &f, &w, &h, &used) == 3 && Port_ViTick() >= f) {
            rs += used;
            if (*rs == ',') rs++;
            sWindow->SetCurrentDimensions((uint32_t)w, (uint32_t)h); // the window backend's own window (SDL, Win32)
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
    DebugDrawDelay();
    sFramesDrawn.fetch_add(1, std::memory_order_relaxed);
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

extern "C" void Port_ProcessControllerInitRequests(void);

bool PumpFrame() {
    static bool saveFailureShown = false;
    if (!saveFailureShown) {
        const auto error = TakeSaveFailure();
        if (!error.empty()) {
            saveFailureShown = true;
            const auto text = error + "\n\nProgress could not be saved. Free disk space or restore write access before continuing. The previous on-disk save was preserved.";
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save failed", text.c_str(), nullptr);
        }
    }
    if (!WindowIsRunning()) {
        return false;
    }
    Port_ProcessControllerInitRequests();
    sWindow->HandleEvents();
    { // the display the window is on sets the pacing (os_devices.cpp, LockstepClock)
        int n = 0;
        if (SDL_Window** ws = SDL_GetWindows(&n)) {
            if (n > 0) Port_DisplayFollowWindow(ws[0]);
            // launcher > Sound > Mute in background (audio_out.cpp reads it on the game thread)
            if (n > 0) NoteSoundWindowFocus((SDL_GetWindowFlags(ws[0]) & SDL_WINDOW_INPUT_FOCUS) != 0);
            SDL_free(ws);
        }
    }
    // Poll the controllers here, on the window thread.
    if (auto deck = ControllerGetControlDeck()) {
        OSContPad pads[MAXCONTROLLERS] = {};
        // The game may not have requested controller initialization yet; polling an uninitialized deck throws
        // because its wheel handler has not been installed.
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

extern "C" float Port_WidenViewSpan(float span) {
    return Port::AspectSetting() == 1 ? span * (4.0f / 3.0f) : span;
}

// Port_WidenEffectGridX (port_compat.h): the effect grids span x -72..63 at 4:3, which puts their right edge at 0.94 of
// the screen's half-width (their left edge at 1.08); in 16:9 the right edge has to reach 4/3 of it, so the grid is
// widened by 1.45 about its centre (x 0: the screen's centre)
extern "C" float Port_WidenEffectGridX(float x) {
    return Port::AspectSetting() == 1 ? x * 1.45f : x;
}

// Port_WideMarginN64 (port_compat.h): N64 pixels beside the 4:3 frame on each side of the picture (16:9: 54, rounded up)
extern "C" int Port_WideMarginN64(void) {
    return Port::AspectSetting() == 1 ? 54 : 0;
}

namespace Port {

void DebugMinimize(unsigned long long frame) {
    static const char* spec = getenv("PORT_DEBUG_MINIMIZE");
    static unsigned long long minFrame = 0, restoreFrame = 0;
    static bool parsed = false, minimized = false, restored = false;
    if (spec == nullptr || restored) return;
    if (!parsed) {
        parsed = true;
        if (sscanf(spec, "%llu:%llu", &minFrame, &restoreFrame) != 2) {
            fprintf(stderr, "[minimize] PORT_DEBUG_MINIMIZE=<frame>:<restore frame>\n");
            restored = true;
            return;
        }
    }
    int n = 0;
    SDL_Window** ws = SDL_GetWindows(&n);
    if (ws == nullptr || n == 0) {
        SDL_free(ws);
        return;
    }
    SDL_Window* w = ws[0];
    SDL_free(ws);
    if (!minimized && frame >= minFrame) {
        minimized = true;
        SDL_MinimizeWindow(w);
        fprintf(stderr, "[minimize] window minimized at frame %llu\n", frame);
    } else if (minimized && frame >= restoreFrame) {
        restored = true;
        SDL_RestoreWindow(w);
        fprintf(stderr, "[minimize] window restored at frame %llu\n", frame);
    }
}

void ApplySoundSettings() {
    SoundSettings s;
    s.master = CVarGetInteger("gSound.Master", 100);
    s.music = CVarGetInteger("gSound.Music", 100);
    s.effects = CVarGetInteger("gSound.Effects", 100);
    s.voices = CVarGetInteger("gSound.Voices", 100);
    s.gameBoy = CVarGetInteger("gSound.GameBoy", 100);
    s.on = CVarGetInteger("gSound.On", 1) != 0;
    s.muteInBackground = CVarGetInteger("gSound.MuteInBackground", 0) != 0;
    s.delay = CVarGetInteger("gSound.Delay", 0);
    SetSoundSettings(s);
}

// a fixed resolution's size for the picture's shape (see ApplyAspect)
static void FixedSize(int mode, int value, bool wide, int* width, int* lines) {
    if (mode == kResN64) {
        *lines = 240 * value;
        *width = (wide ? 426 : 320) * value;
    } else {
        *lines = value;
        *width = wide ? 2 * (int)lround(value * 16.0 / 9.0 / 2.0) : (int)lround(value * 4.0 / 3.0);
        if (wide && ((*width - (int)lround(value * 4.0 / 3.0)) & 1)) (*width)++;
    }
}

std::string ResolutionLabel(int mode, int value) {
    if (mode == kResWindow) return "Match window";
    int w, h;
    FixedSize(mode, value, AspectSetting() == 1, &w, &h);
    char buf[64];
    if (mode == kResN64) snprintf(buf, sizeof buf, "%s %d x %d", value == 1 ? "Native" : (std::to_string(value) + "x").c_str(), w, h);
    else snprintf(buf, sizeof buf, "%dp %d x %d", value, w, h);
    return buf;
}

ResolutionSetting GetResolutionSetting() {
    ResolutionSetting r;
    r.mode = std::clamp(CVarGetInteger("gPort.Resolution.Mode", kResWindow), (int)kResWindow, (int)kResHeight);
    r.multiple = std::clamp(CVarGetInteger("gPort.Resolution.Multiple", 1), 1, 8);
    r.height = std::clamp(CVarGetInteger("gPort.Resolution.Height", 1080), 240, 4320);
    r.scale = 1.0f; // (the window mode's scale: PORT_RESOLUTION=window:n only)
    r.upscale = std::clamp(CVarGetInteger("gPort.Resolution.Upscale", kUpscaleSmooth), (int)kUpscaleExact,
                           (int)kUpscaleSmooth);
    if (const char* e = getenv("PORT_RESOLUTION")) {
        const std::string v = e;
        int n = atoi(e);
        if (v == "native") {
            r.mode = kResN64;
            r.multiple = 1;
        } else if (v.size() >= 2 && v.back() == 'x' && n >= 1) {
            r.mode = kResN64;
            r.multiple = std::min(n, 8);
        } else if (v.size() >= 2 && v.back() == 'p' && n >= 240) {
            r.mode = kResHeight;
            r.height = std::min(n, 4320);
        } else if (v.rfind("window", 0) == 0) {
            r.mode = kResWindow;
            if (v.size() > 7) r.scale = std::clamp(atoi(e + 7) / 100.0f, 0.25f, 4.0f);
        }
    }
    if (const char* e = getenv("PORT_UPSCALE")) {
        r.upscale = strcmp(e, "exact") == 0 ? kUpscaleExact : strcmp(e, "sharp") == 0 ? kUpscaleSharp : kUpscaleSmooth;
    }
    return r;
}

void SetResolutionSetting(const ResolutionSetting& r) {
    CVarSetInteger("gPort.Resolution.Mode", r.mode);
    CVarSetInteger("gPort.Resolution.Multiple", r.multiple);
    CVarSetInteger("gPort.Resolution.Height", r.height);
    CVarSetInteger("gPort.Resolution.Upscale", r.upscale);
    ApplyAspect();
}

void RenderSize(int* w, int* h) {
    *w = *h = 0;
    if (sWindow == nullptr) return;
    if (auto interp = sWindow->GetInterpreterWeak().lock()) {
        uint32_t cw = 0, ch = 0;
        interp->GetCurDimensions(&cw, &ch);
        *w = (int)cw;
        *h = (int)ch;
    }
}

// The picture's shape and resolution go to libultraship's fixed-aspect ("advanced") resolution settings: the window
// mode keeps the window's resolution (times gInternalResolution) cut to the shape; the N64 and fixed-height modes set
// the vertical pixel count and a width for the shape. Their 16:9 widths keep the 4:3 middle on whole pixels with an
// even margin on each side, so the 2D screens and HUD drawn into it are not shifted by half a pixel: the N64's 240
// lines get 426 = 320 + 2 x 53 (16:9 is 426.7; 428, Port_WideMarginN64's 54 a side, would not fit 3x into 1280 x 720
// or 6x into 2560 x 1440 for pixel-exact upscaling), 480 lines 854 (not 853).
void ApplyAspect() {
    const bool wide = AspectSetting() == 1;
    const ResolutionSetting r = GetResolutionSetting();
    CVarSetInteger("gAdvancedResolution.Enabled", 1);
    CVarSetInteger("gAdvancedResolution.IgnoreAspectCorrection", 0);
    if (r.mode == kResWindow) {
        CVarSetFloat("gAdvancedResolution.AspectRatioX", wide ? 16.0f : 4.0f);
        CVarSetFloat("gAdvancedResolution.AspectRatioY", wide ? 9.0f : 3.0f);
        CVarSetInteger("gAdvancedResolution.VerticalResolutionToggle", 0); // the window's resolution
    } else {
        int width, lines;
        FixedSize(r.mode, r.mode == kResN64 ? r.multiple : r.height, wide, &width, &lines);
        CVarSetFloat("gAdvancedResolution.AspectRatioX", (float)width);
        CVarSetFloat("gAdvancedResolution.AspectRatioY", (float)lines);
        CVarSetInteger("gAdvancedResolution.VerticalResolutionToggle", 1);
        CVarSetInteger("gAdvancedResolution.VerticalPixelCount", lines);
    }
    // Scaling (whole multiples: Fast3dGui::DrawGame; nearest) for the fixed resolutions; Match window's picture has the
    // window's pixels and is not scaled (Scaling is greyed out there), so it is always drawn linearly
    CVarSetInteger("gAdvancedResolution.PixelPerfectMode", r.mode != kResWindow && r.upscale == kUpscaleExact ? 1 : 0);
    CVarSetInteger("gAdvancedResolution.NearestUpscale", r.mode != kResWindow && r.upscale != kUpscaleSmooth ? 1 : 0);
    CVarSetInteger("gAdvancedResolution.MatchPixels", r.mode == kResWindow ? 1 : 0);
    if (sWindow != nullptr) {
        sWindow->SetResolutionMultiplier(r.mode == kResWindow ? r.scale : 1.0f);
    }
}

std::string AppPath(const char* name) {
    return Ship::Context::GetPathRelativeToAppDirectory(name);
}

std::shared_ptr<Fast::Fast3dWindow> GetWindow() {
    return sWindow;
}

[[noreturn]] void ExitAfterVulkanFailure(const char* reason) {
    bool saved = false;
    try {
        // Vulkan can fail before the launcher opens. Save both the active backend and the launcher's request so
        // Store() cannot restore the failing renderer on the next start. Keep every other setting and save file.
        if (sWindow != nullptr) {
            auto config = sWindow->GetContext()->GetChildren().GetFirst<Ship::Config>();
            if (config != nullptr) {
                CVarSetInteger("gLauncher.Renderer", Fast::FAST3D_SDL_OPENGL);
                config->SetInt("Window.Backend.Id", Fast::FAST3D_SDL_OPENGL);
                config->SetString("Window.Backend.Name", "OpenGL");
                config->Save();
                CVarSave();
                saved = true;
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Could not save the renderer fallback: %s\n", e.what());
    }
    std::string message = std::string("The Vulkan renderer stopped working.\n\n") + reason +
                          (saved ? "\n\nOpenGL has been selected for the next launch. Please restart the game."
                                 : "\n\nPlease select OpenGL or DirectX 11 in pokestadium.cfg.json before restarting.");
    SPDLOG_ERROR("{}", message);
    fprintf(stderr, "%s\n", message.c_str());
    if (getenv("PORT_HIDDEN_WINDOW") == nullptr) { // (scripted runs: no dialog waiting for a click)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pokémon Stadium: Vulkan error", message.c_str(), nullptr);
    }
    // A failed swapchain may already have retired the old one. Do not unwind driver objects or run static
    // destructors while game threads are still alive (the normal shutdown path also exits the process directly).
    fflush(nullptr);
    std::_Exit(1);
}

void Shutdown() {
    sWindow = nullptr;
    sContext = nullptr;
}

} // namespace Port

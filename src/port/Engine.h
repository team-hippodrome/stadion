#pragma once
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast {
class Fast3dWindow;
}

// Owns the libultraship context and window for the Pokemon Stadium port.
namespace Port {
bool Init();
// Reports a Vulkan swapchain failure, saves a usable renderer for the next start, and exits without driver teardown.
[[noreturn]] void ExitAfterVulkanFailure(const char* reason);
// Runs one frame: pumps window events, submits a display list, presents.
// Returns false once the window has been closed.
bool RunFrame(void* gfxCommands);
bool TrackFrame(void* gfxCommands);
// RunFrame with interpolated matrices (an std::unordered_map<Mtx*, MtxF>, or null); subFrame: the display list drawn
// again for an in-between frame (interp.cpp)
bool RunFrameWith(void* gfxCommands, const void* mtxReplacements, bool subFrame, bool readBack = false);
// saves the picture just drawn as a PPM file
void SaveScreen(const char* path);
// how many frames RunFrameWith has drawn so far (the web build's window loop, main.cpp)
uint64_t FramesDrawn();
void Shutdown();

// Cartridge ROM image the game reads through PI DMA (osEPiStartDma).
bool LoadRom(const char* path);
// a file in the app's data folder (next to pokestadium.cfg.json: SHIP_HOME, e.g. the macOS app's Application Support)
std::string AppPath(const char* name);
// The game picture's shape (Engine.cpp): 0 = 4:3, 1 = 16:9. ApplyAspect hands it to the renderer.
int AspectSetting();
void ApplyAspect();
// The internal resolution (Engine.cpp; the launcher's Graphics > Internal resolution): how many pixels the game is
// drawn with, and how that picture is scaled to the window. ApplyAspect applies it as well.
//   mode kResWindow: Match window, the window's size in pixels (HiDPI displays' too; the default)
//        kResN64: the N64's 240 lines times multiple (1 = native: 320 x 240, 16:9: 426 x 240)
//        kResHeight: a fixed height (480, 720, 1080, 1440, 2160 lines)
//   upscale (Scaling; not used by Match window) kUpscaleExact: Integer, whole multiples only (sharp pixels, black
//           borders where they do not fill the window), kUpscaleSharp: fills the window, nearest, kUpscaleSmooth: linear
// CVars gPort.Resolution.Mode / .Multiple / .Height / .Upscale; PORT_RESOLUTION (window, window:50 (a scale, tests
// only), native, 2x..6x, 480p..2160p) and PORT_UPSCALE (exact, sharp, smooth) override them (tests).
enum { kResWindow = 0, kResN64 = 1, kResHeight = 2 };
enum { kUpscaleExact = 0, kUpscaleSharp = 1, kUpscaleSmooth = 2 };
struct ResolutionSetting {
    int mode = kResWindow;
    int multiple = 1;
    int height = 1080;
    float scale = 1.0f;
    int upscale = kUpscaleSmooth;
};
ResolutionSetting GetResolutionSetting();
void SetResolutionSetting(const ResolutionSetting& r); // stores the CVars (not saved) and applies them
// "Native", "2x", "720p", ... with the pixel size for the current shape ("3x   960 x 720"); "Match window"
std::string ResolutionLabel(int mode, int value);
// the picture's size in pixels the renderer is drawing with now (0 x 0 before the first frame)
void RenderSize(int* w, int* h);
// Hands the launcher's Sound settings (CVars gSound.*) to the audio paths (sound_settings.h). At start and on change.
void ApplySoundSettings();
// the game window (after Init), for the launcher
std::shared_ptr<Fast::Fast3dWindow> GetWindow();
// debug hook, called once per frame (game: VI frame, launcher: its frame): PORT_DEBUG_MINIMIZE=<frame>:<restore frame>
// minimizes the window at the first frame and restores it at the second, to test the renderers' minimized path
void DebugMinimize(unsigned long long frame);
// starts building the shader programs earlier runs drew, and records new ones (shader_warmup.cpp; after Init)
bool RunShaderWarmup();
void BeginShaderWarmup();
bool PumpShaderWarmup();
const uint8_t* RomData();
size_t RomSize();

// Executes queued RSP tasks (display lists) on the window thread and posts SP/DP completion.
void PumpRspTasks();
// Waits until an RSP task is queued, at most the given time.
bool WaitForRspTasks(std::chrono::milliseconds timeout); // true: a task is queued
// Pumps window events; returns false once the window is closed.
bool PumpFrame();
void StopDevices();
// Called with the latest pad state read on the window thread; game threads see it via osContGetReadData.
void PublishPads(const void* pads);
} // namespace Port

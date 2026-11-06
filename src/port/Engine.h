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
// Hands the launcher's Sound settings (CVars gSound.*) to the audio paths (sound_settings.h). At start and on change.
void ApplySoundSettings();
// the game window (after Init), for the launcher
std::shared_ptr<Fast::Fast3dWindow> GetWindow();
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

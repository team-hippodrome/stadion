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
// Runs one frame: pumps window events, submits a display list, presents.
// Returns false once the window has been closed.
bool RunFrame(void* gfxCommands);
bool TrackFrame(void* gfxCommands);
// RunFrame with interpolated matrices (an std::unordered_map<Mtx*, MtxF>, or null); subFrame: the display list drawn
// again for an in-between frame (interp.cpp)
bool RunFrameWith(void* gfxCommands, const void* mtxReplacements, bool subFrame, bool readBack = false);
// saves the picture just drawn as a PPM file
void SaveScreen(const char* path);
void Shutdown();

// Cartridge ROM image the game reads through PI DMA (osEPiStartDma).
bool LoadRom(const char* path);
// a file in the app's data folder (next to pokestadium.cfg.json: SHIP_HOME, e.g. the macOS app's Application Support)
std::string AppPath(const char* name);
// the game window (after Init), for the launcher
std::shared_ptr<Fast::Fast3dWindow> GetWindow();
// starts building the shader programs earlier runs drew, and records new ones (shader_warmup.cpp; after Init)
void StartShaderWarmup();
const uint8_t* RomData();
size_t RomSize();

// Executes queued RSP tasks (display lists) on the window thread and posts SP/DP completion.
void PumpRspTasks();
// Waits until an RSP task is queued, at most the given time.
void WaitForRspTasks(std::chrono::milliseconds timeout);
// Pumps window events; returns false once the window is closed.
bool PumpFrame();
void StopDevices();
// Called with the latest pad state read on the window thread; game threads see it via osContGetReadData.
void PublishPads(const void* pads);
} // namespace Port

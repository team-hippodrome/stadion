#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

// Owns the libultraship context and window for the Pokemon Stadium port.
namespace Port {
bool Init();
// Runs one frame: pumps window events, submits a display list, presents.
// Returns false once the window has been closed.
bool RunFrame(void* gfxCommands);
bool TrackFrame(void* gfxCommands);
void Shutdown();

// Cartridge ROM image the game reads through PI DMA (osEPiStartDma).
bool LoadRom(const char* path);
const uint8_t* RomData();
size_t RomSize();

// Executes queued RSP tasks (display lists) on the window thread and posts SP/DP completion.
void PumpRspTasks();
// Pumps window events; returns false once the window is closed.
bool PumpFrame();
void StopDevices();
// Called with the latest pad state read on the window thread; game threads see it via osContGetReadData.
void PublishPads(const void* pads);
} // namespace Port

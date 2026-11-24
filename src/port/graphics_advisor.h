// Recommended graphics settings for the machine the game runs on (the launcher's Graphics > Recommended, and new
// configurations). What a picture costs the graphics chip grows with the pixels drawn times the anti-aliasing samples;
// Smooth motion draws twice as many pictures (scratch measurements: Metal and OpenGL, MSAA 1/2/4, window and N64
// sizes, 720p and 1440p). So the chip is put in a class from what the renderer reports (name, integrated or a card of
// its own, memory), each class gets a budget of "effective megapixels" per picture at 60 pictures a second, and the
// best setting that fits the budget and the chip's memory is chosen.
#ifndef PORT_GRAPHICS_ADVISOR_H
#define PORT_GRAPHICS_ADVISOR_H

#include <cstdint>
#include <string>

namespace Port {

// What is known about the graphics chip and the machine (QueryGpuFacts)
struct GpuFacts {
    std::string name, vendor;    // the renderer's names (vendor: OpenGL only)
    uint32_t vendorId = 0;       // PCI vendor (D3D, Vulkan)
    bool integrated = false, discrete = false, unifiedMemory = false;
    uint64_t memoryBytes = 0;    // what the chip can use well; 0 if the API does not say
    uint64_t systemRamBytes = 0; // the machine's memory
};

enum GpuClass { kGpuVeryWeak, kGpuWeak, kGpuMedium, kGpuStrong, kGpuVeryStrong };
const char* GpuClassName(GpuClass c);
GpuClass ClassifyGpu(const GpuFacts& gpu);

// A choice of the Graphics panel's settings (Engine.h ResolutionSetting modes; gMSAAValue; gPort.Interpolate)
struct GraphicsPick {
    int resMode = 0, resValue = 0; // kResWindow / kResN64 (value: multiple) / kResHeight (value: lines)
    int msaa = 4;
    bool smooth = true;
    GpuClass gpuClass = kGpuMedium;
    double cost = 0;          // effective megapixels per picture
    uint64_t memoryBytes = 0; // estimated graphics memory for the game's pictures
};

// The best setting for a window of targetW x targetH pixels (wide: the 16:9 picture)
GraphicsPick PickGraphics(const GpuFacts& gpu, int targetW, int targetH, bool wide);

// The running renderer's chip and the machine (valid once the window exists)
GpuFacts QueryGpuFacts();
// PickGraphics for the game window: the display's size when the game is set to fullscreen, else the window's
GraphicsPick RecommendedGraphics();
// One line for people: "Apple M2 (strong) at 2560 x 1440: Match window, 4x anti-aliasing, Smooth motion on"
std::string DescribePick(const GpuFacts& gpu, const GraphicsPick& pick);

// Prints picks for a table of example chips and window sizes (--graphics-advice-table)
void PrintGraphicsAdviceTable();

} // namespace Port

#endif

#pragma once
// What the enhancements' ImGui overlays share (summary_bars.cpp, memory_screen.cpp; render thread).
#include <imgui.h>
#include <cstdint>

extern "C" const char* Port_TextPortString(const char* id); // text_lookup.cpp (the game's encoding)
extern "C" int Port_TextToUtf8(const char* text, char* out, int size);

namespace Port {
int AspectSetting();            // Engine.cpp: 0 = 4:3, 1 = 16:9
ImFont* CardFont(float pixels); // launcher.cpp
} // namespace Port

namespace PortOverlay {
// A card icon's texture (Port_Enh_CardIcon), uploaded on first use; nullptr if there is none.
ImTextureID IconTexture(uint32_t key);
ImU32 Rgba(int r, int g, int b, int a);
// A port string as UTF-8 (port.csv; the text language's, else English).
const char* Str(const char* id);
// Text at `size` pixels, squeezed down (to 70%) when it is wider than `maxw`, then clipped.
void Text(ImDrawList* dl, ImFont* f, float x, float y, float size, float maxw, const char* s, ImU32 col);
// Adds the trainer memory screen's window to the Gui (Port_Enh_InstallOverlay).
void InstallMemoryScreen(void* gui);
} // namespace PortOverlay

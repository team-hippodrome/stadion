#pragma once
// Frame interpolation (interp.cpp): in-between frames at the display's refresh rate for scenes the game draws at 30 or
// 20 fps. Off by default; PORT_INTERPOLATE=1 or the launcher's option (gPort.Interpolate) turns it on.
#include <stdbool.h>
#include <stdint.h>

#include "StadionPort.h" // Port_InterpBeginFrame, Port_InterpNode

#ifdef __cplusplus
extern "C" {
#endif

// A matrix put into the display list being built (gSPMatrix -> Port_GfxPrepareMtx), from that call site.
void Port_InterpTagMtx(const void* mtx, const void* site);
// The game's segment or fragment maps changed (Port_GfxFlushCache): addresses in older display lists may be stale.
void Port_InterpMapsChanged(void);

#ifdef __cplusplus
}

namespace Port {
// Window thread. Draws a display list the game submitted: with interpolation on, as the first of several in-between
// frames (exact: the frame as built, for screenshots). Replaces RunFrame for game frames.
void RunGameFrame(void* gfxCommands, bool exact);
// Draws the next in-between frame when its refresh has come (main loop).
void PumpInterpolation();
// Something else was shown (a CPU-drawn frame, a frame drawn for its state only): no more in-between frames.
void CancelInterpolation();
} // namespace Port
#endif

#pragma once
// Frame pacing: the display's refresh clock and the PORT_TRACE_PACING statistics (pacing.cpp, pacing_mac.mm).
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// steady_clock time in nanoseconds (the clock the lockstep clock sleeps on)
int64_t Port_SteadyNs(void);

// The refresh grid of the display the game window is on: true with the refresh period and the time of one vblank
// (steady clock); false when the platform does not report it.
bool Port_DisplayVblank(int64_t* periodNs, int64_t* vblankNs);
// Window thread: follows the window to the display it is on (the first call starts the display clock).
void Port_DisplayFollowWindow(void* sdlWindow);

// Window thread, right before a game frame is drawn and presented: Android sets when it is to appear
// (pacing_android.cpp); nothing elsewhere.
void Port_BeforePresent(void);

// Makes the calling thread's sleeps end on time (the thread that delivers VI retraces).
void Port_PreciseTimingThread(void);

// PORT_TRACE_PACING=1 statistics: interval percentiles and rhythm per event kind, printed every 600 retraces.
enum { PORT_PACE_RETRACE, PORT_PACE_DRAWN, PORT_PACE_SHOWN, PORT_PACE_KINDS };
bool Port_PacingTrace(void);
void Port_PacingNote(int kind, int64_t tNs);

#ifdef __cplusplus
}
#endif

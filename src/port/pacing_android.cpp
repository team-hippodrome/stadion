// Android: the refresh clock of the display (AChoreographer's vsync callbacks), for the lockstep clock's retraces
// (os_devices.cpp, RetraceSchedule). Without it retraces ran on a free-running 16.67 ms clock that beat against the
// display: frames drawn late in a refresh missed it, and 30 fps scenes alternated 1 and 3 refreshes per frame. (The
// AYN Thor's panel reports 60 Hz but refreshes at ~59.35 Hz: the measured period, not the reported one, counts.)
#include "pacing.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/choreographer.h>
#include <android/looper.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {
// The refresh grid, smoothed as on macOS (pacing_mac.mm): single callbacks come late when the looper thread is
// scheduled late, so the phase follows them slowly and the period is measured over many refreshes.
std::mutex gGridLock;
int64_t gAnchor = 0; // steady-clock time of a vblank
double gPeriod = 0;  // ns per refresh
int64_t gBaseT = 0;  // first timestamp of the period measurement
double gBaseN = 0;   // refreshes since then
int gMisses = 0;     // timestamps far off the grid in a row (a refresh-rate switch: 60 <-> 120 Hz)
int64_t gLast = 0;   // previous callback's timestamp
int64_t gDeltas[8] = {};
int gDeltaCount = 0;

// the shortest of the recent callback intervals: one refresh (callbacks that came late skip refreshes, never add any)
double RecentRefresh() {
    int n = std::min(gDeltaCount, 8);
    int64_t best = 0;
    for (int i = 0; i < n; i++) {
        if (gDeltas[i] > 2000000 && (best == 0 || gDeltas[i] < best)) best = gDeltas[i];
    }
    return (double)best;
}

void OnVsync(int64_t t) {
    std::lock_guard<std::mutex> g(gGridLock);
    if (gLast != 0) gDeltas[gDeltaCount++ % 8] = t - gLast;
    gLast = t;
    if (gAnchor != 0 && gPeriod > 0) {
        double cycles = std::round((double)(t - gAnchor) / gPeriod);
        int64_t predicted = gAnchor + (int64_t)(cycles * gPeriod);
        int64_t err = t - predicted;
        if (std::llabs(err) < gPeriod / 4) {
            gMisses = 0;
            gAnchor = predicted + err / 16;
            gBaseN += cycles;
            if (gBaseN >= 600) gPeriod = (double)(gAnchor - gBaseT) / gBaseN;
            return;
        }
        if (++gMisses < 30) return;
    }
    // start (again) once a few intervals are known: their refresh, phase from this timestamp
    if (gDeltaCount < 8) return;
    double refresh = RecentRefresh();
    if (refresh <= 0) return;
    if (Port_PacingTrace() && std::fabs(refresh - gPeriod) > 100000) {
        fprintf(stderr, "[pacing] display clock: Choreographer, %.2f Hz\n", 1e9 / refresh);
    }
    gPeriod = refresh;
    gAnchor = t;
    gBaseT = t;
    gBaseN = 0;
    gMisses = 0;
}

void FrameCallback(int64_t frameTimeNanos, void* data) {
    OnVsync(frameTimeNanos); // CLOCK_MONOTONIC, the clock steady_clock reads
    AChoreographer_postFrameCallback64(static_cast<AChoreographer*>(data), FrameCallback, data);
}

void StartClock() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            ALooper_prepare(0);
            AChoreographer* c = AChoreographer_getInstance();
            if (c == nullptr) return;
            AChoreographer_postFrameCallback64(c, FrameCallback, c);
            while (ALooper_pollOnce(-1, nullptr, nullptr, nullptr) != ALOOPER_POLL_ERROR) {
            }
        }).detach();
    });
}
} // namespace

extern "C" bool Port_DisplayVblank(int64_t* periodNs, int64_t* vblankNs) {
    StartClock();
    std::lock_guard<std::mutex> g(gGridLock);
    if (gAnchor == 0 || gPeriod <= 0) return false;
    *periodNs = (int64_t)gPeriod;
    *vblankNs = gAnchor;
    return true;
}

// The frame about to be presented (window thread, before the swap) appears at the second refresh after the retrace
// that started it, never earlier: a frame whose draw and swap took 15 ms showed one refresh later than one that took
// 18 ms, so 30 fps scenes alternated 1 and 3 refreshes per frame whenever draws took about a refresh (a busy device).
// The frame then waits in the queue when it is early (eglPresentationTimeANDROID, what Android's Swappy does).
// PORT_PRESENT_REFRESHES=n sets the refreshes (0: off).
extern "C" bool Port_LastRetraceVblank(int64_t* vblankNs, int64_t* periodNs);

extern "C" void Port_BeforePresent(void) {
    static const int refreshes = getenv("PORT_PRESENT_REFRESHES") ? atoi(getenv("PORT_PRESENT_REFRESHES")) : 2;
    static auto presentationTime =
        (PFNEGLPRESENTATIONTIMEANDROIDPROC)eglGetProcAddress("eglPresentationTimeANDROID");
    int64_t vblank, period;
    if (refreshes <= 0 || presentationTime == nullptr || !Port_LastRetraceVblank(&vblank, &period)) {
        return;
    }
    // (a little before that refresh: SurfaceFlinger shows a buffer at the first refresh at or after its time)
    int64_t at = vblank + refreshes * period - period / 4;
    presentationTime(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), (EGLnsecsANDROID)at);
}

// The thread that delivers VI retraces: a display-urgent priority (as Android's own UI and render threads use), so
// that its wake-ups are not held back behind the game's threads.
extern "C" void Port_PreciseTimingThread(void) {
    if (setpriority(PRIO_PROCESS, (id_t)gettid(), -8) != 0 && Port_PacingTrace()) {
        fprintf(stderr, "[pacing] could not raise the retrace thread's priority\n");
    }
}

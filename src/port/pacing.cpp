// Frame pacing helpers: which display the window is on, and the PORT_TRACE_PACING statistics.
// The display's refresh clock itself is platform code (pacing_mac.mm, pacing_android.cpp; other platforms do not report
// it yet).
#include "pacing.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
#include "StadionPort.h"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#endif

extern "C" int64_t Port_SteadyNs(void) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

#if defined(__APPLE__)
void Port_MacFollowDisplay(double x, double y); // pacing_mac.mm: the display at that global point (points)
#endif
#if !defined(__ANDROID__)
extern "C" void Port_BeforePresent(void) {
}
#endif
#if !defined(__APPLE__) && !defined(__ANDROID__)
extern "C" bool Port_DisplayVblank(int64_t*, int64_t*) {
    return false;
}
#if defined(_WIN32)
// Windows ends sleeps on its timer tick, 15.6 ms by default (SDL3 no longer raises it, and the C++ runtime's
// sleep_for/sleep_until is Sleep(ms)): the clock's retrace waits and its 20 µs polls for an idle game then took up to a
// tick each, so retraces came in bursts (presents 30-50 ms apart) and the sound stream ran short. 1 ms ticks for the
// process, as long as it runs, and a thread that is scheduled ahead of the game's.
extern "C" void Port_PreciseTimingThread(void) {
    static const bool sPeriod = timeBeginPeriod(1) == TIMERR_NOERROR;
    (void)sPeriod;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}
#else
extern "C" void Port_PreciseTimingThread(void) {
}
#endif
#endif

extern "C" void Port_DisplayFollowWindow(void* sdlWindow) {
    auto* w = static_cast<SDL_Window*>(sdlWindow);
    static int64_t nextCheck = 0;
    int64_t now = Port_SteadyNs();
    if (w == nullptr || now < nextCheck) return;
    nextCheck = now + 500000000; // twice a second: the window may move to another display
    SDL_DisplayID id = SDL_GetDisplayForWindow(w);
    static SDL_DisplayID last = 0;
    if (id != 0 && id != last) {
        last = id;
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(id);
        if (Port_PacingTrace() && mode) {
            fprintf(stderr, "[pacing] window on display %u (%s): %dx%d at %.2f Hz\n", (unsigned)id,
                    SDL_GetDisplayName(id) ? SDL_GetDisplayName(id) : "?", mode->w, mode->h, mode->refresh_rate);
        }
    }
#if defined(__APPLE__)
    int x = 0, y = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &ww, &wh);
    Port_MacFollowDisplay(x + ww / 2.0, y + wh / 2.0);
#endif
}

// ---- PORT_TRACE_PACING statistics ----
// Per event kind (VI retrace delivered to the game, frame drawn and handed to the GPU, frame shown on the display):
// interval percentiles, the share of intervals off the 60 Hz grid (more than 2 ms from a whole number of 1/60 s), and
// rhythm breaks (an interval of a different number of 1/60 s than the one before: 2,2,2 is even, 1,3,1,3 stutters).
namespace {
constexpr double kFrameMs = 1000.0 / 60.0;
constexpr int kHist = 5; // intervals of 1, 2, 3, 4, 5+ frames

struct Stats {
    std::vector<float> ms;
    uint64_t offGrid = 0, breaks = 0, hist[kHist] = {};
};

struct Series {
    const char* name;
    int64_t last = 0;
    int lastFrames = -1;
    Stats window, total;
};

std::mutex gStatsLock;
Series gSeries[PORT_PACE_KINDS] = { { "retrace" }, { "drawn" }, { "shown" } };
uint64_t gRetraces = 0;

void Add(Stats& s, float ms, int frames, bool off, bool brk) {
    s.ms.push_back(ms);
    s.offGrid += off;
    s.breaks += brk;
    if (frames >= 1) s.hist[std::min(frames, kHist) - 1]++;
}

void Print(const char* scope, const Series& se, const Stats& s) {
    size_t n = s.ms.size();
    if (n == 0) return;
    std::vector<float> v = s.ms;
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) { return v[std::min(n - 1, (size_t)(p * (n - 1) + 0.5))]; };
    char hist[128];
    int len = 0;
    for (int k = 0; k < kHist; k++) {
        if (s.hist[k]) {
            len += snprintf(hist + len, sizeof hist - len, " %s%d:%.1f%%", k == kHist - 1 ? ">=" : "", k + 1,
                            100.0 * s.hist[k] / n);
        }
    }
    hist[len] = '\0';
    fprintf(stderr,
            "[pacing] %-6s %-7s n %5zu  p50 %5.1f  p95 %5.1f  p99 %5.1f  max %6.1f ms  off-grid %5.1f%%  rhythm "
            "breaks %5.1f%%  frames%s\n",
            scope, se.name, n, pct(0.5), pct(0.95), pct(0.99), v.back(), 100.0 * s.offGrid / n, 100.0 * s.breaks / n,
            hist);
}
} // namespace

extern "C" bool Port_PacingTrace(void) {
    static const bool on = getenv("PORT_TRACE_PACING") != nullptr;
    return on;
}

extern "C" void Port_PacingNote(int kind, int64_t tNs) {
    if (!Port_PacingTrace() || kind < 0 || kind >= PORT_PACE_KINDS) return;
    std::lock_guard<std::mutex> g(gStatsLock);
    Series& se = gSeries[kind];
    if (se.last != 0 && tNs > se.last) {
        float ms = (float)((tNs - se.last) / 1e6);
        static const bool sRaw = strcmp(getenv("PORT_TRACE_PACING"), "2") == 0; // =2: every interval as well
        if (sRaw) { // with the phase in the display's refresh (ms after its vblank)
            int64_t period, vblank;
            double phase = -1;
            if (Port_DisplayVblank(&period, &vblank)) phase = (double)(((tNs - vblank) % period + period) % period) / 1e6;
            fprintf(stderr, "[pacing] %s %.2f phase %.2f at %.3f\n", se.name, ms, phase, tNs / 1e6);
        }
        if (ms > 1000) { // a stall (a scene loading, the window hidden): not part of the rhythm
            se.lastFrames = -1;
        } else {
            int frames = (int)std::lround(ms / kFrameMs);
            bool off = frames == 0 || std::fabs(ms - frames * kFrameMs) > 2.0;
            bool brk = se.lastFrames >= 0 && frames != se.lastFrames;
            se.lastFrames = frames;
            Add(se.window, ms, frames, off, brk);
            Add(se.total, ms, frames, off, brk);
        }
    }
    if (tNs > se.last) se.last = tNs;
    if (kind == PORT_PACE_RETRACE && ++gRetraces % 600 == 0) { // every 10 s of game time
        fprintf(stderr, "[pacing] frame %llu:\n", (unsigned long long)Port_ViTick());
        for (Series& s : gSeries) {
            Print("10s", s, s.window);
            s.window = Stats();
        }
        for (Series& s : gSeries) {
            Print("total", s, s.total);
        }
    }
}

// macOS: the refresh clock of the display the game window is on (CVDisplayLink), and with PORT_TRACE_PACING=1 the
// time each frame actually appeared on the display (Metal's presented handler).
#include "pacing.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreVideo/CoreVideo.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <objc/runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#pragma clang diagnostic ignored "-Wdeprecated-declarations" // CVDisplayLink: still the C API for a display's vblanks

namespace {
// The refresh grid, smoothed: CVDisplayLink's timestamps jitter by a few ms when the system is busy (its rate estimate
// read anywhere from 59.3 to 61.2 Hz), and retraces scheduled from them directly moved around with them.
std::mutex gGridLock;
int64_t gAnchor = 0;    // host time of a vblank
double gPeriod = 0;     // host ticks per refresh
int64_t gBaseT = 0;     // first timestamp of the period measurement
double gBaseN = 0;      // refreshes since then (for the long-run period)
int gMisses = 0;        // timestamps far off the grid in a row (another display, a mode change)
CVDisplayLinkRef gLink = nullptr;
CGDirectDisplayID gDisplay = 0;

const mach_timebase_info_data_t& Timebase() {
    static mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t;
        mach_timebase_info(&t);
        return t;
    }();
    return tb;
}

int64_t HostToNs(int64_t host) {
    return (int64_t)((__int128)host * Timebase().numer / Timebase().denom);
}

// a host time as steady-clock time
int64_t HostToSteady(int64_t host) {
    return Port_SteadyNs() - HostToNs((int64_t)mach_absolute_time() - host);
}

CVReturn OnVblank(CVDisplayLinkRef link, const CVTimeStamp*, const CVTimeStamp* out, CVOptionFlags, CVOptionFlags*, void*) {
    if (!(out->flags & kCVTimeStampHostTimeValid)) return kCVReturnSuccess;
    int64_t t = (int64_t)out->hostTime; // when this refresh reaches the display: a point on the vblank grid
    std::lock_guard<std::mutex> g(gGridLock);
    if (gAnchor != 0 && gPeriod > 0) {
        double cycles = std::round((double)(t - gAnchor) / gPeriod);
        int64_t predicted = gAnchor + (int64_t)(cycles * gPeriod);
        int64_t err = t - predicted;
        if (std::llabs(err) < gPeriod / 4) {
            gMisses = 0;
            gAnchor = predicted + err / 16; // follow the phase slowly: single timestamps are noisy
            gBaseN += cycles;
            if (gBaseN >= 600) { // the long-run period, from 10 s and more of refreshes
                gPeriod = (double)(gAnchor - gBaseT) / gBaseN;
            }
            return kCVReturnSuccess;
        }
        if (++gMisses < 30) return kCVReturnSuccess;
    }
    // start (again): the nominal period, phase from this timestamp
    CVTime nominal = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
    if (nominal.timeValue <= 0 || nominal.timeScale <= 0) return kCVReturnSuccess;
    gPeriod = (double)nominal.timeValue / nominal.timeScale * 1e9 * Timebase().denom / Timebase().numer;
    gAnchor = t;
    gBaseT = t;
    gBaseN = 0;
    gMisses = 0;
    return kCVReturnSuccess;
}

// PORT_TRACE_PACING=1: every drawable the renderer takes from the layer reports when it was shown
IMP gNextDrawable = nullptr;
id NextDrawableTraced(id self, SEL cmd) {
    id<CAMetalDrawable> d = ((id(*)(id, SEL))gNextDrawable)(self, cmd);
    [d addPresentedHandler:^(id<MTLDrawable> shown) {
        CFTimeInterval t = shown.presentedTime; // 0: never shown (replaced before a vblank)
        if (t > 0) Port_PacingNote(PORT_PACE_SHOWN, HostToSteady((int64_t)(t * 1e9 * Timebase().denom / Timebase().numer)));
    }];
    return d;
}
} // namespace

void Port_MacFollowDisplay(double x, double y) {
    CGDirectDisplayID id = 0;
    uint32_t count = 0;
    if (CGGetDisplaysWithPoint(CGPointMake(x, y), 1, &id, &count) != kCGErrorSuccess || count == 0) {
        id = CGMainDisplayID();
    }
    if (gLink == nullptr) {
        if (Port_PacingTrace()) {
            Method m = class_getInstanceMethod([CAMetalLayer class], @selector(nextDrawable));
            if (m) gNextDrawable = method_setImplementation(m, (IMP)NextDrawableTraced);
        }
        if (CVDisplayLinkCreateWithCGDisplay(id, &gLink) != kCVReturnSuccess) {
            gLink = nullptr;
            return;
        }
        CVDisplayLinkSetOutputCallback(gLink, OnVblank, nullptr);
        CVDisplayLinkStart(gLink);
        gDisplay = id;
    } else if (id != gDisplay) {
        {
            std::lock_guard<std::mutex> g(gGridLock);
            gAnchor = 0; // the new display's grid arrives with its next refresh
        }
        CVDisplayLinkSetCurrentCGDisplay(gLink, id);
        gDisplay = id;
    } else {
        return;
    }
    if (Port_PacingTrace()) {
        CVTime t = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(gLink);
        fprintf(stderr, "[pacing] display clock: CG display %u, nominal %.3f Hz\n", (unsigned)id,
                t.timeValue > 0 ? (double)t.timeScale / t.timeValue : 0.0);
    }
}

extern "C" bool Port_DisplayVblank(int64_t* periodNs, int64_t* vblankNs) {
    int64_t anchor;
    double period;
    {
        std::lock_guard<std::mutex> g(gGridLock);
        anchor = gAnchor;
        period = gPeriod;
    }
    if (anchor == 0 || period <= 0) return false;
    *periodNs = (int64_t)(period * Timebase().numer / Timebase().denom);
    *vblankNs = HostToSteady(anchor);
    return true;
}

// The thread that delivers VI retraces wakes when a retrace is due. As an ordinary thread its timers were coalesced:
// wake-ups came up to 7.5 ms late, which moved retraces around in the display's refresh. A time-constraint (real-time)
// policy, as audio threads use, makes them precise; it asks for little: about 1 ms of computation per 1/60 s.
extern "C" void Port_PreciseTimingThread(void) {
    const mach_timebase_info_data_t& tb = Timebase();
    auto msToHost = [&](double ms) { return (uint32_t)(ms * 1e6 * tb.denom / tb.numer); };
    thread_time_constraint_policy_data_t policy;
    policy.period = msToHost(1000.0 / 60);
    policy.computation = msToHost(1.0);
    policy.constraint = msToHost(2.0);
    policy.preemptible = TRUE;
    kern_return_t r = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                                        (thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (r != KERN_SUCCESS) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    // and no App Nap or timer coalescing for the process while the game runs (also when its window is in the background)
    static id sActivity = [[[NSProcessInfo processInfo]
        beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
                          reason:@"game frame pacing"] retain];
    (void)sActivity;
}

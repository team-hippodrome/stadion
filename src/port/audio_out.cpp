// The N64 audio interface (AI) for the game's own sound (the GB Tower has its own path: gb_audio_out.cpp).
//
// The game paces its audio frames by the AI: each frame it queues the previous frame's buffer (osAiSetNextBuffer)
// and picks the next frame's length from the bytes left in the current DMA (osAiGetLength; 552 or 368 samples, so
// that on average it produces what the DAC consumes). The AI here is virtual: a two-entry DMA FIFO drained by VI
// frames at the DAC rate, like the hardware, so what the game produces depends only on game time (lockstep runs and
// replays stay deterministic) and never on the host's sound device.
//
// Accepted buffers are copied to an SDL3 audio stream. Playback starts once the queue reaches its target. The playback
// ratio then follows the fill level to absorb the difference between host frame pacing and the N64 DAC rate. If the
// stream falls far behind, the queue is dropped and filled again instead of lagging. A queue that ran dry without a
// long stall raises the target (a host whose frames come unevenly, e.g. a slow GPU; see PORTING_NOTES, Slow GPUs).
// Output is on at normal speed; PORT_SPEED other than 1 turns it off, PORT_SOUND=0/1 overrides.
// The launcher's Sound settings (sound_settings.h) set the stream's gain (master, Sound off, background mute) and
// the queue target (Sound delay).
// PORT_AUDIO_DUMP=<file> writes everything the AI accepts as raw 16-bit stereo PCM at the AI rate (host byte
// order; the rate is printed as "[audio] rate"), plus <file>.frames with the VI frame of each buffer.
#include <SDL3/SDL.h>
#include "web_audio.h" // (web build: an AudioWorklet instead of SDL's audio device)
#include "sound_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include "StadionPort.h"

extern "C" double Port_Speed();
extern "C" void Port_GbAudioSetFrequency(int32_t freq);

namespace {

constexpr double kViClock = 48681812.0; // NTSC
constexpr double kViRate = kViClock / (3094.0 * 263.0); // ~59.826 frames per second (ares' NTSC timing)

std::mutex gLock;
int gFreq = 0;           // DAC rate in Hz (osViClock / dacRate)
uint32_t gCur = 0;       // bytes left in the playing DMA
uint32_t gNext = 0;      // bytes of the queued DMA (0: FIFO slot free)
uint64_t gTick = 0;      // VI frame of the last update
double gFrac = 0;        // fractional bytes drained

int gEnabled = -1;
SDL_AudioStream* gStream = nullptr;
int gStreamFreq = 0;
#if defined(__APPLE__)
constexpr double kDefaultQueueTarget = 0.045;
#else
constexpr double kDefaultQueueTarget = 0.070;
#endif
double gQueueTarget = kDefaultQueueTarget;
constexpr double kMaxQueueTarget = 0.120; // the most the target grows to after underruns (s)
double gRatio = 1.0;
double gRateIntegral = 0.0;
double gQueueAvg = 0.0;   // smoothed fill level (s)
uint64_t gLastOutNs = 0;  // host time of the previous Output (SDL_GetTicksNS)
bool gPlaying = false;    // the device runs (paused while the queue fills up)
float gGain = -1.0f;      // the stream's gain as last set (-1: not set on this stream)
FILE* gDump = nullptr;
FILE* gDumpFrames = nullptr;

bool Enabled() {
    if (gEnabled < 0) {
        const char* e = getenv("PORT_SOUND");
        if (e != nullptr) {
            gEnabled = atoi(e) != 0;
        } else {
            gEnabled = Port_Speed() == 1.0;
        }
    }
    return gEnabled != 0;
}

void Advance() {
    uint64_t tick = Port_ViTick();
    if (tick <= gTick) {
        return;
    }
    if (gFreq > 0) {
        gFrac += (double)(tick - gTick) * gFreq / kViRate * 4.0;
    }
    gTick = tick;
    uint64_t drain = (uint64_t)gFrac & ~(uint64_t)3;
    gFrac -= (double)drain;
    while (drain > 0 && gCur > 0) {
        uint64_t n = std::min<uint64_t>(drain, gCur);
        gCur -= (uint32_t)n;
        drain -= n;
        if (gCur == 0) {
            gCur = gNext;
            gNext = 0;
        }
    }
    if (gCur == 0) {
        gFrac = 0; // the DAC idles (no carry-over into the next buffer)
    }
}

void OpenStream() {
    if (gStream != nullptr && gStreamFreq == gFreq) {
        return;
    }
    if (gStream != nullptr) {
        SDL_DestroyAudioStream(gStream);
        gStream = nullptr;
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "[audio] SDL audio init failed: %s\n", SDL_GetError());
        gEnabled = 0;
        return;
    }
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, gFreq };
#if defined(__APPLE__)
    // SDL's default Core Audio period can exceed a game retrace. Request a
    // shorter period for this stream and respect explicit SDL overrides.
    const bool setPeriod = SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES) == nullptr &&
                           SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
#endif
    gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
#if defined(__APPLE__)
    if (setPeriod) {
        SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
    }
#endif
    if (gStream == nullptr) {
        fprintf(stderr, "[audio] no audio device: %s\n", SDL_GetError());
        gEnabled = 0;
        return;
    }
    gStreamFreq = gFreq;
#if defined(__APPLE__)
    // Let the device start pulling before the game submits its first sound.
    // A slow device startup would otherwise put the opening cue behind a
    // growing queue while the game continues to advance.
    if (SDL_Semaphore* ready = SDL_CreateSemaphore(0)) {
        if (SDL_SetAudioStreamGetCallback(gStream, [](void* data, SDL_AudioStream*, int, int) {
                SDL_SignalSemaphore(static_cast<SDL_Semaphore*>(data));
            }, ready)) {
            const uint64_t start = SDL_GetTicks();
            const bool started = SDL_ResumeAudioStreamDevice(gStream) && SDL_WaitSemaphoreTimeout(ready, 250);
            SDL_PauseAudioStreamDevice(gStream);
            SDL_SetAudioStreamGetCallback(gStream, nullptr, nullptr);
            if (getenv("PORT_TRACE_AUDIO_OUT")) {
                fprintf(stderr, "[audio-out] device %s after %llu ms\n", started ? "ready" : "start wait expired",
                        (unsigned long long)(SDL_GetTicks() - start));
            }
        }
        SDL_DestroySemaphore(ready);
    }
    // Budget one game retrace plus the device's request period, with a
    // small floor for scheduling jitter and the previous target as a fallback.
    SDL_AudioSpec deviceSpec = {};
    int periodFrames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(gStream), &deviceSpec, &periodFrames) &&
        deviceSpec.freq > 0 && periodFrames > 0) {
        const double period = static_cast<double>(periodFrames) / deviceSpec.freq;
        gQueueTarget = std::clamp(1.0 / kViRate + period, 0.023, kDefaultQueueTarget);
        if (getenv("PORT_TRACE_AUDIO_OUT")) {
            fprintf(stderr, "[audio-out] device period %.1f ms, target %.1f ms\n", period * 1000, gQueueTarget * 1000);
        }
    } else {
        gQueueTarget = kDefaultQueueTarget;
    }
#endif
    gQueueTarget = Port::SoundQueueTarget(gQueueTarget); // launcher > Sound > Sound delay
    if (getenv("PORT_TRACE_AUDIO_OUT")) fprintf(stderr, "[audio-out] queue target %.1f ms\n", gQueueTarget * 1000);
    gGain = -1.0f;
    gRatio = 1.0;
    gRateIntegral = 0.0;
    gLastOutNs = 0;
    gPlaying = false; // (the device stream opens paused: playback starts once the queue reaches its target)
}

void Output(const void* data, uint32_t bytes) {
    if (!Enabled() || gFreq <= 0) {
        return;
    }
    OpenStream();
    if (gStream == nullptr) {
        return;
    }
    // Playback speed follows the fill level: a PI control around the target, on the level smoothed over ~0.5 s (the
    // device takes the queue in chunks of several ms). A real device needs ~0.3 % (the host paces frames at 60 Hz, the
    // N64 at ~59.83 Hz); imprecise ones (SDL's disk driver: ~3 % slow) up to 3 %. The gains are per second and per
    // second of fill error: the loop settles in ~30 s, critically damped (ω ≈ 0.1/s, ζ ≈ 0.8), so the ratio moves by
    // hundredths of a percent, never audibly. (It used to step per buffer with a strong integral: an underdamped
    // ~10 s oscillation of up to ±1.5 %, a wobbling pitch.)
    constexpr double kMaxQueue = 0.280, kKp = 0.16, kKi = 0.01, kSmoothing = 0.5;
    const double bytesPerSecond = gFreq * 4.0;
    static const bool trace = getenv("PORT_TRACE_AUDIO_OUT") != nullptr;
    int queued = SDL_GetAudioStreamQueued(gStream);
    // After a stall drains the stream, refill once before resuming instead of playing a succession of short buffers.
    // Also recover from excessive backlog by dropping old audio.
    // SDL can retain input for resampling even when it has no playable output left.
    const bool drained = gPlaying && queued >= 0 && queued < static_cast<int>(bytes) &&
                         SDL_GetAudioStreamAvailable(gStream) == 0;
    const uint64_t now = SDL_GetTicksNS();
    const double sinceLast = gLastOutNs != 0 ? (now - gLastOutNs) / 1e9 : 0.0;
    if (drained || queued > kMaxQueue * bytesPerSecond) {
        if (trace) {
            fprintf(stderr, "[audio-out] frame %llu rebuffering (%s)\n", (unsigned long long)gTick,
                    drained ? "empty queue" : "excess backlog");
        }
        // The queue ran dry without a long stall (loading, which no queue covers): the host's frames come too
        // unevenly for the target (a slow GPU), so keep more queued from now on, up to kMaxQueueTarget.
        if (drained && sinceLast < 0.1 && gTick > 600 && gQueueTarget < kMaxQueueTarget) {
            gQueueTarget = std::min(gQueueTarget * 1.5, kMaxQueueTarget);
            if (trace) fprintf(stderr, "[audio-out] queue target raised to %.1f ms\n", gQueueTarget * 1000);
        }
        SDL_PauseAudioStreamDevice(gStream);
        SDL_ClearAudioStream(gStream);
        gPlaying = false;
        gRateIntegral = 0.0;
        gRatio = 1.0;
        SDL_SetAudioStreamFrequencyRatio(gStream, 1.0f);
        queued = 0;
    }
    const double dt = std::min(sinceLast, 0.1);
    gLastOutNs = now;
    if (gPlaying) {
        gQueueAvg += (queued / bytesPerSecond - gQueueAvg) * std::min(1.0, dt / kSmoothing);
        const double err = gQueueAvg - gQueueTarget; // s
        gRateIntegral = std::clamp(gRateIntegral + err * kKi * dt, -0.03, 0.03);
        gRatio = std::clamp(1.0 + gRateIntegral + std::clamp(err * kKp, -0.005, 0.005), 0.97, 1.03);
        SDL_SetAudioStreamFrequencyRatio(gStream, (float)gRatio);
    }
    const float gain = Port::SoundOutputGain(false); // master volume, Sound off, muted in the background
    if (gain != gGain) {
        SDL_SetAudioStreamGain(gStream, gain);
        gGain = gain;
    }
    SDL_PutAudioStreamData(gStream, data, (int)bytes);
    if (!gPlaying && SDL_GetAudioStreamQueued(gStream) >= gQueueTarget * bytesPerSecond) {
        gPlaying = true;
        gQueueAvg = gQueueTarget;
        SDL_ResumeAudioStreamDevice(gStream);
    }
    static uint64_t lastTrace = 0, put = 0, lastPut = 0;
    static double lastT = 0;
    put += bytes;
    if (trace && gTick >= lastTrace + 120) {
        double t = SDL_GetTicks() / 1000.0;
        uint64_t consumed = put - (uint64_t)SDL_GetAudioStreamQueued(gStream);
        fprintf(stderr, "[audio-out] t %.3f frame %llu queued %.1f ms (avg %.1f) ratio %.4f, played %.0f Hz\n", t,
                (unsigned long long)gTick, queued * 1000.0 / (gFreq * 4.0), gQueueAvg * 1000.0, gRatio, (consumed - lastPut) / 4.0 / (t - lastT));
        lastTrace = gTick;
        lastPut = consumed;
        lastT = t;
    }
}

void Dump(const void* data, uint32_t bytes) {
    static bool init = false;
    if (!init) {
        init = true;
        if (const char* p = getenv("PORT_AUDIO_DUMP")) {
            gDump = fopen(p, "wb");
            gDumpFrames = fopen((std::string(p) + ".frames").c_str(), "w");
        }
    }
    if (gDump != nullptr) {
        fwrite(data, 1, bytes, gDump);
        fflush(gDump);
        fprintf(gDumpFrames, "%llu %u\n", (unsigned long long)gTick, (unsigned)(bytes / 4));
        fflush(gDumpFrames);
    }
}

} // namespace

extern "C" {

// osAiSetFrequency as in libultra: the DAC divides the VI clock; returns the rate actually used.
int32_t osAiSetFrequency(uint32_t frequency) {
    if (frequency == 0) {
        return -1;
    }
    uint32_t dacRate = (uint32_t)(kViClock / (double)frequency + 0.5);
    if (dacRate < 132) {
        return -1;
    }
    int32_t rate = (int32_t)(kViClock / dacRate);
    {
        std::lock_guard<std::mutex> g(gLock);
        Advance();
        gFreq = rate;
        if (getenv("PORT_AUDIO_DUMP")) {
            fprintf(stderr, "[audio] rate %d (asked %u) at frame %llu\n", (int)rate, (unsigned)frequency,
                    (unsigned long long)Port_ViTick());
        }
    }
    Port_GbAudioSetFrequency((int32_t)frequency); // the GB Tower's output (gb_audio_out.cpp) uses the same call
    return rate;
}

int32_t port_osAiSetNextBuffer(void* buf, uint32_t size) {
    std::lock_guard<std::mutex> g(gLock);
    Advance();
    if (gNext != 0) {
        return -1; // FIFO full: the hardware ignores the buffer
    }
    size &= ~7u;
    if (size == 0) {
        return 0;
    }
    if (gCur == 0) {
        gCur = size;
    } else {
        gNext = size;
    }
    Dump(buf, size);
    Output(buf, size);
    return 0;
}

uint32_t port_osAiGetLength(void) {
    std::lock_guard<std::mutex> g(gLock);
    Advance();
    return gCur;
}

uint32_t osAiGetStatus(void) {
    std::lock_guard<std::mutex> g(gLock);
    Advance();
    return gNext != 0 ? 0x80000000u : 0; // AI_STATUS_FIFO_FULL
}

} // extern "C"

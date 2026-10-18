// The N64 audio interface (AI) for the game's own sound (the GB Tower has its own path: gb_audio_out.cpp).
//
// The game paces its audio frames by the AI: each frame it queues the previous frame's buffer (osAiSetNextBuffer)
// and picks the next frame's length from the bytes left in the current DMA (osAiGetLength; 552 or 368 samples, so
// that on average it produces what the DAC consumes). The AI here is virtual: a two-entry DMA FIFO drained by VI
// frames at the DAC rate, like the hardware, so what the game produces depends only on game time (lockstep runs and
// replays stay deterministic) and never on the host's sound device.
//
// Accepted buffers are copied to an SDL3 audio stream. The stream's playback ratio follows its fill level (target
// ~70 ms), which absorbs the difference between the host's frame pacing and the N64 DAC rate without clicks; far
// behind (the host stalled), the queue is dropped instead of lagging. Output
// is on at normal speed; PORT_SPEED other than 1 turns it off (no pile-up, no stall), PORT_SOUND=0/1 overrides.
// PORT_AUDIO_DUMP=<file> writes everything the AI accepts as raw 16-bit stereo PCM at the AI rate (host byte
// order; the rate is printed as "[audio] rate"), plus <file>.frames with the VI frame of each buffer.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

extern "C" uint64_t Port_ViTick(void);
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
double gRatio = 1.0;
double gRateIntegral = 0.0;
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
    gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (gStream == nullptr) {
        fprintf(stderr, "[audio] no audio device: %s\n", SDL_GetError());
        gEnabled = 0;
        return;
    }
    gStreamFreq = gFreq;
    gRatio = 1.0;
    gRateIntegral = 0.0;
    SDL_ResumeAudioStreamDevice(gStream);
}

void Output(const void* data, uint32_t bytes) {
    if (!Enabled() || gFreq <= 0) {
        return;
    }
    OpenStream();
    if (gStream == nullptr) {
        return;
    }
    const double target = 0.07 * gFreq * 4; // bytes
    int queued = SDL_GetAudioStreamQueued(gStream);
    if (queued > target * 4) {
        SDL_ClearAudioStream(gStream); // far behind (the host stalled): drop instead of lagging
        queued = 0;
    }
    // playback speed follows the fill level (PI control around the target): a real device needs ~0.3 % (the host
    // paces frames at 60 Hz, the N64 at ~59.83 Hz); imprecise ones (SDL's disk driver: ~3 % slow) up to 3 %
    double err = std::clamp((queued - target) / target, -1.0, 1.0);
    gRateIntegral = std::clamp(gRateIntegral + err * 0.0005, -0.03, 0.03);
    gRatio = std::clamp(1.0 + gRateIntegral + err * 0.005, 0.97, 1.03);
    SDL_SetAudioStreamFrequencyRatio(gStream, (float)gRatio);
    SDL_PutAudioStreamData(gStream, data, (int)bytes);
    static const bool trace = getenv("PORT_TRACE_AUDIO_OUT") != nullptr;
    static uint64_t lastTrace = 0, put = 0, lastPut = 0;
    static double lastT = 0;
    put += bytes;
    if (trace && gTick >= lastTrace + 120) {
        double t = SDL_GetTicks() / 1000.0;
        uint64_t consumed = put - (uint64_t)SDL_GetAudioStreamQueued(gStream);
        fprintf(stderr, "[audio-out] t %.3f frame %llu queued %.1f ms ratio %.4f, played %.0f Hz\n", t,
                (unsigned long long)gTick, queued * 1000.0 / (gFreq * 4.0), gRatio, (consumed - lastPut) / 4.0 / (t - lastT));
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

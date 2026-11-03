// Web build: sound output through an AudioWorklet (see web_audio.h).
#define PORT_WEB_AUDIO_IMPL
#include <SDL3/SDL.h>
#include "web_audio.h"

#include <emscripten.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// The ring buffer the AudioWorklet reads: two indices (frames; written by the pump, read by the worklet), then
// kRingFrames stereo float frames. The worklet gets the WebAssembly memory and the ring's address (web/port.js).
constexpr uint32_t kRingFrames = 16384; // ~0.34 s at 48 kHz (a power of two)
struct Ring {
    std::atomic<int32_t> write;
    std::atomic<int32_t> read;
    float data[kRingFrames * 2];
};

struct Source {
    SDL_AudioStream* stream;
    bool paused;
};

std::mutex gLock;
std::vector<Source> gSources;
Ring* gRing = nullptr;
int gRate = 48000;

uint32_t Filled() {
    return (uint32_t)(gRing->write.load(std::memory_order_acquire) - gRing->read.load(std::memory_order_acquire)) &
           (kRingFrames - 1);
}

// Keeps the ring about kAhead full: mixes what the streams have (the game's sound and the Game Boy's), and silence
// only when the ring is about to run dry (as a device would play silence then), so no gaps are inserted otherwise.
void Pump() {
    const uint32_t ahead = (uint32_t)(gRate * 0.06); // 60 ms
    const uint32_t low = (uint32_t)(gRate * 0.015);
    std::vector<float> mix, part;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        uint32_t filled = Filled();
        if (filled >= ahead) {
            continue;
        }
        uint32_t want = ahead - filled;
        mix.assign(want * 2, 0.0f);
        part.resize(want * 2);
        uint32_t got = 0;
        {
            std::lock_guard<std::mutex> g(gLock);
            for (const Source& s : gSources) {
                if (s.paused) {
                    continue;
                }
                int bytes = SDL_GetAudioStreamData(s.stream, part.data(), (int)(want * 2 * sizeof(float)));
                uint32_t frames = bytes > 0 ? (uint32_t)bytes / (2 * sizeof(float)) : 0;
                for (uint32_t i = 0; i < frames * 2; i++) {
                    mix[i] += part[i];
                }
                got = std::max(got, frames);
            }
        }
        if (got < want && filled + got >= low) {
            want = got;
        }
        int32_t w = gRing->write.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < want; i++) {
            uint32_t k = ((uint32_t)w + i) & (kRingFrames - 1);
            gRing->data[k * 2] = SDL_clamp(mix[i * 2], -1.0f, 1.0f);
            gRing->data[k * 2 + 1] = SDL_clamp(mix[i * 2 + 1], -1.0f, 1.0f);
        }
        gRing->write.store((int32_t)(((uint32_t)w + want) & (kRingFrames - 1)), std::memory_order_release);
    }
}

void Start() {
    // the AudioContext's rate (web/port.js makes the context when the player presses Play)
    gRate = MAIN_THREAD_EM_ASM_INT({ return Module.portAudio ? Module.portAudio.rate : 48000; });
    gRing = new Ring();
    gRing->write = 0;
    gRing->read = 0;
    MAIN_THREAD_ASYNC_EM_ASM(
        {
            if (Module.portAudio) {
                Module.portAudio.node.port.postMessage({ buffer : HEAPU8.buffer, base : Number($0), frames : $1 });
            }
        },
        gRing, kRingFrames);
    std::thread(Pump).detach();
    fprintf(stderr, "[audio] web output at %d Hz\n", gRate);
}

} // namespace

SDL_AudioStream* PortWebAudio_OpenStream(const SDL_AudioSpec* spec) {
    static std::once_flag started;
    std::call_once(started, Start);
    const SDL_AudioSpec out = { SDL_AUDIO_F32, 2, gRate };
    SDL_AudioStream* stream = SDL_CreateAudioStream(spec, &out);
    if (stream != nullptr) {
        std::lock_guard<std::mutex> g(gLock);
        gSources.push_back({ stream, true }); // paused, as a newly opened device stream
    }
    return stream;
}

static bool SetPaused(SDL_AudioStream* stream, bool paused) {
    std::lock_guard<std::mutex> g(gLock);
    for (Source& s : gSources) {
        if (s.stream == stream) {
            s.paused = paused;
            return true;
        }
    }
    return false;
}

bool PortWebAudio_Pause(SDL_AudioStream* stream) {
    return SetPaused(stream, true);
}

bool PortWebAudio_Resume(SDL_AudioStream* stream) {
    return SetPaused(stream, false);
}

void PortWebAudio_Destroy(SDL_AudioStream* stream) {
    {
        std::lock_guard<std::mutex> g(gLock);
        for (size_t i = 0; i < gSources.size(); i++) {
            if (gSources[i].stream == stream) {
                gSources.erase(gSources.begin() + (long)i);
                break;
            }
        }
    }
    SDL_DestroyAudioStream(stream);
}

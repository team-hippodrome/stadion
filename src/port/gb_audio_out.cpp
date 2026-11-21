// Audio output for the GB Tower.
//
// The GB Tower mixes the Game Boy's sound itself (gb_tower_emulator_86CB0.c: APU channels -> 16-bit stereo PCM) and
// hands each buffer to the audio interface with osGbSetNextBuffer, pacing itself by osAiGetLength. This is a small SDL3
// audio stream just for those buffers (the game's own sound goes through audio_out.cpp).
//
// Off in replay / lockstep runs (PORT_REPLAY, PORT_SPEED=max), where the queued length would make runs depend on
// wall-clock time; PORT_GB_SOUND=1 forces it on, PORT_GB_SOUND=0 off (if unset, PORT_SOUND decides when it is set).
// With the output off, osAiGetLength reports a virtual queue that the audio interface drains by VI frames at the DAC rate (like the N64), so the tower still sizes
// its buffers as on hardware (~802 samples per frame; reporting 0 made every buffer the maximum 816 samples and the
// PORT_GB_WAV dump ran ~1.5 % slow against ares).
#include <SDL3/SDL.h>
#include "web_audio.h" // (web build: an AudioWorklet instead of SDL's audio device)
#include "sound_settings.h" // launcher > Sound: Game Boy volume, master, Sound off, background mute
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include "StadionPort.h"

namespace {
std::mutex gLock;
SDL_AudioStream* gStream = nullptr;
float gGain = -1.0f; // the stream's gain as last set (-1: not set on this stream)
int gFreq = 0;
int gEnabled = -1;
// virtual AI queue (output off): bytes queued, VI tick of the last drain, fractional bytes
uint64_t gVirtQueued = 0;
uint64_t gVirtTick = 0;
double gVirtFrac = 0;

bool Enabled() {
    if (gEnabled < 0) {
        const char* e = getenv("PORT_GB_SOUND");
        if (e == nullptr) {
            e = getenv("PORT_SOUND"); // the switch for all sound output (audio_out.cpp)
        }
        if (e != nullptr) {
            gEnabled = atoi(e) != 0;
        } else {
            const char* speed = getenv("PORT_SPEED");
            gEnabled = getenv("PORT_REPLAY") == nullptr && !(speed != nullptr && strcmp(speed, "max") == 0);
        }
    }
    return gEnabled != 0;
}

void OpenStream(int freq) {
    if (gStream != nullptr && gFreq == freq) {
        return;
    }
    if (gStream != nullptr) {
        SDL_DestroyAudioStream(gStream);
        gStream = nullptr;
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        gEnabled = 0;
        return;
    }
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, freq };
    gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (gStream == nullptr) {
        gEnabled = 0;
        return;
    }
    gFreq = freq;
    gGain = -1.0f;
    SDL_ResumeAudioStreamDevice(gStream);
}
} // namespace

extern "C" {

// osAiSetFrequency: the GB Tower sets its mixing rate here.
void Port_GbAudioSetFrequency(int32_t freq) {
    std::lock_guard<std::mutex> g(gLock);
    if (getenv("PORT_GB_WAV")) fprintf(stderr, "[gbsound] rate %d\n", (int)freq);
    if (!Enabled() || freq <= 0) {
        return;
    }
    if (gStream != nullptr) {
        OpenStream(freq);
    } else {
        gFreq = freq; // opened on the first buffer (osAiSetFrequency is also the game's own call)
    }
}

// One mixed buffer: 16-bit stereo samples in host byte order. PORT_GB_WAV=<file> also writes the raw PCM there
// (16-bit stereo, the rate set by osAiSetFrequency; tools/pcm2wav.py makes a .wav of it).
void Port_GbAudioSubmit(const void* samples, uint32_t bytes) {
    std::lock_guard<std::mutex> g(gLock);
    static FILE* sDump = getenv("PORT_GB_WAV") ? fopen(getenv("PORT_GB_WAV"), "wb") : nullptr;
    if (sDump != nullptr && samples != nullptr) {
        fwrite(samples, 1, bytes, sDump);
        fflush(sDump);
    }
    if (!Enabled() && samples != nullptr) {
        gVirtQueued += bytes;
    }
    if (!Enabled() || samples == nullptr || bytes == 0) {
        return;
    }
    if (gStream == nullptr) {
        OpenStream(gFreq > 0 ? gFreq : 48000); // the GB Tower mixes at 48 kHz (its osAiSetFrequency(0xBB80))
        if (gStream == nullptr) {
            return;
        }
    }
    // keep at most ~0.25 s queued (the tower produces a little more than it needs when it runs ahead)
    if (SDL_GetAudioStreamQueued(gStream) > gFreq) {
        return;
    }
    const float gain = Port::SoundOutputGain(true);
    if (gain != gGain) {
        SDL_SetAudioStreamGain(gStream, gain);
        gGain = gain;
    }
    SDL_PutAudioStreamData(gStream, samples, (int)bytes);
}

// osAiGetLength for the GB Tower: bytes still queued for playback.
uint32_t Port_GbAudioQueuedBytes(void) {
    std::lock_guard<std::mutex> g(gLock);
    if (!Enabled()) {
        // N64 NTSC: VI clock 48681812 Hz, DAC rate = clock / round(clock / f), VI ~59.826 frames per second
        const double clock = 48681812.0;
        int f = gFreq > 0 ? gFreq : 48000;
        double dac = clock / (double)(long)(clock / f + 0.5);
        uint64_t tick = Port_ViTick();
        if (tick > gVirtTick) {
            gVirtFrac += (double)(tick - gVirtTick) * dac / 59.826 * 4.0;
            uint64_t drain = (uint64_t)gVirtFrac & ~(uint64_t)3;
            gVirtFrac -= (double)drain;
            gVirtQueued = gVirtQueued > drain ? gVirtQueued - drain : 0;
            gVirtTick = tick;
        }
        return (uint32_t)gVirtQueued;
    }
    if (gStream == nullptr) {
        return 0;
    }
    int q = SDL_GetAudioStreamQueued(gStream);
    return q > 0 ? (uint32_t)q : 0;
}

} // extern "C"

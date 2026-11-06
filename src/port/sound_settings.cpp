// Sound settings shared between the launcher (window thread), the game's mixer (game thread) and the output streams.
// See sound_settings.h.
#include "sound_settings.h"

#include <algorithm>
#include <atomic>

namespace {

std::atomic<int> gMaster{ 100 }, gMusic{ 100 }, gEffects{ 100 }, gVoices{ 100 }, gGameBoy{ 100 };
std::atomic<bool> gOn{ true }, gMuteInBackground{ false }, gFocused{ true };
std::atomic<int> gDelay{ 0 };

int Clamp(int v) {
    return std::clamp(v, 0, 100);
}

} // namespace

extern "C" int Port_SoundPartGainQ15(int part) {
    int v = 100;
    switch (part) {
        case PORT_SOUND_MUSIC:
            v = gMusic.load(std::memory_order_relaxed);
            break;
        case PORT_SOUND_EFFECTS:
            v = gEffects.load(std::memory_order_relaxed);
            break;
        case PORT_SOUND_VOICES:
            v = gVoices.load(std::memory_order_relaxed);
            break;
    }
    // a perceived halving is about 1/4 of the amplitude: the sliders follow a square curve (50 % -> 0.25)
    return v >= 100 ? 32768 : (v * v * 32768) / 10000;
}

namespace Port {

void SetSoundSettings(const SoundSettings& s) {
    gMaster = Clamp(s.master);
    gMusic = Clamp(s.music);
    gEffects = Clamp(s.effects);
    gVoices = Clamp(s.voices);
    gGameBoy = Clamp(s.gameBoy);
    gOn = s.on;
    gMuteInBackground = s.muteInBackground;
    gDelay = std::clamp(s.delay, 0, 2);
}

SoundSettings GetSoundSettings() {
    SoundSettings s;
    s.master = gMaster;
    s.music = gMusic;
    s.effects = gEffects;
    s.voices = gVoices;
    s.gameBoy = gGameBoy;
    s.on = gOn;
    s.muteInBackground = gMuteInBackground;
    s.delay = gDelay;
    return s;
}

void NoteSoundWindowFocus(bool focused) {
    gFocused.store(focused, std::memory_order_relaxed);
}

float SoundOutputGain(bool gameBoy) {
    if (!gOn.load(std::memory_order_relaxed)) return 0.0f;
    if (gMuteInBackground.load(std::memory_order_relaxed) && !gFocused.load(std::memory_order_relaxed)) return 0.0f;
    float g = gMaster.load(std::memory_order_relaxed) / 100.0f;
    if (gameBoy) g *= gGameBoy.load(std::memory_order_relaxed) / 100.0f;
    return g * g; // square curve, as the part volumes
}

double SoundQueueTarget(double autoTarget) {
    switch (gDelay.load(std::memory_order_relaxed)) {
        case 1: // short: 60 % of the normal target (macOS ~23 ms -> 20 ms, Windows/Linux 70 ms -> 42 ms)
            return std::max(0.020, autoTarget * 0.6);
        case 2: // long: what the adaptive target grows to on a slow PC anyway (audio_out.cpp kMaxQueueTarget)
            return std::max(0.120, autoTarget);
        default:
            return autoTarget;
    }
}

} // namespace Port

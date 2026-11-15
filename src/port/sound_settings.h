// Sound settings (launcher > Sound): volumes per part of the game's mix, the Game Boy (GB Tower) sound, muting and
// the sound delay. The launcher stores them as CVars (gSound.*); Engine.cpp hands them over here at start and on every
// change (Port_ApplySoundCVars), and the audio paths read them from here on their own threads.
//
// Part volumes are applied where the game mixes (game/src/libnumus/player.c for music and effects, audio_stream.c for
// cries and the announcer), so they only change the sound the game produces, never its timing. At 100 % nothing is
// multiplied: the mix stays bit-identical (PORT_AUDIO_DUMP, tools/audio_compare.py). Master, Sound off and the
// background mute are the output stream's gain (audio_out.cpp, gb_audio_out.cpp).
#pragma once

#include "StadionPort.h" // PORT_SOUND_*, Port_SoundPartGainQ15

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}

namespace Port {

struct SoundSettings {
    int master = 100;  // percent, all sound
    int music = 100;   // the game's songs
    int effects = 100; // sound effects (menus, moves)
    int voices = 100;  // Pokémon cries and the announcer
    int gameBoy = 100; // GB Tower (the Game Boy games)
    bool on = true;
    bool muteInBackground = false; // silent while another window has the focus
    int delay = 0;                 // 0 normal, 1 short, 2 long (the output queue: shorter reacts sooner, longer
                                   // rides out uneven frames on slow PCs)
};

void SetSoundSettings(const SoundSettings& s);
SoundSettings GetSoundSettings();
// The window thread reports whether the game window has the keyboard focus (Engine.cpp, every frame).
void NoteSoundWindowFocus(bool focused);
// Gain for an output stream: master x (Game Boy volume for the GB stream) x on x background mute.
float SoundOutputGain(bool gameBoy);
// The output queue target for the chosen delay; autoTarget is the platform's normal one.
double SoundQueueTarget(double autoTarget);

} // namespace Port
#endif

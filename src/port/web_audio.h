// Web build: sound output through an AudioWorklet (web/audio-worklet.js) instead of SDL's audio device.
//
// SDL's browser backend plays through a ScriptProcessorNode, whose callback runs on the browser's main thread: whenever
// that thread is busy (it draws the frames), the sound stutters. Here the SDL audio streams of audio_out.cpp and
// gb_audio_out.cpp are not bound to a device; a thread of their own pulls from them (SDL still converts and resamples,
// and the streams' queued amounts stay what the rate control measures) into a ring buffer in shared memory, which the
// AudioWorklet plays on the browser's audio thread.
//
// Included by the two sound outputs; the macros below turn their SDL device calls into these functions.
#pragma once
#if defined(__EMSCRIPTEN__)
#include <SDL3/SDL_audio.h>

SDL_AudioStream* PortWebAudio_OpenStream(const SDL_AudioSpec* spec);
bool PortWebAudio_Pause(SDL_AudioStream* stream);
bool PortWebAudio_Resume(SDL_AudioStream* stream);
void PortWebAudio_Destroy(SDL_AudioStream* stream);

#if !defined(PORT_WEB_AUDIO_IMPL)
#define SDL_OpenAudioDeviceStream(device, spec, callback, userdata) PortWebAudio_OpenStream(spec)
#define SDL_PauseAudioStreamDevice PortWebAudio_Pause
#define SDL_ResumeAudioStreamDevice PortWebAudio_Resume
#define SDL_DestroyAudioStream PortWebAudio_Destroy
#endif
#endif

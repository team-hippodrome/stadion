#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

// PORT: helpers for the audio engine's ROM data on a 64-bit little-endian host.
//
// The sound banks, songs, FX tables and archive headers are DMA'd from ROM as big-endian blobs whose pointer fields
// are 32-bit offsets the game relocated in place into absolute addresses. Here the headers are byte-swapped on load,
// offsets stay offsets inside a blob (the readers add the blob's address), and the parts the synthesizer reads through
// real pointers (wave tables) are rebuilt as host structures (port_audio.c).

#include "global.h"
#include <PR/libaudio.h>

// Byte-order helpers (Port_ReadBE32, Port_SwapWords, ...) are in port_endian.h, included through global.h.

// 32-bit ids for pointers that the game stores in 32-bit fields (the player's command queue)
u32 PortA_PtrId(const void* p);
void* PortA_IdPtr(u32 id);

// Size of an ALSeqFile with n entries in host layout (ALSeqData holds a pointer: 16 bytes instead of 8).
#define PORT_SEQFILE_BYTES(n) (sizeof(ALSeqFile) + ((n) > 1 ? (n)-1 : 0) * sizeof(ALSeqData))
// ... and as stored in ROM (s16 revision, s16 count, then {u32 offset, s32 len} per entry)
#define PORT_SEQFILE_ROM_BYTES(n) (4 + (n)*8)

// Sound bank ("N64 PtrTablesV2"): the host-side view built by PortA_RelocateSoundBank.
typedef struct PortSoundBank {
    s32 count;
    u8* basenote;            // count bytes in the blob
    f32* detune;             // count floats in the blob (converted in place by the game)
    ALWaveTable** wave_list; // host wave tables (loops and ADPCM books stay in the blob, byte-swapped)
} PortSoundBank;
PortSoundBank* PortA_RelocateSoundBank(void* blob, uintptr_t waveRomBase); // NULL when already relocated
PortSoundBank* PortA_Bank(const void* blob);

// Blocks the calling thread for one frame (for loops that spin on the sound engine's state).
void PortA_SpinWait(void);

// Blocks the calling game thread for that many retraces when Port_LoadWaits() is on.
void Port_LoadStall(s32 retraces);

#endif

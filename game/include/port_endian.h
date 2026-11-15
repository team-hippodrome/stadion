#ifndef PORT_ENDIAN_H
#define PORT_ENDIAN_H

// PORT: byte-order helpers for data the game reads as stored on the N64 (ROM, cartridge saves, fragment blobs), which
// is big-endian. Included by macros.h, so every game file has them. Use these instead of a local copy.
//
// The swaps go through libultraship's BE32SWAP/BE16SWAP (as the HarbourMasters ports do), which are no-ops on a
// big-endian host. The array helpers accept any alignment (save records and blob fields are not always aligned).

#include <string.h>
#include <PR/ultratypes.h>
#include <ship/utils/binarytools/endianness.h>

// One big-endian value, already loaded into a register, to host order (and back: the swap is its own inverse).
static inline u32 Port_Swap32(u32 value) {
    return BE32SWAP(value);
}
static inline u16 Port_Swap16(u16 value) {
    return BE16SWAP(value);
}
#define PORT_BE32(x) Port_Swap32((u32)(x))

// Read a big-endian value from memory.
static inline u32 Port_ReadBE32(const void* source) {
    const u8* bytes = (const u8*)source;
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) | ((u32)bytes[2] << 8) | bytes[3];
}
static inline u16 Port_ReadBE16(const void* source) {
    const u8* bytes = (const u8*)source;
    return (u16)((bytes[0] << 8) | bytes[1]);
}

// Swap `count` consecutive 32-bit words (u32, s32, f32 or 32-bit addresses) in place.
static inline void Port_SwapWords(void* words, u32 count) {
    u8* bytes = (u8*)words;
    u32 value;

    for (; count > 0; count--, bytes += sizeof(value)) {
        memcpy(&value, bytes, sizeof(value));
        value = BE32SWAP(value);
        memcpy(bytes, &value, sizeof(value));
    }
}

// Swap `count` consecutive 16-bit halfwords (u16 or s16) in place.
static inline void Port_SwapHalves(void* halves, u32 count) {
    u8* bytes = (u8*)halves;
    u16 value;

    for (; count > 0; count--, bytes += sizeof(value)) {
        memcpy(&value, bytes, sizeof(value));
        value = BE16SWAP(value);
        memcpy(bytes, &value, sizeof(value));
    }
}

#endif

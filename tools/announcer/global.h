// Stand-in for the game's global.h so tools/announcer/announcer_clips.c can build the port's GSM decoder
// (game/src/libgsm/libgsm.c) on its own.
#ifndef ANNOUNCER_GLOBAL_H
#define ANNOUNCER_GLOBAL_H
#include <stdint.h>
typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
#endif

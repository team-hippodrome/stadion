#ifndef _6A40_H_
#define _6A40_H_

#include "global.h"

void Gfx_InitDisplayListBuffers(u32 buffer_size, s32 pool_side);
void Gfx_FreeDisplayListBuffers(void);
void Gfx_SwapDisplayListBuffer(void);
void Gfx_GetDisplayListRange(uintptr_t* size, uintptr_t* arg1);
void* Gfx_AllocDisplayList(s32 size);
// PORT: a display list the original allocated by its N64 size (8 bytes per command); host commands are sizeof(Gfx)
#define GFX_N64_BYTES(n) ((n) / 8 * (s32)sizeof(Gfx))
s32 Gfx_GetDisplayListRemaining(void);

#endif // _6A40_H_

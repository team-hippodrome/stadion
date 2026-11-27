#include <stdlib.h>
#include <string.h>
#include "src/util.h"
#include "model_callback_library.h"
#include "src/gfx_buffer.h"
#include "src/geo_render.h"
#include "src/battle_hud.h"
#include "StadionPort.h"

// PORT: s16 pair then texture addresses (host addresses; indexed with & 7, past the 6 the original declares)
typedef struct AnimatedFrameTextures {
    /* 0x00 */ s16 scrollStep;
    /* 0x02 */ s16 unk_02;
    /* 0x04 */ uintptr_t frames[8];
} AnimatedFrameTextures; // size = 0x1C

typedef struct AnimatedFrameState {
    PDL_NODE_WITH_DATA(u32*, frameTextures);
    /* 0x18 */ Gfx* gfx;
} AnimatedFrameState; // size = 0x1C

typedef struct TextureState {
    PDL_NODE_WITH_DATA(u32*, textures);
    /* 0x18 */ Gfx* gfx;
} TextureState; // size = 0x1C

// the frame record from ROM: two big-endian s16, then 32-bit addresses. Cached by host address with the raw bytes it
// was made from (see Port_PdlWords): a model loaded where another one was must not get the old model's texture frames.
static AnimatedFrameTextures* Port_PdlFrameRecord(void* arg) {
    static struct {
        const u8* src;
        u8 raw[4 + 40];
        AnimatedFrameTextures* out;
    } sCache[256];
    static s32 sCount;
    static s32 sNext;
    const u8* p = (const u8*)Util_ConvertAddrToVirtAddr((uintptr_t)arg);
    AnimatedFrameTextures* out;
    uintptr_t* words;
    s32 i;
    s32 slot = -1;

    if (p == NULL || !Port_IsRawData(p)) {
        return (AnimatedFrameTextures*)p;
    }
    for (i = 0; i < sCount; i++) {
        if (sCache[i].src == p) {
            if (memcmp(sCache[i].raw, p, sizeof(sCache[i].raw)) == 0) {
                return sCache[i].out;
            }
            slot = i; // same place, other data: convert again into this entry
            break;
        }
    }
    if (slot < 0) {
        if (sCount < 256) {
            slot = sCount++;
            sCache[slot].out = calloc(1, sizeof(*out));
        } else {
            slot = sNext;
            sNext = (sNext + 1) % 256;
        }
        sCache[slot].src = p;
    }
    out = sCache[slot].out;
    memcpy(sCache[slot].raw, p, sizeof(sCache[slot].raw));
    out->scrollStep = (s16)((p[0] << 8) | p[1]);
    out->unk_02 = (s16)((p[2] << 8) | p[3]);
    words = Port_PdlWords((void*)(p + 4));
    for (i = 0; i < 8; i++) {
        out->frames[i] = words[i];
    }
    return out;
}

/**
 * Loads one of the 64x32 RGBA16 texture frames in arg1->frames (picked by D_800AF7AE & 7) and
 * scrolls its S coordinate by scrollStep * D_800AF7AE. Skipped while textureMode != 0.
 */
void DisplayList_BuildAnimatedFrameSegment(Gfx* gfx, AnimatedFrameTextures* arg1) {
    s16 temp_v0;
    s32 var_a3;
    AnimatedFrameTextures* p;
    PortEnhCardEffect* cardEffect = PortEnh_ActiveCardEffect();
    p = arg1;
    if (D_8006F09C->textureMode == 0) {
        temp_v0 = 0x4000;
        // FAKE
        if ((arg1 && arg1) && arg1) {}
        var_a3 = (cardEffect != NULL) ? cardEffect->frame : (s32)D_800AF7AE;
        temp_v0 = temp_v0 - (arg1->scrollStep * var_a3);
        // FAKE
        arg1 = NULL;
        gDPLoadTextureBlock(gfx++, p->frames[var_a3 & 0x7], G_IM_FMT_RGBA, G_IM_SIZ_16b, 64, 32, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_CLAMP, 6, 5, G_TX_NOLOD, G_TX_NOLOD);
        gDPSetTileSize(gfx++, 0, temp_v0, 0, ((temp_v0 + 63) << 2), 124);
    }
    gSPEndDisplayList(gfx++);
}

/**
 * Geo callback (jump-table slot 0x108). Builds DisplayList_BuildAnimatedFrameSegment on the
 * build pass. Referenced many times by the Ponyta, Rapidash and Moltres models (flame mane and tail);
 * the debugger hit it for all three.
 */
void DisplayList_InitAnimatedFrameSegment(s32 arg0, AnimatedFrameState* arg1) {
    intptr_t temp_v0;
    intptr_t sp18;

    if (arg0 == 2) {
        sp18 = (intptr_t)Port_PdlFrameRecord(arg1->frameTextures);
        temp_v0 = Gfx_AllocDisplayList(GFX_N64_BYTES(0x50));
        arg1->gfx = temp_v0;
        DisplayList_BuildAnimatedFrameSegment(temp_v0, sp18);
    }
}

/**
 * Loads textures[index + 1] as a 32x32 RGBA16 texture block.
 */
Gfx* DisplayList_BuildSelectedTextureSegment(Gfx* gfx, uintptr_t* textures, s32 index) {
    gDPLoadTextureBlock(gfx++, textures[index + 1], G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD, G_TX_NOLOD);
    gSPEndDisplayList(gfx++);
    return gfx;
}

/**
 * Geo callback (jump-table slot 0x118). Chooses a texture frame and builds
 * DisplayList_BuildSelectedTextureSegment. Grimer (objectType 88) uses its animation frame: frames 66-73
 * of animations 0 and 1 map to textures 0-7, everything else to texture 0. Other models cycle with
 * D_8006F084 & 7. The debugger hit it for Grimer in battle.
 */
void DisplayList_InitSelectedTextureSegment(s32 arg0, TextureState* state) {
    s32 index;
    uintptr_t* textures;

    if (arg0 == 2) {
        textures = Port_PdlWords(state->textures);
        state->gfx = (Gfx*)Gfx_AllocDisplayList(GFX_N64_BYTES(0x50));
        if (D_8006F09C->objectType == 0x58) {
            index = (s32)D_8006F09C->transformAnim.frameFixed >> 0x10;
            if (index >= 0x42) {
                index = index - 0x42;
            } else {
                index = 0;
            }
            if (index >= 8) {
                index = 0;
            }
            if (D_8006F09C->transformAnim.animationId >= 2) {
                index = 0;
            }
        } else {
            PortEnhCardEffect* cardEffect = PortEnh_ActiveCardEffect();
            index = (cardEffect != NULL) ? (cardEffect->frame & 7) : (D_8006F084 & 7);
        }
        DisplayList_BuildSelectedTextureSegment(state->gfx, textures, index);
    }
}

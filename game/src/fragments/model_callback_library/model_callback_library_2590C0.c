#include "src/util.h"
#include "model_callback_library.h"
#include "src/gfx_buffer.h"
#include "src/geo_render.h"

typedef struct AnimationFrameRange {
    /* 0x00 */ u16 animationId;
    /* 0x02 */ u16 firstFrame;
    /* 0x04 */ u16 lastFrame;
} AnimationFrameRange; // size = 0x6

typedef struct AnimationFlagTarget {
    /* 0x00 */ char unk00[0x1];
    /* 0x01 */ u8 flags;
} AnimationFlagTarget; // size = 0x2

// PORT: the callback's node (was a flat N64 view): target was GraphNode.prevSibling (the callbacks switch the previous
// sibling's render flag, bit 0 of GraphNode.flags at byte 1), range the callback data: three u16 (animation id, first
// and last frame), big-endian in ROM models.
typedef GraphNode AnimationRangeFlagState;

static AnimationFrameRange Port_PdlFrameRange(void* arg) {
    const u8* p = (const u8*)Util_ConvertAddrToVirtAddr((uintptr_t)arg);
    AnimationFrameRange r = { 0xFFFF, 0, 0 };

    if (p != NULL) {
        r.animationId = (u16)((p[0] << 8) | p[1]);
        r.firstFrame = (u16)((p[2] << 8) | p[3]);
        r.lastFrame = (u16)((p[4] << 8) | p[5]);
    }
    return r;
}

/**
 * Geo callback (jump-table slot 0x1E8). Sets bit 0x1 of target->flags while the current display
 * object plays range->animationId between range->firstFrame and range->lastFrame, and clears it otherwise.
 * Referenced by the Sandshrew model.
 */
void DisplayList_SetFlagInRange(s32 arg0, AnimationRangeFlagState* arg1) {
    s32 temp_a2;
    s32 var_v1;
    AnimationFrameRange range;
    AnimationFrameRange* temp_v0 = &range;

    if (arg0 == 2) {
        range = Port_PdlFrameRange(arg1->callbackArg);
        var_v1 = 0;
        if (D_8006F09C->transformAnim.animationId == temp_v0->animationId) {
            temp_a2 = ((s32)D_8006F09C->transformAnim.frameFixed >> 0x10) & 0xFFFF;
            if ((temp_a2 >= (s32)temp_v0->firstFrame) && ((s32)temp_v0->lastFrame >= temp_a2)) {
                var_v1 = 1;
            }
        }
        if (var_v1 == 1) {
            arg1->prevSibling->flags |= 1;
            return;
        }
        arg1->prevSibling->flags &= 0xFFFE;
    }
}

/**
 * Geo callback (jump-table slot 0x1F0). The inverse of DisplayList_SetFlagInRange: bit 0x1 is
 * set outside the frame range and cleared inside it. Referenced by the Sandshrew model.
 */
void DisplayList_SetFlagOutOfRange(s32 arg0, AnimationRangeFlagState* arg1) {
    s32 temp_a2;
    s32 var_v1;
    AnimationFrameRange range;
    AnimationFrameRange* temp_v0 = &range;

    if (arg0 == 2) {
        range = Port_PdlFrameRange(arg1->callbackArg);
        var_v1 = 1;
        if (D_8006F09C->transformAnim.animationId == temp_v0->animationId) {
            temp_a2 = ((s32)D_8006F09C->transformAnim.frameFixed >> 0x10) & 0xFFFF;
            if ((temp_a2 >= (s32)temp_v0->firstFrame) && ((s32)temp_v0->lastFrame >= temp_a2)) {
                var_v1 = 0;
            }
        }
        if (var_v1 == 1) {
            arg1->prevSibling->flags |= 1;
            return;
        }
        arg1->prevSibling->flags &= 0xFFFE;
    }
}

/**
 * Geo callback (jump-table slot 0x1F8). Enables noise alpha dither and sets a dark primitive
 * color whose alpha is the current display object's effectSlot, plus a fixed environment color.
 */
void DisplayList_InitDitheredPrimColorSegment(s32 arg0, DisplayListState* state) {
    Gfx* gfx;

    if (arg0 == 2) {
        gfx = Gfx_AllocDisplayList(GFX_N64_BYTES(0xA0));
        state->gfx = gfx;
        gDPSetAlphaDither(gfx++, G_AD_NOISE);
        gDPSetPrimColor(gfx++, 0, 0, 10, 10, 10, D_8006F09C->effectSlot & 0xFF);
        gDPSetEnvColor(gfx++, 0x37, 0x14, 0x14, 0xB4);
        gDPSetCombineLERP(gfx++, 0, 0, 0, 0, TEXEL0, 1, PRIMITIVE, 1, 0, 0, 0, 0, COMBINED, 0, ENVIRONMENT, 0);
        gSPEndDisplayList(gfx++);
    }
}

#include "model_callback_library.h"
#include "src/gfx_buffer.h"
#include "src/geo_render.h"
#include "src/battle_hud.h"
#include "src/model_renderer_callbacks.h"
#include <stdlib.h>
#include <string.h>
#include "src/util.h"
#include "StadionPort.h"

static uintptr_t Port_PdlAddr(u32 v) {
    if (v < 0x10000000U) {
        return v; // segmented address (or a plain value): resolved by the renderer
    }
    return Util_ConvertAddrToVirtAddr(v);
}

// PORT: converted lists are cached by host address, and each entry keeps the raw bytes it was made from: a model loaded
// where another one was (the rental screen loads each Pokemon into the same block) has other addresses at the same
// place, and a cache by address alone handed out the old model's (after Ponyta, Rapidash's flames used Ponyta's flame
// textures). When the table is full, entries are reused in turn instead of converting into a new buffer on every call.
uintptr_t* Port_PdlWords(void* arg) {
    static struct {
        const u8* src;
        u8 raw[40];
        uintptr_t* out;
    } sCache[512];
    static s32 sCount;
    static s32 sNext;
    const u8* p = (const u8*)Util_ConvertAddrToVirtAddr((uintptr_t)arg);
    uintptr_t* out;
    s32 i;
    s32 slot = -1;

    if (p == NULL || !Port_IsRawData(p)) {
        return (uintptr_t*)p;
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
        if (sCount < 512) {
            slot = sCount++;
            sCache[slot].out = calloc(10, sizeof(uintptr_t));
        } else {
            slot = sNext;
            sNext = (sNext + 1) % 512;
        }
        sCache[slot].src = p;
    }
    out = sCache[slot].out;
    memcpy(sCache[slot].raw, p, sizeof(sCache[slot].raw));
    for (i = 0; i < 10; i++) {
        out[i] =
            Port_PdlAddr(Port_ReadBE32(p + i * 4));
    }
    return out;
}

// The Japanese .rodata starts with D_810041D0; it has no "DUMMY NAME" string.
#if !defined(VERSION_JP)
const char D_810041C0[0xC] = "DUMMY NAME";
const char D_810041CC[0x4] = "";
#endif

const Gfx D_810041D0[] = {
    gsDPPipeSync(),
    gsDPSetCycleType(G_CYC_2CYCLE),
    gsDPSetTextureLUT(G_TT_NONE),
    gsSPLoadGeometryMode(0),
    gsSPSetGeometryMode(G_ZBUFFER | G_SHADE | G_LIGHTING | G_SHADING_SMOOTH),
    gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, TEXEL0, COMBINED, 0, PRIM_LOD_FRAC, 0, 0,
                       0, 0, COMBINED),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsSPEndDisplayList(),
};

const char D_81004210[0x4] = "Ａ";
const char D_81004214[0x4] = "";
const char D_81004218[0x4] = "";
const char D_8100421C[0x4] = "";

/**
 * Writes a display list that calls segments[0], loads one 32x64 I16-sized texture block from
 * textures[index], sets up the render tile, then calls segments[1]. Built by
 * DisplayList_InitIndexedTextureSegment (geo callback slot 0x20 of the fragment's jump table).
 */
Gfx* DisplayList_BuildIndexedTextureSegment(Gfx* gfx, DisplayListAddresses* addresses, s32 index) {
    gSPDisplayList(gfx++, addresses->segments[0]);
    // TODO: Convert to gDPLoadTextureBlock
    gDPSetTextureImage(gfx++, G_IM_FMT_I, G_IM_SIZ_16b, 1, addresses->textures[index]);
    gDPSetTile(gfx++, G_IM_FMT_I, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(gfx++);
    gDPLoadBlock(gfx++, 7, 0, 0, 0x3FF, 0x200);
    gDPPipeSync(gfx++);
    gDPSetTile(gfx++, G_IM_FMT_I, G_IM_SIZ_8b, 4, 0, 0, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(gfx++, 0, 0, 0, 0x7C, 0xFC);
    gSPDisplayList(gfx++, addresses->segments[1]);
    gSPEndDisplayList(gfx++);
    return gfx;
}

/**
 * Geo callback (jump-table slot 0x20). On the build pass (arg0 == 2) allocates a display list and
 * fills it with DisplayList_BuildIndexedTextureSegment, picking texture D_8006F084 & 7, so the texture
 * cycles through 8 frames as the global frame counter advances.
 */
void DisplayList_InitIndexedTextureSegment(s32 arg0, DisplayListState* state) {
    Gfx* gfx;
    DisplayListAddresses* addresses;

    if (arg0 == 2) {
        PortEnhCardEffect* cardEffect = PortEnh_ActiveCardEffect();
        s32 frameIndex = (cardEffect != NULL) ? (cardEffect->frame & 7) : (D_8006F084 & 7);
        addresses = (DisplayListAddresses*)Port_PdlWords(state->addresses);
        gfx = Gfx_AllocDisplayList(GFX_N64_BYTES(0x50));
        state->gfx = gfx;
        DisplayList_BuildIndexedTextureSegment(gfx, addresses, frameIndex);
    }
}

/**
 * Loads textures[index] as a 32x64 IA8 texture, then renders the active model-renderer slot with
 * that texture bound (ModelRenderer_UpdateSlotVertices / ModelRenderer_RenderActiveSlot) and enables
 * geometry mode 0x00020000 afterwards. Used for animated flame textures.
 */
Gfx* DisplayList_BuildAnimatedModelTexture(Gfx* gfx, DisplayListAddresses* addresses, s32 index) {
    gDPLoadTextureBlock(gfx++, addresses->textures[index], G_IM_FMT_IA, G_IM_SIZ_8b, 32, 64, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    ModelRenderer_UpdateSlotVertices();
    {
        Gfx* gfx2 = ModelRenderer_RenderActiveSlot(gfx);
        gSPSetGeometryMode(gfx2++, 0x00020000);
        gSPEndDisplayList(gfx2++);
        return gfx2;
    }
}

/**
 * Geo callback (jump-table slot 0xD8). Picks one of 8 flame texture frames from D_800AF7B0 for the
 * active renderer mode plus a per-call counter, then builds the list with
 * DisplayList_BuildAnimatedModelTexture.
 * The models that reference this slot are Charmander, Charmeleon, Charizard, Magmar and Moltres
 * (pokemon_models files 3-5, 125, 145); the debugger hit it for Charmander, Charizard, Magmar and
 * Moltres in battle. This, rather than the species particle system, animates their flames.
 */
void DisplayList_InitAnimatedModelTexture(s32 arg0, DisplayListState* state) {
    static s32 sAnimatedTextureFrame = 0;
    Gfx* gfx;
    DisplayListAddresses* addresses;
    u8 temp_v1;

    temp_v1 = ModelRenderer_GetActiveMode(); //! bug: Installation can read a NULL D_8006F09C before the event check.
    if (arg0 == 2) {
        PortEnhCardEffect* cardEffect = PortEnh_ActiveCardEffect();
        addresses = (DisplayListAddresses*)Port_PdlWords(state->addresses);
        gfx = Gfx_AllocDisplayList(GFX_N64_BYTES(0x140));
        state->gfx = gfx;
        if (cardEffect != NULL) {
            s32 frameIndex = ((cardEffect->textureNodeIndex * 3) + cardEffect->frame) & 7;
            cardEffect->textureNodeIndex += 1;
            DisplayList_BuildAnimatedModelTexture(gfx, addresses, frameIndex);
        } else {
            DisplayList_BuildAnimatedModelTexture(gfx, addresses, ((D_800AF7B0[temp_v1 & 1] * 3) + sAnimatedTextureFrame) % 8);
            sAnimatedTextureFrame += 1;
        }
    }
}

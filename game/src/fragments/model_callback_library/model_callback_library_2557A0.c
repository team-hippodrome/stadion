#include "model_callback_library.h"
#include "src/gfx_buffer.h"
#include "src/geo_render.h"

/**
 * Writes a display list that calls segments[0] and then segments[1].
 */
void DisplayList_BuildTwoSegmentChain(Gfx* gfx, DisplayListAddresses* addresses) {
    gSPDisplayList(gfx++, addresses->segments[0]);
    gSPDisplayList(gfx++, addresses->segments[1]);
    gSPEndDisplayList(gfx++);
}

/**
 * Geo callback (jump-table slot 0x98). On the build pass allocates a small display list and
 * fills it with DisplayList_BuildTwoSegmentChain.
 */
void DisplayList_InitTwoSegmentChain(s32 arg0, DisplayListState* state) {
    Gfx* gfx;
    DisplayListAddresses* addresses;

    if (arg0 == 2) {
        addresses = (DisplayListAddresses*)Port_PdlWords(state->addresses);
        gfx = Gfx_AllocDisplayList(GFX_N64_BYTES(0x18));
        state->gfx = gfx;
        DisplayList_BuildTwoSegmentChain(gfx, addresses);
    }
}

/**
 * Calls the display list at *address only while the current display object's textureMode is 0,
 * so the part disappears in the alternate texture mode.
 */
// PORT64: the address list holds host pointers (Port_PdlWords); an s32* read kept the low 4 bytes of the first one,
// so Ditto's surface list (texture generation + its texture) was never called and Ditto drew flat white
void DisplayList_BuildConditionalSegment(Gfx* gfx, DisplayListAddresses* addresses) {
    if (D_8006F09C->textureMode == 0) {
        gSPDisplayList(gfx++, addresses->segments[0]);
    }
    gSPEndDisplayList(gfx++);
}

/**
 * Geo callback (jump-table slot 0xB8). Builds DisplayList_BuildConditionalSegment on the build
 * pass. Referenced by the Staryu, Starmie and Ditto models; the debugger hit it for Ditto and Staryu.
 */
void DisplayList_InitConditionalSegment(s32 arg0, DisplayListState* state) {
    Gfx* gfx;
    DisplayListAddresses* addresses;

    if (arg0 == 2) {
        addresses = (DisplayListAddresses*)Port_PdlWords(state->addresses);
        gfx = Gfx_AllocDisplayList(GFX_N64_BYTES(0x50));
        state->gfx = gfx;
        DisplayList_BuildConditionalSegment(gfx, addresses);
    }
}

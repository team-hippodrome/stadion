#include "particle_data_library.h"
#include "src/gfx_buffer.h"
#include "src/geo_render.h"

void DisplayList_BuildTwoSegmentChain(Gfx* gfx, DisplayListAddresses* addresses) {
    gSPDisplayList(gfx++, addresses->segments[0]);
    gSPDisplayList(gfx++, addresses->segments[1]);
    gSPEndDisplayList(gfx++);
}

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

// PORT64: the address list holds host pointers (Port_PdlWords); an s32* read kept the low 4 bytes of the first one,
// so Ditto's surface list (texture generation + its texture) was never called and Ditto drew flat white
void DisplayList_BuildConditionalSegment(Gfx* gfx, DisplayListAddresses* addresses) {
    if (D_8006F09C->textureMode == 0) {
        gSPDisplayList(gfx++, addresses->segments[0]);
    }
    gSPEndDisplayList(gfx++);
}

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

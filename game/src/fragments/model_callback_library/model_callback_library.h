#ifndef _FRAGMENT31_H_
#define _FRAGMENT31_H_

#include "global.h"
#include "gallery.h"

/*
 * Fragment 31 (loaded at 0x81000000, memmap id 0) is a library of geo display-list
 * callbacks for 3D models. Its header is followed by a 60-entry jump table at 0x81000020
 * (asm fragments/31/fragment31_unk_table.s, one `j` per 8 bytes); model data stores those
 * 0x810000xx slot addresses as node callbacks, so the slots keep a fixed address while the targets
 * move. About half the slots jump to this fragment's own functions (animated textures, scrolling
 * panels, conditional parts, minigame and Hall of Fame glyph helpers, and the species particle
 * effects in model_callback_library_2558B0.c); the rest jump to battle-engine move effect, minigame
 * and ParticleGfx functions. Many fragments that draw 3D models load it with FRAGMENT_LOAD
 * (battle engine, battle prep, demo scenes, minigames, gallery, Pokedex, Hall of Fame and others);
 * in battle, move animations then reach it through those slots without loading it again.
 *
 * Runtime notes (US 1.0 debugger): in a Metronome run of all 163 moves (Pidgey vs a Splash
 * opponent), 118 moves called battle-engine slots and none called this fragment's own effect slots.
 * The own slots fired only for specific species models (for example Ponyta, Moltres, Grimer, Ditto),
 * and Transform called Particle31_ResetAllSlots.
 */

#include <stddef.h>

// PORT: the callbacks of this fragment are named by ROM model layouts (through the jump table at the start of the
// fragment): their data is big-endian with 32-bit addresses. Port_PdlWords converts the address lists (display lists,
// textures) to host addresses, cached; segmented addresses stay as they are (the renderer resolves them).
typedef struct DisplayListAddresses {
    /* 0x00 */ uintptr_t segments[2];
    /* 0x08 */ uintptr_t textures[8];
} DisplayListAddresses; // size = 0xC (N64: 2 + n words)

uintptr_t* Port_PdlWords(void* arg);

// A display-list graph node (unk_D_86002F34_alt7): the callback data is GraphNode.callbackArg, the built display list
// follows the node header. (Was a flat N64 view {char[0x14]; data; gfx}.)
#define PDL_NODE_WITH_DATA(type, name)                 \
    union {                                            \
        GraphNode node;                                \
        struct {                                       \
            u8 pad_[offsetof(GraphNode, callbackArg)]; \
            type name;                                 \
        };                                             \
    }

typedef struct DisplayListState {
    PDL_NODE_WITH_DATA(DisplayListAddresses*, addresses);
    /* 0x18 */ Gfx* gfx;
} DisplayListState; // size = 0x1C

// The object passed to Particle31_UnpackSlotData; only its objectType (species) is read.
typedef struct ParticleSnapshotObject {
    /* 0x00 */ char unk00[0x1A];
    /* 0x1A */ s16 objectType;
} ParticleSnapshotObject; // size >= 0x1C

extern const Gfx D_810041D0[];

void Particle31_ResetAllSlots(DisplayObject*);
void Particle31_ResetCounters(void);
void Particle31_PackSlotData(u8*);
void Particle31_UnpackSlotData(ParticleSnapshotObject*, u8*);

#endif // _FRAGMENT31_H_

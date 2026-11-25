// The summary bars' 3D models (hippodrome-utils docs/specs/PARTY_SUMMARY_BARS.md, "Model"): every card shows its own
// copy of the Pokémon's battle model, animated, drawn by the game's own model renderer the way the team select's model
// previews are (poke_icon_preview.c: a preview scene with a camera and a light, the model from the model archive).
// Compiled with the game code.
//
// The copies live outside the game's memory pools (a host buffer each, read from the ROM with a scratch buffer of
// their own), so the battle's own allocations and the model loader's state do not change. Each frame, after the
// battle's HUD, they are drawn into one off-screen picture per side (a libultraship framebuffer that summary_bars.cpp
// creates and shows: G_SETFB), one cell per card.
//
// The copies are not one of the battle's two effect slots: they are initialised with no object type (so
// Model_InitDisplayObject does not restart slot 0 or 1's effects, Charizard's flame and the like) and drawn with
// effectSlot 0xFF (which the slot callbacks skip).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "variables.h"
#include "src/display_object_textures.h"
#include "src/geo_layout.h"
#include "src/geo_node.h"
#include "src/geo_render.h"
#include "src/gfx_rect.h"
#include "src/gfx_buffer.h"
#include "src/matrix.h"
#include "src/memory.h"
#include "src/model_animation.h"
#include "src/model_renderer.h"

#include "enhancements.h"
#include "card_models.h"

void Port_VFree(void* host); // memmap.c: forgets a data blob's pseudo addresses

#define CARD_MODEL_POOL (1536 * 1024)
#define CARD_MODEL_SCRATCH 0x28000 // the compressed file (PokeIcon_OpenModelArchives' buffer size)
#define CARD_MODEL_DL_ROOM 0x6000 // display list bytes kept free for one model (see PortEnh_CardModelsDraw)
#define IDLE_ANIM 1                // the model previews' idle (PokeIcon_SetPreviewMon)

typedef struct CardModel {
    u8 species; // 0 = none
    u8 loaded;
    BattleMon* own;   // the card's Pokémon
    BattleMon* shown; // whose model it shows (another one's while transformed)
    void* memory;
    MainPoolState* pool;
    Fragment* blob;
    GraphNode* root;
    DisplayObject* obj;
    s32 anim;  // the animation playing (-1 none)
    s32 speed; // 16.16 step
    s32 idleAnim;   // its idle (its battle cue once it has been in battle)
    s32 faintAnim;  // its fainting animation (from its battle cues while it is in battle), -1 unknown
    s32 sleepAnim;  // its sleeping idle (the species with one of their own), -1 none: it idles
    u8 faintStarted;
    u8 faintHeld;
} CardModel;

static CardModel sCards[2][HU_SIDE_MAX];
static u8* sScratch;
static DisplayObject* sCaptured;

static intptr_t CaptureModelNode(s32 event, GraphNode* node) {
    if (event == 0) {
        sCaptured = (DisplayObject*)node;
    }
    return 0;
}

// The preview scene of PokeIcon_InitPreviewScene (a camera, a light and the model node), with this file's capture.
static uintptr_t sSceneLayout[] = {
    // PORT64: geo layout slots hold addresses
    0x0C00FFFF, 0x05000000, 0x0B00001E, 0x00000000, 0x010000E0, 0x00000000, 0x0000000A,
    0x015404E2, 0x05000000, 0x0D000000, 0x05000000, 0x14000000, 0x002D013B, 0xFFFFFF32,
    0x14000000, 0xFFD30087, 0x80808032, 0x16646464, 0x0F000001, 0x05000000, 0x1F000000,
    0x00000000, 0x00000000, 0xFF060000, 0x00640064, 0x00640000, 0x08000000, 0 /* CaptureModelNode */,
    0x00000000, 0x06000000, 0x06000000, 0x06000000, 0x06000000, 0x01000000, 0x00000000,
    0x00000000,
};
#define SCENE_CAPTURE_SLOT 27

static void FreeCard(CardModel* c) {
    BattleMon* own = c->own;
    if (c->blob != NULL) {
        Port_VFree(c->blob);
    }
    free(c->memory);
    memset(c, 0, sizeof(*c));
    c->own = own;
    c->anim = -1;
    c->idleAnim = IDLE_ANIM;
    c->faintAnim = -1;
    c->sleepAnim = -1;
}

void PortEnh_CardModelsFree(void) {
    s32 s, i;
    for (s = 0; s < 2; s++) {
        for (i = 0; i < HU_SIDE_MAX; i++) {
            FreeCard(&sCards[s][i]);
            sCards[s][i].own = NULL;
        }
    }
}

// The model file into the card's own pool (PokeIcon_LoadModelArchive's steps). The archive caches a loaded file in its
// entry (BinArchive_LoadFileToPool keeps a handle to it): the entry is put back as it was, so the game never reuses
// this copy.
static BinArchive* sArchive; // PortEnh_CardModelsSetArchive's (NULL: the one the game has open)

void PortEnh_CardModelsSetArchive(BinArchive* archive) {
    sArchive = archive;
}

static Fragment* LoadModelFile(CardModel* c, s32 species) {
    BinArchive* archive = (sArchive != NULL) ? sArchive : D_800ABE10.unk_A04.unk_00;
    BinArchiveFile* entry;
    u32 kept;
    Fragment* f;

    if (archive == NULL || species - 1 >= archive->num_files) {
        return NULL;
    }
    if (sScratch == NULL) {
        sScratch = (u8*)malloc(CARD_MODEL_SCRATCH);
        if (sScratch == NULL) return NULL;
    }
    entry = (BinArchiveFile*)((u8*)archive + sizeof(BinArchive) + (species - 1) * sizeof(BinArchiveFile));
    kept = entry->unk_08;
    if (!(archive->unk_00 & 1) && !(archive->unk_00 & 4) && kept != 0) {
        entry->unk_08 = 0; // (load a copy of our own rather than use the game's)
    }
    f = BinArchive_LoadFileToPool(c->pool, archive, species - 1, (uintptr_t)sScratch, CARD_MODEL_SCRATCH);
    entry->unk_08 = kept;
    return f;
}

// Loads the card's copy of `mon`'s model, framed as PokeIcon_SetPreviewMon frames it. Returns whether it worked.
static s32 LoadCard(CardModel* c, BattleMon* mon) {
    s32 species = mon->species.id;
    arg1_func_80010CA8 variant;
    ModelAnimationBank* model;
    const unk_D_8006FF00* info;
    f32 scale;

    FreeCard(c);
    if (species < 1 || species > 151) {
        return 0;
    }
    c->memory = malloc(CARD_MODEL_POOL);
    if (c->memory == NULL) {
        return 0;
    }
    c->pool = MainPool_InitState(c->memory, CARD_MODEL_POOL);
    sSceneLayout[SCENE_CAPTURE_SLOT] = (uintptr_t)CaptureModelNode;
    sCaptured = NULL;
    c->root = process_geo_layout(c->pool, sSceneLayout);
    c->obj = sCaptured;
    c->blob = LoadModelFile(c, species);
    if (c->root == NULL || c->obj == NULL || c->blob == NULL) {
        FreeCard(c);
        return 0;
    }
    Model_ComputeNicknameColorVariant(&variant, mon); // (its nickname's hue and the hue choice)
    model = Fragment_ProcessGeoLayout(c->pool, c->blob, variant);
    if (model == NULL || model->nodes == NULL || model->nodes[0] == NULL) {
        FreeCard(c);
        return 0;
    }
    info = &D_8006FF00[species - 1];
    c->obj->position.x = (s16)(info->unk_14 >> 0x10) >> 6;
    c->obj->position.y = (s16)(info->unk_14 >> 6) >> 4;
    c->obj->position.z = (s16)(info->unk_14 << 6) >> 6;
    c->obj->rotation = D_8006F05C;
    ModelRenderer_ClearDisplayObject(c->obj);
    Model_InitDisplayObject(c->obj, 0, 0, model->nodes[0]); // (no object type: see the top of this file)
    c->obj->effectSlot = 0xFF;
    scale = info->unk_02 / 100.0f;
    Vec3f_SetComponentsDuplicate(&c->obj->scale, scale, scale, scale);
    c->obj->node.renderFlags &= ~0x40;
    c->obj->node.renderFlags |= 0x80;
    GeoCamera_SetPerspective((CameraNode*)c->root->children, 30.0f, 100.0f, 12800.0f);
    if (Port_Enh_Log()) {
        fprintf(stderr, "[enh] card model: species %d loaded\n", species);
    }
    c->species = (u8)species;
    c->shown = mon;
    c->loaded = 1;
    c->anim = -1;
    return 1;
}

#define VALID(s, i) ((s) >= 0 && (s) <= 1 && (i) >= 0 && (i) < HU_SIDE_MAX)

void PortEnh_CardModelsLoad(s32 side, s32 card, BattleMon* mon) {
    if (!VALID(side, card) || mon == NULL) {
        return;
    }
    sCards[side][card].own = mon;
    if (sCards[side][card].loaded && sCards[side][card].shown == mon) {
        return;
    }
    LoadCard(&sCards[side][card], mon);
}

void PortEnh_CardModelsLoadFrom(s32 side, s32 card, s32 from_side, s32 from_card) {
    BattleMon* shown;
    if (!VALID(side, card) || !VALID(from_side, from_card) || sCards[from_side][from_card].own == NULL) {
        return;
    }
    shown = sCards[from_side][from_card].shown; // the target can itself be transformed
    if (shown == NULL) shown = sCards[from_side][from_card].own;
    LoadCard(&sCards[side][card], shown);
    sCards[side][card].shown = shown;
}

void PortEnh_CardModelsRestore(s32 side, s32 card) {
    if (VALID(side, card) && sCards[side][card].own != NULL && sCards[side][card].shown != sCards[side][card].own) {
        LoadCard(&sCards[side][card], sCards[side][card].own);
    }
}

s32 PortEnh_CardModelSpecies(s32 side, s32 card) {
    return VALID(side, card) && sCards[side][card].loaded ? sCards[side][card].species : 0;
}

s32 PortEnh_CardModelReady(s32 side, s32 card) {
    return VALID(side, card) && sCards[side][card].loaded && Port_Enh_CardModelFramebuffer(side) > 0;
}

static void SetSpeed(CardModel* c, s32 speed) {
    if (c->speed != speed) {
        ModelAnim_SetSpeed(c->obj, speed);
        c->speed = speed;
    }
}

static void Play(CardModel* c, s32 anim, s32 speed) {
    if (c->anim != anim) {
        ModelAnim_SetAnimation(c->obj, anim);
        c->anim = anim;
        c->speed = 0x10000; // (binding an animation sets its speed)
        ModelAnim_SetFrame(c->obj, 0);
    } else if (speed != 0 && c->obj->transformAnim.data != NULL && ModelAnim_IsFinished(c->obj)) {
        ModelAnim_SetFrame(c->obj, 0); // a loop, as the battle restarts its idle (Battle_ReturnToIdleAnimIfFinished)
    }
    SetSpeed(c, speed);
}

// The card's look (the spec's "Model"). The card plays its own animations, never what its battle model is doing
// (hits, moves): idle (half speed when statused), its sleeping idle when asleep (for the species that have one; the
// others idle), held when frozen or not picked, and on fainting the species' fainting animation, started with the
// battle model's, then held on its last frame, in grey. The tint is the model's material colour (the game's blend
// toward a colour).
static void SetLook(CardModel* c, const hu_card* card, const PortEnhCardBattle* battle) {
    hu_model_look look = hu_card_model_look(card, PORT_ENH_FAINT_FRAMES);
    const s32 in_battle = battle != NULL && battle->obj != NULL && c->shown == c->own &&
                          battle->obj->objectType == c->species;
    hu_rgb tint = look.tint;
    f32 tint_amount = look.tint_amount;
    s32 speed = (s32)(look.anim_speed * 0x10000);
    u8 amount;

    if (in_battle) {
        // what the species' battle cues say (only loaded for the Pokémon in battle)
        if (battle->idleAnim >= 0) c->idleAnim = battle->idleAnim;
        if (battle->faintAnim >= 0) c->faintAnim = battle->faintAnim;
        c->sleepAnim = battle->sleepAnim;
    }
    if (look.anim != HU_ANIM_FAINT) {
        c->faintStarted = c->faintHeld = 0;
    }
    switch (look.anim) {
        case HU_ANIM_FAINT:
            if (c->faintHeld) {
                break;
            }
            if (!c->faintStarted && c->faintAnim >= 0 &&
                (!in_battle || battle->obj->transformAnim.animationId == c->faintAnim)) {
                c->faintStarted = 1; // with the battle model's (or now, if it has left the field)
                c->anim = -1;
                Play(c, c->faintAnim, 0x10000);
            } else if (!c->faintStarted && c->faintAnim < 0 && !in_battle) {
                c->faintHeld = 1; // (its fainting animation is not known: it stops where it is)
                SetSpeed(c, 0);
            } else if (c->faintStarted && c->obj->transformAnim.data != NULL && ModelAnim_IsFinished(c->obj)) {
                c->faintHeld = 1;
                SetSpeed(c, 0);
                // it is on its last frame (ModelAnim_IsFinished): held there on the whole frame. (Not set from
                // data->endFrame: the animation data is the ROM's, big-endian, read through the TAD_ accessors; read
                // directly it is byte-swapped and the frame was far past the end, which shows the model's rest pose)
                c->obj->transformAnim.frameFixed &= ~0xFFFF;
            } else if (!c->faintStarted) {
                Play(c, c->idleAnim, 0x10000); // idling until the battle model faints
            }
            break;
        case HU_ANIM_SLEEP:
            Play(c, (c->sleepAnim >= 0) ? c->sleepAnim : c->idleAnim, 0x10000);
            break;
        case HU_ANIM_FROZEN:
        case HU_ANIM_HOLD:
            if (c->anim < 0) Play(c, c->idleAnim, 0);
            SetSpeed(c, 0); // the pose it has, held
            break;
        default:
            Play(c, c->idleAnim, speed);
            break;
    }
    if (look.anim == HU_ANIM_FAINT) {
        const hu_rgb grey = { 0x80, 0x80, 0x80 };
        tint = grey;
        tint_amount = c->faintHeld ? 0.7f : 0.0f;
    }
    amount = (tint_amount <= 0.0f) ? 0 : (tint_amount >= 1.0f) ? 0xFF : (u8)(tint_amount * 0xFF);
    Model_SetMaterialColor(c->obj, tint.r, tint.g, tint.b, amount);
}

static void SetFramebuffer(s32 fb) {
    gDPPipeSync(gDisplayListHead++);
    gDisplayListHead->words.w0 = (uintptr_t)((fb > 0) ? G_SETFB : G_RESETFB) << 24;
    gDisplayListHead->words.w1 = (uintptr_t)((fb > 0) ? fb : 0);
    gDisplayListHead++;
}

void PortEnh_CardModelsDraw(const hu_side sides[2], const PortEnhCardBattle battle[2]) {
    s32 s, i;

    for (s = 0; s < 2; s++) {
        s32 fb = Port_Enh_CardModelFramebuffer(s);
        s32 any = 0;
        if (fb <= 0) continue;
        for (i = 0; i < sides[s].count && i < HU_SIDE_MAX; i++) {
            any |= sCards[s][i].loaded;
        }
        if (!any) continue;
        // the side's off-screen picture: one cell per card, cleared to the colour of the card's model square
        SetFramebuffer(fb);
        gDPSetCycleType(gDisplayListHead++, G_CYC_FILL);
        gDPSetRenderMode(gDisplayListHead++, G_RM_NOOP, G_RM_NOOP2);
        gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, PORT_ENH_CARD_ATLAS_W, PORT_ENH_CARD_CELL);
        gDPSetFillColor(gDisplayListHead++,
                        (GPACK_RGBA5551(0x30, 0x3C, 0x60, 1) << 16) | GPACK_RGBA5551(0x30, 0x3C, 0x60, 1));
        gDPFillRectangle(gDisplayListHead++, 0, 0, PORT_ENH_CARD_ATLAS_W - 1, PORT_ENH_CARD_CELL - 1);
        gDPPipeSync(gDisplayListHead++);
        gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
        for (i = 0; i < sides[s].count && i < HU_SIDE_MAX; i++) {
            CardModel* c = &sCards[s][i];
            if (!c->loaded) continue;
            SetLook(c, &sides[s].cards[i], (i == battle[s].active) ? &battle[s] : NULL);
            GeoCamera_SetViewport((CameraNode*)c->root->children, i * PORT_ENH_CARD_CELL, 0, PORT_ENH_CARD_CELL,
                                  PORT_ENH_CARD_CELL);
            GeoCamera_SetPerspective((CameraNode*)c->root->children, 30.0f, 100.0f, 12800.0f);
            // a model takes 6-12 KB of the frame's display list buffer (its commands and matrices): with less room
            // left it is not drawn this frame (its cell keeps the last picture), rather than the matrices running out
            if (Gfx_GetDisplayListRemaining() < CARD_MODEL_DL_ROOM) {
                continue;
            }
            Geo_RenderRootNode(c->root);
        }
        SetFramebuffer(0);
    }
    // the game's own scissor again (what is drawn after this)
    Gfx_ApplyScissorRect(&gDisplayListHead);
}

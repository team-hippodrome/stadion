// Heal Bell (harder Vs. Mewtwo, hippodrome-utils docs/specs/game_specific/1/HARDER_VS_MEWTWO.md): the move the ROM
// lacks, as move 166 (PORT_MOVE_HEAL_BELL: gMoveData's extra entry, its name from translation/port.csv) with effect
// 0x57 (PORT_EFFECT_HEAL_BELL, the free slot of sMoveEffectHandlers).
//
// The effect cures the user: its major status (with the Attack or Speed that burn or paralysis cut), Toxic's counter,
// confusion, Leech Seed and Disable.
//
// The presentation is drawn from scratch: a golden bell appears over the user, swings and rings a melody (six notes of
// an FM bell, mixed into the game's sound by PortEnh_HealBellMix), each strike sending a ring of light outwards, and
// fades away. The battle has no cue for move 166 (its per-move tables end at Struggle), so once the move has resolved
// its user's usedMove becomes a stand-in self-targeting move (Amnesia, a pose without contact) whose own effect script
// and sound are not played; the presentation waits for the bell (PortEnh_HealBellBusy).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "variables.h"
#include "moves.h"
#include "status.h"
#include "src/gfx_buffer.h"
#include "src/text_system.h"
#include "fragments/battle_engine/battle_engine.h"

#include "StadionPort.h"
#include "enhancements.h"

#define STAND_IN AMNESIA

static s32 sEffectRan; // the action being resolved cured its user with Heal Bell

// ---- the effect ----

void PortEnh_HealBellEffect(void) {
    ActivePokemonState* state = &sActivePokemon[sBattleScene.scene->actingSide]->state;
    BattleMonRuntime* user = sBattleUser;
    u8 had = user->pendingStatusCondition;

    sBattleScene.scene->moveEffectKind = MOVE_PRESENTATION_SELF_TARGET;
    sEffectRan = 1;
    user->pendingStatusCondition = 0;
    state->runtime.roster[state->rosterIndex + state->teamMemberIndex * 3] = 0;
    // the stats burn and paralysis cut, back from the stages
    if (had & STATUS_BURN) {
        BattleEffect_ApplyStatStageMultiplier(&user->modifiedAttack, user->unmodifiedAttack, user->statChanges[0]);
    }
    if (had & STATUS_PARALYSIS) {
        BattleEffect_ApplyStatStageMultiplier(&user->modifiedSpeed, user->unmodifiedSpeed, user->statChanges[2]);
    }
    user->uniqueStatusFlags &= ~UNIQUE_STATUS_TOXIC;
    user->immobilizedReason &= ~IMMOBILIZED_CONFUSED;
    user->volatileStatusFlags &= ~VOLATILE_STATUS_LEECH_SEED;
    user->disabledMoveState = 0;
    user->disabledMove = 0;

    // "A bell chimed!" (no announcer line)
    sBattleMessageQueues->buffer[1][0] = 0;
    strncpy((char*)&sBattleMessageQueues->buffer[1][1], Port_TextPortString("PORT_HEAL_BELL_CHIMED"), 0x3E);
    sBattleMessageQueues->buffer[1][0x3F] = 0;
    if (Port_Enh_Log()) {
        fprintf(stderr, "[heal-bell] side %d cured (status was 0x%02x)\n", (int)sBattleScene.scene->actingSide, had);
    }
}

// ---- the presentation ----

// frames from the bell's appearance
#define BELL_IN 10      // grows in
#define BELL_NOTES 6
static const u8 kNoteFrame[BELL_NOTES] = { 14, 24, 34, 44, 54, 64 };
// G5 C6 E6 G6 E6, then C7 held: a major arpeggio that rises, turns, and resolves an octave up
static const f32 kNoteHz[BELL_NOTES] = { 783.99f, 1046.50f, 1318.51f, 1567.98f, 1318.51f, 2093.00f };
#define BELL_OUT 100    // starts fading
#define BELL_END 114
#define SWING_PERIOD 20 // frames: struck at each end, every 10 frames

static struct {
    ActivePokemon* user; // the bell's Pokémon (NULL: none)
    s32 presenting;      // the stand-in's presentation is the bell's
    s32 started;
    s32 frame;
    s32 lastFrameCounter;
    s32 rowSettled; // the row's animation has settled (PortEnh_HealBellSettled)
} sBell;

// Battle_ResolveMonAction, after each action: Heal Bell's stand-in for the presentation (whose tables end at Struggle),
// and the bell when the move took effect (not when its user could not move).
void PortEnh_HealBellResolved(void) {
    ActivePokemon* a = sActivePokemon[sBattleScene.scene->actingSide];
    s32 ran = sEffectRan;

    sEffectRan = 0;
    memset(&sBell, 0, sizeof(sBell));
    if (a->state.runtime.usedMove != PORT_MOVE_HEAL_BELL) {
        return;
    }
    a->state.runtime.usedMove = STAND_IN;
    if (ran) {
        sBell.user = a;
        sBell.presenting = 1;
    }
}

// The stand-in's sound, model event track and effect script are not played (BattleScene_EnterRow3StartMoveAnim,
// BattleAnim_PlayMoveEffectScript).
int PortEnh_HealBellPresenting(void* active) {
    return sBell.presenting && active == (void*)sBell.user;
}

// BattleScene_EnterRow3StartMoveAnim: the bell appears with the user's pose, in the shot of the arena that follows the
// move's announcement (the announcement's close shot draws the Pokemon alone, without the arena and its effects), and
// rings through "A bell chimed!".
void PortEnh_HealBellStart(void* active) {
    if (!PortEnh_HealBellPresenting(active) || sBell.started) {
        return;
    }
    sBell.started = 1;
    sBell.frame = 0;
    sBell.lastFrameCounter = -1;
}

static void Step(void);

// BattleScene_PhaseSelfTargetMove: the presentation waits for the bell. The bell keeps time here too, in the game's
// logic, so the wait ends even when frames are not drawn (Draw steps it as well; a game frame counts once).
int PortEnh_HealBellBusy(void) {
    Step();
    return sBell.presenting && (!sBell.started || sBell.frame < BELL_END);
}

// BattleScene_PhaseSelfTargetMove's settle: the row's animation settles on one exact frame (its cue's frame counter),
// which can pass while the bell still rings; it is kept until the bell has rung out.
int PortEnh_HealBellSettled(int row_settled) {
    if (!sBell.presenting) {
        return row_settled;
    }
    sBell.rowSettled |= (row_settled != 0);
    return sBell.rowSettled && !PortEnh_HealBellBusy();
}

// the phase is over (or the battle): the bell is gone
void PortEnh_HealBellReset(void) {
    memset(&sBell, 0, sizeof(sBell));
    sEffectRan = 0;
}

// ---- the melody: an FM bell (carrier, an inharmonic modulator at 3.5x, the index and the level decaying) ----

#define VOICES 8
static struct {
    volatile s32 pending[BELL_NOTES]; // the notes the bell struck that the mixer has not started
    f32 hz[VOICES];
    f32 t[VOICES]; // seconds since the strike (< 0: free)
} sSynth = { { 0 }, { 0 }, { -1, -1, -1, -1, -1, -1, -1, -1 } };

static void Strike(s32 note) {
    sSynth.pending[note] = 1;
}

// port_osAiSetNextBuffer (audio_out.cpp): the bell's notes added to each buffer of the game's sound (16-bit stereo).
void PortEnh_HealBellMix(int16_t* samples, int frames, int rate) {
    s32 n, v, i;
    const f32 dt = rate > 0 ? 1.0f / rate : 0.0f;
    if (rate <= 0) {
        return;
    }
    for (n = 0; n < BELL_NOTES; n++) {
        if (sSynth.pending[n]) {
            sSynth.pending[n] = 0;
            for (v = 0; v < VOICES; v++) {
                if (sSynth.t[v] < 0) {
                    sSynth.hz[v] = kNoteHz[n];
                    sSynth.t[v] = 0;
                    break;
                }
            }
        }
    }
    for (v = 0; v < VOICES; v++) {
        f32 hz = sSynth.hz[v];
        f32 t = sSynth.t[v];
        f32 decay;
        if (t < 0) {
            continue;
        }
        // the final note rings longer
        decay = hz > 2000.0f ? 0.9f : 0.55f;
        for (i = 0; i < frames; i++) {
            const f32 env = expf(-t / decay) * (t < 0.004f ? t / 0.004f : 1.0f);
            const f32 index = 2.4f * expf(-t / 0.18f) + 0.3f;
            const f32 mod = sinf(6.2831853f * hz * 3.5f * t) * index;
            const f32 s = (sinf(6.2831853f * hz * t + mod) * 0.8f + sinf(6.2831853f * hz * 2.0f * t) * 0.2f * expf(-t / 0.3f)) *
                          env * 5200.0f;
            s32 l = samples[i * 2] + (s32)s;
            s32 r = samples[i * 2 + 1] + (s32)s;
            samples[i * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            samples[i * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            t += dt;
        }
        sSynth.t[v] = t > decay * 7.0f ? -1.0f : t;
    }
}

// ---- the bell's picture ----

// the bell's profile, from the crown down to the lip: (radius, height) in bell units (scaled by kScale)
#define PROFILE 11
static const f32 kProfile[PROFILE][2] = {
    { 0.0f, 0.0f },   { 3.2f, -0.3f },  { 5.4f, -1.4f },   { 6.4f, -3.2f },   { 6.8f, -6.0f },  { 7.2f, -9.0f },
    { 8.0f, -11.8f }, { 9.4f, -14.0f }, { 10.9f, -15.4f }, { 11.4f, -16.2f }, { 10.6f, -16.6f },
};
#define SEGMENTS 16
#define SCALE 1.9f
#define WAVE_LIFE 16.0f
// where the bell hangs: its crown at the height of the Pokemon's head (over its anchor, its middle), in front of it and
// towards the cameras, so the close shots of the move's announcement show it too
#define BELL_HEIGHT 40.0f
#define BELL_SIDE 48.0f
#define BELL_FRONT 40.0f

static f32 Clamp01(f32 x) {
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

// gold, lit from the upper left and the camera: base (196, 150, 40) to a pale highlight
static void Gold(Vtx* v, f32 x, f32 y, f32 z, f32 nx, f32 ny, f32 nz, f32 glow, u8 alpha) {
    f32 lx = -0.45f, ly = 0.75f, lz = 0.48f;
    f32 d = nx * lx + ny * ly + nz * lz;
    f32 diffuse = Clamp01(d) * 0.75f + 0.3f;
    f32 spec = d > 0.85f ? (d - 0.85f) / 0.15f : 0.0f;
    f32 r = 196 * diffuse + 255 * spec * 0.6f + glow * 60;
    f32 g = 150 * diffuse + 240 * spec * 0.6f + glow * 50;
    f32 b = 40 * diffuse + 170 * spec * 0.6f + glow * 30;
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = (u8)(r > 255 ? 255 : r);
    v->v.cn[1] = (u8)(g > 255 ? 255 : g);
    v->v.cn[2] = (u8)(b > 255 ? 255 : b);
    v->v.cn[3] = alpha;
}

static void LoadMatrix(Gfx** gfx, f32 m[4][4], s32 load) {
    Mtx* mtx = Gfx_AllocDisplayList(sizeof(Mtx));
    guMtxF2L(m, mtx);
    gSPMatrix((*gfx)++, mtx, G_MTX_PUSH | (load ? G_MTX_LOAD : G_MTX_MUL) | G_MTX_MODELVIEW);
}

// the body: SEGMENTS around, the profile's rings, outside and in (no culling, the inside darker)
static void DrawBody(Gfx** gfx, f32 glow) {
    const s32 count = PROFILE * SEGMENTS;
    Vtx* vtx = Gfx_AllocDisplayList(sizeof(Vtx) * count);
    s32 p, s;

    for (p = 0; p < PROFILE; p++) {
        f32 r = kProfile[p][0] * SCALE, y = kProfile[p][1] * SCALE;
        // the profile's normal (outward and up along the slope)
        s32 a = p > 0 ? p - 1 : p, b = p < PROFILE - 1 ? p + 1 : p;
        f32 dr = kProfile[b][0] - kProfile[a][0], dy = kProfile[b][1] - kProfile[a][1];
        f32 len = sqrtf(dr * dr + dy * dy);
        f32 nr = len > 0 ? -dy / len : 1, ny = len > 0 ? dr / len : 0;
        if (p == PROFILE - 1) {
            nr = 0.2f;
            ny = -1.0f; // the lip's underside
        }
        for (s = 0; s < SEGMENTS; s++) {
            f32 ang = 6.2831853f * s / SEGMENTS;
            f32 c = cosf(ang), sn = sinf(ang);
            Gold(&vtx[p * SEGMENTS + s], r * c, y, r * sn, nr * c, ny, nr * sn, glow, 255);
        }
    }
    // two rings at a time (32 vertices)
    for (p = 0; p < PROFILE - 1; p++) {
        gSPVertex((*gfx)++, &vtx[p * SEGMENTS], SEGMENTS * 2, 0);
        for (s = 0; s < SEGMENTS; s++) {
            s32 s1 = (s + 1) % SEGMENTS;
            gSP2Triangles((*gfx)++, s, s1, SEGMENTS + s, 0, s1, SEGMENTS + s1, SEGMENTS + s, 0);
        }
    }
}

// the crown's loop and the clapper: small boxes of gold
static void DrawBox(Gfx** gfx, f32 x, f32 y, f32 z, f32 hx, f32 hy, f32 hz, f32 glow) {
    static const s8 kCorner[8][3] = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 },
    };
    Vtx* vtx = Gfx_AllocDisplayList(sizeof(Vtx) * 8);
    s32 i;
    for (i = 0; i < 8; i++) {
        f32 nx = kCorner[i][0] * 0.577f, ny = kCorner[i][1] * 0.577f, nz = kCorner[i][2] * 0.577f;
        Gold(&vtx[i], x + kCorner[i][0] * hx, y + kCorner[i][1] * hy, z + kCorner[i][2] * hz, nx, ny, nz, glow, 255);
    }
    gSPVertex((*gfx)++, vtx, 8, 0);
    gSP2Triangles((*gfx)++, 0, 1, 2, 0, 0, 2, 3, 0);
    gSP2Triangles((*gfx)++, 4, 6, 5, 0, 4, 7, 6, 0);
    gSP2Triangles((*gfx)++, 0, 4, 5, 0, 0, 5, 1, 0);
    gSP2Triangles((*gfx)++, 3, 2, 6, 0, 3, 6, 7, 0);
    gSP2Triangles((*gfx)++, 0, 3, 7, 0, 0, 7, 4, 0);
    gSP2Triangles((*gfx)++, 1, 5, 6, 0, 1, 6, 2, 0);
}

// a ring of light from a strike: a band facing the camera around the bell, growing and fading (world space, from the
// bell's centre `c`; u and v span the plane facing the camera)
static void DrawWave(Gfx** gfx, const Vec3f* c, const f32 u[3], const f32 v[3], f32 age) {
    Vtx* vtx = Gfx_AllocDisplayList(sizeof(Vtx) * SEGMENTS * 2);
    f32 inner = 22.0f + age * 3.2f, outer = inner + 3.0f + age * 0.5f;
    u8 alpha = (u8)(210.0f * Clamp01(1.0f - age / WAVE_LIFE));
    s32 s;
    for (s = 0; s < SEGMENTS; s++) {
        f32 ang = 6.2831853f * s / SEGMENTS;
        f32 dx = cosf(ang), dy = sinf(ang);
        f32 x = u[0] * dx + v[0] * dy, y = u[1] * dx + v[1] * dy, z = u[2] * dx + v[2] * dy;
        Vtx* a = &vtx[s];
        Vtx* b = &vtx[SEGMENTS + s];
        a->v.ob[0] = (s16)(c->x + x * inner), a->v.ob[1] = (s16)(c->y + y * inner), a->v.ob[2] = (s16)(c->z + z * inner);
        b->v.ob[0] = (s16)(c->x + x * outer), b->v.ob[1] = (s16)(c->y + y * outer), b->v.ob[2] = (s16)(c->z + z * outer);
        a->v.flag = b->v.flag = 0;
        a->v.tc[0] = a->v.tc[1] = b->v.tc[0] = b->v.tc[1] = 0;
        a->v.cn[0] = 255, a->v.cn[1] = 240, a->v.cn[2] = 160, a->v.cn[3] = alpha;
        b->v.cn[0] = 255, b->v.cn[1] = 205, b->v.cn[2] = 70, b->v.cn[3] = 0;
    }
    gSPVertex((*gfx)++, vtx, SEGMENTS * 2, 0);
    for (s = 0; s < SEGMENTS; s++) {
        s32 s1 = (s + 1) % SEGMENTS;
        gSP2Triangles((*gfx)++, s, s1, SEGMENTS + s, 0, s1, SEGMENTS + s1, SEGMENTS + s, 0);
    }
}

// the swing: the bell is struck at each end of its swing, the clapper lagging behind
static f32 SwingAt(s32 frame) {
    f32 amp;
    if (frame < kNoteFrame[0] - SWING_PERIOD / 4) {
        return 0;
    }
    amp = frame < kNoteFrame[BELL_NOTES - 1] ? 22.0f : 22.0f * expf(-(frame - kNoteFrame[BELL_NOTES - 1]) / 14.0f);
    // the ends of the swing at the notes' frames
    return amp * sinf(6.2831853f * (frame - (kNoteFrame[0] - SWING_PERIOD / 4)) / SWING_PERIOD);
}

// one step per game frame (gBattleAnimFrameCounter counts them), however often the bell is drawn
static void Step(void) {
    s32 n;
    if (!sBell.presenting || !sBell.started || gBattleAnimFrameCounter == sBell.lastFrameCounter) {
        return;
    }
    if (sBell.lastFrameCounter >= 0) {
        sBell.frame++;
        for (n = 0; n < BELL_NOTES; n++) {
            if (sBell.frame == kNoteFrame[n]) {
                Strike(n);
            }
        }
    }
    sBell.lastFrameCounter = gBattleAnimFrameCounter;
}

// BattleAnim_RenderEffectsFrame: the bell, in world space.
void PortEnh_HealBellDraw(void) {
    Gfx* gfx;
    Vec3f anchor;
    f32 m[4][4], rot[4][4], tmp[4][4];
    f32 scale, swing, glow = 0;
    f32 u[3], v[3];
    s32 n, f;

    if (!sBell.presenting || !sBell.started || sBell.user == NULL) {
        return;
    }
    Step();
    f = sBell.frame;
    if (f >= BELL_END) {
        return;
    }
    scale = f < BELL_IN ? (f32)f / BELL_IN : f >= BELL_OUT ? 1.0f - (f32)(f - BELL_OUT) / (BELL_END - BELL_OUT) : 1.0f;
    scale = scale * scale * (3 - 2 * scale); // eased
    for (n = 0; n < BELL_NOTES; n++) {
        s32 age = f - kNoteFrame[n];
        if (age >= 0 && age < 8) {
            glow = 1.0f - age / 8.0f;
        }
    }
    swing = SwingAt(f);

    BattleAnim_GetOwnerDefaultAnchorPosition(sBell.user, &anchor);
    // the camera's plane (u across, v up the screen), from the view matrix the battle's particles face the camera with
    {
        const MtxF* view = &BattleAnim_GetCameraContext()->view.floatMatrix;
        s32 k;
        for (k = 0; k < 3; k++) {
            u[k] = view->mf[k][0];
            v[k] = view->mf[k][1];
        }
    }
    gfx = gDisplayListHead;
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gSPTexture(gfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(gfx++, G_LIGHTING | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_FOG);
    gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetCombineMode(gfx++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);

    // beside the Pokémon, in front of it (towards its foe) and towards the side the battle's cameras film from (+z)
    anchor.x += sBell.user->facingDirection * BELL_SIDE;
    anchor.y += BELL_HEIGHT;
    anchor.z += BELL_FRONT;
    guTranslateF(m, anchor.x, anchor.y, anchor.z);
    guRotateF(rot, swing, 0, 0, 1); // (across the view of the cameras)
    guMtxCatF(rot, m, tmp);
    guScaleF(rot, scale, scale, scale);
    guMtxCatF(rot, tmp, m);
    // (loaded: as the battle's particles, the modelview holds only the model's transform, the camera's view being in the
    // projection)
    LoadMatrix(&gfx, m, 1);
    DrawBody(&gfx, glow);
    // the crown's loop
    DrawBox(&gfx, 0, 2.0f * SCALE, 0, 1.2f * SCALE, 2.0f * SCALE, 1.2f * SCALE, glow);
    // the clapper, swinging behind the bell
    {
        f32 lag = -0.6f * (swing - SwingAt(f > 2 ? f - 2 : 0));
        guRotateF(rot, lag, 0, 0, 1);
        LoadMatrix(&gfx, rot, 0);
        DrawBox(&gfx, 0, -13.0f * SCALE, 0, 1.4f * SCALE, 3.6f * SCALE, 1.4f * SCALE, glow * 0.5f);
        gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    }
    gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    // the rings of light (translucent, not writing depth), facing the camera from the bell's middle
    {
        Vec3f c = { anchor.x, anchor.y - 9.0f * SCALE, anchor.z };
        gDPPipeSync(gfx++);
        gDPSetCombineMode(gfx++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(gfx++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
        guMtxIdentF(m);
        LoadMatrix(&gfx, m, 1); // (world coordinates)
        for (n = 0; n < BELL_NOTES; n++) {
            s32 age = f - kNoteFrame[n];
            if (age >= 0 && age < WAVE_LIFE) {
                DrawWave(&gfx, &c, u, v, (f32)age);
            }
        }
        gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    }
    gDPPipeSync(gfx++);
    gDisplayListHead = gfx;
}

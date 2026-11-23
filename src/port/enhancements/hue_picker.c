// Hue choice (hippodrome-utils docs/specs/HUE_CHANGE.md): every Pokémon gets a colour the player can pick, kept in
// <N64 save>.hue beside the save (hippodrome/hue_choice.h). Stadium normally turns a nicknamed Pokémon's colour by a
// hue worked out from its nickname, OT ID and OT name (Model_ComputeNicknameColorVariant); with the enhancement on, a
// stored choice replaces that hue wherever the game builds the Pokémon's model or icon (the team select screens,
// battle, the lab). The picker is on the Pokémon card of the team select screens (TeamSelection_Preview: picking a
// rental or a Game Boy Pokémon for a team, and a registered team's Pokémon): L / R step the hue round the colour
// wheel by 15°, the stick turns it smoothly and Z goes back to the game's own hue. Compiled with the game code.
// Restricted keeps the picker inside the species' own nickname hue range (the min / max of the species table
// D_8006FF00 that the nickname hue is picked from); Unrestricted allows the whole wheel.
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "game_state.h"
#include "variables.h"
#include "src/graphics_textures.h"
#include "src/ui_graphics.h"
#include "src/audio_sfx.h"
#include "src/controller.h"
#include "StadionPort.h"
#include "enhancements.h"

#include <hippodrome/hue_choice.h>
#include <hippodrome/trainer_memory.h> // enum hu_game

arg1_func_80010CA8* Model_ComputeNicknameColorVariant(arg1_func_80010CA8* arg0, BattleMon* arg1); // model_renderer.c

static s32 HueMax(s32 a, s32 b) {
    return a > b ? a : b;
}

// The fields that make a Pokémon itself, in this order: the game, its species, its original trainer's ID and its DVs.
// Its names stay out, so a renamed Pokémon keeps its colour; experience, level, moves and HP change as it grows.
static hu_hue_id HueId(const BattleMon* mon) {
    u8 head[6];
    head[0] = HU_GAME_STADIUM_1;
    head[1] = mon->species.id;
    head[2] = (u8)(mon->unk_0E >> 8); // OT ID
    head[3] = (u8)mon->unk_0E;
    head[4] = (u8)(mon->unk_1E >> 8); // DVs
    head[5] = (u8)mon->unk_1E;
    return hu_hue_id_of(head, sizeof head);
}

// Restricted: the species' range in hue units (out_min/out_max) and 1; unrestricted: 0.
static s32 HueRange(const BattleMon* mon, s16* out_min, s16* out_max) {
    const unk_D_8006FF00* info;
    if (Port_Enh_HueChoiceMode() != PORT_HUE_RESTRICTED || mon->species.id == 0 || mon->species.id > 151) {
        return 0;
    }
    info = &D_8006FF00[mon->species.id - 1];
    *out_min = (s16)(info->unk_10 * HU_HUE_UNITS_PER_DEGREE);
    *out_max = (s16)(info->unk_12 * HU_HUE_UNITS_PER_DEGREE);
    return 1;
}

// The stored choice or the game's hue, held inside the species' range when the setting says so (a choice picked
// while unrestricted stays stored and comes back when switching back).
static s16 Resolve(const BattleMon* mon, hu_hue_id id, s16 game_shift) {
    s16 lo, hi;
    s16 v = hu_hue_resolve(Port_Enh_Hues(), id, game_shift);
    return HueRange(mon, &lo, &hi) ? hu_hue_clamp(v, lo, hi) : v;
}

static s32 sBypass; // set while the game's own hue is asked for

void PortEnh_HueBypass(int on) {
    sBypass = on;
}

int16_t PortEnh_HueShift(struct BattleMon* mon, int16_t game_shift) {
    hu_hue_id id;
    if (mon == NULL || sBypass || mon->species.id == 0 || !Port_Enh_HueChoiceEnabled()) {
        return game_shift;
    }
    id = HueId(mon);
    if (Port_Enh_Log()) {
        static hu_hue_id logged[64];
        static s32 n;
        s32 i;
        for (i = 0; i < n && logged[i] != id; i++) {
        }
        if (i == n && n < 64) {
            logged[n++] = id;
            fprintf(stderr, "[hue] species %d id %016llx game %d -> %d\n", mon->species.id, (unsigned long long)id,
                    game_shift, Resolve(mon, id, game_shift));
        }
    }
    return Resolve(mon, id, game_shift);
}

// The hue the game itself gives `mon` (Model_ComputeNicknameColorVariant without the player's choice).
static s16 GameShift(BattleMon* mon) {
    arg1_func_80010CA8 v;
    sBypass = 1;
    Model_ComputeNicknameColorVariant(&v, mon);
    sBypass = 0;
    return v.unk_00;
}

// L / R: a 15° step; Z: the game's own hue. 1 when the hue changed.
static s32 HueButtons(BattleMon* mon, u16 pressed) {
    hu_hue_store* store = Port_Enh_Hues();
    hu_hue_id id = HueId(mon);
    s16 now = Resolve(mon, id, GameShift(mon));
    s16 next, lo, hi;

    if (pressed & BTN_Z) {
        if (!hu_hue_get(store, id, NULL)) {
            return 0; // already the game's own
        }
        hu_hue_unset(store, id);
        Audio_PlaySoundEffectById(SFX_MENU_BACK);
    } else {
        s32 dir = (pressed & BTN_R) ? 1 : -1;
        next = HueRange(mon, &lo, &hi) ? hu_hue_step_in_range(now, dir, lo, hi) : hu_hue_step(now, dir);
        if (hu_hue_signed(next) == hu_hue_signed(now)) {
            Audio_PlaySoundEffectById(SFX_MENU_ERROR); // at an end of the species' range
            return 0;
        }
        hu_hue_set(store, id, next);
        Audio_PlaySoundEffectById(SFX_MENU_SCROLL);
    }
    Port_Enh_SaveHues();
    return 1;
}

// The stick turns the hue smoothly, for the hues between L / R's steps: its tilt left / right sets the speed, from
// 1/64° an update at the edge of the dead zone to 3° an update at full tilt (squared, so a light touch is fine enough
// to land on single degrees). Restricted stops at the ends of the species' range. The file is written once the stick
// is let go.
#define HUE_STICK_FULL 64.0f
#define HUE_STICK_MAX_UNITS 192.0f

static s32 sStickDirty;
static f32 sStickCarry; // the part of a unit not applied yet

static s32 HueStick(BattleMon* mon, f32 stick_x) {
    hu_hue_id id;
    s16 now, next, lo, hi;
    f32 t, d;
    s32 units;

    if (stick_x == 0.0f) {
        sStickCarry = 0.0f;
        if (sStickDirty) {
            sStickDirty = 0;
            Port_Enh_SaveHues();
        }
        return 0;
    }
    t = stick_x / HUE_STICK_FULL;
    if (t > 1.0f) t = 1.0f;
    if (t < -1.0f) t = -1.0f;
    d = t * (t < 0.0f ? -t : t) * HUE_STICK_MAX_UNITS;
    if (d > -1.0f && d < 1.0f) d = (d < 0.0f) ? -1.0f : 1.0f; // never stalls inside the live range
    sStickCarry += d;
    units = (s32)sStickCarry;
    sStickCarry -= (f32)units;
    if (units == 0) {
        return 0;
    }
    id = HueId(mon);
    now = Resolve(mon, id, GameShift(mon));
    next = (s16)(hu_hue_signed(now) + units);
    if (HueRange(mon, &lo, &hi)) {
        next = hu_hue_clamp(next, lo, hi);
    }
    if (hu_hue_signed(next) == hu_hue_signed(now)) {
        return 0; // at an end of the species' range
    }
    if (hu_hue_degrees(now) / 15 != hu_hue_degrees(next) / 15) {
        Audio_PlaySoundEffectById(SFX_MENU_SCROLL); // a tick each time it crosses one of L / R's steps
    }
    hu_hue_set(Port_Enh_Hues(), id, next);
    sStickDirty = 1;
    return 1;
}

int PortEnh_HuePickerInput(struct BattleMon* mon, uint16_t pressed, float stick_x) {
    if (mon == NULL || mon->species.id == 0 || mon->species.id > 151 || !Port_Enh_HueChoiceEnabled()) {
        return 0;
    }
    if (pressed & (BTN_L | BTN_R | BTN_Z)) {
        return HueButtons(mon, pressed);
    }
    return HueStick(mon, stick_x);
}

// The bar along the bottom of the card's model picture (x, y, w, h: the picture, in the screen's coordinates): L and R
// at its ends, the hue between them; "Z: Reset" over the picture's top while a hue is picked.
void PortEnh_HuePickerDraw(int x, int y, int w, int h, struct BattleMon* mon) {
    char buf[48];
    s16 shift, lo = 0, hi = 0;
    s32 chosen, restricted, tw;
    s32 dim_l = 0, dim_r = 0;
    const s32 font = 4;
    const s32 bar_h = 18;
    const s32 key_w = 16;
    s32 by = y + h - bar_h;
    hu_hue_id id;

    if (mon == NULL || mon->species.id == 0 || mon->species.id > 151 || !Port_Enh_HueChoiceEnabled()) {
        return;
    }
    id = HueId(mon);
    chosen = hu_hue_get(Port_Enh_Hues(), id, &shift);
    if (!chosen) {
        shift = GameShift(mon);
    }
    restricted = HueRange(mon, &lo, &hi);
    if (restricted) {
        shift = hu_hue_clamp(shift, lo, hi);
        dim_l = hu_hue_step_in_range(shift, -1, lo, hi) == shift;
        dim_r = hu_hue_step_in_range(shift, 1, lo, hi) == shift;
    }
    if (hu_hue_degrees(shift) == 0) {
        snprintf(buf, sizeof buf, "%s", Port_TextPortString("PORT_HUE_ORIGINAL"));
    } else if (restricted && hi - lo < 360 * HU_HUE_UNITS_PER_DEGREE) {
        snprintf(buf, sizeof buf, "%+d%s", hu_hue_signed(shift) / HU_HUE_UNITS_PER_DEGREE,
                 Port_TextPortString("PORT_HUE_DEGREES"));
    } else {
        snprintf(buf, sizeof buf, "%d%s", hu_hue_degrees(shift), Port_TextPortString("PORT_HUE_DEGREES"));
    }

    gSPDisplayList(gDisplayListHead++, gUiTranslucentFillDL);
    Gfx_FillRectRgba(x, by, w, bar_h, 0x00, 0x00, 0x00, 0xA0);
    Gfx_FillRectRgba(x + 1, by + 1, key_w, bar_h - 2, 0xFF, 0xFF, 0xFF, dim_l ? 0x18 : 0x48);
    Gfx_FillRectRgba(x + w - key_w - 1, by + 1, key_w, bar_h - 2, 0xFF, 0xFF, 0xFF, dim_r ? 0x18 : 0x48);
    if (chosen) {
        const char* reset = Port_TextPortString("PORT_HUE_RESET");
        tw = Font_MeasureTextExtent(font, 0, "%s", reset);
        Gfx_FillRectRgba(HueMax(x + w - tw - 6, x - 2), y, tw + 6, bar_h, 0x00, 0x00, 0x00, 0xA0);
    }
    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);

    Font_BeginTranslucentTextRendering();
    Font_SetActive(font, 0);
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, dim_l ? 0x60 : 0xFF);
    Font_Printf(x + 1 + (key_w - Font_MeasureTextExtent(font, 0, "L")) / 2, by + 1, "L");
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, dim_r ? 0x60 : 0xFF);
    Font_Printf(x + w - key_w - 1 + (key_w - Font_MeasureTextExtent(font, 0, "R")) / 2, by + 1, "R");
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
    tw = Font_MeasureTextExtent(font, 0, "%s", buf);
    if (tw > w - 2 * key_w - 4) {
        // a long word for "original": the number says it as well
        snprintf(buf, sizeof buf, "0%s", Port_TextPortString("PORT_HUE_DEGREES"));
        tw = Font_MeasureTextExtent(font, 0, "%s", buf);
    }
    Font_Printf(x + (w - tw) / 2, by + 1, "%s", buf);
    if (chosen) {
        const char* reset = Port_TextPortString("PORT_HUE_RESET");
        tw = Font_MeasureTextExtent(font, 0, "%s", reset);
        Font_Printf(HueMax(x + w - tw - 3, x + 1), y + 1, "%s", reset);
    }
    Font_EndTexturedTextRendering();
}

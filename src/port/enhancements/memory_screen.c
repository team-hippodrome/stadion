// The trainer memory screen (hippodrome-utils docs/specs/TRAINER_MEMORY.md): on the screen right before the player picks
// their Game Boy or rental Pokémon (the team select's "whose Pokémon" step, TeamSelection_RunModal), a C button opens
// a list of the cup's (or the castle room's) trainers with what the player has learned about each: its name once
// reached, its team, the moves seen and the speed estimates. D-pad or C-Up / C-Down moves through the list; B, C-Left
// or C-Right closes it. The game keeps running under it (its models animate), but sees no buttons while it is open.
//
// This file keeps the screen's state on the game thread and publishes what it shows each frame; memory_screen.cpp
// draws it with ImGui over the game's picture. ImGui draws on top of each finished frame and never into the game's
// frame buffer, so nothing it covers stays behind when it closes (the spec's "Window" rule), and everything it needs is
// ready before it opens: the memory file and the team icons are read when the select screen starts, the fonts are the
// launcher's (no pause, even the first time).
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "game_state.h"
#include "variables.h"
#include "moves.h"
#include "src/controller.h"
#include "src/audio_sfx.h"
#include "src/gb_data.h"
#include "src/text_system.h"
#include "pokemon.h"

#include "StadionPort.h"
#include "enhancements.h"
#include "battle_enhancements.h"

#include <hippodrome/trainer_memory.h>
#include <hippodrome/window_anim.h>

extern Controller* D_8423E570; // rental_team_selection_2DB900.c: the controller of the player choosing (TrainerSlots)

#define MS_BTN_A 0x8000
#define MS_BTN_B 0x4000
#define MS_BTN_DUP 0x0800
#define MS_BTN_DDOWN 0x0400
#define MS_BTN_CUP 0x0008
#define MS_BTN_CDOWN 0x0004
#define MS_BTN_CANY 0x000F

static s32 sEntered; // the select screen is up and the keys below are its trainers
static s32 sRows;
static hu_tm_key sKeys[PORT_MEM_ROWS];
static s32 sCursor;
static s32 sPortrait[PORT_MEM_ROWS]; // each row's VS screen portrait, -1 none (not met)
static hu_window_anim sAnim;

static u8 HuType(u8 t) { // (as battle_enhancements.c's: Stadium's Gen 1 type ids -> the library's)
    switch (t) {
        case 0x00: return HU_TYPE_NORMAL;
        case 0x01: return HU_TYPE_FIGHTING;
        case 0x02: return HU_TYPE_FLYING;
        case 0x03: return HU_TYPE_POISON;
        case 0x04: return HU_TYPE_GROUND;
        case 0x05: return HU_TYPE_ROCK;
        case 0x07: return HU_TYPE_BUG;
        case 0x08: return HU_TYPE_GHOST;
        case 0x14: return HU_TYPE_FIRE;
        case 0x15: return HU_TYPE_WATER;
        case 0x16: return HU_TYPE_GRASS;
        case 0x17: return HU_TYPE_ELECTRIC;
        case 0x18: return HU_TYPE_PSYCHIC;
        case 0x19: return HU_TYPE_ICE;
        case 0x1A: return HU_TYPE_DRAGON;
        default: return HU_TYPE_UNKNOWN;
    }
}

static Controller* Chooser(void) {
    return (D_8423E570 != NULL) ? D_8423E570 : gPlayer1Controller;
}

// Buttons are read as edges from the pad's own state (buttonDown, which Controller_PollInputs always sets from the
// pad), against the state of the frame before: clearing a controller's buttonDown below makes the game's next poll see
// every held button as newly pressed, so its buttonPressed can't be used while the memory screen has the buttons.
static u16 sPrevDown;
// Buttons held when the screen took them or let them go: the screen under it sees them only once they are released,
// so the press that closes the memory screen (or one still held) doesn't also act on the team select.
static u16 sSwallow;

static u16 Pressed(Controller* c) {
    u16 down = (c != NULL) ? c->buttonDown : 0;
    u16 pressed = down & ~sPrevDown;
    sPrevDown = down;
    return pressed;
}

// The screen under it sees no buttons (and no stick) while the memory screen has them.
static void TakeButtons(void) {
    Controller* c[2];
    s32 i;
    c[0] = Chooser();
    c[1] = gPlayer1Controller;
    for (i = 0; i < 2; i++) {
        if (c[i] != NULL) {
            sSwallow |= c[i]->buttonDown;
            c[i]->buttonPressed = 0;
            c[i]->buttonDown = 0;
            c[i]->buttonReleased = 0;
            c[i]->rawStickX = 0;
            c[i]->rawStickY = 0;
            c[i]->stickX = 0.0f;
            c[i]->stickY = 0.0f;
            c[i]->stickMag = 0.0f;
        }
    }
}

// After it closes: the buttons still held from it stay hidden from the screen under it until they are released.
static void SwallowHeld(void) {
    Controller* c[2];
    u16 held = 0;
    s32 i;
    if (sSwallow == 0) {
        return;
    }
    c[0] = Chooser();
    c[1] = gPlayer1Controller;
    for (i = 0; i < 2; i++) {
        if (c[i] != NULL) {
            held |= c[i]->buttonDown & sSwallow;
            c[i]->buttonPressed &= ~sSwallow;
            c[i]->buttonDown &= ~sSwallow;
            c[i]->buttonReleased &= ~sSwallow;
        }
    }
    sSwallow = held;
}

// The screen starts: its trainers, and everything the window shows of them made ready now rather than on opening.
static void Enter(void) {
    const hu_tm* tm = Port_Enh_Memory(); // (the file, read once)
    u8 met[PORT_MEM_ROWS];
    s32 i, j;
    sEntered = 1;
    sCursor = 0;
    sPrevDown = (Chooser() != NULL) ? Chooser()->buttonDown : 0xFFFF; // (a button held on entering is not a press)
    sSwallow = 0;
    hu_window_anim_init(&sAnim, 8, 6);
    sRows = PortEnh_TmScreenKeys(sKeys, PORT_MEM_ROWS);
    for (i = 0; i < sRows; i++) {
        const hu_tm_trainer* t = hu_tm_find(tm, sKeys[i]);
        met[i] = (t != NULL && (t->flags & HU_TM_REACHED)) ? 1 : 0;
        for (j = 0; t != NULL && j < t->team_count; j++) {
            PortEnh_SpeciesIcon(t->team[j].species);
        }
    }
    PortEnh_TmLoadPortraits(sKeys, met, sRows, sPortrait); // (the met trainers' only, as the spec says)
    if (Port_Enh_Log()) {
        fprintf(stderr, "[enh] memory screen: mode %d division %d round2 %d, %d trainers\n", gSessionContext.mode,
                gSessionContext.modeSelection, gSessionContext.inRound2, (int)sRows);
    }
}

static void Utf8(const char* game_text, char* out, s32 size) {
    Port_TextToUtf8(game_text, out, size);
}

static void Publish(void) {
    static PortEnhMemoryView v;
    const hu_tm* tm = Port_Enh_Memory();
    char** move_names = Text_GetStringTable(0x25);
    const hu_tm_trainer* t;
    u16 se_lo, se_hi;
    char game[0x20];
    s32 i, k;

    memset(&v, 0, sizeof v);
    v.open = hu_window_anim_visible(&sAnim);
    v.size = hu_window_anim_size(&sAnim);
    v.fill = hu_window_anim_fill(&sAnim);
    v.rows = sRows;
    v.cursor = sCursor;
    for (i = 0; i < sRows; i++) {
        t = hu_tm_find(tm, sKeys[i]);
        v.reached[i] = (t != NULL && (t->flags & HU_TM_REACHED)) ? 1 : 0;
        if (v.reached[i]) {
            Utf8((const char*)t->name, v.rowNames[i], sizeof v.rowNames[i]);
        }
    }
    t = hu_tm_find(tm, sKeys[sCursor]);
    v.portrait = (v.reached[sCursor]) ? sPortrait[sCursor] : -1;
    Port_Enh_OpponentStatExp(&se_lo, &se_hi);
    v.teamCount = (t != NULL) ? t->team_count : 0;
    for (i = 0; i < v.teamCount; i++) {
        const hu_tm_mon* m = &t->team[i];
        PortEnhMemoryMon* out = &v.team[i];
        u8 base = (m->species >= 1 && m->species <= 151) ? gSpeciesData[m->species - 1].baseSpeed : 0;
        Utf8(Text_CopySpeciesName(game, m->species), out->name, sizeof out->name);
        out->level = m->level;
        out->iconKey = PortEnh_SpeciesIcon(m->species);
        out->hasSpeed = hu_tm_mon_speed(m, base, se_lo, se_hi, &out->speed) ? 1 : 0;
        out->moveCount = HU_TM_MOVES;
        if (Port_Enh_HarderMewtwo() != 0 && gSessionContext.mode == SESSION_MODE_VS_MEWTWO && t->team_count == 1 &&
            m->species == MEWTWO) {
            // Harder vs Mewtwo: the moves seen, from its side file; four rows at first, one more per move past them
            u16 seen[PORT_ENH_BIG_MOVES];
            const s32 n = Port_Enh_MewtwoMemoryMoves(&sKeys[sCursor], seen, PORT_ENH_BIG_MOVES);
            out->moveCount = (u8)(n > HU_TM_MOVES ? n : HU_TM_MOVES);
            for (k = 0; k < out->moveCount; k++) {
                out->moveTypes[k] = HU_TYPE_UNKNOWN;
                if (k < n && seen[k] >= 1 && seen[k] <= PORT_MOVE_HEAL_BELL) {
                    if (move_names != NULL) {
                        Utf8(Text_GetString(NULL, 0, move_names, seen[k] - 1), out->moves[k], PORT_ENH_TEXT);
                    }
                    out->moveTypes[k] = HuType(gMoveData[seen[k] - 1].type);
                } else {
                    strcpy(out->moves[k], "-");
                }
            }
            continue;
        }
        for (k = 0; k < HU_TM_MOVES; k++) {
            u16 move = m->moves[k];
            out->moveTypes[k] = HU_TYPE_UNKNOWN;
            if (move != 0 && (m->seen & (1 << k)) && move <= 165) {
                if (move_names != NULL) {
                    Utf8(Text_GetString(NULL, 0, move_names, move - 1), out->moves[k], PORT_ENH_TEXT);
                }
                out->moveTypes[k] = HuType(gMoveData[move - 1].type);
            } else if (move != 0 || !(t->flags & HU_TM_TEAM_KNOWN)) {
                strcpy(out->moves[k], "-"); // not seen yet (an empty slot of a known moveset shows nothing)
            }
        }
    }
    Port_Enh_PublishMemory(&v);
}

void PortEnh_MemoryScreenFrame(void) {
    Controller* c;
    u16 pressed;

    if (!Port_Enh_TrainerMemoryEnabled()) {
        sEntered = 0;
        return;
    }
    if (!sEntered) {
        Enter();
    }
    if (sRows == 0) {
        return; // Free Battle and the other modes without fixed trainers
    }
    c = Chooser();
    pressed = Pressed(c); // (one step per press, however long it is held)
    if (!hu_window_anim_visible(&sAnim)) {
        SwallowHeld();
        if (pressed & MS_BTN_CANY) {
            Audio_PlaySoundEffectById(SFX_PANEL_OPEN);
            hu_window_anim_set(&sAnim, true);
            TakeButtons();
        }
    } else {
        if (sAnim.open) {
            if (pressed & (MS_BTN_B | (MS_BTN_CANY & ~(MS_BTN_CUP | MS_BTN_CDOWN)))) {
                Audio_PlaySoundEffectById(SFX_MENU_BACK);
                hu_window_anim_set(&sAnim, false);
            } else if (pressed & (MS_BTN_DUP | MS_BTN_CUP)) {
                Audio_PlaySoundEffectById(SFX_MENU_SCROLL);
                sCursor = (sCursor + sRows - 1) % sRows;
            } else if (pressed & (MS_BTN_DDOWN | MS_BTN_CDOWN)) {
                Audio_PlaySoundEffectById(SFX_MENU_SCROLL);
                sCursor = (sCursor + 1) % sRows;
            }
        }
        TakeButtons(); // (also while it closes)
    }
    hu_window_anim_tick(&sAnim);
    Publish();
}

void PortEnh_MemoryScreenEnd(void) {
    if (sEntered) {
        hu_window_anim_reset(&sAnim);
    }
    sEntered = 0;
}

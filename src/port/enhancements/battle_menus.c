// Two enhancements on the battle's command menus (hippodrome-utils docs/specs), both off by default. Compiled with the
// game code: they read the battle structs and draw with the battle HUD's own panels, font and sounds.
//
// Mid-battle move description (MIDBATTLE_MOVE_DESCRIPTION): in the fight menu, holding a D-pad direction shows the
// move on the C button of that direction (Up = C-Up, Right = C-Right, Down = C-Down, Left = C-Left): its type, power,
// accuracy and description, with the labels and text the Pokémon Lab's move details use (textdata tables 6, 0x26 and
// 0xB, so a chosen text language translates them too). The C buttons still pick the move. The window sits under the
// side's menu bar, or beside the move list while R shows it.
//
// Order queueing (ORDER_QUEUEING): while the turn plays out, Z opens a copy of the command menu: A the moves, B the
// party, then the game's own pick buttons (moves: the C buttons; party: C-Up / C-Right / C-Down with three brought,
// C-Up C-Left B C-Right C-Down A with six) queue the order, L goes back a step and Z closes it and drops the order.
// When the side's next command menu comes up the order is entered for it by pressing the same buttons the player
// would, so the game checks it as usual: the menu opens on the first frame and the order is picked on frame
// kQueuePickTimer of the command phase. The COM locks its own order in at frame 90, or at frame 10 once the player has
// picked (BattleScene_ApplyCpuChoice), so a pick on frame 9 starts the turn exactly as early as mashing the buttons
// does, and the frames before it are the time Z has to take the order back. An order that can no longer be given (the
// Pokémon fainted, is asleep, frozen or locked into a move, the move has no PP left or is disabled, the party member
// fainted) is dropped. Only in battles against the COM: with a human on both sides the window would show your next
// order on the shared screen.
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "game_state.h"
#include "variables.h"
#include "moves.h"
#include "status.h"
#include "src/gb_data.h"
#include "src/graphics_textures.h"
#include "src/status_icons.h"
#include "src/text_system.h"
#include "src/ui_graphics.h"
#include "fragments/battle_engine/battle_engine.h"

#include "StadionPort.h"
#include "enhancements.h"
#include "battle_enhancements.h"

#include <hippodrome/move_description.h>
#include <hippodrome/order_queue.h>
#include <hippodrome/window_anim.h>

static const u16 kMoveButton[4] = { BTN_CUP, BTN_CRIGHT, BTN_CDOWN, BTN_CLEFT }; // slot -> C button
static const u16 kSwitch3[3] = { BTN_CUP, BTN_CRIGHT, BTN_CDOWN };               // party slot -> button
static const u16 kSwitch6[6] = { BTN_CUP, BTN_CLEFT, BTN_B, BTN_CRIGHT, BTN_CDOWN, BTN_A };

static u32 sFrame; // counts the battle's HUD frames

// ---- shared helpers ----

static s32 SideOf(ActivePokemon* a) {
    return BattleScene_GetParticipantSideIndex(a) != 0;
}

static s32 IsHumanSide(ActivePokemon* a) {
    // one human trainer on the side (not a COM: isHuman bit 1; not Free Battle's two-player team)
    return !PortEnh_BattleIsDemo() && a != NULL && a->teamRoster != NULL && !(a->teamRoster->isHuman & 2) &&
           a->sessionTeams != NULL && a->sessionTeams->tagTeamSize == 1;
}

static s32 QueueAllowed(ActivePokemon* a, ActivePokemon* foe) {
    return IsHumanSide(a) && foe != NULL && foe->teamRoster != NULL && (foe->teamRoster->isHuman & 2);
}

static TeamRoster* TeamOf(ActivePokemon* a) {
    return a->sessionTeams->tagTeamMember[a->thumbnail.tagTeam];
}

static s32 PortOf(ActivePokemon* a) { // the side's menu colour (its controller port), as the HUD picks it
    s32 port = TeamOf(a)->controllerPort;
    return (port >= 0 && port < 4) ? port : 0;
}

static s32 MoveUsable(ActivePokemon* a, s32 slot) { // a move the C button can pick (BattleScene_SelectMoveFromButtons)
    u8 m = a->state.runtime.moves[slot];
    return m != 0 && m < STRUGGLE + 1;
}

static s32 MoveLegal(ActivePokemon* a, s32 slot) { // and that BattleScene_ValidateSelectedMove takes
    u8 flags = a->state.runtime.disabledMoveState;
    if (!MoveUsable(a, slot) || !(a->state.runtime.pp[slot] & 0x3F)) {
        return 0;
    }
    return !((flags & 0xF) && ((flags >> 4) == slot));
}

static s32 SwitchLegal(ActivePokemon* a, s32 slot) { // a party member BattleScene_HandlePartySlotCursorInput sends in
    TeamRoster* team = a->sessionTeams->tagTeamMember[0];
    return slot >= 0 && slot < team->numberOfPokemonBrought && team->pokemonPicked[slot].hp != 0 &&
           slot != a->state.rosterIndex;
}

// The side's menu gradient (a window of the battle menus' own colours)
static void MenuRect(s32 port, s32 x, s32 y, s32 w, s32 h) {
    u8 top[3], bottom[3];
    Color_RGB8 c0, c1;
    BattleHud_PortMenuColors(port, top, bottom);
    c0.r = top[0];
    c0.g = top[1];
    c0.b = top[2];
    c1.r = bottom[0];
    c1.g = bottom[1];
    c1.b = bottom[2];
    Battle_DrawGradientRectWrapper(x, y, w, h, &c0, &c1);
}

// A window frame as the battle's menus draw theirs: the gradient, the border tiles and the drop shadow.
static void MenuWindow(s32 port, s32 x, s32 y, s32 w, s32 h) {
    MenuRect(port, x + 1, y + 1, w - 1, h - 1);
    BattleHud_DrawBoxBorder(x, y, w, h);
    BattleHud_DrawTranslucentShadowRect(x - 2, y - 2, w + 6, h + 6, 0);
}

// While a window opens it first grows from its middle (only its gradient), then its contents come in: drawn first,
// a black veil over them fades out (hippodrome-utils window_anim.h; AGENTS.md design principle 2).
static s32 WindowGrowing(s32 port, s32 x, s32 y, s32 w, s32 h, const hu_window_anim* anim) {
    f32 size = hu_window_anim_size(anim);
    if (hu_window_anim_fill(anim) > 0.0f) {
        return 0;
    }
    {
        s32 w2 = (s32)(w * size), h2 = (s32)(h * size);
        if (w2 >= 2 && h2 >= 2) {
            MenuRect(port, x + (w - w2) / 2, y + (h - h2) / 2, w2, h2);
        }
    }
    return 1;
}

static void WindowVeil(s32 x, s32 y, s32 w, s32 h, const hu_window_anim* anim) {
    f32 fill = hu_window_anim_fill(anim);
    if (fill < 1.0f) {
        BattleHud_DrawTranslucentShadowRect(x, y, w + 1, h + 1, (s32)(0xFF * (1.0f - fill)));
    }
}

// =============================================================================================================
// Mid-battle move description
// =============================================================================================================

static hu_move_desc sDesc[2];
static u32 sDescFrame[2]; // sFrame + 1 of the last frame the side's fight menu read its buttons
static s32 sDescInit;

void PortEnh_MoveMenuInput(struct ActivePokemon* a) {
    static const int8_t slot_of[4] = { 0, 1, 2, 3 }; // up, right, down, left: the C buttons' slots
    s32 s;
    u16 held;
    u8 dirs = 0;
    u8 usable = 0;
    s32 i;

    if (!Port_Enh_MoveDescriptionsEnabled() || !IsHumanSide(a) || a->state.controller == NULL) {
        return;
    }
    s = SideOf(a);
    if (!sDescInit) {
        hu_move_desc_init(&sDesc[0], 5, 3);
        hu_move_desc_init(&sDesc[1], 5, 3);
        sDescInit = 1;
    }
    held = a->state.controller->buttonDown;
    if (held & BTN_DUP) {
        dirs |= HU_DPAD_UP;
    }
    if (held & BTN_DRIGHT) {
        dirs |= HU_DPAD_RIGHT;
    }
    if (held & BTN_DDOWN) {
        dirs |= HU_DPAD_DOWN;
    }
    if (held & BTN_DLEFT) {
        dirs |= HU_DPAD_LEFT;
    }
    for (i = 0; i < 4; i++) {
        if (MoveUsable(a, i)) {
            usable |= 1 << i;
        }
    }
    if (hu_move_desc_update(&sDesc[s], dirs, slot_of, usable)) {
        BattleAnim_PlayMenuScrollSoundEffect();
    }
    sDescFrame[s] = sFrame + 1;
}

#define DESC_LINE 13 // the battle font's (set 1) line pitch
#define DESC_LINES 3 // the descriptions' lines (the Pokémon Lab's box)

static char** sDetailText;   // textdata 6: TYPE/, POWER/, ACCURACY/ (the Lab's move details)
static char** sTypeText;     // 0x26: the type names
static char** sMoveDescText; // 0xB: the move descriptions

static void LoadTables(void) {
    if (sMoveDescText == NULL) {
        sDetailText = Text_GetStringTable(6);
        sTypeText = Text_GetStringTable(0x26);
        sMoveDescText = Text_GetStringTable(0xB);
    }
}

// The description's lines (the string's own line breaks), copied into `lines`; returns how many.
static s32 DescriptionLines(s32 move, char lines[DESC_LINES][96]) {
    const char* d = Text_GetString(NULL, 0, sMoveDescText, move - 1);
    s32 n = 0;
    while (d != NULL && *d != '\0' && n < DESC_LINES) {
        const char* nl = strchr(d, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - d) : strlen(d);
        if (len >= sizeof(lines[0])) {
            len = sizeof(lines[0]) - 1;
        }
        memcpy(lines[n], d, len);
        lines[n][len] = '\0';
        n++;
        d = (nl != NULL) ? nl + 1 : NULL;
    }
    return n;
}

static void DrawMoveDescription(ActivePokemon* a, s32 s, s32 slot, const hu_window_anim* anim) {
    s32 move = a->state.runtime.moves[slot];
    s32 port = PortOf(a);
    s32 menu_up = a->state.showingCommandOptions == 1; // R shows the move list
    char lines[DESC_LINES][96];
    char buf[32];
    s32 n, i, w, h, x, y, tx;
    s32 type;
    const ColoredString* type_info;
    u8 alpha;

    if (move <= 0 || move > STRUGGLE) {
        return;
    }
    LoadTables();
    n = DescriptionLines(move, lines);
    // as wide as the text needs (a translation can be longer than the ROM's), at least the menus' 200
    w = 200;
    for (i = 0; i < n; i++) {
        s32 lw = Font_MeasureTextExtent(1, 0, lines[i]) + 12;
        if (lw > w) {
            w = lw;
        }
    }
    if (w > 300) {
        w = 300;
    }
    h = 4 + DESC_LINE * (1 + DESC_LINES) + 3;
    // under the side's menu bar (side 0 at the top, side 1 at the bottom), or beside the move list while R shows it
    if (s == 0) {
        x = 96;
        y = menu_up ? 15 + 56 + 5 : 15 + 16 + 5;
    } else {
        x = menu_up ? 25 : 98;
        y = menu_up ? 166 - 5 - h : 206 - 5 - h;
    }
    if (x + w > 316) {
        x = 316 - w;
    }
    if (WindowGrowing(port, x, y, w, h, anim)) {
        return;
    }
    MenuWindow(port, x, y, w, h);
    alpha = (u8)(0xFF * hu_window_anim_fill(anim));

    Font_BeginTranslucentTextRendering();
    Font_SetActive(1, 0);
    // TYPE/<type>  POWER/<power>  ACCURACY/<accuracy>%: the Lab's labels, in three columns
    type = PokemonType_ToDisplayIndex(gMoveData[move - 1].type);
    type_info = &D_80071D88[(type >= 0 && type < 16) ? type : 0];
    tx = x + 6;
    Gfx_SetEnvColor(0xFF, 0xC8, 0xC8, alpha);
    Font_Printf(tx, y + 4, Text_GetString(NULL, 0, sDetailText, 0x12));
    tx += Font_MeasureTextExtent(1, 0, Text_GetString(NULL, 0, sDetailText, 0x12));
    Gfx_SetEnvColor(type_info->color.r, type_info->color.g, type_info->color.b, alpha);
    Font_Printf(tx, y + 4, Text_GetString(NULL, 0, sTypeText, (type >= 0 && type < 16) ? type : 0));
    tx = x + w * 2 / 5;
    Gfx_SetEnvColor(0xFF, 0xC8, 0xC8, alpha);
    Font_Printf(tx, y + 4, Text_GetString(NULL, 0, sDetailText, 0x13));
    tx += Font_MeasureTextExtent(1, 0, Text_GetString(NULL, 0, sDetailText, 0x13));
    if (gMoveData[move - 1].power >= 2) {
        sprintf(buf, "%d", gMoveData[move - 1].power);
    } else {
        sprintf(buf, "---");
    }
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, alpha);
    Font_Printf(tx, y + 4, buf);
    tx = x + w * 2 / 3;
    Gfx_SetEnvColor(0xFF, 0xC8, 0xC8, alpha);
    Font_Printf(tx, y + 4, Text_GetString(NULL, 0, sDetailText, 0x14));
    tx += Font_MeasureTextExtent(1, 0, Text_GetString(NULL, 0, sDetailText, 0x14));
    sprintf(buf, "%d%%", (((gMoveData[move - 1].accuracy * 100) / 255) + 2) / 5 * 5);
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, alpha);
    Font_Printf(tx, y + 4, buf);
    for (i = 0; i < n; i++) {
        Font_Printf(x + 6, y + 4 + DESC_LINE * (i + 1), lines[i]);
    }
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
    Font_EndTexturedTextRendering();
}

static void MoveDescriptionFrame(void) {
    s32 s;
    if (!sDescInit) {
        return;
    }
    for (s = 0; s < 2; s++) {
        ActivePokemon* a = sActivePokemon[s];
        s32 slot;
        if (a == NULL) {
            continue;
        }
        if (sDescFrame[s] != sFrame + 1 && sDescFrame[s] != sFrame) {
            // the fight menu did not read its buttons this frame: it is gone, the window closes
            hu_move_desc_close(&sDesc[s]);
        }
        slot = hu_move_desc_shown(&sDesc[s]);
        if (slot < 0 || sBattleScene.scene->mode != 1 || a->state.selectedCommandOption == 3) {
            if (slot >= 0 && a->state.selectedCommandOption == 3) {
                hu_move_desc_reset(&sDesc[s]); // the move is picked: gone at once
            }
            continue;
        }
        if (s == 0) {
            gDPPortAnchorLeft(gDisplayListHead++);
        } else {
            gDPPortAnchorRight(gDisplayListHead++);
        }
        DrawMoveDescription(a, s, slot, &sDesc[s].anim);
        gDPPortAnchorEnd(gDisplayListHead++);
    }
}

// =============================================================================================================
// Order queueing
// =============================================================================================================

static hu_order_queue sQueue[2];
static hu_window_anim sQueueWin[2];
static u8 sQueueShown[2];    // the hu_oq_state the window shows (it keeps showing the last one while it closes)
static u8 sLive[2];          // the side's command menu is up (the order is being entered or the player picks)
static u32 sCommandFrame[2]; // sFrame + 1 of the last frame the battle asked this side for its action
static s32 sQueuedRoster[2]; // the Pokémon (rosterIndex) the order was given for
static u16 sInjected[2];     // buttons pressed for the player this frame (taken back after the command input)
static s32 sQueueInit;
static s32 sCommandSeen; // a command phase has come up in this battle (no queueing during the opening)

// The command phase frame (sElapsedOrderSelectionTime) on which a queued order is picked: the COM picks at frame 10
// once the player has (BattleScene_ApplyCpuChoice: elapsed >= 10 and the player's selectedCommandOption == 3, checked
// in side order), so a pick on frame 9 lets the turn start on frame 10 whichever side the player is on, as when
// mashing.
#define kQueuePickTimer 9.0f

static void QueueInit(void) {
    s32 s;
    for (s = 0; s < 2; s++) {
        hu_oq_init(&sQueue[s]);
        sQueue[s].open_frame = 1;      // the menu opens on the first frame of the command selection...
        sQueue[s].pick_frame = 0xFFFF; // ...and the pick waits for the command phase's frame (PlayerCommandBegin)
        hu_window_anim_init(&sQueueWin[s], 6, 4);
        sLive[s] = 0;
        sCommandFrame[s] = 0;
        sQueuedRoster[s] = -1;
        sQueueShown[s] = HU_OQ_HIDDEN;
        sInjected[s] = 0;
    }
    sCommandSeen = 0;
    sQueueInit = 1;
}

void PortEnh_BattleMenusBegin(void) {
    QueueInit();
    if (sDescInit) {
        hu_move_desc_reset(&sDesc[0]);
        hu_move_desc_reset(&sDesc[1]);
    }
}

static void QueueLog(s32 s, const char* what, f32 elapsed) {
    if (Port_Enh_Log()) {
        fprintf(stderr, "[order-queue] side %d: %s (state %d slot %d, frame %u, command frame %d)\n", (int)s, what,
                (int)sQueue[s].state, (int)sQueue[s].slot, (unsigned)sFrame, (int)elapsed);
    }
}

static void Drop(s32 s, const char* why) {
    if (sQueue[s].state != HU_OQ_HIDDEN || sQueue[s].entering) {
        QueueLog(s, why, -1.0f);
    }
    hu_oq_cancel(&sQueue[s]);
    sQueuedRoster[s] = -1;
}

// Whether the queued order can still be given (the game's own rules for its command menu:
// BattleScene_CollectPlayerAction). `at_menu`: the side's command menu is coming up now (the lock checks only mean
// something then: during the turn the battle sets and clears those flags while it plays the moves out).
static s32 OrderLegal(ActivePokemon* a, ActivePokemon* foe, s32 s, s32 at_menu) {
    hu_order_queue* q = &sQueue[s];
    const BattleMonRuntime* rt = &a->state.runtime;
    if (a->state.replacementState == 0x10 || rt->currentHp == 0) {
        return 0; // fainted: the next menu is the forced replacement
    }
    if (sQueuedRoster[s] >= 0 && a->state.rosterIndex != sQueuedRoster[s]) {
        return 0; // a different Pokémon is out now
    }
    if (at_menu && ((rt->volatileStatusFlags & 0x60) || (rt->immobilizedReason & IMMOBILIZED_ACTION_LOCKED))) {
        return 0; // the second turn of Fly, Dig, Solar Beam..., a rampage or recharging: no menu at all this turn
    }
    if (q->state == HU_OQ_SWITCH_QUEUED) {
        return SwitchLegal(a, q->slot);
    }
    if (q->state == HU_OQ_MOVE_QUEUED) {
        if (at_menu && ((rt->pendingStatusCondition & 0x27) ||
                        (rt->immobilizedReason & (IMMOBILIZED_BIDING | IMMOBILIZED_TRAPPING)) ||
                        (foe->state.runtime.immobilizedReason & IMMOBILIZED_TRAPPING))) {
            return 0; // asleep, frozen, locked into a move or held by a trapping move: Fight picks no move
        }
        return MoveLegal(a, q->slot);
    }
    return 1;
}

void PortEnh_PlayerCommandBegin(struct ActivePokemon* a, struct ActivePokemon* foe, float elapsed) {
    s32 s;
    hu_order_queue* q;
    Controller* c;
    hu_oq_action act;

    if (!Port_Enh_OrderQueueEnabled() || !QueueAllowed(a, foe) || a->state.controller == NULL) {
        return;
    }
    if (!sQueueInit) {
        QueueInit();
    }
    s = SideOf(a);
    q = &sQueue[s];
    c = a->state.controller;
    sCommandFrame[s] = sFrame + 1;
    sCommandSeen = 1;
    sInjected[s] = 0;
    if (a->state.replacementState == 0x10) {
        Drop(s, "fainted: dropped");
        sLive[s] = 1;
        return;
    }
    if (foe->state.replacementState == 0x10) {
        return; // the other side sends in a replacement: this side's menu is not up, the turn is not over yet
    }
    if (!sLive[s]) {
        // the moves have played out and this side's command menu comes up
        sLive[s] = 1;
        if (hu_oq_queued(q) && !OrderLegal(a, foe, s, 1)) {
            Drop(s, "illegal at the command menu: dropped");
        } else {
            if (!hu_oq_queued(q) && q->state != HU_OQ_HIDDEN) {
                QueueLog(s, "not queued at the command menu: dropped", elapsed);
            }
            hu_oq_resolution_end(q);
            if (q->entering) {
                QueueLog(s, "entering", elapsed);
            }
        }
    }
    if (!q->entering) {
        return;
    }
    if (c->buttonPressed & BTN_Z) {
        hu_oq_z(q); // taken back: the player picks as usual
        BattleAnim_PlayMenuBackSoundEffect();
        QueueLog(s, "Z: taken back", elapsed);
        return;
    }
    // not the COM's 30/90 frames (hippodrome-utils' default): the COM picks early once the player has, so that timing
    // made a queued order slower than mashing the buttons
    q->pick_frame = (elapsed >= kQueuePickTimer) ? (u16)(q->frame + 1) : 0xFFFF;
    act = hu_oq_command_frame(q);
    switch (act) {
        case HU_OQ_OPEN_FIGHT:
        case HU_OQ_OPEN_SWITCH:
            if (a->state.selectedCommandOption == 0) {
                sInjected[s] = (act == HU_OQ_OPEN_FIGHT) ? BTN_A : BTN_B;
            } else if (a->state.selectedCommand != (u32)((act == HU_OQ_OPEN_FIGHT) ? 1 : 2)) {
                Drop(s, "the player opened another menu: dropped");
            }
            break;
        case HU_OQ_PICK: {
            s32 slot = q->slot;
            s32 is_switch = a->state.selectedCommand == 2;
            if (a->state.selectedCommandOption == 3) {
                break; // the player picked by themselves
            }
            if (is_switch && a->state.selectedCommandOption == 2 && SwitchLegal(a, slot)) {
                sInjected[s] = (a->sessionTeams->tagTeamMember[0]->numberOfPokemonBrought < 4) ? kSwitch3[slot % 3]
                                                                                                : kSwitch6[slot % 6];
                QueueLog(s, "picked the party member", elapsed);
            } else if (!is_switch && a->state.selectedCommand == 1 && a->state.selectedCommandOption == 1 &&
                       MoveLegal(a, slot)) {
                sInjected[s] = kMoveButton[slot & 3];
                QueueLog(s, "picked the move", elapsed);
            } else {
                QueueLog(s, "the menu could not take it: dropped", elapsed);
            }
            sQueuedRoster[s] = -1;
            break;
        }
        default:
            break;
    }
    if (sInjected[s] != 0) {
        c->buttonPressed |= sInjected[s];
        c->buttonDown |= sInjected[s];
    }
}

void PortEnh_PlayerCommandEnd(struct ActivePokemon* a) {
    s32 s;
    if (!sQueueInit || a == NULL || a->state.controller == NULL) {
        return;
    }
    s = SideOf(a);
    if (sInjected[s] != 0) {
        a->state.controller->buttonPressed &= ~sInjected[s];
        a->state.controller->buttonDown &= ~sInjected[s];
        sInjected[s] = 0;
    }
}

// The queue's buttons while the turn plays out (the side's command menu is not up).
static void QueueInput(ActivePokemon* a, ActivePokemon* foe, s32 s) {
    hu_order_queue* q = &sQueue[s];
    u16 p = a->state.controller->buttonPressed;
    s32 changed = 0, back = 0;
    s32 i;

    if (q->entering) {
        return;
    }
    if (p & BTN_Z) {
        back = q->state != HU_OQ_HIDDEN;
        changed = hu_oq_z(q);
        if (q->state == HU_OQ_HIDDEN) {
            sQueuedRoster[s] = -1;
        }
    } else if (q->state == HU_OQ_SWITCH_SELECT) {
        // the party menu's own buttons: with more than three brought B and A pick party members too
        s32 brought = a->sessionTeams->tagTeamMember[0]->numberOfPokemonBrought;
        s32 three = brought < 4;
        for (i = 0; i < (three ? 3 : 6); i++) {
            u16 b = three ? kSwitch3[i] : kSwitch6[i];
            if ((p & b) && SwitchLegal(a, i)) {
                changed = hu_oq_pick(q, i);
                break;
            }
        }
        if (!changed && (p & BTN_L)) {
            back = changed = hu_oq_l(q);
        }
    } else if (q->state == HU_OQ_MOVE_SELECT) {
        for (i = 0; i < 4; i++) {
            if ((p & kMoveButton[i]) && MoveLegal(a, i)) {
                changed = hu_oq_pick(q, i);
                break;
            }
        }
        if (!changed && (p & BTN_L)) {
            back = changed = hu_oq_l(q);
        }
    } else if (p & BTN_A) {
        changed = hu_oq_a(q);
    } else if (p & BTN_B) {
        changed = hu_oq_b(q);
    } else if (p & BTN_L) {
        back = changed = hu_oq_l(q);
    }
    if (hu_oq_queued(q)) {
        sQueuedRoster[s] = a->state.rosterIndex;
    }
    if (changed) {
        if (back) {
            BattleAnim_PlayMenuBackSoundEffect();
        } else {
            BattleAnim_PlayMenuConfirmSoundEffect();
        }
        QueueLog(s, "menu", -1.0f);
    }
    if (hu_oq_queued(q) && !OrderLegal(a, foe, s, 0)) {
        Drop(s, "became illegal: dropped");
    }
}

// The command menu's A / B half (the game's bar has S にげる too, which the queue does not take).
#define QUEUE_BAR_W 134

static void DrawFightPokemonBar(ActivePokemon* a, s32 x, s32 y, s32 port) {
    MenuWindow(port, x, y, QUEUE_BAR_W, 16);
    gSPDisplayList(gDisplayListHead++, gUiMaskedTextureDL);
    Gfx_DrawTextureRgba16Ia8(x + 7, y + 3, 16, 12, gButtonA, gButtonAMask, 0x10, 0x100000);
    Gfx_DrawTextureRgba16Ia8(x + 70, y + 3, 16, 12, gButtonB, gButtonBMask, 0x10, 0x100000);
    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
    Font_BeginTranslucentTextRendering();
    Font_SetActive(1, 0);
    Font_Printf(x + 24, y + 3, Text_GetString(NULL, 0, sBattleMessagesTable, 0x9E));
    Font_Printf(x + 88, y + 3, Text_GetString(NULL, 0, sBattleMessagesTable, 0x9F));
    Font_EndTexturedTextRendering();
}

// The order on a bar: "Next: <move>" or "Next: <Pokémon>", with the buttons that take it back.
static void DrawQueuedBar(ActivePokemon* a, s32 s, s32 x, s32 y, s32 port) {
    hu_order_queue* q = &sQueue[s];
    char line[96];
    const char* what = "";
    const char* hint = Port_TextPortString("PORT_QUEUE_HINT");

    MenuWindow(port, x, y, 200, 16);
    if (q->state == HU_OQ_MOVE_QUEUED || sQueueShown[s] == HU_OQ_MOVE_QUEUED) {
        s32 m = (q->slot >= 0) ? a->state.runtime.moves[q->slot & 3] : 0;
        what = (m > 0 && m <= STRUGGLE) ? Text_GetString(NULL, 0, sMoveNameTable, m - 1) : "";
    } else {
        TeamRoster* team = a->sessionTeams->tagTeamMember[0];
        if (q->slot >= 0 && q->slot < 6) {
            what = (const char*)team->pokemonPicked[q->slot].nickname;
        }
    }
    snprintf(line, sizeof line, "%s %s", Port_TextPortString("PORT_QUEUE_NEXT"), what);
    Font_BeginTranslucentTextRendering();
    Font_SetActive(1, 0);
    Font_Printf(x + 6, y + 3, line);
    Gfx_SetEnvColor(0xE0, 0xE0, 0xF0, 0xFF);
    Font_Printf(x + 200 - 6 - Font_MeasureTextExtent(1, 0, hint), y + 3, hint);
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
    Font_EndTexturedTextRendering();
}

static void DrawQueue(ActivePokemon* a, s32 s) {
    const hu_window_anim* w = &sQueueWin[s];
    // Widescreen: at the top, level with the compact status boxes and in the gap between them. At 4:3 the gap is too
    // narrow for the game's 200-wide panels, so the window goes under the side's status box (side 0 top left, side 1
    // bottom right, as their menus).
    s32 top = Port_WideMarginN64() >= 40;
    s32 three = a->sessionTeams->tagTeamMember[0]->numberOfPokemonBrought < 4;
    s32 x = (s == 0) ? 96 : 25, y = top ? 16 : ((s == 0) ? 60 : 120);
    s32 W, H;
    s32 port = PortOf(a);
    u8 shown = sQueueShown[s];

    switch (shown) {
        case HU_OQ_SHOWING:
            W = QUEUE_BAR_W;
            H = 16;
            break;
        case HU_OQ_MOVE_SELECT:
            W = 199;
            H = 56;
            break;
        case HU_OQ_SWITCH_SELECT:
            W = three ? 99 : 200;
            H = 102;
            break;
        default:
            W = 200;
            H = 16;
            break;
    }
    if (s != 0 && !top) {
        y = 196 - H;
    }
    if (WindowGrowing(port, x, y, W, H, w)) {
        return;
    }
    switch (shown) {
        case HU_OQ_SHOWING:
            DrawFightPokemonBar(a, x, y, port);
            break;
        case HU_OQ_MOVE_SELECT:
            BattleHud_DrawMoveSelectMenu(a, x, y, port);
            break;
        case HU_OQ_SWITCH_SELECT:
            if (three) {
                BattleHud_DrawTeamStatusList(a, a->sessionTeams->tagTeamMember[0], x, y, port);
            } else {
                BattleHud_DrawDoublesTeamStatusList(a, x, y, port);
            }
            break;
        default:
            DrawQueuedBar(a, s, x, y, port);
            break;
    }
    WindowVeil(x, y, W, H, w);
    // a tab above the window says what it is for
    {
        const char* title = Port_TextPortString("PORT_QUEUE_TITLE");
        s32 tw = Font_MeasureTextExtent(1, 0, title);
        static Color_RGB8 kTab = { 0x10, 0x10, 0x18 };
        Battle_DrawGradientRectWrapper(x + 4, y - 14, tw + 8, 13, &kTab, &kTab);
        Font_BeginTranslucentTextRendering();
        Font_SetActive(1, 0);
        Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, (u8)(0xFF * hu_window_anim_fill(w)));
        Font_Printf(x + 8, y - 14, title);
        Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
        Font_EndTexturedTextRendering();
    }
}

static void OrderQueueFrame(void) {
    s32 s;
    if (!sQueueInit) {
        QueueInit();
    }
    for (s = 0; s < 2; s++) {
        ActivePokemon* a = sActivePokemon[s];
        ActivePokemon* foe = sActivePokemon[!s];
        hu_order_queue* q = &sQueue[s];
        s32 resolving;
        if (a == NULL || foe == NULL || !QueueAllowed(a, foe) || a->state.controller == NULL) {
            continue;
        }
        if (sCommandFrame[s] != sFrame + 1 && sCommandFrame[s] != sFrame) {
            if (sLive[s] && q->entering) {
                Drop(s, "the command menu went before the order was entered: dropped");
            }
            sLive[s] = 0; // the side is not being asked for its action: the turn plays out
        }
        resolving = sCommandSeen && !sLive[s] && sBattleScene.scene->winner == 0 && sBattleScene.scene->mode != 1;
        if (resolving) {
            QueueInput(a, foe, s);
        } else if (!sLive[s] && q->state != HU_OQ_HIDDEN && !q->entering && sBattleScene.scene->winner != 0) {
            Drop(s, "the battle is over: dropped");
        }
        if (hu_oq_visible(q) && resolving) {
            sQueueShown[s] = q->state;
            hu_window_anim_set(&sQueueWin[s], 1);
        } else {
            hu_window_anim_set(&sQueueWin[s], 0);
        }
        hu_window_anim_tick(&sQueueWin[s]);
        if (hu_window_anim_visible(&sQueueWin[s])) {
            if (s == 0) {
                gDPPortAnchorLeft(gDisplayListHead++);
            } else {
                gDPPortAnchorRight(gDisplayListHead++);
            }
            DrawQueue(a, s);
            gDPPortAnchorEnd(gDisplayListHead++);
        }
    }
}

// =============================================================================================================

void PortEnh_BattleMenusFrame(void) {
    sFrame++;
    if (Port_Enh_MoveDescriptionsEnabled()) {
        MoveDescriptionFrame();
    }
    if (Port_Enh_OrderQueueEnabled()) {
        OrderQueueFrame();
    }
}

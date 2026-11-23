// The battle's enhancements (hippodrome-utils docs/specs): what runs when a battle starts and on every frame it draws
// its HUD (StadionPort.h "Enhancements": BattleScene_Init and Battle_RenderHud call these). Compiled with the game
// code, kept in the port, so game/ holds only decomp code. Each enhancement checks its own setting.
//
// Side summary bars and speed estimates (PARTY_SUMMARY_BARS.md, SPEED_STAT_ESTIMATE.md): a card per Pokémon of each
// side, kept here from the battle's own state, and published each frame for summary_bars.cpp, which draws the cards
// in the window's room beside the game's picture. Besides the two hooks above, the battle tells this file who acts
// first (PortEnh_TurnOrder, Battle_StartTurn), that the acting Pokémon cannot act this turn (PortEnh_MoveNotUsed,
// ProcessBattleTurn) and that a move's presentation starts (PortEnh_MoveStart, BattleScene_ActionSetupCameraStep).
// Nothing the game does changes.
//
// Trainer memory (TRAINER_MEMORY.md): against a COM trainer with a memory key (stadion_trainer_memory.c), what the
// cards learn is kept in the memory as the battle shows it: each Pokémon sent out, each move used (as the cards reveal
// it) and each narrowed speed estimate. With the setting on, the cards start from what the memory already knows.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "game_state.h"
#include "variables.h"
#include "moves.h"
#include "src/gb_data.h"
#include "src/matrix.h"
#include "src/text_system.h"
#include "fragments/battle_engine/battle_engine.h"

#include "StadionPort.h"
#include "rom_version.h"
#include "enhancements.h"
#include "battle_enhancements.h"

#include <hippodrome/summary_card.h>

arg1_func_80010CA8* Model_ComputeNicknameColorVariant(arg1_func_80010CA8* arg0, BattleMon* arg1); // model_renderer.c

s32 PortEnh_BattleIsDemo(void) {
    // 16 the attract mode's demo battle, 17 and up its cutscenes and the other scripted scenes (game_state.h)
    return gSessionContext.mode >= SESSION_MODE_ATTRACT_DEMO_BATTLE;
}

static void EnhLog(const char* fmt, ...) {
    va_list ap;
    if (!Port_Enh_Log()) return;
    va_start(ap, fmt);
    fprintf(stderr, "[enh] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

// ---- the cards ----

static hu_side sSides[2];
static BattleMon* sCardMon[2][HU_SIDE_MAX];  // each card's Pokémon
static s8 sCardOf[2][2][6];                  // [side][tag team member][pokemonPicked index] -> card, -1 none
static TeamRoster* sRosters[2][2];           // the sides' trainers (two in a tag battle)
static s32 sMembers[2];
static s32 sNeedInit;
static s32 sReady;
static s32 sViewer = -1;    // the side whose own Pokémon are fully known (1P against the COM), -1 in multiplayer
static s32 sPendingSlot[2]; // the move slot each side committed this turn, revealed when the move is presented
static s32 sTransformed[2];
static uint32_t sIconKey[2][HU_SIDE_MAX];
static s32 sHasKey; // the opponent is a trainer of the memory (sKey)
static hu_tm_key sKey;
static s32 sSaved;

void PortEnh_BattleBegin(void) {
    PortEnh_BattleMenusBegin();
    sNeedInit = 1;
    sReady = 0;
}

static s32 IsCom(const TeamRoster* r) {
    return r != NULL && (r->isHuman & 2) != 0; // the COM's roster has isHuman 2 or 3
}

// Same Pokémon: the fields a battle does not change.
static s32 SameMon(const BattleMon* a, const BattleMon* b) {
    return a->species.id == b->species.id && a->unk_0E == b->unk_0E && a->unk_1E == b->unk_1E &&
           a->unk_10 == b->unk_10 && a->level == b->level && memcmp(a->nickname, b->nickname, sizeof(a->nickname)) == 0;
}

static hu_status StatusOf(u8 condition, u8 toxic) {
    if (condition & 0x07) return HU_STATUS_SLEEP;
    if (condition & 0x20) return HU_STATUS_FREEZE;
    if (condition & 0x10) return HU_STATUS_BURN;
    if (condition & 0x40) return HU_STATUS_PARALYSIS;
    if (condition & 0x08) return toxic ? HU_STATUS_TOXIC : HU_STATUS_POISON;
    return HU_STATUS_NONE;
}

static u8 BaseSpeed(u8 species) {
    return (species >= 1 && species <= 151) ? gSpeciesData[species - 1].baseSpeed : 0;
}

// The card's picture: the game's own menu icon of the species (the model icon archive the team select screens load,
// PokeIcon_OpenModelArchive: Rev 2 ROM 0x81D000, a 0x10-byte header, then 0x10-byte entries of offset and size, one
// per species, of 40 x 40 RGBA5551 pixels), in the Pokémon's colours (Model_ComputeNicknameColorVariant, so a
// nickname's hue and the hue choice show). Read straight from the ROM, so the battle's memory pools do not change.
#define ICON_ARCHIVE 0x81D000u
#define ICON_BYTES (PORT_ENH_ICON * PORT_ENH_ICON * 2)

static u32 ReadBE32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static uint32_t LoadIconVariant(u8 species, arg1_func_80010CA8 variant) {
    static u8 raw[ICON_BYTES];
    static u8 rgba[PORT_ENH_ICON * PORT_ENH_ICON * 4];
    u8 head[0x10], entry[0x10];
    u32 base, offset, size;
    uint32_t key;
    s32 i;

    if (species < 1 || species > 151) {
        return 0;
    }
    key = (uint32_t)species | ((uint32_t)(u16)variant.unk_00 << 8) | 0x80000000u;
    if (Port_Enh_HasCardIcon(key)) {
        return key;
    }
    base = Port_RomToRaw(ICON_ARCHIVE);
    if (Port_RomRead(base, head, sizeof head) != sizeof head || ReadBE32(head + 0x0C) < 151 ||
        ReadBE32(head + 0x0C) > 0x400 ||
        Port_RomRead(base + (u32)species * 0x10, entry, sizeof entry) != sizeof entry) {
        return 0;
    }
    offset = ReadBE32(entry);
    size = ReadBE32(entry + 4);
    if (size != ICON_BYTES || Port_RomRead(base + offset, raw, sizeof raw) != sizeof raw) {
        EnhLog("card icon: species %d not readable (size 0x%x)", species, (unsigned)size);
        return 0;
    }
    for (i = 0; i < PORT_ENH_ICON * PORT_ENH_ICON; i++) {
        u16 px = (u16)((raw[i * 2] << 8) | raw[i * 2 + 1]);
        px = Color_AdjustRGB5551(px, variant);
        rgba[i * 4 + 0] = (u8)(((px >> 11) & 0x1F) * 255 / 31);
        rgba[i * 4 + 1] = (u8)(((px >> 6) & 0x1F) * 255 / 31);
        rgba[i * 4 + 2] = (u8)(((px >> 1) & 0x1F) * 255 / 31);
        rgba[i * 4 + 3] = (px & 1) ? 0xFF : 0x00;
    }
    Port_Enh_SetCardIcon(key, rgba);
    return key;
}

static uint32_t LoadIcon(BattleMon* mon) {
    arg1_func_80010CA8 variant;
    if (mon == NULL || mon->species.id < 1 || mon->species.id > 151) {
        return 0;
    }
    Model_ComputeNicknameColorVariant(&variant, mon);
    return LoadIconVariant(mon->species.id, variant);
}

uint32_t PortEnh_SpeciesIcon(u8 species) {
    arg1_func_80010CA8 variant;
    variant.raw = 0; // the species' own colours (a COM trainer's Pokémon have no nickname)
    return LoadIconVariant(species, variant);
}

// Whether Stat Exp difficulty holds this battle's COM Pokémon (statexp_difficulty.c: not in Free Battle or the demos).
static s32 OpponentStatExp(u16* lo, u16* hi) {
    *lo = 0;
    *hi = 65535;
    if (gSessionContext.mode == SESSION_MODE_ANYTHING_GOES || PortEnh_BattleIsDemo()) {
        return 0;
    }
    Port_Enh_OpponentStatExp(lo, hi);
    return 1;
}

static void InitSide(s32 s) {
    ActivePokemon* a = sActivePokemon[s];
    BattleSessionTeams* t = a->sessionTeams;
    s32 members = (t != NULL && t->tagTeamSize == 2) ? 2 : 1;
    TeamRoster* r = (t != NULL && t->tagTeamMember[0] != NULL) ? t->tagTeamMember[0] : a->teamRoster;
    s32 count, brought, i, j, m;
    u8 picked_mask = 0;

    memset(sCardOf[s], -1, sizeof sCardOf[s]);
    memset(sCardMon[s], 0, sizeof sCardMon[s]);
    sMembers[s] = members;
    sRosters[s][0] = r;
    sRosters[s][1] = (members == 2) ? t->tagTeamMember[1] : NULL;

    if (members == 1) {
        // the whole party (a 3-of-6 battle brings three of it): the roster's full list, the brought ones matched to it
        ExtendedRosterInfo* info = r->rosterInfo;
        BattleMon* party = (info != NULL) ? info->battleMon : r->pokemonPicked;
        u8 used = 0;
        brought = r->numberOfPokemonBrought;
        count = (info != NULL) ? info->numberOfPokemonCanBePicked : brought;
        if (count <= 0 || count > 6) count = brought;
        if (brought <= 0 || brought > count) brought = count;
        hu_side_init(&sSides[s], count, brought, s == sViewer ? HU_REVEAL_ALL : HU_REVEAL_AS_USED);
        for (i = 0; i < count; i++) {
            sCardMon[s][i] = &party[i];
        }
        for (i = 0; i < brought; i++) {
            for (j = 0; j < count; j++) {
                if (!(used & (1 << j)) && SameMon(&r->pokemonPicked[i], &party[j])) break;
            }
            if (j == count) { // (not expected) fall back to the species
                for (j = 0; j < count; j++) {
                    if (!(used & (1 << j)) && r->pokemonPicked[i].species.id == party[j].species.id) break;
                }
            }
            if (j < count) {
                used |= 1 << j;
                sCardOf[s][0][i] = (s8)j;
                picked_mask |= 1 << j;
            }
        }
    } else {
        // a tag battle: both trainers' brought Pokémon, in order
        count = 0;
        for (m = 0; m < 2; m++) {
            TeamRoster* mr = sRosters[s][m];
            for (i = 0; mr != NULL && i < mr->numberOfPokemonBrought && count < HU_SIDE_MAX; i++) {
                sCardOf[s][m][i] = (s8)count;
                sCardMon[s][count++] = &mr->pokemonPicked[i];
            }
        }
        brought = count;
        hu_side_init(&sSides[s], count, brought, s == sViewer ? HU_REVEAL_ALL : HU_REVEAL_AS_USED);
        picked_mask = (u8)((1 << count) - 1);
    }
    for (i = 0; i < count; i++) {
        const BattleMon* mon = sCardMon[s][i];
        u16 moves[4];
        for (j = 0; j < 4; j++) moves[j] = mon->moves[j];
        hu_side_set_mon(&sSides[s], i, mon->species.id, mon->level, mon->hp, mon->maxHp, moves,
                        BaseSpeed(mon->species.id), s == sViewer ? mon->speed : 0);
        if (s != sViewer) {
            // the widest range under the opponents' Stat Exp setting (SPEED_STAT_ESTIMATE)
            u16 lo, hi;
            if (OpponentStatExp(&lo, &hi) && IsCom(r)) {
                hu_card* c = &sSides[s].cards[i];
                c->speed = hu_speed_intersect(c->speed, hu_speed_initial_se(BaseSpeed(mon->species.id), mon->level,
                                                                            lo, hi));
                c->transform_speed = c->speed;
            }
        }
        sIconKey[s][i] = Port_Enh_SummaryBarsEnabled() ? LoadIcon(sCardMon[s][i]) : 0;
    }
    if (s == sViewer || members == 2) {
        hu_side_set_picked(&sSides[s], picked_mask);
    }
}

// The trainer memory of the opponent's side, and what it already knows (the moves seen, the speed estimates).
static void InitMemory(void) {
    s32 hidden = (sViewer == 0) ? 1 : -1;
    const hu_tm_trainer* t;
    u16 lo, hi;
    s32 i, k;

    sHasKey = (hidden >= 0) && sMembers[hidden] == 1 && PortEnh_TmKeyForSession(&sKey);
    if (!sHasKey) {
        return;
    }
    t = hu_tm_find(Port_Enh_Memory(), sKey);
    if (t == NULL || !(t->flags & HU_TM_TEAM_KNOWN)) {
        PortEnh_TmTrainerLoaded(); // (the prep screen normally records it)
        t = hu_tm_find(Port_Enh_Memory(), sKey);
    }
    if (!Port_Enh_TrainerMemoryEnabled() || t == NULL) {
        return; // the memory is still kept, but this battle's cards start from scratch
    }
    OpponentStatExp(&lo, &hi);
    for (i = 0; i < sSides[hidden].count; i++) {
        hu_card* c = &sSides[hidden].cards[i];
        const hu_tm_mon* m = hu_tm_find_mon(t, c->species);
        hu_speed_range known;
        if (m == NULL) continue;
        if (hu_tm_mon_speed(m, BaseSpeed(c->species), lo, hi, &known)) {
            c->speed = hu_speed_intersect(c->speed, known);
            c->transform_speed = c->speed;
        }
        for (k = 0; k < HU_CARD_MOVES; k++) {
            if (hu_tm_move_seen(m, c->moves[k])) c->revealed |= (u8)(1 << k);
        }
    }
}

static void Init(void) {
    s32 com0 = IsCom(sActivePokemon[0]->teamRoster), com1 = IsCom(sActivePokemon[1]->teamRoster);

    sViewer = (!com0 && com1) ? 0 : (com0 && !com1) ? 1 : -1;
    InitSide(0);
    InitSide(1);
    sPendingSlot[0] = sPendingSlot[1] = -1;
    sTransformed[0] = sTransformed[1] = 0;
    sSaved = 0;
    InitMemory();
    EnhLog("battle: viewer %d, sides %d/%d (brought %d/%d), mode %d, isHuman %d/%d", (int)sViewer, sSides[0].count,
           sSides[1].count, sSides[0].brought, sSides[1].brought, (int)gSessionContext.mode,
           sActivePokemon[0]->teamRoster->isHuman, sActivePokemon[1]->teamRoster->isHuman);
    if (sHasKey) {
        EnhLog("memory key: cup %d division %d round %d trainer %d", sKey.cup, sKey.division, sKey.round,
               sKey.trainer);
    }
    sNeedInit = 0;
    sReady = 1;
}

static s32 MemberOf(s32 s, s32 m) {
    return (m >= 0 && m < sMembers[s]) ? m : 0;
}

// The card of the Pokémon in battle (`shown`: the one the HUD shows, whose trainer the thumbnail follows).
static s32 CardOf(s32 s, s32 shown) {
    const ActivePokemon* a = sActivePokemon[s];
    s32 m = MemberOf(s, shown ? a->thumbnail.tagTeam : a->state.teamMemberIndex);
    s32 p = a->state.rosterIndex;
    return (p >= 0 && p < 6) ? sCardOf[s][m][p] : -1;
}

// How many times the paralysis cut has been applied to the active Pokémon's speed since it was last recomputed: the
// difference between its stage-applied raw stat and its live speed. Every cut happens on screen (paralysis, then each
// stat change of the opponent: Gen 1's quirk), so this is information the player has.
static hu_speed_mods ModsOf(const BattleMonRuntime* rt, s32 paralysed) {
    hu_speed_mods m = hu_speed_mods_none();
    u16 v;
    m.stage = (s8)(rt->statChanges[2] - 7);
    if (paralysed) {
        v = hu_apply_stage(&hu_gb_speed_rules, rt->unmodifiedSpeed, m.stage);
        while (v > rt->modifiedSpeed && m.paralysis_cuts < 8) {
            v = v >> 2;
            if (v == 0) v = 1;
            m.paralysis_cuts++;
        }
    }
    return m;
}

static const BattleMon* ActiveMon(s32 s) {
    s32 c = CardOf(s, 0);
    return c >= 0 ? sCardMon[s][c] : NULL;
}

static s32 IsSwitch(u8 used_move) {
    return used_move == 0 || (used_move & 0xF0) == 0xF0;
}

static void Poll(void) {
    s32 s, m, p;
    for (s = 0; s < 2; s++) {
        hu_side* side = &sSides[s];
        s32 active = CardOf(s, 0);
        s32 shown = CardOf(s, 1);
        const BattleMonRuntime* rt = &sActivePokemon[s]->state.runtime;

        // The cards change as the battle shows the change (the spec's "Card state"): the HUD boxes' HP and status are
        // those of the party entries (pokemonPicked), which the battle updates as the HP bar drains and as the
        // status appears.
        if (shown >= 0 && !(side->cards[shown].flags & HU_CARD_F_ACTIVE)) {
            hu_side_send_out(side, shown);
            sTransformed[s] = 0;
            if (sHasKey && s != sViewer) {
                hu_tm_record_sent_out(Port_Enh_Memory(), sKey, side->cards[shown].species, side->cards[shown].level);
            }
        }
        for (m = 0; m < sMembers[s]; m++) {
            TeamRoster* r = sRosters[s][m];
            for (p = 0; r != NULL && p < r->numberOfPokemonBrought && p < 6; p++) {
                s32 c = sCardOf[s][m][p];
                const BattleMon* bm = &r->pokemonPicked[p];
                if (c < 0) continue;
                // (Toxic is the active one's poison with its counter running; the party entry keeps plain poison)
                hu_side_set_hp(side, c, bm->hp);
                hu_side_set_status(side, c, StatusOf(bm->statusCondition, c == active && rt->toxicCounter != 0));
                if (bm->hp == 0) {
                    hu_side_faint(side, c);
                }
            }
        }
        if (active >= 0) {
            s32 transformed = (rt->uniqueStatusFlags & 8) != 0;
            hu_side_set_substitute(side, active, rt->substituteHp != 0);
            if (transformed && !sTransformed[s]) {
                // the copied Speed: the viewer knows its own exactly; the opponent's is its estimate
                s32 o = !s;
                s32 oc = CardOf(o, 0);
                hu_speed_range copied = (oc >= 0) ? sSides[o].cards[oc].speed : side->cards[active].speed;
                hu_side_transform(side, active, (u8)sActivePokemon[s]->state.transformedIntoSpecies, copied);
                if (oc >= 0 && Port_Enh_SummaryBarsEnabled()) {
                    sIconKey[s][active] = sIconKey[o][oc]; // the picture of the one it copied
                }
            } else if (!transformed && sTransformed[s]) {
                hu_side_transform(side, active, side->cards[active].species, side->cards[active].speed);
                sIconKey[s][active] = LoadIcon(sCardMon[s][active]);
            }
            sTransformed[s] = transformed;
        }
        hu_side_tick(side);
    }
}

// Stadium's priority brackets (Battle_DetermineMoveOrder): Quick Attack first, Counter last, everything else by Speed.
static s32 MovePriority(u8 move) {
    if (move == QUICK_ATTACK) return 1;
    if (move == COUNTER) return -1;
    return 0;
}

void PortEnh_TurnOrder(int first_side) {
    s32 s;
    if (!sReady) return;
    for (s = 0; s < 2; s++) {
        const ActivePokemonState* st = &sActivePokemon[s]->state;
        s32 slot = st->selectedMoveIndex;
        sPendingSlot[s] = -1;
        if (IsSwitch(st->runtime.usedMove)) continue; // this side switches (or sends one out)
        if (slot >= 0 && slot < 4 && st->runtime.moves[slot] == st->runtime.usedMove &&
            !(st->runtime.uniqueStatusFlags & 8)) {
            sPendingSlot[s] = slot;
        }
    }
    EnhLog("turn: side %d first (moves %u / %u, speed %u / %u)", (int)first_side,
           sActivePokemon[0]->state.runtime.usedMove, sActivePokemon[1]->state.runtime.usedMove,
           sActivePokemon[0]->state.runtime.modifiedSpeed, sActivePokemon[1]->state.runtime.modifiedSpeed);
    // the speed estimate: only against the COM, when Speed decided the order (not on a switch turn, first_side -1)
    if (sViewer >= 0 && first_side >= 0) {
        s32 h = !sViewer;
        const BattleMonRuntime* mine = &sActivePokemon[sViewer]->state.runtime;
        const BattleMonRuntime* theirs = &sActivePokemon[h]->state.runtime;
        const BattleMon* their_mon = ActiveMon(h);
        s32 c = CardOf(h, 0);
        if (c >= 0 && their_mon != NULL && !IsSwitch(mine->usedMove) && !IsSwitch(theirs->usedMove) &&
            MovePriority(mine->usedMove) == MovePriority(theirs->usedMove) && !(mine->uniqueStatusFlags & 8) &&
            !(theirs->uniqueStatusFlags & 8)) {
            hu_card* card = &sSides[h].cards[c];
            if (hu_speed_observe(&card->speed, first_side == h ? HU_MOVED_FIRST : HU_MOVED_SECOND, mine->modifiedSpeed,
                                 &hu_gb_speed_rules, ModsOf(theirs, (their_mon->statusCondition & 0x40) != 0))) {
                EnhLog("speed estimate: side %d card %d now %u-%u", (int)h, (int)c, card->speed.min, card->speed.max);
                if (sHasKey) {
                    u16 lo, hi;
                    OpponentStatExp(&lo, &hi);
                    hu_tm_record_speed_se(Port_Enh_Memory(), sKey, card->species, BaseSpeed(card->species),
                                          card->speed, lo, hi);
                }
            }
        }
    }
    if (sHasKey) {
        Port_Enh_SaveMemory(); // last turn's moves and speed (a battle can end by surrender or a reset)
    }
}

void PortEnh_MoveNotUsed(void) {
    if (!sReady) return;
    // the turn's action is resolved before it is shown: the move step that follows must not reveal the move
    sPendingSlot[sBattleScene.scene->actingSide & 1] = -1;
}

void PortEnh_MoveStart(void) {
    s32 s, c, slot;
    if (!sReady) return;
    s = sBattleScene.scene->actingSide & 1;
    slot = sPendingSlot[s];
    c = CardOf(s, 0);
    if (slot < 0 || c < 0) return;
    hu_side_move_used(&sSides[s], c, slot);
    EnhLog("move: side %d card %d slot %d move %u", (int)s, (int)c, (int)slot, sSides[s].cards[c].moves[slot]);
    if (sHasKey && s != sViewer) {
        hu_tm_record_move(Port_Enh_Memory(), sKey, sSides[s].cards[c].species, sSides[s].cards[c].moves[slot]);
    }
    sPendingSlot[s] = -1;
}

// ---- the view the summary bars draw (summary_bars.cpp, on the render thread) ----

// Stadium's (Gen 1) type ids -> the library's
static u8 HuType(u8 t) {
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
        default: return HU_TYPE_UNKNOWN; // (0x06, the unused "bird" type, included)
    }
}

static void Publish(void) {
    static PortEnhBattleView v;
    char** move_names = Text_GetStringTable(0x25);
    s32 s, i, k;

    memset(&v, 0, sizeof(v));
    v.viewer = sViewer;
    for (s = 0; s < 2; s++) {
        const BattleMonRuntime* rt = &sActivePokemon[s]->state.runtime;
        const BattleMon* active_mon = ActiveMon(s);
        v.sides[s] = sSides[s];
        for (i = 0; i < sSides[s].count; i++) {
            const hu_card* c = &sSides[s].cards[i];
            hu_speed_range sr = hu_card_shown_speed(c);
            // its nickname (an unnamed Pokémon's nickname is its species name); it stays while transformed
            if (sCardMon[s][i] != NULL) {
                Port_TextToUtf8((const char*)sCardMon[s][i]->nickname, v.names[s][i], PORT_ENH_TEXT);
            }
            for (k = 0; k < 4; k++) {
                u16 move = c->moves[k];
                if (move >= 1 && move <= 165) {
                    if (move_names != NULL) {
                        Port_TextToUtf8(Text_GetString(NULL, 0, move_names, move - 1), v.moves[s][i][k], PORT_ENH_TEXT);
                    }
                    v.moveTypes[s][i][k] = HuType(gMoveData[move - 1].type);
                } else {
                    v.moveTypes[s][i][k] = HU_TYPE_UNKNOWN;
                }
            }
            // speed as the turn order compares it now: stage and paralysis applied to the active one
            if ((c->flags & HU_CARD_F_ACTIVE) && active_mon != NULL) {
                if (s == sViewer) {
                    sr = hu_speed_exact(rt->modifiedSpeed);
                } else if (!(rt->uniqueStatusFlags & 8)) {
                    sr = hu_speed_shown(sr, &hu_gb_speed_rules, ModsOf(rt, (active_mon->statusCondition & 0x40) != 0));
                }
            }
            v.speed[s][i] = sr;
            v.iconKey[s][i] = sIconKey[s][i];
        }
    }
    Port_Enh_PublishBattle(&v);
}

void PortEnh_BattleFrame(void) {
    PortEnh_BattleMenusFrame(); // move descriptions, order queueing (they check their own settings)
    // (the cards are kept with the summary bars off too: trainer memory learns from them)
    if (PortEnh_BattleIsDemo() || sActivePokemon[0] == NULL || sActivePokemon[1] == NULL ||
        sBattleScene.scene == NULL) {
        sReady = 0;
        return;
    }
    if (sNeedInit || !sReady) {
        Init();
    }
    Poll();
    // the battle is decided: no cards over the winner's parade and what follows (Publish stops, the bars go)
    if (sBattleScene.scene->winner == 0) {
        if (Port_Enh_SummaryBarsEnabled()) {
            Publish();
        }
    } else if (sHasKey && !sSaved) {
        Port_Enh_SaveMemory();
        sSaved = 1;
    }
}

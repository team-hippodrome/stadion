// The battle's enhancements (hippodrome-utils docs/specs): what runs when a battle starts and on every frame it draws
// its HUD (StadionPort.h "Enhancements": BattleScene_Init and Battle_RenderHud call these). Compiled with the game
// code, kept in the port, so game/ holds only decomp code. Each enhancement checks its own setting.
//
// Side summary bars and speed estimates (PARTY_SUMMARY_BARS.md, SPEED_STAT_ESTIMATE.md): a card per Pokémon of each
// side, kept here from the battle's own state, and published each frame for summary_bars.cpp, which draws the cards
// in the window's room beside the game's picture. Each card shows its own animated copy of the Pokémon's 3D model
// (card_models.c). Besides the two hooks above, the battle tells this file who acts
// first (PortEnh_TurnOrder, Battle_StartTurn), that the acting Pokémon attempts a move (PortEnh_MoveAttempted,
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
#include "src/gfx_buffer.h"
#include "src/jpeg_stream.h"
#include "src/memory.h"
#include "src/matrix.h"
#include "src/text_system.h"
#include "fragments/battle_engine/battle_engine.h"

#include "StadionPort.h"
#include "rom_version.h"
#include "enhancements.h"
#include "battle_enhancements.h"
#include "card_models.h"

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
static s32 sPendingSlot[2]; // the move slot each side attempted this turn, revealed when the move is presented
static uint32_t sIconKey[2][HU_SIDE_MAX];
static s32 sHasKey; // the opponent is a trainer of the memory (sKey)
static hu_tm_key sKey;
static s32 sSaved;

void PortEnh_BattleBegin(void) {
    PortEnh_BattleMenusBegin();
    PortEnh_HealBellReset();
    PortEnh_MewtwoBattleBegin();
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

static s32 sOrderSelect; // the cards are the battle order screen's (PortEnh_OrderSelectFrame), not a battle's

static hu_speed_range InitialOpponentSpeed(const BattleMon* mon, const TeamRoster* roster) {
    hu_speed_range range = hu_speed_initial(BaseSpeed(mon->species.id), mon->level);
    u16 stat_exp_min, stat_exp_max;
    if (OpponentStatExp(&stat_exp_min, &stat_exp_max) && IsCom(roster)) {
        hu_speed_range clamped = hu_speed_intersect(
            range, hu_speed_initial_se(BaseSpeed(mon->species.id), mon->level, stat_exp_min, stat_exp_max));
        range = hu_speed_validate(clamped, mon->speed, range, NULL);
    }
    return range;
}

// A side's cards from its trainers (`t`; `fallback` its roster without them). Both sides start Unknown on the
// order screen. The COM's scripted panel does not determine which Pokémon it can bring.
static void InitSideFrom(s32 s, BattleSessionTeams* t, TeamRoster* fallback, s32 order_required) {
    s32 members = (t != NULL && t->tagTeamSize == 2) ? 2 : 1;
    TeamRoster* r = (t != NULL && t->tagTeamMember[0] != NULL) ? t->tagTeamMember[0] : fallback;
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
        brought = sOrderSelect ? (order_required > 0 ? order_required : 6) : r->numberOfPokemonBrought;
        count = (info != NULL) ? info->numberOfPokemonCanBePicked : brought;
        if (count <= 0 || count > 6) count = brought;
        if (brought <= 0 || brought > count) brought = count;
        if (sOrderSelect) {
            hu_side_init_order_select(&sSides[s], count, brought, s == sViewer ? HU_REVEAL_ALL : HU_REVEAL_AS_USED);
        } else {
            hu_side_init(&sSides[s], count, brought, s == sViewer ? HU_REVEAL_ALL : HU_REVEAL_AS_USED);
        }
        for (i = 0; i < count; i++) {
            sCardMon[s][i] = &party[i];
        }
        for (i = 0; i < brought && !sOrderSelect; i++) {
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
            hu_card* c = &sSides[s].cards[i];
            c->speed = InitialOpponentSpeed(mon, r);
            c->transform_speed = c->speed;
        }
        sIconKey[s][i] = 0;
        if (Port_Enh_SummaryBarsEnabled()) {
            PortEnh_CardModelsLoad(s, i, sCardMon[s][i]); // the card's own copy of the 3D model
            if (!PortEnh_CardModelSpecies(s, i)) {
                sIconKey[s][i] = LoadIcon(sCardMon[s][i]); // (only if the model could not be loaded)
            }
        }
    }
    if (!sOrderSelect && (s == sViewer || members == 2)) {
        hu_side_set_picked(&sSides[s], picked_mask);
    }
}

static void InitSide(s32 s) {
    InitSideFrom(s, sActivePokemon[s]->sessionTeams, sActivePokemon[s]->teamRoster, 0);
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
            const BattleMon* mon = sCardMon[hidden][i];
            hu_speed_range merged = hu_speed_intersect(c->speed, known);
            if (mon != NULL) {
                bool was_reset = false;
                c->speed = hu_speed_validate(merged, mon->speed, c->speed, &was_reset);
                if (was_reset) {
                    EnhLog("speed estimate failsafe: side %d card %d memory %u-%u excludes true speed %u; resetting to %u-%u",
                           (int)hidden, (int)i, known.min, known.max, mon->speed, c->speed.min, c->speed.max);
                    hu_tm_clear_speed(Port_Enh_Memory(), sKey, c->species);
                }
            } else {
                c->speed = merged;
            }
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
    sOrderSelect = 0;
    PortEnh_CardModelsFree();
    InitSide(0);
    InitSide(1);
    sPendingSlot[0] = sPendingSlot[1] = -1;
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
static hu_speed_mods ModsOf(const BattleMonRuntime* rt) {
    hu_speed_mods m = hu_speed_mods_none();
    u16 v;
    m.stage = (s8)(rt->statChanges[2] - 7);
    if (rt->pendingStatusCondition & 0x40) {
        v = hu_apply_stage(&hu_gb_speed_rules, rt->unmodifiedSpeed, m.stage);
        while (v > rt->modifiedSpeed && m.paralysis_cuts < 8) {
            v = v >> 2;
            if (v == 0) v = 1;
            m.paralysis_cuts++;
        }
    }
    return m;
}

static s32 IsSwitch(u8 used_move) {
    return used_move == 0 || (used_move & 0xF0) == 0xF0;
}

static void RestoreCardModel(s32 s, s32 c) {
    if (Port_Enh_SummaryBarsEnabled()) {
        PortEnh_CardModelsRestore(s, c);
        sIconKey[s][c] = PortEnh_CardModelSpecies(s, c) ? 0 : LoadIcon(sCardMon[s][c]);
    }
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
            s32 c;
            for (c = 0; c < side->count; c++) {
                if ((side->cards[c].flags & (HU_CARD_F_ACTIVE | HU_CARD_F_TRANSFORMED)) ==
                    (HU_CARD_F_ACTIVE | HU_CARD_F_TRANSFORMED)) {
                    RestoreCardModel(s, c);
                    break;
                }
            }
            hu_side_send_out(side, shown);
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
            hu_card* c = &side->cards[active];
            s32 transformed = (rt->uniqueStatusFlags & 8) != 0;
            s32 card_transformed = (c->flags & HU_CARD_F_TRANSFORMED) != 0;
            hu_side_set_substitute(side, active, rt->substituteHp != 0);
            if (transformed && !card_transformed) {
                // the copied Speed: the viewer knows its own exactly; the opponent's is its estimate
                s32 o = !s;
                s32 oc = CardOf(o, 0);
                hu_speed_range copied = (oc >= 0) ? hu_card_shown_speed(&sSides[o].cards[oc]) : c->speed;
                hu_side_transform(side, active, (u8)sActivePokemon[s]->state.transformedIntoSpecies, copied);
                if (oc >= 0 && Port_Enh_SummaryBarsEnabled()) {
                    PortEnh_CardModelsLoadFrom(s, active, o, oc); // the model of the one it copied
                    sIconKey[s][active] = sIconKey[o][oc];
                }
            } else if (!transformed && card_transformed) {
                // A reset without a send-out should not clear the card's Active or Substitute state.
                c->flags &= (u8)~HU_CARD_F_TRANSFORMED;
                c->model_species = c->species;
                c->transform_speed = c->speed;
                RestoreCardModel(s, active);
            }
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
    if (!sReady) return;
    sPendingSlot[0] = sPendingSlot[1] = -1;
    EnhLog("turn: side %d first (moves %u / %u, speed %u / %u)", (int)first_side,
           sActivePokemon[0]->state.runtime.usedMove, sActivePokemon[1]->state.runtime.usedMove,
           sActivePokemon[0]->state.runtime.modifiedSpeed, sActivePokemon[1]->state.runtime.modifiedSpeed);
    // the speed estimate: only against the COM, when Speed decided the order (not on a switch turn, first_side -1)
    if (sViewer >= 0 && first_side >= 0) {
        s32 h = !sViewer;
        const BattleMonRuntime* mine = &sActivePokemon[sViewer]->state.runtime;
        const BattleMonRuntime* theirs = &sActivePokemon[h]->state.runtime;
        s32 c = CardOf(h, 0);
        if (c >= 0 && !IsSwitch(mine->usedMove) && !IsSwitch(theirs->usedMove) &&
            MovePriority(mine->usedMove) == MovePriority(theirs->usedMove) && !(mine->uniqueStatusFlags & 8) &&
            !(theirs->uniqueStatusFlags & 8)) {
            hu_card* card = &sSides[h].cards[c];
            const BattleMon* mon = sCardMon[h][c];
            const TeamRoster* roster = sRosters[h][MemberOf(h, sActivePokemon[h]->state.teamMemberIndex)];
            hu_speed_range fallback = (mon != NULL)
                ? InitialOpponentSpeed(mon, roster)
                : hu_speed_initial(card->species ? BaseSpeed(card->species) : 0, card->level);
            bool was_reset = false;
            if (hu_speed_observe_checked(&card->speed, first_side == h ? HU_MOVED_FIRST : HU_MOVED_SECOND,
                                         mine->modifiedSpeed, &hu_gb_speed_rules, ModsOf(theirs),
                                         theirs->unmodifiedSpeed, fallback, &was_reset)) {
                EnhLog("speed estimate: side %d card %d now %u-%u", (int)h, (int)c, card->speed.min, card->speed.max);
                if (sHasKey) {
                    u16 lo, hi;
                    OpponentStatExp(&lo, &hi);
                    hu_tm_record_speed_se(Port_Enh_Memory(), sKey, card->species, BaseSpeed(card->species),
                                          card->speed, lo, hi);
                }
            } else if (was_reset) {
                EnhLog("speed estimate failsafe: side %d card %d reset to %u-%u (true speed %u)", (int)h, (int)c,
                       card->speed.min, card->speed.max, theirs->unmodifiedSpeed);
                card->transform_speed = card->speed;
                if (sHasKey) {
                    hu_tm_clear_speed(Port_Enh_Memory(), sKey, card->species);
                }
            }
        }
    }
    if (sHasKey) {
        Port_Enh_SaveMemory(); // last turn's moves and speed (a battle can end by surrender or a reset)
    }
}

void PortEnh_MoveAttempted(void) {
    const ActivePokemonState* st;
    s32 s, c, slot;
    if (!sReady) return;
    s = sBattleScene.scene->actingSide & 1;
    c = CardOf(s, 0);
    st = &sActivePokemon[s]->state;
    slot = st->selectedMoveIndex;
    // Reveal only a move in this card's regular moveset, not Struggle, a switch, or a copied move.
    if (c >= 0 && slot >= 0 && slot < HU_CARD_MOVES && !IsSwitch(st->runtime.usedMove) &&
        sSides[s].cards[c].moves[slot] == st->runtime.usedMove && !(st->runtime.uniqueStatusFlags & 8)) {
        sPendingSlot[s] = slot;
    }
}

void PortEnh_MoveStart(void) {
    s32 s, c, slot;
    PortEnh_MewtwoRevealPending(); // Harder vs Mewtwo: its card lists the moves as they are revealed
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
    v.orderSelect = (u8)sOrderSelect;
    for (s = 0; s < 2; s++) {
        const BattleMonRuntime* rt = sOrderSelect ? NULL : &sActivePokemon[s]->state.runtime;
        const s32 active = sOrderSelect ? -1 : CardOf(s, 0);
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
            if (i == active && (c->flags & HU_CARD_F_ACTIVE)) {
                if (s == sViewer && !(c->flags & HU_CARD_F_TRANSFORMED) && !(rt->uniqueStatusFlags & 8)) {
                    sr = hu_speed_exact(rt->modifiedSpeed);
                } else {
                    sr = hu_speed_shown(sr, &hu_gb_speed_rules, ModsOf(rt));
                }
            }
            v.speed[s][i] = sr;
            v.iconKey[s][i] = sIconKey[s][i];
            v.modelReady[s][i] = (u8)PortEnh_CardModelReady(s, i);
            v.modelSpecies[s][i] = (u8)PortEnh_CardModelSpecies(s, i);
        }
        // Harder vs Mewtwo: its card lists its revealed moves (it knows more than four)
        if (PortEnh_MewtwoIsCard(s) && sSides[s].count > 0) {
            u16 big[PORT_ENH_BIG_MOVES];
            const s32 n = PortEnh_MewtwoCardMoves(big, PORT_ENH_BIG_MOVES);
            v.bigSide = (s8)s;
            v.bigCard = 0;
            v.bigShown = (u8)n;
            v.bigRows = (u8)(n > HU_CARD_MOVES ? n : HU_CARD_MOVES);
            for (k = 0; k < n; k++) {
                const u16 move = big[k];
                if (move >= 1 && move <= PORT_MOVE_HEAL_BELL && move_names != NULL) {
                    Port_TextToUtf8(Text_GetString(NULL, 0, move_names, move - 1), v.bigMoves[k], PORT_ENH_TEXT);
                    v.bigTypes[k] = HuType(gMoveData[move - 1].type);
                } else {
                    v.bigTypes[k] = HU_TYPE_UNKNOWN;
                }
            }
        }
    }
    Port_Enh_PublishBattle(&v);
}

// The cards' 3D models, drawn into their pictures (card_models.c) with what the battle shows of the Pokémon in it:
// its battle model, and its idle, fainting and sleeping animations from the species' battle cues.
static void DrawModels(void) {
    PortEnhCardBattle battle[2];
    s32 s;
    for (s = 0; s < 2; s++) {
        ActivePokemon* a = sActivePokemon[s];
        const PokemonBattleCues* cues = sSideBattleCues[BattleScene_GetParticipantSideIndex(a) & 1];
        u16 type = (u16)a->displayObject.objectType;
        s32 k, alt = 0;
        for (k = 0; k < 8; k++) alt |= (sPokemonWithAlternateSleepIdle[k] == type);
        battle[s].active = CardOf(s, 1);
        battle[s].obj = &a->displayObject;
        battle[s].faintAnim = (cues != NULL) ? cues->faintCue.id : -1;
        battle[s].sleepAnim = (cues != NULL && alt) ? cues->sleepIdleCue.id : -1;
    }
    PortEnh_CardModelsDraw(sSides, battle);
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
            DrawModels();
            Publish();
        }
    } else if (sHasKey && !sSaved) {
        Port_Enh_SaveMemory();
        sSaved = 1;
    }
}

// ---- the battle order screen (BattleMenu_UpdateOrderSelect) ----
// The summary bars show both parties there too, as the battle will start them: the player's own Pokémon in full, the
// opponent's with what the trainer memory knows. A tag battle's sides are two trainers picking from two parties: no
// cards there (a side has room for six). The battle starts its own cards (PortEnh_BattleBegin).

void PortEnh_OrderSelectFrame(const uint8_t required[2], const uint8_t picked[2]) {
    BattleSessionTeams* t = gSessionContext.battleSides;
    PortEnhCardBattle none[2];
    BinArchive* archive;
    s32 s;

    if (PortEnh_BattleIsDemo() || !Port_Enh_SummaryBarsEnabled() || t[0].tagTeamSize != 1 || t[1].tagTeamSize != 1 ||
        t[0].tagTeamMember[0] == NULL || t[1].tagTeamMember[0] == NULL ||
        t[0].tagTeamMember[0]->rosterInfo == NULL || t[1].tagTeamMember[0]->rosterInfo == NULL) {
        return;
    }
    if (!sOrderSelect) {
        s32 com0 = IsCom(t[0].tagTeamMember[0]), com1 = IsCom(t[1].tagTeamMember[0]);
        sOrderSelect = 1;
        sReady = 0;
        sNeedInit = 1; // (the battle starts its own)
        sViewer = (!com0 && com1) ? 0 : (com0 && !com1) ? 1 : -1;
        PortEnh_CardModelsFree();
        // the screen has no model archive open (the battle opens its own): a table of our own for the loads, from the
        // screen's pool and freed right after (the copies are host buffers of their own)
#if defined(VERSION_US_REV2)
        archive = BinArchive_Open((u8*)0x919000, NULL, 1, 1);
#else
        archive = BinArchive_Open((u8*)0x920000, NULL, 1, 1);
#endif
        PortEnh_CardModelsSetArchive(archive);
        InitSideFrom(0, &t[0], t[0].tagTeamMember[0], required[0]);
        InitSideFrom(1, &t[1], t[1].tagTeamMember[0], required[1]);
        PortEnh_CardModelsSetArchive(NULL);
        if (archive != NULL) {
            main_pool_try_free(archive);
        }
        InitMemory();
        EnhLog("order select: viewer %d, sides %d/%d", (int)sViewer, sSides[0].count, sSides[1].count);
    }
    for (s = 0; s < 2; s++) {
        if (required[s] > 0) {
            hu_side* side = &sSides[s];
            s32 i;
            // Apply take-backs first; the game clears its picks from several different panel states.
            for (i = 0; i < side->count; i++) {
                if (!(picked[s] & (1u << i))) hu_side_unpick(side, i);
            }
            for (i = 0; i < side->count; i++) {
                if (picked[s] & (1u << i)) hu_side_pick(side, i);
            }
        }
        none[s].active = -1;
        none[s].obj = NULL;
        none[s].faintAnim = none[s].sleepAnim = -1;
    }
    PortEnh_CardModelsDraw(sSides, none); // (in the room PortEnh_OrderSelectDisplayListRoom adds; it checks per model)
    Publish();
}

// The screen's display list buffer is sized for the screen alone; twelve card models with active model_callback_library
// effects (flames, smoke, scrolling textures) can take ~200-300 KB of host display list bytes per frame, so both order
// select and battle get extra room while the bars are on.
u32 PortEnh_OrderSelectDisplayListRoom(void) {
    return Port_Enh_SummaryBarsEnabled() ? 0x30000 : 0; // (Gfx_InitDisplayListBuffers' size: the buffers are twice it)
}

u32 PortEnh_BattleDisplayListRoom(void) {
    return Port_Enh_SummaryBarsEnabled() ? 0x30000 : 0;
}

void PortEnh_OrderSelectEnd(void) {
    if (sOrderSelect) {
        PortEnh_CardModelsFree();
        sOrderSelect = 0;
    }
}

// The battle order screen's background in widescreen (StadionPort.h). The screen tiles a 640 x 480 image as 20 x 15
// squares of 32 x 32 (Gfx_DrawTiledRgba16Image: a 16 x 16 tile each, drawn at twice its size); the sides of the picture
// get the image's columns continued as if it repeated: on the left its last columns, on the right its first ones. They
// are drawn anchored to the picture's sides (gDPPortAnchorLeft/Right), so a 2D screen's 4:3 lock does not cut them.
void PortEnh_OrderSelectBackdrop(const void* image) {
    const s32 margin = Port_WideMarginN64() * 2; // (the screen is 640 wide: two of the N64's 320-wide pixels)
    const s32 cols = (margin + 31) / 32;
    const u8* tiles = (const u8*)image;
    s32 side, row, k;

    if (margin <= 0 || tiles == NULL) {
        return;
    }
    gDPPipeSync(gDisplayListHead++);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetTexturePersp(gDisplayListHead++, G_TP_NONE);
    gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, 640, 480);
    gDPSetCombineMode(gDisplayListHead++, G_CC_DECALRGB, G_CC_DECALRGB);
    gDPPipeSync(gDisplayListHead++);
    for (side = 0; side < 2; side++) {
        if (side == 0) {
            gDPPortAnchorLeft(gDisplayListHead++); // (drawn `margin` to the right of where it shows)
        } else {
            gDPPortAnchorRight(gDisplayListHead++);
        }
        for (row = 0; row < 15; row++) {
            for (k = 0; k < cols; k++) {
                const s32 col = (side == 0) ? 20 - cols + k : k;
                s32 x0 = (side == 0) ? 32 * col - 640 + margin : 640 + 32 * col - margin;
                const s32 x1 = x0 + 32;
                s32 s = 0;
                if (x0 < 0) { // (a part of the square: its texture starts further in, at half a texel per pixel)
                    s = -x0 * 16;
                    x0 = 0;
                }
                if (x1 <= x0) continue;
                gDPLoadTextureBlock(gDisplayListHead++, tiles + (row * 20 + col) * 0x200, G_IM_FMT_RGBA, G_IM_SIZ_16b,
                                    16, 16, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                                    G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                gSPTextureRectangle(gDisplayListHead++, x0 << 2, (row * 32) << 2, x1 << 2, (row * 32 + 32) << 2,
                                    G_TX_RENDERTILE, s, 0, 0x0200, 0x0200);
            }
        }
        gDPPortAnchorEnd(gDisplayListHead++);
    }
    gDPPipeSync(gDisplayListHead++);
}

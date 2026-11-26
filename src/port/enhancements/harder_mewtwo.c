// Harder vs Mewtwo (hippodrome-utils docs/specs/game_specific/1/HARDER_VS_MEWTWO.md and MEWTWO_AI_RULES.md). Compiled
// with the game code. Each hook does nothing while the option (gEnhancements.HarderMewtwo) is off or the battle is not
// the Vs. Mewtwo battle against the COM's Mewtwo.
//
// What changes:
// - Mewtwo cannot be frozen (BattleEffect_SecondaryStatusChance asks PortEnh_MewtwoFreezeImmune).
// - Mewtwo knows 12 moves (kMoves). The engine holds four (BattleMonRuntime.moves / pp), so they are a window onto
//   the twelve: the move Mewtwo picks is put into a slot of the window as it is picked (never the slot Disable holds),
//   the slot's PP goes back to the twelve when the move leaves the window. Earthquake (Gen 3) and Heal Bell (Gen 2)
//   are not marked illegal in this battle (PortEnh_MewtwoLegality, after Battle_InitializeMoveIllegalityMask).
// - Heal Bell is move 166 (PORT_MOVE_HEAL_BELL, gMoveData's extra entry, effect PORT_EFFECT_HEAL_BELL): heal_bell.c.
// - Mewtwo picks its moves with the rule tables of MEWTWO_AI_RULES.md (PortEnh_MewtwoChooseAction replaces the stock
//   AI's choice, BattleScene_TriggerCpuChoice), at the tier the launcher sets (Easy 10% ... Lunatic 80%, Kaizo).
// - Round 2: Mewtwo acts twice per turn, both actions together (before or after the player's, never around it). The
//   second action is decided on the state the first left (PortEnh_MewtwoActAgain, from Battle_ActSecondAttacker and
//   where the turn would end); a faint cancels it. When Mewtwo skips it, "MEWTWO is watching <foe> carefully!" shows
//   (PortEnh_MewtwoPassAction, ProcessBattleTurn). Residual damage (poison, burn, Leech Seed) is taken once per turn,
//   after Mewtwo's last action (PortEnh_MewtwoSkipResidual, Battle_ResolveMonAction).
// - The summary bar card lists four rows, and a row more for each move past the fourth that Mewtwo reveals
//   (PortEnh_MewtwoCardMoves, battle_enhancements.c).
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "game_state.h"
#include "variables.h"
#include "moves.h"
#include "pokemon.h"
#include "status.h"
#include "src/gb_data.h"
#include "src/text_system.h"
#include "fragments/battle_engine/battle_engine.h"

#include "StadionPort.h"
#include "enhancements.h"

// ---- the moves ----

static const u8 kMoves[MEWTWO_MOVES] = {
    PSYCHIC_M, AMNESIA,    RECOVER, BLIZZARD,     MIST,       THUNDERBOLT,
    SUBMISSION, SUBSTITUTE, REFLECT, THUNDER_WAVE, EARTHQUAKE, PORT_MOVE_HEAL_BELL,
};
enum {
    M_PSYCHIC, M_AMNESIA, M_RECOVER, M_BLIZZARD, M_MIST, M_THUNDERBOLT,
    M_SUBMISSION, M_SUBSTITUTE, M_REFLECT, M_THUNDER_WAVE, M_EARTHQUAKE, M_HEAL_BELL,
};
// R2 / R9 attacks, in the spec's tie-break order
static const u8 kAttacks[5] = { M_PSYCHIC, M_THUNDERBOLT, M_EARTHQUAKE, M_BLIZZARD, M_SUBMISSION };

// ---- the tiers (MEWTWO_AI_RULES.md "Tier tables"); chances out of 256 ----

typedef struct Rules {
    u16 cure_paralysis, cure_poison, cure_other;
    u16 knockout;
    u16 heal_below, heal;
    u16 substitute, substitute_min_hp;
    u16 paralyze;
    u16 reflect, reflect_threat;
    u16 mist;
    u16 amnesia, amnesia_max_stage, amnesia_safe;
    u16 blunder, pass_second, second_attack;
    u16 damage_roll;
} Rules;

static const Rules kTiers[PORT_MEWTWO_TIERS] = {
    { 0 }, // off
    // 10%
    { 76, 256, 256, 136, 128, 136, 76, 96, 256, 256, 64, 256, 256, 4, 64, 150, 224, 0, 236 },
    // 30%
    { 96, 256, 256, 150, 128, 150, 96, 96, 256, 256, 64, 256, 256, 4, 64, 133, 170, 0, 236 },
    // 50%
    { 116, 256, 256, 163, 128, 163, 116, 96, 256, 256, 64, 256, 256, 4, 64, 117, 118, 0, 236 },
    // 80%
    { 144, 256, 256, 181, 128, 181, 144, 96, 256, 256, 64, 256, 256, 4, 64, 93, 43, 0, 236 },
    // Kaizo: the full-strength table (no blunders, no skipped actions, the second action may attack)
    { 256, 256, 256, 256, 128, 256, 256, 96, 256, 256, 64, 256, 256, 4, 64, 0, 0, 256, 236 },
};

// ---- the battle's state ----

static struct {
    s32 active;      // this battle is the harder Vs. Mewtwo battle
    s32 tier;
    s32 round2;
    s32 side;        // Mewtwo's side
    s32 ready;       // the twelve are set from Mewtwo's runtime
    u8 pp[MEWTWO_MOVES];
    s8 window[4];    // the window slot -> index into kMoves (-1: the slot holds a move not in the twelve)
    u8 revealed[MEWTWO_MOVES]; // the order the moves were revealed in: indices into kMoves
    s32 revealCount;
    s32 pendingReveal; // the index the current action uses, revealed when its presentation starts (-1 none)
    // the turn
    s32 actions;     // Mewtwo's actions resolved this turn
    s32 passing;     // the action being resolved is a skipped second action
    s32 skippedResidual;
    s32 healBellUsed; // (heal_bell.c)
} sM;

static s32 Log(void) {
    return Port_Enh_Log();
}

int PortEnh_MewtwoActive(void) {
    return sM.active && sM.ready;
}

static ActivePokemon* Mewtwo(void) {
    return sActivePokemon[sM.side];
}

static ActivePokemon* Foe(void) {
    return sActivePokemon[!sM.side];
}

static s32 IsMewtwo(ActivePokemon* a) {
    return PortEnh_MewtwoActive() && a != NULL && a == Mewtwo();
}

static s32 IndexOf(u8 move) {
    s32 i;
    for (i = 0; i < MEWTWO_MOVES; i++) {
        if (kMoves[i] == move) return i;
    }
    return -1;
}

static u8 MaxPp(u8 move) {
    u8 base = gMoveData[move - 1].pp;
    return (u8)(base + (base / 5) * 3); // three PP Ups, as a fully trained Mewtwo
}

void PortEnh_MewtwoBattleBegin(void) {
    TeamRoster* com;

    memset(&sM, 0, sizeof(sM));
    sM.pendingReveal = -1;
    sM.tier = Port_Enh_HarderMewtwo();
    if (sM.tier == PORT_MEWTWO_OFF || gSessionContext.mode != SESSION_MODE_VS_MEWTWO) {
        return;
    }
    // the COM's side, whose (only) Pokémon is Mewtwo
    for (sM.side = 1; sM.side >= 0; sM.side--) {
        com = gSessionContext.battleSides[sM.side].tagTeamMember[0];
        if (com != NULL && (com->isHuman & 2) && com->numberOfPokemonBrought > 0 &&
            com->pokemonPicked[0].species.id == MEWTWO) {
            break;
        }
    }
    if (sM.side < 0) {
        return;
    }
    sM.active = 1;
    // Heal Bell's name where the battle reads it from the move table's extra entry (a disabled move's message)
    gMoveDisplayInfo[PORT_MOVE_HEAL_BELL - 1].unk_04 = (char*)Port_TextPortString("PORT_MOVE_HEAL_BELL");
    sM.round2 = gSessionContext.inRound2 != 0 || Port_BattleBootRound2();
    if (Log()) {
        fprintf(stderr, "[mewtwo] harder Mewtwo: tier %d, round %d, side %d\n", (int)sM.tier, sM.round2 ? 2 : 1,
                (int)sM.side);
    }
}

// The twelve, from Mewtwo's runtime once the battle has set it up: its own four keep their PP (and slots).
static void SetUp(void) {
    BattleMonRuntime* rt;
    s32 i, k;

    if (!sM.active || sM.ready || sActivePokemon[0] == NULL || sActivePokemon[1] == NULL) {
        return;
    }
    rt = &Mewtwo()->state.runtime;
    if (rt->id != MEWTWO) {
        return;
    }
    for (i = 0; i < MEWTWO_MOVES; i++) {
        sM.pp[i] = MaxPp(kMoves[i]);
    }
    for (k = 0; k < 4; k++) {
        i = IndexOf(rt->moves[k]);
        sM.window[k] = (s8)i;
        if (i >= 0) {
            sM.pp[i] = rt->pp[k] & 0x3F;
        }
    }
    sM.ready = 1;
    if (Log()) {
        fprintf(stderr, "[mewtwo] window %d %d %d %d\n", rt->moves[0], rt->moves[1], rt->moves[2], rt->moves[3]);
    }
}

// The window's PP back into the twelve.
static void SyncPp(void) {
    BattleMonRuntime* rt = &Mewtwo()->state.runtime;
    s32 k;
    for (k = 0; k < 4; k++) {
        if (sM.window[k] >= 0 && rt->moves[k] == kMoves[(s32)sM.window[k]]) {
            sM.pp[(s32)sM.window[k]] = rt->pp[k] & 0x3F;
        }
    }
}

static s32 DisabledSlot(const BattleMonRuntime* rt) {
    return rt->disabledMoveState != 0 ? (rt->disabledMoveState >> 4) & 3 : -1;
}

static s32 Usable(s32 i) {
    const BattleMonRuntime* rt = &Mewtwo()->state.runtime;
    if (sM.pp[i] == 0) return 0;
    if (rt->disabledMoveState != 0 && rt->disabledMove == kMoves[i]) return 0;
    return 1;
}

// Puts move i into the window and returns its slot.
static s32 Place(s32 i) {
    BattleMonRuntime* rt = &Mewtwo()->state.runtime;
    s32 k, slot = -1, disabled = DisabledSlot(rt);

    SyncPp();
    for (k = 0; k < 4; k++) {
        if (sM.window[k] == i && rt->moves[k] == kMoves[i]) return k;
    }
    // the slot to reuse: the window's that was used least recently (never the disabled one)
    {
        static u32 sUse[4];
        static u32 sClock;
        u32 best = 0xFFFFFFFF;
        for (k = 0; k < 4; k++) {
            if (k == disabled) continue;
            if (sUse[k] < best) {
                best = sUse[k];
                slot = k;
            }
        }
        if (slot < 0) slot = 0;
        sUse[slot] = ++sClock;
    }
    rt->moves[slot] = kMoves[i];
    rt->pp[slot] = (rt->pp[slot] & 0xC0) | sM.pp[i];
    sM.window[slot] = (s8)i;
    Mewtwo()->state.moveSlotIllegality[slot] = 0;
    if (Log()) {
        fprintf(stderr, "[mewtwo] window slot %d <- move %d (PP %d)\n", (int)slot, kMoves[i], sM.pp[i]);
    }
    return slot;
}

// ---- freeze, legality ----

int PortEnh_MewtwoFreezeImmune(void* target) {
    SetUp();
    return IsMewtwo((ActivePokemon*)target);
}

void PortEnh_MewtwoLegality(void* active) {
    ActivePokemon* a = (ActivePokemon*)active;
    s32 k;
    SetUp();
    if (!IsMewtwo(a)) return;
    // Earthquake (Gen 3 for Mewtwo) and Heal Bell (Gen 2) are legal for this battle's Mewtwo; the rest it learns
    for (k = 0; k < 4; k++) {
        a->state.moveSlotIllegality[k] = 0;
    }
}

// ---- the AI (MEWTWO_AI_RULES.md) ----

static s32 Roll(u16 chance) {
    if (chance >= 256) return 1;
    if (chance == 0) return 0;
    return Battle_Random() < chance;
}

static s32 HasType(const BattleMonRuntime* m, u8 type) {
    return m->types[0] == type || m->types[1] == type;
}

// max_damage(user, move, roll): the Gen 1 formula without a critical hit (MEWTWO_AI_RULES.md "Helper predicates")
static s32 Damage(const BattleMonRuntime* user, const BattleMonRuntime* target, u8 move, s32 roll) {
    const MoveData* md;
    s32 a, d, level, damage, power;
    const u8* e;

    if (move == 0 || move > PORT_MOVE_HEAL_BELL) return 0;
    md = &gMoveData[move - 1];
    level = user->unk_26;
    if (md->effect == EFFECT_SPECIAL_DAMAGE) {
        switch (move) {
            case SEISMIC_TOSS:
            case NIGHT_SHADE:
                return level;
            case SONICBOOM:
                return 20;
            case DRAGON_RAGE:
                return 40;
            default: // Psywave: 1 to 1.5 x level, its middle
                return (level * 3) / 4;
        }
    }
    if (md->effect == EFFECT_SUPER_FANG) {
        return target->displayHp / 2 > 0 ? target->displayHp / 2 : 1;
    }
    power = md->power;
    if (power == 0 || md->effect == EFFECT_OHKO) return 0;
    if (md->type < 0x14) { // physical
        a = user->modifiedAttack;
        d = target->modifiedDefense;
        if (target->uniqueStatusFlags & UNIQUE_STATUS_REFLECT) d *= 2;
    } else {
        a = user->modifiedSpecial;
        d = target->modifiedSpecial;
        if (target->uniqueStatusFlags & UNIQUE_STATUS_LIGHT_SCREEN) d *= 2;
    }
    if (a > 255 || d > 255) {
        a /= 4;
        d /= 4;
    }
    if (md->effect == EFFECT_EXPLODE) d /= 2;
    if (a < 1) a = 1;
    if (d < 1) d = 1;
    damage = ((((2 * level) / 5 + 2) * power * a) / d) / 50;
    if (damage > 997) damage = 997;
    damage += 2;
    if (HasType(user, md->type)) damage += damage / 2;
    for (e = sTypeEffects; e[0] != 0xFF; e += 3) {
        if (e[0] == md->type && (e[1] == target->types[0] || e[1] == target->types[1])) {
            damage = (damage * e[2]) / 10;
        }
    }
    damage = (damage * roll) / 255;
    if (md->effect == EFFECT_TWO_TO_FIVE_ATTACKS) damage *= 3;
    if (md->effect == EFFECT_ATTACK_TWICE || md->effect == EFFECT_TWINEEDLE) damage *= 2;
    return damage;
}

typedef struct Threats {
    s32 best, bestPhysical;
    s32 status, statDrop, incapacitated;
} Threats;

static void ReadThreats(Threats* t) {
    const BattleMonRuntime* foe = &Foe()->state.runtime;
    const BattleMonRuntime* me = &Mewtwo()->state.runtime;
    s32 k;

    memset(t, 0, sizeof(*t));
    for (k = 0; k < 4; k++) {
        u8 move = foe->moves[k];
        u8 effect;
        s32 damage;
        if (move == 0 || (foe->pp[k] & 0x3F) == 0) continue;
        effect = gMoveData[move - 1].effect;
        if (effect == EFFECT_SLEEP || effect == 0x43 || effect == 0x42 || effect == EFFECT_CONFUSION || effect == 0x54 ||
            effect == 0x56 || effect == EFFECT_OHKO || effect == EFFECT_EXPLODE) {
            t->status = 1;
        }
        if ((effect >= 0x12 && effect <= 0x17) || (effect >= 0x3A && effect <= 0x3F)) {
            t->statDrop = 1;
        }
        damage = Damage(foe, me, move, 255);
        if (damage > t->best) t->best = damage;
        if (gMoveData[move - 1].type < 0x14 && damage > t->bestPhysical) t->bestPhysical = damage;
    }
    t->incapacitated = (foe->pendingStatusCondition & (STATUS_SLEEP_MASK | STATUS_FREEZE)) != 0;
}

// R2's knockout move or R9's best attack (-1: none usable)
static s32 BestAttack(const Rules* r, s32 knockout) {
    const BattleMonRuntime* me = &Mewtwo()->state.runtime;
    const BattleMonRuntime* foe = &Foe()->state.runtime;
    s32 foeSub = (foe->volatileStatusFlags & VOLATILE_STATUS_SUBSTITUTE) != 0;
    s32 best = -1, bestScore = -1, n;

    for (n = 0; n < 5; n++) {
        s32 i = kAttacks[n];
        s32 damage, score;
        u8 acc;
        if (!Usable(i)) continue;
        damage = Damage(me, foe, kMoves[i], r->damage_roll);
        acc = gMoveData[kMoves[i] - 1].accuracy;
        if (knockout) {
            if (foeSub || damage < foe->displayHp) continue;
            score = acc * 1000 + (5 - n);
        } else {
            score = damage * acc / 255;
            if (foeSub && damage >= foe->substituteHp) score = 2000 + acc;
        }
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

static s32 RandomUsable(void) {
    s32 options[MEWTWO_MOVES], count = 0, i;
    for (i = 0; i < MEWTWO_MOVES; i++) {
        if (Usable(i)) options[count++] = i;
    }
    return count ? options[Battle_Random() % count] : -1;
}

// The rule list for one action: an index into kMoves, -2 to skip the action, -1 when nothing is usable (Struggle).
static s32 Choose(s32 action) {
    const Rules* r = &kTiers[sM.tier];
    const BattleMonRuntime* me = &Mewtwo()->state.runtime;
    const BattleMonRuntime* foe = &Foe()->state.runtime;
    s32 supportOnly, knockout, attack;
    Threats t;

    // T2 skip the second action
    if (action == 1 && Roll(r->pass_second)) return -2;
    // T1 blunder
    if (Roll(r->blunder)) return RandomUsable();
    // T3 support-only second action
    supportOnly = action == 1 && !Roll(r->second_attack);
    ReadThreats(&t);

    // R1 cure
    if (Usable(M_HEAL_BELL)) {
        u16 p = 0;
        if (me->pendingStatusCondition & STATUS_PARALYSIS) {
            p = r->cure_paralysis;
        } else if (me->pendingStatusCondition & (STATUS_POISON | STATUS_BURN)) {
            p = r->cure_poison;
        } else if ((me->immobilizedReason & IMMOBILIZED_CONFUSED) ||
                   (me->volatileStatusFlags & VOLATILE_STATUS_LEECH_SEED) || me->disabledMoveState != 0) {
            p = r->cure_other;
        }
        if (p && Roll(p)) return M_HEAL_BELL;
    }
    // R2 knock out
    knockout = BestAttack(r, 1);
    if (!supportOnly && knockout >= 0 && Roll(r->knockout)) return knockout;
    // R3 heal
    if (Usable(M_RECOVER) && (s32)me->displayHp * 256 < (s32)me->maxHp * r->heal_below && Roll(r->heal)) {
        return M_RECOVER;
    }
    // R4 shield
    if (Usable(M_SUBSTITUTE) && !(me->volatileStatusFlags & VOLATILE_STATUS_SUBSTITUTE) && t.status &&
        !t.incapacitated && (s32)me->displayHp * 256 > (s32)me->maxHp * r->substitute_min_hp && Roll(r->substitute)) {
        return M_SUBSTITUTE;
    }
    // R5 paralyze
    if (Usable(M_THUNDER_WAVE) && foe->pendingStatusCondition == 0 &&
        !(foe->volatileStatusFlags & VOLATILE_STATUS_SUBSTITUTE) && !HasType(foe, 0x04) && Roll(r->paralyze)) {
        return M_THUNDER_WAVE;
    }
    // R6 screen
    if (Usable(M_REFLECT) && !(me->uniqueStatusFlags & UNIQUE_STATUS_REFLECT) &&
        t.bestPhysical * 256 >= (s32)me->displayHp * r->reflect_threat && Roll(r->reflect)) {
        return M_REFLECT;
    }
    // R7 mist
    if (Usable(M_MIST) && !(me->volatileStatusFlags & VOLATILE_STATUS_MIST) && t.statDrop && Roll(r->mist)) {
        return M_MIST;
    }
    // R8 boost (statChanges: 7 is neutral; [3] Special)
    if (Usable(M_AMNESIA) && me->statChanges[3] < 7 + r->amnesia_max_stage &&
        t.best * 256 < (s32)me->displayHp * r->amnesia_safe && Roll(r->amnesia)) {
        return M_AMNESIA;
    }
    // R9 attack
    if (supportOnly) return -2;
    attack = BestAttack(r, 0);
    return attack >= 0 ? attack : RandomUsable();
}

int PortEnh_MewtwoChooseAction(void* cpu, int game_choice) {
    s32 i;
    SetUp();
    (void)cpu; // (the battle's AI only ever chooses for the COM, Mewtwo's side)
    if (!PortEnh_MewtwoActive()) return game_choice;
    sM.actions = 0;
    sM.skippedResidual = 0;
    sM.passing = 0;
    i = Choose(0);
    if (Log()) {
        fprintf(stderr, "[mewtwo] action 1: %s\n", i >= 0 ? "move" : "none");
    }
    if (i < 0) {
        return game_choice; // nothing usable: the game's own choice (Struggle)
    }
    sM.pendingReveal = i;
    if (Log()) fprintf(stderr, "[mewtwo] action 1 = move %d\n", kMoves[i]);
    return Place(i);
}

// ---- two actions per turn (Round 2) ----

static s32 BothStanding(void) {
    const ActivePokemonState* a = &sActivePokemon[0]->state;
    const ActivePokemonState* b = &sActivePokemon[1]->state;
    return a->runtime.displayHp > 0 && b->runtime.displayHp > 0 && a->replacementState != REPLACEMENT_INVOLUNTARY_SWITCH &&
           b->replacementState != REPLACEMENT_INVOLUNTARY_SWITCH;
}

int PortEnh_MewtwoSkipResidual(void) {
    // Mewtwo's first action of a Round 2 turn: its residual damage waits for the second
    if (!PortEnh_MewtwoActive() || !sM.round2 || sBattleScene.scene->actingSide != sM.side) return 0;
    sM.actions++;
    if (sM.actions == 1 && !sM.passing) {
        sM.skippedResidual = 1;
        return 1;
    }
    return 0;
}

int PortEnh_MewtwoActAgainPending(void) {
    return PortEnh_MewtwoActive() && sM.round2 && sM.actions == 1 && sBattleScene.scene->actingSide == sM.side;
}

int PortEnh_MewtwoActAgain(void) {
    ActivePokemon* m;
    ActivePokemonState* st;
    BattleMonRuntime* rt;
    s32 i, slot;

    if (!PortEnh_MewtwoActive() || !sM.round2 || sM.actions != 1 || sBattleScene.scene->actingSide != sM.side) {
        return 0;
    }
    m = Mewtwo();
    st = &m->state;
    rt = &st->runtime;
    SyncPp();
    if (!BothStanding() || (rt->usedMove & 0xF0) == 0xF0) {
        if (Log()) fprintf(stderr, "[mewtwo] second action cancelled\n");
        sM.actions = 2;
        return 0;
    }
    i = Choose(1);
    sM.passing = 0;
    if (i == -2) {
        sM.passing = 1;
        if (Log()) fprintf(stderr, "[mewtwo] action 2: skipped\n");
    } else if (i >= 0) {
        slot = Place(i);
        st->moveIndex = slot;
        st->selectedMoveIndex = slot;
        rt->usedMove = rt->moves[slot];
        sM.pendingReveal = i;
        if (Log()) fprintf(stderr, "[mewtwo] action 2 = move %d\n", kMoves[i]);
    } else {
        // nothing usable: Struggle
        st->moveIndex = 0;
        st->selectedMoveIndex = 0;
        rt->usedMove = STRUGGLE;
    }
    // as Battle_ActSecondAttacker, for the same side
    Battle_ResolveMonAction();
    sBattleScene.scene->unk_A0.z = 0.0f;
    BattleScene_SetPhase(sBattleScene.scene->camera, BATTLE_PHASE_ENTER_QUEUED);
    sBattleScene.scene->nextPhase = sBattleScene.scene->actionPass == BATTLE_ACTION_FIRST ? BATTLE_PHASE_FIRST_ACTION
                                                                                          : BATTLE_PHASE_SECOND_ACTION;
    sBattleScene.scene->phaseStep = 0;
    sBattleScene.scene->stepFrameTimer = 0;
    return 1;
}

// ProcessBattleTurn, first: a skipped second action. "MEWTWO is watching <foe> carefully!" with the no-action
// presentation (as a flinch); the turn's residual damage still follows.
int PortEnh_MewtwoPassAction(void) {
    char text[0x40];
    char line[0x40];
    const char* fmt;
    const char* p;
    s32 n = 0;

    if (!PortEnh_MewtwoActive() || !sM.passing || sBattleScene.scene->actingSide != sM.side) return 0;
    fmt = Port_TextPortString("PORT_MEWTWO_WATCHING");
    for (p = fmt; *p != '\0' && n < (s32)sizeof(line) - 1; p++) {
        const char* insert = NULL;
        if (p[0] == '{' && (p[1] == '0' || p[1] == '1') && p[2] == '}') {
            insert = (const char*)Battle_GetActiveMonNickname(p[1] == '0' ? Mewtwo() : Foe());
            p += 2;
        }
        if (insert != NULL) {
            while (*insert != '\0' && n < (s32)sizeof(line) - 1) line[n++] = *insert++;
        } else {
            line[n++] = *p;
        }
    }
    line[n] = '\0';
    (void)text;
    sBattleMessageQueues->buffer[1][0] = 0; // no announcer line
    strncpy((char*)&sBattleMessageQueues->buffer[1][1], line, 0x3E);
    sBattleMessageQueues->buffer[1][0x3F] = 0;
    sBattleScene.scene->moveEffectKind = MOVE_PRESENTATION_NO_ACTION;
    return 1;
}

void PortEnh_MewtwoActionResolved(void) {
    if (!PortEnh_MewtwoActive() || sBattleScene.scene->actingSide != sM.side) return;
    SyncPp();
}

// ---- the summary bar card ----

int PortEnh_MewtwoIsCard(int side) {
    return PortEnh_MewtwoActive() && side == sM.side;
}

void PortEnh_MewtwoRevealPending(void) {
    s32 i = sM.pendingReveal, k;
    if (!PortEnh_MewtwoActive() || sBattleScene.scene->actingSide != sM.side || sM.passing) return;
    sM.pendingReveal = -1;
    if (i < 0) return;
    for (k = 0; k < sM.revealCount; k++) {
        if (sM.revealed[k] == i) return;
    }
    sM.revealed[sM.revealCount++] = (u8)i;
    if (Log()) fprintf(stderr, "[mewtwo] revealed move %d (%d shown)\n", kMoves[i], (int)sM.revealCount);
}

int PortEnh_MewtwoCardMoves(uint16_t* moves, int max) {
    s32 k;
    for (k = 0; k < sM.revealCount && k < max; k++) {
        moves[k] = kMoves[sM.revealed[k]];
    }
    return k;
}

// Stat Exp difficulty (hippodrome-utils docs/specs/STATEXP_DIFFICULTY.md): the player sets a Minimum and a Maximum
// Stat Exp for the rental Pokémon and for the opponent (COM) Pokémon, each Vanilla (the default: that bound leaves the
// game's values alone), one of the five presets (Casual, Easy, Medium, Hard, Kaizo, on hippodrome-utils' quadratic
// curve, the other way round for rentals, so Kaizo rentals get the least) or Custom (enhancements.h,
// hippodrome/statexp_difficulty.h). Each of the five stat exp values is raised to the Minimum and lowered to the
// Maximum, then the stats are worked out again with the game's own formula (CalculateStatValue, gb_data.c) at the
// Pokémon's level. DVs are never changed. Compiled with the game code.
//
// Where it applies (the hooks in StadionPort.h, "Enhancements"):
// - rentals: the rental roster of the Pokémon select screen (Rental_LoadRosterForMode), each time the screen loads it
//   from the ROM, so the rental cards already show the new stats; and Battle Now's two preset teams for each human
//   side (Team_BuildQuickBattleTeams).
// - opponents: every COM team as a battle starts (BattleScene_Init, before the COM picks its three): the cup and Gym
//   Leader Castle trainers, Mewtwo and Battle Now's COM.
// Never in Free Battle (the spec), nor on the player's own Game Boy Pokémon or the attract mode's demo battles.
#include <stdio.h>

#include "global.h"
#include "game_state.h"
#include "gb_data.h"

#include "StadionPort.h"
#include "enhancements.h"

#include <hippodrome/statexp_difficulty.h>

static void PortEnh_SeApply(BattleMon* mon, const hu_se_bounds* bounds, hu_se_side side) {
    u16 stat_exp[HU_SE_STAT_COUNT];
    u16 old_max_hp = mon->maxHp;
    s32 species = mon->species.id;

    if (species == 0 || species > 151) {
        return;
    }
    // BattleMon keeps the Game Boy order: HP, Attack, Defense, Speed, Special stat exp, then the DVs (unk_1E)
    stat_exp[0] = mon->unk_14;
    stat_exp[1] = mon->unk_16;
    stat_exp[2] = mon->unk_18;
    stat_exp[3] = mon->unk_1A;
    stat_exp[4] = mon->unk_1C;
    hu_se_bounds_apply(bounds, side, stat_exp);
    mon->unk_14 = stat_exp[0];
    mon->unk_16 = stat_exp[1];
    mon->unk_18 = stat_exp[2];
    mon->unk_1A = stat_exp[3];
    mon->unk_1C = stat_exp[4];
    // (stat ids as Pokemon_RecalcStats passes them: 1 HP, 2 Attack, 3 Defense, 4 Speed, 5 Special)
    mon->maxHp = CalculateStatValue(1, species, mon->unk_14, mon->level, mon->unk_1E);
    mon->attack = CalculateStatValue(2, species, mon->unk_16, mon->level, mon->unk_1E);
    mon->defense = CalculateStatValue(3, species, mon->unk_18, mon->level, mon->unk_1E);
    mon->speed = CalculateStatValue(4, species, mon->unk_1A, mon->level, mon->unk_1E);
    mon->special = CalculateStatValue(5, species, mon->unk_1C, mon->level, mon->unk_1E);
    // a team is made at full HP: keep it full (and never above the new maximum)
    if (mon->hp >= old_max_hp || mon->hp > mon->maxHp) {
        mon->hp = mon->maxHp;
    }
}

static void PortEnh_SeApplyAll(BattleMon* mons, s32 count, s32 group, const char* what) {
    hu_se_bounds bounds;
    s32 i;

    // Free Battle keeps the game's values (the spec), as do the attract mode's demo battles and the cutscenes
    if (gSessionContext.mode == SESSION_MODE_ANYTHING_GOES || gSessionContext.mode >= SESSION_MODE_ATTRACT_DEMO_BATTLE) {
        return;
    }
    Port_Enh_StatExpBounds(group, &bounds);
    if (!hu_se_bounds_active(&bounds) || mons == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        PortEnh_SeApply(&mons[i], &bounds, group == PORT_SE_RENTAL ? HU_SE_RENTAL : HU_SE_OPPONENT);
        if (Port_Enh_Log()) {
            fprintf(stderr,
                    "[statexp-difficulty] %s-%s %s %d: species %d L%d dvs %04X stat exp %u/%u/%u/%u/%u -> HP %u Atk %u "
                    "Def %u Spd %u Spc %u\n",
                    hu_se_mode_name(bounds.min.mode), hu_se_mode_name(bounds.max.mode), what, (int)i, mons[i].species.id, mons[i].level, mons[i].unk_1E,
                    mons[i].unk_14, mons[i].unk_16, mons[i].unk_18, mons[i].unk_1A, mons[i].unk_1C, mons[i].maxHp,
                    mons[i].attack, mons[i].defense, mons[i].speed, mons[i].special);
        }
    }
}

void PortEnh_StatExpRentals(struct BattleMon* mons, int count) {
    PortEnh_SeApplyAll(mons, count, PORT_SE_RENTAL, "rental");
}

void PortEnh_StatExpRoster(struct TeamRoster* roster, int group) {
    const char* what = group == PORT_SE_RENTAL ? "rental team" : "opponent";
    s32 brought;
    s32 all;

    if (roster == NULL) {
        return;
    }
    brought = roster->numberOfPokemonBrought;
    if (brought > 6) {
        brought = 6;
    }
    PortEnh_SeApplyAll(roster->pokemonPicked, brought, group, what);
    if (roster->rosterInfo != NULL) {
        all = roster->rosterInfo->numberOfPokemonCanBePicked;
        if (all > 6) {
            all = 6;
        }
        PortEnh_SeApplyAll(roster->rosterInfo->battleMon, all, group, what);
    }
}

void PortEnh_StatExpBattleTeams(void) {
    s32 side;
    s32 i;

    for (side = 0; side < 2; side++) {
        BattleSessionTeams* teams = &gSessionContext.battleSides[side];
        for (i = 0; i < teams->tagTeamSize && i < 2; i++) {
            TeamRoster* roster = teams->tagTeamMember[i];
            if (roster != NULL && (roster->isHuman & 2)) { // the COM's roster has isHuman 3
                PortEnh_StatExpRoster(roster, PORT_SE_OPPONENT);
            }
        }
    }
}

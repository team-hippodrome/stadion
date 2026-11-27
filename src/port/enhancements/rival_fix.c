// Rival bug fix (hippodrome-utils docs/specs/game_specific/1/RIVAL_BUG_FIX.md). Compiled with the game code.
//
// The Gym Leader Castle's rival brings a team that depends on the player's Game Boy game: the rival's starter in Red /
// Blue / Green, or the record of the early rival battles in Yellow. Both are one byte of the Game Boy save, which the
// team select screen reads when the player picks a Game Boy (GbSave_GetStarterChoice into
// gSessionContext.gbStarterChoice) and Team_LoadOpponentPreset turns into the rival's team. A registered team is
// picked as a source of its own (4), which the game treats like the rentals: the byte becomes 0 and the rival brings
// his rental-team team even though the registered team is made of Game Boy Pokémon.
//
// The fix keeps that byte for each registered team made from a Game Boy in a side file (<N64 save>.rival, written by
// enhancements.cpp), and gives it back to the game when the team is picked:
// - RegisteredTeam_Register: the player picks the team's source (PortEnh_RivalRegisterSource), and the new team is
//   written to the save (func_8420DBA0, PortEnh_RivalTeamSaved): the byte goes into the side file under the team's
//   key, even while the option is off, so turning it on later works for teams registered before.
// - Team_CommitSelectionToTrainer: a registered team is picked (PortEnh_RivalTeamPicked): with the option on, its byte
//   replaces the 0 the game set. A team registered from the rentals has no byte, and keeps the rental rival.
// The key is a hash of the team's Pokémon (species, level, moves and DVs), which the save keeps as they were
// registered; the save's slots are renumbered when a team is deleted, so the slot number is not used.
#include <stdio.h>

#include "global.h"
#include "game_state.h"
#include "src/pokemon_stats.h"
#include "src/fragments/rental_team_selection/rental_team_selection.h"

#include "StadionPort.h"
#include "enhancements.h"

static s32 sRegisterStarter = -1; // the Game Boy byte of the team being registered, -1: from the rentals

static u64 PortEnh_RivalKey(unk_D_84229EB0_00024* team) {
    u64 h = 0xCBF29CE484222325ULL; // FNV-1a
    s32 i, j;
    s32 any = 0;

#define RIVAL_MIX(b)                     \
    do {                                 \
        h ^= (u8)(b);                    \
        h *= 0x100000001B3ULL;           \
    } while (0)

    for (i = 0; i < 6; i++) {
        BattleMon* mon = &team->unk_0000[i].unk_004;
        if (mon->species.id == 0) {
            continue;
        }
        any = 1;
        RIVAL_MIX(mon->species.id);
        RIVAL_MIX(mon->level);
        for (j = 0; j < 4; j++) {
            RIVAL_MIX(mon->moves[j]);
        }
        RIVAL_MIX(mon->unk_1E >> 8); // the DVs
        RIVAL_MIX(mon->unk_1E & 0xFF);
    }
#undef RIVAL_MIX
    return any ? h : 0;
}

void PortEnh_RivalRegisterSource(s32 source) {
    sRegisterStarter = (source >= 0 && source < 4) ? GbSave_GetStarterChoice(source) : -1;
}

void PortEnh_RivalTeamSaved(void* team) {
    u64 key = PortEnh_RivalKey((unk_D_84229EB0_00024*)team);

    if (key == 0 || sRegisterStarter < 0) {
        return;
    }
    Port_Enh_RivalRecord(key, sRegisterStarter);
    if (Port_Enh_Log()) {
        fprintf(stderr, "[rival-fix] registered team %016llx: starter choice 0x%02X\n", (unsigned long long)key,
                (int)sRegisterStarter);
    }
}

void PortEnh_RivalTeamPicked(void* team) {
    u64 key;
    s32 starter;

    if (!Port_Enh_RivalFixEnabled() || team == NULL) {
        return;
    }
    key = PortEnh_RivalKey((unk_D_84229EB0_00024*)team);
    starter = key != 0 ? Port_Enh_RivalLookup(key) : -1;
    if (starter >= 0) {
        gSessionContext.gbStarterChoice = (u8)starter;
    }
    if (Port_Enh_Log()) {
        fprintf(stderr, "[rival-fix] registered team %016llx picked: starter choice %d\n", (unsigned long long)key,
                (int)starter);
    }
}

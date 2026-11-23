// Stadium 1's rules for trainer memory (hippodrome-utils docs/specs/TRAINER_MEMORY.md): which trainer a battle is
// against and which trainers the team select screen lists. Compiled with the game code.
//
// Every COM trainer of the cups, Gym Leader Castle and VS Mewtwo is a preset that Team_LoadOpponentPreset loads from
// the trainer data archive: one file per cup (and division, and Round 2), and a slot in it, which it keeps in
// opponentHistory[0][round - 1]. A key is that: `cup` the session mode (1-6 the cups, 7 Gym Leader Castle, 8 VS
// Mewtwo), `division` the cup's division or the castle's room (modeSelection; 0 where the mode has none), `round` Round
// 1 / 2 and `trainer` the slot. Keying on the slot rather than the round keeps a cup whose Round 2 meets its trainers
// in random order (Nintendo Cup '98) right. Free Battle, Battle Now and the event battles have no memory: their COM
// teams are not fixed trainers.
#include <stddef.h>
#include <string.h>

#include "global.h"
#include "game_state.h"
#include "variables.h"

#include "StadionPort.h"
#include "enhancements.h"
#include "battle_enhancements.h"

#include <hippodrome/trainer_memory.h>

#define GLC_ELITE_FOUR 8
#define GLC_RIVAL 9
// trainerName[4] and the 12 bytes after it hold the name (Team_LoadPresetTrainer copies it with HAL_Strcpy)
#define TRAINER_NAME_BYTES 16
#define RIVAL_SLOTS 7 // Team_LoadOpponentPreset: the rival's team depends on the player's starter (slots 0-6)

static s32 HasDivisions(s32 mode) {
    // Team_LoadOpponentPreset: only these modes pick their file by modeSelection
    return mode == SESSION_MODE_NINTENDO_CUP_99 || mode == SESSION_MODE_PRIME_CUP ||
           mode == SESSION_MODE_GYM_LEADER_CASTLE;
}

hu_tm_key PortEnh_TmKey(s32 mode, s32 division, s32 round2, s32 slot) {
    hu_tm_key k;
    k.game = HU_GAME_STADIUM_1;
    k.cup = (u8)mode;
    k.division = HasDivisions(mode) ? (u8)division : 0;
    k.round = round2 ? 1 : 0;
    k.trainer = (u8)slot;
    return k;
}

s32 PortEnh_TmKeyForSession(hu_tm_key* key) {
    s32 mode = gSessionContext.mode;
    s32 round = gSessionContext.roundOrDemoId - 1;
    s32 slot;

    if (mode < SESSION_MODE_NINTENDO_CUP_97 || mode > SESSION_MODE_VS_MEWTWO || round < 0 || round >= 8) {
        return 0;
    }
    slot = gSessionContext.opponentHistory[0][round]; // (eight contiguous entries)
    if (slot == 0xFF) {
        return 0;
    }
    *key = PortEnh_TmKey(mode, gSessionContext.modeSelection, gSessionContext.inRound2, slot);
    return 1;
}

// Team_LoadOpponentPreset has loaded the COM trainer: the battle prep screen now shows its name and team.
void PortEnh_TmTrainerLoaded(void) {
    hu_tm_key key;
    TeamRoster* r = gSessionContext.battleSides[1].tagTeamMember[0];
    hu_tm_team_entry team[HU_TM_TEAM_MAX];
    char name[TRAINER_NAME_BYTES + 1];
    s32 n, i, k;

    if (r == NULL || !PortEnh_TmKeyForSession(&key)) {
        return;
    }
    n = r->numberOfPokemonBrought;
    if (n > HU_TM_TEAM_MAX) n = HU_TM_TEAM_MAX;
    for (i = 0; i < n; i++) {
        team[i].species = r->pokemonPicked[i].species.id;
        team[i].level = r->pokemonPicked[i].level;
        for (k = 0; k < HU_TM_MOVES; k++) team[i].moves[k] = r->pokemonPicked[i].moves[k];
    }
    hu_tm_record_team(Port_Enh_Memory(), key, team, n);
    memcpy(name, (const char*)r + offsetof(TeamRoster, trainerName), TRAINER_NAME_BYTES);
    name[TRAINER_NAME_BYTES] = '\0';
    hu_tm_record_name(Port_Enh_Memory(), key, name);
    Port_Enh_SaveMemory();
}

s32 PortEnh_TmScreenKeys(hu_tm_key* keys, s32 max) {
    const hu_tm* tm;
    s32 mode = gSessionContext.mode;
    s32 division = gSessionContext.modeSelection;
    s32 round2 = gSessionContext.inRound2;
    s32 n = 0, i;

    switch (mode) {
        case SESSION_MODE_NINTENDO_CUP_97:
        case SESSION_MODE_NINTENDO_CUP_98:
        case SESSION_MODE_NINTENDO_CUP_99:
        case SESSION_MODE_PETIT_CUP:
        case SESSION_MODE_PIKA_CUP:
        case SESSION_MODE_PRIME_CUP:
            for (i = 0; i < 8 && n < max; i++) keys[n++] = PortEnh_TmKey(mode, division, round2, i);
            break;
        case SESSION_MODE_GYM_LEADER_CASTLE:
            if (division < 0 || division > GLC_ELITE_FOUR) break;
            // a gym: three trainers, then its leader; the Elite Four: four, then the rival, who is met with the same
            // team (Team_SelectTeams skips the select screen for it)
            for (i = 0; i < 4 && n < max; i++) keys[n++] = PortEnh_TmKey(mode, division, round2, i);
            if (division == GLC_ELITE_FOUR && n < max) {
                tm = Port_Enh_Memory();
                keys[n] = PortEnh_TmKey(mode, GLC_RIVAL, round2, RIVAL_SLOTS - 1);
                for (i = 0; i < RIVAL_SLOTS; i++) { // the team the player met (it follows their starter)
                    const hu_tm_trainer* t = hu_tm_find(tm, PortEnh_TmKey(mode, GLC_RIVAL, round2, i));
                    if (t != NULL && (t->flags & HU_TM_REACHED)) {
                        keys[n] = t->key;
                        break;
                    }
                }
                n++;
            }
            break;
        case SESSION_MODE_VS_MEWTWO:
            if (n < max) keys[n++] = PortEnh_TmKey(mode, 0, round2, 0);
            break;
    }
    return n;
}

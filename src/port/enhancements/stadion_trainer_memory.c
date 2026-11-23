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
#include "sections.h"
#include "versions.h"
#include "src/jpeg_stream.h"
#include "src/memory.h"

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

// The trainers' portraits (the VS screen's: Team_LoadPresetTrainer puts the trainer record's picture number,
// the high byte of its big-endian word at +0x34, in rosterInfo->unk_003, and the battle prep screen draws that file of
// the battle_portraits archive). For the memory screen: the record of each trainer of `keys` whose `want` is set, then
// its picture, copied out as RGBA8 (Port_Enh_SetPortrait); out[i] its number, -1 none. The archives and files only for
// the copy: the pool goes back to where the screen left it.
#define TRAINER_FILE_ROUND2 0x1F
#define TRAINER_SLOT_BYTES 0x230 // PresetTrainerSlot (session.c)
#define TRAINER_PICTURE_AT 0x38  // its PresetTrainer (at +4)'s word at +0x34
#define PORTRAIT_W 64            // a 64-wide RGBA16 image; the picture is its 56 x 56 from (4, 4)

static s32 TrainerFile(hu_tm_key k) {
    s32 f;
    switch (k.cup) {
        case SESSION_MODE_NINTENDO_CUP_97: f = 6; break;
        case SESSION_MODE_NINTENDO_CUP_98: f = 7; break;
        case SESSION_MODE_NINTENDO_CUP_99: f = k.division + 8; break;
        case SESSION_MODE_PETIT_CUP: f = 0; break;
        case SESSION_MODE_PIKA_CUP: f = 1; break;
        case SESSION_MODE_PRIME_CUP: f = k.division + 2; break;
        case SESSION_MODE_GYM_LEADER_CASTLE: f = k.division + 0xC; break;
        case SESSION_MODE_VS_MEWTWO: f = 0x16; break;
        default: return -1;
    }
    return f + (k.round ? TRAINER_FILE_ROUND2 : 0);
}

void PortEnh_TmLoadPortraits(const hu_tm_key* keys, const u8* want, s32 n, s32* out) {
    static u8 rgba[PORT_ENH_PORTRAIT * PORT_ENH_PORTRAIT * 4];
    BinArchive* trainers;
    BinArchive* pictures = NULL;
    s32 i, x, y;

    for (i = 0; i < n; i++) out[i] = -1;
    main_pool_push_state('TMPT');
    trainers = BinArchive_Open(TRAINER_DATA_ROM_START, NULL, 1, 0);
    for (i = 0; trainers != NULL && i < n; i++) {
        const u8* file;
        s32 f = TrainerFile(keys[i]);
        if (!want[i] || f < 0 || f >= trainers->num_files) continue;
        file = (const u8*)BinArchive_GetFile(trainers, f);
        if (file != NULL) {
            out[i] = file[keys[i].trainer * TRAINER_SLOT_BYTES + TRAINER_PICTURE_AT];
        }
    }
    for (i = 0; i < n; i++) {
        const u8* img;
        if (out[i] < 0 || Port_Enh_HasPortrait((uint32_t)out[i])) continue;
        if (pictures == NULL) {
            pictures = ASSET_LOAD2(battle_portraits, 1, 1);
            if (pictures == NULL) break;
        }
        img = (out[i] < pictures->num_files) ? (const u8*)BinArchive_GetFile(pictures, out[i]) : NULL;
        if (img == NULL) {
            out[i] = -1;
            continue;
        }
        for (y = 0; y < PORT_ENH_PORTRAIT; y++) {
            for (x = 0; x < PORT_ENH_PORTRAIT; x++) {
                const u8* p = img + ((y + 4) * PORTRAIT_W + (x + 4)) * 2;
                u16 px = (u16)((p[0] << 8) | p[1]);
                u8* o = &rgba[(y * PORT_ENH_PORTRAIT + x) * 4];
                o[0] = (u8)(((px >> 11) & 0x1F) * 255 / 31);
                o[1] = (u8)(((px >> 6) & 0x1F) * 255 / 31);
                o[2] = (u8)(((px >> 1) & 0x1F) * 255 / 31);
                o[3] = (px & 1) ? 0xFF : 0x00;
            }
        }
        Port_Enh_SetPortrait((uint32_t)out[i], rgba);
    }
    main_pool_pop_state('TMPT');
}

// The enhancements' game-side parts call each other through this header (they are compiled with the game code). The
// hooks the game itself calls are in StadionPort.h ("Enhancements").
#pragma once
#include "global.h"

// The attract mode's demo battles and the cutscene battles, which play themselves: no enhancement runs in them.
s32 PortEnh_BattleIsDemo(void);

// battle_menus.c: mid-battle move descriptions and order queueing
void PortEnh_BattleMenusBegin(void); // from PortEnh_BattleBegin
void PortEnh_BattleMenusFrame(void); // from PortEnh_BattleFrame, every HUD frame

// battle_enhancements.c: the cards' picture of a species in its own colours (the card icons' key, 0 if none)
uint32_t PortEnh_SpeciesIcon(u8 species);

// stadion_trainer_memory.c: Stadium 1's trainer memory keys
#include <hippodrome/trainer_memory.h>
hu_tm_key PortEnh_TmKey(s32 mode, s32 division, s32 round2, s32 slot);
// The key of the COM trainer the session's current round is against; 0 when the mode has no memory.
s32 PortEnh_TmKeyForSession(hu_tm_key* key);
// Team_LoadOpponentPreset loaded that trainer: records its name and team (StadionPort.h).
void PortEnh_TmTrainerLoaded(void);
// The trainers the team select screen lists for the session's mode, division and round (at most `max`).
s32 PortEnh_TmScreenKeys(hu_tm_key* keys, s32 max);

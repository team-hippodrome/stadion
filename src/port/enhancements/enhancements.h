// The port's enhancements from hippodrome-utils (the team's shared library, the hippodrome-utils submodule; specs in
// hippodrome-utils/docs/specs). Each one is an option, off (or Vanilla) by default, so the game plays and looks as on
// the N64 unless it is turned on in the launcher (Graphics > Enhancements).
//
// enhancements.cpp keeps the settings (port CVars gEnhancements.*, not the N64 save). The game-side parts
// (statexp_difficulty.c and the others in this folder) are compiled with the game code, since they read its structs;
// the game calls them through the hooks in StadionPort.h ("Enhancements").
#pragma once
#include <stdint.h>

#include <hippodrome/statexp_difficulty.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stat Exp difficulty (STATEXP_DIFFICULTY): a setting for the rental Pokémon and one for the opponent (COM) Pokémon,
// each Vanilla (the default), Casual / Easy / Medium / Hard / Kaizo (fifths of 65535, the other way round for
// rentals) or Custom (gEnhancements.StatExpDifficulty.<Rental|Opponent>.Mode / Min / Max; Mode is hu_se_mode).
// PORT_STATEXP_RENTAL / PORT_STATEXP_OPPONENT override them for tests: a mode name (vanilla, casual, ..., kaizo) or
// "<min>-<max>" for Custom (e.g. "65535-65535").
#define PORT_SE_RENTAL 0
#define PORT_SE_OPPONENT 1
// (pointers, not structs by value: the game side is C built with the game's flags)
void Port_Enh_StatExpDifficulty(int group, hu_se_setting* out);
void Port_Enh_SetStatExpDifficulty(int group, const hu_se_setting* setting);

// PORT_ENH_LOG=1: what the enhancements do, on stderr (tests).
int Port_Enh_Log(void);

#ifdef __cplusplus
}
#endif

// The port's enhancements from hippodrome-utils (the team's shared library, the hippodrome-utils submodule; specs in
// hippodrome-utils/docs/specs). Each one is an option, off (or Vanilla) by default, so the game plays and looks as on
// the N64 unless it is turned on in the launcher (Graphics > Enhancements).
//
// enhancements.cpp keeps the settings (port CVars gEnhancements.*, not the N64 save). The game-side parts
// (statexp_difficulty.c and the others in this folder) are compiled with the game code, since they read its structs;
// the game calls them through the hooks in StadionPort.h ("Enhancements").
#pragma once
#include <stdint.h>

#include <hippodrome/hue_choice.h>
#include <hippodrome/statexp_difficulty.h>

#ifdef __cplusplus
extern "C" {
#endif

// The On / Off enhancements (port CVars gEnhancements.<Name>, 0 by default). PORT_MOVE_DESCRIPTIONS=0/1 and
// PORT_ORDER_QUEUE=0/1 override them (test aids).
int Port_Enh_MoveDescriptionsEnabled(void); // MIDBATTLE_MOVE_DESCRIPTION
void Port_Enh_SetMoveDescriptionsEnabled(int on);
int Port_Enh_OrderQueueEnabled(void); // ORDER_QUEUEING
void Port_Enh_SetOrderQueueEnabled(int on);

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

// Hue choice (HUE_CHANGE): the colour of each Pokémon, picked on the Pokémon card of the team select screens and kept
// in <N64 save>.hue beside the save (gEnhancements.HueChoice: PORT_HUE_OFF, the default, PORT_HUE_UNRESTRICTED or
// PORT_HUE_RESTRICTED, inside the species' own nickname hue range). PORT_HUE_CHOICE=off / restricted / unrestricted
// overrides it (tests).
#define PORT_HUE_OFF 0
#define PORT_HUE_UNRESTRICTED 1
#define PORT_HUE_RESTRICTED 2
int Port_Enh_HueChoiceMode(void);
void Port_Enh_SetHueChoiceMode(int mode);
int Port_Enh_HueChoiceEnabled(void); // mode != PORT_HUE_OFF
// The hues picked so far (read from the file on first use) and writing them back when they changed.
hu_hue_store* Port_Enh_Hues(void);
void Port_Enh_SaveHues(void);

// PORT_ENH_LOG=1: what the enhancements do, on stderr (tests).
int Port_Enh_Log(void);

#ifdef __cplusplus
}
#endif

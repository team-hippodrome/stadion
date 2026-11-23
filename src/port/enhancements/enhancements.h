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
#include <hippodrome/summary_card.h>

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

// Side summary bars and speed estimates (PARTY_SUMMARY_BARS, SPEED_STAT_ESTIMATE): gEnhancements.SummaryBars, 0 by
// default; PORT_SUMMARY_BARS=0/1 overrides it.
int Port_Enh_SummaryBarsEnabled(void);
void Port_Enh_SetSummaryBarsEnabled(int on);

// What the summary bars show, published by the battle each frame it draws its HUD (battle_enhancements.c, game thread)
// and drawn by summary_bars.cpp beside the game's picture (render thread). Strings are UTF-8.
#define PORT_ENH_TEXT 32
#define PORT_ENH_ICON 40 // the cards' picture: the game's own menu icon of the Pokémon (40 x 40)
typedef struct PortEnhBattleView {
    hu_side sides[2]; // 0 = the left trainer (1P), 1 = the right one
    char names[2][HU_SIDE_MAX][PORT_ENH_TEXT];
    char moves[2][HU_SIDE_MAX][HU_CARD_MOVES][PORT_ENH_TEXT];
    uint8_t moveTypes[2][HU_SIDE_MAX][HU_CARD_MOVES]; // hu_type, for the move names' colours
    hu_speed_range speed[2][HU_SIDE_MAX];             // as shown: the active one's with its stage and paralysis
    uint32_t iconKey[2][HU_SIDE_MAX];                 // which picture the card shows (0: none); see the icons below
    int32_t viewer; // the side whose own Pokémon are known (0 against the COM), -1 if none
} PortEnhBattleView;
void Port_Enh_PublishBattle(const PortEnhBattleView* view);
// The last view if one was published in the last few frames (a battle is on screen); 0 otherwise.
int Port_Enh_LatestBattle(PortEnhBattleView* out);
// The cards' pictures, PORT_ENH_ICON x PORT_ENH_ICON RGBA8 pixels each, kept under a key (species, hue) the view
// names. Port_Enh_HasCardIcon: 1 once that key's picture is kept.
int Port_Enh_HasCardIcon(uint32_t key);
void Port_Enh_SetCardIcon(uint32_t key, const uint8_t* rgba);
// The picture for `key` (NULL if none), on the render thread.
const uint8_t* Port_Enh_CardIcon(uint32_t key);

// PORT_ENH_LOG=1: what the enhancements do, on stderr (tests).
int Port_Enh_Log(void);

#ifdef __cplusplus
}
#endif

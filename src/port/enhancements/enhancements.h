// The port's enhancements from hippodrome-utils (the team's shared library, the hippodrome-utils submodule; specs in
// hippodrome-utils/docs/specs). Each one is an option the player can turn off in the launcher (Graphics >
// Enhancements). Unlike Plethron's, they are on by default (the project's choice: customisability first); Stat Exp
// difficulty starts at Vanilla, as its spec says.
//
// enhancements.cpp keeps the settings (port CVars gEnhancements.*, not the N64 save). The game-side parts
// (statexp_difficulty.c and the others in this folder) are compiled with the game code, since they read its structs;
// the game calls them through the hooks in StadionPort.h ("Enhancements").
#pragma once
#include <stdint.h>

#include <hippodrome/hue_choice.h>
#include <hippodrome/statexp_difficulty.h>
#include <hippodrome/summary_card.h>
#include <hippodrome/trainer_memory.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ENH_TEXT 32 // the overlays' strings (UTF-8)
#define PORT_ENH_ICON 40 // the game's own menu icon of a Pokémon (40 x 40): the memory screen's pictures

// The On / Off enhancements (port CVars gEnhancements.<Name>, 1 by default). PORT_MOVE_DESCRIPTIONS=0/1 and
// PORT_ORDER_QUEUE=0/1 override them (test aids).
int Port_Enh_MoveDescriptionsEnabled(void); // MIDBATTLE_MOVE_DESCRIPTION
void Port_Enh_SetMoveDescriptionsEnabled(int on);
int Port_Enh_OrderQueueEnabled(void); // ORDER_QUEUEING
void Port_Enh_SetOrderQueueEnabled(int on);

// Stat Exp difficulty (STATEXP_DIFFICULTY): for the rental Pokémon and for the opponent (COM) Pokémon, a Minimum and
// a Maximum, each Vanilla (the default), Casual / Easy / Medium / Hard / Kaizo (hippodrome-utils' quadratic presets,
// the other way round for rentals) or Custom (gEnhancements.StatExpDifficulty.<Rental|Opponent>.<Min|Max>.Mode, a
// hu_se_mode, and .Value, the custom value). Not applied in Free Battle. PORT_STATEXP_RENTAL / PORT_STATEXP_OPPONENT
// override them for tests: "<min>:<max>", each a mode name (vanilla, casual, ..., kaizo) or a number (Custom), or one
// mode name for both (e.g. "kaizo", "vanilla:4096").
#define PORT_SE_RENTAL 0
#define PORT_SE_OPPONENT 1
// (pointers, not structs by value: the game side is C built with the game's flags)
void Port_Enh_StatExpBounds(int group, hu_se_bounds* out);
void Port_Enh_SetStatExpBounds(int group, const hu_se_bounds* bounds);

// Hue choice (HUE_CHANGE): the colour of each Pokémon, picked on the Pokémon card of the team select screens and kept
// in <N64 save>.hue beside the save (gEnhancements.HueChoice: PORT_HUE_OFF, PORT_HUE_UNRESTRICTED or
// PORT_HUE_RESTRICTED, the default, inside the species' own nickname hue range). PORT_HUE_CHOICE=off / restricted / unrestricted
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

// Side summary bars and speed estimates (PARTY_SUMMARY_BARS, SPEED_STAT_ESTIMATE): gEnhancements.SummaryBars;
// PORT_SUMMARY_BARS=0/1 overrides it.
int Port_Enh_SummaryBarsEnabled(void);
void Port_Enh_SetSummaryBarsEnabled(int on);

// The opponents' stat experience range under Stat Exp difficulty, as the game holds it (0 and 65535 when both bounds
// are Vanilla), for the speed estimates.
void Port_Enh_OpponentStatExp(uint16_t* lo, uint16_t* hi);

// Trainer memory (TRAINER_MEMORY): gEnhancements.TrainerMemory (PORT_TRAINER_MEMORY=0/1 overrides it). The memory is
// <N64 save>.memory beside the save (read on first use), written when it changed. Battles keep it up to date even
// while the setting is off; the setting decides whether the screen and the cards use it.
int Port_Enh_TrainerMemoryEnabled(void);
void Port_Enh_SetTrainerMemoryEnabled(int on);
hu_tm* Port_Enh_Memory(void);
void Port_Enh_SaveMemory(void);
const char* Port_Enh_MemoryPath(void);

// What the trainer memory screen shows, published by the team select screen each frame (memory_screen.c, game
// thread) and drawn by memory_screen.cpp over the game's picture (render thread). Strings are UTF-8.
#define PORT_MEM_ROWS 8
typedef struct PortEnhMemoryMon {
    char name[PORT_ENH_TEXT];
    uint8_t level;      // 0: not shown
    uint8_t hasSpeed;
    uint32_t iconKey;   // the card icons' key (Port_Enh_CardIcon), 0 for none
    hu_speed_range speed;
    char moves[HU_TM_MOVES][PORT_ENH_TEXT]; // "" for an empty slot; "-" for one not seen yet
    uint8_t moveTypes[HU_TM_MOVES];         // hu_type
} PortEnhMemoryMon;
typedef struct PortEnhMemoryView {
    int32_t open;       // 0: only the hint shows
    float size, fill;   // the window's animation (hu_window_anim_size / _fill)
    int32_t rows, cursor;
    char title[PORT_ENH_TEXT * 2];
    char rowNames[PORT_MEM_ROWS][PORT_ENH_TEXT * 2]; // a trainer's name once reached, else "Not met yet"
    uint8_t reached[PORT_MEM_ROWS];
    int32_t portrait;  // the highlighted trainer's portrait (Port_Enh_Portrait), -1 none
    int32_t teamCount; // the highlighted trainer's Pokémon shown (0: its team is not known)
    PortEnhMemoryMon team[HU_TM_TEAM_MAX];
} PortEnhMemoryView;
void Port_Enh_PublishMemory(const PortEnhMemoryView* view);
int Port_Enh_LatestMemory(PortEnhMemoryView* out); // 0 when the screen is not showing

// What the summary bars show, published by the battle each frame it draws its HUD (battle_enhancements.c, game thread)
// and drawn by summary_bars.cpp beside the game's picture (render thread). Strings are UTF-8.
typedef struct PortEnhBattleView {
    hu_side sides[2]; // 0 = the left trainer (1P), 1 = the right one
    char names[2][HU_SIDE_MAX][PORT_ENH_TEXT];
    char moves[2][HU_SIDE_MAX][HU_CARD_MOVES][PORT_ENH_TEXT];
    uint8_t moveTypes[2][HU_SIDE_MAX][HU_CARD_MOVES]; // hu_type, for the move names' colours
    hu_speed_range speed[2][HU_SIDE_MAX];             // as shown: the active one's with its stage and paralysis
    uint32_t iconKey[2][HU_SIDE_MAX];                 // which picture the card shows (0: none); see the icons below
    int32_t viewer; // the side whose own Pokémon are known (0 against the COM), -1 if none
    uint8_t modelReady[2][HU_SIDE_MAX];   // the card's 3D model is drawn into its cell of the side's picture
    uint8_t modelSpecies[2][HU_SIDE_MAX]; // whose model it is
    uint8_t orderSelect; // the battle order screen's (a 2D screen: the bars go beside its 4:3 frame, PortEnh_OrderSelectFrame)
} PortEnhBattleView;
// The cards' 3D models (card_models.c) are drawn into one off-screen picture per side, a row of
// PORT_ENH_CARD_CELL x PORT_ENH_CARD_CELL cells, one per card; summary_bars.cpp creates it on the render thread and
// shows each card's cell. The framebuffer's id (0 until it exists).
#define PORT_ENH_CARD_CELL 160
#define PORT_ENH_CARD_ATLAS_W (PORT_ENH_CARD_CELL * HU_SIDE_MAX)
#define PORT_ENH_FAINT_FRAMES 40
int Port_Enh_CardModelFramebuffer(int side);
void Port_Enh_PublishBattle(const PortEnhBattleView* view);
// The last view if one was published in the last few frames (a battle is on screen); 0 otherwise.
int Port_Enh_LatestBattle(PortEnhBattleView* out);
// The cards' pictures, PORT_ENH_ICON x PORT_ENH_ICON RGBA8 pixels each, kept under a key (species, hue) the view
// names. Port_Enh_HasCardIcon: 1 once that key's picture is kept.
int Port_Enh_HasCardIcon(uint32_t key);
void Port_Enh_SetCardIcon(uint32_t key, const uint8_t* rgba);
// The picture for `key` (NULL if none), on the render thread.
const uint8_t* Port_Enh_CardIcon(uint32_t key);

// The trainers' portraits, as the VS screen shows them (the battle_portraits archive's picture of the trainer):
// PORT_ENH_PORTRAIT x PORT_ENH_PORTRAIT RGBA8 pixels, kept under the archive's file number.
#define PORT_ENH_PORTRAIT 56
int Port_Enh_HasPortrait(uint32_t index);
void Port_Enh_SetPortrait(uint32_t index, const uint8_t* rgba);
const uint8_t* Port_Enh_Portrait(uint32_t index); // (render thread)

// PORT_ENH_LOG=1: what the enhancements do, on stderr (tests).
int Port_Enh_Log(void);

#ifdef __cplusplus
}
#endif

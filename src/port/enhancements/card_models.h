#pragma once
// The summary bars' 3D models (card_models.c, game code).
#include "global.h"
#include <hippodrome/summary_card.h>

// Frees every copy (a new battle).
void PortEnh_CardModelsFree(void);
// Loads card `card` of `side`'s copy of `mon`'s model (nothing if that one is already there).
void PortEnh_CardModelsLoad(s32 side, s32 card, BattleMon* mon);
// While transformed: card `card` shows the model of (`from_side`, `from_card`); Restore puts its own back.
void PortEnh_CardModelsLoadFrom(s32 side, s32 card, s32 from_side, s32 from_card);
void PortEnh_CardModelsRestore(s32 side, s32 card);
// Whether the card's model is loaded and the picture exists; whose model it shows (0 none).
s32 PortEnh_CardModelReady(s32 side, s32 card);
s32 PortEnh_CardModelSpecies(s32 side, s32 card);

// What a side's battle shows of the Pokémon in battle, for its card.
typedef struct PortEnhCardBattle {
    s32 active;               // its card (-1 none)
    const DisplayObject* obj; // its battle model
    s32 idleAnim;             // its idle (its battle cues), -1 unknown
    s32 faintAnim;            // its fainting animation, -1 unknown
    s32 sleepAnim;            // its sleeping idle, -1 none
} PortEnhCardBattle;
// Draws the copies into the port's card pictures (after the battle's HUD), animated as the cards' states say.
void PortEnh_CardModelsDraw(const hu_side sides[2], const PortEnhCardBattle battle[2]);

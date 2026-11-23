// The battle's enhancements (hippodrome-utils docs/specs): what runs when a battle starts and on every frame it draws
// its HUD (StadionPort.h "Enhancements": BattleScene_Init and Battle_RenderHud call these). Compiled with the game
// code, kept in the port, so game/ holds only decomp code. Each enhancement checks its own setting.
#include "global.h"
#include "game_state.h"

#include "StadionPort.h"
#include "enhancements.h"
#include "battle_enhancements.h"

s32 PortEnh_BattleIsDemo(void) {
    // 16 the attract mode's demo battle, 17 and up its cutscenes and the other scripted scenes (game_state.h)
    return gSessionContext.mode >= SESSION_MODE_ATTRACT_DEMO_BATTLE;
}

void PortEnh_BattleBegin(void) {
    PortEnh_BattleMenusBegin();
}

void PortEnh_BattleFrame(void) {
    PortEnh_BattleMenusFrame(); // move descriptions, order queueing
}

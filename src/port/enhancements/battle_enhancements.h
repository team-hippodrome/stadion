// The enhancements' game-side parts call each other through this header (they are compiled with the game code). The
// hooks the game itself calls are in StadionPort.h ("Enhancements").
#pragma once
#include "global.h"

// The attract mode's demo battles and the cutscene battles, which play themselves: no enhancement runs in them.
s32 PortEnh_BattleIsDemo(void);

// battle_menus.c: mid-battle move descriptions and order queueing
void PortEnh_BattleMenusBegin(void); // from PortEnh_BattleBegin
void PortEnh_BattleMenusFrame(void); // from PortEnh_BattleFrame, every HUD frame

# Memory-set gate trials

| gate | set | callee reached | new functions |
|---|---|---|---|
| BattleAI_ChooseAction -> BattleAI_DecideAction | `BattleAI_ChooseAction:D_843C5564*:4|=4194304` | yes | 10 |
| BattleAI_ChooseAction -> BattleAI_ResolveTiedActionChoice | `BattleAI_ChooseAction:D_843C60AC*+0x4:1=1` | no | 0 |
| BattleAI_RecordUsedMove -> BattleAI_SimulateStatBoostGuess | `BattleAI_RecordUsedMove:D_843C5564*:4|=1024` | yes | 2 |
| BattleScene_ActionSetupFinalizeStep -> BattleScene_RunMoveEffectEndHpTransition | `BattleScene_ActionSetupFinalizeStep:sBattleScene*+0x48:4=32` | yes | 1 |
| BattleScene_HandlePlayerPartySelection -> BattleAnim_PlaySoundEffect1 | `BattleScene_HandlePlayerPartySelection:D_8439014C*+0x8:2|=16` | no | 0 |
| BattleScene_HandlePlayerPartySelection -> BattleAnim_PlaySoundEffect3 | `BattleScene_HandlePlayerPartySelection:D_8439014C*+0x6:2=0` | no | 0 |
| BattleScene_HandlePlayerPartySelection -> BattleScene_SelectActiveController | `BattleScene_HandlePlayerPartySelection:D_8439014C*+0x6:2=0` | no | 0 |
| BattleScene_TickHudTimers -> BattleHud_DrawBattleTimer | `BattleScene_TickHudTimers:sActivePokemon*+0x7f8+0x198*+0x30+0x8:2=23` | no | 0 |

3 of 8 proposals reached their callee; 13 functions new over the baselines.

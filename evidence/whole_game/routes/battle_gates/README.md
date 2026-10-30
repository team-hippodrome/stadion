# Battle engine gates

Runs that open the battle engine's remaining reachable gates (`runs.sh <out> <base.sav>` replays all of them on
`build-evidence-all2`, game at origin/main 8410f9b), and the data proof for the rest (`anim_slots.py` writes
`../../battle_anim_slots.md`). Battle engine: **1873 → 1907 of 2061 functions** (llvm-cov over the instrumented files;
`frontier.md` counted 1868 of 2055 for the same runs), lines 81.2% → 83.6%, branches 72.1% → 74.6%.

| run | how | reaches |
|---|---|---|
| `com_hard` | `free_battle_com_hard.rec`: the Game Pak Free Battle route with one UP on the "COM's Level" dialog (frame 4000), so COM is Hard (`aiDifficulty` 0) | the AI path behind trainer flag 0x400000: `BattleAI_DecideAction`, `BattleAI_DecideFinalAction`, `BattleAI_BuildAllCandidateMoveScores`, `BattleAI_RefineAllCandidateMoveScores`, `BattleAI_ComputeAttackTargetScore`, `BattleAI_ScoreAttackOptionDoubles`, `BattleAI_ResolveTiedActionChoice`, `BattleAI_SelectRandomUsableMoveSlot` |
| `six_check` | `free_battle_six_check.rec` (`--natural`): six entries and six picks in order select (B, C-left, C-up, C-right, A, C-down), then in the first turn B (POKéMON) and R (Check) | `BattleHud_DrawDoublesTeamStatusList`, `BattleHud_DrawDoublesRosterRow` (a side with more than three chosen: Anything Goes lets a lone trainer pick its whole entry, `BattleMenu_IsOrderSelectionComplete`) |
| `party_menu` | `free_battle_party_menu.rec` (`--natural`): B, the active Pokémon's own button (C-up), A, L | `BattleScene_QueuePartySelectionConfirmMessage` ("already out"), `BattleScene_SelectActiveController` (L cancels) |
| `wrap` | Game Pak party: lead Arbok with Leer for COM; the player Toxic once, then Wrap (`--moves 92,35,0,0:1,20,0,0`) | `BattleScene_PhaseAttackHitRecoil` (phase 35: a poisoned Pokémon that cannot act, `usedMove` 0xFF while trapped, takes its residual damage) |
| `fly` | leads with Toxic only for COM; the player Fly (`--moves 19,0,0,0`) | `BattleScene_AdvanceSendOutFollowupCamera` (send-out step 10: the replacement for a Pokémon that fainted from poison while airborne, runtime flag 0x200), `BattleScene_TickQueueList3Selector2OrCFromB10` |
| `subrest` | COM lead with Sonic Boom only; the player Substitute, Rest, then Splash | `BattleAnim_TrySetModelEventTrackFromRow` (the substitute breaks while the Pokémon sleeps: its model reloads with the sleep flag set), `BattleScene_ApplyModelAnimationSlot5A` |
| `parrest` | COM leads with Thunder Wave and Tackle; the player Splash x3, then Rest | `Battle_ApplyStatStageMultiplier` (Rest on a statused Pokémon with `D_8438AC60[0]` 1 restores the stats) |
| `mv12`, `mv60`, `mv100`, `mv154`, `mv155` | the battle_xcheck battle (seed 7) with only Guillotine, Psybeam, Teleport, Fury Swipes, Bonemerang | the effect slots only those scripts name (start 80, 86, 123, 124; end 68, 83, 88) and their callbacks: the batch4 sweep gave four moves at once and these never played |
| `burn7` | the same battle with only Fire Blast | `func_8434EFBC` (start slot 128, list 4 row 6: the burn residual after a burned Pokémon attacks) |

Two-trainer sides (Free Battle "1P & 2P"; `sessionTeams->unk_01 == 2`) are not covered: `BattleScene_GetOpposingSelectedSideTeamSize`
needs one.

What stays unreached (154), from `../../battle_anim_slots.md` and `tools/evidence/deadcode.py`:

| | functions |
|---|---|
| no reference at all (no caller, address or table; no ROM word) | 39 |
| referenced only by those | 44 |
| only unselectable animation slots or never-entered scene state rows | 34 |
| values no code produces (phases 33/34, turn-camera steps 12-15, camera modes 0/3, the pause flag, default draws cleared before drawing) | 14 |
| reachable or not yet decided | 23: six AI scorers behind further trainer flags and states, nine particle draw callbacks in `gBattleAnimParticleDescriptors` (no descriptor-index proof yet), `BattleScene_GetOpposingSelectedSideTeamSize` (two-trainer side), `BattleAnim_MarkParticleInactive`, `BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset`, `BattleAnim_SpawnBatchedParticles`, `BattleScene_IntroStepNop`, `func_84321D34`, `func_843453E4`, `Port_XcheckApplyCaseParties` (port-only) |

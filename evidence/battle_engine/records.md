# Evidence records (reviewed)

Verdicts: confirmed 18, unclear 7, name wrong 1.

## Battle_CheckCriticalHit — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:1183`
- **Comment:** (none)
- **Static callers:** func_84372670
- **Runtime callers:** func_84372670 ×695
- **Runtime path (most frequent):** BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_CheckCriticalHit
- **Scenario:** seed 7, turn 1 (Charmander Flamethrower vs Machop); port: queue Battle_Random#0 = 20/28/31/34/35/36/81; ares: case 'rule Battle_Random@Battle_CheckCriticalHit 34' and '... 35'
- **Observed:** Forced roll < 35 gives a critical hit (pre-random damage 190, 'Critical hit!', Machop left at 0 HP shown as 142 damage), >= 35 none (damage 100 -> 90). Threshold 35 = ((base + 76) >> 2) * 2 >> 1 (Stadium rules, D_8438AC60[0] == 1) with base 65 = Charmander's base SPEED; base Defense 43 would give 29 (31 and 34 would not crit). Focus Energy's flag (unk_4D & 4, set by Battle_Effect_FocusEnergy) is read here.
- **ares:** same: roll 34 crit (damage 142), 35 no crit (90); ares's caller-tagged trace shows Battle_Random call 0 coming from Battle_CheckCriticalHit; all 3 events identical
- **Notes:** Name confirmed. Separate naming bug found: it reads gBaseStats[id].baseDefense, which (gBaseStats = gSpeciesData - 0x1C, PokemonStats in gb_data.h) is SpeciesData.baseSpeed. The PokemonStats stat field names are in reverse order (baseSpecial/baseSpeed/baseDefense/baseAttack at 0x07..0x0A read Attack/Defense/Speed/Special). The STAT_* ids used with these fields (gb_data.c) may carry the same reversal: worth its own record.

## Battle_ApplyDamageRandomFactor — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:257`
- **Comment:** (none)
- **Static callers:** func_84372670
- **Runtime callers:** func_84372670 ×636
- **Runtime path (most frequent):** BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ApplyDamageRandomFactor
- **Scenario:** seed 7 turn 1, same as the crit runs (watched)
- **Observed:** Draws Battle_Random until >= 217 (0xD9): rolls 64, 13, 104 rejected, 232 used; gBattleDamage 100 -> 90 and 190 -> 172 (= d * 232 / 255). Runs before the accuracy check.
- **ares:** same calls (callers named Battle_ApplyDamageRandomFactor), same values and damage

## Battle_Random — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:132` (has `PORT:` changes)
- **Comment:** (none)
- **Static callers:** Battle_ApplyDamageRandomFactor, Battle_CheckCriticalHit, Battle_CheckMonCanAct, Battle_DetermineMoveOrder, Battle_Effect_Bide, Battle_Effect_Confuse, Battle_Effect_ConfuseChance, Battle_Effect_Disable, Battle_Effect_Flinch, Battle_Effect_LowerStat, Battle_Effect_Metronome, Battle_Effect_MultiHit, Battle_Effect_Poison, Battle_Effect_SecondaryStatusChance, Battle_Effect_Sleep, Battle_Effect_Thrash, Battle_Effect_Trap, Battle_QueueMetronomeMoveMessage, func_8437114C
- **Runtime callers:** Battle_ApplyDamageRandomFactor ×3542, Battle_CheckCriticalHit ×642, Battle_Effect_SecondaryStatusChance ×79, Battle_CheckMonCanAct ×46, Battle_Effect_Thrash ×30, Battle_Effect_MultiHit ×19
- **Runtime path (most frequent):** BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ApplyDamageRandomFactor → Battle_Random
- **Scenario:** all runs (forced stream)
- **Observed:** u8 battle random number; 8 runtime callers: damage factor (457), crit (77), secondary-effect chance (20), Battle_CheckMonCanAct (6), MultiHit, Flinch, Thrash, LowerStat. Forcing a value changes exactly the decision of its caller (crit, burn).
- **ares:** same call order and callers over 492 logic entries in seed 1
- **Notes:** PORT: the xcheck hook can replace the value; the N64 one steps a generator by osGetCount.

## Battle_RandomAccuracy — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:154` (has `PORT:` changes)
- **Comment:** (none)
- **Static callers:** func_843708CC
- **Runtime callers:** func_843708CC ×715
- **Runtime path (most frequent):** BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → func_843708CC → Battle_RandomAccuracy
- **Scenario:** seed 7 with queue Battle_RandomAccuracy#0 = 254, and #0 = 255, #1 = 0
- **Observed:** Only caller func_843708CC. A 255 draw is redrawn (two calls in the same frame).
- **ares:** same: two calls (255, then 0) from func_843708CC

## func_843708CC — unclear (proposed: `Battle_CheckMoveHit`)

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:620`
- **Comment:** (none)
- **Static callers:** Battle_Effect_Confuse, Battle_Effect_Counter, Battle_Effect_Disable, Battle_Effect_LeechSeed, Battle_Effect_LowerStat, Battle_Effect_Mimic, Battle_Effect_Paralyze, Battle_Effect_Poison, Battle_Effect_Sleep, func_84372670
- **Runtime callers:** func_84372670 ×650, Battle_Effect_LeechSeed ×45, Battle_Effect_LowerStat ×12, Battle_Effect_Sleep ×8, Battle_Effect_Counter ×6, Battle_Effect_Paralyze ×4
- **Runtime path (most frequent):** BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → func_843708CC
- **Scenario:** (a) seed 7, accuracy forced 254, and 255 then 0; (b) port-only: Charmander's moves set to Confuse Ray, Hypnosis, Recover, Hypnosis (PORT_EVID_MOVES)
- **Observed:** The hit check. A roll >= moveCache.unk_04 (accuracy after stages) makes the move miss via func_843708A0. 255 is redrawn, so 100%-accuracy moves (unk_04 = 255) never miss: forced 254 still hit (damage 90). Real misses: Machop's Fire Blast (gBattleDamage 16 -> 0 inside this call), Charmander's first Hypnosis (no status). Reached from func_84372670 for damaging moves and from the status-move handlers (Battle_Effect_Sleep, _Confuse, _LeechSeed, _Paralyze, _Counter ...) for status moves: one function, two kinds of paths. Also fails moves on a target in Fly/Dig (unk_4D & 0x10) and Dream Eater (effect 8) on a non-sleeping target.
- **ares:** forced 254 / 255,0: identical events (no miss), same redraw
- **Notes:** Proposed name: Battle_CheckMoveHit (or Battle_RollAccuracy). The removed 1/256 miss is a Stadium rule change worth a comment.

## func_843708A0 — unclear (proposed: `Battle_SetMoveMissed`)

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:614`
- **Comment:** (none)
- **Static callers:** func_843708CC
- **Runtime callers:** func_843708CC ×79
- **Runtime path (most frequent):** BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → func_843708CC → func_843708A0
- **Scenario:** as func_843708CC (Fire Blast miss, Hypnosis miss)
- **Observed:** gBattleDamage = 0, sBattleMoveFailed = 1, clears user unk_4C bit 0x20. Only caller func_843708CC (11 runtime calls).
- **ares:** not isolated (no miss forced in ares with a < 100% move)
- **Notes:** Proposed name: Battle_SetMoveMissed. Port-only evidence for the miss itself.

## func_84372670 — unclear (proposed: `Battle_ExecuteMove`)

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:1441`
- **Comment:** (none)
- **Static callers:** Battle_ResolveMonAction
- **Runtime callers:** Battle_ResolveMonAction ×868
- **Runtime path (most frequent):** BattleScene_UpdateFrame → BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670
- **Scenario:** all runs
- **Observed:** Executes one Pokemon's move: only caller Battle_ResolveMonAction (126); runtime callees in order: Battle_CheckMonCanAct, Battle_DecrementPP, Battle_QueueMoveCategoryUsedMessage, Battle_Effect_Counter, Battle_CalcDamage, Battle_CheckCriticalHit, Battle_ApplyTypeEffectAndSTAB, Battle_ApplyDamageRandomFactor, func_843708CC, Battle_ExecuteMoveEffect, Battle_ApplyRageContinuation.
- **ares:** the per-event battle state after each call of its parent matches (seeds 1 and 7)
- **Notes:** Proposed name: Battle_ExecuteMove.

## Battle_Effect_Counter — name wrong (proposed: `Battle_TryCounter`)

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:1149`
- **Comment:** (none)
- **Static callers:** func_84372670
- **Runtime callers:** func_84372670 ×695
- **Runtime path (most frequent):** BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_Effect_Counter
- **Scenario:** all runs; port-only: Counter injected in slots 1-3 (--moves 68,68,68,53)
- **Observed:** Called for every move (83 times from func_84372670, every species/move), not through gMoveEffectHandlers. Returns 1 at once unless the used move is Counter (0x44); for Counter it doubles the last damage when the target's move was Normal/Fighting. With Counter (injected): the last Normal/Fighting hit is doubled: Karate Chop 50 -> 100, Mega Kick 68 -> 136, Thrash 33 -> 66, Double Kick 8 -> 16; then func_843708CC rolls its accuracy.
- **ares:** same per-event state
- **Notes:** Not an effect handler: the gate that handles Counter before damage. Proposed: Battle_TryCounter (returns 1 = 'not Counter, continue normally'). Counter branch reached only with injected moves (port-only).

## Battle_RunMoveEffect — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:1127`
- **Comment:** (none)
- **Static callers:** Battle_ExecuteMoveEffect
- **Runtime callers:** Battle_ExecuteMoveEffect ×508
- **Runtime path (most frequent):** BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect
- **Scenario:** all runs, plus the two move-injection runs
- **Observed:** Dispatches gMoveEffectHandlers[effect - 1]: 18 different handlers seen at runtime (SecondaryStatusChance, LeechSeed, Poison, Paralyze, MultiHit, Flinch, LowerStat, RaiseOwnStat, Recoil, Thrash, TwoTurnMove, ReflectLightScreen, Confuse, Sleep, Heal, Haze, FocusEnergy, Substitute). The watched state change of each call is the handler's (e.g. Recover HP 60 -> 123).
- **ares:** event states identical in seeds 1, 7
- **Notes:** Battle_RunMoveEffect is inlined into Battle_ExecuteMoveEffect: call-site addresses name the wrapper, the shadow stack names the real parent.

## Battle_ExecuteMoveEffect — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:1136`
- **Comment:** (none)
- **Static callers:** Battle_ApplyDamage, func_8436FDBC, func_84372670
- **Runtime callers:** func_84372670 ×504, func_8436FDBC ×4
- **Runtime path (most frequent):** BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect
- **Scenario:** all runs
- **Observed:** Wrapper of Battle_RunMoveEffect. Runtime caller only func_84372670 (78); the static callers Battle_ApplyDamage and func_8436FDBC never called it in these battles.

## Battle_Effect_SecondaryStatusChance — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:202`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×150
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_SecondaryStatusChance
- **Scenario:** seed 7; port queue Battle_Random#5 = 0; ares case 'rule Battle_Random@Battle_Effect_SecondaryStatusChance 0'
- **Observed:** Flamethrower's 10% burn: roll 0 (< 0x1A) burns Machop: status 0 -> 0x10, Attack 98 -> 49 (through func_84370B44), message 'It was burned!'. The natural roll 48 does nothing.
- **ares:** identical events 0-9 (burn, halved Attack, burn damage); the first difference is the next Flamethrower, which the ares rule burns as well and the port queue does not

## func_84370B44 — unclear (proposed: `Battle_ApplyBurnAttackPenalty`)

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:690`
- **Comment:** (none)
- **Static callers:** Battle_ApplyRageStatBoost, Battle_Effect_LowerStat, Battle_Effect_RaiseOwnStat, Battle_Effect_SecondaryStatusChance, func_84370B7C
- **Runtime callers:** func_84370B7C ×255, Battle_Effect_RaiseOwnStat ×11, Battle_ApplyRageStatBoost ×3, Battle_Effect_LowerStat ×2
- **Runtime path (most frequent):** BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → BattleScene_InitializeSwitchActions → func_84373570 → func_84370B7C → func_84370B44
- **Scenario:** burn run
- **Observed:** Halves Attack (BattleMonRuntime.unk_2A, min 1) when status & 0x10 (burn): 98 -> 49 when called from Battle_Effect_SecondaryStatusChance. Runtime callers: func_84370B7C (31, stat recompute at switch-in) and Battle_Effect_RaiseOwnStat (6); static also LowerStat, SecondaryStatusChance, Battle_ApplyRageStatBoost.
- **ares:** the halved Attack (0x31) is in the matching event 2
- **Notes:** Proposed: Battle_ApplyBurnAttackPenalty. Its twin func_84370B0C quarters Speed (unk_2E) on paralysis (status & 0x40); reached from Battle_Effect_Paralyze but not watched.

## Battle_ApplyResidualEffects — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:1040`
- **Comment:** (none)
- **Static callers:** Battle_ResolveMonAction
- **Runtime callers:** Battle_ResolveMonAction ×868
- **Runtime path (most frequent):** BattleScene_UpdateFrame → BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → Battle_ApplyResidualEffects
- **Scenario:** burn run
- **Observed:** After the burned Machop's move: 'It's hurt by the burn!' and its HP 52 -> 44 (142 / 16 = 8). The call itself writes the HP to show (unk_0E = unk_10 = 44); hp changes when the HP bar has moved (next event).
- **ares:** identical (events 4-5)
- **Notes:** BattleMonRuntime.unk_0E / unk_10: pending HP for the HP-bar transition (naming candidates).

## Battle_Effect_Confuse — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:783`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×3
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_Confuse
- **Scenario:** port-only injection: Confuse Ray in slot 1 (Charmander, later Venusaur)
- **Observed:** target unk_4C |= 0x80 (confused), unk_50 = confusion turns 3 and 5 ((Battle_Random & 3) + 2); scene unk_48 = 22 (presentation cue). Calls func_843708CC first.
- **ares:** not run (the injected moves are port-only)
- **Notes:** port-only evidence

## Battle_Effect_Sleep — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:60`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×9
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_Sleep
- **Scenario:** port-only injection: Hypnosis in slots 2 and 4
- **Observed:** First Hypnosis missed (func_843708CC from Battle_Effect_Sleep, no change); the second: target status 0 -> 3 (sleep turns in bits 0-2). Venusaur's later Hypnosis: 0 -> 3 again.
- **Notes:** port-only evidence

## Battle_Effect_Heal — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:996`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×3
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_Heal
- **Scenario:** port-only injection: Recover in slot 3
- **Observed:** Charmander HP 60 -> 123 (+ max HP 126 / 2); scene unk_48 = 21. The Rest branch (full HP, sleep 2, Battle_ApplyStatStageMultiplier) was not reached.
- **Notes:** port-only evidence

## Battle_Effect_RaiseOwnStat — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:354`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×26
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_RaiseOwnStat
- **Scenario:** port-only injection: Swords Dance (also natural: Hitmonlee's Meditate in seed 1)
- **Observed:** stage_atk 7 -> 9 and Attack 83 -> 166 (Charmander), 103 -> 206 (Venusaur); calls func_84370B44 to re-apply the burn penalty.
- **Notes:** stage 7 = neutral; +2 for Swords Dance as on the Game Boy

## Battle_Effect_Haze — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:952`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×2
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_Haze
- **Scenario:** port-only injection: Haze after Swords Dance
- **Observed:** stage_atk 9 -> 7 and Attack 166 -> 83 (the user's own boosts reset).
- **Notes:** port-only evidence

## Battle_Effect_FocusEnergy — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:734`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×2
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_FocusEnergy
- **Scenario:** port-only injection: Focus Energy
- **Observed:** user unk_4D |= 4, no other change; Battle_CheckCriticalHit reads that bit (Stadium rule: threshold * 4 + 160 instead of the Game Boy's halving).
- **Notes:** names the field: unk_4D bit 2 = focus energy

## Battle_Effect_Substitute — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:830`
- **Comment:** (none)
- **Static callers:** Battle_RunMoveEffect [table gMoveEffectHandlers]
- **Runtime callers:** Battle_RunMoveEffect ×1
- **Runtime path (most frequent):** BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_Substitute
- **Scenario:** port-only injection: Substitute
- **Observed:** HP 44 -> 13 (max 126 / 4 = 31), unk_59 = 31 (the substitute's HP), unk_4D |= 0x10; Venusaur: 60 -> 21, unk_59 = 39.
- **Notes:** names the fields: unk_59 = substitute HP, unk_4D bit 4 = has substitute

## Battle_ResolveMonAction — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:1809` (has `PORT:` changes)
- **Comment:** (none)
- **Static callers:** Battle_ActSecondAttacker, Battle_StartTurn
- **Runtime callers:** Battle_StartTurn ×492, Battle_ActSecondAttacker ×376
- **Runtime path (most frequent):** Battle_FrameCallback → BattleScene_UpdateFrame → BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction
- **Scenario:** all runs
- **Observed:** Resolves one side's action per call: called by Battle_StartTurn for the first attacker (70) and Battle_ActSecondAttacker for the second (56); calls func_84372670, Battle_ApplyResidualEffects, Battle_CheckFaintAfterMove.
- **ares:** event states at its entry and return identical in seeds 1, 7
- **Notes:** has PORT: lines (the xcheck event hooks only)

## BattleScene_IsActionSelectionTimerExpired — unclear

- **File:** `src/fragments/battle_engine/battle_engine_2F74E0.c:167` (has `PORT:` changes)
- **Comment:** (none)
- **Static callers:** BattleScene_HandlePlayerActionInput, BattleScene_HandlePlayerForcedActionState
- **Runtime callers:** BattleScene_HandlePlayerActionInput ×951, BattleScene_HandlePlayerForcedActionState ×110
- **Runtime path (most frequent):** BattleScene_UpdateFrame → BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdateIntroCameraSequence → BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → BattleScene_CollectPlayerAction → BattleScene_HandlePlayerActionInput → BattleScene_IsActionSelectionTimerExpired
- **Scenario:** all runs (PORT_XCHECK_COM)
- **Observed:** Queried by BattleScene_HandlePlayerActionInput (140) and BattleScene_HandlePlayerForcedActionState (14); with the xcheck COM answer the game picks the first usable move and first healthy replacement.
- **Notes:** PORT-changed: in these runs the port's hook decides the answer, so the run says nothing about the original timer logic. Needs a run with real input timing (or ares with the hook off).

## Battle_ApplyStatStageMultiplier — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_35DF70.c:46`
- **Comment:** (none)
- **Static callers:** Battle_Effect_Heal
- **Runtime callers:** Battle_Effect_Heal ×2
- **Runtime path (most frequent):** BattleScene_UpdateActionSelectPhase → BattleScene_UpdateActionCollection → BattleScene_CollectActions → Battle_StartTurn → Battle_ResolveMonAction → func_84372670 → Battle_ExecuteMoveEffect → Battle_RunMoveEffect → Battle_Effect_Heal → Battle_ApplyStatStageMultiplier
- **Scenario:** port-only: Charmander's slot 1 = Rest; forced at the first Battle_CheckMonCanAct: status = 8 (poison), HP = 50 (--set 'Battle_CheckMonCanAct#1:gBattleUser*+0x15:1=8;...+0x0C:2=50')
- **Observed:** Rest while poisoned: HP 50 -> 126, status 8 -> 2 (sleep 2 turns), party status copy unk_16 = 2, and Battle_ApplyStatStageMultiplier runs twice (Attack from unk_38 with stage unk_5C[0], Speed from unk_3C with unk_5C[2]): it recomputes a stat from its unmodified value and stage (clearing the burn/paralysis penalties). Values unchanged here (neutral stages, no penalty).
- **Notes:** Reached only with a forced status (memory set): the condition is a status before Rest, which a normal battle reaches through poison/burn/paralysis plus Rest. A run where the penalty was active (paralysed + Rest) would show the value change.

## func_84370ADC — unclear (proposed: `Battle_SyncStatusToParty`)

- **File:** `src/fragments/battle_engine/battle_engine_359F90.c:673`
- **Comment:** (none)
- **Static callers:** BattleScene_ActionSetupFinalizeStep, BattleScene_EndStatusAnimationsAndCommit, BattleScene_Phase14, BattleScene_Phase17, BattleScene_Phase27, BattleScene_Phase28, BattleScene_Phase29, BattleScene_Phase30, BattleScene_Phase31, BattleScene_Phase38, BattleScene_Phase7, BattleScene_RunActionEffectHpTransition, BattleScene_RunBasicHpTransition, BattleScene_RunConditionalHpTransition, BattleScene_RunMoveEffectEndHpTransition, BattleScene_RunRepeatedHitHpTransition, Battle_ResolveMonAction
- **Runtime callers:** BattleScene_ActionSetupFinalizeStep ×63830, Battle_ResolveMonAction ×1736, BattleScene_RunActionEffectHpTransition ×563, BattleScene_Phase7 ×43, BattleScene_RunRepeatedHitHpTransition ×21, BattleScene_RunBasicHpTransition ×15
- **Runtime path (most frequent):** Battle_Main → Battle_FrameCallback → BattleScene_UpdateFrame → BattleScene_Dispatch → BattleScene_Update → BattleScene_UpdatePhase → BattleScene_Phase2 → BattleScene_ActionSetupFinalizeStep → func_84370ADC
- **Scenario:** all runs
- **Observed:** Called from 9 places at runtime (BattleScene_ActionSetupFinalizeStep 8159, Battle_ResolveMonAction 252, BattleScene_RunActionEffectHpTransition 76, phases 7/17 ...). By the code it copies the active Pokemon's status to its party entry (BattleMon.unk_05); that party memory is not in the captured state.
- **Notes:** Proposed: Battle_SyncStatusToParty. Needs the TeamRoster party entries in the watch dump to confirm.

## BattleAnim_ApproachS16 — unclear

- **File:** `src/fragments/battle_engine/battle_engine_2F6930.c:191`
- **Comment:** (none)
- **Static callers:** (none)
- **Runtime:** not reached
- **Scenario:** dead code (no caller in the C code, its address in no ROM word): direct call at the first Battle_StartTurn (run.py --call ... --call-buf 0a00), buffer s16 = 10
- **Observed:** (&10, target 20, step 2) -> 30; step 1 -> 20; step 0 -> 10: *a += (target - *a) * step with an integer step, i.e. it scales the difference; it only 'approaches' for a fractional step.
- **Notes:** Never called in the shipped game. Either the original took a float step (the decomp's s16 type would be wrong) or the name is: a direct call cannot tell which. Same for BattleAnim_ApproachU16 ((&10, 5, 3) -> 0xFFFB).

## BattleAnim_Atan2sFromPoints — confirmed

- **File:** `src/fragments/battle_engine/battle_engine_2F6930.c:54`
- **Comment:** (none)
- **Static callers:** (none)
- **Runtime:** not reached
- **Scenario:** dead code; direct call at the first Battle_StartTurn with float arguments
- **Observed:** (0,0 -> 1,1) returns 8192 and (0,0 -> 0,1) 16384: the binary angle (0x10000 = 360 degrees) of the vector from (arg0, arg1) to (arg2, arg3), through MathUtil_Atan2s.
- **Notes:** Never called in the shipped game; port-only direct call.


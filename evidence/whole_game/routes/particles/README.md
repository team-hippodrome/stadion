# Particles and battle animation: what the data selects

The 80 live functions of `particle_math` (25), `particle_data_library` (2) and `battle_engine` (53) in
`../../dead_classes.md`: 75 are code the shipped game cannot run, 5 are left live. The proof is
`../../particle_descriptors.md`, written by `particle_descriptors.py`; the names are in
`../../data_dead_particles.json` (the form of `data_dead.json`, for `tools/evidence/dead_classes.py`).

```
python3 -I evidence/whole_game/routes/particles/particle_descriptors.py <cg_all4.json> <front20/frontier.json> \
  <US ROM> evidence/whole_game/data_dead.json evidence/whole_game/particle_descriptors.md \
  evidence/whole_game/data_dead_particles.json <scratchpad holding batch4..15 and the other runs>
```

## How the 75 fall

- **Particle descriptors.** `gBattleAnimParticleDescriptors` (85) and `gParticleDescriptorChildren` (57) in
  `battle_engine_315D50.c` are the only particle descriptor tables in the decomp; the minigame particles (`MiniFx_*`)
  have none. A descriptor is selected only by a literal `&gBattleAnimParticleDescriptors[N]` in code (300 literals
  in 168 functions, no other use of either table). The ROM agrees: the battle fragment sits uncompressed in the ROM
  (the descriptor table is at 0x370A90, its child pointers are aligned words there), and no aligned ROM word equals
  any descriptor's address. 56 descriptors are held by code that ran, 4 only by dead code, 25 by nothing. The draw /
  texture callbacks that only those 29 name are dead: `ParticleGfx_DrawBillboardPrim`, `..._DrawBillboardOffsetQuad`,
  `..._DrawBillboardPrimEnvQuadVariantB`/`C`, `..._LoadTextureCallbackI4ParticleFrame_32x64`,
  `..._LoadTextureCallbackIa8Frame_32x64`, nine `BattleAnim_DrawAsset*` / `BattleAnim_DrawParticleNoop`, and what
  only they call (`ParticleGfx_DrawTransformedQuadXYScaled` and its matrix helpers).
- **Callbacks nothing registers.** The `BattleAnim_Callback*` motion callbacks above the live `Setup*` / `Motion*` /
  `_InitParticle` functions (`...Unused*Variant`, `...Descriptor26/50BurstEmitter`, `...OwnerModelTintPaletteApproach*`,
  `...UnregisteredStraightDirectionalStreamFade`, `...UnusedGenericFadeApproach`) have no reference in the decomp and
  no ROM word with their address. They count as "reached" in the frontier only because `dead_calls.md` called each
  once directly (`runs4/dead_calls`, inside batch4's profile): that is how 434 functions nothing references, plus 30
  that ran only below such a call, sit in the frontier's reached set. Everything below them (the `Setup*`,
  `ParticleMath_RandomDirection`, `ParticleMath_AngleXBetweenPoints`, `ParticleGfx_RecolorPrimRandomPalette`, ...) is
  dead.
- **No reference at all.** `ParticleGfx_LoadTextureRgba16WrapT`, `ParticleGfx_LoadTextureTileIa16`,
  `ParticleMath_BitwiseXor`, `ParticleMath_BitwiseAndNot`, `Particle_AddVelocityFromRotationTemplate2` (and
  `Particle_Field7C_SetVecFromField74AnglesXZ_0B330` below it): `deadcode.py`'s ROM test kept them because their
  address bytes occur in the ROM, but only at unaligned offsets (inside compressed data), never as a pointer.
  `DisplayList_BuildIndexedTextureSegment` / `DisplayList_BuildTwoSegmentChain`: their only callers are forced-call
  only.
- **Call sites no value reaches** (checked on the source by the script): slot mode 18 is never passed
  (`BattleAnim_SpawnBatchedParticles`); the six-slot trail is always spawned in mode 0
  (`BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset`); the descriptor-0x19 recolor callback is always registered
  with variant 0 or 1 (`ParticleGfx_RecolorPrimEnvFromPaletteSetB`); chained particles need descriptor 0 or 67, which
  nothing holds, and the minigame emitters are never activated (`Particle_AllocChain`, and `MiniFx_TryEmit`,
  `MiniFx_EmitParticles`, `MiniFx_ApplyEmitterToParticle`, `MiniFx_FreeEmitter` of `minigame_actor_framework`); the
  intro sequence goes from step 9 to 11 (`BattleScene_IntroStepNop`); send-out step 4 never leaves step 5 current
  (`func_84321D34`).
- **Battle scene values** (`../../battle_anim_slots.md`, "Values no code produces", re-checked: no `BattleScene*`
  other than `sBattleScene.unk_00` exists, so those fields have no other writer): phase 33/34
  (`BattleScene_PhaseCheckFaintedEndTurn`, `BattleScene_PhaseSendOutAfterFaint`, `BattleScene_CompleteSendOutFollowup`),
  turn-camera steps 12-15 (`BattleScene_IntroCameraStep12Or14`, `BattleScene_IntroCameraWideHoldStep`,
  `BattleScene_SetCameraSendOutShot`), camera modes 0 and 3 (`func_843297E8`, `func_843297F0`, the two
  `BattleScene_FinalPresentationFrame*`), the pause flag (`BattleScene_UpdatePausedFrame`: `sIsPaused` is only ever
  assigned 0; pressing Start in battle does not reach it), the default draws.

  So the battle situations the task named do not select these: phase 33 is not a double knockout, Explosion,
  Self-Destruct or recoil faint (those go through the faint / send-out phases that ran in the sweeps); no value of
  the scene's phase field is 33, and nothing pauses the scene.

## Left live (5)

| function | what selects it | tried |
|---|---|---|
| `BattleScene_GetOpposingSelectedSideTeamSize` | a side with two trainers (`battleSides[i].unk_01 == 2`: Free Battle's team options, e.g. 1P+2P vs COM, `free_battle_rules.c` `D_84002BE8[1]`), then a faint (`Battle_CheckFaintAfterMove` -> `func_84373018`), a status move against that side (`Battle_HasUsableSleepStatusTarget` / `...MajorStatusTarget`) or the residual-damage defeat check | no route yet: needs a two-controller Free Battle recording (pads 2-4 exist in the recording format) |
| `BattleAnim_MarkParticleInactive` | effect 62 (Spike Cannon, move 131) marks a stream's particle inactive once the stream is older than 40 frames; the effect's update never finishes by itself, the move ends it | every playback ended first: 19 with the battle_xcheck Charmander (`mv131`, 20-21 frames), 75 with Cloyster and Omastar from a Game Pak (`fb_spike`, 21-26 frames, the first stream starts on the 4th); not proven by data, timing |
| `func_843453E4` | the node callback `func_84345448` in mode 2 (the node drawn while visible); Ice Punch's (move 8) effect model sets it | `mv8`: set 7 times (mode 0), never drawn; the node's visibility is in the model's ROM data (not decoded) |
| `BattleAI_AllCandidatesResistType` | the COM scoring a move of effect 0x27 / 0x2B (charge / trapping moves) with damage, when `D_843C60BC != 0` | not tried |
| `BattleAI_ScoreCategorySetReady` | the COM's Substitute-style case 0x4F with one usable candidate and a set-up move found | not tried |

`runs.sh` repeats the runs (`OUT=<dir> BASE=<gbv4/base.sav> sh runs.sh`); their coverage is in the scratchpad
(`par3_particles/runs/`).

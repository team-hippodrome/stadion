# The sequence player's commands: which ones the shipped data uses

The game's sound engine plays songs and sound effects with libmus (`game/src/libnumus/player.c`). Each voice reads a
command stream: `Audio_DecodeVoiceCommandStream` takes every byte >= 0x80 as a command and calls
`command_func_jumptable[byte & 0x7F]` (43 handlers, opcodes 0x80-0xA9 and 0xB0); a byte < 0x80 is a note, followed by
a velocity byte while `velocity_on` is set and by a 1-2 byte duration unless `fixed_length` is set (and `ignore` was
not set for this one note). A handler runs only if a stream holds its opcode, so the shipped streams decide which
handlers the game can run.

## Where the streams are

Every song and FX bank is a Yay0 blob in the ROM's audio region (`gAudioData`, ROM 0x15C0000-0x1FEC050, its size in
the first word), loaded through `Audio_CopyDataWithCacheSync` (Yay0) and then used as a `song_t` (song: per channel a
command stream) or an `fx_t` (FX bank: per entry a command stream). `routes/audio_seq/audio_banks.py` follows
`Audio_InitBanks` and every loader that picks a file by id:

| loader | blobs | entries |
|---|---|---|
| music files (`Audio_StartMusicTrack`, `Audio_LoadWaveTablesAndMusic` -> `D_800FC680`) | 76 songs | 6-16 channels |
| system FX (`D_800FC688`: `SFX_TITLE_START`, `Audio_PlayPersistentSoundCommand`, `Audio_PlaySoundEffectById`) | 1 | 73 |
| move sounds (`func_800479C0` -> `D_800FC698[3]`) | 166 | |
| species sounds (`func_80048060` -> `D_800FC6B0[3]`) | 151 | |
| `Audio_PlayCommand` bank (`D_800FC6F4[0]` -> `D_800FC6A8`) | 1 | 52 |
| categories 2-A (`Audio_DispatchSoundBankCommand`, `D_800FC6F8[0..8]` -> `D_800FC6A8`) | 9 | 12, 6, 8, 16, 14, 10, 11, 26, 29 |
| direct (`Audio_PlayDirectSoundCommand`, `D_800FC6F4[2]`) | 1 | 8 |
| category 11 (`Audio_PlayCategory11SoundCommand`, `D_800FC6F4[3]`) | 1 | 40 |
| category 12 (`Audio_PlayCategory12SoundCommand`, `D_800FC6F4[4]`) | 1 | 11 |

76 songs and 331 FX banks. Scanning the other way, every Yay0 blob of the audio region (576): 76 parse as songs and 331
as FX banks, exactly the loaders' set (`audio_banks.py` prints both and their difference: none). The other 169 are 17
sample banks (`N64 PtrTables V2`) and 152 per-species move-sound index tables (`MoveSoundTable`, 0x3F4 bytes, read by
`func_80048060`), which hold no command streams. FX streams start other entries (0xA7, 0xB0) by index into the same
bank, so those are among the streams decoded.

## Decoding

`routes/audio_seq/decode_seq.py ROM` walks every stream (3077: song channels and FX entries) along every path: both
ways out of each finite `for`/`next` loop (0x95/0x96; a count of 0xFF loops forever, so its `next` never falls
through), goto targets (0xA1), and the state that changes the note layout (`velocity_on`: 0x99 / 0x9A / 0x9B;
`fixed_length`: 0x8B; `ignore`: 0x8C). Argument lengths are the handlers' own (`player.c`). All 3077 streams decode to
their end (0x80) or to their endless loop with no unknown byte and without leaving the blob. The US ROM and its
revisions 1 and 2 give the same result.

| opcode | handler | argument bytes | commands | songs | FX banks |
|---|---|---|---|---|---|
| 0x80 | `Audio_ClearVoicePlaybackState` | 0 | 2466 | 24 | 331 |
| 0x81 | `Audio_SetVoiceSampleIndex` | 1-2 | 27549 | 76 | 271 |
| 0x82 | `Audio_EnableVoicePitchSlide` | 1 | 1169 | 73 | 0 |
| 0x83 | `Audio_DisableVoicePitchSlide` | 0 | **0** | 0 | 0 |
| 0x84 | `Audio_SetVoiceVolumeEnvelope` | 7 | 1766 | 0 | 271 |
| 0x85 | `Audio_SetVoiceSequenceTempo` | 1 | 279 | 76 | 7 |
| 0x86 | `Audio_SetVoiceFixedDuration` | 2 | 3 | 2 | 0 |
| 0x87 | `Audio_SetVoiceReleaseDuration` | 1 | 1408 | 76 | 0 |
| 0x88 | `Audio_EnableVoiceVibrato` | 3 | 192 | 7 | 20 |
| 0x89 | `Audio_EnableVoiceVibratoDown` | 3 | **0** | 0 | 0 |
| 0x8A | `Audio_DisableVoiceVibrato` | 0 | 739 | 53 | 0 |
| 0x8B | `Audio_SetVoiceDurationOverride` | 1-2 | 5752 | 70 | 0 |
| 0x8C | `Audio_EnableVoiceVariableDuration` | 0 | **0** | 0 | 0 |
| 0x8D | `Audio_SetVoiceDetune` | 1 | 888 | 55 | 0 |
| 0x8E | `Audio_ApplyVoiceDetuneOnce` | 0 | **0** | 0 | 0 |
| 0x8F | `Audio_SetVoiceFinePitch` | 1 | 585 | 52 | 0 |
| 0x90 | `Audio_SetVoiceEnvelopeFromTable` | 1-2 | 11869 | 76 | 0 |
| 0x91 | `Audio_SetVoiceFlagED` | 0 | **0** | 0 | 0 |
| 0x92 | `Audio_ClearVoiceFlagED` | 0 | **0** | 0 | 0 |
| 0x93 | `Audio_SetVoiceFlagEE` | 0 | **0** | 0 | 0 |
| 0x94 | `Audio_ClearVoiceFlagEE` | 0 | **0** | 0 | 0 |
| 0x95 | `Audio_PushVoiceLoopState` | 1 | 611 | 52 | 14 |
| 0x96 | `Audio_PopVoiceLoopState` | 0 | 611 | 52 | 14 |
| 0x97 | `Audio_SetVoicePeriodicPitchModulation` | 3 | 635 | 52 | 20 |
| 0x98 | `Audio_DisableVoicePeriodicPitchModulation` | 0 | **0** | 0 | 0 |
| 0x99 | `Audio_EnableVoiceVelocityFromStream` | 0 | 5656 | 76 | 0 |
| 0x9A | `Audio_DisableVoiceVelocityFromStream` | 0 | 5350 | 76 | 0 |
| 0x9B | `Audio_SetVoiceDefaultVelocity` | 1 | 2789 | 76 | 0 |
| 0x9C | `Audio_SetVoicePan` | 1 | 18928 | 76 | 178 |
| 0x9D | `Audio_SkipVoiceCommandWord` | 2 | **0** | 0 | 0 |
| 0x9E | `Audio_SetVoiceSubEnvelopePointer` | 1 | **0** | 0 | 0 |
| 0x9F | `Audio_ClearVoiceSubEnvelopePointer` | 0 | **0** | 0 | 0 |
| 0xA0 | `Audio_SkipVoiceCommandByte` | 1 | **0** | 0 | 0 |
| 0xA1 | `Audio_SetVoiceEnvelopePointers` | 6 | **0** | 0 | 0 |
| 0xA2 | `Audio_SetVoiceFxMix` | 1 | 11420 | 76 | 266 |
| 0xA3 | `Audio_SetRandomVoiceDetune` | 2 | 4 | 0 | 4 |
| 0xA4 | `Audio_SetRandomVoiceVolume` | 2 | **0** | 0 | 0 |
| 0xA5 | `Audio_SetRandomVoicePan` | 2 | 7 | 0 | 2 |
| 0xA6 | `Audio_SetVoiceVolume` | 1 | 2283 | 0 | 271 |
| 0xA7 | `Audio_AllocateSubVoiceFromCommand` | 1-2 | 196 | 0 | 52 |
| 0xA8 | `Audio_SetVoiceVolumeScale` | 1 | 585 | 52 | 0 |
| 0xA9 | `Audio_SetVoicePanModulation` | 1 | 91 | 1 | 2 |
| 0xB0 | `Audio_AllocateVoiceFromCommand` | 1-2 | 756 | 0 | 173 |

(commands: occurrences over all decoded paths; 0xAA-0xAF are empty slots of the table.)

**15 handlers no shipped stream uses**: `Audio_DisableVoicePitchSlide`, `Audio_EnableVoiceVibratoDown`,
`Audio_EnableVoiceVariableDuration`, `Audio_ApplyVoiceDetuneOnce`, `Audio_SetVoiceFlagED`, `Audio_ClearVoiceFlagED`,
`Audio_SetVoiceFlagEE`, `Audio_ClearVoiceFlagEE`, `Audio_DisableVoicePeriodicPitchModulation`,
`Audio_SkipVoiceCommandWord`, `Audio_SetVoiceSubEnvelopePointer`, `Audio_ClearVoiceSubEnvelopePointer`,
`Audio_SkipVoiceCommandByte`, `Audio_SetVoiceEnvelopePointers`, `Audio_SetRandomVoiceVolume`. With the shipped data
the game cannot run them (`data_dead_audio.json`).

## The other audio leftovers

- `Audio_ApplyVoicePanCommand`, `Audio_ApplyVoiceFxMixCommand`: API commands 5 and 6 of `api_func_jumptable`, run by
  `Audio_ProcessCommandQueue` for a command byte in the queue. The queue holds only what the queue functions write
  (each writes its own byte and length, each handler consumes exactly that): byte 5 is written only by
  `Audio_QueueVoicePan`, which nothing calls (referenced by nothing); no function writes byte 6 (the queue writers
  are commands 0, 1, 2, 3, 4, 5, 7, 8, 9, 0xA, 0xB). Dead.
- `n_alSynSetFXMix`: `Audio_PlayerFrameHandler` calls it when `ptr->reverb != ptr->reverb_mix` for a playing voice.
  When the voice starts, `reverb_mix` is set to `reverb_intensity + (128 - reverb_intensity) * reverb / 128`, which is
  `reverb` while `reverb_intensity` is 0; `reverb` changes only in the command stream, and every decode that finds a
  playing voice releases it (all three branches of `Audio_DecodeVoiceCommandStream` call `Audio_ReleaseVoice` with
  `sample` cleared), so a voice never plays on with a changed `reverb`. `reverb_intensity` is cleared by
  `Audio_ResetVoiceState` and set only by command 6 (above). Dead. (The gate: true 0 times, false 303711212 times
  over the batches.)
- `n_alFxParamHdl`: nothing in the C code refers to it (`n_alFxNew` does not install it); `dead_classes.py` keeps it
  live because its N64 address 0x800503A0 occurs once as a ROM word, at ROM 0xa6e778, which lies inside the
  compressed bytes of the Yay0 blob at ROM 0xa54408 (compressed data to 0xa7b4a5). A coincidence: dead.
- Reached in this round:
  - `Audio_CountActiveSoundsByCategory` / `Audio_CountCategorySounds` (`Widget_PauseMenuUpdate`, a tournament that
    ended: `D_8780FC4C == 1`, then the countdown `D_8780FCA6` reaches 0): `pause_end`, the mini-game tournament
    recording (`tour_mg_dynamo_audio.rec`) with sound on, **forced**: `D_8780FC4C = 1` when
    `Widget_PauseMenuCheckTournamentEnd` returns (no player reaches the round target in the recording).
  - `Audio_PortDumpChannels`: the port's channel dump, with `PORT_TRACE_ACTIVITY=1` (`system` sweep run, which calls
    `Audio_GetActivityScore`).
  - `n_alSynDelete`: below `n_alClose`, which nothing in the game calls; a forced direct call of `n_alClose` with the
    sound engine on (`al_close`, `--call-fork`: the child's profile `coverage.profraw.k0`).
- `PortA_BE16`, `PortA_BE32` (`port_audio.h`): reached (the batch15 profile: `port_audio.c:PortA_BE32` 1667585,
  `gsm_decoder.c:PortA_BE32` 1011891, `port_audio.c:PortA_BE16` 2447584, ...). They are `static inline`, so every
  translation unit has its own copy, and `frontier.py` keys statics by their bare name (`cov[name] = ...` for each
  copy, the last one wins): an unused copy (`player.c`, `gb_audio.c`, `DDC0.c`, ...) hides the reached ones. A tool
  issue, not code to reach.

## Runtime cross-check

With the sound engine on (`PORT_GAME_AUDIO=1`), every song and every FX bank entry was played by the game's own play
functions (`routes/audio_seq/`: call files from `gen_calls.py`, `runs.sh`; on the Options screen, which plays nothing
on its own). `check_runs.py` compares the handlers the runs reached with the decoded set:

| run | calls | what plays | song starts / FX starts |
|---|---|---|---|
| `music1`-`music4` | 76 | `Audio_StartMusicTrack(0..0x4B)` (file n = id n), each for one pass of its longest channel + 15% | 84 (with the Options music) / 11 |
| `system` | 484 | `Audio_PlaySoundEffectById` 0-0x60, category 11 and 12 ids, `Audio_PlayDirectSoundCommand`, `Audio_PlayCommand(0..0x60, 1 / 2)`, `Audio_DispatchSoundBankCommand` for categories 2-A | 7 / 232 |
| `moves1`, `moves2` | 1005 | `func_800479C0(move 1-165, 25, mode 0-5)` and the last bank's 15 entries | 4 / 662 |
| `species` | 587 | `func_80048060(0, entry, species 1-151, 16)`: every entry of every species bank | 2 / 589 |

Result: all 28 handlers the data uses were reached; none of the 15 it does not use ran (`mismatches: 0`), and no run
logged `[audio] bad command`. Before each play the game's scene-change fade (`Audio_FadeCategoriesAndStreams(1)`) ends
the previous sound: the category, direct and `Audio_PlayCommand` banks share one buffer (`D_800FC6A8`), and a bank
loaded under a voice that still plays hands that voice other bytes. An earlier sweep without the fade, and one with an
id past a bank's end (category 2 entry 12 of 12), crashed in `Audio_DecodeVoiceCommandStream` on such bytes (an opcode
past the table): input the game does not produce, not a port bug.

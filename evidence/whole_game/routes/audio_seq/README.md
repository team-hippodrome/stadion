# The audio sweep and the sequence data

Every song and sound effect the game ships, played with the sound engine on, and the shipped sequence data decoded
offline. The proof is in `../../audio_commands.md`; the names it proves dead are in `../../data_dead_audio.json`.
Instrumented binary `build-evidence-all2` (game at origin/main e3d3f40); ROM: the US cartridge.

| file | what |
|---|---|
| `decode_seq.py ROM [--json F]` | decompresses every Yay0 blob of the audio region, recognises songs (`song_t`) and FX banks (`fx_t`), walks every path of every command stream and counts the opcodes; prints the used and the unused handlers |
| `audio_banks.py ROM [--json F]` | every song and FX bank the game's loaders reach (`Audio_InitBanks` and the loaders that pick a file by id), with entry counts; checks it equals the decoded set |
| `audio_banks.json` | `audio_banks.py`'s output (input of `gen_calls.py`) |
| `gen_calls.py ROM audio_banks.json DIR` | writes the call files below |
| `music1.calls`-`music4.calls` | `Audio_StartMusicTrack(0..0x4B)`, each track for one pass of its longest channel + 15% + 240 frames (the opcodes 0x8A, 0x8F, 0xA8 come at the end of the first pass, 1-4 minutes in) |
| `system.calls` | system FX, categories 2-A, 11 and 12, the direct and `Audio_PlayCommand` banks, `Audio_GetActivityScore` |
| `moves1.calls`, `moves2.calls` | `func_800479C0(move, 25, mode)`: moves 1-165 in modes 0-5, the last bank's entries; 120 frames each |
| `species.calls` | `func_80048060(0, entry, species, 16)`: every entry of every species bank |
| `options.rec` | A on the title screen; with `--set "State_TitleScreen#1@exit:gCurrentGameState:4=19"` the game goes to the Options screen, which plays nothing on its own |
| `runs.sh OUT` | the eight sweep runs (`PORT_GAME_AUDIO=1`; the `system` run with `PORT_TRACE_ACTIVITY=1`) |
| `check_runs.py decode.json RUN...` | the handlers the runs reached against the decoded set |
| `tour_mg_dynamo_audio.rec` | the mini-game tournament recording that ends rounds with sound on (the parent's `mash/tour_mg_dynamo.rec`) |

Each call line fires at entry of the n-th `Audio_CreateFrameTask` (once per frame); 10 frames before each play the
game's scene-change fade (`Audio_FadeCategoriesAndStreams(1)`) ends the previous sound, since a bank loaded into a
buffer a voice still reads hands it other bytes.

Two more runs (commands from the repository root, sound on):

```bash
PORT_GAME_AUDIO=1 python3 -I tools/evidence/run.py OUT/pause_end --bin build-evidence-all2/stadion --frames 25800 --rec evidence/whole_game/routes/audio_seq/tour_mg_dynamo_audio.rec --natural --set "Widget_PauseMenuCheckTournamentEnd#1@exit:D_8780FC4C:1=1"
```

```bash
PORT_GAME_AUDIO=1 python3 -I tools/evidence/run.py OUT/al_close --bin build-evidence-all2/stadion --frames 3200 --rec evidence/whole_game/routes/audio_seq/options.rec --natural --set "State_TitleScreen#1@exit:gCurrentGameState:4=19" --call "Audio_CreateFrameTask#3000:n_alClose:v:" --call-fork
```

- `pause_end` (**forced**: the tournament-end flag where the game computes it; no player reaches the round target in
  the recording): `Widget_PauseMenuUpdate` reaches `Audio_CountCategorySounds` -> `Audio_CountActiveSoundsByCategory`.
- `al_close` (forced direct call; nothing in the game calls `n_alClose`): `n_alSynDelete`, in the forked child's
  profile `coverage.profraw.k0`.

| | before | after |
|---|---|---|
| reached (batches 4-15 / + these runs) | 6971 | 6975 |
| proven dead | 209 | 228 |
| live | 231 | 208 |
| coverage of the code the shipped game can run | 95.7% | 96.0% |

Audio left live: `PortA_BE16`, `PortA_BE32` (`port_audio.h`): reached; their unused `static inline` copies in other
files hide the reached ones in `frontier.py`, which keys statics by bare name (`audio_commands.md`).

# Smooth motion checks

Scripts used for the frame interpolator's design review (PORTING_NOTES.md, "Frame interpolation" > "Design review
(2026-11)"). They run the game headless (hidden window) from `build/pokestadium` (or `PORT_BIN`) with the ROM in
`PORT_ROM`, and need bash, a C compiler and ffmpeg.

| script | what it does |
|---|---|
| `shoot.sh <out> <stop> <interp 0\|1> [<first>-<last>] [VAR=value ...]` | one run, every picture (exact and in-between) of those game frames as `<out>/shots/interp_<frame>_<i>of<k>.ppm`, the log in `<out>/port.log` |
| `sweep.sh <out> [16:9\|4:3]` | the ceremonies (`PORT_BOOT=demo:<n>`), the Gym Leader Castle ending (`PORT_BOOT=castle_clear`), battles with sleep Zs, Psychic and Fire Blast in arenas 2, 11 (Koga) and 15 (Elite Four), and eight replays; each in-between picture scored against its neighbours (`tri.c`), the worst three per scene kept as pictures, matching statistics per scene in `<out>/summary.txt` |
| `pacing.sh <out> [trials]` | real time, 15 s of the cup-clear ceremony: freezes and frame jumps with Smooth motion on and off, on this GPU and a simulated slow one (`PORT_DEBUG_DRAW_MS`) |
| `sheet.sh <out.png> <columns> <width> <pictures...>` | a contact sheet |

Reading the results:

- `tri` score: in-between pictures of moving objects score about 0.5; 1.5 and over means something is drawn that
  neither neighbour has (a wrong partner, stretched geometry, another scene). HUD panels sliding in (2D blending)
  score 2-6 and are harmless; look at the `worst_*.png` pictures (previous, in-between, current).
- `misindex`: pairs the n-th pairing used before 2026-11 would have blended with another object (0 everywhere is not
  expected: effects that recycle their slots produce them every frame).
- `pacing.sh`: Smooth motion on should show no more freezes or jumps than off; `old25` is the old design with a slow GPU.

Keep runs small: pictures are saved at 1/8 size by default (`SCALE`), and a Retina window doubles them; a full run at
`SCALE=2` takes gigabytes. Other builds on the machine make real-time numbers noisy: compare on and off in one run.

Useful game switches (see PORTING_NOTES): `PORT_INTERP_LOG=1/2/3`, `PORT_INTERP_IMMEDIATE=1` (all pictures drawn at
once) / `late` (drawn after the game has built the next frame, the real-time worst case), `PORT_INTERP_BUDGET=0`,
`PORT_INTERP_MATCH=index` (the old pairing), `PORT_BOOT=demo:<n>` / `PORT_BOOT=castle_clear`.

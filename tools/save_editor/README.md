# Pokémon Stadium (US) save editor

Library, CLI and small web UI for the N64 *Pokémon Stadium* (US) cartridge save: a 128 KiB FlashRAM image
(`.fla` / `.flash`) in the byte order the N64 reads it, as ares stores `save.flash`. Other emulators may store
FlashRAM byte- or word-swapped; such files need converting first (`check` reports every record invalid otherwise).
The format is documented in [SAVE_FORMAT.md](SAVE_FORMAT.md). Python 3.9+, no dependencies. Players do not need
any of this: the game's launcher has the editor built in ([From the game's launcher](#from-the-games-launcher)).

Run everything from this directory (`tools/save_editor`):

```bash
python3 -m stadium_save --help
```

## Common tasks

```bash
# summary / validation
python3 -m stadium_save info -v my.fla
python3 -m stadium_save check my.fla          # exit code 1 if any record copy is not valid
python3 -m stadium_save repair my.fla -o fixed.fla   # what the game does on load: keep the valid copy, reset broken records
python3 -m stadium_save fix my.fla -o fixed.fla      # recompute POKE footers after hex editing

# a blank save identical to the one the game writes
python3 -m stadium_save new -o blank.fla

# everything as JSON, edit it, write it back (dump -> build is byte-exact)
python3 -m stadium_save dump my.fla -o my.json
python3 -m stadium_save build my.json -o my.fla

# single fields by JSON path
python3 -m stadium_save get my.fla options.progress.0
python3 -m stadium_save set my.fla options.sound=3 'teams.poke_cup.0.name="BLUE"' -o out.fla
```

Edits write the record into both flash copies with a fresh `POKE` footer and checksum, as the game's
`Save_CommitRecord` + `Save_FlushBank` do; untouched records keep their exact bytes. Without `-o` the input file is
overwritten.

```bash
# progress: all cups + Gym Leader Castle cleared, Mewtwo unlocked, GB Tower at Dodrio speed
python3 -m stadium_save unlock my.fla --round2 --tower-speed 2
# options
python3 -m stadium_save options my.fla --sound mono --voice off --kids-club-hyper on --bonus EEVEE:round2
# registered team (category: free_battle, poke_cup, petit_cup, pika_cup, prime_cup, gym_leader_castle, mewtwo, ...)
python3 -m stadium_save team my.fla set poke_cup 0 "MEWTWO:55:PSYCHIC/RECOVER/AMNESIA/BARRIER" \
    "PIKACHU:50:THUNDERBOLT/SURF@nick=SPARKY,dv=15,statexp=max" SNORLAX:55 --name ASH --trainer-id 4242
python3 -m stadium_save team my.fla clear poke_cup 0
# Pokémon Lab boxes 1..12, item boxes 1..4
python3 -m stadium_save box my.fla add 1 MEW:5 "BULBASAUR:5@ot=ASH,otid=4242"
python3 -m stadium_save items my.fla add 1 "MASTER BALL:3" "RARE CANDY:99" --name ASH --trainer-id 4242
# Victory Palace statues (mode 3 Poké Cup, 4 Petit, 5 Pika, 6 Prime, 7 Castle)
python3 -m stadium_save statue my.fla set PIKACHU --nickname SPARKY --mode 6 --round2
python3 -m stadium_save statue my.fla set all
# saved cup run ("CONTINUE")
python3 -m stadium_save continue my.fla set MEWTWO:55 PIKACHU:50 SNORLAX:55 --mode 3 --selection 0 --battle 3
python3 -m stadium_save continue my.fla clear
```

Pokémon spec: `SPECIES[:LEVEL][:MOVE/MOVE/...][@nick=X,ot=Y,otid=N,dv=0..15,statexp=N|max]`. New Pokémon get the
experience for their level, full HP and PP, the species' types and catch rate, and — without a move list — the
species' starting moves. Full control: edit the JSON (`dump`/`build`, `set`, or `team ... --json list.json`).
In the JSON each Pokémon has `species` (or `species_index` for invalid indexes), `level`, `hp`, `status`,
`type1/2`, `catch_rate`, `moves` (`move`, `pp`, `pp_up`), `ot_id`, `exp`, `stat_exp`, `dvs`, `nickname`, `ot_name`;
`_info` (dex, computed stats, ...) is informational and ignored when writing. Keys starting with `_raw`, `_tail`,
`_unused_entries`, `_backup` and `_banks` hold bytes the editor does not interpret; keep them for exact round trips.

Web UI (localhost only): `python3 -m stadium_save serve [my.fla]`, then open http://127.0.0.1:8642/ (page:
`stadium_save/editor.html`). Forms for everything the editor understands: an overview with save health (*Repair*) and
quick unlocks, progress per round (cups, divisions, Castle gyms, Mewtwo, GB Tower speed), options (sound, voice, Kids
Club Hyper, bonus Pokémon), registered teams per cup (10 slots each), Lab boxes (12 × 20) and item boxes, Victory Palace
statues, the saved cup run and gallery photo counts. Pokémon open in a dialog (species, nickname, level, trainer, four
moves with PP Ups, DVs and Stat Exp with live stats). Experience, HP, PP and types are recomputed by the server
(`/api/mon`). *Advanced* still shows each record as JSON. On saving, only the records you changed are rewritten, with
a fresh checksum in both copies (`{"base", "edited"}` → `SaveFile.edit`). Unreadable records you edit start from the
game's reset state. `serve --write` (used by the macOS launcher) adds *Save*, which overwrites the file and keeps
`<file>.bak`; otherwise the button downloads the save.

Game Boy games: `serve my.fla --write --gb "Player 1|yellow.sav|yellow.gb"` (repeatable; the ROM gives the title and
the language) also opens Pokémon Red/Blue/Yellow battery saves (`stadium_save/gbsave.py`, international layout incl.
German). Each gets a section: trainer (name, rival, ID, money, badges, Pikachu's friendship in Yellow), party, the 12
PC boxes, bag and PC items, and the Pokédex. With a Game Boy save open, *Pokémon Lab boxes* shows the cartridge next
to the Lab, and the arrow on a Pokémon moves it across, as the Lab PC does. Moves in both directions give the same
bytes as the game's own Lab PC transfer (tests, `fixtures/lab_transfer`): the nickname is re-encoded for the Lab, names
are copied up to their terminator on the cartridge, and a Lab box keeps the old bytes behind its new count. Arriving
Pokémon are marked as caught in the Pokédex, party stats are recomputed, and bank checksums are only rewritten for
banks that changed. Uninitialized PC boxes (bit 7 of 0x284C clear) show as empty and are initialized the way the
game does on the first box switch. *Save* writes every file that changed, each with its `.bak`. It refuses if a file
changed on disk since the page loaded it (for example, the game saved).

Library:

```python
from stadium_save import SaveFile, make_mon
s = SaveFile.load("my.fla")
s.edit(lambda d: d["teams"]["prime_cup"][0].update(flags=1, name="RED", trainer_id=1, entries=[make_mon("MEW", 100)]))
s.save("out.fla")
```

## From the game's launcher

The game has this editor built in, with no Python needed: tick *Debug* in the launcher, then click *Save editor*
(or *Edit save* on a Transfer Pak card for that cartridge's Game Boy save). On the handheld layout (Android) it is
under *Files & help*. It has the same pages as the web page below except the raw JSON view, works with the mouse,
the keyboard and a gamepad, and writes the save the same way (the previous file kept as `.bak`).

It is a C++ port of this library: `src/port/save_editor/stadium_save.cpp` (with `gamedata.h`, written by
`gen_data.py` from the same tables) and the pages in `save_editor_ui.cpp`. `tests/test_native.py` checks that both
give the same JSON and the same bytes:

```
cmake --build <build dir> --target stadium_save_cli
STADIUM_SAVE_CLI=<build dir>/stadium_save_cli python3 -m unittest tests.test_native -v
```

The Python package stays the developer's tool: the command line above and the web page (`serve`).

## Files

- `stadium_save/` — library (`save.py` layout/records/footers/JSON, `pokemon.py` Gen 1 records and stats,
  `text.py` Game Boy / N64 text, `schema.py` exact struct codec, `cli.py`, `web.py`, `gamedata.py` generated tables)
- `gen_data.py` — regenerates `stadium_save/gamedata.py` (names from the ROM text tables; species, move, index and
  charset tables from the decomp): `python3 gen_data.py <rom.z64> ../../../pokestadium/src stadium_save/gamedata.py`.
  It also writes `src/port/save_editor/gamedata.h` for the built-in editor (`python3 gen_data.py --cpp` writes only
  that, from the current `gamedata.py`)
- `fixtures/first_save.flash` — the save the real game writes on a blank chip (ares)
- `tests/` — `python3 -m unittest discover -s tests`
- `ares/` — `ares_save`: the `tools/ares_ref` driver plus FlashRAM load/store, `build.sh`, `run.sh`
- `verify/` — in-game verification: `cases.json` (edits + expected result), `scripts/` (controller input),
  `saves/` (edited saves and the saves the game wrote back), `shots/` (screenshots), `last_run.txt`

## Tests

```bash
python3 -m unittest discover -s tests
```

Covers: blank save == game's first save; load/save and dump/build byte identity (also on random data and random
structured records); footer/checksum rules; repair (backup copy, reset); fix; species/move/stat/exp tables; CLI
edits change only the intended records and leave both copies valid; the verified saves are reproducible from
`verify/cases.json`; saves written back by the game validate.

`tests/test_ui.py` drives the web page inside the macOS launcher. It needs "Pokemon Stadium.app" built with
`tools/make_app.sh` and is skipped otherwise. `tests/ui_test.js` runs in the editor window and uses every button and
field of every section: overview cards, quick actions and Repair, progress checkboxes and dropdowns, options, team
tabs, slots and the Pokémon dialog (species, nickname, level, trainer, moves, PP Ups, starting moves, DVs, Stat Exp,
Perfect stats / No training, Add / Done / Remove / Cancel), box navigation, item boxes, Victory Palace, the saved run,
gallery, Advanced, Discard, Save and ⌘S. The launcher answers each confirmation through its native handler
(`LAUNCHER_AUTO_CONFIRM`), first with OK and then with Cancel (nothing may change). It also answers the
unsaved-changes prompt on closing the window. The test then checks the written save with the library. It also covers
a German Yellow save (`gb/yellow_de.sav`): every Game Boy tab, and moving Pokémon between the Lab and the cartridge,
including the rule that the party keeps at least one Pokémon. Not automated:
*Open save…* and *Download save* (both open file panels, and the launcher only uses Save).

## Checking edits in the real game (ares)

```bash
tools/save_editor/ares/build.sh <work dir> [existing ares checkout to copy]   # once; same pinned ares as tools/ares_ref
ARES_SAVE_BUILD=<work dir>/build python3 verify/run_verify.py [case ...]
```

`run.sh <script> [--flash-in f] [--flash-out f]` runs any `tools/ares_ref` script with a cartridge save inserted;
the extra script line `flashsave <frame> <file>` writes the flash image the game has written so far. Each run boots
from power-on (≈1500 frames to the title), so the game reads the save exactly as on hardware.

Results of the last run (2026-10-18, US ROM, ares 9408cb4, `verify/last_run.txt`):

| case | edit | seen in the game |
|---|---|---|
| blank_chip | none, erased chip | the save the game writes on leaving Options is byte-identical to `fixtures/first_save.flash` |
| first_save | none | area select by day, no Mewtwo |
| unlock | `unlock --tower-speed 2` | night sky and Mewtwo over the Castle (`shots/unlock_area.png`) |
| options | sound mono, voice off | Options shows MONO / OFF; the save the game writes back keeps them, all records valid |
| registered_team | Poké Cup team slot 0 | "Registered Pokémon 1 Set(s)"; ASH, ID 04242, MEWTWO L55, SPARKY L50, … |
| continue | saved Poké Cup run, battle 3 | the run resumes at battle 3 with RED's MEWTWO L55 / PIKACHU L50 / SNORLAX L55 |
| victory_palace | statues for #1 and #2 | statue on No.001 with NAME BULBY, LEVEL 55, OT ASH, ID 12345, POKé CUP |

Not verified in the game: Lab boxes, item boxes and the bonus Pokémon (the Lab needs a Game Boy Game Pak in the
Transfer Pak, which the driver does not emulate), gallery photos (taking and keeping a photo needs a longer input
sequence), Kids Club flag.

## Creating a fresh save without the editor

`verify/scripts/first_save.txt` boots with an erased chip, opens and leaves Options (the game then writes every
record) and saves the flash image (`blank_chip` case above). `python3 -m stadium_save new` produces the same bytes.

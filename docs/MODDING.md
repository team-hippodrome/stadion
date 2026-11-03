# Making mods

A mod is a folder or an `.o2r` archive (a zip) in the port's `mods` folder. At start the port mounts the enabled mods on
top of its own archive, `pokestadium.o2r`: a file in a mod replaces the file with the same path. The design and the plan
for what comes next are in [MODS_CONCEPT.md](MODS_CONCEPT.md).

**What mods can change today (phase 0):** the files of `pokestadium.o2r`, the port's own resources:

| Path | What it is |
|---|---|
| `textures/launcher/*.png` | the launcher's pictures (`n64`, `pokestadium`, `red`, `blue`, `yellow`, `empty`) |
| `fonts/*.ttf` | the launcher's fonts |
| `shaders/**` | the renderer's shader sources (Metal, OpenGL, DirectX) |

The game's own data (models, textures, text, trainers) is still read from your ROM. Replacing it comes in the next
phases (texture packs by content hash, then ROM units, text and data, see the concept, section 8). The folder layout,
the manifest and the load order below stay the same.

## Where mods go

| Build | `mods` folder |
|---|---|
| developer build | `build/mods` (next to `pokestadium.cfg.json`) |
| macOS app | `~/Library/Application Support/<bundle id>/mods` |
| Linux AppImage | `$SHIP_HOME/mods` |
| Windows | `mods` next to `pokestadium.exe` |
| Android | `/sdcard/Android/data/com.teamhippodrome.pokestadium/files/mods` |

The port creates the folder at start. The launcher's **Mods** button (top right, or on the home page of the handheld
layout) shows the full path, and **Open folder** opens it.

Each entry directly in `mods` is one mod: an `.o2r` or `.zip` file, or a folder (handy while you work on a mod: edit a
file, restart, see it). Names that start with `.` or `~` are skipped; `~` is reserved for later folder kinds.

## Enable, disable, order

In the launcher, **Mods** lists every mod:

- the checkbox switches a mod on or off;
- **Up** and **Down** set the priority. **The top of the list wins**: where two mods have the same file, the one higher
  in the list is used; every mod wins over the base archive;
- **Apply & Restart** saves the list and starts the game again (mods are mounted only at start). On Android, restart
  the app yourself.

Each row shows what the mod does in this run: *replaces 1 file of the base game*, *overrides 2 files of Other Mod*,
*1 file overridden by Other Mod*, or, in red, why it was not loaded. The same report is in the log
(`logs/pokestadium.log`, and on the terminal) as lines starting with `[mods]`.

A mod that is new in the folder is switched on and put at the top. The lists are stored in `pokestadium.cfg.json` as
`gSettings.Mods.Enabled` and `gSettings.Mods.Disabled`, each `|`-separated, highest priority first.

For one run, the environment variable `PORT_MODS` decides instead: `PORT_MODS=off` loads none,
`PORT_MODS="my-mod|hello-mod.o2r"` loads exactly these, in this order. The replay and comparison tools
(`tools/replay.sh`, `tools/run_replays.py`, `tools/tour.py`) set `PORT_MODS=off` unless you set it yourself, and an
input recording notes the mods it was made with (`# mods …`); replaying it with other mods prints a warning.

## Make one

The example in [`tools/mods/examples/hello-mod`](../tools/mods/examples/hello-mod) replaces the N64 picture in the
launcher's bottom bar:

```
hello-mod/
  manifest.json
  textures/launcher/n64.png
```

1. Make a folder with the files you want to replace, at the same paths as in `pokestadium.o2r`. To see those paths,
   list the archive: `unzip -l build/pokestadium.o2r`. Replacement pictures can have any size; the launcher keeps
   their aspect ratio.
2. Add a `manifest.json` (optional, but without it the mod is listed under its file name):

   ```json
   {
     "name": "Hello Mod",
     "author": "you",
     "version": "1.0.0",
     "description": "One line about what it changes.",
     "license": "CC0-1.0",
     "pokestadium": {
       "format": 1,
       "min_port_version": "0.0.2",
       "requires": [],
       "conflicts": [],
       "exclusive": false
     }
   }
   ```

   `requires` and `conflicts` name other mods by their manifest name or file name. A mod whose requirement is not
   loaded stays off; of two mods that conflict, the lower one stays off; an `exclusive` mod only loads alone. A
   `game_version`, if given, must be `2432030131` (`0x90F5D9B3`, Pokémon Stadium USA 1.0, the only ROM the port runs).
   Mods cannot contain code: a manifest with `main` or `binaries` is refused (gameplay changes will be built into the
   port and switched on by mods, see the concept, section 5.5).
3. Try it as a folder: copy it into `mods`, start the game, look at the bottom bar.
4. Check it without starting the game: `PORT_MODS_CHECK=1 ./pokestadium` (in `build/`) mounts the mods, prints the
   report with every file that more than one archive has (`textures/launcher/n64.png: hello-mod over base`), and exits
   with 1 if an enabled mod could not be loaded.
5. Pack it for others:

   ```bash
   python3 tools/mods/pack.py tools/mods/examples/hello-mod build/mods/hello-mod.o2r --base build/pokestadium.o2r
   ```

   `pack.py` checks the manifest and, with `--base`, leaves out files that are identical to the base archive's.

Only put your own work into a mod. Never include ROM data or other files you may not share.

## For port developers

- Loader: `src/port/mods.cpp` (`Port::Mods::Mount`, called from `Port::Init` right after `pokestadium.o2r` is mounted).
  libultraship's `ArchiveManager` gives a path to the archive added last, so the list is mounted bottom to top.
- UI: `src/port/launcher.cpp` (`ModsPanel`). `PORT_LAUNCHER_MODS=1` opens it at start (for screenshots with
  `PORT_LAUNCHER_SHOT`).
- Tests: `python3 tools/mods/test_mods.py build/pokestadium` builds fixture mods in a temporary folder and checks the
  priority rule (for `PORT_MODS` and the stored list, archives and folders), `off`, disabled mods, and every manifest
  rule above.

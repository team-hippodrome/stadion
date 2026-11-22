# stadion

A native N64 game port for Windows, macOS, Linux and Android, with local multiplayer,
Transfer Pak support and a built-in Game Boy Tower. Built by Team Hippodrome on
[Team Hippodrome's libultraship fork](https://github.com/team-hippodrome/libultraship).

[Download](https://github.com/team-hippodrome/pokestadium-port/releases/latest) ·
[Report an issue](https://github.com/team-hippodrome/pokestadium-port/issues) ·
[Build from source](docs/BUILDING.md) ·
[Discord](https://discord.com/invite/8EvuQxbxu8)

## Community

Join the [Project Hippodrome Discord](https://discord.com/invite/8EvuQxbxu8) for
project discussion and help with stadion.

## Quick start

stadion does not include game data. You must provide your own supported ROM dump.
Transfer Pak and Game Boy Tower features also require your own compatible Game Boy
ROMs and saves.

### 1. Verify your ROM

The supported base ROMs are the **US releases** (1.0, Rev 1 and Rev 2), in **big-endian
`.z64` format**, 32 MiB (33,554,432 bytes), with one of these SHA-1s:

| Release | SHA-1 |
| --- | --- |
| USA 1.0 | `ed7bef5a306f88c0a6e96b15e71fee2ef32058f3` |
| USA Rev 1 (1.1) | `488bb6d112427823d77fcecdd9297fd86875263d` |
| USA Rev 2 (1.2), retail or kiosk | `0d3b1d740c6ee6da6923550bf24ad827262fd8c0` |

Whichever you use, stadion runs the Rev 2 game code, which fixes the earlier releases' bug with
nicknames that contain a space; the ROM provides the game's data (graphics, text,
teams). The launcher verifies the dump before starting. Other regions, patched ROMs and
byte-swapped `.n64` or `.v64` files are not accepted yet.

### 2. Download and launch

Choose the package for your system from the
[latest release](https://github.com/team-hippodrome/pokestadium-port/releases/latest).
Current downloads and executables use the `pokestadium` filename prefix.

| Platform | Package | Start |
| --- | --- | --- |
| Windows x64 / ARM64 | ZIP for your architecture | Extract into a writable folder, then run `pokestadium.exe`. |
| macOS Apple Silicon | ARM64 ZIP | Extract into a writable folder, then open the included app. |
| Linux x86_64 | AppImage | Make it executable, then launch it. Requires glibc 2.39 or newer and system OpenGL drivers. |
| Android ARM64 | APK | Install the APK, then open the app. Requires Android 10 or newer; current releases are debug-signed. |

On macOS, the app is not notarized. If macOS blocks it, use **System Settings →
Privacy & Security → Open Anyway**. On Linux without FUSE, launch the AppImage with
`--appimage-extract-and-run`.

### 3. Select your files and play

In the launcher, select your base ROM. Optionally use **Import save** to bring in
an existing N64 cartridge save, and choose a Game Boy ROM and battery save for each
player's Transfer Pak. Select **PLAY** to start; your choices are remembered.

Use **Files & help → Check setup** to check your ROMs, save files and writable
folders. Without an imported N64 save, stadion creates a fresh one.

## Configuration

### Controllers and local multiplayer

Use **Controls** in the launcher to map a keyboard or controller for each player.
Player 1 is enabled by default. Enable **Plugged in** for players 2–4 to make the
corresponding multiplayer modes available.

| N64 input | Keyboard | Controller |
| --- | --- | --- |
| A / B | X / Z | A / B |
| Z | Left Shift or Space | Either trigger |
| Start | Return | Start |
| L / R | Q / E | Shoulder buttons |
| D-pad | Arrow keys | D-pad |
| C-buttons | I / K / J / L | Right stick |
| Analog stick | W / A / S / D | Left stick |

### Display and graphics

**Screen** selects 4:3 or 16:9. Widescreen shows more of 3D scenes at the sides;
windows with a different aspect ratio use black bars.

**Renderer** selects from the graphics backends included in your build: DirectX 11,
OpenGL, Metal or Vulkan. Restart to apply a change. Vulkan requires a compatible
driver; availability depends on the platform and build. On Windows, **Restart and
apply** reopens the launcher with the selected renderer.

**Graphics** opens the drawing settings; changes apply at once and are kept:
- **Display**: windowed or fullscreen (borderless), applied when the game starts.
  F11 switches in-game.
- **Internal resolution**: the pixels the game is drawn with. **Match window** (the
  default) draws every pixel of the window, Retina and other HiDPI displays included.
  **Native** is the N64's own 320 x 240 (426 x 240 in 16:9); **2x** to **6x** are
  multiples of it; **480p** to **2160p** are fixed heights. Each entry shows its size.
  Lower is much faster on integrated graphics.
- **Scaling**: how a fixed resolution is stretched to the window. **Integer** uses the
  largest whole multiple that fits (sharp, even pixels, black borders), **Sharp** fills
  the window without blurring, **Smooth** fills it with linear filtering. Not used with
  Match window. Picking Native or a multiple switches it to Integer.
- **Anti-aliasing**: MSAA off, 2x, 4x or 8x. It costs speed on weak chips.
- **VSync**: on stops tearing.
- **Smooth motion**: in-between frames for 3D scenes. Doubles the drawing work.
- **Keep full speed**: when the graphics chip cannot keep up, frames are skipped so
  the game and its sound keep their speed instead of slowing down and stuttering.

Two presets light up when the settings match them: **Recommended** (Match window, 4x
anti-aliasing, VSync, Smooth motion and Keep full speed on; the default for a new
configuration) and **Low-power** (for chips such as Intel HD 520: 2x, Smooth scaling,
anti-aliasing and Smooth motion off, VSync and Keep full speed on). A smaller window
helps as well, and DirectX or OpenGL can be faster than the other on some chips.

**Sound** opens the sound settings; changes apply at once and are kept:
- **Master**, **Music**, **Sound effects**, **Voices** (Pokémon cries and the announcer)
  and **Game Boy** (the GB Tower) volumes.
- **Sound**: on or off. **In the background**: keep playing or mute while another
  window is in front.
- **Sound delay**: short, normal or long. Long helps when the sound crackles on a slow
  PC; it applies when the game starts.

Stereo or mono is set in the game's own Options menu.

Compiled shaders are saved in `shadercache/` beside the settings for DirectX 11,
Vulkan, and OpenGL drivers that support program binaries. Later launches reuse
matching compiler output. Invalid entries are rebuilt automatically. For
diagnostics, `PORT_TRACE_SHADER_CACHE=1` logs cache hits and writes, and
`PORT_NO_SHADER_DISK_CACHE=1` disables this cache.

`PORT_TRACE_RENDER_SPIKES=1` reports frames exceeding 50 ms with shader, texture
upload, draw, and presentation timings. `PORT_RENDER_SPIKE_MS` sets the threshold.
Use `PORT_TRACE_RENDER_TIMING=1` for the full per-frame timing trace.
The optional `shader_cache_check` build target tests corrupted and truncated
entries, concurrent writes, and unavailable storage. Run it with an empty scratch
directory as its argument.

For a GPU copy regression check, configure an evidence build with
`-DCMAKE_PROJECT_INCLUDE="$PWD/cmake/evidence.cmake"`
`-DPORT_EVID_FILES="(game_state.c|battle_prep_)"`, then run
`python3 tools/rtt_check.py <output> --bin <binary> --backend opengl`
(or `metal`, `vulkan`, `directx`). It displays the castle's defeated-trainer card
and checks the preserved portrait target against its original RGBA16 pixels, including alpha.
`PORT_CHECK_RTT_INIT=1` enables the same readback diagnostics during other runs.

### Transfer Pak and Game Boy Tower

Select a compatible Game Boy cartridge ROM and battery save for each player in the
launcher. Transfer Pak features use these files for cartridge access; the Game Boy
Tower runs compatible cartridges through the built-in Game Boy core. Use a separate
save file for each player. **Check setup** reports file and save-size problems before
you start.

## Saves and settings

On desktop, `pokestadium.cfg.json`, logs and the default `saves/` folder live beside
the launcher: the executable on Windows, the app bundle on macOS, or the AppImage on
Linux. Keep these in a writable folder. Move the config and `saves/` with the launcher
to keep your settings and progress. `SHIP_HOME` overrides the data folder.

Android uses `/sdcard/Android/data/com.teamhippodrome.pokestadium/files`.
Files selected through its picker are copied into the app's `imported/` folder.

**Import save** accepts raw 128 KiB N64 cartridge saves (`.flash`, `.fla`, `.sra`,
`.sav` or `.bin`), including recognized byte-swapped emulator dumps. It creates a
working copy in `saves/` and preserves the original. Emulator save states and
Controller Pak files are not supported cartridge saves.

**Files & help** can open the settings and saves folders and export an N64 save.
Android exports also offer the system **Save as** picker.

Save writes replace the active file atomically. Before the first overwrite in each
session, the previous save is backed up in `<save filename>.backups/`; the five newest
session snapshots are kept. To restore one, close the game, keep a copy of your
current save, then copy the chosen `.bak` over the active save file.

## Help and recovery

If **Auto-play** skips the launcher, hold **Shift** while starting, or run:

```sh
pokestadium --launcher
```

If graphics initialization fails, run `pokestadium --safe-mode` to reopen the
launcher with OpenGL, or Metal on macOS. Windows commands use `pokestadium.exe`.
`PORT_FORCE_LAUNCHER=1` and `PORT_SAFE_MODE=1` provide the same recovery options.

Use **Files & help → Export diagnostics** when reporting a problem. Review the
report before attaching it to an
[issue](https://github.com/team-hippodrome/pokestadium-port/issues), along with your
release version, platform, renderer and steps to reproduce.

## Development

See [Building stadion](docs/BUILDING.md) for platform dependencies, packaging,
repository layout and the game-patch workflow. [Porting notes](PORTING_NOTES.md)
cover implementation details, validation tools and debug options.

To start a battle directly (no menus), for example to compare frames with another renderer:
`tools/battle_boot.py --out out --p1 25,4,7 --p2 113 --known2 1,34 --arena 2 --moves 85,85 --seed 1 --turns 2 --frames-every 2 --trace --camlog`
writes a case file, runs the port hidden and leaves the frames and logs in `out/`; `pokestadium <rom> --battle <case>` takes a
case file directly. Format and details: [Battle boot](PORTING_NOTES.md#battle-boot-straight-into-a-battle-from-a-case-file-toolsbattle_bootpy).

## Acknowledgments

- The [decompilation contributors](https://github.com/michiiik/pokestadium).
- The [libultraship contributors](https://github.com/Kenix3/libultraship).
- [Harbour Masters](https://github.com/HarbourMasters) for the porting framework
  and tooling used by this project.

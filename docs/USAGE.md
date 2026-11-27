# Using stadion

Detailed setup, configuration and recovery notes. For the overview, see the [README](../README.md).

## Quick start

stadion does not include game data. You must provide your own supported ROM dump.
Transfer Pak and Game Boy Tower features also require your own compatible Game Boy
ROMs and saves.

### 1. Verify your ROM

The supported base ROMs are the **US and European releases** (English, French, German, Spanish and Italian), in
**big-endian `.z64` format**, each 32 MiB (33,554,432 bytes):

| Release | SHA-1 |
| --- | --- |
| USA 1.0 | `ed7bef5a306f88c0a6e96b15e71fee2ef32058f3` |
| USA Rev 1 (1.1) | `488bb6d112427823d77fcecdd9297fd86875263d` |
| USA Rev 2 (1.2) | `0d3b1d740c6ee6da6923550bf24ad827262fd8c0` |
| Europe (English) 1.0 | `6f26e3c8b35c3232058375e30d19665facb23123` |
| Europe (English) Rev 1 | `9470f899ad0107677b7dfd24e1dd567723a5f84f` |
| France | `4f7267ccaba4a8a9b0ae2cb89e191bc69b153ddc` |
| Germany | `6a25903e733f071e7d6ea54529b7fd60b760cda3` |
| Spain | `f39911ad0bedccc3e44d4338507c73bfa97856bf` |
| Italy | `db5f2797c377fa8957b97465fc5738d6999ff240` |

The launcher verifies the dump before starting. Unlisted revisions and regions,
patched ROMs, and byte-swapped `.n64` or `.v64` files are not accepted.

#### Restored content (optional extra ROMs)

Under **Enhancements → Restored content** the launcher accepts up to three more ROMs of other Stadium releases you
own. They are only read for content the loaded ROM lacks:

- **Gallery**: the French, German, Spanish and Italian releases have no Gallery. With an English (US or European) ROM
  beside them, the Gallery opens with that ROM's graphics and your release's text.
- **Jynx's colour**: *Original* (USA 1.0, Japan) or *Purple* (later releases), when ROMs with both are present.
- **Nidoqueen's animation**: *International* or *Japanese*, when a Japanese ROM is present.

### 2. Download and launch

Choose the package for your system from the
[latest release](https://github.com/team-hippodrome/stadion/releases/latest).
Current downloads and executables use the `stadion` filename prefix.

| Platform | Package | Start |
| --- | --- | --- |
| Windows x64 / ARM64 | ZIP for your architecture | Extract into a writable folder, then run `stadion.exe`. |
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

**Files & help** checks your ROMs, save files and writable folders when it opens
(**Check again** repeats it). Without an imported N64 save, stadion creates a fresh one.

## Configuration

### Controllers and local multiplayer

Controllers are detected automatically. Each one gets its own player in the order
they were connected: the first is player 1 (on a handheld such as the AYN Thor, its
built-in controls), the second player 2, and so on; the keyboard always plays player 1.
A player with a controller is plugged in for the game, so the multiplayer modes appear
without any setup. Controllers SDL knows (Xbox, PlayStation, Switch Pro, 8BitDo and
most others) work with the default buttons; the Controls panel names them as printed
on the controller (Cross and L1 on a PlayStation pad, for example).

Use **Controls** in the launcher to change a player's buttons. Its header shows the
player's controller with a light that shows button presses; **Change** gives the
player the next connected controller (the player who had it takes this one's place),
and the choice is remembered. A player without a controller can still be marked
**Plugged in**. A connected controller that SDL does not recognize is announced at the
bottom of the panel: **Set it up** asks you to press each button in turn and saves the
result to `gamecontrollerdb.txt` in the data folder, so it is recognized from then on.

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
  F11 switches in-game. On Android the game is always full screen, and this row is
  **Screen** (4:3 or 16:9) instead.
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

On desktop, `stadion.cfg.json`, logs and the default `saves/` folder live beside
the launcher: the executable on Windows, the app bundle on macOS, or the AppImage on
Linux. Keep these in a writable folder. Move the config and `saves/` with the launcher
to keep your settings and progress. `SHIP_HOME` overrides the data folder.

Android uses `/sdcard/Android/data/com.teamhippodrome.stadion/files`.
Files selected through its picker are copied into the app's `imported/` folder.

**Import save** accepts raw 128 KiB N64 cartridge saves (`.flash`, `.fla`, `.sra`,
`.sav` or `.bin`), including recognized byte-swapped emulator dumps. It creates a
working copy in `saves/` and preserves the original. Emulator save states and
Controller Pak files are not supported cartridge saves.

**Files & help** shows the active N64 save (size, last change, backups kept) with
**Save editor** and **Export save**, and where the settings and saves are; on desktop
it can open those folders. Android exports also offer the system **Save as** picker.

Save writes replace the active file atomically. Before the first overwrite in each
session, the previous save is backed up in `<save filename>.backups/`; the five newest
session snapshots are kept. To restore one, close the game, keep a copy of your
current save, then copy the chosen `.bak` over the active save file.

## Help and recovery

If **Auto-play** skips the launcher, hold **Shift** while starting, or run:

```sh
stadion --launcher
```

If graphics initialization fails, run `stadion --safe-mode` to reopen the
launcher with OpenGL, or Metal on macOS. Windows commands use `stadion.exe`.
`PORT_FORCE_LAUNCHER=1` and `PORT_SAFE_MODE=1` provide the same recovery options.

Use **Files & help → Export diagnostics** when reporting a problem. Review the
report before attaching it to an
[issue](https://github.com/team-hippodrome/stadion/issues/new/choose), along with your
release version, platform, renderer and steps to reproduce.

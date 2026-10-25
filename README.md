# Pokémon Stadium (US) — native PC port (macOS, Linux, Windows)

A native port of Pokémon Stadium (N64, US) built from the decompilation on top of
[libultraship](https://github.com/Kenix3/libultraship) (Metal on macOS, OpenGL on Linux, DirectX 11 or OpenGL on
Windows), with the Transfer Pak and the GB Tower
(Game Boy emulation) working, including German Pokémon Yellow.

No game data is included. You need your own ROM of *Pokemon Stadium (USA)*
(SHA-1 `ed7bef5a306f88c0a6e96b15e71fee2ef32058f3`) and, for the Transfer Pak / GB Tower, your own Game Boy ROMs
and saves.

## Run

The [releases](https://github.com/team-hippodrome/pokestadium-port/releases) have ready-to-run builds; started without
arguments, each shows the launcher (below):

- **macOS** (Apple Silicon): `pokestadium-macos-arm64-v<version>.zip` holds `Pokemon Stadium.app`. It is not
  notarized: on the first start macOS refuses to open it; allow it under System Settings → Privacy & Security → "Open
  Anyway", or run `xattr -dr com.apple.quarantine "Pokemon Stadium.app"`. Settings, logs and default saves:
  `~/Library/Application Support/com.team-hippodrome.pokestadium`.
- **Linux** (x86_64; glibc 2.39 or newer: Ubuntu 24.04, Debian 13, Fedora 40, SteamOS 3 and later):
  `pokestadium-linux-x86_64-v<version>.AppImage`, a single file. Make it executable (`chmod +x`, or the file manager's
  Properties) and start it. It needs the system's OpenGL driver (libOpenGL, part of Mesa / libglvnd) and runs on X11 or
  Wayland. Settings, logs and default saves: `~/.local/share/pokestadium` (`$XDG_DATA_HOME/pokestadium`; or wherever
  `SHIP_HOME` points). Without FUSE (some containers), run it with `--appimage-extract-and-run`.
- **Windows** (x64, or arm64 for Windows on ARM): unpack `pokestadium-windows-<arch>-v<version>.zip` into a folder of
  its own and start `pokestadium.exe`. Settings, logs and default saves are kept in that folder (the current folder,
  when started from a console elsewhere; it must be writable, so not under `Program Files`). SmartScreen may warn about an unknown publisher: "More info" → "Run anyway".
  Log output: start it from a console (`pokestadium.exe` in cmd/PowerShell writes there), redirect it
  (`pokestadium.exe 2> log.txt`), or set `PORT_CONSOLE=1` for a console window of its own.

Renderer: `Window.Backend` in `pokestadium.cfg.json` (Id 1 DirectX 11, 2 OpenGL, 3 Metal, 4 Vulkan). Vulkan (Windows
and Linux builds; macOS only when built with `-DLUS_ENABLE_VULKAN=ON` against Homebrew's `molten-vk`,
`vulkan-loader` and `glslang`) is used only where a Vulkan driver is installed, otherwise the default renderer. It is
work in progress: shaders are compiled when first used, so new scenes can stutter the first time.

## Build

The game code needs clang (for Windows: llvm-mingw); GCC and MSVC are not supported.

```bash
tools/setup.sh                      # submodules + the port's patches (patches/libultraship.patch, patches/game/)
```

**macOS** (Homebrew):

```bash
brew install cmake ninja sdl3 glew libzip nlohmann-json tinyxml2 spdlog
cmake -S . -B build -G Ninja
cmake --build build
python3 tools/make_macos_bundle.py build "Pokemon Stadium.app" 0.0.2   # optional: self-contained app
```

`tools/make_macos_bundle.py` builds a double-clickable `Pokemon Stadium.app` with the Homebrew libraries inside (the
release download is this app; see Run).

**Linux** (Ubuntu 25.04 or newer, for SDL3; other distributions: the same packages):

```bash
sudo apt install clang lld cmake ninja-build python3 libsdl3-dev libglew-dev libgl-dev libzip-dev libbz2-dev \
  zlib1g-dev nlohmann-json3-dev libtinyxml2-dev libspdlog-dev glslang-dev
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

This build links the distribution's shared libraries and keeps its settings in the current directory. The release
AppImage is built differently, so that it runs on other distributions too (see the `linux` job in
`.github/workflows/build.yml`): on Ubuntu 24.04, with SDL3 built from source and libzip, spdlog, tinyxml2 and
nlohmann-json from vcpkg (`x64-linux`), all linked statically along with the C++ runtime
(`-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"`), then packed by
`python3 tools/make_appimage.py build pokestadium.AppImage <version> <path to appimagetool>`. The script refuses
executables that link anything besides glibc and OpenGL.

**Windows** (cross-compiled from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), as the CI does; its
`bin` directory on `PATH`, vcpkg in `$VCPKG_ROOT`):

```bash
$VCPKG_ROOT/vcpkg install sdl3 glew libzip nlohmann-json tinyxml2 spdlog zlib bzip2 glslang --triplet x64-mingw-static
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$PWD/cmake/toolchain-mingw-clang.cmake
cmake --build build-win
```

For Windows on ARM: `MINGW_TRIPLE=aarch64-w64-mingw32` and the `arm64-mingw-static` triplet (with llvm-mingw's
`bin` on `PATH` for the vcpkg install too, or vcpkg may build glslang with a GCC mingw that does not link). `pokestadium.exe` is a
window-only program (no console window; see Run for its log output) with the icon and version information of
`src/port/pokestadium.rc.in`.

**Android** (64-bit ARM, Android 10 or newer; tested target: AYN Thor). Needs JDK 17 or 21, the Android SDK with NDK
29.0.14206865, platform 36 and build-tools 36 (`sdkmanager "platforms;android-36" "build-tools;36.0.0"
"ndk;29.0.14206865"`), CMake and Ninja. `android/local.properties` names the SDK and the CMake to use (`sdk.dir=...`,
`cmake.dir=/opt/homebrew` with Homebrew's CMake):

```bash
cd android && ./gradlew assembleDebug    # app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The app is `android/` (SDL's Java activity, from the SDL version libultraship fetches) around the game built from
`CMakeLists.txt` as `libmain.so`, with the OpenGL ES 3 renderer. Config, logs and saves live in the app's folder,
`/sdcard/Android/data/com.teamhippodrome.pokestadium/files`; files picked in the launcher (ROMs, saves) are copied into
its `imported/` folder, since Android's file picker hands out content links rather than paths.

The version (Windows file version, AppImage, `Info.plist`) is the `project()` version in `CMakeLists.txt` unless
`-DPORT_VERSION=<version>` says otherwise. `.github/workflows/build.yml` builds Linux, macOS and Windows (x64, arm64);
`.github/workflows/release.yml` builds them with the version it is given, tags the commit `v<version>` and `latest`,
and publishes a GitHub release. Both run by hand from the Actions tab.

Start `pokestadium` (`pokestadium.exe` on Windows) without arguments to get the launcher (all platforms): pick the
Stadium ROM and save and, per player, a Game Boy cartridge and save for the Transfer Pak, then PLAY. The choices are
remembered (`pokestadium.cfg.json`); Auto-play skips the launcher next time (hold Shift while starting to see it again),
and Debug adds a save-editor button (needs python3). Screen picks the picture's shape, 4:3 (the N64's) or 16:9
(widescreen: 3D scenes show more at the sides); a window of another shape gets black bars (`PORT_ASPECT=4:3|16:9`
overrides it). With a ROM path as argument the game starts right away:
`cd build && ./pokestadium "/path/to/Pokemon Stadium (USA).z64"`.
Useful environment variables (all in PORTING_NOTES.md): `PORT_FLASH_FILE` (Stadium save), `PORT_GB_ROM` /
`PORT_GB_SAVE` (Transfer Pak cartridge of player 1; `PORT_GB_ROM2..4` for the others), `PORT_SOUND=0` (no sound
output; it is on by default at normal speed).

Sound: the game's music, sound effects, Pokémon cries and the announcer play through SDL3; the N64 audio microcode they
need is emulated in `src/port/audio_hle.cpp`.

Controls (the same defaults as [pokemonStadiumRecomp](https://github.com/michiiik/pokemonStadiumRecomp)); change them
per player with the launcher's Controls button:

| N64 | Keyboard | Controller |
|---|---|---|
| A / B | X / Z | A / B |
| Z | Left Shift or Space | either trigger |
| Start | Return | Start |
| L / R | Q / E | shoulder buttons |
| D-pad | arrow keys | D-pad |
| C-buttons | I / K / J / L | right stick |
| Stick | W / A / S / D | left stick |

## Layout

- `game/` — submodule: the decompiled game (C), unmodified; the port's changes are in `patches/game/` (see below)
- `src/port/` — host layer: OS/threads, VI, controllers, sound (audio microcode, AI), Transfer Pak, flash saves, GB core
- `src/port/sdk/` — stand-ins for the N64 SDK headers the game includes (`ultra64.h`, `PR/*.h`, force-included `port_compat.h`)
- `libultraship/` — submodule; the port's renderer changes are in `patches/libultraship.patch`
- `ghostship-ref/` — submodule (fonts used by `tools/make_o2r.py`)
- `tools/` — launcher, save editor, ares comparison tooling (`tools/tour.py`), replay helpers
  (`tools/run_replays.py` plays all stored recordings without showing the window and reports which still pass;
  `tools/frame_sweep.py` captures a stretch of the game at 5 pictures per second, skipping repeats, as contact sheets)
- `replays/` — input recordings used as regression tests
- `PORTING_NOTES.md` — what was done, how it was checked against ares, debug switches

## The game submodule

`game/` is an unmodified commit of the decompilation, [michiiik/pokestadium](https://github.com/michiiik/pokestadium)
(`master`). Everything the port changes in it (64-bit pointers, big-endian ROM data, sound, Game Boy, port hooks) is
kept here as patches in `patches/game/`, one per area, and `tools/setup.sh` (and CI) applies them with
`tools/game_patches.py apply`. Only `game/src`, `game/include` and the libultra files listed in
`cmake/ultralib_sources.txt` are compiled; the rest of the decomp (asm, assets, tools) is unused.

To change game code: edit files in `game/`, then `python3 tools/game_patches.py export` and commit `patches/game`.
To move to a newer decomp commit: `python3 tools/game_patches.py reset`, check out the new commit in `game/`, apply
(fix the patches that no longer apply, e.g. with `git apply --3way` and the decomp's renames), export, and commit the
submodule pointer together with `patches/game`. Then regenerate the tables in `src/port/generated/` with
`tools/gen_func_table.py`, `tools/gen_fragment_entries.py` and `tools/gen_abs_syms.py`: they read the map of an
unmodified decomp build of that same commit (`build/pokestadium-us.map`) in `$PORT_DECOMP_DIR`, by default
`../pokestadium`.
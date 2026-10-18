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

## Build

The game code needs clang (for Windows: llvm-mingw); GCC and MSVC are not supported.

```bash
tools/setup.sh                      # submodules + the port's libultraship patch (patches/libultraship.patch)
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
  zlib1g-dev nlohmann-json3-dev libtinyxml2-dev libspdlog-dev
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
$VCPKG_ROOT/vcpkg install sdl3 glew libzip nlohmann-json tinyxml2 spdlog zlib bzip2 --triplet x64-mingw-static
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$PWD/cmake/toolchain-mingw-clang.cmake
cmake --build build-win
```

For Windows on ARM: `MINGW_TRIPLE=aarch64-w64-mingw32` and the `arm64-mingw-static` triplet. `pokestadium.exe` is a
window-only program (no console window; see Run for its log output) with the icon and version information of
`src/port/pokestadium.rc.in`.

The version (Windows file version, AppImage, `Info.plist`) is the `project()` version in `CMakeLists.txt` unless
`-DPORT_VERSION=<version>` says otherwise. `.github/workflows/build.yml` builds Linux, macOS and Windows (x64, arm64);
`.github/workflows/release.yml` builds them with the version it is given, tags the commit `v<version>` and `latest`,
and publishes a GitHub release. Both run by hand from the Actions tab.

Start `pokestadium` (`pokestadium.exe` on Windows) without arguments to get the launcher (all platforms): pick the
Stadium ROM and save and, per player, a Game Boy cartridge and save for the Transfer Pak, then PLAY. The choices are
remembered (`pokestadium.cfg.json`); Auto-play skips the launcher next time (hold Shift while starting to see it again),
and Debug adds a save-editor button (needs python3). With a ROM path as argument the game starts right away:
`cd build && ./pokestadium "/path/to/Pokemon Stadium (USA).z64"`.
Useful environment variables (all in PORTING_NOTES.md): `PORT_FLASH_FILE` (Stadium save), `PORT_GB_ROM` /
`PORT_GB_SAVE` (Transfer Pak cartridge of player 1; `PORT_GB_ROM2..4` for the others).

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

- `game/` — submodule: the decompiled game (C), adapted for 64-bit hosts (see below)
- `src/port/` — host layer: OS/threads, VI, controllers, Transfer Pak, flash saves, GB core
- `src/port/sdk/` — stand-ins for the N64 SDK headers the game includes (`ultra64.h`, `PR/*.h`, force-included `port_compat.h`)
- `libultraship/` — submodule; the port's renderer changes are in `patches/libultraship.patch`
- `ghostship-ref/` — submodule (fonts used by `tools/make_o2r.py`)
- `tools/` — launcher, save editor, ares comparison tooling (`tools/tour.py`), replay helpers
- `replays/` — input recordings used as regression tests
- `PORTING_NOTES.md` — what was done, how it was checked against ares, debug switches

## The game submodule

`game/` is branch `pc-port` of [michiiik/pokestadium](https://github.com/michiiik/pokestadium/tree/pc-port): the
decompilation at commit 697f9ba7 plus commits that adapt its sources for 64-bit hosts. Only `game/src`, `game/include`
and the libultra files listed in `cmake/ultralib_sources.txt` are compiled; the rest of the decomp (asm, assets,
tools) is unused. To change game code: commit in `game/` on `pc-port`, push it there, then commit the new submodule
commit here. `tools/gen_func_table.py` and `tools/gen_fragment_entries.py` write into `game/src`, so their output is
committed the same way; they (and `tools/gen_abs_syms.py`) read the map of an unmodified decomp build in
`../pokestadium`.

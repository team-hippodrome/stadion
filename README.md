# Pokémon Stadium (US) — native PC port (macOS, Linux, Windows)

A native port of Pokémon Stadium (N64, US) built from the decompilation on top of
[libultraship](https://github.com/Kenix3/libultraship) (Metal on macOS, OpenGL on Linux, DirectX 11 or OpenGL on
Windows), with the Transfer Pak and the GB Tower
(Game Boy emulation) working, including German Pokémon Yellow.

No game data is included. You need your own ROM of *Pokemon Stadium (USA)*
(SHA-1 `ed7bef5a306f88c0a6e96b15e71fee2ef32058f3`) and, for the Transfer Pak / GB Tower, your own Game Boy ROMs
and saves.

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
tools/make_app.sh                   # optional: "Pokemon Stadium.app" with the launcher
```

**Linux** (Ubuntu 25.04 or newer, for SDL3; other distributions: the same packages):

```bash
sudo apt install clang lld cmake ninja-build python3 libsdl3-dev libglew-dev libgl-dev libzip-dev libbz2-dev \
  zlib1g-dev nlohmann-json3-dev libtinyxml2-dev libspdlog-dev
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

**Windows** (cross-compiled from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), as the CI does; its
`bin` directory on `PATH`, vcpkg in `$VCPKG_ROOT`):

```bash
$VCPKG_ROOT/vcpkg install sdl3 glew libzip nlohmann-json tinyxml2 spdlog zlib bzip2 --triplet x64-mingw-static
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$PWD/cmake/toolchain-mingw-clang.cmake
cmake --build build-win
```

For Windows on ARM: `MINGW_TRIPLE=aarch64-w64-mingw32` and the `arm64-mingw-static` triplet.

`.github/workflows/build.yml` builds all three on every push; `.github/workflows/release.yml` (run by hand from the
Actions tab) builds them, tags the commit `v<version>` and `latest`, and publishes a GitHub release.

Start `pokestadium` (`pokestadium.exe` on Windows) without arguments to get the launcher (all platforms): pick the
Stadium ROM and save and, per player, a Game Boy cartridge and save for the Transfer Pak, then PLAY. The choices are
remembered (`pokestadium.cfg.json`); Auto-play skips the launcher next time (hold Shift while starting to see it again),
and Debug adds a save-editor button (needs python3). With a ROM path as argument the game starts right away:
`cd build && ./pokestadium "/path/to/Pokemon Stadium (USA).z64"`.
Useful environment variables (all in PORTING_NOTES.md): `PORT_FLASH_FILE` (Stadium save), `PORT_GB_ROM` /
`PORT_GB_SAVE` (Transfer Pak cartridge of player 1; `PORT_GB_ROM2..4` for the others).

## Layout

- `game/` — the decompiled game (C), adapted for 64-bit hosts
- `src/port/` — host layer: OS/threads, VI, controllers, Transfer Pak, flash saves, GB core
- `libultraship/` — submodule; the port's renderer changes are in `patches/libultraship.patch`
- `ghostship-ref/` — submodule (fonts used by `tools/make_o2r.py`)
- `tools/` — launcher, save editor, ares comparison tooling (`tools/tour.py`), replay helpers
- `replays/` — input recordings used as regression tests
- `PORTING_NOTES.md` — what was done, how it was checked against ares, debug switches

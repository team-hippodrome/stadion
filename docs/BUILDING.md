# Building stadion

[Back to the README](../README.md)

Use Clang (AppleClang on macOS, llvm-mingw for the Windows cross-build below), CMake 3.20 or newer, Ninja and Python 3. GCC and the MSVC compiler are not supported.

Clone the repository and prepare its submodules before following the instructions for your platform:

```bash
git clone https://github.com/team-hippodrome/stadion.git
cd stadion
```

```bash
tools/setup.sh                      # submodule (libultraship); the game is vendored in game/
```

**macOS** (Homebrew):

```bash
brew install cmake ninja sdl3 glew libzip nlohmann-json tinyxml2 spdlog
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
python3 tools/make_macos_bundle.py build stadion.app "<version>"   # optional: self-contained app
```

`tools/make_macos_bundle.py` bundles the executable and its Homebrew libraries into a double-clickable app.
Replace `<version>` with the version you are packaging.

**Linux** (a distribution providing SDL3 development packages; package names below are for Ubuntu):

```bash
sudo apt install clang lld cmake ninja-build python3 libsdl3-dev libglew-dev libgl-dev libzip-dev libbz2-dev \
  zlib1g-dev nlohmann-json3-dev libtinyxml2-dev libspdlog-dev glslang-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

This build links the distribution's shared libraries and keeps its settings beside the executable, unless `SHIP_HOME` overrides that location. The release
AppImage is built differently, so that it runs on other distributions too (see the `linux` job in
`.github/workflows/build.yml`): on Ubuntu 24.04, with SDL3 built from source and libzip, spdlog, tinyxml2 and
nlohmann-json from vcpkg (`x64-linux`), linked statically along with the C++ runtime
(`-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"`), then packed by
`python3 tools/make_appimage.py build pokestadium.AppImage <version> <path to appimagetool>`. See the packaging script for its shared-library allowlist.

**Windows** (cross-compiled from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), as the CI does; its
`bin` directory on `PATH`, vcpkg in `$VCPKG_ROOT`):

```bash
$VCPKG_ROOT/vcpkg install "sdl3[core,vulkan]" glew libzip nlohmann-json tinyxml2 spdlog zlib bzip2 glslang --triplet x64-mingw-static
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$PWD/cmake/toolchain-mingw-clang.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-win
```

For Windows on ARM: export `MINGW_TRIPLE=aarch64-w64-mingw32` and the `arm64-mingw-static` triplet (with llvm-mingw's
`bin` on `PATH` for the vcpkg install too, or vcpkg may build glslang with a GCC mingw that does not link). SDL3 needs
its `vulkan` feature (vcpkg leaves it out by default) for the Vulkan renderer. Keep the build type: without one,
vcpkg's debug libraries are linked, and the debug SDL3 stops DirectX 11 at startup with an assertion dialog
(`SDL_IsMainThread` in `SDL_PumpEventsInternal`). `pokestadium.exe` is a
window-only program (no console window; set `PORT_CONSOLE=1` to open one) with the icon and version information of
`src/port/pokestadium.rc.in`.

**Android** (64-bit ARM, Android 10 or newer; tested target: AYN Thor). Needs JDK 17 or 21, the Android SDK with NDK
29.0.14206865, platform 36 and build-tools 36 (`sdkmanager "platforms;android-36" "build-tools;36.0.0"
"ndk;29.0.14206865"`), CMake and Ninja. `android/local.properties` names the SDK and the CMake to use (`sdk.dir=...`,
`cmake.dir=/opt/homebrew` with Homebrew's CMake):

```bash
(cd android && ./gradlew assembleDebug)
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The app is `android/` (SDL's Java activity, from the SDL version libultraship fetches) around the game built from
`CMakeLists.txt` as `libmain.so`, with the OpenGL ES 3 renderer. Config, logs and saves live in the app's folder,
`/sdcard/Android/data/com.teamhippodrome.pokestadium/files`; files picked in the launcher (ROMs, saves) are copied into
its `imported/` folder, since Android's file picker hands out content links rather than paths.

The version (Windows file version, AppImage, `Info.plist`) is the `project()` version in `CMakeLists.txt` unless
`-DPORT_VERSION=<version>` says otherwise. `.github/workflows/build.yml` builds Linux, macOS and Windows (x64, arm64);
`.github/workflows/release.yml` builds them with the version it is given, tags the commit `v<version>` and `latest`,
and publishes a GitHub release. Both run by hand from the Actions tab.

`tools/release.sh <version>` makes the same release on a Mac, plus the Android APK, without GitHub Actions. It builds a
clean checkout of `origin/main` (or `--ref`), never the working tree. macOS and Android build on the Mac. Windows (x64,
arm64) and Linux build in two OrbStack machines that see the Mac's `/Users`: their tool setup is described at the top
of the script. The files land in `../releases/v<version>/dist` with a `SHA256SUMS`. `--publish` then tags the commit,
moves `latest` and creates the GitHub release, after asking. `--only macos,windows-x64` builds some of them, and
`tools/release.sh --help` lists the rest.

## Repository layout

- `game/` — the decompiled game (C), vendored from the decomp with the port's changes folded in (see below)
- `src/port/` — host layer: OS/threads, VI, controllers, sound (audio microcode, AI), Transfer Pak, flash saves, GB core
- `src/port/StadionPort.h` — the game/port boundary: every function the game calls in the port, or implements for
  it, is declared there once. Game files include it explicitly; do not declare `Port_*` functions with a local `extern`
- `src/port/sdk/` — stand-ins for the N64 SDK headers the game includes (`ultra64.h`, `PR/*.h`, force-included `port_compat.h`)
- `libultraship/`: submodule of [Team Hippodrome's fork](https://github.com/team-hippodrome/libultraship);
  the port's renderer changes are committed in the fork, not applied as a local patch
- `tools/` — launcher, save editor, ares comparison tooling (`tools/tour.py`), replay helpers
  (`tools/run_replays.py` plays all stored recordings without showing the window and reports which still pass;
  `tools/frame_sweep.py` captures a stretch of the game at 5 pictures per second, skipping repeats, as contact sheets;
  `tools/audio_compare.py` compares the game's sound with ares; `tools/av_sync.py` checks the timing of pictures
  against the sound, for ares or the port, against a video such as a console capture; `tools/interp_check/` checks
  Smooth motion's in-between pictures and pacing)
- `replays/` — input recordings used as regression tests
- `PORTING_NOTES.md` — what was done, how it was checked against ares, debug switches

## The libultraship submodule

`libultraship/` is pinned to a committed revision of Team Hippodrome's fork. Make renderer and runtime dependency
changes in that repository, then update the submodule pointer here after the fork commit is available remotely.
Setup and CI use the pinned source directly; do not export or apply a libultraship patch.

For an existing checkout, first save any local libultraship edits (including untracked files) outside the checkout.
If the old setup applied the renderer patch, restore the tracked files to the old pinned revision only after
backing up those edits. Then run `tools/setup.sh`: it synchronizes the submodule URL before updating the pin.
Git may refuse the update if old edits or untracked files would be overwritten; do not force it or delete files
until you have verified your backup. A fresh clone avoids this migration step.

## The vendored game

`game/` holds `src/`, `include/` and `lib/ultralib/` of the decompilation,
[team-hippodrome/pokestadium](https://github.com/team-hippodrome/pokestadium), with everything the port changes
(64-bit pointers, big-endian ROM data, sound, Game Boy, port hooks) edited in place. `game/UPSTREAM.md` records the
decomp commit it was last synced with. Only `game/src`, `game/include` and the libultra files listed in
`cmake/ultralib_sources.txt` are compiled. Game code is edited directly in `game/`; mark port-only code with `PORT`
comments as before, and keep the decomp's names for decomp things, so the two trees stay comparable.

To move to a newer decomp commit, merge the decomp's changes since the recorded commit into `game/` (a three-way
merge: base = the recorded commit, theirs = the new commit, ours = `game/`), format with the decomp's
`.clang-format` (clang-format 14), build, and update `game/UPSTREAM.md`. Then regenerate the tables in
`src/port/generated/` with `tools/gen_func_table.py`, `tools/gen_fragment_entries.py` and `tools/gen_abs_syms.py`
(after one build, since they read `build/libstadium_game.a`; run them twice when names changed, as the old table is
part of that library), and `tools/ares_ref/battle_engine.syms`. The generators read the map of an unmodified decomp
build of the new commit (`build/pokestadium-us.map`) in `$PORT_DECOMP_DIR`, by default `../pokestadium`.

## Shader coverage

To audit shader coverage from source, build the optional `shader_source_inventory` CMake target after setup. It writes
`shader_source_candidates.txt` and `vulkan_source_candidates.txt` in the build directory. The scan follows named static
display lists. It explores inherited settings and reports candidates separately so you can compare them with replay
traces before updating the shipped warm-up lists. Dynamic commands and display lists stored in assets still need
runtime coverage. Run `tools/run_replays.py --all --backend metal --shader-capture --out build/shader-capture` (or
`--backend vulkan` with a Vulkan build) to collect recipes from every rendered frame of the replay suite. Pass the
capture directories to `tools/shader_list.py --capture-dir DIR --candidate-dir DIR` to stage the additions. Rerun
on the same backend with `--shader-verify DIR --reference-dir CAPTURE/ref`. Verification uses a fresh profile
and checks for first-draw compilation, new recipes and changes to the final captured image. It covers only the
game paths exercised by those replays. Use `--gb-dir DIR` for the optional Game Boy Tower fixtures and
`--replay-dir DIR` for additional recordings and their `replays.txt` manifest.

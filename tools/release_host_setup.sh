#!/bin/bash
# Sets up a Mac (Apple Silicon, Homebrew, OrbStack installed) to build releases with tools/release.sh:
#   - Homebrew packages for the macOS build (the libraries the app bundles) plus gh and a JDK for Gradle
#   - the Android SDK (platform, build tools, NDK) in ~/Library/Android/sdk
#   - OrbStack machine stadium-linux (arm64): llvm-mingw + vcpkg with the x64/arm64-mingw-static libraries (Windows)
#   - OrbStack machine stadium-x64 (amd64, Rosetta): vcpkg x64-linux, static SDL3 and appimagetool (Linux AppImage)
# The versions match the machines the earlier releases were built on. Safe to run again: each step skips what is
# already there. Logs: ~/Library/Logs/pokestadium-release-setup/.
#
# Usage: tools/release_host_setup.sh [mac|android|windows-vm|linux-vm ...]   (default: all of them)
set -euo pipefail

LLVM_MINGW=llvm-mingw-20261006-ucrt-ubuntu-22.04-aarch64
VCPKG_WIN_COMMIT=bf19794383e8fb95eba5aa553235bc8ec2271688
VCPKG_LINUX_COMMIT=d186940b2f0c4e8e5b4ff804af57d2a26fbfa091
SDL3_VERSION=3.4.18
APPIMAGETOOL_VERSION=1.9.1
ANDROID_PACKAGES=("platforms;android-36" "build-tools;36.0.0" "ndk;29.0.14206865" "platform-tools")
WIN_VM="${PORT_WIN_VM:-stadium-linux}" LINUX_VM="${PORT_LINUX_VM:-stadium-x64}"

LOGS="$HOME/Library/Logs/pokestadium-release-setup"
mkdir -p "$LOGS"
say() { echo "== $*"; }
die() { echo "release_host_setup: $*" >&2; exit 1; }
BREW="$(command -v brew || true)"
[ -n "$BREW" ] || die "Homebrew not found on PATH"
ORB="$(command -v orb || echo "$HOME/.orbstack/bin/orb")"
STEPS=("$@"); [ ${#STEPS[@]} -gt 0 ] || STEPS=(mac android windows-vm linux-vm)

setup_mac() {
    say "Homebrew packages"
    "$BREW" install ninja cmake sdl3 glew libzip nlohmann-json tinyxml2 spdlog glslang gh openjdk@21 \
        > "$LOGS/brew.log" 2>&1 || { tail -20 "$LOGS/brew.log"; die "brew install failed"; }
}

setup_android() {
    say "Android SDK"
    "$BREW" list --cask android-commandlinetools >/dev/null 2>&1 ||
        "$BREW" install --cask android-commandlinetools > "$LOGS/android-cmdline.log" 2>&1
    local sdk="$HOME/Library/Android/sdk"
    mkdir -p "$sdk"
    export JAVA_HOME="$("$BREW" --prefix openjdk@21)/libexec/openjdk.jdk/Contents/Home"
    local sdkmanager="$("$BREW" --prefix)/bin/sdkmanager"
    yes | "$sdkmanager" --sdk_root="$sdk" --licenses > "$LOGS/android-licenses.log" 2>&1 || true
    "$sdkmanager" --sdk_root="$sdk" "${ANDROID_PACKAGES[@]}" > "$LOGS/android-sdk.log" 2>&1 ||
        { tail -20 "$LOGS/android-sdk.log"; die "sdkmanager failed"; }
}

# $1: machine, $2: image, $3: arch
ensure_vm() {
    if "$ORB" list -q 2>/dev/null | grep -qx "$1"; then return; fi
    say "creating OrbStack machine $1 ($2, $3)"
    "$ORB" create -a "$3" "$2" "$1"
}

setup_windows_vm() {
    ensure_vm "$WIN_VM" ubuntu:plucky arm64
    say "$WIN_VM: packages, llvm-mingw, vcpkg (x64/arm64-mingw-static; the first run builds for an hour or more)"
    "$ORB" run -m "$WIN_VM" bash -lc "
        set -e
        sudo apt-get update -q
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends build-essential clang lld \
            cmake ninja-build python3 git curl ca-certificates zip unzip tar xz-utils pkg-config autoconf automake \
            libtool bison flex
        mkdir -p ~/tools && cd ~/tools
        if [ ! -d $LLVM_MINGW ]; then
            curl -sSLf -o llvm-mingw.tar.xz https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/$LLVM_MINGW.tar.xz
            tar xf llvm-mingw.tar.xz && rm llvm-mingw.tar.xz
        fi
        [ -d vcpkg ] || git clone -q https://github.com/microsoft/vcpkg vcpkg
        git -C vcpkg fetch -q origin && git -C vcpkg checkout -q $VCPKG_WIN_COMMIT
        export VCPKG_FORCE_SYSTEM_BINARIES=1 PATH=\$HOME/tools/$LLVM_MINGW/bin:\$PATH
        [ -x vcpkg/vcpkg ] || vcpkg/bootstrap-vcpkg.sh -disableMetrics
        for t in x64-mingw-static arm64-mingw-static; do
            vcpkg/vcpkg install 'sdl3[core,vulkan]' glew libzip nlohmann-json tinyxml2 spdlog zlib bzip2 glslang \
                --triplet \$t --host-triplet arm64-linux
        done
    " > "$LOGS/windows-vm.log" 2>&1 || { tail -30 "$LOGS/windows-vm.log"; die "$WIN_VM setup failed"; }
}

setup_linux_vm() {
    ensure_vm "$LINUX_VM" ubuntu:noble amd64
    say "$LINUX_VM: packages, SDL3 $SDL3_VERSION (static), vcpkg x64-linux, appimagetool"
    "$ORB" run -m "$LINUX_VM" bash -lc "
        set -e
        sudo apt-get update -q
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends clang lld cmake ninja-build \
            python3 pkg-config git curl ca-certificates zip unzip tar file fuse3 \
            libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev libxtst-dev \
            libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev libopengl-dev libgles-dev \
            libdrm-dev libgbm-dev libpulse-dev libpipewire-0.3-dev libasound2-dev libdbus-1-dev libibus-1.0-dev \
            libudev-dev
        mkdir -p ~/rel && cd ~/rel
        if [ ! -f deps/lib/libSDL3.a ]; then
            rm -rf SDL
            git clone -q --depth 1 -b release-$SDL3_VERSION https://github.com/libsdl-org/SDL SDL
            cmake -S SDL -B SDL/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DSDL_SHARED=OFF \
                -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
                -DCMAKE_INSTALL_PREFIX=\$HOME/rel/deps
            cmake --build SDL/build && cmake --install SDL/build
        fi
        [ -d vcpkg ] || git clone -q https://github.com/microsoft/vcpkg vcpkg
        git -C vcpkg fetch -q origin && git -C vcpkg checkout -q $VCPKG_LINUX_COMMIT
        [ -x vcpkg/vcpkg ] || vcpkg/bootstrap-vcpkg.sh -disableMetrics
        vcpkg/vcpkg install 'libzip[core,bzip2]' nlohmann-json tinyxml2 spdlog zlib bzip2 --triplet x64-linux
        if [ ! -x appimagetool ]; then
            curl -sSLf -o appimagetool https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage
            chmod +x appimagetool
        fi
    " > "$LOGS/linux-vm.log" 2>&1 || { tail -30 "$LOGS/linux-vm.log"; die "$LINUX_VM setup failed"; }
}

for s in "${STEPS[@]}"; do
    case "$s" in
        mac) setup_mac ;;
        android) setup_android ;;
        windows-vm) setup_windows_vm ;;
        linux-vm) setup_linux_vm ;;
        *) die "unknown step '$s' (mac, android, windows-vm, linux-vm)" ;;
    esac
done
say "done (logs: $LOGS)"

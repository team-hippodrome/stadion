#!/bin/bash
# Builds a release on this Mac, the way .github/workflows/release.yml does on GitHub, plus the Android APK.
# The "Release (Mac mini)" workflow (.github/workflows/release-macmini.yml) runs this on the self-hosted Mac mini;
# tools/release_host_setup.sh sets a Mac up for it (Homebrew packages, Android SDK, the two OrbStack machines).
#
# Usage: tools/release.sh <version> [--ref REF] [--only LIST] [--serial] [--jobs N] [--keep] [--publish [--yes]]
#   <version>      e.g. 0.0.11 (a leading "v" is dropped); goes into the builds (Info.plist, .exe file version, APK)
#   --ref REF      what to build (default: origin/main, fetched first); any commit, branch or tag
#   --only LIST    some of macos,android,windows-x64,windows-arm64,linux (default: all), comma-separated
#   --serial       one build at a time (default: the Mac, the Windows VM and the Linux VM work at the same time)
#   --jobs N       parallel compile jobs per build (default 4), always at low priority
#   --keep         keep the source checkout and the build folders (default: removed once packaged)
#   --publish      afterwards tag the commit v<version>, move the "latest" tag, push both and create the GitHub
#                  release with the files (asks first; --yes skips the question)
#
# Output: <releases>/v<version>/dist (next to the repository: ../releases unless PORT_RELEASE_ROOT is set), named as on
# GitHub: pokestadium-macos-arm64-v<version>.zip, pokestadium-windows-{x64,arm64}-v<version>.zip,
# pokestadium-linux-x86_64-v<version>.AppImage, pokestadium-android-arm64-v<version>-debug.apk, and SHA256SUMS.
# Logs: <releases>/v<version>/logs/<build>.log.
#
# Where each build runs:
#   macOS, Android   this Mac (Homebrew packages as in the README; Android: the SDK named by android/local.properties,
#                    copied from this checkout)
#   Windows          OrbStack machine $PORT_WIN_VM (default stadium-linux): llvm-mingw in ~/tools/llvm-mingw-*/bin and
#                    vcpkg in ~/tools/vcpkg with the x64-mingw-static and arm64-mingw-static triplets
#   Linux            OrbStack machine $PORT_LINUX_VM (default stadium-x64, amd64): vcpkg in ~/rel/vcpkg (x64-linux),
#                    static SDL3 in ~/rel/deps, appimagetool at ~/rel/appimagetool (the build.yml setup)
# The machines see this Mac's /Users, so they build from the same checkout; their build folders stay in their own home.
# Local paths are kept out of the binaries (CMake PORT_PATH_MAP; debug info stripped except the APK's, kept for crash stacks).
# Windows: the shipped exe has no symbol table; a copy with it goes to releases/v<version>/symbols/ (never published).
# No game data goes into any file: the user's own ROM is needed to play.
set -euo pipefail

die() { echo "release: $*" >&2; exit 1; }
say() { echo "== $*"; }

VERSION="" REF="origin/main" ONLY="macos,android,windows-x64,windows-arm64,linux" SERIAL=0 JOBS=4 KEEP=0 PUBLISH=0 YES=0
while [ $# -gt 0 ]; do
    case "$1" in
        --ref) REF="$2"; shift 2 ;;
        --only) ONLY="$2"; shift 2 ;;
        --serial) SERIAL=1; shift ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --keep) KEEP=1; shift ;;
        --publish) PUBLISH=1; shift ;;
        --yes) YES=1; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        -*) die "unknown option $1 (see --help)" ;;
        *) [ -z "$VERSION" ] || die "one version only"; VERSION="${1#v}"; shift ;;
    esac
done
[ -n "$VERSION" ] || die "no version given (see --help)"
echo "$VERSION" | grep -qE '^[0-9]+(\.[0-9]+)*$' || die "version must look like 0.0.11 (got '$VERSION')"
want() { case ",$ONLY," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }
for b in ${ONLY//,/ }; do
    case "$b" in macos|android|windows-x64|windows-arm64|linux) ;; *) die "unknown build '$b' in --only" ;; esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="$(cd "$(git -C "$ROOT" rev-parse --git-common-dir)/.." && pwd)" # the main checkout (this may be a worktree)
REL="${PORT_RELEASE_ROOT:-$(dirname "$MAIN")/releases}/v$VERSION"
SRC="$REL/src" DIST="$REL/dist" LOGS="$REL/logs"
WIN_VM="${PORT_WIN_VM:-stadium-linux}" LINUX_VM="${PORT_LINUX_VM:-stadium-x64}"
case "$REL" in /Users/*) ;; *) want windows-x64 || want windows-arm64 || want linux &&
    die "the release folder must be under /Users (the build machines see only that): set PORT_RELEASE_ROOT" ;; esac

# Fail before building if this version is already published, or if the remote cannot be checked.
TAG="v$VERSION"
if [ $PUBLISH = 1 ]; then
    REPO="$(cd "$ROOT" && gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null)" || die "gh is not set up for this repository"
    git -C "$ROOT" show-ref --verify --quiet "refs/tags/$TAG" && die "$TAG already exists locally"
    REMOTE_TAG="$(git -C "$ROOT" ls-remote --tags origin "refs/tags/$TAG")" || die "cannot check tags on origin; not published"
    [ -z "$REMOTE_TAG" ] || die "$TAG already exists on GitHub"
fi

# ---- source: a clean checkout of the ref (never the working tree, which may hold uncommitted changes) ----
if [[ "$REF" == origin/* ]]; then
    git -C "$ROOT" fetch -q origin "${REF#origin/}"
fi
COMMIT="$(git -C "$ROOT" rev-parse --verify "$REF^{commit}")" || die "unknown ref $REF"
say "v$VERSION from $REF ($(git -C "$ROOT" log -1 --format='%h %s' "$COMMIT"))"
mkdir -p "$REL" "$DIST" "$LOGS"
if [ -e "$SRC" ]; then
    [ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null)" = "$COMMIT" ] ||
        die "$SRC holds another commit: remove it (git -C '$MAIN' worktree remove --force '$SRC') or pick another version"
else
    git -C "$MAIN" worktree add -q --detach "$SRC" "$COMMIT"
    # the submodule borrows the objects of the main checkout's (no full download)
    # (a shallow clone cannot lend its objects: those are fetched from GitHub by setup.sh)
    for sm in libultraship; do
        if [ -e "$MAIN/$sm/.git" ] && [ "$(git -C "$MAIN/$sm" rev-parse --is-shallow-repository)" = false ]; then
            git -C "$SRC" -c protocol.file.allow=always submodule update -q --init --reference "$MAIN/$sm" "$sm"
        fi
    done
    (cd "$SRC" && tools/setup.sh) > "$LOGS/setup.log" 2>&1 || die "setup failed: $LOGS/setup.log"
fi
# paths rewritten in the binaries (CMake PORT_PATH_MAP): the checkout to nothing, the rest of the home folder (SDKs,
# vcpkg) to "~"; the later, more specific one wins. The build machines have a home folder of their own.
MAP="$HOME/=~/;$SRC/="
VM_MAP="\$HOME/=~/;$SRC/="

# ---- builds ----
# Each writes its file into $DIST and returns non-zero on failure; output goes to $LOGS/<name>.log.

build_macos() {
    local B="$REL/build-macos"
    nice -n 15 cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPORT_VERSION="$VERSION" \
        -DPORT_PATH_MAP="$MAP"
    nice -n 15 cmake --build "$B" -j "$JOBS"
    strip -S "$B/pokestadium" # the debug map lists the object files by local path
    rm -rf "$B/dist"
    python3 -I "$SRC/tools/make_macos_bundle.py" "$B" "$B/dist/Pokemon Stadium.app" "$VERSION"
    ditto -c -k --keepParent "$B/dist/Pokemon Stadium.app" "$DIST/pokestadium-macos-arm64-v$VERSION.zip"
}

build_android() {
    [ -f "$MAIN/android/local.properties" ] || { echo "no android/local.properties in $MAIN (see README)"; return 1; }
    cp "$MAIN/android/local.properties" "$SRC/android/local.properties"
    (cd "$SRC/android" && nice -n 15 ./gradlew --no-daemon -PportVersion="$VERSION" -PportPathMap="$MAP" assembleDebug)
    cp "$SRC/android/app/build/outputs/apk/debug/app-debug.apk" "$DIST/pokestadium-android-arm64-v$VERSION-debug.apk"
}

build_windows() { # $1: x64 or arm64
    local arch="$1" triple vcpkg
    case "$arch" in
        x64) triple=x86_64-w64-mingw32 vcpkg=x64-mingw-static ;;
        arm64) triple=aarch64-w64-mingw32 vcpkg=arm64-mingw-static ;;
    esac
    local stage="$REL/windows-$arch"
    rm -rf "$stage"; mkdir -p "$stage" "$REL/symbols"
    orb run -m "$WIN_VM" bash -lc "
        set -e
        export PATH=\"\$(ls -d \$HOME/tools/llvm-mingw-*/bin | head -1):\$PATH\" MINGW_TRIPLE=$triple
        B=\$HOME/pokestadium-release/v$VERSION-windows-$arch
        nice -n 15 cmake -S '$SRC' -B \"\$B\" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DCMAKE_TOOLCHAIN_FILE=\$HOME/tools/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=$vcpkg \
            -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE='$SRC/cmake/toolchain-mingw-clang.cmake' -DPORT_VERSION=$VERSION \
            -DPORT_PATH_MAP=\"$VM_MAP\"
        nice -n 15 cmake --build \"\$B\" -j $JOBS
        # shipped exe: no symbol table at all (an unstripped table after the last section reads as an appended blob,
        # which antivirus heuristics count against the file); the symbol table copy stays in the release folder only
        llvm-strip --strip-debug -o '$REL/symbols/pokestadium-windows-$arch-v$VERSION.exe' \"\$B/pokestadium.exe\"
        llvm-strip --strip-all -o '$stage/pokestadium.exe' \"\$B/pokestadium.exe\"
        if [ -f \"\$B/pokestadium.o2r\" ]; then cp \"\$B/pokestadium.o2r\" '$stage/'; fi
        [ $KEEP = 1 ] || rm -rf \"\$B\"
    "
    cp "$SRC/README.md" "$stage/README.md"
    cp "$SRC/PORTING_NOTES.md" "$stage/PORTING_NOTES.md"
    mkdir -p "$stage/docs"
    cp "$SRC/docs/BUILDING.md" "$stage/docs/BUILDING.md"
    rm -f "$DIST/pokestadium-windows-$arch-v$VERSION.zip"
    (cd "$stage" && zip -q -r "$DIST/pokestadium-windows-$arch-v$VERSION.zip" .)
    rm -rf "$stage"
}

build_linux() {
    orb run -m "$LINUX_VM" bash -lc "
        set -e
        B=\$HOME/pokestadium-release/v$VERSION-linux
        nice -n 15 cmake -S '$SRC' -B \"\$B\" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_TOOLCHAIN_FILE=\$HOME/rel/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-linux \
            -DCMAKE_PREFIX_PATH=\$HOME/rel/deps -DCMAKE_EXE_LINKER_FLAGS='-static-libstdc++ -static-libgcc' \
            -DPORT_VERSION=$VERSION -DPORT_PATH_MAP=\"$VM_MAP\"
        nice -n 15 cmake --build \"\$B\" -j $JOBS
        strip --strip-debug \"\$B/pokestadium\"
        python3 -I '$ROOT/tools/check_build_paths.py' \"\$B/pokestadium\"
        python3 -I '$SRC/tools/make_appimage.py' \"\$B\" \"\$B/pokestadium-linux-x86_64.AppImage\" $VERSION \$HOME/rel/appimagetool
        cp \"\$B/pokestadium-linux-x86_64.AppImage\" '$DIST/pokestadium-linux-x86_64-v$VERSION.AppImage'
        [ $KEEP = 1 ] || rm -rf \"\$B\"
    "
}

# the file each build makes; only this run's files are checksummed and published (dist may hold files of earlier
# runs, or copies someone unpacked there)
artifact() {
    case "$1" in
        macos) echo "pokestadium-macos-arm64-v$VERSION.zip" ;;
        android) echo "pokestadium-android-arm64-v$VERSION-debug.apk" ;;
        windows-x64) echo "pokestadium-windows-x64-v$VERSION.zip" ;;
        windows-arm64) echo "pokestadium-windows-arm64-v$VERSION.zip" ;;
        linux) echo "pokestadium-linux-x86_64-v$VERSION.AppImage" ;;
    esac
}
FILES=()
for b in ${ONLY//,/ }; do
    rm -f "$DIST/$(artifact "$b")"
    FILES+=("$(artifact "$b")")
done

# a lane runs its builds one after another; lanes run side by side unless --serial
FAILED="$REL/.failed"; rm -f "$FAILED"
run() { # $1: build name, rest: command
    local name="$1"; shift
    local start=$SECONDS
    say "$name: building (log: $LOGS/$name.log)"
    # a subshell of its own, so that set -e stops the build at its first failing step (inside an "if" it would not)
    ( set -e; "$@" ) > "$LOGS/$name.log" 2>&1 &
    if wait $!; then
        say "$name: done in $(( (SECONDS - start) / 60 )) min"
    else
        say "$name: FAILED after $(( (SECONDS - start) / 60 )) min, last lines of $LOGS/$name.log:"
        tail -15 "$LOGS/$name.log" | sed 's/^/   /'
        echo "$name" >> "$FAILED"
    fi
}
lane_mac() {
    if want macos; then run macos build_macos; fi
    if want android; then run android build_android; fi
}
lane_win() {
    if want windows-x64; then run windows-x64 build_windows x64; fi
    if want windows-arm64; then run windows-arm64 build_windows arm64; fi
}
lane_linux() {
    if want linux; then run linux build_linux; fi
}
if [ $SERIAL = 1 ]; then
    lane_mac; lane_win; lane_linux
else
    lane_mac & P1=$!
    lane_win & P2=$!
    lane_linux & P3=$!
    wait $P1 $P2 $P3
fi

if [ -s "$FAILED" ]; then
    die "failed: $(tr '\n' ' ' < "$FAILED")(the source and logs are kept in $REL)"
fi
if [ $KEEP = 0 ]; then
    rm -rf "$REL/build-macos"
    git -C "$MAIN" worktree remove --force "$SRC"
fi
# Check the packaged ZIP/APK contents too; AppImages are audited before compression by make_appimage.py.
AUDIT=()
for f in "${FILES[@]}"; do
    case "$f" in *.AppImage) ;; *) AUDIT+=("$DIST/$f") ;; esac
done
if [ ${#AUDIT[@]} -gt 0 ]; then python3 -I "$ROOT/tools/check_build_paths.py" "${AUDIT[@]}"; fi
(cd "$DIST" && shasum -a 256 "${FILES[@]}" > SHA256SUMS)
say "files in $DIST:"
(cd "$DIST" && ls -l "${FILES[@]}" SHA256SUMS) | sed 's/^/   /'

# ---- publish ----
[ $PUBLISH = 1 ] || { say "built, not published (--publish tags and creates the GitHub release)"; exit 0; }
# Check again in case another release was published while these builds ran.
REMOTE_TAG="$(git -C "$ROOT" ls-remote --tags origin "refs/tags/$TAG")" || die "cannot check tags on origin; not published"
[ -z "$REMOTE_TAG" ] || die "$TAG already exists on GitHub"
for b in macos android windows-x64 windows-arm64 linux; do
    want "$b" || echo "release: note: $b is not part of this release (--only)"
done
echo "Publish $TAG on $REPO from $(git -C "$ROOT" log -1 --format='%h %s' "$COMMIT")?"
echo "This pushes the tag $TAG, moves the tag 'latest' to it, and creates a public release with:"
printf '   %s\n' "${FILES[@]}" SHA256SUMS
if [ $YES = 0 ]; then
    read -r -p "Type yes to publish: " answer || die "not published"
    [ "$answer" = yes ] || die "not published"
fi
git -C "$ROOT" tag -a "$TAG" -m "Pokémon Stadium port $TAG" "$COMMIT"
git -C "$ROOT" push origin "$TAG"
git -C "$ROOT" tag -f latest "$COMMIT"
git -C "$ROOT" push -f origin latest
UPLOAD=()
for f in "${FILES[@]}" SHA256SUMS; do UPLOAD+=("$DIST/$f"); done
gh release create "$TAG" "${UPLOAD[@]}" --repo "$REPO" --title "$TAG" --target "$COMMIT" \
    --notes "Builds for Linux (x86_64 AppImage), macOS (Apple Silicon, Pokemon Stadium.app), Windows (x64 and arm64) and Android (arm64, debug-signed APK). No game data included: you need your own Pokémon Stadium (USA) ROM. See README.md."
say "published: https://github.com/$REPO/releases/tag/$TAG"

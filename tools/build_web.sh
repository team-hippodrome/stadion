#!/bin/bash
# Builds the web version (WebAssembly, WebGL 2) into build_web/ and puts the page together in build_web/site/:
#   tools/build_web.sh            configure (once) and build
#   tools/build_web.sh serve      also serve build_web/site on http://localhost:8642 (with the COOP/COEP headers the
#                                 threads need)
# Needs the Emscripten SDK (https://emscripten.org; EMSDK set, or installed in ~/emsdk). The site holds no game data:
# the page asks for the player's own ROM and keeps it in the browser (web/README.md).
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
EMSDK="${EMSDK:-$HOME/emsdk}"
source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
BUILD="${PORT_WEB_BUILD:-build_web}"
JOBS="${PORT_WEB_JOBS:-4}"
# memory64 (host pointers above 4 GB, see CMakeLists.txt), threads (shared memory), WebAssembly exceptions
FLAGS="-m64 -pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm"
# (also through EMCC_CFLAGS, which emcc adds to every compile: some fetched libraries replace CMAKE_CXX_FLAGS)
export EMCC_CFLAGS="$FLAGS"
if [ ! -f "$BUILD/build.ninja" ]; then
    # libzip needs zlib: Emscripten's own build of it
    embuilder build zlib --wasm64 >/dev/null
    SYSROOT="$EMSDK/upstream/emscripten/cache/sysroot"
    emcmake cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS" -DCMAKE_EXE_LINKER_FLAGS="$FLAGS" \
        -DBUILD_SHARED_LIBS=OFF -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF \
        -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DENABLE_OPENSSL=OFF -DENABLE_GNUTLS=OFF \
        -DENABLE_MBEDTLS=OFF -DENABLE_COMMONCRYPTO=OFF -DENABLE_WINDOWS_CRYPTO=OFF \
        -DZLIB_INCLUDE_DIR="$SYSROOT/include" -DZLIB_LIBRARY="$SYSROOT/lib/wasm64-emscripten/libz.a"
fi
nice -n 15 cmake --build "$BUILD" -j "$JOBS"
SITE="$BUILD/site"
rm -rf "$SITE"
mkdir -p "$SITE"
cp web/index.html web/port.js web/audio-worklet.js web/serve.py "$SITE/"
# the game's files under versioned names (name.<hash>.ext): the server lets browsers keep them, and a new build gets new
# names (web/port.js holds the names); each also as Brotli (.br), which the server sends to browsers that take it
files=""
for f in pokestadium.js pokestadium.wasm pokestadium.o2r; do
    hash=$(shasum -a 256 "$BUILD/$f" | cut -c1-12)
    name="${f%.*}.$hash.${f##*.}"
    cp "$BUILD/$f" "$SITE/$name"
    if command -v brotli >/dev/null; then
        brotli -q 11 -c "$SITE/$name" > "$SITE/$name.br"
    fi
    files="$files ${f##*.}: \"$name\","
done
sed -i.bak "s|^const FILES = {.*|const FILES = {${files%,} };|" "$SITE/port.js" && rm "$SITE/port.js.bak"
echo "site: $SITE"
if [ "$1" = serve ]; then
    exec python3 -I "$SITE/serve.py" "$SITE" 8642
fi

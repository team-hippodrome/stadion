#!/bin/bash
# Builds ares_save (tools/ares_ref driver + cartridge FlashRAM load/store): build.sh <work dir> [ares checkout to copy]
# Same pinned ares commit and CMake setup as tools/ares_ref/build.sh, but a separate target and work dir, so it does
# not touch an ares_ref build. If a second argument names an existing ares checkout it is copied instead of cloned.
# Needs cmake, ninja and /Applications/ares.app (MoltenVK). Run scripts with tools/save_editor/ares/run.sh.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${1:?usage: build.sh <work dir> [existing ares checkout]}"
COPY_FROM="$2"
ARES_COMMIT=9408cb43d4948fc3ea6e152a307a34348df3fe04
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
if [ ! -d "$WORK/ares" ]; then
    if [ -n "$COPY_FROM" ]; then
        cp -R "$COPY_FROM" "$WORK/ares"
    else
        git clone https://github.com/ares-emulator/ares.git "$WORK/ares"
    fi
fi
if [ -d "$WORK/ares/.git" ]; then
    git -C "$WORK/ares" fetch --depth 1 origin "$ARES_COMMIT" 2>/dev/null || true
    git -C "$WORK/ares" checkout -q "$ARES_COMMIT"
fi
if ! grep -q "ares_save" "$WORK/ares/CMakeLists.txt"; then
    python3 -I - "$WORK/ares/CMakeLists.txt" "$HERE/ares_save.cpp" <<'EOF'
import sys
path, src = sys.argv[1], sys.argv[2]
s = open(path).read()
block = f'''# pokestadium-port save-editor verification driver (tools/save_editor/ares)
add_executable(ares_save {src})
target_include_directories(ares_save PRIVATE ${{CMAKE_SOURCE_DIR}} ${{CMAKE_SOURCE_DIR}}/ares)
target_link_libraries(ares_save PRIVATE ares::ares ares::nall)
if(ARES_ENABLE_CHD)
  target_link_libraries(ares_save PRIVATE chdr-static)
endif()

'''
i = s.rindex('return()')
open(path, 'w').write(s[:i] + block + s[i:])
EOF
fi
cmake -S "$WORK/ares" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DARES_CORES=n64 \
    -DARES_BUILD_OPTIONAL_TARGETS=OFF -DARES_PRECOMPILE_HEADERS=OFF -DARES_UNITY_CORES=OFF > "$WORK/configure.log"
ninja -C "$WORK/build" ares_save
echo "built $WORK/build/ares_save"

#!/bin/bash
# Builds the ares reference driver: tools/ares_ref/build.sh <work dir>
# Clones ares into <work dir>/ares (pinned commit), adds the ares_ref target (this directory's ares_ref.cpp) to its
# CMake project and builds the N64 core with paraLLEl-RDP into <work dir>/build. MoltenVK is loaded at run time from
# /Applications/ares.app (brew install --cask ares-emulator). The ares tree gets ares-host-hooks.patch (guest-address
# hooks in the CPU, used by the battle cross-check: tools/battle_xcheck.py). ARES_REF_CLONE_FROM=<ares checkout> clones
# from a local tree instead of GitHub. Then run scripts with ref.sh:
#   ARES_REF_BUILD=<work dir>/build ARES_REF_SRC=<work dir>/ares tools/ares_ref/ref.sh <script> [state]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${1:?usage: tools/ares_ref/build.sh <work dir>}"
ARES_COMMIT=9408cb43d4948fc3ea6e152a307a34348df3fe04
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
if [ ! -d "$WORK/ares/.git" ]; then
    git clone ${ARES_REF_CLONE_FROM:+--shared} "${ARES_REF_CLONE_FROM:-https://github.com/ares-emulator/ares.git}" "$WORK/ares"
fi
git -C "$WORK/ares" fetch --depth 1 origin "$ARES_COMMIT" 2>/dev/null || true
git -C "$WORK/ares" checkout -q "$ARES_COMMIT"
# CPU host hooks (an inert list unless the driver fills it)
if ! grep -q "hostHooks" "$WORK/ares/ares/n64/cpu/cpu.hpp"; then
    git -C "$WORK/ares" apply "$HERE/ares-host-hooks.patch"
fi
# add the driver target before the top-level CMakeLists' final return()
if ! grep -q "ares_ref" "$WORK/ares/CMakeLists.txt"; then
    python3 -I - "$WORK/ares/CMakeLists.txt" "$HERE/ares_ref.cpp" <<'EOF'
import sys
path, src = sys.argv[1], sys.argv[2]
s = open(path).read()
block = f'''# pokestadium-port reference-frame driver (tools/ares_ref)
add_executable(ares_ref {src})
target_include_directories(ares_ref PRIVATE ${{CMAKE_SOURCE_DIR}} ${{CMAKE_SOURCE_DIR}}/ares)
target_link_libraries(ares_ref PRIVATE ares::ares ares::nall)
if(ARES_ENABLE_CHD)
  target_link_libraries(ares_ref PRIVATE chdr-static)
endif()

'''
i = s.rindex('return()')
open(path, 'w').write(s[:i] + block + s[i:])
EOF
fi
cmake -S "$WORK/ares" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DARES_CORES=n64 \
    -DARES_BUILD_OPTIONAL_TARGETS=OFF -DARES_PRECOMPILE_HEADERS=OFF -DARES_UNITY_CORES=OFF > "$WORK/configure.log"
nice -n 15 ninja -C "$WORK/build" ${ARES_REF_JOBS:+-j $ARES_REF_JOBS} ares_ref
echo "built $WORK/build/ares_ref"

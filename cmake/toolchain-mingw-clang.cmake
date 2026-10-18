# Cross-compiles the Windows version with llvm-mingw (clang + lld + libc++, https://github.com/mstorsjo/llvm-mingw).
# MINGW_TRIPLE: x86_64-w64-mingw32 (default) or aarch64-w64-mingw32; the toolchain's bin directory must be on PATH.
# Dependencies come from vcpkg (x64-mingw-static / arm64-mingw-static), chained with VCPKG_CHAINLOAD_TOOLCHAIN_FILE.
if(NOT MINGW_TRIPLE)
  set(MINGW_TRIPLE "$ENV{MINGW_TRIPLE}")
endif()
if(NOT MINGW_TRIPLE)
  set(MINGW_TRIPLE x86_64-w64-mingw32)
endif()
set(CMAKE_SYSTEM_NAME Windows)
string(REGEX REPLACE "-.*" "" CMAKE_SYSTEM_PROCESSOR "${MINGW_TRIPLE}")
set(CMAKE_C_COMPILER ${MINGW_TRIPLE}-clang)
set(CMAKE_CXX_COMPILER ${MINGW_TRIPLE}-clang++)
set(CMAKE_ASM_COMPILER ${MINGW_TRIPLE}-clang)
set(CMAKE_RC_COMPILER ${MINGW_TRIPLE}-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

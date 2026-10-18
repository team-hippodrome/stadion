cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(PokemonStadiumPort VERSION 0.0.1 LANGUAGES C CXX ASM)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Windows: the static C runtime, as libultraship and the vcpkg x64-windows-static libraries use
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Compiler driver: clang/AppleClang (macOS, Linux) or clang-cl (Windows; the game code needs clang extensions such as
# section pragmas, so MSVC's own compiler is not supported). The flags below are spelled for the driver in use.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(PORT_FORCE_INCLUDE "/FI")
  set(PORT_DEBUG_INFO /Z7)
  set(PORT_NO_WARNINGS /w)
  set(PORT_UNSIGNED_CHAR /J)
  set(PORT_NO_BUILTIN /clang:-fno-builtin)
else()
  set(PORT_FORCE_INCLUDE "-include")
  set(PORT_DEBUG_INFO -g)
  set(PORT_NO_WARNINGS -w)
  set(PORT_UNSIGNED_CHAR -funsigned-char)
  set(PORT_NO_BUILTIN -fno-builtin)
endif()

# libultraship is built as a subproject; it is the N64 SDK reimplementation
# (windowing, Fast3D renderer, controllers, audio backends, resource manager).
set(GBI_UCODE F3DEX_GBI_2 CACHE STRING "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # fmt 10.1 (distro spdlog) + clang 20: FMT_STRING's consteval check fails to compile; plain constexpr is enough
  add_compile_definitions(FMT_CONSTEVAL=)
endif()
add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)

add_executable(pokestadium
  src/port/main.cpp
  src/port/Engine.cpp
  src/port/stubs.c
  src/port/port_platform.c
  src/port/generated/abs_syms.S
)
target_include_directories(pokestadium PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include/libultraship
)
target_compile_definitions(pokestadium PRIVATE F3DEX_GBI_2)
target_link_libraries(pokestadium PRIVATE libultraship)

# ---------------------------------------------------------------------------------------------
# Game code: the decompiled Pokemon Stadium (US) sources, compiled for the host.
# Flags mirror game/compile_flags.txt (used by tools/syntax_check.sh).
# ---------------------------------------------------------------------------------------------
file(GLOB_RECURSE STADIUM_GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/game/lib/ultralib/src/*.c)
# C arrays of 16/32-bit words in the game sources (textures among them need N64 byte order): their files are compiled
# through generated wrappers that register the arrays with the renderer (see tools/gen_word_arrays.py)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_word_arrays.py
                        ${CMAKE_CURRENT_SOURCE_DIR}/game/src ${CMAKE_BINARY_DIR}/generated)
include(${CMAKE_BINARY_DIR}/generated/word_wrappers.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_word_arrays.py
             ${STADIUM_GAME_SOURCES})
set(PORT_WRAPPED_SOURCES)
list(LENGTH PORT_WORD_WRAPPERS _n)
math(EXPR _last "${_n} - 1")
foreach(_i RANGE 0 ${_last} 2)
  math(EXPR _j "${_i} + 1")
  list(GET PORT_WORD_WRAPPERS ${_i} _orig)
  list(GET PORT_WORD_WRAPPERS ${_j} _wrap)
  list(REMOVE_ITEM STADIUM_GAME_SOURCES ${_orig})
  list(APPEND STADIUM_GAME_SOURCES ${_wrap})
  list(APPEND PORT_WRAPPED_SOURCES ${_orig} ${_wrap})
endforeach()
add_library(stadium_game STATIC ${STADIUM_GAME_SOURCES})
# Per-fragment data sections, restored whenever the game loads the fragment (see tools/gen_frag_sections.py)
execute_process(COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_frag_sections.py
                        ${CMAKE_CURRENT_SOURCE_DIR}/game/src ${CMAKE_BINARY_DIR}/generated)
include(${CMAKE_BINARY_DIR}/generated/frag_sections.cmake)
# the per-fragment options were set on the original paths: give them to the wrappers that replace those files
list(LENGTH PORT_WRAPPED_SOURCES _n)
if(_n GREATER 0)
  math(EXPR _last "${_n} - 1")
  foreach(_i RANGE 0 ${_last} 2)
    math(EXPR _j "${_i} + 1")
    list(GET PORT_WRAPPED_SOURCES ${_i} _orig)
    list(GET PORT_WRAPPED_SOURCES ${_j} _wrap)
    get_source_file_property(_opts ${_orig} COMPILE_OPTIONS)
    if(_opts)
      set_source_files_properties(${_wrap} PROPERTIES COMPILE_OPTIONS "${_opts}")
    endif()
  endforeach()
endif()
target_sources(pokestadium PRIVATE ${CMAKE_BINARY_DIR}/generated/frag_sections.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_frag_sections.py
             ${CMAKE_CURRENT_SOURCE_DIR}/game/src/port_fragment_entries.c)
set_target_properties(stadium_game PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(stadium_game PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/game
  ${CMAKE_CURRENT_SOURCE_DIR}/game/port
  ${CMAKE_CURRENT_SOURCE_DIR}/game/port/PR
  ${CMAKE_CURRENT_SOURCE_DIR}/game/include
  ${CMAKE_CURRENT_SOURCE_DIR}/game/src
  ${CMAKE_CURRENT_SOURCE_DIR}/game/src/libnaudio
  ${CMAKE_CURRENT_SOURCE_DIR}/game/include/audio
  ${CMAKE_CURRENT_SOURCE_DIR}/game/include/libleo
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include/libultraship
  ${CMAKE_CURRENT_SOURCE_DIR}/game/lib/ultralib/include
  ${CMAKE_CURRENT_SOURCE_DIR}/game/lib/ultralib)
target_compile_definitions(stadium_game PRIVATE
  LANGUAGE_C _LANGUAGE_C F3DEX_GBI_2 NDEBUG _FINALROM N_MICRO NON_MATCHING AVOID_UB
  BUILD_VERSION=VERSION_I_P VERSION_US
  PORT_NO_AUDIO PORT_STATIC_FRAGMENTS) # audio microcode/synth is not ported yet; the audio thread only paces frames
target_compile_options(stadium_game PRIVATE
  ${PORT_DEBUG_INFO} ${PORT_NO_BUILTIN} ${PORT_UNSIGNED_CHAR} ${PORT_NO_WARNINGS}
  # keep a symbol per static array: arm64 global merging folds them into _MergedGlobals, and the renderer finds the
  # 16/32-bit word arrays it must byte-swap (textures, palettes; gen_word_arrays.py) by symbol name
  $<$<AND:$<C_COMPILER_ID:Clang,AppleClang>,$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},arm64>>:-mno-global-merge>
  $<$<AND:$<C_COMPILER_ID:Clang,AppleClang>,$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>>:-mno-global-merge>
  -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-incompatible-pointer-types
  -Wno-return-mismatch
  ${PORT_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/game/port/port_compat.h)

# libultra runtime for the game (threads, message queues, devices). This target sees the game's SDK shim
# headers (game/port/ultra64.h etc.); the LUS-facing code in the executable must not, because libultraship
# headers include <ultra64.h> themselves.
add_library(port_os STATIC src/port/os_thread.cpp src/port/os_devices.cpp src/port/transfer_pak.cpp src/port/gb_audio_out.cpp src/port/gb_core.c
  src/port/jpeg_task.cpp src/port/gfx_translate.cpp)
target_include_directories(port_os PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/port
  ${CMAKE_CURRENT_SOURCE_DIR}/game/port
  ${CMAKE_CURRENT_SOURCE_DIR}/game/port/PR
  ${CMAKE_CURRENT_SOURCE_DIR}/game/include
  ${CMAKE_CURRENT_SOURCE_DIR}/game
  ${CMAKE_CURRENT_SOURCE_DIR}/game/lib/ultralib/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include/libultraship)
target_compile_definitions(port_os PRIVATE LANGUAGE_C _LANGUAGE_C F3DEX_GBI_2 NDEBUG _FINALROM N_MICRO VERSION_US BUILD_VERSION=VERSION_I_P)
target_compile_options(port_os PRIVATE ${PORT_DEBUG_INFO} ${PORT_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/game/port/port_compat.h
  ${PORT_NO_WARNINGS})
# the GB Tower's sound output uses SDL3 directly (same SDL3 the window uses)
find_path(PORT_SDL3_INCLUDE SDL3/SDL.h HINTS /opt/homebrew/include /usr/local/include)
set_source_files_properties(src/port/gb_audio_out.cpp PROPERTIES INCLUDE_DIRECTORIES "${PORT_SDL3_INCLUDE}")
target_link_libraries(pokestadium PRIVATE port_os stadium_game port_os)
if(WIN32 AND NOT MSVC) # mingw (gcc or clang)
  # system libraries MSVC links through #pragma comment(lib) in libultraship (DX11 shader compiler, DWM, audio GUIDs)
  target_link_libraries(pokestadium PRIVATE d3dcompiler dwmapi ksuser)
  # one self-contained exe: the C++ runtime (libc++/libstdc++, unwinder) linked in, no extra DLLs to ship
  target_link_options(pokestadium PRIVATE -static)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # lld resolves symbols across the static libraries in any order (GNU ld is single-pass: the game library uses
  # libultra functions from libultraship, which is linked first)
  target_link_options(pokestadium PRIVATE -fuse-ld=lld)
endif()

# Runtime resources (shaders, fonts) packed into an archive next to the executable.
add_custom_command(TARGET pokestadium POST_BUILD
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_o2r.py $<TARGET_FILE_DIR:pokestadium>/pokestadium.o2r
  COMMENT "Packing pokestadium.o2r")

# Debug aid: cmake -DPORT_ASAN=ON builds the game code and the port layer with AddressSanitizer (finds the
# out-of-bounds writes that 32-bit struct layouts turn into on 64-bit).
option(PORT_ASAN "Build game code with AddressSanitizer" OFF)
if(PORT_ASAN)
  foreach(t stadium_game port_os)
    target_compile_options(${t} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  endforeach()
  target_link_options(pokestadium PRIVATE -fsanitize=address)
endif()

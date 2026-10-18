cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(PokemonStadiumPort LANGUAGES C CXX ASM)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# libultraship is built as a subproject; it is the N64 SDK reimplementation
# (windowing, Fast3D renderer, controllers, audio backends, resource manager).
set(GBI_UCODE F3DEX_GBI_2 CACHE STRING "" FORCE)
add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)

add_executable(pokestadium
  src/port/main.cpp
  src/port/Engine.cpp
  src/port/stubs.c
  src/port/generated/abs_syms.S
  ${CMAKE_BINARY_DIR}/generated/word_arrays.c
)
# C arrays of 16/32-bit words in the game sources (textures among them need N64 byte order; see gen_word_arrays.py)
file(GLOB_RECURSE STADIUM_GAME_C CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game/src/*.c)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/generated/word_arrays.c
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
  COMMAND python3 -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_word_arrays.py ${CMAKE_CURRENT_SOURCE_DIR}/game/src
          ${CMAKE_BINARY_DIR}/generated/word_arrays.c
  DEPENDS ${STADIUM_GAME_C} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_word_arrays.py
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
add_library(stadium_game STATIC ${STADIUM_GAME_SOURCES})
# Per-fragment data sections, restored whenever the game loads the fragment (see tools/gen_frag_sections.py)
execute_process(COMMAND python3 -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_frag_sections.py
                        ${CMAKE_CURRENT_SOURCE_DIR}/game/src ${CMAKE_BINARY_DIR}/generated)
include(${CMAKE_BINARY_DIR}/generated/frag_sections.cmake)
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
  -g -fno-builtin -funsigned-char -w
  # keep a symbol per static array: arm64 global merging folds them into _MergedGlobals, and the renderer finds the
  # 16/32-bit word arrays it must byte-swap (textures, palettes; gen_word_arrays.py) by symbol name
  -mno-global-merge
  -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-incompatible-pointer-types
  -Wno-return-mismatch
  -include ${CMAKE_CURRENT_SOURCE_DIR}/game/port/port_compat.h)

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
target_compile_options(port_os PRIVATE -g -include ${CMAKE_CURRENT_SOURCE_DIR}/game/port/port_compat.h -w)
# the GB Tower's sound output uses SDL3 directly (same SDL3 the window uses)
find_path(PORT_SDL3_INCLUDE SDL3/SDL.h HINTS /opt/homebrew/include /usr/local/include)
set_source_files_properties(src/port/gb_audio_out.cpp PROPERTIES INCLUDE_DIRECTORIES "${PORT_SDL3_INCLUDE}")
target_link_libraries(pokestadium PRIVATE port_os stadium_game port_os)

# Runtime resources (shaders, fonts) packed into an archive next to the executable.
add_custom_command(TARGET pokestadium POST_BUILD
  COMMAND python3 -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_o2r.py $<TARGET_FILE_DIR:pokestadium>/pokestadium.o2r
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

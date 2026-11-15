cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(PokemonStadiumPort VERSION 0.0.2 LANGUAGES C CXX ASM)
# The release version (release.yml passes it as -DPORT_VERSION=<version>); defaults to the project version above.
# Used for the Windows file version and the AppImage/app bundle metadata.
set(PORT_VERSION "${PROJECT_VERSION}" CACHE STRING "Version of this build")
if(NOT PORT_VERSION)
  set(PORT_VERSION "${PROJECT_VERSION}")
endif()
# Redact builder paths in every build, including ordinary local and CI builds.
include(cmake/BuildPaths.cmake)

if(APPLE)
  enable_language(OBJCXX)
endif()
if(WIN32)
  enable_language(RC)
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
  set(PORT_ALL_WARNINGS /W4) # clang-cl: /W4 is -Wall -Wextra (its -Wall means -Weverything)
  set(PORT_UNSIGNED_CHAR /J)
  set(PORT_NO_BUILTIN /clang:-fno-builtin)
else()
  set(PORT_FORCE_INCLUDE "-include")
  set(PORT_DEBUG_INFO -g)
  set(PORT_NO_WARNINGS -w)
  set(PORT_ALL_WARNINGS -Wall -Wextra)
  set(PORT_UNSIGNED_CHAR -funsigned-char)
  set(PORT_NO_BUILTIN -fno-builtin)
endif()

# libultraship is built as a subproject; it is the N64 SDK reimplementation
# (windowing, Fast3D renderer, controllers, audio backends, resource manager).
set(GBI_UCODE F3DEX_GBI_2 CACHE STRING "" FORCE)
if(ANDROID)
  # Android: OpenGL ES 3 renderer (libultraship fetches SDL3 and the other libraries itself, see android/)
  set(USE_OPENGLES ON CACHE BOOL "" FORCE)
endif()
if(EMSCRIPTEN)
  # Web build (tools/build_web.sh): WebGL 2 through the OpenGL ES 3 renderer; libultraship fetches SDL3 and the other
  # libraries as for Android. All code is built as 64-bit WebAssembly (memory64) with threads (see PORT_WEB_* below).
  set(USE_OPENGLES ON CACHE BOOL "" FORCE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # fmt 10.1 (distro spdlog) + clang 20: FMT_STRING's consteval check fails to compile; plain constexpr is enough
  add_compile_definitions(FMT_CONSTEVAL=)
endif()
add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)

set(PORT_MAIN_SOURCES
  src/port/main.cpp
  src/port/Engine.cpp
  src/port/pacing.cpp
  src/port/interp.cpp
  src/port/launcher.cpp
  src/port/launcher_files.cpp
  src/port/launcher_reliability.cpp
  src/port/rom_check.cpp
  src/port/shader_warmup.cpp
  src/port/touch_controls.cpp
  src/port/xcheck.cpp
  src/port/save_editor/stadium_save.cpp
  src/port/save_editor/save_editor_ui.cpp
  src/port/stubs.c
  src/port/port_platform.c
)
if(NOT EMSCRIPTEN)  # web: an object file written by tools/gen_wasm_abs_syms.py instead (below)
  list(APPEND PORT_MAIN_SOURCES src/port/generated/abs_syms.S)
endif()
if(ANDROID)
  # SDL's Java activity (android/) loads the game as libmain.so and calls its main (SDL_main)
  add_library(pokestadium SHARED ${PORT_MAIN_SOURCES})
  set_target_properties(pokestadium PROPERTIES OUTPUT_NAME main)
  target_link_libraries(pokestadium PRIVATE android log EGL)
  target_sources(pokestadium PRIVATE src/port/pacing_android.cpp)
  # bind the library's own symbols inside it: the N64 addresses in src/port/generated/abs_syms.S are absolute symbols,
  # which must stay fixed numbers rather than go through the dynamic loader (which would relocate them)
  target_link_options(pokestadium PRIVATE -Wl,-Bsymbolic)
else()
  add_executable(pokestadium ${PORT_MAIN_SOURCES})
endif()
target_include_directories(pokestadium PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include/libultraship
)
target_compile_definitions(pokestadium PRIVATE F3DEX_GBI_2)
target_link_libraries(pokestadium PRIVATE libultraship)
if(NOT ANDROID AND NOT EMSCRIPTEN)
  # the save library's command-line driver, for tools/save_editor/tests/test_native.py (not shipped)
  add_executable(stadium_save_cli EXCLUDE_FROM_ALL src/port/save_editor/cli_main.cpp src/port/save_editor/stadium_save.cpp)
  find_package(nlohmann_json QUIET)
  if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(stadium_save_cli PRIVATE nlohmann_json::nlohmann_json)
  else()
    target_link_libraries(stadium_save_cli PRIVATE libultraship) # (its include path has nlohmann/json.hpp)
  endif()
  if(WIN32 AND NOT MSVC)
    target_link_options(stadium_save_cli PRIVATE -static)
  endif()
  add_executable(shader_cache_check EXCLUDE_FROM_ALL tools/shader_cache_check.cpp)
  target_link_libraries(shader_cache_check PRIVATE libultraship)
  if(WIN32 AND NOT MSVC)
    target_link_options(shader_cache_check PRIVATE -static)
  endif()
endif()
if(APPLE)
  target_sources(pokestadium PRIVATE src/port/pacing_mac.mm)
  target_link_libraries(pokestadium PRIVATE "-framework CoreVideo" "-framework QuartzCore" "-framework Metal")
endif()

# ---------------------------------------------------------------------------------------------
# Game code: the decompiled Pokemon Stadium (US) sources, compiled for the host.
# Flags mirror tools/compile_flags.txt (used by tools/syntax_check.sh).
# ---------------------------------------------------------------------------------------------
file(GLOB_RECURSE STADIUM_GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game/src/*.c)
# game/ is the vendored decomp (src, include, lib/ultralib): of its libultra sources, only the ones listed are compiled
file(STRINGS cmake/ultralib_sources.txt PORT_ULTRALIB_SOURCES REGEX "^[^#]")
list(TRANSFORM PORT_ULTRALIB_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/game/lib/ultralib/src/)
list(APPEND STADIUM_GAME_SOURCES ${PORT_ULTRALIB_SOURCES})
# tables generated from the decomp's link map (tools/gen_func_table.py, tools/gen_fragment_entries.py); they are compiled
# with the game code but kept in the port, so game/ holds only decomp code
list(APPEND STADIUM_GAME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/port_func_table.c
     ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/port_fragment_entries.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ultralib_sources.txt)
# C arrays of 16/32-bit words in the game sources (textures among them need N64 byte order): their files are compiled
# through generated wrappers that register the arrays with the renderer (see tools/gen_word_arrays.py)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Optional source audit. Keep speculative variants separate from the shipped warm-up lists.
add_custom_target(shader_source_inventory
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_source_list.py
          --output ${CMAKE_CURRENT_BINARY_DIR}/shader_source_candidates.txt
          --vulkan-output ${CMAKE_CURRENT_BINARY_DIR}/vulkan_source_candidates.txt
  VERBATIM)
if(WIN32)
  # Windows has no dladdr: cross-check caller rules use the same named game functions as the ROM callback table.
  execute_process(COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_xcheck_symbols.py
      ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/port_func_table.c
      ${CMAKE_BINARY_DIR}/generated/xcheck_symbols.c RESULT_VARIABLE PORT_XCHECK_SYMBOL_RESULT)
  if(NOT PORT_XCHECK_SYMBOL_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not generate Windows cross-check caller symbols")
  endif()
  target_sources(pokestadium PRIVATE ${CMAKE_BINARY_DIR}/generated/xcheck_symbols.c)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_xcheck_symbols.py
      ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/port_func_table.c)
endif()
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
if(EMSCRIPTEN)
  # web build: FragmentEntry-typed thunks for the fragment entries (WebAssembly traps on calls through another type)
  execute_process(COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_fragment_thunks.py
                          ${CMAKE_CURRENT_SOURCE_DIR}/game/src ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/port_fragment_entries.c
                          ${CMAKE_BINARY_DIR}/generated)
  list(APPEND STADIUM_GAME_SOURCES ${CMAKE_BINARY_DIR}/generated/fragment_thunks.c)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_fragment_thunks.py)
endif()
# Game-code diagnostics, modelled on 2Ship2Harkinian (mm/CMakeLists.txt): -Wall -Wextra without -Werror, minus the
# warnings decompiled code produces by design. The type-mismatch groups (int-conversion, incompatible pointer types)
# are still off while those are fixed subsystem by subsystem; remove an entry here once its count reaches zero.
set(STADION_GAME_WARNINGS
  ${PORT_ALL_WARNINGS} -Wno-error
  -Wno-unused-parameter         # decomp signatures keep parameters the body never reads
  -Wno-unused-function
  -Wno-unused-variable          # stack pads (s32 pad[2]) that keep the original frame layout
  -Wno-unused-but-set-variable
  -Wno-missing-field-initializers
  -Wno-missing-braces           # flat initialisers for nested structs/arrays, as in the original source
  -Wno-parentheses
  -Wno-sign-compare
  -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-incompatible-pointer-types
  -Wno-return-mismatch -Wno-return-type # older clang (macOS CI) rejects missing return values the decomp keeps
  -Werror=implicit-function-declaration)
# The vendored libultra sources stay silent, as 2Ship2Harkinian does for its libultra copy: they are upstream SDK
# code, not maintained here.
set_source_files_properties(${PORT_ULTRALIB_SOURCES} PROPERTIES COMPILE_OPTIONS ${PORT_NO_WARNINGS})
add_library(stadium_game STATIC ${STADIUM_GAME_SOURCES})
if(WIN32)
  # Keep Windows GDI declarations from colliding with game types.
  target_compile_definitions(stadium_game PRIVATE NOGDI)
endif()
# Per-fragment data sections, restored whenever the game loads the fragment (see tools/gen_frag_sections.py)
execute_process(COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_frag_sections.py
                        ${CMAKE_CURRENT_SOURCE_DIR}/game/src ${CMAKE_BINARY_DIR}/generated)
include(${CMAKE_BINARY_DIR}/generated/frag_sections.cmake)
# Type-safety ratchet: the files in cmake/strict_sources.txt are clean of the pointer/integer mismatches that break a
# 64-bit build (int-conversion, incompatible pointer types, pointer/int casts, 64-to-32 truncation, missing returns) and
# keep these as errors. Add a file once it compiles clean; when every game file is listed, the groups move into
# STADION_GAME_WARNINGS and this list goes away. Appended after frag_sections.cmake, which sets COMPILE_OPTIONS.
set(STADION_STRICT_GROUPS int-conversion incompatible-pointer-types incompatible-function-pointer-types
    pointer-to-int-cast int-to-pointer-cast return-type shorten-64-to-32)
set(STADION_STRICT_WARNINGS ${STADION_STRICT_GROUPS})
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  list(TRANSFORM STADION_STRICT_WARNINGS PREPEND /clang:-Werror=)
else()
  list(TRANSFORM STADION_STRICT_WARNINGS PREPEND -Werror=)
endif()
file(STRINGS cmake/strict_sources.txt STADION_STRICT_SOURCES REGEX "^[^#]")
list(TRANSFORM STADION_STRICT_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
set_property(SOURCE ${STADION_STRICT_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ${STADION_STRICT_WARNINGS})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/strict_sources.txt)
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
             ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/port_fragment_entries.c)
set_target_properties(stadium_game PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(stadium_game PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/port # StadionPort.h: the game/port boundary
  ${CMAKE_CURRENT_SOURCE_DIR}/game
  ${CMAKE_CURRENT_SOURCE_DIR}/src/port/sdk
  ${CMAKE_CURRENT_SOURCE_DIR}/src/port/sdk/PR
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
  PORT_STATIC_FRAGMENTS)
target_compile_options(stadium_game PRIVATE
  ${PORT_DEBUG_INFO} ${PORT_NO_BUILTIN} ${PORT_UNSIGNED_CHAR} ${STADION_GAME_WARNINGS}
  # keep a symbol per static array: arm64 global merging folds them into _MergedGlobals, and the renderer finds the
  # 16/32-bit word arrays it must byte-swap (textures, palettes; gen_word_arrays.py) by symbol name
  $<$<AND:$<C_COMPILER_ID:Clang,AppleClang>,$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},arm64>>:-mno-global-merge>
  $<$<AND:$<C_COMPILER_ID:Clang,AppleClang>,$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>>:-mno-global-merge>
  ${PORT_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/src/port/sdk/port_compat.h)

# libultra runtime for the game (threads, message queues, devices). This target sees the game's SDK shim
# headers (src/port/sdk/ultra64.h etc.); the LUS-facing code in the executable must not, because libultraship
# headers include <ultra64.h> themselves.
add_library(port_os STATIC src/port/os_thread.cpp src/port/os_devices.cpp src/port/transfer_pak.cpp src/port/gb_audio_out.cpp src/port/audio_out.cpp src/port/sound_settings.cpp src/port/audio_hle.cpp src/port/gb_core.c
  src/port/jpeg_task.cpp src/port/gfx_translate.cpp)
if(EMSCRIPTEN)
  target_sources(port_os PRIVATE src/port/web_audio.cpp) # sound output through an AudioWorklet (web_audio.h)
endif()
target_include_directories(port_os PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/port
  ${CMAKE_CURRENT_SOURCE_DIR}/src/port/sdk
  ${CMAKE_CURRENT_SOURCE_DIR}/src/port/sdk/PR
  ${CMAKE_CURRENT_SOURCE_DIR}/game/include
  ${CMAKE_CURRENT_SOURCE_DIR}/game
  ${CMAKE_CURRENT_SOURCE_DIR}/game/lib/ultralib/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include
  ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include/libultraship)
set(PORT_BUILD_DESCRIPTION "${PORT_VERSION} (${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})")
set(PORT_SOURCE_REVISION "" CACHE STRING "Source commit for this build (auto-detected in Git checkouts)")
if(NOT PORT_SOURCE_REVISION)
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --verify HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE PORT_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(PORT_SOURCE_REVISION)
      execute_process(COMMAND ${GIT_EXECUTABLE} symbolic-ref -q HEAD
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                      OUTPUT_VARIABLE PORT_GIT_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      foreach(PORT_GIT_FILE HEAD "${PORT_GIT_REF}" packed-refs)
        if(PORT_GIT_FILE)
          execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute --git-path "${PORT_GIT_FILE}"
                          WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                          OUTPUT_VARIABLE PORT_GIT_PATH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
          if(EXISTS "${PORT_GIT_PATH}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PORT_GIT_PATH}")
          endif()
        endif()
      endforeach()
    endif()
  endif()
endif()
if(NOT PORT_SOURCE_REVISION)
  set(PORT_SOURCE_REVISION "unknown")
endif()
string(APPEND PORT_BUILD_DESCRIPTION " revision ${PORT_SOURCE_REVISION}")
target_compile_definitions(pokestadium PRIVATE PORT_BUILD_VERSION="${PORT_BUILD_DESCRIPTION}")
target_compile_definitions(port_os PRIVATE LANGUAGE_C _LANGUAGE_C F3DEX_GBI_2 NDEBUG _FINALROM N_MICRO VERSION_US BUILD_VERSION=VERSION_I_P)
target_compile_options(port_os PRIVATE ${PORT_DEBUG_INFO} ${PORT_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/src/port/sdk/port_compat.h
  ${PORT_ALL_WARNINGS} -Wno-error)
# the GB Tower's sound output uses SDL3 directly (same SDL3 the window uses)
if(ANDROID OR EMSCRIPTEN)  # the SDL3 that libultraship fetched (no system copy to find)
  target_link_libraries(port_os PRIVATE SDL3::Headers)
else()
  find_path(PORT_SDL3_INCLUDE SDL3/SDL.h HINTS /opt/homebrew/include /usr/local/include)
  set_source_files_properties(src/port/gb_audio_out.cpp src/port/audio_out.cpp PROPERTIES INCLUDE_DIRECTORIES "${PORT_SDL3_INCLUDE}")
endif()
target_link_libraries(pokestadium PRIVATE port_os stadium_game port_os)
if(WIN32)
  # a window-only program (no console window next to the game; main.cpp attaches to the console it was started from),
  # with the icon and version information from src/port/pokestadium.rc.in
  set_target_properties(pokestadium PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(pokestadium PRIVATE winmm) # timeBeginPeriod (pacing.cpp)
  string(REGEX MATCHALL "[0-9]+" _v "${PORT_VERSION}.0.0.0.0")
  list(SUBLIST _v 0 4 _v)
  list(JOIN _v "," PORT_VERSION_RC)
  set(PORT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/tools/launcher/assets/pokestadium.ico)
  configure_file(src/port/pokestadium.rc.in ${CMAKE_BINARY_DIR}/generated/pokestadium.rc @ONLY)
  set_source_files_properties("${CMAKE_BINARY_DIR}/generated/pokestadium.rc" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/port/pokestadium.manifest")
  target_sources(pokestadium PRIVATE ${CMAKE_BINARY_DIR}/generated/pokestadium.rc)
endif()
if(WIN32 AND NOT MSVC) # mingw (gcc or clang)
  # system libraries MSVC links through #pragma comment(lib) in libultraship (DX11 shader compiler, DWM, audio GUIDs)
  target_link_libraries(pokestadium PRIVATE d3dcompiler dwmapi ksuser dbghelp)
  # one self-contained exe: the C++ runtime (libc++/libstdc++, unwinder) linked in, no extra DLLs to ship
  target_link_options(pokestadium PRIVATE -static)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # lld resolves symbols across the static libraries in any order (GNU ld is single-pass: the game library uses
  # libultra functions from libultraship, which is linked first)
  target_link_options(pokestadium PRIVATE -fuse-ld=lld)
endif()

if(EMSCRIPTEN)
  # The port tells N64 addresses from host pointers by their value (port_platform.c): host memory must lie above 4 GB,
  # so the program's static data, stacks and heap start there (the 4 GB below stay untouched, not backed by real memory).
  set(PORT_WEB_GLOBAL_BASE 4294967296)
  # The decompiled code calls functions through pointer types that differ from their definitions: harmless on the N64
  # and on native hosts, a trap in WebAssembly. Emscripten's emulation makes every indirect call tolerant (larger and
  # somewhat slower). The fragment entries have exact-type thunks (tools/gen_fragment_thunks.py), but the callbacks
  # (geo layouts, particles, widgets: any of the ~15,000 functions the N64 addresses resolve to) are called through
  # many types, so PORT_WEB_FPCAST=OFF still traps at the first geo callback.
  option(PORT_WEB_FPCAST "Web build: emulate function pointer casts" ON)
  if(PORT_WEB_FPCAST)
    target_link_options(pokestadium PRIVATE -sEMULATE_FUNCTION_POINTER_CASTS=1
                        "-sBINARYEN_EXTRA_PASSES=--pass-arg=max-func-params@64")
  endif()
  # the game's absolute symbols (src/port/generated/abs_syms.S), as a wasm object; it must come first on the link line
  # (the linker options precede the objects), see tools/gen_wasm_abs_syms.py
  set(PORT_WEB_ABS_OBJ ${CMAKE_BINARY_DIR}/generated/abs_syms_wasm.o)
  add_custom_command(OUTPUT ${PORT_WEB_ABS_OBJ}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_wasm_abs_syms.py
            ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/abs_syms.S ${PORT_WEB_ABS_OBJ} ${PORT_WEB_GLOBAL_BASE}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_wasm_abs_syms.py ${CMAKE_CURRENT_SOURCE_DIR}/src/port/generated/abs_syms.S)
  add_custom_target(port_web_abs_syms DEPENDS ${PORT_WEB_ABS_OBJ})
  # each fragment's static data, from the linker map (tools/gen_frag_ranges.py); make_o2r.py packs it (below)
  add_custom_command(TARGET pokestadium POST_BUILD
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_frag_ranges.py ${CMAKE_BINARY_DIR}/pokestadium.map
            ${CMAKE_CURRENT_SOURCE_DIR}/game/src ${CMAKE_BINARY_DIR}/generated/frag_sections.txt
            ${CMAKE_BINARY_DIR}/frag_ranges.txt
    COMMENT "Finding the fragments' data in the linker map")
  set(PORT_O2R_EXTRA --add port/frag_ranges.txt=${CMAKE_BINARY_DIR}/frag_ranges.txt)
  add_dependencies(pokestadium port_web_abs_syms)
  set_property(TARGET pokestadium APPEND PROPERTY LINK_DEPENDS ${PORT_WEB_ABS_OBJ})
  set_target_properties(pokestadium PROPERTIES SUFFIX ".js")
  target_link_options(pokestadium BEFORE PRIVATE ${PORT_WEB_ABS_OBJ})
  target_link_options(pokestadium PRIVATE
    -sGLOBAL_BASE=${PORT_WEB_GLOBAL_BASE}
    # 4 GB unused below the program, plus room for it (the game, the renderer's caches, the ROM)
    -sINITIAL_MEMORY=5GB -sALLOW_MEMORY_GROWTH=0
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
    # every libultra thread is a host thread (os_thread.cpp): workers made ahead of time, since a new worker only starts
    # once the browser's main thread returns to its event loop
    -sPTHREAD_POOL_SIZE=32 -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=2MB
    # the page (web/) loads the ROM and the saves before it starts the game (callMain)
    -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1 -lidbfs.js
    "-sEXPORTED_RUNTIME_METHODS=callMain,ccall,FS,IDBFS" "-sEXPORTED_FUNCTIONS=_main,_setenv,_Port_ViTick"
    # PORT_WEB_EXTRA_LINK: link flags for debugging, e.g. "-sASSERTIONS=1 -sSAFE_HEAP=1" (checks every memory access)
    # the linker map: tools/gen_frag_ranges.py finds each fragment's static data in it (see frag_sections.c)
    "-Wl,--Map=${CMAKE_BINARY_DIR}/pokestadium.map"
    -sENVIRONMENT=web,worker ${PORT_WEB_EXTRA_LINK}
    --profiling-funcs) # function names in the browser's stack traces
  file(GLOB_RECURSE PORT_WEB_RESOURCE_INPUTS CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/src/fast/shaders/*
       ${CMAKE_CURRENT_SOURCE_DIR}/tools/launcher/assets/*.png)
  set_property(TARGET pokestadium APPEND PROPERTY LINK_DEPENDS
               ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_o2r.py
               ${CMAKE_CURRENT_SOURCE_DIR}/src/port/vulkan_pipeline_warmup.txt
               ${CMAKE_CURRENT_SOURCE_DIR}/src/port/shader_warmup.txt ${PORT_WEB_RESOURCE_INPUTS})
  add_custom_command(TARGET pokestadium POST_BUILD
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_o2r.py
            $<TARGET_FILE_DIR:pokestadium>/pokestadium.o2r ${PORT_O2R_EXTRA}
    COMMENT "Packing web resources")
else()
# Runtime resources (shaders, the shader warm-up list, launcher pictures) packed into pokestadium.o2r and built
# into the executable as a C array (tools/make_o2r.py --c): no separate file to lose or damage. The archive itself is
# left in generated/ for inspection.
file(GLOB_RECURSE PORT_O2R_INPUTS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/src/fast/shaders/*
     ${CMAKE_CURRENT_SOURCE_DIR}/tools/launcher/assets/*.png)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/generated/pokestadium_o2r.c
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_o2r.py
          ${CMAKE_BINARY_DIR}/generated/pokestadium.o2r --c ${CMAKE_BINARY_DIR}/generated/pokestadium_o2r.c
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_o2r.py ${CMAKE_CURRENT_SOURCE_DIR}/src/port/shader_warmup.txt
          ${CMAKE_CURRENT_SOURCE_DIR}/src/port/vulkan_pipeline_warmup.txt
          ${PORT_O2R_INPUTS}
  COMMENT "Packing pokestadium.o2r")
target_sources(pokestadium PRIVATE ${CMAKE_BINARY_DIR}/generated/pokestadium_o2r.c)
endif()

# Debug aid: cmake -DPORT_ASAN=ON builds the game code and the port layer with AddressSanitizer (finds the
# out-of-bounds writes that 32-bit struct layouts turn into on 64-bit).
option(PORT_ASAN "Build game code with AddressSanitizer" OFF)
if(PORT_ASAN)
  foreach(t stadium_game port_os)
    target_compile_options(${t} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  endforeach()
  target_link_options(pokestadium PRIVATE -fsanitize=address)
endif()

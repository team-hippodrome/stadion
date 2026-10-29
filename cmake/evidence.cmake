# Function-evidence build (tools/evidence/README.md): configure a separate build directory with
#   cmake -S . -B build-evidence -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_INCLUDE=$PWD/cmake/evidence.cmake
# It compiles the files matching PORT_EVID_FILES (a regex on the source path, default the battle engine) with
# -finstrument-functions and adds the hook runtime src/port/evidence/evidence_rt.c; no other source changes.
set(PORT_EVID_FILES "fragments/battle_engine/" CACHE STRING "regex of the game sources to instrument")
# branch and line coverage of the same files (clang source coverage: LLVM_PROFILE_FILE, llvm-cov), for the conditions
# that keep code unreached (tools/evidence/frontier.py)
option(PORT_EVID_COVERAGE "also compile the instrumented files with -fprofile-instr-generate -fcoverage-mapping" ON)

function(port_evidence_apply)
  get_target_property(_srcs stadium_game SOURCES)
  set(_n 0)
  foreach(_s IN LISTS _srcs)
    set(_match FALSE)
    if(_s MATCHES "${PORT_EVID_FILES}")
      set(_match TRUE)
    elseif(_s MATCHES "/generated/")
      # word-array wrappers include the original file: instrument them when that file matches
      if(EXISTS "${_s}")
        file(STRINGS "${_s}" _inc REGEX "#include")
        if(_inc MATCHES "${PORT_EVID_FILES}")
          set(_match TRUE)
        endif()
      endif()
    endif()
    if(_match)
      set_property(SOURCE "${_s}" TARGET_DIRECTORY stadium_game APPEND PROPERTY COMPILE_OPTIONS
                   -finstrument-functions -fno-omit-frame-pointer -fno-optimize-sibling-calls)
      if(PORT_EVID_COVERAGE)
        set_property(SOURCE "${_s}" TARGET_DIRECTORY stadium_game APPEND PROPERTY COMPILE_OPTIONS
                     -fprofile-instr-generate -fcoverage-mapping)
      endif()
      math(EXPR _n "${_n} + 1")
    endif()
  endforeach()
  target_sources(stadium_game PRIVATE ${CMAKE_SOURCE_DIR}/src/port/evidence/evidence_rt.c)
  if(PORT_EVID_COVERAGE)
    target_link_options(pokestadium PRIVATE -fprofile-instr-generate)
  endif()
  message(STATUS "evidence: instrumenting ${_n} game sources matching '${PORT_EVID_FILES}'")
endfunction()

# (this file runs at every project() call, libultraship's included: schedule the setup once)
get_property(_port_evid_done GLOBAL PROPERTY PORT_EVID_SCHEDULED)
if(NOT _port_evid_done)
  set_property(GLOBAL PROPERTY PORT_EVID_SCHEDULED TRUE)
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL port_evidence_apply)
endif()

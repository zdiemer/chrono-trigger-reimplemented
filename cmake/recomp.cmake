# Generated blocks (tools/recomp, docs/RECOMP.md): C for every statically found
# code block of the user's ROM, generated into the build tree at build time.
#
#   CT_ROM              the ROM to generate from (default roms/ct-usa.sfc); without
#                       it (or without Python 3.11+) ct_recomp is an empty table
#   CT_RECOMP_COVERAGE  coverage files from ct_dev --coverage (files or glob
#                       patterns, ;-separated) seeding the analysis; optional
#   CT_RECOMP_CALLS     call logs from ct_dev --calls (files or patterns); optional
#   CT_RECOMP_SHARDS    number of generated source files (default 64)
#   CT_RECOMP_OPT       optimization flag for the generated files (default -O1 / /O1)

set(CT_ROM "${CMAKE_CURRENT_SOURCE_DIR}/roms/ct-usa.sfc" CACHE FILEPATH "ROM to generate blocks from")
set(CT_RECOMP_COVERAGE "" CACHE STRING "Coverage files or glob patterns (ct_dev --coverage) seeding the analysis")
set(CT_RECOMP_CALLS "" CACHE STRING "Call logs or glob patterns (ct_dev --calls)")
set(CT_RECOMP_SHARDS 64 CACHE STRING "Number of generated block source files")
if(MSVC)
  set(CT_RECOMP_OPT_DEFAULT /O1)
else()
  set(CT_RECOMP_OPT_DEFAULT -O1)
endif()
set(CT_RECOMP_OPT "${CT_RECOMP_OPT_DEFAULT}" CACHE STRING "Optimization flag for generated blocks")

function(ct_recomp_expand out)
  set(files)
  foreach(pattern IN LISTS ARGN)
    if(pattern MATCHES "[*?]")
      file(GLOB matched CONFIGURE_DEPENDS "${pattern}")
      list(SORT matched)
      list(APPEND files ${matched})
    elseif(EXISTS "${pattern}")
      list(APPEND files "${pattern}")
    else()
      message(WARNING "CT_RECOMP: ${pattern} not found")
    endif()
  endforeach()
  set(${out} ${files} PARENT_SCOPE)
endfunction()

if(Python3_Interpreter_FOUND AND EXISTS "${CT_ROM}")
  set(RECOMP_DIR ${CMAKE_CURRENT_BINARY_DIR}/recomp)
  ct_recomp_expand(RECOMP_COVERAGE_FILES ${CT_RECOMP_COVERAGE})
  ct_recomp_expand(RECOMP_CALLS_FILES ${CT_RECOMP_CALLS})
  set(RECOMP_ARGS --rom "${CT_ROM}" --out "${RECOMP_DIR}" --shards ${CT_RECOMP_SHARDS}
      --hints re/hints.toml --registry game/registry.c --stamp "${RECOMP_DIR}/generated.stamp")
  foreach(f IN LISTS RECOMP_COVERAGE_FILES)
    list(APPEND RECOMP_ARGS --coverage "${f}")
  endforeach()
  foreach(f IN LISTS RECOMP_CALLS_FILES)
    list(APPEND RECOMP_ARGS --calls "${f}")
  endforeach()
  set(RECOMP_SOURCES ${RECOMP_DIR}/table.c)
  math(EXPR last "${CT_RECOMP_SHARDS} - 1")
  foreach(i RANGE ${last})
    string(LENGTH "${i}" len)
    if(len EQUAL 1)
      set(i "00${i}")
    elseif(len EQUAL 2)
      set(i "0${i}")
    endif()
    list(APPEND RECOMP_SOURCES ${RECOMP_DIR}/blocks_${i}.c)
  endforeach()
  file(GLOB RECOMP_GENERATOR CONFIGURE_DEPENDS tools/recomp/*.py tools/disasm/*.py)
  # The stamp is the output; the sources are byproducts rewritten only when their
  # content changes, so Ninja recompiles just those.
  add_custom_command(OUTPUT ${RECOMP_DIR}/generated.stamp
    BYPRODUCTS ${RECOMP_SOURCES}
    COMMAND ${Python3_EXECUTABLE} -m tools.recomp ${RECOMP_ARGS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS "${CT_ROM}" re/hints.toml game/registry.c ${RECOMP_COVERAGE_FILES} ${RECOMP_CALLS_FILES}
            ${RECOMP_GENERATOR}
    COMMENT "Generating blocks from the ROM (tools/recomp)"
    VERBATIM)
  add_custom_target(ct_recomp_generate DEPENDS ${RECOMP_DIR}/generated.stamp)
  add_library(ct_recomp STATIC ${RECOMP_SOURCES})
  add_dependencies(ct_recomp ct_recomp_generate)
  set_source_files_properties(${RECOMP_SOURCES} PROPERTIES GENERATED TRUE COMPILE_OPTIONS "${CT_RECOMP_OPT}")
  list(LENGTH RECOMP_COVERAGE_FILES ncov)
  list(LENGTH RECOMP_CALLS_FILES ncalls)
  message(STATUS "Generated blocks: from ${CT_ROM} (${ncov} coverage files, ${ncalls} call logs)")
else()
  add_library(ct_recomp STATIC runtime/port/recomp_table_stub.c)
  if(NOT Python3_Interpreter_FOUND)
    message(STATUS "Generated blocks: Python 3.11+ not found, building an empty table")
  else()
    message(STATUS "Generated blocks: no ROM at CT_ROM (${CT_ROM}), building an empty table")
  endif()
endif()
target_link_libraries(ct_recomp PUBLIC ct_runtime)

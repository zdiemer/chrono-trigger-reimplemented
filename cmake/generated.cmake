# Generated blocks (tools/generate, docs/RECOMP.md): C for every statically found
# code block of the user's ROM, generated into the build tree at build time.
#
#   CT_ROM              the ROM to generate from (default roms/ct-usa.sfc); without
#                       it (or without Python 3.11+) ct_generated is an empty table
#   CT_GENERATED_COVERAGE  coverage files from ct_dev --coverage (files or glob
#                       patterns, ;-separated) seeding the analysis; optional
#   CT_GENERATED_CALLS     call logs from ct_dev --calls (files or patterns); optional
#   CT_GENERATED_SHARDS    number of generated source files (default 64)
#   CT_GENERATED_OPT       optimization flag for the generated files (default -O1 / /O1)

set(CT_ROM "${CMAKE_CURRENT_SOURCE_DIR}/roms/ct-usa.sfc" CACHE FILEPATH "ROM to generate blocks from")
set(CT_GENERATED_COVERAGE "" CACHE STRING "Coverage files or glob patterns (ct_dev --coverage) seeding the analysis")
set(CT_GENERATED_CALLS "" CACHE STRING "Call logs or glob patterns (ct_dev --calls)")
set(CT_GENERATED_SHARDS 64 CACHE STRING "Number of generated block source files")
if(MSVC)
  set(CT_GENERATED_OPT_DEFAULT /O1)
else()
  set(CT_GENERATED_OPT_DEFAULT -O1)
endif()
set(CT_GENERATED_OPT "${CT_GENERATED_OPT_DEFAULT}" CACHE STRING "Optimization flag for generated blocks")

function(ct_generated_expand out)
  set(files)
  foreach(pattern IN LISTS ARGN)
    if(pattern MATCHES "[*?]")
      file(GLOB matched CONFIGURE_DEPENDS "${pattern}")
      list(SORT matched)
      list(APPEND files ${matched})
    elseif(EXISTS "${pattern}")
      list(APPEND files "${pattern}")
    else()
      message(WARNING "CT_GENERATED: ${pattern} not found")
    endif()
  endforeach()
  set(${out} ${files} PARENT_SCOPE)
endfunction()

if(Python3_Interpreter_FOUND AND EXISTS "${CT_ROM}")
  set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  ct_generated_expand(GENERATED_COVERAGE_FILES ${CT_GENERATED_COVERAGE})
  ct_generated_expand(GENERATED_CALLS_FILES ${CT_GENERATED_CALLS})
  set(GENERATED_ARGS --rom "${CT_ROM}" --out "${GENERATED_DIR}" --shards ${CT_GENERATED_SHARDS}
      --hints re/hints.toml --registry game/registry.c --stamp "${GENERATED_DIR}/generated.stamp")
  foreach(f IN LISTS GENERATED_COVERAGE_FILES)
    list(APPEND GENERATED_ARGS --coverage "${f}")
  endforeach()
  foreach(f IN LISTS GENERATED_CALLS_FILES)
    list(APPEND GENERATED_ARGS --calls "${f}")
  endforeach()
  set(GENERATED_SOURCES ${GENERATED_DIR}/table.c)
  math(EXPR last "${CT_GENERATED_SHARDS} - 1")
  foreach(i RANGE ${last})
    string(LENGTH "${i}" len)
    if(len EQUAL 1)
      set(i "00${i}")
    elseif(len EQUAL 2)
      set(i "0${i}")
    endif()
    list(APPEND GENERATED_SOURCES ${GENERATED_DIR}/blocks_${i}.c)
  endforeach()
  file(GLOB GENERATED_GENERATOR CONFIGURE_DEPENDS tools/generate/*.py tools/disasm/*.py)
  # The stamp is the output; the sources are byproducts rewritten only when their
  # content changes, so Ninja recompiles just those.
  add_custom_command(OUTPUT ${GENERATED_DIR}/generated.stamp
    BYPRODUCTS ${GENERATED_SOURCES}
    COMMAND ${Python3_EXECUTABLE} -m tools.generate ${GENERATED_ARGS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS "${CT_ROM}" re/hints.toml game/registry.c ${GENERATED_COVERAGE_FILES} ${GENERATED_CALLS_FILES}
            ${GENERATED_GENERATOR}
    COMMENT "Generating blocks from the ROM (tools/generate)"
    VERBATIM)
  add_custom_target(ct_generate_blocks DEPENDS ${GENERATED_DIR}/generated.stamp)
  add_library(ct_generated STATIC ${GENERATED_SOURCES})
  add_dependencies(ct_generated ct_generate_blocks)
  set_source_files_properties(${GENERATED_SOURCES} PROPERTIES GENERATED TRUE COMPILE_OPTIONS "${CT_GENERATED_OPT}")
  list(LENGTH GENERATED_COVERAGE_FILES ncov)
  list(LENGTH GENERATED_CALLS_FILES ncalls)
  message(STATUS "Generated blocks: from ${CT_ROM} (${ncov} coverage files, ${ncalls} call logs)")
else()
  add_library(ct_generated STATIC runtime/port/generated_table_stub.c)
  if(NOT Python3_Interpreter_FOUND)
    message(STATUS "Generated blocks: Python 3.11+ not found, building an empty table")
  else()
    message(STATUS "Generated blocks: no ROM at CT_ROM (${CT_ROM}), building an empty table")
  endif()
endif()
target_link_libraries(ct_generated PUBLIC ct_runtime)

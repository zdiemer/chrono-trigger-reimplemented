# Matching source (tools/matching, docs/MATCHING.md): source generated from the
# user's ROM into the build tree, reassembled and checked against the ROM's CRC32
# and SHA-1. Uses CT_ROM, CT_GENERATED_COVERAGE and CT_GENERATED_CALLS from
# cmake/generated.cmake (include that first).
#
#   CT_MATCHING  build ct_matching with the default target (default ON); off, it
#                runs only with --target ct_matching
#   CT_ASAR      asar executable; when found, the same source is also assembled
#                with asar and must match too

option(CT_MATCHING "Check the matching source with every build (needs CT_ROM)" ON)
find_program(CT_ASAR NAMES asar DOC "asar, to assemble the matching source a second time")

if(Python3_Interpreter_FOUND AND EXISTS "${CT_ROM}")
  set(MATCHING_DIR ${CMAKE_CURRENT_BINARY_DIR}/matching)
  ct_generated_expand(MATCHING_COVERAGE_FILES ${CT_GENERATED_COVERAGE})
  ct_generated_expand(MATCHING_CALLS_FILES ${CT_GENERATED_CALLS})
  set(MATCHING_ARGS --rom "${CT_ROM}" --out "${MATCHING_DIR}" --hints re/hints.toml
      --registry game/registry.c --stamp "${MATCHING_DIR}/matching.stamp")
  foreach(f IN LISTS MATCHING_COVERAGE_FILES)
    list(APPEND MATCHING_ARGS --coverage "${f}")
  endforeach()
  foreach(f IN LISTS MATCHING_CALLS_FILES)
    list(APPEND MATCHING_ARGS --calls "${f}")
  endforeach()
  if(CT_ASAR)
    list(APPEND MATCHING_ARGS --asar "${CT_ASAR}")
  else()
    list(APPEND MATCHING_ARGS --asar off)
  endif()
  file(GLOB MATCHING_TOOL CONFIGURE_DEPENDS tools/matching/*.py tools/disasm/*.py)
  add_custom_command(OUTPUT ${MATCHING_DIR}/matching.stamp
    BYPRODUCTS ${MATCHING_DIR}/rom.sfc
    COMMAND ${Python3_EXECUTABLE} -m tools.matching ${MATCHING_ARGS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS "${CT_ROM}" re/hints.toml game/registry.c ${MATCHING_COVERAGE_FILES} ${MATCHING_CALLS_FILES}
            ${MATCHING_TOOL}
    COMMENT "Extracting, emitting and reassembling the ROM (tools/matching)"
    VERBATIM)
  if(CT_MATCHING)
    add_custom_target(ct_matching ALL DEPENDS ${MATCHING_DIR}/matching.stamp)
  else()
    add_custom_target(ct_matching DEPENDS ${MATCHING_DIR}/matching.stamp)
  endif()
  message(STATUS "Matching source: from ${CT_ROM}, asar ${CT_ASAR}")
else()
  message(STATUS "Matching source: no ROM at CT_ROM or no Python 3.11+, ct_matching not available")
endif()

# Fails the build where a line of fast_code.ld placed nothing in fast RAM: a
# function renamed or gone would otherwise drop back to the cartridge without
# a word. Run after the link:
#
#   cmake -DLIST=fast_code.ld -DMAP=ct_gba.map -P check_fast_code.cmake
#
# Each listed function's first name must be an input section the map places
# at a fast RAM address (0x03...), and each library member's .text likewise.
file(READ "${MAP}" map)
file(STRINGS "${LIST}" lines REGEX "^\\*")
set(missing "")
foreach(line IN LISTS lines)
  if(line MATCHES "^\\*[^:(]*:([^(]+)\\(\\.text\\)")
    # a library's member, as `*libgcc.a:_udivsi3.o(.text)`
    string(REPLACE "." "\\." member "${CMAKE_MATCH_1}")
    set(pattern "0x030[0-9a-f]+ +0x[0-9a-f]+ [^\n]*\\(${member}\\)")
  elseif(line MATCHES "^\\*\\((\\.text\\.[^ )]+)")
    string(REPLACE "." "\\." name "${CMAKE_MATCH_1}")
    set(pattern "\n ${name}([.][^ \n]*)?[ \n]+0x030[0-9a-f]+ ")
  else()
    continue()
  endif()
  if(NOT map MATCHES "${pattern}")
    string(APPEND missing "\n  ${line}")
  endif()
endforeach()
if(missing)
  message(FATAL_ERROR "fast_code.ld names code the build no longer has, which would run from the cartridge: "
                      "pick the list again with host/run.py --profile (targets/gba/README.md):${missing}")
endif()

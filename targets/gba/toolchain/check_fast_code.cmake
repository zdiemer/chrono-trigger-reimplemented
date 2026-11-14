# Checks after the link that each line of fast_code.ld placed code in fast
# RAM, so that hot code can't drop back to the cartridge unnoticed:
#
#   cmake -DLIST=fast_code.ld -DMAP=ct_gba.map -DROOT=<repo> -P check_fast_code.cmake
#
# A line whose function the sources no longer define (renamed or gone), or
# whose library member the link takes but leaves in the cartridge, fails the
# build. A line whose function is defined but placed nothing was inlined into
# its callers, and a library member the link doesn't take wasn't needed:
# compilers decide those differently, so they are warnings.
file(READ "${MAP}" map)
file(STRINGS "${LIST}" lines REGEX "^\\*")
file(GLOB_RECURSE sources "${ROOT}/game/*.c" "${ROOT}/game/*.h" "${ROOT}/runtime/*.c" "${ROOT}/runtime/*.h"
  "${ROOT}/frontend/*.c" "${ROOT}/frontend/*.h" "${ROOT}/targets/gba/*.c" "${ROOT}/targets/gba/*.h")
set(code "")
foreach(source IN LISTS sources)
  file(READ "${source}" text)
  string(APPEND code "${text}")
endforeach()
set(gone "")
set(inlined "")
foreach(line IN LISTS lines)
  if(line MATCHES "^\\*[^:(]*:([^(]+)\\(\\.text")
    # a library's member, as `*libgcc.a:_udivsi3.o(.text)`: one the link
    # takes must be in fast RAM; one it doesn't take, this compiler didn't need
    string(REPLACE "." "\\." member "${CMAKE_MATCH_1}")
    if(map MATCHES "0x030[0-9a-f]+ +0x[0-9a-f]+ [^\n]*\\(${member}\\)")
      continue()
    elseif(map MATCHES "0x08[0-9a-f]+ +0x0*[1-9a-f][0-9a-f]* [^\n]*\\(${member}\\)")
      string(APPEND gone "\n  ${line}")
    else()
      string(APPEND inlined "\n  ${line}")
    endif()
  elseif(line MATCHES "^\\*\\(\\.text\\.([A-Za-z_0-9]+)")
    set(function "${CMAKE_MATCH_1}")
    if(map MATCHES "\n \\.text\\.${function}([.][^ \n]*)?[ \n]+0x030[0-9a-f]+ ")
      continue()
    elseif(code MATCHES "[^A-Za-z_0-9]${function} *\\([^;]*\\)[ \n]*{")
      string(APPEND inlined "\n  ${line}")
    else()
      string(APPEND gone "\n  ${line}")
    endif()
  endif()
endforeach()
if(inlined)
  message(WARNING "fast_code.ld names code this compiler inlined or didn't need:${inlined}")
endif()
if(gone)
  message(FATAL_ERROR "fast_code.ld names code the program no longer has, which would leave hot code in the "
                      "cartridge: pick the list again with host/run.py --profile (targets/gba/README.md):${gone}")
endif()

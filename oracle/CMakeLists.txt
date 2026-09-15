cmake_minimum_required(VERSION 3.20)
project(ct_oracle C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(ct_oracle
  src/main.c
  src/dynlib.c
  src/media.c
  src/movie.c
  src/ppu_state.c
  src/trace.c)

target_include_directories(ct_oracle PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libretro)

option(CT_WERROR "Treat compiler warnings as errors (CI)" OFF)
if(MSVC)
  target_compile_options(ct_oracle PRIVATE /W4 $<$<BOOL:${CT_WERROR}>:/WX>)
  target_compile_definitions(ct_oracle PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(ct_oracle PRIVATE -Wall -Wextra $<$<BOOL:${CT_WERROR}>:-Werror>)
endif()

if(NOT WIN32)
  target_link_libraries(ct_oracle PRIVATE ${CMAKE_DL_LIBS})
endif()

# A MinGW build otherwise depends on the toolchain's runtime DLLs.
if(MINGW)
  target_link_options(ct_oracle PRIVATE -static)
endif()

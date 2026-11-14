# The GBA's compiler: arm-none-eabi GCC with newlib, on PATH (MSYS2's
# mingw-w64-x86_64-arm-none-eabi-toolchain, a distribution's
# gcc-arm-none-eabi, or devkitARM), for the ARM7TDMI in Thumb
# (docs/PLATFORM.md, "The GBA build, as built").
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "size")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CT_GBA_CPU "-mcpu=arm7tdmi -mthumb -mthumb-interwork")
set(CMAKE_C_FLAGS_INIT "${CT_GBA_CPU} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=arm7tdmi")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CT_GBA_CPU} -specs=nosys.specs -nostartfiles -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

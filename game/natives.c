#include "natives.h"

/* Entry preconditions on P. */
#define NEEDS_M8 .p_mask = CPU_FLAG_M, .p_value = CPU_FLAG_M
#define NEEDS_M8_X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M
#define NEEDS_M8_X8_BINARY .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = CPU_FLAG_M | CPU_FLAG_X

/* cost_milli is the original's time per native bus-access clock, x1000, from
 * the original/native-bus ratio shadow mode reports. dont_care comes from
 * disasm/out/liveness.json ("dead_after_return"). */
const NativeEntry game_natives[] = {
    { .addr = 0xC00B4E, .name = "c0_disable_display_and_interrupts", .fn = c0_disable_display_and_interrupts,
      NEEDS_M8, .cost_milli = 4107, .dont_care = DC_N | DC_Z },
    { .addr = 0xC00B64, .name = "c0_install_field_nmi", .fn = c0_install_field_nmi,
      NEEDS_M8_X16, .cost_milli = 3500, .dont_care = DC_N | DC_Z | DC_D },
    { .addr = 0xC00B75, .name = "c0_install_field_irq", .fn = c0_install_field_irq,
      NEEDS_M8_X16, .cost_milli = 3500, .dont_care = DC_N | DC_Z | DC_D },
    { .addr = 0xC0A88D, .name = "c0_update_object_visibility", .fn = c0_update_object_visibility,
      NEEDS_M8_X8_BINARY, .cost_milli = 4657, .dont_care = DC_X | DC_N | DC_Z },
    { .addr = 0xC0EA63, .name = "c0_field_nmi", .fn = c0_field_nmi, .cost_milli = 1120 },
    { .addr = 0xC0EC71, .name = "c0_wait_frame_loop", .fn = c0_wait_frame_loop, NEEDS_M8, .idle = true },
    { .addr = 0xC20348, .name = "c2_wait_frame_loop", .fn = c2_wait_frame_loop, NEEDS_M8, .idle = true },
    { .addr = 0xC281CA, .name = "c2_wait_ready_loop", .fn = c2_wait_ready_loop, NEEDS_M8, .idle = true },
    { .addr = 0xC30002, .name = "c3_decompress", .fn = c3_decompress, .cost_milli = 3953 },
};

const int game_native_count = (int)(sizeof game_natives / sizeof game_natives[0]);

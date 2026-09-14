#include "registry.h"

#include "attract/attract.h"
#include "core/display.h"
#include "data/decompress.h"
#include "field/field.h"
#include "menu/menu.h"

/* Entry preconditions on P. */
#define NEEDS_M8 .p_mask = CPU_FLAG_M, .p_value = CPU_FLAG_M
#define NEEDS_M16 .p_mask = CPU_FLAG_M, .p_value = 0
#define NEEDS_M8_X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M
#define NEEDS_M8_X8_BINARY .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = CPU_FLAG_M | CPU_FLAG_X

/* Entries are keyed by the original entry address. cost_milli is the
 * original's time per native bus-access clock, x1000, from the
 * original/native-bus ratio shadow mode reports. dont_care comes from
 * disasm/out/liveness.json ("dead_after_return"). */
const NativeEntry game_natives[] = {
    /* core */
    { .addr = 0xC00B4E, .name = "display_blank_and_stop_interrupts", .fn = display_blank_and_stop_interrupts,
      NEEDS_M8, .cost_milli = 4107, .dont_care = DC_N | DC_Z },

    /* data */
    { .addr = 0xC30002, .name = "decompress", .fn = decompress, .cost_milli = 3953 },

    /* field */
    { .addr = 0xC00B64, .name = "field_install_nmi", .fn = field_install_nmi,
      NEEDS_M8_X16, .cost_milli = 3500, .dont_care = DC_N | DC_Z | DC_D },
    { .addr = 0xC00B75, .name = "field_install_irq", .fn = field_install_irq,
      NEEDS_M8_X16, .cost_milli = 3500, .dont_care = DC_N | DC_Z | DC_D },
    { .addr = 0xC0EA63, .name = "field_nmi", .fn = field_nmi, .cost_milli = 1120 },
    { .addr = 0xC0EC71, .name = "field_wait_for_frame_loop", .fn = field_wait_for_frame_loop, NEEDS_M8, .idle = true },
    { .addr = 0xC0A88D, .name = "field_update_object_visibility", .fn = field_update_object_visibility,
      NEEDS_M8_X8_BINARY, .cost_milli = 4657, .dont_care = DC_X | DC_N | DC_Z },

    /* menu */
    { .addr = 0xC20348, .name = "menu_wait_for_frame_loop", .fn = menu_wait_for_frame_loop, NEEDS_M8, .idle = true },
    { .addr = 0xC281CA, .name = "menu_wait_ready_loop", .fn = menu_wait_ready_loop, NEEDS_M8, .idle = true },

    /* attract (runs from RAM; guarded) */
    { .addr = 0x7E3023, .name = "attract_wait_loop", .fn = attract_wait_loop, NEEDS_M16, .idle = true,
      .guard = attract_wait_loop_present },
    { .addr = 0x7E31B0, .name = "attract_vblank_end_loop", .fn = attract_vblank_end_loop, NEEDS_M8, .idle = true,
      .guard = attract_vblank_wait_present },
    { .addr = 0x7E31B7, .name = "attract_vblank_start_loop", .fn = attract_vblank_start_loop, NEEDS_M8, .idle = true,
      .guard = attract_vblank_wait_present },
};

const int game_native_count = (int)(sizeof game_natives / sizeof game_natives[0]);

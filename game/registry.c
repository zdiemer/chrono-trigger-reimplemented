#include "registry.h"

#include "attract/attract.h"
#include "core/display.h"
#include "data/decompress.h"
#include "event/event.h"
#include "field/field.h"
#include "menu/menu.h"

/* Entry preconditions on P. */
#define NEEDS_M8 .p_mask = CPU_FLAG_M, .p_value = CPU_FLAG_M
#define NEEDS_M16 .p_mask = CPU_FLAG_M, .p_value = 0
#define NEEDS_M8_X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M
#define NEEDS_M8_X8 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M | CPU_FLAG_X
#define NEEDS_M8_X16_BINARY .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = CPU_FLAG_M
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

    /* event engine */
    { .addr = 0xC058DE, .name = "event_run_init_scripts", .fn = event_run_init_scripts,
      NEEDS_M8_X16, .cost_milli = 4500 },
    { .addr = 0xC05A93, .name = "event_run_object_script", .fn = event_run_object_script,
      NEEDS_M8_X8, .cost_milli = 4500, .dont_care = DC_X | DC_N | DC_Z | DC_C },

    /* event commands. Entered through the command table with
     * 8-bit A and 16-bit X and Y; N and Z are dead after return. */
#define EVENT_CMD(address, function) \
    { .addr = address, .name = #function, .fn = function, NEEDS_M8_X16_BINARY, .cost_milli = 5000, \
      .dont_care = DC_N | DC_Z }
    EVENT_CMD(0xC062B5, event_cmd_jump_forward),
    EVENT_CMD(0xC062CB, event_cmd_jump_back),
    EVENT_CMD(0xC062DE, event_cmd_if_var_const),
    EVENT_CMD(0xC06313, event_cmd_if_var_const16),
    EVENT_CMD(0xC06361, event_cmd_if_var_var),
    EVENT_CMD(0xC063A4, event_cmd_if_var_var16),
    EVENT_CMD(0xC063E6, event_cmd_if_mem_const),
    EVENT_CMD(0xC02F96, event_cmd_wait),
    EVENT_CMD(0xC0353F, event_cmd_yield),
    EVENT_CMD(0xC03543, event_cmd_halt),
    EVENT_CMD(0xC06442, event_cmd_if_mem0_below),
    EVENT_CMD(0xC06568, event_cmd_if_object_value),
    EVENT_CMD(0xC0662C, event_cmd_if_object_in_view),
    EVENT_CMD(0xC06655, event_cmd_if_object_on_screen),
    /* input conditions */
    EVENT_CMD(0xC066A5, event_cmd_if_any_input),
    EVENT_CMD(0xC066B2, event_cmd_if_input_f8_bit1),
    EVENT_CMD(0xC066BC, event_cmd_if_input_f8_a),
    EVENT_CMD(0xC066C6, event_cmd_if_input_f2_a),
    EVENT_CMD(0xC066E5, event_cmd_if_input_f2_bit3),
    EVENT_CMD(0xC066EF, event_cmd_if_input_f2_bit6),
    EVENT_CMD(0xC066F9, event_cmd_if_input_f2_bit2),
    EVENT_CMD(0xC06705, event_cmd_if_input_f2_bit5),
    EVENT_CMD(0xC0670F, event_cmd_if_input_f2_bit4),
    EVENT_CMD(0xC06719, event_cmd_if_latched_50_bit1),
    EVENT_CMD(0xC06724, event_cmd_if_latched_50_bit7),
    EVENT_CMD(0xC06732, event_cmd_if_latched_51_bit7),
    /* facing */
    EVENT_CMD(0xC05535, event_cmd_face_0),
    EVENT_CMD(0xC05539, event_cmd_face_1),
    EVENT_CMD(0xC0553D, event_cmd_face_2),
    EVENT_CMD(0xC05541, event_cmd_face_3),
    EVENT_CMD(0xC0556C, event_cmd_object_face_0),
    EVENT_CMD(0xC05579, event_cmd_object_face_1),
    EVENT_CMD(0xC05586, event_cmd_object_face_2),
    EVENT_CMD(0xC05593, event_cmd_object_face_3),
    EVENT_CMD(0xC05545, event_cmd_face_operand),
    EVENT_CMD(0xC05557, event_cmd_face_variable),
    EVENT_CMD(0xC055A0, event_cmd_face_object),
    EVENT_CMD(0xC05605, event_cmd_face_party_member),
    /* variables */
    EVENT_CMD(0xC0653B, event_cmd_set_object_value_from_var),
    EVENT_CMD(0xC0654E, event_cmd_set_object_value_from_memory),
    EVENT_CMD(0xC0658F, event_cmd_set_var_from_2980),
    EVENT_CMD(0xC065AA, event_cmd_get_object_tile),
    EVENT_CMD(0xC065EB, event_cmd_get_party_member_tile),
    EVENT_CMD(0xC065F8, event_cmd_get_object_facing),
    EVENT_CMD(0xC0661F, event_cmd_get_party_member_facing),
    /* moving objects */
    EVENT_CMD(0xC05429, event_cmd_move_toward_party_member_until_on_screen),
    EVENT_CMD(0xC05215, event_cmd_move_toward_object),
    EVENT_CMD(0xC05410, event_cmd_move_toward_party_member),
    EVENT_CMD(0xC04F61, event_cmd_move_to_tile),
    EVENT_CMD(0xC0534A, event_cmd_move_toward_object_counted),
    EVENT_CMD(0xC0550E, event_cmd_move_toward_party_member_counted),
    EVENT_CMD(0xC04F0A, event_cmd_move_by_variables),
    EVENT_CMD(0xC052C0, event_cmd_move_toward_object_keep_facing),
    EVENT_CMD(0xC054F5, event_cmd_move_toward_party_member_keep_facing),
    EVENT_CMD(0xC03002, event_cmd_run_state_routine),
    EVENT_CMD(0xC03546, event_cmd_follow_object),
    EVENT_CMD(0xC0354F, event_cmd_follow_party_member),
#undef EVENT_CMD

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

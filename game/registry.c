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
    EVENT_CMD(0xC02FC6, event_cmd_wait_4),
    EVENT_CMD(0xC02FCA, event_cmd_wait_8),
    EVENT_CMD(0xC02FD2, event_cmd_wait_16),
    EVENT_CMD(0xC02FCE, event_cmd_wait_32),
    EVENT_CMD(0xC02FFD, event_cmd_run_state_routine_once),
    /* dialog */
    EVENT_CMD(0xC03557, event_cmd_set_dialog_table),
    EVENT_CMD(0xC03570, event_cmd_dialog),
    EVENT_CMD(0xC035B5, event_cmd_dialog_top),
    EVENT_CMD(0xC035FC, event_cmd_dialog_bottom),
    EVENT_CMD(0xC03643, event_cmd_choice),
    EVENT_CMD(0xC036AA, event_cmd_choice_top),
    EVENT_CMD(0xC036DD, event_cmd_choice_bottom),
    /* animation */
    EVENT_CMD(0xC02E67, event_cmd_loop_animation),
    EVENT_CMD(0xC02E9A, event_cmd_play_animation),
    EVENT_CMD(0xC02F71, event_cmd_static_frame),
    EVENT_CMD(0xC02E8F, event_cmd_reset_animation),
    EVENT_CMD(0xC02E87, event_cmd_loop_animation_0),
    EVENT_CMD(0xC02E8B, event_cmd_loop_animation_1),
    EVENT_CMD(0xC02F01, event_cmd_play_animation_for),
    /* calls */
    EVENT_CMD(0xC05F74, event_cmd_return),
    EVENT_CMD(0xC05FB6, event_cmd_call),
    EVENT_CMD(0xC0607E, event_cmd_call_when_accepted),
    EVENT_CMD(0xC06109, event_cmd_call_and_wait),
    EVENT_CMD(0xC061E9, event_cmd_call_party_member),
    EVENT_CMD(0xC061FE, event_cmd_call_party_member_when_accepted),
    EVENT_CMD(0xC06214, event_cmd_call_party_member_and_wait),
    EVENT_CMD(0xC06240, event_cmd_refuse_calls),
    EVENT_CMD(0xC0624B, event_cmd_accept_calls),
    EVENT_CMD(0xC06254, event_cmd_disable_object),
    EVENT_CMD(0xC06269, event_cmd_set_object_flag),
    EVENT_CMD(0xC06282, event_cmd_clear_object_flag),
    EVENT_CMD(0xC06295, event_cmd_set_1c80),
    EVENT_CMD(0xC062A4, event_cmd_set_arrival_flags),
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
    EVENT_CMD(0xC04E73, event_cmd_move_in_direction),
    EVENT_CMD(0xC05084, event_cmd_move_to_tile_from_variables),
    EVENT_CMD(0xC04FEA, event_cmd_move_to_tile_counted),
    EVENT_CMD(0xC04EC7, event_cmd_move_in_direction_keep_facing),
    EVENT_CMD(0xC05129, event_cmd_move_to_tile_keep_facing),
    EVENT_CMD(0xC05191, event_cmd_move_to_tile_from_variables_keep_facing),
    EVENT_CMD(0xC04D06, event_cmd_move_to_tile_with_velocity),
    EVENT_CMD(0xC04E25, event_cmd_move_with_velocity),
    /* object properties */
    EVENT_CMD(0xC04CEC, event_cmd_show_object_sprite),
    EVENT_CMD(0xC04CF9, event_cmd_hide_object_sprite),
    EVENT_CMD(0xC04CE6, event_cmd_set_sprite_flag_80),
    EVENT_CMD(0xC04867, event_cmd_set_1b01),
    EVENT_CMD(0xC04876, event_cmd_set_step_count),
    EVENT_CMD(0xC04B49, event_cmd_set_1a00),
    EVENT_CMD(0xC04B58, event_cmd_set_1a00_from_variable),
    EVENT_CMD(0xC04B74, event_cmd_set_tile_position),
    EVENT_CMD(0xC04BC3, event_cmd_set_tile_position_from_variables),
    EVENT_CMD(0xC04C27, event_cmd_set_position),
    EVENT_CMD(0xC04C74, event_cmd_set_sprite_priority),
    EVENT_CMD(0xC04CD5, event_cmd_show_sprite),
    EVENT_CMD(0xC04CE0, event_cmd_hide_sprite),
    /* variables and memory */
    EVENT_CMD(0xC06740, event_cmd_if_latched_51_bit3),
    EVENT_CMD(0xC0674E, event_cmd_if_latched_51_bit6),
    EVENT_CMD(0xC0675C, event_cmd_if_latched_51_bit2),
    EVENT_CMD(0xC0676A, event_cmd_if_latched_51_bit5),
    EVENT_CMD(0xC06778, event_cmd_if_latched_51_bit4),
    EVENT_CMD(0xC06788, event_cmd_set_6b),
    EVENT_CMD(0xC068A5, event_cmd_var_set),
    EVENT_CMD(0xC068C6, event_cmd_var_set16),
    EVENT_CMD(0xC068E9, event_cmd_var_copy),
    EVENT_CMD(0xC06913, event_cmd_var_copy16),
    EVENT_CMD(0xC06A1F, event_cmd_var_add),
    EVENT_CMD(0xC06A45, event_cmd_var_add_var),
    EVENT_CMD(0xC06A74, event_cmd_var_add_var16),
    EVENT_CMD(0xC06AA3, event_cmd_var_subtract),
    EVENT_CMD(0xC06AC9, event_cmd_var_subtract16),
    EVENT_CMD(0xC06AF1, event_cmd_var_subtract_var),
    EVENT_CMD(0xC06B20, event_cmd_var_set_bit),
    EVENT_CMD(0xC06B4B, event_cmd_var_clear_bit),
    EVENT_CMD(0xC06BD2, event_cmd_var_and),
    EVENT_CMD(0xC06BF7, event_cmd_var_or),
    EVENT_CMD(0xC06C1C, event_cmd_var_xor),
    EVENT_CMD(0xC06C41, event_cmd_var_shift_right),
    EVENT_CMD(0xC06C69, event_cmd_var_increment),
    EVENT_CMD(0xC06C85, event_cmd_var_increment16),
    EVENT_CMD(0xC06CA1, event_cmd_var_decrement),
    EVENT_CMD(0xC06CBD, event_cmd_var_set_1),
    EVENT_CMD(0xC06CD6, event_cmd_var_set16_1),
    EVENT_CMD(0xC06CF0, event_cmd_var_clear),
    EVENT_CMD(0xC06D09, event_cmd_var_random),
    EVENT_CMD(0xC06792, event_cmd_var_from_pointer),
    EVENT_CMD(0xC067C7, event_cmd_var_from_pointer16),
    EVENT_CMD(0xC067D7, event_cmd_poke),
    EVENT_CMD(0xC067FB, event_cmd_poke16),
    EVENT_CMD(0xC0680C, event_cmd_poke_variable),
    EVENT_CMD(0xC06829, event_cmd_poke_variable16),
    EVENT_CMD(0xC06846, event_cmd_copy_data),
    EVENT_CMD(0xC0693D, event_cmd_var_from_memory),
    EVENT_CMD(0xC06964, event_cmd_var_from_memory16),
    EVENT_CMD(0xC0698B, event_cmd_var_from_memory0),
    EVENT_CMD(0xC069A6, event_cmd_memory_set),
    EVENT_CMD(0xC069C4, event_cmd_memory_set_from_var),
    EVENT_CMD(0xC069EB, event_cmd_memory_set_from_var16),
    EVENT_CMD(0xC06A12, event_cmd_memory0_set),
    EVENT_CMD(0xC06B76, event_cmd_memory_set_bit),
    EVENT_CMD(0xC06BA4, event_cmd_memory_clear_bit),
    /* sound and screen */
    EVENT_CMD(0xC03C11, event_cmd_play_sound),
    EVENT_CMD(0xC03C2F, event_cmd_play_song),
    EVENT_CMD(0xC03C4C, event_cmd_sound_81_83),
    EVENT_CMD(0xC03C98, event_cmd_sound_command),
    EVENT_CMD(0xC03CBC, event_cmd_wait_apu_port1),
    EVENT_CMD(0xC03CC5, event_cmd_wait_apu_port3),
    EVENT_CMD(0xC03C05, event_cmd_set_1f),
    EVENT_CMD(0xC03CD0, event_cmd_brightness),
    EVENT_CMD(0xC03CFD, event_cmd_wait_brightness),
    EVENT_CMD(0xC03D7A, event_cmd_wait_color),
    EVENT_CMD(0xC03D07, event_cmd_fixed_color),
    EVENT_CMD(0xC03D84, event_cmd_set_bc),
    EVENT_CMD(0xC03D97, event_cmd_copy_tiles),
    EVENT_CMD(0xC03DC5, event_cmd_queue_tile_copy),
    EVENT_CMD(0xC03DF4, event_cmd_set_layer_scroll),
    EVENT_CMD(0xC03E30, event_cmd_scroll_screen),
    EVENT_CMD(0xC03E5C, event_cmd_party_routine_6_7),
    EVENT_CMD(0xC03E6E, event_cmd_party_routine_6),
    EVENT_CMD(0xC03E75, event_cmd_party_routine_7),
    /* locations */
    EVENT_CMD(0xC03A68, event_cmd_party_routine_0a),
    EVENT_CMD(0xC03A07, event_cmd_remove_from_party),
    EVENT_CMD(0xC03A83, event_cmd_start_battle),
    EVENT_CMD(0xC03AB7, event_cmd_change_location),
    EVENT_CMD(0xC03AF1, event_cmd_set_location_0a),
    EVENT_CMD(0xC03B1B, event_cmd_set_location_00),
    EVENT_CMD(0xC03B45, event_cmd_set_location_00_with_1e),
    EVENT_CMD(0xC03B4B, event_cmd_change_location_now),
    EVENT_CMD(0xC03B98, event_cmd_change_location_now_with_1e),
    EVENT_CMD(0xC03B9E, event_cmd_change_location_from_variables),
#undef EVENT_CMD

    /* field */
    { .addr = 0xC00B64, .name = "field_install_nmi", .fn = field_install_nmi,
      NEEDS_M8_X16, .cost_milli = 3500, .dont_care = DC_N | DC_Z | DC_D },
    { .addr = 0xC00B75, .name = "field_install_irq", .fn = field_install_irq,
      NEEDS_M8_X16, .cost_milli = 3500, .dont_care = DC_N | DC_Z | DC_D },
    { .addr = 0xC0EA63, .name = "field_nmi", .fn = field_nmi, .cost_milli = 1120 },
    { .addr = 0xC0EC71, .name = "field_wait_for_frame_loop", .fn = field_wait_for_frame_loop, NEEDS_M8, .idle = true },
    /* field object motion; entered with 8-bit A and 16-bit X */
    { .addr = 0xC0ABA2, .name = "field_direction_to_target", .fn = field_direction_to_target,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_X | DC_N | DC_Z },
    { .addr = 0xC0AC69, .name = "field_set_velocity", .fn = field_set_velocity,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_N | DC_V | DC_Z | DC_C },
    { .addr = 0xC0ACFD, .name = "field_start_step", .fn = field_start_step,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_X | DC_N | DC_Z | DC_C },
    { .addr = 0xC05B95, .name = "field_object_in_the_way", .fn = field_object_in_the_way,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_X | DC_N | DC_Z },
    { .addr = 0xC05614, .name = "field_update_walk_animation", .fn = field_update_walk_animation,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_X | DC_N | DC_Z | DC_C },
    { .addr = 0xC0568B, .name = "field_stand_animation", .fn = field_stand_animation,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_X | DC_N | DC_Z | DC_C },
    { .addr = 0xC030B3, .name = "field_line_up_with_object", .fn = field_line_up_with_object,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_X | DC_N | DC_Z },
    { .addr = 0xC0305D, .name = "field_line_up_with_tile", .fn = field_line_up_with_tile,
      NEEDS_M8_X16_BINARY, .cost_milli = 4000, .dont_care = DC_N | DC_Z },
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

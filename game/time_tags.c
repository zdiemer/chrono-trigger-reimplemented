/* The retired routines' times: the routines whose subsystem has left the
 * byte-exact gate (docs/PLAN.md, "Retiring the gate, deliberately"), which the
 * machine no longer enters as ports and shadow mode no longer compares on
 * their own, but whose work the ports still run as C and name
 * (NATIVE_TIME_AS, TIMED_AS, the charges). A port's time is estimated at the
 * rate of the routine it names, so each keeps the rate it was last calibrated
 * to: a retired routine's time is charged as before. No shadow run measures
 * one any more (it has no calls of its own), so tools/timing_gate.py's
 * calibration leaves these alone; the error they make is the callers', which
 * the gate still measures.
 *
 * The runtime puts them in its table beside game/registry.c's entries
 * (frontend/natives.c), marked time_only: the machine finds the original's
 * code there, as it does at any address no port is registered at. The table
 * is also the list of what retired, a row for each entry the registry had:
 * tools/unreached.py ends its walks at them as it did at the entries. */
#include "registry.h"

const NativeEntry game_time_tags[] = {
    /* core (docs/REIMPLEMENTATION.md, "The core services' teardown"): the
     * controller's work and the block move */
    { .addr = 0xC284D2, .name = "input_update", .cost_milli = 3114, .time_only = true },
    { .addr = 0xC284EC, .name = "input_read_joypad", .cost_milli = 7003, .time_only = true },
    { .addr = 0xC28545, .name = "input_map_buttons", .cost_milli = 11401, .time_only = true },
    { .addr = 0xC285AE, .name = "play_time_tick", .cost_milli = 2392, .time_only = true },
    { .addr = 0x7E9690, .name = "memory_move_stub", .cost_milli = 2741, .time_only = true },
    /* the animated tiles: their load and their uploads, through the original's jump */
    { .addr = 0xFDDE98, .name = "tile_animation_load", .cost_milli = 10260, .time_only = true },
    { .addr = 0xFDFFFA, .name = "tile_animation_load_jump", .cost_milli = 10275, .time_only = true },
    { .addr = 0xFDE022, .name = "tile_animation_upload", .cost_milli = 4125, .time_only = true },
    { .addr = 0xFDFFFD, .name = "tile_animation_upload_jump", .cost_milli = 4181, .time_only = true },
    /* the palette's animations: the frame's work (and the original's jump to
     * it), the load and the clear, and the work of each type's handler, its
     * fades' channel modes and their restore paths, which the frame's work
     * names as it runs them */
    { .addr = 0xFDE39C, .name = "palette_animate", .cost_milli = 11344, .time_only = true },
    { .addr = 0xFDFFF7, .name = "palette_animate_jump", .cost_milli = 11393, .time_only = true },
    { .addr = 0xFDE275, .name = "palette_clear_animations", .cost_milli = 14150, .time_only = true },
    { .addr = 0xFDFFEE, .name = "palette_clear_animations_jump", .cost_milli = 14300, .time_only = true },
    { .addr = 0xFDE292, .name = "palette_load_animations", .cost_milli = 8079, .time_only = true },
    { .addr = 0xFDFFF4, .name = "palette_load_animations_jump", .cost_milli = 8111, .time_only = true },
    { .addr = 0xFDE437, .name = "palette_animate_frames", .cost_milli = 5821, .time_only = true },
    { .addr = 0xFDE485, .name = "palette_animate_frames_from_table", .cost_milli = 6790, .time_only = true },
    { .addr = 0xFDE532, .name = "palette_rotate_down", .cost_milli = 3707, .time_only = true },
    { .addr = 0xFDE4E8, .name = "palette_rotate_up", .cost_milli = 3725, .time_only = true },
    { .addr = 0xFDE82C, .name = "palette_fade_to_white", .cost_milli = 11081, .time_only = true },
    { .addr = 0xFDE5A4, .name = "palette_fade_to_black", .cost_milli = 10998, .time_only = true },
    { .addr = 0xFDE8AC, .name = "palette_restore_after_white", .cost_milli = 4262, .time_only = true },
    { .addr = 0xFDE624, .name = "palette_restore_after_black", .cost_milli = 4260, .time_only = true },
    { .addr = 0xFDE807, .name = "palette_pulse_white", .cost_milli = 4000, .time_only = true },
    { .addr = 0xFDE57C, .name = "palette_pulse_black", .cost_milli = 4000, .time_only = true },
    { .addr = 0xFDE64F, .name = "palette_darken_all", .cost_milli = 4934, .time_only = true },
    { .addr = 0xFDE7C0, .name = "palette_darken_green_blue", .cost_milli = 5488, .time_only = true },
    { .addr = 0xFDE73A, .name = "palette_darken_red_blue", .cost_milli = 4000, .time_only = true },
    { .addr = 0xFDE6A1, .name = "palette_darken_blue", .cost_milli = 4000, .time_only = true },
    { .addr = 0xFDE779, .name = "palette_darken_red_green", .cost_milli = 5491, .time_only = true },
    { .addr = 0xFDE6D1, .name = "palette_darken_green", .cost_milli = 4000, .time_only = true },
    { .addr = 0xFDE708, .name = "palette_darken_red", .cost_milli = 4000, .time_only = true },
    { .addr = 0xFDE8D7, .name = "palette_lighten_all", .cost_milli = 5228, .time_only = true },
    { .addr = 0xFDE962, .name = "palette_lighten_red", .cost_milli = 3789, .time_only = true },
    { .addr = 0xFDE98F, .name = "palette_lighten_green", .cost_milli = 6332, .time_only = true },
    { .addr = 0xFDEA5C, .name = "palette_lighten_red_green", .cost_milli = 5106, .time_only = true },
    { .addr = 0xFDE935, .name = "palette_lighten_blue", .cost_milli = 3818, .time_only = true },
    { .addr = 0xFDE9CA, .name = "palette_lighten_red_blue", .cost_milli = 4000, .time_only = true },
    { .addr = 0xFDEA11, .name = "palette_lighten_green_blue", .cost_milli = 5340, .time_only = true },
};

const int game_time_tag_count = (int)(sizeof game_time_tags / sizeof game_time_tags[0]);

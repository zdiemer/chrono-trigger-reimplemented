/* The unused turning-map scene: the party's figures are built from sixteen
 * tiles per pose and drawn as four large sprites. A changed pose is assembled
 * in work RAM before the next vblank uploads it. */
#include "scene/scene.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"
#include "wram.h"

enum { POSE_TILES = 16, TILE_BYTES = 32, POSE_RECORD_BYTES = 40 };
enum { FIGURE_SPRITES = 4, LARGE_SPRITE = 0x80, LEFT_OF_SCREEN = 0x40 };
enum { FIGURES = 4, PARTY_FIGURES = 3, FIGURE_RECORD_BYTES = 0x16, FIGURE_BUFFER = 0x1C00 };
enum { TURN_CENTER_X = 128, TURN_CENTER_Y = 112, NO_POSE_UPLOAD = 0xFFFF };

/* Each pose lists sixteen tile numbers followed by four pairs of sprite
 * offsets. The tile number also says whether to mirror its bitplanes. */
static uint8_t mirrored_bits(uint8_t bits)
{
    bits = (uint8_t)((bits & 0x55) << 1 | (bits >> 1 & 0x55));
    bits = (uint8_t)((bits & 0x33) << 2 | (bits >> 2 & 0x33));
    return (uint8_t)(bits << 4 | bits >> 4);
}

/* The old loader's patched source and work-port cursor are observable to the
 * oracle, although the C loader uses neither to find or copy a tile. */

// $7E:6E42, $7E:6E9E
/* Assemble one pose. Source tile indices wrap within their graphics bank;
 * mirrored tiles reverse each bitplane byte, preserving the plane layout. */
void scene_load_figure_pose(GameState *g, unsigned character, uint16_t pose_offset, uint16_t destination)
{
    uint32_t poses = scene_figure_pose_table(g, character);
    uint32_t graphics = scene_figure_graphics(g, character);
    uint16_t pose = (uint16_t)(poses + pose_offset);
    int last_mirrored = -1;
    for (unsigned tile = 0; tile < POSE_TILES; tile++) {
        uint16_t listed = scene_figure_tile(g, (poses & 0xFF0000) | pose);
        pose = (uint16_t)(pose + 2);
        bool mirrored = (listed & 0x4000) != 0;
        /* The format's discarded bit 11 supplies one extra byte of offset. */
        uint16_t source = (uint16_t)(graphics + (uint16_t)(listed * TILE_BYTES) + ((listed >> 11) & 1));
        uint16_t output = (uint16_t)(destination + tile * TILE_BYTES);
        for (unsigned byte = 0; byte < TILE_BYTES; byte++) {
            uint32_t at = (graphics & 0xFF0000) + (mirrored ? (unsigned)source + byte : (uint16_t)(source + byte));
            uint8_t value = scene_figure_graphics_byte(g, at);
            wram_write8(g, (uint16_t)(output + byte), mirrored ? mirrored_bits(value) : value);
        }
        if (mirrored)
            last_mirrored = (int)tile;
    }
    charge_legacy_accesses(g, 9 + (last_mirrored >= 0 ? 2 : 0));
    charge_legacy_io_accesses(g, 3);
}

// $7E:701E
/* Draw the loaded pose with its four sprite offsets. Negative positions
 * retain the high X bit; the scene itself supplies byte-sized centers. */
void scene_draw_figure_pose(GameState *g, unsigned character, uint16_t pose_offset, uint16_t appearance)
{
    uint32_t poses = scene_figure_pose_table(g, character);
    uint32_t offsets = (poses & 0xFF0000) | (uint16_t)(poses + pose_offset + POSE_TILES * 2);
    uint16_t first = scene_sprite_index(g);
    for (unsigned sprite = 0; sprite < FIGURE_SPRITES; sprite++) {
        uint16_t x = (uint16_t)(scene_figure_graphics_byte(g, offsets + sprite * 2) + scene_sprite_offset_x(g));
        uint8_t y = (uint8_t)(scene_figure_graphics_byte(g, offsets + sprite * 2 + 1) + scene_sprite_offset_y(g));
        uint16_t looks = (uint16_t)(appearance + sprite * 2);
        unsigned high = scene_sprite_high_pointer(g) - SCENE_SPRITE_HIGH_TABLE;
        scene_place_sprite(g, (uint16_t)(first + sprite * 4), (SceneSprite){ (uint8_t)x, y, (uint8_t)looks, (uint8_t)(looks >> 8) });
        set_scene_sprite_high_table_byte(g, high, (uint8_t)(LARGE_SPRITE | (x & 0x8000 ? LEFT_OF_SCREEN : 0) |
                                                          scene_sprite_high_table_byte(g, high) >> 2));
        unsigned next = first + (sprite + 1) * 4;
        if (next % 16 == 0)
            set_scene_sprite_high_pointer(g, (uint16_t)(scene_sprite_high_pointer(g) + 1));
    }
    set_scene_sprite_index(g, (uint16_t)(first + FIGURE_SPRITES * 4));
    charge_legacy_accesses(g, 9);
}

/* ---- The figures and their animations -------------------------------------------------------- */

static unsigned figure_character(const GameState *g, unsigned figure)
{
    return scene_turn_figures_character(g, figure) & 0xFF;
}

/* Positions enter as screen bytes. The old multiplier treats the radius's
 * two magnitudes as signed bytes, including its special -128 endpoint. */
static uint8_t magnitude_byte(int value)
{
    int byte = (int8_t)value;
    return (uint8_t)(byte < 0 ? -byte : byte);
}

// $7E:7649
static void find_figure_origin(GameState *g, Platform *pf, unsigned figure)
{
    (void)pf;
    int16_t x = (int16_t)(scene_turn_figures_x(g, figure) - TURN_CENTER_X);
    int16_t y = (int16_t)(scene_turn_figures_y(g, figure) - TURN_CENTER_Y);
    set_scene_turn_figures_angle(g, figure, scene_offset_angle(g, x, y));
    uint8_t across = magnitude_byte(x), down = magnitude_byte(y);
    uint32_t area = (uint16_t)(across * (int8_t)across) + (uint32_t)(uint16_t)(down * (int8_t)down);
    unsigned radius = scene_radius_from_area(g, (area & 0xFFFF) + (area >> 16));
    set_scene_turn_figures_radius(g, figure, (uint8_t)radius);
    charge_legacy_accesses(g, 4);
    charge_legacy_io_accesses(g, 4);
}

// $7E:74F5, $7E:762E
/* Keep the poses and positions the field handed over. Each figure owns a
 * different pair of tile rows in sprite memory; Magus is the fourth figure. */
static void start_figures(GameState *g, Platform *pf)
{
    static const uint16_t tiles[FIGURES] = { 0x0080, 0x0200, 0x0280, 0x1880 };
    static const uint16_t appearances[FIGURES] = { 0x3A08, 0x3C20, 0x3E28, 0x3388 };
    for (unsigned figure = 0; figure < FIGURES; figure++) {
        uint16_t character = figure < PARTY_FIGURES ? (uint16_t)(scene_turn_party_bytes(g, figure) |
                              (uint16_t)scene_turn_party_bytes(g, figure + 1) << 8) : 6;
        uint8_t pose = scene_turn_saved_positions_pose(g, figure);
        uint8_t animation = scene_turn_initial_animation(g, character & 7);
        set_scene_turn_figures_character(g, figure, character);
        set_scene_turn_figures_pose(g, figure, pose);
        set_scene_turn_figures_loaded_pose(g, figure, pose);
        set_scene_turn_figures_animation(g, figure, animation);
        set_scene_turn_figures_active_animation(g, figure, (uint8_t)(animation + 1));
        set_scene_turn_figures_frames(g, figure, 0x3000);
        set_scene_turn_figures_vram(g, figure, tiles[figure]);
        set_scene_turn_figures_appearance(g, figure, appearances[figure]);
        set_scene_turn_figures_x(g, figure, (uint8_t)((scene_turn_saved_positions_tile_x(g, figure) << 4) |
                                                   scene_turn_saved_positions_fine_x(g, figure)));
        set_scene_turn_figures_y(g, figure, (uint8_t)((scene_turn_saved_positions_tile_y(g, figure) << 4) |
                                                   scene_turn_saved_positions_fine_y(g, figure)));
        find_figure_origin(g, pf, figure);
    }
}

// $7E:72FF, $7E:7316
TaskEnd scene_spread_turn_figures(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    for (unsigned figure = 0; figure < FIGURES; figure++) {
        uint8_t radius = scene_turn_figures_radius(g, figure);
        if (radius != 128)
            set_scene_turn_figures_radius(g, figure, (uint8_t)(radius + (radius > 128 ? 1 : -1)));
    }
    return task_goes_on();
}

/* The table's sine is rounded toward the scene center on its second half.
 * Distances are signed bytes, so crossing zero takes the figure through the
 * center and out along the opposite side of the orbit. */
static uint8_t orbit_coordinate(GameState *g, uint16_t angle, int8_t radius, unsigned center)
{
    int32_t product = scene_sine(g, angle) * radius;
    return (uint8_t)((product >> 8) + center + scene_sine_second_half(angle));
}

// $7E:76A5, $7E:76BC
TaskEnd scene_orbit_turn_figures(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    uint16_t orbit = (uint16_t)(scene_turn_orbit(g) + scene_channel_register(g, 1, 2));
    set_scene_turn_orbit(g, orbit);
    for (unsigned figure = 0; figure < PARTY_FIGURES; figure++) {
        uint16_t angle = (uint16_t)(orbit + scene_turn_figures_angle(g, figure));
        unsigned quadrant = ((uint16_t)(angle + SCENE_FULL_TURN / 8) >> 8) & 3;
        int8_t radius = (int8_t)scene_turn_figures_radius(g, figure);
        set_scene_turn_figures_facing(g, figure, scene_turn_facings(g, quadrant) & 3);
        set_scene_turn_figures_x(g, figure, orbit_coordinate(g, (uint16_t)(angle + SCENE_QUARTER_TURN), radius, TURN_CENTER_X));
        set_scene_turn_figures_y(g, figure, orbit_coordinate(g, angle, radius, TURN_CENTER_Y));
    }
    charge_legacy_accesses(g, 5);
    charge_legacy_rom_accesses(g, 1);
    charge_legacy_io_accesses(g, 4);
    return task_goes_on();
}

// $7E:77E7
static void restart_figure_animation(GameState *g, unsigned figure)
{
    unsigned character = figure_character(g, figure);
    uint16_t frames = scene_figure_frames(g, character);
    uint16_t half = (uint16_t)(scene_figure_frames(g, character + 1) - frames) >> 1;
    uint16_t animation = scene_turn_figures_animation(g, figure) * 4;
    uint16_t delays = (uint16_t)(scene_figure_delays(g, character) + animation);
    unsigned facing = scene_turn_figures_facing(g, figure);
    unsigned direction = facing == 0 ? 0 : facing == 1 ? half >> 1 : facing == 2 ? half : half + (half >> 1);
    set_scene_turn_figures_frames(g, figure, (uint16_t)(frames + direction + animation));
    set_scene_turn_figures_delays(g, figure, delays);
    set_scene_turn_figures_active_animation(g, figure, scene_turn_figures_animation(g, figure));
    set_scene_turn_figures_active_facing(g, figure, (uint8_t)facing);
    charge_legacy_accesses(g, 10);
}

static bool animation_changed(const GameState *g, unsigned figure)
{
    return scene_turn_figures_animation(g, figure) != scene_turn_figures_active_animation(g, figure) ||
           scene_turn_figures_facing(g, figure) != scene_turn_figures_active_facing(g, figure);
}

// $7E:77AE
/* Hold a pose for its delay, then take the next pose and delay together.
 * Changing animation restarts its stream; changing direction advances the
 * current stream until its end marker, just as this scene's player does. */
static void animate_figure(GameState *g, unsigned figure)
{
    bool new_animation = scene_turn_figures_animation(g, figure) != scene_turn_figures_active_animation(g, figure);
    if (!animation_changed(g, figure)) {
        uint8_t delay = (uint8_t)(scene_turn_figures_delay(g, figure) - 1);
        set_scene_turn_figures_delay(g, figure, delay);
        if (delay)
            return;
    }
    if (new_animation || scene_figure_animation_byte(g, scene_turn_figures_frames(g, figure)) == 0xFF)
        restart_figure_animation(g, figure);
    uint16_t frames = scene_turn_figures_frames(g, figure), delays = scene_turn_figures_delays(g, figure);
    set_scene_turn_figures_pose(g, figure, scene_figure_animation_byte(g, frames));
    set_scene_turn_figures_delay(g, figure, scene_figure_animation_byte(g, delays));
    set_scene_turn_figures_frames(g, figure, (uint16_t)(frames + 1));
    set_scene_turn_figures_delays(g, figure, (uint16_t)(delays + 1));
}

// $7E:7721, $7E:7774
/* One changed pose per frame, in rotation, keeps the vblank upload bounded.
 * Every figure is then drawn with its loaded pose and its animation advanced. */
TaskEnd scene_update_turn_figures(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    if (!scene_sprites_fit(g, FIGURES * FIGURE_SPRITES) || scene_turn_next_upload(g) % FIGURE_RECORD_BYTES ||
        scene_turn_next_upload(g) >= FIGURES * FIGURE_RECORD_BYTES)
        return scene_task_needs_the_machine(g, 0x7E7721, "turning figures outside their sprite or upload slots");
    for (unsigned figure = 0; figure < FIGURES; figure++)
        if (figure_character(g, figure) >= 7)
            return scene_task_needs_the_machine(g, 0x7E7721, "a turning figure outside the party graphics");
    set_scene_turn_upload_vram(g, NO_POSE_UPLOAD);
    unsigned next = scene_turn_next_upload(g) / FIGURE_RECORD_BYTES;
    unsigned left = FIGURES;
    while (left) {
        unsigned figure = next;
        next = (next + 1) % FIGURES;
        uint8_t pose = scene_turn_figures_pose(g, figure);
        if (pose != scene_turn_figures_loaded_pose(g, figure)) {
            set_scene_turn_figures_loaded_pose(g, figure, pose);
            set_scene_turn_upload_vram(g, scene_turn_figures_vram(g, figure));
            scene_load_figure_pose(g, figure_character(g, figure), (uint16_t)(pose * POSE_RECORD_BYTES), FIGURE_BUFFER);
            break;
        }
        left--;
    }
    set_scene_turn_next_upload(g, (uint16_t)(next * FIGURE_RECORD_BYTES));
    charge_legacy_accesses(g, 2);
    for (unsigned figure = 0; figure < FIGURES; figure++) {
        uint16_t appearance = scene_turn_figures_appearance(g, figure);
        scene_draw_lists_at(g, scene_turn_figures_x(g, figure), scene_turn_figures_y(g, figure));
        charge_legacy_accesses(g, 2);
        charge_legacy_accesses(g, 4);
        scene_draw_figure_pose(g, figure_character(g, figure),
                               (uint16_t)(scene_turn_figures_loaded_pose(g, figure) * POSE_RECORD_BYTES), appearance);
        animate_figure(g, figure);
    }
    return task_goes_on();
}

// $7E:7868
TaskEnd scene_send_turn_figure(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    uint16_t destination = scene_turn_upload_vram(g);
    if (destination != NO_POSE_UPLOAD) {
        vid_send_vram(pf, PF_AT(0x7E7872), destination, PF_GAME(FIGURE_BUFFER), 256, SCENE_VRAM_WHOLE_WORDS);
        vid_send_vram(pf, PF_AT(0x7E789D), (uint16_t)(destination + 256), PF_GAME(FIGURE_BUFFER + 256), 256, NULL);
    }
    return task_goes_on();
}

// $7E:78AB
TaskEnd scene_cycle_turn_colors(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    if ((scene_frame_counter(g) & 7) == 0) {
        vid_send_palette(pf, PF_AT(0x7E78B4), 21, PF_GAME(0x7E222A), 3);
        for (unsigned byte = 0; byte < 2; byte++) {
            uint8_t first = scene_palette_byte(g, 42 + byte);
            set_scene_palette_byte(g, 42 + byte, scene_palette_byte(g, 44 + byte));
            set_scene_palette_byte(g, 44 + byte, scene_palette_byte(g, 46 + byte));
            set_scene_palette_byte(g, 46 + byte, first);
        }
    }
    return task_goes_on();
}

// $7E:7325
TaskEnd scene_size_turn_windows(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    static const unsigned registers[2] = { 6, 0 };
    for (unsigned window = 0; window < 2; window++) {
        uint16_t radius = scene_channel_register(g, 1, registers[window]);
        if (radius == scene_turn_window_radius(g, window))
            continue;
        set_scene_turn_window_radius(g, window, radius);
        set_scene_circle_radius(g, radius);
        set_scene_circle_table(g, (uint16_t)(0xF000 + window * 0x280));
        TIMED_AS(g, 0xC30377, scene_circle_draw(g, SCENE_CIRCLE_ROUND));
    }
    return task_goes_on();
}

// $7E:73AA
/* Advance a scrolling wave: shift its history up and add a fresh sample at
 * the bottom. The task list supplies the script's step without rounding. */
static void scroll_turn_wave(GameState *g, uint16_t step)
{
    for (unsigned line = 0; line < 223; line++)
        set_scene_turn_wave_lines(g, line, scene_turn_wave_lines(g, line + 1));
    uint16_t angle = (uint16_t)(scene_turn_wave_angle(g) + step);
    set_scene_turn_wave_angle(g, angle);
    set_scene_turn_wave_lines(g, 223, (uint16_t)scene_sine(g, angle) >> 2);
}

TaskEnd scene_scroll_turn_wave(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    scroll_turn_wave(g, scene_channel_register(g, 1, 8));
    return task_goes_on();
}

// $7E:7193
static bool step_turn_mosaic(GameState *g)
{
    uint8_t left = (uint8_t)(scene_turn_mosaic_countdown(g) - 1);
    set_scene_turn_mosaic_countdown(g, left);
    if (left)
        return false;
    set_scene_turn_mosaic_countdown(g, scene_turn_mosaic_period(g));
    set_scene_turn_mosaic_size(g, (uint8_t)((scene_turn_mosaic_size(g) + 1) & 7));
    return true;
}

TaskEnd scene_step_turn_mosaic(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    step_turn_mosaic(g);
    return task_goes_on();
}

// $7E:737E
static void show_turn_mosaic(GameState *g, Platform *pf, uint16_t salt)
{
    uint8_t size = scene_turn_mosaic_size(g);
    if (size != 0xFF) {
        vid_set_mosaic(pf, PF_AT(0x7E738C), VID_BG1_BIT, (size & 15) + 1);
    } else if ((scene_frame_counter(g) & 3) == 0) {
        SceneDraw last = scene_draw_from(0, true, TM_ENTROPY_BYTES);
        uint8_t choice;
        do {
            SceneDraw draw = scene_draw_random(g, pf, PF_AT(0x7E7397), last, salt, TM_ENTROPY_BYTES);
            choice = (uint8_t)((draw & 0x30) + 0x20 + (draw >> 8));
            last = scene_draw_from(choice, true, TM_ENTROPY_BYTES);
        } while (choice == scene_turn_mosaic_previous(g));
        set_scene_turn_mosaic_previous(g, choice);
        vid_set_mosaic(pf, PF_AT(0x7E73A6), VID_BG1_BIT, (choice >> 4) + 1);
    }
}

TaskEnd scene_show_turn_mosaic(GameState *g, Platform *pf, unsigned slot)
{
    show_turn_mosaic(g, pf, (uint16_t)(slot * 2));
    return task_goes_on();
}

// $7E:7286
static void pick_turn_sequence(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    if (scene_turn_pick_sequence(g)) {
        unsigned choice = (scene_next_random_byte(g) & 6) / 2;
        set_scene_channels_script(g, 0, scene_turn_sequences(g, choice));
    }
}

/* ---- Start the turning-map scene ------------------------------------------------------------- */

static void start_turn_map(GameState *g)
{
    set_scene_turn_circle_center(g, 0, TURN_CENTER_X);
    set_scene_turn_circle_center(g, 1, TURN_CENTER_Y);
    set_scene_circle_table_bank(g, SCENE_LOWER_WORK_RAM);
    for (unsigned byte = 0; byte < 0x800; byte++) {
        uint8_t value = scene_turn_saved_map_byte(g, byte);
        set_scene_tilemap_buffer_byte(g, byte, value);
        set_scene_turn_output_map_byte(g, byte, value);
    }
    set_scene_fade_period(g, 2);
    set_scene_fade_countdown(g, 2);
    set_scene_channels_script(g, 0, 0x7157);
    set_scene_channels_script(g, 1, 0x71A5);
}

/* The transformed map is blended with the foreground inside the two
 * circular windows. BG3 and the sprites fill out the main screen. */
static void show_turn_layers(Platform *pf)
{
    const VidBg background = { VID_TILEMAP(0x6000, 32, 32) };
    const VidBg foreground = { VID_TILEMAP(0x7000, 32, 32) };
    const VidColorMath math = { .sub_screen = true, .half = true, .layers = VID_BG2_BIT };
    const VidWindows windows = {
        .w1 = { [VID_BG1] = VID_WIN_OUT },
        .w2 = { [VID_BG1] = VID_WIN_IN, [VID_BG2] = VID_WIN_IN, [VID_BG3] = VID_WIN_OUT },
        .win_main = VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_OBJ_BIT,
        .win_sub = VID_BG1_BIT,
    };
    vid_set_bg_map(pf, PF_AT(0x7E7119), VID_BG3, &foreground);
    vid_set_bg_map(pf, PF_AT(0x7E711D), VID_BG1, &background);
    vid_set_color_math(pf, PF_AT(0x7E7122), &math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_windows(pf, PF_AT(0x7E7127), &windows, VID_WINDOWS_BG1_BG2 | VID_WINDOWS_BG3_BG4);
    vid_set_windows(pf, PF_AT(0x7E712B), &windows, VID_WINDOWS_OBJ_BACK);
    vid_set_scroll(pf, PF_AT(0x7E712D), VID_BG1, 0, 0, VID_SCROLL_H | VID_SCROLL_V);
    vid_set_scroll(pf, PF_AT(0x7E7135), VID_BG2, 0, 0, VID_SCROLL_H | VID_SCROLL_V);
    vid_set_mode(pf, PF_AT(0x7E713F), &(VidMode){ .mode = 1 });
    vid_set_screens(pf, PF_AT(0x7E714F), VID_BG2_BIT | VID_BG3_BIT | VID_OBJ_BIT, VID_BG1_BIT,
                    VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E7154), &windows, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
}

static bool prepare_turn(GameState *g, Platform *pf, bool (*scripts)(void *), void *context)
{
    start_turn_map(g);
    if (!scripts(context))
        return false;
    set_scene_main_loop_call(g, SCENE_PASS_TURN_MAP);
    if (!scene_set_up_line_effects(g, pf, 0x7354))
        return false;
    set_scene_brightness(g, 15);
    start_figures(g, pf);
    return true;
}

static void show_turn(GameState *g, Platform *pf)
{
    vid_send_vram_next_frame(pf, PF_AT(0x7E70F2),
        PF_CALL(0x7E710F, 0x7E31B0),
        0x6800, PF_GAME(0x0C00), 0x800, SCENE_VRAM_WHOLE_WORDS);
    show_turn_layers(pf);
    for (unsigned window = 0; window < 2; window++)
        set_scene_turn_window_radius_low(g, window, 1);
    set_scene_line_effects(g, 15);
}

typedef struct { GameState *game; Platform *pf; } TurnSetup;

static bool turn_scripts_in_c(void *context)
{
    TurnSetup *setup = context;
    for (SceneChannel channel = 0; channel < 2; channel++) {
        SceneTurn turn = SCENE_TURN_ENDED;
        TIMED_AS(setup->game, channel ? 0x7E3754 : 0x7E3749, turn = scene_channel_turn(setup->game, setup->pf, channel));
        if (turn != SCENE_TURN_ENDED)
            return false;
    }
    return true;
}

// $7E:7092
bool scene_turn_set_up(GameState *g, Platform *pf)
{
    TurnSetup setup = { g, pf };
    if (!prepare_turn(g, pf, turn_scripts_in_c, &setup))
        return false;
    show_turn(g, pf);
    return true;
}

const SceneScriptRoutine scene_turn_routines[] = {
    { 0x7286, pick_turn_sequence },
};
const unsigned scene_turn_routine_count = sizeof scene_turn_routines / sizeof scene_turn_routines[0];

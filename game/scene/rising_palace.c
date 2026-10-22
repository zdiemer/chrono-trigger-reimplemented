/* Scenes: the palace rising out of the sea under the red moon.
 *
 * The scene program's mode 3 ($7E:671F) takes the picture the field has just
 * drawn -- the palace and the sea, its map in VRAM -- and plays it with
 * effects of its own: the palace shimmers, every other line of it pushed
 * across by a strength that dies away; its columns ripple the same way; the
 * sea below the horizon scrolls on its own; and its colors darken, flash to
 * white and come back. The attract sequence plays it with a curtain that
 * opens and closes the picture and quicker fades; the game's own showing has
 * neither.
 *
 * The pieces:
 *
 * - the setup: the map moved from where the field left it to BG1's, the top
 *   ten rows of it blank; the screen in mode 2, whose offsets per column BG3
 *   holds, with windows that hide the picture's ragged edges; the per-line
 *   effects (the curtain's window, the shimmer, the sea's scroll and its
 *   map); the palace's colors held as they are; and the two scripts, which
 *   time the rest;
 * - the frame tasks the scripts start: the shimmer, the ripple, the fades,
 *   the curtain's sweep, and the turn's matrix, which the program's turning
 *   of a map would use and this scene works out without turning anything;
 * - the vblank tasks: the column offsets and the scrolls sent, and the map
 *   buffer sent (the setup sends it, as mode 5 does from a task).
 *
 * The scripts' registers are the scene's variables: sequence channel 0's
 * angle and scale for the turn, and channel 1's shimmer strength (register 0),
 * BG1's vertical scroll (register 1) and the sea's scroll (register 2). */
#include "scene/scene.h"

#include "charge.h"
#include "core/palette.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The sequence channels and their registers the scene reads. */
enum { TURN_CHANNEL = 0, TURN_SCALE = 0, TURN_X = 1, TURN_Y = 2, TURN_ANGLE = 5 };
enum { PALACE_CHANNEL = 1, SHIMMER = 0, PALACE_SCROLL_Y = 1, SEA_SCROLL = 2 };

/* ---- The map buffer ---------------------------------------------------------------------------- */

/* The program's buffer for a map of 32x32 tiles, in low RAM as the upload
 * reads it through bank 0. */
enum { MAP_BUFFER = 0x001400, MAP_BYTES = 0x0800 };

static void send_the_map(GameState *g, Platform *pf, uint16_t vram)
{
    vid_send_vram(pf, PF_AT(0x7E7843), vram, PF_GAME(MAP_BUFFER), MAP_BYTES, SCENE_VRAM_WHOLE_WORDS);
    set_scene_map_buffer_waiting(g, false);
}

// $7E:783C
/* In vblank: the map buffer into VRAM, where the setup said, once it is
 * waiting there. */
static void send_the_map_if_waiting(GameState *g, Platform *pf)
{
    if (scene_map_buffer_waiting(g))
        send_the_map(g, pf, scene_map_upload_vram(g));
}

/* ---- The setup --------------------------------------------------------------------------------- */

/* The map the field left at $6000 moved to BG1's at 0, its first ten rows
 * blank (tile $100). The original fills the buffer's top with the Mode 7
 * multiplier's product, $100 times 1, copied out of the product's registers,
 * and reads the rest back out of VRAM, after a read of the port that steps
 * the address on a word first: the buffer's rows from the eleventh are the
 * map's from its 322nd word, one word on from where they were. Both are
 * transfers out of the registers, which go through the SNES-only escape. */
enum { BLANK_ROWS_BYTES = 0x0280, BLANK_TILE_WORD = 0x0100, FIELD_MAP_VRAM = 0x6000 };
enum { BELOW_THE_BLANK_ROWS_VRAM = FIELD_MAP_VRAM + BLANK_ROWS_BYTES / 2, REST_OF_THE_MAP_BYTES = MAP_BYTES - BLANK_ROWS_BYTES };

static void build_the_map(Platform *pf)
{
    vid_set_mode7(pf, PF_AT(0x7E6721), &(VidMode7){ .a = BLANK_TILE_WORD }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E6729), &(VidMode7){ .b = 1 * 256 }, VID_M7_MATRIX_B_HIGH);
    const VidSnesTransfer blank = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = MAP_BUFFER, .size = BLANK_ROWS_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E672C), &blank);
    const VidSnesTransfer rest = { .dest = VID_SNES_VRAM_READ, .source = MAP_BUFFER + BLANK_ROWS_BYTES,
                                   .size = REST_OF_THE_MAP_BYTES, .target_reg = VID_SNES_VRAM_ADDRESS,
                                   .target = BELOW_THE_BLANK_ROWS_VRAM, .port = SCENE_VRAM_WHOLE_WORDS };
    vid_snes_copy(pf, PF_AT(0x7E674D), &rest);
}

/* The layers: BG1 the palace and BG2 the sky and the sea, both with their
 * tiles at $1000; BG1's map at 0 and BG3's (the column offsets) at $7000;
 * mode 2; BG1, BG2 and the sprites on the main screen, BG2 and the sprites on
 * the sub screen, BG1 adding the sub screen to itself; and window 1 masking
 * BG1 and BG2 outside columns 8-248 on the main screen, where the offsets
 * leave the picture's edges ragged. With the curtain, window 2 masks them
 * too, from the curtain's edge (a per-line effect) to the right. */
enum { LAYER_TILES_VRAM = 0x1000, PALACE_MAP_VRAM = 0x0000, OFFSETS_MAP_VRAM = 0x7000 };
enum { EDGE_LEFT = 0x08, EDGE_RIGHT = 0xF8, CURTAIN_RIGHT = 0xFF };

static const VidBg palace_bg = { VID_TILEMAP(PALACE_MAP_VRAM, 32, 32) };
static const VidBg offsets_bg = { VID_TILEMAP(OFFSETS_MAP_VRAM, 32, 32) };
static const VidColorMath palace_math = { .sub_screen = true, .layers = VID_BG1_BIT };

static void set_up_the_layers(Platform *pf, bool with_the_curtain)
{
    VidWindows edges = { 0 };
    edges.w1[VID_BG1] = edges.w1[VID_BG2] = VID_WIN_OUT;
    vid_set_windows(pf, PF_AT(0x7E67B2), &edges, VID_WINDOWS_BG1_BG2);
    if (with_the_curtain) {
        edges.w2[VID_BG1] = edges.w2[VID_BG2] = VID_WIN_OUT;
        vid_set_windows(pf, PF_AT(0x7E67C1), &edges, VID_WINDOWS_BG1_BG2);
    }
    vid_set_mode(pf, PF_AT(0x7E67DF), &(VidMode){ .mode = 2 });
    vid_set_bg_map(pf, PF_AT(0x7E67E4), VID_BG1, &palace_bg);
    vid_set_bg_map(pf, PF_AT(0x7E67E9), VID_BG3, &offsets_bg);
    vid_set_screens(pf, PF_AT(0x7E67EF), VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT, VID_BG2_BIT | VID_OBJ_BIT,
                    VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_color_math(pf, PF_AT(0x7E67F5), &palace_math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_windows(pf, PF_AT(0x7E67FA), &(VidWindows){ .win_main = VID_ALL_LAYERS }, VID_WINDOWS_MAIN);
    vid_set_windows(pf, PF_AT(0x7E6800), &(VidWindows){ .w1_left = EDGE_LEFT, .w1_right = EDGE_RIGHT },
                    VID_WINDOWS_W1_LEFT | VID_WINDOWS_W1_RIGHT);
    vid_set_windows(pf, PF_AT(0x7E6805), &(VidWindows){ .w2_right = CURTAIN_RIGHT }, VID_WINDOWS_W2_RIGHT);
}

/* The per-line effects, from the descriptors at $685E: the curtain's window
 * edge (channel 0, on only with the curtain), the shimmer's per-line scroll,
 * the sea's scroll and the sky's map above the horizon. */
enum { PALACE_LINE_EFFECTS = 0x685E, LINE_EFFECTS_BUT_THE_CURTAIN = 0x0E, EVERY_LINE_EFFECT = 0x0F };

/* The palace's colors: BG palettes 2-3 and 5-6, two slots of the palette's
 * animations. The fades start from the colors as loaded, level 0, and go to
 * the full fade, $FF, a step of their level a frame. */
enum { PALACE_SLOTS = 2, PALACE_COLORS = 0x20, AS_LOADED = 0x00, FULL_FADE = 0xFF, EVERY_FRAME = 1 };
static const uint8_t palace_first_colors[PALACE_SLOTS] = { 0x20, 0x50 };

static void fade_the_palace(GameState *g, bool to_white, uint8_t step, uint8_t from, uint8_t to)
{
    for (unsigned slot = 0; slot < PALACE_SLOTS; slot++) {
        PaletteFade f = { .first_color = palace_first_colors[slot], .color_count = PALACE_COLORS, .step = step,
                          .period = EVERY_FRAME, .from_level = from, .to_level = to, .tint = PALETTE_TINT_NONE };
        if (to_white)
            palette_start_fade_to_white(g, slot, f);
        else
            palette_start_fade_to_black(g, slot, f);
    }
}

/* The attract sequence's fades move faster: their steps, which the original
 * keeps as operands of the fades' own code. */
enum { DARKENING_STEP_FAST = 6, WHITENING_STEP_FAST = 8, CLEARING_STEP_FAST = 8 };

/* The curtain's per-line edges, in the program's bank; it starts with no
 * steps taken, and sweeps when the attract's script says. The race's own
 * closing curtain, which shares the program's code, is made to sweep from
 * the left. */
enum { CURTAIN_TABLE = 0xF1C4, CURTAIN_FROM_THE_LEFT = 1 };

/* The program's turning of a map draws 28 rows of it here, the whole picture,
 * if it is asked to. */
enum { THE_PICTURE_S_ROWS = 0x1C };

/* The scripts, on channels 0 and 1, the attract sequence's or the game's. */
enum { TURN_SCRIPT = 0x6902, PALACE_SCRIPT = 0x693A, CURTAIN_TURN_SCRIPT = 0x6898, CURTAIN_PALACE_SCRIPT = 0x68A1 };

static bool attract_showing(const GameState *g) { return scene_flags(g) & SCENE_FLAG_ATTRACT_SEQUENCE; }

// $7E:671F
/* The palace's setup, from the program's mode table (mode 3), up to the
 * scripts' first step: the map, the layers' tiles and the two scripts. */
static void start_the_palace(GameState *g, Platform *pf)
{
    build_the_map(pf);
    vid_set_bg_chars(pf, PF_AT(0x7E6772), VID_BG1, LAYER_TILES_VRAM, LAYER_TILES_VRAM);
    set_scene_channels_script(g, TURN_CHANNEL, attract_showing(g) ? CURTAIN_TURN_SCRIPT : TURN_SCRIPT);
    set_scene_channels_script(g, PALACE_CHANNEL, attract_showing(g) ? CURTAIN_PALACE_SCRIPT : PALACE_SCRIPT);
}

/* After the first step: the map sent, the per-line effects, the layers, the
 * attract's quicker fades, the palace's colors held and the curtain. The map
 * upload's VRAM address is the vblank task's from now on. */
static void finish_the_palace(GameState *g, Platform *pf)
{
    bool attract = attract_showing(g);
    set_scene_map_upload_vram(g, PALACE_MAP_VRAM);
    /* The original marks the buffer waiting with an increment and sends it
     * through the vblank task's code, so a flag already at $FF wraps to 0 and
     * the map isn't sent. The game only ever has 0 or 1 there; the wrap is
     * kept so fuzzing compares the whole range. */
    if (scene_map_buffer_waiting(g) == 0xFF)
        TIMED_AS(g, 0x7E783C, set_scene_map_buffer_waiting(g, 0));
    else
        TIMED_AS(g, 0x7E783C, send_the_map(g, pf, PALACE_MAP_VRAM));
    set_scene_turned_map_rows(g, THE_PICTURE_S_ROWS);
    TIMED_AS(g, 0x7E36CC, (void)scene_set_up_line_effects(g, pf, PALACE_LINE_EFFECTS));
    set_up_the_layers(pf, attract);
    if (attract) {
        set_scene_palace_darkening_step(g, DARKENING_STEP_FAST);
        set_scene_palace_whitening_step(g, WHITENING_STEP_FAST);
        set_scene_palace_clearing_step(g, CLEARING_STEP_FAST);
    }
    set_scene_line_effects(g, attract ? EVERY_LINE_EFFECT : LINE_EFFECTS_BUT_THE_CURTAIN);

    TIMED_AS(g, 0xFDE275, palette_stop_animations(g));
    fade_the_palace(g, false, EVERY_FRAME, AS_LOADED, AS_LOADED);
    TIMED_AS(g, 0xFDE39C, palette_animate_a_frame(g));
    set_scene_curtain_table_bank(g, SCENE_LOWER_WORK_RAM);
    set_scene_curtain_table(g, CURTAIN_TABLE);
    set_scene_curtain_steps(g, 0);
    set_scene_race_ending_sweep(g, CURTAIN_FROM_THE_LEFT);
}

/* ---- The frame tasks --------------------------------------------------------------------------- */

/* A task's A as its original leaves it, read whole or as its last byte. */
static TaskLeftovers leaves_a(uint16_t a) { return (TaskLeftovers){ .a_set = true, .a = a }; }
static TaskLeftovers leaves_a_low(uint8_t low)
{
    return (TaskLeftovers){ .a_set = true, .a = low, .a_8bit = true, .a_high_as_called = true };
}

/* The turn's matrix: a step of the angle's cosine or sine (in 256ths) times
 * a quarter of the scale (its low byte), over 8, the sign kept apart. */
static int16_t scaled(int16_t trig, uint8_t quarter_scale)
{
    uint16_t magnitude = (uint16_t)(trig < 0 ? -trig : trig);
    uint16_t product = (uint16_t)((uint16_t)(magnitude * quarter_scale) >> 3);
    return trig < 0 ? (int16_t)-product : (int16_t)product;
}

/* The original multiplies on the Mode 7 matrix's registers, which it leaves
 * with the sine's magnitude and the scale (scaffolding: mode 2 draws none of
 * it, and the next scene that turns a map sets them again). */
static void leave_the_multiplier(Platform *pf, int16_t sine, uint8_t quarter_scale)
{
    vid_set_mode7(pf, PF_AT(0x7E7409), &(VidMode7){ .a = (int16_t)(sine < 0 ? -sine : sine) }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E7415), &(VidMode7){ .b = (int16_t)(quarter_scale << 8 | quarter_scale) }, VID_M7_MATRIX_B);
}

// $7E:73C6
/* The turn's matrix from channel 0's angle and scale, and the point it turns
 * about and where it is placed, both channel 0's registers 1 and 2. */
static TaskEnd work_out_the_turn_task(GameState *g, Platform *pf)
{
    uint16_t angle = scene_channel_register(g, TURN_CHANNEL, TURN_ANGLE);
    uint8_t quarter_scale = (uint8_t)((uint8_t)scene_channel_register(g, TURN_CHANNEL, TURN_SCALE) >> 2);
    int16_t cosine = scaled(scene_cosine(g, angle), quarter_scale);
    int16_t sine = scaled(scene_sine(g, angle), quarter_scale);
    set_scene_turn_matrix(g, 0, (uint16_t)cosine);
    set_scene_turn_matrix(g, 3, (uint16_t)cosine);
    set_scene_turn_matrix(g, 1, (uint16_t)sine);
    set_scene_turn_matrix(g, 2, (uint16_t)-sine);
    uint16_t x = scene_channel_register(g, TURN_CHANNEL, TURN_X), y = scene_channel_register(g, TURN_CHANNEL, TURN_Y);
    set_scene_turn_position(g, 0, x);
    set_scene_turn_center(g, 0, x);
    set_scene_turn_position(g, 1, y);
    set_scene_turn_center(g, 1, y);
    ORIGINAL_EFFECT(leave_the_multiplier(pf, scene_sine(g, angle), quarter_scale));
    return task_goes_on();
}

static TaskLeftovers work_out_the_turn_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a(scene_channel_register(g, TURN_CHANNEL, TURN_Y));
}

/* The shimmer: every other line of the palace pushed right by its strength,
 * from the first line on odd frames and from the second on even ones. */
enum { UNSHIFTED = 0 };

// $7E:6A23
static TaskEnd shimmer_the_palace_task(GameState *g, Platform *pf)
{
    (void)pf;
    uint16_t strength = scene_channel_register(g, PALACE_CHANNEL, SHIMMER);
    bool odd_frame = scene_frame_counter(g) & 1;
    for (unsigned line = 0; line < SCENE_PALACE_LINE_SHIFTS_COUNT; line++)
        set_scene_palace_line_shifts(g, line, ((line & 1) == 0) == odd_frame ? strength : UNSHIFTED);
    return task_goes_on();
}

static TaskLeftovers shimmer_the_palace_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a(scene_channel_register(g, PALACE_CHANNEL, SHIMMER));
}

/* The ripple: the columns' offsets a ridge across the picture, rising by the
 * strength from column to column for half the columns and falling back for
 * the rest, its strength the shimmer's (kept to 0-7), drifting across by the
 * strength a frame. Every offset applies to BG1, and to its scroll across. */
enum { COLUMNS = 32, RIDGE_COLUMNS = 16, STRENGTH_MAX = 7, OFFSET_BITS = 0x03FF, OFFSET_FOR_BG1 = 0x2000 };

/* The mosaic counts down a step every 20 frames to none, once a script has
 * set it; BG1's alone. */
enum { MOSAIC_STEP_FRAMES = 0x14 };

static uint16_t column_offset(uint16_t ridge) { return (uint16_t)((ridge & OFFSET_BITS) | OFFSET_FOR_BG1); }

static void step_the_mosaic(GameState *g, Platform *pf)
{
    uint8_t wait = (uint8_t)(scene_palace_mosaic_wait(g) - 1);
    set_scene_palace_mosaic_wait(g, wait);
    if (wait != 0)
        return;
    uint8_t mosaic = scene_palace_mosaic(g);
    if (mosaic == 0)
        return;
    mosaic = (uint8_t)(mosaic - 1);
    set_scene_palace_mosaic(g, mosaic);
    vid_set_mosaic(pf, PF_AT(0x7E6A1B), VID_BG1_BIT, (uint8_t)(mosaic + 1));
    set_scene_palace_mosaic_wait(g, MOSAIC_STEP_FRAMES);
}

// $7E:69AF
static TaskEnd ripple_the_palace_task(GameState *g, Platform *pf)
{
    uint16_t strength = (uint16_t)(scene_channel_register(g, PALACE_CHANNEL, SHIMMER) & STRENGTH_MAX);
    set_scene_channel_register(g, PALACE_CHANNEL, SHIMMER, strength);
    uint16_t ridge = (uint16_t)(0u - strength * 8u);
    for (unsigned column = 0; column < RIDGE_COLUMNS; column++) {
        ridge = (uint16_t)(ridge + strength);
        set_scene_palace_column_offsets(g, column, column_offset(ridge));
    }
    for (unsigned column = RIDGE_COLUMNS; column < COLUMNS; column++) {
        ridge = (uint16_t)(ridge - strength);
        set_scene_palace_column_offsets(g, column, column_offset(ridge));
    }
    ORIGINAL_EFFECT(set_scene_call_ripple_offset(g, scene_program_own(), ridge));
    set_scene_palace_ripple_scroll(g, (uint16_t)(scene_palace_ripple_scroll(g) + strength));
    uint16_t sea = scene_channel_register(g, PALACE_CHANNEL, SEA_SCROLL);
    set_scene_palace_sea_scroll(g, (uint16_t)(sea >> 2));
    set_scene_channel_register(g, PALACE_CHANNEL, SEA_SCROLL, (uint16_t)(sea - 1));
    step_the_mosaic(g, pf);
    return task_goes_on();
}

/* It leaves the sea's scroll in 8-bit A, whose high byte is read. */
static TaskLeftovers ripple_the_palace_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return (TaskLeftovers){ .a_set = true, .a = scene_palace_sea_scroll(g), .a_8bit = true };
}

/* The fades, one after another, each when the palette's animations have
 * finished the last: the palace darkens, then from its colors goes to white,
 * then from white comes back to its colors. The last puts the sky and the
 * sea on the main screen with the palace, and nothing on the sub screen. */
enum { PALACE_FADE_SLOT = 0 };

static bool the_last_fade_runs(const GameState *g) { return palette_animation_playing(g, PALACE_FADE_SLOT); }

// $7E:6A51
static TaskEnd darken_the_palace_task(GameState *g, Platform *pf)
{
    (void)pf;
    if (the_last_fade_runs(g))
        return task_goes_on();
    fade_the_palace(g, false, scene_palace_darkening_step(g), AS_LOADED, FULL_FADE);
    return (TaskEnd){ TASK_HANDS_OVER, SCENE_TASK_WHITEN_THE_PALACE };
}

// $7E:6A74
static TaskEnd whiten_the_palace_task(GameState *g, Platform *pf)
{
    (void)pf;
    if (the_last_fade_runs(g))
        return task_goes_on();
    fade_the_palace(g, true, scene_palace_whitening_step(g), AS_LOADED, FULL_FADE);
    return (TaskEnd){ TASK_HANDS_OVER, SCENE_TASK_CLEAR_THE_PALACE };
}

// $7E:6A97
static TaskEnd clear_the_palace_task(GameState *g, Platform *pf)
{
    if (the_last_fade_runs(g))
        return task_goes_on();
    vid_set_screens(pf, PF_AT(0x7E6AA2), VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT, 0, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    fade_the_palace(g, true, scene_palace_clearing_step(g), FULL_FADE, AS_LOADED);
    return task_is_done();
}

/* Each works in 8-bit A: waiting, its last load is the slot's kind; started,
 * the fade's period, or its last level for the last. */
static TaskLeftovers fade_leaves(const GameState *g, TaskEnd end, uint8_t started)
{
    return leaves_a_low(end.outcome == TASK_GOES_ON ? palette_animations_kind(g, PALACE_FADE_SLOT) : started);
}
static TaskLeftovers darken_the_palace_leaves(const GameState *g, TaskEnd end) { return fade_leaves(g, end, EVERY_FRAME); }
static TaskLeftovers whiten_the_palace_leaves(const GameState *g, TaskEnd end) { return fade_leaves(g, end, EVERY_FRAME); }
static TaskLeftovers clear_the_palace_leaves(const GameState *g, TaskEnd end) { return fade_leaves(g, end, AS_LOADED); }

/* ---- The vblank tasks ---------------------------------------------------------------------------- */

/* The column offsets into BG3's map, and the scrolls: BG3's across, which
 * moves the ripple, and BG1's down, channel 1's register 1. */
enum { OFFSETS = 0x000043, OFFSETS_BYTES = 0x80 };

// $7E:66DD
static TaskEnd send_the_offsets_task(GameState *g, Platform *pf)
{
    vid_send_vram(pf, PF_AT(0x7E66E0), OFFSETS_MAP_VRAM, PF_GAME(OFFSETS), OFFSETS_BYTES, SCENE_VRAM_WHOLE_WORDS);
    vid_set_scroll(pf, PF_AT(0x7E6709), VID_BG3, scene_palace_ripple_scroll(g), 0, VID_SCROLL_H);
    vid_set_scroll(pf, PF_AT(0x7E6715), VID_BG1, 0, scene_channel_register(g, PALACE_CHANNEL, PALACE_SCROLL_Y),
                   VID_SCROLL_V);
    return task_goes_on();
}

static TaskEnd send_the_map_task(GameState *g, Platform *pf)
{
    send_the_map_if_waiting(g, pf);
    return task_goes_on();
}

/* The vblank list reads nothing a task leaves but the overflow, which these
 * keep. */
static TaskLeftovers leaves_nothing_read(const GameState *g, TaskEnd end)
{
    (void)g;
    (void)end;
    return (TaskLeftovers){ 0 };
}

/* ---- The rows ---------------------------------------------------------------------------------- */

enum {
    ROW_WORK_OUT_THE_TURN, ROW_SHIMMER, ROW_RIPPLE, ROW_DARKEN, ROW_WHITEN, ROW_CLEAR, ROW_SEND_THE_OFFSETS,
    ROW_SEND_THE_MAP,
};

const SceneTaskPort scene_palace_tasks[] = {
    [ROW_WORK_OUT_THE_TURN] = { SCENE_TASK_WORK_OUT_THE_TURN, SCENE_FRAME_TASKS, work_out_the_turn_task,
      scene_work_out_the_turn_present, work_out_the_turn_leaves, false },
    [ROW_SHIMMER] = { SCENE_TASK_SHIMMER_THE_PALACE, SCENE_FRAME_TASKS, shimmer_the_palace_task,
      scene_shimmer_the_palace_present, shimmer_the_palace_leaves, false },
    [ROW_RIPPLE] = { SCENE_TASK_RIPPLE_THE_PALACE, SCENE_FRAME_TASKS, ripple_the_palace_task,
      scene_ripple_the_palace_present, ripple_the_palace_leaves, false },
    [ROW_DARKEN] = { SCENE_TASK_DARKEN_THE_PALACE, SCENE_FRAME_TASKS, darken_the_palace_task,
      scene_darken_the_palace_present, darken_the_palace_leaves, false },
    [ROW_WHITEN] = { SCENE_TASK_WHITEN_THE_PALACE, SCENE_FRAME_TASKS, whiten_the_palace_task,
      scene_whiten_the_palace_present, whiten_the_palace_leaves, false },
    [ROW_CLEAR] = { SCENE_TASK_CLEAR_THE_PALACE, SCENE_FRAME_TASKS, clear_the_palace_task,
      scene_clear_the_palace_present, clear_the_palace_leaves, false },
    [ROW_SEND_THE_OFFSETS] = { SCENE_TASK_SEND_THE_PALACE_OFFSETS, SCENE_VBLANK_TASKS, send_the_offsets_task,
      scene_send_the_palace_offsets_present, leaves_nothing_read, false },
    [ROW_SEND_THE_MAP] = { SCENE_TASK_SEND_THE_MAP, SCENE_VBLANK_TASKS, send_the_map_task, scene_send_the_map_present,
      leaves_nothing_read, false },
};
const unsigned scene_palace_task_count = sizeof scene_palace_tasks / sizeof scene_palace_tasks[0];

/* ---- Entries ----------------------------------------------------------------------------------- */

/* The scripts' first step: channel 0's, which runs the first command of
 * channel 1's too. It reads Y -- the setup's load of channel 1's script -- and
 * the overflow as the dispatch left it. */
static void take_the_scripts_first_step(Snes *s)
{
    scene_bridge_first_script_step(s, 0x7E6792, scene_channels_script(state_of(s), PALACE_CHANNEL));
}

/* Entered with 8-bit A. Back, the direct page is the scene's, the data bank
 * the setup's, and Y and the overflow as the palette's frame left them, which
 * the frame's work reads. */
bool scene_palace_set_up_scene(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    start_the_palace(g, pf);
    take_the_scripts_first_step(s);
    finish_the_palace(g, pf);
    s->cpu.y = palette_animations_leave();
    m_set_flag(s, CPU_FLAG_V, false);
    return true;
}

void scene_palace_set_up(Snes *s)
{
    scene_palace_set_up_scene(s);
    m_rts(s);
}

void scene_work_out_the_turn(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_WORK_OUT_THE_TURN]); }
void scene_shimmer_the_palace(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_SHIMMER]); }
void scene_ripple_the_palace(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_RIPPLE]); }
void scene_darken_the_palace(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_DARKEN]); }
void scene_whiten_the_palace(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_WHITEN]); }
void scene_clear_the_palace(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_CLEAR]); }
void scene_send_the_palace_offsets(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_SEND_THE_OFFSETS]); }
void scene_send_the_map(Snes *s) { scene_task_alone(s, &scene_palace_tasks[ROW_SEND_THE_MAP]); }

/* ---- The curtain ------------------------------------------------------------------------------- */

/* With the curtain, a frame task sweeps it ($7E:6844): the curtain's step,
 * and its window's edge read from the half of its table the step didn't
 * write -- the table on odd steps, the half after it on even ones -- through
 * the per-line effect's two lists of the table's runs. The step is a port of
 * its own, called through the machine, so that a comparison of it is its own
 * (the stir waits there for the beam the original latches); it reads none of
 * the registers but X, the list's place of the task, which it is handed as
 * the list hands it. The task is the list's original's for the same reason,
 * and runs as C where the list calls it. */
enum { CURTAIN_STEP = 0xC301E4, CURTAIN_IGNORES = DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C };
enum { CURTAIN_RUNS_ODD = 0x688A, CURTAIN_RUNS_EVEN = 0x6891, CURTAIN_WINDOW_SLOT = 0 };

static uint16_t curtain_runs(const GameState *g)
{
    return (scene_curtain_steps(g) & 1) ? CURTAIN_RUNS_ODD : CURTAIN_RUNS_EVEN;
}

static void show_the_curtain_step(const GameState *g, Platform *pf)
{
    bool odd = scene_curtain_steps(g) & 1;
    vid_set_line_table(pf, PF_AT(odd ? 0x7E6853 : 0x7E685A), CURTAIN_WINDOW_SLOT, curtain_runs(g));
}

// $7E:6844
/* Back in 8-bit A, as the original returns: its caller may read the width
 * before it sets it again. Nothing else it leaves is read. */
void scene_sweep_the_palace_curtain(Snes *s)
{
    m_jsl_dc(s, 0x7E6844, CURTAIN_STEP, CURTAIN_IGNORES);
    show_the_curtain_step(state_of(s), m_platform(s));
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* ---- The guards -------------------------------------------------------------------------------- */

/* The map's upload, but for its VRAM address, an operand the setup changes. */
static bool map_upload_present(Snes *s)
{
    return m_code_matches(s, 0x7E783C, 0x05, 0x66C70DDCCCA82B8Aull) && m_code_matches(s, 0x7E7843, 0x25, 0x8BD16C8B6369962Cull);
}

/* The fades' code, but for their steps, operands the setup changes, and the
 * start of a fade they share. */
static bool fades_present(Snes *s)
{
    return m_code_matches(s, 0x7E6A51, 0x0E, 0x76A269030313F095ull) && m_code_matches(s, 0x7E6A60, 0x22, 0x9CF7C8427464CFB9ull) &&
           m_code_matches(s, 0x7E6A83, 0x28, 0xFD8500DEDA75E486ull) && m_code_matches(s, 0x7E6AAC, 0x58, 0x140EB672974620E3ull);
}

/* The setup's code with the per-line effects' descriptors and the curtain's
 * runs after it, the routines it runs inline -- the map's upload, the start of
 * the fades, the per-line effects' setup. Its stores reach the registers and
 * low RAM through the data bank. */
bool scene_palace_set_up_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E671F, 0x179, 0x7F7DD6C50970626Dull) && map_upload_present(s) &&
           m_code_matches(s, 0x7E6ACD, 0x37, 0x7BD9C42F87390D90ull) && m_code_matches(s, 0x7E36CC, 60, 0x45191A42A1EEF6A8ull);
}

/* The frame tasks' code, and the sine's for the turn; their cells are the
 * scene's page's and low RAM's, through the data bank. */
bool scene_work_out_the_turn_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E73C6, 0x7B, 0x36BC8E88DCDF28B4ull) &&
           m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull);
}
bool scene_shimmer_the_palace_present(Snes *s) { return scene_on_scene_page(s) && m_code_matches(s, 0x7E6A23, 0x2E, 0x5E2E94B36E1B51FEull); }
bool scene_ripple_the_palace_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E69AF, 0x74, 0x8FBF611061CB7DB8ull);
}
bool scene_darken_the_palace_present(Snes *s) { return scene_on_scene_page_low_bank(s) && fades_present(s); }
bool scene_whiten_the_palace_present(Snes *s) { return scene_on_scene_page_low_bank(s) && fades_present(s); }
bool scene_clear_the_palace_present(Snes *s) { return scene_on_scene_page_io_bank(s) && fades_present(s); }

/* The vblank tasks' code, the registers reached through the data bank; the
 * map's upload's flag is on the scene's page, and the curtain's step is
 * guarded as its own entry is. */
bool scene_send_the_palace_offsets_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E66DD, 0x42, 0x36B301835107C4E7ull);
}
bool scene_sweep_the_palace_curtain_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E6844, 0x54, 0xAD0EB1A3FDD62E94ull) && scene_sweep_the_curtain_present(s);
}
bool scene_send_the_map_present(Snes *s) { return scene_on_scene_page_io_bank(s) && map_upload_present(s); }

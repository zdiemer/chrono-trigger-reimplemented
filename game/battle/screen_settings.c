/* Battle: the screen script's settings -- the screen's layers, the windows'
 * settings, colors, sounds and the frame work's switches.
 *
 * Below the window's split the per-line effects set the screen from the
 * split's values: which layers make the main and the sub screen and which
 * of them the windows mask (battle_screen_settings), the color math
 * (battle_color_math_settings), the background mode and the mosaic. The
 * window settings themselves are shadows the frame work sends each frame. A
 * script sets all of these, keeping the layers it turns on to the ones its
 * mask allows; and it sets the fixed color the lines below the split are
 * tinted with, brighter or darker.
 *
 * It also colors the screen (the backdrop, a color copied over another, two
 * sets of background colors from the ROM, the combatants' sprites pointed at
 * the other half of the sprite palettes), starts and stops palette
 * animations, asks the frame wait for a sound and an effect's graphics, and
 * sets the frame work's switches for the sprites, the line wait, palette
 * animation and the HP gradient. */
#include "battle/screen_script.h"

#include "battle/frame.h"
#include "battle/screen_data.h"
#include "original_effects.h"

/* ---- The screen's layers ------------------------------------------------------------------ */

/* The layer settings a script sets, as bytes of the split's values: the
 * screen's four and the color math's layers. */
typedef enum { MAIN_SCREEN, SUB_SCREEN, MAIN_WINDOWED, SUB_WINDOWED, COLOR_MATH_LAYERS } LayerSetting;
enum { COLOR_MATH_HOW, COLOR_MATH_WHICH };

static void set_layer_setting(GameState *g, LayerSetting setting, uint8_t layers)
{
    if (setting == COLOR_MATH_LAYERS)
        set_battle_color_math_settings(g, COLOR_MATH_WHICH, layers);
    else
        set_battle_screen_settings(g, setting, layers);
}

// $CD:3032 (main screen), $CD:3029 (sub), $CD:3020 (main's windows), $CD:3017 (sub's), $CD:3083 (color math)
/* Turn layers on in one of the settings, as far as the script's mask lets
 * it. */
static ScreenFlow show_layers(GameState *g, uint8_t setting, uint8_t layers)
{
    set_layer_setting(g, (LayerSetting)setting, layers & battle_screen_layer_mask(g));
    return screen_on();
}
SCREEN_OPERATION(show_layers, setting, layers)

// $CD:17FF
/* Set which layers the script may turn on. */
static ScreenFlow set_layer_mask(GameState *g, uint8_t layers)
{
    set_battle_screen_layer_mask(g, layers);
    return screen_on();
}
SCREEN_OPERATION(set_layer_mask, layers)

// $CD:308C
static ScreenFlow set_color_math(GameState *g, uint8_t how)
{
    set_battle_color_math_settings(g, COLOR_MATH_HOW, how);
    return screen_on();
}
SCREEN_OPERATION(set_color_math, how)

// $CD:2F6E
/* Set the sub screen's layers (all of them, mask or not) and the color math
 * at once. */
static ScreenFlow set_sub_screen_and_color_math(GameState *g, uint8_t value, uint8_t how, uint8_t layers)
{
    set_battle_screen_settings(g, SUB_SCREEN, value);
    set_battle_color_math_settings(g, COLOR_MATH_HOW, how);
    set_battle_color_math_settings(g, COLOR_MATH_WHICH, layers & battle_screen_layer_mask(g));
    return screen_on();
}
SCREEN_OPERATION(set_sub_screen_and_color_math, value, how, layers)

// $CD:20ED
static ScreenFlow set_screen_mode(GameState *g, uint8_t mode)
{
    set_battle_screen_mode(g, mode);
    return screen_on();
}
SCREEN_OPERATION(set_screen_mode, mode)

/* The mosaic's block size, 0-15, in its setting's high nibble, and the
 * layers it covers in the low. */
enum { LARGEST_BLOCKS = 0x0F };

// $CD:2EF7
/* Grow or shrink the mosaic's blocks, within their range, and say which
 * layers it covers. */
static ScreenFlow change_mosaic(GameState *g, uint8_t layers, uint8_t by, bool smaller)
{
    uint8_t blocks = (uint8_t)((battle_mosaic(g) >> 4) & LARGEST_BLOCKS);
    uint8_t changed = smaller ? (uint8_t)(blocks - by) : (uint8_t)(blocks + by);
    if (changed > LARGEST_BLOCKS)
        changed = smaller ? 0 : LARGEST_BLOCKS;
    set_battle_mosaic(g, (uint8_t)(changed << 4 | layers));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x45, layers));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x47, by));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x49, smaller ? (uint8_t)(blocks - by) : (uint8_t)(blocks + by)));
    return screen_on();
}
SCREEN_OPERATION(change_mosaic, layers, by, smaller)

/* ---- The windows' settings ----------------------------------------------------------------- */

/* The window settings' shadows the frame work sends: BG1 and BG2's, BG3 and
 * BG4's, the sprites' and the color window's, and the two logics. */
enum { BG_WINDOWS, BG3_BG4_WINDOWS, OBJECT_WINDOWS, BG_LOGIC, OBJECT_LOGIC };

/* The window's top border shows the windows shut or open on its line
 * before the battlefield's, as the settings take them. */
enum { BORDER_LINE = 1, WINDOW_SHUT = 0x00FF, WINDOW_OPEN = 0xFF00, BOTH_WINDOWS = 0xFFFF };
enum { BG1_WINDOW_2_ON = 0x08, LOGIC_INVERTED = 0x80, LOGIC_BITS = 0x3F, BOTH_LOGIC_BITS = 0x30 };

// $CD:2FAB, $CD:2FB1, $CD:30B8
static ScreenFlow set_window_settings(GameState *g, uint8_t which, uint8_t value)
{
    set_battle_window_registers(g, which, value);
    return screen_on();
}
SCREEN_OPERATION(set_window_settings, which, value)

// $CD:2F8B
/* Set BG1 and BG2's windows, the border shut where BG1's second window is
 * on and open otherwise. */
static ScreenFlow set_bg_windows(GameState *g, uint8_t value)
{
    uint16_t border = (value & BG1_WINDOW_2_ON) ? WINDOW_SHUT : WINDOW_OPEN;
    set_battle_window_top_lines_w1(g, BORDER_LINE, border);
    set_battle_window_top_lines_w2(g, BORDER_LINE, border);
    set_battle_window_registers(g, BG_WINDOWS, value);
    return screen_on();
}
SCREEN_OPERATION(set_bg_windows, value)

// $CD:3092
/* Set the layers' window logic; the border's first window follows it. */
static ScreenFlow set_window_logic(GameState *g, uint8_t value)
{
    if (value & LOGIC_INVERTED)
        set_battle_window_top_lines_w2(g, BORDER_LINE, BOTH_WINDOWS);
    bool shut = !(value & LOGIC_INVERTED) && (value & BOTH_LOGIC_BITS) == BOTH_LOGIC_BITS;
    set_battle_window_top_lines_w1(g, BORDER_LINE, shut ? WINDOW_SHUT : WINDOW_OPEN);
    set_battle_window_registers(g, BG_LOGIC, value & LOGIC_BITS);
    return screen_on();
}
SCREEN_OPERATION(set_window_logic, value)

/* ---- The fixed color ------------------------------------------------------------------------- */

/* The fixed color's setting: the color channels it goes to in the top three
 * bits, its intensity, 0-31, in the rest. */
enum { FIXED_CHANNELS = 0xE0, FIXED_INTENSITY = 0x1F };

// $CD:303B
/* Set the fixed color, or with a nonzero amount make it brighter or darker
 * (a signed byte) within its range, keeping its channels. */
static ScreenFlow change_fixed_color(GameState *g, uint8_t value, uint8_t amount)
{
    uint8_t fixed = battle_fixed_color(g), channels = fixed & FIXED_CHANNELS;
    if (amount == 0) {
        set_battle_fixed_color(g, value);
        return screen_on();
    }
    uint8_t intensity = (uint8_t)((fixed & FIXED_INTENSITY) + amount);
    if (intensity & FIXED_CHANNELS)
        intensity = (amount & 0x80) ? 0 : FIXED_INTENSITY;
    set_battle_fixed_color(g, (uint8_t)(channels | intensity));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x45, intensity));
    return screen_on();
}
SCREEN_OPERATION(change_fixed_color, value, amount)

/* ---- Colors ---------------------------------------------------------------------------------- */

/* A color, as loaded and as shown. */
static void set_color(GameState *g, unsigned color, uint16_t value)
{
    set_palette_base(g, color, value);
    set_palette_working(g, color, value);
}

// $CD:1864
/* Color the backdrop (color 0). */
static ScreenFlow set_backdrop(GameState *g, uint16_t color)
{
    set_color(g, 0, color);
    return screen_on();
}
SCREEN_OPERATION(set_backdrop, color)

// $CD:17BE
/* Copy a color over another, as loaded and as shown. */
static ScreenFlow copy_color(GameState *g, uint8_t from, uint8_t onto)
{
    set_palette_base(g, onto, palette_base(g, from));
    set_palette_working(g, onto, palette_working(g, from));
    return screen_on();
}
SCREEN_OPERATION(copy_color, from, onto)

/* Two sets of the ROM's background colors go over three colors each of the
 * background's second row; a set of $FF leaves its colors as they are. */
enum { NO_SET = 0xFF, FIRST_SET_COLOR = 25, SECOND_SET_COLOR = 29 };

// $CD:1004
void battle_show_background_sets(GameState *g)
{
    static const unsigned onto[2] = { FIRST_SET_COLOR, SECOND_SET_COLOR };
    for (unsigned which = 0; which < 2; which++) {
        uint8_t set = battle_screen_background_colors(g, which);
        if (set == NO_SET)
            continue;
        for (unsigned color = 0; color < BACKGROUND_SET_COLORS; color++)
            set_color(g, onto[which] + color, background_set_color(g, set, color));
    }
}

// $CD:1F3C
/* Show a set of background colors in both places. */
static ScreenFlow show_background_colors(GameState *g, uint8_t set)
{
    set_battle_screen_background_colors(g, 0, set);
    set_battle_screen_background_colors(g, 1, set);
    battle_show_background_sets(g);
    return screen_on();
}
SCREEN_OPERATION(show_background_colors, set)

// $CD:1AB2
/* The same, the set the one an animation script's variable 10 names. */
enum { BACKGROUND_SET_VARIABLE = 10 };

static ScreenFlow show_background_colors_of_variable(GameState *g)
{
    return show_background_colors(g, battle_script_variables(g, BACKGROUND_SET_VARIABLE));
}
SCREEN_OPERATION_TAKING_NOTHING(show_background_colors_of_variable)

/* The combatants' sprite palettes, and the cursors', move to the other half
 * of the sprite palettes by this bit of their attributes. */
enum { OTHER_PALETTES = 0x08 };

// $CD:1B78
/* Point the combatants' and the cursors' sprites at the other half of the
 * sprite palettes (the swap of palette rows, screen_palette.c, puts them
 * back). */
static ScreenFlow flip_sprite_palettes(GameState *g)
{
    set_battle_sprite_palettes_flipped(g, (uint8_t)(battle_sprite_palettes_flipped(g) + 1));
    for (unsigned slot = 0; slot < BATTLE_OBJECT_PALETTE_COUNT; slot++)
        set_battle_object_palette(g, slot, battle_object_palette(g, slot) ^ OTHER_PALETTES);
    set_battle_cursor_priority(g, battle_cursor_priority(g) ^ OTHER_PALETTES);
    return screen_on();
}
SCREEN_OPERATION_TAKING_NOTHING(flip_sprite_palettes)

/* ---- Palette animations -------------------------------------------------------------------- */

/* The slots a script starts an animation in, from slot 4, twelve bytes a
 * slot. */
enum { FIRST_SCRIPT_SLOT = 4, SLOT_BYTES = 12 };

// $CD:29E3
/* Start a palette animation in one of the slots from 4: its kind and fade
 * channels, its colors, its levels, its step, delay and period. */
static ScreenFlow start_palette_animation(GameState *g, uint8_t kind, uint8_t mode, uint8_t first, uint8_t count,
                                          uint8_t level, uint8_t target, uint8_t step, uint8_t slot, uint8_t delay,
                                          uint8_t period)
{
    unsigned s = FIRST_SCRIPT_SLOT + script_palette_slot_offset(g, slot) / SLOT_BYTES;
    set_palette_animations_kind(g, s, kind);
    set_palette_animations_fade_mode(g, s, mode);
    set_palette_animations_first_color(g, s, first);
    set_palette_animations_color_count(g, s, count);
    set_palette_animations_fade_level(g, s, level);
    set_palette_animations_fade_target(g, s, target);
    set_palette_animations_step(g, s, step);
    set_palette_animations_period(g, s, period);
    set_palette_animations_delay(g, s, delay);
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x45, (uint8_t)(kind | mode)));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x47, (uint8_t)(step | slot)));
    return screen_on();
}
static ScreenFlow start_palette_animation_command(GameState *g, const ScreenCommand *c)
{
    return start_palette_animation(g, c->kind, c->mode, c->first, c->count, c->level, c->target, c->step, c->slot,
                                   c->delay, c->period);
}

/* The battle's own palette animations, slots 8-11, which a script stops
 * every 256th time it counts. */
enum { FIRST_BATTLE_SLOT = 8, BATTLE_SLOTS = 4 };

// $CD:1834
static ScreenFlow count_toward_palette_stop(GameState *g)
{
    uint8_t count = (uint8_t)(battle_screen_palette_stop_count(g) + 1);
    set_battle_screen_palette_stop_count(g, count);
    if (count == 0)
        for (unsigned slot = FIRST_BATTLE_SLOT + BATTLE_SLOTS; slot-- > FIRST_BATTLE_SLOT;)
            set_palette_animations_kind(g, slot, 0);
    return screen_on();
}
SCREEN_OPERATION_TAKING_NOTHING(count_toward_palette_stop)

/* ---- Requests to the frame wait and switches of the frame work --------------------------------- */

/* Where a sound plays across the screen: the middle, or the first animation
 * position. */
enum { PAN_MIDDLE = 0x80 };

// $CD:1923 (in the middle), $CD:18CF (at the first animation position)
/* Ask the frame wait to play a sound. */
static ScreenFlow play_sound(GameState *g, uint8_t sound, bool at_position)
{
    set_battle_script_sound(g, sound);
    set_battle_script_sound_pan(g, at_position ? screen_position_across(g, 0) : PAN_MIDDLE);
    set_battle_script_sound_request(g, (uint8_t)(battle_script_sound_request(g) + 1));
    return screen_on();
}
SCREEN_OPERATION(play_sound, sound, at_position)

// $CD:180A
/* Ask the frame wait to load an effect's graphics. */
static ScreenFlow load_effect_graphics(GameState *g, uint8_t effect)
{
    set_battle_effect_graphics_effect(g, effect);
    set_battle_effect_graphics_wanted(g, (uint8_t)(battle_effect_graphics_wanted(g) + 1));
    return screen_on();
}
SCREEN_OPERATION(load_effect_graphics, effect)

// $CD:1F4D
/* Choose the VRAM row the effect's frames go to. */
static ScreenFlow set_frame_row(GameState *g, uint8_t value)
{
    set_battle_block_upload_row(g, value);
    return screen_on();
}
SCREEN_OPERATION(set_frame_row, value)

// $CD:2687
/* Have the line handler apply the window's arrangement anew, from another
 * base. */
static ScreenFlow rebase_arrangement(GameState *g, uint8_t value)
{
    battle_arrange_the_lines(g, value);
    return screen_on();
}
SCREEN_OPERATION(rebase_arrangement, value)

/* The frame work's switches a script sets. */
typedef enum { SPRITES_FROZEN, KEEP_SPRITES, NO_LINE_WAIT, PALETTES_PAUSED, GRADIENT_LEVEL } FrameSwitch;

// $CD:1918, $CD:190D, $CD:1907, $CD:18C2, $CD:17DF
static ScreenFlow set_frame_switch(GameState *g, uint8_t which, uint8_t value)
{
    switch ((FrameSwitch)which) {
    case SPRITES_FROZEN: set_battle_sprites_frozen_setting(g, value); break;
    case KEEP_SPRITES: set_battle_keep_sprites(g, value); break;
    case NO_LINE_WAIT: set_battle_effect_line_wait_off(g, value); break;
    case PALETTES_PAUSED: set_battle_palette_pause_request(g, value); break;
    case GRADIENT_LEVEL: set_battle_gradient_level_override(g, value); break;
    }
    return screen_on();
}
SCREEN_OPERATION(set_frame_switch, which, value)

SCREEN_OPCODES(screen_setting_opcodes,
    { 0x9D, OPERATION(show_layers), .length = 1, .setting = CONSTANT(MAIN_SCREEN), .layers = BYTE(0) },
    { 0x9E, OPERATION(show_layers), .length = 1, .setting = CONSTANT(SUB_SCREEN), .layers = BYTE(0) },
    { 0x9F, OPERATION(show_layers), .length = 1, .setting = CONSTANT(MAIN_WINDOWED), .layers = BYTE(0) },
    { 0xA0, OPERATION(show_layers), .length = 1, .setting = CONSTANT(SUB_WINDOWED), .layers = BYTE(0) },
    { 0x9B, OPERATION(show_layers), .length = 1, .setting = CONSTANT(COLOR_MATH_LAYERS), .layers = BYTE(0) },
    { 0xF5, OPERATION(set_layer_mask), .length = 1, .layers = BYTE(0) },
    { 0x9A, OPERATION(set_color_math), .length = 1, .how = BYTE(0) },
    { 0x93, OPERATION(set_sub_screen_and_color_math), .length = 3, .value = BYTE(0), .how = BYTE(1), .layers = BYTE(2) },
    { 0xB0, OPERATION(set_screen_mode), .length = 1, .mode = BYTE(0) },
    { 0x90, OPERATION(change_mosaic), .length = 1, .layers = HIGH_NIBBLE(0), .by = LOW_NIBBLE(0), .smaller = TOP_BIT(0) },
    { 0x95, OPERATION(set_window_settings), .length = 1, .which = CONSTANT(BG3_BG4_WINDOWS), .value = BYTE(0) },
    { 0x96, OPERATION(set_window_settings), .length = 1, .which = CONSTANT(OBJECT_WINDOWS), .value = BYTE(0) },
    { 0x99, OPERATION(set_window_settings), .length = 1, .which = CONSTANT(OBJECT_LOGIC), .value = BYTE(0) },
    { 0x94, OPERATION(set_bg_windows), .length = 1, .value = BYTE(0) },
    { 0x98, OPERATION(set_window_logic), .length = 1, .value = BYTE(0) },
    { 0x9C, OPERATION(change_fixed_color), .length = 2, .value = BYTE(0), .amount = BYTE(1) },
    { 0xEA, OPERATION(set_backdrop), .length = 2, .color = WORD(0) },
    { 0xF8, OPERATION(copy_color), .length = 2, .from = BYTE(0), .onto = BYTE(1) },
    { 0xB9, OPERATION(show_background_colors), .length = 1, .set = BYTE(0) },
    { 0xD2, OPERATION(show_background_colors_of_variable), .length = 1 },
    { 0xC9, OPERATION(flip_sprite_palettes), .length = 0 },
    { 0x8E, .operation = start_palette_animation_command, .length = 7, .kind = MASKED(0, 0xF0), .mode = LOW_NIBBLE(0), .first = BYTE(1), .count = BYTE(2), .level = BYTE(3), .target = BYTE(4), .step = MASKED(5, 0xF8), .slot = MASKED(5, 0x07), .delay = HIGH_NIBBLE(6), .period = LOW_NIBBLE(6) },
    { 0xEF, OPERATION(count_toward_palette_stop), .length = 0 },
    { 0xDC, OPERATION(play_sound), .length = 1, .sound = BYTE(0), .at_position = CONSTANT(false) },
    { 0xE4, OPERATION(play_sound), .length = 1, .sound = BYTE(0), .at_position = CONSTANT(true) },
    { 0xF2, OPERATION(load_effect_graphics), .length = 1, .effect = BYTE(0) },
    { 0xB7, OPERATION(set_frame_row), .length = 1, .value = BYTE(0) },
    { 0x87, OPERATION(rebase_arrangement), .length = 1, .value = BYTE(0) },
    { 0xDE, OPERATION(set_frame_switch), .length = 1, .which = CONSTANT(SPRITES_FROZEN), .value = BYTE(0) },
    { 0xE0, OPERATION(set_frame_switch), .length = 1, .which = CONSTANT(KEEP_SPRITES), .value = BYTE(0) },
    { 0xE2, OPERATION(set_frame_switch), .length = 1, .which = CONSTANT(NO_LINE_WAIT), .value = BYTE(0) },
    { 0xE6, OPERATION(set_frame_switch), .length = 1, .which = CONSTANT(PALETTES_PAUSED), .value = BYTE(0) },
    { 0xF7, OPERATION(set_frame_switch), .length = 1, .which = CONSTANT(GRADIENT_LEVEL), .value = BYTE(0) },
);

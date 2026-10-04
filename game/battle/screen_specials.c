/* Battle: the screen scripts' special effects (command $80).
 *
 * A few spells and attacks need a screen effect only they use: bands of
 * colored light down the screen, a wedge of window opening from the middle or
 * the left, a layer bobbing on a wave, the waves' window lines marked one by
 * one, the background's colors moved or whitened, the enemies hidden. A
 * screen script asks for one by number with command $80; each is a small
 * operation here, in the table the command dispatches through.
 *
 * The bands of light are the fixed color's per-line tables, one for each of
 * its channels (blue, green, red), which the line effect sends the fixed
 * color from: 32 bands, each a run of lines and the intensity the channel has
 * on them. */
#include "battle/screen_script.h"

#include "battle/bridge.h"
#include "battle/screen_lines.h"
#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/video.h"

/* ---- The bands of light ------------------------------------------------------------------------ */

/* The three tables in battle_effect_line_work, a channel's each, 32 bands of
 * four bytes (the band's lines, then its color). */
typedef enum { BLUE_BANDS, GREEN_BANDS, RED_BANDS, BAND_TABLES } BandTable;
enum { BANDS = 32, BAND_BYTES = 4, BAND_TABLE_BYTES = BANDS * BAND_BYTES, FIRST_BAND_TABLE = 1, BAND_LINES = 0, BAND_COLOR = 1 };

static const uint8_t band_channel[BAND_TABLES] = { VID_BLUE, VID_GREEN, VID_RED };

static unsigned band_at(BandTable t, unsigned band) { return FIRST_BAND_TABLE + t * BAND_TABLE_BYTES + band * BAND_BYTES; }

static void set_band(GameState *g, BandTable t, unsigned band, uint8_t lines, uint8_t intensity)
{
    set_battle_effect_line_work(g, band_at(t, band) + BAND_LINES, lines);
    set_battle_effect_line_work(g, band_at(t, band) + BAND_COLOR, vid_line_fixed_color(band_channel[t], intensity));
}

/* The bands' light rises two levels a band to the middle and falls back,
 * less how far it has faded; a fade comes up a level each time the bands are
 * laid. */
enum { BAND_RISE = 2, HALF_THE_BANDS = BANDS / 2, BANDS_OF_FOUR_LINES = 4, BANDS_OF_TWENTY_LINES = 20 };

static uint8_t band_intensity(unsigned band, uint8_t fade)
{
    unsigned from_the_edge = band < HALF_THE_BANDS ? band : BANDS - 1 - band;
    unsigned level = BAND_RISE * from_the_edge;
    return level > fade ? (uint8_t)(level - fade) : 0;
}

/* What laying the bands leaves in the cells: the bands' lines, the fade, and
 * the last band's level. */
static void leave_bands(GameState *g, uint8_t lines, uint8_t fade)
{
    screen_leave_cell(g, 0x45, lines);
    screen_leave_cell(g, 0x47, fade);
    screen_leave_cell(g, 0x49, band_intensity(BANDS - 1, fade));
}

// $CD:2D51
static void lay_bands_of_light(GameState *g, uint8_t lines)
{
    uint8_t fade = battle_band_fade(g);
    if (fade)
        set_battle_band_fade(g, --fade);
    for (unsigned band = 0; band < BANDS; band++)
        for (BandTable t = BLUE_BANDS; t < BAND_TABLES; t++)
            set_band(g, t, band, lines, band_intensity(band, fade));
    ORIGINAL_EFFECT(leave_bands(g, lines, fade));
}

// $CD:2D4A
/* Effect 0: bands of light four lines each, at once at their full. */
static void bands_of_light_at_full(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_battle_band_fade(g, 0);
    lay_bands_of_light(g, BANDS_OF_FOUR_LINES);
}

// $CD:2D44
/* Effect 14: bands of light four lines each, coming up a level. */
static void bands_of_light(GameState *g, ScreenChannel channel)
{
    (void)channel;
    lay_bands_of_light(g, BANDS_OF_FOUR_LINES);
}

/* The animation scripts' variable the tall bands signal on. */
enum { BANDS_SIGNAL = 26 };

// $CD:2D39
/* Effect 19: tall bands of light, twenty lines each, coming up a level, and
 * the signal to the animation script that they are up. */
static void tall_bands_of_light(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_battle_script_variables(g, BANDS_SIGNAL, 1);
    lay_bands_of_light(g, BANDS_OF_TWENTY_LINES);
}

static uint8_t band_color(const GameState *g, unsigned band_of_all)
{
    return battle_effect_line_work(g, band_at((BandTable)(band_of_all / BANDS), band_of_all % BANDS) + BAND_COLOR);
}

static void set_band_color(GameState *g, unsigned band_of_all, uint8_t color)
{
    set_battle_effect_line_work(g, band_at((BandTable)(band_of_all / BANDS), band_of_all % BANDS) + BAND_COLOR, color);
}

// $CD:2D1F
/* Effect 1: roll the bands' colors up a band, the three tables' bands taken
 * as one ring of 96, the first's color going round to the last. */
static void roll_the_bands(GameState *g, ScreenChannel channel)
{
    (void)channel;
    uint8_t first = band_color(g, 0);
    for (unsigned band = 0; band + 1 < BAND_TABLES * BANDS; band++)
        set_band_color(g, band, band_color(g, band + 1));
    set_band_color(g, BAND_TABLES * BANDS - 1, first);
}

// $CD:2B45
/* Effect 16: put out every band's light (every channel at nothing). */
static void put_out_the_bands(GameState *g, ScreenChannel channel)
{
    (void)channel;
    for (unsigned band = 0; band < BAND_TABLES * BANDS; band++)
        set_band_color(g, band, vid_line_fixed_color(VID_ALL_CHANNELS, 0));
}

/* ---- The first triangle as a wedge ---------------------------------------------------------- */

/* A few effects shape the first triangle (screen_shapes.c) themselves. */
enum { FIRST_TRIANGLE = 0, WEDGE_OPENS_BY = 2 };

static void set_corners(GameState *g, BattlePoint a, BattlePoint b, BattlePoint c)
{
    ScreenTriangle t = { { a, b, c } };
    screen_set_triangle(g, FIRST_TRIANGLE, t);
}

// $CD:2CF5
/* Effect 2: the first triangle closed to a line down the middle of the
 * battlefield, from its top. */
static void close_wedge_in_the_middle(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_corners(g, (BattlePoint){ 0x80, 0 }, (BattlePoint){ 0x80, 0xD4 }, (BattlePoint){ 0x80, 0xD4 });
}

// $CD:2D0C
/* Effect 3: open the wedge down the middle, its foot's corners apart. */
static void open_wedge_in_the_middle(GameState *g, ScreenChannel channel)
{
    (void)channel;
    ScreenTriangle t = screen_triangle(g, FIRST_TRIANGLE);
    t.corner[1].x = (uint8_t)(t.corner[1].x + WEDGE_OPENS_BY);
    t.corner[2].x = (uint8_t)(t.corner[2].x - WEDGE_OPENS_BY);
    screen_set_triangle(g, FIRST_TRIANGLE, t);
}

// $CD:2C98
/* Effect 6: the first triangle as a wedge from the left edge, point to the
 * right. */
static void wedge_from_the_left(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_corners(g, (BattlePoint){ 0, 1 }, (BattlePoint){ 0, 0x9F }, (BattlePoint){ 0xC0, 0x50 });
}

// $CD:2C8B
/* Effect 7: close the wedge from the left, its edge's corners together. */
static void close_wedge_from_the_left(GameState *g, ScreenChannel channel)
{
    (void)channel;
    ScreenTriangle t = screen_triangle(g, FIRST_TRIANGLE);
    t.corner[0].y = (uint8_t)(t.corner[0].y + WEDGE_OPENS_BY);
    t.corner[1].y = (uint8_t)(t.corner[1].y - WEDGE_OPENS_BY);
    screen_set_triangle(g, FIRST_TRIANGLE, t);
}

// $CD:2CB3
/* Effect 5: turn the first triangle's corners round, each taking the one
 * before's place. */
static void turn_the_corners_round(GameState *g, ScreenChannel channel)
{
    (void)channel;
    ScreenTriangle t = screen_triangle(g, FIRST_TRIANGLE);
    set_corners(g, t.corner[2], t.corner[0], t.corner[1]);
}

// $CD:2C7F
/* Effect 8: both triangles' corners back to their first angle. */
static void straighten_the_angles(GameState *g, ScreenChannel channel)
{
    (void)channel;
    for (unsigned i = 0; i < BATTLE_SHAPE_ANGLES_COUNT; i++)
        set_battle_shape_angles(g, i, 0);
}

/* ---- The windows ---------------------------------------------------------------------------- */

// $CD:2CDC
/* Effect 4: close both windows on every line of the first buffer, from the
 * top border's last line on, and start the line wave from its beginning. */
static void close_the_windows(GameState *g, ScreenChannel channel)
{
    (void)channel;
    for (TableLine line = -1; line < SCREEN_LINES - 1; line++)
        for (unsigned w = WINDOW_1; w < WINDOWS; w++)
            set_line_edges(g, line, (Window)w, edges_between(0, 0));
    set_battle_line_wave_phase(g, 0);
}

/* The nudges for the window lines: a wave twelve columns high, four times
 * over the table. */
enum { NUDGE_WAVE_HEIGHT = 12, NUDGE_WAVE_STEP = 4, NUDGES = 0x100 };

/* The original multiplies the wave's height by each step's sine: the
 * multiplier is left with the last. */
static void leave_last_nudge(GameState *g)
{
    hw_set_multiplicand(g, NUDGE_WAVE_HEIGHT);
    hw_multiply_by(g, battle_sine_magnitude(g, (uint8_t)((NUDGES - 1) * NUDGE_WAVE_STEP)));
}

// $CD:2B69
/* Effect 12: lay the window lines' nudges out as a wave. */
static void lay_out_the_nudge_wave(GameState *g, ScreenChannel channel)
{
    (void)channel;
    for (unsigned n = 0; n < NUDGES; n++)
        set_battle_window_line_deltas(g, n, screen_scaled_sine(g, (uint8_t)(n * NUDGE_WAVE_STEP), NUDGE_WAVE_HEIGHT));
    ORIGINAL_EFFECT(leave_last_nudge(g));
}

// $CD:2B64
/* Effect 13: nudge the window lines by their nudges (screen_window.c). */
static void nudge_the_window_lines(GameState *g, ScreenChannel channel)
{
    (void)channel;
    screen_nudge_window_edges(g);
}

/* The waves' lines: each line's window 1 edges the waves ripple about, and a
 * mark on lines the effects pick out at random. */
enum { WAVE_LINES = BATTLE_WAVE_LINES_COUNT, MARKED_SHIFT = 16, MARKS_A_TIME = 3 };

// $CD:2AEF
/* Effect 21: clear the waves' lines and their marks. */
static void clear_the_wave_lines(GameState *g, ScreenChannel channel)
{
    (void)channel;
    for (unsigned line = 0; line < WAVE_LINES; line++) {
        set_battle_wave_lines_edges(g, line, 0);
        set_battle_wave_lines_spare(g, line, 0);
        set_battle_wave_marks_mark(g, line, 0);
        set_battle_wave_marks_spare(g, line, 0);
    }
}

// $CD:2AAF
/* A line picked from the ROM's random bytes, a step further each time (the
 * window effects' second step count keeps the place). A pick can be past the
 * battlefield's lines, whose marks nothing reads. */
static void mark_a_random_line(GameState *g)
{
    uint8_t place = battle_window_steps(g, 1);
    set_battle_window_steps(g, 1, (uint8_t)(place + 1));
    uint8_t line = battle_random_table(g, place);
    set_battle_wave_marks_mark(g, line, (uint16_t)(battle_wave_marks_mark(g, line) + 1));
}

// $CD:2AC6
/* Effect 22: mark three lines at random, and move every marked line's window
 * 16 columns left (as one word: where its left edge passes the screen's, its
 * right one goes a column further). */
static void shift_marked_lines(GameState *g, ScreenChannel channel)
{
    (void)channel;
    for (unsigned n = 0; n < MARKS_A_TIME; n++)
        mark_a_random_line(g);
    for (unsigned line = 0; line < WAVE_LINES; line++)
        if (battle_wave_marks_mark(g, line))
            set_battle_wave_lines_edges(g, line, (Edges)(battle_wave_lines_edges(g, line) - MARKED_SHIFT));
}

/* The window effects' steps, and the frame count, as a wipe restarts. */
enum { WIPE_RESTART_STEPS = 0x60, WIPE_RESTART_FRAME = 0x40 };

// $CD:2B05
/* Effect 20: start a wipe over: its steps, and the battle's frame count it
 * flows with. */
static void restart_the_wipe(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_battle_window_steps(g, 0, WIPE_RESTART_STEPS);
    set_battle_window_steps(g, 1, 0);
    set_battle_frame_count(g, WIPE_RESTART_FRAME);
}

/* ---- The layer ---------------------------------------------------------------------------------- */

// $CD:2BD2, $CD:2BA8
/* Effects 10 and 11: bob the channel's layer down (or up) by the next step of
 * the ROM's wave of steps. */
static void bob_layer(GameState *g, ScreenChannel channel, bool up)
{
    uint8_t step = battle_screen_wave_step(g, channel);
    int8_t by = battle_layer_wave_step(g, step);
    uint16_t offset = battle_screen_layer_offset_y(g, channel);
    set_battle_screen_layer_offset_y(g, channel, (uint16_t)(up ? offset - by : offset + by));
    set_battle_screen_wave_step(g, channel, (uint8_t)(step + 1));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, (uint16_t)by));
}

static void bob_layer_down(GameState *g, ScreenChannel channel) { bob_layer(g, channel, false); }
static void bob_layer_up(GameState *g, ScreenChannel channel) { bob_layer(g, channel, true); }

/* ---- The sprites ---------------------------------------------------------------------------------- */

/* The effect's first sixteen sprites go in at the fifth sprite of the OAM
 * buffer, in front of the rest, which move sixteen on (the last sixteen
 * dropping off the end); they are large, at the channel's layer, in front of
 * everything, and those that fall below the battlefield (or off the screen)
 * hidden. */
enum { FIRST_SHOWN = 4, SPRITES_SHOWN = 16, SPRITES = OAM_BUFFER_COUNT, SPRITES_A_HIGH_BYTE = 4, SPRITE_BYTES = 4,
       IN_FRONT = 0x30, LARGE_SPRITES_BYTE = 0xAA, HIDDEN_LINE = 0xF0, BELOW_THE_BATTLEFIELD = 0xD5 };

static uint8_t sprite_line(uint16_t down)
{
    uint8_t line = (uint8_t)down;
    bool off_the_screen = down & 0x100;
    if (off_the_screen)
        return line >= HIDDEN_LINE ? line : HIDDEN_LINE;
    return line >= BELOW_THE_BATTLEFIELD ? HIDDEN_LINE : line;
}

static void move_sprite(GameState *g, unsigned to, unsigned from)
{
    set_oam_buffer_x(g, to, oam_buffer_x(g, from));
    set_oam_buffer_y(g, to, oam_buffer_y(g, from));
    set_oam_buffer_tile(g, to, oam_buffer_tile(g, from));
    set_oam_buffer_attributes(g, to, oam_buffer_attributes(g, from));
}

/* What the effect leaves in the cells: the layer's place across and down. */
static void leave_layer_place(GameState *g, uint16_t across, uint16_t down)
{
    screen_leave_word(g, 0x45, across);
    screen_leave_word(g, 0x47, down);
}

// $CD:2BFC
/* Effect 9: show the effect's first sixteen sprites in front of the rest. */
static void show_effect_sprites_in_front(GameState *g, ScreenChannel channel)
{
    for (unsigned s = SPRITES - SPRITES_SHOWN; s-- > FIRST_SHOWN;)
        move_sprite(g, s + SPRITES_SHOWN, s);
    enum { HIGH_BYTES_SHOWN = SPRITES_SHOWN / SPRITES_A_HIGH_BYTE, FIRST_HIGH = FIRST_SHOWN / SPRITES_A_HIGH_BYTE };
    for (unsigned b = OAM_HIGH_TABLE_BYTES - HIGH_BYTES_SHOWN; b-- > FIRST_HIGH;)
        set_oam_high_table_byte(g, b + HIGH_BYTES_SHOWN, oam_high_table_byte(g, b));
    uint16_t across = (uint16_t)(battle_screen_layer_x(g, channel) + battle_screen_layer_offset_x(g, channel));
    uint16_t down = (uint16_t)(battle_screen_layer_y(g, channel) + battle_screen_layer_offset_y(g, channel));
    for (unsigned s = 0; s < SPRITES_SHOWN; s++) {
        unsigned sprite = FIRST_SHOWN + s, at = s * SPRITE_BYTES;
        set_oam_buffer_x(g, sprite, (uint8_t)(across + battle_effect_sprite_data(g, at)));
        set_oam_buffer_y(g, sprite, sprite_line((uint16_t)(down + battle_effect_sprite_data(g, at + 1))));
        set_oam_buffer_tile(g, sprite, battle_effect_sprite_data(g, at + 2));
        set_oam_buffer_attributes(g, sprite, (uint8_t)(battle_effect_sprite_data(g, at + 3) | IN_FRONT));
    }
    for (unsigned b = 0; b < HIGH_BYTES_SHOWN; b++)
        set_oam_high_table_byte(g, FIRST_HIGH + b, LARGE_SPRITES_BYTE);
    ORIGINAL_EFFECT(leave_layer_place(g, across, down));
}

/* ---- Colors, objects and signals ---------------------------------------------------------------- */

// $CD:2A9C, $CD:2A97, $CD:2AA5, $CD:2AAA (screen_palette.c's)
static void whiten_the_scene(GameState *g, ScreenChannel channel) { (void)channel; screen_whiten_scene(g); }
static void whiten_the_high_sprite_palettes(GameState *g, ScreenChannel channel) { (void)channel; screen_whiten_high_sprite_palettes(g); }
static void move_the_band_up(GameState *g, ScreenChannel channel) { (void)channel; screen_move_band_up(g); }
static void move_the_band_down(GameState *g, ScreenChannel channel) { (void)channel; screen_move_band_down(g); }

/* The animation slots that rotate the background's cycle once the rule's
 * hold on the palette animations is lifted (the frame work stops keeping
 * them off it, battle/frame.c). */
enum { CYCLE_SLOTS = 10, CYCLE_SLOT_COUNT = 2, PALETTE_END = 0x80 };

// $CD:2B27
/* Effect 17: lift the rule's limit, and rotate the background's colors from
 * the cycle's first to the end of the background's palettes in slots 10 and
 * 11. */
static void cycle_the_background(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_battle_enemy_rule(g, (uint8_t)(battle_enemy_rule(g) | BATTLE_RULE_LIFTED));
    uint8_t first = battle_background_cycle(g);
    for (unsigned slot = CYCLE_SLOTS; slot < CYCLE_SLOTS + CYCLE_SLOT_COUNT; slot++) {
        set_palette_animations_first_color(g, slot, first);
        set_palette_animations_color_count(g, slot, (uint8_t)(PALETTE_END - first));
    }
}

/* The first animation position's line stays on the battlefield. */
enum { HIGHEST_POSITION = 0x31, LOWEST_POSITION = 0xD4 };

// $CD:2B12
/* Effect 18: keep the first animation position on the battlefield. */
static void keep_the_position_on_the_battlefield(GameState *g, ScreenChannel channel)
{
    (void)channel;
    uint16_t at = battle_animation_positions(g, 0);
    uint8_t line = (uint8_t)(at >> 8);
    line = line < HIGHEST_POSITION ? HIGHEST_POSITION : line >= LOWEST_POSITION ? LOWEST_POSITION : line;
    set_battle_animation_positions(g, 0, (uint16_t)((at & 0x00FF) | line << 8));
}

// $CD:2B5C
/* Effect 15: the object an effect last ended on loses its sprites' extra
 * attribute bits. */
static void clear_the_ended_objects_attributes(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_battle_object_attribute_bits(g, battle_ended_effect_object(g), 0);
}

// $CD:2A8B
/* Effect 27: hide every enemy (each one's hidden count goes up). */
static void hide_the_enemies(GameState *g, ScreenChannel channel)
{
    (void)channel;
    for (unsigned o = BATTLE_FIRST_ENEMY; o < BATTLE_COMBATANTS; o++)
        set_battle_object_hidden(g, o, (uint8_t)(battle_object_hidden(g, o) + 1));
}

// $CD:2AA1
/* Effect 25: give the other channel the signal its wait waits for (the flag
 * that also turns the HP gradient the other way). */
static void give_the_signal(GameState *g, ScreenChannel channel)
{
    (void)channel;
    set_battle_gradient_reversed(g, (uint8_t)(battle_gradient_reversed(g) + 1));
}

/* ---- The command ---------------------------------------------------------------------------------- */

typedef void (*SpecialEffectOperation)(GameState *g, ScreenChannel channel);

/* The effects by number: each operation, and the original's routine for it,
 * whose time the effect takes (as screen_script.c's handlers are the
 * commands'). */
static const struct {
    SpecialEffectOperation operation;
    uint32_t timed_as;
} special_effects[SPECIAL_EFFECTS] = {
    [SPECIAL_BANDS_OF_LIGHT_AT_FULL] = { bands_of_light_at_full, 0xCD2D4A },
    [SPECIAL_ROLL_THE_BANDS] = { roll_the_bands, 0xCD2D1F },
    [SPECIAL_CLOSE_WEDGE_IN_THE_MIDDLE] = { close_wedge_in_the_middle, 0xCD2CF5 },
    [SPECIAL_OPEN_WEDGE_IN_THE_MIDDLE] = { open_wedge_in_the_middle, 0xCD2D0C },
    [SPECIAL_CLOSE_THE_WINDOWS] = { close_the_windows, 0xCD2CDC },
    [SPECIAL_TURN_THE_CORNERS_ROUND] = { turn_the_corners_round, 0xCD2CB3 },
    [SPECIAL_WEDGE_FROM_THE_LEFT] = { wedge_from_the_left, 0xCD2C98 },
    [SPECIAL_CLOSE_WEDGE_FROM_THE_LEFT] = { close_wedge_from_the_left, 0xCD2C8B },
    [SPECIAL_STRAIGHTEN_THE_ANGLES] = { straighten_the_angles, 0xCD2C7F },
    [SPECIAL_SHOW_EFFECT_SPRITES_IN_FRONT] = { show_effect_sprites_in_front, 0xCD2BFC },
    [SPECIAL_BOB_LAYER_DOWN] = { bob_layer_down, 0xCD2BD2 },
    [SPECIAL_BOB_LAYER_UP] = { bob_layer_up, 0xCD2BA8 },
    [SPECIAL_LAY_OUT_THE_NUDGE_WAVE] = { lay_out_the_nudge_wave, 0xCD2B69 },
    [SPECIAL_NUDGE_THE_WINDOW_LINES] = { nudge_the_window_lines, 0xCD2B64 },
    [SPECIAL_BANDS_OF_LIGHT] = { bands_of_light, 0xCD2D44 },
    [SPECIAL_CLEAR_THE_ENDED_OBJECTS_ATTRIBUTES] = { clear_the_ended_objects_attributes, 0xCD2B5C },
    [SPECIAL_PUT_OUT_THE_BANDS] = { put_out_the_bands, 0xCD2B45 },
    [SPECIAL_CYCLE_THE_BACKGROUND] = { cycle_the_background, 0xCD2B27 },
    [SPECIAL_KEEP_THE_POSITION_ON_THE_BATTLEFIELD] = { keep_the_position_on_the_battlefield, 0xCD2B12 },
    [SPECIAL_TALL_BANDS_OF_LIGHT] = { tall_bands_of_light, 0xCD2D39 },
    [SPECIAL_RESTART_THE_WIPE] = { restart_the_wipe, 0xCD2B05 },
    [SPECIAL_CLEAR_THE_WAVE_LINES] = { clear_the_wave_lines, 0xCD2AEF },
    [SPECIAL_SHIFT_MARKED_LINES] = { shift_marked_lines, 0xCD2AC6 },
    [SPECIAL_MOVE_THE_BAND_DOWN] = { move_the_band_down, 0xCD2AAA },
    [SPECIAL_MOVE_THE_BAND_UP] = { move_the_band_up, 0xCD2AA5 },
    [SPECIAL_GIVE_THE_SIGNAL] = { give_the_signal, 0xCD2AA1 },
    [SPECIAL_WHITEN_THE_SCENE] = { whiten_the_scene, 0xCD2A9C },
    [SPECIAL_HIDE_THE_ENEMIES] = { hide_the_enemies, 0xCD2A8B },
    [SPECIAL_WHITEN_THE_HIGH_SPRITE_PALETTES] = { whiten_the_high_sprite_palettes, 0xCD2A97 },
};

// $CD:2A4A
/* Command $80: run a special effect. */
static void run_special_effect(GameState *g, ScreenChannel channel, SpecialEffect effect)
{
    TIMED_AS(g, special_effects[effect].timed_as, special_effects[effect].operation(g, channel));
}

static ScreenFlow special_effect(GameState *g, ScreenChannel channel, SpecialEffect effect)
{
    run_special_effect(g, channel, effect);
    return screen_on();
}
SCREEN_OPERATION(special_effect, channel, special)

/* An effect the table has; the one that clears an object's attributes needs
 * an object that has them. */
static bool is_one_of_the_effects(const GameState *g, const ScreenCommand *c)
{
    if (c->special >= SPECIAL_EFFECTS)
        return false;
    return c->special != SPECIAL_CLEAR_THE_ENDED_OBJECTS_ATTRIBUTES
           || battle_ended_effect_object(g) < BATTLE_OBJECT_ATTRIBUTE_BITS_COUNT;
}

SCREEN_OPCODES(screen_special_opcodes,
    { 0x80, OPERATION(special_effect), .needs = is_one_of_the_effects, .length = 1, .special = BYTE(0) },
);

/* ---- The entries ------------------------------------------------------------------------------ */

/* An effect entered as the command's handler jumps to it: its number doubled
 * in X (the doubling leaves the carry clear, which the window lines' nudge
 * starts its chain of sums with), running a channel. */
static bool names_an_effect(Snes *s, SpecialEffect *effect)
{
    if (s->cpu.x % 2 || s->cpu.x / 2 >= SPECIAL_EFFECTS)
        return false;
    *effect = (SpecialEffect)(s->cpu.x / 2);
    return true;
}

bool battle_special_effect_ready(Snes *s)
{
    SpecialEffect effect = SPECIAL_EFFECTS;
    if (!battle_on_page_a_high_clear(s) || m_flag(s, CPU_FLAG_C) || !names_an_effect(s, &effect))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool on_a_channel = battle_screen_channel(g) % SCREEN_CHANNEL_RECORD_BYTES == 0
                        && screen_running_channel(g) < SCREEN_CHANNELS;
    ScreenCommand c = { .special = effect };
    bool ready = on_a_channel && is_one_of_the_effects(g, &c);
    vars_peek(false);
    return ready;
}

void battle_screen_special_effect(Snes *s)
{
    GameState *g = state_of(s);
    SpecialEffect effect = (SpecialEffect)(s->cpu.x / 2);
    special_effects[effect].operation(g, screen_running_channel(g));
    s->cpu.a = 0;
    m_rts(s);
}

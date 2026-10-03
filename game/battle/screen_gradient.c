/* Battle: the HP gradient.
 *
 * Some battles color three rows of the background after the first enemy's
 * HP: a gradient of 14 colors, one set for each of eight HP levels (eighths
 * of the enemy's maximum HP), which turns through the rows so that the
 * background flows. When the enemy's level changes the gradient fades
 * from the colors shown to the next level's over 32 frames, one level at a
 * time (a script can set the level outright, in one fade). The frame work
 * steps it once a frame where the background has it, while no screen script
 * runs and palette animation slots 8-11 are free.
 *
 * The rows are background rows 5-7, colors 1-14 of each. Each takes the
 * gradient from its own point, which a phase picks and the frame work turns
 * (battle/frame.c), in one direction or the other. */
#include "battle/screen.h"

#include "original_effects.h"

/* ---- The gradient's colors ------------------------------------------------------------------ */

enum { GRADIENT_COLORS = GRADIENT_SET_COLORS, ROW_COLORS = SCREEN_ROW_COLORS, GRADIENT_FIRST_COLOR = 5 * ROW_COLORS + 1 };

/* The fading colors, and after them the same again, so that 14 colors from
 * any point of the turn run on without wrapping. */
static uint16_t fading_color(const GameState *g, unsigned i) { return battle_gradient_colors(g, i); }
static void set_fading_color(GameState *g, unsigned i, uint16_t v) { set_battle_gradient_colors(g, i, v); }

static void repeat_fading_colors(GameState *g)
{
    for (unsigned i = 0; i < GRADIENT_COLORS; i++)
        set_fading_color(g, GRADIENT_COLORS + i, fading_color(g, i));
}

/* A 15-bit color's channels: red in bits 0-4, green 5-9, blue 10-14. One step
 * of a channel moves it a level toward the target's. */
enum { RED = 0x001F, GREEN = 0x03E0, BLUE = 0x7C00, RED_LEVEL = 0x0001, GREEN_LEVEL = 0x0020, BLUE_LEVEL = 0x0400 };

static uint16_t step_channel(uint16_t color, uint16_t target, uint16_t channel, uint16_t level)
{
    if ((color & channel) > (target & channel))
        return (uint16_t)(color - level);
    if ((color & channel) < (target & channel))
        return (uint16_t)(color + level);
    return color;
}

static uint16_t step_toward(uint16_t color, uint16_t target)
{
    color = step_channel(color, target, RED, RED_LEVEL);
    color = step_channel(color, target, GREEN, GREEN_LEVEL);
    return step_channel(color, target, BLUE, BLUE_LEVEL);
}

/* The original compares each channel through a cell, and leaves the last
 * color's blue there over the high byte of its green. */
static void leave_last_target(GameState *g, uint16_t target)
{
    screen_leave_cell(g, 0x45, (uint8_t)((target & BLUE) >> 8));
    screen_leave_cell(g, 0x46, (uint8_t)((target & GREEN) >> 8));
}

/* ---- The HP level -------------------------------------------------------------------------- */

/* The level: how many eighths of its maximum HP the first enemy has, plus one,
 * at most 8. A script can set it instead: with the top bit set, the level is
 * the other seven. */
enum { LEVELS = 8, EIGHTHS = 8, FIRST_ENEMY = 3, LEVEL_SET = 0x80, FADE_FRAMES = 0x20 };

/* The original counts the thresholds in two cells: the one it stopped at and
 * the eighth. */
static void leave_thresholds(GameState *g, uint16_t eighth, uint8_t level)
{
    screen_leave_word(g, 0x45, (uint16_t)(eighth * level));
    screen_leave_word(g, 0x47, eighth);
}

static uint8_t enemy_hp_level(GameState *g)
{
    uint16_t hp = battle_stats_hp(g, FIRST_ENEMY), eighth = (uint16_t)(battle_stats_max_hp(g, FIRST_ENEMY) / EIGHTHS);
    uint8_t level = 1;
    while (level < LEVELS && hp >= (uint16_t)(eighth * level))
        level++;
    ORIGINAL_EFFECT(leave_thresholds(g, eighth, level));
    return level;
}

static uint8_t level_wanted(GameState *g)
{
    uint8_t level = enemy_hp_level(g), set = battle_gradient_level_override(g);
    return (set & LEVEL_SET) ? set & (uint8_t)~LEVEL_SET : level;
}

/* ---- The rows ---------------------------------------------------------------------------------- */

// $D1:EBD0
/* How far into the gradient a row starts, from how far its phase turns it:
 * reversed, the turn itself; otherwise the rest of the way round. */
static uint8_t row_start(const GameState *g, uint8_t turn)
{
    return battle_gradient_reversed(g) ? turn : (uint8_t)(GRADIENT_COLORS - turn);
}

// $D1:EBDF
/* Show the gradient in a row, from `start` colors in, as loaded and as shown. */
static void show_gradient_row(GameState *g, unsigned row, uint8_t start)
{
    for (unsigned i = 0; i < GRADIENT_COLORS; i++) {
        uint16_t color = fading_color(g, start + i);
        unsigned at = GRADIENT_FIRST_COLOR + row * ROW_COLORS + i;
        set_palette_base(g, at, color);
        set_palette_working(g, at, color);
    }
}

// $D1:EA01
/* The phase the background starts at: where the gradient's first color sits
 * in the first row (0 if it isn't there). */
static uint8_t starting_phase(const GameState *g)
{
    uint16_t first = gradient_first_color(g);
    for (uint8_t i = 0; i < GRADIENT_COLORS; i++)
        if (palette_base(g, GRADIENT_FIRST_COLOR + i) == first)
            return i;
    return 0;
}

/* ---- A frame of it ---------------------------------------------------------------------------- */

/* Palette animation slots 8-11 are the battle's effects'; the gradient waits
 * while any runs. */
enum { FIRST_EFFECT_ANIMATION = 8, EFFECT_ANIMATIONS = 4 };

static bool screen_busy(const GameState *g)
{
    if (battle_screen_scripts_running(g))
        return true;
    for (unsigned slot = FIRST_EFFECT_ANIMATION; slot < FIRST_EFFECT_ANIMATION + EFFECT_ANIMATIONS; slot++)
        if (palette_animations_kind(g, slot))
            return true;
    return false;
}

/* Fade the colors a step toward the level's, and show them. */
static void fade_a_step(GameState *g)
{
    uint8_t level = battle_gradient_level(g);
    uint16_t target = 0;
    for (unsigned i = 0; i < GRADIENT_COLORS; i++) {
        target = gradient_set_color(g, level, i);
        set_fading_color(g, i, step_toward(fading_color(g, i), target));
    }
    ORIGINAL_EFFECT(leave_last_target(g, target));
    uint8_t left = (uint8_t)(battle_gradient_fade_frames(g) - 1);
    set_battle_gradient_fade_frames(g, left);
    if (left == 0)
        set_battle_gradient_fading(g, false);
    repeat_fading_colors(g);
    uint8_t phase = battle_gradient_phase(g);
    for (unsigned row = 0; row < GRADIENT_ROWS; row++)
        show_gradient_row(g, row, row_start(g, gradient_row_turn(g, row, phase)));
    /* the row copy counts its colors down in a cell */
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, 0));
}

/* Where the enemy's level has moved from the one shown, start a fade from the
 * shown one's colors: toward the next level on the way, or straight to the
 * level a script set. */
static void follow_hp(GameState *g)
{
    uint8_t wanted = level_wanted(g);
    set_battle_gradient_level(g, wanted);
    uint8_t shown = battle_gradient_level_shown(g);
    if (wanted == shown)
        return;
    set_battle_gradient_fading(g, (uint8_t)(battle_gradient_fading(g) + 1));
    set_battle_gradient_fade_frames(g, FADE_FRAMES);
    for (unsigned i = 0; i < GRADIENT_COLORS; i++)
        set_fading_color(g, i, gradient_set_color(g, shown, i));
    if (!(battle_gradient_level_override(g) & LEVEL_SET)) {
        wanted = wanted >= shown ? (uint8_t)(shown + 1) : (uint8_t)(shown - 1);
        set_battle_gradient_level(g, wanted);
    }
    set_battle_gradient_level_shown(g, wanted);
}

// $D1:EA4B
/* A frame of the HP gradient: fade and show it while a fade runs, or else
 * start one where the level moved. */
static void step_hp_gradient(GameState *g)
{
    if (screen_busy(g))
        return;
    if (battle_gradient_fading(g))
        fade_a_step(g);
    else
        follow_hp(g);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The phase picks a turn from 14; the rows start within the colors' repeat. */
static bool phase_in_range(const GameState *g) { return battle_gradient_phase(g) < GRADIENT_PHASES; }

bool battle_gradient_page(Snes *s)
{
    vars_peek(true);
    bool in_range = phase_in_range(state_of(s));
    vars_peek(false);
    return battle_screen_page_a_clear(s) && in_range;
}

/* A row is one of the three (Y its first color's byte offset from color $51),
 * and its start (A) within the repeat. */
enum { ROW_BYTES = 2 * ROW_COLORS };

bool battle_gradient_row_is_one(Snes *s)
{
    return battle_screen_page_a_clear(s) && s->cpu.y % ROW_BYTES == 0 && s->cpu.y / ROW_BYTES < GRADIENT_ROWS
           && (uint8_t)s->cpu.a <= GRADIENT_COLORS;
}

void battle_step_hp_gradient(Snes *s)
{
    step_hp_gradient(state_of(s));
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_find_gradient_phase(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_gradient_phase(g, starting_phase(g));
    m_rts(s);
}

/* The turn comes in the cell $45, and the start goes back in A. */
void battle_gradient_row_rotation(Snes *s)
{
    GameState *g = state_of(s);
    m_set_a8(s, row_start(g, screen_cell(g, 0x45)));
    m_rts(s);
}

void battle_write_gradient_row(Snes *s)
{
    GameState *g = state_of(s);
    show_gradient_row(g, s->cpu.y / ROW_BYTES, (uint8_t)s->cpu.a);
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, 0));
    s->cpu.a = 0;
    m_rts(s);
}

/* Battle: the screen scripts' palette commands, and pausing palette
 * animation.
 *
 * The battle keeps its palette twice, as loaded (palette_base) and as shown
 * (palette_working), and the palette animation slots (core/palette.c) run on
 * top of both: a slot fades, rotates or steps through frames a run of colors.
 * The commands here recolor whole palette rows in both copies at once --
 * exchanging two sets of sprite palettes, turning rows white, moving a band
 * of background rows and the animations that run on them -- and pausing
 * palette animation freezes the animations that step through frames while
 * the battle asks for it. A palette row is 16 colors: rows 0-7 are the
 * background's, 8-15 the sprites' palettes 0-7. */
#include "battle/screen.h"

#include "battle/bridge.h"

/* ---- The palette, as rows ------------------------------------------------------------------ */

enum { ROW_COLORS = SCREEN_ROW_COLORS, SPRITE_ROWS = SCREEN_SPRITE_ROWS, WHITE = 0x7FFF, BLACK = 0 };

static unsigned row_color(unsigned row, unsigned color) { return row * ROW_COLORS + color; }

/* Set a color as loaded and as shown. */
static void set_color(GameState *g, unsigned color, uint16_t v)
{
    set_palette_base(g, color, v);
    set_palette_working(g, color, v);
}

static void paint_rows(GameState *g, unsigned first_row, unsigned rows, uint16_t v)
{
    for (unsigned c = row_color(first_row, 0); c < row_color(first_row + rows, 0); c++)
        set_color(g, c, v);
}

/* ---- The palette animation slots ------------------------------------------------------------ */

/* The kinds of animation a slot runs (core/palette.c): the high nibble. */
enum {
    ANIMATION_NONE = 0,
    ANIMATION_FRAMES = 0x10,
    ANIMATION_ROTATE_UP = 0x30,
    /* Kinds 8-15 step through frames from a table. */
    ANIMATION_FRAMES_FROM_TABLE = 0x80,
    ANIMATION_TYPE = 0xF0,
};

static void copy_animation(GameState *g, unsigned from, unsigned to)
{
    set_palette_animations_kind(g, to, palette_animations_kind(g, from));
    set_palette_animations_first_color(g, to, palette_animations_first_color(g, from));
    set_palette_animations_color_count(g, to, palette_animations_color_count(g, from));
    set_palette_animations_step(g, to, palette_animations_step(g, from));
    set_palette_animations_delay(g, to, palette_animations_delay(g, from));
    set_palette_animations_period(g, to, palette_animations_period(g, from));
    set_palette_animations_fade_mode(g, to, palette_animations_fade_mode(g, from));
    set_palette_animations_fade_level(g, to, palette_animations_fade_level(g, from));
    set_palette_animations_fade_target(g, to, palette_animations_fade_target(g, from));
    set_palette_animations_frame_table(g, to, palette_animations_frame_table(g, from));
}

/* Move an animation to another slot, and the colors it runs on by `rows`
 * rows (a byte, which wraps). */
static void move_animation(GameState *g, unsigned from, unsigned to, int rows)
{
    copy_animation(g, from, to);
    set_palette_animations_first_color(g, to, (uint8_t)(palette_animations_first_color(g, to) + rows * ROW_COLORS));
}

/* The battle's first eight slots, whose kinds a pause keeps aside. */
enum { BATTLE_ANIMATIONS = 8 };

static uint8_t kind_kept(const GameState *g, unsigned slot) { return battle_saved_palette_types(g, slot); }
static void keep_kind(GameState *g, unsigned slot, uint8_t kind) { set_battle_saved_palette_types(g, slot, kind); }

/* Palette animation is paused while this is set; a command that sets the
 * kinds kept aside marks it too, so that the pause puts them in place as soon
 * as it finds no pause asked for (below). */
static bool animation_paused(const GameState *g) { return battle_palette_paused(g) != 0; }
static void mark_paused(GameState *g) { set_battle_palette_paused(g, (uint8_t)(battle_palette_paused(g) + 1)); }

/* ---- The row commands --------------------------------------------------------------------- */

/* The sprite palettes the swap exchanges: 1-3 and 5-7. */
enum { SWAPPED_LOW = SPRITE_ROWS + 1, SWAPPED_HIGH = SPRITE_ROWS + 5, SWAPPED_ROWS = 3 };

/* The background flag under which slot 7 follows the swapped palettes. */
enum { BACKGROUND_SLOT_FOLLOWS_SWAP = 0x02, SLOT_FOLLOWING_SWAP = 7 };

static void swap_color(GameState *g, unsigned a, unsigned b)
{
    uint16_t shown = palette_working(g, a);
    set_palette_working(g, a, palette_working(g, b));
    set_palette_working(g, b, shown);
    uint16_t loaded = palette_base(g, a);
    set_palette_base(g, a, palette_base(g, b));
    set_palette_base(g, b, loaded);
}

// $D1:E70A
/* Screen command $E1 (no operand): exchange sprite palettes 1-3 with 5-7, as
 * loaded and as shown, putting the combatants' colors back after their
 * sprites were pointed at the other half of the sprite palettes ($CD:1B78);
 * where the background says so, palette animation slot 7 moves with the
 * colors it runs on. */
static void swap_sprite_palettes(GameState *g)
{
    set_battle_sprite_palettes_flipped(g, false);
    if (battle_background_flags(g) & BACKGROUND_SLOT_FOLLOWS_SWAP) {
        uint8_t first = palette_animations_first_color(g, SLOT_FOLLOWING_SWAP);
        set_palette_animations_first_color(g, SLOT_FOLLOWING_SWAP,
                                           (uint8_t)(first ^ (SWAPPED_HIGH - SWAPPED_LOW) * ROW_COLORS));
    }
    for (unsigned c = 0; c < SWAPPED_ROWS * ROW_COLORS; c++)
        swap_color(g, row_color(SWAPPED_LOW, c), row_color(SWAPPED_HIGH, c));
    screen_command_took(g, 0);
}

// $D1:E899
/* Screen command $80, operation 26: turn background rows 2-7 and sprite
 * palettes 1-3 white. */
static void whiten_scene(GameState *g)
{
    paint_rows(g, 2, 6, WHITE);
    paint_rows(g, SPRITE_ROWS + 1, 3, WHITE);
}

// $D1:E8C1
/* Command $80, operation 28: turn sprite palettes 5-7 white. */
static void whiten_high_sprite_palettes(GameState *g) { paint_rows(g, SPRITE_ROWS + 5, 3, WHITE); }

/* The band of background rows the next two commands move: rows 2-4, moved up
 * to rows 5-7 and back. Its animations run in slots 3-5 while it is in place
 * and in slots 0-2 while it is moved up. */
enum { LOW_BAND = 2, HIGH_BAND = 5, BAND_ROWS = 3, MOVED_UP_SLOTS = 0, IN_PLACE_SLOTS = 3 };

// $D1:E91A
/* Command $80, operation 24: move the band of background rows 2-4 up to rows
 * 5-7, leaving black behind, and its animations with it, from slots 3-5 to
 * slots 0-2, rotating there; slots 3-5 stop once the pause puts the kinds
 * kept aside in place. */
static void move_band_up(GameState *g)
{
    for (unsigned c = 0; c < BAND_ROWS * ROW_COLORS; c++) {
        set_color(g, row_color(HIGH_BAND, c), palette_base(g, row_color(LOW_BAND, c)));
        set_color(g, row_color(LOW_BAND, c), BLACK);
    }
    for (unsigned n = 0; n < BAND_ROWS; n++) {
        move_animation(g, IN_PLACE_SLOTS + n, MOVED_UP_SLOTS + n, HIGH_BAND - LOW_BAND);
        set_palette_animations_kind(g, MOVED_UP_SLOTS + n, ANIMATION_ROTATE_UP);
        keep_kind(g, MOVED_UP_SLOTS + n, ANIMATION_ROTATE_UP);
        keep_kind(g, IN_PLACE_SLOTS + n, ANIMATION_NONE);
    }
    set_battle_palette_rows_moved(g, false);
    mark_paused(g);
}

// $D1:E984
/* Command $80, operation 23, the way back: background rows 2-4 get the colors
 * they start the battle with, and the band's animations move back from slots
 * 0-2 to slots 3-5, rotating from their next frame. The frame work does less while the
 * band is back. */
static void move_band_down(GameState *g)
{
    set_battle_palette_rows_moved(g, (uint8_t)(battle_palette_rows_moved(g) + 1));
    set_battle_frame_work_reduced(g, (uint8_t)(battle_frame_work_reduced(g) + 1));
    for (unsigned c = 0; c < BAND_ROWS * ROW_COLORS; c++)
        set_color(g, row_color(LOW_BAND, c), background_row_start_color(g, c));
    for (unsigned n = 0; n < BAND_ROWS; n++) {
        unsigned slot = IN_PLACE_SLOTS + n;
        move_animation(g, MOVED_UP_SLOTS + n, slot, LOW_BAND - HIGH_BAND);
        set_palette_animations_delay(g, slot, 0);
        set_palette_animations_period(g, slot, 1);
        set_palette_animations_kind(g, slot, ANIMATION_ROTATE_UP);
        keep_kind(g, slot, ANIMATION_ROTATE_UP);
    }
    mark_paused(g);
}

/* ---- Pausing palette animation ------------------------------------------------------------- */

// $D1:F411
/* Keep the first eight slots' kinds aside. */
static void keep_kinds(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_ANIMATIONS; slot++)
        keep_kind(g, slot, palette_animations_kind(g, slot));
}

static bool steps_through_frames(uint8_t kind)
{
    return (kind & ANIMATION_FRAMES_FROM_TABLE) || (kind & ANIMATION_TYPE) == ANIMATION_FRAMES;
}

// $D1:F426
/* Once a frame: when a pause is asked for (a screen command sets it), keep the
 * first eight slots' kinds aside and stop those that step through frames;
 * once it is no longer asked for, put the kinds back. */
static void pause_palette_animation(GameState *g)
{
    if (battle_palette_pause_request(g)) {
        if (animation_paused(g))
            return;
        mark_paused(g);
        keep_kinds(g);
        for (unsigned slot = 0; slot < BATTLE_ANIMATIONS; slot++)
            if (steps_through_frames(palette_animations_kind(g, slot)))
                set_palette_animations_kind(g, slot, ANIMATION_NONE);
    } else if (animation_paused(g)) {
        set_battle_palette_paused(g, false);
        for (unsigned slot = 0; slot < BATTLE_ANIMATIONS; slot++)
            set_palette_animations_kind(g, slot, kind_kept(g, slot));
    }
}

/* ---- The entries ------------------------------------------------------------------------ */

bool battle_screen_page(Snes *s) { return battle_on_page(s); }
bool battle_screen_page_a_clear(Snes *s) { return battle_on_page_a_high_clear(s); }

void battle_screen_swap_palette_rows(Snes *s)
{
    swap_sprite_palettes(state_of(s));
    screen_command_done(s);
}

void battle_screen_whiten_palette(Snes *s)
{
    whiten_scene(state_of(s));
    screen_command_done(s);
}

void battle_screen_whiten_high_rows(Snes *s)
{
    whiten_high_sprite_palettes(state_of(s));
    screen_command_done(s);
}

void battle_screen_move_rows_up(Snes *s)
{
    move_band_up(state_of(s));
    screen_command_done(s);
}

void battle_screen_move_rows_down(Snes *s)
{
    move_band_down(state_of(s));
    screen_command_done(s);
}

void battle_save_palette_kinds(Snes *s)
{
    keep_kinds(state_of(s));
    m_rts(s);
}

void battle_pause_palette_animation(Snes *s)
{
    pause_palette_animation(state_of(s));
    m_rtl(s);
}

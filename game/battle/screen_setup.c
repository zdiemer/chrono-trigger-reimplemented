/* Battle: the screen as a battle starts, and the fade tables it builds.
 *
 * As a battle starts ($D1:F1B0), before its first frame: the battle's
 * vblank and line handlers go in, the line handler at line 211; everything
 * the frame work, the screen scripts and the messages keep between frames
 * starts again; the battle window's lines take their values (the layers'
 * maps, scroll and screens, the mode, BG3's rows of text) for each of its
 * layouts; the window table starts empty; the map's palette animations on
 * the sprites' colors stop; the line effects take the battlefield's
 * arrangement; the fade tables are built; and the battle's music starts.
 *
 * The screen effects darken or brighten the battlefield line by line through
 * color math: a per-line table gives, for each band of lines, the value the
 * color-math register takes, all color channels at an intensity of 0-31. The
 * battle builds four such tables at the start, two pairs: in each pair one
 * falls from intensity 8 to nothing down the screen and the other rises from
 * nothing, in steps a third of an intensity apart over 24 lines, or two
 * thirds apart over 12. The screen scripts point the line effect at them.
 *
 * A table entry is four bytes: how many lines it lasts (bit 7 set to write
 * its value on every one of them) and the value, then two bytes left 0. A
 * line count of 0 ends a table. */
#include "battle/screen.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/message.h"
#include "battle/program.h"
#include "battle/screen_lines.h"
#include "core/program.h"
#include "field/sounds.h"
#include "charge.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/native.h"

/* A color-math value: the channels it applies to in the top three bits and the
 * intensity in the low five. The tables step the intensity in 1/256ths of a
 * level, and each entry takes the whole part. */
enum { ALL_CHANNELS = VID_LINE_FIXED_COLOR_ALL_CHANNELS, INTENSITY_FRACTION_BITS = 8,
       INTENSITY_ONE = 1 << INTENSITY_FRACTION_BITS };

static uint8_t color_math_value(uint16_t intensity) { return (uint8_t)(ALL_CHANNELS | intensity >> INTENSITY_FRACTION_BITS); }

/* The four tables in battle_fade_line_data, by the byte their first entry
 * starts at. */
typedef enum { FALL_BY_THIRDS = 0x01, RISE_BY_THIRDS = 0x61, FALL_BY_TWO_THIRDS = 0xC1, RISE_BY_TWO_THIRDS = 0xF1 } FadeTable;

enum { ENTRY_BYTES = 4, ONE_LINE = 1, EVERY_LINE = 0x80, END_OF_TABLE = 0 };

static void set_entry(GameState *g, FadeTable table, unsigned entry, uint8_t lines, uint8_t value)
{
    unsigned at = table + entry * ENTRY_BYTES;
    set_battle_fade_line_data(g, at, lines);
    set_battle_fade_line_data(g, at + 1, value);
}

/* A falling table and a rising one, one line an entry, a step apart each
 * line: the fall starts a step below intensity 9 and the rise a step above
 * nothing. */
typedef struct {
    FadeTable falls, rises;
    unsigned entries;
    uint16_t step;
} FadePair;

enum { FALL_FROM = 9 * INTENSITY_ONE };

static const FadePair pairs[] = {
    { FALL_BY_THIRDS, RISE_BY_THIRDS, 24, INTENSITY_ONE / 3 },
    { FALL_BY_TWO_THIRDS, RISE_BY_TWO_THIRDS, 12, 2 * INTENSITY_ONE / 3 },
};

/* The rising tables' last entry ends them, holding every channel at nothing.
 * The falling ones have no end of their own: each runs on into the rising
 * table after it, which is how the line effect shows a fall and a rise. */
static void end_rising_table(GameState *g, FadeTable table, unsigned entries)
{
    set_entry(g, table, entries - 1, END_OF_TABLE, ALL_CHANNELS);
}

/* The original counts the two intensities in its scratch cells, color-math
 * value and fraction together, and leaves the last pair's there, where a
 * screen effect's settings that the script command doesn't set are read from
 * (battle/screen_motion.c): game state, kept plainly. */
static void leave_intensities(GameState *g, const FadePair *p)
{
    uint16_t fell = (uint16_t)(ALL_CHANNELS << 8 | (uint16_t)(FALL_FROM - (p->entries + 1) * p->step));
    uint16_t rose = (uint16_t)(ALL_CHANNELS << 8 | (uint16_t)((p->entries + 1) * p->step));
    screen_leave_word(g, 0x53, fell);
    screen_leave_word(g, 0x55, rose);
}

// $D1:F83D
/* Build the four fade tables, from nothing. */
static void build_fade_tables(GameState *g)
{
    for (unsigned i = 0; i < BATTLE_FADE_LINE_DATA_COUNT; i++)
        set_battle_fade_line_data(g, i, 0);
    for (unsigned n = 0; n < sizeof pairs / sizeof pairs[0]; n++) {
        const FadePair *p = &pairs[n];
        uint16_t fall = (uint16_t)(FALL_FROM - p->step), rise = p->step;
        for (unsigned e = 0; e < p->entries; e++, fall -= p->step, rise += p->step) {
            set_entry(g, p->falls, e, ONE_LINE, color_math_value(fall));
            set_entry(g, p->rises, e, EVERY_LINE | ONE_LINE, color_math_value(rise));
        }
        end_rising_table(g, p->rises, p->entries);
    }
    leave_intensities(g, &pairs[1]);
}

/* ---- The screen as a battle starts ---------------------------------------------------------- */

/* The battle's own vblank and line handlers (battle/nmi.c), and the line the
 * line handler runs at: the battlefield's last, where the menus begin. */
enum { BATTLE_LINE = 211 };

/* What the frame work, the screen scripts and the messages keep between
 * frames, as a battle starts: no transfers asked for, no shake, no pause, no
 * message, no effect's graphics, the HP gradient at its first level, the
 * screen scripts stopped, every per-line effect on and the screen scripts
 * free to turn any layer on (on a battlefield with the HP gradient, all but
 * BG2). */
enum { NO_MESSAGE = 0xFF, GRADIENT_FIRST_LEVEL = 8, GRADIENT_EVERY_OTHER_FRAME = 7, BATTLEFIELD_ARRANGEMENT = 8,
       ALL_LAYERS = 0xFF, ALL_BUT_BG2 = 0xFD, PAUSE_BLOCKED = 0xFF };

static void start_the_frame_work(GameState *g)
{
    uint8_t layout = battle_alternate_frame(g) & 1;
    set_battle_start_layout(g, layout);
    set_battle_alternate_request(g, layout);
    set_battle_alternate_latched(g, layout);
    set_battle_alternate_toggle_held(g, 0);
    set_battle_alternate_base(g, BATTLEFIELD_ARRANGEMENT);
    set_battle_alternate_apply(g, 1);
    set_battle_block_upload_row(g, 0);
    set_battle_block_upload_request(g, 0);
    set_battle_vram_request_pending(g, 0);
    set_battle_vram_request_busy(g, 0);
    set_battle_vram_copy_mode(g, 0);
    set_battle_interrupt_busy(g, 0);
    set_battle_nmi_pending(g, 0);
    set_battle_mode(g, 0);
    set_battle_frame_count(g, 0);
    set_battle_graphics_load_in_vblank(g, 0);
    set_battle_scroll_mode(g, BATTLE_SCROLL_STEADY);
    set_battle_line_effects_on(g, 0xFF);
    set_battle_window_slide(g, 0);
    for (unsigned i = 0; i < BATTLE_WINDOW_REGISTERS_COUNT; i++)
        set_battle_window_registers(g, i, 0);
    for (unsigned i = 0; i < BATTLE_BG_OFFSET_COUNT; i++)
        set_battle_bg_offset(g, i, 0);
    for (unsigned i = 0; i < BATTLE_SHAKE_OFFSETS_COUNT; i++)
        set_battle_shake_offsets(g, i, 0);
    for (unsigned axis = 0; axis < BATTLE_SHAKE_COUNT; axis++)
        set_battle_shake_on(g, axis, 0);
    set_battle_shake_layers(g, 0);
    set_battle_paused(g, 0);
    set_battle_pause_held(g, 0);
    set_battle_pause_blocked(g, PAUSE_BLOCKED);
    set_battle_frame_advance_count(g, 0);
    set_battle_frame_advance_count_high(g, 0);
    set_battle_setup_flags_copy(g, battle_setup_flags(g));
    set_battle_enemy_rule(g, 0);
    set_battle_background_cycle(g, 0);
}

static void start_the_effects(GameState *g)
{
    bool gradient = battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT;
    set_battle_screen_scripts_running(g, 0);
    set_battle_screen_channels_running(g, 0, 0);
    set_battle_screen_layer_mask(g, gradient ? ALL_BUT_BG2 : ALL_LAYERS);
    set_battle_screen_palette_stop_count(g, 0);
    set_battle_script_sound_request(g, 0);
    set_battle_sprites_frozen_setting(g, 0);
    set_battle_keep_sprites(g, 0);
    set_battle_effect_line_wait_off(g, 0);
    set_battle_palette_preset_request(g, 0);
    set_battle_palette_pause_request(g, 0);
    set_battle_palette_paused(g, 0);
    set_battle_background_restore_request(g, 0);
    set_battle_restore_keeps_screen(g, 0);
    set_battle_battlefield_tiles_replaced(g, 0);
    set_battle_effect_graphics_wanted(g, 0);
    set_battle_effect_graphics_effect(g, 0);
    set_battle_effect_graphics_loading(g, 0);
    set_battle_ripple_frames(g, 0);
    set_battle_palette_rows_moved(g, 0);
    set_battle_gradient_fading(g, 0);
    set_battle_gradient_level(g, 0);
    set_battle_gradient_level_shown(g, GRADIENT_FIRST_LEVEL);
    set_battle_gradient_fade_frames(g, 0);
    set_battle_gradient_level_override(g, 0);
    set_battle_gradient_reversed(g, 0);
    set_battle_gradient_frame_mask(g, GRADIENT_EVERY_OTHER_FRAME);
    set_battle_frame_work_reduced(g, 0);
    set_battle_start_cleared(g, 0);
    set_battle_start_cleared_byte(g, 0);
    for (unsigned i = 0; i < BATTLE_EFFECT_LINE_WORK_COUNT; i++)
        set_battle_effect_line_work(g, i, 0);
}

static void start_the_messages(GameState *g)
{
    set_battle_message_frames(g, 0);
    set_battle_message_timed(g, 0);
    set_battle_message_drawing(g, 0);
    set_battle_message_by_id(g, 0);
    set_battle_message_by_id_high(g, 0);
    set_battle_message_asked(g, NO_MESSAGE);
    set_battle_message_shown(g, NO_MESSAGE);
}

/* The battle window's lines, in each of the window's two layouts: the
 * window's maps, BG1 and BG3, mode 1 with BG3 in front, and the layers'
 * scroll. BG3 holds the window's text, whose rows its scroll spaces: the
 * window's first 19 lines show BG3 four lines up, and every 12 lines after
 * that it moves four lines further on. */
enum { WINDOW_MAPS = 0x78787878, WINDOW_SCREENS = 0x05050505, WINDOW_MODE = 0x0009 };
enum { WINDOW_LINES = 48, LAYOUTS = 2, FIRST_TEXT_ROW_LINES = 19, TEXT_ROW_LINES = 12, TEXT_ROW_STEP = 4 };
enum { BG1_WINDOW_DOWN = 0x0077, BG1_OTHER_LAYOUT_DOWN = 0xFFDF, BG2_WINDOW_DOWN = 0x0077,
       BG3_FIRST_ROW_DOWN = 0xFFFC, BG3_OTHER_LAYOUT_UP = 0x98 };

static void lay_the_window_lines_out(GameState *g)
{
    static const uint16_t short_window_down[BATTLE_SHORT_WINDOW_SCROLL_COUNT] = { 0xFF27, 0x00D7, 0x00D7, 0x000D, 0x00BD };
    set_battle_window_line_maps(g, WINDOW_MAPS);
    set_battle_window_line_screens(g, WINDOW_SCREENS);
    set_battle_window_line_mode(g, WINDOW_MODE);
    set_battle_window_line_bg1_scroll_x(g, 0, 0);
    set_battle_window_line_bg1_scroll_y(g, 0, BG1_WINDOW_DOWN);
    set_battle_window_line_bg1_scroll_x(g, 1, 0);
    set_battle_window_line_bg1_scroll_y(g, 1, BG1_OTHER_LAYOUT_DOWN);
    set_battle_window_line_bg2_scroll_x(g, 0);
    set_battle_window_line_bg2_scroll_y(g, BG2_WINDOW_DOWN);
    for (unsigned i = 0; i < BATTLE_SHORT_WINDOW_SCROLL_COUNT; i++) {
        set_battle_short_window_scroll_x(g, i, 0);
        set_battle_short_window_scroll_y(g, i, short_window_down[i]);
    }
    uint16_t down = BG3_FIRST_ROW_DOWN;
    for (unsigned line = 0; line < WINDOW_LINES; line++) {
        if (line >= FIRST_TEXT_ROW_LINES && (line - FIRST_TEXT_ROW_LINES) % TEXT_ROW_LINES == 0)
            down = (uint16_t)(down + TEXT_ROW_STEP);
        set_battle_window_line_bg3_scroll_x(g, line, 0);
        set_battle_window_line_bg3_scroll_y(g, line, down);
        set_battle_window_line_bg3_scroll_x(g, WINDOW_LINES + line, 0);
        set_battle_window_line_bg3_scroll_y(g, WINDOW_LINES + line, (uint16_t)(down - BG3_OTHER_LAYOUT_UP));
    }
}

/* Both windows start empty on every line of both buffers, and full on the
 * top border's. */
static void empty_the_windows(GameState *g)
{
    for (int line = -TOP_BORDER_LINES; line < 0; line++)
        for (Window w = WINDOW_1; w <= WINDOW_2; w++)
            set_line_edges(g, line, w, EDGES_FULL);
    for (TableLine line = 0; line < TABLE_LINES; line++)
        for (Window w = WINDOW_1; w <= WINDOW_2; w++)
            set_line_edges(g, line, w, EDGES_EMPTY);
}

/* Unless the battlefield has its palette animation follow the sprites'
 * palettes, the map's animations in the first eight slots that run on the
 * sprites' colors (from 128 on) stop. */
enum { MAP_ANIMATIONS = 8, FIRST_SPRITE_COLOR = 0x80, BACKGROUND_FOLLOWS_SPRITES = 0x02 };

static void stop_the_sprite_color_animations(GameState *g)
{
    if (battle_background_flags(g) & BACKGROUND_FOLLOWS_SPRITES)
        return;
    for (unsigned slot = 0; slot < MAP_ANIMATIONS; slot++)
        if (palette_animations_first_color(g, slot) >= FIRST_SPRITE_COLOR)
            set_palette_animations_kind(g, slot, 0);
}

/* Background row 1's last eight colors are kept, for putting the battlefield
 * back. */
enum { KEPT_FROM_COLOR = 24 };

static void keep_the_row_colors(GameState *g)
{
    for (unsigned i = 0; i < BATTLE_KEPT_ROW_COLORS_COUNT; i++)
        set_battle_kept_row_colors(g, i, palette_base(g, KEPT_FROM_COLOR + i));
}

/* The routines the setup calls: the map's layers that scroll by themselves
 * (the field's, battle/frame.c), the message's tiles cleared
 * (battle/message.c), and the battle's song (field/sounds.c). The program
 * makes them as C, and the machine's entry from the original's calls. */
typedef struct {
    bool (*scroll_the_map_layers)(void *ctx);
    bool (*clear_the_message_tiles)(void *ctx);
    bool (*start_the_song)(void *ctx);
} ScreenSetUpCalls;

// $D1:F1B0
/* Set the screen up as a battle starts. False where a call went no further. */
static bool set_the_screen_up(GameState *g, Platform *pf, const ScreenSetUpCalls *calls, void *ctx)
{
    program_install_vblank_handler(g, BATTLE_VBLANK_HANDLER);
    program_install_line_handler(g, BATTLE_LINE_HANDLER);
    if (!calls->scroll_the_map_layers(ctx))
        return false;
    TIMED_AS(g, 0xCD0C9D, battle_take_the_start_settings(g));
    start_the_frame_work(g);
    start_the_effects(g);
    start_the_messages(g);
    tm_set_line(pf, PF_AT(0xD1F29A), BATTLE_LINE);
    TIMED_AS(g, 0xD1EA01, battle_start_gradient_phase(g));
    stop_the_sprite_color_animations(g);
    lay_the_window_lines_out(g);
    TIMED_AS(g, 0xD1F411, battle_keep_palette_kinds(g));
    empty_the_windows(g);
    keep_the_row_colors(g);
    TIMED_AS(g, 0xCEEE6E, battle_arrange_line_effects(g, BATTLEFIELD_ARRANGEMENT));
    TIMED_AS(g, 0xD1F83D, build_fade_tables(g));
    if (!calls->clear_the_message_tiles(ctx) || !calls->start_the_song(ctx))
        return false;
    set_battle_graphics_load_in_vblank(g, 1);
    return true;
}

/* ---- As C ---------------------------------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} SetUpAsC;

/* None of the three has a guard of the game's state: the message's clear
 * and the scroll's turn away only a page or a bank the program has none of. */
static bool scroll_the_map_layers(void *ctx)
{
    GameState *g = ((SetUpAsC *)ctx)->game;
    TIMED_AS(g, BATTLE_SCROLL_MAP_LAYERS_ENTRY, battle_scroll_the_map_layers(g));
    return true;
}

static bool clear_the_message_tiles(void *ctx)
{
    GameState *g = ((SetUpAsC *)ctx)->game;
    TIMED_AS(g, BATTLE_CLEAR_THE_MESSAGE_TILES_ENTRY, battle_clear_the_message_tiles(g));
    return true;
}

static bool start_the_song(void *ctx)
{
    SetUpAsC *c = ctx;
    TIMED_AS(c->game, FIELD_BATTLE_SONG_ENTRY, field_start_the_battle_song(c->game, c->pf));
    return true;
}

bool battle_screen_set_up(GameState *g, Platform *pf)
{
    static const ScreenSetUpCalls as_c = { scroll_the_map_layers, clear_the_message_tiles, start_the_song };
    SetUpAsC c = { g, pf };
    return set_the_screen_up(g, pf, &as_c, &c);
}

/* ---- On the machine ------------------------------------------------------------------------ */

static bool scroll_on_the_machine(void *ctx)
{
    return battle_run_long(ctx, 0xD1F1D7, BATTLE_SCROLL_MAP_LAYERS_ENTRY, battle_scroll_map_layers);
}

/* The start's settings leave the carry set, as their count of the scroll
 * tables' bands ends; the song's call below keeps it on the stack. */
static bool clear_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_set_flag(s, CPU_FLAG_C, true);
    return battle_run_long(s, 0xD1F3FE, BATTLE_CLEAR_THE_MESSAGE_TILES_ENTRY, battle_clear_the_message_tiles_long);
}

/* The battle's song keeps the data bank, the page and the flags on the stack
 * around its call, where the original keeps them. */
static bool song_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.p);
    if (!battle_run_long(s, 0xD1F405, FIELD_BATTLE_SONG_ENTRY, field_start_the_battle_song_entry))
        return false;
    s->cpu.p = m_pull8(s);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    return true;
}

/* ---- The entry ------------------------------------------------------------------------ */

void battle_build_fade_tables(Snes *s)
{
    build_fade_tables(state_of(s));
    m_rtl(s);
}

void battle_set_the_screen_up(Snes *s)
{
    static const ScreenSetUpCalls on_the_machine = { scroll_on_the_machine, clear_on_the_machine, song_on_the_machine };
    s->cpu.db = BATTLE_DATA_BANK;
    s->cpu.d = BATTLE_PAGE;
    if (!set_the_screen_up(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    s->cpu.a = 0;
    m_rtl(s);
}

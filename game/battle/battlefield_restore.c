/* Battle: the battlefield as the battle started it -- taken as the battle's
 * screen is set up, and put back once an action's effects are over.
 *
 * A battle opens on the field's own picture: the field hands over its layers'
 * maps, scroll and screen settings (field/battle_start.c), and the battle
 * takes them for the lines below its window's split, the HP gradient's
 * battlefields with BG2's map a single screen wide. The tint starts black and
 * the picture at full brightness.
 *
 * An action's effects can draw over that picture: an effect's graphics go
 * over the battlefield's tiles in VRAM, and a screen effect can fade the
 * battlefield's own layer and change the screen's settings. Once the screen
 * scripts have ended, the frame wait puts the battlefield back: its tiles are
 * sent again from the copy the battle keeps, and unless the fight asked for
 * the screen to stay as the effects left it, the battlefield's own layer
 * fades back in, the screen's settings go back to the start's, and the
 * picture is given a quarter of a second to settle. Then the battlefield is
 * forgotten (the next effect loads its own), and background row 1's last
 * colors go back to the start's.
 *
 * The work is C on the game's state; what reaches the machine is the bridge
 * around the transfer and the waits, which run the frame work's waits as the
 * original calls them (battle/wait.c). */
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/screen.h"
#include "battle/wait.h"
#include "charge.h"
#include "platform/video.h"
#include "ram.h"
#include "vars.h"

/* ---- The screen the battle started with ---------------------------------------------------- */

/* The layers, as the per-line effects number them. */
enum { BG1_LAYER, BG2_LAYER, BG3_LAYER, BG4_LAYER, SCROLLED_LAYERS = 3 };
/* A layer's map setting's size bits: bit 0 a map two screens wide. */
enum { MAP_TWO_SCREENS_WIDE = 0x01 };
/* The screen's settings below the split, as battle_screen_settings orders
 * them. */
enum { MAIN_SCREEN, SUB_SCREEN, MAIN_WINDOWED, SUB_WINDOWED };
enum { MATH_CONTROL, MATH_LAYERS };
/* Each layer's scroll is a table of an entry a band of lines. */
enum { SCROLL_ENTRIES = 32 };
/* The tint as the per-line effects set it (all channels at no intensity),
 * the fifth of the split's values, and full brightness. */
enum { NO_TINT = 0xE0, TINT_VALUE = 5, FULL_BRIGHTNESS = 0x0F };

/* The screen below the split, as the battle keeps it: BG3's map, the layers
 * on each screen and those the windows mask, the color math, the mode and
 * mosaic, the tint and the brightness. The tint goes into the split's values
 * as a word, whose high byte is the byte the start's settings keep beside it,
 * which nothing reads. */
typedef struct {
    uint8_t bg3_map;
    uint8_t main, sub, main_windowed, sub_windowed;
    uint8_t math_control, math_layers;
    uint8_t mode, mosaic;
    uint8_t tint, beside_the_tint;
    uint8_t brightness;
} BelowTheSplit;

static void show_below_the_split(GameState *g, BelowTheSplit screen)
{
    set_battle_layer_map_settings(g, BG3_LAYER, screen.bg3_map);
    set_battle_screen_settings(g, MAIN_SCREEN, screen.main);
    set_battle_screen_settings(g, SUB_SCREEN, screen.sub);
    set_battle_screen_settings(g, MAIN_WINDOWED, screen.main_windowed);
    set_battle_screen_settings(g, SUB_WINDOWED, screen.sub_windowed);
    set_battle_color_math_settings(g, MATH_CONTROL, screen.math_control);
    set_battle_color_math_settings(g, MATH_LAYERS, screen.math_layers);
    set_battle_screen_mode(g, screen.mode);
    set_battle_mosaic(g, screen.mosaic);
    set_battle_split_line_values(g, TINT_VALUE, (uint16_t)(screen.beside_the_tint << 8 | screen.tint));
    set_battle_fixed_color(g, screen.tint);
    set_battle_brightness(g, screen.brightness);
}

/* The screen the battle started with: the field's settings, untinted, at
 * full brightness. */
static BelowTheSplit the_start_screen(const GameState *g)
{
    return (BelowTheSplit){
        .bg3_map = battle_start_tile_maps(g, BG3_LAYER),
        .main = battle_start_screens_main(g),
        .sub = battle_start_screens_sub(g),
        .main_windowed = battle_start_screens_main_window_mask(g),
        .sub_windowed = battle_start_screens_sub_window_mask(g),
        .math_control = battle_start_screens_math_control(g),
        .math_layers = battle_start_screens_math_layers(g),
        .mode = battle_start_screens_mode(g),
        .mosaic = battle_start_screens_mosaic(g),
        .tint = NO_TINT,
        .beside_the_tint = battle_start_screens_unused(g),
        .brightness = FULL_BRIGHTNESS,
    };
}

// $CD:0CF6
/* The screen's settings below the split go back to the start's. */
static void take_back_the_start_screen(GameState *g) { show_below_the_split(g, the_start_screen(g)); }

// $CD:0CA1
/* As the battle's screen is set up: every layer's map as the field left it
 * (an HP gradient's BG2 a single screen wide), each scrolled layer's every
 * band of lines at the field's scroll, and the start's screen settings. */
void battle_take_the_start_settings(GameState *g)
{
    static const uint8_t own_maps[] = { BG1_LAYER, BG2_LAYER, BG4_LAYER };
    for (unsigned i = 0; i < sizeof own_maps; i++)
        set_battle_layer_map_settings(g, own_maps[i], battle_start_tile_maps(g, own_maps[i]));
    if (battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT)
        set_battle_layer_map_settings(g, BG2_LAYER, battle_layer_map_settings(g, BG2_LAYER) & (uint8_t)~MAP_TWO_SCREENS_WIDE);
    for (unsigned layer = 0; layer < SCROLLED_LAYERS; layer++)
        for (unsigned band = 0; band < SCROLL_ENTRIES; band++) {
            set_battle_scroll_tables_x(g, layer * SCROLL_ENTRIES + band, battle_bg_scroll_x(g, layer));
            set_battle_scroll_tables_y(g, layer * SCROLL_ENTRIES + band, battle_bg_scroll_y(g, layer));
        }
    take_back_the_start_screen(g);
}

/* ---- Putting the battlefield back ------------------------------------------------------------- */

/* An enemy's rule with bit 7 set keeps the screen as the effects leave it, as
 * the fight's own setting does. */
enum { RULE_KEEPS_SCREEN = 0x80 };

static bool screen_stays_as_left(const GameState *g)
{
    return (battle_enemy_rule(g) & RULE_KEEPS_SCREEN) || battle_restore_keeps_screen(g);
}

/* A battlefield with a layer of its own fades it back in where the battle
 * shows it: an HP gradient's, or BG3 on either screen at the start. */
enum { BG3 = 0x04 };

static bool shows_its_own_layer(const GameState *g)
{
    return (battle_background_flags(g) & BATTLE_BACKGROUND_HAS_GRADIENT)
           || ((battle_start_screens_main(g) | battle_start_screens_sub(g)) & BG3);
}

/* The battlefield's BG3 tiles, as the field unpacked them into the second
 * bank of work RAM (field_bg3_tiles_unpacked, field/location.c), which stay
 * there through the battle, and where they go in VRAM. */
enum { TILES_VRAM = 0x5000, TILES_BYTES = 0x0F80 };
/* A quarter of a second for the picture to settle. */
enum { SETTLING_FRAMES = 15 };

static bool send_the_kept_tiles(Snes *s, uint32_t site)
{
    return battle_send_and_wait(s, site, RAM_FIELD_BG3_TILES_UNPACKED, TILES_VRAM, TILES_BYTES);
}

/* The waits pass the carry on to the menu's turn, which reads it, so the port
 * leaves it there as the original's tests do: the battlefield's test sets it
 * for one with a layer of its own, and the fade back in leaves it set as its
 * copy of the preset ends, clear where halved colors left nothing to copy; the
 * colors' copy leaves it set. The tests and the fade also clear A. */
enum { HALVED_MATH = 0x40 };

static void leave_the_tests(Snes *s, const GameState *g, bool own_layer, bool faded)
{
    bool halved = battle_start_screens_math_layers(g) & HALVED_MATH;
    if (!own_layer || faded)
        s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, own_layer && !(faded && halved));
}

// $CD:0453
/* Put the battlefield back. */
static bool put_the_battlefield_back(Snes *s)
{
    GameState *g = state_of(s);
    if (!send_the_kept_tiles(s, 0xCD0460))
        return false;
    set_battle_battlefield_tiles_replaced(g, 0);
    set_battle_scroll_mode(g, BATTLE_SCROLL_STEADY);
    if (!screen_stays_as_left(g)) {
        bool own_layer = !battle_bg3_stays_still(g);
        bool fades = own_layer && shows_its_own_layer(g);
        if (fades)
            TIMED_AS(g, 0xCD0E61, battle_fade_battlefield_layer(g, true));
        leave_the_tests(s, g, own_layer, fades);
        if (!battle_wait_for_the_next_frame_from(s, 0xCD048C))
            return false;
        TIMED_AS(g, 0xCD0CF6, take_back_the_start_screen(g));
        if (!battle_wait_frames(s, 0xCD0494, BATTLE_SOME_FRAMES_WAIT, BATTLE_COUNTING_WAIT_READS, SETTLING_FRAMES))
            return false;
    }
    s->cpu.a = 0;
    set_battle_background(g, 0);
    set_battle_background_restore_request(g, 0);
    TIMED_AS(g, 0xCD0E23, battle_restore_kept_colors(g));
    m_set_flag(s, CPU_FLAG_C, true);
    return battle_wait_for_the_next_frame_from(s, 0xCD04A2);
}

/* ---- The entries ------------------------------------------------------------------------------ */

void battle_put_the_battlefield_back(Snes *s)
{
    if (put_the_battlefield_back(s))
        m_rts(s);
}

/* The start's settings leave A at full brightness, its high byte clear, and
 * the carry set as their count of the bands ends. */
void battle_take_the_start_settings_long(Snes *s)
{
    battle_take_the_start_settings(state_of(s));
    s->cpu.a = FULL_BRIGHTNESS;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rtl(s);
}

void battle_take_back_the_start_screen(Snes *s)
{
    take_back_the_start_screen(state_of(s));
    m_set_a8(s, FULL_BRIGHTNESS);
    m_rts(s);
}

/* The fade back in leaves A clear; the original's callers read none of the
 * rest. */
void battle_fade_battlefield_layer_back_in(Snes *s)
{
    battle_fade_battlefield_layer(state_of(s), true);
    s->cpu.a = 0;
    m_rts(s);
}

// $CD:0E9D
/* Whether the battlefield has a layer of its own, in the carry, with the
 * battlefield in A; one that keeps BG3 still answers with A clear. */
void battle_has_a_layer_of_its_own(Snes *s)
{
    const GameState *g = state_of(s);
    bool own = !battle_bg3_stays_still(g);
    if (own)
        m_set_a8(s, (uint8_t)battle_background(g));
    else
        s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, own);
    m_rts(s);
}

// $CD:0E23
/* Background row 1's last colors as the battle started them; A's high byte
 * left clear, and the carry set as the copy's count ends. */
void battle_restore_kept_colors_entry(Snes *s)
{
    battle_restore_kept_colors(state_of(s));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

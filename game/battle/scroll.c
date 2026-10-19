/* Battle: the layers' scroll, and the screen shake.
 *
 * The battlefield's three background layers are scrolled line by line: each
 * has a table of 32 per-line scroll entries that the vblank's per-line effects
 * read, and the frame work fills them afresh every frame (battle/frame.c).
 * What goes in them depends on the battle's scroll mode:
 *
 *   - steady: each layer at its base position, plus the screen shake's offset,
 *     and BG1 plus the battle's extra offset for it;
 *   - BG3 moving: the same, but BG3 where screen script channel 0 has moved it
 *     (battle/screen_motion.c) instead of at its base position;
 *   - and two modes the screen scripts' line waves set (battle/screen_waves.c,
 *     the Lavos fights' among them): BG3 moving, and the layers the wave names
 *     (battle_line_scroll_layers) each entry further across by the wave's
 *     offset for it (battle_line_scroll_offsets); in the finer one, BG3 has
 *     twice the entries, the wave's whole table.
 *
 * The screen shake moves the layers about by a random amount each frame. Each
 * axis, across and down, picks a new random magnitude every so many frames and
 * steps its amplitude toward where it settles every so many more; its offset
 * is the magnitude scaled by the amplitude, and it goes on the layers the
 * shake names. An axis whose amplitude dies away to nothing stops shaking.
 * The battle's actions start and end it (battle/screen_motion.c).
 *
 * The same pass starts a palette preset a screen effect asked for, by copying
 * its fades over palette animation slots 8 and 9, or 10. */
#include "battle/frame.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/screen_data.h"
#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"

/* ---- A layer's scroll -------------------------------------------------------------------- */

enum { SCROLLED_LAYERS = 3, LINES_PER_LAYER = 32, BG1 = 0, BG2 = 1, BG3 = 2, ACROSS = 0, DOWN = 1 };

typedef struct {
    uint16_t x, y;
} LayerScroll;

/* The shake's offset of a layer on an axis. */
static uint16_t shake_offset(const GameState *g, unsigned layer, unsigned axis)
{
    return battle_shake_offsets(g, layer * 2 + axis);
}

/* Where a layer is this frame, from its base position and the shake. */
static LayerScroll steady_scroll(const GameState *g, unsigned layer)
{
    return (LayerScroll){ (uint16_t)(battle_bg_scroll_x(g, layer) + shake_offset(g, layer, ACROSS)),
                          (uint16_t)(battle_bg_scroll_y(g, layer) + shake_offset(g, layer, DOWN)) };
}

/* BG1 carries the battle's extra offset too. */
static LayerScroll offset_scroll(const GameState *g, unsigned layer)
{
    LayerScroll s = steady_scroll(g, layer);
    return (LayerScroll){ (uint16_t)(s.x + battle_bg_offset(g, ACROSS)), (uint16_t)(s.y + battle_bg_offset(g, DOWN)) };
}

/* BG3 where screen script channel 0 has moved it, plus the shake. */
enum { BG3_CHANNEL = 0 };

static LayerScroll moved_bg3_scroll(const GameState *g)
{
    return (LayerScroll){
        (uint16_t)(battle_screen_layer_x(g, BG3_CHANNEL) + battle_screen_layer_offset_x(g, BG3_CHANNEL)
                   + shake_offset(g, BG3, ACROSS)),
        (uint16_t)(battle_screen_layer_y(g, BG3_CHANNEL) + battle_screen_layer_offset_y(g, BG3_CHANNEL)
                   + shake_offset(g, BG3, DOWN)),
    };
}

/* A layer's entries at one scroll, from `first` up to `end`; a layer the
 * line wave moves takes each entry further across by the wave's offset for
 * it. */
static void scroll_entries(GameState *g, unsigned layer, LayerScroll s, bool waved, unsigned first, unsigned end)
{
    for (unsigned entry = first; entry < end; entry++) {
        uint16_t x = waved ? (uint16_t)(battle_line_scroll_offsets_across(g, entry) + s.x) : s.x;
        set_battle_scroll_tables_x(g, layer * LINES_PER_LAYER + entry, x);
        set_battle_scroll_tables_y(g, layer * LINES_PER_LAYER + entry, s.y);
    }
}

/* In the finer wave mode BG3's entries run on past its 32, through the
 * wave's whole table. */
enum { FINER_BG3_ENTRIES = 2 * LINES_PER_LAYER };

static bool waved_layer(const GameState *g, BattleScrollMode mode, unsigned layer)
{
    bool waves = mode == BATTLE_SCROLL_LINE_OFFSETS || mode == BATTLE_SCROLL_LINE_OFFSETS_LONG_BG3;
    return waves && battle_line_scroll_layers(g, layer) != 0;
}

static void leave_scratch_word(GameState *g, unsigned cell, uint16_t v)
{
    set_battle_scratch(g, cell, (uint8_t)v);
    set_battle_scratch(g, cell + 1, (uint8_t)(v >> 8));
}

/* The original works the six values out in the scratch cells from $45 on,
 * BG1's across first, and leaves them there; in the wave modes, which layers
 * the wave moves in the low cells $1A, $1C and $1E. BG1's across is the
 * first word, which the screen scripts' drift tests the high byte of
 * (battle_screen_leftover, screen_motion.c): game state, written plainly
 * (battle_scroll_layers); the rest only the original's code reads. */
enum { WAVED_LAYER_CELLS = 0x1A };

static void leave_layer_scrolls(GameState *g, BattleScrollMode mode, const LayerScroll s[SCROLLED_LAYERS])
{
    for (unsigned layer = 0; layer < SCROLLED_LAYERS; layer++) {
        if (layer != BG1)
            leave_scratch_word(g, 4 * layer, s[layer].x);
        leave_scratch_word(g, 4 * layer + 2, s[layer].y);
    }
    if (mode != BATTLE_SCROLL_LINE_OFFSETS && mode != BATTLE_SCROLL_LINE_OFFSETS_LONG_BG3)
        return;
    for (unsigned layer = 0; layer < SCROLLED_LAYERS; layer++) {
        uint16_t waved = battle_line_scroll_layers(g, layer);
        set_battle_low_scratch(g, WAVED_LAYER_CELLS + 2 * layer, (uint8_t)waved);
        set_battle_low_scratch(g, WAVED_LAYER_CELLS + 2 * layer + 1, (uint8_t)(waved >> 8));
    }
}

// $CD:06C4 (steady), $CD:076C (BG3 moving), $CD:07E2 (the wave), $CD:07B9 (the finer wave)
void battle_scroll_layers(GameState *g, BattleScrollMode mode)
{
    const LayerScroll s[SCROLLED_LAYERS] = {
        offset_scroll(g, BG1),
        steady_scroll(g, BG2),
        mode == BATTLE_SCROLL_STEADY ? steady_scroll(g, BG3) : moved_bg3_scroll(g),
    };
    for (unsigned layer = 0; layer < SCROLLED_LAYERS; layer++)
        scroll_entries(g, layer, s[layer], waved_layer(g, mode, layer), 0, LINES_PER_LAYER);
    if (mode == BATTLE_SCROLL_LINE_OFFSETS_LONG_BG3)
        scroll_entries(g, BG3, s[BG3], waved_layer(g, mode, BG3), LINES_PER_LAYER, FINER_BG3_ENTRIES);
    set_battle_screen_leftover(g, s[BG1].x);
    ORIGINAL_EFFECT(leave_layer_scrolls(g, mode, s));
}

/* ---- The screen shake --------------------------------------------------------------------- */

/* An axis's random magnitude is a pick from the battle's random bytes, moved
 * on by the frame count: across by how long it held the last one, down from a
 * quarter of the way into the table. (The original's pick adds in the carry
 * of the test that the time is up, which is always set.) */
enum { DOWN_PICKS_FROM = 0x40, TIME_UP_CARRY = 1 };

static uint8_t random_magnitude(const GameState *g, uint8_t from)
{
    return battle_random_table(g, (uint8_t)(from + (uint8_t)battle_frame_count(g) + TIME_UP_CARRY));
}

/* The offset is the magnitude times the amplitude, in 256ths. */
enum { OFFSET_FRACTION_BITS = 8 };

/* The multiplier's operands, as the original leaves them after an axis's
 * product. */
static void leave_multiplier(GameState *g, uint8_t magnitude, uint8_t amplitude)
{
    hw_set_multiplicand(g, magnitude);
    (void)hw_multiply_by(g, amplitude);
}

/* The amplitude steps toward where it settles; dying away to nothing, it
 * stops the axis (whose offset this frame is then nothing). */
static void step_amplitude(GameState *g, unsigned axis)
{
    uint8_t amplitude = battle_shake_amplitude(g, axis), settles_to = battle_shake_target(g, axis);
    if (amplitude > settles_to) {
        set_battle_shake_amplitude(g, axis, --amplitude);
        if (amplitude == 0)
            set_battle_shake_on(g, axis, 0);
    } else if (amplitude < settles_to) {
        set_battle_shake_amplitude(g, axis, (uint8_t)(amplitude + 1));
    }
}

// $CD:0B64 (across), $CD:0BC7 (down)
/* An axis's offset this frame. It holds a magnitude for its period and steps
 * its amplitude at its rate; an axis that isn't shaking puts every layer back
 * on it. */
static uint8_t shake_axis(GameState *g, unsigned axis)
{
    if (!battle_shake_on(g, axis)) {
        for (unsigned layer = 0; layer < SCROLLED_LAYERS; layer++)
            set_battle_shake_offsets(g, layer * 2 + axis, 0);
        return 0;
    }
    uint8_t held = (uint8_t)(battle_shake_elapsed(g, axis) + 1);
    if (held >= battle_shake_period(g, axis)) {
        set_battle_shake_elapsed(g, axis, 0);
        set_battle_shake_magnitude(g, axis, random_magnitude(g, axis == ACROSS ? held : DOWN_PICKS_FROM));
    } else {
        set_battle_shake_elapsed(g, axis, held);
    }
    uint8_t since = (uint8_t)(battle_shake_rate_elapsed(g, axis) + 1);
    if (since >= battle_shake_rate(g, axis)) {
        set_battle_shake_rate_elapsed(g, axis, 0);
        step_amplitude(g, axis);
    } else {
        set_battle_shake_rate_elapsed(g, axis, since);
    }
    uint8_t magnitude = battle_shake_magnitude(g, axis), amplitude = battle_shake_amplitude(g, axis);
    ORIGINAL_EFFECT(leave_multiplier(g, magnitude, amplitude));
    return (uint8_t)(magnitude * amplitude >> OFFSET_FRACTION_BITS);
}

/* Which layers the shake moves: a bit a layer and axis, BG1 across in bit 7
 * down to BG3 down in bit 2. */
enum { SHAKEN_BG1_ACROSS = 0x80 };

static bool shakes(uint8_t layers, unsigned layer, unsigned axis)
{
    return (layers & (SHAKEN_BG1_ACROSS >> (layer * 2 + axis))) != 0;
}

/* The shake's offsets on the axes are left in the scratch cells $45 and
 * $47. */
static void leave_shake(GameState *g, uint8_t across, uint8_t down)
{
    set_battle_scratch(g, 0, across);
    set_battle_scratch(g, 2, down);
}

/* ---- Palette presets ---------------------------------------------------------------------- */

/* A screen effect asks for a preset's fades to start: bit 7 of the request
 * for preset slot 2's over palette animation slot 10, any other bit for slots
 * 0 and 1's over 8 and 9 (and only bit 0 is taken back after). */
enum { REQUEST_SLOT_10 = 0x80, REQUEST_SLOTS_8_9 = 0x01, PRESET_SLOTS_8_9 = 0, PRESET_SLOT_10 = 2,
       FIRST_PRESET_SLOT = 8, PRESET_SLOT_BYTES = 12 };

/* A preset slot is laid out as a palette animation slot, and copied whole. */
static void start_preset(GameState *g, unsigned preset, unsigned slot)
{
    set_palette_animations_kind(g, slot, battle_palette_slot_presets_kind(g, preset));
    set_palette_animations_first_color(g, slot, battle_palette_slot_presets_first_color(g, preset));
    set_palette_animations_color_count(g, slot, battle_palette_slot_presets_color_count(g, preset));
    set_palette_animations_step(g, slot, battle_palette_slot_presets_step(g, preset));
    set_palette_animations_delay(g, slot, battle_palette_slot_presets_delay(g, preset));
    set_palette_animations_period(g, slot, battle_palette_slot_presets_period(g, preset));
    set_palette_animations_fade_mode(g, slot, battle_palette_slot_presets_fade_mode(g, preset));
    set_palette_animations_fade_level(g, slot, battle_palette_slot_presets_fade_level(g, preset));
    set_palette_animations_fade_target(g, slot, battle_palette_slot_presets_fade_target(g, preset));
    set_palette_animations_frame_table(g, slot, battle_palette_slot_presets_frame_table(g, preset));
}

static void start_requested_presets(GameState *g)
{
    uint8_t request = battle_palette_preset_request(g);
    if (request == 0)
        return;
    if (request & REQUEST_SLOT_10) {
        start_preset(g, PRESET_SLOT_10, FIRST_PRESET_SLOT + PRESET_SLOT_10);
        set_battle_palette_preset_request(g, request & (uint8_t)~REQUEST_SLOT_10);
        ORIGINAL_EFFECT(charge_block_move(g, PRESET_SLOT_BYTES));
        return;
    }
    for (unsigned preset = PRESET_SLOTS_8_9; preset < PRESET_SLOT_10; preset++)
        start_preset(g, preset, FIRST_PRESET_SLOT + preset);
    set_battle_palette_preset_request(g, request & (uint8_t)~REQUEST_SLOTS_8_9);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * PRESET_SLOT_BYTES));
}

// $CD:0B51
void battle_shake_screen(GameState *g)
{
    const uint8_t offset[2] = { shake_axis(g, ACROSS), shake_axis(g, DOWN) };
    ORIGINAL_EFFECT(leave_shake(g, offset[ACROSS], offset[DOWN]));
    uint8_t layers = battle_shake_layers(g);
    for (unsigned layer = 0; layer < SCROLLED_LAYERS; layer++)
        for (unsigned axis = ACROSS; axis <= DOWN; axis++)
            if (shakes(layers, layer, axis))
                set_battle_shake_offsets(g, layer * 2 + axis, offset[axis]);
    start_requested_presets(g);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The scroll modes' routines, which the original's dispatch jumps to through
 * its table at $CD:06BC, whose time each mode's fill takes. */
static const uint32_t scroll_routines[BATTLE_SCROLL_MODES] = { 0xCD06C4, 0xCD076C, 0xCD07E2, 0xCD07B9 };

/* The fills finish as the original's do: A's high byte the page's (0), which
 * their callers read, and 8-bit A with 16-bit index registers. */
static void finish_fill(Snes *s)
{
    s->cpu.a = BATTLE_PAGE;
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
}

void battle_update_scroll_tables(Snes *s)
{
    battle_scroll_layers(state_of(s), BATTLE_SCROLL_STEADY);
    finish_fill(s);
}

void battle_update_scroll_tables_bg3_moving(Snes *s)
{
    battle_scroll_layers(state_of(s), BATTLE_SCROLL_BG3_MOVING);
    finish_fill(s);
}

/* The wave's fill leaves X at BG3's second 32 entries, where the finer
 * wave's ($CD:07B9, which calls it) goes on. */
enum { SECOND_HALF_OF_BG3 = 0x80 };

void battle_update_scroll_tables_line_wave(Snes *s)
{
    battle_scroll_layers(state_of(s), BATTLE_SCROLL_LINE_OFFSETS);
    s->cpu.x = SECOND_HALF_OF_BG3;
    finish_fill(s);
}

void battle_update_scroll_tables_finer_line_wave(Snes *s)
{
    battle_scroll_layers(state_of(s), BATTLE_SCROLL_LINE_OFFSETS_LONG_BG3);
    finish_fill(s);
}

static BattleScrollMode scroll_mode(const GameState *g) { return (BattleScrollMode)battle_scroll_mode(g); }

// $CD:06B4
/* The frame work's scroll: the mode's fill, each timed as its own routine. */
void battle_update_scroll_dispatch(Snes *s)
{
    GameState *g = state_of(s);
    BattleScrollMode mode = scroll_mode(g);
    NATIVE_TIME_AS(s, scroll_routines[mode], battle_scroll_layers(g, mode));
    finish_fill(s);
}

/* The frame work's call of the dispatch, which indexes its table with A's
 * high byte too: 0 wherever the game calls it, and anything else is the
 * original's. */
enum { FRAME_WORK_SCROLLS = 0xCD0929, SCROLL_DISPATCH = 0xCD06B4 };

static bool a_high_clear(const Snes *s) { return (s->cpu.a & 0xFF00) == 0; }

bool battle_scroll_the_layers(Snes *s)
{
    GameState *g = state_of(s);
    BattleScrollMode mode = scroll_mode(g);
    if (!a_high_clear(s))
        return battle_call(s, FRAME_WORK_SCROLLS, SCROLL_DISPATCH, DC_X | DC_Y | DC_FLAGS | DC_AL);
    NATIVE_TIME_AS(s, scroll_routines[mode], battle_scroll_layers(g, mode));
    s->cpu.a = BATTLE_PAGE;   /* as the fills leave it */
    return true;
}

/* The mode is one of the four the table has, and A's high byte clear. */
bool battle_scroll_mode_is_one(Snes *s)
{
    vars_peek(true);
    bool one = battle_scroll_mode(state_of(s)) < BATTLE_SCROLL_MODES;
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && one;
}

void battle_update_screen_shake(Snes *s)
{
    battle_shake_screen(state_of(s));
    m_rts(s);
}

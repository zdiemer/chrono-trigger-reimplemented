/* Battle: the screen script's waves across the layers' lines.
 *
 * The frame work fills the layers' per-line scroll tables each frame
 * (battle/scroll.c); in the two line offset modes it adds an offset from
 * battle_line_scroll_offsets to each entry of the layers that take it
 * (battle_line_scroll_layers). A screen script sets those modes and the
 * offsets: a wave, a sine down the lines whose phase moves on every so many
 * frames, so the picture ripples; or a sway, the top half of the screen one
 * way and the bottom the other, by an amount that builds up. The finer mode
 * gives BG3 an entry for every line pair of the whole screen. */
#include "battle/screen_script.h"

#include "battle/frame.h"
#include "battle/screen_data.h"
#include "cpu_io.h"
#include "original_effects.h"

enum { LINE_OFFSET_LAYERS = 3, BG1 = 0, BG3 = 2 };

/* The waves' entries, and how far apart their phases are. */
enum { FINE_ENTRIES = 64, FINE_STEP = 4, COARSE_ENTRIES = 32, COARSE_STEP = 8 };

/* An offset, a signed byte, as the scroll's word. */
static uint16_t widened(uint8_t v) { return (uint16_t)(int16_t)(int8_t)v; }

static void set_offset(GameState *g, unsigned entry, uint16_t across)
{
    set_battle_line_scroll_offsets_across(g, entry, across);
}

/* In the fight with the first of the battle's rules, BG1 keeps still. */


static void leave_wave(GameState *g, uint8_t amplitude, uint8_t every, uint8_t speed, bool fine, uint8_t last_phase)
{
    screen_leave_cell(g, 0x45, amplitude);
    screen_leave_cell(g, 0x47, every);
    if (fine)
        screen_leave_cell(g, 0x49, speed);
    hw_set_multiplicand(g, battle_sine_magnitude(g, last_phase));
    hw_multiply_by(g, amplitude);
}

// $CD:253B
/* Wave the lines of the layers named (BG1, BG2 and BG3 by the operand's top
 * bits) by a sine of `amplitude` pixels down the screen, its phase moving on
 * by `speed` whenever the frame count's bits in `every` are clear. The
 * coarse wave gives each layer 32 entries, a step of its phase apart; the
 * fine one gives BG3 64, half a step apart. */
static ScreenFlow wave_lines(GameState *g, uint8_t layers, uint8_t every, uint8_t amplitude, uint8_t speed, bool fine)
{
    if (((uint8_t)battle_frame_count(g) & every) == 0)
        set_battle_line_wave_phase(g, (uint8_t)(battle_line_wave_phase(g) + speed));
    unsigned entries = fine ? FINE_ENTRIES : COARSE_ENTRIES;
    uint8_t step = fine ? FINE_STEP : COARSE_STEP, phase = battle_line_wave_phase(g);
    for (unsigned entry = 0; entry < entries; entry++, phase = (uint8_t)(phase + step)) {
        uint8_t v = (uint8_t)(battle_sine_magnitude(g, phase) * amplitude >> 8);
        set_offset(g, entry, widened((phase & SCREEN_HALF_TURN) ? (uint8_t)-v : v));
    }
    set_battle_scroll_mode(g, fine ? BATTLE_SCROLL_LINE_OFFSETS_LONG_BG3 : BATTLE_SCROLL_LINE_OFFSETS);
    for (unsigned layer = 0; layer < LINE_OFFSET_LAYERS; layer++)
        set_battle_line_scroll_layers(g, layer, (layers >> layer) & 1);
    if ((battle_enemy_rule(g) & BATTLE_RULE_NUMBER) == BATTLE_THE_FIRST_RULE)
        set_battle_line_scroll_layers(g, BG1, 0);
    ORIGINAL_EFFECT(leave_wave(g, amplitude, every, speed, fine, (uint8_t)(phase - step)));
    return screen_on();
}
SCREEN_OPERATION(wave_lines, layers, every, amplitude, speed, fine)

/* A sway byte below $80 adds to the sway; $FF keeps it; the others end it. */
enum { KEEP_SWAY = 0xFF, HALF_THE_ENTRIES = 32 };

// $CD:187E
/* Sway the screen's halves apart: the top half's lines one way and the
 * bottom half's the other by the sway, which the operand builds up or ends;
 * BG3 takes it unless the operand is 0. */
static ScreenFlow sway_lines(GameState *g, uint8_t value)
{
    set_battle_scroll_mode(g, BATTLE_SCROLL_LINE_OFFSETS_LONG_BG3);
    uint16_t bg3 = battle_line_scroll_layers(g, BG3);
    set_battle_line_scroll_layers(g, BG3, (uint16_t)((bg3 & 0xFF00) | value));
    if (value < SCREEN_HALF_TURN)
        set_battle_line_sway(g, (uint8_t)(battle_line_sway(g) + value));
    else if (value != KEEP_SWAY)
        set_battle_line_sway(g, 0);
    uint16_t sway = battle_line_sway(g);
    for (unsigned entry = 0; entry < HALF_THE_ENTRIES; entry++) {
        set_offset(g, entry, sway);
        set_battle_line_scroll_offsets_down(g, entry, 0);
        set_offset(g, HALF_THE_ENTRIES + entry, (uint16_t)-sway);
        set_battle_line_scroll_offsets_down(g, HALF_THE_ENTRIES + entry, 0);
    }
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, sway));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x47, (uint16_t)-sway));
    return screen_on();
}
SCREEN_OPERATION(sway_lines, value)

SCREEN_OPCODES(screen_wave_opcodes,
    { 0x8F, OPERATION(wave_lines), .length = 3, .layers = TOP_THREE(0), .every = LOW_FIVE(0), .amplitude = BYTE(1), .speed = LOW_SEVEN(2), .fine = TOP_BIT(2) },
    { 0xE7, OPERATION(sway_lines), .length = 1, .value = BYTE(0) },
);

/* Battle: the layers in motion, and the screen-effect settings.
 *
 * Each screen script channel moves a layer of its own (battle_screen_layer;
 * channel 0's is BG3, which the frame work scrolls by it): the channel's
 * commands jitter it about a spot, swing it up and down, and remember where
 * the animation positions were. Apart from the scripts, the battle's actions
 * set a screen effect from five settings bytes: the screen shake, which the
 * frame work runs per axis on the layers it names, or a palette preset, a
 * pair of fades the frame work copies over palette animation slots 8 and 9. */
#include "battle/screen_script.h"

#include "charge.h"
#include "original_effects.h"
#include "port/native.h"

/* ---- The channel's layer ----------------------------------------------------------------- */

static ScreenChannel running_channel(const GameState *g) { return (ScreenChannel)(battle_screen_channel(g) / SCREEN_CHANNEL_RECORD_BYTES); }

// $D1:F474
/* A random byte, picked from the battle's table by a seed moved on by the
 * frame count. */
static uint8_t random_pick(const GameState *g, uint8_t seed)
{
    return battle_random_table(g, (uint8_t)(seed + (uint8_t)battle_frame_count(g)));
}

/* The original's pick also adds in the carry its caller's last addition left:
 * a pick right after an amount that went past 255 is taken one further on.
 * The jitter's first pick comes right after the screen script's dispatch,
 * whose shift of the opcode always leaves it. */
static uint8_t random_pick_after_overflow(const GameState *g, uint8_t seed, bool overflowed)
{
    return random_pick(g, (uint8_t)(seed + overflowed));
}

/* What the jitter leaves in its cells: the two amounts and the spread. */
static void leave_jitter(GameState *g, uint8_t across, uint8_t down, uint8_t spread)
{
    screen_leave_cell(g, 0x45, across);
    screen_leave_cell(g, 0x47, down);
    screen_leave_cell(g, 0x49, spread);
}

// $D1:F47C
/* Shake the channel's layer to a random spot near a point: right by `across`
 * and down by `down`, each plus a random amount within the `spread` mask. The
 * second pick follows from the first amount, so the two move together. */
static void jitter_layer(GameState *g, ScreenChannel ch, uint8_t across, uint8_t down, uint8_t spread)
{
    uint16_t right = (uint16_t)((random_pick_after_overflow(g, spread, true) & spread) + across);
    uint8_t lower = (uint8_t)((random_pick_after_overflow(g, (uint8_t)right, right > 0xFF) & spread) + down);
    set_battle_screen_layer_x(g, ch, (uint16_t)-(uint8_t)right);
    set_battle_screen_layer_y(g, ch, (uint16_t)-lower);
    ORIGINAL_EFFECT(leave_jitter(g, (uint8_t)right, lower, spread));
}

/* Screen command $DF: jitter the layer about a point (three operands: across,
 * down, spread). */
static void jitter_command(GameState *g)
{
    jitter_layer(g, running_channel(g), screen_command_operand(g, 0), screen_command_operand(g, 1),
                 screen_command_operand(g, 2));
    screen_command_took(g, 3);
}

/* The swing's state: which way it is going (the original tests the flag's
 * low byte, and sets it with a word's increment), its speed and its
 * position, 16.16 fixed point in pixels. */
enum { SWING_FRACTION_BITS = 16 };

static bool swing_rising(const GameState *g, ScreenChannel ch) { return (uint8_t)battle_screen_layer_swing_rising(g, ch) != 0; }
static void swing_turn_down(GameState *g, ScreenChannel ch) { set_battle_screen_layer_swing_rising(g, ch, 0); }
static void swing_turn_up(GameState *g, ScreenChannel ch)
{
    set_battle_screen_layer_swing_rising(g, ch, (uint16_t)(battle_screen_layer_swing_rising(g, ch) + 1));
}

// $D1:F559
/* Swing the channel's layer one frame, like a weight on a spring: moving
 * down it slows by the swing's step each frame until it stops, and then it
 * rises, gathering speed, until its speed would run past its range, when it
 * turns down again. The layer's vertical offset follows: the swing's center
 * plus the position, or minus it for the second channel, whose layer swings
 * the other way. */
static void swing_layer(GameState *g, ScreenChannel ch)
{
    uint16_t speed = battle_screen_swing_speed(g, ch), step = battle_screen_swing_step(g, ch);
    uint32_t position = battle_screen_swing_position(g, ch);
    if (swing_rising(g, ch)) {
        if (speed > 0xFFFF - step)
            swing_turn_down(g, ch);
        else
            set_battle_screen_swing_speed(g, ch, speed = (uint16_t)(speed + step));
        position -= speed;
    } else {
        if (speed < step)
            swing_turn_up(g, ch);
        else
            set_battle_screen_swing_speed(g, ch, speed = (uint16_t)(speed - step));
        position += speed;
    }
    set_battle_screen_swing_position(g, ch, position);
    uint16_t center = battle_screen_layer_swing_center(g, ch), by = (uint16_t)(position >> SWING_FRACTION_BITS);
    set_battle_screen_layer_offset_y(g, ch, (uint16_t)(ch == 0 ? center + by : center - by));
}

/* ---- Where the animation positions were ------------------------------------------------ */

/* The saved positions: two slots, each the three positions' across and down
 * bytes, the slots' bytes interleaved. */
enum { SAVED_POSITIONS = 3, SAVED_POSITION_SLOTS = 2 };

static void save_position_byte(GameState *g, uint8_t slot, unsigned byte, uint8_t v)
{
    set_battle_shape_corners(g, byte * SAVED_POSITION_SLOTS + slot, v);
}

// $D1:F5CD
/* Screen command $DD: remember where the first three animation positions are,
 * in the slot the operand names, for a later command to bring back. */
static void save_animation_positions(GameState *g, uint8_t slot)
{
    for (unsigned p = 0; p < SAVED_POSITIONS; p++) {
        uint16_t at = battle_animation_positions(g, p);
        save_position_byte(g, slot, 2 * p, (uint8_t)at);
        save_position_byte(g, slot, 2 * p + 1, (uint8_t)(at >> 8));
    }
}

/* ---- The screen-effect settings ------------------------------------------------------------ */

/* A caller asks for a screen effect with five bytes of settings. The first
 * says which effect in its high nibble: 0-3 a palette preset (the nibble
 * picking which), 4 the screen shake, 5 an end to it. */
enum { SETTINGS_BYTES = SCREEN_EFFECT_SETTINGS };
typedef enum { PALETTE_PRESET, START_SHAKE, STOP_SHAKE } ScreenEffectKind;
enum { SCREEN_EFFECT_CHOICES = 6 };

static const ScreenEffectKind effect_kinds[SCREEN_EFFECT_CHOICES] = {
    PALETTE_PRESET, PALETTE_PRESET, PALETTE_PRESET, PALETTE_PRESET, START_SHAKE, STOP_SHAKE,
};

static unsigned effect_choice(const uint8_t settings[SETTINGS_BYTES]) { return settings[0] >> 4; }

/* A palette preset: a pair of fades over palette animation slots 8 and 9. The
 * preset gives the colors they change and the levels they fade between; the
 * settings' second byte how: bits 0-2 which color channels, 3-5 the pace (their
 * delay, period and step), 6 a fade to black rather than white, run from the
 * target back, and 7 the levels at half. */
typedef struct {
    uint8_t preset;
    uint8_t channels, pace;
    bool to_black, halfway;
} PalettePreset;

static PalettePreset palette_preset_of(const uint8_t settings[SETTINGS_BYTES])
{
    uint8_t how = settings[1];
    return (PalettePreset){ .preset = (uint8_t)(settings[0] >> 4), .channels = how & 0x07, .pace = (how >> 3) & 0x07,
                            .to_black = (how & 0x40) != 0, .halfway = (how & 0x80) != 0 };
}

/* The fades' kinds, as palette animation slots have them (core/palette.c). */
enum { FADE_TO_WHITE = 0x40, FADE_TO_BLACK = 0x50, HALF_LEVEL = 0x80 };

/* At half: a fade to white's levels that aren't nothing, a fade to black's
 * that are. */
static uint8_t halfway_level(uint8_t level, bool to_black)
{
    if (to_black)
        return level == 0 ? HALF_LEVEL : level;
    return level != 0 ? HALF_LEVEL : 0;
}

enum { PRESET_SLOTS = 2 };

static void request_preset_copy(GameState *g) { set_battle_palette_preset_request(g, (uint8_t)(battle_palette_preset_request(g) + 1)); }

// $D1:F74D
/* Set up the palette preset's two fades and ask the frame work to start them. */
static void set_palette_preset(GameState *g, const PalettePreset *p)
{
    for (unsigned slot = 0; slot < PRESET_SLOTS; slot++) {
        PresetFade f = palette_preset_fade(g, p->preset, slot);
        if (p->halfway) {
            f.level = halfway_level(f.level, p->to_black);
            f.target = halfway_level(f.target, p->to_black);
        }
        set_battle_palette_slot_presets_kind(g, slot, p->to_black ? FADE_TO_BLACK : FADE_TO_WHITE);
        set_battle_palette_slot_presets_first_color(g, slot, f.first_color);
        set_battle_palette_slot_presets_color_count(g, slot, f.color_count);
        set_battle_palette_slot_presets_fade_mode(g, slot, p->channels);
        set_battle_palette_slot_presets_delay(g, slot, palette_preset_timing(g, p->pace, slot, false));
        set_battle_palette_slot_presets_period(g, slot, palette_preset_timing(g, p->pace, slot, true));
        set_battle_palette_slot_presets_step(g, slot, palette_preset_step(g, p->pace, slot));
        set_battle_palette_slot_presets_fade_level(g, slot, p->to_black ? f.target : f.level);
        set_battle_palette_slot_presets_fade_target(g, slot, p->to_black ? f.level : f.target);
    }
    request_preset_copy(g);
}

/* The screen shake, per axis, from two bytes of the settings (the second and
 * third across, the fourth and fifth down): the first byte's bits 0-1 which
 * layers it moves (none: it stays still), 2-4 how many frames it holds each
 * magnitude, 5-7 how many between amplitude steps; the second byte the
 * amplitude it starts at (high nibble) and the one it settles to (low). */
enum { SHAKE_AXES = 2 };
typedef struct {
    uint8_t layers, period, rate;
    uint8_t amplitude, settles_to;
} ShakeAxis;

static ShakeAxis shake_axis_of(uint8_t timing, uint8_t levels)
{
    return (ShakeAxis){ .layers = timing & 0x03, .period = (timing >> 2) & 0x07, .rate = timing >> 5,
                        .amplitude = levels >> 4, .settles_to = levels & 0x0F };
}

// $D1:F692
/* End the screen shake. */
static void stop_shake(GameState *g)
{
    for (unsigned axis = 0; axis < SHAKE_AXES; axis++)
        set_battle_shake_on(g, axis, 0);
    set_battle_shake_layers(g, 0);
}

// $D1:F6B4
/* Start the screen shake on both axes. An axis shakes while it moves layers,
 * the frame work picking a new magnitude every period and stepping the
 * amplitude toward where it settles at its rate. */
static void start_shake(GameState *g, const ShakeAxis axes[SHAKE_AXES])
{
    for (unsigned axis = 0; axis < SHAKE_AXES; axis++) {
        const ShakeAxis *a = &axes[axis];
        set_battle_shake_period(g, axis, shake_period_choice(g, a->period));
        set_battle_shake_elapsed(g, axis, 0);
        set_battle_shake_rate(g, axis, shake_rate_choice(g, a->rate));
        set_battle_shake_rate_elapsed(g, axis, 0);
        set_battle_shake_target(g, axis, a->settles_to);
        set_battle_shake_amplitude(g, axis, a->amplitude);
        set_battle_shake_on(g, axis, a->layers);
    }
    uint8_t across = shake_layers_across(g, axes[0].layers);
    set_battle_shake_layers(g, (uint8_t)(shake_layers_down(g, axes[1].layers) | across));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x5B, across));
}

static void start_shake_from(GameState *g, const uint8_t settings[SETTINGS_BYTES])
{
    const ShakeAxis axes[SHAKE_AXES] = { shake_axis_of(settings[1], settings[2]), shake_axis_of(settings[3], settings[4]) };
    start_shake(g, axes);
}

static void set_palette_preset_from(GameState *g, const uint8_t settings[SETTINGS_BYTES])
{
    PalettePreset p = palette_preset_of(settings);
    set_palette_preset(g, &p);
}

/* ---- The script's shake ------------------------------------------------------------------- */

// $CD:1D98
/* A screen script's shake, set straight from its settings: on each axis an
 * amplitude to start at, one to settle to and a rate, a new magnitude every
 * frame; or the shake stops. */
static ScreenFlow shake_screen(GameState *g, bool stop, ScreenTriple across, ScreenTriple down, uint8_t layers)
{
    if (stop) {
        stop_shake(g);
        return screen_on();
    }
    const ScreenTriple *axes[SHAKE_AXES] = { &across, &down };
    for (unsigned axis = 0; axis < SHAKE_AXES; axis++) {
        uint8_t amplitude = axes[axis]->of[0], settles_to = axes[axis]->of[1];
        set_battle_shake_elapsed(g, axis, 0);
        set_battle_shake_rate_elapsed(g, axis, 0);
        set_battle_shake_period(g, axis, 1);
        set_battle_shake_amplitude(g, axis, amplitude);
        set_battle_shake_target(g, axis, settles_to);
        set_battle_shake_rate(g, axis, axes[axis]->of[2]);
        set_battle_shake_on(g, axis, amplitude | settles_to);
    }
    set_battle_shake_layers(g, layers);
    return screen_on();
}
SCREEN_OPERATION(shake_screen, stop, shake_across, shake_down, layers)

/* ---- The commands' rows (battle/screen_script.h) --------------------------------------------------- */

static ScreenFlow jitter_about(GameState *g, ScreenChannel channel, uint8_t across, uint8_t down, uint8_t spread)
{
    jitter_layer(g, channel, across, down, spread);
    return screen_on();
}
SCREEN_OPERATION(jitter_about, channel, across, down, spread)

static ScreenFlow swing(GameState *g, ScreenChannel channel)
{
    swing_layer(g, channel);
    return screen_on();
}
SCREEN_OPERATION(swing, channel)

static ScreenFlow save_positions(GameState *g, uint8_t slot)
{
    save_animation_positions(g, slot);
    return screen_on();
}
SCREEN_OPERATION(save_positions, slot)

static bool names_a_slot(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->slot < SAVED_POSITION_SLOTS;
}

SCREEN_OPCODES(screen_motion_opcodes,
    { 0xDF, OPERATION(jitter_about), .length = 3, .across = BYTE(0), .down = BYTE(1), .spread = BYTE(2) },
    { 0xCE, OPERATION(swing), .length = 1 },
    { 0xDD, OPERATION(save_positions), .needs = names_a_slot, .length = 1, .slot = BYTE(0) },
    { 0xE3, OPERATION(save_positions), .needs = names_a_slot, .length = 1, .slot = BYTE(0) },
    { 0xC0, OPERATION(shake_screen), .length = 8, .stop = TOP_BIT(0), .shake_across = TRIPLE(1), .shake_down = TRIPLE(4), .layers = BYTE(7) },
);

/* ---- The entries ------------------------------------------------------------------------ */

/* The guards. The screen script's dispatch calls its commands' bodies with
 * A's high byte clear and the carry set, running a channel. */
static bool on_a_channel(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool on_one = battle_screen_channel(g) % SCREEN_CHANNEL_RECORD_BYTES == 0 && running_channel(g) < SCREEN_CHANNELS;
    vars_peek(false);
    return battle_screen_page_a_clear(s) && on_one;
}

bool battle_screen_channel_command(Snes *s) { return on_a_channel(s); }
bool battle_screen_dispatched_jitter(Snes *s) { return on_a_channel(s) && m_flag(s, CPU_FLAG_C); }

/* ... and the slots the positions are saved in are two. */
bool battle_screen_saves_positions(Snes *s)
{
    vars_peek(true);
    bool a_slot = screen_command_operand(state_of(s), 0) < SAVED_POSITION_SLOTS;
    vars_peek(false);
    return battle_screen_page_a_clear(s) && a_slot;
}

/* The caller hands the settings in five of the page's cells, $53-$57, and the
 * effect is one of the six the original's table has. */
static void settings_in_cells(const GameState *g, uint8_t settings[SETTINGS_BYTES])
{
    for (unsigned i = 0; i < SETTINGS_BYTES; i++)
        settings[i] = battle_screen_effect_settings(g, i);
}

bool battle_screen_effect_is_one(Snes *s)
{
    uint8_t settings[SETTINGS_BYTES];
    vars_peek(true);
    settings_in_cells(state_of(s), settings);
    vars_peek(false);
    return battle_screen_page_a_clear(s) && effect_choice(settings) < SCREEN_EFFECT_CHOICES;
}

// $D1:F474
void battle_screen_random_byte(Snes *s)
{
    m_set_a8(s, random_pick_after_overflow(state_of(s), (uint8_t)s->cpu.a, m_flag(s, CPU_FLAG_C)));
    m_rts(s);
}

void battle_screen_jitter(Snes *s)
{
    jitter_command(state_of(s));
    screen_command_done(s);
}

void battle_screen_swing(Snes *s)
{
    GameState *g = state_of(s);
    swing_layer(g, running_channel(g));
    screen_command_done(s);
}

void battle_screen_save_positions(Snes *s)
{
    GameState *g = state_of(s);
    save_animation_positions(g, screen_command_operand(g, 0));
    screen_command_done(s);
}

// $D1:F67A
/* Set the screen effect the settings name, each effect's work timed as its
 * own routine's. */
void battle_set_screen_effect_to(GameState *g, const uint8_t settings[SETTINGS_BYTES])
{
    switch (effect_kinds[effect_choice(settings)]) {
    case PALETTE_PRESET: TIMED_AS(g, 0xD1F74D, set_palette_preset_from(g, settings)); break;
    case START_SHAKE: TIMED_AS(g, 0xD1F6B4, start_shake_from(g, settings)); break;
    case STOP_SHAKE: TIMED_AS(g, 0xD1F692, stop_shake(g)); break;
    }
}

void battle_set_screen_effect(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t settings[SETTINGS_BYTES];
    settings_in_cells(g, settings);
    battle_set_screen_effect_to(g, settings);
    screen_command_done(s);
}

void battle_stop_shake(Snes *s)
{
    stop_shake(state_of(s));
    s->cpu.a = 0;
    m_rts(s);
}

void battle_start_shake(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t settings[SETTINGS_BYTES];
    settings_in_cells(g, settings);
    start_shake_from(g, settings);
    s->cpu.a = 0;
    m_rts(s);
}

void battle_set_palette_preset(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t settings[SETTINGS_BYTES];
    settings_in_cells(g, settings);
    set_palette_preset_from(g, settings);
    s->cpu.a = 0;
    m_rts(s);
}

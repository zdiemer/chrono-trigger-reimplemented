/* Scripts directing the screen: fading it in and out, tinting it with a
 * colour (a flash, a red alert, a dream's haze), changing the map's tiles
 * (a door opening, a bridge appearing), drifting the layers, scrolling the
 * camera, and handing the party to the player or taking it away.
 *
 * The fades and tints are gradual when a rate is given: the field's
 * interrupt steps them a frame at a time toward their target, and marks them
 * running in field_effect_status (bit 1 the brightness, bits 3-4 the tint)
 * until they arrive. */
#include "event/bridge.h"

enum { FADING = 0x02, TINTING = 0x08, TINT_CHANGES = 0x18 };   /* field_effect_status */
enum { TILE_COPY_QUEUED = 0x20 };                              /* field_requests */

// $C0:3C05
/* Give the player control of the party (nonzero), or take it away for a
 * cutscene. */
static EventFlow set_player_control(GameState *g, uint8_t control)
{
    set_field_player_control(g, control);
    return next_command();
}

// $C0:3CD0
/* Fade the screen to a brightness (the high nibble, 0 black to 15 full): at
 * once, or at the rate in the low nibble. Ends the turn. */
static EventFlow fade(GameState *g, uint8_t brightness_and_rate)
{
    uint8_t level = (uint8_t)(brightness_and_rate >> 4), rate = brightness_and_rate & 0x0F;
    if (rate != 0) {
        set_field_brightness_rate(g, (uint16_t)(rate << 8 | rate));
        set_field_brightness_target(g, level);
        set_field_effect_status(g, field_effect_status(g) | FADING);
    } else {
        set_field_brightness(g, level);
    }
    return end_turn();
}

// $C0:3CFD
/* Wait for a fade, or a tint, to arrive. */
static EventFlow wait_for_screen(GameState *g, uint8_t changes)
{
    return (field_effect_status(g) & changes) ? stay() : end_turn();
}

/* The tint's colour channels (bits 5-7) become the target's, its strength
 * stays. The original keeps the channels in its scratch cell. */
static void take_target_channels(GameState *g)
{
    uint8_t channels = field_fixed_color_target(g) & 0xE0;
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, channels));
    set_field_fixed_color(g, (uint8_t)((field_fixed_color(g) & 0x1F) | channels));
}

enum { TINT_ADDS = 0x17, TINT_SUBTRACTS = 0x97, NO_TINT = 0xE0 };

// $C0:3D07
/* Tint the screen with a colour, added to the picture or (bit 7 of the rate)
 * taken away from it: at once, or at the rate given, which ends the turn.
 * No colour takes the tint away and gives the picture back its own colour
 * layers; that form of the command has no rate, and is a byte shorter. */
static EventFlow tint(GameState *g, uint8_t colour, uint8_t rate)
{
    if (colour == 0) {
        set_field_fixed_color_target(g, NO_TINT);
        set_field_fixed_color(g, NO_TINT);
        set_field_fixed_color_math_layers(g, field_color_math_layers(g));
        return next_command_after(2);
    }
    set_field_fixed_color_target(g, colour);
    take_target_channels(g);
    uint8_t how = rate & 0x80 ? TINT_SUBTRACTS : TINT_ADDS;
    rate &= 0x7F;
    if (rate == 0) {
        set_field_fixed_color(g, field_fixed_color_target(g));
        set_field_fixed_color_math_layers(g, how);
        return next_command();
    }
    set_field_fixed_color_rate(g, (uint16_t)(rate << 8 | rate));
    set_field_fixed_color_math_layers(g, how);
    take_target_channels(g);
    set_field_effect_status(g, field_effect_status(g) | TINT_CHANGES);
    return end_turn();
}

// $C0:3D84
/* Set field_setting_bc; zero also puts the layers' scroll offsets back. What
 * the field makes of it isn't established (docs/TODO.md). */
static EventFlow set_bc(GameState *g, uint8_t v)
{
    set_field_setting_bc(g, v);
    if (v == 0) {
        set_field_scroll_offset_x(g, 0);
        set_field_scroll_offset_y(g, 0);
    }
    return next_command();
}

/* A change to the map's tiles: a rectangle of tiles (its top left and bottom
 * right) copied to another place on the maps chosen. */
static void describe_tile_copy(GameState *g, uint16_t from, uint16_t to, uint16_t destination, uint8_t maps)
{
    set_field_tile_copy_from(g, from);
    set_field_tile_copy_to(g, to);
    set_field_tile_copy_destination(g, destination);
    set_field_tile_copy_maps(g, maps);
}

// $C0:3DC5
/* Change the map's tiles on the field's next pass: a door opens, a bridge
 * appears. Ends the turn. */
static EventFlow queue_tile_copy(GameState *g, uint16_t from, uint16_t to, uint16_t destination, uint8_t maps)
{
    describe_tile_copy(g, from, to, destination, maps);
    set_field_tile_copy_queued_maps(g, field_tile_copy_maps(g) & 0x07);
    set_field_requests(g, field_requests(g) | TILE_COPY_QUEUED);
    return end_turn();
}

// $C0:3DF4
/* Drift the layers chosen (bits 0-2) at a speed (x in the low byte, y in the
 * high) for a number of frames: clouds, water, a quake. */
static EventFlow drift_layers(GameState *g, uint16_t speed, uint8_t layers, uint8_t frames)
{
    set_field_layer_extra_speed_time(g, frames);
    for (unsigned layer = 0; layer < FIELD_LAYER_EXTRA_SPEED_COUNT; layer++) {
        if (layers & 1u << layer) {
            set_field_layer_extra_speed_x(g, layer, (uint8_t)speed);
            set_field_layer_extra_speed_y(g, layer, (uint8_t)(speed >> 8));
            ORIGINAL_EFFECT(leave_y(speed));   /* the original loads Y with the speed to store it */
        }
    }
    return next_command();
}

enum { SCROLL_IDLE = 0, SCROLL_RUNNING = 1 };

// $C0:3E30
/* Scroll the camera to a tile (in 16-pixel tiles), with the party out of the
 * player's hands, and wait until it gets there. */
static EventFlow scroll_camera(GameState *g, uint8_t x, uint8_t y)
{
    uint8_t state = field_screen_scroll_state(g);
    if (state == SCROLL_RUNNING)
        return stay();
    if (state != SCROLL_IDLE) {
        set_field_screen_scroll_state(g, SCROLL_IDLE);
        return next_command();
    }
    set_field_screen_scroll_target_x(g, (uint8_t)(x << 1));
    set_field_screen_scroll_target_y(g, (uint8_t)(y << 1));
    set_field_screen_scroll_state(g, SCROLL_RUNNING);
    set_field_player_control(g, 0);
    return stay();
}

// $C0:6788
/* How far down the screen the objects' animations may be updated each frame
 * (the scanline the update stops at): a busy map trades animation for time. */
static EventFlow set_animation_line_limit(GameState *g, uint8_t line)
{
    set_field_animation_line_limit(g, (uint16_t)((field_animation_line_limit(g) & 0xFF00) | line));
    return next_command();
}

/* Back from a call-out, which returns to `returns_to`: true if the callee
 * left the machine as the command runs (event_machine_back); otherwise the
 * port stops there, declaring it, and the original carries on. */
static bool back(Snes *s, uint32_t returns_to, bool x8)
{
    if (event_machine_back(s, x8))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

// $C0:3D97
/* Change the map's tiles now (the field's copy, $C0:AF4E, still the
 * original's code, called out to). Ends the turn. */
void event_cmd_copy_tiles(Snes *s)
{
    enum { LENGTH = 8 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    describe_tile_copy(g, script_word(g, (uint16_t)(at + 1)), script_word(g, (uint16_t)(at + 3)),
                       script_word(g, (uint16_t)(at + 5)), script_byte(g, (uint16_t)(at + 7)));
    set_event_saved_position(g, (uint16_t)(at + LENGTH - 1));
    m_jsr_dc(s, 0xC03DBD, 0xC0AF4E, DC_A | DC_X | DC_N | DC_Z | DC_C);
    if (!back(s, 0xC03DC0, false))
        return;
    event_return(s, (EventStep){ (uint16_t)(event_saved_position(g) + 1), false });
}

static EventFlow run_set_player_control(GameState *g, const EventArgs *a) { return set_player_control(g, a->arg[0]); }
static EventFlow run_fade(GameState *g, const EventArgs *a) { return fade(g, a->arg[0]); }
static EventFlow run_wait_for_screen(GameState *g, const EventArgs *a) { return wait_for_screen(g, a->arg[0]); }
static EventFlow run_tint(GameState *g, const EventArgs *a) { return tint(g, a->arg[0], a->arg[1]); }
static EventFlow run_set_bc(GameState *g, const EventArgs *a) { return set_bc(g, a->arg[0]); }
static EventFlow run_queue_tile_copy(GameState *g, const EventArgs *a)
{
    return queue_tile_copy(g, a->arg[0], a->arg[1], a->arg[2], a->arg[3]);
}
static EventFlow run_drift_layers(GameState *g, const EventArgs *a)
{
    return drift_layers(g, a->arg[0], a->arg[1], a->arg[2]);
}
static EventFlow run_scroll_camera(GameState *g, const EventArgs *a) { return scroll_camera(g, a->arg[0], a->arg[1]); }
static EventFlow run_set_animation_line_limit(GameState *g, const EventArgs *a)
{
    return set_animation_line_limit(g, a->arg[0]);
}

EVENT_OPCODES(event_screen_opcodes,
    { 0xE3, run_set_player_control, { BYTE(1) }, 2, .routine = 0xC03C05, .parse = 2 },
    { 0xF0, run_fade, { BYTE(1) }, 2, .routine = 0xC03CD0, .keeps = KEEPS_BYTE(0) },
    { 0xF2, run_wait_for_screen, { CONSTANT(FADING) }, 1, .routine = 0xC03CFD },
    { 0xF3, run_wait_for_screen, { CONSTANT(TINTING) }, 1, .routine = 0xC03D7A },
    { 0xF1, run_tint, { BYTE(1), BYTE(2) }, 3, .routine = 0xC03D07 },
    { 0xF4, run_set_bc, { BYTE(1) }, 2, .routine = 0xC03D84 },
    { 0xE5, run_queue_tile_copy, { WORD(1), WORD(3), WORD(5), BYTE(7) }, 8, .routine = 0xC03DC5 },
    { 0xE6, run_drift_layers, { WORD(1), BYTE(3), BYTE(4) }, 5, .routine = 0xC03DF4, .keeps = KEEPS_BYTE(1),
      .keeps_second = KEEPS_WORD(0) },
    { 0xE7, run_scroll_camera, { BYTE(1), BYTE(2) }, 3, .routine = 0xC03E30 },
    { 0x47, run_set_animation_line_limit, { BYTE(1) }, 2, .routine = 0xC06788, .parse = 2 });

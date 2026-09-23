/* Scripts directing the screen: fading it in and out, tinting it with a
 * colour (a flash, a red alert, a dream's haze), changing the map's tiles
 * (a door opening, a bridge appearing), drifting the layers, scrolling the
 * camera, shaking the screen, and handing the party to the player or taking
 * it away.
 *
 * The fades and tints are gradual when a rate is given: the field's
 * interrupt steps them a frame at a time toward their target, and marks them
 * running (field/field.h, field_effect_status) until they arrive. */
#include "event/bridge.h"
#include "field/field.h"

enum { ALL_MAPS = 0x07 };   /* a tile copy's maps: one bit each */

// $C0:3C05
/* Give the player control of the party (nonzero), or take it away for a
 * cutscene. */
static EventFlow set_player_control(GameState *g, uint8_t control)
{
    set_field_player_control(g, control);
    return next_command();
}
EVENT_OPERATION(set_player_control, control)

// $C0:3CD0
/* Fade the screen to a brightness (0 black to 15 full): at once, or at a
 * rate. Ends the turn. */
static EventFlow fade(GameState *g, uint8_t brightness, uint8_t rate)
{
    if (rate != 0) {
        set_field_brightness_rate(g, (uint16_t)(rate << 8 | rate));
        set_field_brightness_target(g, brightness);
        set_field_effect_status(g, field_effect_status(g) | FIELD_FADING);
    } else {
        set_field_brightness(g, brightness);
    }
    return end_turn();
}
EVENT_OPERATION(fade, brightness, rate)

// $C0:3CFD
/* Wait for a fade, or a tint, to arrive. */
static EventFlow wait_for_screen(GameState *g, uint8_t changes)
{
    return (field_effect_status(g) & changes) ? stay() : end_turn();
}
EVENT_OPERATION(wait_for_screen, changes)

/* A tint: a colour mixed into the whole picture, added to it or taken away.
 * The field keeps it as the hardware has it -- the fixed colour's shadow
 * (which colour channels in bits 5-7, the strength in bits 0-4) and the
 * colour-math setting it applies with -- and its interrupt hands both on each
 * frame; so for now the tint is written in those terms here, the field's
 * vocabulary for it still to come (docs/TODO.md). */
enum { CHANNELS = 0xE0, STRENGTH = 0x1F };
enum { NO_TINT = CHANNELS };   /* every channel, no strength */

/* The colour-math setting a tint applies with: over every layer and the
 * sprites, added or taken away. */
static uint8_t tint_math(TintMix mix) { return mix == TINT_ADDED ? 0x17 : 0x97; }

/* The tint's colour channels become the target's, its strength stays. The
 * original keeps the channels in its scratch cell. */
static void take_target_channels(GameState *g)
{
    uint8_t channels = field_fixed_color_target(g) & CHANNELS;
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, channels));
    set_field_fixed_color(g, (uint8_t)((field_fixed_color(g) & STRENGTH) | channels));
}

// $C0:3D07
/* Tint the screen with a colour, added to the picture or taken away from it:
 * at once, or at a rate, which ends the turn. No colour takes the tint away
 * and gives the picture back its own colour layers; that form of the command
 * has no rate, and is a byte shorter. */
static EventFlow tint(GameState *g, uint8_t colour, TintMix mix, uint8_t rate)
{
    if (colour == 0) {
        set_field_fixed_color_target(g, NO_TINT);
        set_field_fixed_color(g, NO_TINT);
        set_field_fixed_color_math_layers(g, field_color_math_layers(g));
        return next_command_after(2);
    }
    set_field_fixed_color_target(g, colour);
    take_target_channels(g);
    if (rate == 0) {
        set_field_fixed_color(g, field_fixed_color_target(g));
        set_field_fixed_color_math_layers(g, tint_math(mix));
        return next_command();
    }
    set_field_fixed_color_rate(g, (uint16_t)(rate << 8 | rate));
    set_field_fixed_color_math_layers(g, tint_math(mix));
    set_field_effect_status(g, field_effect_status(g) | FIELD_TINT_CHANGES);
    return end_turn();
}
EVENT_OPERATION(tint, colour, mix, rate)

// $C0:3D84
/* Shake the screen, or stop shaking it: a stop puts the layers' scroll back
 * where it was. (The shake is any nonzero value; the field keeps the
 * script's byte.) */
static EventFlow shake_screen(GameState *g, uint8_t shaking)
{
    set_field_screen_shake(g, shaking);
    if (shaking == 0) {
        set_field_scroll_offset_x(g, 0);
        set_field_scroll_offset_y(g, 0);
    }
    return next_command();
}
EVENT_OPERATION(shake_screen, shaking)

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
    set_field_tile_copy_queued_maps(g, field_tile_copy_maps(g) & ALL_MAPS);
    set_field_requests(g, field_requests(g) | FIELD_REQUEST_TILE_COPY);
    return end_turn();
}
EVENT_OPERATION(queue_tile_copy, from, to, destination, maps)

// $C0:3DF4
/* Drift the layers chosen (bits 0-2) at a speed (x in the low byte, y in the
 * high) for a number of frames: clouds, water, a quake. */
static EventFlow drift_layers(GameState *g, uint16_t drift, uint8_t layers, uint8_t frames)
{
    set_field_layer_extra_speed_time(g, frames);
    for (unsigned layer = 0; layer < FIELD_LAYER_EXTRA_SPEED_COUNT; layer++) {
        if (layers & 1u << layer) {
            set_field_layer_extra_speed_x(g, layer, (uint8_t)drift);
            set_field_layer_extra_speed_y(g, layer, (uint8_t)(drift >> 8));
            ORIGINAL_EFFECT(leave_y(drift));   /* the original loads Y with the speed to store it */
        }
    }
    return next_command();
}
EVENT_OPERATION(drift_layers, drift, layers, frames)

enum { SCROLL_IDLE = 0, SCROLL_RUNNING = 1 };

// $C0:3E30
/* Scroll the camera to a tile (in 16-pixel tiles), with the party out of the
 * player's hands, and wait until it gets there. */
static EventFlow scroll_camera(GameState *g, uint8_t scroll_x, uint8_t scroll_y)
{
    uint8_t state = field_screen_scroll_state(g);
    if (state == SCROLL_RUNNING)
        return stay();
    if (state != SCROLL_IDLE) {
        set_field_screen_scroll_state(g, SCROLL_IDLE);
        return next_command();
    }
    set_field_screen_scroll_target_x(g, (uint8_t)(scroll_x << 1));
    set_field_screen_scroll_target_y(g, (uint8_t)(scroll_y << 1));
    set_field_screen_scroll_state(g, SCROLL_RUNNING);
    set_field_player_control(g, 0);
    return stay();
}
EVENT_OPERATION(scroll_camera, scroll_x, scroll_y)

// $C0:6788
/* How far down the screen the objects' animations may be updated each frame
 * (the scanline the update stops at): a busy map trades animation for time. */
static EventFlow set_animation_line_limit(GameState *g, uint8_t scanline)
{
    set_field_animation_line_limit(g, (uint16_t)((field_animation_line_limit(g) & 0xFF00) | scanline));
    return next_command();
}
EVENT_OPERATION(set_animation_line_limit, scanline)

// $C0:3D97
/* Change the map's tiles now (the field's copy, $C0:AF4E, still the
 * original's code, called out to). Ends the turn. */
void event_cmd_copy_tiles(Snes *s)
{
    enum { LENGTH = 8 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, 0xE4);
    describe_tile_copy(g, c.from, c.to, c.destination, c.maps);
    /* the command's last byte, kept across the copy in event_saved_position:
     * the copy's indexed stores can land on it, and the command goes on from
     * what it holds after, as the original's does */
    event_keep_offset(g, (uint16_t)(at + LENGTH - 1));
    if (bridge_call(s, 0xC03DBD, 0xC0AF4E, DC_A | DC_X | DC_N | DC_Z | DC_C, event_machine(s, false)))
        event_return(s, (EventStep){ (uint16_t)(event_saved_position(g) + 1), false });
}

EVENT_OPCODES(event_screen_opcodes,
    { 0xE3, OPERATION(set_player_control), .control = OPERAND(1), .length = 2 },
    { 0xF0, OPERATION(fade), .brightness = OPERAND_BITS(1, 4, 0x0F), .rate = OPERAND_BITS(1, 0, 0x0F), .length = 2 },
    { 0xF2, OPERATION(wait_for_screen), .changes = CONSTANT(FIELD_FADING), .length = 1 },
    { 0xF3, OPERATION(wait_for_screen), .changes = CONSTANT(FIELD_TINTING), .length = 1 },
    { 0xF1, OPERATION(tint), .colour = OPERAND(1), .mix = OPERAND_BITS(2, 7, 0x01), .rate = OPERAND_BITS(2, 0, 0x7F),
      .length = 3 },
    { 0xF4, OPERATION(shake_screen), .shaking = OPERAND(1), .length = 2 },
    { 0xE5, OPERATION(queue_tile_copy), .from = OPERAND_WORD(1), .to = OPERAND_WORD(3),
      .destination = OPERAND_WORD(5), .maps = OPERAND(7), .length = 8 },
    { 0xE6, OPERATION(drift_layers), .drift = OPERAND_WORD(1), .layers = OPERAND(3), .frames = OPERAND(4),
      .length = 5 },
    { 0xE7, OPERATION(scroll_camera), .scroll_x = OPERAND(1), .scroll_y = OPERAND(2), .length = 3 },
    { 0x47, OPERATION(set_animation_line_limit), .scanline = OPERAND(1), .length = 2 },
    { 0xE4, .from = OPERAND_WORD(1), .to = OPERAND_WORD(3), .destination = OPERAND_WORD(5), .maps = OPERAND(7),
      .length = 8 });

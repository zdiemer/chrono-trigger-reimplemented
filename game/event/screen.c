/* Event commands: the screen -- brightness and the fixed color (which the
 * field NMI sends from their shadows every frame, stepping a gradual change
 * toward its target), copying map tiles, the layers' extra scroll speed and
 * scrolling the screen -- and the calls into the menus' party routines.
 *
 * field_effect_status marks the gradual changes running: bit 1 brightness,
 * bits 3 and 4 the fixed color. */
#include "event/bridge.h"

enum { BRIGHTNESS_CHANGING = 0x02, COLOR_CHANGING = 0x08, COLOR_CHANGES = 0x18 };
enum { TILE_COPY_QUEUED = 0x20 };   /* field_requests */

// $C0:3C05
/* $E3: give the player control of the party (operand 1 nonzero) or take it
 * away. */
static EventStep set_player_control(GameState *g, const EventCommand *c)
{
    set_field_player_control(g, operand(g, c, 1));
    return next_command(c, 2, 2);
}

// $C0:3CD0
/* $F0: brightness: operand 1's high nibble, at once, or with a nonzero low
 * nibble a gradual change to it at that rate. The operand is kept in the
 * scratch cell. Ends the turn. */
static EventStep brightness(GameState *g, const EventCommand *c)
{
    uint8_t op = operand(g, c, 1);
    keep8(g, SCRATCH_D9, op);
    uint8_t level = (uint8_t)(op >> 4), rate = op & 0x0F;
    if (rate != 0) {
        set_field_brightness_rate(g, (uint16_t)(rate << 8 | rate));
        set_field_brightness_target(g, level);
        set_field_effect_status(g, field_effect_status(g) | BRIGHTNESS_CHANGING);
    } else {
        set_field_brightness(g, level);
    }
    return end_turn(c, 2, 0);
}

/* Stay on the command while a gradual change runs. */
static EventStep wait_while(GameState *g, const EventCommand *c, uint8_t changing)
{
    return end_turn(c, (field_effect_status(g) & changing) ? 0 : 1, 0);
}

// $C0:3CFD
/* $F2: wait for a brightness change to finish. */
static EventStep wait_brightness(GameState *g, const EventCommand *c) { return wait_while(g, c, BRIGHTNESS_CHANGING); }

// $C0:3D7A
/* $F3: wait for a fixed color change to finish. */
static EventStep wait_color(GameState *g, const EventCommand *c) { return wait_while(g, c, COLOR_CHANGING); }

/* The fixed color takes the target's color channels (bits 5-7), keeping its
 * intensity; the channels are kept in the scratch cell. */
static void take_channels(GameState *g)
{
    uint8_t channels = field_fixed_color_target(g) & 0xE0;
    keep8(g, SCRATCH_D9, channels);
    set_field_fixed_color(g, (uint8_t)((field_fixed_color(g) & 0x1F) | channels));
}

enum { COLOR_MATH_ADD = 0x17, COLOR_MATH_SUBTRACT = 0x97, NO_COLOR = 0xE0 };

// $C0:3D07
/* $F1: the fixed color. Operand 1 zero: none, and the color math layers back
 * to the field's own. Otherwise operand 1 is the color, added (or with bit 7
 * of operand 2, subtracted) at once, or with operand 2's low bits nonzero
 * changed to gradually at that rate, which ends the turn. */
static EventStep fixed_color(GameState *g, const EventCommand *c)
{
    uint8_t color = operand(g, c, 1);
    if (color == 0) {
        set_field_fixed_color_target(g, NO_COLOR);
        set_field_fixed_color(g, NO_COLOR);
        set_field_fixed_color_math_layers(g, field_color_math_layers(g));
        return next_command(c, 2, 0);
    }
    set_field_fixed_color_target(g, color);
    take_channels(g);
    uint8_t rate = operand(g, c, 2);
    uint8_t math = rate & 0x80 ? COLOR_MATH_SUBTRACT : COLOR_MATH_ADD;
    rate &= 0x7F;
    if (rate == 0) {
        set_field_fixed_color(g, field_fixed_color_target(g));
        set_field_fixed_color_math_layers(g, math);
        return next_command(c, 3, 0);
    }
    set_field_fixed_color_rate(g, (uint16_t)(rate << 8 | rate));
    set_field_fixed_color_math_layers(g, math);
    take_channels(g);
    set_field_effect_status(g, field_effect_status(g) | COLOR_CHANGES);
    return end_turn(c, 3, 0);
}

// $C0:3D84
/* $F4: set field_setting_bc to operand 1; zero also clears the scroll offsets. */
static EventStep set_bc(GameState *g, const EventCommand *c)
{
    uint8_t v = operand(g, c, 1);
    set_field_setting_bc(g, v);
    if (v == 0) {
        set_field_scroll_offset_x(g, 0);
        set_field_scroll_offset_y(g, 0);
    }
    return next_command(c, 2, 0);
}

/* A tile copy's parameters, operands 1-7: the source rectangle's top left
 * and bottom right tiles, the destination's top left, and which maps. */
static void load_tile_copy(GameState *g, const EventCommand *c)
{
    set_field_tile_copy_from(g, operand_word(g, c, 1));
    set_field_tile_copy_to(g, operand_word(g, c, 3));
    set_field_tile_copy_destination(g, operand_word(g, c, 5));
    set_field_tile_copy_maps(g, operand(g, c, 7));
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
/* $E4: copy map tiles now ($C0:AF4E, called out to). Ends the turn. */
void event_cmd_copy_tiles(Snes *s)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_at(s);
    load_tile_copy(g, &c);
    set_event_saved_position(g, (uint16_t)(c.at + 7));
    m_jsr_dc(s, 0xC03DBD, 0xC0AF4E, DC_A | DC_X | DC_N | DC_Z | DC_C);
    if (!back(s, 0xC03DC0, false))
        return;
    event_return(s, step_at((uint16_t)(event_saved_position(g) + 1), false, s->cpu.y));
}

// $C0:3DC5
/* $E5: queue the same tile copy for the field main loop, for the maps in the
 * low 3 bits. Ends the turn. */
static EventStep queue_tile_copy(GameState *g, const EventCommand *c)
{
    load_tile_copy(g, c);
    set_field_tile_copy_queued_maps(g, field_tile_copy_maps(g) & 0x07);
    set_field_requests(g, field_requests(g) | TILE_COPY_QUEUED);
    return end_turn(c, 8, 0);
}

// $C0:3DF4
/* $E6: set the extra scroll speed (operands 1-2, x and y) of the layers in
 * operand 3's bits 0-2, for operand 4 frames. The speed and the layers are
 * kept in the scratch cells; the parse position is left holding the speed
 * when any layer is set. */
static EventStep set_layer_extra_speed(GameState *g, const EventCommand *c)
{
    keep16(g, SCRATCH_DB, operand_word(g, c, 1));
    keep8(g, SCRATCH_D9, operand(g, c, 3));
    set_field_layer_extra_speed_time(g, operand(g, c, 4));
    uint16_t speed = scratch16(g, SCRATCH_DB);
    uint8_t layers = scratch8(g, SCRATCH_D9);
    EventStep st = next_command(c, 5, 0);
    for (unsigned layer = 0; layer < FIELD_LAYER_EXTRA_SPEED_COUNT; layer++) {
        if (layers & 1u << layer) {
            set_field_layer_extra_speed_x(g, layer, (uint8_t)speed);
            set_field_layer_extra_speed_y(g, layer, (uint8_t)(speed >> 8));
            st.cursor = speed;
        }
    }
    return st;
}

enum { SCROLL_IDLE = 0, SCROLL_RUNNING = 1 };

// $C0:3E30
/* $E7: scroll the screen to the tile at operands 1-2 (16-pixel tiles). The
 * first run starts the scroll (taking the player's control away), and the
 * command stays until the field has finished it. */
static EventStep scroll_screen(GameState *g, const EventCommand *c)
{
    uint8_t state = field_screen_scroll_state(g);
    if (state == SCROLL_RUNNING)
        return wait_here(c, 0);
    if (state != SCROLL_IDLE) {
        set_field_screen_scroll_state(g, SCROLL_IDLE);
        return next_command(c, 3, 0);
    }
    set_field_screen_scroll_target_x(g, (uint8_t)(operand(g, c, 1) << 1));
    set_field_screen_scroll_target_y(g, (uint8_t)(operand(g, c, 2) << 1));
    set_field_screen_scroll_state(g, (uint8_t)(state + 1));
    set_field_player_control(g, 0);
    return wait_here(c, 0);
}

/* The menus' party routines at $C2:8004 (A the function, X and Y its
 * arguments; 6 and 7 run as $F8-$FA ask, and others through $D5), called out
 * to. The command goes on at the offset it kept in event_saved_position, with
 * what the routine leaves in Y and the carry. */
static bool call_party_routine(Snes *s, uint32_t site, uint8_t function)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | function);
    m_set_nz8(s, function);
    m_jsl(s, site, 0xC28004);
    return back(s, site + 4, false);
}

static void party_routines(Snes *s, uint32_t first_site, uint8_t first, bool then_7)
{
    GameState *g = state_of(s);
    s->cpu.y++;
    set_event_saved_position(g, s->cpu.y);
    if (!call_party_routine(s, first_site, first))
        return;
    if (then_7 && !call_party_routine(s, 0xC03E67, 7))
        return;
    event_return(s, step_at(event_saved_position(g), m_flag(s, CPU_FLAG_C), s->cpu.y));
}

// $C0:3E5C
/* $F8: party routines 6 and 7. */
void event_cmd_party_routine_6_7(Snes *s) { party_routines(s, 0xC03E61, 6, true); }

// $C0:3E6E
/* $F9: party routine 6. */
void event_cmd_party_routine_6(Snes *s) { party_routines(s, 0xC03E67, 6, false); }

// $C0:3E75
/* $FA: party routine 7. */
void event_cmd_party_routine_7(Snes *s) { party_routines(s, 0xC03E67, 7, false); }

EVENT_COMMAND(event_cmd_set_player_control, set_player_control)
EVENT_COMMAND(event_cmd_brightness, brightness)
EVENT_COMMAND(event_cmd_wait_brightness, wait_brightness)
EVENT_COMMAND(event_cmd_wait_color, wait_color)
EVENT_COMMAND(event_cmd_fixed_color, fixed_color)
EVENT_COMMAND(event_cmd_set_bc, set_bc)
EVENT_COMMAND(event_cmd_queue_tile_copy, queue_tile_copy)
EVENT_COMMAND(event_cmd_set_layer_extra_speed, set_layer_extra_speed)
EVENT_COMMAND(event_cmd_scroll_screen, scroll_screen)

/* Scripts moving the party between places, and into battle.
 *
 * A location is a map and where the party stands on it, with how the change
 * is made (bits of the word that names the map). The game keeps three: where
 * the party is, where it goes next, and one a script puts aside for later.
 * Setting the next one and raising the field's request makes the change; the
 * field main loop carries it out, and the script waits on it meanwhile. A
 * battle is requested the same way, and the script waits until it is over. */
#include "event/bridge.h"
#include "event/variables.h"
#include "field/field.h"

enum { FULL_BRIGHTNESS = 0x0F };

/* A location as a script writes it (field/field.h): the map and how in a
 * word, and the tile in a second word. */
static Location location_of(uint16_t map_and_how, uint16_t tile)
{
    return location_from(map_and_how, (uint8_t)tile, (uint8_t)(tile >> 8));
}

static void set_location_as(GameState *g, LocationRecord record, Location l)
{
    static const struct {
        void (*map)(GameState *, uint16_t);
        void (*x)(GameState *, uint8_t);
        void (*y)(GameState *, uint8_t);
        void (*how)(GameState *, uint8_t);
    } kept[] = {
        [WHERE_THE_PARTY_IS] = { set_location_id, set_location_x, set_location_y, set_location_flags },
        [WHERE_IT_GOES_NEXT] = { set_location_next_id, set_location_next_x, set_location_next_y,
                                 set_location_next_flags },
        [PUT_ASIDE] = { set_location_stored_id, set_location_stored_x, set_location_stored_y,
                        set_location_stored_flags },
    };
    kept[record].map(g, l.map);
    kept[record].x(g, l.x);
    kept[record].y(g, l.y);
    kept[record].how(g, l.how);
}

/* Ask the field for the change, at full brightness. */
static void request_change(GameState *g)
{
    set_field_requests(g, field_requests(g) | FIELD_REQUEST_LOCATION_CHANGE);
    set_field_brightness(g, FULL_BRIGHTNESS);
}

static bool change_pending(const GameState *g) { return field_requests(g) & FIELD_REQUEST_LOCATION_CHANGE; }

/* $DE and $DF also raise field_setting_1e with the location they set. What
 * the field does with it isn't established (docs/TODO.md). */
static void raise_setting_1e(GameState *g) { set_field_setting_1e(g, 1); }

// $C0:3AB7
/* Go to another place: the script stays on the command, which the change
 * takes over. Nothing new is asked while a change is already coming. */
static EventFlow go_to_location(GameState *g, uint16_t map_and_how, uint16_t tile)
{
    if (!change_pending(g)) {
        ORIGINAL_EFFECT(keep_word(g, KEPT_FIRST, map_and_how));
        set_location_as(g, WHERE_IT_GOES_NEXT, location_of(map_and_how, tile));
        request_change(g);
    }
    return stay();
}
EVENT_OPERATION(go_to_location, map_and_how, tile)

// $C0:3B9E
/* Go to a place the script has worked out in variables: the map (with how,
 * as a whole word) and the tile and how in bytes. */
static EventFlow go_to_worked_out_location(GameState *g, uint16_t map, uint8_t tile_x, uint8_t tile_y, uint8_t how)
{
    if (change_pending(g))
        return stay();
    set_location_as(g, WHERE_IT_GOES_NEXT, (Location){ map, tile_x, tile_y, how });
    request_change(g);
    return stay();
}
EVENT_OPERATION(go_to_worked_out_location, map, tile_x, tile_y, how)

// $C0:3AF1
/* Set where the party is, or put a place aside for later (the map's word is
 * kept in the scratch cell, by event/leaves.c). */
static EventFlow set_location(GameState *g, LocationRecord record, uint16_t map_and_how, uint16_t tile, bool raise_1e)
{
    if (raise_1e)
        raise_setting_1e(g);
    set_location_as(g, record, location_of(map_and_how, tile));
    return next_command();
}
EVENT_OPERATION(set_location, record, map_and_how, tile, raise_1e)

// $C0:3A83
/* Start a battle, and wait until it is over: the first run asks the field for
 * it (taking the party out of the player's hands) and stays; once the battle
 * is over the next run goes on. `setup` is the battle's own flags, `terms`
 * what the event hands it (whether the field's song plays on). The original
 * asks for a tile copy with the battle (field/field.h). */
static EventFlow fight(GameState *g, uint8_t setup, uint8_t terms)
{
    if (field_requests(g) & FIELD_REQUEST_BATTLE)
        return stay();
    if (field_effect_status(g) & FIELD_BATTLE_OVER) {
        set_field_effect_status(g, field_effect_status(g) & ~FIELD_BATTLE_OVER);
        return end_turn();
    }
    set_battle_setup_flags(g, setup);
    set_battle_event_flags(g, terms);
    set_field_requests(g, field_requests(g) | FIELD_REQUEST_BATTLE | FIELD_REQUEST_TILE_COPY);
    set_field_player_control(g, 0);
    return stay();
}
EVENT_OPERATION(fight, setup, terms)

// $C0:3B4B
/* Go to another place at once. The rest -- waiting for vblank, blanking the
 * screen, keeping the location left and restarting the field at $C0:0000
 * with a fresh stack -- is the original's, from $C0:3B71, where nothing the
 * port could leave in the registers is read. */
static void go_now(Snes *s, uint8_t opcode)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_of(s, opcode);
    ORIGINAL_EFFECT(keep_word(g, KEPT_FIRST, c.map_and_how));
    set_location_as(g, WHERE_IT_GOES_NEXT, location_of(c.map_and_how, c.tile));
    m_stop_at_own(s, 0xC03B71, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

void event_cmd_go_to_location_now(Snes *s) { go_now(s, 0xE1); }

// $C0:3B98
/* The same, raising field_setting_1e. */
void event_cmd_go_to_location_now_raising_1e(Snes *s)
{
    raise_setting_1e(state_of(s));
    go_now(s, 0xDF);
}

#define A_LOCATION .map_and_how = OPERAND_WORD(1), .tile = OPERAND_WORD(3)

EVENT_OPCODES(event_locations_opcodes,
    { 0xE0, OPERATION(go_to_location), A_LOCATION, .length = 5 },
    { 0xE2, OPERATION(go_to_worked_out_location), .map = VARIABLE_WORD(1), .tile_x = VARIABLE_BYTE(2),
      .tile_y = VARIABLE_BYTE(3), .how = VARIABLE_BYTE(4), .length = 5 },
    { 0xDC, OPERATION(set_location), A_LOCATION, .record = CONSTANT(PUT_ASIDE), .length = 5 },
    { 0xDD, OPERATION(set_location), A_LOCATION, .record = CONSTANT(WHERE_THE_PARTY_IS), .length = 5 },
    { 0xDE, OPERATION(set_location), A_LOCATION, .record = CONSTANT(WHERE_THE_PARTY_IS),
      .raise_1e = CONSTANT(true), .length = 5 },
    { 0xD8, OPERATION(fight), .setup = OPERAND(1), .terms = OPERAND(2), .length = 3 },
    { 0xE1, A_LOCATION, .length = 5 },
    { 0xDF, A_LOCATION, .length = 5 });

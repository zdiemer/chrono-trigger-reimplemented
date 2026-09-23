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

enum { LOCATION_CHANGE = 0x80, BATTLE = 0x01, BATTLE_STARTING = 0x21 };   /* field_requests */
enum { BATTLE_OVER = 0x01 };                                              /* field_effect_status */
enum { FULL_BRIGHTNESS = 0x0F };

typedef enum { WHERE_THE_PARTY_IS, WHERE_IT_GOES_NEXT, PUT_ASIDE } Which;

/* A location as a script writes it: the map in the low 9 bits of a word whose
 * high byte says how the change is made, and the tile in a second word. The
 * game keeps the how in a byte of its own: bits 1-4 shifted down, and bit 7. */
typedef struct {
    uint16_t map;
    uint8_t x, y;
    uint8_t how;
} Location;

enum { MAP_BITS = 0x01FF };

static Location location_of(uint16_t map_and_how, uint16_t tile)
{
    uint8_t high = (uint8_t)(map_and_how >> 8);
    return (Location){ map_and_how & MAP_BITS, (uint8_t)tile, (uint8_t)(tile >> 8),
                       (uint8_t)(((high >> 1) & 0x0F) | (high & 0x80)) };
}

static void set_location_as(GameState *g, Which which, Location l)
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
    kept[which].map(g, l.map);
    kept[which].x(g, l.x);
    kept[which].y(g, l.y);
    kept[which].how(g, l.how);
}

/* Ask the field for the change, at full brightness. */
static void request_change(GameState *g)
{
    set_field_requests(g, field_requests(g) | LOCATION_CHANGE);
    set_field_brightness(g, FULL_BRIGHTNESS);
}

static bool change_pending(const GameState *g) { return field_requests(g) & LOCATION_CHANGE; }

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

// $C0:3B9E
/* Go to a place the script has worked out in variables: the map (with how,
 * as a whole word) and the tile and how in bytes. */
static EventFlow go_to_worked_out_location(GameState *g, uint16_t map, uint8_t x, uint8_t y, uint8_t how)
{
    if (change_pending(g))
        return stay();
    ORIGINAL_EFFECT(keep_resume_point(g, 0));
    set_location_next_id(g, map);
    set_location_next_x(g, x);
    set_location_next_y(g, y);
    set_location_next_flags(g, how);
    request_change(g);
    ORIGINAL_EFFECT(leave_parse_at(4));
    return stay();
}

// $C0:3AF1
/* Set where the party is, or put a place aside for later (the map's word is
 * kept in the scratch cell, a column of the rows). */
static EventFlow set_location(GameState *g, Which which, uint16_t map_and_how, uint16_t tile, bool raise_1e)
{
    if (raise_1e)
        raise_setting_1e(g);
    set_location_as(g, which, location_of(map_and_how, tile));
    return next_command();
}

// $C0:3A83
/* Start a battle, and wait until it is over: the first run asks the field for
 * it (taking the party out of the player's hands) and stays; once the battle
 * is over the next run goes on. `setup` is the battle's own flags, `terms`
 * what the event hands it (whether the field's song plays on). */
static EventFlow fight(GameState *g, uint8_t setup, uint8_t terms)
{
    if (field_requests(g) & BATTLE)
        return stay();
    if (field_effect_status(g) & BATTLE_OVER) {
        set_field_effect_status(g, field_effect_status(g) & ~BATTLE_OVER);
        return end_turn();
    }
    set_battle_setup_flags(g, setup);
    set_battle_event_flags(g, terms);
    set_field_requests(g, field_requests(g) | BATTLE_STARTING);
    set_field_player_control(g, 0);
    return stay();
}

// $C0:3B4B
/* Go to another place at once. The rest -- waiting for vblank, blanking the
 * screen, keeping the location left and restarting the field at $C0:0000
 * with a fresh stack -- is the original's, from $C0:3B71, where nothing the
 * port could leave in the registers is read. */
static void go_now(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint16_t map_and_how = script_word(g, (uint16_t)(at + 1));
    ORIGINAL_EFFECT(keep_word(g, KEPT_FIRST, map_and_how));
    set_location_as(g, WHERE_IT_GOES_NEXT, location_of(map_and_how, script_word(g, (uint16_t)(at + 3))));
    m_stop_at(s, 0xC03B71, DC_A | DC_X | DC_Y | DC_V | DC_C);
}

void event_cmd_change_location_now(Snes *s) { go_now(s); }

// $C0:3B98
void event_cmd_change_location_now_with_1e(Snes *s)
{
    raise_setting_1e(state_of(s));
    go_now(s);
}

static EventFlow run_go_to_location(GameState *g, const EventArgs *a) { return go_to_location(g, a->arg[0], a->arg[1]); }
static EventFlow run_go_to_worked_out_location(GameState *g, const EventArgs *a)
{
    return go_to_worked_out_location(g, a->arg[0], a->arg[1], a->arg[2], a->arg[3]);
}
static EventFlow run_set_location(GameState *g, const EventArgs *a)
{
    return set_location(g, a->arg[2], a->arg[0], a->arg[1], a->arg[3]);
}
static EventFlow run_fight(GameState *g, const EventArgs *a) { return fight(g, a->arg[0], a->arg[1]); }

EVENT_OPCODES(event_locations_opcodes,
    { 0xE0, run_go_to_location, { WORD(1), WORD(3) }, 5, .routine = 0xC03AB7 },
    { 0xE2, run_go_to_worked_out_location, { VARIABLE_WORD(1), VARIABLE_BYTE(2), VARIABLE_BYTE(3), VARIABLE_BYTE(4) },
      5, .routine = 0xC03B9E },
    { 0xDC, run_set_location, { WORD(1), WORD(3), CONSTANT(PUT_ASIDE), CONSTANT(false) }, 5, .routine = 0xC03AF1,
      .keeps = KEEPS_WORD(0) },
    { 0xDD, run_set_location, { WORD(1), WORD(3), CONSTANT(WHERE_THE_PARTY_IS), CONSTANT(false) }, 5,
      .routine = 0xC03B1B, .keeps = KEEPS_WORD(0) },
    { 0xDE, run_set_location, { WORD(1), WORD(3), CONSTANT(WHERE_THE_PARTY_IS), CONSTANT(true) }, 5,
      .routine = 0xC03B45, .keeps = KEEPS_WORD(0) },
    { 0xD8, run_fight, { BYTE(1), BYTE(2) }, 3, .routine = 0xC03A83 });

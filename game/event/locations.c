/* Event commands: changing location, starting battles, and the party.
 *
 * A location is kept as a triple: the location's number (the low 9 bits of
 * a word, whose high byte also carries how the change is made), where the
 * party stands in it, and a byte made from that high byte: its bits 1-4
 * shifted down, and its bit 7. The field main loop makes a change once bit 7
 * of field_requests is set. */
#include "event/bridge.h"

enum { LOCATION_CHANGE = 0x80, BATTLE = 0x01, BATTLE_STARTING = 0x21 };   /* field_requests */
enum { BATTLE_OVER = 0x01 };                                              /* field_effect_status */
enum { FULL_BRIGHTNESS = 0x0F };

/* The three triples the commands set: the current location, the next one
 * and the one $DC keeps. */
enum { CURRENT, NEXT, STORED };
static const struct {
    void (*id)(GameState *, uint16_t);
    void (*x)(GameState *, uint8_t);
    void (*y)(GameState *, uint8_t);
    void (*flags)(GameState *, uint8_t);
} triples[] = {
    [CURRENT] = { set_location_id, set_location_x, set_location_y, set_location_flags },
    [NEXT] = { set_location_next_id, set_location_next_x, set_location_next_y, set_location_next_flags },
    [STORED] = { set_location_stored_id, set_location_stored_x, set_location_stored_y, set_location_stored_flags },
};

/* Set a triple from operands 1-4, in the original's order: the location word
 * (kept in the scratch cell, its low 9 bits the location), then where the
 * party stands, then the flags from the word's high byte as kept. */
static void set_triple(GameState *g, const EventCommand *c, unsigned which)
{
    uint16_t word = operand_word(g, c, 1);
    keep16(g, SCRATCH_D9, word);
    triples[which].id(g, word & 0x01FF);
    uint16_t position = operand_word(g, c, 3);
    triples[which].x(g, (uint8_t)position);
    triples[which].y(g, (uint8_t)(position >> 8));
    uint8_t high = scratch8(g, SCRATCH_DA);
    triples[which].flags(g, (uint8_t)(((high >> 1) & 0x0F) | (high & 0x80)));
}

/* Start the change, at full brightness. */
static void request_change(GameState *g)
{
    set_field_requests(g, field_requests(g) | LOCATION_CHANGE);
    set_field_brightness(g, FULL_BRIGHTNESS);
}

static bool change_pending(const GameState *g) { return field_requests(g) & LOCATION_CHANGE; }

// $C0:3AB7
/* $E0: change location to operands 1-4, unless a change is pending. Stays on
 * the command, which the change takes over. */
static EventStep change_location(GameState *g, const EventCommand *c)
{
    if (!change_pending(g)) {
        set_triple(g, c, NEXT);
        request_change(g);
    }
    return wait_here(c, 0);
}

// $C0:3B9E
/* $E2: change location to the one in variables: the 16-bit variable operand 1
 * holds the location word, operands 2-4 name the variables with the rest.
 * The command's offset is kept in event_saved_position. */
static EventStep change_location_from_variables(GameState *g, const EventCommand *c)
{
    if (change_pending(g))
        return wait_here(c, 0);
    set_event_saved_position(g, c->at);
    set_location_next_id(g, var16(g, variable_operand(g, c, 1)));
    set_location_next_x(g, var8(g, variable_operand(g, c, 2)));
    set_location_next_y(g, var8(g, variable_operand(g, c, 3)));
    set_location_next_flags(g, var8(g, variable_operand(g, c, 4)));
    request_change(g);
    return wait_here(c, 4);
}

// $C0:3AF1
/* $DC: keep a location (operands 1-4) in location_stored, and go on. */
static EventStep store_location(GameState *g, const EventCommand *c)
{
    set_triple(g, c, STORED);
    return next_command(c, 5, 0);
}

// $C0:3B1B
/* $DD: set the current location (operands 1-4), and go on. */
static EventStep set_location(GameState *g, const EventCommand *c)
{
    set_triple(g, c, CURRENT);
    return next_command(c, 5, 0);
}

// $C0:3B45
/* $DE: set field_setting_1e, then as $DD. */
static EventStep set_location_with_1e(GameState *g, const EventCommand *c)
{
    set_field_setting_1e(g, 1);
    return set_location(g, c);
}

// $C0:3B4B
/* $E1: change location to operands 1-4 at once. The rest -- waiting for
 * vblank, blanking the screen, keeping the location left and restarting the
 * field at $C0:0000 with a fresh stack -- is the original's, from $C0:3B71,
 * where nothing the port could leave in the registers is read. */
static void change_location_now(Snes *s)
{
    EventCommand c = event_command_at(s);
    set_triple(state_of(s), &c, NEXT);
    m_stop_at(s, 0xC03B71, DC_A | DC_X | DC_Y | DC_V | DC_C);
}

void event_cmd_change_location_now(Snes *s) { change_location_now(s); }

// $C0:3B98
/* $DF: set field_setting_1e, then as $E1. */
void event_cmd_change_location_now_with_1e(Snes *s)
{
    set_field_setting_1e(state_of(s), 1);
    change_location_now(s);
}

// $C0:3A83
/* $D8: start a battle, with operand 1 its setup flags and operand 2 the
 * flags the battle takes from the event. The first run starts it (taking the
 * player's control away) and stays on the command; once the battle is over,
 * the next run clears the mark and goes on. Nothing happens while a battle is
 * still being started. */
static EventStep start_battle(GameState *g, const EventCommand *c)
{
    if (field_requests(g) & BATTLE)
        return wait_here(c, 0);
    if (field_effect_status(g) & BATTLE_OVER) {
        set_field_effect_status(g, field_effect_status(g) & ~BATTLE_OVER);
        return end_turn(c, 3, 0);
    }
    set_battle_setup_flags(g, operand(g, c, 1));
    set_battle_event_flags(g, operand(g, c, 2));
    set_field_requests(g, field_requests(g) | BATTLE_STARTING);
    set_field_player_control(g, 0);
    return wait_here(c, 0);
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

// $C0:3A68
/* $D5: the menus' party routine $0A, with operand 1 in Y and operand 2 in X
 * (called out to). Goes on at the offset kept in event_saved_position, with
 * what the routine leaves in Y and the carry. */
void event_cmd_party_routine_0a(Snes *s)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_at(s);
    set_event_saved_position(g, (uint16_t)(c.at + 3));
    event_set_registers(s, 0x000A, operand(g, &c, 2), operand(g, &c, 1));
    m_set_nz8(s, 0x0A);
    m_jsl(s, 0xC03A7C, 0xC28004);
    if (!back(s, 0xC03A80, false))
        return;
    event_return(s, step_at(event_saved_position(g), m_flag(s, CPU_FLAG_C), s->cpu.y));
}

// $C0:3A07
/* $D6: take character (operand 1) out of the party: the first party member
 * whose object is that character leaves (its state 3, its place in the party
 * empty). The character is kept in the scratch cell and the command's end in
 * event_saved_position. The step goes on as the last comparison of characters
 * found (whether the member's was at least the one asked for); `compared`
 * says whether there was one. */
static EventStep remove_from_party(GameState *g, const EventCommand *c, bool *compared)
{
    enum { NO_MEMBER = 0x80, LEFT_THE_PARTY = 3 };
    uint8_t character = operand(g, c, 1);
    set_event_saved_position(g, (uint16_t)(c->at + 2));
    keep8(g, SCRATCH_D9, character);
    EventStep st = step_at(event_saved_position(g), false, c->at);
    *compared = false;
    for (unsigned member = 0; member < FIELD_PARTY_OBJECTS_COUNT; member++) {
        uint8_t object = field_party_objects(g, member);
        if (object & 0x80)
            continue;
        uint8_t is = field_object_state_character(g, slot_of(object));
        *compared = true;
        st.go_on = is >= character;
        if (is != character)
            continue;
        set_field_object_state_state(g, slot_of(object), LEFT_THE_PARTY);
        set_field_party_objects(g, member, NO_MEMBER);
        set_party_members(g, member, NO_MEMBER);
        break;
    }
    return st;
}

/* When no member was compared, the carry is the one the command was entered
 * with, which the original leaves as it is (clear, from the dispatcher's
 * shift of the command byte into its table index). */
void event_cmd_remove_from_party(Snes *s)
{
    EventCommand c = event_command_at(s);
    bool compared;
    EventStep st = remove_from_party(state_of(s), &c, &compared);
    if (!compared)
        st.go_on = m_flag(s, CPU_FLAG_C);
    event_return(s, st);
}

EVENT_COMMAND(event_cmd_change_location, change_location)
EVENT_COMMAND(event_cmd_change_location_from_variables, change_location_from_variables)
EVENT_COMMAND(event_cmd_store_location, store_location)
EVENT_COMMAND(event_cmd_set_location, set_location)
EVENT_COMMAND(event_cmd_set_location_with_1e, set_location_with_1e)
EVENT_COMMAND(event_cmd_start_battle, start_battle)

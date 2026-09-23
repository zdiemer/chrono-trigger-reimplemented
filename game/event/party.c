/* Scripts and the party (event/party.h): who leads it, someone leaving it,
 * and the menus' party routines, which the scripts call on (what routines 6,
 * 7 and $0A do for the party isn't established; docs/TODO.md).
 *
 * Leaving and the menus' routines are bridges: the first because what it
 * hands back is the original's carry (below), the others because the menus'
 * routines ($C2:8004) are still the original's code. */
#include "event/bridge.h"
#include "event/party.h"
#include "event/variables.h"

// $C0:658F
/* A variable learns who leads the party. */
static EventFlow note_leader(GameState *g, const EventCommand *c)
{
    set_variable_byte(g, c->variable, party_leader(g));
    return next_command();
}

enum { LEFT_THE_PARTY = 3 };   /* the object's state once its character has left */

// $C0:3A07
/* A character leaves the party: the first member who is that character
 * leaves the map and their place empties. Returns the place, or PARTY_PLACES
 * when the character wasn't in the party. */
static PartyPlace take_out_of_party(GameState *g, uint8_t character)
{
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place))
            continue;
        EventObject object = member_object(g, place);
        if (field_object_state_character(g, object) != character)
            continue;
        set_field_object_state_state(g, object, LEFT_THE_PARTY);
        empty_place(g, place);
        return place;
    }
    return PARTY_PLACES;
}

/* The character of the last member the original compared with the one
 * leaving: the last place taken, when it didn't find them. */
static bool last_member_at_least(const GameState *g, uint8_t character, bool *any)
{
    bool at_least = false;
    *any = false;
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place))
            continue;
        *any = true;
        at_least = field_object_state_character(g, member_object(g, place)) >= character;
    }
    return at_least;
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

/* The original keeps where the command goes on in event_saved_position, and
 * reads it back itself. */
static void keep_offset_across_calls(GameState *g, uint16_t at) { set_event_saved_position(g, at); }

/* $D6's entry. Whether the object's turn goes on is the carry the original's
 * search leaves: that of its last comparison of a member's character with
 * the one leaving (set when it found them, and when the last member it
 * looked at was at least that character), or, with no member to compare, the
 * carry the command was entered with. It means nothing a script asks for;
 * the engine goes on by it, so it is kept. */
void event_cmd_take_out_of_party(Snes *s)
{
    enum { LENGTH = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t character = event_command_of(s, 0xD6).character;
    ORIGINAL_EFFECT(keep_offset_across_calls(g, (uint16_t)(at + LENGTH)));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, character));
    bool any, before_or_found = last_member_at_least(g, character, &any);
    bool found = take_out_of_party(g, character) != PARTY_PLACES;
    bool carry = found || (any ? before_or_found : m_flag(s, CPU_FLAG_C));
    event_return(s, (EventStep){ (uint16_t)(at + LENGTH), carry });
}

/* The menus' party routines ($C2:8004): the routine in A, its arguments in X
 * and Y, which the menus' dispatcher ($C2:8C36) takes as they come. The
 * script goes on after the command, with the carry the routine leaves. */
static bool call_party_routine(Snes *s, uint32_t site, uint8_t routine)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | routine);
    m_set_nz8(s, routine);
    m_jsl(s, site, 0xC28004);
    return back(s, site + 4, false);
}

// $C0:3A68
/* Routine $0A, with the command's first operand in Y and its second in X. */
void event_cmd_party_routine_0a(Snes *s)
{
    enum { ROUTINE = 0x0A, LENGTH = 3 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint16_t resume = (uint16_t)(at + LENGTH);
    ORIGINAL_EFFECT(keep_offset_across_calls(g, resume));
    EventCommand c = event_command_of(s, 0xD5);
    event_set_registers(s, 0, c.second_argument, c.first_argument);
    if (call_party_routine(s, 0xC03A7C, ROUTINE))
        event_return(s, (EventStep){ resume, m_flag(s, CPU_FLAG_C) });
}

/* Routines 6 and 7 take no arguments; X is the dispatch's as it came, and Y
 * the command's offset past its opcode, which the dispatcher stores
 * regardless. */
static void party_routines(Snes *s, uint32_t first_site, uint8_t first, bool then_7)
{
    uint16_t resume = (uint16_t)(event_command_offset(s) + 1);
    s->cpu.y = resume;
    ORIGINAL_EFFECT(keep_offset_across_calls(state_of(s), resume));
    if (!call_party_routine(s, first_site, first))
        return;
    if (then_7 && !call_party_routine(s, 0xC03E67, 7))
        return;
    event_return(s, (EventStep){ resume, m_flag(s, CPU_FLAG_C) });
}

// $C0:3E5C
/* The menus' party routines 6, then 7. */
void event_cmd_party_routine_6_7(Snes *s) { party_routines(s, 0xC03E61, 6, true); }

// $C0:3E6E
void event_cmd_party_routine_6(Snes *s) { party_routines(s, 0xC03E67, 6, false); }

// $C0:3E75
void event_cmd_party_routine_7(Snes *s) { party_routines(s, 0xC03E67, 7, false); }

/* $D6's guard: every party member's object is one the port reaches, or there
 * is nobody in the place. */
static bool members_are_objects(const GameState *g, const EventCommand *c)
{
    (void)c;
    for (PartyPlace place = 0; place < PARTY_PLACES; place++)
        if (place_taken(g, place) && (field_party_objects(g, place) & 1))
            return false;
    return true;
}

EVENT_OPCODES(event_party_opcodes,
    { 0x20, note_leader, .variable = VARIABLE(1), .length = 2, .routine = 0xC0658F },
    { 0xD6, .bridge = event_cmd_take_out_of_party, .character = OPERAND(1), .length = 2,
      .accepts = members_are_objects,
      .routine = 0xC03A07 },
    { 0xD5, .bridge = event_cmd_party_routine_0a, .first_argument = OPERAND(1), .second_argument = OPERAND(2),
      .length = 3, .routine = 0xC03A68 },
    { 0xF8, .bridge = event_cmd_party_routine_6_7, .length = 1, .routine = 0xC03E5C },
    { 0xF9, .bridge = event_cmd_party_routine_6, .length = 1, .routine = 0xC03E6E },
    { 0xFA, .bridge = event_cmd_party_routine_7, .length = 1, .routine = 0xC03E75 });

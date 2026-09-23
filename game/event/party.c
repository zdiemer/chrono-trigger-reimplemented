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
static EventFlow note_leader(GameState *g, EventVariable v)
{
    set_variable_byte(g, v, party_leader(g));
    return next_command();
}

static EventFlow run_note_leader(GameState *g, const EventArgs *a) { return note_leader(g, a->arg[0]); }

EVENT_OPCODES(event_party_opcodes, { 0x20, run_note_leader, { VARIABLE(1) }, 2, .routine = 0xC0658F, .parse = 2 });

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
/* The menus' party routine $0A, with the command's first operand in Y and
 * its second in X. The script goes on after the command, with what the
 * routine leaves in Y and the carry. */
void event_cmd_party_routine_0a(Snes *s)
{
    enum { PARTY_ROUTINE = 0x0A, LENGTH = 3 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    set_event_saved_position(g, (uint16_t)(at + LENGTH));
    event_set_registers(s, PARTY_ROUTINE, script_byte(g, (uint16_t)(at + 2)), script_byte(g, (uint16_t)(at + 1)));
    m_set_nz8(s, PARTY_ROUTINE);
    m_jsl(s, 0xC03A7C, 0xC28004);
    if (!back(s, 0xC03A80, false))
        return;
    event_return(s, (EventStep){ event_saved_position(g), m_flag(s, CPU_FLAG_C) });
}

// $C0:3A07
/* A character leaves the party: the first member who is that character
 * leaves the map (their object's state 3) and their place empties. (The
 * original keeps the character in its scratch cell, and where it goes on in
 * event_saved_position, which it reads back itself.)
 *
 * What the command hands back as "go on this frame" is the original's carry:
 * that of its last comparison of a member's character with the one leaving
 * (whether it was at least that one), or, when there was no member to compare,
 * the carry it was entered with. It reads as nothing a script means; it is
 * reproduced because the engine goes on by it. */
static EventStep take_out_of_party(GameState *g, uint16_t at, uint8_t character, bool *compared)
{
    enum { LEFT_THE_PARTY = 3, LENGTH = 2 };
    ORIGINAL_EFFECT(set_event_saved_position(g, (uint16_t)(at + LENGTH)));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, character));
    EventStep st = { (uint16_t)(at + LENGTH), false };
    *compared = false;
    for (PartyPlace place = 0; place < PARTY_PLACES; place++) {
        if (!place_taken(g, place))
            continue;
        EventObject object = member_object(g, place);
        uint8_t is = field_object_state_character(g, object);
        *compared = true;
        st.go_on = is >= character;
        if (is != character)
            continue;
        set_field_object_state_state(g, object, LEFT_THE_PARTY);
        empty_place(g, place);
        break;
    }
    return st;
}

void event_cmd_remove_from_party(Snes *s)
{
    uint16_t at = event_command_offset(s);
    bool compared;
    EventStep st = take_out_of_party(state_of(s), at, script_byte(state_of(s), (uint16_t)(at + 1)), &compared);
    if (!compared)
        st.go_on = m_flag(s, CPU_FLAG_C);
    event_return(s, st);
}

/* The menus' party routines 6 and 7 (A the routine), called out to from the
 * JSL at `site`. The script goes on at the offset kept in
 * event_saved_position, with what the routine leaves in Y and the carry. */
static bool call_party_routine(Snes *s, uint32_t site, uint8_t routine)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | routine);
    m_set_nz8(s, routine);
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
    event_return(s, (EventStep){ event_saved_position(g), m_flag(s, CPU_FLAG_C) });
}

// $C0:3E5C
/* The menus' party routines 6, then 7. */
void event_cmd_party_routine_6_7(Snes *s) { party_routines(s, 0xC03E61, 6, true); }

// $C0:3E6E
void event_cmd_party_routine_6(Snes *s) { party_routines(s, 0xC03E67, 6, false); }

// $C0:3E75
void event_cmd_party_routine_7(Snes *s) { party_routines(s, 0xC03E67, 7, false); }

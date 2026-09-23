/* The event commands on the original's machine (event/bridge.h): their
 * registered entries, the guards, and the registers they leave. */
#include "event/bridge.h"

enum { FIELD_PAGE = 0x0100 };

EventLeaves *event_leaves;

/* ---- The registers a command leaves --------------------------------------------------- */

EventLeaves event_leaves_of(const Snes *s, uint16_t at)
{
    return (EventLeaves){ at, s->cpu.y, m_flag(s, CPU_FLAG_V) };
}

void event_hand_back(Snes *s, const EventLeaves *left, EventStep st)
{
    s->cpu.x = st.next;
    m_set_flag(s, CPU_FLAG_C, st.go_on);
    s->cpu.y = left->y;
    m_set_flag(s, CPU_FLAG_V, left->overflow);
}

void event_return(Snes *s, EventStep st)
{
    s->cpu.x = st.next;
    m_set_flag(s, CPU_FLAG_C, st.go_on);
    m_rts(s);
}

void event_set_registers(Snes *s, uint16_t a, uint16_t x, uint16_t y)
{
    s->cpu.a = a;
    s->cpu.x = x;
    s->cpu.y = y;
}

bool event_machine_back(const Snes *s, bool x8)
{
    return !s->cpu.e && m_m8(s) && m_x8(s) == x8 && m_direct_page(s) == FIELD_PAGE && m_bank_is_zero(s);
}

/* ---- The entries ----------------------------------------------------------------------- */

void event_command_entry(Snes *s, uint8_t opcode)
{
    const EventOpcode *row = event_opcode(opcode);
    uint16_t at = event_command_offset(s);
    EventLeaves left = event_leaves_of(s, at);
    ORIGINAL_EFFECT(leave_into(&left));
    EventStep st = event_perform(state_of(s), m_platform(s), row, at);
    s->cpu.x = st.next;
    m_set_flag(s, CPU_FLAG_C, st.go_on);
    ORIGINAL_EFFECT(event_hand_back(s, &left, st));
    m_rts(s);
}

/* ---- Guards ---------------------------------------------------------------------------
 *
 * The operations name their cells through game/vars.h, which knows them where
 * the field keeps them; the original reaches them through D, the data bank and
 * the object's doubled slot in an index register. So a command is the same
 * routine only when the game enters it the way it always does: D on the field
 * page ($0100), a data bank whose low half is low RAM and I/O and whose high
 * half the $C0 tables (bank $00 or $80), and what event_runs_here asks of the
 * state. Any other call is the original's (a guard that turned calls away
 * shows as a drop in the entry's shadow count). The state is looked at
 * without the machine's time (port/vars_ram.h, vars_peek). */

static bool machine_as_the_engine_keeps_it(const Snes *s)
{
    return m_direct_page(s) == FIELD_PAGE && m_bank_is_zero(s);
}

bool event_takes(Snes *s, const EventOpcode *row, uint16_t at)
{
    vars_peek(true);
    bool ok = event_runs_here(state_of(s), row, at);
    vars_peek(false);
    return ok;
}

bool event_command_accepts(Snes *s, uint8_t opcode)
{
    return machine_as_the_engine_keeps_it(s) && event_takes(s, event_opcode(opcode), event_command_offset(s));
}

/* A bridge's: the machine, the running object, the command inside the
 * buffer (a row with no operands says that much), and `holds`. */
bool event_guard(Snes *s, EventCheck holds)
{
    static const EventOpcode no_operands = { 0 };
    if (!machine_as_the_engine_keeps_it(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    bool ok = event_runs_here(g, &no_operands, at) && (!holds || holds(g, at));
    vars_peek(false);
    return ok;
}

bool event_machine_as_expected(Snes *s) { return event_guard(s, NULL); }

/* The object operand names an object. */
static bool object_operand_is_one(const GameState *g, uint16_t at)
{
    return event_names_object(script_byte(g, (uint16_t)(at + 1)));
}

/* The party member operand names a member whose object is one. */
static bool member_object_is_one(const GameState *g, uint16_t at)
{
    uint8_t place = script_byte(g, (uint16_t)(at + 1));
    return !(place & 1) && event_names_object(field_party_objects(g, place >> 1));
}

bool event_takes_object(Snes *s) { return event_guard(s, object_operand_is_one); }
bool event_takes_member(Snes *s) { return event_guard(s, member_object_is_one); }

/* The engine's: the object the per-frame turn is for (X, 8-bit) is one. */
bool event_runs_an_object(Snes *s)
{
    return machine_as_the_engine_keeps_it(s) && event_names_object((uint8_t)s->cpu.x);
}

/* And the map setup's: there are objects. */
bool event_has_objects(Snes *s)
{
    if (!machine_as_the_engine_keeps_it(s))
        return false;
    vars_peek(true);
    bool some = event_object_count(state_of(s)) != 0;
    vars_peek(false);
    return some;
}

/* $D6's: every party member's object is one, or there is no one there. */
static bool party_objects_are_objects(const GameState *g, uint16_t at)
{
    (void)at;
    for (unsigned place = 0; place < FIELD_PARTY_OBJECTS_COUNT; place++) {
        uint8_t object = field_party_objects(g, place);
        if (!(object & 0x80) && (object & 1))
            return false;
    }
    return true;
}

bool event_party_objects_are_objects(Snes *s) { return event_guard(s, party_objects_are_objects); }

/* The sound commands': the audio service addresses the driver's command
 * buffer at $00:1E00 and the APU ports, which the data bank every command
 * runs with reaches. */
bool event_sound_bank_is_system(Snes *s) { return event_machine_as_expected(s); }

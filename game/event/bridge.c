/* The translation between the event script protocol and the original's
 * registers (event/bridge.h), and the guards every command's entry runs under. */
#include "event/bridge.h"
#include "event/event_data.h"

enum { FIELD_PAGE = 0x0100, OBJECT_OFFSET_END = 0x0100 };

EventCommand event_command_at(const Snes *s) { return (EventCommand){ .at = s->cpu.y }; }

void event_return(Snes *s, EventStep st)
{
    s->cpu.x = st.next;
    s->cpu.y = st.cursor;
    m_set_flag(s, CPU_FLAG_C, st.go_on);
    if (st.overflow != OVERFLOW_KEPT)
        m_set_flag(s, CPU_FLAG_V, st.overflow != 0);
    m_rts(s);
}

void event_command_run(Snes *s, EventHandler handler)
{
    EventCommand c = event_command_at(s);
    event_return(s, handler(state_of(s), &c));
}

void event_command_run_platform(Snes *s, EventPlatformHandler handler)
{
    EventCommand c = event_command_at(s);
    event_return(s, handler(state_of(s), m_platform(s), &c));
}

EventStep event_step_left(const Snes *s)
{
    EventStep st = step_at(s->cpu.x, m_flag(s, CPU_FLAG_C), s->cpu.y);
    st.overflow = m_flag(s, CPU_FLAG_V) ? 1 : 0;
    return st;
}

bool event_machine_back(const Snes *s, bool x8)
{
    return !s->cpu.e && m_m8(s) && m_x8(s) == x8 && m_direct_page(s) == FIELD_PAGE && m_bank_is_zero(s);
}

void event_set_registers(Snes *s, uint16_t a, uint16_t x, uint16_t y)
{
    s->cpu.a = a;
    s->cpu.x = x;
    s->cpu.y = y;
}

/* ---- Guards ---------------------------------------------------------------------------
 *
 * The handlers name their cells through game/vars.h, which knows them where the
 * field keeps them; the original reaches them through D, the data bank and the
 * object's doubled slot in an index register. So a handler is the same routine
 * only when the game enters it the way it always does: D on the field page
 * ($0100), a data bank whose low half is low RAM and I/O and whose high half
 * the $C0 tables (bank $00 or $80), and objects named by an even offset into
 * tables that end below $2000. Any other call is the original's (a guard that
 * turned calls away shows as a drop in the entry's shadow count). */

bool event_names_object(uint16_t doubled) { return !(doubled & 1) && doubled < OBJECT_OFFSET_END; }

/* A command at the end of the script has operands at the last offset, which
 * reads the first byte past bank $7F: an I/O register that holds whatever was
 * last on the bus. No script is that long; such a command is the original's. */
enum { LAST_COMMANDS = 0xFFF0 };

/* What every command assumes of the game's state: the running object is one. */
static bool running_object_is_one(const GameState *g) { return event_names_object(field_current_object(g)); }

bool event_guard(Snes *s, EventStateCheck holds)
{
    if (m_direct_page(s) != FIELD_PAGE || !m_bank_is_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = running_object_is_one(g) && s->cpu.y < LAST_COMMANDS && (!holds || holds(g, s->cpu.y));
    vars_peek(false);
    return ok;
}

bool event_machine_as_expected(Snes *s) { return event_guard(s, NULL); }

/* The object operand names an object. */
static bool object_operand_is_one(const GameState *g, uint16_t at)
{
    return event_names_object(script_byte(g, (uint16_t)(at + 1)));
}

/* The party member operand names a member whose object is one; or, for the
 * commands that check first, no member at all. */
static bool member_at(const GameState *g, uint16_t at, bool may_be_none)
{
    uint8_t member = script_byte(g, (uint16_t)(at + 1));
    if (member & 1)
        return false;
    uint8_t object = field_party_objects(g, member >> 1);
    return (may_be_none && (object & 0x80)) || event_names_object(object);
}

static bool member_object_is_one(const GameState *g, uint16_t at) { return member_at(g, at, false); }
static bool member_is_one_or_none(const GameState *g, uint16_t at) { return member_at(g, at, true); }

bool event_takes_object(Snes *s) { return event_guard(s, object_operand_is_one); }
bool event_takes_member(Snes *s) { return event_guard(s, member_object_is_one); }
bool event_calls_member(Snes *s) { return event_guard(s, member_is_one_or_none); }

/* The engine's: the object the per-frame turn is for (X, 8-bit) is one. */
bool event_runs_an_object(Snes *s)
{
    return m_direct_page(s) == FIELD_PAGE && m_bank_is_zero(s) && event_names_object((uint8_t)s->cpu.x);
}

/* And the map setup's: there are objects. */
static bool has_objects(const GameState *g, uint16_t at)
{
    (void)at;
    return event_object_count(g) != 0;
}

bool event_has_objects(Snes *s)
{
    if (m_direct_page(s) != FIELD_PAGE || !m_bank_is_zero(s))
        return false;
    vars_peek(true);
    bool some = has_objects(state_of(s), 0);
    vars_peek(false);
    return some;
}

/* The comparisons' ($12-$16): the operator is in its table (8 byte operators,
 * 7 word ones; $16's bit 7 picks the memory page instead), which every
 * script's is. */
static bool operator_in_table(const GameState *g, uint16_t at, unsigned n, uint8_t operators, uint8_t keep)
{
    return (uint8_t)((script_byte(g, (uint16_t)(at + n)) & keep) << 1) < 2 * operators;
}

static bool byte_operator(const GameState *g, uint16_t at) { return operator_in_table(g, at, 3, 8, 0xFF); }
static bool word_operator(const GameState *g, uint16_t at) { return operator_in_table(g, at, 4, 7, 0xFF); }
static bool word_operator_of_vars(const GameState *g, uint16_t at) { return operator_in_table(g, at, 3, 7, 0xFF); }
static bool memory_operator(const GameState *g, uint16_t at) { return operator_in_table(g, at, 3, 8, 0x7F); }

bool event_compares_bytes(Snes *s) { return event_guard(s, byte_operator); }
bool event_compares_words(Snes *s) { return event_guard(s, word_operator); }
bool event_compares_words_var(Snes *s) { return event_guard(s, word_operator_of_vars); }
bool event_compares_memory(Snes *s) { return event_guard(s, memory_operator); }

/* $63's and $64's: the bit's mask is read from its table through the data
 * bank, so a bit number that runs the read out of the bank is the original's
 * (no script has one past 7). */
static bool settable_bit(const GameState *g, uint16_t at) { return script_byte(g, (uint16_t)(at + 1)) < 0xE0; }
static bool clearable_bit(const GameState *g, uint16_t at) { return script_byte(g, (uint16_t)(at + 1)) < 0xD8; }

bool event_sets_a_bit(Snes *s) { return event_guard(s, settable_bit); }
bool event_clears_a_bit(Snes *s) { return event_guard(s, clearable_bit); }

/* The dialog commands': a string table is set (its bank isn't 0); without
 * one the original goes to an error handler. */
static bool has_strings(const GameState *g, uint16_t at)
{
    (void)at;
    return (dialog_string_table(g) >> 16) != 0;
}

bool event_dialog_has_strings(Snes *s) { return event_guard(s, has_strings); }

/* $D6's: every party member's object is one, or none. */
static bool party_objects_are_objects(const GameState *g, uint16_t at)
{
    (void)at;
    for (unsigned member = 0; member < FIELD_PARTY_OBJECTS_COUNT; member++) {
        uint8_t object = field_party_objects(g, member);
        if (!(object & 0x80) && (object & 1))
            return false;
    }
    return true;
}

bool event_party_objects_are_objects(Snes *s) { return event_guard(s, party_objects_are_objects); }

/* $00's: the search for a level to resume (a saved position every $80 bytes,
 * from the level the object runs at) finds one before it comes round to where
 * it starts, where the original never stops. */
enum { LOWEST_LEVEL = 7, SAVED_LEVEL_STRIDE = 0x80 };

static bool finds_a_level(const GameState *g, uint16_t at)
{
    (void)at;
    uint8_t level = event_object_call_state_level(g, current_object(g));
    if (level == LOWEST_LEVEL)
        return true;
    uint16_t index = (uint16_t)(level * SAVED_LEVEL_STRIDE + field_current_object(g));
    for (unsigned k = 1; k < 0x10000 / SAVED_LEVEL_STRIDE; k++)
        if (event_saved_positions(g, (uint16_t)(index + k * SAVED_LEVEL_STRIDE) / 2) != 0)
            return true;
    return false;
}

bool event_return_finds_a_level(Snes *s) { return event_guard(s, finds_a_level); }

/* The sound commands': the audio service addresses the driver's command
 * buffer at $00:1E00 and the APU ports, which the data bank every command
 * runs with reaches. */
bool event_sound_bank_is_system(Snes *s) { return event_machine_as_expected(s); }

/* The event engine and commands on the original's machine (event/bridge.h):
 * the engine's two entries and how they run each command, the commands'
 * entries, the guards, and the registers they leave. */
#include "event/bridge.h"
#include "event/engine.h"
#include "event/event_data.h"
#include "port/native.h"

enum { FIELD_PAGE = 0x0100 };

EventLeaves *event_leaves;

/* ---- The registers a command leaves --------------------------------------------------- */

EventLeaves event_leaves_of(const Snes *s, uint16_t at)
{
    return (EventLeaves){ at, s->cpu.y, m_flag(s, CPU_FLAG_V) };
}

void event_hand_back(Snes *s, const EventLeaves *left)
{
    s->cpu.y = left->y;
    m_set_flag(s, CPU_FLAG_V, left->overflow);
}

EventCommand event_command_of(Snes *s, uint8_t opcode)
{
    return event_decode(state_of(s), m_platform(s), event_opcode(opcode), event_command_offset(s));
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

static bool takes(Snes *s, const EventOpcode *row, uint16_t at)
{
    vars_peek(true);
    bool ok = event_runs_here(state_of(s), row, at);
    vars_peek(false);
    return ok;
}

bool event_command_accepts(Snes *s, uint8_t opcode)
{
    return machine_as_the_engine_keeps_it(s) && takes(s, event_opcode(opcode), event_command_offset(s));
}

/* The engine's: the object the per-frame turn is for (X, 8-bit) is one. */
bool event_runs_an_object(Snes *s)
{
    return machine_as_the_engine_keeps_it(s) && event_names_object((uint8_t)s->cpu.x);
}

/* And the map setup's: there are objects, and no more than the tables hold
 * (past them the openings would clear and read the tables' neighbours). */
bool event_has_objects(Snes *s)
{
    if (!machine_as_the_engine_keeps_it(s))
        return false;
    vars_peek(true);
    uint8_t count = event_object_count(state_of(s));
    vars_peek(false);
    return count != 0 && count <= FIELD_OBJECTS;
}

/* ---- The commands' entries --------------------------------------------------------------- */

void event_command_entry(Snes *s, uint8_t opcode)
{
    const EventOpcode *row = event_opcode(opcode);
    uint16_t at = event_command_offset(s);
    EventLeaves left = event_leaves_of(s, at);
    ORIGINAL_EFFECT(leave_into(&left));
    EventStep st = event_perform(state_of(s), m_platform(s), row, at);
    ORIGINAL_EFFECT(event_hand_back(s, &left));
    event_return(s, st);
}

bool event_call_bridge(Snes *s, uint32_t site, uint32_t routine, EventBridge bridge)
{
    uint16_t s_before = s->cpu.s;
    m_push16(s, (uint16_t)(site + 2));
    s->cpu.pb = (uint8_t)(routine >> 16);
    s->cpu.pc = (uint16_t)routine;
    NATIVE_TIME_AS(s, routine, bridge(s));
    return s->cpu.s == s_before && s->cpu.pb == (uint8_t)(site >> 16) && s->cpu.pc == (uint16_t)(site + 3);
}

/* ---- Running a turn's commands on the machine ------------------------------------------------ */

/* The original's two dispatches the engine stands in for: its JSR ($5D6E,X)
 * in the per-frame turn and in the openings, and where each returns to. */
typedef enum { IN_A_TURN, IN_AN_OPENING } Dispatch;
static const uint32_t dispatch_site[] = { 0xC05AB1, 0xC05910 };     /* by Dispatch */
static const uint32_t dispatch_return[] = { 0xC05AB4, 0xC05913 };   /* by Dispatch */

/* Back from a command run on the machine: true if it left the machine as the
 * engine runs (event_machine_back); otherwise the port stops where the
 * dispatch returns to, and the original carries on. */
static bool back(Snes *s, Dispatch d)
{
    if (event_machine_back(s, false))
        return true;
    m_stop_at_own(s, dispatch_return[d], 0);
    return false;
}

/* The registers the original's dispatch leaves a handler: the opcode doubled
 * in A and X (the table's index, with the flags its making leaves: its
 * 16-bit shift clears the carry), the command's offset in Y. The handlers the
 * port doesn't have read them as the original's do; the bridges read Y, and
 * pass A's high byte and the carry on to the original's code they call. */
static void enter_as_the_table_does(Snes *s, uint8_t opcode, uint16_t at)
{
    uint16_t index = (uint16_t)(opcode * 2);
    event_set_registers(s, index, index, at);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_nz16(s, index);
}

typedef struct {
    Snes *s;
    Dispatch dispatch;
} OnTheMachine;

/* A command the port doesn't run: the original's handler, from its table. */
static bool through_the_table(OnTheMachine *m, uint16_t at, EventStep *st)
{
    Snes *s = m->s;
    GameState *g = state_of(s);
    uint8_t opcode = script_byte(g, at);
    enter_as_the_table_does(s, opcode, at);
    m_jsr(s, dispatch_site[m->dispatch], 0xC00000u | command_handler(g, opcode));
    if (!back(s, m->dispatch))
        return false;
    *st = (EventStep){ s->cpu.x, m_flag(s, CPU_FLAG_C) };
    return true;
}

/* A command by its row: its operation as C, or its bridge on the machine. */
static bool run_on_the_machine(void *ctx, uint16_t at, EventStep *st)
{
    OnTheMachine *m = ctx;
    Snes *s = m->s;
    GameState *g = state_of(s);
    uint8_t opcode = script_byte(g, at);
    const EventOpcode *row = event_opcode(opcode);
    if (!row || !takes(s, row, at))
        return through_the_table(m, at, st);
    if (row->bridge) {
        enter_as_the_table_does(s, opcode, at);
        if (!event_call_bridge(s, dispatch_site[m->dispatch], row->routine, row->bridge) || !back(s, m->dispatch))
            return false;
        *st = (EventStep){ s->cpu.x, m_flag(s, CPU_FLAG_C) };
        return true;
    }
    EventLeaves left = event_leaves_of(s, at);
    ORIGINAL_EFFECT(leave_into(&left));
    NATIVE_TIME_AS(s, row->routine, *st = event_perform(g, m_platform(s), row, at));
    /* X and the carry, as the handler returns them: where the original's loop
     * reads them if it takes over after this command */
    s->cpu.x = st->next;
    m_set_flag(s, CPU_FLAG_C, st->go_on);
    ORIGINAL_EFFECT(event_hand_back(s, &left));
    return true;
}

static bool bank_reaches_low_ram(void *ctx) { return m_bank_reaches_low_ram(((OnTheMachine *)ctx)->s); }

/* Hand the rest to the original at `where`, with the script's offset (or the
 * object's) in X as its loop has it there. */
static void hand_over(Snes *s, uint16_t x, uint32_t where, uint16_t dont_care)
{
    s->cpu.x = x;
    m_stop_at_own(s, where, dont_care);
}

// $C0:5A93
/* One object's turn (the object in X, its slot doubled, 8-bit): the engine
 * (event/engine.c) on the machine. Returns with 8-bit index registers; A, X,
 * Y and the flags but V are dead there. */
void event_run_object_script(Snes *s)
{
    OnTheMachine m = { s, IN_A_TURN };
    EventRunner r = { run_on_the_machine, bank_reaches_low_ram, &m };
    uint8_t object = (uint8_t)s->cpu.x;
    m_rep(s, CPU_FLAG_X);
    uint16_t at;
    switch (event_run_turn(state_of(s), object, &r, &at)) {
    case TURN_HANDED_BACK:
        return;
    case TURN_OFF_THE_SCRIPT:
        hand_over(s, at, 0xC05AA3, DC_A | DC_Y | DC_N | DC_Z | DC_C);
        return;
    case TURN_OVER:
        m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
        m_rts(s);
        return;
    default:
        hand_over(s, at, 0xC05ABA, DC_A | DC_Y | DC_N | DC_Z | DC_C);
        return;
    }
}

// $C0:58DE (inside the map setup routine)
/* The map's openings, on the machine; goes on at $C0:9DC3 rather than
 * returning. */
void event_run_init_scripts(Snes *s)
{
    OnTheMachine m = { s, IN_AN_OPENING };
    EventRunner r = { run_on_the_machine, bank_reaches_low_ram, &m };
    uint16_t object, at = 0;
    switch (event_run_openings(state_of(s), &r, &object, &at)) {
    case TURN_HANDED_BACK:
        return;
    case TURN_NOT_OURS_BEFORE:
        hand_over(s, object, 0xC058ED, 0);
        return;
    case TURN_OFF_THE_SCRIPT:
        hand_over(s, at, 0xC05900, 0);
        return;
    case TURN_NOT_OURS:
        hand_over(s, at, 0xC05915, 0);
        return;
    default:
        m_stop_at(s, 0xC09DC3, DC_A | DC_X | DC_N | DC_Z | DC_C);
        return;
    }
}

/* The event engine and commands on the original's machine (event/bridge.h):
 * the engine's two entries and how they run each command, the commands'
 * entries, the guards, and the registers they leave. */
#include "event/bridge.h"
#include "event/engine.h"
#include "event/event_data.h"
#include "port/native.h"
#include "field/parts.h"
#include "field/party.h"
#include "field/walk.h"

/* ---- The registers a command leaves --------------------------------------------------- */

EventLeaves event_leaves_of(const Snes *s) { return (EventLeaves){ s->cpu.y, m_flag(s, CPU_FLAG_V) }; }

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

/* ---- The handlers: where the original's are, and the bridges ------------------------------
 *
 * Each opcode's handler is at the address the original's table gives it
 * (event_data.h), which names the command's time (native_time_as) and its
 * registered entry. It is read without the machine's time where the port
 * runs the command: the original's dispatch reads it too, but only once,
 * where the port has already. */

enum { HANDLERS_BANK = 0xC00000 };

static uint32_t handler_of(Snes *s, uint8_t opcode)
{
    vars_peek(true);
    uint32_t handler = HANDLERS_BANK | command_handler(state_of(s), opcode);
    vars_peek(false);
    return handler;
}

static EventBridge bridge_of(uint8_t opcode)
{
    switch (opcode) {
#define EVENT_BRIDGE_CASE(name, op) case op: return name;
    EVENT_BRIDGES(EVENT_BRIDGE_CASE)
#undef EVENT_BRIDGE_CASE
    default: return NULL;
    }
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
    return machine_as_the_engine_keeps_it(s) && field_names_object((uint8_t)s->cpu.x);
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
    EventLeaves left = event_leaves_of(s);
    EventStep st = event_perform_leaving(state_of(s), m_platform(s), row, at, &left);
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
    return s->cpu.s == s_before && s->cpu.pb == (uint8_t)(site >> 16) && s->cpu.pc == (uint16_t)(site + JSR_SIZE);
}

/* ---- Running a turn's commands on the machine ------------------------------------------------ */

/* The original's dispatches the engine stands in for: its JSR ($5D6E,X) in
 * the per-frame turn, in the openings, in the map's own script and in an
 * object's restart, and where each returns to. */
typedef enum { IN_A_TURN, IN_AN_OPENING, IN_THE_MAP_SCRIPT, IN_A_RESTART } Dispatch;
static const uint32_t dispatch_site[] = { 0xC05AB1, 0xC05910, 0xC05977, 0xC059A0 };     /* by Dispatch */
static const uint32_t dispatch_return[] = { 0xC05AB4, 0xC05913, 0xC0597A, 0xC059A3 };   /* by Dispatch */

/* The registers the original's dispatch leaves a handler: the opcode doubled
 * in A and X (the table's index, with the flags its making leaves: its
 * 16-bit shift clears the carry), the command's offset in Y. The handlers the
 * port doesn't have read them as the original's do; the bridges read Y, and
 * pass A's high byte and the carry on to the original's code they call. */
static void enter_as_the_table_does(Snes *s, uint8_t opcode, uint16_t at)
{
    uint16_t index = (uint16_t)(opcode * 2);
    s->cpu.a = index;
    s->cpu.x = index;
    s->cpu.y = at;
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_nz16(s, index);
}

typedef struct {
    Snes *s;
    Dispatch dispatch;
} OnTheMachine;

/* Back from a command run on the machine: it came back as the engine runs
 * (event/bridge.h), or the port stops where the dispatch returns to, and the
 * original carries on. */
static bool back(OnTheMachine *m, BridgeMachine as)
{
    return bridge_back(m->s, dispatch_return[m->dispatch], as);
}

/* A command the port doesn't run: the original's handler, from its table. */
static bool through_the_table(OnTheMachine *m, uint16_t at, EventStep *st)
{
    Snes *s = m->s;
    uint8_t opcode = script_byte(state_of(s), at);
    BridgeMachine as = event_machine(s, false);
    enter_as_the_table_does(s, opcode, at);
    m_jsr(s, dispatch_site[m->dispatch], HANDLERS_BANK | command_handler(state_of(s), opcode));
    if (!back(m, as))
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
    uint32_t handler = handler_of(s, opcode);
    if (!row->run) {
        BridgeMachine as = event_machine(s, false);
        enter_as_the_table_does(s, opcode, at);
        if (!event_call_bridge(s, dispatch_site[m->dispatch], handler, bridge_of(opcode)) || !back(m, as))
            return false;
        *st = (EventStep){ s->cpu.x, m_flag(s, CPU_FLAG_C) };
        return true;
    }
    EventLeaves left = event_leaves_of(s);
    NATIVE_TIME_AS(s, handler, *st = event_perform_leaving(g, m_platform(s), row, at, &left));
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
    FieldObject object = field_linked_object(field_link_stored((uint8_t)s->cpu.x));
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

/* After the openings the party starts on the map (field/walk.h), and the
 * map setup returns. What the start leaves its caller: X the second
 * member's link as a word, the last it put on the trail; Y's low byte, which the
 * start's look at a slope clears (it counts its shift in Y); and the slope's
 * overflow (field/leaves.c). The map's own script, which the objects' setup
 * runs next, can leave X as it found it for the menus' service after it. */
static void leave_start_registers(Snes *s, bool slope, bool overflow)
{
    const GameState *g = state_of(s);
    s->cpu.x = (uint16_t)(field_party_objects_high(g, PARTY_SECOND_MEMBER) << 8 |
                          field_link_byte(party_member_link(g, PARTY_SECOND_MEMBER)));
    s->cpu.y = slope ? 0 : (uint16_t)(s->cpu.y & 0x00FF);
    m_set_flag(s, CPU_FLAG_V, overflow);
}

static void start_the_party(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool fits = party_start_fits(g);
    bool slope = fits && field_start_on_a_slope(g);
    bool overflow = fits && field_start_overflow(g, m_flag(s, CPU_FLAG_V));
    vars_peek(false);
    if (!fits) {
        m_stop_at(s, 0xC09DC3, DC_A | DC_X | DC_N | DC_Z | DC_C);
        return;
    }
    party_start_on_the_map(g);
    ORIGINAL_EFFECT(leave_start_registers(s, slope, overflow));
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
}

// $C0:595C
/* The map's own script, on the machine. It returns with the script's offset
 * at its zero byte in X, and Y and the overflow as its last command left them
 * (as it came, where none ran), which the menus' service after it keeps;
 * where the port gives the rest up, the original's loop goes on from the
 * command it got to, with Y and the overflow as they are. */
enum { MAP_SCRIPT_LOOP = 0xC05967, THE_LOOP_READS_Y_AND_THE_OVERFLOW = DC_A | DC_N | DC_Z | DC_C };

void event_run_map_script(Snes *s)
{
    OnTheMachine m = { s, IN_THE_MAP_SCRIPT };
    EventRunner r = { run_on_the_machine, bank_reaches_low_ram, &m };
    uint16_t at = 0;
    switch (event_run_map_script_commands(state_of(s), &r, &at)) {
    case TURN_HANDED_BACK:
        return;
    case TURN_OVER:
        s->cpu.x = at;
        m_rts(s);
        return;
    default:
        hand_over(s, at, MAP_SCRIPT_LOOP, THE_LOOP_READS_Y_AND_THE_OVERFLOW);
        return;
    }
}

/* The map's script's: the engine's machine. */
bool event_map_script_can_run(Snes *s) { return machine_as_the_engine_keeps_it(s); }

// $C0:597D
/* The running object's script started over (a character's, after a menu
 * changed the party), on the machine. It returns with 8-bit A holding the
 * least urgent level and X the object's offset; where the port gives the rest
 * up, the original's loop goes on from the command it got to. */
enum { RESTART_LOOP = 0xC05990, THE_RESTART_READS_X = DC_A | DC_Y | DC_N | DC_Z | DC_C };

static void leave_restarted(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t level = event_object_call_state_level(g, object_current(g));
    s->cpu.x = field_object_offset(object_current(g));
    s->cpu.a = level;   /* over the cleared calls' zero, a word */
    m_set_nz8(s, level);
}

void event_restart_the_running_object_entry(Snes *s)
{
    OnTheMachine m = { s, IN_A_RESTART };
    EventRunner r = { run_on_the_machine, bank_reaches_low_ram, &m };
    uint16_t at = 0;
    switch (event_restart_the_running_object(state_of(s), &r, &at)) {
    case TURN_HANDED_BACK:
        return;
    case TURN_OVER:
        ORIGINAL_EFFECT(leave_restarted(s));
        m_rts(s);
        return;
    default:
        hand_over(s, at, RESTART_LOOP, THE_RESTART_READS_X);
        return;
    }
}

/* The running object one of the map's, on the engine's machine. */
bool event_running_object_can_restart(Snes *s)
{
    if (!machine_as_the_engine_keeps_it(s))
        return false;
    vars_peek(true);
    bool ok = object_current_is_one(state_of(s));
    vars_peek(false);
    return ok;
}

// $C0:58DE (inside the map setup routine)
/* The map's openings, on the machine, and then the party's start on the map
 * ($C0:9DC3), which returns from the map setup. */
void event_run_init_scripts(Snes *s)
{
    OnTheMachine m = { s, IN_AN_OPENING };
    EventRunner r = { run_on_the_machine, bank_reaches_low_ram, &m };
    FieldObject object;
    uint16_t at = 0;
    switch (event_run_openings(state_of(s), &r, &object, &at)) {
    case TURN_HANDED_BACK:
        return;
    case TURN_NOT_OURS_BEFORE:
        hand_over(s, field_object_offset(object), 0xC058ED, 0);
        return;
    case TURN_OFF_THE_SCRIPT:
        hand_over(s, at, 0xC05900, 0);
        return;
    case TURN_NOT_OURS:
        hand_over(s, at, 0xC05915, 0);
        return;
    default:
        start_the_party(s);
        return;
    }
}

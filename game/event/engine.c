/* The event engine: every object's script, run a turn at a time.
 *
 * Each frame every object on the map takes a turn: its script runs from where
 * it left off, command after command, until a command ends the turn (a wait,
 * a move under way, a dialog on screen) or the turn's budget of commands
 * (event_commands_per_turn) is spent, and it picks up there next frame. When
 * a map loads, every object first runs the opening of its script -- where it
 * is placed, how it looks, what it starts doing -- up to its first zero byte,
 * all at once.
 *
 * A command is run by its row of the language's table (event/script.h): its
 * operands are decoded by the row and handed to the operation, which says
 * where the script goes next. The table is every file's rows, indexed here by
 * opcode.
 *
 * What still goes through the original's own table ($C0:5D6E) and an
 * emulated call: the commands with no port yet, the ports that call out to
 * the original's code (the moves, the sound driver's commands, the menus'
 * party routines; each a registered bridge), and any state the port doesn't
 * take (the guard's refusals, below). */
#include "event/bridge.h"
#include "event/event_data.h"
#include "event/leaves.h"
#include "event/variables.h"
#include "port/native.h"

/* ---- The table, by opcode ------------------------------------------------------------- */

static const EventOpcodes *const every_file[] = {
    &event_animation_opcodes, &event_calls_opcodes,  &event_dialog_opcodes,     &event_facing_opcodes,
    &event_flow_opcodes,      &event_input_opcodes,  &event_locations_opcodes,  &event_memory_opcodes,
    &event_party_opcodes,     &event_properties_opcodes, &event_screen_opcodes, &event_sound_opcodes,
    &event_story_opcodes,     &event_variables_opcodes,
};

enum { OPCODES = 256 };

const EventOpcode *event_opcode(uint8_t opcode)
{
    static const EventOpcode *by_opcode[OPCODES];
    static bool indexed;
    if (!indexed) {
        for (unsigned f = 0; f < sizeof every_file / sizeof every_file[0]; f++)
            for (unsigned r = 0; r < every_file[f]->count; r++)
                by_opcode[every_file[f]->rows[r].opcode] = &every_file[f]->rows[r];
        indexed = true;
    }
    return by_opcode[opcode];
}

/* ---- What the port takes -----------------------------------------------------------------
 *
 * The operations reach the object tables by slot, where the original indexes
 * them with the object doubled; the two agree for the objects the game makes,
 * which are all even offsets into tables below $2000. And they read operands
 * from the script buffer, which ends with the bank: a command in its last
 * bytes would read its operands through the next bank's mirror of low RAM,
 * where they could be the cells it writes. No script is that long, and no
 * game object odd, so everything else is the original's to run (and would be
 * an assertion in a build without it; docs/PLAN.md). */

enum { OBJECT_OFFSET_END = 0x0100 };
enum { SCRIPT_SIZE = 0xDFFF, LONGEST_COMMAND = 8 };

bool event_names_object(uint16_t doubled) { return !(doubled & 1) && doubled < OBJECT_OFFSET_END; }

/* The party member at `place` (doubled): the place even, and their object
 * one; or, where the command checks first, nobody there. */
static bool member_named(const GameState *g, uint8_t place, bool may_be_absent)
{
    if (place & 1)
        return false;
    uint8_t object = field_party_objects(g, place >> 1);
    return (may_be_absent && (object & 0x80)) || event_names_object(object);
}

/* The bit tables hold 8 masks each, and a bit number past them reads on
 * through the bank; one that would leave it is the original's. */
enum { LAST_BIT_READ = 0xDF, LAST_CLEAR_BIT_READ = 0xD7 };

bool event_runs_here(const GameState *g, const EventOpcode *row, uint16_t at)
{
    if (!event_names_object(field_current_object(g)) || at > SCRIPT_SIZE - LONGEST_COMMAND)
        return false;
    for (unsigned i = 0; i < EVENT_ARGS; i++) {
        uint8_t operand = script_byte(g, (uint16_t)(at + row->args[i].operand));
        switch (row->args[i].kind) {
        case ARG_OBJECT:
            if (!event_names_object(operand))
                return false;
            break;
        case ARG_MEMBER_OBJECT:
        case ARG_MEMBER_PRESENT:
            if (!member_named(g, operand, row->member_may_be_absent))
                return false;
            break;
        case ARG_BIT:
            if (operand > LAST_BIT_READ)
                return false;
            break;
        case ARG_ALL_BUT_BIT:
            if (operand > LAST_CLEAR_BIT_READ)
                return false;
            break;
        default:
            break;
        }
    }
    return !row->accepts || row->accepts(g, at);
}

/* ---- Running a command -------------------------------------------------------------------- */

static uint32_t decode(const GameState *g, const EventArg *arg, uint16_t at)
{
    uint16_t operand = (uint16_t)(at + arg->operand);
    switch (arg->kind) {
    case ARG_CONSTANT: return arg->constant;
    case ARG_BYTE: return script_byte(g, operand);
    case ARG_WORD: return script_word(g, operand);
    case ARG_ADDRESS: return script_word(g, operand) | (uint32_t)script_byte(g, (uint16_t)(operand + 2)) << 16;
    case ARG_VARIABLE: return script_byte(g, operand);
    case ARG_VARIABLE_BYTE: return variable_byte(g, script_byte(g, operand));
    case ARG_VARIABLE_WORD: return variable_word(g, script_byte(g, operand));
    case ARG_OBJECT: return slot_of(script_byte(g, operand));
    case ARG_SELF: return running_object(g);
    case ARG_MEMBER_OBJECT: return slot_of(field_party_objects(g, script_byte(g, operand) >> 1));
    case ARG_MEMBER_PRESENT: return !(field_party_objects(g, script_byte(g, operand) >> 1) & 0x80);
    case ARG_STORY_WORD: return story_word(g, script_word(g, operand));
    case ARG_STORY_PROGRESS: return story_progress(g);
    case ARG_BIT: return bit_mask(g, script_byte(g, operand));
    case ARG_ALL_BUT_BIT: return bit_clear_mask(g, script_byte(g, operand));
    case ARG_HERE: return at;
    default: return 0;
    }
}

/* What the original leaves around a command (event/leaves.h): Y where its
 * parse stopped, its first operands kept in the scratch cells (and, for the
 * comparisons, where the jump byte is, which their shared routine notes and
 * reads back itself), and after it
 * the overflow of its addition, which follows from the parameters and the
 * result: the amount, and the variable as it now is. */
static uint16_t parse_stop(const EventOpcode *row, uint16_t at, uint8_t operand)
{
    uint16_t y = (uint16_t)(at + operand);
    return row->shared_comparison ? (uint16_t)(y & 0x00FF) : y;
}

static void keep_parameter(GameState *g, unsigned cell, uint8_t how, const EventArgs *a, uint16_t at)
{
    if (how & 0x20)
        keep_word(g, cell, (uint16_t)(at + (how & 0x0F)));
    else if (how & 0x40)
        keep_word(g, cell, (uint16_t)a->arg[how & 0x0F]);
    else if (how & 0x80)
        keep_byte(g, cell, (uint8_t)a->arg[how & 0x0F]);
}

static void leave_before(GameState *g, const EventOpcode *row, uint16_t at, const EventArgs *a)
{
    event_leaves->at = at;
    event_leaves->y = parse_stop(row, at, row->parse);
    keep_parameter(g, KEPT_FIRST, row->keeps, a, at);
    keep_parameter(g, KEPT_SECOND, row->keeps_second, a, at);
    if (row->shared_comparison)
        set_event_condition_resume(g, (uint16_t)(at + row->jump));
}

static void leave_after(const GameState *g, const EventOpcode *row, const EventArgs *a)
{
    if (row->overflow == OVERFLOW_NONE)
        return;
    Width w = (Width)a->arg[2];
    uint16_t amount = (uint16_t)a->arg[1], now = variable(g, (EventVariable)a->arg[0], w);
    bool subtracting = row->overflow == OVERFLOW_OF_SUBTRACTING;
    uint16_t before = subtracting ? (uint16_t)(now + amount) : (uint16_t)(now - amount);
    uint16_t added = subtracting ? (uint16_t)~amount : amount;
    leave_overflow(w == AS_BYTE ? overflows8((uint8_t)before, (uint8_t)added, (uint8_t)now)
                                : overflows16(before, added, now));
}

/* A jump is by the distance in its jump byte, forward or back from that byte. */
static EventStep jump(GameState *g, const EventOpcode *row, uint16_t at)
{
    uint16_t from = (uint16_t)(at + row->jump);
    uint8_t distance = script_byte(g, from);
    uint16_t to = row->back ? (uint16_t)(from - distance) : (uint16_t)(from + distance);
    ORIGINAL_EFFECT(keep_word(g, KEPT_FIRST, distance));
    ORIGINAL_EFFECT(leave_overflow(row->back ? overflows16(from, (uint16_t)~distance, to)
                                             : overflows16(from, distance, to)));
    ORIGINAL_EFFECT(leave_y(parse_stop(row, at, row->parse_on_jump)));
    return (EventStep){ to, true };
}

static EventStep follow(GameState *g, const EventOpcode *row, uint16_t at, EventFlow flow)
{
    uint16_t after = (uint16_t)(at + row->length);
    switch (flow.kind) {
    case FLOW_NEXT: return (EventStep){ after, true };
    case FLOW_END_TURN: return (EventStep){ after, false };
    case FLOW_STAY: return (EventStep){ at, false };
    case FLOW_AGAIN: return (EventStep){ at, true };
    case FLOW_JUMP: return jump(g, row, at);
    case FLOW_NEXT_AFTER: return (EventStep){ (uint16_t)(at + flow.to), true };
    case FLOW_GO_TO: return (EventStep){ flow.to, true };
    default: return (EventStep){ flow.to, false };
    }
}

EventStep event_perform(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at)
{
    EventArgs a = { running_object(g), { 0 }, pf };
    for (unsigned i = 0; i < EVENT_ARGS; i++)
        a.arg[i] = decode(g, &row->args[i], at);
    ORIGINAL_EFFECT(leave_before(g, row, at, &a));
    EventFlow flow = row->run(g, &a);
    ORIGINAL_EFFECT(leave_after(g, row, &a));
    return follow(g, row, at, flow);
}

/* ---- A turn, on the machine --------------------------------------------------------------- */

/* The last script offset reads the first byte past bank $7F, an I/O register
 * that holds whatever was last on the bus; no script is that long, and a turn
 * that gets there is left to the original, whose bus it is. */
enum { SCRIPT_END = 0xFFFF };

/* One command of the turn is spent: false when that was the last. */
static bool spend_command(GameState *g)
{
    uint8_t left = (uint8_t)(event_commands_left(g) - 1);
    set_event_commands_left(g, left);
    return left != 0;
}

/* Back from a handler run through the original's table, which returns to
 * `returns_to`: true if it left the machine as the engine runs
 * (event_machine_back); otherwise the port stops there, where A and Y are
 * dead (the entry's own set is for its return), and the original carries on. */
static bool back(Snes *s, uint32_t returns_to)
{
    if (event_machine_back(s, false))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

/* Run a command through the original's table, from the JSR at `site`, with
 * the registers the original has there: the opcode doubled in A and X (the
 * table index, with the flags its making leaves: its 16-bit shift clears the
 * carry), the command's offset in Y. False if the handler came back with
 * other widths; the original then carries on from the call. */
static bool through_the_table(Snes *s, uint32_t site, uint16_t at, uint8_t opcode, uint32_t handler, EventStep *st)
{
    uint16_t index = (uint16_t)(opcode * 2);
    event_set_registers(s, index, index, at);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_nz16(s, index);
    m_jsr(s, site, handler);
    if (!back(s, site + 3))
        return false;
    *st = (EventStep){ s->cpu.x, m_flag(s, CPU_FLAG_C) };
    return true;
}

/* Run the command at `at`: as C when the port has it and takes the state,
 * with its time counted as its handler's; otherwise through the table. The
 * handler is looked up either way, as the original does, so the engine's own
 * work is the same whichever way a command runs. */
static bool run_command(Snes *s, uint32_t site, uint16_t at, EventStep *st)
{
    GameState *g = state_of(s);
    uint8_t opcode = script_byte(g, at);
    uint32_t handler = 0xC00000u | command_handler(g, opcode);
    const EventOpcode *row = event_opcode(opcode);
    if (!row || !event_takes(s, row, at))
        return through_the_table(s, site, at, opcode, handler, st);
    EventLeaves left = event_leaves_of(s, at);
    ORIGINAL_EFFECT(leave_into(&left));
    NATIVE_TIME_AS(s, handler, *st = event_perform(g, m_platform(s), row, at));
    ORIGINAL_EFFECT(event_hand_back(s, &left, *st));
    return true;
}

// $C0:5A93
/* Run the turn of the object in X (its slot doubled, 8-bit) for this frame,
 * and keep where its script stopped. The engine names the object running in
 * the low byte of field_current_object. Returns with 8-bit index registers;
 * A, X, Y and the flags but V are dead there. */
void event_run_object_script(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t object = (uint8_t)s->cpu.x;
    set_event_commands_left(g, event_commands_per_turn(g));
    uint16_t at = event_object_script_position(g, slot_of(object));
    set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | object));
    m_rep(s, CPU_FLAG_X);
    EventStep st;
    do {
        if (at == SCRIPT_END) {
            s->cpu.x = at;
            m_stop_at_own(s, 0xC05AA3, DC_A | DC_Y | DC_N | DC_Z | DC_C);
            return;
        }
        if (!run_command(s, 0xC05AB1, at, &st))
            return;
        at = st.next;
    } while (st.go_on && spend_command(g));

    /* $C0:5ABA: the commands may have named another object running, or left
     * a data bank the store through it doesn't reach low RAM with */
    if (!event_names_object(field_current_object(g)) || !m_bank_reaches_low_ram(s)) {
        s->cpu.x = at;
        m_stop_at_own(s, 0xC05ABA, DC_A | DC_Y | DC_N | DC_Z | DC_C);
        return;
    }
    set_event_object_script_position(g, running_object(g), at);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_rts(s);
}

// $C0:58DE (inside the map setup routine)
/* Run every object's opening: its script up to its first zero byte, and keep
 * the offset after it, where the per-frame turns carry on. Every object's
 * solidity and acceleration are cleared on the way (and, by the original's
 * 16-bit clears, the sprite state of the object after it); an object whose
 * character is negative has no script to run. The object running is named in
 * field_current_object, the end of the slots in event_object_end. Goes on at
 * $C0:9DC3 rather than returning. */
void event_run_init_scripts(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t end = (uint16_t)(event_object_count(g) * 2);
    set_event_object_end(g, end);
    uint16_t object = 0;
    do {
        /* $C0:58ED: the object's cells are reached through the data bank,
         * which a command run by the original may have left elsewhere */
        if (!m_bank_reaches_low_ram(s)) {
            s->cpu.x = object;
            m_stop_at(s, 0xC058ED, 0);
            return;
        }
        EventObject slot = slot_of(object);
        set_field_object_solid(g, slot, 0);
        set_field_object_sprite_state(g, slot + 1, 0);
        set_field_object_accelerating(g, slot, 0);
        set_field_object_acceleration(g, slot, 0);
        if (!(field_object_state_character(g, slot) & 0x80)) {
            uint16_t at = event_object_script_position(g, slot);
            set_field_current_object(g, object);
            EventStep st;
            while (at != SCRIPT_END && script_byte(g, at) != 0) {
                if (!run_command(s, 0xC05910, at, &st))
                    return;
                at = st.next;
            }
            if (at == SCRIPT_END) {
                s->cpu.x = at;
                m_stop_at(s, 0xC05900, 0);
                return;
            }
            /* $C0:5915: the commands may have named another object running,
             * or left another data bank */
            object = field_current_object(g);
            if ((object & 1) || !m_bank_reaches_low_ram(s)) {
                s->cpu.x = at;
                m_stop_at(s, 0xC05915, 0);
                return;
            }
            set_event_object_script_position(g, slot_of(object), (uint16_t)(at + 1));
        }
        object = (uint16_t)(object + 2);
    } while (object != event_object_end(g));
    m_stop_at(s, 0xC09DC3, DC_A | DC_X | DC_N | DC_Z | DC_C);
}

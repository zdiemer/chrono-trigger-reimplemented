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
 * How each command of a turn runs is the caller's (event/engine.h,
 * EventRunner): on the development system, event/bridge.c runs the rows as C,
 * the bridges on the machine, and hands what the port doesn't have to the
 * original. */
#include "event/engine.h"
#include "event/event_data.h"
#include "event/leaves.h"
#include "event/party.h"
#include "event/variables.h"

/* ---- The table, by opcode ------------------------------------------------------------- */

static const EventOpcodes *const every_file[] = {
    &event_animation_opcodes, &event_calls_opcodes,  &event_dialog_opcodes,     &event_facing_opcodes,
    &event_flow_opcodes,      &event_input_opcodes,  &event_locations_opcodes,  &event_memory_opcodes,
    &event_movement_opcodes,  &event_party_opcodes,  &event_properties_opcodes, &event_screen_opcodes,
    &event_sound_opcodes,     &event_story_opcodes,  &event_variables_opcodes,
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
    return (may_be_absent && (object & NOBODY)) || event_names_object(object);
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
    case ARG_MEMBER_OBJECT: return member_object(g, (PartyPlace)(script_byte(g, operand) >> 1));
    case ARG_MEMBER_PRESENT: return place_taken(g, (PartyPlace)(script_byte(g, operand) >> 1));
    case ARG_STORY_WORD: return story_word(g, script_word(g, operand));
    case ARG_STORY_PROGRESS: return story_progress(g);
    case ARG_BIT: return bit_mask(g, script_byte(g, operand));
    case ARG_ALL_BUT_BIT: return bit_clear_mask(g, script_byte(g, operand));
    case ARG_HERE: return at;
    default: return 0;
    }
}

/* A jump is by the distance in its jump byte, forward or back from that byte. */
static EventStep jump(GameState *g, const EventOpcode *row, uint16_t at)
{
    uint16_t from = (uint16_t)(at + row->jump);
    uint8_t distance = script_byte(g, from);
    uint16_t to = row->back ? (uint16_t)(from - distance) : (uint16_t)(from + distance);
    ORIGINAL_EFFECT(original_leaves_on_jump(g, row->opcode, at, from, distance, row->back));
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
    ORIGINAL_EFFECT(original_leaves_before(g, row->opcode, at, row->jump, a.arg));
    EventFlow flow = row->run(g, &a);
    ORIGINAL_EFFECT(original_leaves_after(g, row->opcode, a.arg));
    return follow(g, row, at, flow);
}

/* ---- Turns ------------------------------------------------------------------------------ */

/* The last script offset reads the first byte past bank $7F, an I/O register
 * that holds whatever was last on the bus; no script is that long, and a turn
 * that gets there is left to the original, whose bus it is. */
enum { SCRIPT_END = 0xFFFF };

/* The turn's budget of commands is the game's, in event_commands_left: the
 * commands still run by the original's code can spend it too (fuzzing finds
 * them doing so), and the original's loop, which takes the turn over when a
 * command hands back, counts on in it. So the turn counts in the cell. */
static bool spend_command(GameState *g)
{
    uint8_t left = (uint8_t)(event_commands_left(g) - 1);
    set_event_commands_left(g, left);
    return left != 0;
}

// $C0:5A93
EventTurnEnd event_run_turn(GameState *g, uint8_t object, const EventRunner *r, uint16_t *stopped_at)
{
    set_event_commands_left(g, event_commands_per_turn(g));
    uint16_t at = event_object_script_position(g, slot_of(object));
    set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | object));
    EventStep st;
    do {
        *stopped_at = at;
        if (at == SCRIPT_END)
            return TURN_OFF_THE_SCRIPT;
        if (!r->run(r->ctx, at, &st))
            return TURN_HANDED_BACK;
        at = st.next;
    } while (st.go_on && spend_command(g));
    *stopped_at = at;
    /* the commands may have named another object running, or left a state
     * the port doesn't reach as the original does */
    if (!event_names_object(field_current_object(g)) || !r->state_is_ours(r->ctx))
        return TURN_NOT_OURS;
    set_event_object_script_position(g, running_object(g), at);
    return TURN_OVER;
}

/* An object's opening: its script up to its first zero byte, after which the
 * per-frame turns carry on. */
static EventTurnEnd run_opening(GameState *g, EventObject slot, const EventRunner *r, uint16_t *stopped_at)
{
    uint16_t at = event_object_script_position(g, slot);
    set_field_current_object(g, (uint16_t)(slot * 2));
    EventStep st;
    for (;;) {
        *stopped_at = at;
        if (at == SCRIPT_END)
            return TURN_OFF_THE_SCRIPT;
        if (script_byte(g, at) == 0)
            break;
        if (!r->run(r->ctx, at, &st))
            return TURN_HANDED_BACK;
        at = st.next;
    }
    /* the commands may have named another object running, or left a state the
     * port doesn't reach as the original does */
    uint16_t object = field_current_object(g);
    if ((object & 1) || !r->state_is_ours(r->ctx))
        return TURN_NOT_OURS;
    set_event_object_script_position(g, slot_of(object), (uint16_t)(at + 1));
    return TURN_OVER;
}

/* A map's objects, before their scripts first run: none solid, none
 * accelerating (and, by the original's 16-bit clears, the sprite state of the
 * object after each cleared too). */
static void settle(GameState *g, EventObject slot)
{
    set_field_object_solid(g, slot, 0);
    set_field_object_sprite_state(g, slot + 1, 0);
    set_field_object_accelerating(g, slot, 0);
    set_field_object_acceleration(g, slot, 0);
}

enum { NO_SCRIPT = 0x80 };   /* an object whose character has this bit runs no script */

// $C0:58DE (inside the map setup routine)
EventTurnEnd event_run_openings(GameState *g, const EventRunner *r, uint16_t *object, uint16_t *stopped_at)
{
    set_event_object_end(g, (uint16_t)(event_object_count(g) * 2));
    *object = 0;
    do {
        /* the object's cells are reached through the data bank, which a
         * command the original ran may have left elsewhere */
        if (!r->state_is_ours(r->ctx))
            return TURN_NOT_OURS_BEFORE;
        EventObject slot = slot_of(*object);
        settle(g, slot);
        if (!(field_object_state_character(g, slot) & NO_SCRIPT)) {
            EventTurnEnd e = run_opening(g, slot, r, stopped_at);
            if (e != TURN_OVER)
                return e;
            *object = field_current_object(g);
        }
        *object = (uint16_t)(*object + 2);
    } while (*object != event_object_end(g));
    return TURN_OVER;
}

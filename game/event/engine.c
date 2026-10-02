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
 * The operations reach the object tables by object, where the original
 * indexes them with the object's offset (field/object.h); the two agree for
 * the objects the game makes, which are all even offsets inside the tables.
 * An odd offset, or one past a table, reads the tables' neighbours in the
 * original's memory. And they read operands from the script buffer, which
 * ends with the bank: a command in its last bytes would read its operands
 * through the next bank's mirror of low RAM, where they could be the cells it
 * writes. No script is that long, and no game object odd or past the 64, so
 * everything else is the original's to run (and would be an assertion in a
 * build without it; docs/PLAN.md). */

/* The bytes a command's operands are read from: its own, and at least
 * eight (a row may decode operands its shorter forms don't have). */
enum { OPERANDS_READ = 8 };

static uint16_t bytes_read(const EventOpcode *row) { return row->length > OPERANDS_READ ? row->length : OPERANDS_READ; }

/* A place of the party (doubled, as scripts write it). */
static bool a_place(uint8_t place) { return !(place & 1) && (place >> 1) < PARTY_PLACES; }

/* The party member at `place`: their object one the tables hold; or, where
 * the command checks first, nobody there. An empty place names no object, and
 * a command that takes the member's object without checking is left to the
 * original there; the game makes none of those calls (the commands that do
 * take an empty place ask for the member as Someone, event/party.h). */
static bool member_named(const GameState *g, uint8_t place, bool may_be_absent)
{
    if (!a_place(place))
        return false;
    FieldLink link = party_member_link(g, place >> 1);
    return field_link_is_object(link) ? field_link_names_object(link) : may_be_absent;
}

/* The bit tables hold 8 masks each, and a bit number past them reads on
 * through the bank; one that would leave it is the original's. */
enum { LAST_BIT_READ = 0xDF, LAST_CLEAR_BIT_READ = 0xD7 };

/* Whether one of the command's operands names what its kind says. */
static bool names_what_it_says(const GameState *g, const EventOpcode *row, const EventArg *arg, uint16_t at)
{
    if (arg->kind == ARG_STORY_WORD)   /* the word the command reads is in the story memory */
        return story_holds(script_word(g, (uint16_t)(at + arg->operand)), AS_WORD);
    uint8_t operand = script_byte(g, (uint16_t)(at + arg->operand));
    switch (arg->kind) {
    case ARG_OBJECT: return field_names_object(operand);
    case ARG_MEMBER: return a_place(operand) && member_place_readable(g, (PartyPlace)(operand >> 1));
    case ARG_MEMBER_OBJECT:
    case ARG_MEMBER_PRESENT: return member_named(g, operand, row->member_may_be_absent);
    case ARG_BIT: return operand <= LAST_BIT_READ;
    case ARG_ALL_BUT_BIT: return operand <= LAST_CLEAR_BIT_READ;
    case ARG_FLAG: return operand <= 1;
    default: return true;
    }
}

bool event_runs_here(const GameState *g, const EventOpcode *row, uint16_t at)
{
    if (!object_current_is_one(g) || at > EVENT_SCRIPT_BYTES - bytes_read(row))
        return false;
#define CHECK_PARAMETER(type, name)                   \
    if (!names_what_it_says(g, row, &row->name, at)) \
        return false;
    EVENT_PARAMETERS(CHECK_PARAMETER)
    EVENT_RECORD_PARAMETERS(CHECK_PARAMETER)
#undef CHECK_PARAMETER
    if (!row->accepts)
        return true;
    EventCommand command = event_decode(g, NULL, row, at);
    return row->accepts(g, &command);
}

/* ---- Running a command -------------------------------------------------------------------- */

static uint32_t decode(const GameState *g, const EventArg *arg, uint16_t at)
{
    uint16_t operand = (uint16_t)(at + arg->operand);
    switch (arg->kind) {
    case ARG_CONSTANT: return arg->constant;
    case ARG_BYTE: return script_byte(g, operand);
    case ARG_BYTE_PLUS_ONE: return (uint8_t)(script_byte(g, operand) + 1);
    case ARG_BITS: return (uint32_t)(script_byte(g, operand) >> arg->shift) & arg->constant;
    case ARG_WORD: return script_word(g, operand);
    case ARG_WORD_BITS: return (uint32_t)(script_word(g, operand) >> arg->shift) & arg->constant;
    case ARG_ADDRESS: return script_word(g, operand) | (uint32_t)script_byte(g, (uint16_t)(operand + 2)) << 16;
    case ARG_VARIABLE: return script_byte(g, operand);
    case ARG_VARIABLE_BYTE: return variable_byte(g, script_byte(g, operand));
    case ARG_VARIABLE_WORD: return variable_word(g, script_byte(g, operand));
    case ARG_OBJECT: return field_linked_object(field_link_stored(script_byte(g, operand)));
    case ARG_SELF: return object_current(g);
    case ARG_MEMBER: return (PartyPlace)(script_byte(g, operand) >> 1);
    case ARG_MEMBER_OBJECT: return member_object(g, (PartyPlace)(script_byte(g, operand) >> 1));
    case ARG_MEMBER_PRESENT: return place_taken(g, (PartyPlace)(script_byte(g, operand) >> 1));
    case ARG_STORY_WORD: return story_word(g, script_word(g, operand));
    case ARG_STORY_PROGRESS: return story_progress(g);
    case ARG_BIT: return bit_mask(g, script_byte(g, operand));
    case ARG_ALL_BUT_BIT: return bit_clear_mask(g, script_byte(g, operand));
    case ARG_FLAG: return script_byte(g, operand) != 0;
    default: return 0;
    }
}

/* A formation's place: the top bits of its column and row say the way it
 * faces there (event/party.h). */
enum { FACING_BIT = 0x80, TILE_BITS = 0x7F };

static FormationPlace decode_FormationPlace(const GameState *g, const EventArg *arg, uint16_t at)
{
    if (arg->kind != ARG_FORMATION_PLACE)
        return (FormationPlace){ { 0, 0 }, FACING_UP };
    uint8_t column = script_byte(g, (uint16_t)(at + arg->operand));
    uint8_t row = script_byte(g, (uint16_t)(at + arg->operand + 1));
    Facing facing = formation_facing((column & FACING_BIT) != 0, (row & FACING_BIT) != 0);
    return (FormationPlace){ { (uint8_t)(column & TILE_BITS), (uint8_t)(row & TILE_BITS) }, facing };
}

/* The searchlight's moves: for each corner, its x's start and end, then its
 * y's. */
static SearchlightMove decode_SearchlightMove(const GameState *g, const EventArg *arg, uint16_t at)
{
    SearchlightMove m = { { { 0, 0 } }, { { 0, 0 } } };
    if (arg->kind != ARG_SEARCHLIGHT_MOVE)
        return m;
    uint16_t from = (uint16_t)(at + arg->operand);
    for (unsigned k = 0; k < SEARCHLIGHT_CORNERS; k++, from = (uint16_t)(from + 4)) {
        m.from[k] = (WindowPoint){ script_byte(g, from), script_byte(g, (uint16_t)(from + 2)) };
        m.to[k] = (WindowPoint){ script_byte(g, (uint16_t)(from + 1)), script_byte(g, (uint16_t)(from + 3)) };
    }
    return m;
}

/* A jump is by the distance in its jump byte, forward or back from that byte. */
static EventStep jump(GameState *g, const EventOpcode *row, uint16_t at)
{
    uint16_t from = (uint16_t)(at + row->jump);
    uint8_t distance = script_byte(g, from);
    uint16_t to = row->back ? (uint16_t)(from - distance) : (uint16_t)(from + distance);
    return (EventStep){ to, true };
}

EventStep event_follow(GameState *g, const EventOpcode *row, uint16_t at, EventFlow flow)
{
    uint16_t after = (uint16_t)(at + row->length);
    switch (flow.kind) {
    case FLOW_NEXT: return (EventStep){ after, true };
    case FLOW_END_TURN: return (EventStep){ after, false };
    case FLOW_STAY: return (EventStep){ at, false };
    case FLOW_AGAIN: return (EventStep){ at, true };
    case FLOW_JUMP: return jump(g, row, at);
    case FLOW_NEXT_AFTER: return (EventStep){ (uint16_t)(at + flow.to), true };
    default: return (EventStep){ flow.to, true };
    }
}

EventCommand event_decode(const GameState *g, Platform *pf, const EventOpcode *row, uint16_t at)
{
    EventCommand c = { .self = object_current(g), .at = at, .platform = pf };
#define DECODE_PARAMETER(type, name) c.name = (type)decode(g, &row->name, at);
    EVENT_PARAMETERS(DECODE_PARAMETER)
#undef DECODE_PARAMETER
#define DECODE_RECORD(type, name) c.name = decode_##type(g, &row->name, at);
    EVENT_RECORD_PARAMETERS(DECODE_RECORD)
#undef DECODE_RECORD
    return c;
}

EventStep event_perform(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at)
{
    EventCommand c = event_decode(g, pf, row, at);
    return event_follow(g, row, at, row->run(g, &c));
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
EventTurnEnd event_run_turn(GameState *g, FieldObject object, const EventRunner *r, uint16_t *stopped_at)
{
    set_event_commands_left(g, event_commands_per_turn(g));
    uint16_t at = event_object_script_position(g, object);
    object_make_current(g, object);
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
    if (!object_current_is_one(g) || !r->state_is_ours(r->ctx))
        return TURN_NOT_OURS;
    set_event_object_script_position(g, object_current(g), at);
    return TURN_OVER;
}

/* For the original's code and shadow mode: the map setup keeps the
 * openings' end, the object count doubled, in event_object_end. */
static void leave_openings_end(GameState *g, uint8_t count) { set_event_object_end(g, (uint16_t)(count * 2)); }

/* An object's opening: its script up to its first zero byte, after which the
 * per-frame turns carry on. */
static EventTurnEnd run_opening(GameState *g, FieldObject object, const EventRunner *r, uint16_t *stopped_at)
{
    uint16_t at = event_object_script_position(g, object);
    object_make_current_recognised(g, object);   /* the map setup sets the whole word ($C0:58FB) */
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
    if (!object_current_is_one(g) || !r->state_is_ours(r->ctx))
        return TURN_NOT_OURS;
    set_event_object_script_position(g, object_current(g), (uint16_t)(at + 1));
    return TURN_OVER;
}

// $C0:58DE (inside the map setup routine)
EventTurnEnd event_run_openings(GameState *g, const EventRunner *r, FieldObject *object, uint16_t *stopped_at)
{
    uint8_t count = event_object_count(g);
    ORIGINAL_EFFECT(leave_openings_end(g, count));
    FieldObject next = 0;
    do {
        *object = next;
        /* the object's cells are reached through the data bank, which a
         * command the original ran may have left elsewhere */
        if (!r->state_is_ours(r->ctx))
            return TURN_NOT_OURS_BEFORE;
        object_start_on_the_map(g, next);
        if (object_runs_a_script(g, next)) {
            EventTurnEnd e = run_opening(g, next, r, stopped_at);
            if (e != TURN_OVER)
                return e;
            /* the original's loop goes on from the object its opening left
             * running, which a command could have changed (none the game
             * runs does; the opening kept its place, above) */
            next = object_current(g);
        }
        next++;
    } while (next != count);
    return TURN_OVER;
}

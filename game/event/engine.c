#include "event/script.h"
#include "event/script.h"
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
 * Each command runs by its row, or by the command's own function where the
 * row reads its operands itself (run_command, below), timed as its handler;
 * a command outside its contract is reported and ends the turn. */
#include "event/engine.h"
#include "charge.h"
#include "event/calls.h"
#include "event/event_data.h"
#include "event/party.h"
#include "event/records.h"
#include "event/variables.h"

#include "charge.h"
#include "core/program.h"
#include "port/vars_ram.h"

#include <string.h>

/* ---- The table, by opcode ------------------------------------------------------------- */

static const EventOpcodes *const every_file[] = {
    &event_animation_opcodes, &event_calls_opcodes,  &event_characters_opcodes, &event_colours_opcodes,
    &event_dialog_opcodes,    &event_items_opcodes,  &event_facing_opcodes,
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

#if CT_SCRIPT_TEXT
/* The commands that read their own operands, by name, for a script's text. */
static const struct {
    uint8_t opcode;
    const char *name;
} undecoded_names[] = {
#define EVENT_UNDECODED_COMMAND_NAME(name, op) { op, #name },
    EVENT_UNDECODED_COMMANDS(EVENT_UNDECODED_COMMAND_NAME)
#undef EVENT_UNDECODED_COMMAND_NAME
};
enum { UNDECODED_PREFIX = sizeof "event_" - 1 };

const char *event_command_name(const EventOpcode *row)
{
    if (row->name)
        return row->name;
    for (unsigned i = 0; i < sizeof undecoded_names / sizeof undecoded_names[0]; i++)
        if (undecoded_names[i].opcode == row->opcode)
            return undecoded_names[i].name + UNDECODED_PREFIX;
    return NULL;
}

uint16_t event_command_length(const EventOpcode *row, const uint8_t *command, uint16_t available)
{
    uint16_t length = row->measure ? row->measure(command, available) : row->length;
    return length <= available ? length : 0;
}
#endif

/* ---- What a command may reach -----------------------------------------------------------
 *
 * The operations reach the object tables by object, where the original
 * indexes them with the object's offset (field/object.h); the two agree for
 * the objects the game makes, which are all even offsets inside the tables.
 * And they read operands from the script buffer, which ends with the bank: a
 * command in its last bytes would read past it. No script is that long, and
 * no game object odd or past the 64, so a command outside these bounds breaks
 * a checked contract. */

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
 * through the bank; the port refuses one that would leave it. */
enum { LAST_BIT_READ = 0xDF, LAST_CLEAR_BIT_READ = 0xD7 };

/* Whether an operand names what its kind says, whatever the game's state:
 * an object one of the tables', a place of the party's, a bit the tables
 * hold, a word inside the story memory. */
static bool names_what_it_can(const EventArg *arg, uint16_t operand)
{
    switch (arg->kind) {
    case ARG_OBJECT: return field_names_object((uint8_t)operand);
    case ARG_MEMBER:
    case ARG_MEMBER_OBJECT:
    case ARG_MEMBER_PRESENT: return a_place((uint8_t)operand);
    case ARG_BIT: return operand <= LAST_BIT_READ;
    case ARG_ALL_BUT_BIT: return operand <= LAST_CLEAR_BIT_READ;
    case ARG_STORY_WORD: return story_holds(operand, AS_WORD);
    default: return true;
    }
}

/* ... and whether it does in this state: a party member's place is one the
 * party can be read at, and holds the member the command takes. */
static bool names_it_now(const GameState *g, const EventOpcode *row, EventArgKind kind, uint8_t operand)
{
    switch (kind) {
    case ARG_MEMBER: return member_place_readable(g, (PartyPlace)(operand >> 1));
    case ARG_MEMBER_OBJECT:
    case ARG_MEMBER_PRESENT: return member_named(g, operand, row->member_may_be_absent);
    default: return true;
    }
}

/* Whether one of the command's operands names what its kind says. */
static bool names_what_it_says(const GameState *g, const EventOpcode *row, const EventArg *arg, uint16_t at)
{
    if (arg->kind == ARG_STORY_WORD)   /* the word the command reads is in the story memory */
        return names_what_it_can(arg, script_word(g, (uint16_t)(at + arg->operand)));
    uint8_t operand = script_byte(g, (uint16_t)(at + arg->operand));
    return names_what_it_can(arg, operand) && names_it_now(g, row, arg->kind, operand);
}

/* How the row decodes parameter `p`. */
static const EventArg *row_arg(const EventOpcode *row, EventParameter p)
{
    switch (p) {
#define ROW_ARG(type, name) case EVENT_PARAMETER_##name: return &row->name;
    EVENT_PARAMETERS(ROW_ARG)
#undef ROW_ARG
    default: return NULL;
    }
}

/* The parameters the row names (the rest read nothing), as a run of the
 * named list: the one walk both the check and the decoding take. */
typedef struct {
    const uint8_t *first, *end;
} NamedParameters;

static NamedParameters named_parameters(const EventOpcode *row)
{
    if (row->named)
        return (NamedParameters){ row->named, row->named + row->named_count };
    return (NamedParameters){ &event_named_parameters[event_named_from[row->opcode]],
                              &event_named_parameters[event_named_from[row->opcode + 1]] };
}

bool event_runs_here(const GameState *g, const EventOpcode *row, uint16_t at)
{
    if (!object_current_is_one(g) || at > EVENT_SCRIPT_BYTES - bytes_read(row))
        return false;
    NamedParameters named = named_parameters(row);
    for (const uint8_t *p = named.first; p != named.end; p++)
        if (!names_what_it_says(g, row, row_arg(row, (EventParameter)*p), at))
            return false;
#define CHECK_RECORD(type, name)                                                     \
    if (row->name.kind != ARG_NONE && !names_what_it_says(g, row, &row->name, at)) \
        return false;
    EVENT_RECORD_PARAMETERS(CHECK_RECORD)
#undef CHECK_RECORD
    if (!row->accepts)
        return true;
    EventCommand command = event_decode(g, NULL, row, at);
    return row->accepts(g, &command);
}

/* ---- Running a command -------------------------------------------------------------------- */

/* The bytes a command's operands are read from: the script's, or a copy of
 * the command's own (a builder's, event_record_read). */
typedef struct {
    const GameState *g;
    uint16_t at;
    const uint8_t *command;
} CommandBytes;

static uint8_t command_byte(const CommandBytes *b, uint8_t k)
{
    return b->command ? b->command[k] : script_byte(b->g, (uint16_t)(b->at + k));
}

static uint16_t command_word(const CommandBytes *b, uint8_t k)
{
    return b->command ? (uint16_t)(b->command[k] | b->command[k + 1] << 8) : script_word(b->g, (uint16_t)(b->at + k));
}

/* What a parameter's operand bytes say, the same every time the command
 * runs: its value, or where the kind reads the game's state, the operand it
 * reads it by. */
static uint32_t read_operand(const CommandBytes *b, const EventArg *arg)
{
    uint8_t n = arg->operand;
    switch (arg->kind) {
    case ARG_CONSTANT: return arg->constant;
    case ARG_BYTE:
    case ARG_VARIABLE:
    case ARG_VARIABLE_BYTE:
    case ARG_VARIABLE_WORD:
    case ARG_MEMBER_OBJECT:
    case ARG_MEMBER_PRESENT:
    case ARG_BIT:
    case ARG_ALL_BUT_BIT: return command_byte(b, n);
    case ARG_BYTE_PLUS_ONE: return (uint8_t)(command_byte(b, n) + 1);
    case ARG_BITS: return (uint32_t)(command_byte(b, n) >> arg->shift) & arg->constant;
    case ARG_WORD:
    case ARG_STORY_WORD: return command_word(b, n);
    case ARG_WORD_BITS: return (uint32_t)(command_word(b, n) >> arg->shift) & arg->constant;
    case ARG_ADDRESS: return command_word(b, n) | (uint32_t)command_byte(b, (uint8_t)(n + 2)) << 16;
    case ARG_OBJECT: return field_linked_object(field_link_stored(command_byte(b, n)));
    case ARG_MEMBER: return (PartyPlace)(command_byte(b, n) >> 1);
    default: return 0;
    }
}

/* The bytes of the command, from its opcode, that a parameter's operand
 * takes up to. */
static uint8_t operand_end(const EventArg *arg)
{
    switch (arg->kind) {
    case ARG_CONSTANT:
    case ARG_SELF:
    case ARG_STORY_PROGRESS: return 1;
    case ARG_WORD:
    case ARG_WORD_BITS:
    case ARG_STORY_WORD: return (uint8_t)(arg->operand + 2);
    case ARG_ADDRESS: return (uint8_t)(arg->operand + 3);
    default: return (uint8_t)(arg->operand + 1);
    }
}

/* A parameter's value, from what its operand said: the game's state looked
 * up where its kind reads it. */
static uint32_t looked_up(const GameState *g, EventArgKind kind, uint32_t read)
{
    switch (kind) {
    case ARG_VARIABLE_BYTE: return variable_byte(g, (uint8_t)read);
    case ARG_VARIABLE_WORD: return variable_word(g, (uint8_t)read);
    case ARG_SELF: return object_current(g);
    case ARG_MEMBER_OBJECT: return member_object(g, (PartyPlace)(read >> 1));
    case ARG_MEMBER_PRESENT: return place_taken(g, (PartyPlace)(read >> 1));
    case ARG_STORY_WORD: return story_word(g, (StoryOffset)read);
    case ARG_STORY_PROGRESS: return story_progress(g);
    case ARG_BIT: return bit_mask(g, (uint8_t)read);
    case ARG_ALL_BUT_BIT: return bit_clear_mask(g, (uint8_t)read);
    default: return read;
    }
}

static uint32_t decode(const GameState *g, const EventArg *arg, uint16_t at)
{
    const CommandBytes bytes = { g, at, NULL };
    return looked_up(g, arg->kind, read_operand(&bytes, arg));
}

/* A formation's place: the top bits of its column and row say the way it
 * faces there (event/party.h). */
enum { FACING_BIT = 0x80, TILE_BITS = 0x7F };

/* An effect on colours (event/colours.c): its form, then the form's
 * operands. The object's form rotates or fades a run of its colours named by
 * a byte's two nibbles (the first, from its palette's, and how many) or
 * writes its own over them from the low nibble's on; the screen's names the
 * run's first of the 256, and its length in colours or the bytes of its own.
 * Colours of the script's own follow a word, their bytes' count plus two. */
enum {
    STOP_FORM = 0x00,
    ROTATE_DOWN_FORM = 0x20,
    ROTATE_UP_FORM = 0x30,
    FADE_TO_WHITE_FORM = 0x40,
    FADE_TO_BLACK_FORM = 0x50,
    COLOURS_FORM = 0x80,
    FORM_TYPE = 0xF0,
    FORM_LOW = 0x0F,
};

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
    case FLOW_END_TURN_AFTER: return (EventStep){ (uint16_t)(at + flow.to), false };
    default: return (EventStep){ flow.to, true };
    }
}

static FormationPlace decode_FormationPlace(const GameState *g, const EventArg *arg, uint16_t at)
{
    if (arg->kind != ARG_FORMATION_PLACE)
        return (FormationPlace){ { 0, 0 }, FACING_UP };
    uint8_t column = script_byte(g, (uint16_t)(at + arg->operand));
    uint8_t row = script_byte(g, (uint16_t)(at + arg->operand + 1));
    Facing facing = formation_facing((column & FACING_BIT) != 0, (row & FACING_BIT) != 0);
    return (FormationPlace){ { (uint8_t)(column & TILE_BITS), (uint8_t)(row & TILE_BITS) }, facing };
}

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

/* A colour effect's command's bytes, its opcode's included, from its form
 * byte and the count word the colours of the script's own follow (their
 * bytes plus two). */
static uint16_t colour_effect_length(uint8_t form, uint16_t count, bool object)
{
    uint8_t type = form & FORM_TYPE;
    if (object && form == STOP_FORM)
        return 2;
    if (object && (form == ROTATE_DOWN_FORM || form == ROTATE_UP_FORM))
        return 4;
    if (type == FADE_TO_WHITE_FORM || type == FADE_TO_BLACK_FORM)
        return object ? 5 : 6;
    if (type == COLOURS_FORM)
        return (uint16_t)((object ? 2 : 3) + count);
    return 2;
}

/* The colours' count word is after the form byte, and the screen's first
 * colour's. */
static uint16_t measure_colours(const uint8_t *command, uint16_t available, bool object)
{
    uint16_t count_at = object ? 2 : 3;
    if (available < 2 || ((command[1] & FORM_TYPE) == COLOURS_FORM && available < count_at + 2))
        return 0;
    uint16_t count = (command[1] & FORM_TYPE) == COLOURS_FORM
                         ? (uint16_t)(command[count_at] | command[count_at + 1] << 8) : 0;
    return colour_effect_length(command[1], count, object);
}

uint16_t event_measure_object_colours(const uint8_t *command, uint16_t available)
{
    return measure_colours(command, available, true);
}

uint16_t event_measure_screen_colours(const uint8_t *command, uint16_t available)
{
    return measure_colours(command, available, false);
}

static ColourEffect decode_ColourEffect(const GameState *g, const EventArg *arg, uint16_t at)
{
    ColourEffect e = { COLOURS_UNCHANGED, 0, 0, 0, 0, 0, 0, 0, 2 };
    if (arg->kind != ARG_OBJECT_COLOUR_EFFECT && arg->kind != ARG_SCREEN_COLOUR_EFFECT)
        return e;
    bool object = arg->kind == ARG_OBJECT_COLOUR_EFFECT;
    uint16_t form_at = (uint16_t)(at + arg->operand);
    uint8_t form = script_byte(g, form_at), type = form & FORM_TYPE;
    uint8_t next = script_byte(g, (uint16_t)(form_at + 1));
    e.animation = form;
    if (object && form == STOP_FORM) {
        e.kind = COLOURS_STOP;
    } else if (object && (form == ROTATE_DOWN_FORM || form == ROTATE_UP_FORM)) {
        e.kind = COLOURS_ROTATE;
        e.first = next >> 4;
        e.count = next & FORM_LOW;
        e.period = script_byte(g, (uint16_t)(form_at + 2));
    } else if (type == FADE_TO_WHITE_FORM || type == FADE_TO_BLACK_FORM) {
        uint16_t rest = (uint16_t)(form_at + (object ? 2 : 3));
        e.kind = COLOURS_FADE;
        e.first = object ? (uint8_t)(next >> 4) : next;
        e.count = object ? (uint8_t)(next & FORM_LOW) : script_byte(g, (uint16_t)(form_at + 2));
        e.levels = script_byte(g, rest);
        e.period = script_byte(g, (uint16_t)(rest + 1));
    } else if (type == COLOURS_FORM) {
        uint16_t count_at = (uint16_t)(form_at + (object ? 1 : 2));
        e.kind = COLOURS_SET;
        e.first = object ? (uint8_t)(form & FORM_LOW) : next;
        e.colours_at = (uint16_t)(count_at + 2);
        e.bytes = (uint16_t)(script_word(g, count_at) - 2);
    }
    e.length = colour_effect_length(form, type == COLOURS_FORM ? (uint16_t)(e.bytes + 2) : 0, object);
    return e;
}

static void decode_into(EventCommand *c, const GameState *g, Platform *pf, const EventOpcode *row, uint16_t at)
{
    *c = (EventCommand){ .self = object_current(g), .at = at, .platform = pf };
    NamedParameters named = named_parameters(row);
    for (const uint8_t *p = named.first; p != named.end; p++)
        switch ((EventParameter)*p) {
#define DECODE_PARAMETER(type, name) \
        case EVENT_PARAMETER_##name: c->name = (type)decode(g, &row->name, at); break;
        EVENT_PARAMETERS(DECODE_PARAMETER)
#undef DECODE_PARAMETER
        default: break;
        }
#define DECODE_RECORD(type, name) c->name = decode_##type(g, &row->name, at);
    EVENT_RECORD_PARAMETERS(DECODE_RECORD)
#undef DECODE_RECORD
}

EventCommand event_decode(const GameState *g, Platform *pf, const EventOpcode *row, uint16_t at)
{
    EventCommand c;
    decode_into(&c, g, pf, row, at);
    return c;
}

/* The work the original's handlers spend copying a command's operands
 * before acting on them, kept as part of each handler's time; the port
 * copies nothing. */
static uint8_t operand_copy_work(uint8_t opcode)
{
    switch (opcode) {
    case 0x12: case 0x14: return 4; /* byte comparison and resume offset */
    case 0x13: case 0x15: return 6; /* word comparison and resume offset */
    case 0xE6: return 3; /* layers and drift */
    case 0x16: /* comparison resume offset */
    case 0xDC: case 0xDD: case 0xDE: /* location */
    case 0x48: case 0x49: case 0x4C: case 0x4D: /* memory operand offset */
    case 0x58: case 0x59: /* story value */
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: /* variable value */
    case 0x5D: case 0x5E: case 0x60: case 0x61: /* arithmetic amount */
        return 2;
    case 0xB5: case 0xB6: /* remaining half of the following offset */
    case 0xF0: /* sound operand */
    case 0x56: case 0x65: case 0x66: /* story value or flag operand */
    case 0x4F: case 0x5B: case 0x5F: /* byte value or amount */
    case 0x63: case 0x64: case 0x67: case 0x69: case 0x6B: /* flags */
        return 1;
    default: return 0;
    }
}

/* The decoded command performed, and where the script goes on. */
static EventStep performed(GameState *g, const EventOpcode *row, uint16_t at, const EventCommand *c)
{
    charge_accesses(g, operand_copy_work(row->opcode));
    EventFlow flow = row->run(g, c);
    if (flow.kind == FLOW_JUMP)
        charge_accesses(g, 2); /* the selected jump distance */
    return event_follow(g, row, at, flow);
}

EventStep event_perform(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at)
{
    EventCommand c;
    decode_into(&c, g, pf, row, at);
    return performed(g, row, at, &c);
}

/* ---- Commands read ahead (event/records.h) ----------------------------------------------------- */

/* The command decoded from its record, where the port runs it in this
 * state: as event_runs_here and decode_into, with what its bytes say taken
 * from the record. False where it doesn't, and the engine looks at the
 * command's bytes itself. */
static bool decoded_from_record(EventCommand *c, const GameState *g, Platform *pf, const EventOpcode *row, uint16_t at,
                                const EventRecord *record)
{
    if (!object_current_is_one(g))
        return false;
    *c = (EventCommand){ .self = object_current(g), .at = at, .platform = pf };
    NamedParameters named = named_parameters(row);
    const uint32_t *values = record->value;
    for (const uint8_t *p = named.first; p != named.end; p++, values++) {
        EventArgKind kind = (EventArgKind)(*values >> EVENT_RECORD_KIND_SHIFT);
        uint32_t read = *values & EVENT_RECORD_VALUE_BITS;
        /* a member's place was read halved, as the command takes it */
        if (!names_it_now(g, row, kind, (uint8_t)(kind == ARG_MEMBER ? read << 1 : read)))
            return false;
        uint32_t value = looked_up(g, kind, read);
        switch ((EventParameter)*p) {
#define TAKE_PARAMETER(type, name) \
        case EVENT_PARAMETER_##name: c->name = (type)value; break;
        EVENT_PARAMETERS(TAKE_PARAMETER)
#undef TAKE_PARAMETER
        default: break;
        }
    }
#define DECODE_RECORD(type, name) c->name = decode_##type(g, &row->name, at);
    EVENT_RECORD_PARAMETERS(DECODE_RECORD)
#undef DECODE_RECORD
    return !row->accepts || row->accepts(g, c);
}

unsigned event_record_read(const uint8_t *command, uint16_t available, EventRecord *out)
{
    const EventOpcode *row = event_opcode(command[0]);
    if (!row || !row->run)
        return 0;
#define DECODES_A_RECORD(type, name) || row->name.kind != ARG_NONE
    if (0 EVENT_RECORD_PARAMETERS(DECODES_A_RECORD))
        return 0;
#undef DECODES_A_RECORD
    NamedParameters named = named_parameters(row);
    uint8_t bytes = 1;
    for (const uint8_t *p = named.first; p != named.end; p++) {
        uint8_t end = operand_end(row_arg(row, (EventParameter)*p));
        bytes = end > bytes ? end : bytes;
    }
    if (bytes > EVENT_RECORD_BYTES || bytes > available)
        return 0;
    const CommandBytes from = { NULL, 0, command };
    uint8_t values = 0;
    for (const uint8_t *p = named.first; p != named.end; p++) {
        const EventArg *arg = row_arg(row, (EventParameter)*p);
        uint16_t operand = arg->kind == ARG_STORY_WORD ? command_word(&from, arg->operand)
                           : operand_end(arg) > 1 ? command_byte(&from, arg->operand) : 0;
        uint32_t read = read_operand(&from, arg);
        if (!names_what_it_can(arg, operand) || read > EVENT_RECORD_VALUE_BITS)
            return 0;
        out->value[values++] = (uint32_t)arg->kind << EVENT_RECORD_KIND_SHIFT | read;
    }
    out->opcode = command[0];
    out->bytes = bytes;
    out->values = values;
    out->unused = 0;
    memset(out->made_from, 0, sizeof out->made_from);
    memcpy(out->made_from, command, bytes);
    return EVENT_RECORD_HEAD + 4u * values;
}

uint32_t event_records_language(void)
{
    static uint32_t language;
    if (language)
        return language;
    uint32_t h = 0x811C9DC5u;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 0x01000193u)
    MIX(EVENT_PARAMETER_COUNT), MIX(EVENT_RECORD_BYTES), MIX(EVENT_RECORD_HEAD), MIX(EVENT_RECORD_KIND_SHIFT);
    for (unsigned op = 0; op < OPCODES; op++) {
        const EventOpcode *row = event_opcode((uint8_t)op);
        MIX(row != NULL);
        if (!row)
            continue;
        MIX(row->run != NULL), MIX(row->length), MIX(row->member_may_be_absent);
        /* a row that decodes a record of the language's has no records */
#define DECODES_A_RECORD(type, name) || row->name.kind != ARG_NONE
        MIX(0 EVENT_RECORD_PARAMETERS(DECODES_A_RECORD));
#undef DECODES_A_RECORD
        /* the parameters it names, and how (the rest decode nothing): a few
         * a row, so that the GBA works it out in a moment as a script loads */
        NamedParameters named = named_parameters(row);
        for (const uint8_t *p = named.first; p != named.end; p++) {
            const EventArg *arg = row_arg(row, (EventParameter)*p);
            MIX(*p), MIX(arg->kind), MIX(arg->operand), MIX(arg->constant), MIX(arg->shift);
        }
    }
#undef MIX
    language = h | 1u;   /* never 0, which is not worked out yet */
    return language;
}

/* ---- Turns ------------------------------------------------------------------------------ */

/* The last script offset reads the first byte past bank $7F, an I/O register
 * that holds whatever was last on the bus; no script is that long, and a turn
 * that gets there breaks the script-boundary contract. */
enum { SCRIPT_END = 0xFFFF };

static EventTurnEnd broken_turn(GameState *g, EventTurnEnd end)
{
    program_breaks_a_contract(g, 0xC05A93, "an event turn left its script or object tables");
    return end;
}

/* The turn's budget of commands is the game's, in event_commands_left, which
 * the turn counts down in the cell. */
static bool spend_command(GameState *g)
{
    uint8_t left = (uint8_t)(event_commands_left(g) - 1);
    set_event_commands_left(g, left);
    return left != 0;
}

static bool run_command(GameState *g, Platform *pf, uint16_t at, EventStep *step);

// $C0:5A93
EventTurnEnd event_run_turn(GameState *g, Platform *pf, FieldObject object, uint16_t *stopped_at)
{
    if (object >= FIELD_OBJECTS) {
        *stopped_at = 0;
        return broken_turn(g, TURN_NOT_OURS_BEFORE);
    }
    set_event_commands_left(g, event_commands_per_turn(g));
    uint16_t at = event_object_script_position(g, object);
    object_make_current(g, object);
    EventStep st;
    do {
        *stopped_at = at;
        if (at == SCRIPT_END)
            return broken_turn(g, TURN_OFF_THE_SCRIPT);
        if (!run_command(g, pf, at, &st))
            return TURN_HANDED_BACK;
        at = st.next;
    } while (st.go_on && spend_command(g));
    *stopped_at = at;
    /* the commands may have named another object running */
    if (!object_current_is_one(g))
        return broken_turn(g, TURN_NOT_OURS);
    set_event_object_script_position(g, object_current(g), at);
    return TURN_OVER;
}

/* An object's opening is its script's first function. */
enum { OPENING_FUNCTION = 0 };

/* An object's script run from `at` up to its first zero byte, all at once:
 * the running object then goes on past the zero byte in its per-frame turns.
 * `*stopped_at` is where it stopped, the zero byte where it got there. */
static EventTurnEnd run_to_the_first_zero(GameState *g, Platform *pf, uint16_t at, uint16_t *stopped_at)
{
    EventStep st;
    for (;;) {
        *stopped_at = at;
        if (at == SCRIPT_END)
            return broken_turn(g, TURN_OFF_THE_SCRIPT);
        if (script_byte(g, at) == 0)
            break;
        if (!run_command(g, pf, at, &st))
            return TURN_HANDED_BACK;
        at = st.next;
    }
    /* the commands may have named another object running */
    if (!object_current_is_one(g))
        return broken_turn(g, TURN_NOT_OURS);
    set_event_object_script_position(g, object_current(g), (uint16_t)(at + 1));
    return TURN_OVER;
}

/* An object's opening: its script up to its first zero byte, after which the
 * per-frame turns carry on. */
static EventTurnEnd run_opening(GameState *g, Platform *pf, FieldObject object, uint16_t *stopped_at)
{
    uint16_t at = event_object_script_position(g, object);
    object_make_current_recognised(g, object);   /* the map setup sets the whole word ($C0:58FB) */
    return run_to_the_first_zero(g, pf, at, stopped_at);
}

// $C0:597D
/* The running object starts over in its script, as each character on the
 * map does after a menu changed the party: its opening run again from its
 * script's start, as at the map's start; then every call it was interrupted
 * in forgotten, the object at the least urgent level. */
EventTurnEnd event_restart_the_running_object(GameState *g, Platform *pf, uint16_t *stopped_at)
{
    vars_peek(g, true);
    bool valid = object_current_is_one(g);
    vars_peek(g, false);
    if (!valid) {
        *stopped_at = 0;
        return broken_turn(g, TURN_NOT_OURS_BEFORE);
    }
    uint16_t at = object_function_start(g, object_current(g), OPENING_FUNCTION);
    EventTurnEnd end = run_to_the_first_zero(g, pf, at, stopped_at);
    if (end == TURN_OVER)
        object_forget_its_calls(g, object_current(g));
    return end;
}

/* The map's own script is object 0's: as the map starts, and again as it
 * comes back after a menu or a battle, its second function runs up to its
 * first zero byte, all at once, before any object's turn. Where it stops is
 * kept nowhere: the function starts afresh every time. */
enum { MAP_OBJECT = 0, MAP_STARTS_FUNCTION = 1 };

// $C0:595C
EventTurnEnd event_run_map_script_commands(GameState *g, Platform *pf, uint16_t *stopped_at)
{
    object_make_current_recognised(g, MAP_OBJECT);
    uint16_t at = object_function_start(g, MAP_OBJECT, MAP_STARTS_FUNCTION);
    EventStep st;
    for (;;) {
        *stopped_at = at;
        /* past the buffer the script runs on into the next bank, which no
         * map's does: the original's, whose bus it is */
        if (at >= EVENT_SCRIPT_BYTES)
            return broken_turn(g, TURN_OFF_THE_SCRIPT);
        if (script_byte(g, at) == 0)
            return TURN_OVER;
        if (!run_command(g, pf, at, &st))
            return TURN_HANDED_BACK;
        at = st.next;
    }
}

// $C0:58DE (inside the map setup routine)
EventTurnEnd event_run_openings(GameState *g, Platform *pf, FieldObject *object, uint16_t *stopped_at)
{
    uint8_t count = event_object_count(g);
    if (count == 0 || count > FIELD_OBJECTS) {
        *object = 0;
        *stopped_at = 0;
        return broken_turn(g, TURN_NOT_OURS_BEFORE);
    }
    FieldObject next = 0;
    do {
        *object = next;
        object_start_on_the_map(g, next);
        if (object_runs_a_script(g, next)) {
            EventTurnEnd e = run_opening(g, pf, next, stopped_at);
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

/* ---- The program's runner -------------------------------------------------------------------- */

/* The handlers are in bank $C0, where the table names them; a command's time
 * is its handler's. */
enum { HANDLERS_BANK = 0xC00000 };

typedef bool (*UndecodedCommand)(GameState *g, Platform *pf, uint16_t at, EventStep *step);

static UndecodedCommand undecoded_command_for(uint8_t opcode)
{
    switch (opcode) {
#define EVENT_UNDECODED_COMMAND_CASE(name, op) case op: return name;
    EVENT_UNDECODED_COMMANDS(EVENT_UNDECODED_COMMAND_CASE)
#undef EVENT_UNDECODED_COMMAND_CASE
    default: return NULL;
    }
}

static uint32_t handler_of(const GameState *g, uint8_t opcode) { return HANDLERS_BANK | command_handler(g, opcode); }

static bool run_command(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    uint8_t opcode = script_byte(g, at);
    const EventOpcode *row = event_opcode(opcode);
    const EventRecord *record = row && row->run ? event_record_at(g, at, opcode) : NULL;
    if (record) {
        EventCommand c;
        vars_peek(g, true);
        bool runs_here = decoded_from_record(&c, g, pf, row, at, record);
        vars_peek(g, false);
        if (runs_here) {
            TIMED_AS(g, handler_of(g, opcode), *step = performed(g, row, at, &c));
            return true;
        }
    }
    UndecodedCommand undecoded = undecoded_command_for(opcode);
    /* whether the port runs it is the program's question, not the game's
     * work: it takes none of the original's time (port/vars_ram.h) */
    vars_peek(g, true);
    bool runs_here = row && event_runs_here(g, row, at);
    vars_peek(g, false);
    if (row && !row->run && undecoded && runs_here) {
        bool goes_on = false;
        TIMED_AS(g, handler_of(g, opcode), goes_on = undecoded(g, pf, at, step));
        return goes_on;
    }
    if (!row || !row->run || !runs_here) {
        program_breaks_a_contract(g, handler_of(g, opcode),
                                  row ? "an event command outside its contract" : "an invalid event opcode");
        return false;
    }
    TIMED_AS(g, handler_of(g, opcode), *step = event_perform(g, pf, row, at));
    return true;
}

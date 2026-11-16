/* The command scripts' interpreter (menu/script.h): reading a command by its
 * row, and running an object's script for a frame. */
#include "menu/script.h"

#include <string.h>

#include "menu/menu_data.h"
#include "menu/records.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "vars.h"
#include "platform/video.h"
#include "wram.h"

/* ---- The bytes a command names ---------------------------------------------------------------- */

/* A cell is in low RAM (menu_script_runs_here sees to it). */
static uint32_t cell_address(uint16_t at) { return 0x7E0000u + at; }

uint8_t script_byte_at(const GameState *g, MenuObject self, ByteRef b)
{
    return b.of_the_object ? menu_object_byte(g, self, b.at) : wram_read8(g, cell_address(b.at));
}

enum { LOW_RAM_END = 0x2000 };

bool script_byte_is_the_consoles(ByteRef b) { return !b.of_the_object && b.at >= LOW_RAM_END; }

void script_set_byte_at(GameState *g, MenuObject self, ByteRef b, uint8_t v)
{
    if (b.of_the_object)
        menu_set_object_byte(g, self, b.at, v);
    else
        wram_write8(g, cell_address(b.at), v);
}

/* A setting of the picture's a command changes, which the scripts name by
 * the address that holds it: only the sprites' sizes and their tiles' place,
 * which an ending's scripts set (menu_script_runs_here takes no other). */
void script_set_the_consoles_byte(Platform *pf, uint32_t handler, uint16_t at, uint8_t v)
{
    if (vid_setting_at(at) != VID_SETTING_SPRITES)
        return;
    VidObjConfig sprites = vid_obj_config_decode(v);
    vid_set_obj_config(pf, PF_AT(handler), &sprites);
}

/* ---- Reading a command -------------------------------------------------------------------------- */

const ScriptOpcode *menu_script_opcode(uint8_t opcode)
{
    const ScriptOpcode *row = &menu_script_opcodes.rows[opcode < menu_script_opcodes.count ? opcode : 0];
    return opcode < menu_script_opcodes.count && row->opcode == opcode ? row : NULL;
}

/* A command's operands are read on from its opcode, across the end of the
 * bank if they reach it: from the script's run of the store's bytes, found
 * once for the command, or from a copy of the command's own (a builder's,
 * menu_record_read). */
typedef struct {
    const GameState *g;
    uint32_t at;
    MenuScriptRun run;
    const uint8_t *command;
} CommandBytes;

static uint8_t operand(const CommandBytes *c, uint8_t n)
{
    return c->command ? c->command[n] : menu_script_byte_in(c->g, &c->run, c->at + n);
}
static uint16_t operand_word(const CommandBytes *c, uint8_t n)
{
    return (uint16_t)(operand(c, n) | operand(c, (uint8_t)(n + 1)) << 8);
}

static uint32_t argument(const CommandBytes *b, ScriptArg a)
{
    switch (a.kind) {
    case ARG_CONSTANT: return a.constant;
    case ARG_BYTE: return operand(b, a.operand);
    case ARG_SIGNED: return (uint32_t)(int32_t)(int8_t)operand(b, a.operand);
    case ARG_WORD: return operand_word(b, a.operand);
    case ARG_LONG: return (uint32_t)operand(b, (uint8_t)(a.operand + 2)) << 16 | operand_word(b, a.operand);
    case ARG_WORD_AND_BANK: return (uint32_t)operand(b, a.bank_at) << 16 | operand_word(b, a.operand);
    default: return 0;
    }
}

static ByteRef byte_ref(const CommandBytes *b, ScriptArg a)
{
    if (a.kind == ARG_SLOT_BYTE)
        return (ByteRef){ true, operand(b, a.operand) };
    if (a.kind == ARG_CELL)
        return (ByteRef){ false, operand_word(b, a.operand) };
    return (ByteRef){ false, 0 };
}

/* A parameter's value as a word, what a record keeps of it: a byte it names
 * as BYTE_OF_THE_OBJECT and the byte's offset, or a cell's address; any
 * other as its operands say. */
enum { BYTE_OF_THE_OBJECT = 0x10000 };
static uint32_t byte_ref_value(ByteRef b) { return (b.of_the_object ? BYTE_OF_THE_OBJECT : 0) | b.at; }
static ByteRef byte_ref_of(uint32_t v) { return (ByteRef){ (v & BYTE_OF_THE_OBJECT) != 0, (uint16_t)v }; }

#define SCRIPT_VALUE_OF_ByteRef(b, arg) byte_ref_value(byte_ref(b, arg))
#define SCRIPT_VALUE_OF(b, arg) argument(b, arg)
#define SCRIPT_VALUE_OF_ByteChange SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_ByteTest SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_MenuSlotKind SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_MenuAxis SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_SoundEffectVoices SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_uint8_t SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_int8_t SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_uint16_t SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_uint32_t SCRIPT_VALUE_OF
#define SCRIPT_VALUE_OF_bool SCRIPT_VALUE_OF

static uint32_t parameter_value(const CommandBytes *b, const ScriptOpcode *row, MenuScriptParameter p)
{
    switch (p) {
#define SCRIPT_VALUE_CASE(type, name) case MENU_PARAMETER_##name: return SCRIPT_VALUE_OF_##type(b, row->name);
        SCRIPT_PARAMETERS(SCRIPT_VALUE_CASE)
#undef SCRIPT_VALUE_CASE
    default: return 0;
    }
}

/* ... and the parameter taken from it into the command. */
#define SCRIPT_TAKEN_AS_ByteRef(type, v) byte_ref_of(v)
#define SCRIPT_TAKEN_AS_bool(type, v) ((v) != 0)
#define SCRIPT_TAKEN_AS(type, v) ((type)(v))
#define SCRIPT_TAKEN_AS_ByteChange SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_ByteTest SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_MenuSlotKind SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_MenuAxis SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_SoundEffectVoices SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_uint8_t SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_int8_t SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_uint16_t SCRIPT_TAKEN_AS
#define SCRIPT_TAKEN_AS_uint32_t SCRIPT_TAKEN_AS

static void take_parameter(ScriptCommand *c, MenuScriptParameter p, uint32_t v)
{
    switch (p) {
#define SCRIPT_TAKE_CASE(type, name) case MENU_PARAMETER_##name: c->name = SCRIPT_TAKEN_AS_##type(type, v); break;
        SCRIPT_PARAMETERS(SCRIPT_TAKE_CASE)
#undef SCRIPT_TAKE_CASE
    default: break;
    }
}

/* A command with none of its parameters taken yet. */
static void begin_command(ScriptCommand *c, const ScriptOpcode *row, MenuObject self, uint32_t at, Platform *pf)
{
    memset(c, 0, sizeof *c);
    c->self = self;
    c->at = at;
    c->opcode = row->opcode;
    c->pf = pf;
}

/* A row names a handful of the language's parameters, which the decoding
 * walks (menu_named_parameters); the rest read nothing. */
void menu_script_decode(const GameState *g, const ScriptOpcode *row, MenuObject self, uint32_t at, Platform *pf,
                        ScriptCommand *c)
{
    begin_command(c, row, self, at, pf);
    const CommandBytes bytes = { g, at, menu_script_run(at), NULL };
    for (unsigned p = menu_named_from[row->opcode]; p < menu_named_from[row->opcode + 1]; p++) {
        MenuScriptParameter named = (MenuScriptParameter)menu_named_parameters[p];
        take_parameter(c, named, parameter_value(&bytes, row, named));
    }
}

void menu_script_decode_record(const ScriptOpcode *row, MenuObject self, uint32_t at, Platform *pf,
                               const MenuRecord *record, ScriptCommand *c)
{
    begin_command(c, row, self, at, pf);
    const uint32_t *value = record->value;
    for (unsigned p = menu_named_from[row->opcode]; p < menu_named_from[row->opcode + 1]; p++)
        take_parameter(c, (MenuScriptParameter)menu_named_parameters[p], *value++);
}

/* ---- Commands read ahead (menu/records.h) ----------------------------------------------------- */

/* The bytes of a command, from its opcode, that a parameter's operands take
 * up to, and how many of them reading it reads. */
static uint8_t operand_end(ScriptArg a)
{
    switch (a.kind) {
    case ARG_BYTE:
    case ARG_SIGNED:
    case ARG_SLOT_BYTE: return (uint8_t)(a.operand + 1);
    case ARG_WORD:
    case ARG_CELL: return (uint8_t)(a.operand + 2);
    case ARG_LONG: return (uint8_t)(a.operand + 3);
    case ARG_WORD_AND_BANK: return (uint8_t)(a.operand + 2 > a.bank_at + 1 ? a.operand + 2 : a.bank_at + 1);
    default: return 1;
    }
}

static uint8_t operand_reads(ScriptArg a)
{
    switch (a.kind) {
    case ARG_BYTE:
    case ARG_SIGNED:
    case ARG_SLOT_BYTE: return 1;
    case ARG_WORD:
    case ARG_CELL: return 2;
    case ARG_LONG:
    case ARG_WORD_AND_BANK: return 3;
    default: return 0;
    }
}

static ScriptArg row_arg(const ScriptOpcode *row, MenuScriptParameter p)
{
    switch (p) {
#define SCRIPT_ROW_ARG(type, name) case MENU_PARAMETER_##name: return row->name;
        SCRIPT_PARAMETERS(SCRIPT_ROW_ARG)
#undef SCRIPT_ROW_ARG
    default: return (ScriptArg){ ARG_NONE, 0, 0, 0 };
    }
}

unsigned menu_record_read(const uint8_t *command, uint16_t available, MenuRecord *out)
{
    const ScriptOpcode *row = menu_script_opcode(command[0]);
    if (!row || !row->run)
        return 0;
    uint8_t bytes = row->length, reads = 0;
    unsigned named = menu_named_from[row->opcode + 1] - menu_named_from[row->opcode];
    for (unsigned p = menu_named_from[row->opcode]; p < menu_named_from[row->opcode + 1]; p++) {
        ScriptArg a = row_arg(row, (MenuScriptParameter)menu_named_parameters[p]);
        if (operand_end(a) > bytes)
            return 0;   /* reads past the command (no row does) */
        reads = (uint8_t)(reads + operand_reads(a));
    }
    if (bytes > MENU_RECORD_BYTES || bytes > available || named > MENU_RECORD_MOST_VALUES)
        return 0;
    const CommandBytes from = { NULL, 0, { 0, 0, NULL }, command };
    uint8_t values = 0;
    for (unsigned p = menu_named_from[row->opcode]; p < menu_named_from[row->opcode + 1]; p++)
        out->value[values++] = parameter_value(&from, row, (MenuScriptParameter)menu_named_parameters[p]);
    out->opcode = command[0];
    out->bytes = bytes;
    out->values = values;
    out->reads = reads;
    memset(out->made_from, 0, sizeof out->made_from);
    memcpy(out->made_from, command, bytes);
    return MENU_RECORD_HEAD + 4u * values;
}

uint32_t menu_records_language(void)
{
    static uint32_t language;
    if (language)
        return language;
    uint32_t h = 0x811C9DC5u;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 0x01000193u)
    MIX(MENU_PARAMETER_COUNT), MIX(MENU_RECORD_BYTES), MIX(MENU_RECORD_HEAD), MIX(BYTE_OF_THE_OBJECT);
    for (unsigned op = 0; op < 256; op++) {
        const ScriptOpcode *row = menu_script_opcode((uint8_t)op);
        MIX(row != NULL);
        if (!row)
            continue;
        MIX(row->run != NULL), MIX(row->length);
        /* the parameters it names, and how (the rest decode nothing) */
        for (unsigned p = menu_named_from[op]; p < menu_named_from[op + 1]; p++) {
            ScriptArg a = row_arg(row, (MenuScriptParameter)menu_named_parameters[p]);
            MIX(menu_named_parameters[p]), MIX(a.kind), MIX(a.operand), MIX(a.constant), MIX(a.bank_at);
        }
    }
#undef MIX
    language = h | 1u;   /* never 0, which is not worked out yet */
    return language;
}

/* The screens' tables a command marks entries of are indexed by the table's
 * number doubled in a byte (menu/object_script.c): three of them. */
enum { MARKED_TABLES = 3 };

/* A cell a command names is low RAM, and not one of the cells the
 * interpreter keeps its own place in (the script pointer, the running
 * object), which no script names. */
static bool names_low_ram(ScriptArg spec, ByteRef b)
{
    enum { INTERPRETER_CELLS = 0x4E, INTERPRETER_CELLS_END = 0x5B };
    return spec.kind != ARG_CELL || (b.at < LOW_RAM_END && (b.at < INTERPRETER_CELLS || b.at >= INTERPRETER_CELLS_END));
}

/* ... or the setting of the picture's a script may change, which a command
 * changing a byte sets without reading it first (an ending's scripts set the
 * sprites' sizes so). */
static bool sets_a_register(const ScriptOpcode *row, const ScriptCommand *c)
{
    return row->change.kind != ARG_NONE && row->byte.kind == ARG_CELL && !menu_byte_change_reads(c->change)
           && script_byte_is_the_consoles(c->byte) && vid_setting_at(c->byte.at) == VID_SETTING_SPRITES;
}

/* The commands that queue an upload write their records at the queue's end,
 * which is on a record wherever the game queues: those that name what to
 * upload, and those that scroll or redraw the map, whose strips go up so. */
static bool queues_an_upload(const ScriptOpcode *row)
{
    return row->vram.kind != ARG_NONE || row->group.kind != ARG_NONE || row->plane.kind != ARG_NONE;
}

/* The commands that run the object's sprite script run one the language
 * reads (menu/sprites.c): the one it has, or for a move's first frame the one
 * it is about to be given. Objects that never animate may have none. A sprite
 * script can queue an upload, so the queue is whole for them too. */
static bool animates_a_sprite_script(const GameState *g, const ScriptOpcode *row, const ScriptCommand *c)
{
    bool animates = row->back_script.kind != ARG_NONE || c->animating;
    if (animates && !menu_object_queue_is_whole(g))
        return false;
    if (row->back_script.kind != ARG_NONE && (uint8_t)menu_object_slots_command_state(g, c->self) == 0)
        return menu_given_sprite_script_is_well_formed(g, c->back_script)
               && menu_given_sprite_script_is_well_formed(g, c->forward_script);
    if (!animates)
        return true;
    return menu_sprite_script_is_well_formed(g, c->self);
}

/* What the commands queue is built in the upload buffer, which the game
 * keeps in work RAM above low RAM (from $7E:F000), clear of the cells the
 * commands and the map work in. */
static bool upload_buffer_is_clear(const GameState *g)
{
    enum { WORK_RAM = 0x7E, LOW_RAM_END = 0x2000 };
    uint32_t at = menu_object_upload_buffer(g);
    return (uint8_t)(at >> 16) == WORK_RAM && (uint16_t)at >= LOW_RAM_END;
}

bool menu_script_runs_here(const GameState *g, const ScriptOpcode *row, const ScriptCommand *c)
{
    if (!animates_a_sprite_script(g, row, c))
        return false;
    if (queues_an_upload(row) && !(menu_object_queue_is_whole(g) && upload_buffer_is_clear(g)))
        return false;
    if (!(names_low_ram(row->byte, c->byte) || sets_a_register(row, c)) || !names_low_ram(row->from, c->from))
        return false;
    if (row->table.kind != ARG_NONE && (uint8_t)(c->table << 1) >= 2 * MARKED_TABLES)
        return false;
    return true;
}

/* ---- Running a script ------------------------------------------------------------------------------ */

uint32_t menu_script_follow(const ScriptOpcode *row, uint32_t at, ScriptFlow flow)
{
    uint32_t bank = at & 0xFF0000u;
    switch (flow.kind) {
    case SCRIPT_NEXT: return bank | (uint16_t)(at + row->length);
    case SCRIPT_BRANCH: return bank | (uint16_t)(at + flow.by);
    case SCRIPT_GO_TO: return bank | flow.to;
    default: return at;
    }
}

// $C2:0F63
/* After each command the object keeps where its script has got to, and the
 * command's own state starts clear for the next one. */
ScriptEnd menu_run_script(GameState *g, Platform *pf, MenuObject self, uint32_t *stopped)
{
    uint32_t at = menu_object_slots_script(g, self);
    for (;;) {
        const ScriptOpcode *row = menu_script_opcode(menu_script_byte(g, at));
        ScriptFlow flow = menu_script_run_command(g, pf, self, at);
        if (flow.kind == SCRIPT_HANDED_BACK)
            return SCRIPT_GAVE_BACK;
        if (script_flow_stops(flow)) {
            *stopped = at;
            return flow.kind == SCRIPT_END ? SCRIPT_DONE : SCRIPT_WAITING;
        }
        at = menu_script_follow(row, at, flow);
        set_menu_object_slots_script_at(g, self, (uint16_t)at);
        set_menu_object_slots_command_state(g, self, 0);
    }
}

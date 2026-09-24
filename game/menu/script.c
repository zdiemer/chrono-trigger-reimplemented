/* The command scripts' interpreter (menu/script.h): reading a command by its
 * row, and running an object's script for a frame. */
#include "menu/script.h"

#include "menu/menu_data.h"
#include "menu/vram.h"
#include "vars.h"
#include "wram.h"

/* ---- The bytes a command names ---------------------------------------------------------------- */

/* A cell is low RAM, which the mode reaches through a data bank on it
 * (menu_script_runs_here sees to it). */
static uint32_t cell_address(uint16_t at) { return 0x7E0000u + at; }

uint8_t script_byte_at(const GameState *g, MenuObject self, ByteRef b)
{
    return b.of_the_object ? menu_object_byte(g, self, b.at) : wram_read8(g, cell_address(b.at));
}

void script_set_byte_at(GameState *g, MenuObject self, ByteRef b, uint8_t v)
{
    if (b.of_the_object)
        menu_set_object_byte(g, self, b.at, v);
    else
        wram_write8(g, cell_address(b.at), v);
}

/* ---- Reading a command -------------------------------------------------------------------------- */

const ScriptOpcode *menu_script_opcode(uint8_t opcode)
{
    const ScriptOpcode *row = &menu_script_opcodes.rows[opcode < menu_script_opcodes.count ? opcode : 0];
    return opcode < menu_script_opcodes.count && row->opcode == opcode ? row : NULL;
}

/* A command's operands are read on from its opcode, across the end of the
 * bank if they reach it. */
static uint8_t operand(const GameState *g, uint32_t at, uint8_t n) { return menu_script_byte(g, at + n); }
static uint16_t operand_word(const GameState *g, uint32_t at, uint8_t n)
{
    return (uint16_t)(operand(g, at, n) | operand(g, at, (uint8_t)(n + 1)) << 8);
}

static uint32_t argument(const GameState *g, ScriptArg a, uint32_t at)
{
    switch (a.kind) {
    case ARG_CONSTANT: return a.constant;
    case ARG_BYTE: return operand(g, at, a.operand);
    case ARG_SIGNED: return (uint32_t)(int32_t)(int8_t)operand(g, at, a.operand);
    case ARG_WORD: return operand_word(g, at, a.operand);
    case ARG_LONG: return (uint32_t)operand(g, at, (uint8_t)(a.operand + 2)) << 16 | operand_word(g, at, a.operand);
    case ARG_WORD_AND_BANK: return (uint32_t)operand(g, at, a.bank_at) << 16 | operand_word(g, at, a.operand);
    default: return 0;
    }
}

static ByteRef byte_ref(const GameState *g, ScriptArg a, uint32_t at)
{
    if (a.kind == ARG_SLOT_BYTE)
        return (ByteRef){ true, operand(g, at, a.operand) };
    if (a.kind == ARG_CELL)
        return (ByteRef){ false, operand_word(g, at, a.operand) };
    return (ByteRef){ false, 0 };
}

#define SCRIPT_DECODE_BYTE_REF(name) c.name = byte_ref(g, row->name, at);
#define SCRIPT_DECODE(type, name) c.name = (type)argument(g, row->name, at);
#define SCRIPT_DECODE_ONE(type, name) SCRIPT_DECODE_##type(type, name)
#define SCRIPT_DECODE_ByteRef(type, name) SCRIPT_DECODE_BYTE_REF(name)
#define SCRIPT_DECODE_ByteChange SCRIPT_DECODE
#define SCRIPT_DECODE_ByteTest SCRIPT_DECODE
#define SCRIPT_DECODE_MenuSlotKind SCRIPT_DECODE
#define SCRIPT_DECODE_MenuAxis SCRIPT_DECODE
#define SCRIPT_DECODE_uint8_t SCRIPT_DECODE
#define SCRIPT_DECODE_int8_t SCRIPT_DECODE
#define SCRIPT_DECODE_uint16_t SCRIPT_DECODE
#define SCRIPT_DECODE_uint32_t SCRIPT_DECODE
#define SCRIPT_DECODE_bool(type, name) c.name = argument(g, row->name, at) != 0;

ScriptCommand menu_script_decode(const GameState *g, const ScriptOpcode *row, MenuObject self, uint32_t at,
                                 const ScriptServices *services)
{
    ScriptCommand c = { 0 };
    c.self = self;
    c.at = at;
    c.services = services;
    SCRIPT_PARAMETERS(SCRIPT_DECODE_ONE)
    return c;
}

/* The screens' tables a command marks entries of are indexed by the table's
 * number doubled in a byte (menu/object_script.c): three of them. */
enum { MARKED_TABLES = 3 };

static bool names_low_ram(ScriptArg spec, ByteRef b) { return spec.kind != ARG_CELL || b.at < 0x2000; }

/* The commands that queue an upload write their records at the queue's end,
 * which is on a record wherever the game queues. */
static bool queues_an_upload(const ScriptOpcode *row) { return row->vram.kind != ARG_NONE || row->group.kind != ARG_NONE; }

bool menu_script_runs_here(const GameState *g, const ScriptOpcode *row, const ScriptCommand *c)
{
    if (queues_an_upload(row) && !menu_object_queue_is_whole(g))
        return false;
    if (!names_low_ram(row->byte, c->byte) || !names_low_ram(row->from, c->from))
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
ScriptEnd menu_run_script(GameState *g, MenuObject self, const ScriptRunner *r, uint32_t *stopped)
{
    uint32_t at = menu_object_slots_script(g, self);
    for (;;) {
        const ScriptOpcode *row = menu_script_opcode(menu_script_byte(g, at));
        ScriptFlow flow = r->run(r->ctx, self, at);
        if (flow.kind == SCRIPT_HANDED_BACK)
            return SCRIPT_GAVE_BACK;
        if (script_flow_stops(flow)) {
            *stopped = at;
            return flow.kind == SCRIPT_END ? SCRIPT_DONE : SCRIPT_WAITING;
        }
        at = menu_script_follow(row, at, flow);
        set_menu_object_slots_script(g, self, at);
        set_menu_object_slots_command_state(g, self, 0);
    }
}

ScriptFlow menu_script_run_directly(GameState *g, const ScriptServices *services, MenuObject self, uint32_t at)
{
    const ScriptOpcode *row = menu_script_opcode(menu_script_byte(g, at));
    if (!row)
        return end_object();
    ScriptCommand c = menu_script_decode(g, row, self, at, services);
    return menu_script_perform(g, row, &c);
}

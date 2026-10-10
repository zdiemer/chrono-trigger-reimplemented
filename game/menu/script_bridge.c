/* The command scripts on the original's machine (menu/script_bridge.h): the
 * interpreter's entry and how it runs each command, the commands' entries,
 * the services the commands reach the original's code through, the guards,
 * and the registers a command leaves.
 *
 * What a command is on the machine -- where it calls the original's code
 * from, and what it leaves in X and the carry -- is a table parallel to the
 * language's (menu/object_script.c), a row an opcode, so the language's own
 * rows carry none of it. */
#include "menu/script_bridge.h"

#include "menu/menu_data.h"
#include "menu/script.h"
#include "menu/script_call.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- What a command is on the machine ----------------------------------------------------------- */

/* X, the carry and A after each command, as the original's handler leaves
 * them. Nothing the game runs reads them but the interpreter (A) and its
 * callers (X and the carry, when the command stops the object: the liveness
 * analysis can't show them dead), so they are worked out from what the
 * command did. */
typedef enum {
    X_AS_ENTERED,      /* the doubled opcode */
    X_SLOT,            /* the running object's slot, as its cell has it after the command */
    X_BYTE,            /* the byte the command names: its offset, or the cell's address */
    X_FROM,            /* the byte it copies from */
    X_COPIED,          /* the value it copied */
    X_PAST_STARTED,    /* past the slot it started an object in, or the last it looked at */
    X_STARTED,         /* the slot it started a script's object in */
    X_KIND,            /* the location's kind */
    X_PLANE_INDEX,     /* the plane's number, from 0, doubled */
    X_SCENE_SCRIPT,    /* the script, where it gave one */
    X_QUEUE_END,       /* where the queue ended before */
    X_TABLE_INDEX,     /* the table's number, doubled in a byte */
    X_LAST_ROW,        /* where the copy's last row went */
    X_CALLED,          /* what the original's code it called left */
    X_SCROLLED,        /* the scroll's, where the last move was down; else the slot */
} XLeft;

typedef enum {
    C_CLEAR,
    C_SET,
    C_SET_GOING_ON,    /* set where the command is over */
    C_ADDED,           /* the addition's carry */
    C_SUBTRACTED,      /* no borrow */
    C_COMPARED,        /* the comparison's, where it didn't branch */
    C_KIND_BIT,        /* the bit the location's kind was shifted out of */
    C_SCENE_COMPARE,   /* the last comparison of the member with the party */
    C_RETURN_PAST_BANK,
    C_QUEUE_PAST_PAGE, /* the queue's end, moved past the records it queued, past its byte */
    C_LAST_ROW,
    C_CALLED,
} CarryLeft;

/* Which of the original's calls a command makes, each from its own JSR:
 * the map's scroll for both planes or for one, the sound queue for an
 * effect, a song or any command. */
typedef enum { NO_CALL, SCROLLS_BOTH_PLANES, SCROLLS_ONE_PLANE, SENDS_EFFECT, SENDS_SONG, SENDS_COMMAND } CallKind;

typedef struct {
    uint8_t x, carry;
    uint8_t calls;     /* CallKind */
    uint8_t queues;    /* records a command that queues uploads queues */
} MachineRow;

static const MachineRow MACHINE[] = {
    [0x00] = { X_SLOT, C_CLEAR },           [0x01] = { X_SLOT, C_CLEAR },
    [0x02] = { X_SLOT, C_CLEAR },           [0x03] = { X_PAST_STARTED, C_CLEAR },
    [0x04] = { X_PAST_STARTED, C_CLEAR },   [0x05] = { X_KIND, C_KIND_BIT },
    [0x06] = { X_AS_ENTERED, C_CLEAR },     [0x07] = { X_PLANE_INDEX, C_CLEAR },
    [0x08] = { X_SCENE_SCRIPT, C_SCENE_COMPARE }, [0x09] = { X_STARTED, C_SET },
    [0x0A] = { X_AS_ENTERED, C_CLEAR },     [0x0B] = { X_AS_ENTERED, C_CLEAR },
    [0x0C] = { X_AS_ENTERED, C_CLEAR },     [0x0D] = { X_BYTE, C_CLEAR },
    [0x0E] = { X_BYTE, C_CLEAR },           [0x0F] = { X_BYTE, C_CLEAR },
    [0x10] = { X_BYTE, C_CLEAR },           [0x11] = { X_BYTE, C_CLEAR },
    [0x12] = { X_BYTE, C_CLEAR },           [0x13] = { X_BYTE, C_CLEAR },
    [0x14] = { X_BYTE, C_CLEAR },           [0x15] = { X_BYTE, C_CLEAR },
    [0x16] = { X_BYTE, C_CLEAR },           [0x17] = { X_FROM, C_CLEAR },
    [0x18] = { X_COPIED, C_CLEAR },         [0x19] = { X_FROM, C_CLEAR },
    [0x1A] = { X_AS_ENTERED, C_CLEAR },     [0x1B] = { X_AS_ENTERED, C_CLEAR },
    [0x1C] = { X_AS_ENTERED, C_CLEAR },     [0x1D] = { X_AS_ENTERED, C_CLEAR },
    [0x1E] = { X_BYTE, C_COMPARED },        [0x1F] = { X_BYTE, C_COMPARED },
    [0x20] = { X_BYTE, C_COMPARED },        [0x21] = { X_BYTE, C_COMPARED },
    [0x22] = { X_BYTE, C_CLEAR },           [0x23] = { X_BYTE, C_CLEAR },
    [0x24] = { X_BYTE, C_COMPARED },        [0x25] = { X_BYTE, C_COMPARED },
    [0x26] = { X_BYTE, C_COMPARED },        [0x27] = { X_BYTE, C_COMPARED },
    [0x28] = { X_PAST_STARTED, C_CLEAR },   [0x29] = { X_PAST_STARTED, C_CLEAR },
    [0x2A] = { X_PAST_STARTED, C_CLEAR },   [0x2B] = { X_PAST_STARTED, C_CLEAR },
    [0x2C] = { X_SLOT, C_CLEAR },           [0x2D] = { X_AS_ENTERED, C_CLEAR },
    [0x2E] = { X_SLOT, C_CLEAR },           [0x2F] = { X_SLOT, C_CLEAR },
    [0x30] = { X_SLOT, C_CLEAR },           [0x31] = { X_SLOT, C_CLEAR },
    [0x32] = { X_SCROLLED, C_CLEAR, SCROLLS_BOTH_PLANES },
    [0x33] = { X_QUEUE_END, C_QUEUE_PAST_PAGE, NO_CALL, 1 },
    [0x34] = { X_CALLED, C_CALLED },        [0x35] = { X_PAST_STARTED, C_CLEAR },
    [0x36] = { X_SLOT, C_RETURN_PAST_BANK }, [0x37] = { X_SLOT, C_CLEAR },
    [0x38] = { X_SLOT, C_CLEAR },           [0x39] = { X_SLOT, C_CLEAR },
    [0x3A] = { X_SLOT, C_SET_GOING_ON },
    [0x3B] = { X_CALLED, C_CALLED, SENDS_EFFECT },
    [0x3C] = { X_CALLED, C_CALLED, SENDS_EFFECT },
    [0x3D] = { X_CALLED, C_CALLED, SENDS_SONG },
    [0x3E] = { X_CALLED, C_CALLED },        [0x3F] = { X_SLOT, C_SET_GOING_ON },
    [0x40] = { X_SLOT, C_SET_GOING_ON },    [0x41] = { X_AS_ENTERED, C_CLEAR },
    [0x42] = { X_PAST_STARTED, C_CLEAR },   [0x43] = { X_STARTED, C_SET },
    [0x44] = { X_TABLE_INDEX, C_CLEAR },    [0x45] = { X_TABLE_INDEX, C_CLEAR },
    [0x46] = { X_BYTE, C_ADDED },           [0x47] = { X_BYTE, C_SUBTRACTED },
    [0x48] = { X_BYTE, C_ADDED },           [0x49] = { X_BYTE, C_SUBTRACTED },
    [0x4A] = { X_CALLED, C_CALLED, SENDS_SONG },
    [0x4B] = { X_CALLED, C_CALLED, SENDS_COMMAND },
    [0x4C] = { X_BYTE, C_COMPARED },        [0x4D] = { X_BYTE, C_COMPARED },
    [0x4E] = { X_CALLED, C_CALLED },        [0x4F] = { X_LAST_ROW, C_LAST_ROW },
    [0x50] = { X_QUEUE_END, C_QUEUE_PAST_PAGE, NO_CALL, 2 },
    [0x51] = { X_SCROLLED, C_CLEAR, SCROLLS_ONE_PLANE },
    [0x52] = { X_AS_ENTERED, C_SET },
};

/* ---- The machine a command runs on ---------------------------------------------------------- */

/* The interpreter's dispatch, JSR ($0F91,X) at $C2:0F7A, which returns to
 * $C2:0F7D; the handlers are in bank C2. */
enum { DISPATCH_SITE = 0xC20F7A, HANDLERS_BANK = 0xC20000, JSR_SIZE = 3, JSL_SIZE = 4 };

enum { SCROLL_ACROSS = 0xC20568, SCROLL_DOWN = 0xC2066C };

typedef struct {
    Snes *s;
    ScriptServices services;
    uint8_t opcode;          /* the command running */
    bool called;             /* the command called the original's code, which left ... */
    bool called_down;        /* ... (a scroll down, last) */
    uint16_t called_x;       /* ... X */
    bool called_carry;       /* ... and the carry */
    bool overflow;           /* the overflow the interpreter's dispatch leaves the command */
} OnTheMachine;

static const MachineRow *machine_row(const OnTheMachine *m) { return &MACHINE[m->opcode]; }

/* The commands go on from a call with 16-bit index registers on the mode's
 * page; the map's scrolling, whose caller goes on with 8-bit A, also with
 * that. */
static bool back_as_called(const Snes *s, uint8_t bank, bool a8)
{
    return !s->cpu.e && (m_m8(s) || !a8) && !m_x8(s) && m_direct_page(s) == 0 && s->cpu.db == bank;
}

/* Call the original's routine from the command's JSR (JSL) at `site`, with
 * 8-bit A as every command calls. True if it came back as it went; otherwise
 * the port stops at the call's return and the original's handler carries on
 * from there, returning to the dispatch. */
static bool call_out_as(OnTheMachine *m, uint32_t site, uint32_t routine, uint16_t dont_care, bool a8)
{
    Snes *s = m->s;
    uint8_t bank = s->cpu.db;
    m_sep(s, CPU_FLAG_M);
    m_jsr_dc(s, site, routine, dont_care);
    if (!back_as_called(s, bank, a8)) {
        m_stop_at_own(s, site + JSR_SIZE, 0);
        return false;
    }
    m->called = true;
    m->called_x = s->cpu.x;
    m->called_carry = m_flag(s, CPU_FLAG_C);
    return true;
}

static bool call_out(OnTheMachine *m, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    return call_out_as(m, site, routine, dont_care, routine == SCROLL_ACROSS || routine == SCROLL_DOWN);
}

static bool call_out_far(OnTheMachine *m, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    Snes *s = m->s;
    uint8_t bank = s->cpu.db;
    m_sep(s, CPU_FLAG_M);
    m_jsl_dc(s, site, routine, dont_care);
    if (!back_as_called(s, bank, false)) {
        m_stop_at_own(s, site + JSL_SIZE, 0);
        return false;
    }
    m->called = true;
    m->called_x = s->cpu.x;
    m->called_carry = m_flag(s, CPU_FLAG_C);
    return true;
}

/* What the routines the commands call read of the registers
 * (tools/dontcare_check.py checks each). */
enum {
    MAP_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,          /* $C2:0568, $C2:066C, $C2:09C5 */
    SOUND_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,        /* $C2:2ED9 */
};

/* ---- The services, on the machine ------------------------------------------------------------- */

/* The map's scrolling takes the plane and the step from the mode's first two
 * cells. A scroll of both planes sets the step once, so the second plane
 * scrolls by what the first's scroll left in the cell. */
static bool scroll_map(void *ctx, MenuAxis axis, uint8_t plane, int8_t step)
{
    OnTheMachine *m = ctx;
    GameState *g = state_of(m->s);
    bool both_planes = machine_row(m)->calls == SCROLLS_BOTH_PLANES;
    set_menu_object_scratch_plane(g, plane);
    if (!both_planes || plane != 2)
        set_menu_object_scratch_step(g, (uint8_t)step);
    m->called_down = axis == MENU_DOWN;
    if (!both_planes)
        return axis == MENU_ACROSS ? call_out(m, 0xC2173C, SCROLL_ACROSS, MAP_IGNORES)
                                   : call_out(m, 0xC21751, SCROLL_DOWN, MAP_IGNORES);
    if (axis == MENU_ACROSS)
        return plane == 1 ? call_out(m, 0xC2168A, SCROLL_ACROSS, MAP_IGNORES)
                          : call_out(m, 0xC21691, SCROLL_ACROSS, MAP_IGNORES);
    return plane == 1 ? call_out(m, 0xC216C7, SCROLL_DOWN, MAP_IGNORES) : call_out(m, 0xC216CE, SCROLL_DOWN, MAP_IGNORES);
}

static bool redraw_map(void *ctx, uint8_t plane)
{
    OnTheMachine *m = ctx;
    set_menu_object_scratch_plane(state_of(m->s), plane);
    return call_out(m, 0xC21950, 0xC209C5, MAP_IGNORES);
}

/* The sound queue: each kind of request sends from its own JSR. */
static bool pass_on_sound(void *ctx)
{
    OnTheMachine *m = ctx;
    switch ((CallKind)machine_row(m)->calls) {
    case SENDS_EFFECT: return call_out(m, 0xC218E8, 0xC22ED9, SOUND_IGNORES);
    case SENDS_COMMAND: return call_out(m, 0xC2193E, 0xC22ED9, SOUND_IGNORES);
    default: return call_out(m, 0xC21916, 0xC22ED9, SOUND_IGNORES);
    }
}

/* A routine a script names is called where it is, as the command's call
 * through the call target reaches it (its jump through it at $C2:1819, or
 * its long jump at $C2:1836, adds nothing to the call but the jump), a
 * ported one as C. It may read any register: it gets the ones the command's
 * handler has there. The handler has read its operands through Y (the
 * address's word, and for the far one its bank), which leaves A, N and Z;
 * X, the carry and the overflow are the dispatch's. */
static bool call_routine(void *ctx, uint32_t routine, bool far)
{
    OnTheMachine *m = ctx;
    Snes *s = m->s;
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_V, m->overflow);
    if (!far) {
        set_menu_object_call_target(g, (menu_object_call_target(g) & 0xFF0000u) | (uint16_t)routine);
        s->cpu.a = (uint16_t)routine;
        s->cpu.y = 1;
        m_set_nz16(s, s->cpu.a);
        return call_out_as(m, 0xC21810, 0xC20000u | (uint16_t)routine, 0, false);
    }
    set_menu_object_call_target(g, routine);
    s->cpu.a = (uint16_t)((routine & 0xFF00) | (uint8_t)(routine >> 16));
    s->cpu.y = 3;
    m_set_nz8(s, (uint8_t)(routine >> 16));
    return call_out_far(m, 0xC2182C, routine & 0xFFFFFFu, 0);
}

/* ---- What a command leaves -------------------------------------------------------------------- */

/* What the derivations need of the state before the command changes it. */
typedef struct {
    uint8_t found;           /* the byte it reads (or copies from) */
    uint8_t queue_end;
    uint16_t past_started;
} Noted;

enum { SLOT_BYTES = 0x40, MAP_ROW_BYTES = 0x60 };

static uint16_t past_the_start(const GameState *g, MenuSlotKind kind)
{
    unsigned o = menu_first_free_object(g, kind);
    return (uint16_t)(o == MENU_OBJECTS ? menu_object_address((MenuObject)menu_last_object_of(kind)) + SLOT_BYTES
                                        : menu_object_address((MenuObject)o) + SLOT_BYTES);
}

static void note(const GameState *g, const ScriptOpcode *row, const ScriptCommand *c, Noted *n)
{
    if (row->from.kind != ARG_NONE)
        n->found = script_byte_at(g, c->self, c->from);
    else if (row->byte.kind != ARG_NONE)
        n->found = script_byte_at(g, c->self, c->byte);
    n->queue_end = menu_object_vram_queue_end(g);
    n->past_started = past_the_start(g, c->slots);
}

static bool scene_compare(const GameState *g, const ScriptCommand *c, uint16_t *x)
{
    enum { EXTRA_OBJECT = 7, PARTY = 3 };
    if (c->member == EXTRA_OBJECT) {
        *x = c->scene_script;
        return true;
    }
    bool carry = false;
    for (unsigned slot = 0; slot < PARTY; slot++) {
        carry = c->member >= party_members(g, slot);
        if (c->member == party_members(g, slot)) {
            *x = c->scene_script;
            break;
        }
    }
    return carry;
}

static uint16_t last_row(const GameState *g, const ScriptCommand *c)
{
    uint16_t to = (uint16_t)(menu_plane_map(g, c->plane == 1 ? 0 : 1) + c->column + c->row * MAP_ROW_BYTES);
    unsigned rows = c->rows ? c->rows : 256;
    return (uint16_t)(to + (rows - 1) * MAP_ROW_BYTES);
}

static bool compared(const ScriptCommand *c, ScriptFlow f, uint8_t found)
{
    if (f.kind == SCRIPT_BRANCH)
        return false;
    switch (c->test) {
    case TEST_EQUAL: case TEST_NOT_EQUAL: return c->value >= found;
    case TEST_BELOW: case TEST_AT_LEAST: return found >= c->value;
    default: return false;
    }
}

static uint16_t x_left(const GameState *g, const OnTheMachine *m, const ScriptCommand *c, const Noted *n, uint16_t x)
{
    switch ((XLeft)machine_row(m)->x) {
    case X_SLOT: return menu_object_current(g);
    case X_BYTE: return c->byte.at;
    case X_FROM: return c->from.at;
    case X_COPIED: return n->found;
    case X_PAST_STARTED: return n->past_started;
    case X_STARTED: return (uint16_t)(n->past_started - SLOT_BYTES);
    case X_KIND: return (uint16_t)((c->region >> 1) & 3);
    case X_PLANE_INDEX: return (uint16_t)((uint8_t)(c->plane - 1) << 1);
    case X_SCENE_SCRIPT: (void)scene_compare(g, c, &x); return x;
    case X_QUEUE_END: return n->queue_end;
    case X_TABLE_INDEX: return (uint8_t)(c->table << 1);
    case X_LAST_ROW: return last_row(g, c);
    case X_CALLED: return m->called ? m->called_x : x;
    case X_SCROLLED: return m->called && m->called_down ? m->called_x : menu_object_address(c->self);
    default: return x;
    }
}

static bool carry_left(const GameState *g, const OnTheMachine *m, const ScriptCommand *c, ScriptFlow f, const Noted *n)
{
    uint16_t unused = 0;
    const MachineRow *row = machine_row(m);
    switch ((CarryLeft)row->carry) {
    case C_SET: return true;
    case C_SET_GOING_ON: return f.kind == SCRIPT_NEXT;
    case C_ADDED: return n->found + c->value > 0xFF;
    case C_SUBTRACTED: return n->found >= c->value;
    case C_COMPARED: return compared(c, f, n->found);
    case C_KIND_BIT: return c->region & 1;
    case C_SCENE_COMPARE: return scene_compare(g, c, &unused);
    case C_RETURN_PAST_BANK: return (uint16_t)c->at + 3 > 0xFFFF;
    case C_QUEUE_PAST_PAGE: return n->queue_end + row->queues * MENU_OBJECT_UPLOAD_BYTES > 0xFF;
    case C_LAST_ROW: return last_row(g, c) + MAP_ROW_BYTES > 0xFFFF;
    case C_CALLED: return m->called && m->called_carry;
    default: return false;
    }
}

/* A, as the interpreter reads it: a length, 1 for a jump through the script
 * pointer, a branch's distance, or 0. */
static uint16_t a_left(const ScriptOpcode *row, ScriptFlow f)
{
    switch (f.kind) {
    case SCRIPT_NEXT: return row->length;
    case SCRIPT_GO_TO: return 1;
    case SCRIPT_BRANCH: return (uint16_t)f.by;
    default: return 0;
    }
}

static void leave(OnTheMachine *m, const ScriptOpcode *row, const ScriptCommand *c, ScriptFlow f, const Noted *n)
{
    Snes *s = m->s;
    GameState *g = state_of(s);
    vars_peek(true);
    uint16_t x = x_left(g, m, c, n, s->cpu.x);
    bool carry = carry_left(g, m, c, f, n);
    vars_peek(false);
    s->cpu.x = x;
    s->cpu.a = a_left(row, f);
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, carry);
    m_set_flag(s, CPU_FLAG_Z, s->cpu.a == 0);
}

/* ---- Running a command -------------------------------------------------------------------------- */

static void on_the_machine(OnTheMachine *m, Snes *s, uint8_t opcode)
{
    bool overflow = m->s == s && m->overflow;
    *m = (OnTheMachine){ s, { scroll_map, redraw_map, pass_on_sound, call_routine, m }, opcode, false, false, 0, false,
                         overflow };
}

/* The interpreter moves the script pointer on by what a command handed back
 * with an addition, which leaves its overflow for the next command's
 * dispatch. */
static bool overflow_moving_on(uint16_t pointer, uint16_t by)
{
    uint16_t to = (uint16_t)(pointer + by);
    return (~(pointer ^ by) & (pointer ^ to) & 0x8000) != 0;
}

static uint32_t handler_of(Snes *s, uint8_t opcode)
{
    vars_peek(true);
    uint32_t handler = HANDLERS_BANK | menu_script_handler(state_of(s), opcode);
    vars_peek(false);
    return handler;
}

/* The command by its row, as C, on a machine as the dispatch leaves it; its
 * time is its handler's. */
static ScriptFlow perform(OnTheMachine *m, const ScriptOpcode *row, const ScriptCommand *c)
{
    Snes *s = m->s;
    GameState *g = state_of(s);
    Noted n = { 0 };
    vars_peek(true);
    note(g, row, c, &n);
    vars_peek(false);
    ScriptFlow f;
    NATIVE_TIME_AS(s, handler_of(s, m->opcode), f = menu_script_perform(g, row, c));
    if (f.kind != SCRIPT_HANDED_BACK)
        leave(m, row, c, f, &n);
    return f;
}

/* The registers the dispatch leaves a handler: the opcode doubled in A and X,
 * the carry clear from the doubling. */
static void enter_as_the_dispatch_does(Snes *s, uint8_t opcode)
{
    s->cpu.a = (uint16_t)(opcode * 2);
    s->cpu.x = (uint16_t)(opcode * 2);
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_flag(s, CPU_FLAG_Z, opcode == 0);
}

/* A command the port doesn't run -- an opcode past the language, or one
 * whose cells or tables aren't ones the game has -- goes back to the
 * original's interpreter at the top of its loop, which reads it again: the
 * script pointer is on it, and nothing else carries over. */
enum { INTERPRETER_LOOP = 0xC20F73 };

static ScriptFlow hand_to_the_original(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    m_stop_at_own(s, INTERPRETER_LOOP, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    return handed_back();
}

/* The engine's runner: each command as C where the port has it, with the
 * dispatch's return on the stack for a call of the original's code that
 * stops (the original's handler then returns to the original's interpreter);
 * the script pointer is where the command is, as the original's handlers and
 * the code they call read it. */
static ScriptFlow run_on_the_machine(void *ctx, MenuObject self, uint32_t at)
{
    OnTheMachine *m = ctx;
    Snes *s = m->s;
    GameState *g = state_of(s);
    set_menu_object_script(g, at);
    vars_peek(true);
    uint8_t opcode = menu_script_byte(g, at);
    on_the_machine(m, s, opcode);
    const ScriptOpcode *row = menu_script_opcode(opcode);
    ScriptCommand c = { 0 };
    if (row)
        c = menu_script_decode(g, row, self, at, &m->services);
    bool ours = row && menu_script_runs_here(g, row, &c);
    vars_peek(false);
    if (!ours)
        return hand_to_the_original(s);
    enter_as_the_dispatch_does(s, opcode);
    m_push16(s, (uint16_t)(DISPATCH_SITE + JSR_SIZE - 1));
    ScriptFlow f = perform(m, row, &c);
    if (f.kind == SCRIPT_HANDED_BACK)
        return f;
    (void)m_pull16(s);
    uint16_t pointer = f.kind == SCRIPT_GO_TO ? (uint16_t)(f.to - 1) : (uint16_t)at;
    m->overflow = overflow_moving_on(pointer, s->cpu.a);
    return f;
}

/* ---- The entries ------------------------------------------------------------------------------------ */

static bool on_the_mode_page(Snes *s) { return m_direct_page(s) == 0 && m_bank_reaches_low_ram(s) && !m_x8(s); }

/* The game's scripts are in the ROM's banks, or built in work RAM. */
static bool script_bank_holds_scripts(uint8_t bank) { return bank >= 0xC0 || bank == 0x7E || bank == 0x7F; }

bool menu_object_script_can_run(const GameState *g, uint32_t script)
{
    return script_bank_holds_scripts((uint8_t)(script >> 16)) && menu_sprite_list_is_sound(g);
}

bool menu_object_script_runs(Snes *s)
{
    if (!on_the_mode_page(s))
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    bool ok = menu_object_address_names_one(menu_object_current(g))
              && menu_object_script_can_run(g, menu_object_slots_script(g, menu_object_running(g)));
    vars_peek(false);
    return ok;
}

/* The running object's script for this frame (menu/script.c), each command on
 * the machine. It leaves the carry set for an object that is done, and X as
 * the command it stopped on left it. */
static ScriptEnd run_the_running_object_s_script(Snes *s)
{
    GameState *g = state_of(s);
    OnTheMachine m = { .s = s, .overflow = m_flag(s, CPU_FLAG_V) };
    on_the_machine(&m, s, 0);
    ScriptRunner r = { run_on_the_machine, &m };
    uint32_t stopped = 0;
    ScriptEnd end = menu_run_script(g, menu_object_running(g), &r, &stopped);
    if (end == SCRIPT_GAVE_BACK)
        return end;
    m_set_flag(s, CPU_FLAG_C, end == SCRIPT_DONE);
    m_sep(s, CPU_FLAG_M);
    return end;
}

// $C2:0F63
/* The interpreter as a slot's handler. */
void menu_run_object_script(Snes *s)
{
    if (run_the_running_object_s_script(s) != SCRIPT_GAVE_BACK)
        m_rts(s);
}

/* The call's return is on the stack while the script runs, as the original's
 * JSR leaves it, for a command that hands back to the original's interpreter
 * to return through; its time is the interpreter's own. */
ScriptEnd menu_run_object_script_from(Snes *s, uint32_t call_site)
{
    m_push16(s, (uint16_t)(call_site + JSR_SIZE - 1));
    ScriptEnd end;
    NATIVE_TIME_AS(s, 0xC20F63, end = run_the_running_object_s_script(s));
    if (end != SCRIPT_GAVE_BACK)
        (void)m_pull16(s);
    return end;
}

/* A command's own entry, as the dispatch enters it (the guard sees to it):
 * the command at the script pointer. A jump leaves the pointer a byte before
 * where the script goes on, for the interpreter's step of 1. */
static void command_entry(Snes *s, uint8_t opcode)
{
    GameState *g = state_of(s);
    OnTheMachine m = { .s = s, .overflow = m_flag(s, CPU_FLAG_V) };
    on_the_machine(&m, s, opcode);
    vars_peek(true);
    uint32_t at = menu_object_script(g);
    const ScriptOpcode *row = menu_script_opcode(opcode);
    ScriptCommand c = menu_script_decode(g, row, menu_object_running(g), at, &m.services);
    vars_peek(false);
    ScriptFlow f = perform(&m, row, &c);
    if (f.kind == SCRIPT_HANDED_BACK)
        return;
    if (f.kind == SCRIPT_GO_TO)
        set_menu_object_script(g, (at & 0xFF0000u) | (uint16_t)(f.to - 1));
    m_rts(s);
}

static bool command_accepts(Snes *s, uint8_t opcode)
{
    if (!on_the_mode_page(s) || m_m8(s) || s->cpu.a != opcode * 2 || s->cpu.x != opcode * 2 || m_flag(s, CPU_FLAG_C))
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    bool ok = menu_object_address_names_one(menu_object_current(g));
    if (ok) {
        const ScriptOpcode *row = menu_script_opcode(opcode);
        uint32_t at = menu_object_script(g);
        ScriptCommand c = menu_script_decode(g, row, menu_object_running(g), at, NULL);
        ok = script_bank_holds_scripts((uint8_t)(at >> 16)) && menu_script_opcode(menu_script_byte(g, at)) == row
             && menu_script_runs_here(g, row, &c) && menu_sprite_list_is_sound(g);
    }
    vars_peek(false);
    return ok;
}

#define MENU_SCRIPT_DEFINE_ENTRY(name, opcode)                               \
    void name(Snes *s) { command_entry(s, opcode); }                        \
    bool name##_accepts(Snes *s) { return command_accepts(s, opcode); }
MENU_SCRIPT_ENTRIES(MENU_SCRIPT_DEFINE_ENTRY)

/* The sprite-object mode's objects: their slots, starting them, moving them,
 * and running every one of them once a frame (menu/object.h says what an
 * object is).
 *
 * The vblank handler runs the objects in slot order (menu_run_objects): each
 * slot with a handler runs it, and a handler that says it is done frees its
 * slot; every other object counts a frame of age. A handler is a routine of
 * bank C2 the slot names by its address: the command scripts' interpreter
 * (menu/script.c), the Epoch's screens' objects (menu/epoch.c), the objects
 * that change the screen (menu/screen_objects.c), the wave (menu/wave.c),
 * the scenes' (menu/map_scenes.c) and the overworld's walkers and vehicles
 * (game/overworld/). */
#include "menu/object.h"
#include "menu/objects.h"

#include "charge.h"
#include "core/program.h"
#include "menu/handlers.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- Storage: the slots by their address -------------------------------------------------- */

/* The original names a slot by its address in low RAM, and reaches its bytes
 * through the data bank, which the mode keeps on low RAM; an address that
 * isn't a slot's (the running object before any has run, a byte named past
 * the last slot) is low RAM like any other. */
enum { FIRST_SLOT_ADDRESS = 0x0B30, SLOT_BYTES = 0x40 };
enum { SLOTS_END = FIRST_SLOT_ADDRESS + MENU_OBJECTS * SLOT_BYTES };

uint16_t menu_object_address(MenuObject o) { return (uint16_t)(FIRST_SLOT_ADDRESS + o * SLOT_BYTES); }

static bool in_a_slot(uint16_t address) { return address >= FIRST_SLOT_ADDRESS && address < SLOTS_END; }

bool menu_objects_hold(uint32_t address)
{
    enum { WORK_RAM = 0x7E, LOW_RAM_END = 0x2000 };
    uint8_t bank = (uint8_t)(address >> 16);
    bool low_ram = bank == WORK_RAM || ((bank & 0x7F) < 0x40 && (uint16_t)address < LOW_RAM_END);
    return low_ram && in_a_slot((uint16_t)address);
}

bool menu_object_address_names_one(uint16_t address)
{
    return in_a_slot(address) && (address - FIRST_SLOT_ADDRESS) % SLOT_BYTES == 0;
}

MenuObject menu_object_at_address(uint16_t address) { return (MenuObject)((address - FIRST_SLOT_ADDRESS) / SLOT_BYTES); }

MenuObject menu_object_running(const GameState *g) { return menu_object_at_address(menu_object_current(g)); }
void menu_object_make_running(GameState *g, MenuObject o) { set_menu_object_current(g, menu_object_address(o)); }

static uint8_t byte_at(const GameState *g, uint16_t address)
{
    if (!in_a_slot(address))
        return wram_read8(g, 0x7E0000u | address);
    unsigned from_first = address - FIRST_SLOT_ADDRESS;
    return menu_object_slots_bytes_byte(g, from_first / SLOT_BYTES, from_first % SLOT_BYTES);
}

static void set_byte_at(GameState *g, uint16_t address, uint8_t v)
{
    if (!in_a_slot(address)) {
        wram_write8(g, 0x7E0000u | address, v);
        return;
    }
    unsigned from_first = address - FIRST_SLOT_ADDRESS;
    set_menu_object_slots_bytes_byte(g, from_first / SLOT_BYTES, from_first % SLOT_BYTES, v);
}

uint8_t menu_object_byte(const GameState *g, MenuObject o, uint16_t at)
{
    return byte_at(g, (uint16_t)(menu_object_address(o) + at));
}

void menu_set_object_byte(GameState *g, MenuObject o, uint16_t at, uint8_t v)
{
    set_byte_at(g, (uint16_t)(menu_object_address(o) + at), v);
}

void menu_object_leave_cell8(GameState *g, unsigned cell, uint8_t v) { set_menu_object_scratch_bytes_byte(g, cell, v); }

void menu_object_leave_cell16(GameState *g, unsigned cell, uint16_t v)
{
    menu_object_leave_cell8(g, cell, (uint8_t)v);
    menu_object_leave_cell8(g, cell + 1, (uint8_t)(v >> 8));
}

/* ---- Starting objects ----------------------------------------------------------------------- */

/* A new object takes on the starter's state from its sixth byte on: where it
 * is and how it moves, its look, its scripts. The starter is whichever object
 * ran last: before any has, what the running object's cell names. */
enum { INHERITED_FROM = 0x05 };

static MenuObject first_of(MenuSlotKind kind) { return kind == MENU_FIRST_SLOT ? 0 : MENU_FIRST_OBJECTS; }
static unsigned end_of(MenuSlotKind kind) { return kind == MENU_FIRST_SLOT ? MENU_FIRST_OBJECTS : MENU_OBJECTS; }

unsigned menu_last_object_of(MenuSlotKind kind) { return end_of(kind) - 1; }

unsigned menu_first_free_object(const GameState *g, MenuSlotKind kind)
{
    for (unsigned o = first_of(kind); o < end_of(kind); o++)
        if (menu_object_slots_handler(g, o) == 0)
            return o;
    return MENU_OBJECTS;
}

static void inherit(GameState *g, MenuObject to)
{
    uint16_t from = menu_object_current(g);
    for (unsigned i = INHERITED_FROM; i < SLOT_BYTES; i++)
        set_byte_at(g, (uint16_t)(menu_object_address(to) + i), byte_at(g, (uint16_t)(from + i)));
}

// $C2:048A, $C2:04CD
bool menu_start_object(GameState *g, uint16_t handler, MenuSlotKind kind, MenuObject *started)
{
    ORIGINAL_EFFECT(set_menu_object_scratch_spawn_handler(g, handler));
    unsigned o = menu_first_free_object(g, kind);
    if (o == MENU_OBJECTS)
        return false;
    set_menu_object_slots_handler(g, o, handler);
    set_menu_object_slots_age(g, o, 0);
    set_menu_object_slots_step(g, o, 0);
    inherit(g, (MenuObject)o);
    *started = (MenuObject)o;
    return true;
}

// $C2:04D9, $C2:04FB
MenuObject menu_start_script(GameState *g, uint32_t script, MenuSlotKind kind)
{
    ORIGINAL_EFFECT(set_menu_object_scratch_start_bank(g, (uint8_t)(script >> 16)));
    ORIGINAL_EFFECT(set_menu_object_scratch_start_script(g, (uint16_t)script));
    MenuObject o;
    if (!menu_start_object(g, MENU_SCRIPT_HANDLER, kind, &o))
        o = (MenuObject)menu_last_object_of(kind);
    set_menu_object_slots_script(g, o, script);
    set_menu_object_slots_command_frames(g, o, 0);
    return o;
}

/* ---- Where it is and how it moves --------------------------------------------------------- */

MenuPlace menu_object_place(const GameState *g, MenuObject o)
{
    return (MenuPlace){ menu_object_slots_x(g, o), menu_object_slots_y(g, o) };
}

void menu_object_set_place(GameState *g, MenuObject o, MenuPlace p)
{
    set_menu_object_slots_x(g, o, p.x);
    set_menu_object_slots_y(g, o, p.y);
}

MenuPlace menu_object_velocity(const GameState *g, MenuObject o)
{
    return (MenuPlace){ menu_object_slots_velocity_x(g, o), menu_object_slots_velocity_y(g, o) };
}

void menu_object_set_velocity(GameState *g, MenuObject o, MenuPlace v)
{
    set_menu_object_slots_velocity_x(g, o, v.x);
    set_menu_object_slots_velocity_y(g, o, v.y);
}

// $C2:1C84, $C2:1C9E
void menu_object_turn_round(GameState *g, MenuObject o, MenuAxis axis)
{
    MenuPlace v = menu_object_velocity(g, o);
    if (axis == MENU_ACROSS)
        v.x = 0u - v.x;
    else
        v.y = 0u - v.y;
    menu_object_set_velocity(g, o, v);
}

// $C2:0F38
void menu_object_move(GameState *g, MenuObject o)
{
    MenuPlace p = menu_object_place(g, o), v = menu_object_velocity(g, o);
    menu_object_set_place(g, o, (MenuPlace){ p.x + v.x, p.y + v.y });
}

// $C2:1CB8
/* The map's height is a power of two, so down is kept by masking; across,
 * an object a pixel off either edge comes round to the other. */
void menu_object_wrap_onto_map(GameState *g, MenuObject o)
{
    int16_t x = (int16_t)menu_object_slots_x_pixel(g, o);
    if (x < 0)
        x = (int16_t)(x + MENU_MAP_WIDTH);
    else if (x >= MENU_MAP_WIDTH)
        x = (int16_t)(x - MENU_MAP_WIDTH);
    set_menu_object_slots_x_pixel(g, o, (uint16_t)x);
    set_menu_object_slots_y_pixel(g, o, (uint16_t)(menu_object_slots_y_pixel(g, o) & (MENU_MAP_HEIGHT - 1)));
}

/* ---- Running them ---------------------------------------------------------------------------- */

/* An object's handler is done with it when it says so; the slot is then free.
 * Otherwise the object is a frame older. */
static void after_its_turn(GameState *g, uint16_t address, bool done)
{
    MenuObject o = menu_object_at_address(address);
    if (done)
        set_menu_object_slots_handler(g, o, 0);
    else
        set_menu_object_slots_age(g, o, (uint16_t)(menu_object_slots_age(g, o) + 1));
}

/* A handler is a routine of bank C2, which the slot names by its address. */
enum { HANDLERS_BANK = 0xC20000 };

/* An object's turn: its handler run, saying whether it is done; false where
 * it went no further. The handler leaves the running object where the loop
 * goes on from. */
typedef bool (*TakeATurn)(void *ctx, uint16_t address, uint16_t handler, bool *done);

// $C2:051D
/* Every object's turn this frame, in slot order: each slot with a handler
 * runs it, the slot made the running object. */
static bool run_the_objects(GameState *g, TakeATurn turn, void *ctx)
{
    for (uint16_t address = FIRST_SLOT_ADDRESS; address != SLOTS_END; address += SLOT_BYTES) {
        uint16_t handler = menu_object_slots_handler(g, menu_object_at_address(address));
        if (handler == 0)
            continue;
        set_menu_object_handler(g, handler);
        set_menu_object_current(g, address);
        bool done;
        if (!turn(ctx, address, handler, &done))
            return false;
        address = menu_object_current(g);
        after_its_turn(g, address, done);
    }
    return true;
}

/* ---- As C ---- */

typedef struct {
    GameState *game;
    Platform *pf;
} TurnsAsC;

/* A handler with a C row of its own (menu/handlers.h) runs as C where its
 * guard's state half takes the object; any other, or one that leaves the
 * running object elsewhere than a slot, is the machine's yet, and the
 * program goes no further there. */
static bool turn(void *ctx, uint16_t address, uint16_t handler, bool *done)
{
    TurnsAsC *c = ctx;
    GameState *g = c->game;
    MenuObject self = menu_object_at_address(address);
    const MenuHandler *h = menu_handler(handler);
    if (!h || !h->run) {
        program_needs_the_machine(g, HANDLERS_BANK | handler, h ? h->name : "an object's handler with no C");
        return false;
    }
    if (h->takes && !h->takes(g, self)) {
        program_needs_the_machine(g, HANDLERS_BANK | handler, h->name);
        return false;
    }
    MenuTurn t = MENU_TURN_WENT_NO_FURTHER;
    TIMED_AS(g, HANDLERS_BANK | handler, t = h->run(g, c->pf, self));
    if (t == MENU_TURN_WENT_NO_FURTHER)
        return false;
    if (!menu_object_address_names_one(menu_object_current(g))) {
        program_needs_the_machine(g, HANDLERS_BANK | handler, "a handler that left the running object off its slot");
        return false;
    }
    *done = t == MENU_TURN_DONE;
    return true;
}

bool menu_objects_run(GameState *g, Platform *pf)
{
    TurnsAsC c = { g, pf };
    return run_the_objects(g, turn, &c);
}

/* ---- On the machine ---- */

/* The handler runs where it is registered, called as the original's JSR at
 * $C2:053E calls its jump through the slot's handler (menu_object_handler,
 * $C2:0553), which adds nothing to the call but the jump: a ported handler
 * runs as C, any other as the original. It says it is done with the carry.
 * False where it came back in a way the loop can't go on from, or left the
 * running object elsewhere than a slot: the original then carries on from
 * the call. */
static bool turn_on_the_machine(void *ctx, uint16_t address, uint16_t handler, bool *done)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    if (address != FIRST_SLOT_ADDRESS) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_flag(s, CPU_FLAG_V, false);
    }
    s->cpu.a = handler;
    s->cpu.x = address;
    m_set_nz16(s, handler);
    m_sep(s, CPU_FLAG_M);
    m_jsr(s, 0xC2053E, HANDLERS_BANK | handler);
    if (s->cpu.e || m_x8(s) || !menu_object_address_names_one(menu_object_current(g))) {
        m_stop_at_own(s, 0xC20541, 0);
        return false;
    }
    *done = m_flag(s, CPU_FLAG_C);
    return true;
}

// $C2:051D
/* The loop's own registers are what it leaves: the address past the last
 * slot in A, and the carry of its last comparison. */
void menu_run_objects(Snes *s)
{
    if (!run_the_objects(state_of(s), turn_on_the_machine, s))
        return;
    s->cpu.a = SLOTS_END;
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_V, false);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* Starting an object copies the running object's state: from a slot, or from
 * low RAM below the I/O registers, where the running object's cell names
 * something else before the first object has run. */
static bool starter_in_low_ram(const GameState *g) { return menu_object_current(g) <= 0x2000 - SLOT_BYTES; }

bool menu_objects_on_page(Snes *s) { return m_direct_page(s) == 0 && m_bank_reaches_low_ram(s); }

bool menu_objects_can_start(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    bool ok = starter_in_low_ram(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_object_can_move(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_object_address_names_one(menu_object_current(state_of(s)));
    vars_peek(false);
    return ok;
}

/* A handler is entered with its slot in X as well, which is the one it
 * reaches its own state through. */
bool menu_object_handler_runs(Snes *s)
{
    if (!menu_object_can_move(s))
        return false;
    vars_peek(true);
    bool ok = s->cpu.x == menu_object_current(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_object_handler_takes(Snes *s, bool (*takes)(const GameState *g, MenuObject self))
{
    if (!menu_object_handler_runs(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = takes(g, menu_object_running(g));
    vars_peek(false);
    return ok;
}

/* The address a start leaves in X: past the slot it took, or past the last it
 * looked at where every one was busy. */
static uint16_t past(const GameState *g, MenuSlotKind kind)
{
    unsigned o = menu_first_free_object(g, kind);
    return menu_object_address((MenuObject)(o == MENU_OBJECTS ? end_of(kind) : o + 1));
}

static void start_object_entry(Snes *s, MenuSlotKind kind)
{
    GameState *g = state_of(s);
    uint16_t x = past(g, kind);
    MenuObject started;
    (void)menu_start_object(g, s->cpu.x, kind, &started);
    s->cpu.x = x;
    m_set_flag(s, CPU_FLAG_C, false);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:048A
/* Start an object running the handler in X, in the first free ordinary slot. */
void menu_object_spawn(Snes *s) { start_object_entry(s, MENU_ORDINARY_SLOT); }

// $C2:04CD
/* The same among the four slots a screen runs first. */
void menu_object_spawn_high(Snes *s) { start_object_entry(s, MENU_FIRST_SLOT); }

/* Starting one on a script leaves X on the slot it took (or the last it
 * looked at), the script's high byte over its bank in A, and the carry and
 * overflow of the subtraction that found the slot. */
static void start_script_entry(Snes *s, MenuSlotKind kind)
{
    GameState *g = state_of(s);
    uint8_t bank = (uint8_t)s->cpu.a;
    uint16_t at = s->cpu.x;
    uint16_t x = (uint16_t)(past(g, kind) - SLOT_BYTES);
    menu_start_script(g, (uint32_t)bank << 16 | at, kind);
    s->cpu.x = x;
    s->cpu.a = (uint16_t)((at & 0xFF00) | bank);
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_V, false);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:04D9
/* Start an object on the script at X in the bank in A. */
void menu_object_start_script(Snes *s) { start_script_entry(s, MENU_ORDINARY_SLOT); }

// $C2:04FB
void menu_object_start_script_high(Snes *s) { start_script_entry(s, MENU_FIRST_SLOT); }

// $C2:0F38
/* Move the running object by its velocity; it leaves X on the object's slot. */
void menu_object_step_position(Snes *s)
{
    GameState *g = state_of(s);
    menu_object_move(g, menu_object_running(g));
    s->cpu.x = menu_object_current(g);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:0471
/* Free every object slot: none of them runs a handler, and the first is the
 * one running. */
void menu_free_every_object(GameState *g)
{
    menu_object_make_running(g, 0);
    for (unsigned o = 0; o < MENU_OBJECTS; o++)
        set_menu_object_slots_handler(g, o, 0);
}

// $C2:0471
/* It comes back with an 8-bit accumulator. */
void menu_free_every_object_entry(Snes *s)
{
    menu_free_every_object(state_of(s));
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

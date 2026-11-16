#include "port/state_host.h"
#include "port/vars_host.h"
#include "port/arena.h"

#include <stdlib.h>
#include <string.h>

#include "charge.h"
#include "core/program.h"
#include "battle/wide_window.h"
#include "core/panel_layout.h"
#include "core/sprite_table.h"
#include "event/records.h"
#include "field/margin_objects.h"
#include "field/streaming.h"
#include "menu/menu.h"
#include "menu/records.h"
#include "menu/panels_across.h"
#include "menu/sprites.h"
#include "cpu_io.h"
#include "platform/time_model.h"
#include "port/time_rates.h"
#include "port/vars_ram.h"
#include "rom.h"
#include "wram.h"

struct GameState {
    StateHostHead head;   /* the storage, then the game's time, where the accessors reach them (state_host.h) */
    ProgramHandlers handlers;
    MenuCoroutines menus;
    SpritePlaces sprite_places;
    FieldMargins field_margins;
    PanelLayout panel_layout;
    BattleWideWindow battle_wide_window;
    MenuPanels menu_panels;
    FieldObjectsPast field_objects_past;
    MenuSpritesPast menu_sprites_past;
    EventRecords event_records;
    MenuRecords menu_records;
    /* save RAM: `kept_sram` below, or the host's own bytes (state_host_create_lasting_over) */
    uint8_t *sram;
    /* the multiplier and divider: their operands, and the result the
     * product-or-remainder register shows */
    uint8_t multiplicand;
    uint16_t dividend, quotient, product_or_remainder;
    /* the work RAM port's 17-bit address */
    uint32_t port;
    unsigned long strays;
    uint32_t stray[STATE_HOST_STRAYS_KEPT];
    bool stopped;
    bool scene_next;          /* the program goes on to a scene (program_goes_on_to_a_scene) */
    uint8_t scene;
    /* the scenes the driver starts the game in, one after another (program_starts_in_a_scene) */
    uint8_t first_scene[STATE_HOST_FIRST_SCENES], first_song[STATE_HOST_FIRST_SCENES];
    unsigned first_scenes, first_scene_taken;
    bool field_again;         /* the field starts again (program_starts_the_field_again) */
    bool afresh;              /* the game starts afresh from the title (program_starts_afresh_from_the_title) */
    bool reset;               /* the console reset (program_resets_the_console) */
    uint32_t stopped_at;
    const char *stopped_what;
    uint32_t owed;            /* the original's time charged since the timing backend last took it (master clocks) */
    unsigned timed;           /* index in time_rates, or time_rate_count for untagged work */
    unsigned looks;           /* a guard's looks open (vars_peek), nested */
    uint32_t look_mark;       /* where the outermost look's uncounted stretch began */
    StateHostTime *time;      /* optional diagnostic totals; never used to schedule work */
    bool lasting;             /* in the arena, for the run's whole life (state_host_create_lasting) */
    uint8_t kept_sram[];      /* save RAM where the state keeps it: STATE_HOST_SRAM_BYTES, or none */
};

static GameState *start(GameState *g, uint8_t *save, const uint8_t *sram)
{
    if (!g)
        return NULL;
#ifndef CT_UNTIMED
    g->timed = time_rate_count;
#endif
    vars_host_init(&g->head.vars);
    g->sram = save ? save : g->kept_sram;
    if (sram)
        memcpy(g->sram, sram, STATE_HOST_SRAM_BYTES);
    return g;
}

GameState *state_host_create(const uint8_t *sram)
{
    return start(calloc(1, sizeof(GameState) + STATE_HOST_SRAM_BYTES), NULL, sram);
}

static GameState *lasting(GameState *g)
{
    if (g)
        g->lasting = true;
    return g;
}

GameState *state_host_create_lasting(const uint8_t *sram)
{
    return lasting(start(arena_take(sizeof(GameState) + STATE_HOST_SRAM_BYTES), NULL, sram));
}

GameState *state_host_create_lasting_over(uint8_t *save)
{
    return lasting(start(arena_take(sizeof(GameState)), save, NULL));
}

void state_host_destroy(GameState *g)
{
    if (!g)
        return;
    free(g->time);
    if (!g->lasting)
        free(g);
}

/* The whole record, but the diagnostic totals the run keeps for itself; and
 * where the state is work RAM as the original lays it out (CT_FLAT_STATE),
 * work RAM's 128 KiB after the record, which keeps only where they are. Save
 * RAM comes between, since the record keeps only where it is too. */
#ifdef CT_FLAT_STATE
enum { FLAT_WORK_RAM_BYTES = 0x20000 };
#else
enum { FLAT_WORK_RAM_BYTES = 0 };
#endif
enum { SNAPSHOT_WORK_RAM_AT = sizeof(GameState) + STATE_HOST_SRAM_BYTES };
size_t state_host_snapshot_bytes(void) { return SNAPSHOT_WORK_RAM_AT + (size_t)FLAT_WORK_RAM_BYTES; }

void state_host_snapshot(const GameState *g, uint8_t *out)
{
    memcpy(out, g, sizeof *g);
    memcpy(out + sizeof *g, g->sram, STATE_HOST_SRAM_BYTES);
#ifdef CT_FLAT_STATE
    vars_host_read_span(&g->head.vars, 0x7E0000u, out + SNAPSHOT_WORK_RAM_AT, FLAT_WORK_RAM_BYTES);
#endif
}

void state_host_restore(GameState *g, const uint8_t *in)
{
    StateHostTime *time = g->time;
    bool lasting = g->lasting;
    uint8_t *sram = g->sram;
#ifdef CT_FLAT_STATE
    VarsHost pages = g->head.vars;
#endif
    memcpy(g, in, sizeof *g);
    g->time = time;
    g->lasting = lasting;
    g->sram = sram;
    memcpy(g->sram, in + sizeof *g, STATE_HOST_SRAM_BYTES);
#ifdef CT_FLAT_STATE
    g->head.vars = pages;
    for (uint32_t at = 0; at < FLAT_WORK_RAM_BYTES; at++)
        *vars_flat_byte(&g->head.vars, at) = in[SNAPSHOT_WORK_RAM_AT + at];
#endif
}

ProgramHandlers *program_handlers(GameState *g) { return &g->handlers; }
MenuCoroutines *menu_coroutines(GameState *g) { return &g->menus; }
SpritePlaces *sprite_places(GameState *g) { return &g->sprite_places; }
FieldMargins *field_margins(GameState *g) { return &g->field_margins; }
PanelLayout *panel_layout(GameState *g) { return &g->panel_layout; }
BattleWideWindow *battle_wide_window(GameState *g) { return &g->battle_wide_window; }
MenuPanels *menu_panels(GameState *g) { return &g->menu_panels; }
FieldObjectsPast *field_objects_past(GameState *g) { return &g->field_objects_past; }
MenuSpritesPast *menu_sprites_past(GameState *g) { return &g->menu_sprites_past; }
EventRecords *event_records(GameState *g) { return &g->event_records; }
MenuRecords *menu_records(GameState *g) { return &g->menu_records; }

uint8_t *state_host_sram(GameState *g) { return g->sram; }
/* state_host_head (state_host.h) reaches the head as the state's first member. */
_Static_assert(offsetof(GameState, head) == 0, "the state's head is its first member");

unsigned long state_host_strays(const GameState *g, uint32_t *first, unsigned *kept)
{
    unsigned n = g->strays < STATE_HOST_STRAYS_KEPT ? (unsigned)g->strays : STATE_HOST_STRAYS_KEPT;
    if (first)
        memcpy(first, g->stray, n * sizeof *first);
    if (kept)
        *kept = n;
    return g->strays;
}

bool state_host_stopped(const GameState *g, uint32_t *routine, const char **what)
{
    if (routine)
        *routine = g->stopped_at;
    if (what)
        *what = g->stopped_what;
    return g->stopped;
}

/* ---- Memory at the original's addresses ------------------------------------------------------ */

/* Where an address lies: work RAM (banks $7E-$7F, and the first 8 KiB of every
 * bank that maps the system area), save RAM (HiROM's $6000-$7FFF in banks
 * $20-$3F and $A0-$BF, 8 KiB mirrored), the cartridge, or nowhere the host has. */
typedef enum { NOWHERE, IN_WRAM, IN_SRAM, IN_ROM } Place;

static Place place_of(uint32_t addr, uint32_t *offset)
{
    uint8_t bank = (uint8_t)(addr >> 16);
    uint16_t at = (uint16_t)addr;
    bool system_bank = bank < 0x40 || (bank >= 0x80 && bank < 0xC0);
    if (bank == 0x7E || bank == 0x7F) {
        *offset = addr - 0x7E0000;
        return IN_WRAM;
    }
    if (system_bank && at < 0x2000) {
        *offset = at;
        return IN_WRAM;
    }
    if (system_bank && (bank & 0x3F) >= 0x20 && at >= 0x6000 && at < 0x8000) {
        *offset = (uint32_t)(at - 0x6000) % STATE_HOST_SRAM_BYTES;
        return IN_SRAM;
    }
    if (!system_bank || at >= 0x8000)
        return IN_ROM;
    return NOWHERE;
}

bool state_host_is_cartridge(uint32_t addr)
{
    uint32_t offset;
    return place_of(addr & 0xFFFFFF, &offset) == IN_ROM;
}

static void stray(const GameState *g, uint32_t addr)
{
    GameState *m = (GameState *)g;   /* the count is the backing's own, not the game's state */
    if (m->strays < STATE_HOST_STRAYS_KEPT)
        m->stray[m->strays] = addr;
    m->strays++;
}

uint8_t state_host_read8(const GameState *g, uint32_t addr)
{
    uint32_t offset = (addr & 0xFFFFFF) - 0x7E0000u;
    if (offset < 0x20000u) {
        /* work RAM's own banks, where most reads by address are */
        uint8_t value = 0;
        vars_host_read8(&g->head.vars, 0x7E0000u + offset, &value);
        return value;
    }
    switch (place_of(addr & 0xFFFFFF, &offset)) {
    case IN_WRAM: {
        uint8_t value = 0;
        vars_host_read8(&g->head.vars, 0x7E0000u + offset, &value);
        return value;
    }
    case IN_SRAM: return g->sram[offset];
    case IN_ROM: {
        /* the host's read of the cartridge, not the game's: no time */
        GameState *m = (GameState *)g;
        uint32_t mark = charge_uncounted_begin(m);
        uint8_t v = rom_read8(g, addr);
        charge_uncounted_end(m, mark);
        return v;
    }
    case NOWHERE: break;
    }
    stray(g, addr & 0xFFFFFF);
    return 0;
}

void state_host_read_span(const GameState *g, uint32_t addr, uint8_t *out, uint32_t len)
{
    /* Work RAM's banks, at their own addresses or the first 8 KiB's in a bank
     * that maps the system area, in one read of the storage; anything else a
     * byte at a time, as state_host_read8 has it. */
    if (addr >= 0x7E0000u && addr < 0x800000u && len <= 0x800000u - addr) {
        vars_host_read_span(&g->head.vars, addr, out, len);
        return;
    }
    uint32_t first, last;
    if (len && (addr >> 16) == ((addr + len - 1u) >> 16) && place_of(addr & 0xFFFFFF, &first) == IN_WRAM &&
        place_of((addr + len - 1u) & 0xFFFFFF, &last) == IN_WRAM) {
        vars_host_read_span(&g->head.vars, 0x7E0000u + first, out, len);
        return;
    }
    for (uint32_t i = 0; i < len; i++)
        out[i] = state_host_read8(g, addr + i);
}

void state_host_write8(GameState *g, uint32_t addr, uint8_t v)
{
    uint32_t offset = (addr & 0xFFFFFF) - 0x7E0000u;
    if (offset < 0x20000u) {
        /* work RAM's own banks, where most writes by address are */
        vars_host_write8(&g->head.vars, 0x7E0000u + offset, v);
        return;
    }
    switch (place_of(addr & 0xFFFFFF, &offset)) {
    case IN_WRAM:
        /* Bulk clears may cross retired cells and padding. They have no
         * storage; inspection reads of those addresses return zero. */
        vars_host_write8(&g->head.vars, 0x7E0000u + offset, v);
        return;
    case IN_SRAM: g->sram[offset] = v; return;
    case IN_ROM:
    case NOWHERE: break;
    }
    stray(g, addr & 0xFFFFFF);
}

/* ---- The multiplier, the divider and the work RAM port (cpu_io.h) ---------------------------- */

void hw_set_multiplicand(GameState *g, uint8_t a) { g->multiplicand = a; }

uint16_t hw_multiply_by(GameState *g, uint8_t b)
{
    g->product_or_remainder = (uint16_t)(g->multiplicand * b);
    return g->product_or_remainder;
}

uint8_t hw_multiplicand(const GameState *g) { return g->multiplicand; }

void hw_set_dividend(GameState *g, uint16_t dividend) { g->dividend = dividend; }

/* By 0 the quotient is all ones and the remainder the dividend, as the console's. */
void hw_divide_by(GameState *g, uint8_t divisor)
{
    g->quotient = divisor ? (uint16_t)(g->dividend / divisor) : 0xFFFF;
    g->product_or_remainder = divisor ? (uint16_t)(g->dividend % divisor) : g->dividend;
}

/* The word's high byte goes to the register after the divisor, which isn't the divider's. */
void hw_divide_by_word(GameState *g, uint16_t word) { hw_divide_by(g, (uint8_t)word); }

uint16_t hw_quotient(const GameState *g) { return g->quotient; }
uint16_t hw_remainder(const GameState *g) { return g->product_or_remainder; }
uint16_t hw_dividend(const GameState *g) { return g->dividend; }

enum { PORT_MASK = 0x1FFFF, IO_ACCESS_CLOCKS = 6 };

void hw_port_seek(GameState *g, uint16_t at, uint8_t bank)
{
    state_host_game_access(g, 3 * IO_ACCESS_CLOCKS);
    g->port = ((uint32_t)(bank & 1) << 16 | at) & PORT_MASK;
}
void hw_port_seek_address(GameState *g, uint16_t at)
{
    state_host_game_access(g, 2 * IO_ACCESS_CLOCKS);
    g->port = (g->port & 0x10000) | at;
}
void hw_port_seek_bank(GameState *g, uint8_t bank)
{
    state_host_game_access(g, IO_ACCESS_CLOCKS);
    g->port = (uint32_t)(bank & 1) << 16 | (g->port & 0xFFFF);
}

uint8_t hw_port_read(GameState *g)
{
    state_host_game_access(g, IO_ACCESS_CLOCKS);
    uint8_t v = state_host_read8(g, 0x7E0000u + g->port);
    g->port = (g->port + 1) & PORT_MASK;
    return v;
}

void hw_port_write(GameState *g, uint8_t v)
{
    state_host_game_access(g, IO_ACCESS_CLOCKS);
    state_host_write8(g, 0x7E0000u + g->port, v);
    g->port = (g->port + 1) & PORT_MASK;
}

void hw_port_read_span(GameState *g, uint8_t *bytes, uint16_t count)
{
    state_host_game_access(g, (uint32_t)count * IO_ACCESS_CLOCKS);
    /* to $7F's end, then round to $7E's start (a count is under a bank) */
    uint32_t room = PORT_MASK + 1u - g->port;
    if (count > room) {
        vars_host_read_span(&g->head.vars, 0x7E0000u + g->port, bytes, room);
        bytes += room, count = (uint16_t)(count - room), g->port = 0;
    }
    vars_host_read_span(&g->head.vars, 0x7E0000u + g->port, bytes, count);
    g->port = (g->port + count) & PORT_MASK;
}

void hw_port_write_span(GameState *g, const uint8_t *bytes, uint16_t count)
{
    state_host_game_access(g, (uint32_t)count * IO_ACCESS_CLOCKS);
    /* to $7F's end, then round to $7E's start (a count is under a bank) */
    uint32_t room = PORT_MASK + 1u - g->port;
    if (count > room) {
        vars_host_write_span(&g->head.vars, 0x7E0000u + g->port, bytes, room);
        bytes += room, count = (uint16_t)(count - room), g->port = 0;
    }
    vars_host_write_span(&g->head.vars, 0x7E0000u + g->port, bytes, count);
    g->port = (g->port + count) & PORT_MASK;
}

/* The byte the original leaves below the port: no register, nothing to keep. */
void hw_port_leave_spill(GameState *g, uint8_t low) { (void)g, (void)low; }

/* ---- Work RAM the game locates at run time (wram.h) ------------------------------------------ */

static uint32_t in_bank(uint32_t at, uint32_t offset) { return (at & 0xFF0000u) | (uint16_t)(at + offset); }

static void owe(GameState *g, uint32_t clocks);

void wram_clear(GameState *g, PfOrigin o, uint32_t at, uint32_t bytes)
{
    (void)o;
    if (!bytes)
        return;
    /* Clearing a transition's buffer still occupies its transfer time.
     * Keep it in the state's account, independent of the active port's rate. */
    owe(g, time_of_transfer(bytes));
    wram_fill8(g, at, bytes, 0);
}

/* Work RAM's own banks, $7E and $7F, which a fill reaches in runs of the
 * storage rather than a write at a time. */
enum { BANK_BYTES = 0x10000 };
static bool in_work_ram_banks(uint32_t at)
{
    uint8_t bank = (uint8_t)(at >> 16);
    return bank == 0x7E || bank == 0x7F;
}

void wram_fill8(GameState *g, uint32_t at, uint32_t bytes, uint8_t v)
{
    if (!in_work_ram_banks(at)) {
        for (uint32_t i = 0; i < bytes; i++)
            state_host_write8(g, in_bank(at, i), v);
        return;
    }
    /* to the bank's end, then round to its start */
    at &= 0xFFFFFF;
    while (bytes) {
        uint32_t room = BANK_BYTES - (at & 0xFFFF), n = bytes < room ? bytes : room;
        vars_host_fill_span(&g->head.vars, at, v, n);
        bytes -= n;
        at &= 0xFF0000;
    }
}

void wram_fill16(GameState *g, uint32_t at, uint32_t words, uint16_t v)
{
    /* a word of two bytes the same is a fill of bytes, which work RAM's
     * banks take a run at a time (the battle's memory cleared on the way
     * in, 32 KiB, was half a second of the GBA's frame a word at a time) */
    if ((uint8_t)v == (uint8_t)(v >> 8)) {
        wram_fill8(g, at, 2 * words, (uint8_t)v);
        return;
    }
    for (uint32_t i = 0; i < words; i++) {
        state_host_write8(g, in_bank(at, 2 * i), (uint8_t)v);
        state_host_write8(g, in_bank(at, 2 * i + 1), (uint8_t)(v >> 8));
    }
}

/* Work RAM's two banks as the port steps through them. */
enum { WORK_RAM = 0x7E0000, WORK_RAM_SPAN = 0x20000 };
static uint32_t through_the_port(uint32_t at, uint32_t i) { return WORK_RAM + (at + i) % WORK_RAM_SPAN; }

bool wram_put_unpacked(GameState *g, const uint8_t *from, uint32_t at, uint32_t bytes)
{
#ifdef CT_FLAT_STATE
    /* work RAM as the original lays it out: a run of it in pages owners
     * keep whole reads back what is written there */
    if (!bytes || !in_work_ram_banks(at) || (at & 0xFFFF) + bytes >= BANK_BYTES ||
        !vars_flat_wholly_owned((at & 0xFFFFFF) - WORK_RAM, bytes))
        return false;
    vars_host_write_span(&g->head.vars, at & 0xFFFFFF, from, bytes);
    return true;
#else
    /* the typed state's owners are the records', which it doesn't answer for a run */
    (void)g, (void)from, (void)at, (void)bytes;
    return false;
#endif
}

void wram_port_fill(GameState *g, uint32_t at, uint32_t bytes, uint8_t v)
{
    /* to $7F's end, then round to $7E's start */
    at %= WORK_RAM_SPAN;
    while (bytes) {
        uint32_t room = WORK_RAM_SPAN - at, n = bytes < room ? bytes : room;
        vars_host_fill_span(&g->head.vars, WORK_RAM + at, v, n);
        bytes -= n;
        at = 0;
    }
}

void wram_port_copy(GameState *g, PfData from, uint32_t at, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++) {
        const uint8_t *byte = pf_data_pointer(from, i);
        state_host_write8(g, through_the_port(at, i), byte ? *byte : state_host_read8(g, pf_data_address(from, i)));
    }
}

/* A copy that reaches past work RAM's own banks or runs round one, a byte at
 * a time by address: seldom made, kept apart from wram_copy, which runs from a
 * small target's fast memory. */
__attribute__((noinline)) static void copy_by_address(GameState *g, uint32_t from, uint32_t to, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++)
        state_host_write8(g, in_bank(to, i), state_host_read8(g, in_bank(from, i)));
}

void wram_copy(GameState *g, uint32_t from, uint32_t to, uint32_t bytes)
{
    if (in_work_ram_banks(from) && in_work_ram_banks(to) && (from & 0xFFFF) + bytes <= BANK_BYTES &&
        (to & 0xFFFF) + bytes <= BANK_BYTES)
        vars_host_copy_span(&g->head.vars, from & 0xFFFFFF, to & 0xFFFFFF, bytes);
    else
        copy_by_address(g, from, to, bytes);
}

/* ---- Time: what the frame-level time model charges ------------------------------------------- *
 *
 * A build with no machine has no bus to count, so the game's own work is
 * timed here, as the machine's estimate times a port's (docs/REIMPLEMENTATION.md,
 * "The frame-level time model"): each access the game makes -- its
 * accessors, its reads of the cartridge, save RAM through the save service
 * (TmHostProgram.accessed) -- is the bus clocks it takes on the machine
 * (state_host_game_access), at the calibrated rate of the routine the work is
 * timed as (charge_time_as_begin, TIMED_AS scopes), the
 * rates the machine measured (port/time_rates.h). An access only counts its
 * clocks; a stretch of them at one rate is priced once, before anything
 * changes how it would be priced (price_accesses). What a port does another
 * way is charged as the machine charges it: a charge at its routine's rate
 * (charge_as), a block move at the block move's, and a retired routine's
 * accesses at its retained rate (charge_retired_begin). A nested uncounted
 * stretch drops its accesses before the retired routine is priced. Work no
 * tag names, and a guard's look (vars_peek), take none. The timing backend
 * takes the account as the main thread waits (state_host_take_owed) and makes
 * lag frames of the whole frames it fills, and drops the handlers' at the
 * main thread's start of a frame, keeping it as the frame's own.
 *
 * A build with no time model (CT_UNTIMED, the GBA's) keeps none of this:
 * the accesses count nothing, a scope is its statement (charge.h), and the
 * charges are empty. Its host decides the game's frames instead, replaying
 * the decisions another build's time model made or from its own time
 * (TmHostProgram.decide, platform/native/host.h; docs/PLATFORM.md). */

#ifdef CT_UNTIMED
static void owe(GameState *g, uint32_t clocks) { (void)g, (void)clocks; }

bool state_host_record_time(GameState *g)
{
    (void)g;
    return false;
}

const StateHostTime *state_host_time(const GameState *g, unsigned *count)
{
    (void)g;
    *count = 0;
    return NULL;
}

void charge_as(GameState *g, uint32_t routine, uint32_t accesses) { (void)g, (void)routine, (void)accesses; }
void charge_block_move(GameState *g, uint32_t bytes) { (void)g, (void)bytes; }
void charge_io_accesses(GameState *g, uint32_t accesses) { (void)g, (void)accesses; }
void charge_rom_accesses(GameState *g, uint32_t accesses) { (void)g, (void)accesses; }
void charge_accesses(GameState *g, uint32_t accesses) { (void)g, (void)accesses; }

uint32_t state_host_take_owed(GameState *g)
{
    (void)g;
    return 0;
}

uint32_t charge_retired_begin(GameState *g)
{
    (void)g;
    return 0;
}

void charge_retired_end(GameState *g, uint32_t mark, uint32_t routine, uint32_t accesses)
{
    (void)g, (void)mark, (void)routine, (void)accesses;
}

uint32_t charge_uncounted_begin(GameState *g)
{
    (void)g;
    return 0;
}

void charge_uncounted_end(GameState *g, uint32_t mark) { (void)g, (void)mark; }

void vars_peek(const GameState *g, bool on)
{
#ifdef CT_LEAN
    (void)g, (void)on;
#else
    GameState *state = (GameState *)(uintptr_t)g;
    if (on) {
        if (state->looks++ == 0)
            vars_peek_flag(true);
    } else if (state->looks && --state->looks == 0) {
        vars_peek_flag(false);
    }
#endif
}

int charge_time_as_begin(GameState *g, uint32_t routine)
{
    (void)g, (void)routine;
    return 0;
}

void charge_time_as_end(GameState *g, int was) { (void)g, (void)was; }
#else
static unsigned rate_search(uint32_t routine)
{
    unsigned lo = 0, hi = time_rate_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (time_rates[mid].routine < routine)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < time_rate_count && time_rates[lo].routine == routine ? lo : time_rate_count;
}

/* A rate as the account keeps it (StateHostAccount.rate): its cost_milli
 * split into whole clocks and thousandths, so a pricing needs no division. */
static uint32_t split_rate(uint32_t cost_milli)
{
    return cost_milli / 1000u << STATE_HOST_RATE_MILLI_BITS | cost_milli % 1000u;
}
static uint32_t rate_clocks(uint32_t rate) { return rate >> STATE_HOST_RATE_MILLI_BITS; }
static uint32_t rate_milli(uint32_t rate) { return rate & ((1u << STATE_HOST_RATE_MILLI_BITS) - 1u); }

/* The rows the scopes named lately, each with its rate split, so a scope
 * finds its rate with one comparison rather than a search of the whole
 * table, and with no division. A row the rates name is marked so (NAMED),
 * since a named rate may be 0. Outside the lean build each keeps its index
 * in the table too, for the totals. */
enum { RECENT = 256, ROW_KEPT = 1u << 24, NAMED = 1u << 31 };
typedef struct {
    uint32_t key;    /* the routine with ROW_KEPT; 0, none yet */
    uint32_t rate;   /* its rate split, with NAMED where the rates name it */
#ifndef CT_LEAN
    unsigned index;  /* its row in time_rates, or time_rate_count */
#endif
} RecentRow;
static RecentRow recent[RECENT];

static RecentRow *row_looked_up(RecentRow *r, uint32_t routine)
{
    unsigned index = rate_search(routine);
    r->key = routine | ROW_KEPT;
    r->rate = index < time_rate_count ? split_rate(time_rates[index].cost_milli) | NAMED : 0;
#ifndef CT_LEAN
    r->index = index;
#endif
    return r;
}

static inline const RecentRow *row_of(uint32_t routine)
{
    routine &= 0xFFFFFF;
    RecentRow *r = &recent[(routine ^ routine >> 7 ^ routine >> 14) & (RECENT - 1u)];
    return r->key == (routine | ROW_KEPT) ? r : row_looked_up(r, routine);
}

/* A row's rate split, without its mark; 0 for a routine the rates don't name. */
static uint32_t row_rate(const RecentRow *r) { return r->rate & ~NAMED; }
static bool row_named(const RecentRow *r) { return (r->rate & NAMED) != 0; }

static unsigned row_index(const RecentRow *r)
{
#ifdef CT_LEAN
    (void)r;
    return time_rate_count;
#else
    return r->index;
#endif
}

/* The work's thousandths of a clock made whole, short of the last clock. */
static void whole_clocks(GameState *g)
{
    uint32_t clocks = g->head.account.work_milli / 1000u;
    g->head.account.work_clocks = clocks > UINT32_MAX - g->head.account.work_clocks ? UINT32_MAX : g->head.account.work_clocks + clocks;
    g->head.account.work_milli %= 1000u;
}

/* `clocks` of the machine's bus at a rate of `whole` clocks and `milli`
 * thousandths each, added to the work exactly as their product in
 * thousandths would be, in 32-bit sums: a run of them at a time small enough
 * that neither product can pass 2^32 (a rate's whole clocks fit 16 bits,
 * tests/test_time_rates.py) and the thousandths stay under it. */
enum { MILLI_LIMIT = 0x40000000, CLOCKS_AT_ONCE = 0xFFFF };

static void add_work(GameState *g, uint32_t clocks, uint32_t whole, uint32_t milli)
{
    while (clocks) {
        uint32_t n = clocks < CLOCKS_AT_ONCE ? clocks : CLOCKS_AT_ONCE;
        uint32_t add = n * whole;
        g->head.account.work_clocks = add > UINT32_MAX - g->head.account.work_clocks ? UINT32_MAX
                                                                                     : g->head.account.work_clocks + add;
        g->head.account.work_milli += n * milli;
        if (g->head.account.work_milli >= MILLI_LIMIT)
            whole_clocks(g);
        clocks -= n;
    }
}

/* `clocks` at a split rate. */
static void add_work_at(GameState *g, uint32_t clocks, uint32_t rate)
{
    add_work(g, clocks, rate_clocks(rate), rate_milli(rate));
}

/* Whether the run keeps the totals; the lean build (CT_LEAN) keeps none. */
#ifdef CT_LEAN
#define REPORTING(g) ((void)(g), false)
#else
#define REPORTING(g) ((g)->time != NULL)
#endif

static void price_accesses(GameState *g);

bool state_host_record_time(GameState *g)
{
#ifdef CT_LEAN
    (void)g;
    return false;
#else
    if (g->time)
        return true;
    price_accesses(g);   /* the totals start here: what came before isn't in them */
    g->time = calloc(time_rate_count + 1u, sizeof *g->time);
    if (!g->time)
        return false;
    for (unsigned i = 0; i < time_rate_count; i++)
        g->time[i].routine = time_rates[i].routine;
    return true;
#endif
}

const StateHostTime *state_host_time(const GameState *g, unsigned *count)
{
    price_accesses((GameState *)g);   /* the totals so far: the account is the backing's own */
    *count = g->time ? time_rate_count + 1u : 0;
    return g->time;
}

/* `rate` split, as the account keeps it. */
static void record_work(GameState *g, unsigned index, uint32_t bus, uint32_t charged, uint32_t rate)
{
#ifdef CT_LEAN
    (void)g, (void)index, (void)bus, (void)charged, (void)rate;
#else
    if (REPORTING(g)) {
        StateHostTime *t = &g->time[index];
        t->bus_clocks += bus;
        t->charged_clocks += charged;
        t->milli_clocks += ((uint64_t)bus + charged) * (rate_clocks(rate) * 1000u + rate_milli(rate));
    }
#endif
}

static void owe(GameState *g, uint32_t clocks)
{
    g->owed = clocks > UINT32_MAX - g->owed ? UINT32_MAX : g->owed + clocks;
}

/* The accesses counted since the last pricing, priced at the rate they were
 * made at: the same work as pricing each as it was made. Inside an uncounted
 * stretch they aren't the game's time (the stretch's end puts the count back
 * where it began). Called before the rate, the routine the work is timed as
 * or whether accesses count changes, and as the backend takes the work. */
static void price_accesses(GameState *g)
{
    StateHostAccount *a = &g->head.account;
    uint32_t clocks = a->raw_bus - a->priced;
    a->priced = a->raw_bus;
    if (!clocks || a->uncounted)
        return;
    add_work_at(g, clocks, a->rate);
    record_work(g, g->timed, clocks, 0, a->rate);
}

/* The block move stub's routine ($7E:9690), whose rate a byte moved is
 * charged at (a read and a write), at the retained block-copy rate. */
enum { BLOCK_MOVE = 0x7E9690, ACCESS_CLOCKS = 8, ACCESSES_PER_BYTE_MOVED = 2 };

void charge_as(GameState *g, uint32_t routine, uint32_t accesses)
{
    const RecentRow *row = row_of(routine);
    uint32_t clocks = accesses * ACCESS_CLOCKS;
    uint32_t rate = row_rate(row);
    if (rate) {
        add_work_at(g, clocks, rate);
        record_work(g, row_index(row), 0, clocks, rate);
    } else {
        add_work_at(g, clocks, g->head.account.rate);
        record_work(g, g->timed, 0, clocks, g->head.account.rate);
    }
}

void charge_block_move(GameState *g, uint32_t bytes) { charge_as(g, BLOCK_MOVE, bytes * ACCESSES_PER_BYTE_MOVED); }

void charge_io_accesses(GameState *g, uint32_t accesses)
{
    state_host_game_access(g, accesses * IO_ACCESS_CLOCKS);
}

void charge_rom_accesses(GameState *g, uint32_t accesses)
{
    state_host_game_access(g, accesses * STATE_HOST_CARTRIDGE_CLOCKS);
}

void charge_accesses(GameState *g, uint32_t accesses)
{
    state_host_game_access(g, accesses * ACCESS_CLOCKS);
}

uint32_t state_host_take_owed(GameState *g)
{
    price_accesses(g);
    whole_clocks(g);
    owe(g, g->head.account.work_clocks);
    g->head.account.work_clocks = 0;
    uint32_t owed = g->owed;
    g->owed = 0;
    return owed;
}

uint32_t charge_retired_begin(GameState *g)
{
    price_accesses(g);
    g->head.account.uncounted++;
    return g->head.account.raw_bus;
}

void charge_retired_end(GameState *g, uint32_t mark, uint32_t routine, uint32_t accesses)
{
    uint32_t bus = g->head.account.raw_bus - mark;
    g->head.account.raw_bus = g->head.account.priced = mark;
    g->head.account.uncounted--;
    const RecentRow *row = row_of(routine);
    unsigned index = row_index(row);
    uint32_t rate = row_named(row) ? row_rate(row) : g->head.account.rate;
    uint32_t charged = accesses * ACCESS_CLOCKS;
    add_work_at(g, bus, rate);
    add_work_at(g, charged, rate);
    record_work(g, index < time_rate_count ? index : g->timed, bus, charged, rate);
    if (REPORTING(g))
        g->time[index < time_rate_count ? index : g->timed].tags++;
}

uint32_t charge_uncounted_begin(GameState *g)
{
    price_accesses(g);
    g->head.account.uncounted++;
    return g->head.account.raw_bus;
}

void charge_uncounted_end(GameState *g, uint32_t mark)
{
    g->head.account.raw_bus = g->head.account.priced = mark;
    g->head.account.uncounted--;
}

/* A guard's look is an uncounted stretch: what it reads is counted as any
 * access is, and dropped as it ends, so no access asks whether a look is
 * open. The looks are the program's questions, made through a constant
 * state; the account is the backing's own. Where the port's checks look too
 * (PORT_CHECK, outside the lean build), the flag they look by is set with it. */
void vars_peek(const GameState *g, bool on)
{
    GameState *state = (GameState *)(uintptr_t)g;
    if (on) {
        if (state->looks++ == 0) {
            state->look_mark = charge_uncounted_begin(state);
#ifndef CT_LEAN
            vars_peek_flag(true);
#endif
        }
    } else if (state->looks && --state->looks == 0) {
        charge_uncounted_end(state, state->look_mark);
#ifndef CT_LEAN
        vars_peek_flag(false);
#endif
    }
}

/* A tag the rates don't name leaves the work at the rate it was at, as the
 * machine leaves an attribution to no hand port with the port's own. The
 * scope's end times the work as the routine before it: at the rate the
 * scope's start found, which is that routine's row's, or none. The lean
 * build keeps no totals, so a scope keeps only that rate, split; the others
 * keep the row, whose totals the work goes to. */
#ifdef CT_LEAN
int charge_time_as_begin(GameState *g, uint32_t routine)
{
    uint32_t was = g->head.account.rate;
    uint32_t rate = row_rate(row_of(routine));
    if (rate && rate != was) {   /* an unchanged rate prices the same later */
        price_accesses(g);
        g->head.account.rate = rate;
    }
    return (int)was;
}

void charge_time_as_end(GameState *g, int was)
{
    uint32_t rate = (uint32_t)was;
    if (rate != g->head.account.rate) {
        price_accesses(g);
        g->head.account.rate = rate;
    }
}
#else
/* Whether the accesses counted so far must be priced before the work is
 * timed at `rate`: where the rate stays the same, pricing them now or with
 * the rest of the stretch adds the same thousandths, so only a run that
 * keeps each routine's totals (REPORTING) prices at every scope. */
static bool reprices(const GameState *g, uint32_t rate) { return rate != g->head.account.rate || REPORTING(g); }

int charge_time_as_begin(GameState *g, uint32_t routine)
{
    int was = (int)g->timed;
    const RecentRow *row = row_of(routine);
    unsigned index = row_index(row);
    uint32_t rate = row_rate(row);
    if (rate) {
        if (reprices(g, rate)) {
            price_accesses(g);
            g->head.account.rate = rate;
        }
        g->timed = index;
    }
    if (REPORTING(g))
        g->time[index].tags++;
    return was;
}

void charge_time_as_end(GameState *g, int was)
{
    unsigned index = (unsigned)was;
    uint32_t rate = index < time_rate_count ? split_rate(time_rates[index].cost_milli) : 0;
    if (reprices(g, rate)) {
        price_accesses(g);
        g->head.account.rate = rate;
    }
    g->timed = index;
}
#endif
#endif   /* CT_UNTIMED */

/* ---- Where the program's C ends (core/program.h) --------------------------------------------- */

bool program_went_no_further(const GameState *g) { return g->stopped; }

void program_goes_on_to_a_scene(GameState *g, uint8_t scene)
{
    g->scene_next = true;
    g->scene = scene;
}

bool program_next_scene(GameState *g, uint8_t *scene)
{
    if (!g->scene_next)
        return false;
    g->scene_next = false;
    *scene = g->scene;
    return true;
}

void program_starts_in_a_scene(GameState *g, uint8_t scene, uint8_t song)
{
    if (g->first_scenes == STATE_HOST_FIRST_SCENES)
        return;
    g->first_scene[g->first_scenes] = scene;
    g->first_song[g->first_scenes] = song;
    g->first_scenes++;
}

bool program_first_scene(GameState *g, uint8_t *scene, uint8_t *song)
{
    if (g->first_scene_taken == g->first_scenes)
        return false;
    *scene = g->first_scene[g->first_scene_taken];
    *song = g->first_song[g->first_scene_taken];
    g->first_scene_taken++;
    return true;
}

void program_starts_the_field_again(GameState *g) { g->field_again = true; }

bool program_field_starts_again(GameState *g)
{
    bool again = g->field_again;
    g->field_again = false;
    return again;
}

void program_resets_the_console(GameState *g) { g->reset = true; }

bool program_console_is_resetting(const GameState *g) { return g->reset; }

bool program_console_resets(GameState *g)
{
    bool reset = g->reset;
    g->reset = false;
    return reset;
}

void program_starts_afresh_from_the_title(GameState *g) { g->afresh = true; }

bool program_starts_afresh(GameState *g)
{
    bool afresh = g->afresh;
    g->afresh = false;
    return afresh;
}

void program_fail(GameState *g, uint32_t routine, const char *what)
{
    if (g->stopped)
        return;
    g->stopped = true;
    g->stopped_at = routine;
    g->stopped_what = what;
}

/* All game-state contract failures preserve the first diagnostic. */
void program_breaks_a_contract(GameState *g, uint32_t routine, const char *what)
{
    program_fail(g, routine, what);
}

size_t state_host_bytes(void) { return sizeof(GameState) + STATE_HOST_SRAM_BYTES; }
size_t state_host_vars_bytes(void) { return sizeof(VarsHost); }

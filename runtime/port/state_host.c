#include "port/state_host.h"

#include <stdlib.h>
#include <string.h>

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "original_frame.h"
#include "platform/time_model.h"
#include "port/time_rates.h"
#include "rom.h"
#include "wram.h"

enum { WRAM_BYTES = 0x20000, POWER_ON_BYTE = 0x55 };

struct GameState {
    uint8_t wram[WRAM_BYTES];
    uint8_t sram[STATE_HOST_SRAM_BYTES];
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
    uint32_t rate;            /* the cost_milli the game's accesses are timed at now (charge_time_as_begin); 0, none */
    unsigned uncounted;       /* nested stretches whose accesses aren't the game's own time */
    uint64_t work_milli;      /* the game's own accesses' time since it was last taken (master clocks x 1000) */
};

GameState *state_host_create(const uint8_t *sram)
{
    GameState *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    /* work RAM as the reference emulator powers it on, which the game reads
     * where its own clear at power-on leaves it as it was (the menus' records
     * of a party not yet joined): a run starts where the reference plays do */
    memset(g->wram, POWER_ON_BYTE, sizeof g->wram);
    if (sram)
        memcpy(g->sram, sram, sizeof g->sram);
    return g;
}

void state_host_destroy(GameState *g) { free(g); }

uint8_t *state_host_sram(GameState *g) { return g->sram; }

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
    uint32_t offset;
    switch (place_of(addr & 0xFFFFFF, &offset)) {
    case IN_WRAM: return g->wram[offset];
    case IN_SRAM: return g->sram[offset];
    case IN_ROM: {
        /* the host's read of the cartridge, not the game's: no time */
        GameState *m = (GameState *)g;
        m->uncounted++;
        uint8_t v = rom_read8(g, addr);
        m->uncounted--;
        return v;
    }
    case NOWHERE: break;
    }
    stray(g, addr & 0xFFFFFF);
    return 0;
}

void state_host_write8(GameState *g, uint32_t addr, uint8_t v)
{
    uint32_t offset;
    switch (place_of(addr & 0xFFFFFF, &offset)) {
    case IN_WRAM: g->wram[offset] = v; return;
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

enum { PORT_MASK = 0x1FFFF };

void hw_port_seek(GameState *g, uint16_t at, uint8_t bank) { g->port = ((uint32_t)(bank & 1) << 16 | at) & PORT_MASK; }
void hw_port_seek_address(GameState *g, uint16_t at) { g->port = (g->port & 0x10000) | at; }
void hw_port_seek_bank(GameState *g, uint8_t bank) { g->port = (uint32_t)(bank & 1) << 16 | (g->port & 0xFFFF); }

uint8_t hw_port_read(GameState *g)
{
    uint8_t v = g->wram[g->port];
    g->port = (g->port + 1) & PORT_MASK;
    return v;
}

void hw_port_write(GameState *g, uint8_t v)
{
    g->wram[g->port] = v;
    g->port = (g->port + 1) & PORT_MASK;
}

/* The byte the original leaves below the port: no register, nothing to keep. */
void hw_port_leave_spill(GameState *g, uint8_t low) { (void)g, (void)low; }

/* ---- Work RAM the game locates at run time (wram.h) ------------------------------------------ */

static uint32_t in_bank(uint32_t at, uint32_t offset) { return (at & 0xFF0000u) | (uint16_t)(at + offset); }

void wram_clear(GameState *g, PfOrigin o, uint32_t at, uint32_t bytes)
{
    (void)o;
    wram_fill8(g, at, bytes, 0);
}

void wram_fill8(GameState *g, uint32_t at, uint32_t bytes, uint8_t v)
{
    for (uint32_t i = 0; i < bytes; i++)
        state_host_write8(g, in_bank(at, i), v);
}

void wram_fill16(GameState *g, uint32_t at, uint32_t words, uint16_t v)
{
    for (uint32_t i = 0; i < words; i++) {
        state_host_write8(g, in_bank(at, 2 * i), (uint8_t)v);
        state_host_write8(g, in_bank(at, 2 * i + 1), (uint8_t)(v >> 8));
    }
}

void wram_copy(GameState *g, uint32_t from, uint32_t to, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i++)
        state_host_write8(g, in_bank(to, i), state_host_read8(g, in_bank(from, i)));
}

/* ---- Time: what the frame-level time model charges ------------------------------------------- *
 *
 * A build with no machine has no bus to count, so the game's own work is
 * timed here, as the machine's estimate times a port's (docs/REIMPLEMENTATION.md,
 * "The frame-level time model"): each access the game makes -- its
 * accessors, its reads of the cartridge, save RAM through the save service
 * (TmHostProgram.accessed) -- is the bus clocks it takes on the machine
 * (state_host_game_access), at the calibrated rate of the routine the work is
 * timed as (charge_time_as_begin, the TIMED_AS and NATIVE_TIME_AS tags), the
 * rates the machine measured (port/time_rates.h). What a port does another
 * way is charged as the machine charges it: a charge at its routine's rate
 * (charge_as), a block move at the block move's, and the unpacker at the time
 * model's price (platform/time_model.h), its own accesses left out as they
 * are on the machine (charge_uncounted_begin, charge_retired_begin). Work no
 * tag names, and a guard's look (vars_peek), take none. The timing backend
 * takes the account as the main thread waits (state_host_take_owed) and makes
 * lag frames of the whole frames it fills, and drops the handlers' at the
 * main thread's start of a frame, keeping it as the frame's own. */

static uint32_t rate_of(uint32_t routine)
{
    routine &= 0xFFFFFF;
    unsigned lo = 0, hi = time_rate_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (time_rates[mid].routine < routine)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < time_rate_count && time_rates[lo].routine == routine ? time_rates[lo].cost_milli : 0;
}

static void owe(GameState *g, uint32_t clocks)
{
    g->owed = clocks > UINT32_MAX - g->owed ? UINT32_MAX : g->owed + clocks;
}

void state_host_game_access(const GameState *g, uint32_t clocks)
{
    GameState *m = (GameState *)g;   /* the account is the backing's own, not the game's state */
    if (!m->uncounted)
        m->work_milli += (uint64_t)clocks * m->rate;
}

/* The block move stub's routine ($7E:9690), whose rate a byte moved is
 * charged at (a read and a write), as on the machine (port/state_io.c). */
enum { BLOCK_MOVE = 0x7E9690, ACCESS_CLOCKS = 8, ACCESSES_PER_BYTE_MOVED = 2 };

void charge_as(GameState *g, uint32_t routine, uint32_t accesses)
{
    if (routine == TIME_UNPACKER) {
        owe(g, time_of_charge(routine, accesses));
        return;
    }
    uint32_t rate = rate_of(routine);
    g->work_milli += (uint64_t)accesses * ACCESS_CLOCKS * (rate ? rate : g->rate);
}

void charge_block_move(GameState *g, uint32_t bytes) { charge_as(g, BLOCK_MOVE, bytes * ACCESSES_PER_BYTE_MOVED); }

void charge_legacy_io_accesses(GameState *g, uint32_t accesses)
{
    (void)g;
    (void)accesses;
}

void charge_legacy_rom_accesses(GameState *g, uint32_t accesses)
{
    (void)g;
    (void)accesses;
}

void charge_legacy_accesses(GameState *g, uint32_t accesses)
{
    (void)g, (void)accesses;
}

uint32_t state_host_take_owed(GameState *g)
{
    uint64_t work = g->work_milli / 1000;
    g->work_milli %= 1000;
    owe(g, work > UINT32_MAX ? UINT32_MAX : (uint32_t)work);
    uint32_t owed = g->owed;
    g->owed = 0;
    return owed;
}

uint32_t charge_retired_begin(GameState *g)
{
    g->uncounted++;
    return 0;
}

void charge_settle(GameState *g) { (void)g; }

void charge_retired_end(GameState *g, uint32_t mark, uint32_t routine, uint32_t accesses)
{
    (void)mark;
    g->uncounted--;
    charge_as(g, routine, accesses);
}

uint32_t charge_uncounted_begin(GameState *g)
{
    g->uncounted++;
    return 0;
}

void charge_uncounted_end(GameState *g, uint32_t mark)
{
    (void)mark;
    g->uncounted--;
}

/* A tag the rates don't name leaves the work at the rate it was at, as the
 * machine leaves an attribution to no hand port with the port's own. */
int charge_time_as_begin(GameState *g, uint32_t routine)
{
    int was = (int)g->rate;
    uint32_t rate = rate_of(routine);
    if (rate)
        g->rate = rate;
    return was;
}

void charge_time_as_end(GameState *g, int was) { g->rate = (uint32_t)was; }
void original_frame_enter(GameState *g, uint16_t bytes) { (void)g, (void)bytes; }
void original_frame_leave(GameState *g, uint16_t bytes) { (void)g, (void)bytes; }

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

void program_needs_the_machine(GameState *g, uint32_t routine, const char *what)
{
    if (g->stopped)
        return;
    g->stopped = true;
    g->stopped_at = routine;
    g->stopped_what = what;
}

/* With no machine there is no original to go on with: the program stops. */
void program_breaks_a_contract(GameState *g, uint32_t routine, const char *what)
{
    program_needs_the_machine(g, routine, what);
}

#include "port/state_host.h"

#include <stdlib.h>
#include <string.h>

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "original_frame.h"
#include "rom.h"
#include "wram.h"

enum { WRAM_BYTES = 0x20000 };

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
    uint32_t stopped_at;
    const char *stopped_what;
};

GameState *state_host_create(const uint8_t *sram)
{
    GameState *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
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
    case IN_ROM: return rom_read8(g, addr);
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

/* ---- Time and the original's frames: nothing to charge them to -------------------------------- */

void charge_as(GameState *g, uint32_t routine, uint32_t accesses) { (void)g, (void)routine, (void)accesses; }
void charge_block_move(GameState *g, uint32_t bytes) { (void)g, (void)bytes; }
uint32_t charge_uncounted_begin(GameState *g) { (void)g; return 0; }
void charge_uncounted_end(GameState *g, uint32_t mark) { (void)g, (void)mark; }
int charge_time_as_begin(GameState *g, uint32_t routine) { (void)g, (void)routine; return 0; }
void charge_time_as_end(GameState *g, int was) { (void)g, (void)was; }
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

void program_needs_the_machine(GameState *g, uint32_t routine, const char *what)
{
    if (g->stopped)
        return;
    g->stopped = true;
    g->stopped_at = routine;
    g->stopped_what = what;
}

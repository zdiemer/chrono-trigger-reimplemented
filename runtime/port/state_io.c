/* The backings for game/cpu_io.h and game/rom.h while the game's state lives in
 * emulated work RAM: the multiplier and divider and the work RAM port, written
 * and read through the bus in bank 0 as the original's code reaches them, so
 * shadow mode and the fuzzer see the same register state; the cartridge,
 * read on the bus; and the time charged and the original's frames, kept by the
 * runtime. */
#include "charge.h"
#include "core/program.h"
#include "data/assets.h"
#include "cpu_io.h"
#include "original_frame.h"
#include "rom.h"
#include "wram.h"

#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "port/rom_reads.h"

enum { WRMPYA = 0x004202, WRMPYB = 0x004203, WRDIVL = 0x004204, WRDIVB = 0x004206, RDDIV = 0x004214, RDMPY = 0x004216 };
enum { WMDATA = 0x002180, WMADDL = 0x002181, WMADDB = 0x002183, WMDATA_LESS_ONE = 0x00217F };

void hw_set_multiplicand(GameState *g, uint8_t a) { m_write8(state_snes(g), WRMPYA, a); }

uint16_t hw_multiply_by(GameState *g, uint8_t b)
{
    m_write8(state_snes(g), WRMPYB, b);
    return m_read16(state_snes(g), RDMPY);
}

void hw_set_dividend(GameState *g, uint16_t dividend) { m_write16(state_snes(g), WRDIVL, dividend); }

uint16_t hw_quotient(const GameState *g) { return m_read16(state_snes(g), RDDIV); }

uint16_t hw_remainder(const GameState *g) { return m_read16(state_snes(g), RDMPY); }

void hw_divide_by(GameState *g, uint8_t divisor) { m_write8(state_snes(g), WRDIVB, divisor); }

void hw_divide_by_word(GameState *g, uint16_t word) { m_write16(state_snes(g), WRDIVB, word); }

uint16_t hw_dividend(const GameState *g) { return state_snes(g)->bus.wrdiv; }

uint8_t hw_multiplicand(const GameState *g) { return state_snes(g)->bus.wrmpya; }

void hw_port_seek(GameState *g, uint16_t at, uint8_t bank)
{
    m_write16(state_snes(g), WMADDL, at);
    m_write8(state_snes(g), WMADDB, bank);
}

void hw_port_seek_address(GameState *g, uint16_t at) { m_write16(state_snes(g), WMADDL, at); }

void hw_port_seek_bank(GameState *g, uint8_t bank) { m_write8(state_snes(g), WMADDB, bank); }

uint8_t hw_port_read(GameState *g) { return m_read8(state_snes(g), WMDATA); }

void hw_port_write(GameState *g, uint8_t v) { m_write8(state_snes(g), WMDATA, v); }

void hw_port_leave_spill(GameState *g, uint8_t low) { m_write8(state_snes(g), WMDATA_LESS_ONE, low); }

/* ---- The cartridge (rom.h) -------------------------------------------------------------------- */

static uint8_t *reads_seen;   /* rom_reads_record: a bit a cartridge byte read */

void rom_reads_record(uint8_t *map) { reads_seen = map; }

static AssetFault differs;   /* rom_differing_reads */

const AssetFault *rom_differing_reads(void) { return &differs; }

/* The machine reads the byte as the original's access would, with its time,
 * and the store's copy, where there is one, is what the read gives. */
uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served)
{
    if (reads_seen) {
        uint32_t a = at & 0xFFFFFFu;
        reads_seen[a >> 3] |= (uint8_t)(1u << (a & 7));
    }
    Snes *s = state_snes(g);
    uint8_t v = vars_peeking() ? snes_bus_peek(&s->bus, at & 0xFFFFFF) : m_read8(s, at);
    if (!served)
        return v;
    if (*served != v && differs.count++ == 0)
        differs.first = at & 0xFFFFFFu;
    return *served;
}

/* ---- Time charged (charge.h) and plain moves (wram.h) ------------------------------------------ */

/* The block move stub the menus and the save code call ($7E:9690,
 * core/memory.c): the game's block move as a registered routine, whose own
 * calls calibrate its time per byte. */
enum { BLOCK_MOVE = 0x7E9690 };
/* An access to work RAM, in master clocks; a byte moved is a read and a write. */
enum { ACCESS_CLOCKS = 8, ACCESSES_PER_BYTE_MOVED = 2 };

void charge_as(GameState *g, uint32_t routine, uint32_t accesses)
{
    native_charge_as(state_snes(g), routine, accesses * ACCESS_CLOCKS);
}

void charge_block_move(GameState *g, uint32_t bytes) { charge_as(g, BLOCK_MOVE, bytes * ACCESSES_PER_BYTE_MOVED); }

uint32_t charge_uncounted_begin(GameState *g) { return native_uncounted_begin(state_snes(g)); }

void charge_uncounted_end(GameState *g, uint32_t mark) { native_uncounted_end(state_snes(g), mark); }

int charge_time_as_begin(GameState *g, uint32_t routine) { return native_time_as(state_snes(g), routine); }

void charge_time_as_end(GameState *g, int was) { native_time_end(state_snes(g), was); }

void original_frame_enter(GameState *g, uint16_t bytes) { native_frame_enter(state_snes(g), bytes); }

void original_frame_leave(GameState *g, uint16_t bytes) { native_frame_leave(state_snes(g), bytes); }

static uint32_t in_bank(uint32_t at, uint32_t offset) { return (at & 0xFF0000u) | (uint16_t)(at + offset); }

void wram_fill8(GameState *g, uint32_t at, uint32_t bytes, uint8_t v)
{
    Snes *s = state_snes(g);
    uint32_t mark = native_uncounted_begin(s);
    for (uint32_t i = 0; i < bytes; i++)
        m_write8(s, in_bank(at, i), v);
    native_uncounted_end(s, mark);
}

void wram_fill16(GameState *g, uint32_t at, uint32_t words, uint16_t v)
{
    Snes *s = state_snes(g);
    uint32_t mark = native_uncounted_begin(s);
    for (uint32_t i = 0; i < words; i++) {
        m_write8(s, in_bank(at, 2 * i), (uint8_t)v);
        m_write8(s, in_bank(at, 2 * i + 1), (uint8_t)(v >> 8));
    }
    native_uncounted_end(s, mark);
}

void wram_copy(GameState *g, uint32_t from, uint32_t to, uint32_t bytes)
{
    Snes *s = state_snes(g);
    uint32_t mark = native_uncounted_begin(s);
    for (uint32_t i = 0; i < bytes; i++)
        m_write8(s, in_bank(to, i), m_read8(s, in_bank(from, i)));
    native_uncounted_end(s, mark);
}

/* ---- Where the program's C ends (core/program.h) --------------------------------------------- */

/* On the machine the program goes on from there by address: the CPU reaches
 * the routine's entry, so nothing stops. */
void program_needs_the_machine(GameState *g, uint32_t routine, const char *what)
{
    (void)g, (void)routine, (void)what;
}

/* The backings for game/cpu_io.h and game/rom.h while the game's state lives in
 * emulated work RAM: the multiplier and divider and the work RAM port, written
 * and read through the bus in bank 0 as the original's code reaches them, so
 * shadow mode and the fuzzer see the same register state; and the cartridge,
 * read on the bus. */
#include "charge.h"
#include "cpu_io.h"
#include "rom.h"
#include "wram.h"

#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"

enum { WRMPYA = 0x004202, WRMPYB = 0x004203, WRDIVL = 0x004204, WRDIVB = 0x004206, RDDIV = 0x004214, RDMPY = 0x004216 };
enum { WMDATA = 0x002180, WMADDL = 0x002181, WMADDB = 0x002183 };

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

uint8_t rom_read8(const GameState *g, uint32_t at)
{
    return vars_peeking() ? snes_bus_peek(&state_snes(g)->bus, at & 0xFFFFFF) : m_read8(state_snes(g), at);
}

uint16_t rom_read16(const GameState *g, uint32_t at)
{
    if (!vars_peeking())
        return m_read16(state_snes(g), at);
    return (uint16_t)(rom_read8(g, at) | rom_read8(g, at + 1) << 8);
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

int charge_begin_as(GameState *g, uint32_t routine) { return native_time_as(state_snes(g), routine); }

void charge_end(GameState *g, int was) { native_time_end(state_snes(g), was); }

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

/* The backings for game/cpu_io.h and game/rom.h while the game's state lives in
 * emulated work RAM: the multiplier and divider and the work RAM port, written
 * and read through the bus in bank 0 as the original's code reaches them, so
 * shadow mode and the fuzzer see the same register state; and the cartridge,
 * read on the bus. */
#include "cpu_io.h"
#include "rom.h"

#include "port/machine.h"
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

void hw_divide_by(GameState *g, uint8_t divisor) { m_write8(state_snes(g), WRDIVB, divisor); }

void hw_port_seek(GameState *g, uint16_t at, uint8_t bank)
{
    m_write16(state_snes(g), WMADDL, at);
    m_write8(state_snes(g), WMADDB, bank);
}

void hw_port_seek_address(GameState *g, uint16_t at) { m_write16(state_snes(g), WMADDL, at); }

uint8_t hw_port_read(GameState *g) { return m_read8(state_snes(g), WMDATA); }

void hw_port_write(GameState *g, uint8_t v) { m_write8(state_snes(g), WMDATA, v); }

uint8_t rom_read8(const GameState *g, uint32_t at) { return m_read8(state_snes(g), at); }
uint16_t rom_read16(const GameState *g, uint32_t at) { return m_read16(state_snes(g), at); }

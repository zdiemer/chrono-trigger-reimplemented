/* The backings for the parts of game/cpu_io.h that are the
 * machine's own registers rather than work RAM, while the game's state lives in
 * emulated work RAM: the multiplier and divider, and the work RAM port. Each is
 * written and read through the bus in bank 0, as the original's code reaches it,
 * so shadow mode and the fuzzer see the same register state. */
#include "cpu_io.h"

#include "port/machine.h"
#include "port/vars_ram.h"

enum { WRMPYA = 0x004202, WRMPYB = 0x004203, WRDIVB = 0x004206, RDDIV = 0x004214, RDMPY = 0x004216 };
enum { WMDATA = 0x002180, WMADDL = 0x002181, WMADDB = 0x002183 };

void hw_set_multiplicand(GameState *g, uint8_t a) { m_write8(state_snes(g), WRMPYA, a); }

uint16_t hw_multiply_by(GameState *g, uint8_t b)
{
    m_write8(state_snes(g), WRMPYB, b);
    return m_read16(state_snes(g), RDMPY);
}

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

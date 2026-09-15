/* Binds generated code (port/recomp.h) to the development system's bus: the
 * same bus functions the interpreter calls, so I/O side effects, bus time and
 * open bus match it. The Cpu's bus context is the SnesBus (snes_init). */
#ifndef CT_PORT_RECOMP_SNES_H
#define CT_PORT_RECOMP_SNES_H

#include "snes/bus.h"

/* Instruction bytes the interpreter would have fetched: their bus time, and the
 * last byte left on the bus. */
static inline void rc_snes_fetch(SnesBus *bus, uint32_t addr, unsigned n, uint8_t last)
{
    for (unsigned i = 0; i < n; i++)
        bus->clocks += snes_bus_access_clocks(bus, (addr & 0xFF0000) | (uint16_t)(addr + i));
    bus->open_bus = last;
}

#define RC_READ(c, addr) snes_bus_read((c)->bus.ctx, (addr))
#define RC_WRITE(c, addr, v) snes_bus_write((c)->bus.ctx, (addr), (v))
#define RC_FETCH(c, addr, n, last) rc_snes_fetch((SnesBus *)(c)->bus.ctx, (addr), (n), (last))

#include "port/recomp.h"

#endif

/* Binds generated code (port/recomp.h) to the development system's bus: the
 * same bus functions the interpreter calls, so I/O side effects, bus time and
 * open bus match it. Accesses go through the Cpu's bus hooks (snes_init points
 * them at the SnesBus), which is also how shadow mode replays a block against
 * the accesses the interpreter recorded (runtime/port/native.c). */
#ifndef CT_PORT_RECOMP_SNES_H
#define CT_PORT_RECOMP_SNES_H

#include "cpu/cpu65816.h"
#include "snes/bus.h"

/* Shadow mode's replay of instruction fetches (native.c). */
void native_replay_fetch(Cpu *c, uint32_t addr, unsigned n, uint8_t last);

/* Instruction bytes the interpreter would have fetched: their bus time, and the
 * last byte left on the bus. */
static inline void rc_snes_fetch(Cpu *c, uint32_t addr, unsigned n, uint8_t last)
{
    if (c->bus.read != snes_bus_read) {
        native_replay_fetch(c, addr, n, last);
        return;
    }
    SnesBus *bus = c->bus.ctx;
    for (unsigned i = 0; i < n; i++)
        bus->clocks += snes_bus_access_clocks(bus, (addr & 0xFF0000) | (uint16_t)(addr + i));
    bus->open_bus = last;
}

#define RC_READ(c, addr) ((c)->bus.read((c)->bus.ctx, (addr)))
#define RC_WRITE(c, addr, v) ((c)->bus.write((c)->bus.ctx, (addr), (v)))
#define RC_FETCH(c, addr, n, last) rc_snes_fetch((c), (addr), (n), (last))

#include "port/recomp.h"

#endif

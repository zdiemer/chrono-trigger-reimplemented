/* Binds generated code (port/recomp.h) to the development system's bus: the
 * same bus functions the interpreter calls, so I/O side effects, bus time and
 * open bus match it. Accesses go through the Cpu's bus hooks (snes_init points
 * them at the SnesBus), which is also how shadow mode replays a block against
 * the accesses the interpreter recorded (runtime/port/native.c). */
#ifndef CT_PORT_RECOMP_SNES_H
#define CT_PORT_RECOMP_SNES_H

#include "cpu/cpu65816.h"
#include "snes/bus.h"

struct Snes;

/* Exact timing for a block running in ON mode (docs/RECOMP.md, "Exact timing").
 * Times are the block's clocks so far: bus clocks plus 6 per internal cycle. */
typedef struct RcTiming {
    struct Snes *s;         /* the machine an ON mode block runs on; NULL otherwise */
    uint32_t limit;         /* a block stops before an instruction starting later than this */
    uint32_t insn_start;    /* when the current instruction started */
    uint32_t synced;        /* how much of the block's time has passed on the machine */
    void (*stopped)(struct Snes *s);   /* the block, if it stopped before an instruction */
} RcTiming;
extern RcTiming rc_timing;

/* Before an I/O access in ON mode: let the machine catch up with the start of
 * the current instruction (native.c). */
void rc_io_sync(uint32_t addr, bool write);

/* The CPU I/O area ($2000-$5FFF in banks $00-$3F and $80-$BF). */
static inline bool rc_io_area(uint32_t addr)
{
    return !(addr & 0x400000) && (uint16_t)((uint16_t)addr - 0x2000) < 0x4000;
}

/* Shadow mode's replay of instruction fetches (native.c). */
void native_replay_fetch(Cpu *c, uint32_t addr, unsigned n, uint8_t last);

/* Instruction bytes the interpreter would have fetched: their bus time, and the
 * last byte left on the bus. */
static inline void rc_snes_fetch(Cpu *c, uint32_t addr, unsigned n, uint8_t last)
{
    c->last_cycle_io = false;
    if (c->bus.read != snes_bus_read) {
        native_replay_fetch(c, addr, n, last);
        return;
    }
    SnesBus *bus = c->bus.ctx;
    for (unsigned i = 0; i < n; i++) {
        bus->access_start = bus->clocks;
        bus->clocks += snes_bus_access_clocks(bus, (addr & 0xFF0000) | (uint16_t)(addr + i));
    }
    bus->open_bus = last;
}

static inline uint8_t rc_snes_read(Cpu *c, uint32_t addr)
{
    if (rc_timing.s && rc_io_area(addr))
        rc_io_sync(addr, false);
    c->last_cycle_io = false;
    return c->bus.read(c->bus.ctx, addr);
}

static inline void rc_snes_write(Cpu *c, uint32_t addr, uint8_t v)
{
    if (rc_timing.s && rc_io_area(addr))
        rc_io_sync(addr, true);
    c->last_cycle_io = false;
    c->bus.write(c->bus.ctx, addr, v);
}

#define RC_READ(c, addr) rc_snes_read((c), (addr))
#define RC_WRITE(c, addr, v) rc_snes_write((c), (addr), (v))
#define RC_FETCH(c, addr, n, last) rc_snes_fetch((c), (addr), (n), (last))
#define RC_IO(c, n) ((c)->io_cycles += (n), (c)->last_cycle_io = true)

#include "port/recomp.h"

#endif

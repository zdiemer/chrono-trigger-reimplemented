/* Binding for the SingleStepTests harness of generated instructions
 * (tools/recomp/sst.py): a flat 24-bit memory that counts bus cycles, like
 * sst_runner.c's interpreter bus. With SST_RECOMP_SNES the same generated code
 * binds to the development system's bus instead, to check it compiles there. */
#ifndef CT_TESTS_SST_RECOMP_H
#define CT_TESTS_SST_RECOMP_H

#include <stdint.h>

#ifdef SST_RECOMP_SNES

#include "port/recomp_snes.h"
#define SST_PEEK(c, addr) snes_bus_peek((c)->bus.ctx, (addr))

#else

#include "cpu/cpu65816.h"

extern uint8_t *sst_mem;
extern unsigned sst_bus_cycles;

static inline uint8_t sst_read(uint32_t addr)
{
    sst_bus_cycles++;
    return sst_mem[addr];
}

static inline void sst_write(uint32_t addr, uint8_t v)
{
    sst_bus_cycles++;
    sst_mem[addr] = v;
}

#define RC_READ(c, addr) ((void)(c), sst_read(addr))
#define RC_WRITE(c, addr, v) ((void)(c), sst_write((addr), (v)))
#define RC_FETCH(c, addr, n, last) ((void)(c), (void)(addr), (void)(last), sst_bus_cycles += (n))
#define SST_PEEK(c, addr) ((void)(c), sst_mem[(addr)])

#include "port/recomp.h"

#endif

typedef void (*SstRecompFn)(Cpu *c);

/* By opcode, then context: m8x8, m8x16, m16x8, m16x16, emulation. */
extern const SstRecompFn sst_recomp_table[256][5];

#endif

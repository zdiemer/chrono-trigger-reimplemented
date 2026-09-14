/* WDC 65C816 interpreter.
 *
 * Development scaffold: runs routines that haven't been ported to native code
 * yet, and records which addresses execute with which widths. Release builds
 * don't include it. Timing is instruction-granular; bus access order within an
 * instruction follows the real chip closely enough for I/O side effects.
 */
#ifndef CT_CPU65816_H
#define CT_CPU65816_H

#include <stdbool.h>
#include <stdint.h>

enum {
    CPU_FLAG_C = 0x01,
    CPU_FLAG_Z = 0x02,
    CPU_FLAG_I = 0x04,
    CPU_FLAG_D = 0x08,
    CPU_FLAG_X = 0x10,   /* B in emulation mode */
    CPU_FLAG_M = 0x20,
    CPU_FLAG_V = 0x40,
    CPU_FLAG_N = 0x80,
};

typedef struct CpuBus {
    void *ctx;
    uint8_t (*read)(void *ctx, uint32_t addr);
    void (*write)(void *ctx, uint32_t addr, uint8_t value);
} CpuBus;

typedef struct Cpu {
    uint16_t a, x, y, s, d, pc;
    uint8_t db, pb, p;
    bool e;

    bool nmi_pending;   /* edge: set by the system, cleared when taken */
    bool irq_line;      /* level: held by the system while asserted */
    bool waiting;       /* WAI */
    bool stopped;       /* STP */

    CpuBus bus;

    /* Internal (non-bus) cycles executed; the caller clears it. Each costs 6
     * master clocks. Together with bus accesses this gives exact per-instruction
     * cycle counts (verified against SingleStepTests). */
    uint32_t io_cycles;

    /* Optional: called before each instruction with its 24-bit address. */
    void (*on_insn)(void *ctx, uint32_t pc, uint8_t p, bool e);
    void *on_insn_ctx;
} Cpu;

void cpu_init(Cpu *cpu, CpuBus bus);
void cpu_reset(Cpu *cpu);

/* Service a pending interrupt or execute one instruction. */
void cpu_step(Cpu *cpu);

static inline uint32_t cpu_pc24(const Cpu *cpu) { return (uint32_t)cpu->pb << 16 | cpu->pc; }

#endif

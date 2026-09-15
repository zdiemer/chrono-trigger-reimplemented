/* WDC 65C816: CPU state, and the interpreter.
 *
 * The interpreter (cpu_step, cpu65816.c) is a development scaffold: it runs
 * code that hasn't been ported or generated, and it is the reference generated
 * code is checked against. Timing is instruction-granular; bus access order
 * within an instruction follows the real chip closely enough for I/O side
 * effects. What the CPU does outside instructions (power-on state, reset,
 * interrupt entry, WAI and STP) is also in cpu_entry.c (cpu_init, cpu_reset,
 * cpu_service), which doesn't need the interpreter: a player built without it
 * (docs/PLAYING.md) links only that file.
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
    /* Whether the last cycle executed was internal rather than a bus access;
     * with the bus's own accounting it gives the last cycle's length, where
     * the system samples the interrupt lines. */
    bool last_cycle_io;

    /* Optional: called before each instruction with its 24-bit address. */
    void (*on_insn)(void *ctx, uint32_t pc, uint8_t p, bool e);
    void *on_insn_ctx;
} Cpu;

void cpu_init(Cpu *cpu, CpuBus bus);
void cpu_reset(Cpu *cpu);

typedef enum CpuStep {
    CPU_STEP_INSTRUCTION,   /* executed one instruction */
    CPU_STEP_INTERRUPT,     /* ran an interrupt entry sequence (no interrupt poll) */
    CPU_STEP_IDLE,          /* waiting (WAI) or stopped (STP): no bus activity */
    CPU_STEP_READY,         /* cpu_service only: an instruction is due; nothing was executed */
} CpuStep;

/* Service a pending interrupt or execute one instruction (the interpreter). */
CpuStep cpu_step(Cpu *cpu);

/* What cpu_step does before it executes an instruction, without the
 * interpreter (cpu_entry.c): STP, the NMI and IRQ entry sequences, and WAI.
 * Returns CPU_STEP_INTERRUPT or CPU_STEP_IDLE where cpu_step would, and
 * CPU_STEP_READY where cpu_step would go on to execute the instruction at PC
 * (an IRQ held off by I ends WAI first, as in cpu_step). Bus accesses, internal
 * cycles and state changes are cpu_step's; tests/cpu/recomp_bus_diff.c
 * compares the two. */
CpuStep cpu_service(Cpu *cpu);

static inline uint32_t cpu_pc24(const Cpu *cpu) { return (uint32_t)cpu->pb << 16 | cpu->pc; }

#endif

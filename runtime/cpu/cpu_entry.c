/* What the 65C816 does outside instructions: its state at power-on and reset,
 * the hardware interrupt entry sequences, and WAI and STP. The interpreter
 * (cpu65816.c) has its own copy of the entry sequence inside cpu_step; this one
 * is for the system loop without the interpreter, where generated blocks run
 * every instruction (docs/RECOMP.md, "Without the interpreter"), and it must do
 * exactly what cpu_step does: tests/cpu/generated_bus_diff.c compares the two from
 * random states. */
#include "cpu65816.h"

static uint8_t rd(Cpu *c, uint32_t addr)
{
    c->last_cycle_io = false;
    return c->bus.read(c->bus.ctx, addr & 0xFFFFFF);
}

static void wr(Cpu *c, uint32_t addr, uint8_t v)
{
    c->last_cycle_io = false;
    c->bus.write(c->bus.ctx, addr & 0xFFFFFF, v);
}

static uint16_t read_word_bank0(Cpu *c, uint16_t addr)
{
    uint16_t lo = rd(c, addr);
    return (uint16_t)(lo | rd(c, (uint16_t)(addr + 1)) << 8);
}

/* A push as the interrupt sequence makes it: in emulation mode S stays on page 1. */
static void push8(Cpu *c, uint8_t v)
{
    wr(c, c->s, v);
    c->s--;
    if (c->e)
        c->s = (uint16_t)(0x0100 | (c->s & 0xFF));
}

void cpu_init(Cpu *cpu, CpuBus bus)
{
    *cpu = (Cpu){ 0 };
    cpu->bus = bus;
}

void cpu_reset(Cpu *c)
{
    c->e = true;
    c->p = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_I;
    c->db = 0;
    c->pb = 0;
    c->d = 0;
    c->s = 0x0100 | (c->s & 0xFF);
    c->x &= 0xFF;
    c->y &= 0xFF;
    c->nmi_pending = false;
    c->waiting = false;
    c->stopped = false;
    c->pc = read_word_bank0(c, 0xFFFC);
}

/* NMI or IRQ: two internal cycles, the return state pushed (PB only in native
 * mode; B clear in the pushed P in emulation mode), I set, D clear, and the
 * bank-0 vector entered. */
static void hardware_interrupt(Cpu *c, uint16_t native_vector, uint16_t emulation_vector)
{
    c->io_cycles += 2;
    c->last_cycle_io = true;
    if (!c->e) {
        push8(c, c->pb);
        push8(c, (uint8_t)(c->pc >> 8));
        push8(c, (uint8_t)c->pc);
        push8(c, c->p);
    } else {
        push8(c, (uint8_t)(c->pc >> 8));
        push8(c, (uint8_t)c->pc);
        push8(c, (uint8_t)(c->p & ~CPU_FLAG_X));
    }
    c->p = (uint8_t)((c->p | CPU_FLAG_I) & ~CPU_FLAG_D);
    c->pb = 0;
    c->pc = read_word_bank0(c, c->e ? emulation_vector : native_vector);
}

CpuStep cpu_service(Cpu *c)
{
    if (c->stopped)
        return CPU_STEP_IDLE;
    if (c->nmi_pending) {
        c->nmi_pending = false;
        c->waiting = false;
        hardware_interrupt(c, 0xFFEA, 0xFFFA);
        return CPU_STEP_INTERRUPT;
    }
    if (c->irq_line) {
        c->waiting = false;
        if (!(c->p & CPU_FLAG_I)) {
            hardware_interrupt(c, 0xFFEE, 0xFFFE);
            return CPU_STEP_INTERRUPT;
        }
    }
    return c->waiting ? CPU_STEP_IDLE : CPU_STEP_READY;
}

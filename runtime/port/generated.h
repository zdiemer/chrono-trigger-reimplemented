/* Instruction semantics for C that tools/generate generates from 65816 code.
 *
 * Each helper does what runtime/cpu/cpu65816.c does for the same piece of an
 * instruction: the same bus reads and writes in the same order, the same flag
 * effects (decimal mode included) and the same internal cycles. Register widths
 * and emulation mode are fixed when the code is generated, so there are separate
 * 8-bit, 16-bit and emulation-mode helpers where the interpreter tests them at
 * run time. See docs/RECOMP.md.
 *
 * The including file binds the bus before including this header:
 *   RC_READ(c, addr), RC_WRITE(c, addr, v)
 *       one bus access at a 24-bit address
 *   RC_FETCH(c, addr, n, last)
 *       the interpreter fetching n instruction bytes from addr (wrapping within
 *       the bank), the last of which is `last`: bus time and open bus
 *   RC_IO(c, n)
 *       n internal cycles (default: counted in c->io_cycles, as the interpreter does)
 * runtime/port/generated_snes.h binds them to the development system's bus.
 */
#ifndef CT_PORT_GENERATED_H
#define CT_PORT_GENERATED_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/cpu65816.h"

#if !defined(RC_READ) || !defined(RC_WRITE) || !defined(RC_FETCH)
#error "define RC_READ, RC_WRITE and RC_FETCH before including port/generated.h"
#endif
#ifndef RC_IO
#define RC_IO(c, n) ((c)->io_cycles += (n))
#endif

/* ---- bus ------------------------------------------------------------------- */

static inline uint8_t rc_rd8(Cpu *c, uint32_t addr) { return RC_READ(c, addr & 0xFFFFFF); }
static inline void rc_wr8(Cpu *c, uint32_t addr, uint8_t v) { RC_WRITE(c, addr & 0xFFFFFF, v); }

/* Words. Plain forms carry into the next bank; _b0 forms (direct page and
 * stack-relative data, pointers read from bank 0) wrap within bank 0. */
static inline uint16_t rc_rd16(Cpu *c, uint32_t addr)
{
    uint16_t lo = rc_rd8(c, addr);
    return (uint16_t)(lo | rc_rd8(c, addr + 1) << 8);
}

static inline uint16_t rc_rd16_b0(Cpu *c, uint16_t addr)
{
    uint16_t lo = rc_rd8(c, addr);
    return (uint16_t)(lo | rc_rd8(c, (uint16_t)(addr + 1)) << 8);
}

static inline uint32_t rc_rd24_b0(Cpu *c, uint16_t addr)
{
    uint32_t lo = rc_rd8(c, addr);
    lo |= (uint32_t)rc_rd8(c, (uint16_t)(addr + 1)) << 8;
    return lo | (uint32_t)rc_rd8(c, (uint16_t)(addr + 2)) << 16;
}

/* A jump table entry: a word in the program bank, wrapping within it. */
static inline uint16_t rc_rd16_pb(Cpu *c, uint16_t addr)
{
    uint32_t bank = (uint32_t)c->pb << 16;
    uint16_t lo = rc_rd8(c, bank | addr);
    return (uint16_t)(lo | rc_rd8(c, bank | (uint16_t)(addr + 1)) << 8);
}

static inline void rc_wr16(Cpu *c, uint32_t addr, uint16_t v)
{
    rc_wr8(c, addr, v & 0xFF);
    rc_wr8(c, addr + 1, v >> 8);
}

static inline void rc_wr16_b0(Cpu *c, uint16_t addr, uint16_t v)
{
    rc_wr8(c, addr, v & 0xFF);
    rc_wr8(c, (uint16_t)(addr + 1), v >> 8);
}

/* Read-modify-write instructions write the high byte first. */
static inline void rc_wr16_rev(Cpu *c, uint32_t addr, uint16_t v)
{
    rc_wr8(c, addr + 1, v >> 8);
    rc_wr8(c, addr, v & 0xFF);
}

static inline void rc_wr16_rev_b0(Cpu *c, uint16_t addr, uint16_t v)
{
    rc_wr8(c, (uint16_t)(addr + 1), v >> 8);
    rc_wr8(c, addr, v & 0xFF);
}

/* ---- flags ----------------------------------------------------------------- */

static inline void rc_flag(Cpu *c, uint8_t f, bool on) { c->p = on ? (c->p | f) : (c->p & (uint8_t)~f); }

static inline void rc_nz8(Cpu *c, uint8_t v)
{
    rc_flag(c, CPU_FLAG_Z, v == 0);
    rc_flag(c, CPU_FLAG_N, v & 0x80);
}

static inline void rc_nz16(Cpu *c, uint16_t v)
{
    rc_flag(c, CPU_FLAG_Z, v == 0);
    rc_flag(c, CPU_FLAG_N, v & 0x8000);
}

/* After P or E changes: emulation mode forces 8-bit widths, and 8-bit index
 * registers lose their high bytes. */
static inline void rc_normalize_p(Cpu *c)
{
    if (c->e)
        c->p |= CPU_FLAG_M | CPU_FLAG_X;
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
}

/* ---- stack ------------------------------------------------------------------- */

/* Native mode. */
static inline void rc_push8(Cpu *c, uint8_t v)
{
    rc_wr8(c, c->s, v);
    c->s--;
}

static inline uint8_t rc_pull8(Cpu *c)
{
    c->s++;
    return rc_rd8(c, c->s);
}

static inline void rc_push16(Cpu *c, uint16_t v)
{
    rc_push8(c, v >> 8);
    rc_push8(c, v & 0xFF);
}

static inline uint16_t rc_pull16(Cpu *c)
{
    uint16_t lo = rc_pull8(c);
    return (uint16_t)(lo | rc_pull8(c) << 8);
}

/* Emulation mode, 6502-era instructions: S stays on page 1 for every byte.
 * The 65816 additions use the native forms and then rc_fix_stack_emu. */
static inline void rc_push8_emu(Cpu *c, uint8_t v)
{
    rc_wr8(c, c->s, v);
    c->s = (uint16_t)(0x0100 | ((c->s - 1) & 0xFF));
}

static inline uint8_t rc_pull8_emu(Cpu *c)
{
    c->s = (uint16_t)(0x0100 | ((c->s + 1) & 0xFF));
    return rc_rd8(c, c->s);
}

static inline void rc_push16_emu(Cpu *c, uint16_t v)
{
    rc_push8_emu(c, v >> 8);
    rc_push8_emu(c, v & 0xFF);
}

static inline uint16_t rc_pull16_emu(Cpu *c)
{
    uint16_t lo = rc_pull8_emu(c);
    return (uint16_t)(lo | rc_pull8_emu(c) << 8);
}

static inline void rc_fix_stack_emu(Cpu *c) { c->s = (uint16_t)(0x0100 | (c->s & 0xFF)); }

/* ---- effective addresses ----------------------------------------------------- */

/* Direct page: an extra internal cycle when D's low byte is nonzero. */
static inline uint16_t rc_dp(Cpu *c, uint8_t op)
{
    if (c->d & 0xFF)
        RC_IO(c, 1);
    return (uint16_t)(c->d + op);
}

/* dp,X and dp,Y. In emulation mode with D's low byte zero, operand + index
 * wraps within the page. */
static inline uint16_t rc_dp_index(Cpu *c, uint8_t op, uint16_t index)
{
    RC_IO(c, 1);
    if (c->d & 0xFF)
        RC_IO(c, 1);
    return (uint16_t)(c->d + op + index);
}

static inline uint16_t rc_dp_index_emu(Cpu *c, uint8_t op, uint16_t index)
{
    RC_IO(c, 1);
    if ((c->d & 0xFF) == 0)
        return (uint16_t)((c->d & 0xFF00) | (uint8_t)(op + index));
    RC_IO(c, 1);
    return (uint16_t)(c->d + op + index);
}

static inline uint32_t rc_db(const Cpu *c) { return (uint32_t)c->db << 16; }

/* Indexing an absolute-style base: an extra cycle for writes and 16-bit index
 * registers (always_io), or when the index crosses a page. */
static inline uint32_t rc_indexed(Cpu *c, uint32_t base, uint16_t index, bool always_io)
{
    uint32_t addr = (base + index) & 0xFFFFFF;
    if (always_io || (base >> 8) != (addr >> 8))
        RC_IO(c, 1);
    return addr;
}

static inline uint32_t rc_abs(const Cpu *c, uint16_t op) { return rc_db(c) | op; }
static inline uint32_t rc_absx(Cpu *c, uint16_t op, bool always_io) { return rc_indexed(c, rc_db(c) | op, c->x, always_io); }
static inline uint32_t rc_absy(Cpu *c, uint16_t op, bool always_io) { return rc_indexed(c, rc_db(c) | op, c->y, always_io); }
static inline uint32_t rc_longx(const Cpu *c, uint32_t op) { return (op + c->x) & 0xFFFFFF; }

static inline uint32_t rc_dpind(Cpu *c, uint8_t op)
{
    uint16_t p = rc_dp(c, op);
    return rc_db(c) | rc_rd16_b0(c, p);
}

static inline uint32_t rc_dpxind(Cpu *c, uint8_t op)
{
    uint16_t p = rc_dp_index(c, op, c->x);
    return rc_db(c) | rc_rd16_b0(c, p);
}

static inline uint32_t rc_dpxind_emu(Cpu *c, uint8_t op)
{
    uint16_t p = rc_dp_index_emu(c, op, c->x);
    return rc_db(c) | rc_rd16_b0(c, p);
}

static inline uint32_t rc_dpindy(Cpu *c, uint8_t op, bool always_io)
{
    uint16_t p = rc_dp(c, op);
    return rc_indexed(c, rc_db(c) | rc_rd16_b0(c, p), c->y, always_io);
}

static inline uint32_t rc_dplind(Cpu *c, uint8_t op)
{
    uint16_t p = rc_dp(c, op);
    return rc_rd24_b0(c, p);
}

static inline uint32_t rc_dplindy(Cpu *c, uint8_t op)
{
    uint16_t p = rc_dp(c, op);
    return (rc_rd24_b0(c, p) + c->y) & 0xFFFFFF;
}

static inline uint16_t rc_sr(Cpu *c, uint8_t op)
{
    RC_IO(c, 1);
    return (uint16_t)(c->s + op);
}

static inline uint32_t rc_srindy(Cpu *c, uint8_t op)
{
    RC_IO(c, 1);
    uint16_t p = (uint16_t)(c->s + op);
    RC_IO(c, 1);
    return ((rc_db(c) | rc_rd16_b0(c, p)) + c->y) & 0xFFFFFF;
}

/* ---- accumulator operations ------------------------------------------------------ */

static inline void rc_set_a8(Cpu *c, uint8_t v) { c->a = (uint16_t)((c->a & 0xFF00) | v); }

static inline void rc_lda8(Cpu *c, uint8_t v)
{
    rc_set_a8(c, v);
    rc_nz8(c, v);
}

static inline void rc_lda16(Cpu *c, uint16_t v)
{
    c->a = v;
    rc_nz16(c, v);
}

static inline void rc_ora8(Cpu *c, uint8_t v) { rc_lda8(c, (uint8_t)(c->a | v)); }
static inline void rc_ora16(Cpu *c, uint16_t v) { rc_lda16(c, c->a | v); }
static inline void rc_and8(Cpu *c, uint8_t v) { rc_lda8(c, (uint8_t)(c->a & v)); }
static inline void rc_and16(Cpu *c, uint16_t v) { rc_lda16(c, c->a & v); }
static inline void rc_eor8(Cpu *c, uint8_t v) { rc_lda8(c, (uint8_t)(c->a ^ v)); }
static inline void rc_eor16(Cpu *c, uint16_t v) { rc_lda16(c, c->a ^ v); }

static inline void rc_adc8(Cpu *c, uint8_t data)
{
    int carry = c->p & CPU_FLAG_C;
    uint8_t a = c->a & 0xFF;
    int r;
    if (!(c->p & CPU_FLAG_D)) {
        r = a + data + carry;
    } else {
        r = (a & 0x0F) + (data & 0x0F) + carry;
        if (r > 0x09) r += 0x06;
        carry = r > 0x0F;
        r = (a & 0xF0) + (data & 0xF0) + (carry << 4) + (r & 0x0F);
    }
    rc_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x80);
    if ((c->p & CPU_FLAG_D) && r > 0x9F) r += 0x60;
    rc_flag(c, CPU_FLAG_C, r > 0xFF);
    rc_lda8(c, (uint8_t)r);
}

static inline void rc_adc16(Cpu *c, uint16_t data)
{
    int carry = c->p & CPU_FLAG_C;
    uint16_t a = c->a;
    int r;
    if (!(c->p & CPU_FLAG_D)) {
        r = a + data + carry;
    } else {
        r = (a & 0x000F) + (data & 0x000F) + carry;
        if (r > 0x0009) r += 0x0006;
        carry = r > 0x000F;
        r = (a & 0x00F0) + (data & 0x00F0) + (carry << 4) + (r & 0x000F);
        if (r > 0x009F) r += 0x0060;
        carry = r > 0x00FF;
        r = (a & 0x0F00) + (data & 0x0F00) + (carry << 8) + (r & 0x00FF);
        if (r > 0x09FF) r += 0x0600;
        carry = r > 0x0FFF;
        r = (a & 0xF000) + (data & 0xF000) + (carry << 12) + (r & 0x0FFF);
    }
    rc_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x8000);
    if ((c->p & CPU_FLAG_D) && r > 0x9FFF) r += 0x6000;
    rc_flag(c, CPU_FLAG_C, r > 0xFFFF);
    rc_lda16(c, (uint16_t)r);
}

static inline void rc_sbc8(Cpu *c, uint8_t value)
{
    int carry = c->p & CPU_FLAG_C;
    uint8_t a = c->a & 0xFF;
    uint8_t data = (uint8_t)~value;
    int r;
    if (!(c->p & CPU_FLAG_D)) {
        r = a + data + carry;
    } else {
        r = (a & 0x0F) + (data & 0x0F) + carry;
        if (r <= 0x0F) r -= 0x06;
        carry = r > 0x0F;
        r = (a & 0xF0) + (data & 0xF0) + (carry << 4) + (r & 0x0F);
    }
    rc_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x80);
    if ((c->p & CPU_FLAG_D) && r <= 0xFF) r -= 0x60;
    rc_flag(c, CPU_FLAG_C, r > 0xFF);
    rc_lda8(c, (uint8_t)r);
}

static inline void rc_sbc16(Cpu *c, uint16_t value)
{
    int carry = c->p & CPU_FLAG_C;
    uint16_t a = c->a;
    uint16_t data = (uint16_t)~value;
    int r;
    if (!(c->p & CPU_FLAG_D)) {
        r = a + data + carry;
    } else {
        r = (a & 0x000F) + (data & 0x000F) + carry;
        if (r <= 0x000F) r -= 0x0006;
        carry = r > 0x000F;
        r = (a & 0x00F0) + (data & 0x00F0) + (carry << 4) + (r & 0x000F);
        if (r <= 0x00FF) r -= 0x0060;
        carry = r > 0x00FF;
        r = (a & 0x0F00) + (data & 0x0F00) + (carry << 8) + (r & 0x00FF);
        if (r <= 0x0FFF) r -= 0x0600;
        carry = r > 0x0FFF;
        r = (a & 0xF000) + (data & 0xF000) + (carry << 12) + (r & 0x0FFF);
    }
    rc_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x8000);
    if ((c->p & CPU_FLAG_D) && r <= 0xFFFF) r -= 0x6000;
    rc_flag(c, CPU_FLAG_C, r > 0xFFFF);
    rc_lda16(c, (uint16_t)r);
}

/* CMP/CPX/CPY. */
static inline void rc_cmp8(Cpu *c, uint8_t reg, uint8_t v)
{
    rc_flag(c, CPU_FLAG_C, reg >= v);
    rc_nz8(c, (uint8_t)(reg - v));
}

static inline void rc_cmp16(Cpu *c, uint16_t reg, uint16_t v)
{
    rc_flag(c, CPU_FLAG_C, reg >= v);
    rc_nz16(c, (uint16_t)(reg - v));
}

/* BIT on memory sets N and V from the operand; BIT #imm changes only Z. */
static inline void rc_bit8(Cpu *c, uint8_t v)
{
    rc_flag(c, CPU_FLAG_N, v & 0x80);
    rc_flag(c, CPU_FLAG_V, v & 0x40);
    rc_flag(c, CPU_FLAG_Z, (c->a & v & 0xFF) == 0);
}

static inline void rc_bit16(Cpu *c, uint16_t v)
{
    rc_flag(c, CPU_FLAG_N, v & 0x8000);
    rc_flag(c, CPU_FLAG_V, v & 0x4000);
    rc_flag(c, CPU_FLAG_Z, (c->a & v) == 0);
}

static inline void rc_bit_imm8(Cpu *c, uint8_t v) { rc_flag(c, CPU_FLAG_Z, (c->a & v & 0xFF) == 0); }
static inline void rc_bit_imm16(Cpu *c, uint16_t v) { rc_flag(c, CPU_FLAG_Z, (c->a & v) == 0); }

/* ---- shifts, increments, test-and-set: return the new value ------------------------- */

static inline uint8_t rc_asl8(Cpu *c, uint8_t v)
{
    rc_flag(c, CPU_FLAG_C, v & 0x80);
    v = (uint8_t)(v << 1);
    rc_nz8(c, v);
    return v;
}

static inline uint16_t rc_asl16(Cpu *c, uint16_t v)
{
    rc_flag(c, CPU_FLAG_C, v & 0x8000);
    v = (uint16_t)(v << 1);
    rc_nz16(c, v);
    return v;
}

static inline uint8_t rc_lsr8(Cpu *c, uint8_t v)
{
    rc_flag(c, CPU_FLAG_C, v & 1);
    v >>= 1;
    rc_nz8(c, v);
    return v;
}

static inline uint16_t rc_lsr16(Cpu *c, uint16_t v)
{
    rc_flag(c, CPU_FLAG_C, v & 1);
    v >>= 1;
    rc_nz16(c, v);
    return v;
}

static inline uint8_t rc_rol8(Cpu *c, uint8_t v)
{
    unsigned carry = c->p & CPU_FLAG_C;
    rc_flag(c, CPU_FLAG_C, v & 0x80);
    v = (uint8_t)(v << 1 | carry);
    rc_nz8(c, v);
    return v;
}

static inline uint16_t rc_rol16(Cpu *c, uint16_t v)
{
    unsigned carry = c->p & CPU_FLAG_C;
    rc_flag(c, CPU_FLAG_C, v & 0x8000);
    v = (uint16_t)(v << 1 | carry);
    rc_nz16(c, v);
    return v;
}

static inline uint8_t rc_ror8(Cpu *c, uint8_t v)
{
    bool carry = c->p & CPU_FLAG_C;
    rc_flag(c, CPU_FLAG_C, v & 1);
    v = (uint8_t)(v >> 1 | (carry ? 0x80 : 0));
    rc_nz8(c, v);
    return v;
}

static inline uint16_t rc_ror16(Cpu *c, uint16_t v)
{
    bool carry = c->p & CPU_FLAG_C;
    rc_flag(c, CPU_FLAG_C, v & 1);
    v = (uint16_t)(v >> 1 | (carry ? 0x8000 : 0));
    rc_nz16(c, v);
    return v;
}

static inline uint8_t rc_inc8(Cpu *c, uint8_t v)
{
    rc_nz8(c, ++v);
    return v;
}

static inline uint16_t rc_inc16(Cpu *c, uint16_t v)
{
    rc_nz16(c, ++v);
    return v;
}

static inline uint8_t rc_dec8(Cpu *c, uint8_t v)
{
    rc_nz8(c, --v);
    return v;
}

static inline uint16_t rc_dec16(Cpu *c, uint16_t v)
{
    rc_nz16(c, --v);
    return v;
}

static inline uint8_t rc_tsb8(Cpu *c, uint8_t v)
{
    rc_flag(c, CPU_FLAG_Z, (c->a & v & 0xFF) == 0);
    return (uint8_t)(v | c->a);
}

static inline uint16_t rc_tsb16(Cpu *c, uint16_t v)
{
    rc_flag(c, CPU_FLAG_Z, (c->a & v) == 0);
    return v | c->a;
}

static inline uint8_t rc_trb8(Cpu *c, uint8_t v)
{
    rc_flag(c, CPU_FLAG_Z, (c->a & v & 0xFF) == 0);
    return (uint8_t)(v & ~c->a);
}

static inline uint16_t rc_trb16(Cpu *c, uint16_t v)
{
    rc_flag(c, CPU_FLAG_Z, (c->a & v) == 0);
    return (uint16_t)(v & ~c->a);
}

/* ---- control ----------------------------------------------------------------------- */

/* BRK and COP: push the return state, set I, clear D, enter the bank-0 vector.
 * `ret` is the address after the operand byte. */
static inline void rc_software_interrupt(Cpu *c, uint16_t ret, uint16_t vector)
{
    rc_push8(c, c->pb);
    rc_push16(c, ret);
    rc_push8(c, c->p);
    c->p = (uint8_t)((c->p | CPU_FLAG_I) & ~CPU_FLAG_D);
    c->pb = 0;
    c->pc = rc_rd16_b0(c, vector);
}

static inline void rc_software_interrupt_emu(Cpu *c, uint16_t ret, uint16_t vector)
{
    rc_push16_emu(c, ret);
    rc_push8_emu(c, c->p);
    c->p = (uint8_t)((c->p | CPU_FLAG_I) & ~CPU_FLAG_D);
    c->pb = 0;
    c->pc = rc_rd16_b0(c, vector);
}

/* XCE: exchange carry and emulation, entering emulation with S on page 1. */
static inline void rc_xce(Cpu *c)
{
    bool carry = c->p & CPU_FLAG_C;
    rc_flag(c, CPU_FLAG_C, c->e);
    c->e = carry;
    if (c->e)
        rc_fix_stack_emu(c);
    rc_normalize_p(c);
}

#endif

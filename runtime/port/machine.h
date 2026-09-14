/* Helpers for native routines that stand in for original game code while the
 * development system runs.
 *
 * A native routine is entered where the original would be, right after the
 * JSR/JSL, and must leave the machine exactly as the original's return would:
 * registers, flags, memory (including stack bytes it wrote) and I/O writes.
 * Shadow validation (runtime/port/native.c) checks that on every call.
 */
#ifndef CT_PORT_MACHINE_H
#define CT_PORT_MACHINE_H

#include "port/native.h"
#include "snes/system.h"

static inline uint8_t m_read8(Snes *s, uint32_t addr) { return snes_bus_read(&s->bus, addr & 0xFFFFFF); }
static inline void m_write8(Snes *s, uint32_t addr, uint8_t v) { snes_bus_write(&s->bus, addr & 0xFFFFFF, v); }

static inline uint16_t m_read16(Snes *s, uint32_t addr)
{
    return (uint16_t)(m_read8(s, addr) | m_read8(s, addr + 1) << 8);
}

static inline void m_write16(Snes *s, uint32_t addr, uint16_t v)
{
    m_write8(s, addr, v & 0xFF);
    m_write8(s, addr + 1, v >> 8);
}

/* Address in the data bank / direct page, as the original's abs / dp modes see it. */
static inline uint32_t m_abs(const Snes *s, uint16_t addr) { return (uint32_t)s->cpu.db << 16 | addr; }
static inline uint32_t m_dp(const Snes *s, uint8_t addr) { return (uint16_t)(s->cpu.d + addr); }

/* Stack, native mode. */
static inline void m_push8(Snes *s, uint8_t v)
{
    m_write8(s, s->cpu.s, v);
    s->cpu.s--;
}

static inline uint8_t m_pull8(Snes *s)
{
    s->cpu.s++;
    return m_read8(s, s->cpu.s);
}

static inline void m_push16(Snes *s, uint16_t v)
{
    m_push8(s, v >> 8);
    m_push8(s, v & 0xFF);
}

static inline uint16_t m_pull16(Snes *s)
{
    uint16_t lo = m_pull8(s);
    return (uint16_t)(lo | m_pull8(s) << 8);
}

/* Returns, as RTS / RTL would. */
static inline void m_rts(Snes *s) { s->cpu.pc = (uint16_t)(m_pull16(s) + 1); }
static inline void m_rtl(Snes *s)
{
    s->cpu.pc = (uint16_t)(m_pull16(s) + 1);
    s->cpu.pb = m_pull8(s);
}

/* Flags. */
static inline bool m_flag(const Snes *s, uint8_t f) { return s->cpu.p & f; }
static inline void m_set_flag(Snes *s, uint8_t f, bool on) { s->cpu.p = on ? (s->cpu.p | f) : (s->cpu.p & ~f); }

static inline void m_set_nz8(Snes *s, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_Z, v == 0);
    m_set_flag(s, CPU_FLAG_N, v & 0x80);
}

static inline void m_set_nz16(Snes *s, uint16_t v)
{
    m_set_flag(s, CPU_FLAG_Z, v == 0);
    m_set_flag(s, CPU_FLAG_N, v & 0x8000);
}

/* 8-bit accumulator writes leave the high byte (B) alone. */
static inline void m_set_a8(Snes *s, uint8_t v) { s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | v); }

/* 8-bit binary arithmetic with the CPU's flag effects. Decimal mode isn't
 * supported: register such routines with a precondition of D clear. */
static inline uint8_t m_adc8(Snes *s, uint8_t a, uint8_t b)
{
    unsigned r = (unsigned)a + b + (m_flag(s, CPU_FLAG_C) ? 1u : 0u);
    m_set_flag(s, CPU_FLAG_V, ~(a ^ b) & (a ^ r) & 0x80);
    m_set_flag(s, CPU_FLAG_C, r > 0xFF);
    m_set_nz8(s, (uint8_t)r);
    return (uint8_t)r;
}

static inline uint8_t m_sbc8(Snes *s, uint8_t a, uint8_t b) { return m_adc8(s, a, (uint8_t)~b); }

static inline void m_cmp8(Snes *s, uint8_t reg, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_C, reg >= v);
    m_set_nz8(s, (uint8_t)(reg - v));
}

static inline uint8_t m_asl8(Snes *s, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_C, v & 0x80);
    m_set_nz8(s, (uint8_t)(v << 1));
    return (uint8_t)(v << 1);
}

/* REP/SEP, including their effect on register widths. */
static inline void m_rep(Snes *s, uint8_t bits) { s->cpu.p &= (uint8_t)~bits; }
static inline void m_sep(Snes *s, uint8_t bits)
{
    s->cpu.p |= bits;
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* Loads that set N and Z the way LDA/LDX/LDY do at the given width. */
static inline uint8_t m_lda8(Snes *s, uint8_t v)
{
    m_set_a8(s, v);
    m_set_nz8(s, v);
    return v;
}

static inline uint16_t m_lda16(Snes *s, uint16_t v)
{
    s->cpu.a = v;
    m_set_nz16(s, v);
    return v;
}

static inline uint16_t m_ldx16(Snes *s, uint16_t v)
{
    s->cpu.x = v;
    m_set_nz16(s, v);
    return v;
}

/* BIT #imm (8-bit): only Z changes. */
static inline void m_bit_imm8(Snes *s, uint8_t imm) { m_set_flag(s, CPU_FLAG_Z, ((uint8_t)s->cpu.a & imm) == 0); }

/* TRB on an 8-bit memory byte with the 8-bit accumulator. */
static inline void m_trb8(Snes *s, uint32_t addr)
{
    uint8_t v = m_read8(s, addr);
    uint8_t a = (uint8_t)s->cpu.a;
    m_set_flag(s, CPU_FLAG_Z, (v & a) == 0);
    m_write8(s, addr, (uint8_t)(v & ~a));
}

/* Returns from an interrupt, native mode: P, PC, program bank. */
static inline void m_rti(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
    s->cpu.pc = m_pull16(s);
    s->cpu.pb = m_pull8(s);
}

/* True if the `len` bytes at `addr` hash (64-bit FNV-1a) to `hash`. For guards
 * on routines the game copies or decompresses into RAM: the hash identifies the
 * code a port was written for without keeping any of its bytes. No side effects. */
static inline bool m_code_matches(Snes *s, uint32_t addr, uint16_t len, uint64_t hash)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint16_t i = 0; i < len; i++) {
        h ^= snes_bus_peek(&s->bus, (addr + i) & 0xFFFFFF);
        h *= 0x100000001b3ull;
    }
    return h == hash;
}

/* Call another routine from a native one, as the JSR (3-byte) or JSL (4-byte)
 * instruction at `site` would. */
static inline void m_jsr(Snes *s, uint32_t site, uint32_t target) { native_call(s, site, target, false); }
static inline void m_jsl(Snes *s, uint32_t site, uint32_t target) { native_call(s, site, target, true); }

#endif

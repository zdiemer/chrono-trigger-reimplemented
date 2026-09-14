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

#endif

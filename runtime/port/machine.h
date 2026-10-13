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

#include "platform/platform.h"
#include "port/native.h"
#include "snes/system.h"

/* The platform services' handle (runtime/platform/): on the development system
 * the SNES backend, which is the machine itself, so a native routine's calls act
 * on whichever machine it runs on (the clone, in shadow mode). */
static inline Platform *m_platform(Snes *s) { return (Platform *)s; }

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

/* True if absolute addressing with this data bank reaches the I/O registers
 * (banks $00-$3F and $80-$BF). Ports whose original addresses registers through
 * the data bank check it before calling a platform service, which addresses
 * them directly. */
static inline bool m_io_bank(uint8_t bank) { return (bank & 0x40) == 0; }

/* What a port that names its cells (game/vars.h) assumes of the machine it is
 * entered on: the original reaches the same cells through D and the data bank,
 * so the port is the same routine only where those are what the game always
 * enters it with, and its guard says so. The data bank reaches the I/O
 * registers, or low RAM ($0000-$1FFF: the banks that mirror it, and $7E); and
 * the direct page, for a guard to compare. */
static inline bool m_bank_reaches_io(const Snes *s) { return m_io_bank(s->cpu.db); }
static inline bool m_bank_reaches_low_ram(const Snes *s) { return s->cpu.db == 0x7E || m_io_bank(s->cpu.db); }
static inline uint16_t m_direct_page(const Snes *s) { return s->cpu.d; }
/* The data bank is $00 or its FastROM mirror $80: absolute addresses reach
 * low RAM and the I/O registers below $8000, and the ROM's first bank ($C0)
 * above. */
static inline bool m_bank_is_zero(const Snes *s) { return (s->cpu.db & 0x7F) == 0; }

/* Direct pages the original sometimes puts on the I/O block so that it can
 * reach registers with direct-page addressing. A port reproduces the value in
 * D, which is CPU state, while its own accesses go through the platform
 * services (docs/PLATFORM.md). */
enum { M_DP_PPU = 0x2100, M_DP_CPU = 0x4200, M_DP_DMA = 0x4300 };

/* Registers an original reaches with absolute addressing, so that with a data
 * bank the registers aren't mapped in (m_io_bank) the store lands in memory
 * instead and is no register write at all. A port that has to make that write
 * names the address from here, which keeps game/ free of register constants
 * (tests/test_platform_lint.py). */
enum { M_REG_LINE_EFFECTS = 0x420C };

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

/* Returns, as RTS / RTL would (which is where the entry's don't-care set
 * applies: native_note_return). */
static inline void m_rts(Snes *s)
{
    s->cpu.pc = (uint16_t)(m_pull16(s) + 1);
    native_note_return(s);
}
static inline void m_rtl(Snes *s)
{
    s->cpu.pc = (uint16_t)(m_pull16(s) + 1);
    s->cpu.pb = m_pull8(s);
    native_note_return(s);
}

/* A call made from a port came back where it was made from: the machine is at
 * the return address, with the stack as it was before the call. A bridge asks
 * this after a call-out, to go on as C or leave the rest to the original. */
static inline bool m_came_back(const Snes *s, uint32_t returns_to, uint16_t stack)
{
    return s->cpu.s == stack && s->cpu.pb == (uint8_t)(returns_to >> 16) && s->cpu.pc == (uint16_t)returns_to;
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

static inline uint16_t m_adc16(Snes *s, uint16_t a, uint16_t b)
{
    unsigned r = (unsigned)a + b + (m_flag(s, CPU_FLAG_C) ? 1u : 0u);
    m_set_flag(s, CPU_FLAG_V, ~(a ^ b) & (a ^ r) & 0x8000);
    m_set_flag(s, CPU_FLAG_C, r > 0xFFFF);
    m_set_nz16(s, (uint16_t)r);
    return (uint16_t)r;
}

static inline uint16_t m_sbc16(Snes *s, uint16_t a, uint16_t b) { return m_adc16(s, a, (uint16_t)~b); }

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

static inline void m_cmp16(Snes *s, uint16_t reg, uint16_t v)
{
    m_set_flag(s, CPU_FLAG_C, reg >= v);
    m_set_nz16(s, (uint16_t)(reg - v));
}

static inline uint16_t m_asl16(Snes *s, uint16_t v)
{
    m_set_flag(s, CPU_FLAG_C, v & 0x8000);
    m_set_nz16(s, (uint16_t)(v << 1));
    return (uint16_t)(v << 1);
}

static inline uint8_t m_lsr8(Snes *s, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_C, v & 0x01);
    m_set_nz8(s, (uint8_t)(v >> 1));
    return (uint8_t)(v >> 1);
}

static inline uint16_t m_lsr16(Snes *s, uint16_t v)
{
    m_set_flag(s, CPU_FLAG_C, v & 0x0001);
    m_set_nz16(s, (uint16_t)(v >> 1));
    return (uint16_t)(v >> 1);
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

/* Index register loads, transfers and increments at the current X width, and
 * transfers into A at the current M width, with LDX/TAX/INX-style N and Z. */
static inline bool m_x8(const Snes *s) { return s->cpu.p & CPU_FLAG_X; }
static inline bool m_m8(const Snes *s) { return s->cpu.p & CPU_FLAG_M; }

static inline uint16_t m_set_index(Snes *s, uint16_t *reg, uint16_t v)
{
    if (m_x8(s)) {
        *reg = v & 0xFF;
        m_set_nz8(s, (uint8_t)v);
    } else {
        *reg = v;
        m_set_nz16(s, v);
    }
    return *reg;
}

static inline uint16_t m_ldx(Snes *s, uint16_t v) { return m_set_index(s, &s->cpu.x, v); }
static inline uint16_t m_ldy(Snes *s, uint16_t v) { return m_set_index(s, &s->cpu.y, v); }
static inline uint16_t m_tax(Snes *s) { return m_set_index(s, &s->cpu.x, s->cpu.a); }
static inline uint16_t m_tyx(Snes *s) { return m_set_index(s, &s->cpu.x, s->cpu.y); }
static inline uint16_t m_txy(Snes *s) { return m_set_index(s, &s->cpu.y, s->cpu.x); }
static inline uint16_t m_inx(Snes *s) { return m_set_index(s, &s->cpu.x, (uint16_t)(s->cpu.x + 1)); }
static inline uint16_t m_iny(Snes *s) { return m_set_index(s, &s->cpu.y, (uint16_t)(s->cpu.y + 1)); }

static inline uint16_t m_txa(Snes *s)
{
    if (m_m8(s)) {
        m_lda8(s, (uint8_t)s->cpu.x);
        return s->cpu.a;
    }
    return m_lda16(s, s->cpu.x);
}

/* TDC (16-bit transfer) and XBA (N and Z from the new low byte). */
static inline void m_tdc(Snes *s) { m_lda16(s, s->cpu.d); }
static inline void m_xba(Snes *s)
{
    s->cpu.a = (uint16_t)(s->cpu.a >> 8 | s->cpu.a << 8);
    m_set_nz8(s, (uint8_t)s->cpu.a);
}

/* For a JSR the port inlines rather than calls: write the return address the
 * JSR would push (the address of its last byte) into the stack bytes below S,
 * leaving S unchanged, so memory matches the original after the callee returns. */
static inline void m_jsr_stack_bytes(Snes *s, uint16_t last_byte)
{
    m_write8(s, s->cpu.s, last_byte >> 8);
    m_write8(s, (uint16_t)(s->cpu.s - 1), last_byte & 0xFF);
}

/* The target of JMP (abs,X) or JSR (abs,X): a word in the program bank at
 * table + index, both bytes wrapping within the bank. */
static inline uint16_t m_jump_table(Snes *s, uint8_t bank, uint16_t table, uint16_t index)
{
    uint16_t p = (uint16_t)(table + index);
    return (uint16_t)(m_read8(s, (uint32_t)bank << 16 | p) | m_read8(s, (uint32_t)bank << 16 | (uint16_t)(p + 1)) << 8);
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

/* TSB on an 8-bit memory byte with the 8-bit accumulator. */
static inline void m_tsb8(Snes *s, uint32_t addr)
{
    uint8_t v = m_read8(s, addr);
    uint8_t a = (uint8_t)s->cpu.a;
    m_set_flag(s, CPU_FLAG_Z, (v & a) == 0);
    m_write8(s, addr, (uint8_t)(v | a));
}

/* MVN: copy A+1 bytes from src_bank:X to dst_bank:Y, X and Y counting up,
 * leaving A = $FFFF and the data bank = dst_bank. Native mode, 16-bit X/Y as
 * the game uses it. */
static inline void m_mvn(Snes *s, uint8_t dst_bank, uint8_t src_bank)
{
    s->cpu.db = dst_bank;
    do {
        m_write8(s, (uint32_t)dst_bank << 16 | s->cpu.y, m_read8(s, (uint32_t)src_bank << 16 | s->cpu.x));
        s->cpu.x++;
        s->cpu.y++;
        s->cpu.a--;
    } while (s->cpu.a != 0xFFFF);
}

/* What an MVN leaves in the registers, for a port that moves the bytes itself
 * (entered with A+1 = `bytes`): A $FFFF, X and Y past the bytes, and the data
 * bank the one moved to. */
static inline void m_leave_mvn(Snes *s, uint32_t bytes, uint8_t dst_bank)
{
    s->cpu.x = (uint16_t)(s->cpu.x + bytes);
    s->cpu.y = (uint16_t)(s->cpu.y + bytes);
    s->cpu.a = 0xFFFF;
    s->cpu.db = dst_bank;
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
    native_note_rti(s);
}

/* An interrupt handler that saves the interrupted code's registers on its
 * stack: 16-bit A, X and Y, D and the data bank, leaving 8-bit A and 16-bit
 * index registers; and gives them back and returns (RTI restores P). */
static inline void m_save_interrupted_registers(Snes *s)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, s->cpu.a);
    m_push16(s, s->cpu.x);
    m_push16(s, s->cpu.y);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.db);
    m_sep(s, CPU_FLAG_M);
}

static inline void m_return_to_interrupted(Snes *s)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.db = m_pull8(s);
    s->cpu.d = m_pull16(s);
    s->cpu.y = m_pull16(s);
    s->cpu.x = m_pull16(s);
    s->cpu.a = m_pull16(s);
    m_rti(s);
}

/* The same pair for handlers that push the data bank before the direct page
 * (the battle's), leaving A 16-bit. */
static inline void m_save_interrupted_registers_bank_first(Snes *s)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, s->cpu.a);
    m_push16(s, s->cpu.x);
    m_push16(s, s->cpu.y);
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
}

static inline void m_return_to_interrupted_bank_first(Snes *s)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    s->cpu.y = m_pull16(s);
    s->cpu.x = m_pull16(s);
    s->cpu.a = m_pull16(s);
    m_rti(s);
}

/* A routine reached by a long call that keeps its caller's registers on the
 * stack while it runs, flags first: P, the data bank, D, then 16-bit A, X and
 * Y (PHP, REP #$30, PHB, PHD, PHA, PHX, PHY), leaving 16-bit registers; and
 * gives them back and returns (PLY and PLX at the index width it has then,
 * then 16-bit A, D, the data bank and P, and RTL). */
static inline void m_save_caller_registers(Snes *s)
{
    m_push8(s, s->cpu.p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    m_push16(s, s->cpu.a);
    m_push16(s, s->cpu.x);
    m_push16(s, s->cpu.y);
}

static inline void m_return_to_caller_long(Snes *s)
{
    if (m_x8(s)) {
        s->cpu.y = m_pull8(s);
        s->cpu.x = m_pull8(s);
    } else {
        s->cpu.y = m_pull16(s);
        s->cpu.x = m_pull16(s);
    }
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.a = m_pull16(s);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    s->cpu.p = m_pull8(s);
    if (m_x8(s)) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
    m_rtl(s);
}

/* A coroutine's switch, as the menus' tasks make one (game/menu/tasks.c):
 * the registers a coroutine keeps on its own stack as it gives the processor
 * up -- X and Y at the width it has them, P and D (PHX, PHY, PHP, PHD) -- and
 * the switch back into one that kept them so: S set to where it kept them,
 * the same pulled back (PLD, PLP, then PLY and PLX at the width P gives, the
 * last of which sets N and Z) and its RTS. */
static inline void m_keep_registers_to_switch(Snes *s)
{
    if (m_x8(s)) {
        m_push8(s, (uint8_t)s->cpu.x);
        m_push8(s, (uint8_t)s->cpu.y);
    } else {
        m_push16(s, s->cpu.x);
        m_push16(s, s->cpu.y);
    }
    m_push8(s, s->cpu.p);
    m_push16(s, s->cpu.d);
}

static inline void m_switch_back(Snes *s, uint16_t stack)
{
    s->cpu.s = stack;
    s->cpu.d = m_pull16(s);
    s->cpu.p = m_pull8(s);
    if (m_x8(s)) {
        s->cpu.y = m_pull8(s);
        s->cpu.x = m_pull8(s);
        m_set_nz8(s, (uint8_t)s->cpu.x);
    } else {
        s->cpu.y = m_pull16(s);
        s->cpu.x = m_pull16(s);
        m_set_nz16(s, s->cpu.x);
    }
    m_rts(s);
}

/* An MVN of `bytes` bytes (at least 1) from src_bank:from to dst_bank:to,
 * with the registers it leaves: X and Y past the ends, A $FFFF, the data bank
 * dst_bank. */
static inline void m_block_move(Snes *s, uint8_t dst_bank, uint16_t to, uint8_t src_bank, uint16_t from, uint16_t bytes)
{
    s->cpu.x = from;
    s->cpu.y = to;
    s->cpu.a = (uint16_t)(bytes - 1);
    m_mvn(s, dst_bank, src_bank);
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
 * instruction at `site` would. The callee may take any number of frames (see
 * native_call). The _dc forms name registers and flags the callee doesn't read,
 * which shadow validation then doesn't compare at the call. */
static inline void m_jsr(Snes *s, uint32_t site, uint32_t target) { native_call(s, site, target, false); }
static inline void m_jsl(Snes *s, uint32_t site, uint32_t target) { native_call(s, site, target, true); }
static inline void m_jsr_dc(Snes *s, uint32_t site, uint32_t target, uint16_t dont_care)
{
    native_call_dc(s, site, target, false, dont_care);
}
static inline void m_jsl_dc(Snes *s, uint32_t site, uint32_t target, uint16_t dont_care)
{
    native_call_dc(s, site, target, true, dont_care);
}

/* For a native routine that stops at an instruction of the original (setting
 * PC) rather than returning: registers and flags nothing reads there before
 * setting them, which shadow validation then doesn't compare at the stop, on
 * top of the entry's don't-care set (native_dont_care_at_stop). `at` is that
 * instruction's 24-bit address, or the return instruction the routine carries
 * out last when what it declares is dead after a return: tools/dontcare_check.py
 * checks the set against liveness there, so write both as constants, or
 * from the caller's constants or a table in the same file. */
static inline void m_stop_dont_care(Snes *s, uint32_t at, uint16_t dont_care)
{
    native_dont_care_at_stop(s, at, dont_care);
}

/* Go on at `at` (24-bit) where the original jumps through a target it reads
 * at run time (an interrupt's stub in work RAM), with nothing declared dead
 * there: a stop shadow mode compares as any other. */
static inline void m_jump_to(Snes *s, uint32_t at)
{
    s->cpu.pb = (uint8_t)(at >> 16);
    s->cpu.pc = (uint16_t)at;
}

/* Stop at the original's instruction `at` (24-bit), where the interpreter or
 * the next entry carries on, with `dont_care` dead there (m_stop_dont_care).
 * A routine whose entry has a don't-care set stops this way, so the check
 * knows where the entry's set applies (native_note_return). `at` may be an
 * instruction the original passes more than once, such as a loop's head:
 * shadow mode compares every pass the original makes through it with the same
 * S and takes the first where the machines are the same (native.c,
 * reach_point), so the port can stop after any number of passes. */
static inline void m_stop_at(Snes *s, uint32_t at, uint16_t dont_care)
{
    s->cpu.pb = (uint8_t)(at >> 16);
    s->cpu.pc = (uint16_t)at;
    native_dont_care_at_stop(s, at, dont_care);
}

/* The same, with `dont_care` the stop's own set: the entry's isn't added
 * there (native_own_dont_care_at_stop). For an early exit to code that reads
 * what the entry's callers don't, such as a reset: the entry keeps the larger
 * set for its returns, and the check verifies each where it applies. */
static inline void m_stop_at_own(Snes *s, uint32_t at, uint16_t dont_care)
{
    s->cpu.pb = (uint8_t)(at >> 16);
    s->cpu.pc = (uint16_t)at;
    native_own_dont_care_at_stop(s, at, dont_care);
}

#endif

#include "cpu65816.h"

/* ---- bus and flag helpers ------------------------------------------------ */

static inline uint8_t rd(Cpu *c, uint32_t addr)
{
    c->last_cycle_io = false;
    return c->bus.read(c->bus.ctx, addr & 0xFFFFFF);
}
static inline void wr(Cpu *c, uint32_t addr, uint8_t v)
{
    c->last_cycle_io = false;
    c->bus.write(c->bus.ctx, addr & 0xFFFFFF, v);
}

static inline void io(Cpu *c)
{
    c->io_cycles++;
    c->last_cycle_io = true;
}

static inline bool m8(const Cpu *c) { return c->e || (c->p & CPU_FLAG_M); }
static inline bool x8(const Cpu *c) { return c->e || (c->p & CPU_FLAG_X); }

static inline void set_flag(Cpu *c, uint8_t f, bool on) { c->p = on ? (c->p | f) : (c->p & ~f); }

static inline void set_nz(Cpu *c, uint16_t v, bool w8)
{
    if (w8) {
        set_flag(c, CPU_FLAG_Z, (v & 0xFF) == 0);
        set_flag(c, CPU_FLAG_N, v & 0x80);
    } else {
        set_flag(c, CPU_FLAG_Z, v == 0);
        set_flag(c, CPU_FLAG_N, v & 0x8000);
    }
}

/* Enforce invariants after P or E changes. */
static void normalize_p(Cpu *c)
{
    if (c->e)
        c->p |= CPU_FLAG_M | CPU_FLAG_X;
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
}

static inline void set_a(Cpu *c, uint16_t v)
{
    if (m8(c))
        c->a = (c->a & 0xFF00) | (v & 0xFF);
    else
        c->a = v;
}

static inline uint16_t mask_x(const Cpu *c, uint16_t v) { return x8(c) ? (v & 0xFF) : v; }

/* ---- fetch and stack ----------------------------------------------------- */

static inline uint8_t fetch8(Cpu *c)
{
    uint8_t v = rd(c, (uint32_t)c->pb << 16 | c->pc);
    c->pc++;
    return v;
}

static inline uint16_t fetch16(Cpu *c)
{
    uint16_t lo = fetch8(c);
    return lo | (uint16_t)fetch8(c) << 8;
}

static inline uint32_t fetch24(Cpu *c)
{
    uint32_t lo = fetch16(c);
    return lo | (uint32_t)fetch8(c) << 16;
}

/* Pushes and pulls. In emulation mode the 6502-era opcodes keep S on page 1
 * for every byte; the 65816 additions ("n" variants) only fix S up afterwards. */
static inline void push8(Cpu *c, uint8_t v)
{
    wr(c, c->s, v);
    c->s--;
    if (c->e)
        c->s = 0x0100 | (c->s & 0xFF);
}

static inline uint8_t pull8(Cpu *c)
{
    c->s++;
    if (c->e)
        c->s = 0x0100 | (c->s & 0xFF);
    return rd(c, c->s);
}

static inline void push16(Cpu *c, uint16_t v)
{
    push8(c, v >> 8);
    push8(c, v & 0xFF);
}

static inline uint16_t pull16(Cpu *c)
{
    uint16_t lo = pull8(c);
    return lo | (uint16_t)pull8(c) << 8;
}

static inline void push8n(Cpu *c, uint8_t v) { wr(c, c->s, v); c->s--; }
static inline uint8_t pull8n(Cpu *c) { c->s++; return rd(c, c->s); }
static inline void push16n(Cpu *c, uint16_t v) { push8n(c, v >> 8); push8n(c, v & 0xFF); }
static inline uint16_t pull16n(Cpu *c) { uint16_t lo = pull8n(c); return lo | (uint16_t)pull8n(c) << 8; }
static inline void fix_emulation_stack(Cpu *c)
{
    if (c->e)
        c->s = 0x0100 | (c->s & 0xFF);
}

/* ---- effective addresses ------------------------------------------------- */

/* addr is 24-bit. wrap is the mask applied when reading the byte after addr:
 * direct-page and stack-relative data wraps within bank 0, the rest carries. */
typedef struct {
    uint32_t addr;
    uint32_t wrap;
} Ea;

static inline uint32_t dbr(const Cpu *c) { return (uint32_t)c->db << 16; }

static uint16_t read_word_bank0(Cpu *c, uint16_t addr)
{
    return rd(c, addr) | (uint16_t)rd(c, (uint16_t)(addr + 1)) << 8;
}

static uint32_t read_long_bank0(Cpu *c, uint16_t addr)
{
    return rd(c, addr) | (uint32_t)rd(c, (uint16_t)(addr + 1)) << 8 |
           (uint32_t)rd(c, (uint16_t)(addr + 2)) << 16;
}

/* Direct page address. Indexed modes carry into the high byte, except in
 * emulation mode with D's low byte zero, where operand + index wraps within the
 * page like 6502 zero page. Established empirically from SingleStepTests
 * (every disagreeing case has D low = $00 and a carry out of operand + index). */
/* Direct-page addressing takes an extra internal cycle when D's low byte is
 * nonzero (verified against SingleStepTests). */
static inline uint16_t dp_addr(Cpu *c, uint16_t operand)
{
    if (c->d & 0xFF)
        io(c);
    return (uint16_t)(c->d + operand);
}
static inline uint16_t dp_indexed(Cpu *c, uint8_t operand, uint16_t index)
{
    if (c->d & 0xFF)
        io(c);
    if (c->e && (c->d & 0xFF) == 0)
        return (uint16_t)((c->d & 0xFF00) | (uint8_t)(operand + index));
    return (uint16_t)(c->d + operand + index);
}

static Ea ea_dp(Cpu *c) { return (Ea){ dp_addr(c, fetch8(c)), 0xFFFF }; }
static Ea ea_dpx(Cpu *c)
{
    uint8_t op = fetch8(c);
    io(c);
    return (Ea){ dp_indexed(c, op, c->x), 0xFFFF };
}
static Ea ea_dpy(Cpu *c)
{
    uint8_t op = fetch8(c);
    io(c);
    return (Ea){ dp_indexed(c, op, c->y), 0xFFFF };
}
static Ea ea_dpind(Cpu *c)
{
    uint16_t p = dp_addr(c, fetch8(c));
    return (Ea){ dbr(c) | read_word_bank0(c, p), 0xFFFFFF };
}
static Ea ea_dpxind(Cpu *c)
{
    uint8_t op = fetch8(c);
    io(c);
    uint16_t p = dp_indexed(c, op, c->x);
    return (Ea){ dbr(c) | read_word_bank0(c, p), 0xFFFFFF };
}
/* Indexed absolute-style addressing takes an extra cycle for writes, 16-bit
 * index registers, or when indexing crosses a page. */
static uint32_t indexed(Cpu *c, uint32_t base, uint16_t index, bool write)
{
    uint32_t addr = (base + index) & 0xFFFFFF;
    if (write || !x8(c) || (base >> 8) != (addr >> 8))
        io(c);
    return addr;
}

static Ea ea_dpindy(Cpu *c, bool write)
{
    uint16_t p = dp_addr(c, fetch8(c));
    return (Ea){ indexed(c, dbr(c) | read_word_bank0(c, p), c->y, write), 0xFFFFFF };
}
static Ea ea_dplind(Cpu *c)
{
    uint16_t p = dp_addr(c, fetch8(c));
    return (Ea){ read_long_bank0(c, p), 0xFFFFFF };
}
static Ea ea_dplindy(Cpu *c)
{
    uint16_t p = dp_addr(c, fetch8(c));
    return (Ea){ (read_long_bank0(c, p) + c->y) & 0xFFFFFF, 0xFFFFFF };
}
static Ea ea_abs(Cpu *c) { return (Ea){ dbr(c) | fetch16(c), 0xFFFFFF }; }
static Ea ea_absx(Cpu *c, bool write) { return (Ea){ indexed(c, dbr(c) | fetch16(c), c->x, write), 0xFFFFFF }; }
static Ea ea_absy(Cpu *c, bool write) { return (Ea){ indexed(c, dbr(c) | fetch16(c), c->y, write), 0xFFFFFF }; }
static Ea ea_long(Cpu *c) { return (Ea){ fetch24(c), 0xFFFFFF }; }
static Ea ea_longx(Cpu *c) { return (Ea){ (fetch24(c) + c->x) & 0xFFFFFF, 0xFFFFFF }; }
static Ea ea_sr(Cpu *c)
{
    uint8_t op = fetch8(c);
    io(c);
    return (Ea){ (uint16_t)(c->s + op), 0xFFFF };
}
static Ea ea_srindy(Cpu *c)
{
    uint8_t op = fetch8(c);
    io(c);
    uint16_t p = (uint16_t)(c->s + op);
    io(c);
    return (Ea){ ((dbr(c) | read_word_bank0(c, p)) + c->y) & 0xFFFFFF, 0xFFFFFF };
}

static inline uint16_t read_w(Cpu *c, Ea ea, bool w8)
{
    uint16_t v = rd(c, ea.addr);
    if (!w8)
        v |= (uint16_t)rd(c, (ea.addr + 1) & ea.wrap) << 8;
    return v;
}

static inline void write_w(Cpu *c, Ea ea, uint16_t v, bool w8)
{
    wr(c, ea.addr, v & 0xFF);
    if (!w8)
        wr(c, (ea.addr + 1) & ea.wrap, v >> 8);
}

/* Read-modify-write ops write the high byte first. */
static inline void write_w_modify(Cpu *c, Ea ea, uint16_t v, bool w8)
{
    if (!w8)
        wr(c, (ea.addr + 1) & ea.wrap, v >> 8);
    wr(c, ea.addr, v & 0xFF);
}

/* ---- arithmetic and logic -------------------------------------------------- */

static void op_adc(Cpu *c, uint16_t data)
{
    int carry = c->p & CPU_FLAG_C;
    int r;
    if (m8(c)) {
        uint8_t a = c->a & 0xFF;
        data &= 0xFF;
        if (!(c->p & CPU_FLAG_D)) {
            r = a + data + carry;
        } else {
            r = (a & 0x0F) + (data & 0x0F) + carry;
            if (r > 0x09) r += 0x06;
            carry = r > 0x0F;
            r = (a & 0xF0) + (data & 0xF0) + (carry << 4) + (r & 0x0F);
        }
        set_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x80);
        if ((c->p & CPU_FLAG_D) && r > 0x9F) r += 0x60;
        set_flag(c, CPU_FLAG_C, r > 0xFF);
        c->a = (c->a & 0xFF00) | (r & 0xFF);
        set_nz(c, r, true);
    } else {
        uint16_t a = c->a;
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
        set_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x8000);
        if ((c->p & CPU_FLAG_D) && r > 0x9FFF) r += 0x6000;
        set_flag(c, CPU_FLAG_C, r > 0xFFFF);
        c->a = r & 0xFFFF;
        set_nz(c, c->a, false);
    }
}

static void op_sbc(Cpu *c, uint16_t data)
{
    int carry = c->p & CPU_FLAG_C;
    int r;
    if (m8(c)) {
        uint8_t a = c->a & 0xFF;
        data = ~data & 0xFF;
        if (!(c->p & CPU_FLAG_D)) {
            r = a + data + carry;
        } else {
            r = (a & 0x0F) + (data & 0x0F) + carry;
            if (r <= 0x0F) r -= 0x06;
            carry = r > 0x0F;
            r = (a & 0xF0) + (data & 0xF0) + (carry << 4) + (r & 0x0F);
        }
        set_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x80);
        if ((c->p & CPU_FLAG_D) && r <= 0xFF) r -= 0x60;
        set_flag(c, CPU_FLAG_C, r > 0xFF);
        c->a = (c->a & 0xFF00) | (r & 0xFF);
        set_nz(c, r, true);
    } else {
        uint16_t a = c->a;
        data = ~data & 0xFFFF;
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
        set_flag(c, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x8000);
        if ((c->p & CPU_FLAG_D) && r <= 0xFFFF) r -= 0x6000;
        set_flag(c, CPU_FLAG_C, r > 0xFFFF);
        c->a = r & 0xFFFF;
        set_nz(c, c->a, false);
    }
}

static void op_cmp(Cpu *c, uint16_t reg, uint16_t data, bool w8)
{
    if (w8) {
        reg &= 0xFF;
        data &= 0xFF;
    }
    uint16_t r = reg - data;
    set_flag(c, CPU_FLAG_C, reg >= data);
    set_nz(c, r, w8);
}

static void op_bit(Cpu *c, uint16_t data, bool immediate)
{
    bool w8 = m8(c);
    uint16_t a = w8 ? (c->a & 0xFF) : c->a;
    if (!immediate) {
        set_flag(c, CPU_FLAG_N, data & (w8 ? 0x80 : 0x8000));
        set_flag(c, CPU_FLAG_V, data & (w8 ? 0x40 : 0x4000));
    }
    set_flag(c, CPU_FLAG_Z, ((a & data) & (w8 ? 0xFF : 0xFFFF)) == 0);
}

typedef enum { MOD_ASL, MOD_LSR, MOD_ROL, MOD_ROR, MOD_INC, MOD_DEC, MOD_TSB, MOD_TRB } ModOp;

static uint16_t modify(Cpu *c, ModOp op, uint16_t v, bool w8)
{
    uint16_t top = w8 ? 0x80 : 0x8000;
    uint16_t mask = w8 ? 0xFF : 0xFFFF;
    uint16_t a = c->a & mask;
    bool carry = c->p & CPU_FLAG_C;
    v &= mask;
    switch (op) {
    case MOD_ASL: set_flag(c, CPU_FLAG_C, v & top); v = (v << 1) & mask; break;
    case MOD_LSR: set_flag(c, CPU_FLAG_C, v & 1); v >>= 1; break;
    case MOD_ROL: set_flag(c, CPU_FLAG_C, v & top); v = ((v << 1) | carry) & mask; break;
    case MOD_ROR: set_flag(c, CPU_FLAG_C, v & 1); v = (v >> 1) | (carry ? top : 0); break;
    case MOD_INC: v = (v + 1) & mask; break;
    case MOD_DEC: v = (v - 1) & mask; break;
    case MOD_TSB: set_flag(c, CPU_FLAG_Z, (a & v) == 0); return v | a;
    case MOD_TRB: set_flag(c, CPU_FLAG_Z, (a & v) == 0); return v & ~a & mask;
    }
    set_nz(c, v, w8);
    return v;
}

static void modify_mem(Cpu *c, ModOp op, Ea ea)
{
    bool w8 = m8(c);
    uint16_t v = read_w(c, ea, w8);
    io(c);
    write_w_modify(c, ea, modify(c, op, v, w8), w8);
}

/* ---- control flow ----------------------------------------------------------- */

static void branch(Cpu *c, bool cond)
{
    int8_t off = (int8_t)fetch8(c);
    if (!cond)
        return;
    uint16_t target = (uint16_t)(c->pc + off);
    io(c);
    if (c->e && (target >> 8) != (c->pc >> 8))
        io(c);
    c->pc = target;
}

static void interrupt(Cpu *c, uint16_t native_vec, uint16_t emu_vec, bool software)
{
    if (!software) {
        io(c);
        io(c);
    }
    if (!c->e) {
        push8(c, c->pb);
        push16(c, c->pc);
        push8(c, c->p);
    } else {
        push16(c, c->pc);
        push8(c, software ? c->p : (c->p & ~CPU_FLAG_X));
    }
    c->p |= CPU_FLAG_I;
    c->p &= ~CPU_FLAG_D;
    c->pb = 0;
    c->pc = read_word_bank0(c, c->e ? emu_vec : native_vec);
}

/* ---- public ---------------------------------------------------------------- */

/* cpu_init and cpu_reset are in cpu_entry.c, which doesn't need the interpreter. */

CpuStep cpu_step(Cpu *c)
{
    if (c->stopped)
        return CPU_STEP_IDLE;
    if (c->nmi_pending) {
        c->nmi_pending = false;
        c->waiting = false;
        interrupt(c, 0xFFEA, 0xFFFA, false);
        return CPU_STEP_INTERRUPT;
    }
    if (c->irq_line) {
        c->waiting = false;
        if (!(c->p & CPU_FLAG_I)) {
            interrupt(c, 0xFFEE, 0xFFFE, false);
            return CPU_STEP_INTERRUPT;
        }
    }
    if (c->waiting)
        return CPU_STEP_IDLE;

    if (c->on_insn)
        c->on_insn(c->on_insn_ctx, cpu_pc24(c), c->p, c->e);

    uint8_t op = fetch8(c);
    bool wm = m8(c), wx = x8(c);
    Ea ea;

    switch (op) {
    /* ORA / AND / EOR / ADC / LDA / CMP / SBC share addressing layouts. */
#define GROUP1(name, base, body)                                                                  \
    case base + 0x01: ea = ea_dpxind(c); goto name##_mem;                                   \
    case base + 0x03: ea = ea_sr(c); goto name##_mem;                                       \
    case base + 0x05: ea = ea_dp(c); goto name##_mem;                                       \
    case base + 0x07: ea = ea_dplind(c); goto name##_mem;                                   \
    case base + 0x0D: ea = ea_abs(c); goto name##_mem;                                      \
    case base + 0x0F: ea = ea_long(c); goto name##_mem;                                     \
    case base + 0x11: ea = ea_dpindy(c, false); goto name##_mem;                                   \
    case base + 0x12: ea = ea_dpind(c); goto name##_mem;                                    \
    case base + 0x13: ea = ea_srindy(c); goto name##_mem;                                   \
    case base + 0x15: ea = ea_dpx(c); goto name##_mem;                                      \
    case base + 0x17: ea = ea_dplindy(c); goto name##_mem;                                  \
    case base + 0x19: ea = ea_absy(c, false); goto name##_mem;                                     \
    case base + 0x1D: ea = ea_absx(c, false); goto name##_mem;                                     \
    case base + 0x1F: ea = ea_longx(c); goto name##_mem;                                    \
    name##_mem : {                                                                           \
        uint16_t data = read_w(c, ea, wm);                                                   \
        body;                                                                                \
        break;                                                                               \
    }                                                                                        \
    case base + 0x09: {                                                                      \
        uint16_t data = wm ? fetch8(c) : fetch16(c);                                         \
        body;                                                                                \
        break;                                                                               \
    }

    GROUP1(ora, 0x00, { set_a(c, c->a | data); set_nz(c, c->a, wm); })
    GROUP1(and, 0x20, { set_a(c, c->a & data); set_nz(c, c->a, wm); })
    GROUP1(eor, 0x40, { set_a(c, c->a ^ data); set_nz(c, c->a, wm); })
    GROUP1(adc, 0x60, { op_adc(c, data); })
    GROUP1(lda, 0xA0, { set_a(c, data); set_nz(c, c->a, wm); })
    GROUP1(cmp, 0xC0, { op_cmp(c, c->a, data, wm); })
    GROUP1(sbc, 0xE0, { op_sbc(c, data); })
#undef GROUP1

    /* STA */
    case 0x81: ea = ea_dpxind(c); goto sta;
    case 0x83: ea = ea_sr(c); goto sta;
    case 0x85: ea = ea_dp(c); goto sta;
    case 0x87: ea = ea_dplind(c); goto sta;
    case 0x8D: ea = ea_abs(c); goto sta;
    case 0x8F: ea = ea_long(c); goto sta;
    case 0x91: ea = ea_dpindy(c, true); goto sta;
    case 0x92: ea = ea_dpind(c); goto sta;
    case 0x93: ea = ea_srindy(c); goto sta;
    case 0x95: ea = ea_dpx(c); goto sta;
    case 0x97: ea = ea_dplindy(c); goto sta;
    case 0x99: ea = ea_absy(c, true); goto sta;
    case 0x9D: ea = ea_absx(c, true); goto sta;
    case 0x9F: ea = ea_longx(c); goto sta;
    sta:
        write_w(c, ea, c->a, wm);
        break;

    /* STZ / STX / STY */
    case 0x64: ea = ea_dp(c); write_w(c, ea, 0, wm); break;
    case 0x74: ea = ea_dpx(c); write_w(c, ea, 0, wm); break;
    case 0x9C: ea = ea_abs(c); write_w(c, ea, 0, wm); break;
    case 0x9E: ea = ea_absx(c, true); write_w(c, ea, 0, wm); break;
    case 0x86: ea = ea_dp(c); write_w(c, ea, c->x, wx); break;
    case 0x96: ea = ea_dpy(c); write_w(c, ea, c->x, wx); break;
    case 0x8E: ea = ea_abs(c); write_w(c, ea, c->x, wx); break;
    case 0x84: ea = ea_dp(c); write_w(c, ea, c->y, wx); break;
    case 0x94: ea = ea_dpx(c); write_w(c, ea, c->y, wx); break;
    case 0x8C: ea = ea_abs(c); write_w(c, ea, c->y, wx); break;

    /* LDX / LDY / CPX / CPY */
    case 0xA2: c->x = wx ? fetch8(c) : fetch16(c); set_nz(c, c->x, wx); break;
    case 0xA6: ea = ea_dp(c); goto ldx;
    case 0xB6: ea = ea_dpy(c); goto ldx;
    case 0xAE: ea = ea_abs(c); goto ldx;
    case 0xBE: ea = ea_absy(c, false); goto ldx;
    ldx:
        c->x = read_w(c, ea, wx);
        set_nz(c, c->x, wx);
        break;
    case 0xA0: c->y = wx ? fetch8(c) : fetch16(c); set_nz(c, c->y, wx); break;
    case 0xA4: ea = ea_dp(c); goto ldy;
    case 0xB4: ea = ea_dpx(c); goto ldy;
    case 0xAC: ea = ea_abs(c); goto ldy;
    case 0xBC: ea = ea_absx(c, false); goto ldy;
    ldy:
        c->y = read_w(c, ea, wx);
        set_nz(c, c->y, wx);
        break;
    case 0xE0: op_cmp(c, c->x, wx ? fetch8(c) : fetch16(c), wx); break;
    case 0xE4: ea = ea_dp(c); op_cmp(c, c->x, read_w(c, ea, wx), wx); break;
    case 0xEC: ea = ea_abs(c); op_cmp(c, c->x, read_w(c, ea, wx), wx); break;
    case 0xC0: op_cmp(c, c->y, wx ? fetch8(c) : fetch16(c), wx); break;
    case 0xC4: ea = ea_dp(c); op_cmp(c, c->y, read_w(c, ea, wx), wx); break;
    case 0xCC: ea = ea_abs(c); op_cmp(c, c->y, read_w(c, ea, wx), wx); break;

    /* BIT */
    case 0x89: op_bit(c, wm ? fetch8(c) : fetch16(c), true); break;
    case 0x24: ea = ea_dp(c); op_bit(c, read_w(c, ea, wm), false); break;
    case 0x34: ea = ea_dpx(c); op_bit(c, read_w(c, ea, wm), false); break;
    case 0x2C: ea = ea_abs(c); op_bit(c, read_w(c, ea, wm), false); break;
    case 0x3C: ea = ea_absx(c, false); op_bit(c, read_w(c, ea, wm), false); break;

    /* Shifts, rotates, INC/DEC, TSB/TRB */
    case 0x0A: io(c); set_a(c, modify(c, MOD_ASL, c->a, wm)); break;
    case 0x4A: io(c); set_a(c, modify(c, MOD_LSR, c->a, wm)); break;
    case 0x2A: io(c); set_a(c, modify(c, MOD_ROL, c->a, wm)); break;
    case 0x6A: io(c); set_a(c, modify(c, MOD_ROR, c->a, wm)); break;
    case 0x1A: io(c); set_a(c, modify(c, MOD_INC, c->a, wm)); break;
    case 0x3A: io(c); set_a(c, modify(c, MOD_DEC, c->a, wm)); break;
    case 0x06: modify_mem(c, MOD_ASL, ea_dp(c)); break;
    case 0x16: modify_mem(c, MOD_ASL, ea_dpx(c)); break;
    case 0x0E: modify_mem(c, MOD_ASL, ea_abs(c)); break;
    case 0x1E: modify_mem(c, MOD_ASL, ea_absx(c, true)); break;
    case 0x46: modify_mem(c, MOD_LSR, ea_dp(c)); break;
    case 0x56: modify_mem(c, MOD_LSR, ea_dpx(c)); break;
    case 0x4E: modify_mem(c, MOD_LSR, ea_abs(c)); break;
    case 0x5E: modify_mem(c, MOD_LSR, ea_absx(c, true)); break;
    case 0x26: modify_mem(c, MOD_ROL, ea_dp(c)); break;
    case 0x36: modify_mem(c, MOD_ROL, ea_dpx(c)); break;
    case 0x2E: modify_mem(c, MOD_ROL, ea_abs(c)); break;
    case 0x3E: modify_mem(c, MOD_ROL, ea_absx(c, true)); break;
    case 0x66: modify_mem(c, MOD_ROR, ea_dp(c)); break;
    case 0x76: modify_mem(c, MOD_ROR, ea_dpx(c)); break;
    case 0x6E: modify_mem(c, MOD_ROR, ea_abs(c)); break;
    case 0x7E: modify_mem(c, MOD_ROR, ea_absx(c, true)); break;
    case 0xE6: modify_mem(c, MOD_INC, ea_dp(c)); break;
    case 0xF6: modify_mem(c, MOD_INC, ea_dpx(c)); break;
    case 0xEE: modify_mem(c, MOD_INC, ea_abs(c)); break;
    case 0xFE: modify_mem(c, MOD_INC, ea_absx(c, true)); break;
    case 0xC6: modify_mem(c, MOD_DEC, ea_dp(c)); break;
    case 0xD6: modify_mem(c, MOD_DEC, ea_dpx(c)); break;
    case 0xCE: modify_mem(c, MOD_DEC, ea_abs(c)); break;
    case 0xDE: modify_mem(c, MOD_DEC, ea_absx(c, true)); break;
    case 0x04: modify_mem(c, MOD_TSB, ea_dp(c)); break;
    case 0x0C: modify_mem(c, MOD_TSB, ea_abs(c)); break;
    case 0x14: modify_mem(c, MOD_TRB, ea_dp(c)); break;
    case 0x1C: modify_mem(c, MOD_TRB, ea_abs(c)); break;

    /* Index register increments */
    case 0xE8: io(c); c->x = mask_x(c, c->x + 1); set_nz(c, c->x, wx); break;
    case 0xC8: io(c); c->y = mask_x(c, c->y + 1); set_nz(c, c->y, wx); break;
    case 0xCA: io(c); c->x = mask_x(c, c->x - 1); set_nz(c, c->x, wx); break;
    case 0x88: io(c); c->y = mask_x(c, c->y - 1); set_nz(c, c->y, wx); break;

    /* Branches */
    case 0x10: branch(c, !(c->p & CPU_FLAG_N)); break;
    case 0x30: branch(c, c->p & CPU_FLAG_N); break;
    case 0x50: branch(c, !(c->p & CPU_FLAG_V)); break;
    case 0x70: branch(c, c->p & CPU_FLAG_V); break;
    case 0x90: branch(c, !(c->p & CPU_FLAG_C)); break;
    case 0xB0: branch(c, c->p & CPU_FLAG_C); break;
    case 0xD0: branch(c, !(c->p & CPU_FLAG_Z)); break;
    case 0xF0: branch(c, c->p & CPU_FLAG_Z); break;
    case 0x80: branch(c, true); break;
    case 0x82: {
        uint16_t off = fetch16(c);
        io(c);
        c->pc = (uint16_t)(c->pc + off);
        break;
    }

    /* Jumps, calls, returns */
    case 0x4C: c->pc = fetch16(c); break;
    case 0x5C: {
        uint32_t t = fetch24(c);
        c->pb = t >> 16;
        c->pc = t & 0xFFFF;
        break;
    }
    case 0x6C: c->pc = read_word_bank0(c, fetch16(c)); break;
    case 0x7C: {
        uint16_t p = (uint16_t)(fetch16(c) + c->x);
        io(c);
        uint32_t bank = (uint32_t)c->pb << 16;
        c->pc = rd(c, bank | p) | (uint16_t)rd(c, bank | (uint16_t)(p + 1)) << 8;
        break;
    }
    case 0xDC: {
        uint32_t t = read_long_bank0(c, fetch16(c));
        c->pb = t >> 16;
        c->pc = t & 0xFFFF;
        break;
    }
    case 0x20: {
        uint16_t t = fetch16(c);
        io(c);
        push16(c, (uint16_t)(c->pc - 1));
        c->pc = t;
        break;
    }
    case 0x22: {
        uint16_t t = fetch16(c);
        push8n(c, c->pb);
        io(c);
        uint8_t bank = fetch8(c);
        push16n(c, (uint16_t)(c->pc - 1));
        c->pb = bank;
        c->pc = t;
        fix_emulation_stack(c);
        break;
    }
    case 0xFC: {
        uint16_t lo = fetch8(c);
        push16(c, c->pc);   /* PC now points at the operand's high byte */
        uint16_t p = (uint16_t)((lo | (uint16_t)fetch8(c) << 8) + c->x);
        io(c);
        uint32_t bank = (uint32_t)c->pb << 16;
        c->pc = rd(c, bank | p) | (uint16_t)rd(c, bank | (uint16_t)(p + 1)) << 8;
        break;
    }
    case 0x60:
        io(c);
        io(c);
        c->pc = (uint16_t)(pull16(c) + 1);
        io(c);
        break;
    case 0x6B:
        io(c);
        io(c);
        c->pc = (uint16_t)(pull16n(c) + 1);
        c->pb = pull8n(c);
        fix_emulation_stack(c);
        break;
    case 0x40:
        io(c);
        io(c);
        c->p = pull8(c);
        normalize_p(c);
        c->pc = pull16(c);
        if (!c->e)
            c->pb = pull8(c);
        break;

    /* Software interrupts */
    case 0x00: fetch8(c); interrupt(c, 0xFFE6, 0xFFFE, true); break;
    case 0x02: fetch8(c); interrupt(c, 0xFFE4, 0xFFF4, true); break;

    /* Stack */
    case 0x48: io(c); if (wm) push8(c, c->a & 0xFF); else push16(c, c->a); break;
    case 0xDA: io(c); if (wx) push8(c, c->x & 0xFF); else push16(c, c->x); break;
    case 0x5A: io(c); if (wx) push8(c, c->y & 0xFF); else push16(c, c->y); break;
    case 0x68:
        io(c);
        io(c);
        set_a(c, wm ? pull8(c) : pull16(c));
        set_nz(c, c->a, wm);
        break;
    case 0xFA: io(c); io(c); c->x = wx ? pull8(c) : pull16(c); set_nz(c, c->x, wx); break;
    case 0x7A: io(c); io(c); c->y = wx ? pull8(c) : pull16(c); set_nz(c, c->y, wx); break;
    case 0x8B: io(c); push8(c, c->db); break;
    case 0xAB: io(c); io(c); c->db = pull8n(c); set_nz(c, c->db, true); fix_emulation_stack(c); break;
    case 0x4B: io(c); push8(c, c->pb); break;
    case 0x0B: io(c); push16n(c, c->d); fix_emulation_stack(c); break;
    case 0x2B: io(c); io(c); c->d = pull16n(c); set_nz(c, c->d, false); fix_emulation_stack(c); break;
    case 0x08: io(c); push8(c, c->p); break;
    case 0x28: io(c); io(c); c->p = pull8(c); normalize_p(c); break;
    case 0xF4: push16n(c, fetch16(c)); fix_emulation_stack(c); break;
    case 0xD4: {
        uint16_t p = dp_addr(c, fetch8(c));
        push16n(c, read_word_bank0(c, p));
        fix_emulation_stack(c);
        break;
    }
    case 0x62: {
        uint16_t off = fetch16(c);
        io(c);
        push16n(c, (uint16_t)(c->pc + off));
        fix_emulation_stack(c);
        break;
    }

    /* Transfers */
    case 0xAA: io(c); c->x = mask_x(c, c->a); set_nz(c, c->x, wx); break;
    case 0xA8: io(c); c->y = mask_x(c, c->a); set_nz(c, c->y, wx); break;
    case 0x8A: io(c); set_a(c, c->x); set_nz(c, c->a, wm); break;
    case 0x98: io(c); set_a(c, c->y); set_nz(c, c->a, wm); break;
    case 0x9A: io(c); c->s = c->e ? (0x0100 | (c->x & 0xFF)) : c->x; break;
    case 0xBA: io(c); c->x = mask_x(c, c->s); set_nz(c, c->x, wx); break;
    case 0x9B: io(c); c->y = mask_x(c, c->x); set_nz(c, c->y, wx); break;
    case 0xBB: io(c); c->x = mask_x(c, c->y); set_nz(c, c->x, wx); break;
    case 0x5B: io(c); c->d = c->a; set_nz(c, c->d, false); break;
    case 0x7B: io(c); c->a = c->d; set_nz(c, c->a, false); break;
    case 0x1B: io(c); c->s = c->e ? (0x0100 | (c->a & 0xFF)) : c->a; break;
    case 0x3B: io(c); c->a = c->s; set_nz(c, c->a, false); break;
    case 0xEB: io(c); io(c); c->a = (uint16_t)(c->a >> 8 | c->a << 8); set_nz(c, c->a, true); break;

    /* Flags and mode */
    case 0x18: io(c); c->p &= ~CPU_FLAG_C; break;
    case 0x38: io(c); c->p |= CPU_FLAG_C; break;
    case 0x58: io(c); c->p &= ~CPU_FLAG_I; break;
    case 0x78: io(c); c->p |= CPU_FLAG_I; break;
    case 0xD8: io(c); c->p &= ~CPU_FLAG_D; break;
    case 0xF8: io(c); c->p |= CPU_FLAG_D; break;
    case 0xB8: io(c); c->p &= ~CPU_FLAG_V; break;
    case 0xC2: c->p &= ~fetch8(c); io(c); normalize_p(c); break;
    case 0xE2: c->p |= fetch8(c); io(c); normalize_p(c); break;
    case 0xFB: {
        io(c);
        bool carry = c->p & CPU_FLAG_C;
        set_flag(c, CPU_FLAG_C, c->e);
        c->e = carry;
        if (c->e) {
            c->s = 0x0100 | (c->s & 0xFF);
        }
        normalize_p(c);
        break;
    }

    /* Block moves */
    case 0x54:
    case 0x44: {
        uint8_t dst = fetch8(c);
        uint8_t src = fetch8(c);
        c->db = dst;
        wr(c, (uint32_t)dst << 16 | c->y, rd(c, (uint32_t)src << 16 | c->x));
        io(c);
        io(c);
        int adj = op == 0x54 ? 1 : -1;
        c->x = mask_x(c, c->x + adj);
        c->y = mask_x(c, c->y + adj);
        if (c->a-- != 0)
            c->pc = (uint16_t)(c->pc - 3);
        break;
    }

    /* Misc */
    case 0xEA: io(c); break;
    case 0x42: fetch8(c); break;
    case 0xCB: io(c); io(c); c->waiting = true; break;
    case 0xDB: io(c); io(c); c->stopped = true; break;
    }
    return CPU_STEP_INSTRUCTION;
}

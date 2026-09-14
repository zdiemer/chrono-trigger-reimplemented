#include "spc700.h"

/* Cycles per opcode, not counting the 2 extra cycles of a taken branch.
 * Source: the SingleStepTests SPC700 suite (cycle list length per test; every
 * opcode has a single count, or that count plus 2 for taken branches). */
static const uint8_t cycles[256] = {
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 6, 8,  /* 0_ */
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 4, 6,  /* 1_ */
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 5, 4,  /* 2_ */
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 3, 8,  /* 3_ */
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 6, 6,  /* 4_ */
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 4, 5, 2, 2, 4, 3,  /* 5_ */
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 5, 5,  /* 6_ */
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3, 6,  /* 7_ */
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 2, 4, 5,  /* 8_ */
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 12, 5, /* 9_ */
    3, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 2, 4, 4,  /* A_ */
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3, 4,  /* B_ */
    3, 8, 4, 5, 4, 5, 4, 7, 2, 5, 6, 4, 5, 2, 4, 9,  /* C_ */
    2, 8, 4, 5, 5, 6, 6, 7, 4, 5, 5, 5, 2, 2, 6, 3,  /* D_ */
    2, 8, 4, 5, 3, 4, 3, 6, 2, 4, 5, 3, 4, 3, 4, 7,  /* E_ */
    2, 8, 4, 5, 4, 5, 5, 6, 3, 4, 5, 4, 2, 2, 4, 7,  /* F_ */
};

/* ---- bus and flag helpers ------------------------------------------------ */

static inline uint8_t rd(Spc700 *s, uint16_t addr) { return s->bus.read(s->bus.ctx, addr); }
static inline void wr(Spc700 *s, uint16_t addr, uint8_t v) { s->bus.write(s->bus.ctx, addr, v); }

static inline void set_flag(Spc700 *s, uint8_t f, bool on) { s->psw = on ? (s->psw | f) : (s->psw & ~f); }
static inline bool flag(const Spc700 *s, uint8_t f) { return s->psw & f; }

static inline uint8_t set_nz(Spc700 *s, uint8_t v)
{
    set_flag(s, SPC_FLAG_Z, v == 0);
    set_flag(s, SPC_FLAG_N, v & 0x80);
    return v;
}

static inline uint8_t fetch(Spc700 *s) { return rd(s, s->pc++); }

static inline uint16_t fetch16(Spc700 *s)
{
    uint16_t lo = fetch(s);
    return lo | (uint16_t)fetch(s) << 8;
}

static inline void push(Spc700 *s, uint8_t v) { wr(s, 0x0100 | s->sp--, v); }
static inline uint8_t pull(Spc700 *s) { return rd(s, 0x0100 | ++s->sp); }

/* Direct page address for an 8-bit offset. */
static inline uint16_t dp(const Spc700 *s, uint8_t off) { return (flag(s, SPC_FLAG_P) ? 0x0100 : 0) | off; }

static inline uint16_t read_dp_word(Spc700 *s, uint8_t off)
{
    return rd(s, dp(s, off)) | (uint16_t)rd(s, dp(s, (uint8_t)(off + 1))) << 8;
}

/* ---- arithmetic -------------------------------------------------------------- */

static uint8_t op_adc(Spc700 *s, uint8_t x, uint8_t y)
{
    int z = x + y + flag(s, SPC_FLAG_C);
    set_flag(s, SPC_FLAG_C, z > 0xFF);
    set_flag(s, SPC_FLAG_H, (x ^ y ^ z) & 0x10);
    set_flag(s, SPC_FLAG_V, ~(x ^ y) & (x ^ z) & 0x80);
    return set_nz(s, (uint8_t)z);
}

static uint8_t op_sbc(Spc700 *s, uint8_t x, uint8_t y) { return op_adc(s, x, (uint8_t)~y); }

static void op_cmp(Spc700 *s, uint8_t x, uint8_t y)
{
    int z = x - y;
    set_flag(s, SPC_FLAG_C, z >= 0);
    set_nz(s, (uint8_t)z);
}

static uint8_t op_or(Spc700 *s, uint8_t x, uint8_t y) { return set_nz(s, x | y); }
static uint8_t op_and(Spc700 *s, uint8_t x, uint8_t y) { return set_nz(s, x & y); }
static uint8_t op_eor(Spc700 *s, uint8_t x, uint8_t y) { return set_nz(s, x ^ y); }

/* Binary operation selected by the high nibble of the x4-x9 opcode rows. */
static uint8_t alu(Spc700 *s, uint8_t row, uint8_t x, uint8_t y)
{
    switch (row) {
    case 0x0: return op_or(s, x, y);
    case 0x2: return op_and(s, x, y);
    case 0x4: return op_eor(s, x, y);
    case 0x6: op_cmp(s, x, y); return x;
    case 0x8: return op_adc(s, x, y);
    default:  return op_sbc(s, x, y);   /* 0xA */
    }
}

static uint8_t op_asl(Spc700 *s, uint8_t v) { set_flag(s, SPC_FLAG_C, v & 0x80); return set_nz(s, (uint8_t)(v << 1)); }
static uint8_t op_lsr(Spc700 *s, uint8_t v) { set_flag(s, SPC_FLAG_C, v & 1); return set_nz(s, v >> 1); }
static uint8_t op_rol(Spc700 *s, uint8_t v)
{
    bool c = flag(s, SPC_FLAG_C);
    set_flag(s, SPC_FLAG_C, v & 0x80);
    return set_nz(s, (uint8_t)(v << 1 | c));
}
static uint8_t op_ror(Spc700 *s, uint8_t v)
{
    bool c = flag(s, SPC_FLAG_C);
    set_flag(s, SPC_FLAG_C, v & 1);
    return set_nz(s, (uint8_t)(v >> 1 | c << 7));
}

/* Shift/increment selected by the high nibble of the xB/xC opcode rows. */
static uint8_t modify(Spc700 *s, uint8_t row, uint8_t v)
{
    switch (row) {
    case 0x0: return op_asl(s, v);
    case 0x2: return op_rol(s, v);
    case 0x4: return op_lsr(s, v);
    case 0x6: return op_ror(s, v);
    case 0x8: return set_nz(s, (uint8_t)(v - 1));
    default:  return set_nz(s, (uint8_t)(v + 1));   /* 0xA */
    }
}

static void set_nz16(Spc700 *s, uint16_t v)
{
    set_flag(s, SPC_FLAG_Z, v == 0);
    set_flag(s, SPC_FLAG_N, v & 0x8000);
}

/* ---- public ------------------------------------------------------------------ */

void spc700_init(Spc700 *spc, Spc700Bus bus)
{
    *spc = (Spc700){ 0 };
    spc->bus = bus;
}

void spc700_reset(Spc700 *s)
{
    s->a = s->x = s->y = s->sp = s->psw = 0;
    s->halted = false;
    s->pc = rd(s, 0xFFFE) | (uint16_t)rd(s, 0xFFFF) << 8;
}

unsigned spc700_step(Spc700 *s)
{
    if (s->halted)
        return 2;

    uint8_t op = fetch(s);
    unsigned n = cycles[op];
    uint8_t hi = op >> 4, lo = op & 0x0F;

#define BRANCH(cond)                                          \
    do {                                                      \
        int8_t rel = (int8_t)fetch(s);                        \
        if (cond) {                                           \
            s->pc = (uint16_t)(s->pc + rel);                  \
            n += 2;                                           \
        }                                                     \
    } while (0)

    /* Regular columns first. */
    switch (lo) {
    case 0x1:   /* TCALL n */
        push(s, s->pc >> 8);
        push(s, s->pc & 0xFF);
        s->pc = rd(s, (uint16_t)(0xFFDE - hi * 2)) | (uint16_t)rd(s, (uint16_t)(0xFFDF - hi * 2)) << 8;
        return n;
    case 0x2: {   /* SET1 dp.bit (even hi) / CLR1 dp.bit (odd hi) */
        uint16_t addr = dp(s, fetch(s));
        uint8_t bit = 1 << (hi >> 1);
        uint8_t v = rd(s, addr);
        wr(s, addr, (hi & 1) ? (v & ~bit) : (v | bit));
        return n;
    }
    case 0x3: {   /* BBS dp.bit,rel (even hi) / BBC (odd hi) */
        uint8_t v = rd(s, dp(s, fetch(s)));
        bool set = v & (1 << (hi >> 1));
        BRANCH((hi & 1) ? !set : set);
        return n;
    }
    }

    if (hi <= 0xB && !(hi & 1) && lo >= 0x4 && lo <= 0x9) {
        /* OR/AND/EOR/CMP/ADC/SBC rows, even high nibble: A,dp  A,abs  A,(X)  A,[dp+X]  A,#imm  dp,dp */
        switch (lo) {
        case 0x4: s->a = alu(s, hi, s->a, rd(s, dp(s, fetch(s)))); return n;
        case 0x5: s->a = alu(s, hi, s->a, rd(s, fetch16(s))); return n;
        case 0x6: s->a = alu(s, hi, s->a, rd(s, dp(s, s->x))); return n;
        case 0x7: {
            uint8_t off = fetch(s);
            s->a = alu(s, hi, s->a, rd(s, read_dp_word(s, (uint8_t)(off + s->x))));
            return n;
        }
        case 0x8: s->a = alu(s, hi, s->a, fetch(s)); return n;
        case 0x9: {
            uint8_t src = rd(s, dp(s, fetch(s)));
            uint16_t dst = dp(s, fetch(s));
            uint8_t r = alu(s, hi, rd(s, dst), src);
            if (hi != 0x6)
                wr(s, dst, r);
            return n;
        }
        }
    }
    if (hi <= 0xB && (hi & 1) && lo >= 0x4 && lo <= 0x9) {
        /* Odd high nibble: A,dp+X  A,abs+X  A,abs+Y  A,[dp]+Y  dp,#imm  (X),(Y) */
        uint8_t row = hi - 1;
        switch (lo) {
        case 0x4: s->a = alu(s, row, s->a, rd(s, dp(s, (uint8_t)(fetch(s) + s->x)))); return n;
        case 0x5: s->a = alu(s, row, s->a, rd(s, (uint16_t)(fetch16(s) + s->x))); return n;
        case 0x6: s->a = alu(s, row, s->a, rd(s, (uint16_t)(fetch16(s) + s->y))); return n;
        case 0x7: {
            uint8_t off = fetch(s);
            s->a = alu(s, row, s->a, rd(s, (uint16_t)(read_dp_word(s, off) + s->y)));
            return n;
        }
        case 0x8: {
            uint8_t imm = fetch(s);
            uint16_t dst = dp(s, fetch(s));
            uint8_t r = alu(s, row, rd(s, dst), imm);
            if (row != 0x6)
                wr(s, dst, r);
            return n;
        }
        case 0x9: {
            uint8_t src = rd(s, dp(s, s->y));
            uint16_t dst = dp(s, s->x);
            uint8_t r = alu(s, row, rd(s, dst), src);
            if (row != 0x6)
                wr(s, dst, r);
            return n;
        }
        }
    }
    if (hi <= 0xB && lo == 0xB) {   /* ASL/ROL/LSR/ROR/DEC/INC dp (even) and dp+X (odd) */
        uint8_t off = fetch(s);
        uint16_t addr = dp(s, (hi & 1) ? (uint8_t)(off + s->x) : off);
        wr(s, addr, modify(s, hi & ~1, rd(s, addr)));
        return n;
    }
    if (hi <= 0xB && lo == 0xC) {   /* ... abs (even) and A (odd) */
        if (hi & 1) {
            s->a = modify(s, hi & ~1, s->a);
        } else {
            uint16_t addr = fetch16(s);
            wr(s, addr, modify(s, hi, rd(s, addr)));
        }
        return n;
    }

    switch (op) {
    /* Branches and flow */
    case 0x10: BRANCH(!flag(s, SPC_FLAG_N)); return n;
    case 0x30: BRANCH(flag(s, SPC_FLAG_N)); return n;
    case 0x50: BRANCH(!flag(s, SPC_FLAG_V)); return n;
    case 0x70: BRANCH(flag(s, SPC_FLAG_V)); return n;
    case 0x90: BRANCH(!flag(s, SPC_FLAG_C)); return n;
    case 0xB0: BRANCH(flag(s, SPC_FLAG_C)); return n;
    case 0xD0: BRANCH(!flag(s, SPC_FLAG_Z)); return n;
    case 0xF0: BRANCH(flag(s, SPC_FLAG_Z)); return n;
    case 0x2F: BRANCH(true); n -= 2; return n;   /* BRA: always taken, counted in the table */
    case 0x2E: {   /* CBNE dp,rel */
        uint8_t v = rd(s, dp(s, fetch(s)));
        BRANCH(s->a != v);
        return n;
    }
    case 0xDE: {   /* CBNE dp+X,rel */
        uint8_t v = rd(s, dp(s, (uint8_t)(fetch(s) + s->x)));
        BRANCH(s->a != v);
        return n;
    }
    case 0x6E: {   /* DBNZ dp,rel */
        uint16_t addr = dp(s, fetch(s));
        uint8_t v = (uint8_t)(rd(s, addr) - 1);
        wr(s, addr, v);
        BRANCH(v != 0);
        return n;
    }
    case 0xFE: s->y--; BRANCH(s->y != 0); return n;   /* DBNZ Y,rel */
    case 0x5F: s->pc = fetch16(s); return n;          /* JMP abs */
    case 0x1F: {                                      /* JMP [abs+X] */
        uint16_t p = (uint16_t)(fetch16(s) + s->x);
        s->pc = rd(s, p) | (uint16_t)rd(s, (uint16_t)(p + 1)) << 8;
        return n;
    }
    case 0x3F: {   /* CALL abs */
        uint16_t t = fetch16(s);
        push(s, s->pc >> 8);
        push(s, s->pc & 0xFF);
        s->pc = t;
        return n;
    }
    case 0x4F: {   /* PCALL up */
        uint8_t t = fetch(s);
        push(s, s->pc >> 8);
        push(s, s->pc & 0xFF);
        s->pc = 0xFF00 | t;
        return n;
    }
    case 0x0F:   /* BRK */
        push(s, s->pc >> 8);
        push(s, s->pc & 0xFF);
        push(s, s->psw);
        s->psw = (s->psw | SPC_FLAG_B) & ~SPC_FLAG_I;
        s->pc = rd(s, 0xFFDE) | (uint16_t)rd(s, 0xFFDF) << 8;
        return n;
    case 0x6F: {   /* RET */
        uint16_t lo8 = pull(s);
        s->pc = lo8 | (uint16_t)pull(s) << 8;
        return n;
    }
    case 0x7F: {   /* RETI */
        s->psw = pull(s);
        uint16_t lo8 = pull(s);
        s->pc = lo8 | (uint16_t)pull(s) << 8;
        return n;
    }

    /* Flags */
    case 0x20: s->psw &= ~SPC_FLAG_P; return n;
    case 0x40: s->psw |= SPC_FLAG_P; return n;
    case 0x60: s->psw &= ~SPC_FLAG_C; return n;
    case 0x80: s->psw |= SPC_FLAG_C; return n;
    case 0xA0: s->psw |= SPC_FLAG_I; return n;
    case 0xC0: s->psw &= ~SPC_FLAG_I; return n;
    case 0xE0: s->psw &= ~(SPC_FLAG_V | SPC_FLAG_H); return n;
    case 0xED: s->psw ^= SPC_FLAG_C; return n;

    /* Moves into A/X/Y set N and Z; moves into memory don't touch flags. */
    case 0xC4: wr(s, dp(s, fetch(s)), s->a); return n;
    case 0xD4: wr(s, dp(s, (uint8_t)(fetch(s) + s->x)), s->a); return n;
    case 0xC5: wr(s, fetch16(s), s->a); return n;
    case 0xD5: wr(s, (uint16_t)(fetch16(s) + s->x), s->a); return n;
    case 0xC6: wr(s, dp(s, s->x), s->a); return n;
    case 0xD6: wr(s, (uint16_t)(fetch16(s) + s->y), s->a); return n;
    case 0xC7: {
        uint8_t off = fetch(s);
        wr(s, read_dp_word(s, (uint8_t)(off + s->x)), s->a);
        return n;
    }
    case 0xD7: {
        uint8_t off = fetch(s);
        wr(s, (uint16_t)(read_dp_word(s, off) + s->y), s->a);
        return n;
    }
    case 0xE4: s->a = set_nz(s, rd(s, dp(s, fetch(s)))); return n;
    case 0xF4: s->a = set_nz(s, rd(s, dp(s, (uint8_t)(fetch(s) + s->x)))); return n;
    case 0xE5: s->a = set_nz(s, rd(s, fetch16(s))); return n;
    case 0xF5: s->a = set_nz(s, rd(s, (uint16_t)(fetch16(s) + s->x))); return n;
    case 0xE6: s->a = set_nz(s, rd(s, dp(s, s->x))); return n;
    case 0xF6: s->a = set_nz(s, rd(s, (uint16_t)(fetch16(s) + s->y))); return n;
    case 0xE7: {
        uint8_t off = fetch(s);
        s->a = set_nz(s, rd(s, read_dp_word(s, (uint8_t)(off + s->x))));
        return n;
    }
    case 0xF7: {
        uint8_t off = fetch(s);
        s->a = set_nz(s, rd(s, (uint16_t)(read_dp_word(s, off) + s->y)));
        return n;
    }
    case 0xE8: s->a = set_nz(s, fetch(s)); return n;
    case 0xCD: s->x = set_nz(s, fetch(s)); return n;
    case 0x8D: s->y = set_nz(s, fetch(s)); return n;
    case 0xF8: s->x = set_nz(s, rd(s, dp(s, fetch(s)))); return n;
    case 0xF9: s->x = set_nz(s, rd(s, dp(s, (uint8_t)(fetch(s) + s->y)))); return n;
    case 0xE9: s->x = set_nz(s, rd(s, fetch16(s))); return n;
    case 0xEB: s->y = set_nz(s, rd(s, dp(s, fetch(s)))); return n;
    case 0xFB: s->y = set_nz(s, rd(s, dp(s, (uint8_t)(fetch(s) + s->x)))); return n;
    case 0xEC: s->y = set_nz(s, rd(s, fetch16(s))); return n;
    case 0xD8: wr(s, dp(s, fetch(s)), s->x); return n;
    case 0xD9: wr(s, dp(s, (uint8_t)(fetch(s) + s->y)), s->x); return n;
    case 0xC9: wr(s, fetch16(s), s->x); return n;
    case 0xCB: wr(s, dp(s, fetch(s)), s->y); return n;
    case 0xDB: wr(s, dp(s, (uint8_t)(fetch(s) + s->x)), s->y); return n;
    case 0xCC: wr(s, fetch16(s), s->y); return n;
    case 0x8F: {   /* MOV dp,#imm */
        uint8_t imm = fetch(s);
        wr(s, dp(s, fetch(s)), imm);
        return n;
    }
    case 0xFA: {   /* MOV dp,dp */
        uint8_t v = rd(s, dp(s, fetch(s)));
        wr(s, dp(s, fetch(s)), v);
        return n;
    }
    case 0xAF: wr(s, dp(s, s->x), s->a); s->x++; return n;             /* MOV (X)+,A */
    case 0xBF: s->a = set_nz(s, rd(s, dp(s, s->x))); s->x++; return n;  /* MOV A,(X)+ */
    case 0x5D: s->x = set_nz(s, s->a); return n;
    case 0x7D: s->a = set_nz(s, s->x); return n;
    case 0xDD: s->a = set_nz(s, s->y); return n;
    case 0xFD: s->y = set_nz(s, s->a); return n;
    case 0x9D: s->x = set_nz(s, s->sp); return n;
    case 0xBD: s->sp = s->x; return n;

    /* Compares on X and Y */
    case 0xC8: op_cmp(s, s->x, fetch(s)); return n;
    case 0x3E: op_cmp(s, s->x, rd(s, dp(s, fetch(s)))); return n;
    case 0x1E: op_cmp(s, s->x, rd(s, fetch16(s))); return n;
    case 0xAD: op_cmp(s, s->y, fetch(s)); return n;
    case 0x7E: op_cmp(s, s->y, rd(s, dp(s, fetch(s)))); return n;
    case 0x5E: op_cmp(s, s->y, rd(s, fetch16(s))); return n;

    /* Register increments */
    case 0x1D: s->x = set_nz(s, (uint8_t)(s->x - 1)); return n;
    case 0x3D: s->x = set_nz(s, (uint8_t)(s->x + 1)); return n;
    case 0xDC: s->y = set_nz(s, (uint8_t)(s->y - 1)); return n;
    case 0xFC: s->y = set_nz(s, (uint8_t)(s->y + 1)); return n;

    /* Stack */
    case 0x0D: push(s, s->psw); return n;
    case 0x2D: push(s, s->a); return n;
    case 0x4D: push(s, s->x); return n;
    case 0x6D: push(s, s->y); return n;
    case 0x8E: s->psw = pull(s); return n;
    case 0xAE: s->a = pull(s); return n;
    case 0xCE: s->x = pull(s); return n;
    case 0xEE: s->y = pull(s); return n;

    /* Absolute bit operations: operand is abs (13 bits) | bit << 13 */
    case 0x0E:
    case 0x4E: {   /* TSET1 abs / TCLR1 abs */
        uint16_t addr = fetch16(s);
        uint8_t v = rd(s, addr);
        set_nz(s, (uint8_t)(s->a - v));
        wr(s, addr, op == 0x0E ? (v | s->a) : (v & ~s->a));
        return n;
    }
    case 0x0A: case 0x2A: case 0x4A: case 0x6A: case 0x8A: case 0xAA: case 0xCA: case 0xEA: {
        uint16_t w = fetch16(s);
        uint16_t addr = w & 0x1FFF;
        uint8_t bit = 1 << (w >> 13);
        uint8_t v = rd(s, addr);
        bool b = v & bit;
        bool c = flag(s, SPC_FLAG_C);
        switch (op) {
        case 0x0A: set_flag(s, SPC_FLAG_C, c | b); break;    /* OR1 C,m.b */
        case 0x2A: set_flag(s, SPC_FLAG_C, c | !b); break;   /* OR1 C,/m.b */
        case 0x4A: set_flag(s, SPC_FLAG_C, c & b); break;    /* AND1 C,m.b */
        case 0x6A: set_flag(s, SPC_FLAG_C, c & !b); break;   /* AND1 C,/m.b */
        case 0x8A: set_flag(s, SPC_FLAG_C, c ^ b); break;    /* EOR1 C,m.b */
        case 0xAA: set_flag(s, SPC_FLAG_C, b); break;        /* MOV1 C,m.b */
        case 0xCA: wr(s, addr, c ? (v | bit) : (v & ~bit)); break;   /* MOV1 m.b,C */
        case 0xEA: wr(s, addr, v ^ bit); break;              /* NOT1 m.b */
        }
        return n;
    }

    /* 16-bit operations on YA and direct-page words */
    case 0x1A:
    case 0x3A: {   /* DECW dp / INCW dp */
        uint8_t off = fetch(s);
        uint16_t v = (uint16_t)(read_dp_word(s, off) + (op == 0x3A ? 1 : -1));
        wr(s, dp(s, off), v & 0xFF);
        wr(s, dp(s, (uint8_t)(off + 1)), v >> 8);
        set_nz16(s, v);
        return n;
    }
    case 0x5A: {   /* CMPW YA,dp */
        uint16_t v = read_dp_word(s, fetch(s));
        int z = ((s->y << 8) | s->a) - v;
        set_flag(s, SPC_FLAG_C, z >= 0);
        set_nz16(s, (uint16_t)z);
        return n;
    }
    case 0x7A:
    case 0x9A: {   /* ADDW YA,dp / SUBW YA,dp */
        uint16_t v = read_dp_word(s, fetch(s));
        if (op == 0x7A) {
            s->psw &= ~SPC_FLAG_C;
            s->a = op_adc(s, s->a, v & 0xFF);
            s->y = op_adc(s, s->y, v >> 8);
        } else {
            s->psw |= SPC_FLAG_C;
            s->a = op_sbc(s, s->a, v & 0xFF);
            s->y = op_sbc(s, s->y, v >> 8);
        }
        set_flag(s, SPC_FLAG_Z, s->a == 0 && s->y == 0);
        return n;
    }
    case 0xBA: {   /* MOVW YA,dp */
        uint16_t v = read_dp_word(s, fetch(s));
        s->a = v & 0xFF;
        s->y = v >> 8;
        set_nz16(s, v);
        return n;
    }
    case 0xDA: {   /* MOVW dp,YA */
        uint8_t off = fetch(s);
        wr(s, dp(s, off), s->a);
        wr(s, dp(s, (uint8_t)(off + 1)), s->y);
        return n;
    }
    case 0xCF: {   /* MUL YA */
        uint16_t ya = (uint16_t)(s->y * s->a);
        s->a = ya & 0xFF;
        s->y = ya >> 8;
        set_nz(s, s->y);
        return n;
    }
    case 0x9E: {   /* DIV YA,X: quotient in A, remainder in Y */
        uint16_t ya = (uint16_t)(s->y << 8 | s->a);
        set_flag(s, SPC_FLAG_H, (s->y & 0x0F) >= (s->x & 0x0F));
        set_flag(s, SPC_FLAG_V, s->y >= s->x);
        if (s->y < (s->x << 1)) {
            s->a = (uint8_t)(ya / s->x);
            s->y = (uint8_t)(ya % s->x);
        } else {
            /* The quotient doesn't fit in V:A; the S-SMP's divider produces this. */
            s->a = (uint8_t)(255 - (ya - (s->x << 9)) / (256 - s->x));
            s->y = (uint8_t)(s->x + (ya - (s->x << 9)) % (256 - s->x));
        }
        set_nz(s, s->a);
        return n;
    }
    case 0xDF:   /* DAA A */
        if (flag(s, SPC_FLAG_C) || s->a > 0x99) {
            s->a += 0x60;
            s->psw |= SPC_FLAG_C;
        }
        if (flag(s, SPC_FLAG_H) || (s->a & 0x0F) > 0x09)
            s->a += 0x06;
        set_nz(s, s->a);
        return n;
    case 0xBE:   /* DAS A */
        if (!flag(s, SPC_FLAG_C) || s->a > 0x99) {
            s->a -= 0x60;
            s->psw &= ~SPC_FLAG_C;
        }
        if (!flag(s, SPC_FLAG_H) || (s->a & 0x0F) > 0x09)
            s->a -= 0x06;
        set_nz(s, s->a);
        return n;
    case 0x9F: s->a = set_nz(s, (uint8_t)(s->a >> 4 | s->a << 4)); return n;   /* XCN A */

    case 0x00: return n;   /* NOP */
    case 0xEF:             /* SLEEP */
    case 0xFF:             /* STOP */
        s->halted = true;
        return n;
    }
#undef BRANCH
    return n;
}

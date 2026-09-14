/* Bank C3: the general-purpose decompressor used for scripts, graphics, maps
 * and the code blocks the game runs from RAM. */
#include "natives.h"

#include "port/machine.h"

/* Direct-page variables, relative to $0300 (the routine sets D = $0300). */
enum {
    DP_BASE = 0x0300,
    V_SRC = 0x00,        /* 24-bit source pointer (in) */
    V_DST = 0x03,        /* 24-bit destination (in) */
    V_OUT_LEN = 0x06,    /* 16-bit decompressed length (out) */
    V_SEG_END = 0x09,    /* 16-bit source offset where the current segment ends */
    V_BITS = 0x0B,       /* control bits left in the current control byte */
    V_CTRL = 0x0D,       /* remaining control bits */
    V_COPY_LEN = 0x0F,   /* 16-bit back-reference count minus one; high byte cleared on entry */
    V_TEMP = 0x15,       /* 16-bit scratch: distance, then saved source offset */
};

static uint8_t dp_read8(Snes *s, uint8_t v) { return m_read8(s, (uint32_t)DP_BASE + v); }
static uint16_t dp_read16(Snes *s, uint8_t v) { return m_read16(s, (uint32_t)DP_BASE + v); }
static void dp_write8(Snes *s, uint8_t v, uint8_t x) { m_write8(s, (uint32_t)DP_BASE + v, x); }
static void dp_write16(Snes *s, uint8_t v, uint16_t x) { m_write16(s, (uint32_t)DP_BASE + v, x); }

/* Source bytes are addressed as data bank + offset + index, carrying into the
 * next bank like absolute indexed addressing does. */
static uint8_t src8(Snes *s, uint16_t x, uint16_t offset)
{
    return m_read8(s, (((uint32_t)s->cpu.db << 16) | offset) + x);
}

static uint16_t src16(Snes *s, uint16_t x, uint16_t offset)
{
    uint32_t addr = (((uint32_t)s->cpu.db << 16) | offset) + x;
    return (uint16_t)(m_read8(s, addr) | m_read8(s, addr + 1) << 8);
}

static void emit_literal(Snes *s, uint8_t value) { m_write8(s, 0x002180, value); }

/* $C3:0557, entered through the jump at $C3:0002.
 *
 * Stream format (source at $0300, a 24-bit pointer):
 *   u16 segment length; the first segment ends that many bytes after it.
 *   Then control bytes, each followed by the items it describes. Control bits
 *   are consumed LSB first: 0 = one literal byte, 1 = a 2-byte back-reference.
 *   A control byte of 0 means eight literals follow.
 *   Where a segment ends, a header byte follows: its low 6 bits give how many
 *   bits of the next control byte are used (0 ends the stream), then a u16
 *   gives the next segment's end relative to the source pointer.
 *   Back-references are 16-bit: distance in the low 12 bits and length-2 in the
 *   top 4, or, if the first segment header has either of its top two bits set,
 *   distance in the low 11 bits and length-2 in the top 5. A reference copies
 *   length+1 bytes forward from `distance` bytes behind the output position.
 *
 * Output goes to the 24-bit destination at $0303: literals through the $2180
 * work RAM port, back-references directly within bank $7E or $7F (bit 0 of the
 * destination bank). The decompressed length is left at $0306. All registers
 * are preserved; the direct-page scratch variables, stack bytes below the
 * saved registers, and the work RAM port address are left as the original
 * leaves them. */
void c3_decompress(Snes *s)
{
    Cpu *c = &s->cpu;

    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);

    uint16_t src_base = dp_read16(s, V_SRC);
    uint32_t src_ptr = (uint32_t)dp_read8(s, V_SRC + 2) << 16 | src_base;
    uint16_t x = (uint16_t)(src_base + 2);
    uint16_t first_len = (uint16_t)(m_read8(s, src_ptr) | m_read8(s, (src_ptr + 1) & 0xFFFFFF) << 8);
    dp_write16(s, V_SEG_END, (uint16_t)(x + first_len));

    uint16_t y = dp_read16(s, V_DST);
    m_write16(s, 0x002181, y);
    dp_write8(s, V_COPY_LEN + 1, 0x00);

    /* Data bank := source bank, via push/pull (leaves it in the stack byte). */
    m_push8(s, dp_read8(s, V_SRC + 2));
    c->db = m_pull8(s);

    uint8_t first_header = m_read8(s, (uint32_t)c->db << 16 | dp_read16(s, V_SEG_END));
    bool wide_length = first_header & 0xC0;
    uint8_t dst_bank = dp_read8(s, V_DST + 2);
    m_write8(s, 0x002183, dst_bank);
    uint32_t copy_bank = (dst_bank & 1) ? 0x7F0000 : 0x7E0000;

    dp_write8(s, V_BITS, 8);
    for (;;) {
        if (x == dp_read16(s, V_SEG_END)) {
            uint8_t header = src8(s, x, 0) & 0x3F;
            if (header == 0)
                break;
            dp_write8(s, V_BITS, header);
            dp_write16(s, V_SEG_END, (uint16_t)(src16(s, x, 1) + dp_read16(s, V_SRC)));
            x += 3;
            continue;
        }

        uint8_t control = src8(s, x, 0);
        if (control == 0) {
            for (uint16_t i = 1; i <= 8; i++)
                emit_literal(s, src8(s, x, i));
            x += 9;
            y += 8;
            continue;
        }
        x++;
        bool reference = control & 1;
        dp_write8(s, V_CTRL, control >> 1);

        for (;;) {
            if (!reference) {
                emit_literal(s, src8(s, x, 0));
                y++;
                x++;
            } else {
                uint8_t hi = src8(s, x, 1);
                dp_write8(s, V_COPY_LEN, (uint8_t)((wide_length ? hi >> 3 : hi >> 4) + 2));
                uint16_t distance = src16(s, x, 0) & (wide_length ? 0x07FF : 0x0FFF);
                dp_write16(s, V_TEMP, distance);
                uint16_t from = (uint16_t)(y - distance);
                dp_write16(s, V_TEMP, x);

                m_push8(s, c->db);
                for (uint32_t n = (uint32_t)dp_read16(s, V_COPY_LEN) + 1; n > 0; n--) {
                    m_write8(s, copy_bank | y, m_read8(s, copy_bank | from));
                    from++;
                    y++;
                }
                c->db = m_pull8(s);
                m_write16(s, 0x002181, y);
                x = (uint16_t)(dp_read16(s, V_TEMP) + 2);
            }

            uint8_t bits = (uint8_t)(dp_read8(s, V_BITS) - 1);
            dp_write8(s, V_BITS, bits);
            if (bits == 0) {
                dp_write8(s, V_BITS, 8);
                break;
            }
            uint8_t ctrl = dp_read8(s, V_CTRL);
            reference = ctrl & 1;
            dp_write8(s, V_CTRL, ctrl >> 1);
        }
    }

    dp_write16(s, V_OUT_LEN, (uint16_t)(y - dp_read16(s, V_DST)));

    c->y = m_pull16(s);
    c->x = m_pull16(s);
    c->a = m_pull16(s);
    c->p = m_pull8(s);
    /* Pulling D and the data bank after P sets N and Z again, so the caller
     * sees flags from the restored data bank rather than its own. */
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rtl(s);
}

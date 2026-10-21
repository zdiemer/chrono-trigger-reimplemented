/* ct_dev --apu-port-margins (apu_margins.h). */
#include "apu_margins.h"

/* The ports a message's last two bytes go on, which the SPC700 reads after it
 * echoes. */
enum { FIRST_PORT = 2, PORTS = 2, MARGIN_BITS = 12 };

static struct {
    void (*write)(void *ctx, uint8_t port, uint8_t v);   /* the bus's own */
    const Apu *apu;
    bool echoed;                     /* the driver has echoed a message */
    uint64_t echo_at;
    struct {
        bool read, written, written_first;
        uint64_t read_at;
        unsigned long measured, early;
        uint64_t least;
        unsigned long margins[MARGIN_BITS];   /* by bit length */
    } p[PORTS];
} m;

static void on_spc_write(const Apu *apu, uint8_t port)
{
    if (apu != m.apu || port != 0)
        return;
    m.echoed = true;
    m.echo_at = apu->spc_cycles;
    for (int k = 0; k < PORTS; k++)
        m.p[k].read = m.p[k].written = m.p[k].written_first = false;
}

static void on_spc_read(const Apu *apu, uint8_t port)
{
    int k = port - FIRST_PORT;
    if (apu != m.apu || !m.echoed || k < 0 || k >= PORTS || m.p[k].read)
        return;
    m.p[k].read = true;
    m.p[k].read_at = apu->spc_cycles;
    if (m.p[k].written_first)
        m.p[k].early++;   /* it reads the next message's byte */
}

static void on_cpu_write(void *ctx, uint8_t port, uint8_t v)
{
    Snes *s = ctx;
    int k = (port & 3) - FIRST_PORT;
    if (&s->apu == m.apu && s->apu.phase == APU_DRIVER && m.echoed && k >= 0 && k < PORTS && !m.p[k].written) {
        m.p[k].written = true;
        if (!m.p[k].read) {
            m.p[k].written_first = true;
        } else {
            uint64_t margin = s->apu.spc_cycles - m.p[k].read_at;
            if (!m.p[k].measured++ || margin < m.p[k].least)
                m.p[k].least = margin;
            unsigned bits = 0;
            for (uint64_t g = margin; g && bits < MARGIN_BITS - 1; g >>= 1)
                bits++;
            m.p[k].margins[bits]++;
        }
    }
    m.write(ctx, port, v);
}

void apu_margins_attach(Snes *s)
{
    m.write = s->bus.io.apu_write;
    m.apu = &s->apu;
    s->bus.io.apu_write = on_cpu_write;
    apu_port_read_observer = on_spc_read;
    apu_port_write_observer = on_spc_write;
}

/* An SPC700 cycle in master clocks, to a tenth: 21.477 MHz over 1.024 MHz. */
enum { MASTER_CLOCKS_PER_SPC_CYCLE_X10 = 210 };

void apu_margins_report(FILE *out)
{
    for (int k = 0; k < PORTS; k++) {
        fprintf(out, "apu port margins: port %d: %lu messages' bytes written after the SPC700 read the last, least "
                     "%llu SPC700 cycles after (about %llu master clocks); %lu written before it read them; "
                     "margins by bit length (0, 1, 2-3, 4-7, ...):",
                k + FIRST_PORT, m.p[k].measured, (unsigned long long)m.p[k].least,
                (unsigned long long)(m.p[k].least * MASTER_CLOCKS_PER_SPC_CYCLE_X10 / 10), m.p[k].early);
        for (int b = 0; b < MARGIN_BITS; b++)
            fprintf(out, " %lu", m.p[k].margins[b]);
        fprintf(out, "\n");
    }
}

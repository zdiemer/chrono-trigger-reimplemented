#include "system.h"

#include <string.h>

/* Average internal (non-bus) CPU cycles per instruction, in master clocks. */
#define INTERNAL_CLOCKS_PER_INSN 6
#define HDMA_HCLOCK 1104       /* ~dot 276 */
#define HBLANK_START_HCLOCK 1096
#define HBLANK_END_HCLOCK 16

/* ---- I/O routing ------------------------------------------------------------ */

static void sync_ppu_view(Snes *s)
{
    Ppu *p = &s->ppu;
    p->hcounter = (uint16_t)(s->hclock / 4);
    p->vcounter = s->vline;
    p->active_display = s->vline < SNES_VBLANK_LINE && !ppu_force_blank(p);
    p->wrio_latch_enable = s->bus.wrio & 0x80;
}

static uint8_t io_ppu_read(void *ctx, uint8_t reg, uint8_t open_bus)
{
    Snes *s = ctx;
    sync_ppu_view(s);
    return ppu_read(&s->ppu, reg, open_bus);
}

static void io_ppu_write(void *ctx, uint8_t reg, uint8_t v)
{
    Snes *s = ctx;
    sync_ppu_view(s);
    ppu_write(&s->ppu, reg, v);
}

static uint8_t io_apu_read(void *ctx, uint8_t port)
{
    Snes *s = ctx;
    return apu_read_port(&s->apu, port);
}

static void io_apu_write(void *ctx, uint8_t port, uint8_t v)
{
    Snes *s = ctx;
    apu_write_port(&s->apu, port, v);
}

void snes_init(Snes *s, const uint8_t *rom, size_t rom_size)
{
    memset(s, 0, sizeof *s);
    ppu_init(&s->ppu);
    apu_init(&s->apu);
    snes_bus_init(&s->bus, rom, rom_size, (SnesIo){ s, io_ppu_read, io_ppu_write, io_apu_read, io_apu_write });
    cpu_init(&s->cpu, (CpuBus){ &s->bus, snes_bus_read, snes_bus_write });
}

void snes_power_on(Snes *s)
{
    /* Power-on WRAM contents are undefined on hardware; match the trace oracle
     * (Snes9x fills with $55) so uninitialized bytes compare equal. */
    memset(s->bus.wram, 0x55, sizeof s->bus.wram);
    cpu_reset(&s->cpu);
    s->hclock = 0;
    s->vline = 0;
    s->bus.clocks = 0;
}

/* ---- HDMA ------------------------------------------------------------------- */

static uint8_t hdma_read(Snes *s, uint32_t addr) { return snes_bus_read(&s->bus, addr); }

static void hdma_load_entry(Snes *s, int ch)
{
    uint8_t *r = s->bus.dma[ch].reg;
    uint32_t bank = (uint32_t)r[4] << 16;
    uint16_t a2a = (uint16_t)(r[8] | r[9] << 8);
    r[10] = hdma_read(s, bank | a2a);
    a2a++;
    if (r[0] & 0x40) {
        r[5] = hdma_read(s, bank | a2a);
        r[6] = hdma_read(s, bank | (uint16_t)(a2a + 1));
        a2a += 2;
    }
    r[8] = a2a & 0xFF;
    r[9] = a2a >> 8;
    if (r[10] == 0)
        s->hdma[ch].active = false;
    s->hdma[ch].do_transfer = true;
}

static void hdma_init(Snes *s)
{
    for (int ch = 0; ch < 8; ch++) {
        uint8_t *r = s->bus.dma[ch].reg;
        s->hdma[ch].active = s->bus.hdmaen & (1 << ch);
        if (!s->hdma[ch].active)
            continue;
        r[8] = r[2];
        r[9] = r[3];
        hdma_load_entry(s, ch);
    }
}

static void hdma_line(Snes *s)
{
    static const uint8_t pattern[8][4] = {
        { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
        { 0, 1, 2, 3 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
    };
    static const uint8_t unit_len[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

    for (int ch = 0; ch < 8; ch++) {
        HdmaChannel *h = &s->hdma[ch];
        uint8_t *r = s->bus.dma[ch].reg;
        if (!h->active || !(s->bus.hdmaen & (1 << ch)))
            continue;
        if (h->do_transfer) {
            uint8_t mode = r[0] & 7;
            for (int i = 0; i < unit_len[mode]; i++) {
                uint32_t src;
                if (r[0] & 0x40) {
                    uint16_t das = (uint16_t)(r[5] | r[6] << 8);
                    src = (uint32_t)r[7] << 16 | das;
                    das++;
                    r[5] = das & 0xFF;
                    r[6] = das >> 8;
                } else {
                    uint16_t a2a = (uint16_t)(r[8] | r[9] << 8);
                    src = (uint32_t)r[4] << 16 | a2a;
                    a2a++;
                    r[8] = a2a & 0xFF;
                    r[9] = a2a >> 8;
                }
                snes_bus_write(&s->bus, 0x2100 | (uint8_t)(r[1] + pattern[mode][i]), hdma_read(s, src));
            }
        }
        r[10]--;
        h->do_transfer = r[10] & 0x80;
        if (!(r[10] & 0x7F))
            hdma_load_entry(s, ch);
    }
}

/* ---- timing ------------------------------------------------------------------- */

static void raise_irq(Snes *s) { s->bus.irq_flag = true; }

static void start_line(Snes *s)
{
    s->hdma_done_this_line = false;
    s->htimer_done_this_line = false;

    if (s->vline == 0) {
        s->bus.in_vblank = false;
        s->bus.nmi_flag = false;
        hdma_init(s);
    } else if (s->vline == SNES_VBLANK_LINE) {
        s->bus.in_vblank = true;
        s->bus.nmi_flag = true;
        if (s->bus.nmitimen & 0x80)
            s->cpu.nmi_pending = true;
        if (s->bus.nmitimen & 0x01)
            snes_bus_auto_joypad(&s->bus);
        ppu_vblank(&s->ppu);
        s->frame_done = true;
    }

    /* V-timer IRQ (without H) fires at the start of the matching line. */
    if ((s->bus.nmitimen & 0x30) == 0x20 && s->vline == s->bus.vtime)
        raise_irq(s);
}

static void advance(Snes *s, uint32_t clocks)
{
    s->master += clocks;
    apu_advance(&s->apu, clocks);

    uint32_t before = s->hclock;
    s->hclock += clocks;

    for (;;) {
        uint32_t end = s->hclock < SNES_CLOCKS_PER_LINE ? s->hclock : SNES_CLOCKS_PER_LINE;

        /* H-timer IRQ: every line (H only) or only on the V-timer line (H+V). */
        uint32_t htime = (uint32_t)s->bus.htime * 4;
        uint8_t timer_mode = s->bus.nmitimen & 0x30;
        if (!s->htimer_done_this_line && (timer_mode == 0x10 || (timer_mode == 0x30 && s->vline == s->bus.vtime)) &&
            before <= htime && htime < end) {
            s->htimer_done_this_line = true;
            raise_irq(s);
        }
        if (!s->hdma_done_this_line && before <= HDMA_HCLOCK && HDMA_HCLOCK < end) {
            s->hdma_done_this_line = true;
            if (s->vline < SNES_VBLANK_LINE) {
                if (s->on_line)
                    s->on_line(s->on_line_ctx, s, s->vline);
                hdma_line(s);
            }
        }
        s->bus.in_hblank = end >= HBLANK_START_HCLOCK || end < HBLANK_END_HCLOCK;

        if (s->hclock < SNES_CLOCKS_PER_LINE)
            break;
        s->hclock -= SNES_CLOCKS_PER_LINE;
        before = 0;
        s->vline = (uint16_t)((s->vline + 1) % SNES_LINES_PER_FRAME);
        start_line(s);
    }
}

void snes_run_frame(Snes *s, uint16_t pad1)
{
    s->bus.pad[0] = pad1;
    s->frame_done = false;
    while (!s->frame_done) {
        s->bus.clocks = 0;
        s->cpu.irq_line = s->bus.irq_flag;
        cpu_step(&s->cpu);
        if (s->bus.nmi_request) {
            s->bus.nmi_request = false;
            s->cpu.nmi_pending = true;
        }
        advance(s, s->bus.clocks + INTERNAL_CLOCKS_PER_INSN);
    }
    s->frame++;
}

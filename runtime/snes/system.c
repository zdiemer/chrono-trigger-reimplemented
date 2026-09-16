#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLOCKS_PER_IO_CYCLE 6   /* internal CPU cycles are fast cycles */
#define HDMA_HCLOCK 1104       /* ~dot 276 */
#define HBLANK_START_HCLOCK 1096
#define HBLANK_END_HCLOCK 16
/* The CPU pauses for 40 master clocks at hclock 538 of every line while work RAM
 * is refreshed (bsnes, Snes9x). */
#define REFRESH_HCLOCK 538
#define REFRESH_CLOCKS 40
/* HDMA pauses the CPU too: HDMA_SYNC_CLOCKS to stop and restart it on each line
 * with an active channel, HDMA_BYTE_CLOCKS per table byte. */
#define HDMA_SYNC_CLOCKS 18
#define HDMA_BYTE_CLOCKS 8

/* Interrupt timing (docs/RUNTIME.md, "Interrupt timing"). The 65C816 samples
 * its interrupt inputs at the start of each instruction's last cycle and acts
 * on them after the instruction. /NMI reaches the CPU 6 master clocks into the
 * first vblank line, after RDNMI is set. */
#define NMI_DELAY_CLOCKS 6

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

void snes_clone(Snes *dst, const Snes *src)
{
    memcpy(dst, src, sizeof *dst);
    dst->bus.io.ctx = dst;
    dst->cpu.bus.ctx = &dst->bus;
    dst->apu.spc.bus.ctx = &dst->apu;
    dst->bus.wram_written = NULL;
    dst->cpu.on_insn = NULL;
    dst->apu.on_insn = NULL;
    dst->on_line = NULL;
    dst->on_frame_top = NULL;
    dst->intercept = NULL;
    dst->on_miss = NULL;
}

#define STATE_MAGIC "CTSS"
#define STATE_VERSION 4u

int snes_state_save(const Snes *s, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    uint32_t header[2] = { STATE_VERSION, (uint32_t)sizeof *s };
    int ok = fwrite(STATE_MAGIC, 4, 1, f) == 1 && fwrite(header, sizeof header, 1, f) == 1 &&
             fwrite(s, sizeof *s, 1, f) == 1;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

int snes_state_load(Snes *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    char magic[4];
    uint32_t header[2];
    Snes *loaded = malloc(sizeof *loaded);
    int ok = loaded && fread(magic, 4, 1, f) == 1 && memcmp(magic, STATE_MAGIC, 4) == 0 &&
             fread(header, sizeof header, 1, f) == 1 && header[0] == STATE_VERSION &&
             header[1] == sizeof *s && fread(loaded, sizeof *loaded, 1, f) == 1;
    fclose(f);
    if (!ok) {
        free(loaded);
        return -1;
    }
    /* Everything but pointers comes from the file; pointers stay this process's. */
    const Snes keep = *s;
    *s = *loaded;
    free(loaded);
    s->bus.rom = keep.bus.rom;
    s->bus.rom_size = keep.bus.rom_size;
    s->bus.io = keep.bus.io;
    s->bus.wram_written = keep.bus.wram_written;
    s->cpu.bus = keep.cpu.bus;
    s->cpu.on_insn = keep.cpu.on_insn;
    s->cpu.on_insn_ctx = keep.cpu.on_insn_ctx;
    s->apu.spc.bus = keep.apu.spc.bus;
    s->apu.on_insn = keep.apu.on_insn;
    s->apu.on_insn_ctx = keep.apu.on_insn_ctx;
    s->on_line = keep.on_line;
    s->on_line_ctx = keep.on_line_ctx;
    s->on_frame_top = keep.on_frame_top;
    s->on_frame_top_ctx = keep.on_frame_top_ctx;
    s->intercept = keep.intercept;
    s->intercept_ctx = keep.intercept_ctx;
    s->interpreter = keep.interpreter;
    s->native_only = keep.native_only;
    s->on_miss = keep.on_miss;
    s->on_miss_ctx = keep.on_miss_ctx;
    s->halted = false;
    return 0;
}

void snes_power_on(Snes *s)
{
    /* Power-on RAM contents are undefined on hardware. Match the trace oracle's
     * conventions so bytes the game hasn't written yet compare equal: Snes9x
     * fills WRAM with $55 and SRAM (with no save file) with $60, and preloads
     * CGRAM with a gradient of 3-bit red, 3-bit green and 1-bit blue steps. */
    memset(s->bus.wram, 0x55, sizeof s->bus.wram);
    memset(s->bus.sram, 0x60, sizeof s->bus.sram);
    for (int i = 0; i < 256; i++) {
        uint16_t red = (uint16_t)((i & 7) << 2);
        uint16_t green = (uint16_t)(((i >> 3) & 7) << 2);
        uint16_t blue = (uint16_t)(((i >> 6) & 2) << 3);
        s->ppu.cgram[i] = (uint16_t)(red | green << 5 | blue << 10);
    }
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

/* HDMA's CPU time (Snes9x's accounting; bsnes's is close): starting and
 * stopping the CPU, 8 clocks per table byte read to load a channel's line
 * count and its indirect address, and per byte transferred. */
static uint32_t hdma_load_clocks(const uint8_t *r) { return HDMA_BYTE_CLOCKS * ((r[0] & 0x40) ? 3 : 1); }

/* Returns the CPU time it took. */
static uint32_t hdma_init(Snes *s)
{
    uint32_t clocks = 0;
    for (int ch = 0; ch < 8; ch++) {
        uint8_t *r = s->bus.dma[ch].reg;
        s->hdma[ch].active = s->bus.hdmaen & (1 << ch);
        if (!s->hdma[ch].active)
            continue;
        r[8] = r[2];
        r[9] = r[3];
        clocks += hdma_load_clocks(r);
        hdma_load_entry(s, ch);
    }
    return clocks ? clocks + HDMA_SYNC_CLOCKS : 0;
}

/* Returns the CPU time it took. */
static uint32_t hdma_line(Snes *s)
{
    static const uint8_t pattern[8][4] = {
        { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
        { 0, 1, 2, 3 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
    };
    static const uint8_t unit_len[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

    uint32_t clocks = 0;
    for (int ch = 0; ch < 8; ch++) {
        HdmaChannel *h = &s->hdma[ch];
        uint8_t *r = s->bus.dma[ch].reg;
        if (!h->active || !(s->bus.hdmaen & (1 << ch)))
            continue;
        if (!clocks)
            clocks = HDMA_SYNC_CLOCKS;
        if (h->do_transfer) {
            uint8_t mode = r[0] & 7;
            clocks += unit_len[mode] * HDMA_BYTE_CLOCKS;
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
        if (!(r[10] & 0x7F)) {
            clocks += hdma_load_clocks(r);
            hdma_load_entry(s, ch);
        } else {
            clocks += HDMA_BYTE_CLOCKS;
        }
    }
    return clocks;
}

/* ---- timing ------------------------------------------------------------------- */

/* Master clock at which the current line began (valid inside advance, where
 * hclock can run past the line's end). */
static inline uint64_t line_start(const Snes *s) { return s->master - s->hclock; }

/* Set TIMEUP at master clock `at`; the CPU sees the IRQ from then on. */
static void raise_irq(Snes *s, uint64_t at)
{
    s->bus.irq_flag = true;
    s->irq_at = at;
}

static void schedule_nmi(Snes *s, uint64_t at)
{
    if (!s->nmi_scheduled || at < s->nmi_at)
        s->nmi_at = at;
    s->nmi_scheduled = true;
}

/* Inside advance, where hclock has already moved past `hpos` on the current
 * line: the CPU pauses for `clocks` from hpos on, and the rest of the step's
 * time comes after the pause. Everything but the CPU keeps running. */
static void stall_at(Snes *s, uint32_t hpos, uint32_t clocks)
{
    if (!clocks)
        return;
    uint64_t at = line_start(s) + hpos;
    SnesStall *st = &s->stalls[s->stall_next];
    s->stall_next = (uint8_t)((s->stall_next + 1) % SNES_STALL_HISTORY);
    st->cpu_at = at - s->stall_total;
    st->total_before = s->stall_total;
    s->stall_total += clocks;
    s->master += clocks;
    s->hclock += clocks;
    apu_advance(&s->apu, clocks);
}

static void start_line(Snes *s)
{
    s->hdma_done_this_line = false;
    s->htimer_done_this_line = false;
    s->refresh_done_this_line = false;

    if (s->vline == 0) {
        s->bus.in_vblank = false;
        s->bus.nmi_flag = false;
        stall_at(s, 0, hdma_init(s));
        if (s->on_frame_top)
            s->on_frame_top(s->on_frame_top_ctx, s);
    } else if (s->vline == SNES_VBLANK_LINE) {
        s->bus.in_vblank = true;
        s->bus.nmi_flag = true;
        if (s->bus.nmitimen & 0x80)
            schedule_nmi(s, line_start(s) + NMI_DELAY_CLOCKS);
        if (s->bus.nmitimen & 0x01)
            snes_bus_auto_joypad(&s->bus);
        ppu_vblank(&s->ppu);
        s->frame_done = true;
    }

    /* V-timer IRQ (without H) fires at the start of the matching line. */
    if ((s->bus.nmitimen & 0x30) == 0x20 && s->vline == s->bus.vtime)
        raise_irq(s, line_start(s));
}

static inline uint32_t line_end(const Snes *s)
{
    return s->hclock < SNES_CLOCKS_PER_LINE ? s->hclock : SNES_CLOCKS_PER_LINE;
}

/* H-timer IRQ: every line (H only) or only on the V-timer line (H+V). */
static void check_htimer(Snes *s, uint32_t before, uint32_t end)
{
    uint32_t htime = (uint32_t)s->bus.htime * 4;
    uint8_t timer_mode = s->bus.nmitimen & 0x30;
    if (!s->htimer_done_this_line && (timer_mode == 0x10 || (timer_mode == 0x30 && s->vline == s->bus.vtime)) &&
        before <= htime && htime < end) {
        s->htimer_done_this_line = true;
        raise_irq(s, line_start(s) + htime);
    }
}

static void advance(Snes *s, uint32_t clocks)
{
    s->master += clocks;
    apu_advance(&s->apu, clocks);

    uint32_t before = s->hclock;
    s->hclock += clocks;

    for (;;) {
        uint32_t end = line_end(s);
        /* Line events in hclock order. The CPU pauses for work RAM refresh
         * and HDMA, which moves the end of the step (and the events after
         * them) later. */
        if (!s->refresh_done_this_line && before <= REFRESH_HCLOCK && REFRESH_HCLOCK < end) {
            s->refresh_done_this_line = true;
            stall_at(s, REFRESH_HCLOCK, REFRESH_CLOCKS);
            end = line_end(s);
        }
        check_htimer(s, before, end);
        if (!s->hdma_done_this_line && before <= HDMA_HCLOCK && HDMA_HCLOCK < end) {
            s->hdma_done_this_line = true;
            if (s->vline < SNES_VBLANK_LINE) {
                if (s->on_line)
                    s->on_line(s->on_line_ctx, s, s->vline);
                stall_at(s, HDMA_HCLOCK, hdma_line(s));
                end = line_end(s);
                check_htimer(s, before, end);
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

void snes_sync(Snes *s, uint32_t clocks)
{
    /* Nothing the timing model does between here and the horizon touches the
     * CPU's bus accounting, but keep it intact regardless: the step in progress
     * still owns it. */
    uint32_t bus_clocks = s->bus.clocks, access_start = s->bus.access_start;
    uint8_t open_bus = s->bus.open_bus;
    advance(s, clocks);
    s->bus.clocks = bus_clocks;
    s->bus.access_start = access_start;
    s->bus.open_bus = open_bus;
}

static inline uint64_t clock_before(uint64_t t) { return t ? t - 1 : 0; }

uint64_t snes_horizon(const Snes *s)
{
    uint64_t start = s->master - s->hclock;
    /* The line ends (and the next one's events happen) in the advance that
     * reaches hclock 1364, so an instruction may start up to 1363. */
    uint64_t h = start + SNES_CLOCKS_PER_LINE - 1;
    /* Line events at hclock P happen in the advance that goes past P. */
    if (s->vline < SNES_VBLANK_LINE && !s->hdma_done_this_line && s->hclock <= HDMA_HCLOCK)
        h = start + HDMA_HCLOCK;
    /* Pauses too: the time after them moves later. */
    if (!s->refresh_done_this_line && s->hclock <= REFRESH_HCLOCK)
        h = start + REFRESH_HCLOCK;
    uint8_t timer_mode = s->bus.nmitimen & 0x30;
    uint32_t htime = (uint32_t)s->bus.htime * 4;
    /* One clock early: the IRQ it raises can be recognized from the event's
     * own time on, which only a step ending after that time reaches. The same
     * goes for interrupts already raised and not yet recognized. */
    if (!s->htimer_done_this_line && (timer_mode == 0x10 || (timer_mode == 0x30 && s->vline == s->bus.vtime)) &&
        s->hclock <= htime && htime < SNES_CLOCKS_PER_LINE && clock_before(start + htime) < h)
        h = clock_before(start + htime);
    if (s->nmi_scheduled && clock_before(s->nmi_at) < h)
        h = clock_before(s->nmi_at);
    if (s->bus.irq_flag && s->poll_at < s->irq_at && clock_before(s->irq_at) < h)
        h = clock_before(s->irq_at);
    return h;
}

uint32_t snes_clocks_to_next_poll_change(const Snes *s)
{
    if (s->hclock < HBLANK_END_HCLOCK)
        return HBLANK_END_HCLOCK - s->hclock;
    if (s->hclock < HBLANK_START_HCLOCK)
        return HBLANK_START_HCLOCK - s->hclock;
    return SNES_CLOCKS_PER_LINE - s->hclock;
}

void snes_enter_vblank(Snes *s)
{
    s->hclock = 0;
    s->vline = SNES_VBLANK_LINE;
    s->nmi_scheduled = false;
    s->nmi_at = 0;
    s->irq_at = 0;
    s->poll_at = s->master;
    s->bus.irq_flag = false;
    s->bus.nmi_request = false;
    s->bus.in_hblank = false;
    s->cpu.nmi_pending = false;
    s->cpu.irq_line = false;
    for (int ch = 0; ch < 8; ch++)
        s->hdma[ch] = (HdmaChannel){ 0 };
    start_line(s);   /* line 225: RDNMI, the NMI, auto-joypad, OAM reload, frame_done */
}

/* After a step that ended now: the master clock at which its last cycle,
 * `last_cycle` clocks of CPU time long, began. Pauses of the CPU inside that
 * cycle delay its end, not its start. `step_stalls` is stall_total before the
 * step; when pauses inside the cycle outnumber the history (a long DMA
 * transfer, which counts as part of the last cycle), all of the step's are. */
static uint64_t last_cycle_start(const Snes *s, uint32_t last_cycle, uint64_t step_stalls)
{
    uint64_t cpu_start = s->master - s->stall_total - last_cycle;
    uint64_t inside_from = s->stall_total;   /* stall_total before the first pause inside the cycle */
    int i;
    for (i = 1; i <= SNES_STALL_HISTORY; i++) {
        const SnesStall *st = &s->stalls[(s->stall_next + SNES_STALL_HISTORY - i) % SNES_STALL_HISTORY];
        if (st->total_before < step_stalls || st->cpu_at <= cpu_start)
            break;
        inside_from = st->total_before;
    }
    if (i > SNES_STALL_HISTORY)
        inside_from = step_stalls;
    return s->master - last_cycle - (s->stall_total - inside_from);
}

void snes_run_frame(Snes *s, uint16_t pad1)
{
    if (s->halted)
        return;
    s->bus.pad[0] = pad1;
    s->frame_done = false;
    while (!s->frame_done) {
        s->bus.clocks = 0;
        s->cpu.io_cycles = 0;
        s->cpu.irq_line = s->bus.irq_flag && s->poll_at >= s->irq_at;

        /* Run one step and find where its last cycle begins. */
        uint32_t clocks = 0, last_cycle = CLOCKS_PER_IO_CYCLE;
        bool polled = true;
        s->intercept_last_cycle = 0;
        uint64_t step_stalls = s->stall_total;
        int32_t spent = s->intercept ? s->intercept(s->intercept_ctx, s) : -1;
        CpuStep kind = CPU_STEP_READY;
        if (spent < 0 && (s->native_only || !s->interpreter)) {
            /* Without the interpreter: what cpu_step does besides instructions
             * happens here, and an instruction no native code runs is a miss. */
            bool waiting = s->cpu.waiting;
            kind = cpu_service(&s->cpu);
            if (kind == CPU_STEP_READY && waiting && s->intercept)
                spent = s->intercept(s->intercept_ctx, s);   /* WAI ended by a masked IRQ: the next instruction runs now */
            if (kind == CPU_STEP_READY && spent < 0 &&
                !(s->on_miss && s->on_miss(s->on_miss_ctx, s) && s->interpreter)) {
                s->halted = true;
                return;
            }
        }
        if (spent >= 0) {
            clocks = (uint32_t)spent;   /* native code: its time ends in one poll */
            if (s->intercept_last_cycle)
                last_cycle = s->intercept_last_cycle;
        } else {
            if (kind == CPU_STEP_READY)
                kind = s->interpreter(&s->cpu);
            polled = kind != CPU_STEP_INTERRUPT;   /* entry sequences don't poll */
            clocks = s->bus.clocks + s->cpu.io_cycles * CLOCKS_PER_IO_CYCLE;
            if (s->bus.clocks && !s->cpu.last_cycle_io)
                last_cycle = s->bus.clocks - s->bus.access_start;
        }
        if (!clocks)
            clocks = CLOCKS_PER_IO_CYCLE;   /* WAI/STP idle, or native code that took no time */
        advance(s, clocks);

        /* Enabling NMIs while RDNMI is set raises /NMI when the write happens,
         * in the step's last cycle, so it's recognized one step later. */
        if (s->bus.nmi_request) {
            s->bus.nmi_request = false;
            schedule_nmi(s, s->master);
        }
        if (polled) {
            s->poll_at = last_cycle_start(s, last_cycle, step_stalls);
            if (s->nmi_scheduled && s->poll_at >= s->nmi_at) {
                s->nmi_scheduled = false;
                s->cpu.nmi_pending = true;
            }
        }
    }
    s->frame++;
}

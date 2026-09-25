/* Save states, field by field (docs/RUNTIME.md, "Save states").
 *
 *   "CTST"                       magic
 *   u16 version, u16 flags       STATE_VERSION, 0
 *   records, each:
 *     u8 name length (1-255), the name ("cpu.a", "apu.dsp.voice[3].env")
 *     u8 element size (1, 2, 4 or 8), u32 element count
 *     the elements, each little-endian
 *   u8 0                          end of the records
 *   u32 CRC-32 of everything before it
 *
 * A loader looks up every field its build has by name. A field the state
 * lacks (a build that predates it wrote the state) keeps its power-on value;
 * a record no field of this build has (a newer build wrote it, or the field
 * was dropped) is skipped. A record whose element size or count differs from
 * the field's, a value outside the range the emulation relies on, a damaged
 * or truncated file, a newer format version and a state from another ROM are
 * rejected, and the machine is left as it was. So a field whose meaning
 * changes gets a new name, and one that is added needs nothing but its line
 * in visit() below. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"

#define STATE_MAGIC "CTST"
#define STATE_VERSION 1u
/* The raw machine images written before this format. */
#define RAW_MAGIC "CTSS"

typedef enum { KIND_UINT, KIND_BOOL } Kind;

typedef struct {
    const uint8_t *name;
    size_t name_len;
    unsigned size;
    uint32_t count;
    const uint8_t *data;
    bool used;
} Record;

typedef enum { MODE_WRITE, MODE_READ, MODE_LIST } Mode;

typedef struct {
    Mode mode;
    /* MODE_WRITE */
    uint8_t *out;
    size_t len, cap;
    bool failed;
    /* MODE_READ */
    Record *records;
    size_t nrecords;
    char *err;
    size_t err_len;
    /* MODE_LIST */
    SnesStateField fn;
    void *ctx;
} Visitor;

static void fail(Visitor *v, const char *fmt, ...)
{
    if (v->failed)
        return;
    v->failed = true;
    if (v->err && v->err_len) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(v->err, v->err_len, fmt, ap);
        va_end(ap);
    }
}

/* ---- CRC-32 (the zlib polynomial) ------------------------------------------------ */

static uint32_t crc32_of(const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ---- bytes ---------------------------------------------------------------------- */

static void put_bytes(Visitor *v, const void *p, size_t n)
{
    if (v->failed)
        return;
    if (v->len + n > v->cap) {
        size_t cap = v->cap ? v->cap : 1 << 19;
        while (v->len + n > cap)
            cap *= 2;
        uint8_t *out = realloc(v->out, cap);
        if (!out) {
            v->failed = true;
            return;
        }
        v->out = out;
        v->cap = cap;
    }
    memcpy(v->out + v->len, p, n);
    v->len += n;
}

static void put_le(Visitor *v, uint64_t x, unsigned size)
{
    uint8_t b[8];
    for (unsigned i = 0; i < size; i++)
        b[i] = (uint8_t)(x >> (8 * i));
    put_bytes(v, b, size);
}

static uint64_t get_le(const uint8_t *p, unsigned size)
{
    uint64_t x = 0;
    for (unsigned i = 0; i < size; i++)
        x |= (uint64_t)p[i] << (8 * i);
    return x;
}

/* An integer (or bool) field in memory, as an unsigned value of its size. */
static uint64_t load_elem(const void *p, unsigned size)
{
    switch (size) {
    case 1: return *(const uint8_t *)p;
    case 2: return *(const uint16_t *)p;
    case 4: return *(const uint32_t *)p;
    default: return *(const uint64_t *)p;
    }
}

static void store_elem(void *p, unsigned size, uint64_t x)
{
    switch (size) {
    case 1: *(uint8_t *)p = (uint8_t)x; break;
    case 2: *(uint16_t *)p = (uint16_t)x; break;
    case 4: *(uint32_t *)p = (uint32_t)x; break;
    default: *(uint64_t *)p = x; break;
    }
}

/* ---- the fields ------------------------------------------------------------------- */

/* One field: `count` elements of `size` bytes at p, integers of that size
 * (signed ones are written as their two's complement) or bools. */
static void field(Visitor *v, const char *name, void *p, unsigned size, uint32_t count, Kind kind)
{
    if (v->failed)
        return;
    if (v->mode == MODE_LIST) {
        v->fn(v->ctx, name, p, size, count);
        return;
    }
    if (v->mode == MODE_WRITE) {
        size_t n = strlen(name);
        put_le(v, n, 1);
        put_bytes(v, name, n);
        put_le(v, size, 1);
        put_le(v, count, 4);
        if (size == 1 && kind == KIND_UINT) {
            put_bytes(v, p, count);
        } else {
            for (uint32_t i = 0; i < count; i++) {
                uint64_t x = load_elem((uint8_t *)p + (size_t)i * size, size);
                put_le(v, kind == KIND_BOOL ? x != 0 : x, size);
            }
        }
        return;
    }
    size_t n = strlen(name);
    for (size_t r = 0; r < v->nrecords; r++) {
        Record *rec = &v->records[r];
        if (rec->name_len != n || memcmp(rec->name, name, n) != 0)
            continue;
        rec->used = true;
        if (rec->size != size || rec->count != count) {
            fail(v, "its field %s has %u elements of %u bytes; this build's has %u of %u", name,
                 (unsigned)rec->count, rec->size, (unsigned)count, size);
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            uint64_t x = get_le(rec->data + (size_t)i * size, size);
            if (kind == KIND_BOOL && x > 1) {
                fail(v, "its field %s isn't a bool", name);
                return;
            }
            store_elem((uint8_t *)p + (size_t)i * size, size, x);
        }
        return;
    }
    /* Not in the state: the field keeps its power-on value. */
}

#define U(v, name, x) field((v), (name), &(x), sizeof(x), 1, KIND_UINT)
#define B(v, name, x) field((v), (name), &(x), sizeof(x), 1, KIND_BOOL)
#define UA(v, name, a) field((v), (name), (a), sizeof((a)[0]), sizeof(a) / sizeof((a)[0]), KIND_UINT)

static void visit_cpu(Visitor *v, Cpu *c)
{
    U(v, "cpu.a", c->a);
    U(v, "cpu.x", c->x);
    U(v, "cpu.y", c->y);
    U(v, "cpu.s", c->s);
    U(v, "cpu.d", c->d);
    U(v, "cpu.pc", c->pc);
    U(v, "cpu.db", c->db);
    U(v, "cpu.pb", c->pb);
    U(v, "cpu.p", c->p);
    B(v, "cpu.e", c->e);
    B(v, "cpu.nmi_pending", c->nmi_pending);
    B(v, "cpu.irq_line", c->irq_line);
    B(v, "cpu.waiting", c->waiting);
    B(v, "cpu.stopped", c->stopped);
    U(v, "cpu.io_cycles", c->io_cycles);
    B(v, "cpu.last_cycle_io", c->last_cycle_io);
}

static void visit_bus(Visitor *v, SnesBus *b)
{
    UA(v, "bus.wram", b->wram);
    UA(v, "bus.sram", b->sram);
    U(v, "bus.open_bus", b->open_bus);
    U(v, "bus.clocks", b->clocks);
    U(v, "bus.access_start", b->access_start);
    U(v, "bus.dma_clocks", b->dma_clocks);
    U(v, "bus.nmitimen", b->nmitimen);
    U(v, "bus.wrio", b->wrio);
    U(v, "bus.wrmpya", b->wrmpya);
    U(v, "bus.wrmpyb", b->wrmpyb);
    U(v, "bus.wrdiv", b->wrdiv);
    U(v, "bus.rddiv", b->rddiv);
    U(v, "bus.rdmpy", b->rdmpy);
    U(v, "bus.htime", b->htime);
    U(v, "bus.vtime", b->vtime);
    U(v, "bus.hdmaen", b->hdmaen);
    B(v, "bus.fastrom", b->fastrom);
    B(v, "bus.nmi_flag", b->nmi_flag);
    B(v, "bus.irq_flag", b->irq_flag);
    B(v, "bus.in_vblank", b->in_vblank);
    B(v, "bus.in_hblank", b->in_hblank);
    B(v, "bus.nmi_request", b->nmi_request);
    UA(v, "bus.pad", b->pad);
    UA(v, "bus.joy", b->joy);
    U(v, "bus.latch", b->latch);
    UA(v, "bus.serial", b->serial);
    U(v, "bus.wram_port_addr", b->wram_port_addr);
    for (int i = 0; i < 8; i++) {
        char name[64];
        snprintf(name, sizeof name, "bus.dma[%d].reg", i);
        UA(v, name, b->dma[i].reg);
    }
    U(v, "bus.mdmaen", b->mdmaen);
}

static void visit_ppu(Visitor *v, Ppu *p)
{
    UA(v, "ppu.vram", p->vram);
    UA(v, "ppu.cgram", p->cgram);
    UA(v, "ppu.oam", p->oam);
    UA(v, "ppu.reg", p->reg);
    U(v, "ppu.vram_addr", p->vram_addr);
    U(v, "ppu.vram_prefetch", p->vram_prefetch);
    U(v, "ppu.cg_addr", p->cg_addr);
    B(v, "ppu.cg_second", p->cg_second);
    U(v, "ppu.cg_latch", p->cg_latch);
    U(v, "ppu.oam_reload", p->oam_reload);
    U(v, "ppu.oam_addr", p->oam_addr);
    U(v, "ppu.oam_latch", p->oam_latch);
    U(v, "ppu.bgofs_latch", p->bgofs_latch);
    U(v, "ppu.bghofs_latch", p->bghofs_latch);
    UA(v, "ppu.bg_hofs", p->bg_hofs);
    UA(v, "ppu.bg_vofs", p->bg_vofs);
    U(v, "ppu.m7_latch", p->m7_latch);
    U(v, "ppu.m7a", p->m7a);
    U(v, "ppu.m7b", p->m7b);
    U(v, "ppu.m7c", p->m7c);
    U(v, "ppu.m7d", p->m7d);
    U(v, "ppu.m7x", p->m7x);
    U(v, "ppu.m7y", p->m7y);
    U(v, "ppu.m7hofs", p->m7hofs);
    U(v, "ppu.m7vofs", p->m7vofs);
    U(v, "ppu.fixed_color", p->fixed_color);
    B(v, "ppu.range_over", p->range_over);
    B(v, "ppu.time_over", p->time_over);
    U(v, "ppu.vram_writes", p->vram_writes);
    U(v, "ppu.cgram_writes", p->cgram_writes);
    U(v, "ppu.oam_writes", p->oam_writes);
    U(v, "ppu.h_latch", p->h_latch);
    U(v, "ppu.v_latch", p->v_latch);
    B(v, "ppu.h_second", p->h_second);
    B(v, "ppu.v_second", p->v_second);
    B(v, "ppu.counters_latched", p->counters_latched);
    U(v, "ppu.ppu1_open_bus", p->ppu1_open_bus);
    U(v, "ppu.ppu2_open_bus", p->ppu2_open_bus);
    U(v, "ppu.hcounter", p->hcounter);
    U(v, "ppu.vcounter", p->vcounter);
    B(v, "ppu.active_display", p->active_display);
    B(v, "ppu.wrio_latch_enable", p->wrio_latch_enable);
}

static void visit_dsp(Visitor *v, Dsp *d)
{
    UA(v, "apu.dsp.regs", d->regs);
    for (int i = 0; i < 8; i++) {
        DspVoice *vo = &d->voice[i];
        char name[64];
#define VOICE(f) (snprintf(name, sizeof name, "apu.dsp.voice[%d]." #f, i), name)
        UA(v, VOICE(buf), vo->buf);
        U(v, VOICE(buf_pos), vo->buf_pos);
        U(v, VOICE(brr_offset), vo->brr_offset);
        U(v, VOICE(brr_addr), vo->brr_addr);
        U(v, VOICE(interp_pos), vo->interp_pos);
        U(v, VOICE(env), vo->env);
        U(v, VOICE(hidden_env), vo->hidden_env);
        U(v, VOICE(env_mode), vo->env_mode);
        U(v, VOICE(kon_delay), vo->kon_delay);
#undef VOICE
    }
    U(v, "apu.dsp.counter", d->counter);
    U(v, "apu.dsp.noise", d->noise);
    U(v, "apu.dsp.every_other", d->every_other);
    U(v, "apu.dsp.new_kon", d->new_kon);
    U(v, "apu.dsp.kon", d->kon);
    U(v, "apu.dsp.koff", d->koff);
    U(v, "apu.dsp.echo_esa", d->echo_esa);
    U(v, "apu.dsp.echo_flg", d->echo_flg);
    U(v, "apu.dsp.echo_offset", d->echo_offset);
    U(v, "apu.dsp.echo_length", d->echo_length);
    U(v, "apu.dsp.fir_pos", d->fir_pos);
    field(v, "apu.dsp.fir_hist", &d->fir_hist[0][0], sizeof d->fir_hist[0][0], 16, KIND_UINT);
    U(v, "apu.dsp.clock", d->clock);
    U(v, "apu.dsp.samples", d->samples);
    U(v, "apu.dsp.out_count", d->out_count);
    U(v, "apu.dsp.out_dropped", d->out_dropped);
    UA(v, "apu.dsp.out", d->out);
}

static void visit_apu(Visitor *v, Apu *a)
{
    UA(v, "apu.ram", a->ram);
    UA(v, "apu.cpu_in", a->cpu_in);
    UA(v, "apu.cpu_out", a->cpu_out);
    /* An enum, whose size is the compiler's: a state holds it as a byte. */
    if (v->mode == MODE_LIST) {
        v->fn(v->ctx, "apu.phase", &a->phase, sizeof a->phase, 1);
    } else {
        uint8_t phase = (uint8_t)a->phase;
        U(v, "apu.phase", phase);
        if (v->mode == MODE_READ)
            a->phase = (ApuPhase)phase;
    }
    U(v, "apu.write_ptr", a->write_ptr);
    U(v, "apu.expected", a->expected);
    U(v, "apu.last_port0", a->last_port0);
    U(v, "apu.ipl_busy", a->ipl_busy);
    U(v, "apu.ipl_poll_cycles", a->ipl_poll_cycles);
    U(v, "apu.ipl_byte_cycles", a->ipl_byte_cycles);
    U(v, "apu.spc.pc", a->spc.pc);
    U(v, "apu.spc.a", a->spc.a);
    U(v, "apu.spc.x", a->spc.x);
    U(v, "apu.spc.y", a->spc.y);
    U(v, "apu.spc.sp", a->spc.sp);
    U(v, "apu.spc.psw", a->spc.psw);
    B(v, "apu.spc.halted", a->spc.halted);
    U(v, "apu.control", a->control);
    U(v, "apu.dsp_addr", a->dsp_addr);
    visit_dsp(v, &a->dsp);
    for (int i = 0; i < 3; i++) {
        ApuTimer *t = &a->timer[i];
        char name[64];
#define TIMER(f) (snprintf(name, sizeof name, "apu.timer[%d]." #f, i), name)
        B(v, TIMER(enabled), t->enabled);
        U(v, TIMER(target), t->target);
        U(v, TIMER(stage), t->stage);
        U(v, TIMER(output), t->output);
        U(v, TIMER(divider), t->divider);
#undef TIMER
    }
    U(v, "apu.budget", a->budget);
    U(v, "apu.lead", a->lead);
    U(v, "apu.spc_cycles", a->spc_cycles);
}

/* Every field a state holds. Hooks, host pointers and the ROM are the
 * loading machine's own; `halted` is cleared by a load. */
static void visit(Visitor *v, Snes *s)
{
    visit_cpu(v, &s->cpu);
    visit_bus(v, &s->bus);
    visit_ppu(v, &s->ppu);
    visit_apu(v, &s->apu);
    U(v, "snes.hclock", s->hclock);
    U(v, "snes.vline", s->vline);
    U(v, "snes.master", s->master);
    U(v, "snes.frame", s->frame);
    B(v, "snes.frame_done", s->frame_done);
    U(v, "snes.poll_at", s->poll_at);
    U(v, "snes.nmi_at", s->nmi_at);
    B(v, "snes.nmi_scheduled", s->nmi_scheduled);
    U(v, "snes.irq_at", s->irq_at);
    U(v, "snes.stall_total", s->stall_total);
    for (int i = 0; i < SNES_STALL_HISTORY; i++) {
        char name[64];
        snprintf(name, sizeof name, "snes.stalls[%d].cpu_at", i);
        U(v, name, s->stalls[i].cpu_at);
        snprintf(name, sizeof name, "snes.stalls[%d].total_before", i);
        U(v, name, s->stalls[i].total_before);
    }
    U(v, "snes.stall_next", s->stall_next);
    for (int i = 0; i < 8; i++) {
        char name[64];
        snprintf(name, sizeof name, "snes.hdma[%d].active", i);
        B(v, name, s->hdma[i].active);
        snprintf(name, sizeof name, "snes.hdma[%d].do_transfer", i);
        B(v, name, s->hdma[i].do_transfer);
    }
    B(v, "snes.hdma_done_this_line", s->hdma_done_this_line);
    B(v, "snes.htimer_done_this_line", s->htimer_done_this_line);
    B(v, "snes.refresh_done_this_line", s->refresh_done_this_line);
    U(v, "snes.intercept_last_cycle", s->intercept_last_cycle);
}

/* What the emulation indexes with or loops on, so a state that passes its
 * checksum but holds nonsense is refused instead of run. */
static const char *out_of_range(const Snes *s)
{
    if (s->stall_next >= SNES_STALL_HISTORY)
        return "snes.stall_next";
    if (s->vline >= SNES_LINES_PER_FRAME)
        return "snes.vline";
    if (s->hclock >= SNES_CLOCKS_PER_LINE)
        return "snes.hclock";
    if (s->ppu.oam_addr > 0x3FF)
        return "ppu.oam_addr";
    if (s->ppu.oam_reload > 0x3FF)
        return "ppu.oam_reload";
    if ((unsigned)s->apu.phase > APU_DRIVER)
        return "apu.phase";
    if (s->apu.dsp.out_count > DSP_OUT_FRAMES)
        return "apu.dsp.out_count";
    if (s->apu.dsp.fir_pos > 7)
        return "apu.dsp.fir_pos";
    for (int i = 0; i < 8; i++) {
        if (s->apu.dsp.voice[i].env_mode > DSP_ENV_SUSTAIN)
            return "apu.dsp.voice[].env_mode";
    }
    return NULL;
}

/* ---- the file ------------------------------------------------------------------- */

void snes_state_fields(Snes *snes, SnesStateField fn, void *ctx)
{
    Visitor v = { .mode = MODE_LIST, .fn = fn, .ctx = ctx };
    visit(&v, snes);
}

uint8_t *snes_state_write(const Snes *snes, size_t *len)
{
    Visitor v = { .mode = MODE_WRITE };
    put_bytes(&v, STATE_MAGIC, 4);
    put_le(&v, STATE_VERSION, 2);
    put_le(&v, 0, 2);
    uint32_t rom_crc = crc32_of(snes->bus.rom, snes->bus.rom_size);
    field(&v, "rom.crc32", &rom_crc, 4, 1, KIND_UINT);
    visit(&v, (Snes *)snes);   /* only read in MODE_WRITE */
    put_le(&v, 0, 1);
    if (!v.failed)
        put_le(&v, crc32_of(v.out, v.len), 4);
    if (v.failed) {
        free(v.out);
        return NULL;
    }
    *len = v.len;
    return v.out;
}

/* Split the records out of a state that passed its checks. Returns the count,
 * or -1 if they don't fit the data. */
static long parse_records(const uint8_t *p, const uint8_t *end, Record **out)
{
    size_t cap = 256, n = 0;
    Record *recs = malloc(cap * sizeof *recs);
    if (!recs)
        return -1;
    for (;;) {
        if (p >= end)
            goto bad;
        size_t name_len = *p++;
        if (!name_len)
            break;
        if ((size_t)(end - p) < name_len + 5)
            goto bad;
        Record r = { .name = p, .name_len = name_len };
        p += name_len;
        r.size = *p++;
        r.count = (uint32_t)get_le(p, 4);
        p += 4;
        if ((r.size != 1 && r.size != 2 && r.size != 4 && r.size != 8) ||
            (uint64_t)r.size * r.count > (uint64_t)(end - p))
            goto bad;
        r.data = p;
        p += (size_t)r.size * r.count;
        for (size_t i = 0; i < n; i++) {
            if (recs[i].name_len == r.name_len && !memcmp(recs[i].name, r.name, r.name_len))
                goto bad;
        }
        if (n == cap) {
            cap *= 2;
            Record *grown = realloc(recs, cap * sizeof *recs);
            if (!grown)
                goto bad;
            recs = grown;
        }
        recs[n++] = r;
    }
    if (p != end)
        goto bad;
    *out = recs;
    return (long)n;
bad:
    free(recs);
    return -1;
}

int snes_state_read(Snes *s, const uint8_t *data, size_t len, char *err, size_t err_len)
{
    Visitor v = { .mode = MODE_READ, .err = err, .err_len = err_len };
    if (len >= 4 && !memcmp(data, RAW_MAGIC, 4)) {
        fail(&v, "it is a raw machine image from before versioned states, which only the build that wrote it "
                 "reads");
        return -1;
    }
    if (len < 13 || memcmp(data, STATE_MAGIC, 4) != 0) {
        fail(&v, "it isn't a state file");
        return -1;
    }
    unsigned version = (unsigned)get_le(data + 4, 2);
    if (version > STATE_VERSION || version == 0) {
        fail(&v, "it is state format %u, and this build reads up to %u", version, STATE_VERSION);
        return -1;
    }
    if (get_le(data + len - 4, 4) != crc32_of(data, len - 4)) {
        fail(&v, "it is damaged or incomplete (its checksum doesn't match)");
        return -1;
    }
    long n = parse_records(data + 8, data + len - 4, &v.records);
    if (n < 0) {
        fail(&v, "its records are malformed");
        return -1;
    }
    v.nrecords = (size_t)n;

    /* Fields the state lacks keep what a machine has at power-on. */
    Snes *tmp = malloc(sizeof *tmp);
    if (!tmp) {
        free(v.records);
        fail(&v, "out of memory");
        return -1;
    }
    snes_init(tmp, s->bus.rom, s->bus.rom_size);
    snes_power_on(tmp);
    uint32_t rom_crc = 0;
    field(&v, "rom.crc32", &rom_crc, 4, 1, KIND_UINT);
    if (!v.failed && rom_crc && rom_crc != crc32_of(s->bus.rom, s->bus.rom_size))
        fail(&v, "it was saved with another ROM (CRC-32 %08X)", (unsigned)rom_crc);
    visit(&v, tmp);
    const char *bad = v.failed ? NULL : out_of_range(tmp);
    if (bad)
        fail(&v, "its field %s is out of range", bad);
    free(v.records);
    if (v.failed) {
        free(tmp);
        return -1;
    }

    /* The machine's contents from the state; its ROM, hooks and host pointers stay. */
    const Snes keep = *s;
    *s = *tmp;
    free(tmp);
    s->bus.rom = keep.bus.rom;
    s->bus.rom_size = keep.bus.rom_size;
    s->bus.io = keep.bus.io;
    s->bus.wram_written = keep.bus.wram_written;
    s->bus.wram_scaffold = keep.bus.wram_scaffold;
    s->bus.scaffolding = keep.bus.scaffolding;
    s->cpu.bus = keep.cpu.bus;
    s->cpu.on_insn = keep.cpu.on_insn;
    s->cpu.on_insn_ctx = keep.cpu.on_insn_ctx;
    s->apu.spc.bus = keep.apu.spc.bus;
    s->apu.on_insn = keep.apu.on_insn;
    s->apu.on_insn_ctx = keep.apu.on_insn_ctx;
    s->apu.on_dsp_write = keep.apu.on_dsp_write;
    s->apu.host = keep.apu.host;
    s->apu.host_ctx = keep.apu.host_ctx;
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
    /* Write tracking isn't in the state: the epochs carry on from the machine's
     * own, and all of work RAM and VRAM counts as just written, so a machine
     * copy refreshed since the last write (shadow mode's) takes all of it. */
    s->bus.write_epoch = keep.bus.write_epoch;
    s->ppu.write_epoch = keep.ppu.write_epoch;
    snes_mark_all_written(s);
    return 0;
}

int snes_state_save(const Snes *s, const char *path)
{
    size_t len;
    uint8_t *data = snes_state_write(s, &len);
    FILE *f = data ? fopen(path, "wb") : NULL;
    int ok = f && fwrite(data, len, 1, f) == 1;
    if (f && fclose(f) != 0)
        ok = 0;
    free(data);
    return ok ? 0 : -1;
}

int snes_state_load(Snes *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    uint8_t *data = NULL;
    size_t len = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)n)) != NULL &&
            fread(data, 1, (size_t)n, f) == (size_t)n)
            len = (size_t)n;
    }
    fclose(f);
    int rc = len ? snes_state_read(s, data, len, NULL, 0) : -1;
    free(data);
    return rc;
}

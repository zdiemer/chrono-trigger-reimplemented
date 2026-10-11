#include "io_log.h"

#include <stdlib.h>
#include <string.h>

#include "platform/snes/backend.h"
#include "port/native.h"

typedef enum {
    ORIGIN_PORT_SERVICE,   /* a hand port, through a platform service call */
    ORIGIN_PORT_DIRECT,    /* a hand port touching the register itself */
    ORIGIN_IDLE_SERVICE,   /* an idle entry (a hand-written busy-wait pass), through a service call */
    ORIGIN_IDLE,           /* an idle entry touching the register itself */
    ORIGIN_GENERATED,      /* a generated block */
    ORIGIN_INTERPRETER,
    ORIGIN_HDMA,
    ORIGIN_KINDS
} OriginKind;

static const char *const kind_names[ORIGIN_KINDS] = {
    "port-service", "port-direct", "idle-service", "idle", "generated", "interpreter", "hdma",
};
static const char *const kind_titles[ORIGIN_KINDS] = {
    "hand ports, through services", "hand ports, direct", "idle entries, through services", "idle entries, direct",
    "generated blocks", "interpreter", "HDMA",
};

/* APU ports: $2140-$2143; apu-mirror: $2144-$217F, which reach the same four
 * ports (in the movies only as the low byte of 16-bit accesses aimed at the
 * work RAM port $2180, docs/PLATFORM.md P1). */
typedef enum {
    GROUP_PPU, GROUP_APU, GROUP_APU_MIRROR, GROUP_JOYPAD, GROUP_CPU, GROUP_MATH, GROUP_DMA_REG, GROUP_DMA_DATA, GROUPS
} Group;
static const char *const group_names[GROUPS] = { "ppu", "apu", "apu-mirror", "joypad", "cpu", "math", "dma-reg",
                                                 "dma-data" };

/* When in the frame an access happened, as the PPU sees it (docs/REIMPLEMENTATION.md,
 * "Where video writes land"): on a vblank line (225-261); on a picture line
 * (0-224) in forced blank ($2100 bit 7, as it was before the access); or on a
 * picture line with the display on, where ppu.c drops VRAM and OAM data writes
 * and every other register takes effect mid-picture. */
typedef enum { TIME_VBLANK, TIME_BLANK, TIME_VISIBLE, TIMES } TimeClass;
static const char *const time_titles[TIMES] = { "vblank", "forced blank", "visible" };
enum { LINE_VISIBLE, LINE_UNBLANK, LINE_KINDS };
#define AFFECTED_LINES (SNES_VBLANK_LINE + 1)

typedef struct {
    const char *origin;    /* entry name, or NULL (the pc, HDMA) */
    const char *call;      /* platform service call, or NULL */
    /* interpreter: the instruction; service call: its origin site; generated
     * block: the program counter a block keeps (where it started or resumed,
     * or the last branch it took) */
    uint32_t pc;
    uint16_t addr;
    uint8_t kind;
    uint8_t flags;         /* SNES_IO_* */
    uint64_t count;
    uint32_t first_frame;
    uint32_t entry;        /* hand ports and idle entries: the entry's address */
    uint64_t times[TIMES];
    uint16_t line_min, line_max;   /* of the accesses on picture lines */
    uint32_t first_picture_frame;
    /* By the first line the access can change the picture of (its own line,
     * or the next once its own has been drawn; 225 after the last), allocated
     * at the first: [LINE_VISIBLE] TIME_VISIBLE accesses; [LINE_UNBLANK]
     * writes that end forced blank with a nonzero brightness, so the picture
     * starts on that line. */
    uint32_t (*lines)[AFFECTED_LINES];
    bool used;
} Slot;

struct IoLog {
    Snes *s;
    uint32_t pc;
    bool hdma;
    uint64_t hdma_master;  /* the master clock at the line hook HDMA's writes follow */
    int drawn_line;        /* the line the line hook last drew */
    Slot *slots;
    size_t cap, used;
    uint64_t totals[ORIGIN_KINDS][GROUPS];
    uint64_t video[ORIGIN_KINDS][TIMES];   /* writes to the video registers, by when */
};

/* The registers whose writes change the picture: the PPU's ($2100-$2133) and
 * HDMAEN ($420C), which the frame's HDMA setup on line 0 reads. */
static bool video_register(uint16_t a) { return (a >= 0x2100 && a <= 0x2133) || a == 0x420C; }

static TimeClass time_class(const Snes *s)
{
    if (s->vline >= SNES_VBLANK_LINE)
        return TIME_VBLANK;
    return ppu_force_blank(&s->ppu) ? TIME_BLANK : TIME_VISIBLE;
}

static int group_of(uint16_t a, unsigned flags)
{
    if (flags & SNES_IO_DMA)
        return GROUP_DMA_DATA;
    if (a >= 0x2100 && a <= 0x213F)
        return GROUP_PPU;
    if (a >= 0x2140 && a <= 0x2143)
        return GROUP_APU;
    if (a >= 0x2144 && a <= 0x217F)
        return GROUP_APU_MIRROR;
    if (a == 0x4016 || a == 0x4017)
        return GROUP_JOYPAD;
    if ((a >= 0x4202 && a <= 0x4206) || (a >= 0x4214 && a <= 0x4217))
        return GROUP_MATH;
    if (a >= 0x4200 && a <= 0x421F)
        return GROUP_CPU;
    if (a >= 0x4300 && a <= 0x437F)
        return GROUP_DMA_REG;
    return -1;
}

static uint64_t slot_hash(const Slot *k)
{
    uint64_t h = 0xcbf29ce484222325ull;
    uint64_t parts[4] = { (uint64_t)(uintptr_t)k->origin, (uint64_t)(uintptr_t)k->call, k->pc,
                          (uint64_t)k->addr << 16 | k->kind << 8 | k->flags };
    for (int i = 0; i < 4; i++) {
        h ^= parts[i];
        h *= 0x100000001b3ull;
        h ^= h >> 29;
    }
    return h;
}

static bool slot_same(const Slot *a, const Slot *b)
{
    return a->origin == b->origin && a->call == b->call && a->pc == b->pc && a->addr == b->addr &&
           a->kind == b->kind && a->flags == b->flags;
}

static Slot *slot_find(IoLog *log, const Slot *key)
{
    size_t mask = log->cap - 1;
    for (size_t i = (size_t)slot_hash(key) & mask;; i = (i + 1) & mask) {
        Slot *sl = &log->slots[i];
        if (!sl->used || slot_same(sl, key))
            return sl;
    }
}

static void grow(IoLog *log)
{
    Slot *old = log->slots;
    size_t old_cap = log->cap;
    log->cap *= 2;
    log->slots = calloc(log->cap, sizeof *log->slots);
    for (size_t i = 0; i < old_cap; i++)
        if (old[i].used)
            *slot_find(log, &old[i]) = old[i];
    free(old);
}

static void observe(void *ctx, const SnesBus *bus, uint16_t addr, uint8_t value, unsigned flags)
{
    IoLog *log = ctx;
    if (bus != &log->s->bus)
        return;
    int group = group_of(addr, flags);
    if (group < 0)
        return;
    Slot key = { .addr = addr, .flags = (uint8_t)flags };
    const NativeEntry *e = native_running_entry();
    /* HDMA writes happen in the system's line events, which a generated block's
     * catch-up before its own I/O (rc_io_sync) can run: they are the accesses at
     * the line hook's master clock (HDMA's own pause moves the CPU's time on
     * before anything else runs). */
    if (log->hdma && log->s->master == log->hdma_master && (flags & (SNES_IO_WRITE | SNES_IO_DMA)) == SNES_IO_WRITE &&
        addr >= 0x2100 && addr <= 0x21FF) {
        key.kind = ORIGIN_HDMA;
    } else if (e && e->generated) {
        key.kind = ORIGIN_GENERATED;
        key.origin = e->name;
        key.pc = cpu_pc24(&log->s->cpu);
    } else if (e && e->idle) {
        key.entry = e->addr;
        key.origin = e->name;
        if (pf_snes_call.name) {
            key.kind = ORIGIN_IDLE_SERVICE;
            key.call = pf_snes_call.name;
            key.pc = pf_snes_call.site;
        } else {
            key.kind = ORIGIN_IDLE;
        }
    } else if (e) {
        key.entry = e->addr;
        key.origin = e->name;
        if (pf_snes_call.name) {
            key.kind = ORIGIN_PORT_SERVICE;
            key.call = pf_snes_call.name;
            key.pc = pf_snes_call.site;
        } else {
            key.kind = ORIGIN_PORT_DIRECT;
        }
    } else {
        key.kind = ORIGIN_INTERPRETER;
        key.pc = log->pc;
    }
    log->totals[key.kind][group]++;
    Slot *sl = slot_find(log, &key);
    if (!sl->used) {
        *sl = key;
        sl->used = true;
        sl->first_frame = log->s->frame;
        sl->line_min = UINT16_MAX;
        if (++log->used * 2 > log->cap) {
            grow(log);
            sl = slot_find(log, &key);
        }
    }
    sl->count++;
    TimeClass t = time_class(log->s);
    sl->times[t]++;
    if (t != TIME_VBLANK) {
        uint16_t line = log->s->vline;
        if (sl->line_min == UINT16_MAX)
            sl->first_picture_frame = log->s->frame;
        if (line < sl->line_min)
            sl->line_min = line;
        if (line > sl->line_max)
            sl->line_max = line;
        int which = t == TIME_VISIBLE ? LINE_VISIBLE : -1;
        if (t == TIME_BLANK && addr == 0x2100 && (flags & SNES_IO_WRITE) && !(value & 0x80) && (value & 0x0F))
            which = LINE_UNBLANK;
        if (which >= 0) {
            if (!sl->lines)
                sl->lines = calloc(LINE_KINDS, sizeof *sl->lines);
            sl->lines[which][line + (line == log->drawn_line)]++;
        }
    }
    if ((flags & SNES_IO_WRITE) && video_register(addr))
        log->video[key.kind][t]++;
}

IoLog *io_log_create(Snes *s)
{
    IoLog *log = calloc(1, sizeof *log);
    log->s = s;
    log->cap = 1u << 14;
    log->drawn_line = -1;
    log->slots = calloc(log->cap, sizeof *log->slots);
    snes_bus_io_observer = observe;
    snes_bus_io_observer_ctx = log;
    return log;
}

void io_log_note_instruction(IoLog *log, uint32_t pc)
{
    log->pc = pc;
    log->hdma = false;
}

void io_log_note_hdma(IoLog *log)
{
    /* HDMA writes this line only with a channel running, and then its pause
     * (at least HDMA_SYNC_CLOCKS) separates them from the CPU's next access. */
    const Snes *s = log->s;
    log->hdma = false;
    for (int ch = 0; ch < 8; ch++)
        if (s->hdma[ch].active && (s->bus.hdmaen & (1 << ch)))
            log->hdma = true;
    log->hdma_master = s->master;
    log->drawn_line = s->vline;
}

static int slot_order(const void *pa, const void *pb)
{
    const Slot *a = pa, *b = pb;
    if (a->kind != b->kind)
        return a->kind < b->kind ? -1 : 1;
    int c = strcmp(a->origin ? a->origin : "", b->origin ? b->origin : "");
    if (c)
        return c;
    if (a->pc != b->pc)
        return a->pc < b->pc ? -1 : 1;
    if (a->entry != b->entry)
        return a->entry < b->entry ? -1 : 1;
    c = strcmp(a->call ? a->call : "", b->call ? b->call : "");
    if (c)
        return c;
    if (a->addr != b->addr)
        return a->addr < b->addr ? -1 : 1;
    return (int)a->flags - (int)b->flags;
}

int io_log_write(IoLog *log, const char *path, FILE *summary)
{
    Slot *rows = malloc(sizeof *rows * (log->used ? log->used : 1));
    size_t n = 0;
    for (size_t i = 0; i < log->cap; i++)
        if (log->slots[i].used)
            rows[n++] = log->slots[i];
    qsort(rows, n, sizeof *rows, slot_order);

    int rc = 0;
    FILE *f = fopen(path, "w");
    if (!f) {
        rc = -1;
    } else {
        fprintf(f, "# ct_dev --io-log: hardware register accesses by origin\n");
        fprintf(f, "# kind origin site service register group access count first-frame"
                   " entry vblank blank visible picture-lines first-picture-frame visible-lines unblank-lines\n");
        for (size_t i = 0; i < n; i++) {
            const Slot *r = &rows[i];
            char site[16] = "-", entry[16] = "-", lines[16] = "-", first_picture[16] = "-";
            if (r->kind == ORIGIN_INTERPRETER || r->kind == ORIGIN_PORT_SERVICE || r->kind == ORIGIN_IDLE_SERVICE ||
                r->kind == ORIGIN_GENERATED)
                snprintf(site, sizeof site, "%02X:%04X", (unsigned)(r->pc >> 16), (unsigned)(r->pc & 0xFFFF));
            if (r->origin && r->kind != ORIGIN_GENERATED)
                snprintf(entry, sizeof entry, "%02X:%04X", (unsigned)(r->entry >> 16), (unsigned)(r->entry & 0xFFFF));
            if (r->line_min != UINT16_MAX) {
                snprintf(lines, sizeof lines, "%u-%u", (unsigned)r->line_min, (unsigned)r->line_max);
                snprintf(first_picture, sizeof first_picture, "%u", (unsigned)r->first_picture_frame);
            }
            fprintf(f, "%s %s %s %s $%04X %s %s %llu %u %s %llu %llu %llu %s %s ", kind_names[r->kind],
                    r->origin ? r->origin : "-", site, r->call ? r->call : "-", r->addr,
                    group_names[group_of(r->addr, r->flags)], (r->flags & SNES_IO_WRITE) ? "write" : "read",
                    (unsigned long long)r->count, (unsigned)r->first_frame, entry,
                    (unsigned long long)r->times[TIME_VBLANK], (unsigned long long)r->times[TIME_BLANK],
                    (unsigned long long)r->times[TIME_VISIBLE], lines, first_picture);
            for (int k = 0; k < LINE_KINDS; k++) {
                const char *sep = "";
                for (int line = 0; r->lines && line < AFFECTED_LINES; line++) {
                    if (r->lines[k][line]) {
                        fprintf(f, "%s%d:%u", sep, line, (unsigned)r->lines[k][line]);
                        sep = ",";
                    }
                }
                if (!*sep)
                    fputc('-', f);
                fputc(k + 1 < LINE_KINDS ? ' ' : '\n', f);
            }
        }
        if (fclose(f) != 0)
            rc = -1;
    }

    fprintf(summary,
            "io-log: register accesses by origin (columns: ppu apu apu-mirror joypad cpu math dma-reg dma-data)\n");
    for (int k = 0; k < ORIGIN_KINDS; k++) {
        fprintf(summary, "  %-30s", kind_titles[k]);
        for (int g = 0; g < GROUPS; g++)
            fprintf(summary, " %11llu", (unsigned long long)log->totals[k][g]);
        fprintf(summary, "\n");
    }
    fprintf(summary, "io-log: video register writes ($2100-$2133, $420C; transfer bytes included) by when "
                     "(columns: %s, %s, %s)\n", time_titles[TIME_VBLANK], time_titles[TIME_BLANK],
            time_titles[TIME_VISIBLE]);
    for (int k = 0; k < ORIGIN_KINDS; k++) {
        fprintf(summary, "  %-30s", kind_titles[k]);
        for (int t = 0; t < TIMES; t++)
            fprintf(summary, " %11llu", (unsigned long long)log->video[k][t]);
        fprintf(summary, "\n");
    }
    free(rows);
    return rc;
}

void io_log_destroy(IoLog *log)
{
    if (!log)
        return;
    if (snes_bus_io_observer_ctx == log) {
        snes_bus_io_observer = NULL;
        snes_bus_io_observer_ctx = NULL;
    }
    for (size_t i = 0; i < log->cap; i++)
        free(log->slots[i].lines);
    free(log->slots);
    free(log);
}

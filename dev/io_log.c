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

typedef struct {
    const char *origin;    /* entry name, or NULL (the pc, HDMA) */
    const char *call;      /* platform service call, or NULL */
    uint32_t pc;           /* interpreter: the instruction; service call: its origin site */
    uint16_t addr;
    uint8_t kind;
    uint8_t flags;         /* SNES_IO_* */
    uint64_t count;
    uint32_t first_frame;
    bool used;
} Slot;

struct IoLog {
    Snes *s;
    uint32_t pc;
    bool hdma;
    Slot *slots;
    size_t cap, used;
    uint64_t totals[ORIGIN_KINDS][GROUPS];
};

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
    (void)value;
    IoLog *log = ctx;
    if (bus != &log->s->bus)
        return;
    int group = group_of(addr, flags);
    if (group < 0)
        return;
    Slot key = { .addr = addr, .flags = (uint8_t)flags };
    const NativeEntry *e = native_running_entry();
    if (e && e->generated) {
        key.kind = ORIGIN_GENERATED;
        key.origin = e->name;
    } else if (e && e->idle) {
        key.origin = e->name;
        if (pf_snes_call.name) {
            key.kind = ORIGIN_IDLE_SERVICE;
            key.call = pf_snes_call.name;
            key.pc = pf_snes_call.site;
        } else {
            key.kind = ORIGIN_IDLE;
        }
    } else if (e) {
        key.origin = e->name;
        if (pf_snes_call.name) {
            key.kind = ORIGIN_PORT_SERVICE;
            key.call = pf_snes_call.name;
            key.pc = pf_snes_call.site;
        } else {
            key.kind = ORIGIN_PORT_DIRECT;
        }
    } else if (log->hdma) {
        key.kind = ORIGIN_HDMA;
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
        if (++log->used * 2 > log->cap) {
            grow(log);
            sl = slot_find(log, &key);
        }
    }
    sl->count++;
}

IoLog *io_log_create(Snes *s)
{
    IoLog *log = calloc(1, sizeof *log);
    log->s = s;
    log->cap = 1u << 14;
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

void io_log_note_hdma(IoLog *log) { log->hdma = true; }

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
        fprintf(f, "# kind origin site service register group access count first-frame\n");
        for (size_t i = 0; i < n; i++) {
            const Slot *r = &rows[i];
            char site[16] = "-";
            if (r->kind == ORIGIN_INTERPRETER || r->kind == ORIGIN_PORT_SERVICE || r->kind == ORIGIN_IDLE_SERVICE)
                snprintf(site, sizeof site, "%02X:%04X", (unsigned)(r->pc >> 16), (unsigned)(r->pc & 0xFFFF));
            fprintf(f, "%s %s %s %s $%04X %s %s %llu %u\n", kind_names[r->kind], r->origin ? r->origin : "-", site,
                    r->call ? r->call : "-", r->addr, group_names[group_of(r->addr, r->flags)],
                    (r->flags & SNES_IO_WRITE) ? "write" : "read", (unsigned long long)r->count,
                    (unsigned)r->first_frame);
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
    free(log->slots);
    free(log);
}

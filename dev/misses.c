#include "misses.h"

#include <stdlib.h>
#include <string.h>

#include "port/recomp_table.h"

/* Operand bytes per opcode; M and X: 1 or 2 by the width flag. */
static const char operand_bytes[257] =
    "111111110M0022231111111102002223"
    "213111110M0022231111111102002223"
    "011121110M0022231111211102003223"
    "012111110M0022231111111102002223"
    "112111110M0022231111111102002223"
    "X1X111110M0022231111111102002223"
    "X11111110M0022231111111102002223"
    "X11111110M0022231111211102002223";

typedef struct {
    uint32_t key;          /* pc << 3 | e << 2 | m << 1 | x, plus 1 (0 is empty) */
    int16_t section;       /* a section whose image the bytes matched, or -1 */
    uint64_t count;
    uint64_t modified;     /* runs inside some section's range with bytes no image has */
    uint8_t diff;          /* of those, the bytes that differed from the first section's (bit 0: opcode) */
} Miss;

struct Misses {
    const Snes *s;
    Miss *slots;
    size_t cap, used;
};

Misses *misses_create(const Snes *s)
{
    Misses *m = calloc(1, sizeof *m);
    m->s = s;
    m->cap = 1u << 16;
    m->slots = calloc(m->cap, sizeof *m->slots);
    return m;
}

void misses_destroy(Misses *m)
{
    if (!m)
        return;
    free(m->slots);
    free(m);
}

static bool is_rom(uint32_t pc)
{
    uint8_t bank = pc >> 16;
    return bank >= 0xC0 || (bank >= 0x40 && bank <= 0x7D) || (!(bank & 0x40) && (pc & 0xFFFF) >= 0x8000);
}

/* The canonical WRAM address ($7E:0000-$7F:FFFF) of pc, or 0 if it isn't WRAM. */
static uint32_t wram_addr(uint32_t pc)
{
    uint8_t bank = pc >> 16;
    if (bank == 0x7E || bank == 0x7F)
        return pc;
    if (!(bank & 0x40) && (pc & 0xFFFF) < 0x2000)
        return 0x7E0000 | (pc & 0xFFFF);
    return 0;
}

static Miss *slot(Misses *m, uint32_t key)
{
    size_t mask = m->cap - 1;
    for (size_t i = (key * 2654435761u) & mask;; i = (i + 1) & mask) {
        if (m->slots[i].key == key || m->slots[i].key == 0)
            return &m->slots[i];
    }
}

static void grow(Misses *m)
{
    Miss *old = m->slots;
    size_t n = m->cap;
    m->cap *= 2;
    m->slots = calloc(m->cap, sizeof *m->slots);
    for (size_t i = 0; i < n; i++)
        if (old[i].key)
            *slot(m, old[i].key) = old[i];
    free(old);
}

void misses_note(Misses *m, uint32_t pc, uint8_t p, bool e)
{
    bool m8 = e || (p & CPU_FLAG_M), x8 = e || (p & CPU_FLAG_X);
    uint32_t key = (pc << 3 | (uint32_t)e << 2 | (uint32_t)m8 << 1 | (uint32_t)x8) + 1;
    if (m->used * 2 >= m->cap)
        grow(m);
    Miss *ms = slot(m, key);
    if (!ms->key) {
        ms->key = key;
        ms->section = -1;
        m->used++;
    }
    ms->count++;
    uint32_t w = wram_addr(pc);
    if (!w)
        return;
    /* Compare the instruction's bytes with every section image holding them. */
    const uint8_t *wram = m->s->bus.wram;
    uint8_t op = wram[(w - 0x7E0000) & 0x1FFFF];
    char size = operand_bytes[op];
    int len = 1 + (size == 'M' ? (m8 ? 1 : 2) : size == 'X' ? (x8 ? 1 : 2) : size - '0');
    const uint8_t *code = wram + ((w - 0x7E0000) & 0x1FFFF);
    uint8_t diff = 0;
    bool inside = false;
    for (int i = 0; i < recomp_ram_section_count; i++) {
        const RecompRamSection *sec = &recomp_ram_sections[i];
        if (w < sec->base || w + (uint32_t)len > sec->base + sec->size)
            continue;
        const uint8_t *img = sec->image + (w - sec->base);
        if (!memcmp(code, img, (size_t)len)) {
            ms->section = (int16_t)i;
            return;
        }
        if (!inside)
            for (int k = 0; k < len; k++)
                diff |= (uint8_t)((code[k] != img[k]) << k);
        inside = true;
    }
    if (inside) {
        ms->modified++;
        ms->diff |= diff;
    }
}

static int by_key(const void *a, const void *b)
{
    const Miss *p = a, *q = b;
    return p->key < q->key ? -1 : p->key > q->key;
}

int misses_write(Misses *m, const char *path, FILE *summary)
{
    Miss *list = malloc(sizeof *list * (m->used ? m->used : 1));
    size_t n = 0;
    for (size_t i = 0; i < m->cap; i++)
        if (m->slots[i].key)
            list[n++] = m->slots[i];
    qsort(list, n, sizeof *list, by_key);

    FILE *f = fopen(path, "w");
    if (!f) {
        free(list);
        return -1;
    }
    enum { ROM, SECTION, MODIFIED, UNKNOWN, OTHER, KINDS };
    static const char *kind_names[KINDS] = { "ROM", "RAM in a section", "RAM modified", "RAM unknown", "other" };
    uint64_t insns[KINDS] = { 0 }, addrs[KINDS] = { 0 };
    uint64_t *sec_insns = calloc((size_t)recomp_ram_section_count + 1, sizeof *sec_insns);
    uint64_t *sec_addrs = calloc((size_t)recomp_ram_section_count + 1, sizeof *sec_addrs);
    fprintf(f, "# instructions the interpreter ran: BB:AAAA m x # count, ROM or RAM and its section (ct_dev --misses)\n");
    for (size_t i = 0; i < n; i++) {
        const Miss *ms = &list[i];
        uint32_t key = ms->key - 1, pc = key >> 3;
        bool e = key & 4, m8 = key & 2, x8 = key & 1;
        int kind = is_rom(pc) ? ROM : !wram_addr(pc) ? OTHER : ms->section >= 0 && !ms->modified ? SECTION
                 : ms->modified ? MODIFIED : UNKNOWN;
        insns[kind] += ms->count;
        addrs[kind]++;
        fprintf(f, "%02X:%04X %d %d # %llu %s", pc >> 16, pc & 0xFFFF, m8 ? 8 : 16, x8 ? 8 : 16,
                (unsigned long long)ms->count, kind == ROM ? "ROM" : kind == OTHER ? "other" : "RAM");
        if (kind == SECTION || kind == MODIFIED) {
            if (ms->section >= 0) {
                fprintf(f, " %s", recomp_ram_sections[ms->section].name);
                sec_insns[ms->section] += ms->count;
                sec_addrs[ms->section]++;
            }
            if (ms->modified)
                fprintf(f, " modified %llu (%s)", (unsigned long long)ms->modified,
                        ms->diff & 1 ? "opcode" : "operand");
        } else if (kind == UNKNOWN) {
            fprintf(f, " unknown");
        }
        fprintf(f, "%s\n", e ? " emulation" : "");
    }
    int rc = fclose(f);
    fprintf(summary, "misses:");
    for (int k = 0; k < KINDS; k++)
        fprintf(summary, "%s %s %llu at %llu", k ? "," : "", kind_names[k], (unsigned long long)insns[k],
                (unsigned long long)addrs[k]);
    fprintf(summary, "\n");
    for (int i = 0; i < recomp_ram_section_count; i++)
        if (sec_addrs[i])
            fprintf(summary, "  section %s: %llu at %llu\n", recomp_ram_sections[i].name,
                    (unsigned long long)sec_insns[i], (unsigned long long)sec_addrs[i]);
    free(sec_insns);
    free(sec_addrs);
    free(list);
    return rc;
}

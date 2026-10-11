#include "misses.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "port/generated_table.h"

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

int misses_instruction_length(uint8_t opcode, bool m8, bool x8)
{
    char size = operand_bytes[opcode];
    return 1 + (size == 'M' ? (m8 ? 1 : 2) : size == 'X' ? (x8 ? 1 : 2) : size - '0');
}

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

/* The RAM section whose image has the bytes of the instruction at WRAM address
 * `w` (returned), or -1; then `range` is the first section whose range holds
 * them, or -1, and `diff` the bytes that differ from its image (bit 0: opcode). */
static int find_section(const Snes *s, uint32_t w, bool m8, bool x8, int *range, uint8_t *diff)
{
    const uint8_t *wram = s->bus.wram;
    const uint8_t *code = wram + ((w - 0x7E0000) & 0x1FFFF);
    int len = misses_instruction_length(code[0], m8, x8);
    *range = -1;
    *diff = 0;
    for (int i = 0; i < generated_ram_section_count; i++) {
        const GeneratedRamSection *sec = &generated_ram_sections[i];
        if (w < sec->base || w + (uint32_t)len > sec->base + sec->size)
            continue;
        const uint8_t *img = sec->image + (w - sec->base);
        if (!memcmp(code, img, (size_t)len))
            return i;
        if (*range < 0) {
            *range = i;
            for (int k = 0; k < len; k++)
                *diff |= (uint8_t)((code[k] != img[k]) << k);
        }
    }
    return -1;
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
    int range;
    uint8_t diff;
    int section = find_section(m->s, w, m8, x8, &range, &diff);
    if (section >= 0) {
        ms->section = (int16_t)section;
    } else if (range >= 0) {
        ms->modified++;
        ms->diff |= diff;
    }
}

/* One line of a report. Returns whether it was an entry. */
static bool parse_line(Misses *m, const char *line)
{
    unsigned bank, addr;
    int mw, xw;
    unsigned long long count;
    char rest[400] = "";
    if (line[0] == '#' || sscanf(line, "%2x:%4x %d %d # %llu%399[^\n]", &bank, &addr, &mw, &xw, &count, rest) < 5)
        return false;
    bool e = strstr(rest, " emulation") != NULL, m8 = mw == 8, x8 = xw == 8;
    uint32_t pc = (uint32_t)bank << 16 | addr;
    uint32_t key = (pc << 3 | (uint32_t)e << 2 | (uint32_t)m8 << 1 | (uint32_t)x8) + 1;
    if (m->used * 2 >= m->cap)
        grow(m);
    Miss *ms = slot(m, key);
    if (!ms->key) {
        ms->key = key;
        ms->section = -1;
        m->used++;
    }
    ms->count += count;
    char name[128];
    if (sscanf(rest, " RAM %127s", name) == 1)
        for (int i = 0; i < generated_ram_section_count; i++)
            if (!strcmp(generated_ram_sections[i].name, name))
                ms->section = (int16_t)i;
    const char *mod = strstr(rest, " modified ");
    unsigned long long modified;
    char what[16];
    if (mod && sscanf(mod, " modified %llu (%15[a-z])", &modified, what) == 2) {
        ms->modified += modified;
        ms->diff |= !strcmp(what, "opcode") ? 1 : 2;
    }
    return true;
}

int misses_parse(Misses *m, const char *text, size_t len)
{
    char line[512];
    int n = 0;
    size_t at = 0;
    while (at < len) {
        size_t k = 0;
        while (at < len && k < sizeof line - 1) {
            char c = text[at++];
            if (c == '\r')
                continue;
            line[k++] = c;
            if (c == '\n')
                break;
        }
        line[k] = 0;
        n += parse_line(m, line);
    }
    return n;
}

int misses_load(Misses *m, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f))
        n += parse_line(m, line);
    int bad = ferror(f);
    fclose(f);
    return bad ? -1 : n;
}

size_t misses_count(const Misses *m, uint64_t *instructions)
{
    if (instructions) {
        *instructions = 0;
        for (size_t i = 0; i < m->cap; i++)
            *instructions += m->slots[i].count;
    }
    return m->used;
}

void misses_describe(const Snes *s, char *out, size_t n)
{
    const Cpu *c = &s->cpu;
    uint32_t pc = cpu_pc24(c);
    bool m8 = c->e || (c->p & CPU_FLAG_M), x8 = c->e || (c->p & CPU_FLAG_X);
    int len = snprintf(out, n, "%02X:%04X m%d x%d%s, ", pc >> 16, pc & 0xFFFF, m8 ? 8 : 16, x8 ? 8 : 16,
                       c->e ? " emulation mode" : "");
    if (len < 0 || (size_t)len >= n)
        return;
    out += len, n -= (size_t)len;
    uint32_t w = wram_addr(pc);
    if (is_rom(pc)) {
        snprintf(out, n, "ROM");
    } else if (!w) {
        snprintf(out, n, "neither ROM nor work RAM");
    } else {
        int range;
        uint8_t diff;
        int section = find_section(s, w, m8, x8, &range, &diff);
        if (section >= 0)
            snprintf(out, n, "RAM in section %s", generated_ram_sections[section].name);
        else if (range >= 0)
            snprintf(out, n, "RAM in section %s, but its %s differs from the image", generated_ram_sections[range].name,
                     diff & 1 ? "opcode" : "operand");
        else
            snprintf(out, n, "RAM in no section");
    }
}

static int by_key(const void *a, const void *b)
{
    const Miss *p = a, *q = b;
    return p->key < q->key ? -1 : p->key > q->key;
}

/* A growing string for misses_format. */
typedef struct {
    char *s;
    size_t len, cap;
    bool failed;
} Text;

static void put(Text *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0 || t->failed) {
        t->failed = true;
        return;
    }
    if (t->len + (size_t)n + 1 > t->cap) {
        size_t cap = (t->len + (size_t)n + 1) * 2;
        char *s = realloc(t->s, cap);
        if (!s) {
            t->failed = true;
            return;
        }
        t->s = s;
        t->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(t->s + t->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    t->len += (size_t)n;
}

static char *text_done(Text *t, size_t *len)
{
    put(t, "");
    if (t->failed) {
        free(t->s);
        return NULL;
    }
    *len = t->len;
    return t->s;
}

char *misses_format(Misses *m, size_t *len, FILE *summary)
{
    Miss *list = malloc(sizeof *list * (m->used ? m->used : 1));
    size_t n = 0;
    for (size_t i = 0; i < m->cap; i++)
        if (m->slots[i].key)
            list[n++] = m->slots[i];
    qsort(list, n, sizeof *list, by_key);

    Text f = { 0 };
    enum { ROM, SECTION, MODIFIED, UNKNOWN, OTHER, KINDS };
    static const char *kind_names[KINDS] = { "ROM", "RAM in a section", "RAM modified", "RAM unknown", "other" };
    uint64_t insns[KINDS] = { 0 }, addrs[KINDS] = { 0 };
    uint64_t *sec_insns = calloc((size_t)generated_ram_section_count + 1, sizeof *sec_insns);
    uint64_t *sec_addrs = calloc((size_t)generated_ram_section_count + 1, sizeof *sec_addrs);
    put(&f, "# instructions without generated code: BB:AAAA m x # count, ROM or RAM and its section "
               "(docs/GENERATED.md, miss report)\n");
    for (size_t i = 0; i < n; i++) {
        const Miss *ms = &list[i];
        uint32_t key = ms->key - 1, pc = key >> 3;
        bool e = key & 4, m8 = key & 2, x8 = key & 1;
        int kind = is_rom(pc) ? ROM : !wram_addr(pc) ? OTHER : ms->section >= 0 && !ms->modified ? SECTION
                 : ms->modified ? MODIFIED : UNKNOWN;
        insns[kind] += ms->count;
        addrs[kind]++;
        put(&f, "%02X:%04X %d %d # %llu %s", pc >> 16, pc & 0xFFFF, m8 ? 8 : 16, x8 ? 8 : 16,
                (unsigned long long)ms->count, kind == ROM ? "ROM" : kind == OTHER ? "other" : "RAM");
        if (kind == SECTION || kind == MODIFIED) {
            if (ms->section >= 0) {
                put(&f, " %s", generated_ram_sections[ms->section].name);
                sec_insns[ms->section] += ms->count;
                sec_addrs[ms->section]++;
            }
            if (ms->modified)
                put(&f, " modified %llu (%s)", (unsigned long long)ms->modified,
                        ms->diff & 1 ? "opcode" : "operand");
        } else if (kind == UNKNOWN) {
            put(&f, " unknown");
        }
        put(&f, "%s\n", e ? " emulation" : "");
    }
    if (!summary) {
        free(sec_insns);
        free(sec_addrs);
        free(list);
        return text_done(&f, len);
    }
    fprintf(summary, "misses:");
    for (int k = 0; k < KINDS; k++)
        fprintf(summary, "%s %s %llu at %llu", k ? "," : "", kind_names[k], (unsigned long long)insns[k],
                (unsigned long long)addrs[k]);
    fprintf(summary, "\n");
    for (int i = 0; i < generated_ram_section_count; i++)
        if (sec_addrs[i])
            fprintf(summary, "  section %s: %llu at %llu\n", generated_ram_sections[i].name,
                    (unsigned long long)sec_insns[i], (unsigned long long)sec_addrs[i]);
    free(sec_insns);
    free(sec_addrs);
    free(list);
    return text_done(&f, len);
}

int misses_write(Misses *m, const char *path, FILE *summary)
{
    size_t len;
    char *text = misses_format(m, &len, summary);
    FILE *f = text ? fopen(path, "w") : NULL;
    int ok = f && fwrite(text, 1, len, f) == len;
    if (f && fclose(f) != 0)
        ok = 0;
    free(text);
    return ok ? 0 : -1;
}

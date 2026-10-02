/* Checks the save state format (runtime/snes/state.c, docs/RUNTIME.md "Save
 * states") on a machine running a small synthetic program (no ROM needed):
 *
 * - a state round-trips every field, and the loaded machine runs on exactly
 *   like the one that saved it;
 * - every byte of Snes is either written to a state or is a host pointer, a
 *   hook, write-tracking bookkeeping or padding, so a field added without its
 *   line in state.c is caught (one small enough to pass for padding only if
 *   the program changed it);
 * - write tracking isn't saved, and a load counts all of work RAM and VRAM as
 *   written, so a machine copy refreshed with snes_clone_since before the load
 *   takes all of them afterwards (shadow mode would otherwise compare against
 *   memory the load replaced);
 * - a state from an older build, which lacks a field this one has, loads with
 *   that field at its power-on value; one from a newer build, with a field
 *   this one lacks, loads with the field skipped;
 * - a truncated, damaged, foreign, newer-format or inconsistent state (a
 *   field whose size changed, a value out of range, another ROM's) is
 *   rejected with a reason, and leaves the machine as it was;
 * - a host program's fields (SnesStateExtra) round-trip beside the machine's,
 *   reach the program only once the whole state has loaded, and a state loads
 *   only the way it was saved, with them or without.
 *
 * Exits non-zero on failure.
 *
 *   state_test */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes/system.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ---- a machine to save ------------------------------------------------------------ */

#define ROM_SIZE 0x10000

/* A loop that keeps the CPU, work RAM, VRAM, CGRAM and the multiplier busy,
 * with NMIs on, in bank $00 at $8000 (HiROM maps ROM $8000-$FFFF there). */
static const uint8_t program[] = {
    0x78,             /* 8000 SEI */
    0x18,             /* 8001 CLC */
    0xFB,             /* 8002 XCE */
    0xA9, 0x81,       /* 8003 LDA #$81 */
    0x8D, 0x00, 0x42, /* 8005 STA $4200     NMI and auto-joypad on */
    0xA9, 0x0F,       /* 8008 LDA #$0F */
    0x8D, 0x00, 0x21, /* 800A STA $2100     display on */
    0xE6, 0x10,       /* 800D INC $10 */
    0xA5, 0x10,       /* 800F LDA $10 */
    0x8D, 0x18, 0x21, /* 8011 STA $2118     VRAM */
    0x8D, 0x22, 0x21, /* 8014 STA $2122     CGRAM */
    0x8D, 0x02, 0x42, /* 8017 STA $4202     multiplier */
    0x8D, 0x03, 0x42, /* 801A STA $4203 */
    0xAD, 0x16, 0x42, /* 801D LDA $4216 */
    0x85, 0x11,       /* 8020 STA $11 */
    0x80, 0xE9,       /* 8022 BRA $800D */
};
static const uint8_t nmi_handler[] = {
    0xE6, 0x20, /* 8030 INC $20 */
    0xAD, 0x10, 0x42, /* 8032 LDA $4210 */
    0x40,       /* 8035 RTI */
};

static uint8_t *make_rom(uint8_t fill)
{
    uint8_t *rom = malloc(ROM_SIZE);
    memset(rom, fill, ROM_SIZE);
    memcpy(rom + 0x8000, program, sizeof program);
    memcpy(rom + 0x8030, nmi_handler, sizeof nmi_handler);
    rom[0xFFFC] = 0x00, rom[0xFFFD] = 0x80;   /* reset (emulation mode) */
    rom[0xFFEA] = 0x30, rom[0xFFEB] = 0x80;   /* NMI (native mode) */
    return rom;
}

static void boot(Snes *s, const uint8_t *rom)
{
    snes_init(s, rom, ROM_SIZE);
    s->interpreter = cpu_step;
    snes_power_on(s);
}

static void run(Snes *s, int frames, uint16_t pad)
{
    for (int i = 0; i < frames; i++)
        snes_run_frame(s, (uint16_t)(pad + i * 0x10));
}

/* ---- comparing machines field by field ---------------------------------------------- */

typedef struct {
    size_t offset, bytes;
    unsigned size;   /* of one element */
} FieldAt;

typedef struct {
    const Snes *base;
    FieldAt list[1024];
    int n;
} Fields;

static void collect(void *ctx, const char *name, void *field, unsigned size, uint32_t count)
{
    Fields *f = ctx;
    (void)name;
    if (f->n < (int)(sizeof f->list / sizeof f->list[0])) {
        f->list[f->n].size = size;
        f->list[f->n].offset = (size_t)((const uint8_t *)field - (const uint8_t *)f->base);
        f->list[f->n].bytes = (size_t)size * count;
    }
    f->n++;
}

static Fields *fields_of(Snes *s)
{
    Fields *f = calloc(1, sizeof *f);
    f->base = s;
    snes_state_fields(s, collect, f);
    return f;
}

/* The first field whose bytes differ between a and b, or -1. */
static int first_difference(const Fields *f, const Snes *a, const Snes *b)
{
    for (int i = 0; i < f->n; i++) {
        size_t o = f->list[i].offset;
        if (memcmp((const uint8_t *)a + o, (const uint8_t *)b + o, f->list[i].bytes) != 0)
            return i;
    }
    return -1;
}

/* ---- editing states ------------------------------------------------------------------ */

static uint32_t crc32_of(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* Where the record called `name` starts, or NULL. */
static uint8_t *find_record(uint8_t *state, size_t len, const char *name, size_t *rec_len)
{
    uint8_t *p = state + 8, *end = state + len - 4;
    while (p < end && *p) {
        size_t name_len = p[0];
        unsigned size = p[1 + name_len];
        uint32_t count = get32(p + 2 + name_len);
        size_t total = 1 + name_len + 1 + 4 + (size_t)size * count;
        if (name_len == strlen(name) && !memcmp(p + 1, name, name_len)) {
            *rec_len = total;
            return p;
        }
        p += total;
    }
    return NULL;
}

/* Replace `cut` bytes at `at` with `insert_len` bytes, and fix the checksum. */
static uint8_t *splice(const uint8_t *state, size_t len, size_t at, size_t cut, const uint8_t *insert,
                       size_t insert_len, size_t *out_len)
{
    size_t n = len - cut + insert_len;
    uint8_t *out = malloc(n);
    memcpy(out, state, at);
    memcpy(out + at, insert, insert_len);
    memcpy(out + at + insert_len, state + at + cut, len - at - cut);
    put32(out + n - 4, crc32_of(out, n - 4));
    *out_len = n;
    return out;
}

static uint8_t *reseal(uint8_t *state, size_t len)
{
    put32(state + len - 4, crc32_of(state, len - 4));
    return state;
}

static size_t make_record(uint8_t *out, const char *name, unsigned size, uint32_t count, const uint8_t *data)
{
    size_t n = strlen(name), at = 0;
    out[at++] = (uint8_t)n;
    memcpy(out + at, name, n);
    at += n;
    out[at++] = (uint8_t)size;
    put32(out + at, count);
    at += 4;
    memcpy(out + at, data, (size_t)size * count);
    return at + (size_t)size * count;
}

/* Loading `state` into a copy of `s` fails, the reason mentions `why`, and the
 * copy is unchanged. */
static void expect_rejected(const Snes *s, const Fields *f, const uint8_t *state, size_t len, const char *why,
                            const char *what)
{
    Snes *t = malloc(sizeof *t);
    snes_clone(t, s);
    char err[256] = "";
    int rc = snes_state_read(t, state, len, err, sizeof err);
    CHECK(rc != 0, "%s: loaded", what);
    CHECK(!why || strstr(err, why), "%s: reason \"%s\" doesn't mention \"%s\"", what, err, why);
    CHECK(first_difference(f, s, t) < 0, "%s: a rejected load changed the machine", what);
    free(t);
}

/* Not machine state: the ROM, host pointers and hooks, `halted`, which a load
 * clears, and write tracking (SNES_TRACK_PAGE_BITS), which a load resets
 * (check_write_tracking). */
static bool host_byte(size_t offset)
{
    static const struct {
        size_t offset, size;
    } host[] = {
#define HOST(member) { offsetof(Snes, member), sizeof(((Snes *)0)->member) }
        HOST(cpu.bus), HOST(cpu.on_insn), HOST(cpu.on_insn_ctx),
        HOST(bus.rom), HOST(bus.rom_size), HOST(bus.io), HOST(bus.wram_written), HOST(bus.wram_scaffold), HOST(bus.scaffolding),
        HOST(bus.on_access),
        HOST(apu.spc.bus), HOST(apu.on_insn), HOST(apu.on_insn_ctx), HOST(apu.on_dsp_write), HOST(apu.host), HOST(apu.host_ctx), HOST(apu.host_boot), HOST(apu.host_command),
        HOST(on_line), HOST(on_line_ctx), HOST(on_frame_top), HOST(on_frame_top_ctx),
        HOST(intercept), HOST(intercept_ctx), HOST(interpreter), HOST(native_only),
        HOST(on_miss), HOST(on_miss_ctx), HOST(halted),
        HOST(bus.write_epoch), HOST(bus.wram_page_epoch), HOST(ppu.write_epoch), HOST(ppu.vram_page_epoch),
#undef HOST
    };
    for (size_t i = 0; i < sizeof host / sizeof host[0]; i++) {
        if (offset >= host[i].offset && offset < host[i].offset + host[i].size)
            return true;
    }
    return false;
}

/* ---- the checks ------------------------------------------------------------------------- */

static void check_round_trip(Snes *a, const uint8_t *rom, const Fields *f)
{
    size_t len;
    uint8_t *state = snes_state_write(a, &len);
    CHECK(state != NULL, "write failed");
    Snes *b = malloc(sizeof *b);
    boot(b, rom);
    run(b, 3, 0x1234);   /* something to overwrite */
    char err[256] = "";
    CHECK(snes_state_read(b, state, len, err, sizeof err) == 0, "round trip: %s", err);
    int d = first_difference(f, a, b);
    CHECK(d < 0, "round trip: field %d (offset %zu) differs", d, d < 0 ? 0 : f->list[d].offset);
    CHECK(b->interpreter == cpu_step && b->bus.rom == rom, "a load dropped the machine's own hooks");
    /* Byte for byte, too: both machines' padding is snes_init's zeros, so a
     * difference outside the fields is a field the state doesn't hold. */
    for (size_t at = 0; at < sizeof(Snes); at++) {
        if (!host_byte(at) && ((uint8_t *)a)[at] != ((uint8_t *)b)[at]) {
            CHECK(false, "round trip: Snes byte %zu differs (a field missing from runtime/snes/state.c?)", at);
            break;
        }
    }

    /* Both run on identically. */
    run(a, 40, 0x0800);
    run(b, 40, 0x0800);
    d = first_difference(f, a, b);
    CHECK(d < 0, "after running on: field %d (offset %zu) differs", d, d < 0 ? 0 : f->list[d].offset);

    /* The same state written again is the same bytes. */
    size_t len2;
    uint8_t *again = snes_state_write(b, &len2);
    uint8_t *from_a = snes_state_write(a, &len);
    CHECK(len2 == len && !memcmp(again, from_a, len), "states of equal machines differ");
    free(again);
    free(from_a);
    free(state);
    free(b);
}

/* Every byte of Snes is in a field, a host pointer or hook, or padding. A gap
 * shorter than the alignment of what follows it is taken for padding; anything
 * else is a field state.c doesn't write. That misses only a forgotten field
 * small enough to pass for padding (a byte before a uint16_t, a bool before a
 * uint64_t), which the round trip below catches if the machine changed it. */
static void check_coverage(const Fields *f)
{
    uint8_t *covered = calloc(1, sizeof(Snes));
    for (int i = 0; i < f->n; i++)
        memset(covered + f->list[i].offset, 1, f->list[i].bytes);
    for (size_t at = 0; at < sizeof(Snes); at++) {
        if (host_byte(at))
            covered[at] = 2;
    }
    for (size_t at = 0; at < sizeof(Snes);) {
        if (covered[at]) {
            at++;
            continue;
        }
        size_t end = at;
        while (end < sizeof(Snes) && !covered[end])
            end++;
        /* Padding is shorter than the alignment of what follows it (a field, or
         * the struct it starts), which is at most the largest power of two up
         * to 8 that divides where it starts. */
        size_t align = 8;
        while (end % align)
            align /= 2;
        CHECK(end - at < align,
              "Snes bytes %zu-%zu are in no state field (a field missing from runtime/snes/state.c?)", at, end - 1);
        at = end;
    }
    free(covered);
}

static void check_versions(Snes *s, const uint8_t *rom, const Fields *f)
{
    size_t len;
    uint8_t *state = snes_state_write(s, &len);

    /* An older build's state: it has no snes.frame. That field comes up at its
     * power-on value, and everything else as saved. */
    size_t rec_len;
    uint8_t *rec = find_record(state, len, "snes.frame", &rec_len);
    CHECK(rec != NULL, "no snes.frame record");
    if (rec) {
        size_t old_len;
        uint8_t *old = splice(state, len, (size_t)(rec - state), rec_len, NULL, 0, &old_len);
        Snes *t = malloc(sizeof *t);
        boot(t, rom);
        char err[256] = "";
        CHECK(snes_state_read(t, old, old_len, err, sizeof err) == 0, "an older build's state: %s", err);
        Snes *fresh = malloc(sizeof *fresh);
        boot(fresh, rom);
        CHECK(t->frame == fresh->frame && s->frame != fresh->frame, "a missing field isn't at its power-on value");
        t->frame = s->frame;
        CHECK(first_difference(f, s, t) < 0, "an older build's state: other fields differ");
        free(fresh);
        free(t);
        free(old);
    }

    /* A newer build's state: it has a field this build doesn't know. */
    uint8_t extra[64];
    const uint8_t value[4] = { 1, 2, 3, 4 };
    size_t extra_len = make_record(extra, "ppu.a_field_from_the_future", 2, 2, value);
    size_t new_len;
    uint8_t *newer = splice(state, len, 8, 0, extra, extra_len, &new_len);
    Snes *t = malloc(sizeof *t);
    boot(t, rom);
    char err[256] = "";
    CHECK(snes_state_read(t, newer, new_len, err, sizeof err) == 0, "a newer build's state: %s", err);
    CHECK(first_difference(f, s, t) < 0, "a newer build's state: fields differ");
    free(t);
    free(newer);
    free(state);
}

static void check_rejected(Snes *s, const uint8_t *rom, const Fields *f)
{
    size_t len;
    uint8_t *state = snes_state_write(s, &len);
    uint8_t *copy = malloc(len + 16);

    /* Every short prefix, and a spread of longer ones. */
    for (size_t n = 0; n < len; n += n < 256 ? 1 : n > len - 256 ? 1 : 997)
        expect_rejected(s, f, state, n, NULL, "truncated");
    /* A flipped byte anywhere. */
    for (size_t at = 0; at < len; at += at < 64 ? 1 : 4099) {
        memcpy(copy, state, len);
        copy[at] ^= 0x40;
        expect_rejected(s, f, copy, len, NULL, "damaged");
    }
    memcpy(copy, state, len);
    copy[len / 2] ^= 1;
    expect_rejected(s, f, copy, len, "checksum", "damaged");

    memcpy(copy, state, len);
    memcpy(copy, "CTSS", 4);   /* the raw images of earlier builds */
    expect_rejected(s, f, copy, len, "raw machine image", "raw image");
    memcpy(copy, "PK\x03\x04", 4);
    expect_rejected(s, f, copy, len, "isn't a state", "not a state");

    memcpy(copy, state, len);
    copy[4] = 3;   /* a newer format */
    expect_rejected(s, f, reseal(copy, len), len, "format 3", "newer format");

    /* A field whose size changed: cpu.a written as one byte. */
    size_t rec_len;
    uint8_t *rec = find_record(state, len, "cpu.a", &rec_len);
    if (rec) {
        uint8_t one[32];
        const uint8_t v = 0x12;
        size_t n = make_record(one, "cpu.a", 1, 1, &v), out_len;
        uint8_t *changed = splice(state, len, (size_t)(rec - state), rec_len, one, n, &out_len);
        expect_rejected(s, f, changed, out_len, "cpu.a", "resized field");
        free(changed);
    }
    /* An array whose length changed. */
    rec = find_record(state, len, "bus.pad", &rec_len);
    if (rec) {
        uint8_t three[32];
        const uint8_t v[6] = { 0 };
        size_t n = make_record(three, "bus.pad", 2, 3, v), out_len;
        uint8_t *changed = splice(state, len, (size_t)(rec - state), rec_len, three, n, &out_len);
        expect_rejected(s, f, changed, out_len, "bus.pad", "resized array");
        free(changed);
    }
    /* A duplicate record. */
    rec = find_record(state, len, "cpu.pc", &rec_len);
    if (rec) {
        size_t out_len;
        uint8_t *dup = splice(state, len, (size_t)(rec - state), 0, rec, rec_len, &out_len);
        expect_rejected(s, f, dup, out_len, "malformed", "duplicate");
        free(dup);
    }
    /* Values the emulation would index out of bounds with. */
    static const struct {
        const char *name;
        uint8_t value;
    } wild[] = { { "snes.stall_next", 200 }, { "apu.phase", 9 }, { "apu.dsp.fir_pos", 8 }, { "cpu.e", 2 } };
    for (size_t i = 0; i < sizeof wild / sizeof wild[0]; i++) {
        memcpy(copy, state, len);
        rec = find_record(copy, len, wild[i].name, &rec_len);
        CHECK(rec != NULL, "no %s record", wild[i].name);
        if (rec) {
            rec[1 + strlen(wild[i].name) + 1 + 4] = wild[i].value;
            expect_rejected(s, f, reseal(copy, len), len, wild[i].name, wild[i].name);
        }
    }

    /* Another ROM's state. */
    uint8_t *other_rom = make_rom(0xEA);
    Snes *o = malloc(sizeof *o);
    boot(o, other_rom);
    Fields *fo = fields_of(o);
    expect_rejected(o, fo, state, len, "another ROM", "another ROM");
    free(fo);
    free(o);
    free(other_rom);
    (void)rom;

    free(copy);
    free(state);
}

/* Write tracking across a load. A copy of the machine is refreshed, the
 * machine loads an older state (different work RAM and VRAM, no bus or PPU
 * write in between), and the next refresh must bring the copy level with it.
 * The same with the memory overwritten directly and no snes_mark_all_written
 * shows the check can fail. */
static void check_write_tracking(const uint8_t *rom)
{
    Snes *a = malloc(sizeof *a), *copy = malloc(sizeof *copy);
    boot(a, rom);
    run(a, 3, 0);
    size_t len;
    uint8_t *older = snes_state_write(a, &len);
    run(a, 20, 0);

    uint64_t since = snes_clone_since(copy, a, 0);
    run(a, 2, 0);
    since = snes_clone_since(copy, a, since);
    CHECK(!memcmp(copy->bus.wram, a->bus.wram, sizeof a->bus.wram) &&
              !memcmp(copy->ppu.vram, a->ppu.vram, sizeof a->ppu.vram),
          "a refresh after running missed a write");

    /* Without marking, a direct overwrite goes unseen: the check is live. */
    Snes *b = malloc(sizeof *b);
    boot(b, rom);
    CHECK(snes_state_read(b, older, len, NULL, 0) == 0, "loading the older state failed");
    memcpy(a->bus.wram, b->bus.wram, sizeof a->bus.wram);
    memcpy(a->ppu.vram, b->ppu.vram, sizeof a->ppu.vram);
    Snes *probe = malloc(sizeof *probe);
    memcpy(probe, copy, sizeof *probe);
    snes_clone_since(probe, a, since);
    CHECK(memcmp(probe->bus.wram, a->bus.wram, sizeof a->bus.wram) != 0,
          "a direct overwrite reached the copy unmarked; the check below would prove nothing");
    free(probe);
    run(a, 1, 0);
    since = snes_clone_since(copy, a, 0);

    /* A load: the next refresh takes everything, and the epochs carry on. */
    uint64_t epoch = a->bus.write_epoch;
    char err[256] = "";
    CHECK(snes_state_read(a, older, len, err, sizeof err) == 0, "load: %s", err);
    CHECK(a->bus.write_epoch == epoch && a->ppu.write_epoch == epoch, "a load changed the write epoch");
    CHECK(memcmp(copy->bus.wram, a->bus.wram, sizeof a->bus.wram) != 0, "the load changed no work RAM");
    snes_clone_since(copy, a, since);
    CHECK(!memcmp(copy->bus.wram, a->bus.wram, sizeof a->bus.wram), "a refresh after a load missed work RAM");
    CHECK(!memcmp(copy->ppu.vram, a->ppu.vram, sizeof a->ppu.vram), "a refresh after a load missed VRAM");

    /* The state holds no tracking. */
    CHECK(find_record(older, len, "bus.write_epoch", &(size_t){ 0 }) == NULL, "the state holds write tracking");
    free(b);
    free(older);
    free(copy);
    free(a);
}

/* ---- a host program's fields ------------------------------------------------------------- */

typedef struct {
    uint16_t level;
    bool on;
    int took;   /* how many loads it took */
} Program;

static void visit_program(SnesStateVisitor *v, void *fields)
{
    Program *p = fields;
    snes_state_uint(v, "program.level", &p->level, sizeof p->level, 1);
    snes_state_bool(v, "program.on", &p->on, 1);
}

static void program_loaded(void *fields, void *loading)
{
    Program *p = fields, *from = loading;
    p->level = from->level;
    p->on = from->on;
    p->took++;
}

static void check_host_fields(Snes *s, const uint8_t *rom)
{
    Program saved = { 0x1234, true, 0 }, live = { 0, false, 0 }, loading = { 0, false, 0 };
    SnesStateExtra write = { "the program", visit_program, &saved, NULL, program_loaded };
    SnesStateExtra read = { "the program", visit_program, &live, &loading, program_loaded };
    size_t len, plain_len;
    uint8_t *state = snes_state_write_with(s, &write, &len);
    uint8_t *plain = snes_state_write(s, &plain_len);
    Snes *t = malloc(sizeof *t);
    boot(t, rom);
    char err[256] = "";
    CHECK(snes_state_read_with(t, &read, state, len, err, sizeof err) == 0, "a host program's state: %s", err);
    CHECK(live.level == 0x1234 && live.on && live.took == 1, "the program's fields: %04X %d %d", live.level, live.on,
          live.took);
    CHECK(snes_state_read(t, state, len, err, sizeof err) != 0 && strstr(err, "host program"),
          "a host program's state loaded without it: %s", err);
    live.took = 0;
    CHECK(snes_state_read_with(t, &read, plain, plain_len, err, sizeof err) != 0 && strstr(err, "the program") &&
              live.took == 0,
          "a machine's state loaded with a host program: %s", err);
    /* damaged: nothing reaches the program */
    state[len / 2] ^= 1;
    CHECK(snes_state_read_with(t, &read, state, len, err, sizeof err) != 0 && live.took == 0,
          "a damaged state reached the program");
    free(t);
    free(state);
    free(plain);
}

int main(void)
{
    uint8_t *rom = make_rom(0x00);
    Snes *s = malloc(sizeof *s);
    boot(s, rom);
    run(s, 25, 0x8000);
    Fields *f = fields_of(s);
    CHECK(f->n > 100 && f->n <= (int)(sizeof f->list / sizeof f->list[0]), "%d fields", f->n);
    CHECK(s->bus.wram[0x20] == 0x55 + 24 || s->bus.wram[0x20] == 0x55 + 25, "the program didn't run (%d NMIs)",
          s->bus.wram[0x20] - 0x55);

    check_coverage(f);
    check_round_trip(s, rom, f);
    check_versions(s, rom, f);
    check_rejected(s, rom, f);
    check_write_tracking(rom);
    check_host_fields(s, rom);

    free(f);
    free(s);
    free(rom);
    if (failures) {
        printf("state_test: %d failures\n", failures);
        return 1;
    }
    printf("state_test: all checks passed\n");
    return 0;
}

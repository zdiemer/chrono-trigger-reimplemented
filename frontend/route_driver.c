/* Route protocol over host callbacks. Checkpoints keep input rather than
 * pretending that the memory visible through peek includes C continuations. */
#include "route_driver.h"
#include "movie.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 4096, WATCHES = 32, LINE_BYTES = 8192, WRAM = 0x7E0000, WRAM_BYTES = 0x20000 };
typedef struct { uint32_t *pads; size_t count, capacity; uint32_t digest; bool present; void *extra; } Record;
typedef struct {
    const RouteHost *host;
    Record live, slots[SLOTS];
    uint32_t address[WATCHES], length[WATCHES];
    unsigned watches;
    bool diagnostic, failed;
    unsigned long replays;
} Driver;

static bool reserve(Record *r, size_t count)
{
    if (count <= r->capacity) return true;
    if (count > SIZE_MAX / sizeof *r->pads) return false;
    size_t capacity = count < 1024 ? 1024 : count;
    if (r->capacity <= SIZE_MAX / 2 / sizeof *r->pads && capacity < r->capacity * 2)
        capacity = r->capacity * 2;
    uint32_t *pads = realloc(r->pads, capacity * sizeof *pads);
    if (!pads) return false;
    r->pads = pads;
    r->capacity = capacity;
    return true;
}

static bool number(const char *word, unsigned base, uint32_t limit, uint32_t *out)
{
    char *end;
    if (!word || !*word || *word == '-' || *word == '+') return false;
    errno = 0;
    unsigned long v = strtoul(word, &end, (int)base);
    if (errno || *end || v > limit) return false;
    *out = (uint32_t)v;
    return true;
}

static bool advance(Driver *s, uint32_t pads)
{
    if (s->failed || s->live.count == UINT32_MAX || !reserve(&s->live, s->live.count + 1)) return false;
    if (!s->host->step(s->host->ctx, pads)) {
        s->failed = true;
        return false;
    }
    s->live.pads[s->live.count++] = pads;
    return true;
}

/* Check the observable state after rebuilding its hidden continuations. */
static uint32_t digest(const Driver *s)
{
    uint32_t value = 2166136261u;
    for (uint32_t i = 0; i < WRAM_BYTES; i++)
        value = (value ^ s->host->read(s->host->ctx, WRAM + i)) * 16777619u;
    for (uint32_t i = 0; i < 0x2000; i++)
        value = (value ^ s->host->read(s->host->ctx, 0x306000 + i)) * 16777619u;
    return value;
}

static bool restore(Driver *s, const Record *r)
{
    if (!reserve(&s->live, r->count)) return false;
    /* A load ahead on precisely the same input branch already has its live
     * continuations. Otherwise abandon them and replay from power-on. */
    bool prefix = !s->failed && !s->diagnostic && s->live.count <= r->count
        && (!s->live.count || !memcmp(s->live.pads, r->pads, s->live.count * sizeof *r->pads));
    if (!prefix) {
        if (!s->host->reset(s->host->ctx)) { s->failed = true; return false; }
        s->live.count = 0;
        s->failed = s->diagnostic = false;
    }
    bool accelerated = !prefix && r->extra;
    if (accelerated && !s->host->replay->begin(s->host->ctx, r->extra)) { s->failed = true; return false; }
    for (size_t i = s->live.count; i < r->count; i++)
        if (!advance(s, r->pads[i])) return false;
    if (accelerated && !s->host->replay->end(s->host->ctx, r->extra)) { s->failed = true; return false; }
    s->replays++;
    if (digest(s) != r->digest) {
        fprintf(stderr, "native replay differs at frame %zu\n", r->count);
        s->failed = true;
        return false;
    }
    return true;
}

static void bytes(Driver *s, FILE *out, uint32_t addr, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) fprintf(out, "%02x", s->host->read(s->host->ctx, addr + i));
}

static bool word_write(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fwrite(b, 1, 4, f) == 4;
}
static bool word_read(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return true;
}

static bool save_file(Driver *s, const char *path, const uint8_t *seed, size_t size, uint32_t identity)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite("CTNR\1\0\0\0", 1, 8, f) == 8 && word_write(f, identity)
        && word_write(f, (uint32_t)size) && fwrite(seed, 1, size, f) == size
        && word_write(f, (uint32_t)s->live.count) && word_write(f, digest(s));
    for (size_t i = 0; ok && i < s->live.count; i++) ok = word_write(f, s->live.pads[i]);
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool load_file(Driver *s, const char *path, const uint8_t *seed, size_t size, uint32_t identity)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[8];
    uint32_t id, nseed, count = 0;
    bool ok = fread(magic, 1, 8, f) == 8 && !memcmp(magic, "CTNR\1\0\0\0", 8)
        && word_read(f, &id) && id == identity && word_read(f, &nseed) && nseed == size;
    for (size_t i = 0; ok && i < size; i++) ok = fgetc(f) == seed[i];
    uint32_t expected = 0;
    ok = ok && word_read(f, &count) && word_read(f, &expected);
    /* Check the file length before allocating from an untrusted count. */
    long start = ftell(f), end = -1;
    if (ok && start >= 0 && fseek(f, 0, SEEK_END) == 0) end = ftell(f);
    ok = ok && end >= start && (uint64_t)(end - start) == (uint64_t)count * 4
        && fseek(f, start, SEEK_SET) == 0;
    Record r = {0};
    ok = ok && reserve(&r, count);
    for (uint32_t i = 0; ok && i < count; i++) ok = word_read(f, &r.pads[i]);
    fclose(f);
    r.count = count;
    r.digest = expected;
    if (ok) ok = restore(s, &r);
    free(r.pads);
    return ok;
}

int route_driver(const RouteHost *host, const uint8_t *seed, size_t seed_size,
                 uint32_t identity, FILE *input, FILE *out)
{
    Driver *s = calloc(1, sizeof *s);
    if (!s) return 1;
    s->host = host;
    char line[LINE_BYTES];
    while (fgets(line, sizeof line, input)) {
        if (!strchr(line, '\n') && !feof(input)) {
            int c;
            while ((c = fgetc(input)) != '\n' && c != EOF) {}
            fprintf(out, "err line too long\n"); fflush(out); continue;
        }
        line[strcspn(line, "\r\n")] = 0;
        size_t line_length = strlen(line);
        char *cmd = strtok(line, " \t"), *a = strtok(NULL, " \t");
        /* Paths are the rest of the line, including spaces. */
        bool file_command = cmd && (!strcmp(cmd, "shot") || !strcmp(cmd, "wram") || !strcmp(cmd, "movie")
            || !strcmp(cmd, "savefile") || !strcmp(cmd, "loadfile"));
        char *b = NULL, *c = NULL, *extra = NULL;
        if (file_command && a) {
            char *tail = a + strlen(a);
            if (tail < line + line_length) *tail = ' ';
        } else if (cmd && strcmp(cmd, "watch")) {
            b = strtok(NULL, " \t"); c = strtok(NULL, " \t"); extra = strtok(NULL, " \t");
        }
        uint32_t n = 0, pad = 0, pad2 = 0;
        if (!cmd) fprintf(out, "err command\n");
        else if ((!strcmp(cmd, "run") || !strcmp(cmd, "runw")) && !extra
                 && number(a, 10, UINT32_MAX, &n) && number(b, 10, 65535, &pad)
                 && (!c || number(c, 10, 65535, &pad2))) {
            bool ok = true;
            for (uint32_t i = 0; ok && i < n; i++) ok = advance(s, pad | pad2 << 16);
            if (!ok) fprintf(out, "err frame %zu\n", s->live.count);
            else {
                fprintf(out, "ok %zu", s->live.count);
                if (!strcmp(cmd, "runw")) {
                    fputc(' ', out);
                    for (unsigned i = 0; i < s->watches; i++) bytes(s, out, s->address[i], s->length[i]);
                }
                fputc('\n', out);
            }
        } else if (!strcmp(cmd, "watch")) {
            uint32_t address[WATCHES], length[WATCHES];
            unsigned count = 0;
            bool ok = true;
            for (char *w = a; w; w = strtok(NULL, " \t")) {
                char *colon = strchr(w, ':');
                if (!colon || count == WATCHES) { ok = false; break; }
                *colon = 0;
                if (!number(w, 16, 0xFFFFFF, &address[count]) || !number(colon + 1, 10, WRAM_BYTES, &length[count])
                    || length[count] > 0x1000000 - address[count]) { ok = false; break; }
                count++;
            }
            if (ok) {
                memcpy(s->address, address, count * sizeof *address);
                memcpy(s->length, length, count * sizeof *length);
                s->watches = count;
            }
            fprintf(out, ok ? "ok\n" : "err watch\n");
        } else if (!strcmp(cmd, "peek") && !c && number(a, 16, 0xFFFFFF, &pad)
                   && number(b, 10, WRAM_BYTES, &n) && n <= 0x1000000 - pad) {
            fprintf(out, "ok "); bytes(s, out, pad, n); fputc('\n', out);
        } else if ((!strcmp(cmd, "save") || !strcmp(cmd, "load") || !strcmp(cmd, "drop"))
                   && !b && number(a, 10, SLOTS - 1, &n)) {
            Record *r = &s->slots[n];
            if (!strcmp(cmd, "drop")) {
                if (r->extra) host->replay->drop(r->extra);
                free(r->pads); *r = (Record){0}; fprintf(out, "ok\n");
            } else if (!strcmp(cmd, "save")) {
                if (s->diagnostic || s->failed) fprintf(out, "err unreplayable\n");
                else if (!reserve(r, s->live.count)) fprintf(out, "err memory\n");
                else {
                    void *extra_state = host->replay ? host->replay->save(host->ctx) : NULL;
                    if (host->replay && !extra_state) { fprintf(out, "err memory\n"); fflush(out); continue; }
                    if (r->extra) host->replay->drop(r->extra);
                    r->extra = extra_state;
                    if (s->live.count) memcpy(r->pads, s->live.pads, s->live.count * sizeof *r->pads);
                    r->count = s->live.count; r->present = true;
                    r->digest = digest(s);
                    fprintf(out, "ok %zu\n", s->live.count);
                }
            } else if (!r->present) fprintf(out, "err empty\n");
            else if (!restore(s, r)) fprintf(out, "err replay\n");
            else fprintf(out, "ok %zu\n", s->live.count);
        } else if (!strcmp(cmd, "movie") && a) {
            char *to = strrchr(a, ' ');
            bool ok = to && number(to + 1, 10, UINT32_MAX, &n) && n >= s->live.count;
            ct_movie movie;
            if (to) *to = 0;
            ok = ok && ct_movie_load(&movie, a) == 0;
            if (ok) {
                while (ok && s->live.count < n) {
                    uint32_t frame = (uint32_t)s->live.count;
                    ok = advance(s, (uint32_t)ct_movie_buttons(&movie, frame)
                                 | (uint32_t)ct_movie_buttons2(&movie, frame) << 16);
                }
                ct_movie_free(&movie);
            }
            fprintf(out, ok ? "ok %zu\n" : "err movie %zu\n", s->live.count);
        } else if (file_command && a) {
            bool ok = false;
            if (!strcmp(cmd, "shot")) ok = host->shot(host->ctx, a);
            else if (!strcmp(cmd, "savefile"))
                ok = !s->diagnostic && !s->failed && save_file(s, a, seed, seed_size, identity);
            else if (!strcmp(cmd, "loadfile")) ok = load_file(s, a, seed, seed_size, identity);
            else {
                FILE *f = fopen(a, "wb");
                if (f) {
                    for (uint32_t i = 0; i < WRAM_BYTES; i++) fputc(host->read(host->ctx, WRAM + i), f);
                    ok = !ferror(f);
                    if (fclose(f) != 0) ok = false;
                }
            }
            if (!ok) fprintf(out, "err file\n");
            else if (!strcmp(cmd, "savefile") || !strcmp(cmd, "loadfile")) fprintf(out, "ok %zu\n", s->live.count);
            else fprintf(out, "ok\n");
        } else if (!strcmp(cmd, "poke") && !c && number(a, 16, 0x7FFFFF, &pad)
                   && pad >= WRAM && b && strlen(b) % 2 == 0 && strlen(b) / 2 <= 0x800000 - pad) {
            bool ok = true;
            for (size_t i = 0; b[i]; i += 2) {
                char hex[3] = { b[i], b[i + 1], 0 };
                if (!number(hex, 16, 255, &n)) { ok = false; break; }
            }
            if (ok) {
                for (size_t i = 0; b[i]; i += 2) {
                    char hex[3] = { b[i], b[i + 1], 0 };
                    number(hex, 16, 255, &n); host->write(host->ctx, pad + (uint32_t)i / 2, (uint8_t)n);
                }
                s->diagnostic = true;
            }
            fprintf(out, ok ? "ok\n" : "err hex\n");
        } else if (!strcmp(cmd, "frame") && !a) fprintf(out, "ok %zu\n", s->live.count);
        else if (!strcmp(cmd, "quit") && !a) { fprintf(out, "ok\n"); fflush(out); break; }
        else fprintf(out, "err command\n");
        fflush(out);
    }
    int result = s->failed ? 1 : 0;
    for (unsigned i = 0; i < SLOTS; i++) {
        if (s->slots[i].extra) host->replay->drop(s->slots[i].extra);
        free(s->slots[i].pads);
    }
    free(s->live.pads); free(s);
    return result;
}

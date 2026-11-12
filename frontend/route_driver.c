/* Route protocol over host callbacks. Checkpoints keep input rather than
 * pretending that the memory visible through peek includes C continuations. */
#include "route_driver.h"
#include "movie.h"
#include "resume.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 4096, WATCHES = 32, LINE_BYTES = 8192, WRAM = 0x7E0000, WRAM_BYTES = 0x20000 };
typedef struct { Resume state; bool present; void *extra; } Record;
typedef struct {
    const RouteHost *host;
    Record live, slots[SLOTS];
    uint32_t address[WATCHES], length[WATCHES];
    unsigned watches;
    bool diagnostic, failed;
} Driver;

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
    if (s->failed || s->live.state.frames == UINT32_MAX || !resume_reserve(&s->live.state, s->live.state.frames + 1)) return false;
    if (!s->host->step(s->host->ctx, pads)) {
        s->failed = true;
        return false;
    }
    s->live.state.input[s->live.state.frames++] = pads;
    return true;
}

/* Check the observable state after rebuilding its hidden continuations. */
static uint32_t digest(void *ctx)
{
    const Driver *s = ctx;
    uint32_t value = 2166136261u;
    for (uint32_t i = 0; i < WRAM_BYTES; i++)
        value = (value ^ s->host->read(s->host->ctx, WRAM + i)) * 16777619u;
    for (uint32_t i = 0; i < 0x2000; i++)
        value = (value ^ s->host->read(s->host->ctx, 0x306000 + i)) * 16777619u;
    return value;
}

static bool replay_reset(void *ctx, const Resume *base)
{
    Driver *s = ctx;
    (void)base; /* Route files validate their fixed seed before starting replay. */
    if (!s->host->reset(s->host->ctx)) return false;
    s->live.state.frames = 0;
    s->failed = s->diagnostic = false;
    return true;
}
static bool replay_frame(void *ctx, uint32_t pads) { return advance(ctx, pads); }
static bool replay_begin(void *ctx, const void *extra)
{
    Driver *s = ctx;
    return s->host->replay->begin(s->host->ctx, extra);
}
static bool replay_end(void *ctx, const void *extra)
{
    Driver *s = ctx;
    return s->host->replay->end(s->host->ctx, extra);
}
static bool restore(Driver *s, const Record *r)
{
    if (!resume_reserve(&s->live.state, r->state.frames)) return false;
    /* A load ahead on precisely the same input branch already has its live
     * continuations. Both drivers otherwise use the shared frame replay. */
    bool prefix = !s->failed && !s->diagnostic && s->live.state.frames <= r->state.frames
        && (!s->live.state.frames || !memcmp(s->live.state.input, r->state.input,
                                            s->live.state.frames * sizeof *r->state.input));
    ResumeReplay replay;
    ResumeHost host = { s, replay_reset, replay_frame, digest, replay_begin, replay_end };
    bool ok = resume_start(&replay, host, &r->state, prefix ? s->live.state.frames : 0,
                           !prefix, prefix ? NULL : r->extra);
    while (ok && replay.active) ok = resume_advance(&replay);
    if (!ok) {
        fprintf(stderr, "native replay differs at frame %u\n", r->state.frames);
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
        && word_write(f, (uint32_t)s->live.state.frames) && word_write(f, digest(s));
    for (uint32_t i = 0; ok && i < s->live.state.frames; i++) ok = word_write(f, s->live.state.input[i]);
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
    ok = ok && resume_reserve(&r.state, count);
    for (uint32_t i = 0; ok && i < count; i++) ok = word_read(f, &r.state.input[i]);
    fclose(f);
    r.state.frames = count;
    r.state.outcome = expected;
    if (ok) ok = restore(s, &r);
    free(r.state.input);
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
            if (!ok) fprintf(out, "err frame %u\n", s->live.state.frames);
            else {
                fprintf(out, "ok %u", s->live.state.frames);
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
                free(r->state.input); *r = (Record){0}; fprintf(out, "ok\n");
            } else if (!strcmp(cmd, "save")) {
                if (s->diagnostic || s->failed) fprintf(out, "err unreplayable\n");
                else if (!resume_reserve(&r->state, s->live.state.frames)) fprintf(out, "err memory\n");
                else {
                    void *extra_state = host->replay ? host->replay->save(host->ctx) : NULL;
                    if (host->replay && !extra_state) { fprintf(out, "err memory\n"); fflush(out); continue; }
                    if (r->extra) host->replay->drop(r->extra);
                    r->extra = extra_state;
                    (void)resume_copy(&r->state, &s->live.state); /* capacity was reserved above */
                    r->present = true;
                    r->state.outcome = digest(s);
                    fprintf(out, "ok %u\n", s->live.state.frames);
                }
            } else if (!r->present) fprintf(out, "err empty\n");
            else if (!restore(s, r)) fprintf(out, "err replay\n");
            else fprintf(out, "ok %u\n", s->live.state.frames);
        } else if (!strcmp(cmd, "movie") && a) {
            char *to = strrchr(a, ' ');
            bool ok = to && number(to + 1, 10, UINT32_MAX, &n) && n >= s->live.state.frames;
            ct_movie movie;
            if (to) *to = 0;
            ok = ok && ct_movie_load(&movie, a) == 0;
            if (ok) {
                while (ok && s->live.state.frames < n) {
                    uint32_t frame = (uint32_t)s->live.state.frames;
                    ok = advance(s, (uint32_t)ct_movie_buttons(&movie, frame)
                                 | (uint32_t)ct_movie_buttons2(&movie, frame) << 16);
                }
                ct_movie_free(&movie);
            }
            fprintf(out, ok ? "ok %u\n" : "err movie %u\n", s->live.state.frames);
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
            else if (!strcmp(cmd, "savefile") || !strcmp(cmd, "loadfile")) fprintf(out, "ok %u\n", s->live.state.frames);
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
        } else if (!strcmp(cmd, "frame") && !a) fprintf(out, "ok %u\n", s->live.state.frames);
        else if (!strcmp(cmd, "quit") && !a) { fprintf(out, "ok\n"); fflush(out); break; }
        else fprintf(out, "err command\n");
        fflush(out);
    }
    int result = s->failed ? 1 : 0;
    for (unsigned i = 0; i < SLOTS; i++) {
        if (s->slots[i].extra) host->replay->drop(s->slots[i].extra);
        free(s->slots[i].state.input);
    }
    free(s->live.state.input); free(s);
    return result;
}

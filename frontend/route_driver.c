/* The route protocol over host callbacks. A slot is the host's snapshot of
 * the program at a frame end, with the frame it was taken at; a state file
 * is the same, behind the seed and catalogue it belongs to. */
#include "route_driver.h"
#include "checksum.h"
#include "movie.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 4096, WATCHES = 32, LINE_BYTES = 8192, WRAM = 0x7E0000, WRAM_BYTES = 0x20000 };
typedef struct { uint8_t *bytes; size_t len; uint32_t frames; } Record;
typedef struct {
    const RouteHost *host;
    uint32_t frames;
    Record slots[SLOTS];
    uint32_t address[WATCHES], length[WATCHES];
    unsigned watches;
    /* poked: the program is where no input takes it, so neither a slot nor a
     * file may keep it as a route's state, which a movie must reproduce */
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
    if (s->failed || s->frames == UINT32_MAX) return false;
    if (!s->host->step(s->host->ctx, pads)) {
        s->failed = true;
        return false;
    }
    s->frames++;
    return true;
}

static bool take(Driver *s, Record *r)
{
    size_t len = 0;
    uint8_t *bytes = s->host->snapshot(s->host->ctx, &len);
    if (!bytes) return false;
    free(r->bytes);
    *r = (Record){ bytes, len, s->frames };
    return true;
}

static bool put_back(Driver *s, const uint8_t *bytes, size_t len, uint32_t frames)
{
    if (!s->host->restore(s->host->ctx, bytes, len)) return false;
    s->frames = frames;
    s->failed = s->diagnostic = false;
    return true;
}

static void bytes(Driver *s, FILE *out, uint32_t addr, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) fprintf(out, "%02x", s->host->read(s->host->ctx, addr + i));
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A state file: the magic, the catalogue and seed it belongs to, then its
 * frame, the snapshot's length and checksum, and the snapshot. */
enum { FILE_HEAD = 8 + 4 + 4, FILE_TAIL = 12 };
static const uint8_t FILE_MAGIC[8] = { 'C', 'T', 'N', 'R', 2, 0, 0, 0 };

static bool save_file(Driver *s, const char *path, const uint8_t *seed, size_t size, uint32_t identity)
{
    Record r = { 0 };
    if (!take(s, &r)) return false;
    uint8_t head[FILE_HEAD], tail[FILE_TAIL];
    memcpy(head, FILE_MAGIC, 8);
    put32(head + 8, identity);
    put32(head + 12, (uint32_t)size);
    put32(tail, r.frames);
    put32(tail + 4, (uint32_t)r.len);
    put32(tail + 8, ct_crc32(r.bytes, r.len));
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(head, 1, sizeof head, f) == sizeof head && fwrite(seed, 1, size, f) == size
        && fwrite(tail, 1, sizeof tail, f) == sizeof tail && fwrite(r.bytes, 1, r.len, f) == r.len;
    if (f && fclose(f) != 0) ok = false;
    free(r.bytes);
    return ok;
}

static bool load_file(Driver *s, const char *path, const uint8_t *seed, size_t size, uint32_t identity)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    long length = -1;
    if (fseek(f, 0, SEEK_END) == 0) length = ftell(f);
    uint8_t *data = length > 0 && fseek(f, 0, SEEK_SET) == 0 ? malloc((size_t)length) : NULL;
    bool ok = data && fread(data, 1, (size_t)length, f) == (size_t)length;
    fclose(f);
    size_t at = FILE_HEAD + size + FILE_TAIL;
    ok = ok && (size_t)length >= at && !memcmp(data, FILE_MAGIC, 8) && get32(data + 8) == identity
        && get32(data + 12) == size && !memcmp(data + FILE_HEAD, seed, size);
    if (ok) {
        const uint8_t *tail = data + FILE_HEAD + size;
        size_t len = get32(tail + 4);
        ok = (size_t)length - at == len && ct_crc32(data + at, len) == get32(tail + 8)
            && put_back(s, data + at, len, get32(tail));
    }
    free(data);
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
        bool file_command = cmd && (!strcmp(cmd, "shot") || !strcmp(cmd, "layershot") || !strcmp(cmd, "wram") || !strcmp(cmd, "movie")
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
            if (!ok) fprintf(out, "err frame %u\n", s->frames);
            else {
                fprintf(out, "ok %u", s->frames);
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
                free(r->bytes); *r = (Record){0}; fprintf(out, "ok\n");
            } else if (!strcmp(cmd, "save")) {
                if (s->diagnostic || s->failed) fprintf(out, "err unreplayable\n");
                else if (!take(s, r)) fprintf(out, "err memory\n");
                else fprintf(out, "ok %u\n", s->frames);
            } else if (!r->bytes) fprintf(out, "err empty\n");
            else if (!put_back(s, r->bytes, r->len, r->frames)) fprintf(out, "err restore\n");
            else fprintf(out, "ok %u\n", s->frames);
        } else if (!strcmp(cmd, "movie") && a) {
            char *to = strrchr(a, ' ');
            bool ok = to && number(to + 1, 10, UINT32_MAX, &n) && n >= s->frames;
            ct_movie movie;
            if (to) *to = 0;
            ok = ok && ct_movie_load(&movie, a) == 0;
            if (ok) {
                while (ok && s->frames < n) {
                    uint32_t frame = s->frames;
                    ok = advance(s, (uint32_t)ct_movie_buttons(&movie, frame)
                                 | (uint32_t)ct_movie_buttons2(&movie, frame) << 16);
                }
                ct_movie_free(&movie);
            }
            fprintf(out, ok ? "ok %u\n" : "err movie %u\n", s->frames);
        } else if (file_command && a) {
            bool ok = false;
            uint16_t scroll[8], colors[256];
            if (!strcmp(cmd, "shot")) ok = host->shot(host->ctx, a);
            else if (!strcmp(cmd, "layershot")) {
                /* layershot LAYERS PATH: answers the layers' scrolls, then
                 * the colors in hex, four digits each */
                char *path = strchr(a, ' ');
                if (path) *path++ = 0;
                ok = path && *path && number(a, 16, 0x0F, &n) && host->layer_shot(host->ctx, path, (uint8_t)n, scroll, colors);
                if (ok) {
                    fprintf(out, "ok");
                    for (unsigned k = 0; k < 8; k++) fprintf(out, " %u", scroll[k]);
                    fprintf(out, " ");
                    for (unsigned k = 0; k < 256; k++) fprintf(out, "%04x", colors[k]);
                    fprintf(out, "\n");
                    fflush(out);
                    continue;
                }
            }
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
            else if (!strcmp(cmd, "savefile") || !strcmp(cmd, "loadfile")) fprintf(out, "ok %u\n", s->frames);
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
        } else if (!strcmp(cmd, "frame") && !a) fprintf(out, "ok %u\n", s->frames);
        else if (!strcmp(cmd, "quit") && !a) { fprintf(out, "ok\n"); fflush(out); break; }
        else fprintf(out, "err command\n");
        fflush(out);
    }
    int result = s->failed ? 1 : 0;
    for (unsigned i = 0; i < SLOTS; i++)
        free(s->slots[i].bytes);
    free(s);
    return result;
}

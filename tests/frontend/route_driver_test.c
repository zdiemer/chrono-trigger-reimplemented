/* The route protocol over a made-up program whose whole state is two words:
 * slots and state files keep the host's snapshots and put them back, a load
 * lands on the frame its save was taken at, a poked program can't be kept,
 * and a file of another seed, catalogue or length is refused untouched. */
#include "route_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t value, steps; unsigned snapshots, restores; } Program;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

static bool step(void *ctx, uint32_t pads)
{
    Program *p = ctx;
    p->value = p->value * 3 + pads;
    p->steps++;
    return true;
}
static uint8_t read_byte(void *ctx, uint32_t at)
{
    return (uint8_t)(((Program *)ctx)->value >> ((at & 3) * 8));
}
static void write_byte(void *ctx, uint32_t at, uint8_t v) { (void)at; ((Program *)ctx)->value = v; }
static bool shot(void *ctx, const char *path) { (void)ctx; return !strcmp(path, "path with spaces.png"); }
static uint8_t *snapshot(void *ctx, size_t *len)
{
    Program *p = ctx;
    uint8_t *bytes = malloc(8);
    if (!bytes) return NULL;
    memcpy(bytes, "SNAP", 4);
    memcpy(bytes + 4, &p->value, 4);
    p->snapshots++;
    *len = 8;
    return bytes;
}
static bool restore(void *ctx, const uint8_t *bytes, size_t len)
{
    Program *p = ctx;
    if (len != 8 || memcmp(bytes, "SNAP", 4)) return false;
    memcpy(&p->value, bytes + 4, 4);
    p->restores++;
    return true;
}

static void run(Program *p, uint32_t identity, const char *commands, const char *expected)
{
    FILE *in = tmpfile(), *out = tmpfile();
    CHECK(in && out);
    if (!in || !out) exit(1);
    fputs(commands, in); rewind(in);
    uint8_t seed[] = {1, 2, 3};
    RouteHost host = {p, step, read_byte, write_byte, shot, snapshot, restore};
    CHECK(route_driver(&host, seed, sizeof seed, identity, in, out) == 0);
    rewind(out);
    char text[4096];
    size_t n = fread(text, 1, sizeof text - 1, out); text[n] = 0;
    if (strcmp(text, expected)) fprintf(stderr, "got:\n%s\nexpected:\n%s", text, expected);
    CHECK(!strcmp(text, expected));
    fclose(in); fclose(out);
}

int main(void)
{
    /* 0 -> 2 -> 8 -> 26 after `run 3 2`; then pads 1|1<<16 twice */
    Program p = {0};
    run(&p, 42,
        "save 0\nrun 3 2\nsave 1\nrun 2 1 1\npeek 7e0000 4\nsave 2\n"
        "run 2 50\nload 1\npeek 7e0000 4\nrun 2 1 1\npeek 7e0000 4\nload 2\nframe\n"
        "load 0\nframe\ndrop 1\nload 1\nload 9\nshot path with spaces.png\nquit\n",
        "ok 0\nok 3\nok 3\nok 5\nok ee000400\nok 5\n"
        "ok 7\nok 3\nok 1a000000\nok 5\nok ee000400\nok 5\nok 5\n"
        "ok 0\nok 0\nok\nerr empty\nerr empty\nok\nok\n");
    CHECK(p.snapshots == 3 && p.restores == 3 && p.steps == 9);

    p = (Program){0};
    run(&p, 42,
        "watch 7e0000:4\nrunw 3 2\nwatch bad\nrunw 0 0\n"
        "peek 7e0000 -1\nrun -1 0\nrun 1 65536\nsave 4096\n"
        "save 0\npoke 7e0000 00xz\npeek 7e0000 1\npoke 7e0000 ff\nsave 1\nsavefile route test.state\n"
        "peek 7e0000 1\nload 0\nsave 1\nquit\n",
        "ok\nok 3 1a000000\nerr watch\nok 3 1a000000\n"
        "err command\nerr command\nerr command\nerr command\n"
        "ok 3\nerr hex\nok 1a\nok\nerr unreplayable\nerr file\n"
        "ok ff\nok 3\nok 3\nok\n");

    p = (Program){0};
    run(&p, 42, "run 5 1 1\nsavefile route test.state\nrun 1 99\nloadfile route test.state\npeek 7e0000 4\nquit\n",
        "ok 5\nok 5\nok 6\nok 5\nok 79007900\nok\n");
    /* A file comes back in a fresh drive, independently of the slots. */
    p = (Program){0};
    run(&p, 42, "loadfile route test.state\npeek 7e0000 4\nquit\n", "ok 5\nok 79007900\nok\n");
    /* Another catalogue's, or a damaged one, is refused, the program untouched. */
    p = (Program){0};
    run(&p, 43, "run 3 2\nloadfile route test.state\nframe\npeek 7e0000 1\nquit\n", "ok 3\nerr file\nok 3\nok 1a\nok\n");
    FILE *bad = fopen("route test.state", "ab");
    CHECK(bad != NULL);
    if (bad) { fputc(0, bad); fclose(bad); }
    p = (Program){0};
    run(&p, 42, "loadfile route test.state\nframe\nquit\n", "err file\nok 0\nok\n");
    remove("route test.state");
    FILE *movie = fopen("route test.ctm", "w");
    CHECK(movie != NULL);
    if (movie) { fputs("0 5 A\n", movie); fclose(movie); }
    /* A ($0080) held for all five frames: $80 * (1 + 3 + 9 + 27 + 81) = $3C80. */
    p = (Program){0};
    run(&p, 42, "movie route test.ctm 5\nframe\npeek 7e0000 4\nquit\n", "ok 5\nok 5\nok 803c0000\nok\n");
    remove("route test.ctm");
    printf("route_driver_test: %d failures\n", failures);
    return failures != 0;
}

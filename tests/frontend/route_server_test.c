/* A deterministic program with locals on a main stack and a child stack.
 * Loading must reconstruct both, even when the child owns the frame wait. */
#include "route_server.h"
#include "port/fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Fiber *main, *waiting; uint32_t pads, value; unsigned resets, copies, drops, replays; } Program;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

static void child(void *ctx)
{
    Program *p = ctx;
    uint32_t local = 7;
    for (;;) {
        local = local * 3 + p->pads;
        p->value = local;
        p->waiting = fiber_current();
        fiber_switch(fiber_main());
    }
}
static void main_thread(void *ctx)
{
    Program *p = ctx;
    uint32_t local = 11;
    for (unsigned i = 0; i < 3; i++) {
        local += p->pads;
        p->value = local;
        p->waiting = fiber_current();
        fiber_switch(fiber_main());
    }
    fiber_switch(fiber_create(child, p));
    abort();
}
static bool reset(void *ctx)
{
    Program *p = ctx;
    fiber_destroy_tree(p->main);
    p->resets++;
    p->pads = p->value = 0;
    p->main = p->waiting = fiber_create(main_thread, p);
    return true;
}
static bool step(void *ctx, uint32_t pads)
{
    Program *p = ctx;
    p->pads = pads;
    fiber_switch(p->waiting);
    return true;
}
static uint8_t read_byte(void *ctx, uint32_t at)
{
    return (uint8_t)(((Program *)ctx)->value >> ((at & 3) * 8));
}
static void write_byte(void *ctx, uint32_t at, uint8_t v) { (void)at; ((Program *)ctx)->value = v; }
static bool shot(void *ctx, const char *path) { (void)ctx; return !strcmp(path, "path with spaces.png"); }
typedef struct { Program *program; uint32_t value; } Extra;
static void *capture(void *ctx)
{
    Program *p = ctx; Extra *copy = malloc(sizeof *copy);
    if (copy) { *copy = (Extra){p, p->value}; p->copies++; }
    return copy;
}
static void drop(void *ctx) { Extra *copy = ctx; copy->program->drops++; free(copy); }
static bool begin(void *ctx, const void *copy) { (void)copy; ((Program *)ctx)->replays++; return true; }
static bool end(void *ctx, const void *copy) { return ((Program *)ctx)->value == ((const Extra *)copy)->value; }

static void run(Program *p, const char *commands, const char *expected)
{
    FILE *in = tmpfile(), *out = tmpfile();
    CHECK(in && out);
    if (!in || !out) exit(1);
    fputs(commands, in); rewind(in);
    uint8_t seed[] = {1, 2, 3};
    RouteReplay replay = {capture, drop, begin, end};
    RouteHost host = {p, reset, step, read_byte, write_byte, shot, &replay};
    CHECK(route_server(&host, seed, sizeof seed, 42, in, out) == 0);
    rewind(out);
    char text[4096];
    size_t n = fread(text, 1, sizeof text - 1, out); text[n] = 0;
    if (strcmp(text, expected)) fprintf(stderr, "got:\n%s\nexpected:\n%s", text, expected);
    CHECK(!strcmp(text, expected));
    fclose(in); fclose(out);
}

int main(void)
{
    Program p = {0}; reset(&p);
    run(&p,
        "save 0\nrun 3 2\nsave 1\nrun 2 1 1\npeek 7e0000 4\nsave 2\n"
        "run 2 50\nload 1\nrun 2 1 1\npeek 7e0000 4\nload 2\npeek 7e0000 4\n"
        "load 0\nframe\ndrop 1\nload 1\nshot path with spaces.png\nquit\n",
        "ok 0\nok 3\nok 3\nok 5\nok 43000400\nok 5\n"
        "ok 7\nok 3\nok 5\nok 43000400\nok 5\nok 43000400\n"
        "ok 0\nok 0\nok\nerr empty\nok\nok\n");
    CHECK(p.resets == 3); /* the load of the identical frame needs no reset */
    CHECK(p.copies == 3 && p.drops == 3 && p.replays == 2);
    reset(&p);
    run(&p,
        "watch 7e0000:4\nrunw 3 2\nwatch bad\nrunw 0 0\n"
        "peek 7e0000 -1\nrun -1 0\nrun 1 65536\nsave 4096\n"
        "poke 7e0000 00xz\npeek 7e0000 1\npoke 7e0000 ff\nsave 0\npeek 7e0000 1\nquit\n",
        "ok\nok 3 11000000\nerr watch\nok 3 11000000\n"
        "err command\nerr command\nerr command\nerr command\nerr hex\nok 11\nok\nerr unreplayable\nok ff\nok\n");
    reset(&p);
    run(&p, "run 5 1 1\nsavefile route test.state\nrun 1 99\nloadfile route test.state\npeek 7e0000 4\nquit\n",
        "ok 5\nok 5\nok 6\nok 5\nok 43000400\nok\n");
    /* Files resume in a fresh server, independently of in-memory slots. */
    reset(&p);
    run(&p, "loadfile route test.state\npeek 7e0000 4\nquit\n", "ok 5\nok 43000400\nok\n");
    FILE *wrong = fopen("route test.state", "r+b");
    CHECK(wrong != NULL);
    if (wrong) { fseek(wrong, 8, SEEK_SET); fputc(43, wrong); fclose(wrong); }
    reset(&p);
    run(&p, "run 3 2\nloadfile route test.state\nframe\nquit\n", "ok 3\nerr file\nok 3\nok\n");
    wrong = fopen("route test.state", "r+b");
    CHECK(wrong != NULL);
    if (wrong) { fseek(wrong, 8, SEEK_SET); fputc(42, wrong); fclose(wrong); }
    FILE *bad = fopen("route test.state", "ab");
    CHECK(bad != NULL);
    if (bad) { fputc(0, bad); fclose(bad); }
    reset(&p);
    run(&p, "loadfile route test.state\nframe\nquit\n", "err file\nok 0\nok\n");
    remove("route test.state");
    FILE *movie = fopen("route test.ctm", "w");
    CHECK(movie != NULL);
    if (movie) { fputs("0 5 A\n", movie); fclose(movie); }
    reset(&p);
    run(&p, "movie route test.ctm 5\npeek 7e0000 4\nquit\n", "ok 5\nok 3f020000\nok\n");
    remove("route test.ctm");
    fiber_destroy_tree(p.main);
    printf("route_server_test: %d failures\n", failures);
    return failures != 0;
}

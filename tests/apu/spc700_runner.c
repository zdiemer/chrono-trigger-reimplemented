/* Runs converted SingleStepTests SPC700 files (see fetch_sst_spc700.py) against
 * the interpreter, checking registers, memory and the cycle count.
 *
 *   spc700_runner [-v] tests/apu/data/<op>.bin ...
 *
 * Prints one line per failing file and a summary; exits non-zero on any failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../runtime/apu/spc700.h"

typedef struct {
    uint16_t pc;
    uint8_t a, x, y, sp, psw;
    uint16_t ram_count;
    uint16_t *addr;
    uint8_t *val;
} State;

static uint8_t mem[0x10000];

static uint8_t bus_read(void *ctx, uint16_t addr) { (void)ctx; return mem[addr]; }
static void bus_write(void *ctx, uint16_t addr, uint8_t v) { (void)ctx; mem[addr] = v; }

static const uint8_t *rd_state(const uint8_t *p, State *s)
{
    s->pc = (uint16_t)(p[0] | p[1] << 8);
    s->a = p[2]; s->x = p[3]; s->y = p[4]; s->sp = p[5]; s->psw = p[6];
    s->ram_count = (uint16_t)(p[7] | p[8] << 8);
    p += 9;
    s->addr = malloc(sizeof *s->addr * (s->ram_count + 1));
    s->val = malloc(s->ram_count + 1);
    for (int i = 0; i < s->ram_count; i++) {
        s->addr[i] = (uint16_t)(p[0] | p[1] << 8);
        s->val[i] = p[2];
        p += 3;
    }
    return p;
}

static void free_state(State *s) { free(s->addr); free(s->val); }

static int run_file(const char *path, int verbose, long *tests_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) { fclose(f); free(buf); return -1; }
    fclose(f);

    const uint8_t *p = buf;
    uint32_t count = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
    p += 4;
    int failures = 0;
    Spc700 spc;
    spc700_init(&spc, (Spc700Bus){ NULL, bus_read, bus_write });

    for (uint32_t t = 0; t < count; t++) {
        State in, out;
        p = rd_state(p, &in);
        p = rd_state(p, &out);
        unsigned want_cycles = p[0] | p[1] << 8;
        p += 2;
        for (int i = 0; i < in.ram_count; i++) mem[in.addr[i]] = in.val[i];

        spc.pc = in.pc; spc.a = in.a; spc.x = in.x; spc.y = in.y; spc.sp = in.sp; spc.psw = in.psw;
        spc.halted = false;
        uint8_t opcode = mem[in.pc];
        unsigned got_cycles = spc700_step(&spc);
        /* SLEEP and STOP halt the chip; the suite records however many idle cycles
         * its harness ran afterwards, which isn't a property of the instruction. */
        if (opcode == 0xEF || opcode == 0xFF)
            got_cycles = want_cycles;

        char why[256] = "";
        int n = 0;
#define CHK(field, want, w)                                                                  \
        if ((unsigned)(spc.field) != (unsigned)(want) && n < (int)sizeof why - 40)           \
            n += snprintf(why + n, sizeof why - n, " " #field "=%0" #w "X(want %0" #w "X)",  \
                          (unsigned)(spc.field), (unsigned)(want));
        CHK(pc, out.pc, 4) CHK(a, out.a, 2) CHK(x, out.x, 2) CHK(y, out.y, 2) CHK(sp, out.sp, 2)
        CHK(psw, out.psw, 2)
#undef CHK
        if (got_cycles != want_cycles)
            n += snprintf(why + n, sizeof why - n, " cycles=%u(want %u)", got_cycles, want_cycles);
        for (int i = 0; i < out.ram_count; i++) {
            if (mem[out.addr[i]] != out.val[i] && n < (int)sizeof why - 40)
                n += snprintf(why + n, sizeof why - n, " [%04X]=%02X(want %02X)",
                              out.addr[i], mem[out.addr[i]], out.val[i]);
        }
        if (n) {
            if (failures < verbose)
                printf("  %s test %u:%s\n", path, t, why);
            failures++;
        }
        for (int i = 0; i < in.ram_count; i++) mem[in.addr[i]] = 0;
        for (int i = 0; i < out.ram_count; i++) mem[out.addr[i]] = 0;
        free_state(&in);
        free_state(&out);
    }
    free(buf);
    *tests_out = count;
    return failures;
}

int main(int argc, char **argv)
{
    int verbose = 0, files = 0, bad_files = 0;
    long total = 0, total_fail = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) { verbose = 3; continue; }
        long n = 0;
        const char *name = strrchr(argv[i], '/');
        name = name ? name + 1 : argv[i];
        int fails = run_file(argv[i], verbose, &n);
        if (fails < 0) { bad_files++; continue; }
        files++;
        total += n;
        total_fail += fails;
        if (fails) {
            bad_files++;
            printf("FAIL %s: %d/%ld\n", name, fails, n);
        }
    }
    printf("%d files, %ld tests, %ld failures, %d failing files\n", files, total, total_fail, bad_files);
    return total_fail || bad_files;
}

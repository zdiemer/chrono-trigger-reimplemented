/* Runs converted SingleStepTests 65816 files (see fetch_sst.py) against the
 * interpreter.
 *
 *   sst_runner [-v] tests/cpu/data/<op>.<mode>.bin ...
 *
 * Prints one line per file and a summary; exits non-zero on any failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../runtime/cpu/cpu65816.h"

typedef struct {
    uint16_t pc, s, a, x, y, d;
    uint8_t p, dbr, pbr, e;
    uint16_t ram_count;
    uint32_t *addr;
    uint8_t *val;
} State;

static uint8_t *mem;

static uint8_t bus_read(void *ctx, uint32_t addr) { (void)ctx; return mem[addr]; }
static void bus_write(void *ctx, uint32_t addr, uint8_t v) { (void)ctx; mem[addr] = v; }

static const uint8_t *rd_state(const uint8_t *p, State *s)
{
#define U8() (*p++)
#define U16() (p += 2, (uint16_t)(p[-2] | p[-1] << 8))
    s->pc = U16(); s->s = U16(); s->p = U8(); s->a = U16(); s->x = U16(); s->y = U16();
    s->dbr = U8(); s->d = U16(); s->pbr = U8(); s->e = U8(); s->ram_count = U16();
    s->addr = malloc(sizeof *s->addr * (s->ram_count + 1));
    s->val = malloc(s->ram_count + 1);
    for (int i = 0; i < s->ram_count; i++) {
        s->addr[i] = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
        s->val[i] = p[4];
        p += 5;
    }
    return p;
#undef U8
#undef U16
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
    Cpu cpu;
    cpu_init(&cpu, (CpuBus){ NULL, bus_read, bus_write });

    for (uint32_t t = 0; t < count; t++) {
        State in, out;
        p = rd_state(p, &in);
        p = rd_state(p, &out);
        for (int i = 0; i < in.ram_count; i++) mem[in.addr[i]] = in.val[i];

        cpu.pc = in.pc; cpu.s = in.s; cpu.p = in.p; cpu.a = in.a; cpu.x = in.x; cpu.y = in.y;
        cpu.db = in.dbr; cpu.d = in.d; cpu.pb = in.pbr; cpu.e = in.e;
        cpu.nmi_pending = cpu.irq_line = cpu.waiting = cpu.stopped = false;
        if (cpu.e)   /* S is confined to page 1 in emulation mode */
            cpu.s = 0x0100 | (cpu.s & 0xFF);
        uint8_t opcode = mem[(uint32_t)in.pbr << 16 | in.pc];
        if (opcode == 0x44 || opcode == 0x54) {
            /* The suite captures block moves partway through, after a fixed number of
             * cycles, with PC mid-instruction. Run the same number of iterations and
             * don't compare PC. */
            uint16_t iterations = (uint16_t)(in.a - out.a);
            for (unsigned i = 0; i < iterations; i++)
                cpu_step(&cpu);
            cpu.pc = out.pc;
        } else {
            cpu_step(&cpu);
        }

        char why[256] = "";
        int n = 0;
#define CHK(field, want, w)                                                                  \
        if ((unsigned)(cpu.field) != (unsigned)(want) && n < (int)sizeof why - 40)           \
            n += snprintf(why + n, sizeof why - n, " " #field "=%0" #w "X(want %0" #w "X)",  \
                          (unsigned)(cpu.field), (unsigned)(want));
        CHK(pc, out.pc, 4) CHK(s, out.s, 4) CHK(p, out.p, 2) CHK(a, out.a, 4) CHK(x, out.x, 4)
        CHK(y, out.y, 4) CHK(db, out.dbr, 2) CHK(d, out.d, 4) CHK(pb, out.pbr, 2) CHK(e, out.e, 1)
#undef CHK
        for (int i = 0; i < out.ram_count; i++) {
            if (mem[out.addr[i]] != out.val[i] && n < (int)sizeof why - 40)
                n += snprintf(why + n, sizeof why - n, " [%06X]=%02X(want %02X)",
                              out.addr[i], mem[out.addr[i]], out.val[i]);
        }
        if (n) {
            if (failures < verbose)
                printf("  test %u:%s\n", t, why);
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
    mem = calloc(1 << 24, 1);
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

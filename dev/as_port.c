/* The original's routine as if it were a hand port: see as_port.h. */
#include "as_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IO_CYCLE_CLOCKS = 6 };   /* an internal CPU cycle, as the system counts it */

static void run_original_now(Snes *s)
{
    const NativeEntry *e = native_running_entry();
    Cpu *c = &s->cpu;
    uint16_t entry_s = c->s;
    bool nmi = c->nmi_pending, irq = c->irq_line;
    c->nmi_pending = false;   /* time is stopped: nothing is raised while it runs */
    c->irq_line = false;
    CpuStep (*step)(Cpu *) = s->interpreter ? s->interpreter : cpu_step;
    unsigned long n = 0;
    uint32_t io = 0;
    while (c->s <= entry_s && !c->stopped && !c->waiting) {
        if (++n > AS_PORT_STEP_LIMIT) {
            fprintf(stderr, "as-port %s: no return after %d instructions (waiting for time to pass?); "
                            "stopped at $%02X:%04X, frame %u\n",
                    e ? e->name : "?", AS_PORT_STEP_LIMIT, c->pb, c->pc, (unsigned)s->frame);
            break;
        }
        c->io_cycles = 0;
        step(c);
        io += c->io_cycles * IO_CYCLE_CLOCKS;
    }
    /* The bus counted its own accesses as they ran; the internal cycles are
     * added so the time owed is the routine's whole time. */
    s->bus.clocks += io;
    c->io_cycles = 0;
    c->nmi_pending = nmi;
    c->irq_line = irq;
}

static uint32_t parse_addr(const char *p, const char **end)
{
    char digits[8];
    int k = 0;
    for (; *p && *p != ','; p++) {
        if (*p == '$' || *p == ':')
            continue;
        if (k < 7)
            digits[k++] = *p;
    }
    digits[k] = 0;
    *end = p;
    char *stop;
    unsigned long v = strtoul(digits, &stop, 16);
    return k == 6 && !*stop ? (uint32_t)v : 0xFFFFFFFFu;
}

int as_port_add(NativeTable *t, const char *spec)
{
    for (const char *p = spec; *p;) {
        const char *end;
        uint32_t addr = parse_addr(p, &end);
        if (addr == 0xFFFFFFFFu) {
            fprintf(stderr, "--as-port: %s is not a list of BBAAAA addresses\n", spec);
            return -1;
        }
        NativeEntry *grown = realloc(t->entries, sizeof *grown * (size_t)(t->count + 1));
        char *name = malloc(24);
        if (!grown || !name) {
            fprintf(stderr, "out of memory\n");
            return -1;
        }
        t->entries = grown;
        snprintf(name, 24, "as_port_%06X", (unsigned)addr);
        t->entries[t->count++] = (NativeEntry){ .addr = addr, .name = name, .fn = run_original_now,
                                                .cost_milli = 1000 };
        t->ported++;
        p = *end ? end + 1 : end;
    }
    return 0;
}

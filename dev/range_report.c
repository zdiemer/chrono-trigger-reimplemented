#include "range_report.h"

#include <string.h>

#include "port/vars_ram.h"

/* `[64]` for an element past an array's count, `[at 300]` or `[row 70]` for
 * the other bounds, so the line says which parameter was out of range. */
static void print_index(FILE *f, const VarsRangeHit *h)
{
    static const struct {
        const char *bound, *param;
    } params[] = { { "bytes", "at " }, { "rows", "row " }, { "columns", "column " }, { "instances", "instance " } };
    const char *param = "";
    for (unsigned i = 0; i < sizeof params / sizeof params[0]; i++)
        if (strcmp(h->bound, params[i].bound) == 0)
            param = params[i].param;
    fprintf(f, "[%s%u]", param, h->index);
}

static void print_at(FILE *f, uint32_t pc) { fprintf(f, "$%02X:%04X", (unsigned)(pc >> 16), (unsigned)(pc & 0xFFFF)); }

unsigned range_report(FILE *f)
{
    unsigned n, out = 0;
    const VarsRangeFault *r = vars_range_faults(&n);
    for (unsigned i = 0; i < n; i++) {
        if (!r[i].count)
            continue;
        const VarsRangeHit *first = &r[i].first[0];
        fprintf(f, "accessor out of range: %s", r[i].var);
        print_index(f, first);
        fprintf(f, " (%s %u) x%lu (first at ", first->bound, first->limit, r[i].count);
        print_at(f, first->pc);
        fprintf(f, " in %s", first->accessor);
        for (unsigned k = 1; k < r[i].kept; k++) {
            fprintf(f, k == 1 ? "; then " : ", ");
            print_index(f, &r[i].first[k]);
            fputc(' ', f);
            print_at(f, r[i].first[k].pc);
            if (strcmp(r[i].first[k].accessor, first->accessor) != 0)
                fprintf(f, " in %s", r[i].first[k].accessor);
        }
        fprintf(f, ")\n");
        out++;
    }
    return out;
}

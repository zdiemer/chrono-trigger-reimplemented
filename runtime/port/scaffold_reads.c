/* ct_dev --scaffold-reads: port reads of bytes whose latest write was
 * scaffolding (runtime/port/scaffold_reads.h). */
#include "port/scaffold_reads.h"

#include <stdlib.h>

#include "port/native.h"

int original_effect_depth;

typedef struct {
    uint32_t byte;              /* index into work RAM */
    const NativeEntry *entry;   /* the port that read it, or NULL */
    const NativeEntry *writer;  /* the port whose scaffolding wrote it, the first time */
    unsigned long count;
    uint64_t first_frame;
} Read;

enum { MAX_READS = 4096 };
static Read reads[MAX_READS];
static int nreads;
static unsigned long dropped;
static const NativeEntry *writers[SNES_WRAM_SIZE];   /* by byte: the latest scaffolding writer */

void scaffold_reads_start(Snes *s)
{
    static uint8_t marks[SNES_WRAM_SIZE];
    s->bus.wram_scaffold = marks;
    s->bus.scaffolding = &original_effect_depth;
}

/* The work RAM byte a bus address reaches, as the accessors spell them: banks
 * $7E-$7F, or the first 8 KiB of a system bank. */
static bool wram_byte(uint32_t addr, uint32_t *out)
{
    uint8_t bank = (uint8_t)(addr >> 16);
    uint16_t low = (uint16_t)addr;
    if (bank == 0x7E || bank == 0x7F) {
        *out = (uint32_t)(bank & 1) << 16 | low;
        return true;
    }
    if ((bank & 0x40) == 0 && low < 0x2000) {
        *out = low;
        return true;
    }
    return false;
}

void scaffold_read(Snes *s, uint32_t addr)
{
    uint32_t byte;
    if (original_effect_depth || !wram_byte(addr, &byte) || !s->bus.wram_scaffold[byte])
        return;
    const NativeEntry *e = native_running_entry();
    for (int i = 0; i < nreads; i++) {
        if (reads[i].byte == byte && reads[i].entry == e) {
            reads[i].count++;
            return;
        }
    }
    if (nreads == MAX_READS) {
        dropped++;
        return;
    }
    reads[nreads++] = (Read){ byte, e, writers[byte], 1, s->frame };
}

void scaffold_write(Snes *s, uint32_t addr)
{
    (void)s;
    uint32_t byte;
    if (wram_byte(addr, &byte))
        writers[byte] = native_running_entry();
}

int scaffold_reads_report(FILE *out)
{
    for (int i = 0; i < nreads; i++) {
        const Read *r = &reads[i];
        fprintf(out, "scaffold read: $%02X:%04X by %s (%lu times, first at frame %llu, written by %s)\n",
                0x7E + (unsigned)(r->byte >> 16), (unsigned)(r->byte & 0xFFFF),
                r->entry && r->entry->name ? r->entry->name : "(no port)", r->count,
                (unsigned long long)r->first_frame, r->writer && r->writer->name ? r->writer->name : "?");
    }
    if (dropped)
        fprintf(out, "scaffold read: %lu more not kept\n", dropped);
    return nreads;
}

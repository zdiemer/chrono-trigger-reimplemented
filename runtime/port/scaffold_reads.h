/* Reads of scaffolding, caught in the act: ct_dev --scaffold-reads.
 *
 * What a port writes inside ORIGINAL_EFFECT (game/original_effects.h) is
 * scaffolding, which the native build leaves out; the port's logic must never
 * read it back. tools/effects_check.py looks for such reads in the source; this
 * is the same question asked of a running game, in the shipping mode, where
 * order is known. The bus marks every work RAM byte with whether its latest
 * write was made inside an ORIGINAL_EFFECT (SnesBus.wram_scaffold), and the
 * accessors' reads (runtime/port/vars_ram.c) report a byte so marked when a
 * port reads it outside one: in the native build that read would find what
 * was there before the scaffolding's write. Reads while a guard peeks don't
 * count. A write the original's code makes clears the mark, so a value the
 * game itself sets again is not reported.
 *
 * Shipping mode only (`--native on`): in shadow mode a port runs on a copy of
 * the machine, which carries no marks. docs/PORTING.md, "Scaffolding". */
#ifndef CT_PORT_SCAFFOLD_READS_H
#define CT_PORT_SCAFFOLD_READS_H

#include <stdio.h>

#include "snes/system.h"

/* How many ORIGINAL_EFFECTs are being evaluated (game/original_effects.h). */
extern int original_effect_depth;

/* Start marking s's work RAM, from now on. */
void scaffold_reads_start(Snes *s);

/* A port read `addr` (a bus address): note it if its byte is marked. */
void scaffold_read(Snes *s, uint32_t addr);

/* An accessor wrote `addr`: inside an ORIGINAL_EFFECT, note which port did,
 * for the report. (The bus marks the byte whoever writes it.) */
void scaffold_write(Snes *s, uint32_t addr);

static inline void scaffold_note_read(Snes *s, uint32_t addr)
{
    if (s->bus.wram_scaffold)
        scaffold_read(s, addr);
}

static inline void scaffold_note_write(Snes *s, uint32_t addr)
{
    if (s->bus.wram_scaffold && original_effect_depth)
        scaffold_write(s, addr);
}

/* One line per (byte, port): `scaffold read: $7E:1234 by NAME (N times, first
 * at frame F, written by WRITER)`, the writer the port whose scaffolding wrote
 * the byte the first time (`?` for a write not through an accessor). Returns
 * how many lines. */
int scaffold_reads_report(FILE *out);

#endif

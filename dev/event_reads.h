/* ct_dev --event-reads: what reads the effects an event command leaves beyond
 * its protocol (docs/RECOMP.md, "The event commands' scaffolding, measured").
 *
 * A command handler, entered through the table at $C0:5D6E, returns X (where
 * the script goes on) and the carry (whether the turn goes on); that is the
 * protocol. It also leaves Y (where its parse of its operands stopped), the V
 * flag, and whatever it wrote to the field page's event cells. This follows
 * those three on the interpreter, instruction by instruction, and counts every
 * read of one of them that the command didn't make itself:
 *
 * - Y and V from the moment a handler returns until something overwrites them.
 *   A push (PHY, PHP) carries them onto the stack and a pull brings them back;
 *   any other read of a pushed copy counts. Interrupt handlers are skipped:
 *   they save and restore what they use.
 * - each byte of $01C1-$01E4 (the event cells of the field page: the count,
 *   the saved position, the pointers, the scratch cells) a command writes,
 *   until something outside a
 *   command writes it again: reads in the command's own handler body, in the
 *   routines it calls, in a later command, outside any command and in
 *   interrupt handlers are counted apart.
 *
 * Meaningful with --native off --generated off, where everything the CPU runs is
 * the original's and the interpreter's instruction hook sees all of it. */
#ifndef CT_DEV_EVENT_READS_H
#define CT_DEV_EVENT_READS_H

#include <stdio.h>

#include "snes/system.h"

typedef struct EventReads EventReads;

/* Starts observing the machine's work RAM. */
EventReads *event_reads_create(Snes *s);
/* From the CPU's instruction hook: the interpreter is about to run `pc`. */
void event_reads_note_instruction(EventReads *r, uint32_t pc);
/* Writes the counts to `path` and a summary to `summary`. Returns 0 on success. */
int event_reads_write(EventReads *r, const char *path, FILE *summary);
void event_reads_destroy(EventReads *r);

#endif

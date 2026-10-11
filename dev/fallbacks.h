/* The fallback measure: how much of a run was the original's code --
 * generated or interpreted -- running in place of hand ports, and why
 * (docs/RECOMP.md, "The fallback measure"; tools/fallbacks.py reads what this
 * writes). In the shipping mode (`--native on --generated on`) the ports run, and
 * what runs besides is counted. In shadow mode the original runs everything,
 * and the runtime says where a port stands for it (NativeObserver): from where
 * the original reaches a port's entry until it reaches the port's call-out,
 * stop or return, the original is running what the port does, and isn't
 * counted; a wait loop's pass that comes round to its head again is an idle
 * port's, and is taken back. Since the original's play doesn't change with
 * the ports' timing, that count is the same every run of a movie until a port
 * changes: the gate's ratchet is taken over it.
 *
 * Every instruction the original's code executes is counted by its address,
 * under the reason it ran there:
 *
 *   refused   a hand port is registered where the call began, and every hand
 *             entry there turned it away (its precondition on P, the CPU's
 *             mode, or its guard): the original runs the whole call;
 *   handback  a hand port handed the rest to the original: the callee of a
 *             call-out it made, or the code after the point it stopped at
 *             (m_stop_at, or a PC it set), up to where its routine returns;
 *   skipped   a hand entry applied but the runtime didn't run it (the nesting
 *             of suspended routines was at its limit);
 *   unported  no hand port is involved: code nobody has ported yet.
 *
 * The reason is a dynamic extent, followed with the stack: a refused call
 * lasts until S rises above where it was entered (its return), a call-out
 * until its callee returns, a hand-back until the port's routine returns. An
 * interrupt starts an extent of its own (unported unless something inside it
 * says otherwise), so a handler that interrupts a refused call isn't charged
 * to it. Each instruction is also filed under the routine it belongs to: the
 * target of the latest call the original's code made (seen in the instruction
 * stream), or where the extent began; code reached with no call on record
 * (the main loop, a stack another task switched to) is filed under its bank.
 * Stacks switched by a task (S far below the innermost extent's) are counted
 * as unported, since the extent can't be followed there.
 *
 * Refusals are also recorded per entry with the first few machine states, for
 * the gate's refusal check (tools/fallbacks.py --refusals).
 *
 * One measure per process: the generated code's hook and the interpreter's
 * wrapper are global. */
#ifndef CT_DEV_FALLBACKS_H
#define CT_DEV_FALLBACKS_H

#include <stdio.h>

#include "port/native.h"
#include "snes/system.h"

typedef struct Fallbacks Fallbacks;

/* Measures the machine `s`: installs the generated code's instruction hook and
 * makes s->interpreter a wrapper of cpu_step that notes interrupt entries (a
 * host that sets s->interpreter again sets it to fallbacks_interpreter). The
 * host feeds the interpreter's instructions with fallbacks_note, from its own
 * on_insn, or sets s->cpu.on_insn to fallbacks_on_insn with the measure as
 * its context. */
Fallbacks *fallbacks_create(Snes *s);
/* Observe a runtime's hand-offs (native_set_observer): again after every
 * restart. `shadow`: the runtime is in SHADOW mode (see the header). */
void fallbacks_attach(Fallbacks *f, NativeRuntime *rt, bool shadow);
CpuStep fallbacks_interpreter(Cpu *cpu);
void fallbacks_note(Fallbacks *f, uint32_t pc, uint8_t p, bool e);
void fallbacks_on_insn(void *ctx, uint32_t pc, uint8_t p, bool e);
/* The machine was replaced (a state or a copy loaded): the extents in progress
 * are forgotten, and counting goes on from the base. */
void fallbacks_reset(Fallbacks *f);
/* Writes the counts (the format is tools/fallbacks.py's; a run's own file, so
 * tools add files rather than this adding to one). Returns 0 on success. */
int fallbacks_write(Fallbacks *f, const char *path);
/* One line for the run's log: instructions by reason. */
void fallbacks_summary(const Fallbacks *f, FILE *out);
void fallbacks_destroy(Fallbacks *f);

#endif

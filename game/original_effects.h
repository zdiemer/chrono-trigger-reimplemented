/* The original's effects: what a port does only because the original does it
 * and something that isn't the game still looks.
 *
 * A port reproduces two kinds of thing the game itself has no use for:
 *
 * - what shadow mode compares: a scratch cell the original writes and nothing
 *   reads again, a register or a flag the original's callers can't be shown
 *   to ignore (the liveness analysis can't follow them), which a don't-care
 *   declaration therefore can't drop;
 * - what the original's still-generated code reads through its own calling
 *   conventions: a register it was handed, a cell it expects filled.
 *
 * Both are scaffolding. They stand while the gate is byte-exact and the
 * original's code runs beside the port, and they come down with it
 * (docs/PLAN.md, "Retiring the gate, deliberately"). So they are written apart
 * from the logic, each through ORIGINAL_EFFECT, and never read back by it:
 *
 *     ORIGINAL_EFFECT(keep_compared(g, left, right));
 *
 * Building with CT_WITHOUT_ORIGINAL_EFFECTS defined (CMake:
 * -DCT_ORIGINAL_EFFECTS=OFF) turns every one of them into nothing: the
 * expression is still compiled, so it can't rot, but never evaluated. That
 * build is the shape of a native one. It is not expected to pass the gate --
 * shadow mode then sees exactly what was left out -- and it is built in CI so
 * that the logic is shown not to depend on any of it. docs/PORTING.md,
 * "Scaffolding", says when to reach for this and how tools/portstyle.py counts
 * it. */
#ifndef CT_GAME_ORIGINAL_EFFECTS_H
#define CT_GAME_ORIGINAL_EFFECTS_H

#ifdef CT_WITHOUT_ORIGINAL_EFFECTS
#define ORIGINAL_EFFECTS 0
/* Behind a constant false, not inside sizeof: an unevaluated operand doesn't
 * count as a use, so clang would call a helper only scaffolding calls unneeded. */
#define ORIGINAL_EFFECT(...) ((void)(0 && ((__VA_ARGS__), 1)))
#else
#define ORIGINAL_EFFECTS 1
/* The count lets the runtime tell scaffolding's writes from the logic's
 * (ct_dev --scaffold-reads, runtime/port/scaffold_reads.h): a byte whose latest
 * write was made in here and that the logic then reads is one the native build
 * would read stale. tools/effects_check.py asks the same of the source. */
extern int original_effect_depth;
#define ORIGINAL_EFFECT(...) ((void)(original_effect_depth++, (__VA_ARGS__), original_effect_depth--))
#endif

#endif

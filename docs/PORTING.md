# Writing the game in C

The product is ct_native. Change game behavior through named operations on game
state, with hardware access behind `runtime/platform/`. CLAUDE.md's rules are
the portability and review bar: plain integer C, small locals, no host library
except memcpy/memset/memcmp, typed state accessors and named asset resources.
There is no machine entry layer. Each operation takes `GameState *g`, the
`Platform *pf` services it needs, and named inputs; its result is a C value.

## Native functions

Use separate state and service parameters: a palette calculation needs only
state, a screen upload only services, and a mode loop needs both. Do not build
a replacement CPU context. For example, warp's frame operation takes its
`WarpKind`, updates the plane, light and circle, and returns whether the trip
continues. `warp_runs` waits through `tm_wait_frame`, calls that operation,
then restores the story flags and returns to the program loop.

Core owns mode hand-offs and frame callbacks. Install a `ProgramHandler` with
`program_install_vblank_handler` or `program_install_line_handler`; passing
NULL disables that callback. The timing service decides when enabled handlers
run. A mode returns normally; scene, title and reset requests use the named
program operations. `program_fail` reports invalid or unsupported game state;
it never dispatches to cartridge code.

## Converting a subsystem

1. Follow the native caller graph. Wrappers, held continuations, emulated
   returns and registry rows have been deleted across the tree. Simplify the
   remaining single-implementation callback adapters into direct calls, keeping
   real game-state checks and observable ordering.
2. Replace register-shaped inputs and leftovers with meaningful parameters and
   results. Delete unused results, code-image guards, CPU-width/decimal-mode
   checks and scratch effects. Preserve index wrapping and deliberate overruns
   that live callers can observe. Use typed accessors; coordinate storage
   ownership separately through STATE.md.
3. Keep `TIMED_AS` scopes and the authored rates in `runtime/port/time_rates.c`.
   A timing key or `PF_AT`/`PF_CALL` origin identifies work, not a callable
   address. Removing reads can remove time even when their values are unused:
   preserve counted work with an explicit charge and explain its derivation.
   Never keep dummy register writes or change rates to make an outcome pass.
4. Use named resource and platform operations. The remaining video escapes
   need services expressing their effects. Core and warp have none; changes
   to field, battle, menu, scene and overworld can stay inside those modules
   and their service implementations without changing core dispatch.
5. Delete the old helper, declaration, test or exception with its last caller.
   Run source inventories and the lean gate, including outcomes and picture
   fingerprints. A clean build alone cannot establish unchanged frame pacing.

## Scaffolding

Comparison-only scratch writes have retired. Keep working state in C values;
do not recreate dead register, flag or scratch effects. Preserve real saved
game state and observable behavior under the native movie and route checks.
The source inventory `tools/machine_joints.py` must report zero scaffolding writes.

## Validation

Run `python3 tools/suite.py --ct-native build/main/ct_native -j 2` and read its
last line. Only `suite OK` passes, under ten minutes including unit tests.
Use a targeted native route when the change affects a route outside the fast
set. Preserve save/place baseline protections; exceptions require a concrete
reviewable reason and must pin the observed difference.

## Review and status

Run portstyle and portsmell before qualitative review. Mechanical checks do not
prove that the source expresses the game clearly. Read the subsystem as a reader
who has not seen the implementation report; the review rubric is in
`.claude/skills/review-port/SKILL.md`. Keep original routine addresses beside
the operations that implement them; the retired registry ledger is unnecessary.
Keep as-built mechanisms in REIMPLEMENTATION.md, open work in TODO.md and order
in PLAN.md. Delete finished plan items.

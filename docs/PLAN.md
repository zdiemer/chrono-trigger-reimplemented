# Plan

The product is ct_native. The targets are desktop native play, experimental
GBA/Wii/GameCube ports, and eventually a SNES ROM compiled from the hand-written
C. Observable divergence is accepted where the game stays structurally the same.

## Working order

1. **The lean gate and a minutes-per-change landing loop.** Agents iterate on
   the sampled native gate, including unit tests, within ten minutes at `-j 2`.
   Land coherent changes with current docs; one qualitative review per merge,
   then push and CI. The full-library/every-route batch is non-blocking: findings
   become fixes and never hold up a commit, merge or push.
2. **The machine out of game/.** Work module by module under the lean gate.
   Give the sound driver's register write a meaning (TODO.md, Platform).
   PORTING.md gives the native function and callback pattern. Preserve real state contracts,
   timing charges, cartridge saves, route queries and recorded save/place
   protections. Measure progress with the source and storage inventories.
3. **ct_native plays the game by its own checks.** Exercise every native
   route, now that slot loads are snapshots, turn findings into fixes, and add
   a playthrough from a new game to an ending. The broad batch informs this
   work without becoming a landing gate.

## Phase 4b: platform milestones

Decode assets one resource at a time into named, editable data. Widen the
renderer and add GPU/controller backends. Keep the portability subset for
smaller targets; measure stacks and memory before the GBA experiment.

A relocatable asset layout and a SNES target follow from the C logic, with
symbolic references and target glue. See TODO.md for the remaining work.

## Validation

CLAUDE.md gives the blocking gate and the separate `suite.py --long` command.
Only the lean gate reports `suite OK`. The batch retains its logs and findings
for follow-up fixes; it has no ten-minute threshold and is never a prerequisite
for landing work.

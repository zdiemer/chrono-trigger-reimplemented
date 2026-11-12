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

Decode assets one resource at a time into named, editable data. Finish the
wider view (PLATFORM.md, "The wider view, as built", says what shows at its
edges) and add GPU/controller backends. Keep the portability subset for
smaller targets; measure stacks and memory before the GBA experiment.

A relocatable asset layout and a SNES target follow from the C logic, with
symbolic references and target glue. See TODO.md for the remaining work.

### Layering

`game/` stays the reimplementation alone; ports and enhancements live beside it.

- **Backends and targets** (a GPU renderer, controllers, GBA, Wii/GameCube)
  implement the services in `runtime/platform/`, one directory per backend
  beside `runtime/platform/native/`, with a driver in `frontend/` or its own
  frontend. They never change `game/`.
- **Asset decoders** serve the named resource accessors through the assets
  provider. A decoded or edited resource reaches the ports unchanged.
- **Enhancements that change what is shown** (widescreen first) parameterize
  the game rather than patch it. `game/` asks a service for the view size and
  reads named options. At their defaults it is exactly the reimplementation,
  and the gate runs only at the defaults, so every movie, outcome and route
  keeps measuring the original. An option may change how much is shown and
  where it is laid out, never the game's rules. Each enhancement adds its own
  checks on top of the gate.
- **Rule changes** (difficulty, quality-of-life) are not planned. If wanted,
  they would be a separate, named layer with the same defaults-are-original
  rule, not edits threaded through the ports.

`tests/test_platform_lint.py` holds the line. `game/` includes only:

- its own headers;
- the service interfaces at the top of `runtime/platform/`;
- the typed state's storage and the fiber interface from `runtime/port/`;
- the four C headers of the portability subset.

A backend, decoder, frontend or enhancement header in `game/` fails the gate.

## Validation

CLAUDE.md gives the blocking gate and the separate `suite.py --long` command.
Only the lean gate reports `suite OK`. The batch retains its logs and findings
for follow-up fixes; it has no ten-minute threshold and is never a prerequisite
for landing work.

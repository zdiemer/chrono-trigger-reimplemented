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
   Give the sound driver's register write a meaning (issues labelled area: sound).
   PORTING.md gives the native function and callback pattern. Preserve real state contracts,
   timing charges, cartridge saves, route queries and recorded save/place
   protections. Measure progress with the source and storage inventories.
3. **ct_native plays the game by its own checks.** Exercise every native
   route, now that slot loads are snapshots, turn findings into fixes, and add
   a playthrough from a new game to an ending. The broad batch informs this
   work without becoming a landing gate.

## Phase 4b: platform milestones

The GBA port is the focus. Its bar is full speed on real hardware (a flash
cart, an Analogue Pocket), with mGBA the first milestone. The order:

1. **mGBA headless (issue #365).** ct_native built for the GBA with the lean
   configuration (`-DCT_LEAN=ON`, which leaves out the program's own
   diagnostics and plays every gate movie as the normal build does), playing
   a movie in mGBA with no picture or sound.
2. **The hot spots the lean build's profile found** (PLATFORM.md, "Memory,
   stacks and per-frame work against small targets"): the state's address
   view (issue #424), the video uploads' byte reads (#425), the time model's
   per-access charges (#426), the overlays' last writer (#427), the sound
   mixer (#428) and the interpreters' byte-at-a-time reads (#429). The lean
   build's game and sound are 11-25 times a GBA frame's work before them,
   the game's own logic a fifth of that.
3. **Video (issue #367),** then **the 240x160 view (issue #366):** decided,
   a camera crops the original's picture and the windows are laid out again
   for the smaller screen; nothing is scaled.
4. **Audio, input and saves (issue #368).**

What a player gets is a standalone builder (issue #421), released on GitHub
as ct_native is: it takes the player's SNES cartridge, verifies it, converts
its assets to the GBA's formats (the packer, issue #369) and writes a `.gba`.
It embeds the compiled GBA program, which is code only, so no cartridge data
is ever distributed.

Decoding assets into named, editable data, the wider view (PLATFORM.md, "The
wider view, as built", says what shows at its edges) and GPU/controller
backends continue beside it. A relocatable asset layout and a SNES target
follow from the C logic, with symbolic references and target glue. The
remaining work is in the "Phase 4b: platform milestones" and "SNES ROM and
small targets" milestones.

### Layering

`game/` stays the reimplementation alone; ports and enhancements live beside it.

- **Backends and targets** (a GPU renderer, controllers, GBA, Wii/GameCube)
  implement the services in `runtime/platform/` and never change `game/`.
  The desktop's backends stay in `runtime/platform/native/` and its driver in
  `frontend/`. A target lives whole under `targets/<name>/`: for the GBA,
  `targets/gba/` holds the platform services' backends, the driver and its
  frame loop, the ARM fiber switch, the linker script, the CMake toolchain
  file, the ROM packing tool and the builder (issue #421), leaving room for
  `targets/snes/`. The lean build is a configuration, not a target's code, and
  stays in the normal tree.
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

## Follow-ups

Open follow-ups are GitHub issues
(<https://github.com/zdiemer/chrono-trigger-reimplemented/issues>), one per
piece of work, each under the milestone for the step it advances: "Step 2: the machine
out of game/", "Step 3: plays by its own checks", "Phase 4b: platform
milestones", "SNES ROM and small targets" and "Port quality". CLAUDE.md
("Follow-ups") says how to file one; issue #268 explains the labels.

## Validation

CLAUDE.md gives the blocking gate and the separate `suite.py --long` command.
Only the lean gate reports `suite OK`. The batch retains its logs and findings
for follow-up fixes; it has no ten-minute threshold and is never a prerequisite
for landing work.

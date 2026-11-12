# Plan

What to do next, in order. [TODO.md](TODO.md) lists every open follow-up; this
says which of them to work on and why the rest waits. Milestones themselves are
in [APPROACH.md](APPROACH.md), [GENERATED.md](GENERATED.md),
[REIMPLEMENTATION.md](REIMPLEMENTATION.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md).

## The targets

Four, and every ordering decision here serves them:

1. **Native ports for Windows, macOS and Linux**: no emulation backend, no
   interpreted assembly, the game fully reimplemented in C.
2. **Experimental ports to era hardware** -- GBA, Wii, GameCube -- running this
   code natively, not under emulation.
3. **An experimental SNES ROM compiled from the reimplementation.**
4. **Divergence is accepted** where the game stays structurally the same.

### What that makes scaffolding

The emulator, generated C, shadow mode and oracle are validation scaffolding.
They leave the product as their callers retire. The platform interfaces and
host backends stay, along with the game's frame scheduler and timing estimates:
`ct_native` needs those to preserve the pace of play (PLATFORM.md, "Timing
without a beam, as built"). Refining emulated cycles beyond what validation
needs is not a target.

The game's main thread and eight menu tasks still use coroutines whose waits
are deep in their call stacks (`runtime/port/fiber.h`). A small target needs
smaller measured stacks or resumable tasks; a browser needs a stack-switching
backend or the same rewrite. Neither needs a new implementation of the
per-port fibers that exist only to suspend the emulated machine.

### What "done" means, measured

A port's presence is not proof of native execution or gameplay equivalence.
Use these measures together; keep run-specific counts in the milestone docs
and their ledgers, not as competing snapshots here.

| Measure | Done when |
|---|---|
| `tools/burndown.py`, `tools/unreached.py` | All live code is implemented; unreached paths have execution evidence or explicit safety arguments. |
| `tools/portstyle.py`, `tools/portsmell.py`, qualitative review | The game reads as source; the machine is confined to interfaces awaiting removal. |
| `tools/census.py`, `tools/machine_joints.py` | No game entry needs the registry, emulated stack, bus, register escapes or comparison scaffolding, and no function in `game/` takes `Snes *`. |
| Asset catalogue and accessors | Every resource is loaded through the store; no transfer depends on a mapped cartridge. |
| State accessors | Game variables live in C structs, with defined aliases and bounds, rather than a work-RAM image. |
| `tools/strict_check.py`, `tools/native_check.py`, `tools/outcomes.py` | The library completes with protected saved games, save RAM, pictures and places; routes verify intended gameplay outcomes. |

Burndown credits inlined work and guarded hand-backs, so its near-zero
remainder does not establish that the machine can leave. The census names
which caller still requires an entry. Recorded outcomes distinguish reaching
a movie's frame limit from playing its intended route.

### After the byte-exact gate

Every subsystem has retired its byte-exact comparison (REIMPLEMENTATION.md,
the teardown sections). What it leaves behind still holds: each retired
guard is an assertion (`contract broken:`), a claim that the game never
produces the state it refused, checked over the movies and routes, where a
refusal is a bug in the port rather than a call to pass on. The comparison-only
writes and their gate checks have retired.
`ct_dev`, its interpreter and generated C stay as development tools for tracing
divergences against the original and timing calibration, outside the product gate
after phase B below.

### Working order

The order to a production native build. Each step's detail is in Phase 4a.

1. **Prove `ct_native` plays the game.** A playthrough from a new game to an ending, and the
   open divergences traced. Before the machine is deleted, not after, so the
   deletions have a check that doesn't depend on it.
2. **The game's state as C structs**, with `.srm` saves still loading.
3. **The machine out of the product**: wrappers, registry, emulated stack,
   held waits as C loops, `Snes *` gone from `game/`; `ct_play` becomes
   `ct_native`.
4. **The player as a product**: what `ct_play` has that `ct_native` lacks,
   and release builds.

Native play, state storage and machine removal can overlap per module. Investigate
the first playback divergence before changing a rate or re-recording an
exception. Independent branches use separate worktrees and a shared process
budget, with shared-file edits kept local so they merge.

## The validation gate

Branches pass the sampled gate (`tools/gate_movies.txt`), unit tests and a
warning-free build. Main runs long routes once per timing/runtime merge batch;
the whole-library batch (`--all-movies`) alone runs timing and vblank checks
and supplies calibration and baseline updates. Subsets never prune unseen rows.

The gate retains shadow movies' contract, page, accessor and asset diagnostics;
strict and native movies with recorded saved-game, save-RAM, picture and place
outcomes; shipping, native-shape and native routes; sound fuzz and digests;
style and smell ratchets; unreached-path evidence; and the asset catalogue.
Native crashes, unexplained dark screens and unaccepted route failures fail it.
CLAUDE.md's "Validation" gives the command; REIMPLEMENTATION.md's "The gate
after the comparisons, phase A" records the retirement and remaining checks.

## Phase 4a: what still runs on emulation

The remaining work between a finished `game/` and a native build with nothing
under it is below. None is optional for target 1; the host service backends
already built are described in PLATFORM.md.

- **`ct_native` plays the game, checked in `ct_native`.** Resolve the remaining
  state-driven route divergences listed in TODO.md. Take complete frame-end
  bases during play so slot loads can resume the unfinished route audit
  without replaying from power-on; add a playthrough route
  from a new game to an ending;
  record the endings not yet recorded and the Reunion route; trace the open
  divergences (TODO.md: the Tyrano Lair walls' saved game, Beyond Time's
  ending, the Dino Age interrupt, the tech sweep's wipe near the Core).
  Remaining timing debts (menu-guard accounting, sound timing, line-boundary
  uploads) are settled where a divergence traces to them.
  Next, **phase B of the gate**: once mid-route snapshots support the audit and
  native routes reach their outcomes over the route set, move `accessor out of
  range`, `contract broken` and `page refused` diagnostics into a checking build
  of `ct_native`. Then retire `ct_drive`'s shipping-mode routes, native-shape's
  routes, shadow-mode movies and strict mode. Keep `ct_dev` with the interpreter
  and generated C outside the gate for tracing divergences against the original
  and timing calibration.
- **The game's state as C, not a work-RAM image.** Move the remaining
  image-backed variables into module-owned structs through the generated
  backing (STATE.md). Resolve the inventory's shared scratch, internal field
  aliases, page instances and table overruns before selecting each module;
  retire its raw-address clients and keep cartridge saves and native route
  address queries working through the projection. `tools/ram_map.py storage`
  measures the share of `re/ram.toml`'s variables still backed by the image;
  finish when none is, then remove the residual image and unnamed clients.
- **The machine out of the product.** `game/core/program.c` boots the game
  and dispatches its main thread and handlers; `ct_native` runs it with host
  services (PLAYING.md, "The native program"). What is left, counted by
  `tools/machine_joints.py`:
  13 registered entries, 136 held continuations, 111 stops (including guard
  refusals), 14 address call-outs and nine registry sibling lookups. The
  console's direct boot and stub calls are described in REIMPLEMENTATION.md,
  "The console's direct hand-offs".
  The held count excludes the field, warp, overworld, two menu and two scene
  C loops; all 143 compatibility rows remain (RUNTIME.md, "Restartable C loops").
  The 37-movie sample's census records no calls by the machine after the
  frame driver's reset and interrupt calls (RUNTIME.md) and the field's
  direct game-over restart (REIMPLEMENTATION.md).
  1. The remaining calls through the registry become C calls that keep the
     callee's full contract (widths, decimal mode, page, bank, code image)
     and its time.
  2. The held waits' continuations become C loops, keeping task switching
     and saveable caller frames (RUNTIME.md, "Held waits").
  3. The entry wrappers, returns through the emulated stack, stops and
     register escapes go; the remaining video escapes become named services
     (TODO.md lists each site). Functions take the game's state, not
     `Snes *`.
  4. The registry, intercept, entry macros and emulator leave what ships;
     `ct_play` becomes `ct_native`.

  The census identifies the caller keeping an entry alive; the routes in
  `ct_native` establish how far its replacement plays.
- **The player as a product.** What `ct_play` has and `ct_native` lacks
  (PLAYING.md, "The native program"): quick save and load, bindings, movie
  recording, importing the asset pack on first run. The playback, audio-stall
  and performance reports in TODO.md re-checked on the native program.
  Release builds per platform: CI builds `ct_native` but doesn't ship it.

**Assets: imported once from the user's ROM, then loaded by name.** The
ports reach ROM data through named accessors (`*_data.h`), every byte of it in
a resource of the catalogue (`re/assets.tsv`), and read it from a store the
driver supplies: `ct_play`'s is an asset pack it imports from the ROM once,
beside the saves (PLATFORM.md, "The assets, as built"). The same importer can
build the image for a target that can't afford the ROM or the decoding (GBA,
Wii, GameCube). What is left:

**Decoded next, a resource at a time.** Decode the raw pack into structured
resources: streams decompressed, tiles in a neutral format, palettes as
colours, text as UTF-8, scripts readable (MATCHING.md's codecs and the
text and event-script decodings are the start). Each port stops calling
the original's decoder for that resource, a reviewed change per resource,
and its accessors name the resource rather than the address it was kept at.
Widescreen, translations
and replaceable assets come from here; the readable layout as a whole is
Phase 5's MD3.

Decoding proceeds one resource at a time; the transfer path already reads
the store (PLATFORM.md, "The assets, as built").

## Phase 4b: the platform milestones

Serving widescreen and the GPU backends rather than "no emulation", so they
wait behind 4a.

- **P4 widescreen**, and with it the answer to whether `VidFrame` holds up:
  PLATFORM.md's open questions name the assumptions it would expose.
- **Host backends beyond SDL** (TODO.md, "Platform"): rendering on the GPU,
  controllers.

## Phase 5: the long tail

- **MD3**, and the decoded half of the assets it enables (Phase 4a,
  "Assets", the decoded step): a layout meant to be read rather than reassembled, and
  the scripts as text. **MD3 is the prerequisite for
  target 3**, the SNES ROM compiled from the reimplementation -- every pointer
  symbolic, so a routine that differs in length can move -- so it comes forward
  with that target rather than waiting here.
- **The SNES ROM itself** (TODO.md, "A SNES ROM compiled from the hand ports"):
  MD3, then a hybrid that links one compiled routine in place of the original's
  and checks the ROM plays, then outward. `tests/test_port_subset.py` already
  keeps `game/` inside what a 65816 compiler handles, and the decoupling
  is what makes a routine linkable at all: the logic apart from the emulated
  machine is exactly what a SNES build compiles.
- **MD2's remainder**: 42590 census-unknown bytes, 228.3 KiB still `mixed`, and
  `lz` re-encoding, plus the **MD4 residue**. The unknown bytes are by
  definition ones no reader has been found for, so no port depends on them;
  they are proof-of-completeness work, and the formats that ports do depend on
  were typed before the bulk conversion.
- **The asar cross-check**, which has never run.

## Slot in whenever

- **The `fair` and `fair-companion` regression.** After the timing fix these two
  got *worse* against Snes9x and nobody knows why (TODO.md, "Movies and
  coverage"). Investigate when it blocks validation; its age does not establish the cause.
- **CI's macOS job**, which costs 10x minutes on a private repo.

## What this plan deprioritizes, and why

The targets decide this, so the old list is gone: a GBA port and a SNES ROM
compiled from the reimplementation are targets 2 and 3, not interests, and they
are scheduled above (Phase 4a's asset seam, Phase 5's MD3) rather than deferred.
What stays deprioritized is what serves the scaffolding or nothing:

- **M4** (generator speed). The generated C is a means; making it faster helps
  no target.
- **Emulator accuracy**: the IRQ trigger delay, the S-DSP's KON poll,
  re-measuring the audio comparison table, big-endian hosts, the ZSNES state
  import. Out of scope unless one of them blocks validation.
- **The wasm stack question.** The per-port fibers exist only to suspend an
  emulated machine mid-instruction while a port runs; with the machine gone
  there are no call-outs and no per-port fibers. The main thread and menu tasks stay (above), so a third `fiber.h` backend is worth building
  for those when a browser build is wanted, not before. A browser build,
  if it is wanted, is a frontend on top of a finished `game/` -- which is the
  same thing targets 1 and 2 need.
- **Anything that makes the hybrid modes nicer to live in.** `--generated on` and
  shadow mode are how the work is checked, not what is being built.

The host seam was never deferred, because it was the same work as making the
desktop build shippable: the player advances a frame per call and persists
bytes under names (PLAYING.md, "How the player is built"), so a new frontend
needs a driver of its own rather than a fork of the player. That is what makes
targets 1 and 2 a matter of writing drivers once `game/` stands alone.

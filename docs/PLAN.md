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
| `tools/fallbacks.py` | No original code substitutes for hand ports, no unexplained refusal, and the build without scaffolding reaches the same route outcomes. |
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
refusal is a bug in the port rather than a call to pass on. The scaffolding
writes left (`tools/machine_joints.py`) go with the comparisons that read
them.

### Working order

The order to a production native build. Each step's detail is in Phase 4a.

1. **The gate after the comparisons.** Decide which checks the gate keeps
   now that subsystem comparisons are retired, and what becomes of the
   generated C, the interpreter and shadow mode.
2. **Prove `ct_native` plays the game.** Routes driven from state in
   `ct_native` itself, a playthrough from a new game to an ending, and the
   open divergences traced. Before the machine is deleted, not after, so the
   deletions have a check that doesn't depend on it.
3. **The game's state as C structs**, with `.srm` saves still loading.
4. **The machine out of the product**: wrappers, registry, emulated stack,
   held waits as C loops, `Snes *` gone from `game/`; `ct_play` becomes
   `ct_native`.
5. **The player as a product**: what `ct_play` has that `ct_native` lacks,
   and release builds.

The first two steps can run side by side; state storage and machine removal
overlap per module. Investigate
the first playback divergence before changing a rate or re-recording an
exception. Independent branches use separate worktrees and a shared process
budget, with shared-file edits kept local so they merge.

## The validation gate

Nothing lands without it, and it is the same gate for every phase. It runs
over the gate's sample of the movie library (`tools/gate_movies.txt`, a greedy
cover of the library's code per frame played plus what each check needs) by
default, and over the whole library with `--all-movies`, run batched over
everything merged since its last run (it takes 8 hours and more), which is
where recalibration and baseline updates come from; "every movie" below is every movie run:

- every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines,
  no `page refused` lines (a guard that turned away a call on a direct
  page the map declares no instance for) and no `accessor out of range`
  lines (a port that reached past a variable the map declares, into
  whatever follows it in work RAM; both in DISASSEMBLY.md, "The accessors");
- `--native on --generated only` traces identically to `--native off`;
- every hand port's estimated time stays within the timing gate's limits over
  the shadow runs, or is on its exceptions list with a reason
  (`tools/timing_gate.py`; REIMPLEMENTATION.md, "The timing gate");
- no video write in the shipping mode lands on the picture where the
  reference's doesn't, unless it is an accepted exception
  (`tools/vblank_check.py`);
- the route scripts reach their outcomes in the shipping mode, with the C
  sound driver (`tools/movies/route_check.py`: the fast route every time, all
  of them with `--long-routes` on main once per timing/runtime merge batch);
- the C sound driver, over a short `ct_sound_fuzz` with the game's songs
  and with made-up ones, answers every command, places no sample where the
  echo writes and drops none, the native mixer makes the S-DSP's every frame,
  and each kind of run's sound is the digest `tools/sound_golden.txt` keeps
  (SOUND.md, "How it is checked"; nothing compares it with the original's
  any more);
- every don't-care declaration in `game/` is one liveness agrees with
  (`tools/dontcare_check.py`; REIMPLEMENTATION.md, "Before the bulk");
- no port transliterates the assembly where one didn't before
  (`tools/portstyle.py`, a ratchet against its baseline);
- no port's logic reads what some port writes only as scaffolding where
  none did before (`tools/effects_check.py`, a ratchet against its baseline;
  PORTING.md, "Scaffolding");
- no port carries one of the review rubric's mechanical smells where one
  didn't before (`tools/portsmell.py`, a ratchet against its baseline);
- the original's code runs in place of hand ports nowhere new, and no more of
  it where it did, over every movie's reference play (`tools/fallbacks.py
  --check`, a ratchet against its baseline; REIMPLEMENTATION.md, "The fallback
  measure");
- no hand port's guard refuses a call real play makes unless its allowlist
  line says why (`tools/fallbacks.py --refusals`; "After the byte-exact
  gate", above);
- the build with the scaffolding compiled out runs every movie to its end and
  reaches the routes' outcomes in the shipping mode (`ct_dev_native_shape`,
  `ct_drive_native_shape`; REIMPLEMENTATION.md, "The native shape, run");
- strict mode and `ct_native` preserve their recorded saved-game, save-RAM,
  picture and place outcomes (`tools/outcomes.py`); a retired guard reports
  `contract broken:` rather than silently weakening the contract;
- `--misses` stays empty;
- `py -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

## Phase 4a: what still runs on emulation

The remaining work between a finished `game/` and a native build with nothing
under it is below. None is optional for target 1; the host service backends
already built are described in PLATFORM.md.

- **The gate after the comparisons.** With subsystem comparisons retired,
  decide for each check whether it still protects the product: shadow
  mismatches, `--generated only` against `--native off`, the fallback and
  refusal measures, the effects check and the don't-care check compare
  against the original or its scaffolding; the routes, recorded outcomes,
  strict and native checks, sound digests, style and smell ratchets do not.
  Delete the checks that are empty, and their ledgers with them. Decide in
  the same item what the generated C, the interpreter and shadow mode are
  kept for (the oracle tools, a debugging build) or whether they go.
  Audit the remaining effects, unreached, timing and uncompared records
  against the retired comparisons; preserve the evidence still needed by
  held continuations and native timing.
- **`ct_native` plays the game, checked in `ct_native`.** The routes reach
  their outcomes in `ct_drive`, which still has the emulator under it, and
  `ct_native` is checked only by replaying recorded input, which a small
  timing difference sends elsewhere. Drive the routes from game state in
  `ct_native` itself; add a playthrough route from a new game to an ending;
  record the endings not yet recorded and the Reunion route; trace the open
  divergences (TODO.md: the Tyrano Lair walls' saved game, Beyond Time's
  ending, the Dino Age interrupt, the tech sweep's wipe near the Core).
  Remaining timing debts (menu-guard accounting, sound timing, line-boundary
  uploads) are settled where a divergence traces to them.
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
  13 registered entries, 143 held continuations, 104 stops (including guard
  refusals), five address call-outs and two registry sibling lookups. The
  console's direct boot and stub calls are described in REIMPLEMENTATION.md,
  "The console's direct hand-offs".
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

**Audit the remaining address sources.** The conservative source counter has
122 unproved candidates (TODO.md, "Unproved cartridge-address sources").
Establish which expressions can only select RAM and which descriptors already
reach named accessors; cartridge-capable reads need the resource seam. The
earlier zero covered only the 23 targeted sites, not every possible bypass.

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

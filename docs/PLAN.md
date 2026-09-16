# Plan

What to do next, in order. [TODO.md](TODO.md) lists every open follow-up; this
says which of them to work on and why the rest waits. Milestones themselves are
in [APPROACH.md](APPROACH.md), [RECOMP.md](RECOMP.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md).

## What this plan optimizes for

1. **A readable decompilation** is the goal the order serves. The recompilation
   runs the whole game exactly, so what is left to build is *source*: hand
   written C, named and structured by what it does, in place of the mechanical
   image of the ROM. That is [RECOMP.md](RECOMP.md)'s M5, and it is the largest
   remaining task in the project by a wide margin.
2. **A build other people can play**, soon. Small next to M5, and it stops being
   cheap to defer once anyone else is running it.
3. **One track at a time.** Each phase below finishes and passes its gate before
   the next starts.

## The validation gate

Nothing lands without it, and it is the same gate for every phase:

- every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines;
- `--native on --recomp only` traces identically to `--native off`;
- `--misses` stays empty;
- `python3 -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

## Phase 0: finish what is in flight

- The **jet bike race's display code** (19 blocks between `$7E:36DA` and
  `$7E:8752`), the last direct register writers and the end of P2.
- The **MD2 data-typing** work through the gate.

Both are started; finishing them costs less than carrying them.

## Phase 1: a build other people can play

Days of work, and it unblocks anyone else running this.

- **Gamepad support.** `in_host_set` is already the single per-frame host entry
  point, so this is an SDL GameController call into it.
- **Configurable keys**, replacing the fixed `keymap` table in `frontend/ct_play.c`.
- **Quick saves that survive a rebuild.** They are raw machine images today and
  break whenever a structure changes, which P3 demonstrated by changing
  `sizeof(Snes)`. Needs a versioned, field-wise format.
- **Packaging**, so `tools/dist.py`'s output runs on a machine that never built
  it.

## Phase 2: M5's foundation

The question that decides whether M5 is a decade of work or a year, and it
should be answered before any bulk conversion.

**First, settle the timing.** A hand port charges estimated time
(`cost_milli`), and a generated block is exact, which is why `--recomp only` is
the default and why `--recomp on` can play a battle out differently. M5 as
designed in [RECOMP.md](RECOMP.md) does not change that: it prints a draft, and
the port is hand written from it as ports are today. Converting the ROM that way
makes the drift grow with every routine.

Two facts make that survivable, and the phase starts by choosing between them:

- `--recomp only` ignores hand ports, so ports can accumulate without touching
  what the default build plays. The decompilation grows; the shipping path stays
  exact. This works indefinitely but leaves the readable code permanently
  second-class.
- A port *derived* from a block could carry that block's exact cost instead of
  an estimate, which is what TODO.md means by "ports derived from generated
  blocks". If that holds, `--recomp on` becomes trace-identical to
  `--recomp only`, and the readable code becomes the shipping path. It is worth
  a spike before committing to the first option.

**Then build the workflow.** M5 is a tooling problem, not a grinding problem.
`tools/recomp/show` is designed but unwritten. What it needs to earn:

- a draft close enough that the remaining work is naming and structure;
- a **ground truth check**: derive a routine that is *already* hand ported and
  compare, which says how good the drafts are without waiting for new ports to
  be validated;
- a burndown metric, the share of the ROM's 245 KiB of code that registered
  entries cover, reported per subsystem the way `--misses` reports coverage.

**Done when** a subsystem's worth of routines has been converted through the
workflow, the metric exists, and the timing question has an answer written down.

## Phase 3: M5 in bulk

Subsystem by subsystem, each through the gate, cheapest and best understood
first. The order should follow what the profile says runs most and what the
existing ports already surround, so each conversion has neighbours to check it
against.

This is the long haul. It is worth re-reading the burndown after the first two
subsystems and deciding whether the workflow needs another pass before
continuing.

## Phase 4: the platform milestones

Deferred, not abandoned: these serve the native port and widescreen rather than
the decompilation, so they wait behind it.

- **P3's leftover**: the observer does not fill in the per-span memory pointers
  the decoder now does. Until it does, the renderer cannot be fed by service
  calls rather than registers, which is what widescreen and any GPU backend
  need.
- **P4 widescreen**, and with it the answer to whether `VidFrame` holds up:
  PLATFORM.md's open questions name the assumptions it would expose.
- **The beam-coupled routines**, the event scheduler `$C0:5A46` and the sprite
  frame queue `$C0:CA76`, which stop when the beam passes a scanline. These are
  game-side rewrites no backend fixes.
- **Host backends beyond SDL** (TODO.md, "Platform"): rendering on the GPU, a
  native mixer behind `au_*`, controllers.

## Phase 5: the long tail

- **MD2's remainder** (43481 census-unknown bytes, 322.5 KiB still `mixed`, the
  string encoding, the script formats), **MD3** and the **MD4 residue**.
- **The asar cross-check**, which has never run.
- **Accuracy**: the IRQ timing simplifications, the S-DSP's KON poll, and
  re-measuring the audio comparison table, which predates the DMA and refresh
  timing fix.
- **Coverage**: the unrecorded endings, the Reunion route, and re-running
  `--misses` over the whole library.

## Slot in whenever

- **The `fair` and `fair-companion` regression.** After the timing fix these two
  got *worse* against Snes9x and nobody knows why (TODO.md, "Movies and
  coverage"). It is the only open item that might be a bug rather than
  unfinished work, so it should not wait for a phase.
- **CI's macOS job**, which costs 10x minutes on a private repo.

## What this plan deprioritizes, and why

- **M4** (recompiler speed): rendering and sound dominate run time, so it buys
  little now.
- **Big-endian hosts**: no such host is in use, and nothing else depends on it.
- **A GBA port**: P5 is a feasibility study, and its constraints would reshape
  the platform API. Worth pulling forward only if the GBA becomes a real target
  rather than an interest.

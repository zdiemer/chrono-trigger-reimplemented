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
2. **A build other people can play** comes first when it breaks. Others run it
   now ([PLAYING.md](PLAYING.md)), and it is small next to M5.
3. **Phases in order, work within them in parallel.** A phase's items can run
   side by side when they don't share files, each in its own worktree and each
   through the gate before it lands, and a later phase's item can start early
   when it depends on nothing still open. Parallel work shares one machine, so
   each track is given a process budget.

## The validation gate

Nothing lands without it, and it is the same gate for every phase:

- every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines;
- `--native on --recomp only` traces identically to `--native off`;
- every hand port's estimated time stays within the timing gate's limits over
  the shadow runs, or is on its exceptions list with a reason
  (`tools/timing_gate.py`; RECOMP.md, "The timing gate");
- no video write in the shipping mode lands on the picture where the
  reference's doesn't, unless it is an accepted exception
  (`tools/vblank_check.py`);
- the route scripts reach their outcomes in the shipping mode
  (`tools/movies/route_check.py`: the fast route every time, all of them with
  `--long-routes` before a change to timing or the native runtime lands);
- every don't-care declaration in `game/` is one liveness agrees with
  (`tools/dontcare_check.py`; RECOMP.md, "Before the bulk");
- `--misses` stays empty;
- `python3 -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

## Phase 3: M5 in bulk

Where it stands, from `python3 tools/burndown.py` (an estimate; the tool's
docstring says which way it is wrong). **83% of the code is still generated.**

| Subsystem | Code | Ported | Left | Entries |
|---|--:|--:|--:|--:|
| battle (`$C1`, `$CE`) | 64.9K | 10.1K | 84% | 42 |
| field engine, events (`$C0`) | 59.4K | 9.6K | 84% | 52 |
| menus, text (`$C2`) | 59.3K | 5.2K | 91% | 52 |
| scene program, RAM (`$7E`) | 24.9K | 2.8K | 89% | 54 |
| battle frame work (`$CD`) | 15.5K | 2.2K | 86% | 27 |
| field graphics (`$FD`) | 15.3K | 1.5K | 90% | 9 |
| battle sprites (`$CF`) | 6.8K | 3.3K | 52% | 18 |
| battle NMI, graphics (`$D1`) | 5.7K | 0.7K | 87% | 9 |
| battle setup (`$CC`) | 4.9K | 4.3K | 11% | 8 |
| scene engine (`$C3`) | 3.9K | 3.0K | 25% | 28 |
| scene data (`$C6`) | 3.5K | 1.1K | 68% | 25 |
| sound driver (`$C7`) | 2.6K | 2.6K | 0% | 43 |
| misc (`$FF`) | 1.7K | 0.1K | 95% | 1 |
| **total** | **268.5K** | **46.5K** | **83%** | **368** |

The three big banks are the work; the 368 entries are concentrated in small hot
routines, which is why the shares are so high despite the count. The burndown
now follows the jump tables the analysis has sized, which a port that
dispatches through one inlines: that alone moved 1.1 KiB (`$C2`, `$CD`, `$C3`)
and took the sound driver from 46% left to 4% before the pilot touched it, so
its old figure was mostly the tool's. `$CC` is the
one subsystem near done. `$C3` (the scene engine) is done but for code the
beam's counters decide, one routine nothing reaches and the edge sequence the
field's rasterizer inlines (PORTING.md's row); `$C6` has its small object
routines and irises, and its Mode 7 map and perspective effects, table
builders and orbit routines are left, none of which a movie reaches.

Subsystem by subsystem, each through the gate, cheapest and best understood
first. `--uncovered BANK` lists the largest unported runs, which is how to pick
the next routine. Taking a subsystem includes bringing its existing ports up to
the same bar (below), so the refactor is paid where the code is already being
read rather than as a sweep. The same goes for the script formats' numbered
commands (`cmd_6C`, `if_0A`, `set_1c80`): porting a command's handler is when
it gets a real name in `tools/disasm/eventscript.py` or `battlescript.py`, since
that's when someone has read what it does.

Naming is part of that bar. The RAM map (`re/ram.toml`, docs/DISASSEMBLY.md)
names 91% of the work RAM addresses `game/` spells as literals, so a subsystem's
ports switch to `game/ram.h`'s names the way `game/battle/setup.c` did
(DISASSEMBLY.md, "The RAM map"), and each routine converted adds what it learns
about the rest. `python3 tools/ram_map.py coverage --open --subsystem S` lists
what is still unnamed; what it can't show is direct-page scratch and the
absolute operands of `$2000` and up, which a porter names while reading the
routine.

The per-port bar, for new ports and existing ones brought up to it: every call
matches in shadow mode over the movie library; the port is **fuzzed** with
`ct_fuzz`, including the paths the movies don't reach, with the run recorded
in its PORTING.md row (a generated block is right on every path by
construction, a hand port only on the paths something exercised); its
`cost_milli` is the calibrated value and its estimate error is within the
timing gate's threshold; and it reads as source, with no cycle costs
carried.

It also includes **laying the asset seam**, which is cheap here and expensive
later. Only about 150 places in `game/` read ROM data by address; a subsystem's
share of them is a handful. Routing those through a named accessor for the
resource, rather than `m_read8(s, 0xD15E10 + c->x)`, costs nothing now, reads
better either way, and is what lets Phase 4 point the same call at an extracted
file or a GBA-format conversion without revisiting the ports. Where a resource
already has a type from MD2 (MATCHING.md), name the accessor after it. The order should follow what the profile says runs most and
what the existing ports already surround, so each conversion has neighbours to
check it against. How to write the ports themselves is in CLAUDE.md, "Writing a
port from generated code".

**What the pilot taught** ([RECOMP.md](RECOMP.md), "The $C7 pilot"),
which converted the sound driver to this bar:

- **The bar is reachable, and the logic ends up with no emulated registers**,
  but only after the gate stopped comparing what the original itself
  overwrites before reading: popped stack bytes, what a port declares dead where
  it stops, A's high byte on its own (PORTING.md, Modes). Every subsystem has
  `PHP`/`PLP` subroutines, so that was decided once, for all of them.
- **Cost.** 2.6 KiB of 65816 became 1550 lines of C in about a working day
  including validation, of which the one-time decision above was the largest
  part. Code like it, with its services already in place, should go at 2-3 KiB
  a day; code that needs new service calls, or RAM nobody has named, slower.
  Machine time is what paces it: a library shadow run is 6 minutes, fuzzing a
  subsystem minutes, the full gate with every route most of an hour.
- **The draft is a reference more than a starting text**: its header, names
  and control flow carried over, its text almost none of it (0-7% of the
  port's lines). The draft tool's fixes the pilot asked for are in (RECOMP.md,
  "Before the bulk"): drafts carry far fewer flags and stubs, name their
  hand-backs' dead registers, and keep D across call-outs.
- **Declarations are checked.** Every don't-care set a port declares is
  verified against liveness where shadow mode applies it
  (`tools/dontcare_check.py`, in the gate), so leaving a register out of the
  comparison is a claim the check holds a port to.
- **Fuzzing earns its place**: it found three bugs no movie reaches, where the
  port had tidied an index or a width the original doesn't.
- **Existing ports in the early style** can be brought over the same way; the
  sound driver's 1066 emulated-register operations went to 86, all in its
  entries and stubs.

**The ports that exist.** The early entries were written
over months and they are not one standard. The recent ones read as source: named
constants, service calls, a comment saying what the data means. The early ones
read as transcription, still moving emulated A and X about and naming direct
page bytes by their addresses (`game/battle/setup.c` is the clearest case; its
variables now carry the RAM map's names, but its shape is still the ROM's). Both
pass the gate, so nothing is broken; what is missing is that half of them are
not the thing M5 is for.

Two things do *not* need a pass, which is worth knowing before scoping one: the
platform lint already covers all of `game/` bar four documented files, so
hardware access is behind the services everywhere; and no screen geometry is
baked in that widescreen would have to undo.

So the refactor is readability only, it is cheap to defer, and it should be paid
per subsystem in Phase 3 rather than as a sweep: converting a subsystem's
generated code includes bringing that subsystem's existing ports to the same
bar, while its behaviour is already under the gate. A sweep across all of
`game/` at once would be a large diff with no behavioural check beyond the gate
it already passes.

This is the long haul. It is worth re-reading the burndown after the first two
subsystems and deciding whether the workflow needs another pass before
continuing; RECOMP.md's "What drafts don't do yet" lists where the draft tool
falls short, starting with D and DB where only interrupts reach.

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
- **Assets as data rather than ROM reads** (TODO.md, "Platform"). The pipeline
  half belongs here: a layout meant to be read rather than reassembled, the
  scripts as text (the event scripts have a readable form, MATCHING.md "The
  event scripts"; the battle scripts need one), and a runtime that can load
  from a user data directory instead of the ROM image. A GBA build is what
  forces it, since it can't map a 4 MiB SNES image the way the recompilation
  does; the seam it needs is laid in Phase 3 (below).

## Phase 5: the long tail

- **MD2's remainder**: 43481 census-unknown bytes, 322.5 KiB still `mixed`, and
  `lz` re-encoding; then **MD3** and the **MD4 residue**. MD3 is also the
  prerequisite for a SNES ROM compiled from the ports (TODO.md), so it comes
  forward if that target does. The unknown bytes are
  by definition ones no reader has been found for, so no port depends on them;
  they are proof-of-completeness work, and the formats that ports do depend on
  were typed before the bulk conversion.
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
- **The other targets** (TODO.md, "Targets beyond the desktop SNES build"): a
  wasm build, an AYN Thor multi-screen port, DS and PlayStation content in the
  original game, native touch controls, and a SNES ROM compiled from the hand
  ports. They are wanted, and most of them are mostly blocked on work this plan
  already has in order -- M5's coverage and the asset seam -- so
  scheduling them ahead of it would mean doing that work twice. The host seam
  wasn't deferred, because it was the same work as making the desktop build
  shippable: the player advances a frame per call and persists bytes under
  names (PLAYING.md, "How the player is built"), so a browser build needs a
  driver of its own rather than a fork of the player.

  What stays open is the **stack question**, and it is the one thing no phase
  here touches. Every non-idle registry entry runs on a stack of its own: a
  call-out is `job_run` in `runtime/port/native.c` switching to a fiber, so the
  port can run the routine to completion while the machine it was called from
  waits mid-instruction. The abstraction for that already exists and is in the
  right place -- `runtime/port/fiber.h` is four functions, with Windows fibers
  and POSIX `ucontext` behind it -- so the question is not what to abstract but
  whether a third implementation can sit there at a price worth paying. Wasm
  can't switch stacks on its own, and the candidates differ in kind: Asyncify
  instruments the whole program to unwind and rewind it, which costs size and
  speed everywhere rather than at the call-out; JSPI does it in the host, which
  is cheaper but sets a floor on what can run the build. The alternative is to
  stop needing a stack -- a call-out that returns to its caller instead of
  suspending it -- which is a `native.c` redesign and would be felt by shadow
  mode and the whole gate.

  It is narrower than it looks. Generated blocks never call out and run without
  a fiber, and `--recomp only` runs no hand ports, so a user-compiled build in
  that mode never switches stacks: `fiber.h` needs a backend there only to
  link. What needs a real one is a build that runs hand ports -- the public one
  that can't carry generated code, and the default player, which runs them
  since the timing gate made `--recomp on` its default.

  So the spike is: put a third backend behind `fiber.h`, run the movie suite
  under it, and measure. A day's answer that decides whether a browser build is
  a frontend job or a runtime project, and it is cheap to write down either way.
  Worth doing whenever; it doesn't wait on M5, and M5 doesn't wait on it.

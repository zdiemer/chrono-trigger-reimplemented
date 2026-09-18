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
3. **Phases in order, work within them in parallel.** A phase's items can run
   side by side when they don't share files, each in its own worktree and each
   through the gate before it lands; a later phase's item can start early when it
   depends on nothing still open (Phase 2's RAM map beside Phase 1, for
   instance). Parallel work shares one machine, so each track is given a process
   budget.

## The validation gate

Nothing lands without it, and it is the same gate for every phase:

- every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines;
- `--native on --recomp only` traces identically to `--native off`;
- `--misses` stays empty;
- `python3 -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

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

### The host seam

Three of those four are edits to `frontend/ct_play.c` and the save path, which
is also where any host that isn't a desktop has to be let in. Doing them in the
shape that host needs costs little more than doing them twice.

`Player` and `step` (`frontend/ct_play.c`) are already the right idea: no SDL
type is in the struct, and a frame is a function call rather than a loop
iteration. What isn't factored is everything around them. `run_window` owns the
loop, the pacing (`SDL_Delay` against a deadline), the event poll, the window
and the audio device; `run_headless` is a second copy of that shape; and
persistence is spelled as paths throughout -- `sram_path` and `state_path` in
`Player`, `fopen` in the frontend, `snes_save_state` and `zst.c` taking a path
in `runtime/snes/`. A browser offers neither of those: the host calls back once
a frame rather than being called, and there is no synchronous filesystem.

Two seams, each worth having on the desktop by itself:

- **A driver seam.** Promote `Player`, `step` and the audio fill to a header of
  their own, so `run_window` is one driver over them and `run_headless` another.
  The rule that makes it hold: below the driver, nothing owns the loop, sleeps,
  spins on a clock or reads an event queue -- the game advances one frame per
  call, and input arrives as `in_host_set` already demands. A third driver is
  then a file the size of the other two rather than a fork of the player.
  Configurable keys and gamepad support are bindings *in* a driver, so they
  should be written on the far side of this rather than into `run_window`.
- **A storage seam.** Persistence moves bytes under a name: the `.srm`, a quick
  save, a recorded movie, the misses file. Files sit behind it on the desktop
  and whatever the host has elsewhere, and the runtime's by-path helpers become
  buffer functions with the path wrapper in the driver. Quick saves need a
  versioned, field-wise format in this phase regardless, and that is the same
  code, so the two land together.

Neither seam answers wasm's real question, which is the stack one below; they
are what makes the rest of that build ordinary.

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

The spike settles it, and both answers carry work rather than only a note:

- **If a derived port can carry an exact cost**, that is the fix for the drift,
  and applying it to the 259 entries that hold an estimate is a step of its own,
  scheduled here rather than left implied. The check is the gate's own: with the
  ports in, `--native on --recomp on` traces identically to `--native off`, the
  comparison only `--recomp only` passes today. Do it before Phase 3 converts
  more code, so the bulk work produces exact ports rather than a second
  backlog.
- **If it can't**, write down that `--recomp only` is the shipping path for as
  long as the SNES backend is the one that matters, and that the readable code
  is validated by shadow mode rather than by running. That is a real decision
  about what the decompilation is *for*, and Phase 3 should not start before it
  is made.

Two other timing threads exist and neither is this one, so they shouldn't be
conflated with it: the machine's own approximations (TODO.md, "Correctness and
accuracy" -- the IRQ trigger delay, an IRQ pending across `CLI`, the S-DSP's KON
poll), and the game code that waits on beam position, which is a game-side
rewrite in Phase 4. The unexplained `fair` and `fair-companion` regression
belongs with the first group and is the only one that might be a bug.

**Then build the workflow.** M5 is a tooling problem, not a grinding problem.
`tools/recomp/show` is designed but unwritten. What it needs to earn:

- a draft close enough that the remaining work is naming and structure;
- a **ground truth check**: derive a routine that is *already* hand ported and
  compare, which says how good the drafts are without waiting for new ports to
  be validated.

`python3 tools/burndown.py` already reports the share of code the registered
entries cover, per bank or per subsystem, which is how a conversion's progress
is seen. It reads as an estimate for the reasons its own docstring gives.

**Build the RAM map first.** Ports spell every variable as its address, because
there is nowhere to get a name from: roadmap step 1 ([APPROACH.md](APPROACH.md))
called for "a symbol and RAM-map database" and only the code/data side of it
exists, in `re/hints.toml`. So `game/` holds 10955 hex literals against 42 named
groups, and a port reads `0x95D5` where it means the battle's state.

The job is smaller than that number: 519 distinct low-RAM addresses
(`$0100-$1FFF`, the game's variables) in 1206 uses, and 144 long WRAM addresses
in 216 more. Most of the rest are direct page offsets and bit masks. The
knowledge is mostly written down already, scattered through port comments,
PORTING.md's status table and MOVIES.md, so this is largely harvesting rather
than reverse engineering. A map in `re/`, a generated header, and naming a
subsystem's variables becomes mechanical as Phase 3 reaches it. Nothing about
the bytes changes, so the gate is unaffected.

The 1845 ROM addresses in `game/` are *not* the target and should be left
alone: nearly all of them are call sites, call-out targets, registry entries and
service origins, where the address is the routine's identity and the
`// $BB:AAAA` contract. Only about 150 are reads of ROM data, which is the class
the asset work in TODO.md covers.

**Type the formats the code interprets.** MD2 has typed where the data is; what
it hasn't done is the scripts the game *interprets*, and those are what the big
subsystems are written around: the event, battle-animation and enemy script
formats as typed regions with round trips (MATCHING.md, "MD2 as built"; TODO.md,
"Matching disassembly"), each built the way MATCHING.md's "The string
encodings" is: a codec in `tools/disasm/`, the regions typed in
`re/hints.toml`, and the matching build assembling them from the decoded form.
`$C0` is 84% generated and much of it is the event interpreter; porting a
command handler is a different job when the command's operands have names and a
parser says what follows it. The same work is the first dependency of script-as-text extraction
and of new content (TODO.md, "Targets beyond the desktop SNES build"), so it is
done once, here, before the conversion that leans on it. The round trip is the
check: parse, re-encode, and the matching build still produces CRC32
`2D206BF7`.

**Then set the bar against the ports that exist.** The 291 entries were written
over months and they are not one standard. The recent ones read as source: named
constants, service calls, a comment saying what the data means. The early ones
read as transcription, still moving emulated A and X about and naming direct
page bytes by their addresses (`game/battle/setup.c` is the clearest case). Both
pass the gate, so nothing is broken; what is missing is that half of them are
not the thing M5 is for.

Two things do *not* need a pass, which is worth knowing before scoping one: the
platform lint already covers all of `game/` bar five documented files, so
hardware access is behind the services everywhere; and no screen geometry is
baked in that widescreen would have to undo.

So the refactor is readability only, it is cheap to defer, and it should be paid
per subsystem in Phase 3 rather than as a sweep: converting a subsystem's
generated code includes bringing that subsystem's existing ports to the same
bar, while its behaviour is already under the gate. A sweep across all of
`game/` at once would be a large diff with no behavioural check beyond the gate
it already passes.

**Done when** the timing spike has an answer and whichever work it implies is
done, the RAM map exists, the script formats round-trip, and a subsystem's worth of routines has been converted
through the workflow to the bar above.

## Phase 3: M5 in bulk

Where it stands, from `python3 tools/burndown.py` (an estimate; the tool's
docstring says which way it is wrong). **85% of the code is still generated.**

| Subsystem | Code | Ported | Left | Entries |
|---|--:|--:|--:|--:|
| battle (`$C1`, `$CE`) | 64.8K | 10.1K | 84% | 42 |
| field engine, events (`$C0`) | 59.4K | 9.5K | 84% | 50 |
| menus, text (`$C2`) | 59.3K | 4.5K | 92% | 51 |
| scene program, RAM (`$7E`) | 24.9K | 1.7K | 93% | 32 |
| battle frame work (`$CD`) | 15.5K | 2.0K | 87% | 27 |
| field graphics (`$FD`) | 15.3K | 1.5K | 90% | 8 |
| battle sprites (`$CF`) | 6.8K | 3.3K | 52% | 18 |
| battle NMI, graphics (`$D1`) | 5.7K | 0.7K | 87% | 9 |
| battle setup (`$CC`) | 4.9K | 4.3K | 11% | 8 |
| scene engine (`$C3`) | 3.9K | 2.1K | 48% | 11 |
| scene data (`$C6`) | 3.5K | 0.0K | 100% | 0 |
| sound driver (`$C7`) | 2.6K | 1.4K | 46% | 34 |
| misc (`$FF`) | 1.7K | 0.1K | 95% | 1 |
| **total** | **268.5K** | **41.2K** | **85%** | **291** |

The three big banks are the work; the 291 entries are concentrated in small hot
routines, which is why the shares are so high despite the count. `$CC` is the
one subsystem near done, and `$C6`'s 3.5 KiB has no entries at all -- MD4's
script parsing found routines there that MD1 had typed as data, and nobody has
been back.

Subsystem by subsystem, each through the gate, cheapest and best understood
first. `--uncovered BANK` lists the largest unported runs, which is how to pick
the next routine. Taking a subsystem includes bringing its existing ports up to
the same bar (Phase 2), so the refactor is paid where the code is already being
read rather than as a sweep.

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
- **Assets as data rather than ROM reads** (TODO.md, "Platform"). The pipeline
  half belongs here: a layout meant to be read rather than reassembled, the
  script as text once MD2 has the event script format, and a runtime that can load
  from a user data directory instead of the ROM image. A GBA build is what
  forces it, since it can't map a 4 MiB SNES image the way the recompilation
  does; the seam it needs is laid in Phase 3 (below).

## Phase 5: the long tail

- **MD2's remainder**: 43481 census-unknown bytes, 322.5 KiB still `mixed`, and
  `lz` re-encoding; then **MD3** and the **MD4 residue**. The unknown bytes are
  by definition ones no reader has been found for, so no port depends on them;
  they are proof-of-completeness work, and the formats that ports do depend on
  moved to Phase 2.
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
  original game, and native touch controls. They are wanted, and three of the
  four are mostly blocked on work this plan already has in order -- M5's
  coverage, the RAM map, the asset seam, MD2's script formats -- so
  scheduling them ahead of it would mean doing that work twice. Phase 1's host
  seam is the part that isn't deferred, because it is the same work as making
  the desktop build shippable.

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
  a fiber, and the default player (`--recomp only`) runs no hand ports, so a
  user-compiled build in that mode never switches stacks: `fiber.h` needs a
  backend there only to link. What needs a real one is a build that runs hand
  ports -- the public one that can't carry generated code, or any build once
  the timing spike makes hand ports the shipping path.

  So the spike is: put a third backend behind `fiber.h`, run the movie suite
  under it, and measure. A day's answer that decides whether a browser build is
  a frontend job or a runtime project, and it is cheap to write down either way.
  Worth doing whenever; it doesn't wait on M5, and M5 doesn't wait on it.

# Plan

What to do next, in order. [TODO.md](TODO.md) lists every open follow-up; this
says which of them to work on and why the rest waits. Milestones themselves are
in [APPROACH.md](APPROACH.md), [RECOMP.md](RECOMP.md), [MATCHING.md](MATCHING.md)
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

The emulator under `runtime/` -- the 65816 interpreter, the SPC700, the S-DSP,
the PPU and the bus -- **must not ship**, and neither must the generated C,
shadow mode, the oracle or the timing model. What does ship from `runtime/` is
the platform services' interfaces (`runtime/platform/`) with their host
backends, and `runtime/port/fiber.h` for the one coroutine the game's main
thread runs as (below, and PLATFORM.md, "Timing without a beam"). They exist to prove the
reimplementation correct, and they are load-bearing until the subsystem they
check is finished. But they are a means, so:

- **Nothing gets refined past what validation needs.** The timing model
  (`cost_milli`, calibration, the exceptions list) makes a port consume
  plausible emulated cycles so interrupts land where the original puts them. A
  native build has no cycle budget, so this is worth exactly as much as the
  gate needs and no more.
- **Emulator accuracy is out of scope** unless it blocks validation: the IRQ
  trigger delay, the S-DSP's KON poll, big-endian hosts, the ZSNES state
  import. These make the scaffolding better.
- **So is most of the stack question.** The per-port fibers exist only
  because a hand port must run to completion while an emulated machine waits
  mid-instruction (`job_run`, `runtime/port/fiber.h`). No emulated machine, no
  call-outs, no per-port fibers. What stays is one coroutine for the game's
  main thread, whose waits for the next frame are deep in its call stack
  (PLATFORM.md, "Timing without a beam"), unless every caller of those waits
  is rewritten to resume. So a browser build still needs one stack switch; the
  wasm backend spike is not scheduled, and when it is, it is for that one.
- **M4** (recompiler speed) buys nothing the targets want.

### What "done" means, measured

Three numbers, because no one of them says it. `tools/burndown.py` counts only
65816 code in the ROM image, so it cannot see the two items below; and a port
it counts as done may still be transliterated or welded to the machine, which
`tools/portstyle.py` measures instead.

| | Now | Done when |
|---|---|---|
| ROM code reimplemented (`burndown.py`) | 70% left | 0% |
| Ports that read as source (`portstyle.py`) | 0.27 ops/line | ~0 |
| Ports off the machine (`portstyle.py`) | 79% take a `Snes *` | entry wrappers only |
| ROM reads by address | 117, in 22 files | 0, all through accessors |
| SPC700 sound driver | read and mapped (SOUND.md); port not started | reimplemented in C |
| Video fed by service calls | decoded from PPU registers | `vid_*` direct |

### The two gaps this list exposed

**The sound driver proper is still interpreted assembly.** `game/core/sound_driver.c`
is the CPU half only -- the code that uploads Square's driver and sends it
commands. The driver itself runs on the emulated SPC700, feeding the emulated
S-DSP, in the shipping path today. It is not in the burndown, not in `game/`,
and was not in any phase. **It is now Phase 4a**, and it has been read
([SOUND.md](SOUND.md)). The decision taken: port the driver (the sequencer, the
song and effect handling, voice output) to C as game code, against a
voice-level interface whose second implementation is a native mixer reading the
same BRR samples, ADSR envelopes and echo settings. The upload protocol between
the two processors is not ported: once both halves are C, commands are calls
and song data comes from the asset accessors. The mixer is the audio engine,
not an emulator; the SPC700 core is deleted with everything else under
`runtime/`.

**The native renderer is fed by emulated registers.** P3 is done and
pixel-identical, but it works by decoding PPU register writes, so the register
file is still in the path. PLATFORM.md's P3 leftover -- spans fed by `vid_*`
service calls instead -- was filed as serving widescreen. It does not: it gates
"no emulation backend", and it moves to Phase 4a with the audio.

### Retiring the gate, deliberately

Byte-exact validation is a scaffold, and target 4 is the licence to take it
down: the game has to stay structurally the same, not byte-identical. So a
subsystem retires it **on purpose, in its own commit**, when it is fully ported
and no generated code touches its state any more: its storage moves out of
emulated work RAM, and shadow mode is replaced for that subsystem by the routes
and the gameplay tests. Until that point the gate holds, in full. Retiring it
early, or by accident, is the one failure this plan is most concerned with.

**A guard becomes an assertion when the gate retires.** While the emulator is
there, a port's guard refusing a state is harmless: the original runs it, so
the product stays exact, and the rewrites lean on that -- event's guards turned
away 4.2 million of the 7.3 million states its fuzzing drew, all of them states
no movie produces (per-entry shadow totals match the predecessor's on every
entry). A native build has no original to hand a refused call to. So when a
subsystem retires the gate, each of its guards becomes a claim that the game
never produces that state, and it is checked as one: over the routes and the
movie library first, then as an assertion in the build, where a refusal is a
bug to fix in the port rather than a call to pass on.

**Retiring the gate also switches the scaffolding off.** What a port writes only
for shadow mode or for the original's still-generated code goes through
`ORIGINAL_EFFECT` (docs/PORTING.md, "Scaffolding"), and the build already
compiles every port once without it (`ct_game_native_shape`). When a subsystem
retires the gate, its scaffolding is deleted in the same commit, and anything
the evidence found reading it from outside (event's scratch cells feed the
menus' wave phase; TODO.md) is decided first. A subsystem's `portstyle.py`
scaffolding count is how much of this is left to take down.

### Working order

Phases in order, work within them in parallel. A phase's items can run side by
side when they don't share files, each in its own worktree and each through the
gate before it lands, and a later phase's item can start early when it depends
on nothing still open. Parallel work shares one machine, so each track is given
a process budget.

## The validation gate

Nothing lands without it, and it is the same gate for every phase:

- every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines
  and no `page refused` lines (a guard that turned away a call on a direct
  page the map declares no instance for; DISASSEMBLY.md, "The accessors");
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
- no port transliterates the assembly where one didn't before
  (`tools/portstyle.py`, a ratchet against its baseline; "Phase 3a" below);
- `--misses` stays empty;
- `python3 -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

## Phase 3a: the transcription backlog

**This comes before converting any more generated code.** The ports written
before the reimplementation bar existed are not decompilation: they move
emulated registers about, spell the game's variables as addresses and follow
the original's basic blocks. They pass the gate, so nothing is broken, and that
is the trap -- the burndown counts them as ported, so the number that paces
Phase 3 measures the distance to *native execution* while the goal is a
*readable decompilation*. Every bank converted on top of them widens the gap.

There is a second failure, independent of the first and deeper: **98% of the
functions in `game/` take a `Snes *`**. They can only run on an emulated SNES,
whatever they look like inside. `game/battle/effect_paths.c` is the proof that
the two are different problems -- it scores 0.03, among the best in the tree,
and every one of its functions is on the machine. Nothing in a GBA, browser or
SNES-ROM build has a 128 KiB work RAM at `$7E` to index, so this is what
actually blocks Phase 4, and it is why 3a rewrites for both at once: touching
62 files twice would be waste.

`python3 tools/portstyle.py` measures both. The style score counts operations
on the emulated registers per line (not memory access, call-outs or hand-backs:
the failure mode, not talking to the machine at all). Beside it is the share of
each subsystem's functions that take the machine. **32 of 96 files, 14,026 of
27,926 lines (50%), are on the backlog**, and all but the scenes, the events
and the field is on the machine:

| Subsystem | Files on the backlog | of | Lines | of | Style | On the machine |
|---|--:|--:|--:|--:|--:|--:|
| battle | 20 | 27 | 7,779 | 10,310 | 0.33 | 99% |
| menu | 10 | 12 | 5,947 | 6,936 | 0.35 | 97% |
| event | 0 | 19 | 0 | 2,233 | 0.01 | 14% |
| field | 0 | 17 | 0 | 4,134 | 0.02 | 26% |
| scene | 0 | 13 | 0 | 2,397 | 0.05 | 54% |
| core, data | 2 | 7 | 300 | 1,883 | 0.11 | 99% |
| **total** | **32** | **96** | **14,026** | **27,926** | **0.22** | **65%** |

Scene's 54%, event's 14% and field's 26% are their entries' wrappers, their
guards and the bridges around their call-outs, and nothing else, counted by
name (`portstyle.py`'s own column misses one-line definitions and
macro-generated entries; RECOMP.md's "Phase 3a" sections for the three list
what is neither an entry nor a guard, and why). Event's moves call the field's
motion as C now (RECOMP.md, "Phase 3a: event onto the field's objects"); its
bridges left are the commands whose callees are still the original's (the
sound driver, the menus' party routines, the field's state routines, the tile
copy and the location change).

### What decoupling means here, and what it must not break

The validation gate is byte-level: `native.c` refreshes a machine copy and
compares every written page of work RAM after **every call**, millions per
movie. That comparison is why a port is proven rather than believed. A port
that keeps the party's HP in a C struct instead of at `$7E:5E30 + slot * $80`
has nothing left to compare -- and 70% of the ROM is still generated code
reading and writing those same bytes. **Decoupling the storage now would delete
the oracle.**

So the source is decoupled now and the storage is swapped per subsystem, later:

1. **The port sees only abstraction.** Typed accessors with the game's own
   names and no addresses, plain C parameters and results, and no `Snes *` and
   no `m_*` outside a thin wrapper at each registered entry, which is where the
   original's registers, flags and direct page are translated. CLAUDE.md's
   65816 rule already asks for exactly this shape; nothing measured it.
2. **The accessors are RAM-backed while a subsystem is still half-generated**,
   so shadow mode keeps comparing bytes and nothing is lost. The seam is the
   one the platform services already use: an interface `game/` includes, and a
   backend under `runtime/` that satisfies it.
3. **The storage swaps once a subsystem is fully ported** and no generated code
   touches its state, in one change behind unchanged accessors. That
   subsystem's validation moves from byte diffs to the routes and the gameplay
   tests at the same time, deliberately and in its own commit.

`re/ram.toml` is already the schema this needs: 800 variables carrying `count`,
`stride`, `index` and named `field`s. `tools/ram_map.py` emits
`#define RAM_FOO 0x7E5E30` from it today; emitting typed accessors and record
structs instead is a tooling change, not a rewrite, and it comes first.

**What can't be decoupled yet, and why that's fine.** A routine that calls out
to still-generated code mid-body needs the machine to make that call, so it
keeps a machine-aware wrapper around a decoupled core until its callees are
ported. That is rarer than it sounds: 138 call-outs across all of `game/`, and
38 of 87 files have none. Decouple leaves first.

The bar is CLAUDE.md's "Writing a port from generated code", and PORTING.md's
"Writing a port" opens with what it looks like when it's met and when it isn't.
The substance of it: a reimplementation may diverge from the original's shape,
because only observable behaviour has to match, and the way to get there is to
write the vocabulary a routine needs -- named operations, named resources, a
service call where it touches the host -- instead of working without one.

The accessors a rewrite calls are generated from `re/ram.toml`:
`python3 tools/ram_map.py accessors` writes `game/vars.h`, the game's variables
as values with no layout in sight, and `runtime/port/vars_ram.c`, the backing
that keeps them in emulated work RAM (docs/DISASSEMBLY.md, "The RAM map").
`game/scene/circle.c` is the pilot that proved the shape, and the rest of
`game/scene/` the first subsystem done to it (RECOMP.md, "Phase 3a: the scene
subsystem off the machine"). What that added for the subsystems after it:
`game/cpu_io.h` for the multiplier, divider and work RAM port;
`game/rom.h` for the `*_data.h` accessors; the page guards in
`runtime/port/machine.h`; page kinds in the map, for cells on a page the
caller chooses (below); and
`tools/fuzz_compare.py`, which fuzzes a rewrite and the port it replaces side by
side from constraints kept in `tools/fuzz_routines.toml`, where each subsystem
adds its entries. The rewrite's first step is the largest don't-care set
`tools/dontcare_check.py --list` accepts for every entry: that is what takes the
register traffic out of the wrappers rather than moving it.

**How it is run.** Per subsystem, by the wave protocol below, each subsystem's
files taken together so shared helpers get written once. A rewrite is cheaper
than a fresh port and much better checked: the behaviour is already under the
gate, so shadow mode compares the rewrite against the original call for call
over the whole library, and the entry keeps its fuzzing. Rewriting a file to
the bar means deleting its line from `tools/portstyle_baseline.txt`; the gate
holds every other file where it is, so the list only shrinks. Order: `menu`
next, then `battle`, whose 7.8K lines are the largest and whose bank is also
the least converted.

What `menu` can take from the field's rewrite (RECOMP.md, "Phase 3a: the field
subsystem off the machine"): a domain module written before the files that use
it (`field/object.h`), with a register-shaped result kept out of its types by
a trace out-parameter only the entry's wrapper passes; a bridge that runs a
ported step inline, timed as that routine (`NATIVE_TIME_AS`), and calls its
entry instead where the state is one the entry's guard would refuse; the
scaffolding writes in one helper per file, with final values only; the flags
a caller reads derived in the wrapper through one set of helpers; and the work
RAM port through `game/cpu_io.h` instead of a direct page on the registers,
which `menu/display.c`'s fills and copies still use. Menu's objects are its own
(the object scripts at `$C2`), not the field's.

**The bar grew after event, and event is being brought up to it.** Reading
event's rewrite, it still read as a translation: the accessors had taken the
addresses out and left the original's structure in, with one function per ROM
routine, the same sequence of per-byte stores, and positional operands. So
CLAUDE.md and the port-routine skill now also require three things:
- the game's concepts modelled above the accessors, which are storage, not
  vocabulary;
- interpreters that dispatch through a C table, where each opcode family is one
  operation, rather than through the ROM's pointer table and an emulated `JSR`;
- comments that say what a function is for in the game's terms.

Event and field are at that bar, event's commands on the field's object
module (`field/object.h`, the only way to an object's state) and the RAM map's
object tables named by concept rather than by the bytes beside them
(RECOMP.md, "Phase 3a: event onto the field's objects"). `tools/ram_map.py`
rejects a stuttering name and `check` lists the names still shaped like
addresses, so the rest of the map can be brought to it as `menu` and `battle`
are rewritten.

**Scene fails the qualitative review, and its rework joins that pass.** The
review's first run, on `scene` (merged under the older bar), rated three files
translation:
- `program.c` emulates the arithmetic unit, with add-with-carry and
  rotate-left as C functions and a divide done as 32 rounds of rotate and
  subtract;
- `circle.c`, the Phase 3a pilot, runs the original's X and Y through RAM cells
  and chains the carry through every edge;
- `bitmap.c` passes a struct that is the CPU (`carry, overflow, x, y, bank`)
  through its logic.

Most of the rest rated mostly. The review's five changes:
- C arithmetic, with flags derived in the wrappers;
- loop state in locals, with final values written once as scaffolding;
- the sequence script decoded into an operation enum;
- display state in the video service's vocabulary, not literal register bytes
  fed to decoders;
- the subsystem's own concepts (a task page kind, one `scene_flags`
  definition, named scratch fields, entries named for what they do, and files
  grouped by title, race, script and frame rather than by where the code sits
  in the ROM).

The last of those touches the RAM map the way the naming pass does, so scene's
rework runs with it, and it closes when scene passes a fresh review.

**Cells on a page the caller chooses** have a generated shape: a page kind in
`re/ram.toml` lists the pages its code runs on, as measured over the library
(`ct_dev --page-report`), and its variables' accessors take which of them a
port means, never a D (DISASSEMBLY.md, "The accessors"; RECOMP.md, "Page
kinds"). It covers the three sets of such cells the map knew: scene's math
scratch, its own page or a sequence channel's, which `scene/scratch.c` now
reaches through it; the menus' `menu_record` page, for `menu`'s rewrite of
`objects.c`; and the controller input cells, which measurement put on one page,
D = `$0000`, so they are ordinary accessors for `core/input.c` and whatever
reads them. The event engine and its objects run on the fixed field page
(`$0100`), which ordinary accessors cover, and its input conditions
(`event/input.c`) read the input cells.

A rewrite that hits something the accessors can't say is a finding to report,
not something to work around: a buffer the game locates at run time reaches
work RAM by address through `game/wram.h`, and every use of it is a variable
waiting for a name in the map.

What this phase does **not** do is add ports or coverage. The burndown's
percentage stands still while it runs, which is the point: it was already
counting this work as done. What moves instead are portstyle's two numbers:
0.27 operations per line toward zero, and 79% of functions on the machine
toward the entry wrappers alone.

## Phase 3: M5 in bulk

Comes after 3a, on a tree where every existing port reads as source.

Where it stands, from `python3 tools/burndown.py` (an estimate; the tool's
docstring says which way it is wrong). **70% of the code is still generated.**

| Subsystem | Code | Ported | of it inlined | Left | Entries |
|---|--:|--:|--:|--:|--:|
| battle (`$C1`, `$CE`) | 64.9K | 11.2K | 1.1K | 83% | 42 |
| field engine, events (`$C0`) | 59.4K | 18.7K | 0.4K | 68% | 249 |
| menus, text (`$C2`) | 59.3K | 15.0K | 2.6K | 75% | 173 |
| scene program, RAM (`$7E`) | 24.9K | 2.8K | 0.0K | 89% | 54 |
| battle frame work (`$CD`) | 15.5K | 2.7K | 0.5K | 83% | 27 |
| field graphics (`$FD`) | 15.3K | 8.1K | 0.0K | 47% | 52 |
| battle sprites (`$CF`) | 6.8K | 6.4K | 0.0K | 7% | 44 |
| battle NMI, graphics (`$D1`) | 5.7K | 4.8K | 0.0K | 17% | 52 |
| battle setup (`$CC`) | 4.9K | 4.5K | 0.1K | 9% | 8 |
| scene engine (`$C3`) | 3.9K | 3.5K | 0.5K | 10% | 28 |
| scene data (`$C6`) | 3.5K | 1.1K | 0.0K | 68% | 25 |
| sound driver (`$C7`) | 2.6K | 2.6K | 0.0K | 0% | 43 |
| misc (`$FF`) | 1.7K | 0.1K | 0.0K | 95% | 1 |
| **total** | **268.5K** | **81.4K** | **5.1K** | **70%** | **798** |

The three big banks are the work; the entries are concentrated in small hot
routines (and the 185 event command entries), which is why the shares are so high
despite the count. The burndown follows the jump tables the analysis has sized,
counts the registry's macro rows, and credits the callees a port inlines (no
call-out at their call; RECOMP.md, "The tooling pass after wave 1: the
analysis", has what each of those moved): helpers no longer need a registry
entry of their own to count. `$CC` is the
one subsystem near done. `$C3` (the scene engine) is done but for code the
beam's counters decide and one routine nothing reaches (PORTING.md's row; the
edge sequence the field's rasterizer inlines is credited to it); `$C6` has its small object
routines and irises, and its Mode 7 map and perspective effects, table
builders and orbit routines are left, none of which a movie reaches.

Subsystem by subsystem, each through the gate, cheapest and best understood
first. `--uncovered BANK` lists the largest unported runs, which is how to pick
the next routine. Phase 3a has already brought a subsystem's existing ports to
the bar, so a conversion here only has to hold them there, which the gate's
ratchet does. The same goes for the script formats' numbered
commands (`cmd_6C`, `if_0A`, `set_bc`): porting a command's handler is when
it gets a real name in `tools/disasm/eventscript.py` or `battlescript.py`, since
that's when someone has read what it does.

Naming is part of that bar. The RAM map (`re/ram.toml`, docs/DISASSEMBLY.md)
names 91% of the work RAM addresses `game/` spells as literals (422 of 466),
so a subsystem's
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

### How a wave is run

A wave is a few subsystems converted at once, each by its own agent. What
follows is the shape that worked for wave 1 and the tooling pass; it is here
because the merge costs, not the porting, are what a new wave gets wrong.

- **One worktree and one branch per agent**, cut from `main`
  (`git worktree add -b <branch> .worktrees/<branch> main`), and **one directory
  of `game/` each** (`game/battle/`, `game/field/`, `game/menu/`,
  `game/scene/`), so two agents never edit the same port. Nobody writes to the
  main checkout, and nobody links its gitignored directories (`roms/`,
  `traces/`) into a worktree: a worktree removal deletes through such a link.
  An agent stages its own ROM (`python3 tools/rom.py stage <path>`) or points
  `-DCT_ROM` at the main checkout's copy, which is read-only use.
- **Shared files are edited in place, not reorganised.** `game/registry.c` (each
  agent's entries together, in its subsystem's part), `re/ram.toml` (its
  subsystem's part), `tools/timing_exceptions.txt` and
  `tools/vblank_check_exceptions.txt` (whose headers say what may be accepted),
  and additive-only changes to `runtime/platform/*.h`. Calibration
  (`tools/timing_gate.py --calibrate game/registry.c`) rewrites every measured
  `cost_milli`, so an agent commits only its own subsystem's.
- **Each agent validates its own branch** with the full gate and
  `--long-routes`, and fuzzes every entry it writes or rewrites.
- **Whoever merges** takes them one at a time, and after each: regenerates
  `game/ram.h` from the merged `re/ram.toml` (`python3 tools/ram_map.py
  header`), recomputes the burndown for this table, and runs the **full gate
  with `--long-routes` on the merged tree**, because a tree combining two
  branches is one nobody has gated. Doc conflicts are usually both sides
  appended; a status row two agents changed takes both edits.
- **A change is reviewed for how it reads before it merges.** A reader
  who hasn't seen the author's report reads every file of the subsystem against
  the rubric in `.claude/skills/review-port/`, and rates each file hand-written,
  mostly, or translation, with findings by line. A file rated translation
  doesn't merge; a "mostly" finding is fixed, or accepted with a reason in the
  subsystem's RECOMP.md section. The gate proves behaviour and `portstyle.py`
  measures syntax, so this is the only check that sees whether the code reads
  as a hand reimplementation, which is the goal. Event scored 0.02 on
  portstyle and still read as a translation.
- **A wave ends with a review**: each agent reports where the workflow and the
  tools still hurt, and those answers decide whether the next thing is another
  wave or a tooling pass. Wave 1's review produced the tooling pass recorded in
  RECOMP.md, which cut the cost every later port pays.

**Wave 2: the three big banks.** `$C0` (field and events, 68% left), `$C1`
(battle, 83%) and `$C2` (menus, 83%) are about 160 KiB between them, far too
much for one agent each, so each bank goes in chunks of roughly 8-12 KiB, taken
in an order the burndown's `--uncovered` and the profile suggest, and one agent
per bank at a time so each keeps its own directory. One thing found in wave 1
belongs to a bank's own wave rather than to whoever finds it: the battle
routines that live in `$FD:A8A5`-`$B7EB` but are called from `$C1`.

`$C2`'s first three chunks are in (RECOMP.md, "Phase 3, wave 2"): the Epoch's
two Mode 7 screens, which were also P2's last leftover; the object scripts'
whole instruction set; and the map those scripts scroll and redraw. What is
left of the bank is the menus proper -- `python3 tools/burndown.py --uncovered
C2` puts the largest runs at `$C2:9C31` (1685 bytes), `$C2:B520` (1530),
`$C2:42F8` (1441) and `$C2:27DE` (1385) -- and bringing the rest of the bank's
early ports (`menu/display.c`, `menu/text.c`, the sprite drawing in
`menu/objects.c`) up to the per-port bar. `$C2:27DE` is the run to take with
`menu/display.c`'s loader half rather than on its own, and only once the
streams it decompresses are typed: it is the object mode's screen setup, about
fifteen routines that each load one stream named by a byte of the scene record
at `$C6:FD00`, and without knowing what each stream holds their ports would be
named after their destination addresses (RECOMP.md, "Phase 3, wave 2: the
menus' map").

**What event's bridges still call in the original.** Of the event commands'
44 bridge helpers, most call the field's motion and animation, which are hand
ports and, since the field's rewrite, C in `field/object.h` (`$C0:ABA2`,
`$C0:5B95`, `$C0:ACFD`, `$C0:AC69`, `$C0:5614`, `$C0:568B`, `$C0:305D`,
`$C0:30B3`), and the sound driver (`$C7:0004`, a port). What is still generated
behind them, with the instructions each reaches and its bytes to the next
label (`functions.json`):

| Routine | Called from | Instructions | Bytes |
|---|---|--:|--:|
| `$C0:9E29` | `$B0`, an object in state 0 | 33 | 91 |
| `$C0:A26B` | `$B0`, state 1 | 215 | 99 |
| `$C0:A2CE` | `$B0`, state 2 | 219 | 109 |
| `$C0:5B8D` | `$B0` after `$C0:9E29`, whose carry decides whether `$C0:3154` runs | 6 | 8 |
| `$C0:3154` | `$B0` when it does | 42 | 93 |
| `$C0:AF4E` | `$E4`'s tile copy | 167 | 328 |
| `$C2:8004` | the menus' party routines (`$D5`, `$F8`-`$FA`), through their dispatcher | 3 | 146 |

The three state routines share code past their labels (215 and 219
instructions in about 100 bytes each), and `$C0:3154` lies in a 1003-byte
unported run, `$C0:9E29` in one of 462 and `$C0:A26B`/`$C0:A2CE` in the 1168
bytes from `$C0:A079`: those runs, with `$C0:AF4E`, are the `$C0` chunk that
would take the move and state commands off the machine entirely. `$C2:8004` is
the menus' and goes with `$C2`.

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

**What `$FD` has left** after wave 1 (RECOMP.md, "Phase 3, wave 1"): the
time-gate tunnel scenes event `$FF` starts with `$DF` = `$80`-`$83`
(`$FD:DA00`-`$DD95`, their interrupt handlers `$FD:DE15`/`$DE87` and helpers
`$FD:EAA6`-`$F2xx`, 2.6 KiB), which draw through the PPU directly and want the
video service; the location's tile animation loader `$FD:DE98`, blocked on the
work RAM port writes through `$217F` (TODO.md, "P1 leftovers"); and 4.2 KiB of
battle routines at `$FD:A8A5`-`$B7EB` that `$C1` calls, which belong with the
battle's conversion.

**Two things the backlog does *not* need**, worth knowing before scoping Phase
3a: the platform lint already covers all of `game/` bar three documented files,
so hardware access is behind the services everywhere, and no screen geometry is
baked in that widescreen would have to undo. The rewrite is readability, not
portability.

This is the long haul. It is worth re-reading the burndown after the first two
subsystems and deciding whether the workflow needs another pass before
continuing; RECOMP.md's "What drafts don't do yet" lists where the draft tool
falls short, starting with D and DB where only interrupts reach.

## Phase 4a: what still runs on emulation

Three things stand between a finished `game/` and a native build with nothing
under it. None of them is optional for target 1, and none was scheduled before
the audit that found them.

- **The SPC700 sound driver, 7151 bytes.** Square's audio engine runs as
  interpreted SPC700 assembly today (`runtime/platform/direct/audio.c` runs the
  core; the ported `game/core/sound_driver.c` is only the CPU side that uploads
  it and sends commands). Block 0 of the boot upload (`$C7:0000`), 7151 bytes
  to SPC700 `$0200`, is the program: 3156 instructions and 0.7 KiB of tables,
  read and mapped in [SOUND.md](SOUND.md). Reimplement it in C as game code:
  the sequencer and its 60 events (about 40% of it), the song, effect, fade and
  command handling (30%), and voice output (10%); the rest is the upload
  protocol, which a build where both halves are C doesn't need. On the order of
  2000 lines of C, a few times the `$C7` CPU half.

  **Tooling: a linear disassembler and coverage, nothing more.** Both exist
  (`tools/spc700dis.py`, `ct_dev --spc-coverage`); with five dispatch tables as
  entry points the listing reaches all the code, and the movies run 90% of it,
  with no dead subsystem. Liveness, don't-care sets and drafts don't apply: the
  port isn't compared at routine exits, and a draft of uniform 8-bit code would
  only transliterate it (SOUND.md, "What the port needs").

  **Write it against a voice-level interface** -- key on with a sample, pitch
  and envelope; set pitch; set volume; echo and noise settings -- whose first
  implementation writes the S-DSP registers, so it can be checked register for
  register against the interpreted driver. The native mixer is then that
  interface's second implementation. And drop the protocol in the native build:
  commands become calls, the three status values the game polls become calls,
  and a song's data is read from the asset accessors instead of uploaded.

  Validation is per tick, not per call: run the port in lockstep with the
  interpreted driver, one tick each time it starts one, fed the commands it took
  that tick, and compare the DSP register writes and the status every tick of
  every movie (`ct_dev --dsp-log` is the reference half). Once the port drives
  the audio, a note stream (key ons with sample, pitch, envelope and volume)
  compared between runs is the structural check target 4 allows (SOUND.md,
  "Validating the port").
- **A native mixer behind `au_*`.** BRR decoding, ADSR envelopes, echo and
  mixing, reading the same sample data the driver points at. This is the audio
  engine, not an emulator: it replaces `runtime/apu/dsp.c` in the shipped
  product. Everything the driver produces is S-DSP state (SOUND.md, "What
  the port needs"), so its maths is `dsp.c`'s: start from that file's voice,
  envelope and echo code behind the driver's voice-level interface, with the
  register file and its polling cadence taken out. Divergence in the last bits of a sample is accepted (target 4);
  structure -- which note, which instrument, which envelope -- is not.
- **Video fed by service calls.** P3's leftover: the observer fills in the
  per-span memory pointers the register decoder fills in now, so the renderer
  takes `vid_*` calls rather than a decoded PPU register file. Widescreen and
  any GPU backend need this too, but it is listed here because without it the
  PPU register file is still in the path.
- **Host backends for the other four services.** The seam is right -- each of
  `video.h`, `audio.h`, `timing.h`, `input.h` and `save.h` picks its backend
  with `#ifdef CT_PLATFORM_SNES` -- but **only one backend exists**, and it
  writes SNES registers. That backend is not waste: compiled for real hardware
  it is what target 3 runs on. What is missing is the host side of four of
  them. Input and save are small (`in_host_set` and `frontend/player.c` already
  carry the host's half). Audio is Phase 4a's mixer, above.

  **Timing is decided** (PLATFORM.md, "Timing without a beam", with the census
  behind it: 117 instructions, 49 ported). The game has no raster effect done
  with an interrupt: all three line handlers blank the picture after line 211
  and upload. So the native backend is a frame of vblank handler, main thread
  to its wait, line handler, with `tm_enable_interrupts` and `tm_set_line`
  recording which handlers run and where the picture ends. Five timing calls
  are added (`tm_wait_frame`, `tm_frame_time_left`, `tm_wait_line`,
  `tm_entropy`, `tm_odd_frame`) and `in_ready` in input, and
  `tm_video_status` has no native answer, so its six call sites are rewritten
  to those. The work is the backend, the calls, and the rewrites TODO.md lists
  under "Timing without a beam"; the main thread runs as one coroutine.

**The asset seam comes forward to here** from where it sat behind everything.
It is 117 ROM reads by literal address across 22 files -- days, not weeks --
and until it is done every port reads a mapped 4 MiB ROM image, which a GBA
cannot map at all (target 2) and which makes "load from a user's own ROM into a
data directory" impossible (TODO.md, "Platform"). Route each through a named
accessor for the resource, as `sound_data.h` and `scene_data.h` already do.
The pipeline half -- a layout meant to be read rather than reassembled, the
scripts as text -- follows in Phase 5 with MD3.

## Phase 4b: the platform milestones

Serving widescreen and the GPU backends rather than "no emulation", so they
wait behind 4a.

- **P4 widescreen**, and with it the answer to whether `VidFrame` holds up:
  PLATFORM.md's open questions name the assumptions it would expose.
- **The beam-coupled routines**, the event scheduler `$C0:5A46` and the sprite
  frame queue `$C0:CA76`, which stop when the beam passes a scanline. Their
  shape is decided (PLATFORM.md, "Timing without a beam"): the budget is
  `tm_frame_time_left`, which a native backend answers with "all of it", so
  the work the original defers on about 1% of frames runs a frame earlier.
  They need nothing else from 4a, so they can come forward into Phase 3's
  field work once the call exists; what waits for them here is only the check
  that the routes still pass with the budgets drained.
- **Host backends beyond SDL** (TODO.md, "Platform"): rendering on the GPU,
  controllers.

## Phase 5: the long tail

- **MD3**, and the asset pipeline it enables: a layout meant to be read rather
  than reassembled, and the scripts as text. **MD3 is the prerequisite for
  target 3**, the SNES ROM compiled from the reimplementation -- every pointer
  symbolic, so a routine that differs in length can move -- so it comes forward
  with that target rather than waiting here.
- **The SNES ROM itself** (TODO.md, "A SNES ROM compiled from the hand ports"):
  MD3, then a hybrid that links one compiled routine in place of the original's
  and checks the ROM plays, then outward. `tests/test_port_subset.py` already
  keeps `game/` inside what a 65816 compiler handles, and Phase 3a's decoupling
  is what makes a routine linkable at all: the logic apart from the emulated
  machine is exactly what a SNES build compiles.
- **MD2's remainder**: 42590 census-unknown bytes, 228.3 KiB still `mixed`, and
  `lz` re-encoding, plus the **MD4 residue**. The unknown bytes are by
  definition ones no reader has been found for, so no port depends on them;
  they are proof-of-completeness work, and the formats that ports do depend on
  were typed before the bulk conversion.
- **The asar cross-check**, which has never run.
- **Coverage**: the unrecorded endings, the Reunion route, and re-running
  `--misses` over the whole library.

## Slot in whenever

- **The `fair` and `fair-companion` regression.** After the timing fix these two
  got *worse* against Snes9x and nobody knows why (TODO.md, "Movies and
  coverage"). It is the only open item that might be a bug rather than
  unfinished work, so it should not wait for a phase.
- **CI's macOS job**, which costs 10x minutes on a private repo.

## What this plan deprioritizes, and why

The targets decide this, so the old list is gone: a GBA port and a SNES ROM
compiled from the reimplementation are targets 2 and 3, not interests, and they
are scheduled above (Phase 4a's asset seam, Phase 5's MD3) rather than deferred.
What stays deprioritized is what serves the scaffolding or nothing:

- **M4** (recompiler speed). The generated C is a means; making it faster helps
  no target.
- **Emulator accuracy**: the IRQ trigger delay, the S-DSP's KON poll,
  re-measuring the audio comparison table, big-endian hosts, the ZSNES state
  import. Out of scope unless one of them blocks validation.
- **The wasm stack question.** The per-port fibers exist only to suspend an
  emulated machine mid-instruction while a port runs; with the machine gone
  there are no call-outs and no per-port fibers. One coroutine stays, the
  game's main thread (above), so a third `fiber.h` backend is worth building
  for that alone when a browser build is wanted, not before. A browser build,
  if it is wanted, is a frontend on top of a finished `game/` -- which is the
  same thing targets 1 and 2 need.
- **Anything that makes the hybrid modes nicer to live in.** `--recomp on` and
  shadow mode are how the work is checked, not what is being built.

The host seam was never deferred, because it was the same work as making the
desktop build shippable: the player advances a frame per call and persists
bytes under names (PLAYING.md, "How the player is built"), so a new frontend
needs a driver of its own rather than a fork of the player. That is what makes
targets 1 and 2 a matter of writing drivers once `game/` stands alone.

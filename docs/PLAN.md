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

The emulator under `runtime/` -- the 65816 interpreter, the SPC700, the S-DSP,
the PPU and the bus -- **must not ship**, and neither must the generated C,
shadow mode, the oracle or the timing model. What does ship from `runtime/` is
the platform services' interfaces (`runtime/platform/`) with their host
backends, and `runtime/port/fiber.h` for the coroutines the game's main
thread and the menus' tasks run as (below, and PLATFORM.md, "Timing without a
beam"). They exist to prove the
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
  call-outs, no per-port fibers. What stays is the game's own design: a
  coroutine for its main thread, whose waits for the next frame are deep in its
  call stack (PLATFORM.md, "Timing without a beam"), and one for each of the
  menus' eight cooperative tasks, which the original runs on stacks of their
  own and which wait a frame from deep in their work (`menu/tasks.c`). Nine
  fibers at `fiber.c`'s 512 KiB each is more than a GBA has, so a small target
  wants the tasks rewritten to resume (each a state machine called once a
  frame) and its main thread's stack sized to what it uses; the way out for
  the main thread is the same rewrite of every caller of its waits. So a
  browser build still needs stack switching; the wasm backend spike is not
  scheduled, and when it is, it is for that.
- **M4** (generator speed) buys nothing the targets want.

### What "done" means, measured

Several numbers, because no one of them says it. `tools/burndown.py` counts only
65816 code in the ROM image, so it cannot see the two items below; a port
it counts as done may still be transliterated or welded to the machine, which
`tools/portstyle.py` measures instead; and it is a static estimate, where
`tools/fallbacks.py` counts what real play still runs as the original's code
in place of the ports: the native coverage.

| | Now | Done when |
|---|---|---|
| ROM code reimplemented (`burndown.py`) | 3% left (97% has a port) | 0% |
| Native coverage: of the code real play runs, the share that runs as hand ports | 87% (212.6 KiB run by the 28 movies; battle 97%, field and events 98%, menus and overworld 84%, scene program about 75%, battle frame work 90%, field graphics 92%, scene engine 71%) | 100% |
| Code run in place of hand ports (`fallbacks.py`) | the reference play (42 movies): 6.6 KiB, 0.95 M instructions (handback 2.6 KiB, unported 4.4 KiB; 1.66 M before chunk 10, the Epoch's dive's 0.41 M of it now 9.5 K); shipping mode over the movies 0.09 M (2.40 M before chunk 10, 2.31 M of it the dive's, now 2.1 K), the routes 0.06 M (2.37 M before); no entry refuses in real play over the library; the native shape runs every movie and reaches every route's outcome, with no exceptions (REIMPLEMENTATION.md, "The fallback measure", "The last refusals in real play: the attract demo's states") | 0, no refusal, the native shape passing |
| Ports that read as source (`portstyle.py`) | 0.02 ops/line | ~0 |
| Ports off the machine (`portstyle.py`) | 27% of functions take a `Snes *` | entry wrappers only |
| ROM data behind named accessors | every read but one (`battle/scripts.c`); the accessors still address a mapped ROM image | backed by an asset pack imported once from the user's ROM (Phase 4a, "Assets") |
| SPC700 sound driver | reimplemented in C and what `ct_play` plays, its commands calls rather than uploads (SOUND.md); the voices play through the native mixer, which makes every frame the S-DSP makes (PLATFORM.md), or with `--mixer dsp` the S-DSP; the original stays as the gate's reference | the original and the S-DSP deleted |
| Video fed by service calls | decoded from PPU registers | `vid_*` direct |
| How the game is entered | the emulated CPU walks the ROM from its reset vector and finds each port by address in `game/registry.c` | `main()` boots the game and callers call ports; no registry, wrappers or emulator linked (Phase 4a, "The native program") |

**Burndown overstates how native the game is.** It counts a port's whole
reach as done, including what the port hands back to the original at run
time: a call-out, a stop, a refused guard. Of the 49.7 KiB real play still runs
as the original, 25 KiB is code burndown counts as ported -- battle's
animation commands and call-outs (9.1 KiB), the menus and overworld (6.6 KiB),
the battle frame work's renderer pieces and waits (2.4 KiB) among it. So a
fully native game is two pieces of work, not one: porting the 12% burndown
lists, and retiring the fallbacks inside code it counts as done, which is
about two thirds as much again. Native coverage is the headline; burndown says
how much has a port. The second piece is mostly callees a port calls out to,
which belong to the port that calls them, and the waits still made on the
machine, which go to the timing service's calls ("Timing", below). How the
two numbers are measured: the fallback measure's report (`tools/fallbacks.py
--report`) against `burndown.py`'s coverage over the movies' executed code.

### The two gaps this list exposed

**The sound was the emulated S-DSP fed by interpreted assembly.**
`game/core/sound_driver.c` is the CPU half only -- the code that uploads
Square's driver and sends it commands -- and the driver itself ran on the
emulated SPC700. It is not in the burndown and was not in any phase. It is
Phase 4a: read ([SOUND.md](SOUND.md)), ported to C as game code
(`game/sound/`: the sequencer, the song and effect handling, voice output,
and the game's side, whose commands are calls and whose samples the driver
places itself), and now what `ct_play` plays. The voices' second
implementation, a native mixer reading the same BRR samples, ADSR envelopes
and echo settings in place of the emulated S-DSP, is built and makes the
S-DSP's every frame, and is what `ct_play` plays. The mixer is the
audio engine, not an emulator; the SPC700 core is deleted with everything
else under `runtime/`.

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

**The order now, by native coverage.** What to port next is picked by what real
play still runs as the original (`tools/fallbacks.py --by routine`), not by
which bank has the most unported bytes: a handback is a port calling code that
isn't ported yet, so retiring it is porting its callee. In order:

1. **The menus and the overworld** (84%): what the shipping mode still runs
   of bank `$C2` is code no port stands for, the screens' own routines still
   the original's (`C2:----`, the fade out `$C2:83CA`, the gate screen's swirl
   `$C2:CE86` and the number tiles in `$FF` it calls), and the endings'
   scenes' line effects behind `$C3:000E` (TODO.md, "What the world map's set
   up left"); and the fallback measure's filing of one task's code under
   another's hand-back, which shows where to look (TODO.md, "What the
   shipping mode's fallbacks' eighth chunk left").
2. **The scene program**, its unported modes after movies reach them, and
   the title's and the attract program's own frame tasks, which the movies
   run as the original's (`$7E:5D71`, `$7E:6A23`, `$7E:69AF` and their
   neighbours, the largest of its handbacks), and what the endings still
   hand back to the original: the program's first script step (`$7E:3749`)
   and the program's sine and
   multiplies (`$7E:33D6`, `$7E:3461`, `$7E:3465`) where a guard refuses
   them.
3. **The battle's** last handback in the shipping mode, the enemies' action
   showing's stub (`$C1:BD6F`).

## The validation gate

Nothing lands without it, and it is the same gate for every phase:

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
  of them with `--long-routes` before a change to timing or the native runtime
  lands);
- the C sound driver matches the original's at every tick of every movie
  (`ct_dev --sound-lockstep --sound-lockstep-level state`, in the shadow runs)
  and over a short `ct_sound_fuzz` in both its modes (SOUND.md, "How it was
  checked");
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
  line says why (`tools/fallbacks.py --refusals`; "Retiring the gate,
  deliberately", above);
- the build with the scaffolding compiled out runs every movie to its end and
  reaches the routes' outcomes in the shipping mode (`ct_dev_native_shape`,
  `ct_drive_native_shape`; REIMPLEMENTATION.md, "The native shape, run");
- `--misses` stays empty;
- `python3 -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

## Phase 3a: what is left of the transcription backlog

The rewrite of the ports written before the reimplementation bar is done:
scene, event, field, menu, battle, core and sound are all at the bar and
merged (docs/history/reimplementation-phase3a.md has each subsystem's account).
**1 of 307 files, 117 of 95,610 lines, is left on the backlog**: `data/decompress.c`,
the one line in `tools/portstyle_baseline.txt`. What is still on the machine
is, outside core, the entries' wrappers, the guards and the bridges round the
call-outs to the original's code:

| Subsystem | Lines | Style | On the machine |
|---|--:|--:|--:|
| battle | 17,302 | 0.02 | 27% |
| menu | 10,035 | 0.03 | 33% |
| field | 9,028 | 0.01 | 21% |
| event | 4,255 | 0.01 | 14% |
| scene | 2,492 | 0.04 | 45% |
| core, data, attract | 2,054 | 0.06 | 70% |
| sound | 1,983 | 0.00 | 0% |
| **total** | **45,119** | **0.02** | **28%** |

What the phase settled, and what still holds for every port:

- **The source is decoupled now; the storage is swapped per subsystem,
  later.** The gate compares every written page of work RAM after every call,
  and much of the ROM is still the original's code reading those bytes, so
  decoupling the storage now would delete the oracle. Ports see only typed
  accessors (`game/vars.h`, generated from `re/ram.toml` with
  `python3 tools/ram_map.py accessors`), plain C parameters and results, and
  no `Snes *` or `m_*` outside a thin wrapper at each registered entry. The
  accessors are RAM-backed (`runtime/port/vars_ram.c`) until a subsystem is
  fully ported, when its storage moves behind unchanged accessors ("Retiring
  the gate, deliberately", above).
- **A routine that calls out to still-generated code mid-body** keeps a
  machine-aware wrapper around a decoupled core until its callees are ports.
- **The bar is CLAUDE.md's "Writing a port from generated code"**, with
  PORTING.md's "Writing a port" as its worked examples: the game's concepts
  modelled above the accessors, interpreters dispatching through a C table,
  comments in the game's terms. `tools/fuzz_compare.py` fuzzes a rewrite
  against the port it replaces.
- **A rewrite that hits something the accessors can't say is a finding**: a
  buffer the game locates at run time reaches work RAM by address through
  `game/wram.h`, and every such use (150 in 37 files) is a variable waiting for
  a name in the map. Cells on a page the caller chooses have page kinds
  (DISASSEMBLY.md, "The accessors").

## Phase 3: M5 in bulk

Phase 3a has brought every existing port but one to the bar, so a conversion
here only has to hold them there, which the gate's ratchets do.

Where it stands, from `python3 tools/burndown.py` (an estimate; the tool's
docstring says which way it is wrong). **3% of the code is still generated.**

| Subsystem | Code | Ported | of it inlined | Left | Entries |
|---|--:|--:|--:|--:|--:|
| battle (`$C1`, `$CE`) | 63.9K | 63.9K | 16.3K | 0% | 446 |
| field engine, events (`$C0`) | 59.4K | 58.3K | 14.0K | 2% | 399 |
| menus, text, overworld (`$C2`) | 59.1K | 56.6K | 15.7K | 4% | 535 |
| scene program, RAM (`$7E`) | 25.1K | 21.3K | 2.8K | 15% | 196 |
| battle frame work (`$CD`) | 15.4K | 15.2K | 4.3K | 1% | 224 |
| field graphics (`$FD`) | 15.1K | 15.0K | 2.2K | 0% | 111 |
| battle sprites (`$CF`) | 6.8K | 6.8K | 0.2K | 0% | 47 |
| battle NMI, graphics (`$D1`) | 5.7K | 5.7K | 0.0K | 0% | 52 |
| battle setup (`$CC`) | 4.8K | 4.8K | 0.1K | 0% | 11 |
| scene engine (`$C3`) | 3.8K | 3.6K | 0.6K | 7% | 32 |
| scene data (`$C6`) | 3.4K | 3.4K | 0.8K | 0% | 32 |
| sound driver (`$C7`) | 2.6K | 2.6K | 0.1K | 1% | 9 |
| misc (`$FF`) | 1.5K | 1.5K | 0.0K | 0% | 12 |
| console stubs (`$00`) | 0.0K | 0.0K | 0.0K | 0% | 5 |
| **total** | **266.7K** | **258.8K** | **57.2K** | **3%** | **2111** |

`$CD`, the battle's frame work, is C but for 0.2 KiB: the whole screen script
language (REIMPLEMENTATION.md, "Phase 3, battle: the screen script's shapes"),
the frame work's table moves and waits, and the battle's close, the
battlefield put back and the messages (history/reimplementation-chunks.md,
"Phase 4a, timing: the battle frame work's remainders"). What `python3 tools/burndown.py
--uncovered CD` still lists is not worth a port as it stands: the screen
script runner's hand-back for a byte past the language's last command
(`$CD:165F`, which no script has), the wait for a line of the picture
(`$CD:3B82`, the timing service's `tm_wait_line` already) and script command
`$C1`'s jump to it (`$CD:1D93`, in no movie), a battle lost on the screen
effects' battlefield (`$CD:0D93`, `$CD:0DA2`, which no movie reaches), and the
jump stubs `$CD:0000`, `$CD:0012` and `$CD:0039`. The fallback measure still
counts the frame work's code the timers' dispatcher reaches (above, "The
order now").

The three big banks are the work; the entries are concentrated in small hot
routines (and the 225 event command entries), which is why the shares are so high
despite the count. The burndown follows the jump tables the analysis has sized,
counts the registry's macro rows, and credits the callees a port inlines (no
call-out at their call; REIMPLEMENTATION.md, "The tooling pass after wave 1: the
analysis", has what each of those moved): helpers no longer need a registry
entry of their own to count. `$CC` is the
one subsystem near done. `$C3` (the scene engine) is done but for code the
beam's counters decide and one routine nothing reaches (its row in
docs/ports.tsv; the
edge sequence the field's rasterizer inlines is credited to it); `$C6` has its small object
routines and irises, and the Day of Lavos's turning world, which
`battle-lavos-gameover` reaches; its table builders and orbit routines are
left, which no movie reaches.

Subsystem by subsystem, each through the gate, cheapest and best understood
first. `--uncovered BANK` lists the largest unported runs, which is how to pick
the next routine. The same goes for the script formats' numbered
commands (`cmd_6C`, `if_0A`): porting a command's handler is when
it gets a real name in `tools/disasm/eventscript.py` or `battlescript.py`, since
that's when someone has read what it does.

Naming is part of that bar. Ports reach work RAM through `game/vars.h`'s
accessors, generated from the RAM map (`re/ram.toml`, DISASSEMBLY.md, "The RAM
map"), and each routine converted adds what it learns to the map. It names 74%
of the work RAM addresses `game/` still spells as literals or `RAM_` names (112
of 152, `python3 tools/ram_map.py coverage`). `python3 tools/ram_map.py coverage --open --subsystem S` lists
what is still unnamed; what it can't show is direct-page scratch and the
absolute operands of `$2000` and up, which a porter names while reading the
routine.

The per-port bar, for new ports and existing ones brought up to it: every call
matches in shadow mode over the movie library; the port is **fuzzed** with
`ct_fuzz`, including the paths the movies don't reach, with the run recorded
in its docs/ports.tsv row (a generated block is right on every path by
construction, a hand port only on the paths something exercised); its
`cost_milli` is the calibrated value and its estimate error is within the
timing gate's threshold; and it reads as source, with no cycle costs
carried.

It also **holds the asset seam**: every port but one read (`$C1:7A6B` in
`battle/scripts.c`) reaches ROM data through a named accessor for the resource
(`*_data.h`), never `m_read8(s, 0xD15E10 + c->x)`, and a new port keeps it that
way. That is what lets Phase 4a point the same call at an extracted file or a
GBA-format conversion without revisiting the ports. Where a resource
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
  `--long-routes`, and fuzzes every entry it writes or rewrites. The gate's
  effects check holds the branch to the native shape: a read by the logic of
  what a port writes only as scaffolding is answered before the merge, not
  added to its baseline unexplained (PORTING.md, "Scaffolding").
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
  subsystem's REIMPLEMENTATION.md section. The gate proves behaviour and `portstyle.py`
  measures syntax, so this is the only check that sees whether the code reads
  as a hand reimplementation, which is the goal. Event scored 0.02 on
  portstyle and still read as a translation.
- **A wave ends with a review**: each agent reports where the workflow and the
  tools still hurt, and those answers decide whether the next thing is another
  wave or a tooling pass. Wave 1's review produced the tooling pass recorded in
  REIMPLEMENTATION.md, which cut the cost every later port pays.

**Wave 2: the three big banks**, `$C0`, `$C1` and `$C2`, in chunks of
roughly 8-12 KiB, taken in an order the burndown's `--uncovered` and the
profile suggest, and one agent per bank at a time so each keeps its own
directory. **Aim at the top of that range.** What a chunk costs is mostly
fixed -- the skill, the porting rules, the subsystem's headers, the analysis
the drafts come from -- so two 6 KiB chunks cost nearly twice one of 12 KiB for
the same code. The size to stop at is what an agent can hold well enough to
reimplement rather than translate, not the smallest slice that will pass the
gate. Each chunk's gate run stands alone; the **review** is once per branch, on
the accumulated diff of its chunks, before it merges
(`.claude/skills/review-port/`, "One review per merge"). The per-chunk
accounts are in docs/history/reimplementation-waves.md and reimplementation-chunks.md, the latest
in REIMPLEMENTATION.md.

**`$C1`: the battle.** The bank is in: the enemies' scripts, the
target search, the random numbers, an enemy's turn, the statuses' timers,
every action's effect, a party member's action carried out, the setup's party
helpers with the inventory and gold service (`$C1:8003`), the fight's start
(`$C1:FA8B`) with the `$FD` routines it calls, the rewards of a won
battle (`$C1:F93E`, `$C1:F205`, `$C1:B4E9`, `$FD:B201`: the experience, the
levels, the techs, the items put back), all of which the main loop runs as C,
and the animation script language (chunks 9a and 9b: the command table in C,
the flow, values, poses, drawing, effects and colors; the places and facings,
every move, the grid walk, the sound commands and the screen effect), and
chunk 10: the enemies' movements (`$C1:3772`-`$C1:4021`), the party's turns as statuses, counters and called-off combinations decide
them (`$FD:AB30`, `$FD:AC6E`, `$FD:A8CE`-`$A95F`, `$C1:B3F9`, `$C1:B762`), the
demo's party, and the battle's end's bookkeeping; and the leftovers: the
battle's start from the field and its return there, the setup's last part,
the scheduler's animation step, an effect's next frame and a member that comes
back once; the player choosing (the menu's frame, its lists, the target
kinds and the area search); and an action shown (history/reimplementation-chunks.md, "Phase 3, battle:
an action shown"): the showing's step, the action's animation chosen and
loaded, the poses' looks, the battle's own frame step and step 3, and the
hits landing with a felled enemy's spoils. `python3 tools/burndown.py
--uncovered C1` (and `CF`, `CC`) leaves only jump stubs into ports, and bank
`$FD`'s battle routines are C too, the rewards' messages (`$FD:AD17`) and the
turns' work cleared (`$FD:A982`) the last (REIMPLEMENTATION.md, "Phase 4a,
timing: the battle frame work's remainders, chunk 2"). The
burndown counts the bank done, but the fallback measure shows what its ports
still hand back: with an action's showing C, the largest left are the
battle frame work's loads the showing calls -- the background's change
(`$CD:0018`, `$CD:0EBD` and its moves at `$CD:107D`-`$CD:12B8`, 29 million
instructions of the reference play) and the effects' graphics sets
(`$CD:0015`, `$CD:002A`), which wait on the vblank handler's sends -- then the
steps' routines the dispatch (`$C1:0003`, `battle/steps.c`) still calls out to
and the frame waits (`$C1:007E`, the timing service's; TODO.md, "What an
action's showing left at its edge").

**`$C2`: the menus.** In: the Epoch's two Mode 7 screens, the object scripts'
instruction set and the map they scroll, the load screen, the window command
language, the cursor and sounds, the typesetter, the menus' opening, the
characters in the records, the records' task, the frame's input, the
tilemap touches, the equipment screen, the items screen, the techs screen,
the settings screen (its options through a table, the buttons' assignment,
the meters, the gauge and the window's design, and the main menu's panel for
it), the shop (its tabs, the goods and the party's items for sale, the
panel of who could use an item and how their figures change, the quantity
and the deal), the party's services the field's scripts, the power-on and a
New Game+ call (the party's record, equipping, filling HP and MP), the
figures, the healing and its rising numbers, the inventory's add and take,
the paging, the records' sprites and colours, the held cursors, the list
slide, the description's typing, the main menu, the save screen's opening,
name entry and the battle mode's screen, the shop's steps on the party
(a member's slots and the list of what fits), and the party's exchange
(screens 5 and 7, the main menu's panel for it and the gate's song), the
objects that change the screen (the fades, the mosaic, the palette's loads
and fades, the tile streamer), the equipment screen's list of what fits,
what the scenes' scripts call, the sprite-object mode's start and the frame's
input from the other banks. The
world map walked on foot is its own subsystem, `game/overworld/` (the
leader, the followers, the ground and its places, the regions' music, the
place's sign and the main loop with its round trips to the menus and the
Epoch's screens), with the vehicles' controller and the Epoch flown over the
map and through time, and the world map's set up from its record's streams.
The dactyls' flight over the 65,000,000 B.C. map is C too (history/reimplementation-chunks.md
"Phase 4a, the shipping mode's fallbacks, chunk 5").

**`$C0`: the field and events**, 2% left after seventeen chunks and the field's part of the battle frame work's remainders, and 98% of
what real play runs of it native. What is left is a character's start
(`$C0:597D`), which the menus call, and
waits the timing service takes over where their callers
are ports (TODO.md, "Frame waits still made on the machine"). Every event
command has a row; event's bridges still call the unpacker (`$C3:0002`,
TODO.md).

**`$7E`: the scene program**, the program the field and the game over
decompress to `$7E:3000` (the title, the attract sequence, the jet bike race
and the endings; DISASSEMBLY.md's `[[ramcode]]`), on top of the scene engine
in `$C3`. The jet bike race is C from its setup to its end: the bikes' rules,
the camera, the drawing and the regrouping of its scaled pictures (history
file, "Phase 3, the scene program: the jet bike race running"), then the bikes
placed in perspective, the plane's perspective (shared with the endings), the
main loop's scaling and the setup as one routine (history file, "Phase 3, the
scene program: the rest of the jet bike race"); and so is the telepod
ending's staff roll, mode 13, with the credits' roll the Beyond Time ending
shares (history file, "Phase 3, the scene program: the staff roll"); and so
are the endings that share `$7E:8538` (modes 10, 11, 14 and 15): their
setup, the picture the main loop zooms, and the scenes their scripts start
(history file, "Phase 3, the scene program: the shared endings"); and so are
the star field, the captions and the last scene's swept window, with the
program's stirred random numbers as game code (history file, "Phase 3, the
scene program: the star field, the captions and the random numbers"); and so
are the program's start and end and the title from its setup to its end
(history file, "Phase 4a, the native frame: the title"; history file, "Phase 4a, the native frame: the scene's tables as C").
So is the Beyond Time ending's night: the flight over the world's map, the
Epoch, the town's lights, the fireworks and the Mode 7 pictures' vblank
tasks, with the program's sine and signed multiply as entries (history file, "Phase 4a,
the shipping mode's fallbacks, chunk 4"), and the Mode 7 picture's tiles'
expansion (history/reimplementation-chunks.md "Phase 4a, the shipping mode's fallbacks,
chunk 5"), and the swept window's rider's sprite list (history/reimplementation-chunks.md,
"Phase 4a, the shipping mode's fallbacks, chunk 6"). **Next**, by what the
library reaches: the program's first script step's tail after the sequence
interpreter's wait for the music (`$7E:3749`, `$C3:0C28`). The helpers before the shared setup (`$7E:7FC4`-`$7E:8537`) and
`$7E:8A01`-`$7E:8B51` no movie reaches. The attract sequence's two scenes (modes 1 and 3) are ports (REIMPLEMENTATION.md,
"Phase 4a, the native frame: the attract sequence's pages of credits and the
palace rising"). The program's other scenes (modes 4-8, whose routines lie
around the title's between `$7E:53BB` and `$7E:7FC3`) wait for a movie that
reaches them: none does, so a port there would rest on fuzzing alone.

**What `$FD` has left** after wave 1 (docs/history/reimplementation-waves.md, "Phase 3,
wave 1: bank `$FD`"), the time warp (`game/warp/`) and the location's
animated tiles (`core/tile_animation.c`): the warp's line handler `$FD:DE87`
(empty, and never enabled) and a palette turn `$FD:DD1D` no code reaches. The battle's routines in the bank go with `$C1` (above).

REIMPLEMENTATION.md's "What drafts don't do yet" lists where the draft tool still falls
short, starting with D and DB where only interrupts reach.

## Phase 4a: what still runs on emulation

Four things stand between a finished `game/` and a native build with nothing
under it. None of them is optional for target 1. The first three were found
by an audit; the fourth is the program itself.

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
  carry the host's half). Audio is the native mixer (PLATFORM.md), which
  already makes the sound from the voices with no S-DSP.
- **The native frame.** The power-on path and the interrupts' entries are
  hand ports (`core/console.c`; history/reimplementation-chunks.md, "Phase 3, field and events: the
  console's start and the field's frame"), and so are the field's first start
  and main loop, and the scene program's start and the title from its setup
  to its end: `ct_play --generated off --interpreted stop` (PLAYING.md) runs
  the field's first start, the program's start into the title, the title's
  frames and its scripts' routines, the music fading out and the screen going
  dark, the scene's end into the menus' attract scene and its curtain, the
  attract sequence's pages of credits and its field scenes, the change of
  place an event makes at once between them, and into the attract's battle,
  to frame 3779, and stops at `$CD:0000`, the jump stub the battle's start
  (`battle/from_field.c`) reaches the battle screen's setup through
  (`$CD:0039`, then the port at `$D1:F1B0`; REIMPLEMENTATION.md, "Phase 4a,
  timing: the battle frame work's remainders, chunk 2"). Over 3,000 frames
  from power-on `--interpreted log` lists nothing; over 20,000 nothing in the
  scene program, the field or bank `$FD`, and 204 addresses (from 513): the
  battle's as the attract's battle plays them (`$C1`, 195, most of them the
  party's poses `$C1:35DD`-`$C1:3713` the setup ends with, which are C now
  and await a new count, and its jump stubs at `$C1:0003`-`$C1:0058`), and
  the stubs `$C0:0002`, `$CC:F062`, `$CD:0000`, `$CD:0003` and `$CD:0039`.
  Next: the stubs, then on to the load screen and a new game.

  **Timing** (PLATFORM.md, "Timing without a beam, as built"): the calls and
  their host backend are built, the field's budgets, the menus' task switch
  and the battle's wait routines are ports, and no port reads the beam's
  status. What is left is in TODO.md, "Timing without a beam": the waits
  still made on the machine (the field's `$C0:EC60` the largest, which needs
  a wait a machine copy can hold before it takes the battle's shape; the
  line handlers wait for their line through `tm_wait_line_drawn`), the host
  backend's place in `ct_native`, and the
  menus' tasks, which each need a coroutine of their own or a rewrite to
  resume.

- **The native program, and the registry taken down.** Today the emulated
  CPU walks the ROM from its reset vector, and a port runs because the
  intercept finds its address in `game/registry.c`; its entry wrapper turns
  the machine's registers into C arguments and back. That is what lets the
  gate run the original beside the port, and it is scaffolding like the rest:
  a shipped build has a `main()` that boots the game and callers that call
  ports. It is taken down in deliberate steps, each its own commit, none of
  them by accident:
  1. **Measure it.** A census from a shipping-mode run (`ct_dev --native on
     --generated on`): for each registered entry, how often it was entered by
     the machine (from generated code, the interpreter or a vector) and how
     often a port called it as C. The count entered by the machine is the
     number this item drives to 0, and goes in the table above. The census
     names which caller still holds each entry to the registry.
  2. **Ports call ports.** Where a port reaches another port through a
     call-out (`portsmell`'s `sibling-through-machine` already finds these), it
     calls the C function, keeping the callee's time with `NATIVE_TIME_AS`.
     This happens anyway as subsystems finish; the census says where it is
     still owed.
  3. **The skeleton.** `game/` gets the program's top: a boot function, the
     main thread and the menus' tasks as coroutines (`runtime/port/fiber.h`)
     whose frame waits yield to the driver, and the frame's work -- the vblank handler,
     the line handler -- as functions the driver calls each frame, in place
     of the interrupt vectors and the RAM trampolines the game points them
     through. The power-on item above ports what those entries do; this
     step makes them functions the driver calls rather than addresses the
     CPU jumps to.
  4. **A slice with nothing under it, early.** A build target (`ct_native`)
     that links `game/`, `runtime/platform/` with its host backends and
     `fiber.h`, and none of the emulator, the generated C or the registry,
     run as soon as boot, the title and a few field maps are ported. Its
     link errors and its first frames are the list of what still assumes a
     machine -- a wait on the beam, code run from RAM, a port that reads a
     register -- found while there is time to change course, not after the
     last port. It is a measurement, not the product, until the rest
     arrives.
  5. **Per subsystem, the registry goes with the gate.** When a subsystem
     retires the gate ("Retiring the gate, deliberately", above), its rows
     leave `game/registry.c`, its entry wrappers and leaves files are
     deleted, its entries become ordinary functions its callers call, and
     its guards become assertions. The dev build keeps the registry for the
     subsystems still under the gate.
  6. **Last, the machine leaves the product.** When no subsystem is under
     the gate, `game/registry.c`, the intercept, the entry macros and
     `runtime/`'s emulator are deleted from what ships; `ct_play` is
     `ct_native`.

**Assets: imported once from the user's ROM, then loaded by name.** The
ports reach ROM data through named accessors (`*_data.h`), all but
`battle/scripts.c`'s one read, which goes first. The accessors still read a
mapped 4 MiB ROM image by address, which a GBA cannot map (target 2), and
which keeps the ROM's layout in every build. The plan:

1. **One catalogue of resources.** Every resource the accessors read gets a
   row: its name, its type (MD2's types, MATCHING.md), where it lies in the
   ROM and its codec. It is built from the typed regions the matching build
   already extracts. The importer, the accessors and a check that every
   accessor read lands inside a catalogued resource all come from it.
2. **An import, once.** On first launch, or with `ct_play --import <rom>`,
   the importer checks the ROM's CRC32 and writes every resource into a
   versioned asset pack in the user data directory beside the saves. Later
   runs load the pack and never need the ROM. The same importer runs on a PC
   to build the image for a target that can't afford the ROM or the decoding
   (GBA, Wii, GameCube). Nothing it writes enters the repository: the pack is
   ROM-derived (CLAUDE.md).
3. **Loaded by name, through the driver.** The game asks an asset store for a
   resource by name and type; the driver supplies the store (a file, a
   linked-in image, a browser's storage), as it does saves. Nothing in
   `game/` opens a file or knows a path.
4. **Raw first.** The pack holds each resource's original bytes, and the
   accessors read the pack instead of ROM addresses. No port changes, and
   shadow mode still compares bytes, so the gate proves the move. This is the
   step a build with no emulator needs ("The native program", above): the
   mapped ROM image leaves with it.
5. **Decoded next, a resource at a time.** The pack holds the structured
   form: streams decompressed, tiles in a neutral format, palettes as
   colours, text as UTF-8, scripts readable (MATCHING.md's codecs and the
   text and event-script decodings are the start). Each port stops calling
   the original's decoder for that resource, a reviewed change per resource,
   in step with its subsystem retiring the gate. Widescreen, translations and
   replaceable assets come from here; the readable layout as a whole is
   Phase 5's MD3.

Steps 1-4 belong to this phase and can start now: the seam exists, and they
change no port.

## Phase 4b: the platform milestones

Serving widescreen and the GPU backends rather than "no emulation", so they
wait behind 4a.

- **P4 widescreen**, and with it the answer to whether `VidFrame` holds up:
  PLATFORM.md's open questions name the assumptions it would expose.
- **Host backends beyond SDL** (TODO.md, "Platform"): rendering on the GPU,
  controllers.

## Phase 5: the long tail

- **MD3**, and the decoded half of the assets it enables (Phase 4a,
  "Assets", step 5): a layout meant to be read rather than reassembled, and
  the scripts as text. **MD3 is the prerequisite for
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

- **M4** (generator speed). The generated C is a means; making it faster helps
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
- **Anything that makes the hybrid modes nicer to live in.** `--generated on` and
  shadow mode are how the work is checked, not what is being built.

The host seam was never deferred, because it was the same work as making the
desktop build shippable: the player advances a frame per call and persists
bytes under names (PLAYING.md, "How the player is built"), so a new frontend
needs a driver of its own rather than a fork of the player. That is what makes
targets 1 and 2 a matter of writing drivers once `game/` stands alone.

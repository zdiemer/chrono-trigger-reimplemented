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
| ROM code reimplemented (`burndown.py`) | 37% left | 0% |
| Ports that read as source (`portstyle.py`) | 0.02 ops/line | ~0 |
| Ports off the machine (`portstyle.py`) | 28% of functions take a `Snes *` | entry wrappers only |
| ROM data behind named accessors | every read but one (`battle/scripts.c`); the accessors still address a mapped ROM image | backed by extracted data |
| SPC700 sound driver | reimplemented in C and what `ct_play` plays, its commands calls rather than uploads (SOUND.md); the voices play through the emulated S-DSP by default, or through the native mixer, which makes every frame the S-DSP makes (PLATFORM.md); the original stays as the gate's reference | the mixer the default, and the original deleted |
| Video fed by service calls | decoded from PPU registers | `vid_*` direct |

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
S-DSP's every frame; what is left is making it the default. The mixer is the
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

## The validation gate

Nothing lands without it, and it is the same gate for every phase:

- every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines,
  no `page refused` lines (a guard that turned away a call on a direct
  page the map declares no instance for) and no `accessor out of range`
  lines (a port that reached past a variable the map declares, into
  whatever follows it in work RAM; both in DISASSEMBLY.md, "The accessors");
- `--native on --recomp only` traces identically to `--native off`;
- every hand port's estimated time stays within the timing gate's limits over
  the shadow runs, or is on its exceptions list with a reason
  (`tools/timing_gate.py`; RECOMP.md, "The timing gate");
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
  (`tools/dontcare_check.py`; RECOMP.md, "Before the bulk");
- no port transliterates the assembly where one didn't before
  (`tools/portstyle.py`, a ratchet against its baseline);
- no port's logic reads what some port writes only as scaffolding where
  none did before (`tools/effects_check.py`, a ratchet against its baseline;
  PORTING.md, "Scaffolding");
- no port carries one of the review rubric's mechanical smells where one
  didn't before (`tools/portsmell.py`, a ratchet against its baseline);
- `--misses` stays empty;
- `python3 -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

## Phase 3a: what is left of the transcription backlog

The rewrite of the ports written before the reimplementation bar is done:
scene, event, field, menu, battle, core and sound are all at the bar and
merged (docs/history/recomp-phase3a.md has each subsystem's account).
**1 of 191 files, 117 of 53,308 lines, is left on the backlog**: `data/decompress.c`,
the one line in `tools/portstyle_baseline.txt`. What is still on the machine
is, outside core, the entries' wrappers, the guards and the bridges round the
call-outs to the original's code:

| Subsystem | Lines | Style | On the machine |
|---|--:|--:|--:|
| battle | 17,302 | 0.02 | 27% |
| menu | 10,035 | 0.03 | 33% |
| field | 7,983 | 0.01 | 22% |
| event | 3,270 | 0.01 | 12% |
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
  `game/wram.h`, and every such use (140 in 33 files) is a variable waiting for
  a name in the map. Cells on a page the caller chooses have page kinds
  (DISASSEMBLY.md, "The accessors").

## Phase 3: M5 in bulk

Phase 3a has brought every existing port but one to the bar, so a conversion
here only has to hold them there, which the gate's ratchets do.

Where it stands, from `python3 tools/burndown.py` (an estimate; the tool's
docstring says which way it is wrong). **35% of the code is still generated.**

| Subsystem | Code | Ported | of it inlined | Left | Entries |
|---|--:|--:|--:|--:|--:|
| battle (`$C1`, `$CE`) | 64.9K | 57.0K | 20.3K | 12% | 383 |
| field engine, events (`$C0`) | 59.4K | 46.5K | 10.4K | 22% | 300 |
| menus, text (`$C2`) | 59.3K | 28.1K | 8.1K | 53% | 300 |
| scene program, RAM (`$7E`) | 24.9K | 1.9K | 0.0K | 92% | 54 |
| battle frame work (`$CD`) | 15.5K | 4.7K | 2.8K | 70% | 27 |
| field graphics (`$FD`) | 15.3K | 11.3K | 0.6K | 26% | 79 |
| battle sprites (`$CF`) | 6.8K | 6.4K | 0.4K | 6% | 44 |
| battle NMI, graphics (`$D1`) | 5.7K | 4.8K | 0.0K | 17% | 52 |
| battle setup (`$CC`) | 4.9K | 4.6K | 0.3K | 6% | 8 |
| scene engine (`$C3`) | 3.9K | 3.3K | 0.6K | 16% | 28 |
| scene data (`$C6`) | 3.5K | 3.4K | 2.3K | 4% | 25 |
| sound driver (`$C7`) | 2.6K | 2.6K | 0.0K | 0% | 43 |
| misc (`$FF`) | 1.7K | 0.1K | 0.0K | 95% | 1 |
| **total** | **268.5K** | **174.6K** | **45.8K** | **35%** | **1344** |

The three big banks are the work; the entries are concentrated in small hot
routines (and the 185 event command entries), which is why the shares are so high
despite the count. The burndown follows the jump tables the analysis has sized,
counts the registry's macro rows, and credits the callees a port inlines (no
call-out at their call; RECOMP.md, "The tooling pass after wave 1: the
analysis", has what each of those moved): helpers no longer need a registry
entry of their own to count. `$CC` is the
one subsystem near done. `$C3` (the scene engine) is done but for code the
beam's counters decide and one routine nothing reaches (its row in
docs/ports.tsv; the
edge sequence the field's rasterizer inlines is credited to it); `$C6` has its small object
routines and irises, and its Mode 7 map and perspective effects, table
builders and orbit routines are left, none of which a movie reaches.

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
  subsystem's RECOMP.md section. The gate proves behaviour and `portstyle.py`
  measures syntax, so this is the only check that sees whether the code reads
  as a hand reimplementation, which is the goal. Event scored 0.02 on
  portstyle and still read as a translation.
- **A wave ends with a review**: each agent reports where the workflow and the
  tools still hurt, and those answers decide whether the next thing is another
  wave or a tooling pass. Wave 1's review produced the tooling pass recorded in
  RECOMP.md, which cut the cost every later port pays.

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
accounts are in docs/history/recomp-waves.md and recomp-chunks.md, the latest
in RECOMP.md.

**`$C1`: the battle.** Nine chunks are in: the enemies' scripts, the
target search, the random numbers, an enemy's turn, the statuses' timers,
every action's effect, a party member's action carried out, the setup's party
helpers with the inventory and gold service (`$C1:8003`), the fight's start
(`$C1:FA8B`) with the `$FD` routines it calls, the rewards of a won
battle (`$C1:F93E`, `$C1:F205`, `$C1:B4E9`, `$FD:B201`: the experience, the
levels, the techs, the items put back), all of which the main loop runs as C,
and the animation script language (chunks 9a and 9b: the command table in C,
the flow, values, poses, drawing, effects and colors; the places and facings,
every move, the grid walk, the sound commands and the screen effect). **Next**,
by `python3 tools/burndown.py --uncovered C1` (7% of the bank left), the enemy
effects (`$C1:3772`, 2,278 bytes), the bank's largest run by far; the rest
are runs of a few hundred bytes. What is
left of the battle's routines in bank `$FD` (`$FD:A8A5`-`$B7EB`) goes with
them: a member's turn (`$FD:AB30`), the counterattacks (`$FD:AC6E`) and
`$FD:A8CE`; the rewards' messages (`$FD:AD17`) wait for the battle messages'
port, since each waits for the player (TODO.md).

**`$C2`: the menus.** In: the Epoch's two Mode 7 screens, the object scripts'
instruction set and the map they scroll, the load screen, the window command
language, the cursor and sounds, the typesetter, the menus' opening, the
characters in the records, the records' task, the frame's input, the
tilemap touches, the equipment screen (its steps with the member list and
card, and its two windows: the slots and the list of what fits, sliding in
and out), and the items screen with its panel (the list of the party's items,
moving and sorting them, who can equip one, an item used on the members).
Next is the techs screen (`$C2:B353`-`$C2:BF34`, with the largest run left,
`$C2:B520`, 1530 bytes), which shares the items screen's member window and
the lists' slide; then `$C2:42F8` (1441), `$C2:2826` (1313), `$C2:D52B`
(1290, the shop) and `$C2:3B8C` (1116). The equipment screen's preview of an
item's figures (`$C2:A5C3`, `$C2:A5E7`, 138 bytes) is still the original's:
the flags it saves come after the list's move returns, which leaves them as
none of its paths work out yet (TODO.md). `$C2:27DE` goes with
`menu/display.c`'s loader half, and only once the streams it decompresses are
typed: it is the object mode's screen setup, about fifteen routines that each
load one stream named by a byte of the scene record at `$C6:FD00`, and without
knowing what each stream holds their ports would be named after their
destination addresses.

**`$C0`: the field and events**, 22% left after fourteen chunks, and all of
it routines the ports call out to: the event commands at `$C0:4186` (the
party's characters' objects, `$57`-`$83`) and `$C0:3711` (items, gold and
the party, `$C7`-`$D7`), the field's setup after a menu
and a map (`$C0:01A5`, `$C0:038F`, `$C0:0000`), the keeping of the field
(`$C0:011B`), the fade in (`$C0:2824`), the sprite lists (`$C0:B192`), the
frame work (`$C0:00BF`) and a choice's buttons (`$C0:1ADF`). Event's bridges
still call the menus' party routines (`$C2:8004`), which go with `$C2`.

**What `$FD` has left** after wave 1 (docs/history/recomp-waves.md, "Phase 3,
wave 1: bank `$FD`"): the time-gate tunnel scenes event `$FF` starts with
`$DF` = `$80`-`$83` (`$FD:DA00`-`$DD95`, their interrupt handlers
`$FD:DE15`/`$DE87` and helpers `$FD:EAA6`-`$F2xx`, 2.6 KiB), which draw through
the PPU directly and want the video service; and the location's tile animation
loader `$FD:DE98`, blocked on the work RAM port writes through `$217F`
(TODO.md, "P1 leftovers"). The battle's routines in the bank go with `$C1`
(above).

RECOMP.md's "What drafts don't do yet" lists where the draft tool still falls
short, starting with D and DB where only interrupts reach.

## Phase 4a: what still runs on emulation

Three things stand between a finished `game/` and a native build with nothing
under it. None of them is optional for target 1, and none was scheduled before
the audit that found them.

- **The native mixer the default: next.** The sound driver is C and plays by
  default (`game/sound/`, SOUND.md "The port as built" and "Playing it"): the
  game's commands are calls, the driver places the songs' samples itself, and
  saved states hold its fields. Its voices' second implementation, the native
  mixer (PLATFORM.md, "The native mixer, as built"), makes every frame the
  S-DSP makes over every movie and the fuzzer, and `dsp_test` and the gate's
  sound fuzz hold it there; `ct_play --mixer native` plays it. What is left is
  to make it `ct_play`'s default and the route check's (`ct_drive --mixer
  native`), which takes the S-DSP out of the product as it ships: states saved
  through the S-DSP then load with `--mixer dsp`. `runtime/apu/dsp.c` stays as
  the mixer's reference while the mixer is checked against it. The original
  driver stays as the driver's reference until then (the gate, the movies and
  the lockstep run it, and `ct_play --sound-driver original` plays it); it
  goes, with the CPU half's protocol (`game/core/sound_driver.c`'s uploads and
  waits), the SPC700 core and APU-direct, when the gate retires for sound.
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

**The asset seam's other half comes forward to here.** The ports reach ROM
data through named accessors (`*_data.h`) now, all but `battle/scripts.c`'s
one read, which goes first. But the accessors themselves still read a mapped
4 MiB ROM image by address, which a GBA cannot map at all (target 2) and which
makes "load from a user's own ROM into a data directory" impossible (TODO.md,
"Platform"). So what is left is their backing: each resource extracted from
the user's ROM once and loaded, behind the same accessors.
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

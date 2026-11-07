# Reimplementation

The game becomes hand-written C a routine at a time. A hand port takes
precedence over the code generated from the ROM ([GENERATED.md](GENERATED.md)),
which runs whatever nobody has ported yet; shadow mode checks every port
against the original as it runs, and the gate (`tools/suite.py`, whose
checks CLAUDE.md lists) holds the ports to the original's behaviour and
bounds their time. The rules a port is written to are CLAUDE.md's "Writing a port from
generated code"; the workflow is [PORTING.md](PORTING.md); what to port next,
and why, is [PLAN.md](PLAN.md).

## What is here, and what moved to docs/history/

This file holds the **mechanisms** of the hand ports: how their time is
estimated, what that costs and where their video writes land, the timing gate,
how a draft is made (M5), what the checks in the gate are for, and the newest
as-built section of each porting stream as the shape a new one follows. It is
meant to be read.

The per-chunk and per-subsystem record -- the account of each wave, each
subsystem's move off the machine and each chunk of routines as it became C --
is history rather than contract, and it grew past what anyone reads. It lives
beside this file:

| File | What it records |
|---|---|
| [history/reimplementation-waves.md](history/reimplementation-waves.md) | the `$C7` pilot and Phase 3's waves 1 and 2, bank by bank |
| [history/reimplementation-phase3a.md](history/reimplementation-phase3a.md) | Phase 3a: each subsystem taken off the machine |
| [history/reimplementation-chunks.md](history/reimplementation-chunks.md) | Phase 3's per-chunk sections for the menus, the field, the battle and the scene program, and Phase 4a's timing chunks |

A new as-built section goes in **this** file, under the phase it belongs to;
when the next chunk of the same stream lands, the one before it moves to its
history file in the same commit. What stays here is one worked example per
stream, not the whole run of them.

## The menus' machine joints

The screen bridges continue into their next screen step as C: opening and
closing screens, pointing at equipment, returning to the main menu, fades,
sounds and saving in a slot. The vblank upload continues into the queued
uploads; the NMI entry retains its comparison checkpoint. Guarded
continuations retain the original stop when a callee refuses. These are the
gate's bridges; the native program already uses the screens' game-state
sequences and task coroutines.

The scripts call map scrolling, drawing and the sound mailbox as game-state
functions, without making an emulated return frame. A table selects 55 other
registered wrappers as C behind their guards. Unknown or refused targets
still use the near/far fallback. The two curtain routines retain their entry
boundaries: inlining them attributes the scene RNG's accepted shared-scratch
reads to the interpreter, outside their existing effects evidence. Retiring
that dependency belongs with the scene engine. No exception was added.

Across the 21 assigned bridge, display, frame, list, object, task and
description files, the machine-joint count is 560 to 500: returns 157 to 157,
stops 56 to 22, counted call-outs 18 to 6, machine time tags 15 to zero,
register escapes two to two, video calls 58 to 58, scaffolding 254 to 255.
The shared map helpers add no joint; the member-card wrapper adds one
scaffolding result outside those 21 files. The register results exposed by
joining the screen steps were the card's final queued upload and the last
task stopped before the naming fade. Both are confined to machine wrappers.

Narrowed shadow checks cover the menu sweep, buying and equipping, New
Game+, save points, field menus, End of Time and the attract sequence.
The script chunk's differential fuzz compared 1,832 states over fifteen
cases; the continuation chunk compares 1,418 over eleven cases, all with
zero mismatches before and after (seed 1, existing constraints). Fuzz has
no active registry, so guarded continuations use their fallback there;
the movies exercise their C paths. The original card and naming failures
were fixed without widening don't-care sets. Per-entry counts and the
limits of each check are recorded in `ports.tsv`.

The once-only sampled gate, before restoring the NMI checkpoint, has zero
mismatches over 37 shadow movies. Trace identity passes for 7,200 frames;
timing covers 1,812 entries and 163,540,413 stretches within the existing
limits, and vblank has no unexcepted finding. All 56 routes reach their
outcomes in shipping mode and with scaffolding compiled out; that build
also completes all 37 movies with no excepted failure. Every other gate
check, including evidence regeneration, passes, but its last line is
`FAILED: uncompared`: joining the NMI work matches its outer entry while
abandoning all 51,429 nested `menu_nmi_work` comparisons. Its checkpoint is
therefore retained, without adding an exception or changing the shared
comparison tools. The full sampled gate is not repeated. There are no
calibration or exception changes. The Python unit suite reports OK after
467 tests, with ten skipped.

After retaining the checkpoint, `menu`, `menus-sweep` and `menus-epoch`
have zero mismatches: `menu_nmi_enter` and `menu_nmi_work` each compare and
match all 8,358 calls, none interrupted or abandoned. Trace identity and
timing pass on these follow-up runs. The narrowed suite ends `suite OK`,
including the comparison ratchet, effects, port style and port smell checks;
both fast routes pass in shipping mode and with scaffolding compiled out.
This follow-up does not repeat the long routes or evidence regeneration.

Review restored the callee guard on the save-point screen's continuation
from `$C2:D506` to `$C2:CF0A`. A movable cursor does not imply that the
shown save slot is valid. `menu_go_on_in` now retains the callee's refusal
and the original stop's live high accumulator byte. The new invalid-slot
fuzz variant presses choose with slots 4 through 255: seed 1 compares all
2,000 states after the fix, with zero mismatches, skips or out-of-range
accesses. Before it, only 140 compare and 1,860 skip; a diagnostic run of
100 states reports 100 out-of-range slot reads. Shadow runs of
`menus-save-point`, `menus-field-menu` and `menus-sweep` have zero mismatches.
No fuzz domain was narrowed, and no sampled gate was run for this review.

The evidence helper now runs each movie's variants sequentially, so its
worker count also caps all child processes, including trace comparisons.
The concurrent regression test observes eight children with the previous
launcher and at most two with two workers after the fix; all four variants'
results, including failures and evidence counts, remain accounted for.

The merged gate exposed the capsule iris's reads of the circle renderer's
scaffolding under the newly joined script caller: the radius at
`$7E:0354-0355` and center line at `$7E:0352-0353`. The capsule now takes
its center and radius from the menu object's real state, keeping the radius
in a local through the copy loop; its return-position calculation uses the
same object state. The circle's comparison-only writes remain scaffolding.
Both capsule entries compare and match 1,938 states each before and after
(seed 1, 2,000 iterations each, 62 unchanged guard refusals each). No guard,
fuzz domain, calibration or effects exception was changed.
The `events-ocean-palace-lavos` shadow run matches all 235 and 152 capsule
calls, and 33,394 script calls with four interrupted. This worktree's
power-on shipping replay does not reach the capsules, unlike the reported
merged-tree run; the shadow and differential fuzz checks cover the changed
path. No full sampled gate was run for this fix.
The requested one-movie suite ends `suite OK`, including vblank, effects,
portstyle and portsmell; both fast routes also pass with scaffolding
compiled out.

Remaining stops belong to task switches and held waits, reset and mode
boundaries, invalid script dispatch, comparison checkpoints and guard
fallbacks. In particular the menu-entry boundary still owns nested shadow
checkpoints, as documented
beside `menu_mode_enter`. The two WRAM transfers need a memory service that
preserves the gate's transfer effects. Registered return wrappers cannot
leave until their original callers and registry entries retire together.

## Time in hand ports

Generated code keeps the original's time to the instruction
([GENERATED.md](GENERATED.md), "Exact timing (M2)"); a hand port can't. These
sections say how a port's time is charged instead, why it isn't made exact,
where its video writes land as a result, and how the timing gate bounds the
error of its estimate.

### Hand ports in `on` mode

Hand ports still charge estimated time (`cost_milli` times their bus clocks;
since the timing gate, DMA's time as it is plus `cost_milli` times the rest)
after they stop, a scanline at a time (interrupts were held until it was paid;
since the timing gate they are taken during it), and sync explicitly where they
talk to the APU ([PORTING.md](PORTING.md)). The measurements below predate
both. Their time can't be made
exact without running the original, and any error moves everything after it:
registering the hand ports of one subsystem at a time (with all generated
blocks), `new-game` departs from the interpreter-only run for every subsystem
whose ports it reaches, most within frames 22-40 (`memory` and `menu` at 1750),
so no movie stays identical with hand ports.

How close `on` mode gets, as the share of frames whose framebuffer is the
interpreter-only run's at the same frame ("same"), and whose framebuffer
appears in that run within 120 frames ("near", a looser measure of being in
the same scene), with hand ports and generated blocks:

| Movie | M1: same / near | M2: same / near |
|---|---|---|
| `new-game` | 70.5% / 99.9% | 70.5% / 99.9% |
| `menu` | 74.0% / 99.5% | 74.1% / 99.6% |
| `field` | 75.8% / 99.3% | 75.8% / 99.4% |
| `fair` | 71.8% / 94.7% | 71.8% / 94.2% |
| `fair-companion` | 65.8% / 89.2% | 74.8% / 93.2% |
| `gato-battle` | 56.9% / 77.0% | 64.7% / 81.1% |
| `saves/battle-magus` | 43.9% / 45.1% | 27.6% / 36.1% |
| `saves/battle-golem-twins-gameover` | 36.6% / 53.5% | 41.7% / 50.2% |
| `saves/battle-tyrano-lair-walls` | 43.5% / 47.2% | 54.0% / 58.3% |
| `saves/late-omen-battles` | 29.0% / 34.0% | 34.7% / 37.4% |
| `saves/late-omen-terramutant` | 22.3% / 27.0% | 22.3% / 27.0% |
| `saves/late-omen-zeal` | 9.4% / 11.0% | 9.4% / 11.0% |
| `saves/late-lavos` | 6.4% / 9.0% | 7.3% / 9.9% |
| `saves/events-jetbike-race` | 52.1% / 66.7% | 62.7% / 68.1% |
| `saves/events-end-of-time-gate` | 62.6% / 91.2% | 68.9% / 91.2% |
| `saves/events-prison-escape` | 57.7% / 95.0% | 57.8% / 95.1% |
| `saves/events-keepers-dome` | 73.0% / 87.6% | 73.0% / 87.6% |
| `saves/events-blackbird` | 96.3% / 98.9% | 96.3% / 98.9% |
| `saves/menus-field-menu` | 92.0% / 99.7% | 92.0% / 99.5% |
| `saves/menus-shop-inn` | 90.9% / 95.9% | 90.9% / 96.0% |
| `saves/menus-epoch` | 69.5% / 88.2% | 70.3% / 89.2% |
| `saves/menus-end-of-time` | 66.8% / 79.8% | 62.9% / 78.8% |
| all frames | 44.4% / 56.3% | 45.8% / 56.9% |

Exact blocks help a little overall, and most in movies with battles and
races, where the M1 blocks' late interrupts added to the drift; `battle-magus`
and `menus-end-of-time` went the other way, which is the same drift landing
differently. The late battles still go differently from the interpreter-only
run: in `late-lavos` the party ends up in the tech menu against the outer shell
from about frame 25000 to the end in both M1 and M2 (the movie's input no
longer fits the fight; the menu keeps moving, the game doesn't hang), while
`--native off`, and `--native on --generated only`, reach Lavos's core. Two ways of
giving hand ports more exact time were tried on six movies (`new-game`, `field`,
`gato-battle`, `late-omen-battles`, `late-omen-terramutant`,
`events-prison-escape`) and not kept:

- **Sync before time-observing I/O**, as generated blocks do: every hand port
  access to the APU ports, `$2137-$213F` and `$4210-$4212` (and in a second
  variant also PPU register and `$4200-$420C` writes) first let the time the
  routine had taken so far pass (`native_sync`). Same-frame agreement changed by
  at most 0.1 point, except `late-omen-battles` with the second variant (34.7%
  to 31.8%). The sound driver ports, which already sync, are nearly all the
  time-observing I/O hand ports do.
- **No idle entries in `on` mode**, so the busy-wait loops run as exact
  generated blocks instead of skipping ahead: worse (`field` 75.8% to 49.9%,
  `gato-battle` 64.7% to 53.0%, `new-game` 70.5% to 57.9%).

So `--generated only` is the configuration for checking `on` mode against the
interpreter; hand ports are checked by shadow mode as before. Whether a hand
port could carry exact time instead is the spike below; it could, only for a
class of calls, and the decision it led to is to ship the readable code with
estimated time and measure the estimates instead.

### Can a hand port be exact? (the timing spike)

Phase 2 of [PLAN.md](PLAN.md) opened with a question: can a hand port carry
its code's exact time, so that `--native on --generated on` traces identically to
`--native off` and the readable code, rather than the generated blocks, is what
the SNES build runs? The answer was **partly**: the mechanism exists and was
demonstrated, but only for a class of calls, and the class excludes what the
porting rules themselves ask ports to be. The decision it led to is at the end
of this section: timing *differences* from the original are accepted, the
readable code ships, and timing *errors* become a measured, gated quantity.
What was kept is the measurement, off by default:

| Path | What |
|---|---|
| `runtime/port/native.c` | Shadow mode's timing counters for every hand-port entry; `PORT_TIMING_ORACLE` in `on` mode |
| `runtime/port/native.h` | The counters (`NativeStats.t_*`), `native_set_port_timing`, `native_set_port_horizon`, `native_set_timing_report` |
| `runtime/snes/system.c` | `snes_interrupt_horizon`, `snes_span_bound` (the oracle's interrupt horizon) |
| `dev/ct_dev.c` | `--include PREFIX`, `--timing-report`, `--port-timing estimate\|oracle`, `--port-horizon strict\|interrupts`; later `--page-report` ("Page kinds", below) |

`--timing-report` adds per hand-port entry, in shadow mode, over the stretches
(calls, or parts of calls between call-outs) no interrupt fell in: how many,
how many the `cost_milli` estimate got exactly right, its mean and largest
absolute error in master clocks, the mean original time, the `cost_milli` the
measured totals imply ("calibrated"), how many fit the two horizons below, and
the distinct values of the time a port can't see. Shadow mode's original runs
on the interpreter, so these are the same in every run whatever the estimates
are: they are what the timing gate (PLAN.md) is built on. The self-timing
prototype described below was removed from the tree when the decision was
made; it is in the history of the `timing-spike` branch.

**What exactness needs.** Three things, each checked:

1. *The time itself.* A port's bus clocks are the original's data accesses
   only. What it can't see is the time of the instruction fetches and internal
   cycles along the path the original took: branches, loop counts, an indexed
   read whose index carries into the next page, a direct page off a page
   boundary. `--timing-report` in shadow mode records, per port, the distinct
   values of *original time minus the port's bus clocks* (the time the port
   can't see). Over the 26 movies (113.5 million stretches of hand-port code in
   415 reached entries): 167 entries always had one value (16.4% of hand-port
   time), 108 had 2-4 (9.4%), 39 had 5-8 (8.5%) and 100 more than 8 (65.7%).
   The `cost_milli` estimate is exactly right on 7.4% of stretches and off by
   125 master clocks on average.
2. *Where things land.* Even an exact total moves anything the system does
   between two of the original's instructions to the end of the call. What
   that covers decides how many calls can run at all. Against M2's horizon
   (the next refresh, HDMA point or line end) only 51.5% of stretches, and 6.5%
   of hand-port time, fit: the longest gap is 825 master clocks, a few
   dozen instructions. But the refresh, HDMA points and ordinary line ends
   don't observe a routine that does no time-dependent I/O: advancing across
   them in one step does what advancing across them instruction by instruction
   does, as long as the call's span in master clocks, pauses included, is
   bounded (`snes_span_bound`: the refresh and each enabled HDMA channel's
   worst case per line). What can't be absorbed is an interrupt the CPU would
   recognize inside the call and a frame boundary (the trace and the renderer
   see the state there). Against that horizon (`snes_interrupt_horizon`)
   99.4% of stretches and 88.2% of the time fit.
3. *I/O.* A call whose I/O depends on or changes the time (anything in the
   synced area of "I/O" above: the PPU, the APU ports, the counters, `$4200`
   writes) must happen at the original's instruction time, which a port that
   runs as a unit can't give it. The WRAM port and the multiplier and divider
   are exempt, as for blocks.

The stack bytes and registers an interrupt right after the call pushes were a
concern (ports may leave don't-care registers different): over every call the
oracle below ran, no port left any register different from the original's.

**Measured drift** (`--native on --generated on` against `--native off`, first
divergent frame, `new-game` unless noted). All hand ports: frame 1. By
subsystem (`--include PREFIX`): `field_` 1 (`field_fill_memory` alone), `sound_`
27, `decompress` 27, `display_` 28, `event_` 29, `play_` 34, `palette_` and
`input_` 35, `tile_` 40, `scene_` 82, `attract_` 142, `menu_` 1711, `memory_`
1753, `battle_` not reached. Single ports: `input_map_buttons` 36,
`scene_multiply` 140, `field_update_screen_position` 5839,
`field_link_object_row` 5840. `scene_multiply`'s estimate equals the original's
time on every one of its 3870 calls; it still drifts, because 8 of those calls
contain an interrupt, which the estimate path takes after the whole debt is
paid. The 14 ports of the self-timed set below, with their estimates: frame 35
or 36 in all 23 movies tried.

**Designs.** (a) *Take the time from the generated block*, running it on a copy
of the machine; (c) *run a block only for its timing and interrupts*: both
need the block to run the whole call, reads and writes, to follow its path, so
the port recomputes what the block already computed; the port would execute,
but it would not be what decides anything. (a) is kept as an oracle, to test
the rest. (b) *The port counts its own time*: the port adds the time it can't
see with `m_spend` along its path, and an entry's `time_extra` holds a
constant part (both removed with the prototype). That is the only design in
which the port is what runs, and the one prototyped.

**Mechanism** (`on` mode, per hand-port call). The machine is copied
(incrementally, as shadow mode does); the port runs on the machine and gives
its time; if it did no time-dependent I/O (an I/O observer counted it), didn't
call out or sync, and its span fits the horizon, the step returns that time
with the return's last cycle (an internal cycle for `RTS`, the last pull for
`RTL`). Otherwise the machine is put back from the copy and the generated
block at the same address runs, exact by M2. In the self-timing prototype
(`--port-timing self`, removed) the port gave its own time, and shadow mode
checked it against the original's on every stretch. The oracle
(`--port-timing oracle`, kept) takes the time from the generated code instead,
run on a second copy to where the port stopped, and compares the port's end
state with it.

**Results.**

- *Oracle, all hand ports.* Strict horizon, `new-game`: identical, 17.6% of
  calls run as the port. Interrupt horizon: identical in all 23 movies tried
  (every movie but the three endings: 328808 frames, 23.6 million calls):
  67.5% of calls and about 59% of hand-port time run as the port, with no register or state
  difference from the timed original. The rest fell back: calls that call out
  (24.1% of calls, 20.5% of time; the prototype times whole calls only,
  though a stretch between call-outs is as atomic as a whole call),
  time-dependent I/O (7.1%, 11.1%), no room before an interrupt or frame
  boundary (1.1%, 9.1%: long routines such as the decompressor), and code with
  no generated block (0.3%). A first version compared CPU time with a
  master-clock horizon and diverged in `new-game` at frame 2412, in menu text:
  the refresh and HDMA pauses inside a 25000-clock glyph call carried it past
  a frame boundary; `snes_span_bound` is that fix.
- *Self-timed, 14 ports* (interrupt horizon, `--port-timing self`; tried,
  worked, not adopted): 11 whose
  hidden time was one constant in every movie (`time_extra`:
  `input_map_buttons`, `scene_multiply`, `field_copy_sprite_tile_flipped`,
  `scene_clear_sprite_table`, `menu_reset_sprite_nodes`,
  `menu_hide_all_sprites`, `battle_clear_oam_buffer`,
  `battle_reset_0b40_table`, `battle_multiply_8x8`, `battle_multiply_8x16`,
  `battle_update_scroll_tables`) and three with per-path costs written from
  the listing (`field_link_object_row` 3 paths, `field_unlink_object_row` 4,
  `field_update_screen_position` 1 branch per axis, and the page-crossing
  cycles of their indexed reads). All 26 movies (762693 frames) trace
  identically to `--native off`, and 96.7% of the 5.4 million calls ran as the
  port. The first set had a fifteenth, `menu_reset_sprites`, which looked
  constant over six movies and isn't: shadow's check flagged it in two menu
  movies (`time original 15860 port 16202`), and self mode diverged in exactly
  those two (frames 3623 and 3654); without it they are identical too (the
  other 24 movies were run with it, where its time did match).
  `scene_clear_sprite_table` always falls back: its time is exact, but it
  programs a DMA.
- *Cost.* `gato-battle` without a trace, CPU seconds: `--generated only` 7.2, the
  14 self-timed ports 8.0, the oracle 31.0, estimated hand ports 5.3,
  interpreter 9.5. The prototype's copy per call is what costs; a write
  journal would be cheaper.

**The answer, and where the line is.** A hand port can be exact if its call
(1) carries the original's hidden time for the path it took, (2) does no
time-dependent I/O, and (3) contains no interrupt recognition or frame
boundary; the generated block has to run every call that fails a test, so the
blocks ship regardless. That line excludes, by construction:

- *Ports behind the platform services.* The rules ([CLAUDE.md](../CLAUDE.md))
  tell a port to keep SNES semantics and drop SNES mechanics, "write order,
  the timing of a write". An exact SNES time for I/O is exactly those
  mechanics, so a port written to the rules is exact only by falling back.
  That is 11% of hand-port time now, and the display and sound code Phase 3
  converts is mostly this.
- *Routines longer than the gap between interrupts* (9%), and calls an
  interrupt happens to fall in, whatever their length.
- *The time itself, for anything with more than a few paths.* The hidden time
  depends on the original's control flow instruction by instruction: which
  branches were taken, how often each loop ran, which indexed reads crossed a
  page, whether D was page aligned. A port that shares one helper between two
  copies of the original's code (`field_move_object`'s two axes: the first
  ends in `BRA`, the second in `RTS`), folds a loop, or uses a service in
  place of a register sequence has to pay each of those back by hand, which
  is carrying the draft's shape in comments that happen to be numbers. Two
  thirds of hand-port time is in entries with more than eight hidden-time
  values. Shadow mode checks the annotation on every call, but only on the
  paths the movies reach, and a "constant" is a hypothesis (as
  `menu_reset_sprites` showed).

So readable code can run exactly where it is small, computational and short:
counted by call, most of today's port calls are that; counted by where the
game's time goes, and by the code Phase 3 will write, most are not.

**The decision** (the user's). Exactness was the
wrong requirement. A timing *difference* from the original -- a battle that
plays out differently -- is acceptable; a timing *error* that shows, such as a
video write landing outside vblank where the original's was inside it, is not.
So:

- The readable code ships: `--generated on`, hand ports with generated code
  wherever no port exists, becomes the player's default, but only once the
  timing gate exists (PLAN.md).
- `--generated only` stays as the exact mode and the gate's reference; the movie
  library stays the regression suite on it, since movies desync under
  `--generated on` and can't test it end to end.
- Timing errors are measured and gated: each port's estimate error per call
  from shadow mode's counters above, which don't drift; a threshold; the worst
  offenders fixed case by case; `cost_milli` recalibrated from the measured
  totals; and a check for video writes outside vblank.
- Ports don't carry cycle costs. The self-timing prototype is not adopted,
  for the reasons above: it exacts per-path numbers the rules tell a port to
  leave behind, it excludes ports behind the platform services by design,
  and every call outside its line still needs the generated block. Its oracle
  and counters are kept as measurement.

### Where video writes land

Timing that differs from the original is acceptable in the shipping mode
(`--native on --generated on`); timing that damages the picture isn't. One class
of damage is a display update that must land in vblank or forced blank landing
on the picture instead, because a routine ran late or long: the battle's black
bands and missing window before the DMA timing fix (RUNTIME.md) were this.
`tools/vblank_check.py` looks for it. It is one piece of the timing gate, and a
row of `tools/suite.py`'s `CHECKS` (`--no-vblank` skips it).

**When a write lands.** `ct_dev --io-log` (PLATFORM.md, "I/O log") now also
counts each access by when it landed, as the PPU sees it rather than by a rule
of thumb: the time is the machine's line when the access happens, which is the
time `ppu.c` itself acts on (`sync_ppu_view`).

- *vblank*: lines 225-261 (`SNES_VBLANK_LINE` on). The PPU accepts VRAM and OAM
  data, RDNMI is set, and nothing is drawn.
- *forced blank*: lines 0-224 with `$2100` bit 7 set, as it was before the
  write. Data lands, and the line is drawn black.
- *visible*: lines 0-224 with the display on: exactly `Ppu.active_display`.
  `ppu.c` drops VRAM (`$2118-$2119`) and OAM (`$2104`) data written then, and
  any other register changes the picture from the next line drawn.

Line 0 is a picture line to the PPU (it locks VRAM, and the frame's HDMA setup
happens on it) though it isn't shown. The renderer draws each line whole at
the line hook (dot 276, where HDMA runs), so for picture-line writes the log
also keeps a histogram by the *first line the write can change*: its own line,
or the next if its own has been drawn. That separates a register write every
shown line sees (line 0 or 1, harmless) from one that splits the picture, and
it finds the write that ends forced blank with a nonzero brightness: the lines
above it were black. Hand ports run to completion at the time they start and
pay their estimated time afterwards, so their writes are timed where the PPU
sees them, at the start of the port or after its last sync or call-out;
generated blocks catch up to each I/O instruction (`rc_io_sync`); a DMA
transfer's bytes all land at the time of the `$420B` write, as `ppu.c` sees
them. HDMA writes are counted apart: they are meant for the picture. They are
told from the CPU's by the master clock of the line hook they follow, which
also stops HDMA writes made during a generated block's catch-up being counted
as the block's (the log did before).

**The comparison.** Run the same movie in the reference (`--native on
--generated only`, generated code timed exactly like the original) and in the
shipping mode. The two runs desync within a few dozen frames, so a
frame-by-frame diff means nothing; instead the check compares, per movie, what
each *routine* does with each *register group* (display, OAM, BG, VRAM, mode 7,
CGRAM, screens, HDMA enable): a finding is something the shipping mode does on
the picture that the same routine never does in the reference of the same
movie. That survives desync because it asks about a routine's behaviour, not a
frame's contents, and a routine the shipping run reaches that the reference
never ran is reported as `unmatched` rather than judged.

To be the same routine in both runs, a write is keyed by ROM address. A hand
port is its entry; its region of the ROM runs from the entry to its last
service call site (the log records both), and the reference's generated code
in that region is the port's. Other code is keyed by the generated block
containing the address a block keeps in PC (where it started, resumed or last
branched), mapped to its block through the build's generated C. The categories:

| Category | Fails | Meaning |
|---|---|---|
| `visible` | yes | writes with the display on, changing a shown line (or VRAM/OAM data, lost on any picture line), where the reference routine makes none |
| `unblank` | yes | the write ending forced blank on a line after the first shown one, on a line the reference routine never ends it on: black lines above it |
| `lines` | yes | a raster write (display on) landing on lines the reference routine never writes that group on, where it writes it on some |
| `late` | no | other writes in forced blank on a picture line where the reference makes none: the routine ran late, but the screen was blank anyway (the cost appears as `unblank` in whichever routine ends the blank) |
| `unmatched` | no | picture-line writes by a routine the reference never ran |

**Pass/fail.** The check exits 0 only when every failing finding is listed in
`tools/vblank_check_exceptions.txt` (`routine group category reason`), checked
in with reasons. It lists seven ("The timing gate", below, has the first four;
the $C7 pilot added three).

    python3 tools/vblank_check.py --ct-dev build/main/ct_dev -j 2
    python3 tools/vblank_check.py --no-run --failing-only     # re-read the logs in build/vblank

Both runs of all 26 movies take about 730 CPU seconds (6 minutes at `-j 2`;
`on` mode is fast). Logs go to `build/vblank`: they name ROM addresses.
`tests/test_vblank_check.py` covers the comparison on made-up logs.

**What it found** (the 26 movies, before the timing gate's changes). The
shipping mode damaged the picture on this axis in four ways, all of them the
hand ports' estimated time:

- **Late NMIs in battle.** `battle_nmi` (`$D1:E77C`) forces blank and turns
  HDMA off with the display on, from line 2 down: 255 times in the 11 movies with
  battles (the reference: never). The rest of that frame is black. The NMI is
  late because interrupts wait while a port's time is paid off (PORTING.md), the
  cause RUNTIME.md's timing history already named for `gato-battle`'s 11
  one-frame flickers. `field_nmi` (`$C0:EA63`) does the same on 32 frames of 8
  movies, with windows, color math, BG mode and HDMA changing mid-picture.
- **Black bands at the top.** The write that ends forced blank lands on lines
  2-150 instead of in vblank: `field_nmi` 1348 times (mostly line 6, in every
  field movie), `battle_frame_work` (`$CD:088D`, the brightness restore at
  `$CD:0925`) 1023 times, `scene_frame_work` (`$C3:08D0`) 367 times in the
  jetbike race, `battle_setup_display` (`$CD:09F1`) 53 times. The ports that ran
  late with the screen blanked (`late`, 36 findings) are mostly the uploads
  behind them: `battle_upload_vram` (to line 97), `battle_upload_alternating_vram`,
  `battle_load_graphics`, `field_nmi`'s windows and color math, and the jetbike
  race's map uploads.
- **The raster split one line early, every frame.** The line IRQs' forced blank
  under the playfield (`field_irq_after_hblank` `$C0:ECF2`,
  `battle_irq_after_hblank` `$D1:FEBF`) changes line 211 in the shipping mode
  and line 212 in the reference: 144857 and 305627 writes, one a frame, so the last line of
  the field and battle picture is black in nearly every frame (seen in
  screenshots of `gato-battle`: row 210 black in the shipping mode, drawn in the
  reference). The ports run when the idle entry's hblank wait ends (hclock
  1096), 8 clocks before the line is drawn; the original's instructions after
  its `$4212` poll take it past that point.
- **Late line IRQs.** `battle_irq` (`$D1:FE97`) and `battle_irq_after_hblank`
  land on lines 212-224 and above (8292 and about 7800 times): the blank under
  the battle window starts late, or a frame late.
- Smaller: the ending's scene program (`scene_program_restart` `$7E:302A`,
  `scene_show_layers` `$7E:5C98`) moves 4 raster writes each by 1-27 lines.

No port writes VRAM or OAM data with the display on: every data write the
shipping mode makes on a picture line is in forced blank. The 22 `unmatched`
findings are all forced-blank writes too, mostly menu loads (`menu_clear_vram`
and others) whose original does the work in code past the port's last call
site, where the region can't follow it.

The timing gate's changes (next) cleared all of these; four findings of the
same kind as the reference's own are accepted. The $C7 pilot's ports moved the
shipping mode's timing again and three more were accepted, each checked in
screenshots: two field-menu writes that land on frames the field's fade has
already made black (or all but), and the battle's forced blank under the window
on the lines the reference itself uses in the same fights.

### The timing gate

The timing decision (above) accepts timing *differences* in the
shipping mode and bounds timing *errors*. This is the bound: every hand port's
estimated time is measured against the original's over the movie library, on
every suite run, and each entry is held to limits tied to the budgets an error
could break. It is one check of `tools/suite.py`, beside the vblank check above
and the route runner, which joined it; `ct_play`'s default is the shipping mode
(`--native on --generated on`) since.

| Path | What |
|---|---|
| `tools/timing_gate.py` | Parses `--timing-report` output, sums it per entry over the movies, checks the limits, `--table`, `--calibrate` |
| `tools/timing_exceptions.txt` | Entries allowed over a limit, each with its reason |
| `tools/suite.py` | Runs the shadow movies with `--timing-report`; its `CHECKS` table runs the trace comparison and then the timing check |
| `runtime/port/native.c` | The report's exact totals, DMA share and error histograms; the estimate (DMA as is); paying debt with interrupts taken; the idle exit |
| `runtime/snes/bus.c` | `SnesBus.dma_clocks`: DMA's time, which the estimate takes as it is |
| `game/registry.c` | Every measured entry's `cost_milli`, calibrated |
| `tests/test_timing_gate.py` | Aggregation, limits, exceptions and calibration on made-up reports |

**What is measured.** Shadow mode's timing counters (the spike above) run on
every shadow run whether or not `--timing-report` is given; the switch only
prints them, a few lines per entry at exit, so the suite's runs carry it at no
cost. The report now also gives the exact totals (the original's time, the
port's bus clocks and their DMA part, the summed error), the error histogram by
bit length, and the same for the stretches within one vblank (below), with
the largest error there and on picture lines. The original runs on the
interpreter in shadow mode, so none of it moves with the estimates; only the
error does. `tools/timing_gate.py` sums the reports over the 26 movies, weighted
by stretch count: 414 entries, 113.4 million stretches.

**The estimate.** A port's time was `cost_milli`/1000 times all of its bus
clocks. A DMA transfer's time counts among them (the `$420B` write takes the
whole transfer), and DMA takes the same time in the port as in the original,
while the rest of the bus clocks stand for a few times their length in
instructions. One ratio can't fit both in a port that does some of each, so a
port's estimate is now its DMA time as it is plus `cost_milli`/1000 times the
rest (`estimate_clocks`), and the calibrated `cost_milli` leaves DMA out of both
sides. This is a better estimate, not a per-path cost: ports carry nothing new.

**Calibration.** `python3 tools/timing_gate.py build/suite/shadow-*.log
--calibrate game/registry.c` sets every measured entry to the value its totals
imply. The event command and field upload tables' macros take the value as an
argument, so each entry has its own. Over the library:

| | exact | mean \|error\| | share of port time | net drift | off by >= 1024 clocks | entries over drift |
|---|---|---|---|---|---|---|
| Before (hand-set `cost_milli`) | 7.4% | 78.4 clocks | 7.79% | +2.085% | 0.774% | 208 |
| Recalibrated | 8.7% | 41.2 clocks | 4.09% | +0.003% | 0.172% | 0 |
| DMA as is, recalibrated | 9.0% | 38.1 clocks | 3.79% | +0.001% | 0.104% | 0 |

(The spike's "125 clocks on average" was over a different run's counters; these
are from the totals.) What one scalar fixed is the systematic part: 208 entries
had drifted more than the limit below, some by far (`event_cmd_follow_party_member`
+135%, `battle_divide` +69%, `battle_nmi` +33%, `tile_animation_upload` -27%,
`field_vram_transfer` +288%), and all are now within 0.03%. Taking DMA as it is
fixed the ports that mix a transfer with code: `tile_animation_upload` from 31%
mean error to 3.1%, `field_upload_vram_queue` 33% to 8.9%, `battle_upload_vram`
4.1% to 0.7%, `menu_object_nmi` 4.4% to 0.7%, `field_fill_memory` 1.3% to 0.

What a scalar can't fix is the spread: entries whose original time follows
their data or their path (the spike's "more than eight hidden-time values")
keep a large error per call around a mean that is now right.
`field_update_objects` (28.7% of a 115-clock call), `field_nmi` (29%),
`menu_run_objects` (41%), `battle_draw_window` (33%), `battle_draw_menu_window`
(28%) and `field_build_sprite_table` (19%) are the largest; all but the six
exceptions below are within the limits anyway, because their errors are small
in clocks or fall outside vblank. None was split: no split of these reads
naturally as source (their variation is inside one loop or one helper, not
between two entry paths the game names), and none needed one to meet a budget.

**The limits** (`tools/timing_gate.py`), each from what an error of that kind
can break. An error moves the work after the port's call; with interrupts now
taken on time during a port's time (below), what it can move past is the end
of vblank, for work done in vblank, and the end of the frame's work, for the
rest.

| Limit | Bound | Why |
|---|---|---|
| `drift` | \|mean signed error\| <= 1364/357368 = 0.38% of the entry's time | A systematic error adds up over every call in a frame. At this bound, a frame spent entirely in the entry would end its work one scanline off. |
| `vblank-tail` | at most 1% of the stretches within one vblank off by >= 1024 clocks | In vblank the NMI handler does the frame's video work, and an error of about a line (the histogram's bound nearest one, 3/4 of a line) moves the handler's later writes a line nearer the picture. |
| `vblank-worst` | no stretch within one vblank off by more than 12617 clocks | A quarter of vblank (37 lines, 50468 clocks): a spike that takes a quarter of the handler's time. |
| `worst` | no other stretch off by more than 50468 clocks | The rest end on a picture line, where an error moves the main loop's work within its frame; interrupts come on time and raster writes are the vblank check's. What it can break is the frame: an error longer than vblank can push work that ended before the NMI past it, and the frame's update slips. |

Mean and worst are kept apart because they break different things: the mean
error (drift) accumulates and is what a scalar fixes, so its bound is tight and
relative; single spikes don't accumulate, so their bounds are absolute and sized
by the budget they could hit. Drift and `vblank-tail` need 100 stretches to
judge.

A stretch is a vblank one when the original began *and ended* it in the same
vblank (`end_segment`, `runtime/port/native.c`; the frame counter goes up as
vblank begins, so that is the same frame count and a vblank line at both ends).
At first it was one that began there, which judged the calls that begin in
vblank and run on into the picture -- a load, an object setup, the load screen's
frame, 80-120 lines long -- by vblank's budget, though their error moves only
work that comes after them on the picture, in the original as in the port;
`worst` judges them now. That retired 92 of the exceptions' 146 lines, most of
whose reasons said as much ("the calls are as long as vblank or longer, so the
original's own time already runs them into the picture"), and it is what the
world-map movies needed: `field_set_up_the_objects` (calls of 78 lines) passed
`MIN_STRETCHES` vblank stretches with them, and in `field-12kbc-1f5`, whose
save's names and figures draw shorter, every frame of the load screen
(`menu_show_save_slot`, `menu_choose_on_load_screen`, 117 lines each) was off by
about 2200 clocks, a one-off per save that no calibration moves.

The drift bound is tight enough that it holds only for a calibrated value: an
entry's drift over one part of the library differs from its drift over all of it
by far more than 0.38% (over the two movies `gato-battle` and `battle-magus` alone, more than 80 entries are over
it, `battle_cycle_party_palettes` by +29%). So adding a movie to the library, or
a port, means running `--calibrate` again, which is the point: `cost_milli` is
the measured value, not a guess. That variation between scenes is real
systematic error in the frames it happens in, and the per-stretch limits are
what bound it where it could matter.

**Adding movies.** Every movie moves some entries' drift past the bound when it
joins: leaving each of the 32 movies out, calibrating on the rest and adding it
back puts 11 (each world-map walk) to 156 (`endings-bucket-lavos`) entries
over, because each entry's ratio of the original's time to the port's differs
between scenes (`overworld_follow` +96% in `field`, where Crono walks alone, -7%
with two followers in `field-65m-bc`) and the bound holds the library's mean of them. That is
calibration falling behind, not a broken estimate, and recalibrating clears it
(with the four world-map movies, all 15 entries over). A full `--calibrate`
rewrites every measured row of `game/registry.c` and collides with every branch
that calibrates its own, so a batch of movies is added this way:

1. Write the movies (a route's `--movie`, docs/MOVIES.md) into
   `oracle/movies/saves/` with their headers, and add their rows to RUNTIME.md's
   table.
2. Run the suite over the whole library (`--all-movies`: the default sample
   leaves most new movies out, and every number below is the library's). The fallback ratchet's instruction counts grow with the
   library, so `python3 tools/fallbacks.py build/suite/fallbacks/shadow --update`
   takes them (a routine whose distinct bytes rose is original code the new
   movies run in place of a port: say which in the commit; the world-map walks
   added 146 bytes in bank `$C2`, 59 with no port yet and 87 handed back by
   `menu_run_objects`). Drift failures are the
   expected ones:
   `python3 tools/timing_gate.py build/suite/shadow-*.log --calibrate game/registry.c --drifted`
   sets only the entries over the bound to their calibrated values, going round
   again for the dispatchers that run a recalibrated handler inline, and leaves
   the rest of the registry alone (15 rows for the world-map movies).
3. Run the suite again, `--all-movies` (the shipping mode's checks move with the values). What
   is left over is a real finding: a vblank or worst limit, which calibration
   doesn't move, belongs to an entry whose time follows data its estimate
   doesn't follow; make it follow (a charge per unit of work, `game/charge.h`,
   or the part timed as its own routine), and only where that can't reach it,
   an exception with the vblank check's word that no write moved.

**The part timed as its own routine: the menus' map scroll.** The scroll
(`menu/map.c`) moves a plane a pixel at a time and, where a tile column or row
comes into view, builds a strip of 30 or 34 tilemap words and queues it. One
`cost_milli` stood for both, so the frames that built a strip were off by a line
or more: the scroll command `menu_object_cmd_scroll_map` in 29.85% of its 11,975
vblank stretches, the one-plane command in 91% of 123, and the object scripts'
frame that runs them in 2.11%, all three timing exceptions. The original builds
and queues each strip in routines of its own (`$C2:0773`, `$C2:07C2`, `$C2:0843`,
`$C2:088F`, `$C2:090D`, `$C2:0961`), so those are registered entries now, which the
original's scroll enters in shadow mode and which are calibrated on those calls,
and the scroll here runs each strip under its routine's `TIMED_AS` tag: a strip
is estimated at the strip routines' rate (`cost_milli` about 15,200 for a build,
4,500-4,800 for a queueing), a pixel's move at the scroll's (about 5,100). Two
things it took. An entry's own calls are measured on what the entry does, and
the strip and scroll entries also take their operands from the original's cells,
put results back and return, which their C callers never do; with those
accesses in, a build was calibrated on 1,464 bus clocks a strip where the scroll
spends 1,256, and every strip the scroll built was underestimated by 2,700
clocks. The entries leave that calling convention out of the estimate
(`charge_uncounted_begin`), as the battle script runner's table read is. And
the ports that run the scroll inline (the two commands, the overworld's walk,
dactyl and Epoch, the scene's drifting map) were moved by what the split moves
them on the 35 movies that scroll the map, rather than calibrated on those
movies alone. Over those movies the strips' estimates are within 0.1% a call,
the scrolls' 2.5% (46-50% before), and none of the three entries is over the
vblank tail (0%, 0% and 0.53%); the dactyl's frames that only run a sprite
script, which its own share of the frame no longer props up, went from 895 to
1,041 clocks off and are an exception.

An entry over a limit fails unless `tools/timing_exceptions.txt` lists it
for that limit with a reason; an exception an entry no longer needs is reported
over the whole library (`--all-movies`; the sample can't tell an exception no
longer needed from one only its other movies need), so the list doesn't go stale.
The gate's default sample (`tools/gate_movies.txt`) is judged on the worst
cases only: drift and the vblank tail are shares of every stretch, and a subset
of scenes moves them against costs calibrated to the whole library (over the
sample alone, 305 entries were past the drift limit that the library holds
within it), so those two are `--all-movies`'s to judge, as calibration is.

**Exceptions** (6 entries, 11 limits). Each is an entry whose time follows its
data in a way one scalar can't, and whose error was shown not to reach the
budget: the vblank check finds none of their writes moved.

| Entry | Over | Why it doesn't matter |
|---|---|---|
| `field_rasterize_quad` | `vblank-tail`, `vblank-worst` | Rasterizes a window shape into a RAM table, 35 lines a call (as long as vblank), 7.5% error, at most 14885 clocks. Main-loop work running on into vblank after the NMI, writing RAM, and the original's own time already runs it into the picture. |
| `menu_object_wave_hdma` | `vblank-tail` | Fills a RAM scroll-wave table in 53-line calls, longer than vblank; its error never exceeds 1396 clocks (one line), just over the tail's 1024. |
| `menu_draw_glyph` | `vblank-tail` | Draws a glyph into the menu's tile buffer in RAM, 18-line calls; at most 2658 clocks (two lines). |
| `menu_draw_text` | `vblank-tail` | Draws text into the menu's tilemap buffer in RAM, 19-line calls; at most 4509 clocks in vblank. |

**Interrupts during a port's time.** The vblank check (above) found the
shipping mode's worst damage, and recalibration alone didn't clear it: with the
calibrated values the 16 failing findings were still all there, most of them
smaller (`field_nmi`'s black bands 1348 to 6, `battle_nmi`'s mid-picture blanks
255 to 64). The cause was how a port's time is paid. A port runs to completion
at once and its time is charged afterwards, a scanline at a time, and
interrupts waited until the whole debt was paid: an NMI arriving during a long
port was taken up to that port's length late, and the handler's video writes
went with it. The spike saw the same with `scene_multiply`, whose totals were
exact and still drifted.

Now a debt is owed at a place: the instruction boundary where the port stopped
(its return address, or the first instruction of a callee it called out to),
with S as it was there (`owe`, `pay_debt`). While the machine is at that
boundary the debt is paid, and an interrupt the CPU would recognize is taken
there instead, as the original would take it somewhere inside the routine: the
handler runs on time (seeing the port's work already done, a timing difference
the decision accepts), returns to the boundary, and the rest is paid then. A
payment step also ends just after the next point an interrupt can be raised
(`until_interrupt`, from the spike's `snes_interrupt_horizon`), so the poll sees
it that step; an interrupt already raised and not yet seen (a V-timer IRQ at the
start of the line the step starts on) ends the step after one cycle. Debts nest
as handlers run ports of their own. A port suspended in `native_sync` (between
two APU accesses) owes its time the same way: its PC and S stay put while it
waits, an interrupt taken then returns to them, and the port goes on once the
rest is paid. (At first those debts were paid whole with interrupts held; a
sound driver upload then delayed `field_irq` past line 211 in `late-lavos`,
and its blank landed on line 214.) If the machine leaves the boundary some
other way than by an interrupt, the debt is paid where it is. Idle waits end their steps at the same points, so a line IRQ
arriving during one is no longer taken at the next hblank change.

**The idle exit.** The raster split one line early was a separate cause: an
idle entry's waiting pass skips to the point the polled bits change, so the
exit pass saw the change at the change itself, and the line IRQs' forced blank
after their hblank wait landed at hclock 1102, before the line is drawn (1104).
The original sees a change on the first read of its next pass and then falls
through its branch. An idle entry that waits for a change (no `idle_clocks`)
now charges its exit pass `IDLE_EXIT_CLOCKS`, the shortest such loop's pass
(`LDA abs`/`AND #`/`BEQ`, 54 clocks), so the code after the loop lands no
earlier than the original's could.

**A race it exposed.** With the idle exit, the `beyond_time` route
(`tools/movies/route_check`, shipping mode) stuck in map `$DA01`: the party
reached the passage to the second form's chamber and nothing happened. Only
`field_irq_wait_hblank_loop`'s exit made the difference (bisected by entry
prefix: with the exit on every other idle entry the route passed). What it
changed is the phase of a busy loop. The event command that changes location
now (`$E1`, whose port stops at `$C0:3B71`) waits for vblank before blanking and
loading: `LDA $4210 : BPL`, with the vblank interrupt enabled, and the field NMI
handler reads `$4210` too (`$C0:EA70`), which clears the flag. The loop sees the
flag only if its read lands between the flag going up at line 225 and the NMI
being taken; otherwise the handler clears it first, and the line interrupt at
line 211 returns to the loop at the same point every frame, so it misses it in
every frame after, for good. The original's exact timing wins that race (the
reference changes map 83 frames after the push, as the interpreter does); the
line interrupt ending 48 clocks later lost it, and before the idle exit the
route passed on the same luck. Machine states at frame boundaries were identical
apart from the latched H counter, and so was all of work RAM; the CPU sat at
`$C0:3B71` from then on. Two loops in the ROM race like this (`$C0:3B71` and
`$C0:4181`, both followed by `$C0:0B4E`, which blanks the screen and stops
interrupts); the other two `$4210` loops (`$C0:0B38`, `$FD:EADA`) run with the
NMI disabled. Both are idle entries now (`field_change_location_wait_for_vblank_loop`,
`field_restart_wait_for_vblank_loop`): a pass reads the flag as the original
does and leaves when it is set or, if the handler got there first, when the
blanking status says vblank. That is what the loop waits for, and it can't lose
the race; the reference (`--generated only`) and shadow mode don't run idle entries,
so nothing they check moves.

**The vblank check, before and after** (failing findings over the 26 movies):

| | Failing findings |
|---|---|
| Before (the hand-set `cost_milli`) | 16 |
| Recalibrated only | 16, most of them smaller |
| + interrupts taken during a port's time | 6: both line IRQs' split at line 211, `battle_irq` a line late 20 times, the boot program's three |
| + the idle exit | 5: `battle_irq` and its blank a line late about 15 times in 5 movies, the boot program's three |
| + the step that sees an IRQ already raised | 3, the boot program's, accepted |

(These were measured with the scalar calibration; the final build, with DMA
taken as it is and recalibrated, has the same three and nothing else, and 4
`late` findings where the first run had 36. After the race fix below changed
the movies' paths, `battle_irq` landed once on line 224 in
`endings-bucket-lavos`, behind the battle's frame work running with interrupts
masked; the reference does the same in that fight on lines 212-223 and in
vblank, so it is accepted too.)

The three left are writes the original also makes on the picture: the boot and
title program at `$7E:3000` shows the title's layers from line 32 instead of 31
(`scene_show_layers`), and blanks from line 40 instead of 38 on the frame before
it restarts (`scene_program_restart`, display and HDMA enable), once each per
movie. They land wherever the frame's main-loop work has got to, which in the
shipping mode is estimated; they are in `tools/vblank_check_exceptions.txt`.

None of this touches `--generated only` (no hand ports, so no debt and no idle
entries) or shadow mode (no debt), and the trace identity check is unchanged:
`new-game` shows no divergence in 7200 frames.

**Failing.** With `battle_draw_sprite_parts`'s `cost_milli` set from 3011 to
3500 (the entry with the most hand-port time), the whole suite fails and names
it, with its numbers:

    26 of 26 movies clean in 529s
    timing: 1 of 414 entries over the limits  <-- FAILED
      battle_draw_sprite_parts over the drift limit: +16.26% (limit +/-0.38%)
          1855861 stretches in 11 movie(s), mean time 5542, mean |error| 901 (16.3%), drift +16.26%,
          max 2189; in vblank 46330, off by >= 1024 clocks 0.17%, max 2177; cost_milli 3500, calibrated 3011
    FAILED: timing

**Cost.** Nothing measurable: the counters were already there, the report adds a
few lines per entry to each shadow log, and parsing 26 logs takes under a
second. The suite at `-j 2` runs its 26 shadow movies in about 520-550 seconds
(716 in the baseline run, on a busier machine).

    python3 tools/suite.py --ct-dev build/main/ct_dev -j 2        # the gate (its sample), timing check included
    python3 tools/suite.py --ct-dev build/main/ct_dev -j 2 --all-movies   # the whole library, to calibrate from
    python3 tools/timing_gate.py build/suite/shadow-*.log --table # every entry's numbers
    python3 tools/timing_gate.py build/suite/shadow-*.log --calibrate game/registry.c

**Dispatchers: the time of the routines a port runs** (between Phase 3's
waves 1 and 2). The exceptions went from 11 to 27 in wave 1, and most of the
new ones had one cause: a port that runs a dispatcher's handlers as C calls
rather than calling out to them (`palette_animate` and its eight types, the
battle's effect slots and its fifteen path handlers, the object drawing pass
and the six routines it calls per object) spends its time on whichever
handlers ran, and one `cost_milli` for the whole call can't fit that:
`palette_animate` ran twelve delay counts one frame and a fade of 256 colors
through the multiplier the next, and missed by 25.9% on average while each
handler on its own fitted its own scalar within 0.3-4.4%. Wave 2's banks are
full of dispatchers, so the fix is in the estimate rather than in more
exceptions.

A port now names the routine whose work it is doing, by its address:

```c
NATIVE_TIME_AS(s, 0xCFEDB8, draw_underlays(s));        /* $CF:EDB8's time, not the pass's */

int was = native_time_as(s, type_handlers[type]);      /* around several statements */
x = run_type(s, slot, type);
native_time_end(s, was);
```

and the port's bus clocks in between are estimated with the `cost_milli` of
the hand port registered at that address instead of its own
(`runtime/port/native.c`: the running job keeps the routine it attributes to,
closes a part when that changes, and `estimate_parts` sums DMA's time as it is
and each part's other clocks times its routine's value). Nothing in `game/`
says how long anything takes: the tag is an address, and the value it selects
is calibrated like every other entry's. That works because the original runs
the handlers as routines, so shadow mode already measures each of them on
every call it makes, from its entry to its return, as an entry of its own
(nested inside the dispatcher's comparison); the dispatcher's estimate is then
the sum over the handlers it actually ran, at their own measured rates, plus
its own loop at its rate. Scopes nest (a fade handler attributes its colors to
its channel mode's loop), a scope naming the running port is its own time, and
a helper that is a routine of its own can carry the tag inside it
(`battle_sine_scaled` names `$CF:F9FB` wherever it runs) so every caller
attributes it.

The shadow report lists the attributed clocks per routine (`timing: part
<addr> <name> native-bus N dma M cost_milli C`), and `tools/timing_gate.py`
estimates an entry as its own clocks at its value plus each part at its
routine's. `--calibrate` works the handlers out first, from their own calls,
and then gives the dispatcher's own part what the handlers' new estimates
leave of its time. An attribution to an address with no hand port counts as
the port's own time, and the report names it; the gate fails on it with no
exception possible, and `tests/test_timing_gate.py` checks every literal tag
in `game/` against the registry. `tests/port/time_parts_test.c` runs a made-up
dispatcher and two handlers in shadow mode and checks the parts' clocks, the
handlers' own measurement and the report of a bad tag. Nothing shadow mode
compares changes: a tag only decides which `cost_milli` a port's clocks are
estimated with, in the report and in `on` mode's charge.

A sibling that a port runs as C where the original's call reached it through
the machine is measured on its own stretches too, not inside its caller's
(`native_time_as_sibling`, `runtime/port/native.h`): a stretch more than half
of whose bus clocks were spent in such a run is the sibling's in the report,
its errors and its calibration, with the caller's own few clocks a part of it
at the caller's value. The battle's steps showed why. Once the dispatch
(`$C1:0003`) was a port running the frame's work and an action's showing as C,
every stretch of theirs was the dispatch's, so the dispatch's own few clocks
(the asker's registers pushed and the return) took up the siblings' error,
calibrated at 94,180 per thousand, and the asker's cost absorbed what that
did elsewhere (`battle_give_turn` 6451 to 4767). Charged so in the shipping
mode, a battle's frames ended at other points: `endings-telepod-lavos` lost
the Lavos shell fight, went back to the title, and its presses opened New
Game's name entry for about 95,000 frames (3.29 M to 8.29 M instructions in
place of hand ports). Measured as the siblings' own, the dispatch calibrates
at 2367, the askers at their old values, and the movie wins the fight again
and plays to its end as before (3.26 M). `tests/port/time_parts_test.c` checks the rule on a made-up
handler. Only the report moves; the estimate and `on` mode's charge don't.

A tag needs a hand port at its address to be measured. Where the part of a
dispatcher that varies wasn't one, it became one, as an entry of its own with
its guard, don't-care set and fuzzing (and `tools/dontcare_check.py` agrees
with all four): the underlays' marker `$CF:EE2C` and prepared number sprites
`$CF:EE9F`, which the original jumps to, and the fades' restore paths `$FD:E8AC`
and `$FD:E624`, which it branches to: a fade that ends by restoring its colors
copies up to 255 of them at a rate nothing like the level arithmetic's, which
was the fades' and `palette_animate`'s whole worst case (30817 clocks). An
entry can start at any instruction the original reaches, and shadow mode
measures it from there to its return like any other.

Adopted on the dispatchers behind the wave-1 exceptions: `palette_animate`'s
types and the fades' channel modes and restore paths (`core/palette.c`), the
battle's effect slot handlers with the flicker step, the orbit step, the sine,
multiply, distance and angle helpers (`battle/effect_paths.c`), the object
drawing's six routines per object, the trail's afterimages, the underlays'
three and the tile copy (`battle/draw_objects.c`), and the menu text's tiles
and numbers and the glyph's pixels (`menu/text.c`, `menu/glyphs.c`).
Recalibrated over the library, the entries it changed:

| Entry | mean \|error\| before | after | vblank stretches off by >= 1024, before | after | largest in vblank, before | after |
|---|---|---|---|---|---|---|
| `palette_animate` | 25.9% | 1.8% | 57.66% | 0.46% | 26796 | 1725 |
| `palette_fade_to_white` | 3.3% | 0.4% | 4.45% | 0.00% | 17718 | 889 |
| `palette_fade_to_black` | 1.1% | 0.5% | 1.07% | 0.00% | 30817 | 527 |
| `battle_step_effect_slots` | 18.1% | 4.9% | 7.91% | 1.94% | 18133 | 2838 |
| `battle_draw_objects` | 10.7% | 3.7% | 92.23% | 85.36% | 23882 | 7311 |
| `battle_draw_object_underlays` | 55.4% | 12.7% | 8.96% | 8.11% | 17409 | 1855 |
| `menu_draw_text` | 2.0% | 2.1% | 32.39% | 19.35% | 4509 | 3498 |
| `menu_draw_glyph` | 5.1% | 5.5% | 53.89% | 59.92% | 2658 | 2932 |

Over all 523 entries the estimate's error fell from 95.4 to 52.7 clocks a
stretch on average (7.01% to 3.89% of hand-port time), and the stretches
off by 1024 clocks or more from 1.115% to 0.512% (in vblank, 1.053% to 0.723%).

**The exceptions, before and after**: 27 limits over 16 entries before, and
18 limits over 13 entries after. Removed: all six of `palette_animate`,
`palette_fade_to_white` and `palette_fade_to_black`, and the `vblank-worst` of
`battle_draw_objects`, `battle_draw_object_underlays` and
`battle_step_effect_slots`. What is left, and why each stays:

| Entry | Over | Why |
|---|---|---|
| `field_rasterize_quad` | `vblank-tail`, `vblank-worst` | Its time follows the quadrilateral's edges; the edge sequence it inlines is no routine the original calls, so there is nothing to attribute to. |
| `menu_object_wave_hdma`, `battle_screen_wave_windows`, `scene_draw_circle`, `scene_draw_circle_jump`, `battle_step_hp_gradient` | `vblank-tail` | One routine each, its time following a path inside it (the sign of each wave step's product, the circle's midpoint steps, the gradient's rows): long calls whose error is 1-5% but over the tail's absolute 1024 clocks. |
| `menu_draw_glyph` | `vblank-tail` | Its pixels are a routine (`$C2:5E89`) and are attributed to it, but that routine's own time follows the pixel's bits (7.1% on its own), so the sum varies as much as before. |
| `menu_draw_text` | `vblank-tail` | Its tiles and numbers are attributed (the share off by a line fell from 32% to 19%); the control codes' handlers are no entries yet, and the code loop's own time follows the string. |
| `battle_draw_objects` | `vblank-tail` | Every routine it runs per object is attributed (10.7% to 3.7%); what is left is its own work, building and sorting the draw list (an exchange sort whose swaps depend on the order) in 37-line calls, where 3.7% is still over a line. |
| `battle_draw_object_underlays` | `vblank-tail` | Its three routines are attributed (55.4% to 12.7%); what is left is its own loop over the object's eight effect records, whose time follows which are set. |
| `battle_step_effect_slots` | `vblank-tail` | Its handlers and the helpers they call are attributed (18.1% to 4.9%, and 7.9% of its vblank calls off by a line to 1.9%); what is left follows its own loop over which of the nineteen slots are active and due, and the handlers' own spread. |

None of these writes a video register: they write RAM the next vblank uploads,
and the vblank check finds no video write moved.

**Calls that outlast a frame, and the battles' drift.** Of the 45 movies
`tools/strict_baseline.txt` marked `drifts`, 33 first moved at a battle: a
member's command menu opening, or the menu closing, frames early or late, and
`tools/drift.py` saw most of those battles start from the random number one
below the reference's. Four causes, each measured by comparing the reference
play with a strict run over one battle (`ct_dev --state-trace` both ways, the
shadow run's per-call times):

- *The load screen's frame count.* The battles' random numbers start from the
  menus' vblank count as the load screen is left (`frames_at_leaving`), and in
  every save movie strict mode's count was one short: the master clocks of the
  menus' first vblank were the reference's, but the run had counted one more
  frame by then. The menus clear video memory with one DMA transfer longer than
  what is left of the frame; the original's transfer stalls the CPU inside the
  one instruction that starts it, so the reference's step ran past two vblanks
  and its frame counter (the movie's frames) went up once, where the port's
  debt was paid a line at a time and counted both. A port's longest transfer is
  now paid as one step (`pay_debt`, `runtime/port/native.c`), and every save
  movie's battles start from the reference's number.
- *Time no estimate saw.* The way in from the field clears the battle's 32 KiB
  of memory with a store a word, about four frames of the original's time, and
  the port's fill took none (`battle_start_from_field`: 1,347,388 clocks of the
  original's against an estimate of 827, every battle). It is charged now
  (`charge_as`, at the way in's own rate).
- *Entries no calibration reached.* The timing report measured only stretches
  no interrupt fell in, and a routine whose calls always outlast a frame never
  has one: the battle's setup (`battle_set_up`, `battle_setup_state`,
  `battle_setup_graphics`, `battle_start_fight`, `battle_main_loop_start`),
  `field_battle_begins`, the field's return from a battle, the menus' panels,
  kept their hand-set `cost_milli` (3000, 5000) or one from before they grew;
  `battle_setup_graphics` ran at half the original's time. A stretch an
  interrupt fell in is now measured with the handlers' time taken out of the
  original's (`end_segment`; an interrupt is followed from the step it is
  taken at to the step that comes back to its S, one that never comes back,
  the menus' vblank handler going on in another task, given up after two
  frames and the stretch left out). Its totals count in the drift and the
  calibration, its single error in none of the per-stretch limits, and
  `timing_gate.py --calibrate --interrupted` sets only the entries measured
  mostly so, which a calibration over part of the library may do without
  moving the entries the library calibrates.
- *A song loaded under the battle's handler.* A battle with its own song
  loads it after the battle's vblank handler is in, and the time model's
  upload rates are the wall's under a light one: `battle-mud-imp`'s battle
  song took 47 frames in strict mode and 56 in the original's. Taking the
  handlers' time on top of a sound command's wait closed it, and broke the
  native-shape build's `beyond_time:form2` route as it had once before, so
  it is not in (the frame-level time model, below; TODO.md).

Measured with the calibration of the entries only interrupted stretches
measure over 73 shadow runs (the drifting movies and the gate's sample;
`timing_gate.py --calibrate --interrupted --also battle_start_from_field`),
and `tools/drift.py --drifting --no-timed`: before, 34 of the 45 parted with
a battle menu's shift in force, a median 11 frames, and every save movie's
battles started one random number low; after the first three changes, the
battles start from the reference's number (`events-proto-dome`'s differs for
another cause, as it did), 29 part with a battle menu's shift in force, a
median 7 frames, and `battle-denadoro` ends as the reference
(`strict_check.py`). `battle-tyrano-lair-walls`, which ended as the reference,
now drifts (`drift.py`: with the original's time it plays as the reference
to its end). The gate sample's strict outcomes were re-recorded with the
change (`tools/outcomes_exceptions.txt`; the checkpoints a run no longer
matches leave `tools/outcomes_baseline.txt`, each movie that lost one being
drift or the same as the reference by `drift.py`). Of the rest, 19 part later
than they did and 6 earlier: a battle a frame off meets the recorded presses
otherwise from there on, so where a movie parts moves either way with any
change to the time. With the sound waits' handlers on top as well, the
median was 4 frames and `events-sun-stone` ended as the reference too; that
change is out until the route it breaks is understood. What is left is in
TODO.md ("Strict mode's battles a few frames off").

### The frame-level time model

The estimate keeps a port's own time on the machine; a build with no machine
has none to keep, and a movie's presses, recorded against the original, land
on the frames the original's time put them on. The time model is what keeps
those frames where nothing counts clocks: the original's time for the work
that spends frames, charged as whole frames the main thread sits out.
`runtime/platform/time_model.h` holds it, constants and pure functions in the
original machine's master clocks, with no state, no clock and no `Snes`.

**Beside the estimate, not in its place.** On the machine a port's time stays
its calibrated `cost_milli` estimate, measured and bounded by the timing gate
above: it is what keeps the shipping mode's video writes where the vblank
check wants them, frame by frame. The model charges only what no estimate can
see because the port hands the work away: the C sound driver's boot and
commands (below). Without a machine there is no bus to count, and the model
is all the time there is: the game's own work timed as the machine's
estimate times it, and what the ports hand away charged by what they moved.
The two backings of `charge.h` divide it: `runtime/port/state_io.c` (the
machine) puts a charge into the port's estimate, as it always has;
`runtime/port/state_host.c` (`ct_native`) puts it, and the game's own work,
into the model's. At the teardown the estimate and the timing gate retire
with the machine that measures them; the model's constants and the rates,
measured from it once, and the places' ratchet (below) are what stay.

**The game's own work, at the calibrated rates.** `ct_native` counts each
access the game makes -- its accessors (`vars_ram.c`), its reads of the
cartridge (`rom_host.c`), save RAM through the save service
(`TmHostProgram.accessed`) -- at the bus clocks it takes on the machine (8, 6
for the cartridge's), and times them at the `cost_milli` of the routine the
work is tagged as (`TIMED_AS`, `NATIVE_TIME_AS`: `charge_time_as_begin`), the
rate the machine calibrated for it. The rates are the registry's and the time
tags' (`game/time_tags.c`), which the build writes as a table
(`tools/time_rates.py`, `runtime/port/time_rates.h`), so the program keeps no
registry and always has the last calibration's. A charge is made as on the
machine: at its routine's rate, the accesses it stands for left out
(`charge_uncounted_begin`), the unpacker at the price below. For that the
charges are plain, not scaffolding (`game/charge.h`): 171 of them were in
`ORIGINAL_EFFECT`, so the build with no scaffolding had none, and the
native-shape build ran without them too. What isn't the game's work takes
none: a guard's look (`vars_peek`, which the event engine's C path now takes
as its machine path does), the C sound driver and the CPU half's side of a
command (`frontend/sound_native.c`, whose time the hooks answer), the
handlers' (below). The menus' load screen shows what this brings: its setup
(`$C2:800E` up to its tasks) took the original 14.5 frames (the shadow run's
estimate of its parts, 5.17 M clocks), and the model charged 2.4, its sends
and sound alone; it charges 14.5 now, and `battle-ozzies-fort`'s battles start
from a random number two from the reference's where they started twenty off.

**What a lag frame is computed from.** The host timing backend
(`platform/native/timing.c`) keeps what the main thread owes since it last
waited (`tm_host_charge`, and the game's state's account through
`TmHostProgram.owed`). As the main thread starts its part of a frame it takes
what the handlers did since it last ran, the frame's own: that much less of
the frame is the main thread's (up to half of it). At its wait, the whole
frames its work fills, so measured, become lag frames, in which the vblank and
line handlers run and the main thread doesn't, as the original's did while it
worked; what is left fits in the frame and goes. Where the handlers change
(`tm_enable_interrupts`), what is owed is sat out first, under the handlers
it was done with: the menus' setup runs with the vblank handler off, and its
fourteen frames pass with it off, as the original's, before the handler that
counts the menus' frames comes on. `timing_test` holds the arithmetic.

Before the work was timed, the frames owed went to the nearest, half a frame
standing for the work nothing counted. Counting the work at one mean rate (40
clocks an access) was tried then: the attract demo kept to the reference far
better, but the library entered fewer places on time, a mean rate
over-charging light frames and under-charging heavy ones. The routines' own
rates are what follow the heavy frames, and with them the whole frames filled,
not the nearest, are the measure: to the nearest, the field's every fifth
frame (two thirds of a frame of work, which fits the original's frame) lagged.

**What is charged, and at what rates** (each measured on the machine; the
numbers in `time_model.h` with how):

| Work | Charged | From |
|---|---|---|
| The sound driver's boot | 7,360,649 clocks (20.6 frames) | the CPU half's port with the original driver, from the boot ROM made ready to the driver's start; one cost, the upload never varies. A host's driver starts its time (its first echo's wait) at the upload's end |
| A command the CPU half passes on | 3,200 | the same, per command |
| A song's start | 95,000, plus 405 a byte loaded and 380 a byte the sound processor moves | 70 songs in eight movies: about 94 echoes of tables and the command itself, then three bytes a message with the music stopped (400 to 425 a byte under a light handler) |
| Command `$70`'s effects, `$71`'s sample | 750 a byte | uploaded with the music playing, the sound processor answering between its ticks |
| A song's start while the echo's buffer settles | what is left of the wait, counted after the time the port owes so far has passed (`native_sync`, `tm_host_sit_out_owed`) | the original's song waits on the ports for it; the driver's wait runs through the CPU's work after the last song, which reaches the ports only after that work |
| The unpacker (`$C3:0002`), in `ct_native` | 64 a charged access | its charge counts the accesses the original makes to its own cells, which follow the stream; its whole time over new-game's 60 streams (29.0 M clocks in the shadow run) against those accesses |
| A send to video memory from the main thread, in `ct_native` | 8 a byte | DMA's rate; the handlers' sends are the frame's own |
| The game's own work, in `ct_native` | 8 a memory access (6 the cartridge's), at the tagged routine's `cost_milli` | the machine's estimate of a port, its rates the registry's and the time tags' as last calibrated; a charge at its routine's rate |

The sound's are the wall's time under a light vblank handler, and the machine
lets it pass as a wait (`native_wait_time`, interrupts taken meanwhile). The
same song loads in about twice the frames under the menus' busy handler
(`menus-shop-equip`'s songs, 820 to 1,230 clocks a byte), and a battle's song,
loaded once the battle's vblank handler is in, in 56 frames where strict mode
takes 47 (`battle-mud-imp`'s `$45`), which the model doesn't follow. Waiting
the CPU half's own time instead, the handlers' on top, followed it, but sent
the native-shape build's `beyond_time` route into a wait it never walked out
of; tried twice, it was dropped both times (TODO.md). The C driver
keeps the count: `SoundDriver.bytes_sent` and `bytes_moved` are
what it loads (`game/sound/`, a size and no time), and its two hosts
(`frontend/sound_host.c` on the machine, `frontend/sound_native.c` without)
answer the boot's and each command's time through the hooks (`Apu.host_boot`,
`host_command`; `AuHost.boot`, `command`). A song held while the echo settles
loads at the wait's end, which isn't charged.

**Measured.** The places a run enters, by frame, against the reference play's
(`tools/places.py`; `ct_dev --locations` and `ct_native --locations`, the
suite's shadow runs keeping the reference's): how many of the reference's
places a run enters within 30 frames of the reference's frame, among those it
enters in the reference's order. Over the 53 library movies (460 places):

| | `ct_native`, places on time | strict mode, movies ending as the reference's |
|---|---|---|
| Before | 240 | 34 |
| The sound driver's time | 294 | 37 (339 places on time) |
| And loads (`ct_native` only) | 299 | |

Timing the game's own work was measured over the gate's 37-movie sample
(`tools/native_check.py --movies ...`, against the suite's shadow runs): 194 of
the reference's 293 places entered on time before, 209 after; the places
entered within 5 frames of the reference's, 148 before and 196 after; and the
attract demo, whose places are the game's own time, 1,229 frames behind the
reference at its end before and 211 after. Movies ending as the
reference's: 18 of 36 compared before, 18 of 37 after (`events-fair-rescue`,
`late-omen-terramutant` and `menus-shop-inn` among those that now do,
`menus-end-of-time` and `battle-magus` among those that now drift). The new
time took `late-omen-battles` into a battle where an enemy waits for its
target to move with none in sight, which the program's C path refused
though the movement's entry takes it (`battle_enemy_movement_needs_target`
now says the waiting movement goes on with none); it now reaches its end.
The native outcomes were re-recorded with it (`tools/outcomes_exceptions.txt`,
each line's reason naming the change); the protections lost leave
`tools/outcomes_baseline.txt` (the end state of `menus-end-of-time`, the save
RAM of `events-death-peak`, a place of `events-ocean-palace-lavos` and
checkpoint pictures in nine movies, each a run whose places keep to the
reference's within the window but whose frames moved), and the matches
gained join it; `tools/places_baseline.txt` takes the counts the change
brings (none lower).

Strict mode's three, `battle-magus`, `battle-tyrano-lair-walls` and
`field-factory-switch`, now end as the reference's, and none ends worse; in
`ct_native` the title shows 3 frames early where it showed 26 early, and
`gato-battle` enters 10 of its 11 places on time where it entered 9.
`tools/native_check.py --check` holds `ct_native` to it, a ratchet on
`tools/places_baseline.txt`, and `tools/strict_check.py` reports it for strict
mode.

## Cleanup into hand ports (M5)

A port starts from a draft, not from the listing:

```sh
python3 -m tools.generate.show C1:423A              # widths from the analysis, or --m 8 --x 16
python3 -m tools.generate.show C0:62DE C0:5FB6 > <scratch file outside the repo>
```

`tools/generate/show.py` prints the routine shaped the way the porting rules
(CLAUDE.md, "Writing a port from generated code") ask a port to be, derived
mechanically from the ROM. It prints to stdout like `tools.generate.linear`: a
draft is a mechanical image of the ROM, so it is never committed, and nothing in
the repository contains one. What is committed is the tool, its tests on
synthetic code, and two checks: `tools.generate.draftcheck`, which registers
drafts in a scratch tree to shadow them, and `tools.generate.groundtruth`, which
compares a draft with the hand port of the same routine. The workflow a porter
follows is PORTING.md's "From a draft to a port"; this section is how the tool
is built and how good its drafts are.

### What a draft is

Three parts, for a routine at `$BB:AAAA`:

- A **logic function** that takes the registers the routine reads as C
  parameters and returns the ones its callers read (a struct when there are
  several), computing with C locals: `a`, `x`, `y` and the flags `negative`,
  `overflow`, `zero`, `carry`, the last only where something reads them. Memory
  goes through `m_read8`/`m_write16` and friends with work RAM spelled by its
  `game/ram.h` name. It has no emulated CPU in it: no `c->x`, no `m_lda8`, no
  `m_rep`.
- A **wrapper**, `void name(Snes *s)` under the `// $BB:AAAA` comment, that is
  what registers: it moves the machine's registers into the logic's parameters
  and its results back, sets P's widths and returns as the original does
  (`m_rts`, `m_rtl`, `m_rti`, or setting PC where it hands back). It is the thin
  translation a 65816 build would replace with glue to the original calling
  convention.
- **Call-out stubs**, one per callee and register signature, that do the same
  around each `m_jsr`/`m_jsl`: the logic says `call_battle_multiply_8x16(s,
  0xC1429F, a, y, overflow, carry)` and never touches an emulated register. A
  stub sets only what the callee reads (and what the draft reads back, since a
  callee can leave a register alone), and passes the rest to shadow mode as
  don't-care.

A comment ahead of it says what the tool knows: the command it handles if it is
a script command's handler, the entry widths, D and DB and where they came from,
what callers read after it returns, where and why it hands back to the
interpreter, the I/O registers it touches (which a port reaches through
`runtime/platform/`), the machine state it sets directly (D, DB, S, P's
interrupt and decimal bits), and the registry line it registers with. For a
made-up routine at `$C0:8010` -- `LDA $10 : STA $20 : RTS`, called with D =
`$0100`, whose callers reload A's low byte and the flags afterwards -- the
logic is:

```c
static uint8_t sub_C08010_logic(Snes *s)
{
    uint8_t a;
    a = m_read8(s, RAM_TEST_SOURCE);
    m_write8(s, 0x7E0120, a);
    return a;
}
```

### How it is made

The draft is not made from the generator's C. `tools/generate/translate.py`
writes what the emulated CPU does -- every flag, every bus access in order, the
fetches and cycles -- which is what makes generated blocks exact, and none of
which a port wants; rewriting that text into values would be parsing C back
into meaning. The draft takes the same inputs the generator does (the
analysis's code and widths, `tools/disasm/liveness.py`, the hints) and lifts
the instructions again, into values.

1. **The routine** is the analysis's context at the address and widths:
   everything reachable from the entry without a call, with the widths each
   instruction runs with, including jump-table targets. An address the analysis
   never entered (a loop head, a mid-routine entry a port registers) gets a
   context of its own. The draft stops -- hands back to the interpreter -- at
   another registered entry, as generated blocks do, since that code is its own
   port.
2. **D and DB** (`tools/generate/frame.py`). The analysis tracks widths, not D,
   and a direct-page offset has a name only relative to its page. A forward
   pass over each routine follows D, DB, the accumulator's bytes and what the
   stack holds, so `LDA #$1D00 : TCD`, `PEA #$0300 : PLD`, `PHK : PLB` and a
   `PHD ... PLD` pair give values; a call keeps them unless the callee's
   returns show it sets them (a summary per callee, which can be "set on one
   path, kept on another"). Entry values are solved for the whole program at
   once: an entry's D is the set of values its call sites have (the static
   call sites and the ones the call logs saw), each resolved in its caller,
   iterated from nothing to the least fixpoint; D and DB are sets of up to four
   values, so a callee that resets D on one path is still known in a caller
   that had that D. Interrupt vectors, coverage seeds and hints are entered from
   places nobody can see, so they, and whatever only they reach, stay unknown.
   **Which page applies** follows from that: when D is one value, a
   direct-page access is the work RAM address D + offset, named by a variable
   `re/ram.toml` puts on a page with that D (step 7), or else a plain scratch
   name; when D isn't known the
   access stays `m_dp(s, offset)` with a note in the header, and `--page NAME`
   names the offsets of a page kind, whose D is not fixed (`scene_math`,
   `menu_record`), through their `_DP` macros. `--d`/`--db` give values by hand. A draft that
   assumed its callers' D or DB registers with a guard on them, so a caller
   the analysis missed falls back to the original instead of reading the
   wrong page.
3. **Lifting** (`tools/generate/lift.py`): each instruction becomes statements on
   the locals -- every flag it sets its own assignment -- with addresses as
   constants wherever D and DB are known. The stack, D, DB, S and P's other
   bits stay the machine's (`m_push16`, `s->cpu.d = ...`), so a call-out or the
   interpreter continuing sees what the original would have left. ADC and SBC
   are binary (a draft that has them registers with the `_BINARY`
   precondition). XCE, WAI, STP, BRK, COP, instructions whose widths the
   analysis doesn't know, and jump tables it didn't size hand back at that
   instruction.
4. **Cleanup** (`tools/generate/draft.py`): basic blocks in address order;
   liveness per flag and per byte of A, from what the routine's callers read
   after it (`ret_live`, over every routine the address is part of), what is
   read where it hands back (`live_at`), and what each callee reads before it
   returns (`callee_reads`; see "Before the bulk" below); dead
   assignments dropped (a read of an I/O register is kept for its side
   effect); constants propagated within a block; and a value computed once and
   read once in the same block written where it is read, when nothing between
   changes what it reads -- which is what turns `LDA $10 : STA $20` into one
   statement and `CMP #$05 : BCC` into `if (a < 5)`. Call-outs read back only
   what is read after them and the callee may change (a may-def summary over
   the callee and everything it calls, SEP #$10's truncation of X and Y
   included). A routine that never reads or writes A's high byte itself works
   on an 8-bit `a`: the machine's high byte then stays exactly the original's
   (only callees change it), and the wrapper and stubs move the low byte alone.
5. **Wait loops.** Native code runs in no time, so a loop that polls an I/O
   register without calling out, or reads only fixed addresses and stores
   nothing (a flag an interrupt sets), would never end in a draft; the draft
   hands back where the loop is entered and says what it waits on, and a
   routine that starts with one says to register it as an idle entry instead.
6. **Structure** (`tools/generate/structure.py`): the statements come in address
   order with labels and gotos; loops (`do`/`while`, `for (;;)` with `break` and
   `continue`), then `if`, then `else` are formed, smallest first, only where
   nothing outside a region jumps into it. What matches no pattern keeps its
   goto.
7. **Names** (`tools/generate/names.py`): work RAM from the RAM map, by the rules
   its coverage count uses (a name from the routine's own subsystem, `core`, or
   the subsystem sharing its page; arrays and records as `RAM_X + k *
   RAM_X_STRIDE`), the subsystem being the bank's or, in the banks several
   share, the callers'; a direct-page cell only from a variable on that page,
   else `SCRATCH_01DB` ("The tooling pass after wave 1: the analysis"); I/O registers by their conventional names in a comment
   (`0x002100 /* INIDISP */`); callees by their port or hint name. A routine
   that is the handler of an event command (`tools/disasm/eventscript.py`) or
   of a battle animation command, enemy condition or enemy action
   (`tools/disasm/battlescript.py`) is named after it (`event_cmd_if_var_const`)
   with the operand layout in the header, and an event handler's reads of its
   own operands, which go through X and Y as the command's offset plus a
   constant, are written `RAM_EVENT_SCRIPT + cmd + 1 /* var */`, `cmd` being
   the offset it was entered with.

### Decisions

**Drafts compile, and register as ports.** A draft that compiles can be
checked the way ports are, by shadow mode, which measures the tool's
correctness mechanically instead of by reading drafts. That shaped the output
less than expected: the logic reads as C either way, and what compiling costs
is at the edges -- the wrapper, the stubs, a `(a & 0xFF00) | ...` where a
routine keeps A's high byte that its callers might read, a cast where a value
wraps. Where exactness and readability pull apart, the draft stays exact and
says so, since a porter can drop what a caller doesn't need once they know the
callers, and a draft that looked cleaner but did something else would cost
them a debugging session. Two departures are deliberate: a direct-page access
indexed past `$1FFF` is drafted in bank `$7E` (the original would reach bank
0's I/O area), and an event handler's operand read at an offset past `$FFFF`
doesn't wrap. No game code does either.

**The workflow gets its own gate.** `python3 -m tools.generate.draftcheck --out
<scratch>` exports the committed tree to a scratch directory outside the
repository, drafts every routine `game/registry.c` registers, writes the drafts
there and points each registry entry at its draft, with the draft's own
precondition, don't-care set and D/DB guard. Built there and run in
`--native shadow` over the movie library, every call to a drafted routine is
compared with the original, at every call-out and at the return. Idle entries,
entries with a guard of their own, routines the analysis enters with more than
one set of widths and routines that start with a wait loop are left as they
are. Given `BB:AAAA` addresses nothing registers yet, it adds entries for their
drafts, which is how a porter checks a draft before rewriting it.

### Results

Over the 466 distinct routines the registry names that the analysis enters with
one set of widths (25 more need `--m`/`--x`), drafted with the committed hints,
the RAM map and the 75 coverage and call files of the movie library:

| | |
|---|---|
| D known where the routine uses the direct page | 349 of 396 (88%) |
| DB known where it uses absolute addressing | 301 of 357 (84%) |
| Work RAM in the logic by name / as a number | 2017 / 1187 (distinct in each draft, summed) |
| Drafts with no goto | 336 (72%); 577 gotos in the other 130 |
| Loops / ifs formed | 566 / 1531 |
| Emulated-register operations in the logic | none; stack pushes and pulls and 318 machine-state writes (D, DB, S, P) remain |
| Script command handlers named | 180 (179 event commands) |

**Shadow validation.** `draftcheck` registered 347 drafts in place of the hand
ports (144 entries were left: 121 idle or with a guard of their own, 23 entered
with several widths). They compiled with `-Wall -Wextra` without a
warning, and over the whole movie library (26 movies, 762693 frames) in
`--native shadow`,
289 of the 347 drafts were called, 52.0 million times. 51.5 million calls
matched the original at every call-out and at the return, 84203 were
interrupted (not compared, as for ports), and 14 mismatched, all of them in
`menu_nmi_work` at its call to `$C2:8473`: the carry of a CMP against `$4210`,
whose open-bus bits read differently natively (see below). Guards and
preconditions turned away 438811 calls to 11 drafts, 432877 of them to
`field_scroll_layers`: the analysis has the field's main loop calling it with D
= `$0100` and DB = `$00`, but after a battle the loop runs with D = `$0000` and
DB = `$7E`, having been returned to along a path the analysis doesn't see. The
guard on the D and DB the draft assumed is what kept those calls on the
original; the rest are interrupt handlers and scene routines entered with other
widths than the draft's. 58 drafts are never called by the movies.

**Ground truth.** The ports were validated call by call against the original,
so each is a known-correct target for the draft of its address.
`python3 -m tools.generate.groundtruth BB:AAAA ...` drafts routines that are
already ported and compares, in terms that mean something to a porter: the work
RAM addresses the port's text reads and writes (literals, `RAM_` names and
direct-page offsets resolved with the draft's D) that the draft also accesses,
and how many each spells by name; the port's call-outs the draft also makes;
if/else/loops/switch/goto in each; port lines with a close line in the draft
and a token similarity; and the emulated-register operations each has (`m_lda8`,
`c->x`, `m_push16`, ...). A varied set:

| Port | Accesses in both / port (draft) | Named: port vs draft | Call-outs in both / port (draft) | Structure: port / draft | Port lines close / similarity | Register ops: port / draft | Left in the draft |
|---|--:|--:|--:|---|--:|--:|---|
| `battle_multiply_8x8` `$C1:0089` | 4/4 (5) | 0/4 vs 2/5 | 0/0 (0) | - / - | 0/14, 0.42 | 6 / 0 | - | 3 unnamed |
| `battle_clear_ad9c_table` `$FD:ACEE` | 1/1 (2) | 0/1 vs 1/2 | 0/0 (0) | 2 loop / 2 loop | 0/10, 0.44 | 9 / 0 | - | 1 unnamed |
| `battle_find_due_animation` `$C1:423A` | 2/2 (21) | 0/4 vs 9/21 | 0/0 (2) | 3 if, 4 loop / 11 if, 1 else, 4 loop, 2 goto | 3/29, 0.16 | 28 / 0 | 4269 | 12 unnamed, 2 goto, 5 temps, 2 callouts |
| `menu_draw_glyph` `$C2:5E36` | 2/6 (4) | 0/7 vs 3/4 | 0/0 (1) | 5 loop / 4 loop | 6/45, 0.24 | 28 / 7 | - | 1 unnamed, 3 temps, 3 machine, 4 callouts |
| `field_compute_layer_edges` `$C0:7F9A` | 4/6 (58) | 0/12 vs 54/58 | 0/0 (4) | 2 if, 1 else, 1 loop / - | 3/67, 0.17 | 35 / 12 | - | 4 unnamed, 2 temps, 12 callouts |
| `battle_draw_shadow` `$CF:EA8A` | 11/11 (11) | 0/12 vs 10/11 | 0/0 (0) | 1 if / 2 if | 0/30, 0.14 | 30 / 0 | - | 1 unnamed, 1 temps |
| `battle_update_scroll_dispatch` `$CD:06B4` | 1/1 (36) | 0/1 vs 23/36 | 0/0 (1) | - / 4 if, 4 else, 4 loop, 1 switch, 5 goto | 0/5, 0.04 | 6 / 0 | - | 13 unnamed, 5 goto, 10 temps, 1 callouts, 3 handbacks |
| `event_cmd_if_var_const` `$C0:62DE` | 1/1 (5) | 0/1 vs 3/5 | 0/0 (1) | 4 if, 2 else, 1 switch / 1 if | 2/72, 0.22 | 52 / 0 | - | 2 unnamed, 2 temps, 1 callouts |
| `event_cmd_call` `$C0:5FB6` | 9/9 (16) | 0/12 vs 13/16 | 0/0 (0) | 7 if, 1 else, 2 loop / 6 if, 3 goto | 1/84, 0.20 | 59 / 0 | - | 3 unnamed, 3 goto, 7 temps |
| `menu_object_nmi` `$C2:015E` | 1/2 (75) | 0/4 vs 62/75 | 3/4 (6) | 6 if, 9 loop / 2 if, 1 else | 26/161, 0.18 | 85 / 18 | - | 13 unnamed, 63 io, 6 machine, 6 callouts |

What it says:

- **The memory and the calls are the port's.** 36 of the 43 addresses the
  ports' text shows are in the drafts, and where the two stop at the same
  place, all of them are (`battle_draw_shadow` 11 of 11, `event_cmd_call` 9 of
  9, `battle_multiply_8x8` 4 of 4); shadow mode already says the effects are
  the same. The differences are where the port split or joined code: it inlines a
  callee the draft calls out (`menu_draw_glyph`'s pixel routine,
  `field_compute_layer_edges`'s four edge routines), stops early and hands back
  (`battle_find_due_animation` at `$4269`), or is a dispatcher whose targets
  are ports of their own (`battle_update_scroll_dispatch`, 7 lines against the
  draft's 102 because the two targets that aren't registered are drafted
  inline).
- **The drafts are more named than the ports.** The ports these routines have
  spell work RAM by number (none of the 58 port addresses above uses a `RAM_`
  name; `game/battle/setup.c` is the one file converted so far); the drafts name
  180 of their 233 (40-93% each), and the unnamed rest is what the map doesn't
  have yet (scratch cells like the event commands' `$01D9`).
- **The text isn't the port's, and that is the point.** 0-16% of a port's
  lines have a close line in the draft and token similarity is 0.04-0.44,
  because the ports are transcriptions: 6 to 85 emulated-register operations
  each, against none in the drafts' logic. The draft is closer to the bar
  CLAUDE.md sets than most existing ports are, which is also what Phase 3 of
  PLAN.md says about bringing them up to it.
- **Control structure** matches where the port kept the original's shape
  (`battle_clear_ad9c_table`, two loops each); elsewhere the draft has more
  ifs, since it keeps the early returns and branches the port folded into one
  condition, and some gotos where loops and ifs interleave.

### Found by shadow mode, while building it

Each was a wrong draft that compiled and looked right, found in the first
shadow runs, fixed, and covered by a test on synthetic code: a value
substituted into a branch whose target still read the variable (liveness after
a branch has to include the target's); a callee's `SEP #$10` truncating Y
without being a "definition" of Y; the machine's A written whole at a call-out
while the draft had tracked only the byte it needed; a truncation lost when an
8-bit temporary was replaced by its (wider) expression; a cast rendered around
the wrong part of an expression; and busy-wait loops, which never end in native
code.

### Before the bulk: liveness, the don't-care check, the draft's hand-backs

The pilot left three things to do once rather than in every subsystem (PLAN.md,
Phase 3): liveness that sees through a saved P, a check that every don't-care
set a port declares is one the original agrees with, and the draft tool's
remaining gaps. They were done in that order, since each needed the one before.

**Liveness through PHP and PLP.** `tools/disasm/liveness.py` counted `PHP` as
reading every flag, and Chrono Trigger's subroutines save P at entry and
restore it before returning, so nearly every routine "read" N, V, Z and C, and
a draft carried all four through every routine that called one. Saving P
reads nothing by itself: a flag is live at a `PHP` exactly when it is live
after the `PLP` that pulls the byte. So for each `PHP` a walk forward counts
the bytes pushed and pulled above its byte; where every path reaches a `PLP`
that pulls exactly it, the `PHP` reads what is live after those `PLP`s, and
otherwise it stays a read of every flag:

- **S changes**: `TCS`, `TXS`, `XCE`, a push whose width the analysis doesn't
  know, or paths that meet with different depths. `LDX #const : TXS` (the
  mode loaders' new stack) and `CLC : XCE` are understood: a byte under a new
  stack is never read again.
- **Read otherwise**: the byte is pulled by something else or with other
  bytes, a return pulls it, a stack-relative access reaches it, or `TSC`/`TSX`
  copies S while it is on the stack.
- **A callee reaches it**: every routine has a `reach`, how far above its entry
  S it (or what it calls) pulls or reads the stack; 2609 of 3136 routines reach
  exactly a JSR's return address, 262 a JSL's, 228 can't be bounded (tasks that
  switch stacks, code in RAM). A call made while the byte is on the stack, to a
  routine that reaches that far, may read it.
- **Control leaves** with the byte on the stack: a return, a tail jump, an
  indirect jump, a flow out of the traced code. A call that doesn't return
  just ends the path.

Of the 669 `PHP`s the analysis traces (counted once per context; 399
addresses): 566 paired (84.6%, 321 addresses), none where S changes, 13 read
otherwise (5 addresses: the menus' task switch at `$C2:821E` copies S, and
`$C7:0A39`'s refused path pulls its P byte with a `PLA`), 87 where a callee
reaches the byte (71: calls into the menus' task switch, and the sound driver's
`$C7:0A39`, whose refused path drops its caller's frame and leaves through the
driver's common exit), 3 where control leaves (2). Routines whose entry reads
all four flags went from 424 of 3136 to 164; flag bits read at entries, summed,
from 4336 to 3671; flag bits live after returns from 5400 to 5141. The
generated blocks don't use liveness (M4's dead flags aren't built), so `python3
-m tools.generate` writes byte-identical C before and after (all 65 files), and
the gate passed unchanged (`suite OK` with `--long-routes`; trace identity,
`sst_generated_runner` and `generated_bus_diff` 0 failures). What changes is the
drafts, checked by `draftcheck` (below). The draft of a routine that starts
with `PHP` now pushes the machine's P as it is (a `PHP` reached only along a
straight line from the entry through instructions that leave P alone, where the
machine's P is still the original's), so the flags don't enter the draft at
all.

**The don't-care check** (`tools/dontcare_check.py`). Shadow mode doesn't
compare what a port declares dead, so a wrong declaration hides a difference
and ships a wrong register. Each declaration is a claim about the original at
the point `runtime/port/native.c` applies it, and the check verifies it against
liveness there:

| Declaration | Where shadow mode leaves it out | What liveness must say |
|---|---|---|
| a registry entry's `.dont_care` | where the port finishes: at its return, and at every stop | dead after the routine returns (over every routine the entry is part of: `$C1:1C55` is also the fall-through end of `$C1:1C4A`), and dead at each stop the port declares |
| `m_stop_at(s, at, set)`, `m_stop_dont_care(s, at, set)` | at the stop, with the entry's set | `set` and the entry's dead at `at`; or, when `at` is a return instruction the port carries out last, dead after that return |
| `m_jsr_dc`, `m_jsl_dc`, `au_driver_call` | at the callee's entry | not read by the callee before it returns (`Liveness.callee_reads`) |

That needed three runtime changes, so the claims mean what the check verifies:

- **Stops say where.** `m_stop_dont_care` takes the stop's address, and
  `m_stop_at(s, at, set)` sets PC and declares in one call. Shadow mode (and
  `ct_fuzz`) report a port that stops somewhere other than the address it
  declared, and one whose entry has a don't-care set that finishes without
  either returning (`m_rts`, `m_rtl`, `m_rti` now tell the runtime) or
  declaring its stop: the entry's set applies there, so the check has to know
  the address. 24 entries stop somewhere; each stop is now an `m_stop_at`.
- **A call-out's set means what the callee reads, and no more.** When a
  callee returns, shadow mode reloaded the port's machine from the original's,
  so a register the port declared don't-care at the call and the callee
  carried through came back as the original's, while in the shipping mode it
  comes back as the port's. Now a declared register the callee left as it
  found it is the port's own again after the reload (`keep_untouched`): if the
  port relies on it later, shadow mode sees what the shipping mode would. The
  claim at the call is then only that the callee doesn't read the set, which
  is `machine.h`'s documented meaning.
- **Liveness splits A.** `DC_AH` claims A's high byte alone, so liveness has
  `A` (the low byte) and `AH` apart: an 8-bit accumulator reads and writes the
  low byte, `XBA` swaps the two bytes' liveness, `TAX` with 8-bit index
  registers reads the low byte. `DC_A` is dead only when both are.

The declarations are read from the source, not run: registry entries through
the file's macros, the rest with their arguments evaluated from constants,
the function's parameters (every call of it in the same file is followed, so
the sound driver's `stop_at(s, loop, dead)` and `call_driver(...)` helpers
resolve per call), locals, the rows of a table in the file (the wait table's
address and dead set stay paired), and both branches of a condition decided
at run time. A call-out whose target is computed (a jump table's) takes its
targets from the original's call at the site. A declaration the check can't
read is itself a finding. It is a row of `tools/suite.py`'s `CHECKS`
(`--no-dontcare`), fed the call logs the shadow runs just wrote, and takes
about 20 seconds; `--tree` checks another tree, such as a `draftcheck` one.
Call-outs are also held to a second solve that trusts no target set the machine
picks at run time, which doubles the solve ("Call-outs into targets the machine
picks", below).

**What it found on the ports**, and what was done. Every finding was either
liveness being too conservative, fixed where that was sound, or a declaration
wider than the original allows, narrowed:

| Finding | Kind | Resolution |
|---|---|---|
| the sound driver's boot stops (`$C7:00AB`, `$C7:00DB`) declare `DC_AH`; liveness had A's high byte live | liveness | the value there is overwritten on every path after the `XBA`s that move it; with `XBA` swapping the bytes' liveness, it is dead |
| `$C7:0922`'s entry and the stop at `$C7:08FA` declare X and Y; liveness had them read | liveness | `$C7:09DA` carries X and Y back to callers that read them, and a call counted every caller's readers. Liveness is now solved as a function of what callers read (a summary per routine, the value at each instruction packed per register the caller may read), so a call carries only its own caller's through |
| the call-outs through `au_driver_call` declare X and Y (loop counters the port keeps in C) and `$C7:01AE`'s call to `$C7:0A39` the flags | semantics | the callee reads none of them; what the caller does after the call is the port's, now compared as the shipping mode would run it (above). `$C7:0A39`'s `PHP` byte is pulled by a `PLA` whose A is dead: pairing is now for every push, and a pull into another register reads what was pushed only where that register (or N and Z) is live |
| `menu_nmi_work`'s `DC_C` checked at its return | declaration | it never returns: it stops at `$C2:8466`, or after a call-out that changed the widths; the three stops are declared, and the carry is dead at each |
| `battle_build_object_graphics` (`$C1:1C55`) declares X and C | declaration | live after `$C1:1C4A`'s callers resume, which it returns to when `$C1:1C4A` falls into it; narrowed to N and Z |
| `input_read_joypad` declares Y and V | declaration | live at its stops (the joypad wait at `$C2:84F9`, the reset at `$C0:FF00`); narrowed. V is `$4212`'s hblank bit from a `BIT` that native time can't reproduce, and dead after the return: that is now declared at the `RTS` (`$C2:8544`) alone |
| `event_run_object_script` X and C, `field_object_in_the_way` X, `menu_run_objects` Y | declaration | live at the stops each makes after a call-out changes the widths (`$C0:5AB4`, `$C0:5BE6`, `$C2:0541`); narrowed |

Over the 26 movies in shadow mode the narrowed ports still match everywhere:
the early ports reproduce every register they declared, so none of the wrong
declarations was hiding a difference on the paths the movies reach. The one
exception proves the mechanism: with V taken out of `input_read_joypad`'s
set, 416 calls mismatched at the return on V, the hblank bit; declared at the
`RTS`, none do. The tree as this work left it: 0 findings over 284 entries with
a don't-care set, 56 stop and 45 call-out declarations (a value per helper call
and table row). The counts have grown with every wave since; the check still
reports 0.

The liveness those fixes left, from the same analysis (3136 routines):

| | Before | PHP pairing | Now |
|---|--:|--:|--:|
| routines whose entry reads all four flags | 424 | 164 | 123 |
| flag bits read at entries, summed | 4336 | 3671 | 2409 |
| flag bits live after returns, summed | 5400 | 5141 | 3968 |
| routines X is live after | 1635 | 1635 | 1032 |
| routines A's low byte is live after (A whole before) | 2553 | 2553 | 511 |
| routines with something dead after return | 2663 | 2743 | 2820 |

Pushes of every register pair the same way now (1980 in contexts): 1784
paired (1140 addresses), 3 where S changes, 60 read otherwise, 120 where a
callee reaches the byte, 13 where control leaves. The solve takes 15 s.

**The draft tool's fixes**, in the pilot's order, each on the machinery above:

- **Liveness at hand-backs.** A draft stops with `m_stop_at(s, at, set)`,
  `set` being what liveness has dead at `at`, and writes back only what is live
  there (and A's low byte wherever its high byte is, since no set leaves out
  the low byte alone). Its registry line declares what is dead everywhere it
  finishes, after the return and at every stop, and a return declares the
  rest at the return instruction, so a draft suggests declarations the check
  accepts: `tools/dontcare_check.py --tree` on the `draftcheck` tree finds
  nothing over 335 entries, 113 stops and 516 call-outs.
- **Call-out stubs set what the callee reads** (`callee_reads`) and name the
  rest don't-care, rather than everything any caller reads after the callee;
  and a `PLP` that pulls the byte the routine's first `PHP` pushed restores the
  caller's P, so it no longer makes the call "change" every register. Fewer
  inputs and read-backs mean fewer stub signatures (`$C7:0004`: 16 to 11).
- **D kept across call-outs that leave it alone.** A callee's summary of D and
  DB now takes only the returns that come back to its caller (at the depth it
  was entered with): `$C7:0A39`'s refused path, which pulls its caller's frame
  and returns further up, no longer makes D unknown after the call, and the
  rest of `$C7:0004`'s draft names its direct page (135 `m_dp` accesses to
  none).
- **The ground truth reads a port the way the check does.** Its file-local
  names (`COUNTER` for `RAM_ABS(RAM_SOUND_DRIVER_UPLOAD_COUNTER)`) resolve
  through the file's macros, a helper's parameters count as addresses only
  where the helper reads or writes through them, and call-outs are followed
  through the file's stubs to their targets.

Drafts before and after (`python3 -m tools.generate.show`, the library's coverage
and call logs):

| Routine | Flag assignments | Flag parameters | Call-out stubs | `m_dp` accesses | Logic lines | Stops declared |
|---|--:|--:|--:|--:|--:|--:|
| `$C7:0004` (m8 x16) | 140 → 11 | 4 → 1 | 16 → 11 | 135 → 0 | 893 → 690 | 0 → 7 |
| `$C7:0655` | 15 → 2 | 4 → 0 | 3 → 3 | 43 → 0 | 128 → 90 | - |
| `$C7:0A39` | 5 → 0 | 4 → 0 | 1 → 1 | - | 26 → 19 | 0 → 1 |
| `$C2:015E` | 2 → 0 | 2 → 2 | 6 → 6 | - | 115 → 109 | - |
| `$C1:423A` | 7 → 6 | 1 → 1 | 2 → 2 | - | 112 → 109 | - |
| `$C0:62DE`, `$C0:5FB6` | unchanged | | | | 33, 79 | |

The ground truth on the `$C7` port, which it undercounted:

| Port | Accesses in both / port (before) | Named in the port (before) | Call-outs in both / port (before) |
|---|--:|--:|--:|
| `$C7:0004` | 23 / 23 (0 / 43) | 97 of 97 (0 of 62) | 5 / 5 (0 / 0) |
| `$C7:0655` | 8 / 8 (0 / 5) | 13 of 13 (0 of 5) | 2 / 2 (0 / 0) |
| `$C7:0922` | 5 / 7 (0 / 4) | 11 of 11 (0 of 4) | 2 / 2 (0 / 0) |
| `$C7:0A39` | 2 / 6 (0 / 1) | 13 of 13 (0 of 4) | 0 / 0 (0 / 0) |

The draft reaches every address the port's `$C7:0004` does and calls what it
calls; where the counts still differ, the port inlines a stage the draft calls
out (`$C7:09FD`, `$C7:0734`) or reads a table through the driver's own
accessors. Its other ports read as before (`$C1:423A`, `$C0:62DE`,
`$C2:015E`, `$C0:5FB6` unchanged).

`draftcheck` over the library, with every fix: 188 drafts registered in place
of the ports (134 left as they are), 182 called, 53.1 million calls,
none mismatching, 84824 interrupted. It exports the committed tree by default;
`--rev` takes another (`git stash create` gives one of uncommitted work).

### What drafts don't do yet

These are the tool's next improvements, or its known limits:

- **D or DB unknown** in 12-16% of drafts: interrupt handlers and what only
  they reach, and bank `$FD`'s palette code, whose data bank comes from callers
  the analysis doesn't connect. Those accesses stay `m_dp`/`m_abs` offsets.
- **Names are the map's.** An address the map doesn't have stays a number (a
  direct-page cell a `SCRATCH_` name), and an absolute one it names for
  another use of the same bytes by the routine's subsystem is taken as it is;
  a name landing deep in an array (`RAM_BATTLE_PART_OFFSETS_X + 41 *
  ..._STRIDE` for `$9FEB`) is worth checking against the map's bounds.
- **I/O stays register writes**, named in comments: turning `m_write8(s,
  0x002100 /* INIDISP */, 0x80)` into `vid_set_display` is the porter's, as is
  recognising a multiply through `$4202`/`$4216` (the multiplier and divider
  are left as they are, which ports may do).
- **Operand names** come only for event commands; battle animation and enemy
  script handlers are named, but their operand reads (through a script pointer
  rather than an index register) are not.
- **Open bus.** `menu_nmi_work`'s acknowledge is a CMP against `$4210`, whose
  bits 4-6 are open bus and read differently natively; its port marks the
  carry don't-care at the next call, and liveness can't know to.
- **Structure** stays a goto where regions interleave (28% of drafts have one),
  and a `switch` over a jump table goes to its cases by label.

- **Inlined subroutines and shared shape** (from the $C7 pilot). The draft of
  `$C7:0004` is 690 lines with 49 gotos because it inlines every stage of
  `play_song`; the six copies of the header-retry and upload loops that the
  port shares as helpers appear once per site.
- **Stub signatures still vary by what is read back** after a call (the
  flags a callee may set), so a callee called from several places can still
  get a few stubs (`$C7:09DA`: five in `$C7:0004`'s draft).
- **Liveness stays conservative where S is saved in memory**: across the
  menus' task switch at `$C2:821E` and the menus' entry `$C2:800E`, which copy
  S (a `PHP` there reads every flag, which is what keeps the overflow flag
  live after the field loop's callees), and in the scene program's task lists,
  whose tasks leave through the loop's own code rather than return ("The
  tooling pass after wave 1: the analysis", below).

### Between the waves: the validator

Wave 1's reports named five places where shadow mode and the don't-care check
made a port reproduce what the original leaves dead, or broke on a port shaped
the natural way. Each is fixed in `runtime/port/native.c` and
`tools/dontcare_check.py`, under one rule: anything that lets shadow mode
compare less is shown sound (from liveness, or from the original's own
execution), and a deliberately wrong port is shown still caught. The runtime
side is tested on a synthetic program (`tests/port/shadow_test.c`, in CI), the
check's on synthetic code (`tests/test_dontcare_check.py`), and each on the
ROM below.

**A's low byte alone: `DC_AL`.** Liveness has A's two bytes apart, but only
the high byte could be declared by itself (`DC_AH`), so a routine whose callers
read A's high byte (a later 16-bit load or `TAX` takes it along) had to return
the exact low byte an 8-bit routine leaves, on every path: the battle's effect
handlers returned "what the original leaves in A" as a value, and the frame
update set A's low byte before each of its call-outs although no callee reads
it. `DC_AL` (bit 10) leaves out the low byte alone, at a return, a stop and a
call-out; with it every register byte and flag liveness tracks can be declared
on its own. Sound for the same reason as every declaration: the check maps
`DC_AL` to liveness's low-byte bit and verifies it where shadow mode applies
it, and at a call-out a low byte the callee left as it found it goes back to
being the port's own (`keep_untouched`), so a port that relies on the value it
declared dead there is compared as the shipping mode would run it. Shown: the
synthetic program's routine that returns a wrong low byte under `DC_AL`
matches and one with a wrong high byte doesn't ("A's high byte original 12
native 99"); one that declares the low byte dead at a call-out and then stores
it is caught at its return; on the ROM, `DC_AL` on `battle_sine_scale`'s entry
(its callers read the result) and on the call to `$CD:3E8E` (which pushes the
bank A holds) are findings. `--list` now says, for every declaration, what
else liveness has dead there (`could add`): 312 declarations could add
`DC_AL` before the ports below adopted it, 291 after (295 once merged with
the fuzz and timing pass's new entries).

**A stop's own set: `m_stop_at_own`.** An entry's set applied at its returns
and at every stop it made, so one early exit to code that reads a register the
callers don't shrank the whole entry's set: the frame update's demo exit to the
reset code at `$C0:FF00`, which reads Y and V, kept both out of a set that is
otherwise dead after the return, and `input_read_joypad` declared V at its
`RTS` as a stop to get around it. `m_stop_at_own(s, at, set)` (and
`native_own_dont_care_at_stop`) makes `set` the stop's alone: shadow mode and
`ct_fuzz` leave out exactly that set there, and the check verifies it alone at
`at`; the entry's set then applies only at returns and at stops that aren't
their own, and is checked at each (`native_finish_dont_care` is the one rule
both runtimes use). Nothing is compared less than it is declared, and every
set is checked where it applies. Shown: on the synthetic program, a wrong Y at
an own stop is caught and the same stop declared with `m_stop_at`, which adds
the entry's `DC_Y`, hides it; on the ROM, `scene_run_sequence` with D added to
its entry (dead after its return, read at the sound wait) and a wrong D at the
wait matches all 32854 calls of `endings-beyond-time` as a shared stop, which
the check rejects ("where liveness has D read"), and mismatches 5142 as its own
stop, which the check accepts; an own set with V at `$C0:FF00` is rejected.

**Call-outs compare exactly what the callee reads.** With `DC_AL`, a call-out's
set can be the complement of `Liveness.callee_reads` bit for bit, and the check
verifies it there as before. Making the runtime leave out what no callee reads
by itself, from a liveness table built with the generated code, was considered
and not done: it would put the ROM's analysis into the runtime (and make what
is compared depend on the build's call logs), and it would move the claim out
of the source, where the declaration is reviewed and `--list` shows what else
could be declared.

**Stops and calls at a loop's head.** Shadow mode took a stop or call-out as
reached the first time the original passed its instruction with the same S, so
a port that ran several passes of an interpreter loop and stopped at its head
was compared against the first, and ports worked around it (the scene
sequence interpreter handed the rest of a run back as a call-out from its
entry's own `JSR` site, which is compared at the same first pass and was only
right when no command had run in that stretch). Now every pass is compared,
and the first where the machines are the same counts (`reach_point`). It can't
accept a wrong pass: the comparison covers everything the original's code can
observe except what is dead there (the declared sets, checked against
liveness, and the stack below S) and what changes with time rather than code
(HDMA's registers, the SPC700's port clears, the OAM reload), which the first
pass left out as well. What the original does from a pass is decided by the
machine at that pass, so from a pass where the two are the same the original
goes on exactly as the shipping mode goes on from where the port stopped:
whichever pass it is, what follows is what the original did. A port that is
the same at no pass is reported with the first pass's difference, and the
last's, once the original leaves the point's frame, when an interrupt comes
(its handler runs on the original only, so a later pass can't be compared:
reported as a mismatch, as the first pass's difference was before), after
65536 passes, or when the run ends with it still waiting (`native_failed`).
So nothing the old rule reported as a mismatch passes now unless a later pass
matched exactly. A failed pass costs a comparison per later pass until then.
`ct_fuzz` does the same over the original's passes. Shown: on the synthetic
program, a stop after two passes of a loop and a call-out from a loop's head
after two inlined passes match (under the old runtime, every call mismatched,
"X original 0 native 2"), a stop whose memory matches no pass is caught ("and
at 2 later passes, then the original left"), and in a loop longer than a
frame a port matching no pass is reported at the NMI. On the ROM,
`scene_run_sequence` made to stop at its loop's head `$C3:0B03` after one
command: in `title-start` 235 of its 1500 calls mismatch under the first-pass
rule and none now; the same with the script pointer one byte off, 238 mismatch
("... at the last: WRAM[0A20] original 3B native 3C").
`scene_run_sequence` stopped at `$C3:0B03` in place of the workaround, until
the fuzzer as it is after the fuzz and timing pass found 4793 of 20000 states
where no pass matched, and the comparison was right not to take one: a command
the port gives up on (one no script has) has already written the scratch cell
`$F0` and changed the flags, which the original does only once it runs the
command, so at the loop's head, on every pass, the port's machine is one the
original never has. The port went back to the call-out (which the fuzzer
compares only at the return, after the original has run the command itself);
it now hands such a command to the original's code for that command, entered
as its dispatch enters it (REIMPLEMENTATION.md, "Phase 3, the scene program: the staff
roll").

**An entry on another port's path.** An entry the original reached while it
was catching up with a native routine started a comparison of its own, nested,
and only the innermost comparison was checked, so one still running when the
original got to the point the enclosing routine waited for took that point
for itself (the same call, typically), and the enclosing comparison failed:
the $C7 pilot's `play_song`, and `$C3:0C28`, where the scenes' sound wait goes
on and the store command's path runs through. The runtime handles it rather
than the check rejecting it: at each step the outermost routine waiting at the
original's point comes first (`settle_nested`). The nested comparisons are
checked there first, innermost first, as before, and one that finishes or goes
on (its own point is here too, or its callee unwound past it) is dealt with as
it was; any other is set aside, counted as `abandoned` with a note naming the
enclosing routine, and the enclosing one is compared (a difference a set-aside
one had shown stays a mismatch, and one waiting for a later pass is reported
then). That compares less only
for a call the shipping mode never makes (there the enclosing routine runs
natively and the entry isn't called at all), and the comparison it gives way
to covers everything the stretch did. Nothing that matched before is set
aside: nested comparisons that end inside the stretch, the common case (a
helper the port inlines and the burndown credits as an entry), are compared as
they were, and those that outlasted it made the enclosing one fail. Over the
library the shadow runs' counts are the same as under the old rules for every
entry (calls, 64558726 matched, 64158 interrupted, 489 abandoned, none
mismatched), so both this and the loop heads change nothing that passed. A
first version didn't start an entry inside another's stretch at all, which is
the shipping mode's rule, but that dropped 17.3 million of those comparisons
(the battle's per-object drawing routines and the menus' glyph and number
routines, all inlined by their callers) and moved four entries' timing past
the timing gate's limits, so it was replaced. A check could only reject such a
registration by knowing which of the original's instructions each port stands
for, which the burndown only estimates, and it would forbid the registrations
porters want. Shown: on the synthetic program, an entry registered in the
middle of another's path took the outer one's call-out (all 250 of the outer's
calls mismatched, "original returned without the call"); now both match, the
inner one set aside once per pass inside the outer's stretch and compared at
its own calls, and a callee the outer inlines, registered as well, is still
compared at every call (none abandoned). On the ROM, `$C3:0C28` registered for
the demonstration: under the old rule `events-jetbike-race` reports
`scene_run_sequence: original returned without the call to C70004`, the scene
agent's failure, and `endings-beyond-time` 8 such (2 of `scene_run_sequence`,
6 of the new entry's own); now none, and the new entry is compared at 26 of
its 35 calls there (1 of 2 in the race), the rest set aside where they
outlasted a run's call.

**A reader fix.** The check read a table's struct with two fields in one
declaration (`uint16_t flag, copy;`, `battle_run_queued_scripts`' table) as one
field, which shifted every column after it, so a declaration taken from such a
table would be checked with the wrong address or set; it now reads every
declarator (`struct_fields`, tested on synthetic code).

**The ports that adopted them**, each changed only as far as the declarations
go, fuzzed again and matching over the library (PORTING.md's rows):
`battle_frame_update` declares Y and V dead after its return, its demo exit
its own set, and its call-outs `DC_AL | DC_X | DC_Y | DC_FLAGS`, so the low
byte it set before each call is gone; the fifteen effect handlers, called by
themselves, and `battle_place_formation` declare `DC_AL` and no longer
reproduce what the original leaves in A's low byte; `input_read_joypad`
declares Y and V after its return and its two stops their own sets, in place
of its stop at the `RTS`. (`scene_run_sequence`'s stop at its loop's head was
taken back, above.)

### The tooling pass after wave 1: the analysis

Wave 1's reports (above) named what the analysis side cost the ports:
liveness keeping every register live after bank C6's routines and carry and
overflow live after the field loop's callees, a burndown that credits nothing
a port inlines, and drafts that name scratch cells after unrelated variables.
Before wave 2 takes on the three big banks, each was fixed where it could be
made precise without claiming anything the original doesn't show.

**Liveness through indirect jumps** (`tools/disasm/liveness.py`). A routine an
indirect `JMP`/`JML` reaches counted everything as read after it returns, and
the jump itself used only a hint's targets, not the ones derived from data.
Now the jump goes to every target the analysis follows; a `JMP (abs)` after a
`PER` is a call that returns to the `PER`'s address, any other a tail jump
whose targets return for the routine making it, so their callers' liveness is
that routine's. That holds only if nothing else reaches them, so a routine
keeps everything live where some site that reaches it has a target set nobody
argued complete (neither a `derive` nor a `bound` hint: a movie-only list could
have other targets from anywhere). All eight indirect jump sites in the ROM
are argued, so all 176 routines they reach are now precise, none kept live;
the count is `Liveness.indirect_entries`. After the C6 routines the menu object
scripts reach, X and the carry are what is read.

**The field loop's carry and overflow.** They were the analysis being coarse,
not the loop reading them. Following the reads back from the loop found two
causes. The sound driver's entry `$C7:0004` saves P and X (and the rest) and
calls `$C7:0A39`, which on its refused path pulls its own return address and
runs the entry's exit: its saves "reached by a callee" counted as reads of
everything, for every routine that sends a sound command. A push a callee
reaches is now followed into the callee: where every path either returns
normally or pulls the byte with a pull of its size and then only pulls the
rest of the caller's frame and returns as the caller does, the push reads what
is live after that pull, which is what the caller's callers read. A return
below the depth a routine was entered at is a return through its caller's
frame, and its liveness is carried apart from an ordinary return's (packed
values have a second set of segments for it), so what follows the escape isn't
merged into the callee's other returns. The second cause was the analysis
walking every context's width variants at an instruction: `$FF:F9C4` runs its
`PHX` with 8-bit X from one caller and 16-bit from another, which read as
depths that disagree, so its reach was unbounded and every push around a call
to it read everything. Each context now runs the widths it was traced with.
What stays: the overflow flag, which the menus' entry `$C2:800E` saves with
`PHP` and then copies S into `$0920` for a stack of its own (`TSX`), which no
pairing follows (docs/TODO.md). `RTI` now reads no flag (it pulls P).

**What turned out wrong, and was fixed.** Three places where liveness was *less*
careful than the code: a `JSR (abs,X)` whose table is filled at run time
(count 0, every target derived from data, as the scene program's task lists at
`$7E:3998`, `$7E:39CF` and `$7E:37F6` are) called nothing at all, so the task
loop read nothing its tasks read and the tasks' callers' liveness never
reached them; the derived and hinted targets of any call table were left out
of what callers read after them; and a `PER` then `JMP (abs)` was taken for a
tail jump, dropping the code after it (it happened to return to an `RTS`). The
task loop's register saves, which tasks leave through `JMP $39A7` without
returning, were "paired" because the tasks weren't known; they are now "a
callee reaches it", and a return through a caller's frame counts what is read
after it. With the tasks known, five declarations in `game/` were wider than
the original allows, and were narrowed, the ports changed as little as that
needed: the scene sprite lists `$C3:0D0E`/`$C3:0D5E` now leave Y (the next
free entry) and A (the loop's last value) as the original does, and
`$C3:0DBC` Y, since the scene task loop carries them into the next task, and a
task reached the field's start (through the reset), whose menu call reads Y;
`$C3:094C`'s Y was already the original's and is no longer declared; the
sequence interpreter's call to `$7E:37F3` passes its carry, which a task's
chained `ADC` reads.

| | Before | After |
|---|--:|--:|
| routines everything is live after (all 3136) | 209 | 34 |
| of them: entered by an indirect jump | 175 | 0 |
| register and flag bits live after returns, summed | 14677 (flags 3968) | 11731 (2192) |
| bits read at entries, summed | 11895 (flags 2409) | 10255 (1324) |
| routines A's low byte / X / the carry / the overflow is live after | 511 / 1032 / 1296 / 1884 | 298 / 850 / 998 / 800 |
| routines with something dead after return | 2820 | 3092 |
| bank C6 (41 routines): everything live after | 34 | 0 |
| bank C6: A / Y / any flag live after (routines) | 35 / 37 / 36 | 1 / 0 / 35 (the carry) |
| `$FD:C2C1`: live after it returns | AH X V C DB D | AH V DB D |
| `PHP`s paired / a callee reaches it (per context) | 567 / 87 | 579 / 75 |
| pushes paired through a callee that leaves the caller's frame | - | 12 (`$C7:0140`-`$C7:0149`) |

(Pushes of any register counted per context moved from paired to "a callee
reaches it" in 145 places, every one around the scene task loops' calls, which
were paired only because the tasks weren't known, and the other way in 63.)

**Why it is sound.** Everything the check now accepts that it rejected before
is dead on every path the analysis knows the original takes: a tail jump's
targets return exactly where the routine making the jump returns, and are
reached from nowhere else unless a site whose targets aren't argued complete
reaches them (then nothing changes); a pull in a callee is followed only where
every path from it pulls the rest of the frame and returns as the caller would,
so what is live after it is what the caller's callers read; an instruction runs
in a context only with the widths the analysis traced there; and `RTI` replaces
P. The assumptions are the ones liveness already made for `JSR` targets (that
the analysis sees every caller; the call logs back it up for the movies'
paths). Checked three ways: `tools/dontcare_check.py` finds nothing on
`game/` as the pass left it (417 entries, 67 stops, 73 call-outs); a
declaration the new liveness allows and the old one refused (`menu_object_read_mode` declaring
`DC_A | DC_Y | DC_N | DC_V | DC_Z`) passes, and one that is wrong (the same with
`DC_X`, which its callers read) is still a finding; and the drafts, which are
made from liveness, match the original over the library (below).

**Drafts before and after.** `tools.generate.draftcheck` over the library (26
movies, shadow mode), with the committed tools and with these: 194 drafts
registered before, 197 after (three entries the analysis enters with several
widths now pick one, below); 166 and 169 called, 39.09 and 39.10 million calls,
none mismatching either time (84819 and 84841 interrupted), and the don't-care
check finds nothing in the drafts' own declarations (471 entries, 124 stops,
519 call-outs). The drafts the fixes were for: the C6 routines' headers went
from "callers read A X Y N V Z C" to "X C" (`$C6:E237` 3 flag assignments to 1,
registering with `DC_A | DC_Y | DC_N | DC_V | DC_Z` where it had 0), and
`$FD:C2C1`'s from "A X V C" to "A V", its logic losing the carry (12 mentions to
none) and X from its result. The gate passes (`suite OK` with
`--long-routes`), and the strict player builds warning-free.

**The burndown** (`tools/burndown.py`) credited a port with its routine's code
only, so every helper a port inlined had to be a registry entry to count, and
it read the registry with a pattern that missed its macro rows (the 180 event
commands, the field uploads). It now reads the entries as the don't-care
check does, and follows a call into its callee where the port inlines it:
where the port makes no call-out at that call, by the call-outs its functions
make, read through the file's helpers, tables and parameters the way the
don't-care check reads declarations. The original makes that call during the
port's call and the port doesn't, so the port does its work, which shadow
mode would otherwise show. It doesn't over-credit where the reading can't be
trusted: a port with a call-out (or a PC it sets) the check can't read, or
one whose site reads as something that isn't a call (a table whose struct
declared two fields in one declaration came out shifted, which the
validator's pass has since fixed: "Between the waves: the validator"),
credits no callee, and neither does a call past a place the port hands back
(an `m_stop_at`, a PC it sets), since the original may be making it; 22 ports
credit none for those reasons, among them the two that would otherwise claim
50 KiB between them (`battle_run_queued_scripts`, `battle_main_loop_body`).
Idle entries credit none either. Per bank (the wave-1 banks and the three big
ones):

| Bank | Before | The macro rows | And inlining (now) |
|---|--:|--:|--:|
| `$C0` | 9.6K, 84% left | 18.3K, 69% | 18.7K, 68% |
| `$C1` | 10.0K, 84% | 10.0K, 84% | 10.7K, 83% |
| `$C2` | 5.2K, 91% | 5.2K, 91% | 5.6K, 90% |
| `$C3` | 3.0K, 25% | 3.0K, 23% | 3.5K, 10% |
| `$C6` | 1.1K, 68% | 1.1K, 68% | 1.1K, 68% |
| `$CF` | 6.3K, 7% | 6.3K, 7% | 6.4K, 7% |
| `$D1` | 4.8K, 17% | 4.8K, 17% | 4.8K, 17% |
| `$FD` | 8.1K, 47% | 8.1K, 47% | 8.1K, 47% |
| total | 60.1K, 78% | 69.0K, 74% | 71.6K, 73% |

`$C0` moves most, and not from inlining: 197 of its 249 entries are macro rows
the old pattern didn't see. `$C3` drops from 25% to 10% left with the edge
sequence `field_rasterize_quad` inlines (`$C3:0F35`), the frame work's
controller check (`$C3:0A8E`) and song update (`$C3:09A4`) credited; its
middle column also counts the targets of `$C3:0F32`'s indirect jump, which the
walk now follows. `$FD`, `$CF` and `$D1` barely move because wave 1 had
already registered what their ports inline, to get it counted.

**Draft names** (`tools/generate/names.py`). A routine's subsystem was its
bank's, and banks `$C3`, `$C6` and `$FD` serve several (and `$FD` had none,
so any subsystem's name was taken). In those banks it is now the one every
caller comes back to, through the call sites, call logs and followed indirect
jumps (none where they differ or there is no caller, and then only `core`
names it). A direct-page access is named only by a variable the map puts on a
page with that D: of the routine's own subsystem, or of any where the page is
its own subsystem's (field code takes the event engine's cells on the field's
page, event commands on the field's page don't take the field's). Any other
cell gets a plain name, `SCRATCH_01DB`, which the draft defines and undefines.
Over the drafts of every registered routine (194 before, 197 after): 1016
distinct names before, 990 after, 261 scratch names, and 713 addresses left as
numbers before against 466 after. The two that went wrong: `event_cmd_call`
(`$C0:5FB6`) wrote `RAM_FIELD_SPRITE_RANGE_NEXT` for its `$DB`-`$DF` and
`0x7E01D9`/`0x7E01E1`/`0x7E01E3` for the rest, and now has `SCRATCH_01D9` to
`SCRATCH_01E3`; `menu_object_read_mode` (`$C6:E237`) wrote
`RAM_SCENE_FRAME_READY` for the menu's `$00` and a number for `$7E:1BA9`, and
now has `SCRATCH_0000` and `RAM_MENU_SCENE_MODE_BITS` (its subsystem is the
menus', its callers' ). Nine drafts lost names: `$FD`'s
`battle_clear_b328_table` a menu name it had taken for want of a subsystem,
and the C6 irises and motion routines and `$C3:0DF3` (which nothing calls) the
scene's circle and scaler cells, which they use on the menu's page
(docs/TODO.md). A routine the analysis enters with several widths is drafted
with the registry precondition's, then the widths the coverage saw it run with;
if several remain, `show` prints one draft for each rather than stopping.

### Page kinds: cells on a page the caller chooses

The accessor shape scene's rewrite found missing, built before `event`'s
(PLAN.md, "Phase 3a"). A page in `re/ram.toml` either has a fixed `d` or, as a
page kind, lists the `instances` its code runs on -- named pages, arrays of
pages, or both -- and the variables on it take which instance a port means
(DISASSEMBLY.md, "The accessors"). `tools/ram_map.py accessors` writes the
handle types into `game/vars.h`, the per-instance addresses into
`runtime/port/vars_ram.c`, and `runtime/port/vars_pages.h`, a
`<page>_page_of(d, &handle)` per kind for the entry wrappers, which refuses a D
that is no declared instance and counts it.

**The measurement.** `ct_dev --page-report` counts, per hand port, the D each
call was made with, before the guard; `tools/suite.py` passes it, so every gate
run's shadow logs hold the evidence. Over the 26 movies:

| Page | Routine | D at entry (calls) |
|---|---|---|
| `scene_math` | `scene_multiply` `$7E:31BF` | `$0000` 146235, `$0A00` 23842, `$0B00` 1920 |
| | `scene_divide` `$7E:339F` | `$0000` 20115 |
| | `scene_divide_8_8` `$7E:32E2` | `$0000` 16225 |
| | `scene_multiply_signed` `$C3:094C` | `$0000` 21120 |
| | `scene_run_sequence` `$C3:0AFF` | `$0A00` 61680, `$0B00` 18316 |
| | `scene_script_wait_sound_port2` `$C3:0C2A` | `$0A00` 5041, `$0B00` 102 |
| `menu_record` | `menu_draw_record_sprites` `$C2:F871` | `$1800` 3744, `$1840` 3497, `$1880` 3545, `$18C0`-`$1980` 1868 each, `$19C0` 1259 |
| (input) | `input_read_joypad` `$C2:84EC` | `$0000` 453397, `$0100` 195982, `$0D00` 35169, `$1E00` 1596, `$2100` 246, `$1D00` 173, `$0300` 102, `$0500` 1 |
| | `input_map_buttons` `$C2:8545` | `$0000` 651497 |

So `scene_math` is the scene's own page and the two channel pages, which is
also every page the code allows (the attract program has two channels,
`scene_channels`); `$0B00` is reached only in `endings-beyond-time` (multiplies
and sequences) and the race (sequences). The divides and the signed
multiply are only ever called on `$0000`, but their guards accept either
channel, as the predecessor's did. `menu_record` is all eight records of the
table at `$1800`, every one reached (all eight in `menus-end-of-time`).

**The controller cells are one set of bytes.** The map had them on a `caller`
page with no D, from `input_read_joypad`'s entries on five different pages.
Measured, it is entered with eight -- and it sets D = `$0000` itself
(`PEA #$0000 : PLD` at `$C2:84EC`), and `input_map_buttons`, which works on the
caller's page, runs only from `input_update` after it, on `$0000` in all 651497
calls. The event engine's input conditions read the same cells at `$00F2` and
`$00F8` through the data bank. They are on the fixed page `input`, D =
`$0000`, and their accessors are ordinary ones (`input_held(g)`); the menus'
epoch screens and `core/input.c` keep `game/ram.h`'s form until their rewrites.

**Scene's scratch on the generated shape.** `scene_math_scratch` is the
`scene_math` kind's 16 bytes at `$F0`; `scene_channels` lost its `math_scratch`
field, which named the same bytes on the channel pages. `scene/scratch.c` keeps
only the widths the helpers use (8, 16 and 32 bits, byte by byte, low first, as
before), and its hand mapping from D to a page -- `scene_page_of`, `ScenePage`
and the dispatch between two accessors -- is deleted: the guards call
`scene_math_page_of`, the sequence interpreter takes its channel from the
handle's index, and the ports name the scene's own page `scene_math_scene()`
and a channel's `scene_math_channel(ch)`. The per-entry shadow totals over the
26 movies (calls, matched, mismatched, interrupted, skipped) are identical to
`main`'s for every hand port, not only the scene's, and no run printed a `page
refused` line. `tools/fuzz_compare.py`, predecessor against rewrite on seed 1,
with channel-page variants added to `tools/fuzz_routines.toml` for the
arithmetic and the interpreter and a variant on a page that is no instance:

| Routine | States | Mismatched before | after |
|---|--:|--:|--:|
| `scene_multiply` | 20000 | 0 | 0 |
| `scene_multiply` (channel, D = `$0B00`) | 20000 | 0 | 0 |
| `scene_multiply` (other page, D = `$0A80`) | 20000 | all refused by the guard | all refused by the guard |
| `scene_multiply_signed` | 20000 | 0 | 0 |
| `scene_multiply_signed` (channel) | 20000 | 0 | 0 |
| `scene_divide` | 20000 | 0 | 0 |
| `scene_divide` (channel) | 20000 | 0 | 0 |
| `scene_divide_8_8` | 20000 | 0 | 0 |
| `scene_divide_8_8` (channel) | 20000 | 0 | 0 |
| `scene_find_divisor` | 20000 | 0 | 0 |
| `scene_write_matrix_code` | 20000 | 0 | 0 |
| `scene_transfer` | 200 | 0 | 0 |
| `scene_race_begin_tile_uploads` | 20000 | 0 | 0 |
| `scene_run_sequence` | 20000 | 0 of 14061 compared | 0 of 14061 |
| `scene_run_sequence` (channel 1) | 20000 | 0 of 14049 compared | 0 of 14049 |
| `scene_script_wait_sound_port2` | 20000 | 0 | 0 |

The accessors read and write the same bytes in the same order as the hand
mapping did, so no `cost_milli` moved.

**Refusals are visible.** Each `<page>_page_of` counts the D values it turns
away; `ct_dev` prints `page refused: <page> <count> (last D $XXXX)` for a kind
that refused any, and `tools/suite.py` fails a movie whose log has one. That is
the check the scene rewrite did by hand, comparing every entry's shadow count
with its predecessor's movie by movie, after a guard that assumed one page
turned away 5412 of the race's multiplies. `tests/port/vars_test.c` checks the
mapping and the accessors on a machine with no ROM.

**What it unblocks.** `event`: the event engine and its objects are on the
fixed field page (`$0100`), not a page kind, so the only cells it needed were
the input conditions' (`event/input.c`), which now have accessors. `menu`: the
record page, for `objects.c`'s record sprites (whose wrapper's
`menu_record_page_of` gives the record). `core`: `input.c`'s reader and mapper.

### The accessor bounds checks

The generated backing computed `base + index * stride` with no bound, so an
index past a variable's `count` reached whatever came next in work RAM, and
worked only because the storage is still the original's layout. Code-reading
reviews had found four such aliases; nothing found them otherwise. Every
indexed accessor in `runtime/port/vars_ram.c` now checks each index -- the
element against `count`, a buffer's byte against its size, a table's row and
column, a page kind's instance -- and records a violation without changing the
access, so shadow mode compares the same bytes (DISASSEMBLY.md, "The
accessors"). The check is compiled only with `CT_ACCESSOR_CHECKS`: the backing
is built twice (`ct_vars_checked` for `ct_dev`, `ct_fuzz` and `vars_test`,
`ct_vars` for `ct_play` and `ct_drive`), and `game/` is compiled the same
either way. `ct_dev` prints an `accessor out of range` line per variable, and
the gate fails a movie that prints one; `ct_fuzz` prints the same after its
counts, and `tools/fuzz_compare.py` lists them by routine. The deliberate
stray accesses have a sanctioned path: a variable declares `unchecked = "<why>"`
in the map and gets an `<name>_unchecked` pair with no check, which marked
scaffolding calls; `game/wram.h` has no count and isn't checked.

**What the library found.** Over the 26 movies, before any fix, three
variables' families went out of range, in 26, 12 and 2 movies. None was a
map size error, and none traced to generated code or the runtime (which the
checks don't cover: only the accessors are checked).

| Variable, index (bound) | Where | Movies, accesses | What it was | What was done |
|---|---|---|---|---|
| `field_dialog_shading[100]`, `[101]`... (count 100) | `field_fill_dialog_shading` `$FD:D52D`, run by `$FD:C124` | all 26, 448-3696 accesses each | A hidden alias. The copy for table set 1 is a 256-byte block move from the shading's start into the copy, which lies right after the shading's 100 lines, so its lines 100-127 are read from the copy's own first 28. The port read them as "shading lines 100-127". | Made explicit, not stray: `copy_dialog_shading` reads the copy's own lines past the shading's. Byte-identical; the per-entry totals don't move. |
| `field_sprite_records[at 833]`, `[at 836]` (832 bytes) | the 4-sprite placing `$C0:B788`/`$C0:B8CA`, run inline by the draws `$C0:B271`, `$C0:B309`, `$C0:B701` | 12, 27-837 accesses each | The known stray (`leave_stray_fifth_sprite_y`): an object above the screen gets a fifth sprite's Y, one record past its four, which for the last object's records is past the table, in the objects' high bits. A second read the review hadn't named was found with it: the placing's entry leaves the overflow of that same fifth sum in V, reading the fifth record's Y offset. | The deliberate stray kept, through the new path: `field_sprite_records` declares `unchecked` in the map, and both the write and the V flag's read go through `stray_fifth_y_offset` and the `_unchecked` pair. |
| `field_tile_properties`, `_upper_attributes`, `_lower_attributes`: `[column 64]`, and `[row 255]`/`[column 255]` (64x64) | the event commands that put an object on a tile `$C0:4B74`, `$C0:4C27` on their own and inline under the openings `$C0:58DE` | `endings-beyond-time` 2 commands, `events-prison-escape` 18 | A hidden alias, on calls the game makes: a script puts an object that follows the map at tile column 64, or at (255, 255) to take it out of the way, and the original layers its sprite from the tables' neighbours (another row's tile maps, the other attribute table). | A guard (`tile_on_the_map`, in the rows' checks) leaves those commands to the original: 20 real calls, an unported path in TODO.md. `$C0:4B74` goes from 1013 matched to 1004 matched and 9 skipped, `$C0:4C27` from 520 to 519 and 1; the 10 run inline are handed to the original's handler through the table. |

With those, the gate passes with the checks on, and no other entry's shadow
totals move (798 entries compared with `main`'s). The sprite records' new
bound needs each object's layout, which the build's walk to see whether every
draw is its own now reads once and hands to the check and to the records'
work, rather than reading it again: an extra timed read there had moved three
sprite entries past the timing gate's drift limit (+0.7% to +1.3%).

**What fuzzing found.** `tools/fuzz_compare.py` over all 525 fuzz entries
(`main`'s `ct_fuzz` against this one, 5000 states each, then the event and
field subsystems at full length) found no new mismatch, and listed the
accessors random states drive out of range. The clear-cut ones got guards,
each refusing only what the game never makes, and the fuzz entries' domains
were narrowed to match (so the tight variants compare as many states as
before, and all match; the loose variants show the refusals):

| Routines | Out of range | Guard |
|---|---|---|
| the story commands `$1C`, `$53`, `$54`, `$56`, `$58`, `$59` (`$C0:654E`, `$C0:693D`, `$C0:6964`, `$C0:69A6`, `$C0:69C4`, `$C0:69EB`), and the engine running them | `event_memory` (512 bytes) at any 16-bit offset | `story_holds`: the offset's byte or word inside the story memory. The map was right: every event script the ROM can load (`derive.event_scripts`) gives these commands an offset below `$1EE`. |
| the follows `$B5`, `$B6` (`$C0:3546`, `$C0:354F`) | `field_object_*` up to `[236]` (count 64) | the other moves' `can_look_its_way`: the search for an object in the way runs from the object count down, which random states make past 64 |
| `field_unpack_tile_maps` `$C0:A521` | `field_layer_maps`, `field_tile_*` columns past 64 | `fits_the_tables`: each layer at most 64 tiles each way |
| the sprite records' draw, update and placing (`$C0:B309`, `$C0:B701`, `$C0:B788`, `$C0:B8CA`, and the build's own-draw check) | `field_sprite_records`, `field_sprite_frame_data` at offsets to `$7FFF` | `records_in_the_tables`: the object's records, for its layout, inside the records (it was: inside bank `$7F`) |

Three families are left, listed in TODO.md: the tile ahead of a moving object
off the map (the event moves and `field_start_step`), the line effect tables'
builders past a table's 87 bytes, and the event engine reading the script past
its buffer (widening the off-the-script hand-over mismatched on the openings'
path at `$C0:5900`, so it waits).

**Cost.** Measured a movie at a time, the builds alternating: `gato-battle`
in shadow mode took 41.8 s with `main`'s `ct_dev`, 42.2-42.3 s with the checks
and 41.9 s with this tree linked without them (about 1%);
`battle-golem-twins-gameover` and `fair`, with the suite's own arguments, and
`fair` four at a time, were within their run-to-run noise either way. The
whole gate with `--long-routes -j 5` took 31m38s against `main`'s 32m17s run
straight after it, most of it the routes, which run `ct_drive` and have no
checks. Its shadow movies took 393 s against 317 s in those two runs (and 398 s
against 327 s in an earlier pair); the controlled runs above don't reproduce
that gap, so it is left as an open question in TODO.md rather than put down to
the checks.


### Call-outs into targets the machine picks

Scene's rework declared A dead at the sequence interpreter's call to the scene
program's routines (`$C3:0C58` to `$7E:37F3`); liveness agreed, the don't-care
check passed it, and fuzzing found the callee reading A in 60 and 64 states of
its two variants. The analysis did see the callee: the program is a
`[[ramcode]]` image, traced, and `$7E:37F3` is `LDX #0 : JSR ($00F0,X) : RTL`,
a call through the word script command `$A0` stores at `$00F0`, whose 33
targets `derive = "seqscript.call"` finds in every script the game can run.
None of them reads A, Y or V, so for the game's own scripts the declaration was
right. What liveness couldn't see is that the target is the caller's to
choose: the port stores the script's operand at `$00F0` itself, and a script
the fuzzer makes up (its script pointer lands anywhere in the program) names
any address in bank `$7E`. Instrumenting the fuzzer to print `$00F0` at the
call showed the two states that still mismatched calling `$28F6` and `$14A4`,
routines no script names. The promise that unknown control flow keeps
everything live held; this transfer wasn't unknown, it was followed with a set
that was complete for the original and not for a caller that picks the target.

**The analysis.** `Liveness.dynamic_transfer` names the transfers whose
targets the machine decides at run time: a jump table or pointer in work RAM,
a call whose operand the program writes, a table whose extent comes from data
(29 sites; DISASSEMBLY.md, "Liveness"). `Liveness(strict=True)` solves with
each of them keeping everything live, and `dynamic_sites` says which of them a
routine reaches. Liveness as the analysis uses it elsewhere is unchanged: the
generated code, the drafts and `liveness.json` still follow the argued sets,
which are sound for the original.

**The check.** A call-out's set must be dead in both solves; a call whose
target the port computes at a site that is itself such a transfer counts as
reading everything. A set only the trusting solve supports is a finding that
names the transfer, unless the comment beside the declaration carries a
`dc-evidence:` note for a fuzz run that scrambled the set and found nothing
(PORTING.md, "Declarations liveness can't prove"; the check verifies the note's
form, that the routine it names is a registered, listed fuzz entry that
reaches the declaration, at least 100 scrambled call-outs, and 0 mismatched,
and flags a note liveness doesn't need). The second solve takes the check from
about 38 to 65 seconds.

**The evidence.** Fuzzing hid the scene bug until the port stopped setting A:
a port that passes the original's value never shows a wrong declaration. So
`ct_fuzz --poison-dc` scrambles what each call-out declares dead, each register
to a value other than the port's, and runs the callee twice from the same
machine (clean on a copy, then scrambled), so what the callee left alone can be
given back to the port as shadow mode does; comparing the scrambled value
alone, as `keep_untouched` does, restored a byte the callee happened to write
with the scrambled value in 7 of 1400 states (a false mismatch).
`--poison-site BBAAAA` scrambles one site's calls, and the run reports how many
call-outs it scrambled in the states it compared.

**The audit.** Over `game/` (589 entries with a set, 89 stops, 348
call-outs), the old check had 0 findings and the new one 2, both the scene's:

| Declaration | Strict solve | Fuzzed (`--poison-site`, 2000 states) | Resolution |
|---|---|---|---|
| `script.c`, `$C3:0C58` to `$7E:37F3`: X Y N V Z | Y and V read past `$7E:37F6` (`$00F0`) | 2 of 1390 compared mismatch, both in routines no script names | Y and V passed: the port leaves Y as the caller's and keeps V (`note_flags`), as the original has them; X N Z stay, set by the `LDX #0` |
| `frame.c`, `$C3:08F7` to `$7E:39BE`: A X Y N Z | Y read past `$7E:39CF` (the task list at `$0940`) | 0 of 987, but only 4 call-outs scrambled | Y passed: the port never touches it before the call, so it is the caller's, as in the original |

Both are then proven by the strict solve, with no note needed. Fuzzed with the
scene's constraints (`tools/fuzz_routines.toml`, seed 1, 20000 states), with and
without scrambling: `scene_run_sequence` 14061 matched and 0 mismatched (5745
call-outs scrambled), its channel variant 14049 and 0 (5734), and
`scene_frame_work` 10023 and 1 (20 scrambled), the one being the predecessor's
residue the scene rework recorded, the same state with and without. Nothing in
`game/event/`, `game/field/`, `game/battle/`, `game/core/` or `game/menu/` is a
finding: their call-outs reach no run-time transfer that reads what they
declare. Stops and entries aren't held to the strict solve yet (TODO.md).


### The tooling pass after Phase 3's first chunks

The first chunk of each of the three big banks (battle's `$C1`, field and
events' `$C0`, the menus' `$C2`) found gaps that made every chunk slower or let
a bug past the gate. This pass closed the ones that were tools.

**The native shape, checked.** Two of the three chunks wrote a value the game
reads only inside `ORIGINAL_EFFECT`: the battle's random draws divided by the
long arithmetic's leftover high word, and the menus drew a window's inside to
the size the frame's routine left, both written only as scaffolding. The
native-shape build compiles that out and was never run, and shadow mode runs
the default build, so nothing saw either. Two checks look now, one in the
source and one in a running game, and the gate runs both as a ratchet
(`tools/effects_check.py`, `tools/effects_baseline.txt`):

- *In the source.* For every write of a work RAM cell made inside an
  `ORIGINAL_EFFECT`, or in `#if ORIGINAL_EFFECTS` code, or in a function
  called from either (the `leave_*` and `original_cell*` helpers, to any
  depth), the check finds the reads of the same bytes by code outside all of
  that, in any function the game reaches (a registry entry, a table's row, a
  callback, and what they call). A cell is bytes, not a name: constants are
  followed through the helpers' parameters, enum constants and `#define`s, so
  `set_work_byte(g, LEFT_TURNED, 0)` is `battle_work`'s element 0 and a read
  of element 1 isn't reported; a helper's branches are all taken and an
  element past the variable's end drops out, which is what keeps
  `original_cell8`'s two accessors apart; a read and a write of different
  variables meet only where their bytes overlap within one subsystem (or the
  families sharing a page), since work RAM is reused between modes. Reads
  while a guard peeks (`vars_peek`) and in registry guards are the runtime's
  question, not the logic's, and don't count. It is textual (it borrows
  `tools/dontcare_check.py`'s reader of C) and takes two seconds.
- *In a run.* `ct_dev --native on --scaffold-reads` marks every work RAM byte
  with whether its latest write was made inside an `ORIGINAL_EFFECT`
  (`SnesBus.wram_scaffold`; the macro now counts its depth,
  `original_effect_depth`), and the accessors' reads report a marked byte a
  port reads outside one, by port (`runtime/port/scaffold_reads.h`). A write
  the original's code makes clears the mark. That is the question with its
  order: the native build, at that read, would have found what was there
  before the scaffolding's write. The vblank check's shipping-mode runs take
  the flag (they are the library in `--native on` already), so the gate gets
  it for nothing, and `effects_check.py --runs` reads their logs. The one gap
  is `event/leaves.c`, whose runner is `#if ORIGINAL_EFFECTS` code around the
  operation rather than an `ORIGINAL_EFFECT`: the source check sees it, the
  run doesn't mark it. The other way to run the native shape, a
  `-DCT_ORIGINAL_EFFECTS=OFF` `ct_dev` compared with the default build at the
  level of game outcomes, would find less for more: the shipping mode's timing
  already drifts from the reference, so outcomes part for reasons that have
  nothing to do with scaffolding, and a difference names no port. A mark names
  the reading port, the byte and the port whose scaffolding wrote it.

**Which cells count.** The source check reports a read whether or not some
port also writes the cell plainly, and says which (`[plain]` or `[only]`):
the check has no order, and the battle's case was exactly a cell other ports
wrote plainly, read after the one routine that kept it only as scaffolding. A
rule of "flag only cells nothing writes plainly" would have missed it. Nor can
it see the original's still-generated code, which sets most of the operand
cells the entry wrappers read (`[wrapper]` marks a read in a function that
takes the machine: usually the calling convention, sometimes a real input).
So the source check is broad, 131 findings on main before this pass, and the run check is the
precise one: over the library it found 15 port-and-variable pairs, each a read
the native build would get wrong or would have to show it doesn't. One was the
scene's and is fixed: the race's matrix generator left its column step in the
scene's cells only as scaffolding, and the column step's cell is the product's
third byte, which the next signed multiply adds into its product (once in the
jet bike race's 21,120 signed multiplies); the step is written plainly now,
and the multiply reads that byte through a field of its own
(`scene_call.product_third_byte`) rather than the whole product. Three are the
scene circle's, answered below; the rest are battle's, the field's and
events', and the menus' (docs/TODO.md, by subsystem).

Both are ratchets: `--check` fails on a finding not in the baseline, which
holds main's findings, the source findings of the conversion branch still in
flight (so its merge doesn't break on what it already had) and the library's
run findings. A source finding's line names the read's file and function,
what it reads (with the element, where the source names one) and the variable
written as scaffolding, and says `only` where no port writes the cell plainly,
so a plain write turned into scaffolding is a new line rather than the old
one. That is what makes the ratchet hold the battle's first chunk as it was
before its fixes: with the leftover high word, the stat offset's high byte
and the tech's hits kept only as scaffolding again (a scratch copy of main
with the three writes put back inside `ORIGINAL_EFFECT`), the check reports
the reads of the high word (`battle_wide_math[6]` and `[7]`, in `wide_word`,
which the random draw calls) and of the tech's hits (`battle_low_scratch[14]`)
as new; the stat offset's high byte (`[11]`) is a finding on main already,
another routine writing that byte as scaffolding too, and is in the baseline.
Its first cut missed the high word, which the arithmetic writes through a
setter it passes to a helper as a function pointer, now followed. The re-review's later cases (stats read through
the stat blocks' table wherever it points, which can be the direct page's
working cells) are aliasing through a computed address, which the source can't
see and the library's runs don't reach: a build of main with that fix taken
out finds no new run finding. A new finding is answered
before it merges, by making the write plain (it is the game's) or by showing
where the value the read takes comes from and adding its line with the reason
after a `#`. A line nothing produces any more is reported, not failed, since
the baseline carries the branches' lines until they merge; `--prune` drops
them. The scene's fifteen source findings left are answered in the baseline:
the scene program's operand cells (`scene_call`, one record whose fields are
each call's operands and results, so one call's leftovers sit where the next
reads its operands, which the program sets before each call; no run finds
one), and the circle's center and radius, whose reads take the low byte the
callers write, which the scaffolding writes back unchanged while it clears the
high byte (the runs do see those: the circle's jump draws it again without its
callers writing it). The core's one is the block-move stub's own port, which
reads what its caller wrote into the stub. 130 source findings were left on
main then; with the battle's and the menus' first chunks merged, 252 (counted
by element, as the baseline now keys them), and the
battle's chunk added eight run findings of its own (docs/TODO.md).

**A port that spans frames.** The menus' chunk ported the load screen's
opening (`$C2:E613`, which draws the screen through `$C2:E7C3`), which matched
where shadow mode could compare it (every call interrupted) but broke the
`beyond_time` route in the shipping mode: black screen, then a `BRK`. To
reproduce it without the lost port, `ct_dev --as-port BBAAAA` runs a routine
of the original as one hand port would run in the shipping mode: all at once,
the CPU stepped with time stopped and interrupts held, and its bus and
internal clocks owed afterwards at a `cost_milli` of 1000 (`dev/as_port.h`).
The drawing owes about 1.2 frames. The first vblank's NMI was taken at the
port's boundary, correctly; but the menus' handler (`$C2:840E`) moves the
stack up to its own at `$1DFD`, and `pay_debt` took a machine above the
boundary's S for one that had left the boundary, so it paid the remaining
436,911 clocks inside the handler, before its vblank work and with interrupts
held. The next NMI was then taken inside the first handler, whose stack switch
overwrote the S the outer one had saved, and the outer handler returned
through garbage. A debt now remembers that a handler was taken at its
boundary and waits for it wherever the stack goes, for up to a frame, after
which it is paid where the machine is (a handler that never comes back, such
as a task switch); coming back to the boundary clears it. So ports need no
rule for spanning frames. The same path had been delaying the menus' vblank
work on main by up to 11 lines, 12 times in `menus-field-menu`.
`tests/port/debt_test.c` covers a handler on its own stack, one on the
interrupted stack and one that never returns (with the fix taken out it finds
nine nested handlers and the main loop starved). The load screen's port can
come back unchanged; `$C2:E613` ends in the task yield (`JMP $83B2`), where it
stops with `m_stop_at`.

**Fuzzing.** `tools/fuzz_compare.py` runs an entry the `--before` binary
doesn't have on `--after` alone and reports it `new` with its own counts
(before, every new entry was a "before error"), and without `--before` runs
every routine that way, which is how a chunk's new ports go through the same
constraints file. `ct_fuzz`'s areas take a stride (`AREA:STEP`, a field of
each record in a table), a set of values to pick each element from
(`=V,V,...`, as wide as the widest written, so a script place or a record
offset is a real one) and a tie (`=@BBAAAA`, another cell's bytes, applied
after every other area), and it takes 32 areas (as the menus' branch had it).
A port that waits for the next frame through a menu task's yield (`$C2:821E`)
had every state skipped, the yield never coming back on the stack it was
called on; `--stop-at-call BBAAAA` ends each state's comparison at the port's
call-out to that routine instead, comparing the machines at the callee's entry
with the call's don't-care set (`menu_erase_slot` is the case). Running on
past the yield is still out of reach: that is the other task's frame, and the
fuzzer runs one stack.

**Shadow mode's report.** Mismatches are described apart from notes
(abandoned comparisons, timing), each up to its own limit, and mismatches
first, so a run of `abandoned` lines can't push the mismatch the gate fails on
out of the report; `ct_dev --shadow-details N` raises the limit (16, at most
256), and a line says how many more there were. An entry that runs only
inside another port's stretch (the menus' labels in `menu_open`) is compared
on its own where the original enters it, up to its first point: if that point
is the enclosing routine's too, it is compared there and goes on; otherwise it
is set aside at the enclosing routine's point, counted `abandoned`. What it
did to then is timed, so its `cost_milli` is calibrated from those stretches
(`menu_read_save_slots` has its stretches in `menus-field-menu`); what it
isn't is compared past that point, which would need shadow mode to follow two
comparisons waiting on different points of the original at once, the enclosing
one resumed by its callee's return while the nested one waits for a later
instruction: a checker over every active comparison rather than the innermost.
The enclosing comparison covers the stretch, and the shipping mode never calls
the nested entry there, so that is left (docs/TODO.md).

### Phase 3, battle: the screen script's shapes (`$CD:1F47`-`$CD:3BFC`) and the frame work's line moves

What `python3 tools/burndown.py --uncovered CD` listed after the cuts and
bands: the window shapes the screen script draws (`$CD:31AA`-`$CD:3BD4`), its
special effects (`$CD:2A4A`-`$CD:2DCA`), the commands whose handlers were still
the original's, and the frame work's remainder. The chunk took the first three
whole and the frame work's table moves, and stopped where the frame work
waits for frames (below). `$CD` went from 36% left to 6%; every command of the
screen script language is a row of its table now, and only `$C1`'s wait for a
line of the picture still hands the machine to the original (a bridge).

What it is, in the game's terms:

- **A window's straight sides** (`battle/screen_sides.c`): a shape is a top
  line, the two sides' columns there and up to three stretches, each side
  following one edge down a stretch at a fixed 8.8 slope; the quadrilaterals
  (`screen_beam.c`) and the triangles share it, and with it the one running
  sum both sides step in, whose carry the comment explains for both signs of
  a slope (review rubric 119).
- **Triangles** (`battle/screen_triangles.c`, `$CD:367C`): sorted down the
  screen, a flat top, a flat bottom, or a side that turns at the middle
  corner's line; the right triangle hung from the top of the screen (`$B8`,
  `$CD:3924`), always flat-topped, the window above it filled to the top edge;
  and a triangle's center where two medians cross (`$AD`, `$B4`, `$CD:21C0`):
  each median a line `y = slope * x + intercept`, the crossing the intercepts'
  difference times 256 over the slopes', as one word over another
  (`crossing_of` says how the original shares the 256 between them). The
  triangle commands draw in C now, so the runner's hand-off to the original's
  rasterizer and its flow kind went; what the sizing leaves only the machine
  knows (the controller's byte) is the runner's, in a table by command.
- **Circles** (`battle/screen_circles.c`, `$8B`, `$CD:2FB7`-`$CD:3656`, and the
  circle commands moved from `screen_shapes.c`): the half widths of the
  rows by the midpoint circle (a byte-wide error term), laid out about the
  middle of the worked rows and copied into the window table about the
  circle's line; the six kinds are an enum the place and kind commands take
  (round, flattened below, hung from the top, tall, wide, twice the size).
  `$B5`/`$B6` draw a square the same way.
- **The special effects** (`battle/screen_specials.c`, `$80`, `$CD:2A4A`):
  29 named operations in a C table: bands of light (the fixed color's
  per-line tables, written through `vid_line_fixed_color`), wedges of the
  first triangle, a layer bobbing on a wave, the waves' lines marked at
  random, the palette effects `screen_palette.c` already had, the enemies
  hidden. The waves' lines and marks are records in the map now
  (`battle_wave_lines`, `battle_wave_marks`).
- **The rest of the language**: `$A1` draws a triangle at its corners,
  `$DA` a wedge between positions and `$CC` measures a direction
  (`screen_between.c`), `$D7` drifts the layer (`screen_motion.c`), `$F9`
  pokes work RAM (`screen_script.c`, through `wram.h`). `$C1` is a row whose
  bridge calls the original's wait for a line (`$CD:3B82`).
- **The frame work's table moves** (`battle/display.c`, `frame.c`,
  `scroll.c`): the slide's line moves (`$CD:053C`/`$0577`, the runs of the
  nine per-line tables), the two line-wave scroll modes (`$CD:07E2`/`$07B9`,
  the Lavos fights'), and the rule's hold on the battle's palette animations
  (`$CD:0B1E`), all called as C where they were call-outs.

**Narrowed.** The frame work's waits and what they call: the battle's
closing (`$CD:0006`, `$03B4`-`$04A5`), the messages (`$CD:0021`, `$01B3`), the
start settings' copy (`$CD:0C9D`/`$0CF6`), the frame wait's tail's callees,
and `$CD:3B82` itself; they wait on frames, the sound and the menus' text,
and are the next chunk (docs/PLAN.md).

**The third trap, settled first.** `tools/v_readers.py` has V dead after the
one call-out the chunk adds (`$CD:3B82`, from `$C1`'s bridge); the
don't-care check found the line wave's fill leaving X where the finer wave
goes on, and the slide's moves leaving Y for the frame work, which the
entries now keep. The triangle's center works in the cells `$23`-`$2A`, over
`battle_party_defeated` and the random cells: written plainly, as the
direction's measure writes them (docs/TODO.md).

**What shadow mode found.** Circles the scripts grow past radius 127 (the
Lavos fights'): the original works the rows out for the radius's low seven
bits and copies the run for the whole byte, so such a circle is a small one
with older rows above and below it, and one of 128 draws nothing. The
rule's hold is on slots 8-11, not 0-3 (the slots' table starts at `$0520`).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the circles 53,267 calls, the triangles drawn at their corners 27,096, the
hung wedges 2,127, the special effects 3,470, the line wave 7,619 and the
finer one 631, the slide's moves 713 and 598, the rule's hold 432,395. No
movie has `$AD`, `$B4`, `$B5`/`$B6`, `$C1`, `$CC`, `$D7`, `$DA` or `$F9`; no row
turns away a call the library makes.

**Fuzzing**, seed 1 (`tools/fuzz_routines.toml`), none mismatching: the
triangles' drawing 0 of 16,762, `$A1` 0 of 8,173, `$B8` 0 of 8,616, `$AD` and
`$B4` 0 of 9,775, the circles 0 of 10,069, the square 0 of 10,061, the
special effects 0 of 9,464 through the command and each effect's own entry
0 of 20,000 (the nudge 0 of 10,029, with the carry the dispatch leaves
clear), `$F9` 0 of 9,775, `$D7` 0 of 6,724, `$DA` 0 of
8,383, and the frame work's five 0 of 20,000 each; the 167 screen entries
against the tree before the chunk, the same counts every one. Fuzzing found
the nudge wave leaving the multiplier's operands the other way round (352 of
9,464), and random corners and runs past the window table on odd frames,
which the rows' needs turn away (`screen_triangle_fits_the_table`, the
circles' `run_fits`), as the quadrilaterals' did.

**Timing.** The special effects differ in their work a hundredfold under one
handler, so each is a registered entry of its own and runs timed as its
original routine (`TIMED_AS`, a table beside the effects'); the circle's rows,
which the port works out in locals where the original works each at length,
are charged at a rate a row (`charge_as`), and its copy is timed as
`$CE:E7C8`'s. The new entries and the screen script's runner were calibrated
from the shadow runs; what is left over the limits is the circles' spread
(4.1% error, at most 14,483 clocks) and the marked lines' (5.6%, at most
11,823), and the runner and the frame work above them, all writing RAM, in
`tools/timing_exceptions.txt`.

### Phase 3, battle: the battlefield changed and an effect's rows of its tiles (`$CD:0D28`-`$CD:12FF`, `$CD:1314`-`$CD:1367`, `$CD:15D5`-`$CD:1608`)

The battle's previous chunk is in history/reimplementation-chunks.md ("Phase 3, battle:
an action shown").

**How it was chosen.** By the fallback measure: once an action's showing was
C, the battle frame work's largest handbacks were the original's code it
called to load what the animation needs -- the background's change through
`$CD:0018` (its map built at `$CD:107D`, 14.5 million instructions over the
reference play; its map buffer cleared at `$CD:1061`/`$CD:1052`, 8.7 million;
its tiles decoded at `$CD:11E4`-`$CD:12B8`, 5.9 million) and the effects'
rows of the battlefield's tiles through `$CD:0015` and `$CD:002A`. They are
work, not waits; the waits inside them stay the original's.

**What it is.** `battle/battlefield_change.c`: changing the battlefield --
one battlefield standing for three by which combatant the animation's
channels animate, the old layer's effects stopped, its colors faded out
through palette slot 10's preset (`$CD:0E79`) and the layer taken off the
screen over two frames, the new battlefield's look (screen scripts, color
sets) and layout taken from the ROM, and, for a battlefield with a BG3 layer
of its own, four tile sets decoded (mirrored and flipped as their
descriptors say) and sent, the blank 16x16 tile made, the map buffer cleared
to it and sent, and the map built from its strips; then its colors and its
screen scripts, which `screen_script.c` starts (`$CD:0D33`, `$CD:15D5`) --
and an effect's rows of tiles, decoded by `tiles.c` and sent a row set at a
time. The ROM's tables behind them are `frame_data.h`'s battlefield section
and `screen_data.h`'s script starts. The waits (`$CD:3E75`, `$CD:3E3B`) and
the frame work's transfer (`$CD:3ED2`) are call-outs made where the original
makes them, with what the original keeps on the stack there, since shadow
mode pairs a call by its site and its stack. The showing calls the loads as
sibling ports (`battle_run_long`), and the original's other loaders of a
battlefield (the screen effects' and the HP gradient's song, `$CD:0D62`) reach
`$CD:0EBD`'s port by its address. Nine entries.

**The third trap, settled first.** `tools/dontcare_check.py` agreed with
every call-out's set but A's high byte: the waits carry it to the battle's
pause and the transfer reads it, so the calls give the original's (clear
where the original clears it, else what the last wait left), and the entries
leave it clear for their callers, as the original does. Nothing else of the
registers is handed on.

**What shadow mode found.** An effect's rows wait for the next frame once,
then send each row set straight after the last (the loop goes back past the
wait); a port waiting before every row paired its second send with the
original's fifth. The guards read the layout from the ROM through the bus,
which charged the original's run their reads (36 clocks a call) until they
peeked. And the shipping mode's scaffold reads found the animation scripts'
channel reading what the loading leaves in the cells `$53`-`$57`, where a
screen effect takes the settings its command doesn't give: what the loading
leaves up to `$61` is kept plainly, the rest of its cells as scaffolding.

**Validation.** The library's 33 movies in shadow mode, none mismatching:
the change 484 calls, the load 487 (15 compared alone, the rest within the
showing's comparison or interrupted), the fade 326, the scripts' start 487,
a tile set's decoding 1,888, the map's building and its clear 472 each (all
but 6 spanning the vblank handler), the effects' rows 984 and 984 (compared
within the showing's). Fuzzing, seed 1 (`tools/fuzz_routines.toml`): the
decoding, the clear, the building (one to eight strips of one to sixteen
rows), the fade and the scripts' start 0 of 20,000 each; the load up to its
first wait or transfer 0 of 10,844 (the guard refusing layouts past the map
buffer, which no battlefield of the game has); the effects' rows up to their
wait 0 of 20,000 each; the change 0 of 452 (its load runs as C inside it, so
most states run past the budget). Over the reference play the code run in
place of hand ports fell from 28.3 KiB and 171.6 million instructions to 27.1
KiB and 141.9 million, the frame work's handbacks from 3.0 KiB and 48.5
million to 1.7 KiB and 18.9 million, and its native coverage from 77% to 87%
of the 12.7 KiB real play runs; 16 routines left the baseline. In the
shipping mode over the movies and the routes, from 26.2 KiB and 391 million to
24.9 KiB and 357 million.

**Timing.** The new entries were calibrated from the shadow runs
(`--drifted`); the map's building from its six whole stretches, and the
clear, which no stretch of the library completes, from its loop.

### Phase 4a, the shipping mode's fallbacks, chunk 11: the hand ports alone, every movie to its end (`$C2:8000`-`$C2:8341`, `$C2:83B2`-`$C2:8403`, `$C2:813C`, `$C2:940D`, `$C2:89F2`-`$C2:8AFB`, `$C2:CECE`, `$C2:0454`, `$C2:2556`, `$C6:EE31`, `$C0:00A4`-`$C0:00B7`, `$C0:74D4`-`$C0:74F7`, `$C0:597D`, `$C1:0023`-`$C1:003C`, `$CD:0000`, `$CD:0003`, `$CD:0D93`, `$FD:DA00`-`$FD:DB24`, `$7E:37F3`, `$7E:3753`, `$7E:375E`, `$C3:0C28`, `$7E:399B`, `$7E:39D2`, `$7E:6248`-`$7E:64BF`)

The fallbacks' stream (history/reimplementation-chunks.md, "Phase 4a, the
shipping mode's fallbacks, chunk 10", for the one before), chosen by a new
measure: how far each movie gets on the hand ports alone. `ct_dev --native on
--generated off --no-interpreter strict` (the C sound driver) runs no code
generated from the ROM and no interpreter, so the first instruction no port
runs ends the run. Before this chunk no movie got past frame 3,779: 41 of the
42 stopped at the menus' way in (`$C2:8000`) by frame 1,699, and the attract
demo at the battle screen's setup (`$CD:0000`). Now all 48 reach their end.
`tools/strict_check.py` makes the runs, and its `--check` is the gate's
ratchet against `tools/strict_baseline.txt`, a line per movie: `end`,
`drifts`, or the frame it must reach, and why; the suite runs it with -j 2
(PLAYING.md, "Without the interpreter"). Reaching the last frame counts only
with a clean exit and no `halted:`, `accessor out of range` or `page refused`
line, and the end is compared with the reference play's: the saved game (the
eight runs of work RAM a save holds) and save RAM, less the play clock and the
battles' random start, from `ct_dev --end-state` beside the suite's shadow
runs. A whole end state can't be compared: the strict run is the shipping
mode's byte for byte (new-game was measured so), and the shipping mode, with
its ports' estimated time, runs ahead or behind the original, so at
new-game's last frame its story count is 3 where the reference's is still 0,
and the menu movie's work RAM differs in 713 bytes of timers, animation and
scroll. So the saved game is what is compared: 31 of the 48 movies end
with the reference's, and 17 drift, each line saying how (the fights' turns
landing otherwise and ending with other HP and items, a menu's cursor buying
other things, the party standing elsewhere at the last frame).

What stood in the way was mostly not code but its joints: the jump stubs the
banks are entered through, the waits a routine makes by jumping into a task
switch, and the places the original's code comes back to after a stretch that
no longer exists as a stretch on the machine. Each is now a port of what the
game does there:

- **The menus' way in and out** (`menu/display.c`, `menu/fades.c`). The
  entry keeps its caller's registers, asks what it was called for (the menus'
  own names now, `menu_asked_for` and `menu_asked_with`), and either starts a
  game afresh -- the load screen asked for before any game is set up, the
  story's flag for it in `re/ram.toml` (`story_new_game_set_up`) -- or moves to
  the menus' own stack, opens, and starts the tasks. The two ways out stop
  every task, show what the party wears again and fade the screen out, then
  return or restart. The fades are one function, a step of brightness a frame
  until full or black, run inside the original's frame for its routine
  (`IN_THE_ORIGINALS_FRAME`); a fade out that reaches black hands the last
  frame's wait the step's sum and its flags, as the original does.
- **Waits that are jumps.** Where the original ends a routine by jumping into
  the task switch so that it returns to its caller a frame later, the port
  runs the routine as C inside the original's frame (`IN_THE_ORIGINALS_FRAME`)
  and yields from the caller's site (`menu_yield`); where a task's loop
  waits, `menu_wait_in_the_task_loop` and `menu_wait_frames_in_the_task_loop`
  (menu/tasks.c, one wait on the task switch's two entries), which the held
  cursors' tasks, the gate screen's and the save's last wait call.
  `menu_go_on_in` is a jump to a sibling's port: run as C where the runtime
  would, a stop declaring the set otherwise. The menus' way in (`$C2:8000`),
  a jump to the mode's start, stays a stop the runtime takes to the start's
  entry: run as C there, or the start registered at both, the start's
  call-outs fall inside the way in's comparison, which shadow mode hands to
  the start's nested one, and all 48 movies mismatched.
- **The continuations.** A task the scene program's lists called can come back
  to the list frames later (a script that waited for the music); the lists'
  continuations (`$7E:399B`, `$7E:39D2`) run the rest of the list from the
  slot after it, and the list's exits and the vblank list's own exit are the
  loop's (scene/frame.c). A task whose port takes the machine (the race's,
  the Epoch's dive's, the endings', the channels' first steps) is a row of
  the lists' table by its routine, run as the list's call reaches it; the 76
  rows are the tasks the library's runs reach, in shadow mode and shipping. The field's frame is a table of its pass's steps,
  any of which it can go on from (`field/main_loop.c`), and the channels'
  scripts and the sequence after its wait go on in C (`scene/script.c`).
- **The battle's setup and its stubs** (`battle/opening.c`): the setup up to
  the graphics' load and after it, the frame work's first mode, the screen's
  setup for the field (`$CD:0000`), and the defeat on the screen effects'
  battlefield (`$CD:0D93`). The setup step stays a call-out from the steps'
  dispatch: run as C under the dispatch's frame, its waits broke the call
  chain (`battle/steps.c`).
- **The planet** (`scene/planet.c`): the setup of the planet seen from space
  as the game is lost and as it ends (modes 7 and 8), the fades its scripts
  call, its caption, and its wait for a button and count down to the reset.
- And the warp's four trips' starts and waits, an object's script started
  over (`event/engine.c`), the field's layers put back at their starts, the
  member bar, the low HP marks, the overworld's way back from the menus and
  the object mode's frames' wait sending the music.

Two guards turned real play away and are ports now. **The battle's point
jump** (`$C1:672E`, `$C1:6743`, `$C1:674A`) takes its lift from a direct-page
cell other work shares (`$90`, `battle_jump_lift`); the port took the usual
value, 6, and refused anything else, which Spekkio's fight in
`battle-tech-sweep-spekkio` met 208 times. It reads the cell now, and every
writer that lands on it leaves it as game state, through one helper
(`battle_leave_jump_lift`, battle/battle.h): the animation slots' places, an
area search's side, the tech list's last tech, a blink's place in the other
targets' list and a walk's way back (the last two had left it only as
scaffolding, which the native shape would have read stale; chunk 11's review
found it). **The scaler's sample positions** (`$C3:0309`) wrap past 16 bits
for a racer drawn at a sixteenth of its size, which strict mode's timing
reaches in `events-jetbike-race-won`; the wrap's carry moves the samples after
it on by 1/256 of a pixel, as the original's sum does, kept in one function
(`group_positions`) that says so.

**Runtime.** Shadow mode notes a port that finished on a stack it switched to
(`note_a_port_s_stack_switch`) and keeps a stretch's stack switch across its
call-outs. A registered entry can declare the bytes of its caller's frame it
pulls (`NativeEntry.pulls`): the lists' continuations pull the six their loop
keeps under a call, and the original is counted as returned only above them.
`ct_dev --fallbacks` prints each run-start address the first time it is
entered, and `ct_dev --end-state` writes work RAM and save RAM at a run's end
(the strict check's comparison). `ct_fuzz` leaves the bytes of an original's
frame a port stands inside uncompared, as shadow mode does
(`native_frame_dead_direct`): the menus' ways out fade inside the fade's frame
without writing its return address, which fuzzing otherwise counted in 1,636
of 2,000 states. tools/dontcare_check.py reads the menus' task-loop waits as stops,
and the fade, the scene's unpacker and its first script step as fixed
delegates.

**The fallback measure** (dev/fallbacks.c) had counted code an enclosing
comparison covers as the original's in place of ports. The field's pass's
continuations (`$C0:00A4`-`$C0:00B7`) are entries the original reaches inside
`field_frame`'s stretch at the pass's own S, and a port taking over there
ended every extent begun at that S, the enclosing comparison's own among
them: a request answered over frames (a script's menu, a tile copy) was then
counted as unported although `field_frame` went on comparing it, and so were
the gate's swirl under the exchange and the race's setup under the scene's
first frame. An entered port now leaves an enclosing comparison's extent in
place while the runtime still makes that comparison (`native_compares`,
runtime/port/native.h); one the runtime has ended goes as before, which also
uncovers 767 instructions of the field movie's window list (`$C2:EF05`) a
comparison that had ended still covered. Address by address against chunk
10's measure over its 42 movies, that window copy is the only code counted
now that wasn't then; the rest of the fall below is the measure's fix and the
ports together (the race's setup's mirroring, `$7E:3ED3`, 98,672 of it, and the
field's event loop at `$C0:5900`, now under comparisons that cover them). The
window copy rose inside the menus' falling `C2:----` line, which no call on
record files it under, so tools/fallbacks.py now cuts it out onto a line of
its own (`CUT_OUT`, `C2:EF05`, 4,705 instructions) for the ratchet to hold.

**The `open` lines** (what the original ran under a comparison still open when
a movie ended, which the headline leaves out) rose with the chunk, 0.24 M
over the 48 movies against 0.21 M over chunk 10's 42: three movies end inside
a member's action now that the step's dispatch is C (`open C1:B967`,
battle-tech-sweep 17,858, late-omen-zeal 10,562, late-lavos
7,617; chunk 10 had 32,045 under the dispatch's own comparison, `open
C1:0003`, and counted the rest of battle-tech-sweep's as the original's before
the measure kept a live comparison's extent), late-omen-terramutant ends inside
an action's showing (`open C1:AC57`, 14,042), the new battle-menu-buttons
inside the expired timers' dispatch (12,916 of `open C1:8290`), and
endings-telepod-lavos inside the field's frame (3,515 of `open C0:0097`).
The shipping mode runs none of the original's code over the same 48 movies,
so none of it is code without a port: it is a port's own work the movie's end
cut short, whose part a port might hand back the measure can't tell before
the comparison closes. Each line's reason says which movies it is.

**The second batch of movies** joins the library: `battle-tech-sweep-spekkio`
(134,646 frames), `field-65m-bc-dactyl-flight` (3,992),
`field-floor-switch` (3,501), `battle-menu-buttons` (5,086),
`field-factory-switch` (8,387) and `field-northern-ruins-switch` (3,811),
their routes re-run on this build and the movies they wrote identical
(docs/movies-wanted.md). In the shipping mode the first trio's second fight
leaves the party where the cell search can't land in Spekkio's narrow
doorway, so the route edges there a frame at a time when the search misses
(`edge_to`, tools/movies/routes/tech_sweep_spekkio.py); the reference play,
where the search finds it, is unchanged.

**Numbers.** Strict mode: 0 of the 42 movies reached their end before (41
stopped at `$C2:8000` by frame 1,699, the attract demo at `$CD:0000` at 3,778);
all 48 do now, 31 of them ending with the reference play's saved game. The shipping mode (`--native on --generated on`, the C
sound driver) runs none of the original's code over the 48 movies (0.09 M instructions
over 42 before), and 18,033 over the 21 routes (0.06 M), all hand-backs: the
program's start's in the Black Tyrano's route (14,992, `$7E:350E`), the vblank
list's and the battle's calling in (`$FD:A990`). Reference play over chunk 10's
42 movies 0.95 M to 0.54 M, 0.55 M over the 48; its baseline takes the new
movies, and lines that are the same code filed under a new port's hand-back
(`$7E:398A`, `$FD:DA62`) or hidden under a comparison open at a movie's end
say so (tools/fallback_baseline.txt). The burndown: 259.6K of 266.7K ported
(258.8K before), 2,168 entries (2,111). The timing gate's exceptions add the
joints' list runs and the field's pass going on (they run the steps' and
tasks' ports inline at those ports' rates), the second batch's longer
stretches of older ports and the world map's way back from the menus with the
screen blanked (tools/timing_exceptions.txt); the vblank check's add the
warp's end, a frame early in the shipping mode. The warp's four starts had
`warp_set_up`'s cost copied onto them; calibrated, it is 1 (their own work is
a store between two calls timed as their routines, and their estimates were
exact over the library either way), the long trip's, which no movie takes,
given the others' value, and the warp's end still comes a frame early. The planet's setup (`$7E:6248`) is an entry as the other setups
are: its one call over the library runs inside the scene's first frame's
comparison, which matched, and fuzzing it is 0 of 900.

### Phase 4a: remaining field, menu and battle-frame entries

The uncovered-run audit of banks `$C0`, `$C2` and `$CD` found mostly work
already implemented inside larger ports, plus the script-loader diagnostics.
Twenty-eight entry boundaries now make that work independently comparable:
fourteen screen effects and their C-table dispatch, party placement, the
first picture's line effects, restarting after the screen goes dark, script
validation and built-in commands, controller sampling's wait and continuation,
two empty menu interrupts, and the battle's window-status and defeat
continuations. The effects call the existing game-state
operations; the defeat continuation shares its sequence with the full defeat
entry. The dispatch and loader use the bridge that checks the callee's full
registry contract before making a C call. No scene or warp port changed.

The restart dispatch row hands off at `$C0:4181` before the idle wait, with
the original JSR frame. Its stop has its own empty don't-care set: every
register is preserved and compared there, rather than borrowing the
dispatcher's return-only set. Running an idle pass as a direct C call had
left the dispatcher without a declared stop. The active-registry restart
fuzz case reproduced that defect in all 20 states before the fix and
compares all 10,000 states after it, with zero mismatches or skips. The
ordinary admission-only fuzz case cannot exercise that direct-call path.
Both event-screen parents tag their inline effects with each handler's
address, and the defeat parent tags its continuation as `$CD:0DA2`, so
those operations use their own timing estimates.

Invalid scripts now show the original diagnostic through platform services:
amber for a text-file header, dark red for an oversized unpacked script,
with the same signed length comparison and wraparound. The palette service
can replace one colour on both backends; interrupt stubs become empty and
the diagnostic idles. No shipped script requests this diagnostic.

**Coverage.** Burndown's remaining bytes fall from 856 to 190 in field/events,
96 to 18 in menus/overworld, and 94 to 76 in battle-frame work. All three
display **0% left**, which is rounded, not a claim of zero uncovered bytes.
The residual field loops are already C for valid scripts but hand back on
out-of-buffer positions or a changed machine bank. The battle runner hands
back on bytes beyond its command table; its five-byte line-request handler
is inline in the timing service but lacks an independent entry (TODO).
Five menu task waits retain their
refusal stops; four lone returns are counted after stops although the wrappers
already return. TODO records why the guarded paths remain; no baseline or
exception was widened to remove them from the measure.

**Validation.** Every new entry has a reproducible seed-1 `ct_fuzz` row.
The fourteen effect entries each compare 10,000 states; party placement,
script validation, built-in copying, restart, controller continuation and
window status each compare 10,000. A second 10,000-state validation run
forces the text-file diagnostic. The dispatcher, loader continuation, both
menu interrupts each compare 10,000. Dispatcher
and loader comparisons end at callee admission because fuzz has no active
registry; the callees are fuzzed separately. The idle
controller and diagnostic compare 10,000 and 1,000. Defeat compares 2,000
at the battlefield-change call. Picture setup compares 140 of 2,000,
with 1,860 call-budget skips; the individual line-effect callees have their
own fuzz rows. All compared states match. The new defeat boundary exposed
the index-load N/Z flags at its first call; the thin bridge now supplies
them. The initial party run admitted only 27 states because its high bytes
were unconstrained; explicitly zeroing those bytes exercises all 10,000
without changing the guard or excluding any mismatch. The ready-controller
continuation consumes the sample without repeating the readiness test;
its full-read and frame-update parents each pass another 20,000 states.

The 37-movie sample compares controller continuation 375,427 times, picture
setup 310, party placement 292, each script-loader entry 329 and screen
dispatch 290, all with zero mismatches. Eleven effect entries get standalone
comparisons as well as their inline coverage. Window status compares 297,896
calls; its 200 interrupted calls are not counted as matches, and its 10,000
randomized fuzz states deliberately cover the status calculation independently.
The diagnostic, restart tail, defeat continuation, empty interrupts and three
effect entries (cover-left, end-field and ripple) have only standalone fuzz
evidence. The initial sampled gate with long routes and evidence ends `suite OK`.
All 37 shadow movies are clean; all 56 routes reach their outcomes in both
the shipping and scaffolding-free builds, with no native-shape exception
needed. The remaining runtime, static and evidence checks pass without
changing any baseline or exception. Per-entry counts are in `ports.tsv`.

The Python suite passes 468 tests (10 skipped), and the fourteen C unit-test
programs pass. An attract replay with `--px-check` compares all 22,500 frames
and every line and pixel identically. The MinGW build reports no warnings.

Nineteen new rates are fitted to the complete sample, leaving every existing
entry's rate unchanged. The controller continuation is 5,568, party placement
4,248, window status 3,437 and built-in copying 37,206. Picture setup's own
rate reaches the estimator's lower bound of 1: its separately charged callees
already account for its measured time. Unreached entries do not claim
calibration. Library-wide drift remains the batched library check's measure;
the branch runs the prescribed sample, not an all-movies recalibration.

### Phase 4a, the native frame: the attract sequence's pages of credits and the palace rising (`$7E:6B04`-`$7E:6CBF`, `$7E:66B0`-`$7E:6B03`, `$7E:73C6`-`$7E:7440`, `$7E:783C`)

The fifth chunk of the native frame's stream, after the curtain (history
file, "Phase 4a, the native frame: the curtain, and the fallback measure's
unfinished comparisons").

**How it was chosen.** The strict player stopped at frame 2663 on `$7E:6B04`,
the setup of the scene program's mode 1, and over 3,000 frames the rest of
what it interpreted was mode 1's and mode 3's code. They are the two scenes of
the program the attract sequence plays between its field scenes; nothing
else reaches them.

**What it is.**
- The pages of credits (`scene/credit_pages.c`, mode 1): five pages of the
  game's staff, one each time the mode runs, the page kept below the
  program's work RAM. The names are BG1's map, written from the program's
  text a letter a tile; the jobs above them are sprites in a small font, 5
  pixels apart, written straight into the sprite table, which the page's frame
  task keeps from being built over by holding the sprite table's cursor at its
  start. On the program's 90th frame the task hands its slot to the darkening
  that ends the scene. The letters' resource is half sent as it is and half
  widened from two bitplanes to four, the two added blank.
- The palace rising (`scene/rising_palace.c`, mode 3): the setup takes the map
  the field left at VRAM `$6000` and shows it as BG1 in mode 2, its top ten rows
  blank, with per-line effects for a shimmer (every other line pushed across, by
  frame parity), the sea's scroll below the horizon and the sky's map above it;
  frame tasks ripple BG1's columns through mode 2's offsets, fade the palace
  dark, to white and back, and work out a turning matrix the scene never uses
  (the program's turning of a map, mode 5's, would); vblank tasks send the
  offsets and the scrolls. The attract sequence's showing adds a curtain (a
  frame task that steps the curtain, `$C3:01E4`, through the machine, so the
  step's comparison stays its own) and faster fades. The setup changes three
  of the program's operands -- the map upload's VRAM address, the turning's row
  count and the fades' steps -- which are now variables in the RAM map, and the
  guards hash the code around them.
- The services: `vid_send_oam_fill`, `vid_set_sprite` and
  `VID_STEP_VRAM_READ_FIRST` (PLATFORM.md); and `palette_animation_playing`,
  which the palace's fades wait on.

**What shadow mode and fuzzing found.** The letters' unpacking is called from
inside the setup's loader routine, whose return the original has under the
call: without it shadow mode paired the call with nothing. Y and the overflow
are live at the setups' end (the frame's work reads them): each setup leaves
them as its original does, the credit pages' overflow worked out in the
wrapper from the page's last letter. The curtain's task is a frame task, not a vblank one, and the
original's step waits for a latched beam: run as C inside the list, it put the
whole frame list's comparison in a latch wait (124 frames uncompared), so the
task is the list's original's, a port reached through the machine like the
race's. Over the 35 movies, 0 mismatching: the credit pages' setup 10 calls (compared inside the program's start) and their task 900; the palace's setup 2, the turn, the shimmer, the ripple, the offsets' upload and the curtain's task 562 each, the fades 2, 86 and 64, the map's upload 2, all in `attract-demo`, which plays the attract sequence's showing of the palace. **Fuzzing** (seed 1): 0
mismatches over the 14 rows -- the two setups 300 states each, the tasks
20,000 each (a page's 90th frame, a fade over or not, a mosaic due and a
curtain under way among the variants), none turned away. The palace's setup
sends the map itself, where the original increments the buffer's flag and
sends through the vblank task's code; a flag already at $FF, which the game
never has, wraps there and sends nothing, and the port keeps that wrap
(`finish_the_palace`), so its row fuzzes the flag's whole range.

**Measured.** The strict run goes on through both scenes and the attract
sequence's field scenes to frame 3449, and stops at `$C0:3B76`, in the field's
event command that goes to a location at once (`$C0:3B4B`), after its wait for
the vblank flag. Over 3,000 frames `--interpreted log` lists nothing (from
224); over 20,000, nothing in the program at `$7E`: what is left (513 addresses)
is the field's, the battle's and bank `$FD`'s. The fallback measure's reference play went from 52.21 M to 51.43 M (62,881 still under open comparisons); one line rose, the program's code with no port (`7E:----`, +826 instructions): the tails of the two new setups after their call-outs, which the original runs in the reference after the setup's comparison is set aside inside the program's start, as the staff roll's and the shared endings' setups' do.

**Timing.** Calibrated: the new entries, and the program's start, whose
stretch now holds the two setups as C.

### The reading cost of a chunk

By field chunk 13 and battle chunk 6, three streams were landing a chunk and a
review every couple of hours, and most of what each one cost was reading rather
than porting. The fixed reading was measured: docs/PORTING.md 240 KB,
docs/RECOMP.md (since split into this file and GENERATED.md) 556 KB,
docs/TODO.md 108 KB, the review rubric 49 KB and 112 items, against a chunk's own diff of 2-3 thousand lines. Four changes cut it,
none of them to the bar:

- **The rubric's decidable items are a tool.** `tools/portsmell.py` checks
  eleven of them -- quoted assembly (15), an admitted "presumably" (17), twin
  functions per operand encoding (2), a named emulation of the arithmetic unit
  (12), a register carried out to a wrapper (26), the scaffolding switch as a
  value (23), the PC read for identity (72), the sign bit of a difference (19),
  a call-out into one of our own registered ports (22, 61), a `source` line
  citing a file that is gone (25), and an enum nothing takes as a type (33);
  two more came later, a flag noted inside the logic's loop or game branch
  (93, 105) and a return frame pushed by hand outside a bridge (85, 101). It
  is a ratchet against `tools/portsmell_baseline.txt` and the suite runs it, so
  each of those items is caught before a port is submitted instead of coming
  back from a review a day later, and the rubric names the tool in one line
  instead of spelling the item out. Written against the tree it found 47
  standing instances, all on the baseline and in docs/TODO.md: 20 stale map
  citations and 17 sibling ports called through the machine among them. It says
  nothing about whether a port reads as hand-written -- that is still
  `portstyle.py` and the review.
- **The milestone doc holds mechanisms, not a log.** The per-wave,
  per-subsystem and per-chunk accounts moved to docs/history/ (waves, phase3a,
  chunks), leaving the mechanisms, the measurements and the newest section of
  each stream as the shape a new one follows: 8,420 lines to about 3,300, with
  an index of the history files here. A citation names the file that holds its
  section.
- **The ports ledger is a file to grep.** docs/PORTING.md's Status table, a row
  of prose per entry, was 140 KB of the doc's 240 KB; it is docs/ports.tsv now,
  and PORTING.md is 63 KB.
- **One review per merge, not per chunk.** The gate runs at every commit because
  behaviour has to hold there; the review's cost is fixed per reading, so it runs
  once on a branch's accumulated chunks before it merges. Same files, same
  rubric, read when there is more to read.

### The fallback measure

The goal is a program with no emulator in it, and four things keep the
original's code running beside the ports: a guard that refuses so the original
runs, a port that stops and hands the rest back, a call-out to code that is
still the original's, and code no port covers. Until this pass nothing measured
how much real play still ran on them, and the native-shape build had only ever
been compiled. Three gate checks look now (tools/suite.py's `fallbacks`,
`refusals` and `native-shape`).

**The measure.** `ct_dev --fallbacks` and `ct_drive --fallbacks`
(`dev/fallbacks.c`) count every instruction the original's code executes in
place of hand ports, interpreted (the CPU's instruction hook) or generated
(`generated_insn_hook`, a test per instruction in `RC_INSN` when it is unset),
by address, by the routine it belongs to, and by why it ran. The runtime
tells the tool when it hands off (`NativeObserver`, `runtime/port/native.h`):
every hand entry at an address refused (with which condition: the P
precondition, the CPU's mode, or the guard), a port entered, called out,
stopped, returned from an interrupt, or was skipped at the nesting limit. The
tool follows the stack from there: a refused call lasts until S rises above
where it was entered, a call-out until its callee returns, a stop until the
port's routine returns; an interrupt opens an extent of its own that an RTI
ends wherever the stack is; and a hand port entered at or above an extent's
level ends it (the original went on to a port without returning: a main loop's
next pass). Each instruction is then `refused`, `handback`, `skipped` or
`unported`, filed under the target of the latest call on record (the port's
entry for what it hands back after a stop).

**Measured where it holds still.** The shipping mode can't carry a ratchet: a
port's estimated time moves every movie's presses to other frames, so a movie
goes somewhere else with every timing change. Running the library with the
native-shape build, whose only difference is timing (the scaffolding's bus
accesses), moved 99 of 1068 (kind, routine, port) keys and doubled the wait
loops' counts. So the ratchet is taken over the **reference play**: the shadow
runs the gate makes anyway, where the original runs everything at its own
timing. There the runtime's same calls mark what a port stands for -- from the
original's arrival at a port's entry until it reaches the port's call-out,
stop or return, the original is running the port's work and isn't counted --
and an idle entry's pass of its wait loop, counted as the original runs it, is
taken back when the loop comes round to its head. The pass that leaves the
wait is taken back too where it ran nothing but the loop's own instructions
and a hand port's entry takes over at the exit, or where it returned having
run only the bytes its entry declares, the loop's and its return's
(`NativeEntry.idle_return_span`: the warp's frame wait, and the scene
scripts' wait for the music, which returns every frame until the music gets
there); any other leaving pass stays counted, since what follows a loop's
exit, or a helper it calls, can be the original's. A pass an interrupt cuts is kept aside while the handler runs
and goes on where the handler returns to the S it was cut at, so it is taken
back too. A comparison's stretch goes on across a switch of stacks (S rising
further in a step than an instruction pulls, as the menus' task switch moves
it) until the runtime says the comparison is over, as an interrupt entry's
does. A comparison the runtime gives up because an interrupt's handler moved
to a stack of its own above the routine's (the menus' vblank handler: shadow
mode can't compare a routine whose stack the handler left) keeps its stretch,
uncompared, where the port's run had ended by then: the handler comes back
to the routine, and what the original runs of it from there is still the
port's, until the routine returns, the original reaches where the port
stopped (from there it is handed back, as at any stop), the handler comes
back to another stack, or the routine's task is switched away from
(`NativeObserver.given_up` says where the port's run ended). A port's
call-out that moves onto a stack of its own and comes back
(`NATIVE_STACK_SWITCH_DISTANCE`, the pixels' spread) stays the call-out's
until it returns, as the runtime waits for it; and where the runtime sets the
comparisons nested in a port's stretch aside at a call the port makes, the
port's stretch goes on from the caller's level, so the callee, itself a port
(the scene program's unpacker), doesn't end it. That count changes only when a port does. It costs the shadow runs
nothing measurable (gato-battle 47 s with it, 49 s without). The shipping
mode's count is still made, over every movie (`--native on --generated on`,
43 s for the library) and the routes (`route_check
--fallbacks`, `$CT_DRIVE_FALLBACKS` through `tools/movies/ctdrv.py`), and
ratcheted on its own, loosely: `tools/fallback_shipping_baseline.txt`, judged
as a subset since its counts wander with timing, fails on a routine it doesn't
have or on more distinct bytes or instructions past the slack in one. It is
the backstop for the reference play's: whatever that measure counts as a
port's that the ports don't run shows here. A route's count is over the frames it kept: as the run
ends, the input it kept is played again from its start by a `ct_drive` that
counts (`Machine.count_kept_frames`), so a search's probes, the attempts a
restore undid and the frames a load replays from a base are left out, and a
run resumed from a checkpoint is counted from its start. The replay is
checked against the run: it must end on the run's frame with the run's work
RAM, or the route fails rather than count another run, and input with frames
no buttons were kept for (a probe no restore undid, a state loaded with no
`.pads` beside it) is an error, not frames of no buttons. The shadow runs use the original sound driver, so the CPU
half's waits for its answers count there and not in the shipping mode.

**The ratchet.** `tools/fallbacks.py --check` against
`tools/fallback_baseline.txt`, a line per routine with its instructions,
distinct bytes and reason. A routine the baseline doesn't have, or more
distinct bytes in one (a new address run in place of a port), fails; so do
more instructions past 5% and 1000. Kinds are summed per routine so that a
port landing doesn't move its callees (what was `unported` becomes `handback`,
same routine); the report keeps them apart. `--update` takes a gain and keeps
the reasons of the lines it keeps.

**A movie ending inside a comparison.** The suite uses
`ct_dev --finish-fallbacks 120`: counts, traces and the saved end state freeze
at the movie's last frame, then at most 120 input-free frames let existing
comparisons finish. A `closed` record cancels only its own file's `open`
instructions, after the exact entry's match count increases by the number
of pending invocations, with no new calls, mismatches, interruptions or
abandonments. Completion is observed between instructions so another call
later in the same frame cannot obscure it. Unresolved comparisons retain
their original counts and ratchet failures; the tail's mismatches still fail
the shadow run. Comparison statistics and instruction diagnostics include
the tail; saved-game, trace and fallback counts retain the movie's cutoff.

`battle-black-tyrano` ends during scene teardown and field loading: 20
instructions under `scene_program_restart` and 44,292 under `decompress`
were previously reported as new or increased open fallbacks. Both comparisons
match within 11 extra frames. The cutoff saved state is byte-identical to
the reference at frame 49219; the movie still measures 18 fallback
instructions (10 handback, eight unported), with none new or increased.
No fallback baseline was raised to accept the loading call's partial count.
A replay from a reference state before the cutoff checks zero-, one- and
120-frame limits: all three retain identical cutoff files and saved states
before the appended completion records. One extra frame completes only
decompression, so restart's 20 instructions still fail the ratchet. Eleven
complete both. Reader tests also reject closure exceeding its own run's
open count or attempting to cancel another movie's comparison.

**On main**, when this came in, the reference play ran 46.5 KiB of distinct
code and 347 million instructions in place of hand ports, the same in two runs
to the instruction; by kind (a byte can be in two):

| | Reference play (28 movies, shadow) | Shipping mode (28 movies and 3 routes) |
|---|--:|--:|
| refused | 2.4 KiB, 10.3 M | 2.6 KiB, 40.6 M |
| handback | 33.8 KiB, 296.0 M | 36.5 KiB, 1,789 M |
| skipped | 0.0 KiB, 0.3 M | 0 |
| unported | 13.8 KiB, 40.8 M | 11.8 KiB, 134 M |
| all | 46.5 KiB, 347 M | 45.0 KiB, 1,964 M |

By subsystem (the reference play, KiB of code and millions of instructions):
field engine and events 9.8 KiB, 121.8 M; scene program 8.1 KiB, 68.1 M;
battle 8.8 KiB, 36.6 M, its frame work 3.2 KiB, 49.5 M, sprites 0.1 KiB,
4.4 M, NMI 3.3 M; menus, text and overworld 13.8 KiB, 21.8 M; field graphics
3.7 KiB, 31.6 M; scene engine 1.0 KiB, 6.1 M; the sound driver's CPU half
0.9 KiB, 3.8 M. Almost all of it is `handback`: the ports are dispatchers
around code that is still the original's. The largest are the
field frame's call-outs to the beam-coupled waits (`$C0:59D9`, `$C0:5A46`,
`$C0:CA76`; PLATFORM.md, "Timing without a beam"), the scene program's frame
tasks, and the battle frame work's call-outs.

**Refusals as failures** (`tools/fallbacks.py --refusals`,
`tools/refusal_allowlist.txt`). A native build has no original to hand a
refused call to ("Retiring the gate, deliberately", PLAN.md), so a refusal in
real play -- the shadow runs, the movies in the shipping mode, the routes --
fails unless its entry and condition have an allowlist line with a reason.
Each is printed with its count and its first four states (registers, the run,
and for code in work RAM whether its bytes are the image the generated blocks
were made from, `misses_describe`, which is how the code-presence guards that
turn away another program's code are told from gaps). On main, 23 entries
refuse in real play, 7,609 times: four are another program's code at a RAM
address a port was written for (the attract program's), and 19 are gaps,
listed in docs/TODO.md, "Refusals in real play".

Closing one is finding which of the guard's predicates the real state fails
(a print at the guard over the movie that refuses), reading the original on
that state, and making the port take it -- never widening the guard past
what the port does. The last of the library's gaps went that way:
- the equipment list's guard read eight rows even past the list's end,
  where the inventory screen had left `$FF`s; the drawing stops at the end
  and repeats its blank row, so the guard reads no further, and the list's
  making (`$C2:A463`, `$C2:A45D`) asks what the made list will hold rather
  than what the last one left (`battle-unequipped-armor`, 36 calls);
- Robo staying behind at Fiona's house steps as the party's third while the
  place already names who took it: the guard asked the place to name the
  one stepping. The original moves the one in the place, and, waiting, shows
  its pose on that one while the member turns: the port now does the same
  (`field/party_trail.c`), and leaves Y on whichever it posed (120 calls);
- Flea's scripts turn with facing `$1F`, past the facings' table, which runs
  on into the places': the code finds where the subject it numbers stands
  and keeps the facing found last (`script_places.c`, `no_way_found`; four
  calls);
- 600 A.D. Guardia Forest holds all 64 objects, and the battle's search
  starts at the count, one past the tables: the port starts at the last
  object, and the guard leaves to the original only a slot past the tables
  whose bytes (object 0's in the next tables) read as an enemy on the
  screen (`battle_start.c`, `stray_slot_is_an_enemy`).
Each was fuzzed with a variant made for the state (`tools/fuzz_routines.toml`:
`another`, `ended`, `past the facings`, `full map`), 0 mismatches but
`field_battle_begins`' one in about 4,000, which the old constraints show
too (TODO.md).

**The native shape, run.** `ct_dev_native_shape` and `ct_drive_native_shape`
link `ct_game_native_shape`, the ports with `ORIGINAL_EFFECT` compiled out
(now a static library rather than objects only). Nothing can compare them byte
for byte -- the scaffolding is what they leave out, and their timing differs
from the default build's by its accesses -- so the check is the outcomes: every
movie in the shipping mode exits 0 within its time (a hang), with no `halted:`
line (`ct_dev` prints one where the machine ended stopped), `accessor out of
range` or `page refused` line that the default build's run of the same movie
doesn't show; and the routes reach their outcomes (the fast set; all three
with `--long-routes`). A failure is traced through what the original's code
reads of scaffolding: `ct_dev --scaffold-reads original` (the default build's
shipping runs take it) notes each read the original's code makes of a byte
whose latest write was a port's `ORIGINAL_EFFECT`, by the instruction, and
`tools/effects_check.py --originals` gives each the source of the write; the
check writes the list beside its logs. On main, 101 such (byte, instruction)
pairs over the library.

When the check came in, all 28 movies ran to their end in the native shape,
and the fast routes passed, but all three whole routes failed, each traced and
confirmed by making the writes plain in a scratch build and running the route
again. The targeting's port fixed all three (docs/history/reimplementation-chunks.md,
"Phase 3, battle: the targeting"), and `tools/native_shape_exceptions.txt` is
empty:

- **Battle items land on the wrong side** (`beyond_time`, `bucket_lavos`):
  `battle_carry_out_member_action` wrote the acting member's record
  (`$0006`, game/battle/member_turn.c:499) and the item targeting's cell
  (`$000C`, :420) only as scaffolding, and the original's targeting, which
  the port called out to, read them (`$C1:C24A`, `$C1:C57F`). A revive healed an
  enemy instead; the final fights ended in wipes. With both writes plain, both
  routes reach their endings.
- **A walk stops short** (`telepod_lavos`): `object_run_script_from` cleared an
  object's step count only as scaffolding (game/event/calls.c:173), and the
  original's event scheduler reads it (`$C0:59F0`). The walk to the reaper
  statue in the developers' room ended a tile and a half away, and the seventh
  round was missed. With that and the battle's two plain, the route reaches its
  ending.

The movies don't see either: their presses drift in the shipping mode
anyway, and nothing in them checks an outcome.

### The census: how the game is entered

The fallback measure says what of the original still runs; the census says
why the registry still has to exist (PLAN.md, "The native program, and the
registry taken down"). `ct_dev --native on --census out.txt` (dev/census.h)
counts, per registered entry, each time the machine reached its address and
the runtime ran it, filed under how it got there -- a vector, the original's
code (generated or interpreted), or a port's hand-off through the machine:
a call-out, a stop or jump, a task's yield, a return into a frame the
original's code would have come back to -- with the caller that made it; and
each time a running port ran the entry's work as C instead (its
`NATIVE_TIME_AS`/`TIMED_AS` scope naming the entry). Idle entries are filed
apart, as `wait`: a pass of a wait loop the machine still makes is the timing
service's to take away, not a caller's. The runtime's census hooks
(`native_census_hooks`, port/native.h) carry the two counts the observer
doesn't. `tools/census.py --ct-dev build/main/ct_dev` plays the library and
adds the files up; `--files` reads ones already written, `--entries` lists
every entry. It plays the C sound driver, as `ct_play` does.

Over the 48 movies (shipping mode, chunk 11's tree): **259 entries are entered
by the machine, 21.5 M times**, none of them from the original's code -- 1.9 M
through the vectors, 12.4 M from ports' call-outs, 5.3 M from their stops,
1.8 M from returns into the original's frames, 0.1 M from the menus' task
switches -- and 19 wait loops are passed on the machine 406 M times (the menus'
ready loop and the field's frame wait the most). 1,293 entries are only ever
run as C. What holds the most entries to the registry, by distinct entries:

| Caller | Entries | Why it goes through the machine |
|---|---|---|
| the menus' object list (`$C2:051D`) and an object's script (`$C2:0F63`) | 35, 31 | each object's script and its commands are call-outs into ports |
| the battle's vblank (`$D1:E77C`) and line handler (`$D1:FE97`) | 20, 17 | the frame's work called out, the handlers entered through the stubs |
| the menus' main task and task list (`$C2:826F`, `$C2:81CF`) | 19, 11 | tasks switched on the machine's stacks |
| the field's frame (`$C0:0097`) | 15 | its budgets and the objects' update called out so shadow mode compares them a call at a time |
| the battle's timers and frame dispatch (`$C1:8290`, `$CD:0C95`) | 15, 13 | the script channels and the frame's steps called out |
| the vectors and stubs (`$00:FF10`, `$00:FF14`, `$00:0500`, `$00:0504`) | every handler | the interrupts reach each mode's handler by address |

**The sprite-object mode's vblank, as calls.** The first row and the last
met on the world map and the Epoch's screens: the vblank stub left the
machine at `menu_object_nmi` (`$C2:015E`), whose frame steps were call-outs,
and `menu_run_objects` called each object's handler by address. Now the stub
calls the handler (`core/console.c`, a table of the handlers it calls), the
handler runs its steps as C (`menu_run_or_call`, `menu/display.c`), and the
object loop runs each handler's registered entry as C (`menu/objects.c`,
`turn_on_the_machine`), each only where the registry would run that entry
there -- its P mask with the decimal flag, widths, page, bank and guard, the
return address pushed as the call pushes it -- and by the machine as before
where it wouldn't. The shipping mode charges a port's time when its stretch
ends, and each of those calls used to end one, so the machine's clock caught
up between the steps and between the objects; as one call the whole frame's
work landed at once, and the strict runs drifted (`menus-shop-inn`'s places
and checkpoints, `menus-epoch`'s saved game). The machine side lets the time
pass (`tm_sync`) before each step and each object's turn and again after it,
before the loop counts the turn or the frame is marked shown, as the calls
did: a script object's first turn on the Epoch's screen runs some 13 frames
of work, and with the time passing only before the next turn the loop's
count of it landed three frames early. The input step, run as C before this
too, keeps no pause after it. With both, `menus-epoch`'s strict run is the
one before the change, frame for frame; `attract-demo`'s map upload on frame
2250 lands a scanline earlier, and its curtain, whose stir mixes in the beam,
is ragged differently from 2252 (its strict pictures re-recorded, nothing
protected lost). Census of the four world-map movies (`field-65m-bc`,
`-dactyl`, `-dactyl-flight`, `field-12kbc-1f4`): entries the machine enters
82 to 61, its call-outs 75,053 to 32,978 and stops 30,252 to 28,197;
`menu_object_nmi`, `menu_run_objects`, the frame's five steps and the
overworld's objects (`overworld_lead`, `_follow`, the vehicles, the Epoch,
the dactyls, the sign, the region's song, 1,663 to 3,854 entries each) are no
longer entered by the machine. Their rows stay registered: the calls find
their contracts there, and the overworld's teardown retires them with its
main loop's held wait. `tools/machine_joints.py` doesn't move: its patterns
count stops and named call-outs, not the `m_jsr` and `m_jump_to` hand-offs
this replaced.

### The sound driver's teardown

The first subsystem taken off the gate (PLAN.md, "Retiring the gate,
deliberately"; "The native program, and the registry taken down", step 3),
on purpose: the gate no longer compares sound with the original's. The C
sound driver (`game/sound/`) is the only driver anything of ours plays --
`ct_play`, `ct_drive`, `ct_native` and every `ct_dev` run of the hand ports
(`--native on`, not `--generated only` or `--load-zst`) -- and it takes the
game's boot and commands as calls (`core/sound_commands.c`: `au_command`'s
SNES backend hands the buffer to the host, `field_first_start` and
`field_power_on` the boot). There is no `--sound-driver` any more.

**Deleted.** The port of the CPU half in bank `$C7` (`core/sound_driver.c`:
the boot's upload, the commands' and songs' transfers, the slots' records):
its 9 registry rows, its entry wrappers (5 returns through the emulated
stack), its 2 stops and its 29 scaffolding writes, with its rows in
`docs/ports.tsv`, `tools/fuzz_routines.toml` (and `ct_fuzz
--apu-port1-flips`, which only they used), `tools/effects_baseline.txt`,
`tools/uncompared_baseline.txt` and `tools/fallback_baseline.txt`. The
audio service's waits it alone made (`au_driver_exchange`, `_wait_moved`,
`_wait_echo_or_give_up`); the APU-direct backend (`--audio direct`), which
ran the original's driver out of the machine's time from those entries; the
sound lockstep (`ct_dev --sound-lockstep`, `dev/sound_lockstep.c`,
`sound_compare.c`, `sound_state.c`, `sound_audio.c`) and `ct_sound_fuzz`'s
mode against the original; `ct_dev --apu-port-margins`. `tools/machine_joints.py`:
registered entries 2,189 to 2,180, returns 993 to 988, stops 177 to 175,
scaffolding writes 2,066 to 2,037. The census (84 movies) enters nothing of
bank `$C7` by the machine, as before (the shipping mode had handed the
commands to the host as calls already): 284 entries by the machine, 17 of
them wait loops, and 1,366 run only as C, two fewer than before for the time
tags the commands and the boot were charged under (`$C7:0004`, `$C7:0000`),
which went with their rows: a command's own C is the caller's time now, and the time the original's driver took is the time model's
(`platform/time_model.h`), as before.

**What replaces the comparison.** Checks that need no original:
- `sound_test` (no ROM, CI): the driver on a made-up cartridge as it was,
  and now the host as the player plays it -- the boot and a run of the
  game's commands as calls, heard through the S-DSP and the native mixer
  side by side, every frame the same, the sound's 64-bit FNV-1a digest the
  one the test keeps;
- `ct_sound_fuzz` (the gate): random command streams, half as the game's
  calls and half through the ports as the scenes send them, with the game's
  songs and with made-up ones; every command answered, no sample placed
  where the echo writes, none dropped, the mixer's every frame the S-DSP's,
  and each kind of run's digest the one `tools/sound_golden.txt` keeps
  (`9b3ab44a6678e66a` and `a36313e5e19a4d59`). A digest is a hash of our own
  output, nothing of the cartridge's; one that moves is a change to what the
  driver plays, taken on purpose;
- the host's faults in every run that plays it (`sound_host_report`: a
  sample where the echo writes, a command dropped, exit 1), which the strict
  check, the native shape and the vblank check's shipping runs see; and the
  routes, which reach their outcomes with it.

**The reference play keeps the cartridge's driver, and why that is sound.**
Shadow mode, `--native off` and `--generated only` (and `ct_play`'s movie
replay and recording, and a ZSNES state) are the cartridge on the emulated
console: the original runs everything at its own timing, and the movies were
recorded against it. The console's sound processor running the cartridge's
driver, and the cartridge's CPU half speaking to it, are part of that machine
as the PPU is: they set when the CPU half's waits end, and so the frame every
press lands on. Putting the C driver there would make the reference play's
timing the time model's, which only estimates the original's, and move every
movie off where it was recorded. So nothing about the reference changed: the
other subsystems' shadow comparisons are a port against the original from
the same machine state, as before, and `--generated only` is still
trace-identical to `--native off`. What changed is that no port stands for
bank `$C7` there, so a port's sound command in shadow mode calls out to the
cartridge's CPU half on the machine, uncompared. The measures that count the
original's code are told it is retired rather than unported: the fallback
measure doesn't count bank `$C7` where the cartridge's driver plays
(`dev/fallbacks.c`; where the C driver plays it would, as nothing should
reach it), and `tools/burndown.py` leaves the bank out ("Retired code"). A
SNES build (target 3) needs a CPU half again: the cartridge's, from the
matching source, or the deleted port from history (TODO.md).

### The data subsystem's teardown

The second subsystem taken off the gate, on purpose: the gate no longer
compares the unpacker (`game/data/decompress.c`, the original's `$C3:0557`
and its jump at `$C3:0002`) with the original's routine. The rest of
`game/data/` -- the asset catalogue, the store and `packed_graphics.h` --
never had an entry: it is the cartridge's bytes by name, which
`tools/assets.py check` and the gate's `asset outside the catalogue` line
already hold to the catalogue, so the unpacker was the subsystem's machine
side, and all of it.

**The preconditions, and how each was shown.**
- *Fully ported, no original code for it in real play.* `tools/burndown.py`
  counted 0% left of bank `$C3`; the fallback measure's baselines had no line
  of the decompressor's code in either kind of play, only what the reference
  ran under its own comparisons (`open C3:0557`, `open C3:0002`); the census
  over the gate's 37 movies (shipping mode) entered `decompress` 636 times and
  `decompress_direct` 60, every one from a port's call-out (629 and 60) or a
  return into one (7) -- none from the original's code.
- *No generated code touches its state.* No entry is entered from the
  original's code, so in the shipping mode nothing generated calls the
  decompressor or reads its page; its cells (`decompress_*`, `$7E:0300-$0316`)
  are read outside it only by ports (the callers set the source and the
  destination; the scene program's transfer reads the destination and
  length), and the cells it keeps as it works (`$0309-$0316`) by nothing:
  `tools/effects_check.py` found no port reading them and no run-time read
  of them in the shipping mode, the map names nothing else there.
- *Its unreached rows covered.* `tools/unreached.py` had both entries at 0
  bytes no movie runs (the table above: data, 0).
- *Its recorded outcomes filled for the movies that protect it.* Every
  movie unpacks -- the maps, their scripts, the graphics, the scene program
  -- so the protected identities of all 37 recorded movies, strict and
  native ("Recorded outcomes", below), are the unpacker's: pictures for what
  it unpacks to be seen, places for the maps and scripts that move play on,
  `game` and `sram` for the scripts' consequences. Strict mode and `ct_native`
  ran only the C unpacker before this teardown as after it, so those
  observations were already the C's, and stay checked.

**Deleted.** The two registry rows, the entry wrapper (its pushes and pulls
of every register round the C, and the return through the emulated stack),
its row in `docs/ports.tsv`, its two lines of `tools/timing_exceptions.txt`
(seven of its callers' took them over: their stretches carry its estimate
error now, which a tag or a call-out had put on its entry, at most 258 K
clocks in loads of 0.5 M and more with the error 2-14% of them) and its rows
of `tools/unreached_baseline.txt`, and its scaffolding: the
three `ORIGINAL_EFFECT`s that left the cells it works in as the original
leaves them. Every caller calls it as C: the machine paths that made a call-out to
`$C3:0002` or `$C3:0557` (the battle's effect graphics and its action's
tiles, the menus' record sprites, the gate screen's swirl, the Epoch's gate
stream, the world map's scenes' Mode 7 pictures, the event commands `$82`
and `$83`, the scene's boot), those that ran its entry in place (the world
map's loads and the Epoch's map view, the credits' letters, the scene's
unpacker by resource), and the C paths, which named it in `TIMED_AS`. The decompressor's contract left nothing to check at those calls: no P
mask, width, page, bank or code-image guard, every register given back, and
N and Z as its last pull of the data bank sets them, which the one caller
whose declared set compares them (the world map's scenes' pictures) sets
the same way. `tools/machine_joints.py`: registered entries 2,241 to 2,239,
returns 1,023 to 1,022, scaffolding writes 2,065 to 2,062 (its count of
entries now reads the registry's table alone); `tools/burndown.py`: the
scene engine's code 3.8 K to 3.0 K and its entries 47 to 45, the total
264.1 K to 263.2 K, 0% left either way; `tools/unreached.py`: 361 entries
to 359. The census no longer has the two entries to count (it counts
registered ones); it wasn't run again over the library. It had no guard,
so none became an assertion.

**What its callers still on the gate see.** A caller's comparison covers the
unpacking it does, as any inlined callee's, but for the cells the
unpacker keeps its place in as it works: the original writes them, the C
keeps them in locals and nothing else reads them, so every comparison --
shadow mode's and `native_compare`'s, fuzzing's -- leaves them out
(`game_uncompared_wram` in `game/registry.c`, `native_leave_uncompared`).
The destination, the length and the work RAM port are compared, as before.
**Its time, kept as it was.** The strict and native plays' recorded
outcomes hold the ports' pace, and some protected identities sit at the
edge of their window (`attract-demo`'s place 12 in strict mode was entered
30 frames early of the reference, the most `places.ON_TIME` admits): a
first version that charged the time model's price on the machine, as
`ct_native` does, cost 0 of 37 strict ratchets, and one with the same
rate but none of the old call's structure still moved that place a frame.
So the unpacker is timed as its entries timed it (`charge_retired`,
`game/charge.h`): its own accesses at the rate of the entry it was reached
through, kept in `runtime/port/state_io.c` (`retired_rates`, 4,060 and
4,057 thousandths), the accesses the original makes besides at the jump's
entry's, as its charge was made, and taken as they are, as a DMA
transfer's time is, in the stretch that calls it
(`native_charge_at_rate`); a machine half that ran the entry in place adds
the entry's own 25 accesses (`unpack_stream_run`), and one that called it
on the machine also lets the time pass either side of it, as the call did
(`unpack_stream_called`, `charge_settle`). `attract-demo`'s strict play
is then identical to the one before over its 22,600 frames, but for the
uncompared cells and the stack bytes the entry's pushes left below S, and
all 37 strict ratchets pass. With no machine, as before, the time model's
price of the original's accesses.

**What replaces the comparison.** `unpack_test` (no ROM, CI): a worked
stream and 2,000 random ones, written by an encoder from items -- literals
and references, both forms of a reference, every segment layout, a short
control byte after a header, runs of literals as a control byte of 0,
references that overlap what they copy -- must unpack to exactly what the
items make, in either bank, nothing written past it, the length returned
and kept, and the work RAM port after it. A change to one of the format's
rules fails it (a reference's length taken a bit short fails 2,208 checks).
And the recorded outcomes above, in strict mode and `ct_native`, over the
real streams.

**The reference play keeps the cartridge's decompressor**, as it keeps the
sound driver: shadow mode, `--native off` and `--generated only` are the
cartridge on the console, and its callers' original code calls it there.
The fallback measure doesn't count it where it runs in the reference
(`dev/fallbacks.c`), and does where the hand ports play, as nothing of ours
reaches it; `tools/burndown.py` and `tools/unreached.py` leave its bytes out
as retired (`RETIRED_RUNS`: `$C3:0002-$C3:0004`, `$C3:0557-$C3:08B2`).

**What stays in work RAM.** Its storage didn't move: the source, the
destination and the length are the callers' interface in `$7E:0300-$0307`,
which ports on the gate set and read, and they move with the first of them
to leave work RAM; the working cells are written by nothing of ours now
(TODO.md, "The data subsystem's storage").

### The core services' teardown

The third subsystem off the gate, and only part of it: `game/core/`'s
services retire, its machine entry points stay. The machine-entry check
came first (`tools/census.py`, shipping mode, six movies of the sample:
`title-start`, `field-65m-bc-dactyl`, `battle-menu-buttons`, `menus-sweep`,
`events-epoch-lavos`, `field-factory-switch`). Of core's 50 registry rows the
machine entered 7: the reset, the boot and the interrupts' vectors and stubs
(41.7 K entries from a vector, 42 K through the stubs), which are how the
console reaches the program at all; and the palette animations' frame through
the original's jump at `$FD:FFF7`, 4,288 times, from four ports' call-outs (the
battle's frame dispatch and its pause, the field's script fade, the field's
tile-copy request). The controller's wait (`$C2:84F9`, an idle entry) the
movies didn't reach, but the menus' machine path stops in it where the
console's read isn't done. Everything else was run only as C: the input work,
the animated tiles, the palette's handlers, loads and clears, the screen gone
dark, the block move stub.

**Retired: 39 rows.** The palette animations (`palette_animate` and its jump,
the eight types' handlers, the fourteen channel modes, the two restore
paths, the load and the clear and their jumps), the animated tiles (the load,
the send, their jumps), the input work (`input_update`, the read, the button
assignments, the play time's tick) and `memory_move_stub` (`core/memory.c`,
deleted). Their entry wrappers went (31 functions, 7 guards); the four call-outs became runs as C under the
original's call. What remains of the machine in them is what a still-gated
caller needs: `palette_animate` and `tile_animation_upload`, the forms the
field's and the battle's machine paths run under the original's call, with
the registers the original leaves (`core/machine_forms.c`, which the bridges
ask where they asked the registry); `input_update`, which the menus' long
entry `$C2:8002` runs on the machine and which stops in the controller's wait;
and `display_go_dark`, which the field's and the events' machine paths
run.

**Guards are assertions.** `core/machine_forms.c` holds each form's contract
as its row held it -- widths, binary arithmetic, the guard -- and the C paths
(`palette_animated`) hold theirs: a state the contract doesn't take is
`program_breaks_a_contract`, which on the machine counts a break
(`native_contract_broken`, `ct_dev`'s `contract broken:` line, which fails
the suite, the strict check and the outcomes) and lets the call go to the
machine as the refusal did, and with no machine stops the program. Over the
library no core guard ever refused (the fallback baseline has no line of
core's).

**Time is charged as before.** The retired rows live on as time tags
(`game/time_tags.c`): entries marked `time_only`, which the runtime puts in its
table beside the registry's for `NATIVE_TIME_AS`, `TIMED_AS` and the charges to
name, and which no dispatch, report or guard ever finds. No shadow run
measures them any more, so each keeps the `cost_milli` it was last calibrated
to; their error is their callers', which the timing gate still measures. The
two timing exceptions of retired entries (`memory_move_stub`,
`tile_animation_load`) went with them.

**What still compares them.** Every caller of core's services is still under
the gate, and its shadow comparison covers the service's work inside its
stretch: the palette's colors, the tiles' uploads, the controller's sets and
the play time are compared as the field's frame's, the battle's, the menus'
and the scenes'. That is also why the services' scaffolding stays: the
palette's scratch cells, multiplier and pointer and the tiles' load leftovers
(`ORIGINAL_EFFECT`) are bytes those comparisons see, and the charges in them
are time. It comes down with the last of their callers (TODO.md). So the
movies that protect core are every movie, in shadow mode, through its
callers. The recorded outcomes cover the 37 movies of the gate's sample (end
states, pictures and places, checked in strict mode and `ct_native` against
their reviewed exceptions: `tools/outcomes.txt`), and the rest of the library
waits for its batched
run (TODO.md, "Recorded outcomes across the library") -- which a caller's
teardown needs before core's work goes uncompared.

**The measures.** `tools/machine_joints.py`: registered entries 2,241 to
2,202, returns through the emulated stack 1,023 to 1,013, scaffolding writes
2,065 to 2,064. The census over the six movies: 159 entries entered by the
machine to 158, the call-outs 314,265 to 309,977, nothing of core's but the
console. `tools/burndown.py` needs no telling: a retired routine's code is
walked as the callers' that now run it as C, so it stays counted as ported.
The fallback measure needs none either: in the reference play a retired
service runs inside its caller's stretch, which stands for it. The unreached
ledger dropped the 39 entries' rows, with the fuzz evidence of their 410
bytes no movie reaches (the palette's count-0 paths, the pulses, the fade to
black's ends and four channel modes, the tiles' stack-reading slots, the
clock's roll-over),
recorded against the original before they retired; its walks end at a
retired routine as they did at its entry (`tools/unreached.py`), and the
controller sample's reviewed reset hand-off was re-hashed over its unchanged
code.

**What stays, and why.** The console's eight rows: the vectors and the reset
are the machine's own way into the program, and go when the machine leaves
the product (PLAN.md, "The native program", step 4). The controller's wait
and the sample taken where it ends (`$C2:84F9`, `$C2:84FE`): a held wait, as
the attract's main loop is (TODO.md, "The scene main loop still starts at its
original boundary"), which the menus' machine path stops in and returns to
the original from. `program.c`, `tasks.c` and `sound_commands.c` were never
registered. And `display_blank_and_stop_interrupts` (`$C0:0B4E`) stays
registered: retiring it showed the reference play's own code still calling it
outside any port's stretch (198 instructions, 22 bytes, over the sample's
shadow runs: the fallback measure's new line), so the original's calls of it
need the port to stand for them until their callers are ports.

### The time warp's teardown

The fourth subsystem taken off the gate, and the first whose main loop the
main thread rests in: the gate no longer compares the time warp
(`game/warp/`, the tunnel of the time gates, `$FD:DA00-$FD:DD95`, its
handlers `$FD:DE15-$FD:DE97` and its routines `$FD:EAA6-$FD:F28F`) with the
original's code. Its earlier attempt stopped at the main loop (attract's
reason): a trip's frame end is the main thread's wait, and as a C loop a
port would stay suspended for the whole trip. Held waits whose
continuations the runtime dispatches by the wait's number (RUNTIME.md,
"Held waits") are what let it go.

**The held wait first, in its own commit.** The wait each kind's loop calls
(`$FD:EAEB`) holds at its loop (`$FD:EAED`) under `HOLD_WARP_FRAME`
(`game/holds.h`), the call's return to the kind's next frame on the stack,
which names the kind. Its continuations are two rows of `game_held_waits`:
`warp_next_frame`, which takes the frame mark, returns to the kind's loop,
makes the frame and holds again (it declares `.pulls = 2`, the loop's return
it takes, so shadow mode followed the original through the frame), and the
idle pass before the vblank handler has taken the frame. The frame entries
(`$FD:DA06`, `$DA65`, `$DAC4`, `$DB27`), the loops' waits (`$FD:DA03` and
the three like it) and the wait routine left the registry. Shadow over the
three movies that take a gate, before the teardown: `warp_next_frame` 159,
255 and 767 calls, every one matched, its stretches estimated within 238 to
778 clocks on average, of 54,305 to 64,868.

**The preconditions, and how each was shown.**
- *Fully ported, no original code for it in real play.* The burndown left
  nothing of the warp unported; the shipping fallback baseline had no `FD:`
  line of it; the three movies' census before entered its entries only from
  ports' stops and returns and its own idle passes.
- *No generated code touches its state.* Its cells (`warp_*` in
  `re/ram.toml`, the page `$0500` and the tables in `$7F`) are written and
  read by the warp alone; the one shared byte range, the light's pixel masks
  that the field's palette animations read after the warp, the C still
  writes (`picture.c`, `set_the_pixel_masks`).
- *Its unreached rows covered.* Its 9 bytes held by `unreached_safe` were
  the circle's clamps (fixed centres at 128, a radius of 0 or 2..127 from
  the cartridge's ramp). The claim is now checked where it is made:
  `warp_build_circle` stops a build with no machine at any other radius
  (`program_needs_the_machine`), and keeps the clamps.
- *Its recorded outcomes filled for the movies that protect it.*
  `events-end-of-time-gate` (the shortest trip), `battle-lavos-gameover`
  (the short trip) and `endings-bucket-lavos` (the flickering one) hold
  `places`, `game` and `sram` in strict mode and `ct_native`, pictures
  excepted in two of them; the places after each trip and the saved game
  are what a trip's end decides.

**Deleted.** Every row the warp had in `game_natives` (the starts, the
set-up, the ends, the end's going dark and story flags, the screen's wait
and turning on, the vblank handler); the entry wrappers and `warp/bridge.c`
(a sibling run under an emulated JSR); the guards `warp_has_a_destination`
(the field's entry holds that contract itself, and `scene_accept_continuation`
checks it) and `warp_bank_reaches_io`; all the scaffolding: the work cells
the original leaves in `warp_work`, the circle's half widths and centres,
the multiplier and divider left as the original's last step left them, the
work-RAM port, and A's high byte carried into the first frame (C reads the
bottom line's scale from RAM, `flicker`). The circle's step charge is no
longer scaffolding: it is the frame's pace on the machine (`charge_as`,
plain). Its rows in `docs/ports.tsv`, `tools/fuzz_routines.toml`,
`tools/unreached_baseline.txt` and `unreached_safe.toml`, and
`tools/uncompared_baseline.txt`.

**What calls it now.** In the shipping mode (`native_runs_retired_as_c`,
`m_runs_retired_as_c`: ON mode, in a port the runtime runs):
- the field's end (`field/main_loop.c`, `start_the_scene`) calls
  `warp_starts_on_the_machine`: the warp's page, bank and widths, the
  set-up, the screen's wait and turning on, and the hold for the first
  frame. The set-up's row had no guard or width precondition to keep.
- the console's vblank stub (`core/console.c`) runs the warp's handler as C
  and returns from the interrupt, where the stub names it.
- the held wait's continuations (`.retired = true`, which `natives_select`
  adds in ON mode only) make every frame and, after the last, the end: the
  frame's time passes first (`tm_sync`, as the end's own entry started in
  step with the original, so the line effects stop on the original's line),
  the screen goes dark, the story flags go back and the field's entry runs
  as the continuation it was. The `$210E` difference that kept the end an
  entry of its own went with the comparison.
- the screen's wait (`$FD:EADA`) waits where the port stands
  (`NATIVE_WAKE_VBLANK_FLAG`, the flag up or the picture in its blank, a
  pass of the loop after, as its idle entry went on) and the turning on is
  the timing service's (`pf_snes_screen_on_wait`).

Elsewhere -- shadow mode's reference play, `--generated only`, the tools --
the field stops at the kind's start as before and the console's stub jumps
to the handler, so the cartridge's warp runs, compared with nothing; the
fallback measure doesn't count it there (`dev/fallbacks.c`,
`the_cartridges_warp`) and does where the hand ports play, and the burndown
and `tools/unreached.py` leave its runs out (`RETIRED_RUNS`).

**The guards.** The held wait's two continuations keep theirs, the runtime's
contract for running them: the warp's page with its zero, the frame taken
(and the wait's return to a kind's loop) or not. Each is a claim the game
keeps, checked over real play by the refusal measure
(`tools/fallbacks.py --refusals`).

**Its time, kept as it was.** The ports run the warp's work under the old
entries' names (`TIMED_AS`, `NATIVE_TIME_AS`), and those names have no row:
`game_kept_estimates` keeps each routine's calibrated `cost_milli` (the
set-up 24,782, a frame 8,303-8,332, the vblank handler 6,710, going dark
4,300, the story flags 6,994, the screen turned on 4,429), which the runtime
appends to its table where only a tag finds them (`native_keep_estimates`;
the machine never enters one). In the shipping mode the three movies play
the same frame by frame as before the teardown -- work RAM, VRAM, CGRAM,
OAM, save RAM and the picture over 5,600, 16,192 and 196,000 frames
(`tools/trace.py diff`) -- but for the warp's own scaffolding cells and the
stack bytes below S, and on two frames of the first the circle table's
line 0, off the picture, which the original's widest circle's last fraction
overwrote.

**What a held wait now does to what it leaves.** The field's end starts the
trip on the game's fresh stack from inside the field's frame, whose
call-outs the original never returns to. The machine's RTS into the first
frame used to unwind past them, and the runtime dropped them; a
continuation run as C makes no such move, and the wait rests at the very S
the field's frame's call-out waits to return to. So where the main thread
rests in a held wait, call-outs still waiting at or below its S are dropped,
counted as abandoned (`drop_what_the_held_wait_left`, runtime/port/native.c):
nothing is suspended in a held wait, by its definition.

**The measures.** Census over the three movies (shipping mode): the machine
entered the warp's entries 747,559 times before (the frame wait's idle
passes 743,589, the screen's wait 1,599, the vblank handler 1,181 by the
console's jump, the frames 1,181 by return, starts, ends and the screen
turned on 3 each), and 0 now: its 1,181 frames and 747,440 idle passes are
the held wait's continuations, run by the wait's number; the vblank handler
runs as C in the console's stub 1,181 times, the set-up, the screen turned
on, going dark and the story flags 3 times each as C. Frame ends with a
routine suspended, driven a frame at a time in `ct_drive --native on`, over
the frames the warp's handler is installed: 112 in each of the three
movies (the set-up's own time, about 110 frames of the original's with the
screen dark, which the field's event entry owes and pays in the screen's
wait: TODO.md, "The time warp's set-up is owed with a port suspended"),
against 113, 112 and 113 before; over the whole movies 1,170, 8,001 and
181,423 frames, against 2,680, 9,510 and 182,933, as the trip no longer
leaves the field's frame suspended past it. A `ct_play`
quick save inside the tunnel: a state saved at
events-end-of-time-gate's frame 4,000 (in the held wait, the tunnel
running), loaded in a fresh `ct_drive` and in `ct_play`, and played 500
frames past the trip's end into Medina, ends with the same work RAM and
picture as the run that didn't stop, with nothing suspended or replayed. `tools/machine_joints.py`:
registered entries 2,235 to 2,211, held
waits' continuations 2 to 4, returns through the emulated stack 1,021 to
1,016, stops 98 to 97, time tags that take the machine 9 to 8, scaffolding
writes 2,062 to 2,047.

**What replaces the comparison.** The three movies' recorded outcomes, in
strict mode and `ct_native` (whose trip, `warp_runs`, is the same C), the
vblank check over the shipping mode's video writes, and the frame-by-frame
equality with the shipping mode before the teardown measured above.

### Recorded outcomes

`tools/outcomes.py` keeps the reference play's results in `tools/outcomes.txt`:
one line per movie per kind, digests and small numbers only. `game` and
`sram` are SHA-256 of exactly the saved-game and save-RAM bytes that
`strict_check.SavedGame` compares, with the same play-clock, random-start
and slot-checksum exclusions. `pictures` holds RGB FNV-1a64 fingerprints
of the drawn 256 by 224 picture, every 300 completed frames and at the first
and last frame, including neighbours two frames either side. `places`
holds every location entered and its completed-frame number, as
`tools/places.py` measures. `input` binds these to the movie's SHA-256,
frame count, format version and saved-byte selection digest. Editing a
movie or changing the projection requires a deliberate new recording.

`ct_dev --outcomes` and `ct_native --outcomes` print only picture hashes;
`ct_dev` draws pixels only for those frames unless another option needs
them. The suite digests its shadow run's `--end-state` and log into
`outcome-<movie>.txt` beside them. Strict and native checks digest their
own runs into `outcomes/strict` and `outcomes/native` before discarding
their scratch end states. This adds no play to the suite. Raw end states,
logs, pixels and traces stay in ignored build output; only the digest
ledger, ratchet and reasons are committed.

Record and check existing suite output (use `--movies NAME...` for a subset):

```sh
python3 tools/outcomes.py --record --reference build/suite --movies title-start menu
python3 tools/outcomes.py --check --runs build/suite/outcomes/strict --mode strict --movies title-start menu
python3 tools/outcomes.py --check --runs build/suite/outcomes/native --mode native --movies title-start menu
```

Checking artifacts needs neither a ROM nor a reference executable, log or dump.
For a fresh run, give `--ct-dev` (strict) or `--ct-native`, `--rom` and
`--runs DIR` to `--check`, with `--movies` as needed. This runs only the
hand ports, at most two processes, and checks their digests against the
ledger. The ROM supplies assets and the saved-byte selection, never a
reference play; `--assets` reuses an imported pack. For example:

```sh
python3 tools/outcomes.py --check --ct-dev build/main/ct_dev --rom roms/ct-usa.sfc --runs build/outcomes/strict --movies menu
python3 tools/outcomes.py --check --ct-native build/main/ct_native --rom roms/ct-usa.sfc --runs build/outcomes/native --movies menu
```
It rejects missing data, duplicate rows, incomplete/faulted runs and stale
movie inputs. It reports each movie's exact matches, drift and differences.
An identical picture within two frames is drift, accepted; a place entered
within `places.ON_TIME` (30 frames) in the original order is accepted drift.
An extra place, wrong order, changed end state or picture outside that
window is a difference. No perceptual threshold silently excuses changed
pixels: a line or two of harmless visual difference can have a per-movie
`pictures` reason after checking that play cannot change. An exception is
not evidence of equivalence.

`tools/outcomes_baseline.txt` protects individual identities, separately
for strict and native: each end digest that matches, each checkpoint frame
and each place index entered on time. An exception in
`tools/outcomes_exceptions.txt` needs a movie, mode, kind, observed SHA-256
and reason. The digest pins that kind's complete observed values: SHA-256
of its artifact row's values joined by ASCII spaces, with no newline. This
includes every checkpoint neighbour and every place visit, so a blackened
picture, removed visit or changed end digest cannot hide under an exception.
While any difference remains in the kind, a changed digest fails even with
`--update`; deliberate re-recording requires reviewing the new outcome and
its reason. The failure reports the newly observed digest. An entire kind
that now matches the reference (including accepted drift) can tighten via
`--update`, after which the old difference cannot return. Exceptions cannot
excuse losing a protected match.
`--update` adds matches and preserves omitted movies; it never drops a
protected identity. Before filtering, the check validates every recorded
movie's membership and header; the suite also validates the gate sample.
Missing, unreadable or malformed movies fail even when a subset is selected.
New movies or missing records fail closed. The suite
runs both recorded checks after strict and native checks, and clears their
old artifacts before a run, so skipping a producer cannot pass on stale data.

The ledger records the whole library, 84 movies (2,289,640 frames): the gate's
37 first, the other 47 from the batched library run of `92026499`. Strict mode
passes its ratchet on all 84 and native mode on all 84, with these protected
observations (after that run's drift-only calibration; `battle-ozzies-fort`'s
native row was recorded after it, once the chest messages it reads joined the
asset catalogue):

| Mode | Saved game / save RAM matches (movies) | Picture match / drift / differ (checkpoints) | Place match / drift / differ (visits) |
|---|---|---|---|
| Strict | 42 / 80 of 84 | 1,062 / 178 / 6,508 | 436 / 163 / 271 |
| Native | 36 / 72 of 84 | 583 / 97 / 7,068 | 93 / 461 / 330 |

No movie matches every kind in either mode. These are existing debts,
explicitly reasoned per movie, not a claim of harmless pixel changes.
Power-on native save RAM starts at zero whereas the reference initializes
it to `0x60`; the other end-state debts and place visits are named in the
exceptions. A teardown can rely on the protected matches, not on a movie's
aggregate ratchet pass.

A batched library run checks the recorded outcomes as the sample's runs do.
A change that moves a drifting movie's observations (a recalibration) is
re-recorded deliberately: `--check` names the newly observed digest, and the
exception's line takes it with a reason. Recording never silently changes the
ratchet.

**What a teardown cites.** The replacements each remaining subsystem needs:

| Teardown | Recorded checks it relies on, alongside routes and assertions |
|---|---|
| Events, field and world map | `game` for story/party/inventory; `places` for progression; `pictures` for the field and map |
| Battles | `game` for party, items and results; `places` for returning to play; `pictures` for combat presentation |
| Menus and saving | `game` and `sram` for selections and persistence; `pictures` for screens and cursors |
| Scene engine and video | `pictures` for presentation; `places` for scene transitions; `game` for story consequences |
| Storage and program entry | The same `game`/`sram` projection from the new storage, plus the movies' `places` and `pictures` |
| Time model | `places` and `pictures` with the documented tolerances, and `game`/`sram` to detect changed input outcomes |

A teardown names its movies, mode, kinds and protected identities here,
records them before retiring the comparison, and resolves the exceptions
it needs to depend on. Storage changes keep the same saved-game projection
through a host dump adapter. These samples do not prove every intermediate
state: routes and assertions remain necessary. Sound continues to rely on
its own tests and digests described in "The sound driver's teardown".

### Scene continuations: contracts and review validation

Direct scene continuations now consult the registry before entering C. A
refusal leaves the original entry and call frame intact. The scaler retains
its binary-mode contract; race and Epoch decimal callers stop at its row
entry. The ending's preparation also uses a binary-only zoom division, so
its guard refuses decimal preparation before changing the picture. A picture
already awaiting upload can still return in decimal mode.

The clear, final sound send and task completion now check the continuation's
code-image guard. Completion also checks the saved task-list widths and data
bank before unwinding. The audit added checks to boot-to-program, the clear's
next startup step, title music, caption drawing, credits scrolling, both
picture gatherers, and scene/warp field entry. Title startup propagates a
refused music wait instead of proceeding to the first frame. The clear leaves
its DMA-page high byte even on refusal, while its standalone continuation
preserves the incoming high byte.

The other direct continuations retain upstream contracts: scaler start/sample
entries share binary widths and preflight the remaining bitmap; sound retry
and long-send entries share the sender's image guard; curtain entries share
the same page and code guard; boot blanking establishes M8; console reset
admits either mode. Perspective callers preflight the builder's image and
data on the camera page. Script command continuations use ROM code, validated
command operands and the interpreter's binary context; sound returns preserve
that context. Panorama helpers are covered by their setup's image guards;
turning setup's unpacker/scripts use checked calls. Warp frame loops keep the
warp page and widths across their frame wait. Host-only GameState calls have
no machine registry contract.

The map sampler computes fixed-point coordinates from row and column indices.
Its one rounding helper handles source-period rounding and the tile address;
the traversal no longer carries packed-coordinate overflow between samples.
Two thousand randomized matrix/origin cases agree with both the previous
build and the interpreter, without skips. Continuation fuzz covers accepted
paths, decimal refusal, modified clear/send/task-list images and a modified
title music wait. Standalone fuzz supplies contracts specifically for tail
continuations; ordinary call-outs remain interpreted. Synthetic runtime tests
check decimal/guard refusal and that this table cannot override a live runtime.

Shadow comparison recognizes aliases of one C function at the same stack
depth as one invocation. Field entry's two addresses no longer start duplicate
comparisons inside each other, allowing its comparison exemption to be
removed; a synthetic regression verifies the outer comparison still finishes.
Seven isolated shadow runs also compare held waits with interrupts enabled:
414 warp waits, the complete short and shortest trips and frame loops, the
title's music wait and ten sound retries all match. Their combined-run
exemptions name this evidence. The five bitmap/race exemptions name the
independently compared interrupt handlers and explicitly retain the
mid-picture interaction gap in TODO; standalone fuzz does not close it.

`tools/uncompared_evidence.py` makes eight further comparisons repeatable:
the flickering trip's wait and frame continuations, and the scene program's
start, setup, first frame, clear, post-clear and handback entries. In a combined
shadow run an enclosing port reaches its wait or call-out first, so shadow
mode abandons these nested continuations before their own comparison point.
The isolated fixtures remove those ancestors, preserving the callees and
their registry contracts. Each scene entry matches in `title-start`; each
flickering entry matches a complete 767-frame trip with real interrupts in
the first 20,000 frames of `endings-bucket-lavos`. All eight fixtures require
nonzero calls and every call matched, with no mismatches elsewhere in the
run. Their baseline reasons name this evidence. The suite runs applicable
fixtures for selected movies, or all eight with `--evidence`; these extra
logs never enter the combined timing or fallback totals.

Six narrowed movies, including the complete Beyond Time ending, pass with
zero mismatches; attract and Epoch pass again after the final startup fix.
The final 29 focused fuzz fixtures compare 24,371 cases without a mismatch;
the broad startup fixture retains the pre-review build's 11 mismatches in
384 comparisons (16 call-out budget skips), recorded in TODO. No mismatch
was newly introduced. The map rewrite adds 2,000 matched before/after cases.
The shadow runtime regression and 18 tool tests pass; portstyle and portsmell
have no new findings. Measured scene/warp joints change from 800 to 802:
stops from five to seven, the two additions being guarded refusal boundaries.
The sampled gate is reserved for the merge coordinator and
was not run for this review. The four requested code-structure follow-ups are
in TODO.

Merging main at `1fea6c49` keeps the sound CPU-half deletion: scene commands
continue through the C driver's host ports. Their bounded echo wait retains
the scene's 82-clock reference poll and held C continuation, with no direct
execution adapter for the retired driver. The native backend gives the C
driver time to answer through its host interface. The evidence runner has
one sequential-child policy per movie worker; both branches' concurrency and
result-retention tests are kept. The fallback ledger keeps main's measurement
reductions and this branch's documented open-comparison ownership entries;
retired menu code and completed scene stops are not restored. Both histories
and port ledgers are retained, and PLAN reports the merged measurements.

The merged tree builds with two jobs, including the matching check, and all
15 C test executables pass. The Python suite runs 469 tests with ten skipped;
its sole initial failure was PLAN's stale file count. Correcting that count
and refreshing its style table leaves all eight tests in the affected module
passing on rerun. Portstyle and portsmell pass. Full-length shadows of
`attract-demo`, `events-epoch-lavos`, `events-jetbike-race-won`,
`battle-lavos-gameover`, `events-end-of-time-gate`, `menus-sweep` and
`endings-beyond-time` each exit successfully with zero mismatches. The sampled
gate is left to the merge coordinator and is not run for this integration.


### Direct continuations preserve their entry contracts

Review found that a scripted menu could return with decimal mode set, while
the directly called fade requires binary arithmetic. The field bridge checks
native mode, widths, page and data bank, but not decimal mode. The scripted
tail now checks the fade's complete registry contract (native mode, M8/X16,
binary arithmetic, field page and low-RAM bank). Refusal stops at `$C0:2824`
with the former tail's own don't-care set. Overworld setup and menu return
have the same gap after their unpacker/menu bridges; both now check their
callee's full contract and stop at `$C2:23CF` or `$C2:2556` on refusal.

Every continuation made direct in the loop-joint chunks was checked against
the actual registry P mask, emulation requirement and guard. All require
native mode. The remaining proof, including decimal mode, is:

| Continuation | Callee contract and caller guarantee |
| --- | --- |
| Scripted menu to fade | M8/X16, binary, field page, low-RAM bank: full check restored after the bridge. |
| Frame work and objects-in-view to sprite table | M8/X16, field page, low-RAM bank; decimal permitted. Successful bridges preserve native mode, M8, page and bank zero; the caller restores X16. Both drawable-row checks remain. |
| Object defaults to opening scripts | M8/X16, binary, field page, bank zero, bounded nonzero object count. The caller has the same requirements; defaults make no machine call-outs and change none of them. |
| Field walk and ripple completion to frame completion | Only native mode and a low-RAM bank; all P values permitted. Their successful bridges preserve native mode and bank zero. |
| Tile-copy completion to frame completion | Same preconditions are satisfied, but the interrupt boundary is restored as described below. |
| Empty battle request to field walk | M8/X16, field page, bank zero; decimal permitted. Request entry and successful battle bridge guarantee these. |
| Screen-effect close to scene close | M8/X16 and field page; decimal and any data bank permitted. Effect entry and successful bridges preserve these. |
| Event fade to scene start | M8/X16 and field page; decimal and any data bank permitted. Event entry and successful bridges preserve these, including when the fade takes no steps. |
| Overworld menu return to map restoration | M8/X16, binary, object page and low-RAM bank: full check after the menu bridge. |
| Overworld setup to first frame | Same contract: full check after the unpacker bridges. |

The three battle continuations were already restored to stops following the
route regression. Changes that only replaced a timing tag on an existing C
call do not change dispatch or its preconditions. The earlier chunks and
their validation are in `history/reimplementation-chunks.md`.

The tile-copy tail again stops before `field_finish_frame`. A timing scope
only partitions the charge; it is not an interrupt-delivery boundary.
Combining these operations lets frame completion increment
`field_frame_pending` before the tile send's pending vblank is delivered.
Returning to the runtime first preserves the former order: charge the tile
send and deliver interrupts, then enter frame completion. No runtime,
calibration, registry, exception or fuzz-domain changes were needed.

**Validation.** Both game shapes build with two workers. Six narrowed shadows
(`new-game`, `battle-menu-buttons`, `field-65m-bc-dactyl`, `menus-epoch`,
`menus-field-menu`, `events-end-of-time-gate`) have zero mismatches. Requests
compare 6,398 of 6,411 calls, overworld loop 6,202 of 6,213, setup all eight,
and map restoration its one call. These entries have no interrupted calls;
the remaining calls were abandoned within enclosing comparisons.
Same-seed before/after fuzz of eight request variants and the two overworld
entries compares 2,064 states on each binary, zero mismatches and identical
skips. All 200 setup states still exceed the existing comparison budget;
the request/loop domains stop before the changed tails.

Focused scratch probes therefore exercise the actual scripted tail and shared
overworld check against the registry's own masks and guards: all 256 P values,
both emulation modes, all 256 banks and four pages (524,288 configurations).
The fade accepts 4,128 and refuses 520,160 at its original entry; both overworld
contracts match throughout. Another 10,000 randomized CPU states, with each
pending-NMI/IRQ combination, confirm the tile tail stops without calling frame
completion or changing the CPU beyond its destination. This checks the boundary,
not cycle-by-cycle interrupt execution. Both probes fail on the predecessor.
The style and smell ratchets pass. Static don't-care analysis reports zero
findings; it has no call logs, so it does not establish dynamic coverage.
No sampled gate was run for this correction; the merge coordinator owns
integration validation.

The assigned-file census rises from 1,748 to 1,752: stops 52 to 56, all other
categories unchanged. Against the branch's original 1,799, this retains 47
fewer joints: stops 62 to 56, registry lookups three to two and ordinary
machine timing tags 40 to zero. Main-loop stops are field 13, battle six and
overworld five. The remaining contracts are recorded in TODO.

### The largest fallbacks left: Zeal's fall and the scripted exits

### The largest fallbacks left, 2: what the measure counted that the ports run

Chosen by size from `tools/fallback_baseline.txt`: the three lines with no
routine of their own, `C2:----`, `FF:----` and `7E:----` (0.51 M of its
0.72 M instructions), and the scene scripts' wait for the music under
`7E:398A` (50 K). The shipping mode ran none of them: over `menus-shop-equip`
(`C2:----` 25,680, `FF:----` 129,885 in the reference play) and the attract
demo the shipping mode runs no instruction of the original's. Each was the
measure's (dev/fallbacks.c), not a port missing, so this chunk is the
measure's three edges, found by following the extents at the first counted
instruction of each (the menus' window commands' block move, `$C2:ED8B`; the
scene program's main loop, `$7E:3023`).

- **A comparison a vblank handler gives up.** The menus' vblank handler moves
  to a stack of its own above the interrupted routine's, and shadow mode
  can't compare a routine whose stack its handler left: it ends every
  comparison under it as interrupted (`native.c`, the `left` case). The
  measure ended their stretches with them, so the rest of each routine after
  the handler's return -- a window list half drawn, a shop's setup, the
  records' number tiles in bank `$FF` -- ran with no extent and counted as
  code with no call on record. Such a stretch now stays, uncompared, until
  its routine returns, the handler comes back to another stack, or its task
  is switched away from (`abandon_under_the_handler`, `settle_abandoned`).
- **A pass an interrupt cuts, matched by the wrong S.** A wait loop's pass
  an interrupt cut was taken back where the handler returned to the pass's
  own S; one cut inside a routine the loop calls (the scene program's main
  loop calls an empty one) came back with the routine's S, and stayed
  counted: about a quarter of the main loop's passes in every movie that
  plays the title. The pass is matched by the S the interrupt was taken at.
- **The pass that leaves a wait.** It stayed counted, though the idle entry
  makes that pass too: four instructions a frame of the warp's frame wait,
  and all of the scene scripts' wait for the music, which returns every
  frame until the music gets there. It is taken back where it ran only the
  loop's instructions and a port takes over at the exit (the field's
  screen-on wait), or returned having run only the bytes its entry declares
  (`NativeEntry.idle_return_span`, set on those two waits), so a helper the
  exit called, or a path the port refuses, stays counted.

The audit before merge asked for three more guards against hiding a gap:
the shipping mode's count ratcheted (`tools/fallback_shipping_baseline.txt`,
seeded over the sample and the long routes: six lines, 4,530 instructions,
the refusals TODO.md's "Refusals in real play" holds and the program's empty
routine under the vblank list), the leaving pass bounded by its entry's
bytes rather than by what fits before a return, and a given-up stretch
ended where its port stopped.

**Measured.** Over the gate's 37-movie sample the reference play's count went
from 425,995 instructions to 9,892 (`FF:----` 0, `C2:----` 1,525, `7E:----`
160, `$7E:398A` 56, `$C2:5DC4`, `$C2:EFBE`, `$C2:8839`, the warp's waits and
nine more menu lines 0), with the same `open` lines. The baseline takes the
sample's fall line by line, the movies outside it held at their earlier
counts until the library's batched run takes them: 0.72 M to 0.31 M. What
the sample still counts is the scene program's own small hand-backs (the
title's setup's call-outs, `7E:59D5` 1,680 the largest) and the sound
command's wait (`7E:377E`). No port changed, so nothing was calibrated or
fuzzed; the measure is checked by the shipping mode's count, 0 over the same
menus' movies.

### The code the movie library does not reach

Before a subsystem loses the original as an oracle, `tools/unreached.py`
compares its candidate live instruction bytes with the union of the standard
library's `ct_dev --coverage` files. It uses the disassembler's live code and
a conservative audit walk, assigning a byte to its own entry before an inlined caller and resolving
shared helpers in registry order. Macro-generated event entries participate
too. Coverage retains the instruction's M/X widths, so an immediate operand
is counted at the width actually executed. The estimate includes guarded
error paths and original-only handoffs; these need explicit explanations,
not invented fuzz coverage. A stop declaration says where a port can hand
back, not that every path stops there: the audit follows continuations and
all decoded callees until another registered entry owns them. It does not
use burndown's stop/call-out pruning. Missing instructions and unresolved
calls or jumps are listed by entry and address and fail the inventory.

The inventory covers 361 entries in six subsystems plus the screen command's
separately registered field restart dependency, using the existing
84-movie coverage union (the movie inputs agree with the checkout supplying
it). Optional hand recordings are outside that standard library. No game
code changed in this work. Since the scene merge registered the map-turning
callback (`scene_turn_map`, `$7E:7441`) as its own entry, the attract frame
pass no longer reaches it and attract has no unreached bytes. The data
subsystem's two entries, at 0 bytes, left the inventory with its teardown
(359 entries since): its code is retired, which the walk leaves out as
burndown does ("The data subsystem's teardown"), and the two event
commands whose graphics-overflow arguments cite the unpacking's C
(`characters.c`) were reviewed again and their digests taken anew.

| Subsystem | Bytes no movie executes | Executed in matched fuzz trials | Reviewed safety argument |
|---|--:|--:|--:|
| attract | 0 | 0 | 0 |
| data | 0 | 0 | 0 |
| core | 421 | 417 | 4 |
| warp | 104 | 95 | 9 |
| overworld | 1,354 | 1,318 | 36 |
| event | 1,498 | 1,426 | 72 |
| field restart dependency | 5 | 0 | 5 |
| Total | 3,382 | 3,256 | 126 |

`tools/unreached_baseline.txt` is the greppable ledger: a movie manifest,
one inventory row per entry (including zero-byte entries), and exact byte
runs attached to passing fuzz recipes or reviewed safety reasons. The
first record's 102 cited recipes made 100,400 trials, 70,097 matched, zero mismatches
(108 are cited since the screen effects and the controller's ready sample got
entries of their own); skips
contribute no evidence. `ct_fuzz --coverage` accumulates original instructions
only after a trial matches. Logs with accessor/page/resource diagnostics
are rejected, even when the comparator reports a match. Broader casting
trials reproduced the already documented out-of-domain bank-wrap divergence
in `event/characters.c`; the accepted error-path fixtures end below the
graphics half-bank without overwriting the running script. No new game bug
was found.

**A targeted state.** The random follower recipe seldom reaches the end of
a walking leg: a changed travel byte resets its step first. The
`overworld_follow:unreached_leg_end` case ties the previous travel to the
current one, ties the goal coordinates to the follower's position, and
chooses one or two remaining legs on either axis. Its 1,000 matched states
exercise the turn onto the second leg and the return to standing. The same
approach covers party arrivals, full palette-animation slots, landing near
another vehicle, wraparound dactyl formations and completed Epoch scenes.
Cases in `tools/unreached_cases.toml` extend the existing recipes and explain
their constraints. Their base states are replayed with `--native off` and
cached with the movie, state specification and interpreter fingerprints.
Cross-entry evidence requires the file's explicit list of shared C callers:
the word comparisons share the byte comparison's operator helpers, and the
dactyl shares the leader's ground lookup and next-tile helpers. An unrelated
interpreted call-out visiting an address on both machines is not evidence
for its port.

The screen command is a regression example for the conservative walk.
Its restart-only stop at `$C0:4009` must not remove the other dispatch arms.
`event_cmd_screen_command:unreached_effects` executes all 90 uncovered
effect-handler bytes, including the curtain at `$C0:4145-$C0:4180` and ripple
at `$C0:41C8-$C0:41DB`, in 1,000 matched comparisons. Those bytes now belong to
the per-effect entries (`event_screen_cover_left` and its siblings), machine
bridges over the same `run_screen_command` table, so the file's shared list
names the screen command as their caller. The restart's five-byte wait
belongs to `field_restart_wait_for_vblank_loop`;
that dependency has its own ledger row rather than disappearing at an
ownership boundary.

**What is reviewed rather than executed.** Most exclusions are rejected
selectors, missing-resource diagnostics, explicit error handoffs or branches
excluded by the cartridge's fixed resources. Every world-map record omits
the optional scene-tile stream; every warp radius is zero or 2 through 127,
with the circle centred at 128. Six overworld bytes are a narrower reviewed
equivalence: the sign-refresh increments after returning from the map view
and time dial have exactly the port's nonzero-sign predicate and byte
wraparound, and their flags are dead. Those arms did not run in the fuzz
comparisons. `tools/unreached_safe.toml` records the arguments, and TODO.md
keeps the returning-screen fixture and guard-to-assertion obligations.
The conservative traversal also exposes the controller's reset transfer
(4) and the event engine's original-only unknown-command diagnostic (6).
Their source-bound review arguments are explicit. The field restart's
five-byte wait uses the documented vblank timing escape, not byte-exact
timing equivalence; its following 11 bytes are `field_restart_in_the_dark`'s,
a field entry outside the audit.

**The ratchet.** `tools/unreached.py --check` re-derives the current port reach
and checks the ledger's movie hashes, recipe fingerprints, proof coverage
and safety dependencies. Added entries or changed reach, removed/edited
movies, stale recipes and changed safety dependencies fail closed. Added
movies can only increase the recorded union. The sampled suite uses this
full-library record, never its own partial union to infer absence. With
`--evidence`, it also replays the cited recipes and verifies that their clean
results execute every byte claimed in their ledger rows. This is an
instruction-coverage obligation, not a claim of exhaustive path or input
coverage.
A fuzz row's committed fingerprint names its recipe, base state and movie,
not the C: hashing every source into the ledger staled all of it at any
edit anywhere. Each fresh run instead records `implementation`, a hash of the
C sources and headers in `game/`, `runtime/` and `dev/`, and `--recheck-fuzz`
accepts only runs of the tree being checked, so a changed helper or
comparison harness still needs new evidence, without tying the ledger to one
host's binary. A safety row hashes the definitions its argument rests on,
not whole files: `depends` in `tools/unreached_safe.toml` names each as
`path:symbol` (a function, macro, enum constant or table, with every line
of its file naming it, so callers and table rows citing a guard count; or a
`re/ram.toml` variable less its prose), and the ledger adds the entry's
registry row without `cost_milli`, the functions the row runs and guards
with, and for a macro-generated event entry its command-list row and the
generator. Recalibrating the timing estimates or editing an unrelated
function leaves the arguments standing; a change to what one cites stales it
for review.

The scene-continuation merge transfers `$7E:7441-$7E:74DC` from
`attract_frame_pass` to `scene_turn_map`, and `$7E:31BE` from
`attract_vblank_start_loop` to `scene_frame_wait_done`. Re-deriving both
trees' exact reach shows that every retained audit range is a subset of
the original full-library range; none gains bytes. The former 156-byte
callback exclusion leaves this ledger because its separately registered
scene port owns it now (its comparisons are in `docs/ports.tsv`). This is
an ownership correction using the existing library record, not an absence
inferred from the sample. The retained safety entries keep their registry
contracts apart from calibrated costs; added scene RAM fields do not move
the sign-refresh cells, and the warp continuations do not change the circle
builder or its radius selection. Each refreshed row records that review;
the merge gate replays the unchanged fuzz byte obligations against the new
implementation fingerprints.

To inspect or rebuild the record, supply the verified ROM with `--rom`:

```
python3 tools/unreached.py --coverage-dir build/library-coverage
python3 tools/unreached_fuzz.py --ct-dev build/main/ct_dev --ct-fuzz build/main/ct_fuzz --ledger tools/unreached_baseline.txt
python3 tools/unreached.py --coverage-dir build/library-coverage --record
python3 tools/unreached.py --check --recheck-fuzz
```

Recording requires every standard-library coverage file and complete clean
evidence or a reviewed reason; it cannot silently accept an unchecked run.
ROMs, states, logs and coverage remain local build artifacts. The no-ROM
unit tests exercise width accounting, stale inputs, missing/overlapping
proofs, conditional-stop dispatches, unresolved walks and implementation
fingerprints; `fuzz_test` checks that failed, refused and
timed-out trials never contribute coverage and that counter notifications
are applied to both machines.

The focused unreached and burndown unit tests pass without a ROM. The
sampled gate's `--long-routes --evidence` run rechecks the complete ledger
and replays all 108 cited recipes alongside its normal validation.

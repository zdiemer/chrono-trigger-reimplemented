# Authoring movies from game state

Test movies are the input side of validation: shadow mode only checks the code a
movie reaches, and coverage only names the instructions it runs
([PORTING.md](PORTING.md), [RUNTIME.md](RUNTIME.md)). Recording one by hand
([PLAYING.md](PLAYING.md)) is the quickest way to get one; long routes that have
to win boss fights are another matter, and that is what the tools here are for.
[movies-wanted.md](movies-wanted.md) lists the code no movie runs and the movies that would reach it.

`ct_drive` (`dev/ct_drive.c`) runs the development system as a server, and
`tools/movies` drives it: it reads the game's memory to decide what to press —
whose turn it is in a battle, where the party is standing, whether a text window
is waiting — instead of counting frames. A run logs the buttons it held on every
frame, so a finished run *is* a movie. And because a route reads the machine
instead of counting frames, it is also the one test of the mode the player
ships in, where movies can't follow (see Routes as a test).

```
route script ──> tools/movies ──> ct_drive (the interpreter, or --native on)
      ▲                │
      └── game memory ─┘            run's pad log ──> movie.ctm ──> ct_dev --native shadow
```

## The driver

By default `ct_drive` is the same machine as `ct_dev --native off` — the interpreter and
the modeled hardware, no ported routines and no generated blocks — so anything
it reaches, a `ct_dev` replay of the same input reaches too. `tools/movies/check_drive.py` is that check: it replays a movie's first 20000
frames in both and compares all of work RAM (identical, at the commit that added
these tools). Redo it after changing either program; it needs the ROM, so it
isn't part of the unit tests.

```sh
python3 -m tools.movies.check_drive --rom roms/ct-usa.sfc --sram save.srm \
    --movie oracle/movies/saves/endings-beyond-time.ctm --frames 20000 \
    --ct-dev build/main/ct_dev --drive build/main/ct_drive
```

```sh
cmake --build build/main --target ct_drive
build/main/ct_drive roms/ct-usa.sfc save.srm
build/main/ct_drive --native on --generated on roms/ct-usa.sfc save.srm   # the shipping mode
```

`--native off|shadow|on` and `--generated off|on|only` mean what they mean to
`ct_play`: `frontend/natives.c` chooses the registered entries for both, and
for `ct_dev` (generated blocks only for the ROM they came from; `--generated only`
leaves the ported routines out in on mode, and in `ct_dev` in every mode; see
`natives.h`). `--generated` defaults to `on`. Only `--native
off` is the machine movies are made on; see Native code for what the others
change.

Commands arrive one per line and answer one line starting `ok` or `err`
(`dev/ct_drive.c` has the full list):

| Command | What |
|---|---|
| `run <n> <pad> [<pad2>]` / `runw ...` | run n frames holding pad (JOY1 bit order), and pad2 on the second controller (JOY2; default none); `runw` also returns the watched bytes. A route names the second controller's buttons `P2:SELECT+P2:START`, held in the pad word's high 16 bits (`mkmovie.pad_of`), which is how the load screen's erase question is reached |
| `watch <addr:len> ...` | the byte ranges `runw` returns |
| `movie <path> <to>` | run to a frame with a `.ctm`'s buttons |
| `peek <addr> <len>` | read the bus |
| `save`/`load`/`drop <slot>` | in-memory machine copies, 4096 slots |
| `savefile`/`loadfile <path>` | save states, the format `ct_dev --save-state` writes |
| `shot <path>` | PNG of the last rendered frame |
| `wram <path>`, `frame`, `quit` | work RAM dump, frame number, exit |
| `native` | mode, routines suspended now, and how loads rebuilt suspended routines (Native code) |
| `poke <addr> <hex>` | **diagnostic only**: write work RAM |

`poke` is for asking "what does this flag do"; a run that used it is no longer
reproducible from input, so it must not become a movie.

### Native code

With `--native on`, what a run does to the machine is the same as without it —
commands, slots and state files work as before — but two things needed care.

**Suspended routines and owed time.** A hand port that calls code that isn't
ported is suspended on a stack of its own until the callee returns
(`native_call`). In the field that is rare at a frame's end; in battle and in
parts of the ending it is usual (in `battle-tyrano-lair-walls`, at 33 of the 79
frame ends sampled). And ON mode charges a port's estimated time after it
stops, a scanline at a time, so at a frame's end the ports may still owe time
the machine hasn't let pass (`native_owed`). A slot or a state file holds the
machine, neither that stack nor that debt. `ct_play` handles it by taking a
quick save only where it can (PLAYING.md, "Saves"), which a route can't:
its searches copy the machine at the frame they are at. Loading such a copy
with a fresh runtime would leave the rest of each routine to the original code,
which can read registers the port never set, and drop the time owed. So
`ct_drive` keeps a *base*, the last frame end where nothing was suspended and
nothing owed (a fresh runtime there is the runtime the run had; refreshed each
such frame with `snes_clone_since`, so it copies only the written pages), and
the buttons of every frame since. A copy made anywhere else keeps the base and
those buttons, and loading it restores the base and replays them, which brings
the routines and the debt back as they were. A state file keeps them beside it,
`<path>.base` and `<path>.base.pads`.

Two kinds of wait are held by the runtime on purpose, and each says how it is
saved, restored and dropped (`runtime/port/native.h`; `tests/port/suspend_test.c`
has a case for each):

- *A yield in the middle of a task's work* (`native_yield_call_dc`; the menus'
  `menu_yield`, a step waiting for the next frame through the task switch at
  `$C2:821E`). The port is *parked*: set aside, off the routines in progress, while
  the other tasks take their turns, and it goes on when the machine is back at
  the yield's return with its S, frames later. A parked port counts in
  `native_suspended` (`native_parked` counts them alone), so it is never
  *saved*: ct_drive keeps its base from before the yield and *restores* the
  port by replaying from there, as for any suspended routine (a step waits a
  few frames for a slide, a description typed or the healing's numbers, and
  as long as the player takes over a choice made a frame at a time, a member
  to use an item on). ct_play's quick save waits for a frame end with none
  parked, and after two seconds takes one with only parked steps, which the
  original's code finishes from their waits once it is loaded (PLAYING.md,
  "Saves"): the machine at a yield is the original's but for what nothing
  reads after it. It is
  *dropped* where its task can't go on from the yield: the runtime asks the
  menus at every frame's start and every stack switch
  (`menu_task_goes_on_from`: the task still waiting there, its record keeping
  the stack the wait kept, or being taken back into it), and a task stopped --
  every task is, as the menus close -- or started afresh over the wait is
  dropped, counted abandoned, as the original's routine never finishes either.
  A second yield to the same return and S drops the first (its stack has been
  used again). A task that goes on after a drop anyway has the original's code
  run the rest of its step, as for a port abandoned.
- *A frame wait the main thread is in at nearly every frame's end* (the
  field's, `$C0:EC71`; the sprite-object mode's, `$C2:0348`; the menus' wait for
  the frame's uploads, `$C2:81CA`). A port waiting there in place, as the
  battle's do, would leave a routine suspended at every frame end, and no base
  would ever come. So the wait is *held*: `native_wait_loop_held`
  (`tm_wait_for_handlers_held`) has the shipping mode stop the port at the
  loop's head, where the idle entry registered there waits and goes on as the
  port would after it -- the port's continuation is the idle entry. Nothing is
  suspended, so a copy made then *saves* everything and *restores* exactly with
  a fresh runtime, and there is nothing to drop. Shadow mode makes the same
  port wait through the original's loop and compares its work after the wait,
  and compares the stop at the head the shipping mode makes with the set it
  declares there (`PF_LOOP_HELD`, checked by `tools/dontcare_check.py` as a
  stop).

The replay is checked against the copy (their save-state images), and every
one matches: over both routes in the shipping mode (`route_check --mode
on:on`), 2507 loads in `beyond_time` and 128 in `telepod_lavos:shell` were
away from a base, and all replayed exactly (`replayed differing` 0 in
`ct_drive`'s exit line). A mismatch would be counted "inexact" in `native`'s
answer and the replay kept, since its routines are intact. Before owed time
counted, a base could hold a debt the fresh runtime dropped, and 15 of 2253
such loads in `beyond_time` came out inexact. The price is longer replays
where exact frame ends are rare: the scaled-bitmap scenes of the Lavos fights
and the ending run `scene_scale_bitmap_row` row after row, so nearly every
frame end there owes time or has a routine suspended, and `beyond_time` had
two stretches of 18846 and 30370 frames without a base. The route took 1004 s
rather than about 715. Only a copy with no base loads without its routines and
debt (`lost`): one made after a `poke` away from a base.

**Write tracking.** A `poke` or a load replaces memory without the bus, so it
calls `snes_mark_all_written` as before (PORTING.md, "What shadow mode costs"),
and the base's next refresh copies everything. In shadow mode a poke or load
also restarts the runtime: the machine copies of routines in progress didn't
see the change, so their comparisons are dropped (what `ct_play`'s loads do),
and the mismatches found so far are reported first.

Checked with the same command script (movie into a battle, slots saved and
loaded while routines were suspended, a state file, a poke) against the
previous `ct_drive`: `--native off` answers identically and ends with identical
work RAM, and so do `--native on --generated only` (generated code alone keeps the
original's timing) and `--native shadow` (no mismatches); `--native on
--generated on` gives the same result on every run.

## Routes

A route is a Python script (`tools/movies/routes/`) that plays one scene from a
cartridge save to an ending. The three ending movies in `oracle/movies/saves`
come from these:

```sh
python3 -m tools.movies.routes.telepod_lavos --prefix work/telepod \
    --movie oracle/movies/saves/endings-telepod-lavos.ctm
python3 -m tools.movies.routes.bucket_lavos --prefix work/bucket ...
python3 -m tools.movies.routes.beyond_time --prefix work/beyond ...
```

`lavos_lost` (`battle-lavos-gameover.ctm`) takes `telepod_lavos`'s way to
Lavos's shell (its first phases, `to_the_shell`) and loses the fight, for the
Day of Lavos a loss plays.

The other New Game+ endings are hand-run routes: a Lavos fight and ending takes
100,000-200,000 frames, so `HAND_RUN_ROUTES` in `tools/suite.py` excludes them
from the gates, including `--long-routes`. Their recordings belong in scratch
storage, not `oracle/movies`. `tools/movies/other_endings.py` shares the fights
and recording setup; the routes supply the ending's walks and completion checks.

```sh
python3 -m tools.movies.routes.successor_of_guardia --prefix work/successor/run \
    --movie work/successor.ctm
python3 -m tools.movies.route_check --mode on:on:c -j 1 \
    --scratch work/shipping successor_of_guardia
```

`successor_of_guardia` (technoplaza ct-state17 slot 2, 129223 frames) plays
the telepod, all three Lavos fights with Crono and Marle, the return to the fair,
the royal guard's invitation, the royal family's reveal, and Frog and Leene's
credits through The End. The save's story counter `$27` selects `$6E = 5` in
location `$052`'s ending dispatcher; the route verifies that flag and the final
halt of `$057`'s script, rather than treating a timeout as completion. Short
waypoints pass east of the fountain and the visitor on the stairs; the guard
is approached from the north because his south side is beyond the exit.
Shadow has zero mismatches and refusals, no page/accessor/asset diagnostics,
and identical sound lockstep. Coverage adds 159 instruction bytes over the
48-movie library, none in the `$7E` scene program: this ending starts none of
modes 5, 6, 10/11/14/15 or 12. `ct_native` and strict mode both complete the
129223-frame input with exit 0 and no stop; that alone does not prove the
ending in modes whose timing differs. `route_check --mode on:on:c` reaches
The End at frame 176603, without a wipe; all six restores away from a base
replay exactly, with no C sound-driver errors. The static checks and route-check
unit tests pass. Since no movie joins the library, this route does not require
a narrowed movie-suite run or changes to its aggregate baselines.
The per-ending evidence and remaining saves are in
[movies-wanted.md, target 7](movies-wanted.md#7-the-other-endings).

`legendary_hero` (technoplaza ct-state19 slot 2, 144640 frames) shares the
bucket approach and fights in `other_endings.py`: the shell's boss imitations,
inner Lavos and Core, then Robo and Atropos, Tata's journey, the party in place
of the villains, the staff credits and The End. Story `$5D` selects `$1AB = 1`;
the ending advances it to 6 and finishes in `$025`'s final wait/jump loop,
which the route checks. All fights win without a wipe. Shadow exits 0 with
zero mismatches and refusals, no page/accessor/asset diagnostics, and identical
sound lockstep. It adds one instruction byte over the library (`$C0:A88C`),
no `$7E` code, and starts none of modes 5, 6, 10/11/14/15 or 12. `ct_native`
stops at frame 14237 in `$FD:B438`, an enemy coming into battle (TODO.md);
strict mode runs all frames with exit 0. The shipping route (`on:on:c`) reaches
The End at frame 168451 without a wipe; all 12 restores replay exactly and the
C sound driver reports no errors.

`unknown_past` (technoplaza ct-state20 slot 2, 160225 frames) shares the bucket
and Lavos fights, then plays the credits and character vignettes through Robo's
dance and The End. Story `$69` selects `$1AA = 1`; completion checks flag 6,
object 0 waiting for Robo and Robo's final wait loop. Inner Lavos needed 24
retries, with only winning input retained. Shadow exits 0 with zero mismatches
or refusals, no page/accessor/asset diagnostics and identical sound lockstep.
Coverage adds 92 instruction bytes over the library, no `$7E` scene code;
none of modes 5, 6, 10/11/14/15 or 12 starts. `ct_native` and strict mode both
run the entire input with exit 0 and no stop. Shipping-mode `route_check`
(`on:on:c`) reaches The End at frame 146361; all 12 restores replay exactly,
with no C sound-driver errors.

`people_of_the_times` (technoplaza ct-state21 slot 2, 160570 frames) plays the
bucket, all three Lavos fights and the character credits through The End.
Story `$7E` selects `$05B`; the route follows the credits to `$05F` and checks
object 1's final loop at `$035C`. The default Core seed is 2 (the other fights
use 1). Seed 1 won the battle but the reference sound driver stalled before
the final reveal, waiting for a command echo at `$C7:0635`; another 9000 frames
and button probes did not release it. Seed 2, retried from the Core checkpoint,
finishes. The helper accepts a Core seed offset so that input can be regenerated.
Shadow exits 0 with zero mismatches and refusals, no page/accessor/asset diagnostics
and identical sound lockstep. Coverage adds 16 instruction bytes over the library,
none in `$7E`; none of modes 5, 6, 10/11/14/15 or 12 starts. `ct_native` and strict
mode run the whole input with exit 0 and no stop. Shipping-mode `route_check`
(`on:on:c`) reaches The End at frame 146552; all 12 restores replay exactly,
with no C sound-driver errors.

So do the menu, event and battle movies made for what no movie ran
([movies-wanted.md](movies-wanted.md), Targets): `menu_sweep`
(`menus-sweep.ctm`), `save_point` (`menus-save-point.ctm`), `epoch_lavos`
(`events-epoch-lavos.ctm`), `black_tyrano` (`battle-black-tyrano.ctm`, whose
fight `policies.party_with_lapis` wins by healing with what the bag holds),
`guardian` (`battle-guardian.ctm`) and `tech_sweep` (`battle-tech-sweep.ctm`).
They were recorded on one build and made again on the next before they went
into the library, which reproduced them byte for byte: the timing change
between was the shipping mode's alone, which the interpreter's runs don't
see. The second batch joined the same way, made again on chunk 11's build and
identical: `tech_sweep_spekkio` (`battle-tech-sweep-spekkio.ctm`),
`dactyl_flight_65m_bc` (`field-65m-bc-dactyl-flight.ctm`), `floor_switches`
(`field-floor-switch.ctm`), `battle_menu_buttons` (`battle-menu-buttons.ctm`),
`factory_switch` (`field-factory-switch.ctm`) and `northern_ruins_switch`
(`field-northern-ruins-switch.ctm`).

Each starts from the technoplaza cartridge save its movie header names
(`games.technoplaza.net/snes/chronotrigger/savestates.php`), which is in
`oracle/saves/technoplaza/` and is the route's `SAVE`; `--sram` picks another.
`--prefix` is where the run's checkpoints, log and
screenshots go — a scratch directory, not the repo. Add `--rom` or `--drive` (or
`$CT_ROM`, `$CT_DRIVE`) when they aren't at `roms/ct-usa.sfc` and
`build/main/ct_drive`, and `--native`/`--generated` (or `$CT_DRIVE_NATIVE`,
`$CT_DRIVE_GENERATED`) to run it on native code. `--until <phase>` stops it once
that phase's checkpoint is written. A route ends by printing `outcome reached
<goal> frame <n>` (exit status 0) or `outcome failed <why>` (1): its asserts
along the way, and a check of its goal at the end (the Dream Team all spoken
to; the farewells at the Gate and the King in the Beyond Time ending).

A route is a list of phases, each ending in a checkpoint file. Re-running it
resumes at the last phase that finished, so a failing phase can be iterated on
without replaying the hour before it. Delete the checkpoints to start over.

## What the route runner does

`tools/movies/campaign.py`:

- **Dialogue.** `$7E:0127` is non-zero while a text window waits, so A is pressed
  only then. Blind A presses are what put stray inputs into battle menus.
- **Control.** `find_player()` looks for the object that moves *both* ways when
  the pad is pushed (against a no-input baseline), which tells a cutscene apart
  from the player's turn and also finds which object index is the party leader
  on this map.
- **Walking.** `bfs.py` searches with the machine as the map: from a checkpoint,
  try the four directions, keep the positions they reach, and stop when the goal
  holds — a position, or simply "the screen is another map". Only the winning
  path is replayed with logged input, so the search costs frames of emulation,
  not frames of movie. Doors that need a direction held get a second pass
  (`push`); the overworld, which moves in whole tiles, uses `ow_bfs`.
- **Talking.** `talk()` tries the spots around an object in turn, faces it,
  presses A and checks the flag the conversation should set. An object that
  walks is aimed at where its walk comes nearest the party as well as at where
  it stands now: `object_path()` samples where it goes over the next few
  seconds and puts the machine back, so what the route aims at is a property of
  the object's script and not of the frame the route happened to look on (see
  Routes as a test).
- **Fighting.** `bot.py` reads the battle menus (`$7E:95D5` whose menu is open,
  `$95DB` which page, `$9609` targeting, the cursors, `$A62D` the targets,
  `$5E30 + slot * $80` the stats) and verifies every step: press, then wait for
  the value that should change. `policies.py` decides the action from the state
  — revive, heal, the strongest tech the MP allows, otherwise attack — with
  every threshold jittered, so retries differ.
- **Backtracking.** Every command menu is a checkpoint. A party wipe reloads one
  of the last few, reseeds the policy and re-rolls a small random delay before
  acting (which shifts the fight's timing, so the retry plays out differently).
  With a checkpoint per decision, a lost fight costs seconds, not the run.

## Turning a run into a movie

`Run.write_movie()` (or `python3 -m tools.movies.mkmovie <pads> <out.ctm>`)
writes the pad log as events. Headers follow [ORACLE.md](ORACLE.md): what the
movie covers, the save it needs with its `# sram crc32`, and `# frames N`.
`{FRAMES}` and `{CRC}` in a route's header are filled in when it writes.

Then validate it the usual way, from power-on:

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --sram save.srm --movie m.ctm --frames N \
    --native shadow --coverage cov.txt --calls calls.txt
```

Adding it to the library (`oracle/movies/saves/`) moves the gate's measures
that sum over the library, the timing gate's drift and the fallback ratchet's
counts; [REIMPLEMENTATION.md](REIMPLEMENTATION.md), "The timing gate", says what a batch of movies
needs ("Adding movies").

**Routes are timing-independent; movies are not.** A route reads the machine, so
it still works after the hardware model's timing changes — re-run it and it
finds the new inputs. The `.ctm` it produced is a fixed list of frames: change
the timing and it drifts, and a long one usually stops reaching its ending. When
a timing change lands, re-run the routes rather than patching the movies.

## Routes as a test

The player ships with `--native on --generated on`: hand ports where they exist,
generated code elsewhere. Hand ports charge estimated time, so a run in that
mode drifts from the original's timing, and a movie's presses land on the wrong
frames; the movie library can only test `--generated only`, which is exact. A
route plays the same scene whatever the timing, so whether it still reaches its
goal is a test of the shipping mode: of what the hand ports do along a whole
scene, not of their timing.

`tools/movies/route_check.py` runs routes in chosen modes, each from its save
and from scratch, and reports each outcome, with the frame each phase was
reached at, modes side by side. It exits 0 only if every run reached its goal.

```sh
python3 -m tools.movies.route_check                        # telepod_lavos:shell and beyond_time, on:on:c
python3 -m tools.movies.route_check --mode off --mode on:on --scratch work/check -j 2 \
    telepod_lavos:shell beyond_time
```

A mode is `off` or `<native>:<generated>[:<sound driver>]`: `on:on:c`, the
default and the suite's, is the build that ships, with the C sound driver
playing in the SPC700's place (`ct_drive --sound-driver c`, as `ct_play` does
by default; SOUND.md, "Playing it"), whose commands take no time, so its
frames are not `on:on`'s. `ct_drive`'s machine copies and state files then
hold the driver's fields beside the machine's, and it exits 1 at `quit` where
the host dropped a command or placed a sample where the echo was writing,
which the route reports as failed. The original driver's runs in the shipping
mode are the vblank check's, over every movie, so the suite doesn't run the
routes in `on:on` as well.

A long run can be cut short (a time limit on the session running it, a
restart): with `--scratch` kept and `--resume`, running the same command again
carries each route on from its last checkpoint rather than from its save. Each
run's directory records the route, the mode and the SHA-256 of the `ct_drive`,
the ROM, the route's script and `campaign.py` that played its checkpoints; a
run where any differs starts over, so no outcome rests on phases another build played. The suite
takes the same through `--routes-scratch DIR` (with `--long-routes`, all three
ending routes at once where `-j` allows):

```sh
python3 -m tools.movies.route_check --scratch work/long --resume -j 3 beyond_time bucket_lavos telepod_lavos
```

A restore in a native mode costs a replay from the last base while native
routines are suspended, and in a battle they are for its whole length (its
native code spans the frames), so the last base is from before the fight: each
restore there took about 20 seconds at the end of Lavos Core. The ending's
player search (`campaign.py`, `find_player`), which restores five times, called
every 20 frames while the battle wound down, made that stretch take hours; it
now doesn't probe while anything is suspended, when no pad moves an object
anyway, and `beyond_time`'s ending runs in minutes again.

`telepod_lavos:shell` is the field test: the world map, Leene Square and the
telepod exhibit, walking, talking and dialogue, stopped at the Gate.
`beyond_time` is the battle test: two Lavos fights, then the ending's walks and
talks.

As built, `--mode off --mode on:on -j 2` (frames at each checkpoint):

| Route, phase | `off` (interpreter) | `on:on` (shipping) |
|---|---|---|
| `telepod_lavos:shell` | reached, 140 s | reached, 116 s |
| &nbsp; fair / exhibit / shell | 2728 / 4008 / 5102 | 2728 / 4024 / 5118 |
| `beyond_time` | reached, 604 s | reached, 715 s |
| &nbsp; inside / form2 / core / ending | 2554 / 3762 / 27520 / 58135 | 2554 / 3802 / 27446 / 57631 |
| &nbsp; the end | 106136 | 105640 |

Since bases wait for owed time too (Native code, above), `on:on` reaches the
same goals: `telepod_lavos:shell` at 2728 / 4016 / 5110 in 120 s,
`beyond_time` at 2554 / 3762 / 27229 / 57331 and the end at 105334, in 1004 s.

Both modes reached every goal: the same maps in the same order through the
ending, both Lavos fights won without a single backtrack, the King found at the
same spot. The frame counts drift by up to 500 frames, the accepted difference.
In the shipping mode the searches and fights made 2253 loads with routines
suspended (15 of them inexact, before owed time counted) and the field route 56
(none). Neither route
needed a change to survive the drift. The `off` run through the `core`
checkpoint wrote the same input and the same state file as the `ct_drive`
before these options.

### A race in the route, not in the game

Run whole rather than to `shell`, `telepod_lavos` failed in `on:on` and passed
in `off`: in the development room's last scene the route spoke to four of the
five Dream Team members and gave up on the fifth (`dream team 0F`, then
`outcome failed AssertionError: all five of the Dream Team`). The cause was in
`talk()`, not in a port.

The fifth member is the object that paces the left aisle of the Dream Team room
(map `$5701`): its script walks it three tiles up the aisle and back, forever,
while the party can walk only into the aisle's near end -- a search of every
position the party can reach in that room covers about the bottom fifth of it
and nothing above. So a spot beside that object exists only while it is near
that end. `talk()` sampled its position once per spot and walked to a point
fixed before that walk began, which made reaching it a race with its own step:
whether any of the thirteen spots could be walked to came down to where the
object stood at the moment the route looked, which is a frame count. The
interpreter won that race by a hair -- the tenth spot, `(0, 32, "UP")`, 12657
frames into the attempt -- and the shipping mode's timing put the object
elsewhere, so no spot could be reached and the route gave up.

What shows it is the route and not the mode: from one and the same state in
that room the two modes play the scene identically, frame for frame (all five
spoken to at frames 126710, 127129, 127612, 127945 and 129762 in both), and a
phase shifted by 40 frames of idling before the talk fails the same spots with
the same goals in *both* modes. Nothing about the shipping mode is involved
except that its timing picks a different phase.

`talk()` now aims a walking object's spots at where its walk comes nearest the
party as well as at where it stands (`object_path`, `aims`), so the aim is the
near end of the aisle whatever the phase, and its press loop covers a full pass
of a walking object rather than most of one. Measured over six phases 40 frames
apart in that room, all five members are spoken to from every one of them, in
2300-3400 frames each and the fifth at the first spot in 700-1800 of them;
before the change the same measurement failed outright at the third phase, in
both modes, and the interpreter's own passing run had needed its tenth spot and
12657 frames for that member alone.

Run whole, `telepod_lavos` now reaches the ending in both modes, the fifth
member spoken to from the near end of the aisle in each:

| Route, phase | `off` (interpreter) | `on:on` (shipping) |
|---|---|---|
| `telepod_lavos` | reached, 1242 s | reached, 1645 s |
| &nbsp; fair / exhibit / shell | 2728 / 4008 / 5102 | 2728 / 4016 / 5110 |
| &nbsp; cavern / inner / core | 16909 / 18659 / 53046 | 16948 / 18698 / 52166 |
| &nbsp; ending / devroom / tour | 125213 / 128553 / 139606 | 151907 / 155247 / 166362 |
| &nbsp; kokubo / credits | 140473 / 146990 | 167229 / 175453 |

The `off` run ends 10347 frames earlier than the one before the change, all of
it that one member's spot search. The frame counts of the two modes diverge far
more here than the 500 frames of the shorter routes, because the Lavos Core
fight's retries take different paths, which is what a route is for: what must
match is the outcome, not the frames.

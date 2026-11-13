# Native reimplementation

ct_native is the product. The last commit containing the emulator and oracle tooling is tagged `emulator-oracle`.

## The lean gate

`tools/suite.py --ct-native build/main/ct_native -j 2` runs the sample in
`tools/gate_movies.txt`, the New Game+ and inventory item-use routes, portstyle,
portsmell, doc-names (no doc or RAM-map prose naming code that is gone), Python unit tests and the native CTest tests. Every sampled movie
must reach its declared frame count, exit successfully and avoid an unexplained
dark ending. `suite OK` is the final pass line; wall time includes every stage
and must be below 600 seconds. Its stages run side by side in lanes: the movies
(then their outcomes), the routes, the snapshots, the unit tests and the source
checks. The three that run ct_native share two slots (`tools/native_slots.py`,
lock files the suite names in `CT_NATIVE_SLOTS`), so no more than two of its
processes run at once and none waits for another stage to end; movies and
routes start longest first (routes by the times `route_check --scratch` keeps). The separate `--long` batch runs the whole library, every public route
module and the wide-view end-state check at `-j 2`, with stage logs and `findings.json` under `build/suite-long`.
Gameplay failures remain findings, but a missing binary or a stage launch
failure aborts the batch with nonzero status and a diagnostic. It reports
findings without a gate time budget; fixes follow from those findings, and
commits, merges and pushes never wait for the batch.

Recorded outcomes protect the selected saved-game bytes, cartridge save RAM
and ordered place visits. The ledger records the movie hash and projection
hash so a stale artifact or changed projection cannot silently pass. Missing
movies, incomplete states and runtime diagnostics fail. Existing native
baseline matches remain protected; a pinned exception accepts only the recorded
difference, never a new state or missing visit. Subsets do not prune other rows.
A movie whose fight a gate route carries (`tools/fight_routes.txt`, MOVIES.md
"Fights carried by routes") keeps its protected matches but has no pinned
exception: the route, run by the gate, is the fight's outcome.
Pictures are checked for dark endings. Three native fingerprints in
`tools/native_picture_baseline.txt` protect the accepted title BG1 clear in
attract-demo and both Golem Twins movies. They pin native frame hashes after
the fill; no emulator is built or run. Main's accepted values and reasons are
retained, independently of the save/place ratchets.

The repeated whole-tree smell scans in the Python tests share one immutable
result per test process; every individual smell fixture still executes. Native
C tests cover the frame scheduler, video, tasks, timing, cartridge save round
trips, asset loading, route protocol, audio replay, persistence and bindings.

## Validation receipt

With the fight routes carried and their facts checked, and the plays stages
side by side on the two ct_native slots, the sampled native gate finished
`suite OK` in **351.7 seconds** at `-j 2` (321.4 in a quieter run; 447.2 with the
stages in turn), on a machine other agents shared: 252 Python tests (113.0 s),
29 native C tests (19.8 s), 58 movies (329.0 s), 58 outcome ratchets (0.2 s),
the fast pair and 19 fight routes (351.7 s), snapshots (231.5 s), portstyle
(0.2 s), portsmell (96.7 s) and doc-names (1.3 s); the three plays stages
overlap, so their times add to more than the wall. All three native picture fingerprints passed.

Windows MinGW GCC and WSL GCC/Clang built with warnings as errors; both Linux
builds passed all 15 C tests. Opening, menu and battle quick-load continuations
matched uninterrupted state, pictures and PCM; route slot/file restores and
native recording playback passed too. The packaged Windows player passed its
launch smoke check. The CI matrix also configures macOS Clang; that platform
was not run locally.

## Game and host

`game/core/program.c` boots the game and dispatches its frame work. The game
uses platform services for video, audio, input, saves and timing. The desktop
frontend owns the window, event polling and files. Route tools drive the same
program over stdin/stdout with `--drive`.

Game operations take `GameState *g` and, only when needed, `Platform *pf`, plus
named parameters and C results. Separate parameters keep state-only calculations
independent of services and service-only uploads independent of game storage.
Neither handle contains an emulated CPU. The frontend owns the frame loop and
files; the timing service owns coroutine scheduling.

Core stores typed `ProgramHandler` callbacks in per-game state. Modes install
their vblank and line functions directly. Frame dispatch reads those pointers;
it never decodes a jump instruction or searches a table by address. Scene, warp,
field restart, title and reset hand-offs use normal calls and named requests.
Invalid state records the first diagnostic and returns; there is no fallback
to cartridge code. The program test covers callback isolation, replacement,
disabling, mode hand-offs and failed initial scenes without a ROM.

The former machine entry graph had no native callers. Deleting it across all
subsystems removed the registry, held-continuation ledger, game time-tag rows,
console vectors, entry macros, machine access helpers, CPU shell headers and
the no-machine stubs. Native C tables retain their real operations and timing keys.
Machine-only cost APIs and their dead calculations were removed too. These
deletions do not finish the vocabulary and storage work in every subsystem:
the video escapes have named services (PLATFORM.md, "Memory moves and VRAM
read-backs"), and the sound driver's register write became the voice calls
it means (PLATFORM.md, "Audio"): no register escape is left.

The callback tables that stood between a sequence and its one implementation
went next, a module at a time (event, overworld, scene, battle, field, menu):
the menus' main menu, for one, asked a table of its steps for its tick, its
wait, its panel and its end, and now calls `menu_tick`, `menu_task_waits`, its
`show_the_panel` and `menu_close` itself, each timed where the table's entry
was. Callbacks through an opaque context went from 312 to 2 (a coroutine's
body and the host's asset provider, each with several implementations), and
carry-shaped values from 12 to 0 (`tools/machine_joints.py`), with every gate
movie's timing report the same as before.

### Time without a machine

`TIMED_AS(g, key, operation)` selects an authored work rate. Accessors, resource
reads and explicit charges accumulate native work; the scheduler converts that
debt into lag frames, retaining fractional carry. Rates remain unchanged in
`runtime/port/time_rates.c`; an address-shaped key identifies an estimate, never
a function to execute. There is no generator or emulator calibration at build
time. Untimed validation remains excluded where it was excluded before.

Removing a read can change the estimate even if its value is dead. Callback
selection therefore retains four access units, and disabling both callbacks
retains sixteen. A battle upload guard used to inspect both request flags
seventeen times, plus five bytes for a queued transfer and two for a block;
the stack guard is gone, but its counted work remains an explicit charge.
The lean gate caught the missing charge through battle picture and outcome
changes; restoring the work restored those checks without changing baselines.

**The battle's time against the original's.** The frozen `emulator-oracle`
tag's development build, with a scratch profiler added (not kept), charges
every clock of the original's code to the innermost timing key on its call
stack, main thread and interrupt handlers apart. Set beside ct_native's
`--timing-report` over the same fights (Dalton's Golem Boss, the Golem Twins,
Giga Gaia), it showed where native battle time fell short:

- The machine's removal (cd01adc1) deleted the registry's 2,184 timed-only
  routines, and 1,707 of them have had no timing scope since, their work charged
  at their callers' rates. The battle setup's (`$FD:B22E`, `$FD:B438`,
  `$FD:AE52`, `$FD:AD09`, `$C1:C96A`, `$C1:CA1A`, `$C1:CDFF`, `$FD:B0D5`,
  `$FD:B4D6`, `$FD:AF80`) and the frame's input (`$C2:84EC`, `$C2:8545`,
  `$C2:85AE`) have their scopes back; with them most rates match the original
  within 10%. The long arithmetic (`$C1:C90B`, `$C1:C92A`), the name converter
  (`$C1:01A9`) and the empty place's pass (`$FD:B01C`) are charged their
  routines' time. The field's are the next stretch (below); the rest is
  issue #349's.
- The handlers took ~65k clocks a frame against the original's ~160k. A
  handler's wait for a line (the frame work's wait for line 64 while effects
  run, `$CD:3B82`) now holds the frame until the beam is there, and a
  handler's transfers are charged; 40 handler routines' rates are recalibrated
  by the ratio of the original's clocks a call to native's, which agreed
  within ±15% across the three fights for all but `$FD:FFFD` (2.72 in
  Dalton's and the Golem Twins' fights, 2.07 in Giga Gaia's; its rate takes
  the ratio of the three fights' clocks pooled, 2.59). Most ratios are 1.2 to 2.8; seven fall
  outside: `$CD:05B2` 3.38, `$CD:0ADE` 3.16, `$CD:0AEE` 4.28, `$CD:2DD6` 4.54
  and `$CD:2EB5` 3.75 are small routines of a few hundred clocks a call whose
  ports keep in locals what the original reads and writes in memory, so few
  accesses carry their time, and `$CF:F68D` and `$CF:F7FC` at 0.89 count a
  little more than the original. Every ratio, with its fights, is in
  `tools/timing_ratios.txt`. The main thread's
  aggregate already matched (~78k against ~70k) and is left alone.
- Ten more rates are calibrated from the battle setup's profile
  (`$CC:ED87` ×2.72, `$CD:088D` ×15, `$CD:09F1` ×46, `$C1:C90B` ×2.38,
  `$C1:C92A` ×3.2, `$C2:84EC` ×2.27, `$FD:AF80` ×3.42, `$C1:C96A` ×0.49,
  `$C1:CA1A` ×0.52, `$C1:1C55` ×1.2). The frame's input `$C2:84EC` was
  wrong there: over every frame of 25 movies and the three fights it reads at
  0.61 of that figure, and `$C2:85AE` at 1.19, which their rates now take.
  The script that summed native's time added a routine's charged clocks to
  its milli clocks, which already hold them; rechecked with the sum
  corrected, the handlers' 40 rates stay within 0.89-1.10 of the original,
  and only the name converter's figure moves (0.90 to 1.01).
- The field (issue #349). The scopes of 26 field routines the original calls
  every frame are back: the line effects' builders for each table set
  (`$FD:C2EB`-`$FD:D27E`, the scroll sends, BG2's and window 2's tables, the
  wave), the party's steps (`$C0:9E29`, `$C0:A26B`, `$C0:A2CE`), the step's
  start and look ahead (`$C0:ACFD`, `$C0:5B95`), the walk animations
  (`$C0:5614`, `$C0:568B`), the freed sprite areas, the dialog shading and
  the leader's touch (`$C0:3154`).
  Over whole runs of the 25 gate movies whose field follows the reference
  (each routine's calls within 10% of the original's), 61 field and event
  rates take the ratio of the original's clocks a call to native's, each
  steady across the movies (no movie's ratio more than 1.35 times another's);
  the ratios are in `tools/timing_ratios.txt`. The dialog window's four
  builders take theirs too (1.39-1.42), though the window opens on other
  frames in ct_native, so their calls differ by more. Most are 1.2 to 2.4; the line
  effects' setup in the vblank (`$FD:C1EE`) is 21.7, its register writes
  being a service call in the port. Left as they are: the frame's wait
  (`$C0:EC60`), the layer streaming (`$C0:93E1` with `$C0:7F9A`, 1.07 together;
  the original reaches `$C0:7F9A` without a call), routines whose ratio
  differs from movie to movie (`$C0:AA07`, `$C0:A810`, `$C0:B1B2`,
  `$C0:1F87`, and `$C0:CAD9`, 0.94-0.99 in its busiest movies and 0.74 in
  another) and the song changes, where the original waits 1-10 frames on
  the sound processor. Across the 84 movies native_check runs, places on time
  go from 589 to 598 and in the reference's order from 689 to 696 (the gate's
  58: 394 to 394, 476 to 478); four move away, each with its measured lag in
  `tools/outcomes_exceptions.txt`.
- A lag frame gets the main thread time its own handlers leave (issue #321),
  so a frame whose handlers made a load's sends no longer shortens every lag
  frame after it.

The turn timers' first tick, frames before the original's, before and after:

| fight | before | after |
|---|---|---|
| Golem Twins | 12 | 3 |
| Death Peak's Lavos Spawn | 9 | 2 |
| Beyond Time's Lavos | 13 | 3 |
| Spekkio | 9 | 1 |
| Bucket's Lavos | 11 | 1 |
| Lavos game over | 24 | 15 |

The Lavos game over's 15 and the Golem Twins' last 3 stay as they were with the
field's rates calibrated, so they are not the field's main routines' time.

### Warp as the subsystem pattern

Warp is the pilot because its small mode includes setup, animation, frame
waits, line effects and a return to field. `warp_runs(g, pf, scene)` selects a
`WarpKind`, installs typed callbacks, preserves the story flags, and builds
frames until the trip ends. `make_a_frame` updates the plane, light and circle
and returns whether another frame is needed. The timing service yields while
the frame is pending; completion restores the story flags and returns to the
program loop. No wrapper, address stop, emulated return or register escape
remains in core or warp. PORTING.md gives the steps for other subsystems.

### Source inventory

`tools/machine_joints.py --files` measures source dependencies, not coverage.

| Joint | Before | After |
| --- | ---: | ---: |
| Functions taking `Snes *` | 2009 | 0 |
| Registered entries / held continuations | 13 / 136 | 0 / 0 |
| Emulated returns / address stops | 505 / 111 | 0 / 0 |
| Address call-outs / sibling lookups | 14 / 9 | 0 / 0 |
| Machine time tags | 8 | 0 |
| Register escapes | 20 | 1 |
| Video service calls | 543 | 551 |
| Missing video backends / cartridge reads by address / scaffolding | 0 / 0 / 0 | 0 / 0 / 0 |

## Scaffolding removal

Comparison-only writes and scaffold-read instrumentation were removed. The
source inventory's scaffolding count fell from 13 to zero. All 58 sampled native
outcome artifacts were byte-identical before and after that deletion, including
the saved-game and place projections. No game logic or timing estimate changed.

## State and resources

Typed accessors describe the game's state; STATE.md records the struct storage
and compatibility projection. Cartridge `.srm` saves still load through the
real save-slot routines. Their round trip is tested without a ROM.

The authored asset catalogue in `re/assets.tsv` generates the committed C
lookup table with `tools/assets.py generate`. The native frontend imports a
verified user cartridge into a local pack and then serves resources by name.
The pack and all captured runtime state remain uncommitted.

## Native route findings

Son of Sun's approach now corrects the settled flight position before landing,
so it no longer relies on a fixed coast reaching the desired tile. Neither
Son of Sun nor Epoch Lavos has a route exception. Native continuations reach
Fiona's location 424, Sun Stone's rewards, Dalton's outcome and Mammon Machine
after the first Queen Zeal fight. These distinguish missed fixed-movie inputs
from a timing-model defect; they do not justify changing work rates. Keep the
Sun Stone and Queen Zeal continuations reproducible as route checks (issue #215).

With slot loads as snapshots every route runs in minutes. `motherbrain`'s
escort-pod charge and `telepod_lavos`'s reaper statue were route assumptions,
not game faults: the escort counts the charge down a step a turn (every four
frames, the same in the last emulator build) while it stands by the party, so
the axis-only dash round Geno Dome's floor (about 750 frames) arrived a count
or two either side of the limit; the route now dashes on the diagonals as
well (about 600 frames). The reaper statue is reached by a jump off the
platform's edge, which the route now waits out instead of searching for a
point inside it. The new-game `playthrough` route found a refusal: the
overworld leader's guard required the trail index to be in range even for a
leader setting off, which lays the trail afresh, and leaving Guardia Castle in
600 A.D. leaves the index at `$80` (fixed in `game/overworld/handlers.c`).

All 2,213 live variables now have module-owned storage, and 2,143 of them
typed storage, counting a value as typed only when it is exactly one owner field
or element of its own width; 70 remain blob-backed (the event script, tile graphics and line
tables in the hardware's formats, byte-copied records, two-width record views
and views of bytes another mode shares), and none is image-backed. The 27
retired sound-loader variables, 20 dead scratch declarations and the residual
128 KiB work-RAM image were removed.
The baseline was 1,953 typed, 283 blob-backed and 27 image-backed variables.
The generated address view supports cartridge saves, route inspection and
remaining dynamic/bulk operations. The generator also emits designed field-object, combatant, character, menu
and sprite records without changing the public accessors.
[STATE.md](STATE.md) describes the ownership rules and validation.

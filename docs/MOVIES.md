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
over 100,000 frames, so `HAND_RUN_ROUTES` in `tools/suite.py` excludes them
from the gates, including `--long-routes`. Their recordings belong in scratch
storage, not `oracle/movies`. `tools/movies/other_endings.py` shares the fights
and recording setup; the routes supply the ending's walks and completion checks.

```sh
python3 -m tools.movies.routes.successor_of_guardia --prefix work/successor/run \
    --movie work/successor.ctm
python3 -m tools.movies.route_check --mode on:on:c -j 1 \
    --scratch work/shipping successor_of_guardia
```

The routes check the ending itself, never a timeout: each reads the story
counter that picks its branch in location `$052`'s ending dispatcher and waits
for the last scene's script to halt. `successor_of_guardia` (technoplaza
ct-state17 slot 2), for one, checks that story `$27` set `$7F:006E` to 5, then
that `$057`'s object 0 stops at its final halt after Frog and Leene's scene.
What each ending covered and how its recording and shipping-mode runs went is
in [history/endings-routes.md](history/endings-routes.md); the saves still
wanted are in [movies-wanted.md, target 7](movies-wanted.md#7-the-other-endings).

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

`motherbrain` (`battle-motherbrain.ctm`, 135481 frames) starts from fantasyanime
ctsave14 slot 2 on the 600 A.D. world map. It boards the Epoch, selects 2300 A.D.,
lands southwest of Geno Dome and puts Robo in the lead. The route clears the five
conveyor battles, opens the charging chamber, turns on the first and third code
switches, charges the doll and escort pods, and brings the escort robot to the
guard for the other doll. The robot follows the leader directly, so the route
keeps it around corners and balances the reversed conveyor with Down while it
catches up, checking the robot's position before advancing. It clears the
main-floor encounters before the timed escort charge. Map exits, event flags
and object positions supply the destinations; searches on machine copies keep
only their winning input. No RAM writes are used.

The existing `party` policy wins Robo's solo Atropos fight and MotherBrain's
fight, both without a retry in this recording. The route places both dolls,
plays the shutdown and return to the world map, then re-enters Geno Dome and
waits for field control. Its final checks require location `$0F8` and the
MotherBrain defeat bit (`$7F:013B` bit 4); stopping at the battle's last hit would
leave the ending scene and field load unfinished.

The route's verification from power-on reaches that outcome at frame 150806.
Its MotherBrain fight uses `policies.motherbrain`: prioritize Flare, Ice 2 and
Robo's magic before her repeated stat increases make a prolonged fight costly,
with healing as needed. That fight also wins without a retry.

The recording's shadow replay has 0 mismatches and no refusals or accessor-range
errors. It adds 1244 executed bytes beyond the other 48 movies, all already
ported, including MotherBrain's add-to-stats-then-tech action; see the coverage
and comparison limits in [movies-wanted.md](movies-wanted.md#5-boss-fights).
`ct_native` consumes all 135481 frames without a stop (nine screen setups and
four SNES-only video escapes, first at `$7E:5AEB`). Strict mode also reaches
the end; its saved game differs in 323 bytes, recorded as `drifts` in
`tools/strict_baseline.txt`. These fixed-input native runs drift from the
reference route's timing; the reference recording establishes the boss victory.

A third batch went looking for where the native program stops rather than
for code no movie runs: a throwaway route loaded every save point no movie
started from and walked out of its map and into the next, and another walked
up to the nearest objects until one attacked, and each run was played in
`ct_native` and on the hand ports alone (TODO.md, "The native program",
"Where the save movies stop"). What stopped, and a fight of a place no
movie fought in, became routes: `magic_cave` (`events-magic-cave.ctm`),
`last_village_12kbc` (`field-12kbc-after-the-fall.ctm`) and `denadoro_fight`
(`battle-denadoro.ctm`), the last walking into its fight with
`tools/movies/encounter.py` (the nearest objects in turn, the next map when
none attacks; and if that finds none, a wider search from the checkpoint
before it, for the shipping mode, whose timing has the monsters elsewhere).
A fight inside Magus's castle (ctsave06 slot 3) was found the same way but
left out: the way from the castle's hall into the next room (`$0A8`) that
the interpreter's search found, it found only at some frames, and in the
shipping mode at none.

`nizbel_ii` (`battle-nizbel-ii.ctm`) starts outside Tyrano Lair on ctsave07
slot 3, frees Kino, crosses the switch rooms and teleport maze, opens Nizbel
II's door, rests with a Shelter at his room's save point, and wins the fight.
The route uses the location exits and event scripts for its itinerary and
live field searches for each walk. Its maze search targets the exit warp's
destination so it cannot keep choosing a nearby return warp; at a corner
landing it tests a step away before asking the usual control check again.
`policies.nizbel_ii` resolves learned techs by id, uses lightning against the
boss's defence, and heals and restores MP with this save's supplies. The movie
ends after the victory messages with field control restored. See Targets, 5
in [movies-wanted.md](movies-wanted.md) for its coverage and validation.

`unequipped_armor` (`battle-unequipped-armor.ctm`, ctsave04 slot 2, 22859 frames)
reproduces the US equipment glitch: R+Up preserves the equipment list's member-plus-slot
cache key, allowing Marle to equip item zero in her armor and helmet slots. It crosses
Zenan Bridge and Truce Canyon, then beats Spekkio with Ice and ends with field control.
Her Ice deals 244-246; an equipped comparison from a machine copy deals 157-159.
Incoming Ice is 15 versus 108, Lightning 20 versus 171. The two empty slots give +20
magic and +30 Lightning/Shadow/Water resistance; Fire is unchanged. See
[Target 14](movies-wanted.md#14-the-originals-known-glitches) for the code and measurements.
It adds 0.49 KiB beyond the other movies. Shadow has 0 mismatches and no invalid
access or refused page; its 44 equipment-list guard refusals are documented gaps.
Strict stops at frame 7594 (`$C0:1AAC`, field confirm); `ct_native` stops at frame
3147 (`$C2:AC3A`, the inventory-list slide). Target 14 records the calibration and
narrowed checks too.

`luccas_mother` records `events-luccas-mother.ctm` (15701 frames): recover
Robo at Fiona's shrine, the campfire, the red gate to 990 A.D., save Lara
with the machine's timed L, A, R, A password, and return for Robo's
GreenDream. It ends on the 1000 A.D. world map with control back, checking
Lara's saved bit (`$7F:007C` bit 7), the gift and a movement probe on a copy.
The password search restores the console checkpoint between attempts:
three-frame taps miss the script's checks; eight-frame holds separated by
24 released frames succeed. Only the winning inputs enter the movie.
The bedroom's direct workshop exit reaches an isolated balcony; the route
goes through the front room to reach the console beside the machine.

Its save is `routes/luccas-mother-before-campfire.srm`, slot 1, made through
normal play from technoplaza `ct-state11`. `prepare_luccas_mother` plays the
Retinite and forest prerequisites and saves through the menu; it belongs to
`HAND_RUN_ROUTES`, with its 40031-frame movie outside the library. Its replay
reproduces the prepared SRAM byte for byte. The stock `ctsave13`
already has Lara rescued, and `ctsave12` has not opened the desert
([save provenance](../oracle/saves/README.md#routes)). Shadow is clean,
including sound lockstep; strict mode completes and matches the reference
end state. `ct_native` stops at frame 3120, recruiting Robo (`$C0:38BE`).
The [side-quest account](movies-wanted.md#12-the-side-quests) records its
coverage and the visibility-flag alias observed by the effects check.

`fionas_forest` (`events-fionas-forest.ctm`, 36440 frames) starts from
fantasyanime ctsave12 slot 3. The forest flags (`$7F:019E`) and Retinite's
defeated bit (`$7F:01A3` bit 0) are clear. The sapling's bit (`$7F:00F7`
bit 1) is clear too, so the route first visits the Last Village in 12,000 B.C.
and chooses to plant it. Crono, Robo and Marle cross the Sunken Desert,
win its central encounter and defeat Retinite: Ice 2 softens its defenses,
Confuse and Uzzi Punch kill the upper half, legs, then core. Keeping the core
alive avoids the fiercer attacks after its death. The route leaves Robo with
Fiona in 600 A.D., flies the Epoch forward 400 years, recovers him at the
shrine and plays the campfire through Lucca back in hand in 1000 A.D.
It ends before her optional gate to the past. Machine copies search the
moving sand and retry battles; only successful inputs become the movie.
The movie adds 828 bytes of executed code over the other 48 movies, all ported:
the sand and party trail, Retinite's stat condition, and event arithmetic and
party removal. Its dialogue table at `$FF:6B00` and its 185 strings join the
asset catalogue; the catalogue contains locations and types, not ROM data.
`ct_native` and strict mode both run to frame 36440 without stopping; strict
mode ends with 131 saved-game bytes different from the reference, so its
baseline is `drifts`. The departure's guard gaps are in TODO's "Refusals in
real play" and `tools/refusal_allowlist.txt`.
Shadow reports zero mismatches, no refused page or out-of-range accessor,
and no uncatalogued asset reads; its 120 guard refusals are the two documented
departure gaps. Sound lockstep matches all 111713 ticks, and the full
36440-frame generated-only trace matches the reference.
Timing costs are calibrated with `--drifted` over this movie; the eleven
remaining timing exceptions record the measured paths and limits. Its
calibrated shipping-mode vblank check has zero unexcepted findings.
The narrowed suite (`--movies events-fionas-forest -j 1`), including its static
checks, ends with `suite OK`.

`son_of_sun` (`battle-son-of-sun.ctm`, 16377 frames) starts at ctsave13 slot 3
and boards the Epoch on the save's starting tile. It flies to 2300 A.D., lands
east of the Sun Palace and wins Son of Sun, stopping with the field back in
hand. The save's quest byte, `$7F:013A`, is `$00`; ctsave14 slot 1 has `$F7`.
The flame policy reads enemy `$F7`'s reaction state after Roulette Shuffle:
stat offset `$3C` is `$62` on the real flame and `$63` on the counterattacking
ones. Before that choice is available it tries a flame; thereafter it attacks
the real one and uses healing items. The eye's AI ends the fight at 10000 HP
or less, from 12000, while the flames still have HP. The route checks that
threshold and then checks field control, rather than waiting for every enemy
to have zero HP. No machine writes are used.

`sun_stone` is the optional hand-run extension, listed in `HAND_RUN_ROUTES`:
from the same save, win the fight, collect the Moon Stone, return to the Epoch
and fly to 65,000,000 B.C.'s Sun Keep (`$147`). Leave the stone there and return
to field control, with `$7F:013A` at `$07` and the Moon Stone removed from the
bag (22857 frames). The charging starts in prehistory; 2300 A.D.'s Keep is
where the charged stone is eventually collected. The intervening theft and
recovery are not played. Its movie repeats the boss fight and stays outside
the library, for example `--movie build/sun-stone-hand-run.ctm`.

`rust_tyrano` (`battle-rust-tyrano.ctm`, 23282 frames) starts at Giant's
Claw's save point in 600 A.D., opens the nearby door and defeats Rust Tyrano
with Crono, Lucca and Robo. It includes the countdown and fire breath,
Confuse and Uzzi Punch, healing and MP restoration, and stops with the field
back in hand in the shell's chamber (`$0C5`). A fresh route run reproduced
the movie's inputs. Shadow replay has zero mismatches, refusals, refused
pages or out-of-range accessors. Its coverage adds 54 instruction bytes
(0.05 KiB, all ported) over the preceding 48 movies: field walking, healing
and battle reward branches (Targets 12 in movies-wanted.md).
The shipping-mode route (`on:on:c`) also wins without backtracking, at
frame 23105, and returns field control at 24605, with zero fallback
instructions or guard refusals. It restores Crono's MP for Life 2 if a
party member falls; that recovery branch is unused by the recorded movie.
`ct_native` runs all 23282 frames without a stop (four existing video
escapes, first `$7E:5AEB`). Strict mode also reaches the end without a
stop; its saved game differs in 72 bytes of inventory counts and character
records, recorded as `drifts` in the strict baseline. The narrowed gate
requires 177 timing constants recalibrated with `--drifted` and an
uncompared baseline entry for `battle_apply_damage` (67 interrupted calls,
56 matched). Two vblank-tail exceptions cover `battle_record_trails` and
`field_nmi`; the movie's video check finds no writes moved onto the picture
(TODO.md records the estimates to refine). The combined library needs its
own timing measurement.

Its starting save is `oracle/saves/routes/rainbow-shell.srm`, slot 1, made
through the game's own save menu after a Shelter. `rainbow_shell` plays the
long approach from fantasyanime ctsave13 slot 1: the Epoch to 600 A.D.,
the ruined throne, cave encounters, floor switches and pits, then the save
point. The recorded approach takes 55184 frames; it is a hand-run route
(`HAND_RUN_ROUTES`), with its movie kept outside the library. It exports
the cartridge SRAM as `<prefix>.srm`; a fresh preparation run reached the
save point at frame 53687. Replaying the original 55184-frame inputs
recreated the archived SRAM exactly. Neither route uses memory pokes.
The archive's slot 2 is already after the boss and shell delivery; slot 3
is after the trial. The delivery and trial are not in this movie.

`cyrus` (`events-cyrus.ctm`, 39872 frames) starts from ctsave14 slot 1,
before the Northern Ruins repairs. It takes the carpenter's request in
600 A.D., borrows the tools from his counterpart's family in 1000 A.D.,
returns them and commissions the repairs. The three left-wing guardian
groups are beaten, that floor is repaired for 2000 G, and Frog replaces
Crono for Cyrus's grave: the ghost, Masa and Mune, and the Masamune restored.
The route checks the repair and completion flags, Frog's upgraded weapon,
and player control at the end. Slot 2, used by `northern_ruins_switch`, is
already after this quest. The right-wing treasure rooms need no visit.
Its waypoint searches use machine copies and retain only successful walks;
the fights use the `party` policy. Shadow has 0 mismatches and no refusals;
strict mode reaches the end with five character-record bytes differing.
The route also reaches its checked outcome in shipping mode
(`--native on --generated on --sound-driver c`), at frame 38104.
`ct_native` consumes all frames without a reported stop, but ends blanked,
so that run does not establish the quest's outcome. See Targets, 12 in
[movies-wanted.md](movies-wanted.md) for the coverage and native stops.

`ozzies_fort` (`battle-ozzies-fort.ctm`, 39237 frames) starts from fantasyanime
ctsave12 slot 2 at the End of Time. The fort flags at `$7F:01A1` are `$00` in
slot 2 and `$FF` in slot 3, including the final trap's bit 7. It boards the
Epoch, selects 600 A.D., lands east of the fort, and plays Flea Plus, the
conveyor scene, Super Slash, the treasure trap (walking past the chest), the
three generals, the switch battle, the fall downstairs and the return for
the cat to spring the final trap. It ends with all eight fort bits set and
the field back in hand. Lucca casts Flare against the trio while Crono and
Marle heal between its counters; machine-copy retries keep only the winning
inputs. The route stops on each event's trigger row before waiting for its
dialogue, since the animation can start before the window opens.
Against the preceding 48 movies, its coverage adds 393 bytes (0.38 KiB), all
ported: chiefly single-tech learning and the rewards that announce it, plus
event flag setting and the Epoch arrival. Shadow has no mismatch or refusal;
the C sound driver's state matches at every tick. Strict mode reaches the
frame budget with different saved-game state as the inputs drift.
`ct_native` reports no stop over the same budget but leaves its display
blanked; that playback does not establish completion of the fort (TODO.md).
The narrowed timing calibration leaves the screen-command dispatch's varying
work at 11 of 1029 vblank stretches over 1024 clocks, at most 1949 clocks,
with no drift; `timing_exceptions.txt` records it beside its channel and
frame runners. The movie's vblank check finds no unallowed write moved.
Interrupts leave 113 of 210 tile-row decodes uncompared; the existing isolated
whole-row fuzz test compares 10000 states with no mismatch or skip (seed 1),
recorded in `uncompared_baseline.txt`.

Each starts from the cartridge save its movie header names and its route's
`SAVE` selects; `--sram` picks another. The saves include technoplaza's
(`games.technoplaza.net/snes/chronotrigger/savestates.php`) in
`oracle/saves/technoplaza/`, and fantasyanime's in `oracle/saves/fantasyanime/`.
`--prefix` is where the run's checkpoints, log and
screenshots go — a scratch directory, not the repo. Add `--rom` or `--drive` (or
`$CT_ROM`, `$CT_DRIVE`) when they aren't at `roms/ct-usa.sfc` and
`build/main/ct_drive`, and `--native`/`--generated` (or `$CT_DRIVE_NATIVE`,
`$CT_DRIVE_GENERATED`) to run it on native code. `--until <phase>` stops it once
that phase's checkpoint is written. A route ends by printing `outcome reached
<goal> frame <n>` (exit status 0) or `outcome failed <why>` (1): its asserts
along the way, and a check of its goal at the end (the Dream Team all spoken
to; the farewells at the Gate and the King in the Beyond Time ending).

`r_series` (`battle-r-series.ctm`, 65437 frames) takes ctsave03 slot 2 from the 2300 A.D.
world map through the Factory Ruins' lab security doors and fights. The
location exit lists and the scripts for `$0E5`, `$0E6` and `$0EB` establish
the path: the upper lab console opens the ladder, the lower console opens
both security doors, and the basement code reader accepts X, A, B, Y without
requiring the warehouse cranes. The route holds each code button until its
acceptance variable changes. Restoring power sets story `$3F`; escaping the
shutters and returning up the lab corridor starts the R Series scene.
The `r_series` battle policy uses Crono and Lucca's Fire Whirl and Cyclone,
with the early save's healing items. The movie continues through Robo's
rescue and repair and the Gate trip, ending with the field back in hand at
the End of Time. Its battle-start check requires all six initialized robot
stat records, since the menu byte alone briefly looks ready during loading.

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
needs ("Adding movies"), and those measures are taken over `suite.py --all-movies`. A new movie
joins the gate's default sample (`tools/gate_movies.txt`) only where it earns its time there: code
no sampled movie runs, at a good rate per frame, or something a check needs. A long movie made to
find code or `ct_native`'s stops usually stays in the library alone, run by `--all-movies`.

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
mode are the vblank check's, over every movie the gate runs, so the suite doesn't run the
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

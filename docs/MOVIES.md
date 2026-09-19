# Authoring movies from game state

Test movies are the input side of validation: shadow mode only checks the code a
movie reaches, and coverage only names the instructions it runs
([PORTING.md](PORTING.md), [RUNTIME.md](RUNTIME.md)). Recording one by hand
([PLAYING.md](PLAYING.md)) is the quickest way to get one; long routes that have
to win boss fights are another matter, and that is what the tools here are for.

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
build/main/ct_drive --native on --recomp on roms/ct-usa.sfc save.srm   # the shipping mode
```

`--native off|shadow|on` and `--recomp off|on|only` mean what they mean to
`ct_play`: `frontend/natives.c` chooses the registered entries for both, and
for `ct_dev` (generated blocks only for the ROM they came from; `--recomp only`
leaves the ported routines out in on mode, and in `ct_dev` in every mode; see
`natives.h`). `--recomp` defaults to `on`. Only `--native
off` is the machine movies are made on; see Native code for what the others
change.

Commands arrive one per line and answer one line starting `ok` or `err`
(`dev/ct_drive.c` has the full list):

| Command | What |
|---|---|
| `run <n> <pad>` / `runw <n> <pad>` | run n frames holding pad (JOY1 bit order); `runw` also returns the watched bytes |
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
work RAM, and so do `--native on --recomp only` (generated code alone keeps the
original's timing) and `--native shadow` (no mismatches); `--native on
--recomp on` gives the same result on every run.

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

Each starts from the technoplaza cartridge save its movie header names
(`games.technoplaza.net/snes/chronotrigger/savestates.php`), which is in
`oracle/saves/technoplaza/` and is the route's `SAVE`; `--sram` picks another.
`--prefix` is where the run's checkpoints, log and
screenshots go — a scratch directory, not the repo. Add `--rom` or `--drive` (or
`$CT_ROM`, `$CT_DRIVE`) when they aren't at `roms/ct-usa.sfc` and
`build/main/ct_drive`, and `--native`/`--recomp` (or `$CT_DRIVE_NATIVE`,
`$CT_DRIVE_RECOMP`) to run it on native code. `--until <phase>` stops it once
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
  presses A and checks the flag the conversation should set.
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

**Routes are timing-independent; movies are not.** A route reads the machine, so
it still works after the hardware model's timing changes — re-run it and it
finds the new inputs. The `.ctm` it produced is a fixed list of frames: change
the timing and it drifts, and a long one usually stops reaching its ending. When
a timing change lands, re-run the routes rather than patching the movies.

## Routes as a test

The player ships with `--native on --recomp on`: hand ports where they exist,
generated code elsewhere. Hand ports charge estimated time, so a run in that
mode drifts from the original's timing, and a movie's presses land on the wrong
frames; the movie library can only test `--recomp only`, which is exact. A
route plays the same scene whatever the timing, so whether it still reaches its
goal is a test of the shipping mode: of what the hand ports do along a whole
scene, not of their timing.

`tools/movies/route_check.py` runs routes in chosen modes, each from its save
and from scratch, and reports each outcome, with the frame each phase was
reached at, modes side by side. It exits 0 only if every run reached its goal.

```sh
python3 -m tools.movies.route_check                        # telepod_lavos:shell and beyond_time, on:on
python3 -m tools.movies.route_check --mode off --mode on:on --scratch work/check -j 2 \
    telepod_lavos:shell beyond_time
```

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

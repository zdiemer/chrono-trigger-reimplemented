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
frame, so a finished run *is* a movie.

```
route script ──> tools/movies ──> ct_drive (interpreter, no ported code)
      ▲                │
      └── game memory ─┘            run's pad log ──> movie.ctm ──> ct_dev --native shadow
```

## The driver

`ct_drive` is the same machine as `ct_dev --native off` — the interpreter and
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
```

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
| `poke <addr> <hex>` | **diagnostic only**: write work RAM |

`poke` is for asking "what does this flag do"; a run that used it is no longer
reproducible from input, so it must not become a movie.

## Routes

A route is a Python script (`tools/movies/routes/`) that plays one scene from a
cartridge save to an ending. The three ending movies in `oracle/movies/saves`
come from these:

```sh
python3 -m tools.movies.routes.telepod_lavos --sram "<ct-state16>/Chrono Trigger (U).srm" \
    --prefix work/telepod --movie oracle/movies/saves/endings-telepod-lavos.ctm
python3 -m tools.movies.routes.bucket_lavos  --sram "<ct-state18>/Chrono Trigger (U).srm" ...
python3 -m tools.movies.routes.beyond_time   --sram "<ct-state15>/Chrono Trigger (U).srm" ...
```

The saves are the technoplaza cartridge saves the movie headers name
(`games.technoplaza.net/snes/chronotrigger/savestates.php`); like all saves they
stay out of the repo. `--prefix` is where the run's checkpoints, log and
screenshots go — a scratch directory, not the repo. Add `--rom` or `--drive` (or
`$CT_ROM`, `$CT_DRIVE`) when they aren't at `roms/ct-usa.sfc` and
`build/main/ct_drive`.

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

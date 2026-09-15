# Playing

`ct_play` runs the game in a window: ported routines run natively and
everything else runs on the development system (see [RUNTIME.md](RUNTIME.md)
and [PORTING.md](PORTING.md)). It needs your own ROM (see the README) and SDL2.

There is no sound yet. The sound driver runs on the SPC700, but the DSP doesn't
generate samples.

## Build

Install SDL2's development package (CMake config or pkg-config), then build
with the main project. `ct_play` is skipped when SDL2 isn't found.

```sh
cmake -S . -B build/main -G Ninja && cmake --build build/main --target ct_play
```

For an optimized player in `dist/`:

```sh
python3 tools/dist.py                                   # Release build in build/dist, installed to dist/
python3 tools/dist.py -- -DCMAKE_PREFIX_PATH=<prefix>   # when CMake can't find SDL2 on its own
```

On Windows it copies `SDL2.dll` next to `ct_play.exe`. `dist/` is never
committed.

## Run

```sh
dist/ct_play --rom path/to/ct.sfc [--scale 3] [--fullscreen] [--native on|shadow|off] [--save-dir dir]
```

The ROM can also be given as the only argument, so dropping it onto the
executable works. `--native off` interprets everything, the reference behavior;
`--native shadow` also interprets everything and checks each ported routine
against it as it goes (the title bar shows "shadow mismatch" if one differs,
and the details are printed on exit).
`ct_play --help` lists the options and controls.

| Key | Button |
|---|---|
| Arrows | D-pad |
| X | A |
| Z | B |
| S | X |
| A | Y |
| Q | L |
| W | R |
| Enter | Start |
| Right Shift | Select |
| P | pause |
| Tab (hold) | fast forward |
| F5 / F7 | quick save / quick load |
| F11, Alt+Enter | fullscreen |
| Esc | quit |

The mapping is the `keymap` table at the top of `frontend/ct_play.c`.

## Saves

In-game saves go to `<rom name>.srm` in the ROM's directory (or `--save-dir`),
written within a second of the game changing them and again on exit. The file
is the cartridge's 8 KiB SRAM, the format other emulators use.

Quick saves go to `<rom name>.state` in the same place. They're raw machine
images, valid only for the build that wrote them. A quick save waits until no
native routine is suspended in a call-out, since a state can't capture one.
In the field that's immediate; in battle it can take several seconds (the
title bar shows "saving state..." meanwhile), and it gives up after 30. Loading a quick save also restores the SRAM it held,
which is written to the `.srm` file on the next check.

## Recording test movies

Shadow validation (see [PORTING.md](PORTING.md)) is only as good as the
movies it runs, so `ct_play` can record what you play as a `.ctm` input movie
that `ct_dev` replays exactly:

```sh
dist/ct_play --rom path/to/ct.sfc --record oracle/movies/my-session.ctm
build/main/ct_dev --rom path/to/ct.sfc --frames N --movie oracle/movies/my-session.ctm --native shadow
```

The movie's header comment gives `N`, the number of frames recorded. Movies
hold only button presses, so they can be committed; they are replayed from
power-on, so a recording:

- starts from power-on with blank SRAM, as `ct_dev` does: it doesn't load or
  write `<rom name>.srm`, and `--load-state` isn't allowed;
- runs in shadow mode (or `--native off`), because on mode changes the timing
  and the movie wouldn't replay the same. Shadow mode also checks the ported
  routines while you play;
- can continue an existing movie: `--movie base.ctm` replays it at full speed
  first (up to its last input, or `--movie-frames N`), and the recording holds
  its input followed by yours;
- keeps its quick saves in `<movie name>.state` next to the ROM (or in
  `--save-dir`). Loading one rewinds the recording to the frame it was saved
  on, so retries don't end up in the movie. Only a quick save made during the
  same recording loads.

The movie is written every 10 seconds and on exit. To add a recording to the
validation runs, put it in `oracle/movies/` with a comment saying what it
covers, and list it in the movie table in [RUNTIME.md](RUNTIME.md).

`--movie` also works without `--record`, to watch a movie and then take over.

## Headless check

For checking a build without a display, `--frames N` runs without a window and
`--screenshot out.png` writes the last frame. `--hold BUTTON[+BUTTON]@FIRST-LAST`
holds buttons over a range of frames, and `--load-state`/`--save-state` read and
write state files:

```sh
build/main/ct_play --rom roms/ct-usa.sfc --frames 1500 --screenshot captures/title.png
build/main/ct_play --rom roms/ct-usa.sfc --frames 3600 --hold START@1620-1624 \
    --hold A@2400-2403 --hold START@3000-3003 --hold START@3200-3203 --screenshot captures/opening.png
```

Screenshots are derived from the ROM; keep them out of the repository.

`--movie` and `--record` work headless too (holds count from the start of the
run), which is how recording is checked: record with holds, replay the movie
with `ct_dev` for the same number of frames, and compare the two final states.

# Playing

`ct_play` runs the game in a window: ported routines run natively and
everything else runs on the development system (see [RUNTIME.md](RUNTIME.md)
and [PORTING.md](PORTING.md)). It needs your own ROM (see the README) and SDL2.

Sound comes from the game's sound driver reimplemented in C (`game/sound/`,
[SOUND.md](SOUND.md)) playing through the S-DSP model (see the APU section of
[RUNTIME.md](RUNTIME.md)), and out through SDL audio. `--mute` turns it off.

## Build

Install SDL2's development package (CMake config or pkg-config), then build
with the main project. `ct_play` is skipped when SDL2 isn't found.

```sh
cmake -S . -B build/main -G Ninja && cmake --build build/main --target ct_play
```

For an optimized player in `dist/`:

```sh
python3 tools/dist.py                                   # Release build in build/dist, installed to dist/
python3 tools/dist.py --strict                          # the same without the interpreter linked at all
python3 tools/dist.py -- -DCMAKE_PREFIX_PATH=<prefix>   # when CMake can't find SDL2 on its own
```

`dist/` is never committed. Once it is installed, `tools/dist.py` checks that
it runs on a machine that never built it, from the binaries' own import lists:

- **Windows**: every DLL `ct_play.exe` loads, and every DLL those load, is either
  part of Windows (in the system directory, or an API set) or copied into
  `dist/` from `PATH` and the compiler's directory; one it can't find stops it.
  With MSYS2's MinGW-w64 gcc that is `SDL2.dll` alone: `ct_play` links libgcc
  statically (`-static-libgcc`), uses Windows fibers rather than a thread
  library, and SDL2 itself imports only Windows DLLs, so no MinGW runtime DLL is
  needed. A toolchain that links one (a C++ runtime, `libwinpthread`) gets it
  copied. Checked by running `dist/` from a directory outside the build with
  `PATH` cut down to the Windows directories: headless, it played a movie and
  wrote its screenshot and save.
- **Linux and macOS**: it lists the shared libraries the player needs beyond the
  base system (`ldd`, `otool -L`), which is SDL2; a player's machine installs it
  from its package manager (`libsdl2-2.0-0`, `brew install sdl2`), since `dist/`
  doesn't bundle it. Neither is checked on a machine without a build yet: CI
  builds `ct_play` on both, but nothing there runs `tools/dist.py`.

The `dist/` player runs without the interpreter (below), so it needs
the ROM at build time (`CT_ROM`, `roms/ct-usa.sfc` by default); `tools/dist.py`
stops if no blocks were generated. `--interpreter` builds the development
player instead.

### Without the interpreter

The development system's 65816 interpreter is the reference everything is
checked against, not something a player needs: the code generated from the ROM
runs every instruction of the movie library ([GENERATED.md](GENERATED.md), M3). The
CMake option `CT_PLAYER_INTERPRETER` decides what `ct_play` keeps of it (`ct_dev`,
`ct_fuzz` and the test runners always have it):

| Value | `ct_play` | Used by |
|---|---|---|
| `ON` (default) | the interpreter is linked and every `--native` and `--generated` mode works | `cmake` builds |
| `FALLBACK` | generated code only; an instruction without generated code (a *miss*) runs on the interpreter and is logged | `tools/dist.py` |
| `OFF` | generated code only; the interpreter isn't linked (about 28 KB of code), and a miss stops the game | `tools/dist.py --strict` |

```sh
cmake -S . -B build/main -G Ninja -DCT_PLAYER_INTERPRETER=FALLBACK && cmake --build build/main --target ct_play
```

The two players without the interpreter only run `--native on` with
`--generated on` (the default) or `--generated only`, and refuse to start when they
have no generated blocks for the ROM (a build without `CT_ROM`, or another
ROM). `ct_play --help` ends with the mode the build runs in; so do the first
line on stderr and the title bar (`[generated code, fallback]`,
`[generated code only]`).

**Misses** are logged to `<rom name>.misses.txt` next to the saves (the ROM's
directory, or `--save-dir`): one line per address and register widths with how
often it ran, and whether it is ROM code or RAM code and in which RAM section,
the format of `ct_dev --misses`. The file is written within a second of a new
miss in a window, when the game stops, and on exit, and each session adds to
the counts already there. The
fallback player prints the first 20 addresses to stderr as they happen and
shows the number of misses in the title bar. The strict player stops at the
first miss: the frame stays on screen, a message box and stderr name the
address (`no generated code for 7E:1F00 m8 x8, RAM in no section`), `F7` loads
the quick save and `Esc` quits; headless (`--frames`), it exits with status 1.

The player built with the interpreter does either at run time with
`--interpreted log` or `--interpreted stop` (with `--native on`), over what
`--generated` leaves the interpreter: with `--generated on` or `only`, code nothing
was generated for; with `--generated off`, everything no hand port covers, which
is how far the hand ports alone carry the game. The list goes to
`<rom name>.interpreted.txt`, the same format, started afresh each session, and
the title bar shows `[hand ports]` or `[generated code]` with the count. A
build without `CT_ROM` is enough for `--generated off`.

A misses file is a coverage file, so feeding it back is adding it to the
build's coverage and rebuilding, which generates blocks for that code:

```sh
python3 tools/dist.py -- "-DCT_GENERATED_COVERAGE=traces/coverage-*.txt;path/to/ct.misses.txt"
```

That covers ROM code and RAM code inside the RAM sections `re/hints.toml`
describes; RAM code in no section needs a `[[ramcode]]` section first
([GENERATED.md](GENERATED.md), "RAM code as generated blocks"). Like coverage files,
misses files stay out of the repository.

### Linux and macOS

The project builds with gcc or clang on Linux and Apple clang on macOS (CI
covers x86-64 Linux and ARM64 macOS; only little-endian hosts are supported).
Python 3.11+ is needed for the generated code, as on Windows. Install the tools
and SDL2, then build as above:

```sh
sudo apt install build-essential cmake ninja-build python3 libsdl2-dev   # Debian, Ubuntu
brew install cmake ninja python sdl2                                     # macOS (Homebrew)
cmake -S . -B build/main -G Ninja && cmake --build build/main
```

`CT_ROM` works the same way (without a ROM the build has an empty table of
generated blocks). Native routines run on `ucontext` coroutines there instead
of Windows fibers (`runtime/port/fiber.c`). CI builds and tests every platform
without a ROM (`.github/workflows/ci.yml`).

## Run

```sh
dist/ct_play --rom path/to/ct.sfc [--scale 3] [--fullscreen] [--mute] [--native on|shadow|off] \
    [--generated only|on|off] [--audio snes|direct] [--renderer native|scanline] [--save-dir dir]
```

The ROM can also be given as the only argument, so dropping it onto the
executable works. By default (`--native on --generated on`) the game runs as the
hand-ported routines, the readable C in `game/`, with the C generated from your
ROM at build time ([GENERATED.md](GENERATED.md)) wherever no routine is ported yet.
The ports' timing is estimated rather than the original's, so events can play
out differently from the original (a battle may go another way); the timing
gate bounds how far the estimates may be off and checks that no display write
lands on the picture where the original's doesn't (REIMPLEMENTATION.md, "The timing
gate"). `--generated only` runs the generated C alone, which keeps the original's
timing exactly: it behaves frame for frame like `--native off`, and it is what
movies replay and record in (below). `--native off` interprets everything, the
reference behavior;
`--native shadow` also interprets everything and checks each ported routine
against it as it goes (the title bar shows "shadow mismatch" if one differs,
and the details are printed on exit).
`ct_play --help` lists the options and controls.

The emulation produces 32000 Hz audio in step with the frames it runs, and
frames are paced by the host clock. The audio device plays at its own rate
(usually 44100 or 48000 Hz) from its own clock, so the player resamples into a
buffer of about 80 ms and adjusts the resampling ratio by up to 0.5% to keep
that buffer at its target fill. Video is never slowed down or skipped for
audio, and the pitch change is inaudible. Pausing and fast forward stop the
device and empty the buffer. If the host can't keep up, the buffer runs dry and
there are gaps of silence.

`--audio direct` selects the APU-direct audio backend ([PLATFORM.md](PLATFORM.md),
P1): the sound driver's uploads and commands no longer take the game's time,
so there are no pauses while a new song loads. It runs in the hand-ported
sound driver, so it implies `--generated on`, and it can't be combined with
`--record`. After a long upload the APU has produced its sound ahead of the
game and then waits for it; the buffer absorbs short leads, and a long one can
show up as a short gap. `--audio snes` (the default) keeps the original timing.

The sound driver is the C one (`game/sound/`, [SOUND.md](SOUND.md) "The port as
built") unless `--sound-driver original` asks for the game's own on the
SPC700. The game's CPU half of the driver hands its boot and its commands to
`frontend/sound_host.c` as calls: the driver boots, and each command is
carried out as the CPU half took it, a song with its samples placed in sound
memory by the driver itself, so nothing is uploaded and the SPC700 stays idle.
The host runs the driver's ticks on the APU's time, waits out the echo's
buffer as the original does, and has the driver play into the same S-DSP
model. So a new song starts at once rather than after its upload, and the
game's timing is not the original's. What needs that timing or the
original's code gets the original driver: replaying or recording a movie,
`--load-zst`, `--audio direct`, `--native off` or `shadow` and `--generated only`
(the C driver takes the CPU half's calls in its ported entries, which run only
with `--native on` and the ported routines); `--sound-driver c` with any of
them is refused. The C driver's voices are made into sound by the native mixer
([PLATFORM.md](PLATFORM.md), "The native mixer, as built"), with no S-DSP;
`--mixer dsp` plays them through the emulated S-DSP instead, the same sound
frame for frame.

The picture comes from the native renderer ([PLATFORM.md](PLATFORM.md), P3),
which draws each frame from the video service's frame state rather than from the
PPU registers line by line. `--renderer scanline` selects the reference renderer
instead; the two are pixel-identical on every frame of every movie, so the
option is for comparing them, not for choosing a look.

### Controls

The keyboard and any number of gamepads (SDL game controllers: Xbox,
PlayStation, Switch Pro and most others) work at once, all as controller 1. Pad
buttons go by position, as on a SNES pad, whatever their labels say: the bottom
face button is B, the right one A, the left one Y, the top one X.

| SNES / player | Keyboard | Gamepad |
|---|---|---|
| D-pad | Arrows | D-pad, left stick |
| A | X | right face button (`Pad b`) |
| B | Z | bottom face button (`Pad a`) |
| X | S | top face button (`Pad y`) |
| Y | A | left face button (`Pad x`) |
| L / R | Q / W | shoulder buttons |
| Start | Return | Start |
| Select | Right Shift | Back / Select |
| pause | P, Pause | |
| fast forward (hold) | Tab | right trigger |
| quick save / quick load | F5 / F7 | |
| fullscreen | F11, Alt+Return | |
| quit | Escape | |

Any of it can be changed, and pad buttons can drive the player too. A key file
holds one binding a line, `INPUT = ACTION`, with `#` starting a comment:

```
# ct_play.keys
Space = A
Left Ctrl = B
Tab = None              # no more fast forward on Tab
Pad guide = QuickSave
Pad lefttrigger = QuickLoad
Pad rightstick = Pause
```

An input is a key by its SDL name (`X`, `Return` or `Enter`, `Right Shift`,
`Space`, `F5`, `Keypad 8`), or `Pad` and a controller input by its SDL name: a
button (`a`, `b`, `x`, `y`, `back`, `guide`, `start`, `leftstick`,
`rightstick`, `leftshoulder`, `rightshoulder`, `dpup`, `dpdown`, `dpleft`,
`dpright`, `misc1`, `paddle1`-`paddle4`, `touchpad`), a stick direction
(`leftx-`, `leftx+`, `lefty-` is up, `lefty+`, and the same for `right`) or a
trigger (`lefttrigger`, `righttrigger`). Sticks and triggers count past half
way. An action is a SNES button (`A`, `B`, `X`, `Y`, `L`, `R`, `Start`,
`Select`, `Up`, `Down`, `Left`, `Right`), `Pause`, `FastForward`, `QuickSave`,
`QuickLoad`, `Fullscreen`, `Quit`, or `None` to unbind the input. A binding
replaces whatever that input did; several inputs can share an action.

`ct_play` reads `ct_play.keys` next to the executable if there is one, or the
file `--keys` names, and then `--bind INPUT=ACTION` options on top
(`--bind "Pad guide=QuickSave"`). `ct_play --help` lists the controls as bound.
Alt+Return always toggles fullscreen, and opposite d-pad directions pressed
together cancel, as a real d-pad can't press them. Bindings live in the window
driver (`frontend/bindings.c`); the player only sees the buttons they add up
to.

The SNES picture is 256x224, but a TV showed it at a 4:3 aspect and hid roughly
8 rows at the top and bottom as overscan; Chrono Trigger leaves its last dozen
or so rows black, which a TV mostly hid. By default the window shows it that
way (`--aspect 4:3 --crop 8`): stretched to 4:3, 8 rows cropped at each edge,
scaled by whole rows where the window is tall enough, and letterboxed on
resize. `--aspect 8:7` uses the SNES's own pixel aspect, `--aspect square`
shows square pixels, and `--crop 0` (or `--crop TOP,BOTTOM`) shows more rows.
Screenshots (`--screenshot`, `ct_dev --shot-dir`) always hold the full
256x224 frame.

## Saves

In-game saves go to `<rom name>.srm` in the ROM's directory (or `--save-dir`),
written within a second of the game changing them and again on exit. The file
is the cartridge's 8 KiB SRAM, the format other emulators use.

Quick saves go to `<rom name>.state` in the same place. A quick save made by
one build loads in later builds too, including ones whose machine has changed
since: the state is written field by field, and a field added since comes up at
its power-on value ([RUNTIME.md](RUNTIME.md), "Save states"). With the C sound
driver the state holds the driver's fields beside the machine's
(`sound_host_state`), so a state loads only with the driver it was saved with,
and through the mixer it was saved with (`--mixer`):
one saved with `--sound-driver original` (or by `ct_dev`) needs that option to
load, and so do quick saves made before the C driver became the default, which
were all saved with the original's. A state that can't be read -- damaged, from another ROM, from before
this format, or from the other driver -- is refused with a message, and the
game goes on as it was. A quick save waits for the end
of a frame where the state keeps everything: no native routine suspended in a
call-out, and no time the hand ports owe (their estimated time is charged
after they run, so it can still be passing when a frame ends;
`native_owed`). A state holds neither, so a routine suspended then would be
left to the original code, and time owed would be dropped and the game resume
slightly early. The title bar shows "saving state..." meanwhile. In the field
that's rarely more than a few frames (over `telepod_lavos:shell`'s run, 71 at
worst). In battle it can take seconds, and in scenes
where a port runs all the time (the scaled Lavos and ending pictures) a frame
end with nothing owed may never come, so after 2 seconds (`SAVE_EXACT_FRAMES`)
a frame end with only time owed will do: the save leaves that time out and
says how much on stderr (tens of thousands of master clocks, a scanline is
1364). A routine suspended is never saved; the save gives up after 30 seconds.
The one exception is a menu step parked in its wait for the next frame
(`native_parked`; docs/MOVIES.md, "Suspended routines and owed time"), which
waits as long as the player takes over a choice (a member to use an item on):
after the same 2 seconds such a frame end will do, and loading the state has
the original's code finish the step from its wait, the machine there being the
original's but for what nothing reads after the wait (checked with `ct_dev
--load-state` over `menus-field-menu`'s item screen, a state saved with its
member choice parked: 600 frames on, shadow mode 0 mismatching, the shipping
mode playing on); `player_save_ready` is the policy both drivers share.
Measured with `ct_play --save-state` after 3000-8000 frames of three battle
movies in the shipping mode (18 saves): 10 were exact at once, 4 exact after 8
to 1320 frames, 2 left out time owed after about 400 frames, and 2 gave up,
routines suspended throughout, as they did before owed time counted. Loading a
quick save also restores the SRAM it held, which is written to the `.srm` file
on the next check.

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
  write `<rom name>.srm`, and `--load-state` isn't allowed. `--sram save.srm`
  starts it with a cartridge save instead (for reaching later parts of the
  game quickly); the movie records the save's CRC-32, and `ct_dev` then needs
  the same file with `--sram`;
- runs in shadow mode by default, which also checks the ported routines while
  you play. `--native off`, or `--native on` (which records with `--generated only`
  unless `--generated` is given; fastest), keep the timing exact too; `--generated on`
  doesn't, so it's refused.
  A player built without the interpreter records in `--native on` mode;
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
It replays at full speed unless `--watch` is given, which plays it at normal
speed with sound (hold Tab to fast-forward):

```sh
dist/ct_play --rom path/to/ct.sfc --movie oracle/movies/gato-battle.ctm --watch
dist/ct_play --rom path/to/ct.sfc --movie oracle/movies/saves/battle-magus.ctm --watch
```

A movie started from a cartridge save finds that save by its CRC-32 among the
committed ones in `oracle/saves/` (or below the ROM's directory); `--sram`
picks one explicitly. A movie replays with `--generated only` unless `--generated` is
given, because that is where it replays exactly what `ct_dev` validates (so
does `--native off`); with `--generated on` or `--audio direct` the timing differs
and the replay drifts from the recording, since a movie is button presses at
frame numbers.

`--load-zst state.zst` starts from a ZSNES 1.51 save state (in a window or
headless), for looking around late in the game. Its cartridge save isn't
written back, it can't be recorded, and some of its state is approximated
([RUNTIME.md](RUNTIME.md), "ZSNES save states").

Movies too long to play by hand — a route through boss fights to an ending —
can be written by a script that reads the game's memory instead of counting
frames: [MOVIES.md](MOVIES.md) covers `ct_drive` and `tools/movies`.

## Headless check

For checking a build without a display, `--frames N` runs without a window and
`--screenshot out.png` writes the last frame. `--hold BUTTON[+BUTTON]@FIRST-LAST`
holds buttons over a range of frames, `--load-state`/`--save-state` read and
write state files, and `--wav out.wav` writes the sound (32000 Hz stereo):

```sh
build/main/ct_play --rom roms/ct-usa.sfc --frames 1500 --screenshot captures/title.png
build/main/ct_play --rom roms/ct-usa.sfc --frames 3600 --hold START@1620-1624 \
    --hold A@2400-2403 --hold START@3000-3003 --hold START@3200-3203 --screenshot captures/opening.png
```

Screenshots and audio are derived from the ROM; keep them out of the repository.

`--movie` and `--record` work headless too (holds count from the start of the
run), which is how recording is checked: record with holds, replay the movie
with `ct_dev` for the same number of frames, and compare the two final states
(`--save-state` on both; give `ct_dev` a `--wav` too, so that both have taken
the sound out of the machine and the states match byte for byte).

## How the player is built

`ct_play` is a player and two drivers over it, split so that a host which isn't
a desktop (a browser calling back once a frame, a handheld) needs a driver of
its own rather than a fork of the player (CLAUDE.md, "Writing host-side code",
holds the rules that keep it that way).

| File | What it is |
|---|---|
| `frontend/player.h`, `player.c` | the machine, its renderer and native runtime, the input track that replays and records, quick saves |
| `frontend/natives.h`, `natives.c` | which entries `--native`, `--generated` (and `ct_dev`'s `--exclude` and `--include`) register, and a runtime over them; shared with `ct_drive` and `ct_dev` so the three can't drift apart |
| `frontend/audio_fill.h`, `audio_fill.c` | the resampler and ring buffer between the APU's 32000 Hz and an audio device's rate (above) |
| `frontend/window.c` | `run_window`: the SDL window, audio device, keyboard and gamepads, and the frame pacing |
| `frontend/bindings.h`, `bindings.c` | the window's controls: keys and pad inputs to SNES buttons and player actions |
| `frontend/headless.c` | `run_headless`: `--frames`, `--hold`, `--screenshot`, `--wav` |
| `frontend/files.h`, `files.c` | the desktop's storage: what the saves are called, and the files they go in |
| `frontend/ct_play.c` | the options, which it turns into a player and one driver |

The player advances one frame per `player_step` call, and the buttons it is
given reach the game through the input service's `in_host_set`. Below the
driver nothing owns the loop, sleeps, reads a clock, polls events or opens a
device: the drivers do all of that, and take each frame's picture
(`player_pixels`) and sound (`player_take_audio`, or `audio_fill` for a device
with its own clock) to wherever they go. A quick save the player can't take yet
(a native routine is suspended, or time is owed; `player_save_ready`) is a
request it carries out at the end of a later frame, so no driver has to wait
for it.

The three tools that run native code choose it with one `natives_select`, and
differ in one deliberate place, a parameter of it (`NativesOnly`): which
ported routines `--generated only` leaves out. The option exists for the
original's exact timing, which hand ports lose (they charge an estimate) and
generated blocks keep. `ct_play` and `ct_drive` leave the ports out only where
that changes anything, in on mode when there are generated blocks
(`NATIVES_ONLY_TIMED`): shadow and off modes run the original code whatever
the table holds, so a shadow recording with `--generated only` still checks the
ports, and a build
without generated blocks still has the ports to run. `ct_dev` leaves them out
in every mode (`NATIVES_ONLY_ALWAYS`): its `--generated only` means the generated
code alone, so shadow mode checks the generated blocks without the ports, and
on mode without generated blocks is the interpreter, still exact.
Neither choice reaches the gate, which runs `--native shadow` with `--generated
on` and `--generated only` in on mode with generated blocks, where the two agree;
nor `ct_play --record`, which runs shadow mode with `--generated on` unless told
otherwise, and a shadow run's machine doesn't depend on the table. Moving
`ct_dev` onto `natives.c` left its reports, coverage, call logs and traces
byte-identical (`new-game`, `menu`, `field` and `battle-magus` in shadow mode,
the first three with `--native on --generated only`, and `--exclude`, `--include`
and shadow `--generated only` runs of `menu` and `field`).

Nor does the player know a path. What it persists it takes and hands out as
bytes: the cartridge save (through the save service's `sv_host_bytes`), a quick
save it has taken, the text of the movie being recorded, the misses log. The
driver decides what they are called and where they go -- on the desktop,
`frontend/files.c` names them after the ROM or the recording and writes them
next to the ROM or into `--save-dir` -- so a host without a filesystem keeps
them its own way. The runtime's file helpers are the same split: the player
uses `snes_state_write`/`snes_state_read`, `zst_import`, `ct_movie_parse`/
`ct_movie_format` and `misses_parse`/`misses_format`, and the path functions the
development tools call (`snes_state_save`, `zst_import_file`, `ct_movie_load`,
`misses_write`) are wrappers over them.

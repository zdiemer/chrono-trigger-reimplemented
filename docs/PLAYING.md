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

`ct_dev --native on --generated off --no-interpreter strict --sound-driver c`
is the same for a movie: the hand ports and the C sound driver alone, the first
instruction no port runs ending the run (`frame F: no generated code for
BB:AAAA ...; stopping`). `tools/strict_check.py` plays every library movie so,
and its `--check` is the gate's ratchet against `tools/strict_baseline.txt`. A
movie that gets to its last frame has to get there cleanly -- exit 0, no
`halted:`, `accessor out of range` or `page refused` line -- and its end is
compared with the reference play's (`ct_dev --end-state`, from the suite's
shadow runs, which run the original's code): the saved game, the eight runs
of work RAM a save holds (the party, the items, the story's flags, where the
party is), and save RAM, but for the play clock and the menus' frame count the
battles start their random numbers from, which the ports' own timing writes.
A movie whose saved game is the reference's is `end`; one whose isn't is
`drifts`, the line saying what differs: the hand ports estimate their own
time, so a movie's presses land on other frames, a cursor stops on another
item, a battle's blows land otherwise, and the story gets a step further by
the last frame. The strict run ends where the shipping mode's does, byte for
byte, so the drift is the shipping mode's own; the routes, which play to a
goal rather than to a frame, are what answers for it. Every movie reaches its
end; 31 of the 48 as the reference play does.

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

## The game's resources

The game's data -- its tables, scripts, text, graphics and music -- is read
from an asset pack, `<rom name>.assets` beside the saves (the ROM's
directory, or `--save-dir`). The first launch without one imports it: `ct_play`
checks that the ROM is Chrono Trigger (USA) by its CRC-32 and copies out every
resource of the catalogue, each under its name and type, in its original
bytes ([PLATFORM.md](PLATFORM.md), "The assets, as built"). `ct_play --import
<rom>` imports it again and exits, and a pack made by a build with another
catalogue of resources is imported again on launch. The pack is made from your
ROM and stays on your machine like it; nothing in the repository holds it. The
player still needs the ROM itself, for the original's code the machine runs
beside the ports.

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
| `frontend/files.h`, `files.c` | the desktop's storage: what the saves and the asset pack are called, and the files they go in |
| `frontend/assets.h`, `assets.c` | the asset pack: the import from a ROM image, and the stores over a pack or a ROM image the game reads its resources from |
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

### The native program

`ct_native` is the game with no emulator under it, the first version of
what `ct_play` becomes (PLAN.md, "The native program, and the registry taken
down"). It is a measurement, not yet a player:

```sh
build/main/ct_native --rom roms/ct-usa.sfc [--assets pack] [--frames N] [--sram save.srm] [--movie in.ctm]
```

With `--movie` the first controller presses what the movie's frames say,
frame for frame; the native program's frames aren't the machine's (nothing
in it takes the original's time), so a movie's presses land where they will.

It links the whole of `game/` but `game/registry.c` -- the program's top in
`game/core/program.c` (the boot, the main thread, the vblank and line
handlers sent wherever the running mode's stubs say) and every port -- with
the platform services' host backends (`runtime/platform/native/`), the
coroutines (`runtime/port/fiber.h`) and the game's state held in the host's
memory (the accessors' backing built with `CT_VARS_HOST`, beside
`runtime/port/state_host.c`, with the multiplier, divider and work RAM port
worked out in C), and none of the CPU, the PPU, the APU or the generated code.
The game's data comes from the asset pack, read through the store
(`game/data/assets.h`; "The game's resources", above): `<rom name>.assets`
beside the ROM, or `--assets`, imported from the ROM where there is none. The
ROM is read only to import it; no image of it is mapped
(`runtime/port/rom_host.c`). Like the shipping shape, it is built without the
ports' scaffolding, and it builds without a ROM.

Nothing in it makes a machine, so the entry wrappers and bridges `game/`
still has, which take a `Snes *`, are linked but never reached: the program
calls the functions of the game's state they stand in front of. What they
call of the machine -- the emulated bus, the runtime's call-outs, stops and
returns, shadow mode's declarations, the time estimates -- is
`runtime/port/no_machine.c`, each definition ending the program with what was
reached. That file is the list of what `game/` still asks of a machine, and
it shrinks as the wrappers leave.

The host backends answer every service call the game makes. Video folds the
calls into the picture's state through the development system's observer
(`runtime/platform/observe.h`), reading the game's memory through the
driver's reader; nothing draws it yet. The SNES's escapes a host has no
meaning for are counted and reported, but for the two whose meaning is
memory: the field's clear, the multiplier's product copied over work RAM, and
the menus' clears and copies through work RAM's data port, which the host
carries out at the port's address. Audio plays
the C sound driver (`game/sound/`) in the sound processor's place
(`frontend/sound_native.c`, the native counterpart of `sound_host.c`): the
CPU half hands it the boot and the commands as calls, the game's sends
through the service reach the CPU half as C (`sound_driver_send`), the
scenes' commands come on the ports, and the driver's time passes a frame at
a time and while the game waits on a port. Saves read and write the save
RAM the driver loaded. Work RAM powers on holding `$55` throughout, as the
reference emulator's does, since the game reads some of it before writing it
(the menus' records of the members not yet joined). A cartridge byte that no
resource of the catalogue holds has no answer without the ROM, so a read of
one stops the program with its address, as a step only the machine runs does.

The driver (`frontend/ct_native.c`) starts the host backends with the
program and runs its frames headless: the vblank handler, the main thread as a
coroutine until it waits for the frame, the line handler, then a frame of the
sound driver's time. Where the program's next step is still only a machine
entry, `program_needs_the_machine` stops it and the driver says where (exit
status 1; here from a cartridge save with `saves/menus-field-menu.ctm`'s
presses, at the equipment's slots window):

```
frame 3651: the program's C goes no further: $C2:A71E, the equipment's slots window opened, runs only on the machine
```

So far the boot and the field run as C. At power-on the field's handlers
are installed, the screen goes dark, work RAM is cleared, the sound driver
boots and the cartridge's saves are looked at. Then the field is entered at
location 0: the location loaded (its song, tile set, map and event script),
its sprites built afresh, its objects started with their openings and the
map's own script run by the event engine as C (`event_runner`), its
screen set up and faded in; and its main loop runs a pass a frame -- the
pause and the menus, the requests (a location change starts the field again
at the new place), the leader's walk, the confirm button, the dialog
window, the screen effect, the scripts' layer work, and the frame's work
(the objects' turns and moves, the line effects, the palettes, the frames
drawn, the sprite table) -- with the field's vblank and line handlers as C
in `core/program.c`'s table. The next location's script ends the field into
the title, a scene: the scene engine boots it and unpacks its program into
work RAM, whose data (the scripts, the sprite lists) the C reads; the
program starts (the sound put back to normal, the first screen), the title
is set up and its song started and waited for, and its frames run -- the
NMI's frame work with its two task lists as C, the sequence scripts'
channels turned as C (`scene_channel_turn`) -- until a script ends the
scene, when the game goes back to the field (`scene_runs`,
`scene/program.c`; the main thread goes between the two, `core/program.c`).
Left alone, the title gives way to the attract demo: the field again, until
it enters a world map, which the menus' sprite-object mode shows and which
runs as C too (`overworld_runs`, `overworld/main_loop.c`): the mode starts
(`$C2:0000`), the map is set up from its record's streams (the unpacker as
C), and the overworld's loop runs a pass a frame, with the mode's vblank
handler in `core/program.c`'s table: the frame sent, the input taken, and
every object's turn (`menu_objects_run`), each handler and each
routine a command script calls a row of a C table (`menu/handlers.c`) --
the scene's scripts on the interpreter as C, the fades, the layer over the
map, the irises and curtains, the drifting map. The scene leaves for the
field at the location it chose, and the field goes on there, its screen
effects as C (`field_screen_effect_moved_on`: the holes and ovals, the
searchlight, the curtain swept by `scene/curtain.c`, the ripple and the
field's close), until it closes into a scene: BG1 drawn and sent, the field
put aside as for a battle (`field_battle_set_up`), the interrupts handed
over, and the scene the close leads to started: a page of the credits, set
up as C with its letters unpacked by the unpacker as C. The field after it
goes on through its scripts' changes of place made at once (event `$DF`,
`$E1`: the field started again, `program_starts_the_field_again`) to a
battle, which runs as C (`battle_fought`, `battle/from_field.c`): the field
put aside, the battle set up, its main loop, its turns and its frames with
its vblank and line handlers in `core/program.c`'s table, and the field
taken up again after it (`field_taken_up_after_battle`). The attract demo
goes on through its next world map into the jet bike race's scene, set up as
C (`scene_race_set_up`) with its tasks as C (`scene/frame.c`'s table of the
tasks whose entries take the machine has a C column for each), then the
rising palace (`scene_palace_set_up`), and round to the title again: left
alone, `ct_native` runs the attract sequence without stopping (200,000
frames). With
`new-game.ctm`'s presses, Start ends the title and the field's next script
asks for a menu, and the menus run as C: the field fades out and is put
aside, the menus' mode opens (`menu_mode_runs`, `menu/display.c`: the
opening, the effects quietened, the screen set up, the save slots read, the
records' sprites and the item tables built) on the screen asked for, and
its tasks run on coroutines (`core/tasks.c`; `menu/tasks.c`'s scheduler,
the same one the machine's entries keep), a turn each a frame once the
menus' vblank handler (in `core/program.c`'s table now) has sent what the
last frame queued: the main task's screen steps, the cursor's, the fade
in's and the typing's. The naming's screen asks the battle mode first, then
names Crono; its close fades the screen out and the mode returns to the
field, which is set up again and faded in, and the script goes on to the
world map: the opening's bells over Guardia, as C, and then the field in
Crono's house, its people cast and its tiles copied, to the movie's end.
With `fair.ctm`'s, Crono leaves the house and walks the world map -- the
walkers, the places' watcher, the music and the sign as C
(`overworld/handlers.c`) -- into Leene Square, where the movie ends; with
`gato-battle.ctm`'s, on through the fair to Gato's battle and the field
after it, to the movie's end. With `menu.ctm`'s, Crono opens the main menu
in his house: its opening, its choosing and its panels, and the equipment,
the items, the techs and the settings opened from it and left again, each
step the menus' main task runs a row of `menu/screens.c`'s table with a C
column, and the scroll bar's and the records' tasks on their coroutines;
then the menu closed, to the movie's end. From a cartridge save
(`--sram`) the game starts on the load screen, as C too: its slots drawn,
the cursor among them, and a game loaded or a New Game+ carried over, out
of the menus into the game, and the save screens and the party exchange, and after a game over the title again; 20 of the save movies play to their end
(REIMPLEMENTATION.md, "The native program from a cartridge save", says
where the others stop).

The field's steps as the program calls them (`field_runs`,
`field/main_loop.c`) are functions of the game's state that the entries
the machine reaches run too: where an entry's work had no call into the
machine, the entry calls the same function; where the machine needs its
frames and call-outs to compare a step with the original's (the budgets'
turns, the song's calls to the driver, the pass's steps), the entry and the
program run one sequence whose steps are callbacks, the entry's calling
the step from the original's call. The program's function takes the plain
name (`field_fade_in`); where it would share a name with the machine's, the
machine's entry is `field_fade_in_entry` (its registry name unchanged) and a
step's machine callback is `*_on_the_machine`. Where a step meets a state the port doesn't take
-- a guard that would hand the call to the original on the machine -- or a
request, an event command or a menu only the machine runs yet, the program
stops there and says which. It reports any access the host had no memory
for (a register; none so far), and any video escape it couldn't follow.
What the rest of the game still assumes of a machine is TODO.md's "The
native program". The menus' tasks are the same way: each task's body is one
function of the game's state whose waits are callbacks, which the machine's
entry runs a turn at a time (its wait stops at the original's task switch)
and a coroutine runs through (its wait comes back the next frame); a task
the C has no body for yet (the scroll bar's, the records', the held
cursors') stops the program where it starts.

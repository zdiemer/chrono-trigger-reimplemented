# Playing

`ct_play` runs the game in a window: ported routines run natively and
everything else runs on the development system (see [RUNTIME.md](RUNTIME.md)
and [PORTING.md](PORTING.md)). It needs your own ROM (see the README) and SDL2.

Sound comes from the game's sound driver reimplemented in C (`game/sound/`,
[SOUND.md](SOUND.md)) playing through the S-DSP model (see the APU section of
[RUNTIME.md](RUNTIME.md)), and out through SDL audio. `--mute` turns it off.

## Build

Install SDL2's development package (CMake config or pkg-config), then build
with the main project. `ct_play` is skipped when SDL2 isn't found;
`ct_native` still builds and runs headless.

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

### Native build artifacts

The `native-player` job in `.github/workflows/ci.yml` builds `ct_native` in
Release mode without a ROM on Linux GCC, Linux Clang, macOS Apple Clang and
Windows MinGW GCC. Each uploads a `ct_native-<platform>-<architecture>` build artifact with
[the player README](NATIVE-README.txt). There is no release publication step.

`tools/package_native.py --build-dir build/main --output build/player-artifact`
packages an existing build into an empty destination. It copies only the
executable, README and, on Windows, the recursively discovered non-system DLLs
using `tools/dist.py`'s dependency checker. Local Windows verification found
only `SDL2.dll` beyond Windows itself. Linux/macOS players install SDL2 as
described in the README. Packs, ROMs, saves, generated code and other build
outputs are never copied. CI smoke-tests the packaged executable's `--help`;
a local Windows run also checked first import and a later ROM-free launch
using SDL's dummy video/audio devices and an isolated preferences directory.

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

`ct_dev --native on --generated off --no-interpreter strict`
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

**Drift or a bug.** The end state can't tell a movie that drifts from a port
that computes something else, so `tools/drift.py` finds where each `drifts`
movie parts from the reference and says why, and its baseline line gives that
cause. It plays the movie three ways with `ct_dev --state-trace` (work RAM and
save RAM every frame, nothing drawn): the reference, strict mode, and the hand
ports given the original's time (`--port-timing oracle --port-horizon
interrupts`, with the C sound driver: each port call charged what the
original's code takes for it). From each run it reads the frames of the game's
milestones -- a place entered, a song started, the field fading in or going
dark, a battle member's command menu opening, a text window, a story step --
and the states the saved game passes through, and matches them in order with
the reference's. The run parts at the first saved-game state or milestone the
reference doesn't have there, and the report says what moved before it: the
milestone that set the shift in force and by how much, the play clock's offset
(the field seeds its random draws from it), and a battle that starts from
another random number. None of that decides the verdict, since a strict run's
frames part from the reference's by the ports' estimated time and a port's
mistake later would show the same way. The run with the original's time does: the movie is **drift** only when
that run plays as the reference to its end, so the original's time alone makes
the difference, and a **bug** when it parts anywhere or ends short. That run
times only the calls it can, and the rest run the original's code, so the report
counts both (`with the original's time ... (hand ports N calls, the original's
code M block calls)`, a block counted per call, finer than a port's call): the verdict answers for time over the ports' calls, and
shadow mode for their results. Each run copies the reference's presses, so a
run that parts never comes back; only the first parting is a finding. Findings
a reader verified by hand go after `hand-verified:` on the baseline line, and
`--baseline` keeps them as they are. Over the 45 `drifts` movies every one is
drift. 34 part in a battle whose menus open some frames off, so the recorded
presses meet other turns, 19 of them also starting the battle from the next
random number (`$7E:29AD`, from the menus' frame count). The traces are ROM-derived and go to a scratch
directory (`--keep DIR`, outside the repository, keeps them for `tools/trace.py`).

```sh
python3 tools/drift.py --ct-dev build/main/ct_dev --drifting -j 2     # every `drifts` line
python3 tools/drift.py --ct-dev build/main/ct_dev --movies events-death-peak-wind
```

A drift can still be worth fixing where a frame-level fault sets it off.
Death Peak's wind movie drifted with no milestone showing why: the wind on
the slope ($0F4) gusts and calms on a cycle the map's script starts once the
map is up ($7F:0218, which the `death_peak_wind` route reads), and the slope
faded in 15 frames late (frame 3015, against 3000), so the recorded
departures from shelter met a gust and the party was blown back to the world
map. The load starts song $00, does some 27 frames of work and starts song
$22; the hand ports' time for that work is charged after the port stops, so
$22 reached the C sound host a frame after $00, which charged it the 14
frames left of $00's echo wait, a wait the original's driver had sat out
during the work. A song's start now lets the port's time pass first, as the
original's CPU half writes it after that work (SOUND.md, "The port as
built"), $22 starts at 2991 as in the reference, and the movie ends as the
reference play does.

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
    [--generated only|on|off] [--renderer native|scanline] [--save-dir dir]
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

The sound driver is the C one (`game/sound/`, [SOUND.md](SOUND.md) "The port as
built"). The game hands its boot and its commands to `frontend/sound_host.c`
as calls (`core/sound_commands.c`): the driver boots, and each command is
carried out as the original's CPU half took it, a song with its samples
placed in sound memory by the driver itself, so nothing is uploaded and the
SPC700 stays idle. The host runs the driver's ticks on the APU's time, waits
out the echo's buffer as the original does, and lets the time the original's
upload took pass (the time model). The reference play is the exception:
replaying or recording a movie, `--load-zst`, `--native off` or `shadow` and
`--generated only` run the cartridge on the emulated console, the cartridge's
own driver on its sound processor, whose timing a movie is recorded against
(REIMPLEMENTATION.md, "The sound driver's teardown"). The C driver's voices are made into sound by the native mixer
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
one saved in the reference play (or by `ct_dev`) loads only there, and so do
quick saves made before the C driver became the default, which were all saved
with the original's. A state that can't be read -- damaged, from another ROM, from before
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
does `--native off`); with `--generated on` the timing differs
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
| `frontend/audio_fill.h`, `audio_fill.c` | the resampler and ring buffer between a source of 32000 Hz stereo samples and an audio device's rate (above), shared by both programs |
| `frontend/window.h`, `window.c` | `window_run`: the shared SDL window, audio device, keyboard and gamepads, and frame pacing; callbacks supply one frame, pixels and stereo samples |
| `frontend/window_player.c` | `ct_play`'s adapter: the player, quick saves and desktop files behind those callbacks |
| `frontend/bindings.h`, `bindings.c` | the window's controls: keys and pad inputs to SNES buttons and player actions |
| `frontend/headless.c` | `run_headless`: `--frames`, `--hold`, `--screenshot`, `--wav` |
| `frontend/storage.h`, `storage.c`, `asset_files.c` | shared file naming, byte storage and importing packs; `files.c` adapts them to `ct_play` |
| `frontend/track.h`, `track.c` | shared input history and `.ctm` recording |
| `frontend/resume.h`, `resume.c` | shared native checkpoint history, encoding and one-frame reconstruction, with no paths or devices |
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
`frontend/storage.c` names them after the ROM or the recording and the drivers write them
next to the ROM or into `--save-dir` -- so a host without a filesystem keeps
them its own way. The runtime's file helpers are the same split: the player
uses `snes_state_write`/`snes_state_read`, `zst_import`, `ct_movie_parse`/
`ct_movie_format` and `misses_parse`/`misses_format`, and the path functions the
development tools call (`snes_state_save`, `zst_import_file`, `ct_movie_load`,
`misses_write`) are wrappers over them.

### The native program

`ct_native --server --rom roms/ct-usa.sfc --sram save.srm` exposes the native
frame loop to the state-driven route scripts, through the line protocol in
[MOVIES.md](MOVIES.md#native-program-checkpoints). It links the same native
program as window play. Its replay checkpoints reconstruct live coroutine
stacks from power-on input; these are separate from the player's future
quick saves and from emulator state files.
The [route outcome ledger](history/native-route-outcomes.md) records native
and shipping results; the gate runs the fast native prefixes every time and
the 42 passing full routes with `--long-routes`, retaining the fast prefixes
of excepted endings. Fourteen full routes have explicit failure, timeout or
incomplete-audit reasons; the ledger distinguishes them.

`ct_native` is the game with no emulator under it, the first version of
what `ct_play` becomes (PLAN.md, "The machine out of the product"
). With SDL2 it opens the same window as `ct_play`, with the native
renderer, live keyboard/gamepad input and the C sound driver through the
native mixer and SDL audio. Without SDL2 it remains a headless program.

On Windows, from the repository root with the toolchain and SDL2 DLL on
`PATH`, build and start a fresh game (use an unused SRAM name to start afresh):

```powershell
cmake -S . -B build/main -G Ninja -DCT_ROM=roms/ct-usa.sfc -DCT_WERROR=ON
cmake --build build/main -j 2
.\build\main\ct_native.exe --rom roms/ct-usa.sfc --sram build/native-new.srm
```

To start from a cartridge save, copy it to a working save and run:

```powershell
Copy-Item oracle/saves/fantasyanime/ctsave12-side_quests1.srm build/native-save.srm
.\build\main\ct_native.exe --rom roms/ct-usa.sfc --sram build/native-save.srm
```

Choose a slot on the game's load screen. Your own 8 KiB `.srm` works the same
way. Native window play writes SRAM after the game completes a save, detected
by its final last-saved-slot write. Starting afresh can clear completion
metadata in memory, but does not write that change to disk. Both players
write cartridge saves to a temporary file beside the destination, flush and
close it, then replace the destination; a failed write leaves the old save
intact. An interrupted process may leave a temporary file, which does not
prevent later saves. A missing save file starts erased.
Without `--sram`, window play uses `roms/ct-usa.srm` for this ROM. Headless
runs retain their old behavior: an explicit save is read only, and without
one they start erased. Keep screenshots, asset packs and end states out of Git.
The asset pack uses the same importer and format as `ct_play`; `--assets`
selects another pack and `--save-dir` selects where the default pack and saves
are kept. Give the ROM as a positional argument (or drop it on the executable)
on the first launch. Once imported, later launches need no arguments and no
ROM: the desktop driver remembers the pack and default save location in SDL's
per-user preferences directory. `--import ROM` rebuilds the pack and exits.
`CT_NATIVE_HOME` can select an existing preferences directory, including for
isolated tests. An incompatible pack needs the ROM to import again. No ROM is
needed at build time for `ct_native` itself.

Controls use `ct_play`'s default bindings: arrows move, **X/Z/S/A** press
SNES **A/B/X/Y**, **Q/W** are L/R, **Return** is Start, **Right Shift** is
Select. **P** pauses, **Tab** fast-forwards, **F11** or **Alt+Return** toggles
fullscreen, and **Escape** quits. SDL gamepads use the D-pad/left stick,
face buttons by position, shoulders, Start/Back and right trigger for fast
forward (see the table above). `--scale 3`, `--fullscreen` and `--mute` work
in the shared window. Both players read `ct_play.keys` beside the executable,
then `--keys FILE` and `--bind INPUT=ACTION` overrides through the same driver
code. Native states are separate from emulator `.state` and ZSNES files.

**Quick saves.** F5 saves the current completed frame to `<rom name>.native-state`
beside the cartridge save; F7 restores it. Native routines keep live C stacks,
including menu tasks, which cannot be stored as portable stack addresses. A
native checkpoint instead holds the initial cartridge save, scene selection,
both controllers' input history and an outcome checksum. Loading starts the
same program and reconstructs those continuations one frame per driver call,
including video, sound, random state and time owed. Every completed frame is
saveable, including frames with suspended routines. A request made during
restoration waits until restoration completes. Nothing hands execution to an
emulator or drops owed time.

Loading takes longer with longer sessions. The window shows the current and
target frame, continues handling events, and mutes restoration audio; even a
paused game can finish loading. Checkpoints require the same execution-source
fingerprint, and reject incompatible or damaged files before restarting. Keep
cartridge `.srm` saves for progress across builds. The reconstructed RAM, save
RAM and picture must match the checkpoint checksum before play continues.
`--save-state FILE` and `--load-state FILE` expose the same format headlessly;
`--frames N` remains the total frame limit since power-on, including restored
frames. `--screenshot FILE` writes the last picture; headless `--wav FILE`
records sound after restoration.

**Recording.** `--record FILE.ctm` uses the recorder shared with `ct_play`.
It starts with empty SRAM unless `--sram` is supplied, never writes cartridge
saves, flushes every 600 frames and on exit, and can continue `--movie` input.
The header names the starting save and its CRC-32. A final no-button event
preserves trailing idle frames in existing readers. Native timing differs
from the reference's: `ct_play` and `ct_dev` accept the movie and replay its
inputs, but may take a different route. Recordings cannot start with `--scene`
or `--load-state`, or continue a two-controller movie (the shared recorder
records controller 1). While recording, only a checkpoint from that recording
loads; loading rewinds its input. Quick saves then use the movie's name.

Measured on the native player: new-game frame 5200 to 7200, menu frame 7400 to
8000, and Magus frame 5000 to 5600 resumed with byte-identical projected game
state, SRAM, picture and subsequent PCM audio. Three successive live menu
reloads also matched uninterrupted continuations. A 7200-frame recording of
new-game replayed to the same native end state, loaded in `ct_play`, and gave
`ct_dev` the same reference trace as the source movie. ROM-free
`player_files_test`, `recorder_test`, `resume_test`, `bindings_test` and
`fiber_test` cover naming, trailing idle frames, starting-save metadata,
checkpoint corruption, binding overrides and discarded coroutine stacks.
`python3 tools/player_check.py --build build/main --rom roms/ct-usa.sfc`
repeats the ROM-backed continuation and cross-player recording checks serially;
its generated files remain under `build/player-check`.

The window runs until closed. `--frames N` selects the old headless behavior,
while `--headless` alone runs 600 frames. `--window` overrides `--frames` so a
bounded movie can be watched with sound and exits automatically:

```powershell
.\build\main\ct_native.exe --rom roms/ct-usa.sfc --window --movie oracle/movies/new-game.ctm --frames 7200
```

Movies supply both controllers through the last recorded input, then live
input takes over in a window. They play at normal speed unless fast forward
is held. Headless measurements and scene selection still work:

```sh
build/main/ct_native --rom roms/ct-usa.sfc [--assets pack] [--frames N] [--sram save.srm] [--movie in.ctm]
                     [--shot-every N --shot-dir DIR] [--end-state out.bin] [--locations] [--scene flags[,song]]...
```

With `--movie` the controllers press what the movie's frames say,
frame for frame; the native program's frames keep the original's time only as
far as the frame-level time model does (REIMPLEMENTATION.md: the game's own
work at the rates the machine calibrated, the sound driver's uploads and a
load's unpacking and sends, the whole frames they fill being lag frames, the
run's last line counting them, `--lag-log` naming each), so a long movie can
still go elsewhere than its reference play: TODO.md, "The native program". `--locations` lists each
place the run enters, with its frame, as `ct_dev --locations` does for the
reference play, and `tools/places.py` sets the two side by side; `--end-state`
writes work RAM and save RAM at the end, as `ct_dev --end-state` does.

`--timing-report` reports each routine's number of timed spans, access bus
clocks (including legacy replacements), `charge_as` bus clocks and resulting time in thousandths of a
master clock. It also reports untagged work (routine zero) and the unpacker's
separate charge. Accounting is optional and does not change the run.
`native_check.py --timing-report --logs DIR` collects it over selected movies.
These are work totals, including handlers; they are not elapsed frames or
counts of machine calls. A retired operation can charge separate spans for
its own accesses and the original work it replaced.
Shadow reports measure overlapping parent and child calls, so their totals
must not simply be added when comparing them with this report.

Each frame's picture is drawn by the native renderer
(`runtime/platform/native/render.c`) from the video service's state, and the
run ends saying whether it is showing and the longest stretch it was black
(`picture: dark since frame F ...`); `--shot-every` writes every Nth as a PNG.
A failed screenshot is logged and the run continues, including `--end-state`.
`tools/native_check.py` runs every library movie so, and the gate fails a
movie that ends on a dark screen: getting to the last frame says nothing of
the picture. The one way past that is a line in
`tools/native_dark_exceptions.txt`, for a run whose time carries it late into
a stretch the reference play is itself black for, saying where. That is how the line handlers' end of the picture was found
taken as the whole frame's (the field's, the battles' and the scene boot's
blank and line effects off below line 211, which left a picture blank, or with
no text layer and its backgrounds garbled where the per-line settings were
lost): the native video backend keeps what the line handler changes for the
lines below it (PLATFORM.md, "The model").

For the same reason a movie that plays its way to a story scene seldom
brings the native program there: `--scene` starts the game in the scene engine after the
power-on (`program_starts_in_a_scene`), with the flags an event hands it and
the song the field would have had playing, which a scene that keeps the
field's song waits on, and goes on to the field after it. Given again, the
scenes run one after another, each taking up what the one before left, as
an ending's do. As the story plays them: `--scene 0x24` Lavos falling,
`--scene 0x26,0x50` the Epoch's dive, `--scene 0x2B,0x3F --scene 0x2E,0x3F`
the Beyond Time ending's two and `--scene 0x2B,0x3F --scene 0x2F,0x3F` the
reunion's, `--scene 0x2D` the staff roll.

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

Selected module state lives in generated C structs behind those same
accessors, with alias views sharing each owner's fields. Other variables
still use a residual work-RAM image. The native
address view projects struct fields for save/load, video sources, end states
and route queries, including the original's low-bank mirrors; it does not
keep a second copy of moved fields. Cartridge `.srm` files retain their
format. [STATE.md](STATE.md) describes the mechanism, inventory and remaining
image-backed share (`python3 tools/ram_map.py storage`). Emulator-backed
`ct_dev`, `ct_drive` and hybrid `ct_play` retain RAM-backed accessors.

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
driver's reader, and the driver draws the frame it makes. The SNES's escapes a host has no
meaning for are counted and reported, but for the two whose meaning is
memory: the field's clear, the multiplier's product copied over work RAM, and
the menus' clears and copies through work RAM's data port, which the host
carries out at the port's address. Audio plays
the C sound driver (`game/sound/`) in the sound processor's place
(`frontend/sound_native.c`, the native counterpart of `sound_host.c`): the
game's side of the commands (`core/sound_commands.c`) hands it the boot and
the commands as calls, the game's sends through the service reaching it as
C (`sound_driver_send`); the
scenes' commands come on the ports, and the driver's time passes a frame at
a time and while the game waits on a port. Saves read and write the save
RAM the driver loaded. Work RAM powers on holding `$55` throughout, as the
reference emulator's does, since the game reads some of it before writing it
(the menus' records of the members not yet joined). A cartridge byte that no
resource of the catalogue holds has no answer without the ROM, so a read of
one stops the program with its address, as a step only the machine runs does.

The desktop storage/import helpers, recorder, binding configuration and SDL
window are shared with `ct_play` (`storage.c`, `asset_files.c`, `track.c`,
`bindings_config.c`, `window.c`). Checkpoint input history and reconstruction
(`resume.c`) are shared by player quick saves and route-server slots. The driver calls `resume_advance` once per frame;
completion checks the caller's state checksum and any captured host-service
checkpoint. The route server retains exact audio-call verification and sound
state reuse. Player checkpoint encoding takes and returns bytes; the driver
chooses names and writes files atomically. Resetting
suspended stacks goes through `fiber_destroy_tree` in `runtime/port/fiber.h`.
The shared restart also recreates the renderer because its cached lines own
pixels in the discarded framebuffer. `tools/player_check.py` compares player
checkpoint continuations (RAM, picture and sound) and route slot/file
continuations (RAM and picture, with the slot's audio-call checks enabled).

The driver (`frontend/ct_native.c`) starts the host backends with the
program and advances one frame per call, from its headless loop or the shared
window driver: the vblank handler, the main thread as a
coroutine until it waits for the frame, the line handler, then a frame of the
sound driver's time. Where the program's next step is still only a machine
entry, `program_needs_the_machine` stops it with exit status 1 and the driver
prints the routine and frame to stderr in either mode. Fatal
`no_machine.c` entries also name their function and current frame, and exit 3
(a fatal failure, distinct from an intentional stop). A crash or timeout
after a stop diagnostic still fails `native_check.py`. Run from a
terminal and include that line when reporting a failure. For example, from a
cartridge save with `saves/menus-field-menu.ctm`'s
presses, a stop at the equipment's slots window would look like this:

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
of the menus into the game, and the save screens and the party exchange, and after a game over the title again;
the switches a step presses, the time gate's tunnel, and the Day of Lavos
lost (the world turning, the planet, and the console's reset to the title);
the Epoch's time dial and map view, and the jet bike race from its opening;
the menus the player opens through the equipment's slots and list, an item
moved or used and the healing it shows, the techs' list and the members a
tech is used on, the settings' meters, gauge and buttons, and the End of
Time gate's exchange with its song and swirl (history/reimplementation-chunks.md, "The
native program through the menus the player opens"); and a shop, its goods
bought and the party's items sold, and the party's equipment changed from
it. 43 of the 45 save movies play to their end (history/reimplementation-chunks.md, "The
native program through the shops"), all but two made to find where it
stops, Frog's memory at the Magic Cave and the winged Epoch's map after
Zeal's fall (TODO.md, "Refusals in real play"). The story's scenes run as C
too, each to its end (`--scene`, above): Lavos falling to the planet, the
Epoch's dive into Lavos, the shared endings' zooming pictures, captions,
stars, credits and last scene, the Beyond Time ending's night, and the staff
roll and The End; the scene's main loop scales the race's racers, the
endings' picture and the Epoch a bitmap at a time (`scene_scale_the_bitmap`).
Mode 10's ending, which no recording reaches, stops at its picture's vblank
task (`$7E:9CE2`).

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

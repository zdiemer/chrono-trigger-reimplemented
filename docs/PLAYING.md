# Playing ct_native

Build with a C17 compiler, CMake, Ninja, Python and optionally SDL2:

```sh
cmake -S . -B build/main -G Ninja -DCMAKE_BUILD_TYPE=Release -DCT_WERROR=ON
cmake --build build/main -j 2
python3 tools/rom.py stage <your-cartridge>
build/main/ct_native --rom roms/ct-usa.sfc
```

The build needs no ROM. SDL2 supplies windowed video and audio; without it the
program is headless. Use `.exe` on Windows. `--help` lists display options.
The native frontend imports the user's cartridge into a local asset pack on
first use. `--assets <pack>` chooses the pack; no game assets are distributed.
Beside the pack, `<pack name>.palettes`, `<pack name>.fonts`,
`<pack name>.graphics`, `<pack name>.maps`, `<pack name>.scripts`,
`<pack name>.commands`, `<pack name>.text` and `<pack name>.records` hold the
game's palettes, fonts, tiles, maps, event scripts, battle, menu and enemy
scripts, text and tables of records as text, written from
the pack the first time: edit a color, draw a glyph or a tile, change a map's
tiles, a script's command or a line of dialog there and it is what the game
shows from the next start; delete a file to write it afresh.
`tools/sheets.py <pack name>.fonts DIR` (or `.graphics`, with `--only
NAME...` for a few) draws each font or graphics resource into DIR as a
picture; `tools/sheets.py <pack name>.maps DIR --location N` draws a
location's map (or, from `0x1F0`, a world map) assembled from its tile set
and palette, and `--tilemap NAME
--tiles NAME` a tilemap with a graphics resource's tiles.
`--sram <save.srm>` loads a cartridge save, including fixtures in `saves/`.

## Movies and diagnostics

`--movie <input.ctm> --frames N` runs headlessly to the movie's declared end.
`--locations` logs visited places; `--end-state <file>` writes the saved-game
projection source for outcome checks. `--shot-every N --shot-dir <dir>` captures
screens. These outputs are local, uncommitted artifacts. A reporting option
only looks: its reads of the game's state are a guard's look (`vars_peek`),
which owes no time, so the run plays the same with or without it. The ctest
`observers` (tests/frontend/observers.py) plays title-start with each of
`--timing-report`, `--locations`, `--lag-log`, `--outcomes`, `--end-state`,
`--screenshot` and `--shot-every` and holds the end state, the last picture
and the time owed to the plain run's. `--window` forces a
window even with a frame limit. `--drive` exposes the route protocol over
stdin/stdout and cannot combine with a movie, scene fixture or window.

## How the player is built

The frontend owns the loop, event polling and filesystem. The game advances
one frame at a time through native platform services. Persistence transfers
bytes under names; `frontend/file_store.c` handles host files. Main-thread and
menu-task stack switching goes through `runtime/port/fiber.h`. The native
renderer and mixer operate on service state, with no emulation backend.

## The native program

`game/core/program.c` boots the services and calls field, scene and warp modes
directly. Game operations take game state and the platform services they use.
The program stores each mode's vblank and line callbacks as C function pointers
in its own state. The frame driver calls those callbacks and resumes the main
coroutine; waits yield through the timing service. Reset and title requests
return to the program loop rather than changing a program counter or stack.

Native time comes from counted work and the authored rates in
`runtime/port/time_rates.c`. The original addresses retained as timing keys and
diagnostic origins are never executed. There is no registry, emulated CPU,
machine-entry macro layer or missing-machine stub backend.

`tools/dist.py` builds and packages ct_native and its host libraries. On Windows
it follows DLL imports and copies non-system dependencies; on Unix it lists
external shared libraries for the target machine to install.

## Saves, controls and recording

Give the ROM as a positional argument (or drop it onto the executable) on first
launch. Later launches use the pack and save location remembered in SDL's
preferences directory without needing the ROM. `--import ROM` rebuilds the pack
and exits; `CT_NATIVE_HOME` selects an existing preferences directory.
`--save-dir` chooses the default pack and save directory. Window play writes
cartridge `.srm` saves atomically after the game's save completes; headless runs
read their starting save without writing it back. Movies find their starting
save by the CRC32 header if `--sram` is omitted.

Arrows move; X/Z/S/A press SNES A/B/X/Y, Q/W press L/R, Return presses Start,
and Right Shift presses Select. P pauses, Tab fast-forwards, F11 or Alt+Return
toggles fullscreen, F9 changes the scaling, F10 the filter and F8 the pixels'
shape (below). Escape asks before it quits: a small prompt stops the game, where a second Escape
quits and Return plays on; the window's close button quits at once. SDL
gamepads use positional face buttons, D-pad/left stick, shoulders, Start/Back
and right trigger for fast-forward. `ct_native.keys` beside the executable
supplies bindings (`Scaling`, `Filter` and `Quit` are actions like the rest);
`--keys FILE` and `--bind INPUT=ACTION` override them. `--scale` and `--mute`
control the window. `--view WIDTHx224` shows a wider picture (up to 480 across,
the original's screen in its middle; docs/PLATFORM.md, "The wider view, as
built"), in the window and in screenshots; the game plays the same at any view.

F5 saves a snapshot of the completed frame; F7 loads it at once.
`--save-state` and `--load-state` do the same headlessly. Snapshots belong to
one build and memory layout (below); use cartridge `.srm` saves for progress
across builds.

`--record FILE.ctm` records native input from power-on, including starting-save
metadata and trailing idle frames. During recording only a checkpoint from that
recording can load; it rewinds the recorded input. Recording cannot start with
`--scene`, `--load-state` or a two-controller movie. `frontend/track.c` owns the
input history, while the frontend owns names and atomic writes.
`python3 tools/player_check.py --build build/main --rom roms/ct-usa.sfc` checks
opening, menu and battle continuations from a state file loaded by another
process (state, picture and PCM), route slot/file restores, recording playback
and `--check-snapshots` over a field, menu, battle and scene movie, two processes
at a time; the gate runs it. Outputs stay under the ignored
`build/player-check` directory.

### Scaling and fullscreen

The renderer's frame goes to the GPU as a texture and is scaled there
(`frontend/presentation.h`, drawn by `frontend/window.c`); the picture the game
draws is the same under every choice.

- `--pixels tv` (the default) shows the pixels as a television did: 208 of
  the 224 rows, eight hidden top and bottom in its overscan, and each pixel
  7:6 wide, the 256 across filling a 4:3 screen's width. `--pixels square`
  shows all 224 rows with square pixels. F8 switches.
- `--view 16:9` (or `16:10`, `21:9`, `4:3`) works the view's width out from
  the pixel shape and the rows shown, so the picture shown has that shape:
  398 across for square pixels at 16:9, 317 for tv pixels; held within 256
  to 480 (21:9 square is 480, a little narrower than 21:9; 4:3 tv is the
  original's 256). `--view WIDTHx224` gives the width itself. The width is
  worked out at launch, since the game draws the view it started with: F8
  during play changes the pixels' shape at once and the view's width at the
  next launch.
- `--scaling integer` (the default) gives every picture pixel the same whole
  number of output pixels: down, the most rows that fit; across, the whole
  number nearest that times the pixel's width. The picture is pixel-exact and
  letterboxed: with tv pixels 5x6 at 1080p (1536x1040), 6x7 at 1440p
  (1792x1248), 10x12 at 4K (3072x2080), its aspect within 3% of 7:6; with
  square pixels and a 16:9 view, 4x4 at 1080p (1592x896).
- `--scaling fit` fills the output's height (or width) at the picture's shape, drawn
  sharp-bilinear: scaled up by whole numbers to the size at or above the
  output's, then smoothly down to it, so an edge blurs over at most one
  output pixel.
- `--filter scanlines` darkens the lower third of each picture row (none
  where a row is under two output pixels tall); `--filter none` is the default.
- `--fullscreen` fills the screen, `--fullscreen-mode borderless` (the
  default) over the desktop at its own resolution, `exclusive` with the
  display set to the desktop's mode for the window alone. F11 toggles the
  chosen mode.

The window remembers the last scaling, filter, fullscreen, fullscreen mode,
pixel shape and view in window.txt in the preferences directory, as `name = value` lines; the
command line's choices replace them and are remembered in turn. Headless runs
never read it. `--present WxH` with `--screenshot` writes the frame
as the window would show it on a display that size, with `--scaling`,
`--filter`, `--pixels` and `--view`, through SDL's software renderer: the way to look at a
presentation without a window. `presentation_test` checks the layouts at
1080p, 1440p and 4K, and the views' widths for each shape.

## Snapshots

A snapshot (`frontend/snapshot.h`) is the whole program at a frame's end, so
loading one costs its size (a few hundred KiB, well under a millisecond) and
not the frames that led to it. It holds the game's state record
(`runtime/port/state_host.c`, which also keeps the program's handlers and the
open menus' coroutines), the services' state (the timing service's waits, debt
and generator, the pads, the video observer's memory and last frame,
`runtime/platform/native/snapshot.c`), the C sound driver with its mixer and
unplayed samples, and the suspended fibers. The driver adds its frame count
and picture statistics; on a load the renderer starts afresh and redraws the
restored frame.

The main thread's waits and the menus' tasks stay where they are, deep in
their C calls; the fibers make that state data instead of rewriting it as
state machines (`runtime/port/fiber.c`). Every fiber runs on a stack from a
fixed pool, and a suspended fiber is its stack's bytes from where it stopped
to the top and its context: on Windows x86-64 a stack pointer, the registers
pushed by a short assembly switch; elsewhere the `ucontext_t` kept in the
slot. A snapshot copies those bytes and a restore copies them back to the same
addresses. The bytes hold pointers -- return addresses, frames, the game's
state, the asset pack's data, the sound player a command was being handed to
when the frame's time ran out -- so the pool, the game's state, the pack and
the sound player are taken from an arena reserved at a fixed address
(`runtime/port/arena.h`), and
on Windows ct_native links at a fixed image base. A run that makes the same
allocations lays them out the same, so on Windows a snapshot file loads in a
later run of the same build; `tools/player_check.py` proves it there. Each
snapshot records the addresses it depends on and a run laid out otherwise
refuses it, changing nothing. On Linux and macOS a suspended stack also holds
what only its own process can vouch for -- return addresses into libc's and
SDL's moved images (the `ucontext` trampoline), and stack-protector canaries
-- so a snapshot there also records its run, and a file loads only in the run
that wrote it.

What a snapshot leaves out has to hold nothing a frame goes on from. The
game's code keeps no state of its own outside the game's state record: the
only mutable statics in `game/` are lookups built from constant tables (the
script interpreters' opcode indexes) or from the run (the asset store, what it
served, its last resource and its count of reads outside the catalogue). A
suspended frame can hold no pointer into the services' heap -- the video
observer, the sound driver, the save's bytes -- because `Platform` is opaque
to `game/`: the game reaches the host only through service calls, which read
those pointers afresh, and the restore keeps them as the run has them. The
fibers' records are checked before anything changes, the services (which can
fail only where memory for the observer's copies runs out) are restored next,
and only then do the fibers switch, so a refused restore leaves the run as it
was.

`ct_native --check-snapshots EVERY[,SPAN]` (headless) takes a snapshot every
EVERY frames, plays SPAN frames, loads the snapshot through a file's bytes and
plays them again, failing on the first frame whose game state, picture or
sound differs. Over the whole movie library at 1009,90 every frame matched.

## Native build artifacts

The `native-player` CI job builds Release artifacts for Linux GCC/Clang, macOS
Clang and Windows MinGW GCC, without a ROM. `tools/package_native.py --build-dir
build/main --output build/player-artifact` packages the executable, the player
README and the host libraries into an empty directory: on Windows the DLLs
it imports; on macOS the dylibs, Homebrew's SDL2 (sdl2-compat) and the SDL3
it loads by name, each named relative to the executable and signed again ad hoc;
Linux players install SDL2 themselves. `tools/dist.py` does the same into `dist/`. CI smoke-tests the
packaged `--help`, then runs it where it was never built -- in a Debian 12
container with only `libsdl2-2.0-0` installed, and on macOS with Homebrew's
SDL uninstalled -- and uploads it without publishing a release. No cartridge,
asset pack, save or captured state enters the artifact.

What a clean machine needs, as the artifacts are built (docs/NATIVE-README.txt
says it for players):

- **Windows** (x86-64): nothing; the DLLs are in the directory.
- **Linux** (x86-64): glibc 2.34 or later (Debian 12, Ubuntu 22.04, Fedora 35
  and later) and the SDL2 runtime (`libsdl2-2.0-0`, `SDL2`, `sdl2`); the
  executable links only it and libc.
- **macOS** (Apple silicon): the version the runner built for and later
  (`macos-latest`, macOS 26 now): ct_native and Homebrew's dylibs take their
  lowest version from the machine that built them. Nothing to install. An
  Intel Mac or an older macOS needs ct_native built there (`tools/dist.py`).
- Everywhere, the artifact's zip drops the executable bit: `chmod +x
  ct_native` after extracting on Linux and macOS.

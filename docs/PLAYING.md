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
Beside the pack, `<pack name>.palettes` and `<pack name>.fonts` hold the game's
palettes and fonts as text, written from the pack the first time: edit a color
or draw a glyph there and it is what the game shows from the next start; delete
a file to write it afresh. `tools/font_sheets.py <pack name>.fonts DIR` draws
each font's glyphs into DIR as pictures.
`--sram <save.srm>` loads a cartridge save, including fixtures in `saves/`.

## Movies and diagnostics

`--movie <input.ctm> --frames N` runs headlessly to the movie's declared end.
`--locations` logs visited places; `--end-state <file>` writes the saved-game
projection source for outcome checks. `--shot-every N --shot-dir <dir>` captures
screens. These outputs are local, uncommitted artifacts. `--window` forces a
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
toggles fullscreen, Escape quits. SDL gamepads use positional face buttons,
D-pad/left stick, shoulders, Start/Back and right trigger for fast-forward.
`ct_native.keys` beside the executable supplies bindings; `--keys FILE` and
`--bind INPUT=ACTION` override them. `--scale`, `--fullscreen` and `--mute`
control the window.

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
and `--check-snapshots` over a field, menu, battle and scene movie, one process
at a time; the gate runs it. Outputs stay under the ignored
`build/player-check` directory.

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
state, the asset pack's data -- so the pool, the game's state and the pack are
taken from an arena reserved at a fixed address (`runtime/port/arena.h`), and
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
README and Windows host DLLs into an empty directory; Unix users install SDL2.
CI smoke-tests the packaged `--help` and uploads it without publishing a release.
No cartridge, asset pack, save or captured state enters the artifact.

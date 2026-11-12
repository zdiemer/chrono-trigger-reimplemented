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

F5 quick-saves the completed frame; F7 restores it incrementally while the
window stays responsive. `--save-state` and `--load-state` do the same headlessly.
These checkpoints require the same execution-source fingerprint; use cartridge
`.srm` saves for progress across builds. `ResumeReplay` in `frontend/resume.c`
reconstructs continuations from power-on input for both quick loads and route
checkpoints. Route slots can reuse exact audio-host replies. Mid-route bases
remain planned because replay cost grows with the session.

`--record FILE.ctm` records native input from power-on, including starting-save
metadata and trailing idle frames. During recording only a checkpoint from that
recording can load; it rewinds the recorded input. Recording cannot start with
`--scene`, `--load-state` or a two-controller movie. `frontend/track.c` owns the
input history, while the frontend owns names and atomic writes.
`python3 tools/player_check.py --build build/main --rom roms/ct-usa.sfc` manually
checks opening, menu and battle continuations (state, picture and PCM), route
slot/file restores and recording playback, one process at a time. Outputs stay
under the ignored `build/player-check` directory.

## Native build artifacts

The `native-player` CI job builds Release artifacts for Linux GCC/Clang, macOS
Clang and Windows MinGW GCC, without a ROM. `tools/package_native.py --build-dir
build/main --output build/player-artifact` packages the executable, the player
README and Windows host DLLs into an empty directory; Unix users install SDL2.
CI smoke-tests the packaged `--help` and uploads it without publishing a release.
No cartridge, asset pack, save or captured state enters the artifact.

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

`tools/dist.py` builds and packages ct_native and its host libraries. On Windows
it follows DLL imports and copies non-system dependencies; on Unix it lists
external shared libraries for the target machine to install.

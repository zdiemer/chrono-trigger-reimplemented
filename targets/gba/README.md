# ct_gba: the game on the GBA

`ct_gba` is the game on the Game Boy Advance, with its picture and its
sound: the lean build's game and state (`CT_LEAN`, `CT_FLAT_STATE`), compiled
for the ARM7TDMI by arm-none-eabi GCC with newlib, playing a movie the
cartridge carries and reporting how it ended over mGBA's debug log. Its bar
is full speed on real hardware. docs/PLATFORM.md, "The GBA build, as built",
says what it shows, what it approximates and what each part of its frame
costs; this file says where things are and how to build and run it.

## How the pieces fit

The game (`game/`) and the shared runtime (`runtime/`) are compiled as they
are for ct_native. The GBA supplies what ct_native's host supplies: a start
and a memory layout, fibers, a driver that runs a frame at a time, and the
platform services' backends. Nothing here is included from `game/`.

- `CMakeLists.txt`: its own project, configured with the GBA's toolchain.
- `toolchain/`: how the program is built and laid out.
  - `toolchain.cmake`: arm-none-eabi GCC for the ARM7TDMI, Thumb by default.
  - `gba.ld`: code, constants and the data's initial values in the
    cartridge; the data and the zeroed state in work RAM (EWRAM), then the
    arena to its end; the line interrupts, the code that runs most, the
    fibers' stacks and the driver's stack in fast RAM (IWRAM).
  - `fast_code.ld`: the functions copied to fast RAM, chosen from
    `run.py --profile`. `check_fast_code.cmake` fails the build where a
    line names a function the sources no longer define, so hot code can't
    drop back to the cartridge unnoticed, and warns where this compiler
    inlined one away or didn't need a library's.
  - `start.s`: the cartridge's header and the processor's start: the
    cartridge's waits (3 and 1, with the prefetch), the copies and zeroing,
    then `main`.
- `system/`: the driver and what the C library and the runtime need.
  - `main.c`: the driver. One frame of the game a turn of its loop, as
    ct_native's: the movie's pads, `tm_host_frame`, the sound player's frame,
    then the picture in the vertical blank. At the end it reports the frames
    and lag frames, a CRC-32 of work RAM and save RAM as
    `ct_native --end-state` writes them, each part's GBA frames of work a
    frame, the arena and the stacks' deepest use.
  - `system.c`, `debug_log.h`: the arena and the C library's heap over the end
    of work RAM, stdout and stderr to mGBA's debug log, a cycle count from two
    chained timers.
  - `interrupts.c`, `interrupts.h`: the one interrupt vector, each source to
    its own handler (the picture's lines, the sound's timer).
  - `fiber.c`, `fiber.h`, `switch.s`: runtime/port/fiber.h on the ARM, eight
    slots in fast RAM (the game's main thread's 3 KiB, the tasks' 2 KiB), each
    with a guard, and no snapshots.
- `services/`: the platform services' backends, one per service.
  - `video.c`, `video.h`: the video service on the GBA's own picture.
  - `mixer.c`, `mixer.h`: the sound driver's voices played, in the native
    mixer's place (frontend/sound_native.c takes it through
    `SOUND_NATIVE_MIXER` and `SOUND_NATIVE_MIXER_PLAYS`): the packed samples
    mixed at the GBA's rate, 32 samples at a time.
  - `direct_sound.c`, `direct_sound.h`: the mix played on Direct Sound A and
    B from a ring, a timer's interrupt moving it to the FIFOs.
- `host/`: tools that run on the host, not on the GBA.
  - `pack.py`: the packing step, the seed of the builder (issue #421). It
    verifies the player's cartridge, cuts the catalogue's resources from it,
    and appends them to the program with a directory the driver indexes in
    place, the movie as a pad word a frame and the save RAM it starts from.
    The program is code only; the packed `.gba` is never committed.
  - `samples.py`: the sound's samples converted from BRR to 8-bit PCM for
    the pack, their loops kept, each listed by its block's length and hash.
  - `run.py`, `mgba_run.c`: the check. `run.py` builds the runner against
    libmgba and zlib, packs the cartridge, plays it in mGBA's core with no
    window and no sound played aloud until the program says it is done,
    plays the movie in ct_native too, and fails where the two end states'
    CRCs differ. `--wav` and `--stream` write the GBA's sound as WAV files.

## Building and running

The ARM toolchain (MSYS2's `mingw-w64-x86_64-arm-none-eabi-toolchain`, a
distribution's `gcc-arm-none-eabi` with newlib, or devkitARM) on `PATH`, and
for running, mGBA's library and headers (MSYS2's `mingw-w64-x86_64-mgba`,
installed or only unpacked, or a build of mGBA) and a staged cartridge
(`tools/rom.py stage`):

    cmake -S targets/gba -B build/gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=<repo>/targets/gba/toolchain/toolchain.cmake
    cmake --build build/gba
    python3 targets/gba/host/run.py --mgba <mgba prefix> --program build/gba/ct_gba.bin \
        --ct-native build/main/ct_native --movie movies/title-start.ctm --shot 1500=title.png

`--shot FRAME=FILE.png` writes the GBA's picture once the game reaches a
frame; `--profile FILE` writes each function's share of where the processor
was as the movie played. CI builds the program (the `gba-arm-none-eabi`
job); with no cartridge it doesn't play it.

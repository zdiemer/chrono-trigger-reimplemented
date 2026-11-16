# ct_gba: the game on the GBA

`ct_gba` is the game on the Game Boy Advance, with its picture and its
sound: the lean build's game and state (`CT_LEAN`, `CT_FLAT_STATE`), compiled
for the ARM7TDMI by arm-none-eabi GCC with newlib. One ROM, `ct.gba`, is
both the game a player plays from the keypad, its save in the cartridge's
SRAM, and the program the checks drive from outside through mGBA's core.
Its bar is full speed on real hardware. docs/PLATFORM.md, "The GBA build, as built",
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
    arena to its end, with an assert that keeps 28 KiB of it free; the line interrupts, the code that runs most, the
    fibers' stacks and the driver's stack in fast RAM (IWRAM).
  - `fast_code.ld`: the functions copied to fast RAM, chosen from
    `run.py --profile`. `check_fast_code.cmake` fails the build where a
    line names a function the sources no longer define, so hot code can't
    drop back to the cartridge unnoticed, and warns where this compiler
    inlined one away or didn't need a library's.
  - `start.s`: the cartridge's header and the processor's start: the
    cartridge's waits (3 and 1, with the prefetch, or 2 and 1 where the
    packing step asked for them), the copies and zeroing, then `main`.
- `system/`: the driver and what the C library and the runtime need.
  - `main.c`: the driver. One frame of the game a turn of its loop, as
    ct_native's: the pads, `tm_host_frame`, the sound player's frame, then
    the picture in the vertical blank, or none where the frame ran late
    (never two frames in a row). Asked by a runner, it reports the frames and lag
    frames, a CRC-32 of work RAM and save RAM as `ct_native --end-state`
    writes them, each part's GBA frames of work a frame, the pictures'
    pacing (how many GBA frames apart they are, and the longest gaps), the arena and the
    stacks' deepest use.
  - `keypad.c`, `keypad.h`: the keypad as the SNES controller (Controls,
    below): the driver's bindings, given to the game through `in_host_set`.
  - `drive.c`, `drive.h`, `drive_box.h`: the program driven from outside.
    The driver names a box in work RAM in its debug log as it starts; a
    runner that reads the log arms it, and from then on the pads each frame
    are what the runner puts in the box's ring. Between frames with the ring
    empty the driver waits and answers the runner's reads of the game's
    memory and its asks for the report. Nothing arms it on hardware or in
    mGBA played by hand, and the driver reads the keypad.
  - `system.c`, `debug_log.h`: the arena and the C library's heap over the end
    of work RAM, stdout and stderr to mGBA's debug log, a cycle count from two
    chained timers.
  - `copies.s`: the C library's `memcpy` and `memset` in place of newlib's,
    ARM in fast RAM, eight words to a load and a store.
  - `divide.s`: the C library's division (`/` and `%`, which the processor
    has no instruction for) in place of libgcc's, ARM in fast RAM.
  - `interrupts.c`, `interrupts.h`, `nested.s`: the one interrupt vector,
    each source to its own handler (the picture's lines, the sound's timer),
    on a stack of its own; a handler can hand longer work on to run with the
    other interrupts on, on its own stack too (the sound's mixing).
  - `frames.c`, `frames.h`: what the time model decides of the game's
    frames, which the GBA, keeping no time of the original's (`CT_UNTIMED`),
    decides itself: ct_native's decisions replayed where a runner put them
    in the cartridge, the GBA's own time and line counter otherwise.
  - `fiber.c`, `fiber.h`, `switch.s`: runtime/port/fiber.h on the ARM, nine
    slots in fast RAM (the game's main thread's 2.5 KiB, and 1 KiB for each of the
    menus' eight tasks, which can all run at once, sized from the movie
    library's deepest use), each with a guard, and no snapshots.
- `services/`: the platform services' backends, one per service.
  - `cartridge_save.c`, `cartridge_save.h`: the cartridge's SRAM. The
    SNES's 8 KiB save at its start, as the .srm has it, is the game's save
    RAM, read and written where it is; then a mark (`CTSV`) that says it is
    there (a cartridge without the mark starts with the save erased); then
    the top of the game's work RAM, `$7F:B000` on, its coldest
    (`VARS_FLAT_COLD_FROM`). mGBA keeps the SRAM in the `.sav`
    beside the `.gba`.
  - `video.c`, `video.h`: the video service on the GBA's own picture.
  - `mixer.c`, `mixer.h`: the sound driver's voices played, in the native
    mixer's place (frontend/sound_native.c takes it through
    `SOUND_NATIVE_MIXER` and `SOUND_NATIVE_MIXER_PLAYS`): the packed samples
    mixed at the GBA's rate, 32 samples at a time, as the playback asks.
  - `mix.s`: the mixer's loops, ARM in fast RAM: a voice's wave into the
    mix four samples a turn, and the mix to each side's 8 bits.
  - `direct_sound.c`, `direct_sound.h`: the mix played on Direct Sound A and
    B from a ring by the sound DMA, a timer's interrupt every quarter of it
    starting the DMA again and having the mixer fill what was played.
- `host/`: tools that run on the host, not on the GBA.
  - `pack.py`: the packing step, the seed of the builder (issue #421). It
    verifies the player's cartridge, cuts the catalogue's resources from it,
    and appends them to the program with a directory the driver indexes in
    place, with `--profile`, the colours' table, and with
    `--script-records`, the locations' event scripts and the menus' object
    scripts read a command at a time, which `ct_native --script-records FILE`
    works out from the same cartridge (frontend/script_records.h). The program is code only; the packed
    `.gba` is never committed.
    `--fast-rom` asks for the cartridge's faster waits, `--bios` puts in
    the logo a GBA checks (Building and running, below).
  - `samples.py`: the sound's samples converted from BRR to 8-bit PCM for
    the pack, their loops kept, each listed by its block's length, hash
    and place in the SNES cartridge.
  - `streams.py`: the cartridge's packed streams unpacked for the pack, by
    the game's own steps, each with the counts its unpacking's time comes
    from; the game copies one in rather than unpacking it, where the place
    it goes takes it as it is (game/data/decompress.c).
  - `mgba_run.c`: the runner, the GBA's `ct_native --drive`: `ct.gba` in
    mGBA's core with no window and nothing played aloud, its pads from a
    movie or from commands on stdin (Driving it, below); with a movie,
    `--wav` and `--stream` write the GBA's sound as WAV files, and
    `--watch` every picture the GBA shows over a run of the game's frames,
    as a player sees them at the GBA's speed, and `--record` every GBA
    frame's picture with the game's frame it shows, for the census.
  - `run.py`: the check. It builds the runner against libmgba and zlib,
    has ct_native write the script records, packs `ct.gba` with them
    (`--no-script-records` without), plays the movie in ct_native with its time model's
    decisions written, has the runner play it on `ct.gba` from the save RAM
    the movie starts from with those decisions replayed, and fails where the
    two end states' CRCs differ.
  - `census.py`: the census of what a player sees go wrong (The census,
    below): every GBA frame's picture against ct_native's, and the
    stretches where pictures come late or are left out.

## Controls

The GBA has ten keys and the SNES twelve buttons:

| GBA | SNES | In the game |
|---|---|---|
| Pad, A, B, Start, Select | the same | B held runs; Start pauses a battle |
| L, R | the same | both together flee a battle; alone, the menus' pages |
| L or R held, then A | X | the menu |
| L or R held, then B | Y | |

A key keeps the button it was pressed as until it is let go: let the shoulder
go before A and the menu doesn't take an A. A shoulder held as the shift isn't
pressed itself while it makes the X or Y. The bindings are the driver's
(`system/keypad.c`), not the game's.

## Building and running

The ARM toolchain (MSYS2's `mingw-w64-x86_64-arm-none-eabi-toolchain`, a
distribution's `gcc-arm-none-eabi` with newlib, or devkitARM) on `PATH`, and
for running, mGBA's library and headers (MSYS2's `mingw-w64-x86_64-mgba`,
installed or only unpacked, or a build of mGBA) and a staged cartridge
(`tools/rom.py stage`):

    cmake -S targets/gba -B build/gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=<repo>/targets/gba/toolchain/toolchain.cmake
    cmake --build build/gba
    build/main/ct_native --rom roms/ct-usa.sfc --headless --script-records build/gba/script.records
    python3 targets/gba/host/pack.py --program build/gba/ct_gba.bin --out build/gba/ct.gba \
        --script-records build/gba/script.records

`ct.gba` is the game: open it in mGBA, or put it on a flash cart, and play.

A GBA's BIOS starts only a cartridge whose header holds its logo, which the
project can't ship. `pack.py --bios FILE`, with your own dump of the GBA's
BIOS (16 KiB, CRC-32 81977335), copies it in from the BIOS; without it the
logo is left blank, which mGBA and flash carts that patch the header (most
EZ-Flash and EverDrive loaders) boot and a GBA on its own doesn't.
`run.py --bios FILE` packs with it and starts mGBA in that BIOS, so the run
goes through the BIOS's logo and its check as a GBA's does.

`pack.py --fast-rom` (and `run.py --fast-rom`) writes a flag into the word
after the header that has the start set the cartridge's waits to 2 and 1
with the prefetch, rather than 3 and 1, which every cartridge handles. Most
flash carts (EZ-Flash, EverDrive) and the Analogue Pocket read the cartridge
at 2 and 1; some cheap reproduction cartridges don't, and a ROM packed with
it may crash or show garbage on them. It takes about 5% off the GBA's work a
frame (docs/PLATFORM.md, "The GBA build, as built"); the game plays the
same either way.

`pack.py --profile port-style` brightens the colours for a GBA's screen,
which shows them darker than a television: a gamma of 0.7 on each channel,
then 15% more saturation. The default, `original`, leaves the SNES's colours
as they are. The profile is a 64 KiB table in the cartridge that the driver
reads as it fills the palette (`services/video.c`, `gba_video_use_colours`);
`run.py --colours` packs with one.

Its save is the cartridge's 32 KiB SRAM (mGBA's `ct.sav`). A `.srm` from
ct_native or a SNES emulator becomes a `.sav` with the mark after it: its
8 KiB, `CTSV`, then `FF` to 32 KiB (`run.py`'s `gba_save`); what the
program leaves after the mark is work RAM's, not the save's.

To check it against ct_native:

    python3 targets/gba/host/run.py --mgba <mgba prefix> --program build/gba/ct_gba.bin \
        --ct-native build/main/ct_native --movie movies/title-start.ctm --shot 1500=title.png

`--shot FRAME=FILE.png` writes the GBA's picture once the game has played
that many frames; `--profile FILE` writes each function's share of where the
processor was as the movie played. CI builds the program (the
`gba-arm-none-eabi` job); with no cartridge it doesn't play it.

## The census

`census.py` finds the glitches and the stutter a player sees, so that fixes
come from a list rather than from play:

    python3 targets/gba/host/census.py --mgba <mgba prefix> --program build/gba/ct_gba.bin \
        --ct-native build/main/ct_native --out <dir outside the repo>

For each movie (`--movies`; by default the six the GBA's milestones name and
the gate's movies under 6,000 frames) ct_native plays it at the GBA's view
(`--view 240x160`), writing every frame's picture and its decisions, and the
runner plays it on `ct.gba` with the decisions replayed, writing every GBA
frame's picture with the game's frame the driver put on the screen last
(the drive box's `shown`). Each GBA frame must show that frame's picture
exactly, compared at the GBA's 5 bits a channel (and through the colour
profile, `--colours`). Where a GBA frame doesn't, its mismatch is `torn`
(its lines from that frame and the one before), `frame` (a whole frame early
or late), or split by how far its pixels are off: `colour` (at most 8 steps
of 31 on every channel: gradients, colour math, fades made another way) and
`picture` (the rest: what is drawn wrong or not at all). Mismatches of a kind
close in time, each mostly in the screen's 8-pixel cells of the one before,
make a cluster, ranked by its pixels summed over the GBA frames that show
it, with a PNG of its worst frame: ct_native's, the GBA's, and where they
differ. From the same records, every game frame over two GBA frames and
every picture left out, grouped into stretches ranked by the GBA frames they
cost, and every hang (two seconds with no game frame begun).

`--hand` runs play without ct_native's decisions, the GBA's own time
deciding its frames as on hardware: five minutes of power-on with no input
(`attract-idle`) and a pad script from the title through a new game's naming
to a walk in Crono's room (`new-game-by-hand`). These check the pacing and
hangs, and list as leads the pictures ct_native shows nowhere within 600
frames, against its picture with the most lines the same.

It writes `census.md` and `census.json` and the PNGs to `--out`; its working
files (about 100 KB a frame) go in `--out`'s `work/`, deleted movie by movie
unless `--keep`. With numpy it compares faster; it doesn't need it.

## Driving it

`mgba_run` drives `ct.gba` from outside as `tools/movies/driver.py` drives
`ct_native --drive`; `run.py` builds it into the program's directory (on
Windows, with mGBA's `bin` on `PATH` to run it):

    mgba_run ct.gba [--bios BIOS.bin] [--sav FILE.sav] --movie MOVIE.ctm --frames N [--shot FRAME FILE.png]... [--samples FILE] [--watch FROM TO DIR] [--record FILE] [--stall N] [--decisions FILE]
    mgba_run ct.gba [--bios BIOS.bin] [--sav FILE.sav] --drive

`--sav` is the SRAM to start from, held in memory and never written back.
`--drive` reads a command a line and answers a line, as ct_native's do:

| Command | Answer |
|---|---|
| `run N PAD [PAD2]` | `ok FRAMES`: N frames with the pads held (decimal, the SNES's bits) |
| `movie PATH TO` | `ok FRAMES`: the movie's pads up to frame TO |
| `peek ADDRESS LENGTH` | `ok HEX`: the game's memory from a SNES bus address (hex) |
| `wram PATH` | `ok`: work RAM's 128 KiB to a file |
| `shot PATH` | `ok`: the GBA's picture as a PNG |
| `end` | `ok FRAMES END_CRC SAVE_RAM_CRC`: the CRC-32s of `ct_native --end-state`'s file and of its save RAM |
| `save SLOT`, `load SLOT` | `ok FRAMES`: the whole machine kept in memory, and put back |
| `savefile PATH`, `loadfile PATH` | `ok FRAMES`: the same in a file (mGBA's state) |
| `sav PATH` | `ok`: the cartridge's SRAM as a `.sav` |
| `frame`, `quit` | `ok FRAMES`, `ok` |

The program's own lines go to stderr.

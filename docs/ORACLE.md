# Oracle harness

`ct_oracle` runs the original ROM in an emulator core, drives it from an input
movie, and records what memory looks like after every frame. The port will
write the same trace format, so `tools/trace.py diff` can point at the first
frame and address where the two disagree.

```
movie.ctm ─┬─> ct_oracle + libretro core + ROM ──> oracle.cttr ─┐
           └─> port                               ──> port.cttr  ─┴─> trace.py diff
```

## Building

Needs CMake 3.20+ and a C17 compiler. MinGW gcc is used for traces; CI also
builds it with gcc and clang on Linux and Apple clang on macOS, where the
loader shim uses `dlopen`. If CMake and Ninja aren't installed,
`python3 -m pip install cmake ninja` provides both.

```sh
cmake -S oracle -B build/oracle -G Ninja
cmake --build build/oracle
```

## Cores

Cores are downloaded for local use and never committed.

```sh
python3 tools/fetch_core.py            # snes9x and bsnes2014_accuracy into cores/
```

| Core | Role | Regions | Speed |
|---|---|---|---|
| `snes9x` | **Trace oracle.** Deterministic, and its savestates are pure snapshots. | WRAM, VRAM, SRAM, CGRAM, OAM | ~1200 fps |
| `bsnes2014_accuracy` | Accuracy cross-check (bsnes v094, cycle-accurate profile). | WRAM, VRAM, SRAM | ~250 fps |
| `bsnes` (current) | Not usable. `retro_get_memory_data` returns nothing. | none | |

Why Snes9x rather than bsnes is the trace oracle:

- **bsnes2014 randomizes power-on state.** `System::power()` seeds its RNG from
  `time(0)` and fills WRAM, VRAM, OAM, CGRAM, APU RAM and many PPU registers
  with random values. The switch that disables this (`configuration.random`)
  isn't exposed to libretro. Chrono Trigger never writes roughly 41 KiB of WRAM
  and 28 KiB of VRAM during the attract sequence, so two bsnes2014 runs differ
  there forever. Their framebuffers are identical, so the game logic itself is
  deterministic.
- **bsnes2014 savestates advance emulation.** Its `retro_serialize` calls
  `runtosave()`, so reading CGRAM/OAM every frame would change the run being
  traced.

Snes9x shows neither problem. Repeated runs match byte for byte in every
region, and traces taken with and without CGRAM/OAM extraction are identical.
Use bsnes2014 when a Snes9x behavior looks suspicious: compare framebuffers,
or restrict memory diffs to addresses the game initializes. A bsnes build with
`configuration.random = false` would remove the first problem.

## Running

```sh
build/oracle/ct_oracle --core cores/snes9x_libretro.dll --rom roms/ct-usa.sfc \
    --frames 1800 --movie oracle/movies/title-start.ctm --trace traces/title-start.cttr
```

| Option | |
|---|---|
| `--core <file>` | libretro core (`.dll`, `.so`, `.dylib`) |
| `--rom <file>` | ROM image; `tools/rom.py stage` puts a verified one at `roms/ct-usa.sfc` |
| `--frames N` | number of `retro_run` calls |
| `--movie <file.ctm>` | input movie; no input without it |
| `--trace <file.cttr>` | write a state trace |
| `--trace-from A`, `--trace-to B` | record only frames A..B (inclusive); the first recorded frame is the keyframe |
| `--shot-every N --shot-dir <dir>` | PNG of the framebuffer on every frame divisible by N |
| `--wav <file>` | 16-bit stereo audio at the core's sample rate |
| `--sram <file>` | start with this cartridge save: copied into the core's save RAM (`RETRO_MEMORY_SAVE_RAM`, which must be the file's size) after the game loads, before the first frame |
| `--option key=value` | override a core option; otherwise each option's default is used |
| `--no-ppu-state` | skip CGRAM/OAM extraction |
| `--work-dir <dir>` | core system/save directory (default `<temp>/ct_oracle`) |
| `--verbose` | log core options, memory maps, unhandled environment calls |

The core starts from power-on with no save file unless `--sram` gives one, and
runs as fast as the host allows. Keep traces, screenshots and audio in gitignored locations such as
`traces/` or `captures/`, or outside the repository. They are derived from the ROM.

## Frame numbering

- Frame F is the F-th call to `retro_run`, counting from 0 right after the ROM is loaded.
- The movie's buttons for frame F are what the core sees for every input poll
  during `retro_run` number F.
- State for frame F is captured after `retro_run` number F returns.

Where `retro_run` starts and stops relative to the SNES's own frame (vblank,
auto-joypad read) is up to the core. With Snes9x, START held on frame 1620 is
already visible in `$7E:0003` in the frame-1620 state. The port has to pick the
same boundary: run the game until the point where Snes9x returns, so frame F
means the same thing on both sides.

## Movie format (`.ctm`)

Text, one event per line; `#` starts a comment.

```
# <start_frame> <frame_count> <buttons>
1620 5 START
2000 1 A+RIGHT
```

- `buttons` is `-` for none, or `+`-joined names from
  `B Y SELECT START UP DOWN LEFT RIGHT A X L R`.
- An event holds its buttons for frames `start_frame .. start_frame+frame_count-1`.
- Overlapping events OR together.
- Movies recorded with `ct_play --record` (docs/PLAYING.md) have one event per
  run of unchanged buttons and a `# frames N` comment giving the recorded length.
- A `# sram crc32 XXXXXXXX` comment means the movie starts from power-on with a
  cartridge save in SRAM instead of blank SRAM. The saves are committed in
  `oracle/saves/` (see its README). `ct_dev` and `ct_play` find the save by
  that CRC-32 when `--sram` isn't given (in `oracle/saves` next to the movie's
  directory, or below the ROM's directory; `oracle/src/sram_find.c`);
  `ct_oracle` needs `--sram`. A save with a different CRC-32 is refused.

Buttons are stored in SNES JOY1 (`$4218/$4219`) bit order, so the port can load
the value straight into its joypad register:

| Bit | 15 | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3-0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Button | B | Y | Select | Start | Up | Down | Left | Right | A | X | L | R | 0 |

A libretro joypad id `i` (0..11) maps to bit `15 - i`.

## Trace format (`.cttr`)

Binary, little-endian. The format depends on nothing libretro-specific;
`oracle/src/trace.c` is the reference writer and the port can link it directly.

### Header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `CTTR` |
| 4 | 2 | version (1) |
| 6 | 2 | header size in bytes, including region descriptors and producer string |
| 8 | 4 | CRC32 of the ROM (copier header excluded) |
| 12 | 2 | region count R |
| 14 | 2 | producer string length P |
| 16 | 16·R | region descriptors |
| 16+16·R | P | producer string, UTF-8, not NUL-terminated |

Region descriptor, 16 bytes: `u8 id`, `u8 flags` (0), `u16 reserved`,
`u32 size`, `char name[8]` (NUL-padded).

| id | Name | Size | Contents |
|---|---|---|---|
| 0 | `WRAM` | 0x20000 | `$7E:0000-$7F:FFFF` |
| 1 | `VRAM` | 0x10000 | PPU video RAM |
| 2 | `SRAM` | 0x2000 | cartridge battery RAM (`$30:6000-7FFF` in HiROM) |
| 3 | `CGRAM` | 0x200 | 256 BGR555 colors, little-endian: color *n* is bytes `2n`, `2n+1` |
| 4 | `OAM` | 0x220 | 128 × 4-byte low table, then the 32-byte high table |
| 5 | `WRAMW` | 0x20000 | `ct_dev` only: 1 for each WRAM byte the game has written so far |

A producer only includes the regions it can read. `trace.py diff` compares the
regions both traces have.

### Frame record

| Size | Field |
|---|---|
| 4 | record size (bytes after this field) |
| 4 | frame number |
| 2 | input for this frame (JOY1 order) |
| 1 | flags: bit 0 = keyframe |
| 1 | reserved |
| 2 | framebuffer width |
| 2 | framebuffer height |
| 8 | framebuffer hash (0 if no frame has been presented) |
| … | one region block per region, in header order |

Region block: `u64 hash` of the region's full contents, `u32 run count`, then
the runs. Each run is `u32 offset`, `u32 length`, `length` bytes. A keyframe
stores each region as one run covering all of it. Other frames store only the
bytes that changed since the previous record. Changed spans less than 8 bytes
apart are merged into one run. Reconstructing a frame means applying every
record from the keyframe onward.

Frames appear in increasing order but don't need to be contiguous. Diffing
aligns records by frame number.

### Hashes

64-bit FNV-1a: start at `0xcbf29ce484222325`; for each byte, `h ^= byte; h *= 0x100000001b3`.

- Region hash: over the region's bytes.
- Framebuffer hash: over `u16 width`, `u16 height` (little-endian), then each
  pixel as 4 bytes `B, G, R, 0` with 8 bits per channel. RGB565 and 0RGB1555
  output is widened by bit replication first (`c << 3 | c >> 2` for 5-bit
  values, `c << 2 | c >> 4` for 6-bit ones).

Framebuffers are only comparable within one renderer. By default `diff` reports
framebuffer mismatches but doesn't count them as divergences; pass `--framebuffer` to count them.

## trace.py

```sh
python3 tools/trace.py info run.cttr
python3 tools/trace.py peek run.cttr '$7E:0003' --frames 1600:1700     # value whenever it changes
python3 tools/trace.py peek run.cttr CGRAM:0002 --len 2 --all          # every frame
python3 tools/trace.py peek run.cttr 0x21 --region OAM
python3 tools/trace.py diff a.cttr b.cttr --region WRAM --ignore '$7E:0000-00FF' --max 32
python3 tools/trace.py diff bsnes.cttr dev.cttr --region WRAM --written-only --offset 1
python3 tools/trace.py dump run.cttr --frame 1700 --region CGRAM -o traces/cgram.bin
```

- **Addresses:** `$7E:0100`, `7E:0100` or `7E0100` are SNES bus addresses.
  Banks `$7E/$7F` and low-RAM mirrors map to WRAM; `$20-$3F:6000-7FFF` maps to
  SRAM. `VRAM:1234`-style addresses, or a bare offset with `--region`, are
  region-relative.
- **`diff`:** exits 0 when nothing diverges, 1 on divergence, 2 when nothing
  overlaps. It prints the first divergent frame and up to `--max` differing
  addresses per region. `--keep-going` counts every divergent frame. Frames
  whose region hashes match are skipped without comparing bytes.
- **`--offset N`:** compares frame F of the first trace with frame F+N of the
  second, for runs that settle a frame or more apart.
- **`--written-only`:** compares WRAM only where the game has written, using
  the `WRAMW` region of whichever trace has one. Use it against cores that
  randomize power-on RAM, like bsnes.
- **`dump`:** refuses to write inside the repository unless the path is gitignored.

## CGRAM and OAM

libretro has memory ids for WRAM, VRAM and SRAM only. Neither tested core
describes CGRAM or OAM through `SET_MEMORY_MAPS`. Snes9x and bsnes2014 publish
no memory map at all, and bsnes-mercury's covers only the CPU bus.

The harness reads both from Snes9x savestates (`oracle/src/ppu_state.c`). The
`PPU` block of a version 11-14 snapshot holds `CGDATA[256]` (big-endian words)
at offset 64 and `OAMData[544]` at offset 2003. It also holds `OBJ[128]`,
which Snes9x decodes from OAM writes. Every frame, the harness checks all 128
`OBJ` entries against the raw OAM bytes. If a future Snes9x changes the layout,
the check fails and the run stops instead of recording wrong data. On the
attract sequence, every pixel of the framebuffer uses a color present in the
extracted CGRAM.

Other routes if this ever needs replacing:

1. **A core with memory ids for CGRAM/OAM.** Patch a core locally to return
   them for custom memory ids. It's the simplest option, but means maintaining
   a core build.
2. **Register-write logging.** Record `$2102-$2104` and `$2121-$2122` writes,
   then replay them. That needs bus-access hooks libretro doesn't offer.
3. **Framebuffer-only palette checks.** Weaker, and color math breaks them.

## Known gaps

- **PPU and CPU registers aren't traced.** Screen mode, scroll positions,
  windows, color math, HDMA tables in flight, and the CPU/APU registers are all
  missing. Savestates contain them, so the Snes9x adapter is the natural place
  to add a `PPUREG` region once the port needs one. That would require a
  core-neutral encoding.
- **No sub-frame timing.** Traces are sampled once per `retro_run`, so
  mid-frame writes such as raster effects or HDMA only show through their
  end-of-frame result and the framebuffer.
- **APU state isn't traced.** SPC700 RAM and DSP registers are absent. For
  audio, compare `--wav` output with `ct_dev --wav` using
  `tools/audio_compare.py` (docs/RUNTIME.md, S-DSP).
- **Trace windows.** `--trace-from` still emulates every frame before the
  window; there's no savestate seeking.
- **bsnes2014 limits** are described under [Cores](#cores).

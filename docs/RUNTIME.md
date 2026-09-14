# Development runtime

`ct_dev` runs the original ROM on our own hardware model: the 65C816
interpreter, the SNES bus, PPU registers, and the APU with the SPC700 core. It
writes the same `.cttr` traces as `ct_oracle`, so `tools/trace.py diff` shows
where it departs from Snes9x. It's the scaffold that ported routines will plug
into (see [APPROACH.md](APPROACH.md)), and it never ships.

```sh
cmake -S . -B build/main -G Ninja && cmake --build build/main
build/main/ct_dev --rom roms/ct-usa.sfc --frames 1800 --trace traces/dev.cttr \
    [--movie oracle/movies/title-start.ctm] [--coverage traces/coverage.txt] [--status-every 60]
python3 tools/trace.py diff traces/oracle.cttr traces/dev.cttr --region WRAM
```

`--coverage` writes every executed instruction address with its M/X widths, for
seeding the disassembler.

## Components

| Path | What | Verified by |
|---|---|---|
| `runtime/cpu/cpu65816.c` | 65C816 interpreter | SingleStepTests, 5.12M cases, 0 failures |
| `runtime/apu/spc700.c` | SPC700 interpreter with cycle counts | SingleStepTests, 256K cases, 0 failures |
| `runtime/apu/apu.c` | Sound RAM, `$F0-$FF` registers, timers, boot upload protocol | Oracle traces |
| `runtime/snes/bus.c` | HiROM map, `$4200` registers, joypads, DMA | Oracle traces |
| `runtime/snes/ppu.c` | VRAM/CGRAM/OAM access, decoded scroll/Mode 7/color registers, multiply, counters | Oracle traces |
| `runtime/snes/render.c` | Scanline renderer with Snes9x-compatible output colors | Snes9x framebuffers (see Renderer) |
| `runtime/snes/system.c` | Scanline timing, NMI/IRQ, HDMA, auto-joypad | Oracle traces |

Build the conformance runners with the same CMake project (`sst_runner`,
`spc700_runner`); test data comes from `tests/cpu/fetch_sst.py` and
`tests/apu/fetch_sst_spc700.py`.

## Design notes

- **No boot ROM.** The console's SPC700 IPL ROM isn't included. `apu.c`
  implements its documented upload protocol directly: announce `$BBAA`,
  receive blocks at consecutive addresses, echo indices, then jump to the
  driver. It polls the ports the way the ROM does, because games write ports 0
  and 1 with one 16-bit write. Per-byte latency (`IPL_BYTE_CYCLES`) is
  calibrated so upload blocks land on the same frames as in Snes9x.
- **Frame boundary.** A frame ends at the start of vblank (scanline 225), after
  the NMI flag is set and before the handler runs.
- **Oracle conventions.** WRAM powers on filled with `$55` to match Snes9x.
  Snes9x also fills SRAM with `$60` and preloads CGRAM; those aren't mirrored
  yet, so ignore those regions until the game writes them.

## Renderer

`runtime/snes/render.c` draws each scanline from the PPU register layer's
state when the system's `on_line` hook fires (start of hblank, before that
line's HDMA writes). Lines 1-224 become framebuffer rows 0-223 of a 256x224
XRGB8888 image. `ct_dev` hooks it up, writes its hash into traces, and saves
PNGs with `--shot-every N --shot-dir <dir>`. It's a low-level reference
renderer, kept separate from `ppu.c` so a native renderer can replace it.

Supported:
- BG modes 0-7 with per-mode priorities, including the mode 1 BG3 priority
  bit; 2/4/8bpp tiles, flips, and 32/64-tile maps laid out as 32x32 quadrants
- offset-per-tile in modes 2, 4 and 6
- Mode 7 with flips, wrap, transparent or tile-0 fill, and EXTBG
- sprites with every OBSEL size, the 32-sprite and 34-tile limits with
  range/time-over flags, and priority rotation
- mosaic, windows with all four logic ops, main/sub screen window masks
- color math: add/subtract, half, sub screen or fixed color, clip to black,
  math windows, and direct color

Framebuffer colors follow the Snes9x libretro core so hashes can match
`ct_oracle` (`render_snes9x_color`, `render_rgb565_to_xrgb8888`). Snes9x scales
each palette color by master brightness first (`(c * brightness + 7) / 15`
per 5-bit channel), packs it as RGB565 with green's low bit copied from its top
bit, and does color math on those packed values. Full add saturates each
channel at the brightness-scaled maximum, half add averages the packed words,
and subtract and half subtract floor each channel at zero, with green
subtracted at 6 bits.

Checked against Snes9x by rendering the PPU state from its savestates and
comparing with its framebuffer for the same frame. The attract sequence (Mode 7
pendulum with sub screen math, mode 2 logo with offset-per-tile) matches
pixel for pixel, as does the new-game opening (mode 1, half-add sub screen).
Scenes that change PPU state mid-frame, such as split-screen dialog boxes and
HDMA menus, can't be checked that way. They need the development system to
run the game correctly first.

Gaps:
- Hi-res modes 5 and 6 and pseudo-hires are drawn 256 wide (every other hi-res
  pixel, main screen only); Snes9x outputs 512 wide, so those frames can't hash-match.
- Interlace and overscan (240-line) output aren't modeled.
- Which sprites lose tiles past the 34-tile limit follows OAM order from the
  first sprite; unverified against hardware.
- The ignored 16x16 tile-size bits of `$2105` have no effect, as on hardware.

## Status

With no input, WRAM matches Snes9x exactly through frame 21. The first
divergence is at frame 22, in the first command handshake with the sound
driver. The driver checks the ports once per 62.5 Hz timer tick, and the main
CPU only waits about a third of a frame for an echo before resending. Whether
the echo lands in that window depends on sub-frame timing between the two
CPUs. Ours misses it, so the game starts about 5 frames late.

## Next

1. **Cycle-accurate CPU timing.** Internal cycles are currently a flat 6 master
   clocks per instruction. Count real internal operations per opcode and check
   them against SingleStepTests cycle counts, as the SPC700 core already is.
2. Revisit the driver handshake and IPL latency with exact timing.
3. Mirror or ignore Snes9x's SRAM and CGRAM power-on fill.
4. Import `--coverage` output into `tools/disasm` as width-annotated entries.
5. PPU renderer, to compare framebuffers and see the game.

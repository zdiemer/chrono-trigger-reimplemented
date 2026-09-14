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
- **Work RAM port.** `$2181-$2183` set an address and each read or write of
  `$2180` accesses that WRAM byte and advances the address. Only the low 17
  bits select the byte, so `$00:3000` and `$7E:3000` are the same. Chrono
  Trigger's decompressor writes all of its output through this port, including
  code it then runs from `$7E:3000`.
- **Exact CPU timing.** Each instruction costs its bus accesses at their real
  speeds plus its internal cycles (6 master clocks each), both verified per
  opcode against SingleStepTests.
- **Oracle conventions.** Power-on RAM contents follow Snes9x so unwritten
  bytes compare equal: WRAM `$55`, SRAM `$60` (no save file), and a gradient
  preloaded into CGRAM. SRAM then matches Snes9x throughout the attract
  sequence, and CGRAM matches until the game loads its palette.

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

With no input, the development system runs boot, the sound driver upload, and
the attract sequence. Over the first 1800 frames, every non-blank frame it
renders is pixel-identical to a Snes9x frame.

Frames line up with an offset, not one to one, because of sound-driver
handshake races that emulators resolve differently:

- WRAM matches Snes9x exactly through frame 21.
- After the first driver command, we run one frame behind Snes9x and bsnes.
  Comparing only bytes the game has written, WRAM matches bsnes exactly for
  frames 23-26.
- The next transfer starts at frame 27 in bsnes, 28 here, and 41 in Snes9x. On
  that race Snes9x is the outlier, and the attract sequence ends up about 16
  frames behind ours.

With `oracle/movies/new-game.ctm` (title, battle mode, name entry, the opening
and the first dialog), the input resynchronizes both systems: after START,
frames match Snes9x with no offset. Over frames 1800-5400, 3212 of 3233
non-blank frames are pixel-identical to the same Snes9x frame. In the dialog
section (5400-7200), 1354 of 1550 match, most of the rest within a frame or
three. WRAM there differs only in frame-count and animation-timer bytes left
over from the earlier timing offset.

## Validation strategy

External emulators check that the hardware model is right. They aren't
required to match frame for frame, because sub-frame races with the sound
driver resolve differently even between Snes9x and bsnes. Ported routines are
checked against this development system running the original code, which is
deterministic and must match exactly.

To compare against bsnes, whose power-on RAM is randomized, compare only the
WRAM bytes the game has written: `ct_dev` traces carry a `WRAMW` region, and
`tools/trace.py diff --written-only [--offset N]` uses it.

## Next

1. **Movie coverage.** Extend movies to a battle, the menu and the world map.
2. **Snes9x conventions.** Mirror or ignore its SRAM and CGRAM power-on fill.
3. **Start porting.** Begin replacing routines with native code (roadmap phase 4).

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
| `runtime/snes/ppu.c` | VRAM/CGRAM/OAM access, Mode 7 multiply, counters; no rendering | Oracle traces |
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

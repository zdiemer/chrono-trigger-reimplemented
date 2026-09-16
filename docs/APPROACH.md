# Approach

## Target

*Chrono Trigger* (USA), No-Intro image, verified by `tools/rom.py`:

| | |
|---|---|
| CRC32 / SHA-1 | `2D206BF7` / `de5822f4f2f7a55acb8926d4c0eaa63d5d989312` |
| Mapping | HiROM + FastROM (`$31`), 32 Mbit (4 MiB) |
| Cartridge | ROM + RAM + battery, 8 KiB SRAM |
| Coprocessors | None |

Every address, symbol and trace in this repo refers to exactly these bytes.

## Decision: native reimplementation, not SNESRecomp

[SNESRecomp](https://github.com/RetroPortingToolKit/snesrecomp) supports plain
HiROM and could plausibly get Chrono Trigger booting. We are not building on it,
because of what the result would be rather than whether it would run:

- **Only the CPU is native.** The emitter transliterates 65816 instructions into
  C that still manipulates emulated A/X/Y, flags and M/X width state. PPU, APU,
  DSP and DMA come from a LakeSnes-derived hardware core, and unresolved code
  falls back to an interpreter tier at runtime.
- **The output isn't source.** Generated C is a mechanical image of the ROM. It
  can't be read, refactored, or extended the way a port needs to be, and it has
  to be regenerated from the ROM.
- **Timing is approximated.** The compiled path runs a frame of CPU and then a
  render/IRQ pass, so interrupt effects land at different points than on
  hardware. The project documents this as an open, medium-large problem
  (`FRAME_MODEL_TIMING.md`).
- **License and coupling.** PolyForm Noncommercial, alpha framework with
  shifting APIs.

It's still a useful reference, especially its tripwire and co-simulation ideas.

## What "native" means for a SNES game

The model is the snesrev ports (`zelda3`, `smw`): hand-written C that
reimplements the game's routines and is validated against the original running
in an emulator. Two parts of the SNES stay low-level at first, on purpose:

- **Graphics.** The game's data *is* tiles, tilemaps, palettes and OAM entries.
  The first renderer draws from the VRAM/CGRAM/OAM state our code writes, which
  gives exact output and makes it diffable. It is a renderer, not a CPU
  emulator. Once the game is correct it can be replaced with a high-level one
  (widescreen, higher-resolution Mode 7, etc.).
- **Audio.** The music is Square's sound driver, a separate program running on
  the SPC700, plus sample data. It talks to the main CPU through four I/O
  ports. We run an SPC700 + S-DSP core behind that boundary first. Rewriting the
  sound driver natively is optional and comes later.

Everything on the main CPU is reimplemented: reset/init, NMI/IRQ handlers,
decompression, field engine and event-script interpreter, menus, battle,
world map, and special-mode scenes.

## Differential testing against the ROM

Chrono Trigger is too big to reimplement blind. Correctness comes from diffing
the port against the original, frame by frame:

1. **Oracle.** A reference emulator (a bsnes or Snes9x libretro core, supplied
   locally and loaded at runtime, never vendored) runs the ROM from an input
   movie and dumps WRAM, VRAM, CGRAM, OAM and PPU register state each frame.
2. **Shared memory layout.** Early on, the port keeps the original `$7E/$7F`
   WRAM layout so its state compares byte for byte with the oracle.
3. **Divergence reports.** The port replays the same movie. The first
   differing frame and address point to the routine that's wrong.
4. **Movie library.** Coverage grows from boot → title → new game → first map
   out to full-playthrough movies. These become the regression suite.

Once the port matches, the WRAM layout can be dropped subsystem by subsystem.

## Decision: incremental porting scaffold

A game can't run until every routine on its path exists. There are two ways to
handle that:

- **A. Interpreter scaffold (recommended).** A dev-only 65816 interpreter runs
  routines that haven't been ported yet. Ported routines are called natively.
  That puts something on screen early and lets routines be ported in any order,
  each validated in place. Release builds exclude the interpreter, and the
  milestone metric is *zero interpreter entries* across the movie library.
- **B. Pure top-down.** Port the boot path by hand until it reaches a frame.
  There's nothing to show until then, and bugs pile up before the first diff.

**Chosen: A.** It's scaffolding with a burndown chart, not the architecture,
and it never ships. It also doubles as the dynamic code-coverage source for
the disassembler.

## Roadmap

0. **Foundation.** Repo, ROM verification, docs. *(done)*
1. **Disassembly and knowledge base.** 65816 disassembler with M/X width
   tracking, a code/data map, and a symbol and RAM-map database. Pull in
   community documentation of event commands and data tables as reference.
   *(in progress: [DISASSEMBLY.md](DISASSEMBLY.md); a byte-identical,
   reassemblable disassembly as the proof that the recompiled code set is
   complete is scoped in [MATCHING.md](MATCHING.md))*
2. **Oracle harness.** libretro frontend, input-movie format, per-frame state
   dumps, divergence tool. *(done: [ORACLE.md](ORACLE.md); Snes9x is the
   trace oracle)*
3. **Host runtime.** SDL window, input, audio, PPU-level renderer, SPC700/DSP
   core, SRAM, and an explicit frame loop with defined NMI/IRQ points.
   *(development system done: [RUNTIME.md](RUNTIME.md); it plays the attract
   sequence and new-game opening with frames matching Snes9x. Native SDL host
   still to do)*
4. **Port.** Boot → title → name entry → Millennial Fair, then field, menu,
   battle, world map and special scenes, each diffed to zero.
   *(started: [PORTING.md](PORTING.md); routines are validated call by call in
   shadow mode)*
5. **Full-game equivalence.** Zero divergences and zero interpreter entries
   across the movie library.
6. **Beyond the SNES.** Native renderer, widescreen, quality-of-life options.
   *(in progress: [PLATFORM.md](PLATFORM.md); game code moves to platform
   services with the SNES hardware model as the reference backend. Audio and
   video are behind the services and the native renderer draws every frame of
   the movie library pixel-identically; widescreen and a GBA feasibility study
   follow)*

Loose ends from finished milestones are collected in [TODO.md](TODO.md).

## Conventions

- Game code in C17, host in C17 or C++20, built with CMake.
- Tools in Python 3 (stdlib first).
- Nothing copyrighted or ROM-derived gets committed: no ROM bytes, extracted
  assets, SPC dumps, or traces. Assets load from the user's ROM at runtime.
- Routines are named and addressed: `// $C0:1234` above every ported function.

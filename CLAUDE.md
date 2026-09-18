# chrono-trigger-recomp

Native C reimplementation of Chrono Trigger (SNES, USA), validated by
per-frame state diffs against the original ROM. Strategy: docs/APPROACH.md.

## Facts

- ROM: No-Intro *Chrono Trigger (USA)*, CRC32 `2D206BF7`. HiROM+FastROM, 4 MiB,
  8 KiB SRAM, no coprocessor. `tools/rom.py stage <path>` verifies it and
  copies it to `roms/ct-usa.sfc` (gitignored).
- Addresses are written in SNES bus form, `$C0:1234`. HiROM file offset =
  `(bank & 0x3F) << 16 | addr` for banks `$C0-$FF` (and `$40-$7D`).

## Commands

- `python3 -m tools.disasm`: static code discovery. Writes `disasm/out/`
  (gitignored). Knowledge goes in `re/hints.toml`. See docs/DISASSEMBLY.md.
- `python3 -m tools.disasm.linear BB:AAAA -n N --m 8 --x 16`: quick look at code.
- `python3 -m unittest discover tests`
- Oracle: `cmake -S oracle -B build/oracle -G Ninja && cmake --build build/oracle`, cores via
  `python3 tools/fetch_core.py`, then `build/oracle/ct_oracle --core cores/snes9x_libretro.<ext>
  --rom roms/ct-usa.sfc --frames N --movie <.ctm> --trace traces/<name>.cttr`. Inspect and diff with
  `python3 tools/trace.py`. Snes9x is the trace oracle. See docs/ORACLE.md.
- Main build: `cmake -S . -B build/main -G Ninja -DCT_ROM=roms/ct-usa.sfc
  "-DCT_RECOMP_COVERAGE=traces/coverage-*.txt;traces/saves/cov-*.txt"
  "-DCT_RECOMP_CALLS=traces/calls-*.txt;traces/saves/calls-*.txt" && cmake --build build/main` builds
  `ct_dev` (ROM on our runtime, writes `.cttr` traces; see docs/RUNTIME.md), `ct_play` (docs/PLAYING.md),
  `ct_fuzz`, the conformance runners, the C generated from the ROM (docs/RECOMP.md) and the matching
  reassembly check (docs/MATCHING.md). Coverage and call files are optional, gitignored inputs.
- CPU tests: `python3 tests/cpu/fetch_sst.py` and `python3 tests/apu/fetch_sst_spc700.py` once, then
  `build/main/sst_runner tests/cpu/data/*.bin`, `build/main/sst_recomp_runner tests/cpu/data/*.bin`,
  `build/main/recomp_bus_diff` and `build/main/spc700_runner tests/apu/data/*.bin` (all must report 0
  failures). `build/main/dsp_test`, `fiber_test`, `state_test` and `bindings_test` need no data.
- CI (`.github/workflows/ci.yml`): Linux gcc and clang, macOS Apple clang, Windows MSYS2 MinGW gcc, all
  configured without a ROM and with `-DCT_WERROR=ON`; runs the unit tests, `dsp_test`, `fiber_test`, `state_test`, `bindings_test`,
  the conformance runners (test data cached) and builds `ct_play` and the oracle. Code must build
  warning-free on all four and tests must pass without a ROM. Docs-only changes don't trigger it.
- Movies: `oracle/movies/*.ctm` from power-on, and `oracle/movies/saves/*.ctm` from cartridge saves
  (`--sram`; each header names its save and frame count; docs/RUNTIME.md). Record new ones with
  `ct_play --record` (docs/PLAYING.md), or script them from game state: `build/main/ct_drive` plus
  `python3 -m tools.movies.routes.<route> --sram <save.srm> --prefix <scratch dir> --movie <out.ctm>`
  (docs/MOVIES.md). Routes survive timing changes; the movies they write don't, so re-run the route.
- `python3 tools/burndown.py`: how much of the ROM's code the hand ports cover, per bank or
  `--by subsystem`, with `--uncovered BANK` listing the largest unported runs. An estimate, for
  sequencing the port work (docs/PLAN.md); its docstring says which way it is wrong.
- Validation: `python3 tools/suite.py --ct-dev build/main/ct_dev` runs the gate and exits 0 only when
  it passes (`--only core|saves`, `--movies <name>...` and `-j N` narrow it while iterating). The gate
  is: every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines;
  `--native on --recomp only` produces traces identical to `--native off` (`python3 tools/trace.py
  diff`); `--native on --recomp only --misses out.txt` lists anything still interpreted;
  `ct_dev --px-check` must report every frame identical (the native renderer against `render.c`,
  docs/PLATFORM.md P3).

- Porting: add routines under `game/<subsystem>/` (named by what they do, not by ROM bank), register in
  `game/registry.c`, validate with `ct_dev ... --native shadow` over the movie library (0 mismatches),
  then `--native on`, and fuzz with `build/main/ct_fuzz --load-state <state> --routine <name>`. Hardware
  access in converted files goes through `runtime/platform/` (docs/PLATFORM.md; a lint test enforces
  it). See docs/PORTING.md.

## Rules

- **Writing a port from generated code** (docs/PLAN.md, docs/RECOMP.md's M5). The draft
  `tools.recomp` prints is a mechanical image of the ROM; the port is source, and the later
  goals (a native renderer, widescreen, a GBA or browser build, assets as data) are made
  easy or hard here rather than in a backend:
  - Write what the routine *does*, not what the draft says. Don't carry over the draft's
    shape: emulated register variables, dead flag updates, block boundaries, or a name from
    its address. Keep the `// $BB:AAAA` comment and nothing else mechanical.
  - Reach the host only through `runtime/platform/` (a lint test enforces it). No PPU, DMA,
    HDMA or APU register number appears in `game/`. Read and write the machine through
    `machine.h`'s `m_read8`/`m_write8` and friends, never by indexing a mapped image, so a
    build that loads its data from somewhere else can redirect it.
  - Keep SNES *semantics* -- tiles, palettes, priority, window logic -- and drop SNES
    *mechanics*: which register latches what, channel programming, write order, the timing
    of a write. The services exist to express the first and hide the second.
  - Don't bake in what a later target changes: the 256x224 screen, the 0-255 coordinate
    range, a tile's size or bit depth, or a fixed bank layout. Take them from the service or
    give them a named constant. Screen coordinates are signed, so positions off the original
    screen survive widescreen.
  - Where the original waits on beam position or depends on exact timing, don't reproduce
    the wait silently: port what it computes, say so in a comment at its `// $BB:AAAA`, and
    add it to docs/TODO.md. Those routines are what block the platform milestones.
  - A port that can't be expressed through a service yet is a finding, not a defeat: add the
    call the subsystem needs (additive), or use the documented SNES escape and write down
    why, as docs/PLATFORM.md's "as built" sections do.
  - Ports don't carry cycle costs: no per-path timing numbers in `game/`. A port's time is
    its `cost_milli` estimate, and the timing gate measures and bounds the estimates' error
    (docs/PLAN.md, Phase 2). Fuzzing is part of a port's definition of done.
- **Writing host-side code** (docs/PLAYING.md, "How the player is built"). `game/` is kept
  portable by the platform services; what a host that isn't a desktop runs into instead is
  the frontend, so hold the line there too. A host that calls back once a frame, with no
  filesystem and no stack of its own, is the case these are for:
  - The loop belongs to the driver (`frontend/window.c`, `frontend/headless.c`).
    Below it nothing sleeps, spins on a clock, polls an event queue or blocks on a device:
    the game advances one frame per call, and input arrives through `in_host_set`.
  - Persistence moves bytes under a name, not a path. A new runtime or player function takes
    a buffer; opening a file, choosing a directory and knowing what a save is called are the
    driver's, which is also what makes a save survive a host with no `fopen`.
  - Stack switching goes through `runtime/port/fiber.h`, which is where a host that can't
    switch stacks natively gets its one implementation. No `ucontext`, thread or `setjmp`
    stack trick anywhere else.
  - Keys, pads and window geometry are bindings in a driver, not player state. A second
    frontend should be able to differ in all three without touching the player.
- Keep the repo machine-independent: no local paths, drive letters, or
  toolchain install locations in code, docs, or build files.
- Never commit ROM bytes or anything derived from them: extracted assets, SPC
  dumps, traces, disassembly listings with data bytes, save states.
  Cartridge saves (`.srm` battery saves: game progress, not assets) are fine and
  live in `oracle/saves/`; emulator save states are not.
- Reference emulator cores are supplied locally under `cores/` and never
  vendored.
- Every ported routine carries its original address in a `// $BB:AAAA` comment.
- Keep the docs current as work lands, in the same commit as the work: the
  milestone docs (docs/APPROACH.md, RECOMP.md, MATCHING.md, PLATFORM.md,
  RUNTIME.md, PORTING.md) say what was built and how it was measured,
  docs/TODO.md holds the open follow-ups, and docs/PLAN.md says what to do next
  and why the rest waits.
- **Delete finished work from the plan and the follow-ups rather than marking it
  done.** A completed item leaves docs/PLAN.md and docs/TODO.md entirely; what
  it produced belongs in its milestone doc's "as built" section, which is where
  the history lives. No "(done)" annotations, no struck-through lines, no
  completed-work sections.

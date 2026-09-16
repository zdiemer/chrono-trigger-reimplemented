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
  failures). `build/main/dsp_test` and `build/main/fiber_test` need no data.
- CI (`.github/workflows/ci.yml`): Linux gcc and clang, macOS Apple clang, Windows MSYS2 MinGW gcc, all
  configured without a ROM and with `-DCT_WERROR=ON`; runs the unit tests, `dsp_test`, `fiber_test`,
  the conformance runners (test data cached) and builds `ct_play` and the oracle. Code must build
  warning-free on all four and tests must pass without a ROM. Docs-only changes don't trigger it.
- Movies: `oracle/movies/*.ctm` from power-on, and `oracle/movies/saves/*.ctm` from cartridge saves
  (`--sram`; each header names its save and frame count; docs/RUNTIME.md). Record new ones with
  `ct_play --record` (docs/PLAYING.md), or script them from game state: `build/main/ct_drive` plus
  `python3 -m tools.movies.routes.<route> --sram <save.srm> --prefix <scratch dir> --movie <out.ctm>`
  (docs/MOVIES.md). Routes survive timing changes; the movies they write don't, so re-run the route.
- Validation: every movie in `--native shadow` must exit 0 with no `mismatched [1-9]` lines;
  `--native on --recomp only` must produce traces identical to `--native off` (`python3 tools/trace.py
  diff`); `--native on --recomp only --misses out.txt` lists anything still interpreted;
  `ct_dev --px-check` must report every frame identical (the native renderer against `render.c`,
  docs/PLATFORM.md P3).

- Porting: add routines under `game/<subsystem>/` (named by what they do, not by ROM bank), register in
  `game/registry.c`, validate with `ct_dev ... --native shadow` over the movie library (0 mismatches),
  then `--native on`, and fuzz with `build/main/ct_fuzz --load-state <state> --routine <name>`. Hardware
  access in converted files goes through `runtime/platform/` (docs/PLATFORM.md; a lint test enforces
  it). See docs/PORTING.md.

## Rules

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

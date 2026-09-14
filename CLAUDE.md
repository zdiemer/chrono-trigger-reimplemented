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
- Main build: `cmake -S . -B build/main -G Ninja && cmake --build build/main` builds `ct_dev` (ROM on
  our runtime, writes `.cttr` traces; see docs/RUNTIME.md) and the conformance runners.
- CPU tests: `python3 tests/cpu/fetch_sst.py` and `python3 tests/apu/fetch_sst_spc700.py` once, then
  `build/main/sst_runner tests/cpu/data/*.bin` and `build/main/spc700_runner tests/apu/data/*.bin`
  (both must report 0 failures).

- Porting: add routines under `game/`, register in `game/natives.c`, validate with
  `build/main/ct_dev ... --movie oracle/movies/new-game.ctm --native shadow` (must report 0
  mismatches), then `--native on`. See docs/PORTING.md.

## Rules

- Keep the repo machine-independent: no local paths, drive letters, or
  toolchain install locations in code, docs, or build files.
- Never commit ROM bytes or anything derived from them: extracted assets, SPC
  dumps, traces, disassembly listings with data bytes, save states.
- Reference emulator cores are supplied locally under `cores/` and never
  vendored.
- Every ported routine carries its original address in a `// $BB:AAAA` comment.

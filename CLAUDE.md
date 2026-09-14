# chrono-trigger-recomp

Native C reimplementation of Chrono Trigger (SNES, USA), validated by
per-frame state diffs against the original ROM. Strategy: docs/APPROACH.md.

## Facts

- ROM: No-Intro *Chrono Trigger (USA)*, CRC32 `2D206BF7`. HiROM+FastROM, 4 MiB,
  8 KiB SRAM, no coprocessor. `tools/rom.py stage <path>` verifies it and
  copies it to `roms/ct-usa.sfc` (gitignored).
- Addresses are written in SNES bus form, `$C0:1234`. HiROM file offset =
  `(bank & 0x3F) << 16 | addr` for banks `$C0-$FF` (and `$40-$7D`).

## Rules

- Keep the repo machine-independent: no local paths, drive letters, or
  toolchain install locations in code, docs, or build files.
- Never commit ROM bytes or anything derived from them: extracted assets, SPC
  dumps, traces, disassembly listings with data bytes, save states.
- Reference emulator cores are supplied locally under `cores/` and never
  vendored.
- Every ported routine carries its original address in a `// $BB:AAAA` comment.

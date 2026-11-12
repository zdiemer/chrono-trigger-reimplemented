# chrono-trigger-reimplemented

A native C reimplementation of Chrono Trigger (SNES, USA). The product is
`ct_native`, with native rendering, sound and platform services. See
[Playing](docs/PLAYING.md) to build and run it, [Plan](docs/PLAN.md) for remaining
work, and [Validation](docs/REIMPLEMENTATION.md) for the lean native gate.

You need your own USA cartridge, CRC32 `2D206BF7`. No game data is distributed.
`python3 tools/rom.py stage <path>` verifies and copies it to `roms/ct-usa.sfc`.
The native program and its unit tests build without a ROM.

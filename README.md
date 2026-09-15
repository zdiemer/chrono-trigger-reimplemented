# chrono-trigger-recomp

A native PC port of *Chrono Trigger* (SNES, USA), reimplemented in C and
validated frame by frame against the original ROM.

This is not an emulator and not an automated recompilation. See
[docs/APPROACH.md](docs/APPROACH.md) for the strategy and roadmap.

## Status

Foundation stage: ROM verification and project plan.

## ROM

You need your own copy of *Chrono Trigger (USA)*
(CRC32 `2D206BF7`, SHA-1 `de5822f4f2f7a55acb8926d4c0eaa63d5d989312`).
This repository contains no game data.

```sh
python3 tools/rom.py verify path/to/ct.sfc
python3 tools/rom.py stage  path/to/ct.zip   # copies to roms/ct-usa.sfc
```

`.sfc`, `.smc` (copier header is stripped) and `.zip` are accepted.

To play in a window with the keyboard, see [docs/PLAYING.md](docs/PLAYING.md).

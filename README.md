# chrono-trigger-recomp

A native PC port of *Chrono Trigger* (SNES, USA), reimplemented in C and
validated frame by frame against the original ROM.

The goal is readable, hand-written C, not an emulator. Until every routine is
ported, the original code runs on a development hardware model and a 65816
interpreter, with each ported routine checked against it. A static recompiler
([docs/RECOMP.md](docs/RECOMP.md)) is being built as scaffolding: it turns the
remaining original code into mechanical C at build time from your ROM, as a
starting point for hand ports. See [docs/APPROACH.md](docs/APPROACH.md) for the
strategy and roadmap.

## Status

Porting: the attract sequence, field, menu, event and battle systems are
partly native, validated in shadow mode over 21 input movies
([docs/PORTING.md](docs/PORTING.md)). The development runtime plays the game
with sound ([docs/PLAYING.md](docs/PLAYING.md)). The recompiler translates every
65816 instruction ([docs/RECOMP.md](docs/RECOMP.md)); generating whole banks is
next. [docs/PLATFORM.md](docs/PLATFORM.md) designs the platform services
(video, audio, timing, input, saves) that let the game run on backends other
than the SNES hardware model: a native renderer, widescreen, and possibly GBA.

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

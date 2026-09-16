"""Check that `ct_drive` is the machine `ct_dev --native off` is.

Replays a movie in both and compares all of work RAM. Needs the ROM, the save
the movie starts from, and both binaries, so it isn't part of the unit tests;
redo it after changing either program (docs/MOVIES.md).

    python3 -m tools.movies.check_drive --rom roms/ct-usa.sfc --sram save.srm \
        --movie oracle/movies/saves/endings-beyond-time.ctm --frames 20000 \
        --ct-dev build/main/ct_dev --drive build/main/ct_drive
"""
import argparse
import subprocess
import tempfile
import os

from .ctdrv import Machine

WORK_RAM = [(0x7E0000, 0x10000), (0x7F0000, 0x10000)]


def read_work_ram(machine):
    return b"".join(machine.peek(addr, length) for addr, length in WORK_RAM)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rom", required=True)
    parser.add_argument("--sram")
    parser.add_argument("--movie", required=True)
    parser.add_argument("--frames", type=int, default=20000)
    parser.add_argument("--ct-dev", required=True)
    parser.add_argument("--drive")
    args = parser.parse_args(argv)

    with tempfile.TemporaryDirectory() as scratch:
        state = os.path.join(scratch, "check.state")
        command = [args.ct_dev, "--rom", args.rom, "--movie", args.movie, "--frames", str(args.frames),
                   "--native", "off", "--save-state", state]
        if args.sram:
            command += ["--sram", args.sram]
        subprocess.run(command, check=True)
        with Machine(rom=args.rom, sram=args.sram, drive=args.drive) as machine:
            machine.play_movie(args.movie, args.frames)
            driven = read_work_ram(machine)
        with Machine(rom=args.rom, sram=args.sram, drive=args.drive) as machine:
            machine.load_state(state)
            replayed = read_work_ram(machine)

    differences = sum(1 for left, right in zip(driven, replayed) if left != right)
    print("%s: %d frames, %d work RAM bytes differ" % (args.movie, args.frames, differences))
    return 0 if differences == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

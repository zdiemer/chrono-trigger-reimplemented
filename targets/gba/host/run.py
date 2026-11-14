"""Play a movie on the GBA build in mGBA, headless, and check that it ends as
ct_native's does: the same CRC-32 of work RAM and save RAM at the end, as
`ct_native --end-state` writes them (docs/PLATFORM.md, "The GBA build, as
built").

    python3 targets/gba/host/run.py --mgba MGBA_PREFIX --program build/gba/ct_gba.bin \\
        --ct-native build/main/ct_native --movie movies/title-start.ctm

MGBA_PREFIX holds mGBA's library and headers (include/mgba, bin/libmgba*.dll
or lib/libmgba.so): MSYS2's mingw-w64-x86_64-mgba package, installed or only
unpacked, or a build of mGBA. The runner (targets/gba/host/mgba_run.c) is built
with the host's C compiler into the program's directory. The GBA build is
packed first (targets/gba/host/pack.py) from the staged cartridge. With
`--profile FILE`, the runner samples where the processor is as the movie
plays and FILE gets each function's share of the samples, the largest first.
"""
import argparse
import bisect
import glob
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import pack  # noqa: E402

END = re.compile(r"^ct_gba: end state crc32 ([0-9A-F]{8}), save RAM crc32 ([0-9A-F]{8})", re.M)


def runner(prefix, out_dir):
    """The mGBA runner, built against the library under `prefix`."""
    exe = out_dir / ("mgba_run.exe" if os.name == "nt" else "mgba_run")
    source = HERE / "mgba_run.c"
    if exe.exists() and exe.stat().st_mtime >= source.stat().st_mtime:
        return exe
    libraries = glob.glob(str(prefix / "bin" / "libmgba*.dll")) + glob.glob(str(prefix / "lib" / "libmgba.so*"))
    if not libraries:
        raise SystemExit("run: no libmgba under %s" % prefix)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-O2", "-I", str(prefix / "include"), str(source), libraries[0], "-lz", "-o", str(exe)],
                   check=True)
    return exe


def frame_symbol(elf):
    """The address of the game frame the program is in (main.c's ct_gba_frame)."""
    nm = shutil.which("arm-none-eabi-nm")
    out = subprocess.run([nm, str(elf)], stdout=subprocess.PIPE, text=True, check=True).stdout
    return next(line.split()[0] for line in out.splitlines() if line.endswith(" ct_gba_frame"))


def profile(elf, samples, out):
    """The runner's samples by function, from the program's symbols: each
    function's share and count, the largest first; samples outside any
    function by their 4 KiB page, and outside the program's memory (the
    BIOS's interrupt dispatch) as <other>."""
    nm = shutil.which("arm-none-eabi-nm")
    listing = subprocess.run([nm, "-n", "-S", "--defined-only", str(elf)], stdout=subprocess.PIPE, text=True,
                             check=True).stdout
    functions = []
    for line in listing.splitlines():
        field = line.split()
        if len(field) == 4 and field[2] in "tTwW":
            functions.append((int(field[0], 16) & ~1, int(field[1], 16), field[3]))
    functions.sort()
    starts = [f[0] for f in functions]
    counts, total = {}, 0
    for line in samples.read_text().splitlines():
        at, count = line.split()
        count = int(count)
        total += count
        if at == "other":
            name = "<other>"
        else:
            pc = int(at, 16) - 4   # the program counter reads two Thumb instructions ahead
            i = bisect.bisect_right(starts, pc) - 1
            inside = i >= 0 and pc < functions[i][0] + max(functions[i][1], 2)
            name = functions[i][2] if inside else "?%08x" % (pc & ~0xFFF)
        counts[name] = counts.get(name, 0) + count
    with out.open("w", encoding="utf-8") as f:
        for name, count in sorted(counts.items(), key=lambda item: -item[1]):
            f.write("%6.2f%% %9d %s\n" % (100.0 * count / max(total, 1), count, name))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mgba", type=pathlib.Path, required=True, help="mGBA's install prefix")
    ap.add_argument("--program", type=pathlib.Path, required=True)
    ap.add_argument("--ct-native", type=pathlib.Path, required=True)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--movie", type=pathlib.Path, required=True)
    ap.add_argument("--max-gba-frames", type=int, default=3000000)
    ap.add_argument("--shot", action="append", default=[], metavar="FRAME=FILE.png",
                    help="write the GBA's picture once the game reaches FRAME")
    ap.add_argument("--profile", type=pathlib.Path, metavar="FILE",
                    help="sample where the processor is as the movie plays; FILE gets each function's share")
    args = ap.parse_args(argv)
    out_dir = args.program.resolve().parent
    exe = runner(args.mgba.resolve(), out_dir)
    image = out_dir / (args.movie.stem + ".gba")
    if pack.main(["--program", str(args.program), "--rom", str(args.rom), "--movie", str(args.movie),
                  "--out", str(image)]):
        return 2
    env = dict(os.environ)
    env["PATH"] = str(args.mgba.resolve() / "bin") + os.pathsep + env.get("PATH", "")
    command = [str(exe), str(image), str(args.max_gba_frames)]
    if args.shot:
        command += ["--frame-at", frame_symbol(args.program.resolve().with_suffix(".elf"))]
        for shot in args.shot:
            frame, _, path = shot.partition("=")
            command += ["--shot", frame, path]
    samples = out_dir / (args.movie.stem + ".samples")
    if args.profile:
        command += ["--samples", str(samples)]
    gba = subprocess.run(command, stdout=subprocess.PIPE, text=True, env=env)
    if args.profile and samples.exists():
        profile(args.program.resolve().with_suffix(".elf"), samples, args.profile)
    print(gba.stdout, end="")
    found = END.search(gba.stdout)
    if gba.returncode or not found:
        print("run: the GBA build didn't reach its end")
        return 1
    frames = re.search(r"^# frames (\d+)", args.movie.read_text(encoding="utf-8"), re.M).group(1)
    with tempfile.TemporaryDirectory(prefix="gba-") as scratch:
        end = pathlib.Path(scratch) / "end.bin"
        command = [str(args.ct_native), "--rom", str(args.rom), "--headless", "--mute", "--movie", str(args.movie),
                   "--frames", frames, "--end-state", str(end)]
        sram = pack.SRAM_CRC.search(args.movie.read_text(encoding="utf-8"))
        if sram:
            crc = int(sram.group(1), 16)
            command += ["--sram", next(str(p) for p in sorted((ROOT / "saves").rglob("*.srm"))
                                       if zlib.crc32(p.read_bytes()) == crc)]
        native = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if native.returncode or not end.exists():
            print(native.stderr)
            print("run: ct_native didn't play the movie")
            return 2
        state = end.read_bytes()
    whole, sram = "%08X" % zlib.crc32(state), "%08X" % zlib.crc32(state[0x20000:])
    same = (found.group(1), found.group(2)) == (whole, sram)
    print("run: ct_native ends with end state crc32 %s, save RAM crc32 %s: %s" % (
        whole, sram, "the same" if same else "DIFFERENT"))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())

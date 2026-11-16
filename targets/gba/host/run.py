"""Play a movie on the GBA build in mGBA, headless, driven from outside, and
check that it ends as ct_native's does: the same CRC-32 of work RAM and save
RAM at the end, as `ct_native --end-state` writes them (docs/PLATFORM.md,
"The GBA build, as built").

    python3 targets/gba/host/run.py --mgba MGBA_PREFIX --program build/gba/ct_gba.bin \\
        --ct-native build/main/ct_native --movie movies/title-start.ctm

MGBA_PREFIX holds mGBA's library and headers (include/mgba, bin/libmgba*.dll
or lib/libmgba.so): MSYS2's mingw-w64-x86_64-mgba package, installed or only
unpacked, or a build of mGBA. The runner (targets/gba/host/mgba_run.c) is built
with the host's C compiler into the program's directory. The GBA build is
packed first (targets/gba/host/pack.py) from the staged cartridge as ct.gba,
the one ROM a player plays too, with the event and object scripts read a
command at a time as ct_native works them out (`--script-records`;
`--no-script-records` packs without them, and the game reads every command
from its bytes).
ct_native plays the movie first and writes
what its time model decides of the game's frames (`--decisions`), which the
GBA build, keeping no time of the original's, replays
(targets/gba/system/frames.h); the runner feeds it the movie's pads and the
decisions, and the save RAM the movie starts from goes in as its .sav. With
`--profile FILE`, the runner samples where the processor is as the movie
plays and FILE gets each function's share of the samples, the largest first.
With `--wav FILE.wav`, FILE gets what mGBA plays; with `--stream FILE.wav`,
every sample the program queued, without the gaps where its frames were
late. With `--fast-rom`, the ROM is packed with the cartridge's faster waits
(pack.py); with `--colours PROFILE`, with that profile's colours (pack.py's
--profile). With `--bios FILE`, the player's own dump of the GBA's BIOS, the
ROM gets the logo the BIOS checks (pack.py) and the runner starts the GBA in
that BIOS, which checks it, rather than in mGBA's own, which starts the program
at once. Nothing is played aloud.

With `--own-time`, the GBA plays the movie as it plays by hand instead, its
own time deciding the game's frames (targets/gba/system/frames.h), so its
state isn't ct_native's; the check is that it goes through the same places
in the same order (`ct_native --locations`), the location read every few
frames, with at most the last differing where the two stand at the end. A
scene that waits on its scripts' timing and goes astray in the GBA's time
(the attract demo's Magus's castle, issue #553) fails it.
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
import zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import pack  # noqa: E402

SRAM_CRC = re.compile(r"^# sram crc32 ([0-9A-Fa-f]{8})", re.M)
SRAM_MARK = b"CTSV"     # targets/gba/services/cartridge_save.c's: the SRAM holds a save
GBA_SRAM_BYTES = 0x8000
END = re.compile(r"^ct_gba: end state crc32 ([0-9A-F]{8}), save RAM crc32 ([0-9A-F]{8})", re.M)
LOCATION = re.compile(r"^location: frame \d+ \$([0-9A-F]{3})", re.M)
LOCATION_ID = 0x7E0100  # location_id (re/ram.toml), its low nine bits the place
LOCATION_BITS = 0x1FF
LOCATION_EVERY = 8      # frames between the GBA's reads, under the shortest stay the attract demo makes (19)


def runner(prefix, out_dir):
    """The mGBA runner, built against the library under `prefix`."""
    exe = out_dir / ("mgba_run.exe" if os.name == "nt" else "mgba_run")
    sources = [HERE / "mgba_run.c", ROOT / "frontend/movie.c"]
    depends = sources + [HERE.parent / "system/drive_box.h"]
    if exe.exists() and all(exe.stat().st_mtime >= p.stat().st_mtime for p in depends):
        return exe
    libraries = glob.glob(str(prefix / "bin" / "libmgba*.dll")) + glob.glob(str(prefix / "lib" / "libmgba.so*"))
    if not libraries:
        raise SystemExit("run: no libmgba under %s" % prefix)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-O2", "-I", str(prefix / "include"), "-I", str(ROOT / "frontend"), "-I", str(HERE.parent)]
                   + [str(p) for p in sources] + [libraries[0], "-lz", "-o", str(exe)], check=True)
    return exe


def starting_save(movie_text):
    """The save RAM the movie starts from, the one in saves/ its CRC names, or None."""
    wanted = SRAM_CRC.search(movie_text)
    if not wanted:
        return None
    crc = int(wanted.group(1), 16)
    found = next((p for p in sorted((ROOT / "saves").rglob("*.srm")) if zlib.crc32(p.read_bytes()) == crc), None)
    if found is None:
        raise SystemExit("run: no save in saves/ has the CRC-32 %08X the movie starts from" % crc)
    return found


def gba_save(srm):
    """A cartridge save as the GBA's SRAM holds it: the save, its mark, and the rest erased."""
    data = srm.read_bytes()
    if len(data) != 0x2000:
        raise SystemExit("run: a cartridge save is 8 KiB")
    return (data + SRAM_MARK).ljust(GBA_SRAM_BYTES, b"\xff")


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


def places(seen):
    """Locations as entered, a stay counted once however often it was read."""
    order = []
    for location in seen:
        if not order or order[-1] != location:
            order.append(location)
    return order


def own_time(args, exe, image, env, frames, srm, out_dir):
    """The movie played in the GBA's own time: the same places as ct_native's, in order."""
    command = [str(args.ct_native), "--rom", str(args.rom), "--headless", "--mute", "--movie", str(args.movie),
               "--frames", frames, "--locations"]
    if srm:
        command += ["--sram", str(srm)]
    native = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if native.returncode:
        print(native.stderr)
        print("run: ct_native didn't play the movie")
        return 2
    expected = places(int(m, 16) for m in LOCATION.findall(native.stderr))
    command = [str(exe), str(image), "--drive"]
    if srm:
        sav = out_dir / (args.movie.stem + ".sav")
        sav.write_bytes(gba_save(srm))
        command += ["--sav", str(sav)]
    asks = []
    for frame in list(range(LOCATION_EVERY, int(frames), LOCATION_EVERY)) + [int(frames)]:
        asks += ["movie %s %d" % (args.movie, frame), "peek %06X 2" % LOCATION_ID]
    gba = subprocess.run(command, input="\n".join(asks + ["quit"]) + "\n", stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, env=env)
    answers = gba.stdout.split("\n")
    seen = []
    for ask, answer in zip(asks, answers):
        if not answer.startswith("ok"):
            print("run: the GBA build stopped at %s: %s" % (ask, answer))
            return 1
        if ask.startswith("peek"):
            word = bytes.fromhex(answer.split()[1])
            seen.append((word[0] | word[1] << 8) & LOCATION_BITS)
    if len(seen) * 2 != len(asks):
        print("run: the GBA build stopped after %d of %d reads" % (len(seen), len(asks) // 2))
        return 1
    got = places(seen)
    common = min(len(got), len(expected))
    same = got[:common] == expected[:common] and abs(len(got) - len(expected)) <= 1
    print("run: ct_native goes through %d places, the GBA in its own time %d: %s" % (
        len(expected), len(got), "the same" if same else "DIFFERENT"))
    if not same:
        apart = next((i for i in range(common) if got[i] != expected[i]), common)
        print("run: from place %d, ct_native %s, the GBA %s" % (
            apart, " ".join("$%03X" % p for p in expected[apart:apart + 6]),
            " ".join("$%03X" % p for p in got[apart:apart + 6])))
    return 0 if same else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mgba", type=pathlib.Path, required=True, help="mGBA's install prefix")
    ap.add_argument("--program", type=pathlib.Path, required=True)
    ap.add_argument("--ct-native", type=pathlib.Path, required=True)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--movie", type=pathlib.Path, required=True)
    ap.add_argument("--shot", action="append", default=[], metavar="FRAME=FILE.png",
                    help="write the GBA's picture once the game has played FRAME frames")
    ap.add_argument("--wav", type=pathlib.Path, metavar="FILE.wav",
                    help="write what mGBA plays as the movie plays, in the GBA's time")
    ap.add_argument("--stream", type=pathlib.Path, metavar="FILE.wav",
                    help="write every sample the program queued, as it plays where the game keeps its time")
    ap.add_argument("--profile", type=pathlib.Path, metavar="FILE",
                    help="sample where the processor is as the movie plays; FILE gets each function's share")
    ap.add_argument("--fast-rom", action="store_true", help="pack the ROM with the cartridge's faster waits (pack.py)")
    ap.add_argument("--colours", choices=pack.PROFILES, default="original",
                    help="pack the ROM with this profile's colours (pack.py's --profile)")
    ap.add_argument("--no-script-records", action="store_true",
                    help="pack without the scripts read ahead (pack.py --script-records)")
    ap.add_argument("--bios", type=pathlib.Path, metavar="FILE",
                    help="your own dump of the GBA's BIOS: the ROM gets its logo and the GBA starts in it")
    ap.add_argument("--own-time", action="store_true",
                    help="play in the GBA's own time, as by hand, and check the places it goes through")
    args = ap.parse_args(argv)
    out_dir = args.program.resolve().parent
    exe = runner(args.mgba.resolve(), out_dir)
    image = out_dir / "ct.gba"
    records = out_dir / "script.records"
    if not args.no_script_records:
        written = subprocess.run([str(args.ct_native), "--rom", str(args.rom), "--headless", "--script-records", str(records)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if written.returncode or not records.exists():
            print(written.stderr)
            print("run: ct_native didn't write the script records")
            return 2
    if pack.main(["--program", str(args.program), "--rom", str(args.rom), "--out", str(image), "--profile", args.colours]
                 + (["--fast-rom"] if args.fast_rom else []) + (["--bios", str(args.bios)] if args.bios else [])
                 + ([] if args.no_script_records else ["--script-records", str(records)])):
        return 2
    movie_text = args.movie.read_text(encoding="utf-8")
    frames = re.search(r"^# frames (\d+)", movie_text, re.M).group(1)
    srm = starting_save(movie_text)
    env = dict(os.environ)
    env["PATH"] = str(args.mgba.resolve() / "bin") + os.pathsep + env.get("PATH", "")
    if args.own_time:
        return own_time(args, exe, image, env, frames, srm, out_dir)
    decisions = out_dir / (args.movie.stem + ".decisions")
    end = out_dir / (args.movie.stem + ".end")
    command = [str(args.ct_native), "--rom", str(args.rom), "--headless", "--mute", "--movie", str(args.movie),
               "--frames", frames, "--end-state", str(end), "--decisions", str(decisions)]
    if srm:
        command += ["--sram", str(srm)]
    native = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if native.returncode or not end.exists() or not decisions.exists():
        print(native.stderr)
        print("run: ct_native didn't play the movie")
        return 2
    state = end.read_bytes()
    command = [str(exe), str(image), "--movie", str(args.movie), "--frames", frames, "--decisions", str(decisions)]
    if srm:
        sav = out_dir / (args.movie.stem + ".sav")
        sav.write_bytes(gba_save(srm))
        command += ["--sav", str(sav)]
    for shot in args.shot:
        frame, _, path = shot.partition("=")
        command += ["--shot", frame, path]
    if args.wav:
        command += ["--wav", str(args.wav)]
    if args.stream:
        command += ["--stream", str(args.stream)]
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
    if "decisions unread" in gba.stdout or "decisions replayed" not in gba.stdout:
        print("run: the GBA build didn't replay ct_native's decisions to the movie's end")
        return 1
    whole, sram = "%08X" % zlib.crc32(state), "%08X" % zlib.crc32(state[0x20000:])
    same = (found.group(1), found.group(2)) == (whole, sram)
    print("run: ct_native ends with end state crc32 %s, save RAM crc32 %s: %s" % (
        whole, sram, "the same" if same else "DIFFERENT"))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())

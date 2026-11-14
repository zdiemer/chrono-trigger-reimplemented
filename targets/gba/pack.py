"""Pack a GBA cartridge: the program (build/gba/ct_gba.bin, code only), the
SNES cartridge's resources the catalogue names, a movie to play and the save
RAM it starts from, written as one .gba (docs/PLATFORM.md, "The GBA build,
as built"). The seed of the builder (issue #421): the cartridge's bytes come
from the player's own copy, verified first, and the result is never
committed.

    python3 targets/gba/pack.py --program build/gba/ct_gba.bin --rom roms/ct-usa.sfc \\
        --movie movies/title-start.ctm --out build/gba/title-start.gba

The pack after the program (targets/gba/main.c, PackHead): "CTGB", the
catalogue's version and its count, the movie's frames and where its pads are,
where the save RAM is (0: none); then each resource's place and length, in
the catalogue's order; then the resources, the movie's pads (a word a frame,
the first controller's in its low half and the second's in its high, as
tools/movies/mkmovie.py's pad words) and the save RAM.
"""
import argparse
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools import assets, rom  # noqa: E402
from tools.movies import mkmovie  # noqa: E402

ALIGN = 256          # main.c's: the pack starts at the program's end rounded up
HEAD = struct.Struct("<4sIIIII")
ENTRY = struct.Struct("<II")
TITLE, CODE = b"CT NATIVE", b"CTNE"
SRAM_BYTES = 0x2000
SRAM_CRC = re.compile(r"^# sram crc32 ([0-9A-Fa-f]{8})", re.M)


def catalogue_version():
    """The catalogue's version as game/data/asset_catalogue.c has it."""
    text = (ROOT / "game/data/asset_catalogue.c").read_text(encoding="utf-8")
    return int(re.search(r"asset_catalogue_version = 0x([0-9A-Fa-f]+)u;", text).group(1), 16)


def resources(cartridge):
    """Each resource's bytes, as the cartridge has them, in the catalogue's order."""
    out = []
    for row in assets.load():
        at = assets.bus_to_offset(row.at)
        out.append(cartridge[at:at + row.end - row.at + 1])
    return out


def header_check(image):
    """The complement check the BIOS reads at $BD, over $A0-$BC."""
    return (-(sum(image[0xA0:0xBD]) + 0x19)) & 0xFF


def movie_pads(text):
    """A movie's frames (its `# frames` line, or its last event's end) and
    its pad words, a frame each."""
    events = mkmovie.parse_movie(text)
    found = re.search(r"^# frames (\d+)", text, re.M)
    frames = int(found.group(1)) if found else max((start + count for start, count, _ in events), default=0)
    return frames, mkmovie.pads_from_events(events, frames)


def pack(program, cartridge, movie_text, sram):
    parts = resources(cartridge)
    out = bytearray(program)
    out += bytes(-len(out) % ALIGN)
    base = len(out)
    table = HEAD.size + ENTRY.size * len(parts)
    body = bytearray()
    entries = []
    for data in parts:
        body += bytes(-(table + len(body)) % 4)
        entries.append((table + len(body), len(data)))
        body += data
    frames, pads = movie_pads(movie_text.decode("utf-8"))
    body += bytes(-(table + len(body)) % 4)
    pads_at = table + len(body)
    body += struct.pack("<%dI" % frames, *pads)
    sram_at = 0
    if sram is not None:
        body += bytes(-(table + len(body)) % 4)
        sram_at = table + len(body)
        body += sram
    out += HEAD.pack(b"CTGB", catalogue_version(), len(parts), frames, pads_at, sram_at)
    for at, length in entries:
        out += ENTRY.pack(at, length)
    out += body
    out[0xA0:0xAC] = TITLE.ljust(12, b"\0")
    out[0xAC:0xB0] = CODE
    out[0xBD] = header_check(out)
    out += bytes(-len(out) % 4)
    return bytes(out), base


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--program", type=pathlib.Path, required=True)
    ap.add_argument("--rom", type=pathlib.Path, default=ROOT / "roms/ct-usa.sfc")
    ap.add_argument("--movie", type=pathlib.Path, required=True)
    ap.add_argument("--sram", type=pathlib.Path, help="the save RAM the movie starts from (default: the one its CRC names, in saves/)")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args(argv)
    cartridge = rom.load(args.rom)
    if zlib.crc32(cartridge) != rom.EXPECTED_CRC32:
        print("pack: %s is not the No-Intro Chrono Trigger (USA) image" % args.rom, file=sys.stderr)
        return 1
    movie_text = args.movie.read_bytes()
    sram = args.sram.read_bytes() if args.sram else None
    wanted = SRAM_CRC.search(movie_text.decode("utf-8", "replace"))
    if wanted and sram is None:
        crc = int(wanted.group(1), 16)
        sram = next((p.read_bytes() for p in sorted((ROOT / "saves").rglob("*.srm"))
                     if zlib.crc32(p.read_bytes()) == crc), None)
        if sram is None:
            print("pack: no save in saves/ has the CRC-32 %08X the movie starts from" % crc, file=sys.stderr)
            return 1
    if sram is not None and len(sram) != SRAM_BYTES:
        print("pack: a cartridge save is 8 KiB", file=sys.stderr)
        return 1
    image, base = pack(args.program.read_bytes(), cartridge, movie_text, sram)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image)
    print("pack: %s, %d bytes: the program %d, the pack from %d" % (args.out, len(image), base, base))
    return 0


if __name__ == "__main__":
    sys.exit(main())

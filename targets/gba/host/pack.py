"""Pack a GBA cartridge: the program (build/gba/ct_gba.bin, code only) and the
SNES cartridge's resources the catalogue names, written as one .gba (docs/PLATFORM.md, "The GBA build,
as built"). The seed of the builder (issue #421): the cartridge's bytes come
from the player's own copy, verified first, and the result is never
committed.

    python3 targets/gba/host/pack.py --program build/gba/ct_gba.bin --rom roms/ct-usa.sfc \\
        --out build/gba/ct.gba

The pack after the program (targets/gba/system/main.c, PackHead): "CTGB", the
catalogue's version and its count, where the sound's samples are; then each
resource's place and length, in the catalogue's order; then the resources,
and the sound's samples converted to 8-bit PCM (samples.py). The game's save is the GBA's SRAM,
which mGBA keeps in the .sav beside the .gba (targets/gba/services/cartridge_save.h).
"""
import argparse
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools import assets, rom  # noqa: E402

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import samples  # noqa: E402

ALIGN = 256          # main.c's: the pack starts at the program's end rounded up
HEAD = struct.Struct("<4sIII")
ENTRY = struct.Struct("<II")
TITLE, CODE = b"CT NATIVE", b"CTNE"


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


def sound_samples(cartridge):
    """The sound's samples as the GBA's mixer plays them (samples.py)."""
    return samples.table(cartridge, assets.load(), assets.bus_to_offset)


def header_check(image):
    """The complement check the BIOS reads at $BD, over $A0-$BC."""
    return (-(sum(image[0xA0:0xBD]) + 0x19)) & 0xFF


def pack(program, cartridge):
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
    body += bytes(-(table + len(body)) % 4)
    samples_at = table + len(body)
    body += sound_samples(cartridge)
    out += HEAD.pack(b"CTGB", catalogue_version(), len(parts), samples_at)
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
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args(argv)
    cartridge = rom.load(args.rom)
    if zlib.crc32(cartridge) != rom.EXPECTED_CRC32:
        print("pack: %s is not the No-Intro Chrono Trigger (USA) image" % args.rom, file=sys.stderr)
        return 1
    image, base = pack(args.program.read_bytes(), cartridge)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image)
    print("pack: %s, %d bytes: the program %d, the pack from %d" % (args.out, len(image), base, base))
    return 0


if __name__ == "__main__":
    sys.exit(main())

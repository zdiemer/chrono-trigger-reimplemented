#!/usr/bin/env python3
"""Verify and stage the Chrono Trigger (USA) ROM.

    python3 tools/rom.py verify path/to/ct.sfc
    python3 tools/rom.py stage  path/to/ct.zip   # -> roms/ct-usa.sfc

Accepts .sfc/.smc images or a .zip containing one. A 512-byte copier header is
stripped. Only the No-Intro USA release is accepted; everything downstream
(addresses, symbols, traces) assumes exactly these bytes.
"""

import argparse
import hashlib
import sys
import zipfile
import zlib
from pathlib import Path

EXPECTED_SIZE = 0x400000
EXPECTED_CRC32 = 0x2D206BF7
EXPECTED_SHA1 = "de5822f4f2f7a55acb8926d4c0eaa63d5d989312"

REPO_ROOT = Path(__file__).resolve().parent.parent
STAGED_ROM = REPO_ROOT / "roms" / "ct-usa.sfc"

HEADER = 0xFFC0  # HiROM internal header


def load(path: Path) -> bytes:
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as z:
            names = [n for n in z.namelist() if n.lower().endswith((".sfc", ".smc"))]
            if len(names) != 1:
                sys.exit(f"{path}: expected exactly one .sfc/.smc inside, found {names}")
            data = z.read(names[0])
    else:
        data = path.read_bytes()
    if len(data) % 0x8000 == 0x200:
        data = data[0x200:]
    return data


def describe(data: bytes) -> str:
    h = data[HEADER:HEADER + 0x30]
    title = h[0:21].decode("ascii", "replace").rstrip()
    checksum = h[0x1E] | h[0x1F] << 8
    complement = h[0x1C] | h[0x1D] << 8
    return (
        f"title      {title!r}\n"
        f"map mode   ${h[0x15]:02X}  cart type ${h[0x16]:02X}\n"
        f"rom size   {1 << h[0x17]} KiB  sram {1 << h[0x18]} KiB\n"
        f"region     ${h[0x19]:02X}  version ${h[0x1B]:02X}\n"
        f"checksum   ${checksum:04X} (complement ${complement:04X})"
    )


def verify(data: bytes) -> bool:
    crc = zlib.crc32(data)
    sha1 = hashlib.sha1(data).hexdigest()
    print(describe(data))
    print(f"size       {len(data):#x}")
    print(f"crc32      {crc:08X}")
    print(f"sha1       {sha1}")
    ok = len(data) == EXPECTED_SIZE and crc == EXPECTED_CRC32 and sha1 == EXPECTED_SHA1
    print("OK: Chrono Trigger (USA)" if ok else "MISMATCH: not the No-Intro Chrono Trigger (USA) image")
    return ok


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["verify", "stage"])
    ap.add_argument("rom", type=Path)
    args = ap.parse_args()

    data = load(args.rom)
    if not verify(data):
        sys.exit(1)
    if args.command == "stage":
        STAGED_ROM.parent.mkdir(exist_ok=True)
        STAGED_ROM.write_bytes(data)
        print(f"staged     {STAGED_ROM}")


if __name__ == "__main__":
    main()

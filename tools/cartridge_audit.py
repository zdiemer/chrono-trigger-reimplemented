"""Check finite ROM producers used by the address-source audit; prints no bytes.

    python3 tools/cartridge_audit.py --rom roms/ct-usa.sfc

The allowlist's other proofs are C bounds/guards. These checks cover tables
loaded from resources: text column offsets, scene line descriptors, save runs and event memory
peek operands. They are not a substitute for the sampled shipping gate.
"""
import argparse
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools.assets import read_rom
from tools.disasm import eventscript, lz


DESCRIPTORS = (0x3D45, 0x685E, 0x7354, 0x7B9C, 0x7BB6,
               0x8FEA, 0x9003, 0x900E, 0x9023, 0x9029)


def check(rom):
    errors = []
    for table in (0x25FE6, 0x26066):
        for column in range(32):
            at = table + column * 2
            if int.from_bytes(rom[at:at + 2], "little") > 0x5E0:
                errors.append(f"text column {column} exceeds the audited WRAM reach")
    program_at = 0x30000 | int.from_bytes(rom[0x3E0003:0x3E0005], "little")
    program, _ = lz.decompress(rom, program_at)
    for table in DESCRIPTORS:
        at = table - 0x3000
        for _ in range(9):
            if program[at] == 0xFF:
                break
            if program[at + 4] != 0x7E:
                errors.append(f"scene descriptor {table:04X} selects a non-WRAM bank")
            at += 5
        else:
            errors.append(f"scene descriptor {table:04X} has no terminator")
    for run in range(8):
        at = 0x3FFA7B + 5 * run
        source = int.from_bytes(rom[at:at + 3], "little")
        length = int.from_bytes(rom[at + 3:at + 5], "little")
        bank, offset = source >> 16, source & 0xFFFF
        if bank not in (0x7E, 0x7F) and not (bank == 0 and offset + length <= 0x2000):
            errors.append(f"save run {run} selects a non-WRAM range")

    hardware = collections.defaultdict(set)
    parsed = 0
    skipped = []
    problems = []
    for event in range(513):
        at = 0x3CF9F0 + 3 * event
        pointer = int.from_bytes(rom[at:at + 3], "little") & 0x3FFFFF
        unpacked = lz.decompress(rom, pointer)
        if unpacked is None:
            skipped.append(event)
            continue
        try:
            script = eventscript.parse(unpacked[0])
        except eventscript.ScriptError:
            skipped.append(event)
            continue
        parsed += 1
        if script.problems:
            problems.append(event)
        for pos in script.commands.keys() | script.shared.keys():
            if script.buf[pos] not in (0x48, 0x49):
                continue
            address = int.from_bytes(script.buf[pos + 1:pos + 4], "little")
            width = 1 if script.buf[pos] == 0x48 else 2
            for byte in range(address, address + width):
                bank, offset = byte >> 16, byte & 0xFFFF
                if (bank & 0x7F) < 0x40 and 0x2000 <= offset < 0x6000:
                    hardware[byte].add(event)
    if dict(hardware) != {0x2142: {0x114}}:
        errors.append("event peek hardware operands changed; audit their platform services")
    if problems:
        errors.append("event scripts have partial paths; hardware operand audit is incomplete")
    print(f"cartridge audit: {len(DESCRIPTORS)} scene descriptor tables; 8 save runs")
    print(f"event peeks: {parsed} parsed streams; {len(skipped)} non-script entries; "
          f"{len(problems)} streams with partial paths")
    for at, events in sorted(hardware.items()):
        print(f"hardware ${at:06X}: events " + ", ".join(f"${e:03X}" for e in sorted(events)))
    for error in errors:
        print(error)
    print("cartridge audit: FAILED" if errors else "cartridge audit: OK")
    return bool(errors)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    sys.exit(check(read_rom(parser.parse_args().rom)))

"""SNES bus <-> ROM file offset mapping for a HiROM cartridge, and address parsing."""

import re

ROM_SIZE = 0x400000


def bus_to_offset(addr: int) -> int | None:
    """ROM file offset for a 24-bit bus address, or None if it isn't ROM."""
    bank, a = addr >> 16, addr & 0xFFFF
    if 0xC0 <= bank <= 0xFF:
        off = (bank - 0xC0) << 16 | a
    elif 0x40 <= bank <= 0x7D:
        off = (bank - 0x40) << 16 | a
    elif (bank <= 0x3F or 0x80 <= bank <= 0xBF) and a >= 0x8000:
        off = (bank & 0x3F) << 16 | a
    else:
        return None
    return off if off < ROM_SIZE else None


def fmt(addr: int) -> str:
    return f"{addr >> 16:02X}:{addr & 0xFFFF:04X}"


_ADDR_RE = re.compile(r"^\$?([0-9A-Fa-f]{2})[:/]?([0-9A-Fa-f]{4})$")


def parse(s: str | int) -> int:
    """Parse "$C0:1234", "C0:1234", "C01234" (or an int) into a bus address."""
    if isinstance(s, int):
        return s
    m = _ADDR_RE.match(s.strip())
    if not m:
        raise ValueError(f"bad address {s!r}; expected BB:AAAA")
    return int(m.group(1), 16) << 16 | int(m.group(2), 16)

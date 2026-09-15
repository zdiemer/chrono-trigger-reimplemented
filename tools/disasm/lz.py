"""The general-purpose decompressor at $C3:0557, in Python.

Written from our C port (game/data/decompress.c), which documents the stream
format: a u16 length of the first segment, then control bytes, each followed by
the items it describes (bits LSB first: 0 = a literal byte, 1 = a 2-byte
back-reference; a control byte of 0 means eight literals). Where a segment
ends, a header byte gives how many bits of the next control byte count (0 ends
the stream) and a u16 gives the next segment's end, relative to the stream.
A back-reference copies length + 1 bytes, one at a time, from `distance` bytes
behind the output position; the top two bits of the first segment header pick
the split of its 16 bits (4-bit length and 12-bit distance, or 5 and 11).

Source indices are 16 bits and added to the stream's bank, so a stream can run
into the next bank as the original's absolute indexed reads do. Offsets here
are ROM file offsets.
"""

MAX_OUTPUT = 0x10000


def decompress(data: bytes, off: int, strict: bool = True) -> tuple[bytes, int] | None:
    """(output, bytes consumed including the end header) for the stream at file
    offset off, or None if it runs off the ROM, loops, or (strict) refers back
    before the start of its own output."""
    n = len(data)
    bank = off & ~0xFFFF
    base = off & 0xFFFF

    def at(x: int) -> int:                   # x: absolute 16-bit index in the stream's bank
        a = bank + (x & 0xFFFF)
        if a >= n:
            raise IndexError
        return data[a]

    try:
        x = (base + 2) & 0xFFFF
        seg_end = (x + (at(base) | at(base + 1) << 8)) & 0xFFFF
        wide = at(seg_end) & 0xC0
        out = bytearray()
        bits = 8
        steps = 0
        while True:
            steps += 1
            if steps > 0x20000 or len(out) > MAX_OUTPUT:
                return None
            if x == seg_end:
                header = at(x) & 0x3F
                if header == 0:
                    consumed = ((x + 1) - base) & 0xFFFF or 0x10000
                    return bytes(out), consumed
                bits = header
                seg_end = ((at(x + 1) | at(x + 2) << 8) + base) & 0xFFFF
                x = (x + 3) & 0xFFFF
                continue
            control = at(x)
            if control == 0:
                out += bytes(at(x + i) for i in range(1, 9))
                x = (x + 9) & 0xFFFF
                continue
            x = (x + 1) & 0xFFFF
            ref = control & 1
            control >>= 1
            while True:
                if not ref:
                    out.append(at(x))
                    x = (x + 1) & 0xFFFF
                else:
                    lo, hi = at(x), at(x + 1)
                    word = lo | hi << 8
                    count = ((hi >> 3) if wide else (hi >> 4)) + 3
                    distance = word & (0x07FF if wide else 0x0FFF)
                    if distance == 0 or distance > len(out):
                        if strict:
                            return None
                        out += bytes(distance - len(out))      # unknown RAM before the output
                    src = len(out) - distance
                    for i in range(count):
                        out.append(out[src + i])
                    x = (x + 2) & 0xFFFF
                bits -= 1
                if bits == 0:
                    bits = 8
                    break
                ref = control & 1
                control >>= 1
    except IndexError:
        return None


def stream_length(data: bytes, off: int) -> int | None:
    r = decompress(data, off)
    return None if r is None else r[1]

"""The cartridge's packed streams unpacked when the cartridge is packed
(pack.py), so that the GBA copies what a stream unpacks to rather than
working it out a byte at a time as a map, a menu or a battle loads (issue
#524; game/data/decompress.c, `unpack`, $C3:0557).

The unpacking here is the game's own, step for step: the same reads of the
stream, the same items, and the counts the unpacker's time is worked out
from (decompress.c's Counts, its reads of the stream, its literals and its
references' bytes), which the GBA's record of a stream holds beside its
bytes. A stream is kept only where its unpacking depends on nothing but the
stream: every reference reads bytes the stream itself wrote, none before
where it starts, and every read of the stream lies in its own resource. The
GBA uses the record only where the place it unpacks to takes the bytes as
they are (game/wram.h, wram_put_unpacked); anything else is unpacked by the
game as before.
"""
import struct

FIRST_LENGTH_BYTES, HEADER_BYTES, BITS_IN_A_CONTROL = 2, 3, 8
HEADER_BITS, LONG_RUNS = 0x3F, 0xC0
LONGEST = 0xFFFF     # what the unpacked length's 16 bits hold

# A stream's record (game/data/assets.h, AssetUnpacked): its counts, then its bytes.
RECORD = struct.Struct("<HHIIIIIIII")


class Stream:
    """The stream's bytes as decompress.c reads them: an offset into its
    bank that runs on into the next, read through `read` (bus address to
    byte, or None outside the resource)."""

    def __init__(self, read, start):
        self.read, self.bank, self.at, self.reads = read, start >> 16, (start + FIRST_LENGTH_BYTES) & 0xFFFF, 0

    def byte(self, ahead):
        self.reads += 1
        value = self.read((self.bank << 16) + self.at + ahead)
        if value is None:
            raise ValueError("a read outside the stream's resource")
        return value


def unpack(read, start):
    """The stream at `start` unpacked: (bytes, record) where it depends only
    on itself, else None."""
    first = [read(start), read((start + 1) & 0xFFFFFF)]
    if None in first:
        return None
    s = Stream(read, start)
    segment_end = (s.at + (first[0] | first[1] << 8)) & 0xFFFF
    flag = read((s.bank << 16) | segment_end)
    if flag is None:
        return None
    long_runs = flag & LONG_RUNS
    out = bytearray()
    turns = segments = controls = references = items = literals = reference_bytes = 0
    bits_left = BITS_IN_A_CONTROL
    try:
        while True:
            turns += 1
            if s.at == segment_end:
                bits = s.byte(0) & HEADER_BITS
                if not bits:
                    break
                segments += 1
                bits_left = bits
                segment_end = ((s.byte(1) | s.byte(2) << 8) + (start & 0xFFFF)) & 0xFFFF
                s.at = (s.at + HEADER_BYTES) & 0xFFFF
                continue
            control = s.byte(0)
            if not control:
                out += bytes(s.byte(k) for k in range(1, BITS_IN_A_CONTROL + 1))
                literals += BITS_IN_A_CONTROL
                s.at = (s.at + 1 + BITS_IN_A_CONTROL) & 0xFFFF
                continue
            controls += 1
            items += bits_left
            s.at = (s.at + 1) & 0xFFFF
            for _ in range(bits_left):
                if control & 1:
                    references += 1
                    high = s.byte(1)
                    word = s.byte(0) | high << 8
                    s.reads += 1      # the high byte, read again (decompress.c, reference_at)
                    distance, length = (word & 0x07FF, (high >> 3) + 3) if long_runs else (word & 0x0FFF, (high >> 4) + 3)
                    if distance > len(out) or distance == 0:
                        return None   # it reads what was there before the stream, or the byte it writes
                    for _ in range(length):
                        out.append(out[-distance])
                    reference_bytes += length
                    s.at = (s.at + 2) & 0xFFFF
                else:
                    out.append(s.byte(0))
                    literals += 1
                    s.at = (s.at + 1) & 0xFFFF
                control >>= 1
                if len(out) > LONGEST:
                    return None
            bits_left = BITS_IN_A_CONTROL
    except ValueError:
        return None
    if not out:
        return None
    record = RECORD.pack(len(out), 0, turns, segments, controls, references, items, s.reads, literals, reference_bytes)
    return bytes(out), record

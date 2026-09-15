"""Census stream parser on synthetic streams (no ROM needed).

    python3 -m unittest discover tests
"""

import unittest

from tools.disasm.census import parse_stream


def stream(body: bytes) -> bytes:
    """A one-segment stream: u16 length of the body, the body, end header 0."""
    return len(body).to_bytes(2, "little") + body + b"\x00"


class ParseStream(unittest.TestCase):
    def test_eight_literals(self):
        s = stream(b"\x00" + bytes(range(1, 9)))
        self.assertEqual(parse_stream(s + b"\xEE" * 4, 0), (len(s), 8))

    def test_back_reference(self):
        # Control bits LSB first: literal, reference, then six literals.
        # The reference copies 3 bytes from distance 1.
        body = bytes([0b00000010]) + b"A" + b"\x01\x00" + b"BCDEFG"
        s = stream(body)
        self.assertEqual(parse_stream(s, 0), (len(s), 1 + 3 + 6))

    def test_reference_before_output_is_rejected(self):
        body = bytes([0b00000001]) + b"\x01\x00" + b"ABCDEFG"
        self.assertIsNone(parse_stream(stream(body), 0))

    def test_missed_segment_end_is_rejected(self):
        # Eight literals, but the header claims the segment ends one byte earlier.
        s = (8).to_bytes(2, "little") + b"\x00" + bytes(range(1, 9)) + b"\x00\x00"
        self.assertIsNone(parse_stream(s, 0))

    def test_second_segment(self):
        # Segment 1: eight literals. Header: 8 bits used, next end relative to the
        # stream start. Segment 2: eight more literals, then the end header.
        seg1 = b"\x00" + bytes(8)
        seg2 = b"\x00" + bytes(8)
        end2 = 2 + len(seg1) + 3 + len(seg2)
        s = (len(seg1)).to_bytes(2, "little") + seg1 + b"\x08" + end2.to_bytes(2, "little") + seg2 + b"\x00"
        self.assertEqual(parse_stream(s, 0), (len(s), 16))


if __name__ == "__main__":
    unittest.main()

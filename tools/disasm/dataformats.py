"""Reader-backed MD2 formats. No cartridge contents are stored here.

The graphics pool codec deliberately describes storage, not tile ownership:
CD:1509 can select either two or three planes at an eight-byte-aligned offset.
"""

from dataclasses import dataclass
import struct


@dataclass(frozen=True)
class SpriteRecord:
    graphics: int
    assembly: int
    palette: int
    animation: int
    flags: int
    battle_fields: tuple = ()  # +5..9, copied separately at CC:EBB0-EBCC

    def encode(self):
        return bytes((self.graphics, self.assembly, self.palette, self.animation,
                      self.flags, *self.battle_fields))


def decode_sprites(blob, size):
    if size not in (5, 10) or len(blob) % size:
        raise ValueError("sprite table has a partial record or unsupported stride")
    return tuple(SpriteRecord(*blob[o:o + 5], tuple(blob[o + 5:o + size]))
                 for o in range(0, len(blob), size))


@dataclass(frozen=True)
class Exit:
    x: int
    y: int
    extent: int
    destination: int
    flags: int
    destination_x: int
    destination_y: int

    def encode(self):
        return bytes((self.x, self.y, self.extent, self.destination, self.flags,
                      self.destination_x, self.destination_y))


def decode_exits(blob, count):
    """C0:A695: count+1 bank-relative offsets, then seven-byte records.

    Repeated offsets are empty lists. The final offset is a sentinel, not an
    extra list. The region starts at bank offset zero and ends at that sentinel.
    """
    if count <= 0 or len(blob) < 2 * (count + 1):
        raise ValueError("truncated exit offset table")
    offsets = struct.unpack_from(f"<{count + 1}H", blob)
    if offsets[0] != 2 * (count + 1) or offsets[-1] != len(blob):
        raise ValueError("exit offsets do not consume exactly the region")
    lists = []
    for start, end in zip(offsets, offsets[1:]):
        if not offsets[0] <= start <= end <= len(blob) or (end - start) % 7:
            raise ValueError("exit offsets are descending, out of bounds, or split a record")
        lists.append(tuple(Exit(*blob[o:o + 7]) for o in range(start, end, 7)))
    return tuple(lists)


def encode_exits(lists):
    offset = 2 * (len(lists) + 1)
    offsets = [offset]
    payload = bytearray()
    for records in lists:
        for record in records:
            payload.extend(record.encode())
            offset += 7
        offsets.append(offset)
    return struct.pack(f"<{len(offsets)}H", *offsets) + payload


def decode_graphics(blob, planes):
    """Eight rows, MSB-left pixels. Two planes use SNES interleaved rows.

    One plane is an eight-byte storage unit in a mixed 2/3bpp pool; it does
    not assert that each unit is a separately addressable tile.
    """
    if planes not in (1, 2) or len(blob) % (8 * planes):
        raise ValueError("graphics region has a partial plane/tile")
    return tuple(tuple(tuple(sum(((blob[o + y * planes + p] >> (7 - x)) & 1) << p
                                 for p in range(planes)) for x in range(8))
                       for y in range(8)) for o in range(0, len(blob), 8 * planes))


def encode_graphics(tiles, planes):
    if planes not in (1, 2):
        raise ValueError("unsupported plane count")
    out = bytearray()
    for tile in tiles:
        if len(tile) != 8 or any(len(row) != 8 for row in tile):
            raise ValueError("a tile must have eight rows of eight pixels")
        for row in tile:
            if any(not 0 <= pixel < 1 << planes for pixel in row):
                raise ValueError("pixel exceeds plane count")
            for p in range(planes):
                out.append(sum(((pixel >> p) & 1) << (7 - x) for x, pixel in enumerate(row)))
    return bytes(out)


def event_bytes(blob, region):
    """The event commands a region holds: an `lz` region's output (the whole
    buffer), or a `script` region's bytes. None for another type."""
    if region.type == "lz":
        from .lz import decompress
        got = decompress(blob, 0)
        if got is None or got[1] != len(blob):
            raise ValueError("not a stream the decompressor consumes exactly")
        return got[0]
    return blob if region.type == "script" else None


def event_entries(region):
    """Positions in the region of an `event-commands` region's entries."""
    from .memmap import parse as parse_addr
    return [parse_addr(e) - region.start for e in region.params.get("entries", [])]


def check_event(blob, region):
    """(encoded, expected) for an event format: the script decoded to its
    readable form and encoded back (tools/disasm/eventscript.py)."""
    from . import eventscript as ev
    raw = event_bytes(blob, region)
    if raw is None:
        return None
    form = region.params.get("format")
    try:
        if form == "event":
            if region.type != "lz":
                raise ValueError("format event is for lz regions")
            text = ev.decode(raw)
        else:
            if region.type != "script":
                raise ValueError("format event-commands is for script regions")
            text = ev.decode_commands(raw, event_entries(region))
        return ev.encode(text), raw
    except ev.ScriptError as exc:
        raise ValueError(f"event script: {exc}") from None


def check_format(blob, region, context=None):
    """Validate explicitly supported formats; older format annotations stay as-is.
    Text formats (tools/disasm/text.py) decode and re-encode; `context` carries
    what they share, the decoded dialog dictionary. The event formats decode
    and re-encode the commands, for an `lz` region its decompressed output."""
    form = region.params.get("format")
    if region.type == "text" and form is not None:
        from .text import decode, encode, region_params
        params = region_params(region)
        encoded = encode(form, decode(form, blob, params, context or {}), params, context or {})
    elif form == "location-exits":
        if region.start & 0xFFFF:
            raise ValueError("exit offset table must start at bank offset zero")
        encoded = encode_exits(decode_exits(blob, region.params["count"]))
    elif form in ("snes-2bpp", "plane-pool"):
        planes = 2 if form == "snes-2bpp" else 1
        encoded = encode_graphics(decode_graphics(blob, planes), planes)
    elif form in ("event", "event-commands"):
        got = check_event(blob, region)
        if got is None:
            return
        encoded, blob = got              # compared on the commands, not the stream
    elif form == "sprite-records":
        encoded = b"".join(record.encode() for record in decode_sprites(blob, region.params["size"]))
    else:
        return
    if encoded != blob:
        raise ValueError("format does not round trip")

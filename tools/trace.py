#!/usr/bin/env python3
"""Read, inspect and diff .cttr per-frame state traces (docs/ORACLE.md).

    python3 tools/trace.py info  run.cttr
    python3 tools/trace.py peek  run.cttr '$7E:0100' --len 4 --frames 0:600
    python3 tools/trace.py peek  run.cttr 0x2000 --region VRAM
    python3 tools/trace.py diff  a.cttr b.cttr [--region WRAM] [--ignore '$7E:0000-00FF']
    python3 tools/trace.py dump  run.cttr --frame 600 --region CGRAM -o traces/cgram.bin
"""

import argparse
import mmap
import re
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

MAGIC = b"CTTR"
VERSION = 1
FRAME_HEADER = struct.Struct("<IIHBBHHQ")  # size, frame, input, flags, reserved, fb_w, fb_h, fb_hash
REGION_HEADER = struct.Struct("<QI")  # hash, run count
RUN_HEADER = struct.Struct("<II")  # offset, length

BUTTONS = ["B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R"]  # bits 15..4


def buttons_str(mask: int) -> str:
    names = [name for i, name in enumerate(BUTTONS) if mask & (1 << (15 - i))]
    return "+".join(names) if names else "-"


@dataclass
class Region:
    id: int
    name: str
    size: int


@dataclass
class Frame:
    frame: int
    input: int
    keyframe: bool
    fb_width: int
    fb_height: int
    fb_hash: int
    hashes: list  # per region
    runs: list  # per region: list of (offset, memoryview)


class Trace:
    def __init__(self, path: Path):
        self.path = path
        self._file = open(path, "rb")
        self.data = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
        d = self.data
        if len(d) < 16 or d[0:4] != MAGIC:
            raise SystemExit(f"{path}: not a .cttr trace")
        version, header_size, self.rom_crc32, region_count, producer_len = struct.unpack_from("<HHIHH", d, 4)
        if version != VERSION:
            raise SystemExit(f"{path}: trace version {version}, this tool reads version {VERSION}")
        self.regions = []
        pos = 16
        for _ in range(region_count):
            rid, _flags, _res, size, name = struct.unpack_from("<BBHI8s", d, pos)
            self.regions.append(Region(rid, name.rstrip(b"\0").decode("ascii"), size))
            pos += 16
        self.producer = bytes(d[pos:pos + producer_len]).decode("utf-8", "replace")
        self.header_size = header_size

    def region_index(self, name: str) -> int:
        for i, r in enumerate(self.regions):
            if r.name.upper() == name.upper():
                return i
        raise SystemExit(f"{self.path}: no region {name!r} (have {', '.join(r.name for r in self.regions)})")

    def frames(self, with_runs: bool = True):
        d = self.data
        pos = self.header_size
        mv = memoryview(d)
        while pos + 4 <= len(d):
            size, frame, inp, flags, _res, fbw, fbh, fbhash = FRAME_HEADER.unpack_from(d, pos)
            end = pos + 4 + size
            if end > len(d):
                print(f"warning: {self.path}: truncated frame record at byte {pos}", file=sys.stderr)
                return
            p = pos + FRAME_HEADER.size
            hashes, runs = [], []
            for _region in self.regions:
                h, count = REGION_HEADER.unpack_from(d, p)
                p += REGION_HEADER.size
                region_runs = []
                for _ in range(count):
                    off, length = RUN_HEADER.unpack_from(d, p)
                    p += RUN_HEADER.size
                    if with_runs:
                        region_runs.append((off, mv[p:p + length]))
                    p += length
                hashes.append(h)
                runs.append(region_runs)
            yield Frame(frame, inp, bool(flags & 1), fbw, fbh, fbhash, hashes, runs)
            pos = end

    def states(self):
        """Yield (frame, [bytearray per region]) with state reconstructed. The
        bytearrays are reused between iterations; copy them to keep them."""
        mem = [bytearray(r.size) for r in self.regions]
        for f in self.frames():
            for i, region_runs in enumerate(f.runs):
                for off, chunk in region_runs:
                    mem[i][off:off + len(chunk)] = chunk
            yield f, mem


# ---- addresses --------------------------------------------------------------

def parse_int(s: str) -> int:
    s = s.strip()
    if s.startswith("$"):
        return int(s[1:], 16)
    return int(s, 0) if s.lower().startswith("0x") else int(s, 16)


def resolve_address(trace: Trace, text: str, region: str | None) -> tuple[int, int]:
    """Return (region index, offset) for an address.

    Forms: '$7E:0100', '7E:0100', '7E0100' (SNES bus address), 'VRAM:1234'
    (region-relative), or a bare offset with --region."""
    text = text.strip()
    m = re.fullmatch(r"([A-Za-z]+):([$0-9A-Fa-fx]+)", text)
    if m and not re.fullmatch(r"[0-9A-Fa-f]{1,2}", m.group(1)):
        return trace.region_index(m.group(1)), parse_int(m.group(2))
    if region:
        return trace.region_index(region), parse_int(text)

    m = re.fullmatch(r"\$?([0-9A-Fa-f]{1,2}):?([0-9A-Fa-f]{4})", text)
    if not m:
        raise SystemExit(f"cannot parse address {text!r}; use $BB:AAAA, BBAAAA, or an offset with --region")
    bank, addr = int(m.group(1), 16), int(m.group(2), 16)
    if bank in (0x7E, 0x7F):
        return trace.region_index("WRAM"), (bank - 0x7E) << 16 | addr
    if (bank & 0x7F) <= 0x3F and addr < 0x2000:
        return trace.region_index("WRAM"), addr
    if (bank & 0x7F) in range(0x20, 0x40) and 0x6000 <= addr < 0x8000:
        sram = trace.region_index("SRAM")
        return sram, (addr - 0x6000) % trace.regions[sram].size
    raise SystemExit(f"${bank:02X}:{addr:04X} is not RAM; use a region-relative address like VRAM:1234")


def format_address(region: Region, offset: int) -> str:
    if region.name == "WRAM":
        return f"$7{'E' if offset < 0x10000 else 'F'}:{offset & 0xFFFF:04X}"
    return f"{region.name}:{offset:04X}"


def parse_range(text: str) -> tuple[int, int]:
    a, _, b = text.partition(":")
    start = int(a) if a else 0
    end = int(b) if b else 1 << 62
    return start, end


def parse_ignore(trace: Trace, spec: str) -> tuple[int, int, int]:
    """'$7E:0100-01FF' or 'VRAM:0000-00FF' or '$7E:0100' -> (region, start, end inclusive)."""
    if "-" in spec:
        head, _, tail = spec.rpartition("-")
    else:
        head, tail = spec, None
    region, start = resolve_address(trace, head, None)
    if tail is None:
        end = start
    else:
        end = int(tail.lstrip("$"), 16)
        if trace.regions[region].name == "WRAM" and end < 0x10000 and start >= 0x10000:
            end |= 0x10000
    return region, start, end


# ---- commands ---------------------------------------------------------------

def cmd_info(args) -> int:
    t = Trace(args.trace)
    print(f"file       {args.trace} ({args.trace.stat().st_size:,} bytes)")
    print(f"producer   {t.producer}")
    print(f"rom crc32  {t.rom_crc32:08X}")
    print("regions    " + ", ".join(f"{r.name}={r.size:#x}" for r in t.regions))
    count = keyframes = 0
    first = last = None
    for f in t.frames(with_runs=False):
        count += 1
        keyframes += f.keyframe
        first = f.frame if first is None else first
        last = f.frame
    if count:
        print(f"frames     {count} ({first}..{last}), {keyframes} keyframe(s)")
        print(f"avg size   {(args.trace.stat().st_size - t.header_size) / count:,.0f} bytes/frame")
    else:
        print("frames     0")
    return 0


def cmd_peek(args) -> int:
    t = Trace(args.trace)
    ri, off = resolve_address(t, args.address, args.region)
    region = t.regions[ri]
    if off + args.len > region.size:
        raise SystemExit(f"{region.name} is {region.size:#x} bytes; {off:#x}+{args.len} is out of range")
    start, end = parse_range(args.frames)
    label = format_address(region, off)
    prev = None
    for f, mem in t.states():
        if f.frame < start:
            continue
        if f.frame > end:
            break
        value = bytes(mem[ri][off:off + args.len])
        if args.all or value != prev:
            word = ""
            if args.len in (2, 3, 4):
                word = f"  = {int.from_bytes(value, 'little'):#0{args.len * 2 + 2}x}"
            print(f"frame {f.frame:6d}  {label}  {value.hex(' ').upper()}{word}  [{buttons_str(f.input)}]")
        prev = value
    return 0


def diff_bytes(a: bytearray, b: bytearray, ignore: list[tuple[int, int]]):
    """Yield offsets where a and b differ, skipping ignored inclusive ranges."""
    size = min(len(a), len(b))
    chunk = 256
    for base in range(0, size, chunk):
        if a[base:base + chunk] == b[base:base + chunk]:
            continue
        for i in range(base, min(base + chunk, size)):
            if a[i] != b[i] and not any(s <= i <= e for s, e in ignore):
                yield i


def cmd_diff(args) -> int:
    ta, tb = Trace(args.a), Trace(args.b)
    if ta.rom_crc32 != tb.rom_crc32:
        print(f"warning: ROM CRC32 differs ({ta.rom_crc32:08X} vs {tb.rom_crc32:08X})", file=sys.stderr)

    names = args.region or [r.name for r in ta.regions
                            if r.name != "WRAMW" and any(r.name == q.name for q in tb.regions)]

    # --written-only: compare WRAM only where the game has written, using the
    # WRAMW region (ct_dev) of whichever trace has one.
    mask_side = None
    if args.written_only:
        for side, t in (("a", ta), ("b", tb)):
            if any(r.name == "WRAMW" for r in t.regions):
                mask_side = side
                break
        if mask_side is None:
            raise SystemExit("--written-only needs a trace with a WRAMW region (ct_dev writes one)")
    pairs = []
    for name in names:
        ia, ib = ta.region_index(name), tb.region_index(name)
        if ta.regions[ia].size != tb.regions[ib].size:
            print(f"warning: {name} size differs; comparing the common prefix", file=sys.stderr)
        pairs.append((name, ia, ib))
    missing = [r.name for r in ta.regions if r.name not in names] + [r.name for r in tb.regions if r.name not in names]
    if missing and not args.region:
        print(f"note: regions not in both traces, not compared: {', '.join(sorted(set(missing)))}")

    ignore: dict[str, list[tuple[int, int]]] = {}
    for spec in args.ignore:
        for part in spec.split(","):
            ri, s, e = parse_ignore(ta, part)
            ignore.setdefault(ta.regions[ri].name, []).append((s, e))

    start, end = parse_range(args.frames)
    sa, sb = ta.states(), tb.states()
    fa, ma = next(sa, (None, None))
    fb_, mb = next(sb, (None, None))
    compared = 0
    first_frame = last_frame = None
    divergent_frames = 0
    reported = False
    input_warned = False

    while fa is not None and fb_ is not None:
        if fa.frame + args.offset < fb_.frame:
            fa, ma = next(sa, (None, None))
            continue
        if fb_.frame < fa.frame + args.offset:
            fb_, mb = next(sb, (None, None))
            continue
        frame = fa.frame
        if frame > end:
            break
        if frame >= start:
            compared += 1
            first_frame = frame if first_frame is None else first_frame
            last_frame = frame
            if fa.input != fb_.input and not input_warned:
                print(f"warning: input differs first at frame {frame}: "
                      f"{buttons_str(fa.input)} vs {buttons_str(fb_.input)}")
                input_warned = True

            region_diffs = []
            for name, ia, ib in pairs:
                if fa.hashes[ia] == fb_.hashes[ib]:
                    continue
                offs = list(diff_bytes(ma[ia], mb[ib], ignore.get(name, [])))
                if mask_side and name == "WRAM":
                    t, m = (ta, ma) if mask_side == "a" else (tb, mb)
                    written = m[t.region_index("WRAMW")]
                    offs = [o for o in offs if written[o]]
                if offs:
                    region_diffs.append((name, ia, ib, offs))
            fb_differs = (fa.fb_width, fa.fb_height, fa.fb_hash) != (fb_.fb_width, fb_.fb_height, fb_.fb_hash)
            counts_fb = args.framebuffer and fb_differs

            if region_diffs or counts_fb:
                divergent_frames += 1
                if not reported:
                    reported = True
                    print(f"first divergence at frame {frame}" +
                          (f" (b frame {frame + args.offset})" if args.offset else ""))
                    if fb_differs:
                        print(f"  framebuffer: {fa.fb_width}x{fa.fb_height} {fa.fb_hash:016x} vs "
                              f"{fb_.fb_width}x{fb_.fb_height} {fb_.fb_hash:016x}")
                    for name, ia, ib, offs in region_diffs:
                        region = ta.regions[ia]
                        print(f"  {name}: {len(offs)} byte(s) differ")
                        for off in offs[:args.max]:
                            print(f"    {format_address(region, off)}  {ma[ia][off]:02X} vs {mb[ib][off]:02X}")
                        if len(offs) > args.max:
                            print(f"    ... {len(offs) - args.max} more")
                    if not args.keep_going:
                        break
        fa, ma = next(sa, (None, None))
        fb_, mb = next(sb, (None, None))

    if compared == 0:
        print("no overlapping frames to compare")
        return 2
    if not reported:
        print(f"no divergence in {compared} frame(s) ({first_frame}..{last_frame}); regions {', '.join(names)}")
        return 0
    if args.keep_going:
        print(f"{divergent_frames} of {compared} frame(s) diverge ({first_frame}..{last_frame})")
    return 1


def ensure_safe_output(path: Path, force: bool) -> None:
    """Refuse to write ROM-derived bytes into tracked parts of the repository."""
    resolved = path.resolve()
    try:
        resolved.relative_to(REPO_ROOT)
    except ValueError:
        return
    try:
        ignored = subprocess.run(["git", "-C", str(REPO_ROOT), "check-ignore", "-q", str(resolved)],
                                 capture_output=True).returncode == 0
    except OSError:
        ignored = False
    if not ignored and not force:
        raise SystemExit(f"refusing to write {path}: it is inside the repository and not gitignored "
                         f"(use traces/ or captures/, or pass --force)")


def cmd_dump(args) -> int:
    t = Trace(args.trace)
    ri = t.region_index(args.region)
    ensure_safe_output(args.output, args.force)
    for f, mem in t.states():
        if f.frame == args.frame:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(mem[ri])
            print(f"wrote {t.regions[ri].name} at frame {f.frame} ({t.regions[ri].size:#x} bytes) to {args.output}")
            return 0
        if f.frame > args.frame:
            break
    raise SystemExit(f"frame {args.frame} is not in {args.trace}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    p = sub.add_parser("info", help="header and frame summary")
    p.add_argument("trace", type=Path)
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("peek", help="print a value over frames (changes only unless --all)")
    p.add_argument("trace", type=Path)
    p.add_argument("address", help="$7E:0100, 7E0100, VRAM:1234, or an offset with --region")
    p.add_argument("--region", help="region for a bare offset")
    p.add_argument("--len", type=int, default=1)
    p.add_argument("--frames", default=":", help="a:b inclusive frame range")
    p.add_argument("--all", action="store_true", help="print every frame, not only changes")
    p.set_defaults(func=cmd_peek)

    p = sub.add_parser("diff", help="find the first divergent frame between two traces")
    p.add_argument("a", type=Path)
    p.add_argument("b", type=Path)
    p.add_argument("--region", action="append", help="region to compare (repeatable; default: all shared)")
    p.add_argument("--ignore", action="append", default=[],
                   help="address ranges to ignore, e.g. '$7E:0000-00FF' or 'VRAM:0000-01FF' (repeatable)")
    p.add_argument("--frames", default=":", help="a:b inclusive frame range (frames of trace a)")
    p.add_argument("--offset", type=int, default=0, help="compare frame F of a with frame F+OFFSET of b")
    p.add_argument("--framebuffer", action="store_true", help="count framebuffer hash mismatches as divergence")
    p.add_argument("--max", type=int, default=32, help="differing addresses to list per region")
    p.add_argument("--keep-going", action="store_true", help="count all divergent frames")
    p.add_argument("--written-only", action="store_true",
                   help="compare WRAM only where the game has written (needs a ct_dev trace with WRAMW); "
                        "use against cores that randomize power-on RAM")
    p.set_defaults(func=cmd_diff)

    p = sub.add_parser("dump", help="write one region at one frame as raw bytes")
    p.add_argument("trace", type=Path)
    p.add_argument("--frame", type=int, required=True)
    p.add_argument("--region", required=True)
    p.add_argument("-o", "--output", type=Path, required=True)
    p.add_argument("--force", action="store_true", help="allow writing to a non-ignored path in the repo")
    p.set_defaults(func=cmd_dump)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

"""The decoded maps mean what the game draws: for several places, the map
drawn from the texts beside the pack (tools/sheets.py, which reads the map,
the looks or tile groups and the tiles as their texts have them) is compared
pixel for pixel with ct_native's own picture of BG1, of BG2 and of the two
together, sprites, windows and color math left out (the route protocol's
`layershot`, frontend/route_driver.h), each layer placed where the game has
scrolled it, in the colors the game has loaded.

A byte-identical round trip says a text keeps every byte; this says its
fields are the ones the game reads: the half of a tile set's looks a tile's
properties pick, the tiles' numbers, flips, palettes and priority, BG2 under
BG1, and a world map's two planes and their groups.

The places are points of the gate's movies where nothing has changed the
map since it loaded (an event's copied tiles, an animated tile, a dialog
window drawn into BG1 or a battle's own layers would differ, as the game
means them to). Most are fixed frames of scenes; a place a battle follows
closely is found by the game's state instead, so a change of timing can't
move it into the battle: from a movie's frame on, the first look at which
the pad has walked the party in the location for WALKED looks running.

Run by ctest with ct_native's path; without a staged ROM
(roms/ct-usa.sfc) it says so and passes.

    python3 tests/frontend/map_pictures.py <ct_native> [--keep DIR]
"""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.sheets import FIRST_WORLD_MAP, place_at, png  # noqa: E402

ROM = ROOT / "roms" / "ct-usa.sfc"
WIDTH, HEIGHT = 256, 224
BG1, BG2 = 1, 2
LOCATION_ID = 0x7E0100     # location_id, the nine-bit location number
SCREEN_EDGES = 0x7E1D0A    # field_screen_left, _right and _top, in 8-pixel tiles
PLANE_COLUMN = 0x7E00E3    # menu_map_column and menu_map_row, each plane's
PLANE_ROW = 0x7E00E7       # top left 8-pixel tile on a world map
TILE = 8
# The picture's first row is the screen's line 1: a layer's row one past its scroll.
FIRST_LINE = 1
FIELD_PLAYER_CONTROL = 0x7E011F  # nonzero while the pad walks the party (tools/movies/addresses.py)
# Frames between looks, how far a place is looked for, and how many looks the pad must
# have walked the party there: the field hands the pad over while the map still loads.
STEP, SEARCH, WALKED = 10, 3000, 6

# (movie, frame, location, whether the place is looked for from the frame on)
PLACES = [
    ("movies/fair.ctm", 6110, 0x002, False),                   # Crono's room
    ("movies/fair.ctm", 7760, 0x001, False),                   # Crono's house, downstairs
    ("movies/fair.ctm", 11000, 0x1F0, False),                  # the world map, 1000 A.D.
    ("movies/fair.ctm", 11810, 0x005, False),                  # Leene Square
    ("movies/saves/battle-heckran.ctm", 2850, 0x031, False),   # Heckran's Cave
    ("movies/saves/battle-heckran.ctm", 2850, 0x02F, True),    # and further in, before its fights
]


def read_png(path: Path):
    """Rows of (r, g, b) from an 8-bit RGB PNG with no filtering, as
    ct_native writes them (frontend/media.c)."""
    data = path.read_bytes()
    at, idat, width, height = 8, b"", 0, 0
    while at < len(data):
        n, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            width, height = struct.unpack_from(">II", body)
        elif kind == b"IDAT":
            idat += body
        at += 12 + n
    raw = zlib.decompress(idat)
    stride = 1 + 3 * width
    return [[tuple(raw[y * stride + 1 + 3 * x:y * stride + 4 + 3 * x]) for x in range(width)] for y in range(height)]


def shown(word: int):
    """A BGR555 color as the native renderer shows it at full brightness:
    packed as RGB565, green's low bit its top bit, then widened."""
    r, g, b = word & 31, word >> 5 & 31, word >> 10 & 31
    g6 = g << 1 | g >> 4
    return (r << 3 | r >> 2, g6 << 2 | g6 >> 4, b << 3 | b >> 2)


def sram_for(movie: Path):
    """The cartridge save a movie starts from, by its CRC-32, or None."""
    crc = next((int(line.split()[-1], 16) for line in movie.read_text().splitlines()
                if line.startswith("# sram crc32")), None)
    if crc is None:
        return None
    for path in sorted((ROOT / "saves").rglob("*")):
        if path.suffix.lower() == ".srm" and path.stat().st_size == 8192 and zlib.crc32(path.read_bytes()) == crc:
            return path
    raise SystemExit(f"{movie}: no save with CRC-32 {crc:08X} under saves/")


class Drive:
    """ct_native --drive, the route protocol (frontend/route_driver.h)."""

    def __init__(self, binary: str, pack: Path, sram):
        args = [str(Path(binary).resolve()), "--rom", str(ROM), "--assets", str(pack), "--drive"]
        if sram:
            args += ["--sram", str(sram)]
        self.p = subprocess.Popen(args, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True)

    def cmd(self, line: str) -> str:
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        answer = self.p.stdout.readline().strip()
        if not answer.startswith("ok"):
            raise SystemExit(f"ct_native --drive: {line!r} answered {answer!r}")
        return answer[2:].strip()

    def words(self, at: int, n: int):
        b = bytes.fromhex(self.cmd(f"peek {at:06x} {2 * n}"))
        return [b[2 * k] | b[2 * k + 1] << 8 for k in range(n)]

    def close(self):
        self.cmd("quit")
        self.p.wait(timeout=60)


def origins(drive: Drive, location: int, scroll):
    """The map pixel each of BG1 and BG2 shows at the picture's top left:
    on a world map each plane's top left tile and its fine scroll; in a
    location the screen's top left tile and BG1's fine scroll, BG2 where
    its scroll differs from BG1's."""
    if location >= FIRST_WORLD_MAP:
        columns, rows = drive.words(PLANE_COLUMN, 2), drive.words(PLANE_ROW, 2)
        return {layer: (columns[k] * TILE + (scroll[2 * k] & 7), rows[k] * TILE + (scroll[2 * k + 1] & 7) + FIRST_LINE)
                for k, layer in enumerate(("bg1", "bg2"))}
    edges = bytes.fromhex(drive.cmd(f"peek {SCREEN_EDGES:06x} 6"))
    left = edges[0] * TILE + (scroll[0] & 7)
    top = edges[4] * TILE + (scroll[1] & 7) + FIRST_LINE
    return {layer: (left + scroll[2 * k] - scroll[0], top + scroll[2 * k + 1] - scroll[1])
            for k, layer in enumerate(("bg1", "bg2"))}


def compare(place, layers, shot, colors):
    """The pixels where the map drawn from the texts, in the game's colors,
    differs from the shot; where every layer is clear the shot shows the
    backdrop, which must be one color."""
    drawn = place.picture(layers, WIDTH, HEIGHT, lambda p, c: colors[16 * p + c])
    clear = {shot[y][x] for y in range(HEIGHT) for x in range(WIDTH) if drawn[y][x] is None}
    backdrop = clear.pop() if len(clear) == 1 else None
    wrong = [(x, y, shot[y][x], backdrop if drawn[y][x] is None else shown(drawn[y][x]))
             for y in range(HEIGHT) for x in range(WIDTH)
             if shot[y][x] != (backdrop if drawn[y][x] is None else shown(drawn[y][x]))]
    return wrong, drawn, backdrop


def find_place(drive: Drive, movie: Path, frame: int, location: int, look: bool):
    """Play the movie to the place: to `frame`, or with `look` on from it to
    where the pad has walked the party in `location` for WALKED looks. The
    location it ends in, or None where a looked-for place isn't within SEARCH."""
    if not look:
        drive.cmd(f"movie {movie} {frame}")
        return drive.words(LOCATION_ID, 1)[0] & 0x1FF
    walked = 0
    for at in range(frame, frame + SEARCH, STEP):
        drive.cmd(f"movie {movie} {at}")
        there = drive.words(LOCATION_ID, 1)[0] & 0x1FF
        walking = bytes.fromhex(drive.cmd(f"peek {FIELD_PLAYER_CONTROL:06x} 1"))[0]
        walked = walked + 1 if there == location and walking else 0
        if walked == WALKED:
            return there
    return None


def check_place(drive: Drive, pack: Path, location: int, out: Path, keep: bool) -> int:
    place = place_at(pack.with_suffix(".maps"), location)
    failures = 0
    for mask, name in ((BG1, "bg1"), (BG2, "bg2"), (BG1 | BG2, "bg1+bg2")):
        path = out / f"location_{location:03x}_{name}_game.png"
        answer = drive.cmd(f"layershot {mask:x} {path}").split()
        scroll = [int(v) for v in answer[:8]]
        colors = [int(answer[8][4 * k:4 * k + 4], 16) for k in range(256)]
        at = origins(drive, location, scroll)
        layers = [(layer, at[layer]) for k, layer in ((1, "bg2"), (0, "bg1")) if mask >> k & 1]
        wrong, drawn, backdrop = compare(place, layers, read_png(path), colors)
        if keep:
            rows = [[(backdrop or (0, 0, 0)) if c is None else shown(c) for c in row] for row in drawn]
            (out / f"location_{location:03x}_{name}_text.png").write_bytes(png(WIDTH, HEIGHT, rows))
        if wrong:
            failures += 1
            x, y, got, want = wrong[0]
            print(f"map_pictures: location ${location:03X} {name} at {layers}: {len(wrong)} pixels differ, "
                  f"the first at {x},{y}: the game {got}, the text {want}")
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ct_native")
    ap.add_argument("--keep", type=Path, help="keep the shots and the maps drawn from the texts here")
    args = ap.parse_args(argv)
    if not ROM.exists():
        print("map_pictures: no ROM staged, the maps not compared with the game's pictures")
        return 0
    work = Path(tempfile.mkdtemp(prefix="ct-map-pictures-"))
    out = args.keep or work
    out.mkdir(parents=True, exist_ok=True)
    failures = 0
    try:
        pack = work / "pack.assets"
        movies = {}
        for movie, frame, location, look in PLACES:
            movies.setdefault(movie, []).append((frame, location, look))
        for movie, places in movies.items():
            sram = sram_for(ROOT / movie)
            if sram:
                shutil.copy(sram, work / "start.srm")
            drive = Drive(args.ct_native, pack, work / "start.srm" if sram else None)
            try:
                for frame, location, look in places:
                    there = find_place(drive, ROOT / movie, frame, location, look)
                    if there != location:
                        print(f"map_pictures: {movie} from frame {frame} is in location "
                              f"{'nowhere walked' if there is None else f'${there:03X}'}, not ${location:03X}")
                        failures += 1
                    else:
                        failures += check_place(drive, pack, location, out, args.keep is not None)
            finally:
                drive.close()
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
    print(f"map_pictures: {len(PLACES)} places, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

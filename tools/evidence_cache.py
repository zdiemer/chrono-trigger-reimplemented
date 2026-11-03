"""A cache of the passing results of the gate's evidence runs.

The evidence the suite makes with --evidence (tools/dc_evidence.py's library
runs, tools/uncompared_evidence.py's isolated comparisons, tools/unreached_fuzz.py's
fuzz trials) is a pure function of what the program run was built from and what
it was given. A result whose inputs are all unchanged since it last passed is
taken from here rather than made again; anything else runs. A result that
failed is never stored, so a failure is always made again.

A result's key is the SHA-256 of a JSON object naming every input it depends on:

- the built program it ran, by the SHA-256 of its bytes (`binary_digest`).
  That is the C of game/, runtime/ and dev/, the C generated from the ROM and
  the generator that wrote it, the build's options and the compiler, all at
  once, and nothing else: the program reads no environment variable and no
  file it isn't named on its command line. A Windows executable's link time
  and checksum are zeroed first: the linker stamps both at every link, and a
  MinGW relink of the same objects differs from the last only there. The
  bytes are the key rather than
  the sources because a key of the sources would have to list every input of
  the build, and a missing one would be a stale hit; the bytes can only miss
  too often. A tree that only touches tools/ or docs/ keeps its binary and
  its hits.
- the ROM, by the SHA-256 of its bytes;
- the movie or game state, by the SHA-256 of its text with CRLF made LF (a
  checkout's line endings don't change what it plays) or of its bytes; a
  movie that starts from a cartridge save names the save by its CRC, which
  the program checks before it plays, so the save is in the movie's bytes;
- the recipe: the run's command line with its paths taken out, the frame
  count, the sites, entries and dispatches it poisons or checks;
- the Python that judges the run (the tool itself and tools/trace.py where
  it diffs traces), by the SHA-256 of its text, and a SCHEMA number to bump
  when what a stored result means changes.

The entries live as one JSON file each in the build directory, beside the
binary (`<build>/evidence-cache/<tool>/<key>.json`), never in the repository.
`--no-cache` on each tool, `--no-evidence-cache` on the suite, or the
environment variable CT_EVIDENCE_CACHE=off ignores the cache: nothing is read
from it, and what passes is still written.
"""

import hashlib
import json
import os
import pathlib
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ENV = "CT_EVIDENCE_CACHE"
SCHEMA = 1

_digests = {}


def _cached_digest(path: pathlib.Path, kind: str, make) -> str:
    path = pathlib.Path(path).resolve()
    stat = path.stat()
    memo = (kind, str(path), stat.st_size, stat.st_mtime_ns)
    if memo not in _digests:
        _digests[memo] = make(path)
    return _digests[memo]


def file_digest(path) -> str:
    """The SHA-256 of a file's bytes (a binary, the ROM, a game state)."""
    def make(p):
        h = hashlib.sha256()
        with p.open("rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()
    return _cached_digest(path, "bytes", make)


def pe_link_stamps(data: bytes) -> list:
    """The offsets of a Windows executable's link time and checksum, which the
    linker writes afresh at every link: (offset, size) pairs, none for a file
    that isn't one (ELF and Mach-O carry no such stamp)."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        return []
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if data[pe:pe + 4] != b"PE" + bytes(2) or len(data) < pe + 24 + 68:
        return []
    return [(pe + 8, 4), (pe + 24 + 64, 4)]   # COFF TimeDateStamp, optional header CheckSum


def binary_digest(path) -> str:
    """The SHA-256 of a built program's bytes, its link time and checksum
    zeroed: two links of the same objects then hash the same (checked for
    the MinGW build: a relink differs from the last only there), and nothing
    the program does reads either."""
    def make(p):
        data = bytearray(p.read_bytes())
        for offset, size in pe_link_stamps(data):
            data[offset:offset + size] = bytes(size)
        return hashlib.sha256(data).hexdigest()
    return _cached_digest(path, "program", make)


def text_digest(path) -> str:
    """The SHA-256 of a text file's bytes with CRLF made LF (a movie, a tool)."""
    return _cached_digest(path, "text", lambda p: hashlib.sha256(p.read_bytes().replace(b"\r\n", b"\n")).hexdigest())


def tools_digest(*paths) -> dict:
    """The judging Python's digests, by path relative to the repository."""
    return {pathlib.Path(p).resolve().relative_to(ROOT).as_posix(): text_digest(p) for p in paths}


def make_key(**parts) -> str:
    """The key of a result: every input it depends on, named."""
    return hashlib.sha256(json.dumps(dict(parts, schema=SCHEMA), sort_keys=True, default=str).encode()).hexdigest()


def disabled_by_env() -> bool:
    return os.environ.get(ENV, "").strip().lower() in ("off", "0", "no", "false")


def default_dir(binary, tool: str) -> pathlib.Path:
    """Beside the binary, in its (gitignored) build directory."""
    return pathlib.Path(binary).resolve().parent / "evidence-cache" / tool


class Cache:
    """Passing results by key. `read` False ignores what is stored."""

    def __init__(self, directory, read: bool = True):
        self.directory = pathlib.Path(directory)
        self.read = read and not disabled_by_env()
        self.hits = self.misses = 0

    def get(self, key: str):
        path = self.directory / (key + ".json")
        if self.read and path.exists():
            try:
                stored = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                stored = None
            if isinstance(stored, dict) and stored.get("key") == key:
                self.hits += 1
                return stored["result"]
        self.misses += 1
        return None

    def put(self, key: str, result) -> None:
        """Store a passing result; the caller never stores a failing one."""
        self.directory.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=self.directory, suffix=".tmp")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(dict(key=key, result=result), f, sort_keys=True)
        os.replace(tmp, self.directory / (key + ".json"))

    def summary(self) -> str:
        return "%d from the cache, %d made" % (self.hits, self.misses)

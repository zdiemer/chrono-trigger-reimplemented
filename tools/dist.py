#!/usr/bin/env python3
"""Build an optimized ct_play into dist/ (see docs/PLAYING.md).

    python3 tools/dist.py                        # configure build/dist, build, install to dist/
    python3 tools/dist.py --strict               # a player without the interpreter linked at all
    python3 tools/dist.py -- -DCMAKE_PREFIX_PATH=<SDL2 prefix>

Configures a Release build in build/dist, builds the ct_play target, and
installs it (stripped) into dist/. Arguments after `--` go to the CMake
configure step. dist/ is never committed, and existing files in it (saves next
to a ROM kept there) are left alone apart from the binaries being replaced.

Then it checks that dist/ runs on a machine that never built it, from the
binaries' own import lists rather than a list of what they should need. On
Windows every DLL ct_play.exe loads, and every DLL those load, is either part
of Windows (in the system directory, or an API set) or copied into dist/ from
PATH (SDL2.dll, and whatever runtime DLLs the toolchain linked); one that can't
be found stops it. On Linux and macOS it lists the shared libraries the player
needs from outside the base system (SDL2), which a player's machine installs
from its package manager; bundling them there isn't done.

The player runs the code generated from the ROM (CT_ROM, roms/ct-usa.sfc by
default) without the 65816 interpreter (docs/PLAYING.md, "Without the
interpreter"): by default the interpreter is linked only as a fallback for
instructions nothing was generated for, which the player logs to
<rom name>.misses.txt (CT_PLAYER_INTERPRETER=FALLBACK); --strict leaves it out,
and such an instruction stops the game (OFF); --interpreter builds the
development player with every --native mode (ON).
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build" / "dist"
DIST_DIR = REPO_ROOT / "dist"


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def find_on_path(name: str) -> Path | None:
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = Path(d) / name
        if d and p.is_file():
            return p
    return None


def pe_imports(path: Path) -> list[str]:
    """The DLL names a PE file imports, normally and delay-loaded."""
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path} isn't a PE file")
    nsections = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dirs = opt + (112 if magic == 0x20B else 96)
    sections = []
    for i in range(nsections):
        at = opt + opt_size + 40 * i
        vsize, va, raw_size, raw = struct.unpack_from("<IIII", data, at + 8)
        sections.append((va, max(vsize, raw_size), raw))

    def offset(rva: int) -> int:
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError(f"{path}: RVA {rva:#x} is in no section")

    def name_at(rva: int) -> str:
        at = offset(rva)
        return data[at:data.index(b"\0", at)].decode("ascii")

    names = []
    for index, entry_size, name_field in ((1, 20, 12), (13, 32, 4)):   # imports, delay-load imports
        rva, size = struct.unpack_from("<II", data, dirs + 8 * index)
        if not rva:
            continue
        at = offset(rva)
        while True:
            name_rva = struct.unpack_from("<I", data, at + name_field)[0]
            if not name_rva:
                break
            names.append(name_at(name_rva))
            at += entry_size
    return names


def windows_system_dll(name: str) -> bool:
    lower = name.lower()
    if lower.startswith(("api-ms-win-", "ext-ms-")):
        return True
    system = Path(os.environ.get("SystemRoot") or os.environ.get("windir", "")) / "System32"
    return (system / name).is_file()


def bundle_windows_dlls(dist: Path, search: list[Path]) -> list[str]:
    """Copy every non-system DLL the binaries in dist need into it. Returns
    the DLLs that couldn't be found."""
    missing = []
    queue = [p for p in dist.iterdir() if p.suffix.lower() in (".exe", ".dll")]
    seen = {p.name.lower() for p in queue}
    while queue:
        binary = queue.pop()
        for name in pe_imports(binary):
            if name.lower() in seen or windows_system_dll(name):
                continue
            seen.add(name.lower())
            source = next((d / name for d in search if (d / name).is_file()), None)
            if not source:
                missing.append(f"{name} (needed by {binary.name})")
                continue
            target = dist / name
            shutil.copy2(source, target)
            print(f"copied {name} (needed by {binary.name}) from {source.parent}")
            queue.append(target)
    return missing


def list_unix_libraries(binary: Path) -> None:
    """Print the shared libraries a Linux or macOS player needs beyond the base system."""
    if sys.platform == "darwin":
        out = subprocess.run(["otool", "-L", str(binary)], capture_output=True, text=True).stdout
        libs = [line.split()[0] for line in out.splitlines()[1:] if line.strip()]
        extra = [lib for lib in libs if not lib.startswith(("/usr/lib/", "/System/"))]
    else:
        out = subprocess.run(["ldd", str(binary)], capture_output=True, text=True).stdout
        libs = [line.split()[0] for line in out.splitlines() if "=>" in line]
        base = ("libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux", "linux-vdso",
                "libgcc_s.so")
        extra = [lib for lib in libs if not lib.startswith(base)]
    print(f"{binary.name} needs, beyond the base system: {', '.join(extra) or 'nothing'}")
    if extra:
        print("  a player's machine installs these itself (SDL2 from its package manager); dist/ doesn't bundle them")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--strict", dest="interpreter", action="store_const", const="OFF",
                      help="don't link the interpreter: an instruction without generated code stops the game")
    mode.add_argument("--interpreter", dest="interpreter", action="store_const", const="ON",
                      help="link and use the interpreter (every --native mode), as the development build does")
    ap.set_defaults(interpreter="FALLBACK")
    ap.add_argument("cmake_args", nargs="*", help="extra CMake configure arguments (after --)")
    args = ap.parse_args()

    if not shutil.which("cmake"):
        sys.exit("cmake not found on PATH")
    configure = ["cmake", "-S", str(REPO_ROOT), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release",
                 f"-DCT_PLAYER_INTERPRETER={args.interpreter}"]
    if shutil.which("ninja") and not (BUILD_DIR / "CMakeCache.txt").exists():
        configure += ["-G", "Ninja"]
    run(configure + args.cmake_args)

    targets = BUILD_DIR / "CMakeFiles" / "TargetDirectories.txt"
    if targets.exists() and "ct_play" not in targets.read_text(errors="replace"):
        sys.exit("ct_play isn't configured: SDL2 wasn't found (install its development package, "
                 "or pass -- -DCMAKE_PREFIX_PATH=<SDL2 prefix>)")

    run(["cmake", "--build", str(BUILD_DIR), "--config", "Release", "--target", "ct_play"])
    if args.interpreter != "ON" and not (BUILD_DIR / "generated" / "generated.stamp").exists():
        sys.exit("no blocks were generated (no ROM at CT_ROM, or no Python 3.11+), and a player without the "
                 "interpreter can't run without them: pass -- -DCT_ROM=<rom>, or --interpreter")
    DIST_DIR.mkdir(exist_ok=True)
    run(["cmake", "--install", str(BUILD_DIR), "--config", "Release", "--prefix", str(DIST_DIR), "--strip"])

    if sys.platform == "win32":
        # Where the build found its libraries: PATH, and next to the compiler.
        search = [Path(d) for d in os.environ.get("PATH", "").split(os.pathsep) if d]
        cache = BUILD_DIR / "CMakeCache.txt"
        for line in cache.read_text(errors="replace").splitlines() if cache.exists() else []:
            if line.startswith("CMAKE_C_COMPILER:"):
                search.insert(0, Path(line.split("=", 1)[1]).parent)
        missing = bundle_windows_dlls(DIST_DIR, search)
        if missing:
            sys.exit("cannot find " + ", ".join(missing) + ": put its directory on PATH and run again")
        print("every DLL ct_play.exe loads is in dist/ or part of Windows")
    else:
        list_unix_libraries(DIST_DIR / "ct_play")

    for p in sorted(DIST_DIR.iterdir()):
        if p.is_file() and p.suffix.lower() in ("", ".exe", ".dll", ".so", ".dylib"):
            print(f"  {p.relative_to(REPO_ROOT)} ({p.stat().st_size:,} bytes)")
    print(f"ct_play built with CT_PLAYER_INTERPRETER={args.interpreter} (ct_play --help says what that means)")


if __name__ == "__main__":
    main()

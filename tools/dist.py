#!/usr/bin/env python3
"""Build an optimized ct_play into dist/ (see docs/PLAYING.md).

    python3 tools/dist.py                        # configure build/dist, build, install to dist/
    python3 tools/dist.py -- -DCMAKE_PREFIX_PATH=<SDL2 prefix>

Configures a Release build in build/dist, builds the ct_play target, and
installs it (stripped) into dist/ together with the SDL2 runtime library on
Windows. Arguments after `--` go to the CMake configure step. dist/ is never
committed, and existing files in it (saves next to a ROM kept there) are left
alone apart from the binaries being replaced.
"""

import argparse
import os
import shutil
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


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmake_args", nargs="*", help="extra CMake configure arguments (after --)")
    args = ap.parse_args()

    if not shutil.which("cmake"):
        sys.exit("cmake not found on PATH")
    configure = ["cmake", "-S", str(REPO_ROOT), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release"]
    if shutil.which("ninja") and not (BUILD_DIR / "CMakeCache.txt").exists():
        configure += ["-G", "Ninja"]
    run(configure + args.cmake_args)

    targets = BUILD_DIR / "CMakeFiles" / "TargetDirectories.txt"
    if targets.exists() and "ct_play" not in targets.read_text(errors="replace"):
        sys.exit("ct_play isn't configured: SDL2 wasn't found (install its development package, "
                 "or pass -- -DCMAKE_PREFIX_PATH=<SDL2 prefix>)")

    run(["cmake", "--build", str(BUILD_DIR), "--config", "Release", "--target", "ct_play"])
    DIST_DIR.mkdir(exist_ok=True)
    run(["cmake", "--install", str(BUILD_DIR), "--config", "Release", "--prefix", str(DIST_DIR), "--strip"])

    if sys.platform == "win32" and not (DIST_DIR / "SDL2.dll").exists():
        dll = find_on_path("SDL2.dll")
        if dll:
            shutil.copy2(dll, DIST_DIR / "SDL2.dll")
            print(f"copied {dll.name}")
        else:
            print("warning: SDL2.dll not found on PATH; copy it next to ct_play.exe", file=sys.stderr)

    for p in sorted(DIST_DIR.iterdir()):
        if p.is_file() and p.suffix.lower() in ("", ".exe", ".dll", ".so", ".dylib"):
            print(f"  {p.relative_to(REPO_ROOT)} ({p.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()

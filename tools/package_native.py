#!/usr/bin/env python3
"""Package an already built ct_native, its host libraries (Windows DLLs, macOS
dylibs; Linux players install SDL2 themselves) and player instructions.

No ROM, generated code, asset pack, save or other build output is copied.
The destination must be empty so an old local pack cannot enter an artifact.
"""
import argparse
import os
from pathlib import Path
import shutil
import sys

from dist import bundle_macos_dylibs, bundle_windows_dlls, list_unix_libraries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("output must be empty (protects against bundling local game data)")
    suffix = ".exe" if os.name == "nt" else ""
    source = args.build_dir / ("ct_native" + suffix)
    if not source.is_file():
        parser.error(f"missing player: {source}")
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.output / source.name
    shutil.copy2(source, binary)
    shutil.copy2(Path(__file__).resolve().parents[1] / "docs" / "NATIVE-README.txt", args.output / "README.txt")
    if os.name == "nt":
        search = [Path(p) for p in os.environ.get("PATH", "").split(os.pathsep) if p]
        missing = bundle_windows_dlls(args.output, search)
        if missing:
            sys.exit("missing runtime libraries: " + ", ".join(missing))
    elif sys.platform == "darwin":
        missing = bundle_macos_dylibs(args.output)
        if missing:
            sys.exit("cannot bundle: " + ", ".join(missing))
    else:
        list_unix_libraries(binary)


if __name__ == "__main__":
    main()

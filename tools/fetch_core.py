#!/usr/bin/env python3
"""Download SNES libretro cores from the libretro buildbot into cores/.

    python3 tools/fetch_core.py                  # snes9x and bsnes2014_accuracy
    python3 tools/fetch_core.py bsnes2014_accuracy

snes9x is the trace oracle; bsnes2014_accuracy is an accuracy cross-check
(see docs/ORACLE.md for why). Cores are third-party software under their own
licenses (bsnes: GPLv3, snes9x: non-commercial). They are downloaded for local
use only and are never committed or redistributed by this project.
"""

import argparse
import io
import platform
import sys
import urllib.request
import zipfile
from pathlib import Path

BUILDBOT = "https://buildbot.libretro.com/nightly"
CORES = ["snes9x", "bsnes2014_accuracy"]

REPO_ROOT = Path(__file__).resolve().parent.parent
CORES_DIR = REPO_ROOT / "cores"


def host() -> tuple[str, str]:
    """Return (buildbot path, shared library suffix) for this machine."""
    machine = platform.machine().lower()
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "arm64", "aarch64": "aarch64"}.get(machine)
    if arch is None:
        sys.exit(f"unsupported architecture {machine!r}")
    if sys.platform == "win32":
        return f"windows/{arch}", ".dll"
    if sys.platform == "darwin":
        return f"apple/osx/{'arm64' if arch in ('arm64', 'aarch64') else 'x86_64'}", ".dylib"
    if sys.platform.startswith("linux"):
        return f"linux/{'armv8' if arch in ('arm64', 'aarch64') else arch}", ".so"
    sys.exit(f"unsupported platform {sys.platform!r}")


def fetch(core: str) -> Path:
    path, suffix = host()
    name = f"{core}_libretro{suffix}"
    url = f"{BUILDBOT}/{path}/latest/{name}.zip"
    print(f"fetching {url}")
    with urllib.request.urlopen(url) as r:
        payload = r.read()
    with zipfile.ZipFile(io.BytesIO(payload)) as z:
        data = z.read(name)
    CORES_DIR.mkdir(exist_ok=True)
    out = CORES_DIR / name
    out.write_bytes(data)
    print(f"  -> {out} ({len(data):,} bytes)")
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cores", nargs="*", metavar="core", help=f"one of {', '.join(CORES)} (default: all)")
    args = ap.parse_args()
    for core in args.cores:
        if core not in CORES:
            ap.error(f"unknown core {core!r}; expected one of {', '.join(CORES)}")
    for core in args.cores or CORES:
        fetch(core)


if __name__ == "__main__":
    main()

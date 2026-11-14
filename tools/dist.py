#!/usr/bin/env python3
"""Package ct_native and its host libraries into dist/; no cartridge data is bundled."""
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


MACOS_SYSTEM = ("/usr/lib/", "/System/")
# Libraries a bundled library loads by name at run time rather than links:
# Homebrew's SDL2 is sdl2-compat, which looks for libSDL3.dylib beside the executable
# before anywhere else.
MACOS_LOADED_LATER = {"libSDL2-2.0.0.dylib": ["libSDL3.dylib"]}


def macos_imports(binary: Path) -> list[str]:
    """The libraries a Mach-O file links, its own install name left out."""
    out = subprocess.run(["otool", "-L", str(binary)], capture_output=True, text=True, check=True).stdout
    names = [line.split()[0] for line in out.splitlines()[1:] if line.strip()]
    own = subprocess.run(["otool", "-D", str(binary)], capture_output=True, text=True).stdout.splitlines()[1:]
    return [name for name in names if name not in own]


def bundle_macos_dylibs(dist: Path) -> list[str]:
    """Copy every non-system library ct_native loads into dist, each named
    by @executable_path, and sign what changed again (ad hoc), so the player
    runs where Homebrew never was. Returns the libraries that couldn't be found."""
    missing, changed = [], set()
    binary = dist / "ct_native"
    queue, copied = [binary], {}

    def copy(source: Path, needed_by: str) -> Path:
        target = dist / source.name
        shutil.copy2(source.resolve(), target)
        target.chmod(0o755)
        run(["install_name_tool", "-id", "@executable_path/" + source.name, str(target)])
        changed.add(target)
        copied[source.name] = target
        queue.append(target)
        print(f"copied {source.name} (needed by {needed_by}) from {source.resolve().parent}")
        return target

    while queue:
        file = queue.pop()
        for lib in macos_imports(file):
            if lib.startswith(MACOS_SYSTEM) or lib.startswith("@executable_path/"):
                continue
            name = Path(lib).name
            source = Path(lib)
            if name not in copied:
                if not source.is_file():
                    missing.append(f"{lib} (needed by {file.name})")
                    continue
                copy(source, file.name)
                for later in MACOS_LOADED_LATER.get(name, []):
                    found = next((d / later for d in (source.resolve().parent, source.parent,
                                                      Path("/opt/homebrew/lib"), Path("/usr/local/lib"))
                                  if (d / later).is_file()), None)
                    if found and later not in copied:
                        copy(found, name)
            run(["install_name_tool", "-change", lib, "@executable_path/" + name, str(file)])
            changed.add(file)
    for file in sorted(changed):
        run(["codesign", "--force", "--sign", "-", str(file)])
    left = [lib for f in [binary, *copied.values()] for lib in macos_imports(f)
            if not lib.startswith(MACOS_SYSTEM) and not lib.startswith("@executable_path/")]
    return missing + [f"{lib} still linked by path" for lib in left]


def list_unix_libraries(binary: Path) -> None:
    """Print the shared libraries a Linux player needs beyond the base system,
    which its machine installs from its package manager (SDL2: libsdl2-2.0-0)."""
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
    ap.add_argument("cmake_args", nargs="*", help="extra CMake configure arguments (after --)")
    args = ap.parse_args()

    if not shutil.which("cmake"):
        sys.exit("cmake not found on PATH")
    configure = ["cmake", "-S", str(REPO_ROOT), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release"]
    if shutil.which("ninja") and not (BUILD_DIR / "CMakeCache.txt").exists():
        configure += ["-G", "Ninja"]
    run(configure + args.cmake_args)

    targets = BUILD_DIR / "CMakeFiles" / "TargetDirectories.txt"
    if targets.exists() and "ct_native" not in targets.read_text(errors="replace"):
        sys.exit("ct_native isn't configured: SDL2 wasn't found (install its development package, "
                 "or pass -- -DCMAKE_PREFIX_PATH=<SDL2 prefix>)")

    run(["cmake", "--build", str(BUILD_DIR), "--config", "Release", "--target", "ct_native", "-j", "2"])
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
        print("every DLL ct_native.exe loads is in dist/ or part of Windows")
    elif sys.platform == "darwin":
        missing = bundle_macos_dylibs(DIST_DIR)
        if missing:
            sys.exit("cannot bundle " + ", ".join(missing))
        print("every library ct_native loads is in dist/ or part of macOS")
    else:
        list_unix_libraries(DIST_DIR / "ct_native")

    for p in sorted(DIST_DIR.iterdir()):
        if p.is_file() and p.suffix.lower() in ("", ".exe", ".dll", ".so", ".dylib"):
            print(f"  {p.relative_to(REPO_ROOT)} ({p.stat().st_size:,} bytes)")
    print("ct_native packaged in dist/")


if __name__ == "__main__":
    main()

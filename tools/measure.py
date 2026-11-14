"""Memory, stacks and per-frame work against small targets' budgets (issue #364;
docs/PLATFORM.md, "Memory, stacks and per-frame work").

Runs ct_native --measure (frontend/measure.h) headless over movies that put
each mode on screen, takes the largest of each figure across them, and adds
the game library's static data (`size`). It prints the measures and a
comparison with the SNES's and the GBA's memory and clock.

    python3 tools/measure.py --ct-native build/main/ct_native [--movies NAME...] [--check]

--check (the ctest) runs one short movie and only checks the report is whole.
Without a staged ROM (roms/ct-usa.sfc) it says so and passes.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, "roms", "ct-usa.sfc")

# Movies that between them hold each mode: the attract sequence (the field,
# battles, menus and scenes), a new game, a battle, the menus, the field
# walked, and late scenes (the Epoch into Lavos, an ending).
MOVIES = ["attract-demo", "new-game", "gato-battle", "menu", "field", "saves/events-epoch-lavos",
          "saves/battle-magus", "saves/endings-telepod-lavos"]
CHECK_MOVIES = ["menu"]

KB = 1024
SNES = {"name": "SNES", "work_ram": 128 * KB, "video_ram": 64 * KB, "fast_ram": 0, "mhz": 3.58}
GBA = {"name": "GBA", "work_ram": 256 * KB, "video_ram": 96 * KB, "fast_ram": 32 * KB, "mhz": 16.78}
# Instructions a frame each target retires, roughly: the 65816 averages about
# four cycles an instruction; the ARM7TDMI runs Thumb from the cartridge or
# EWRAM at about half an instruction a cycle once wait states are counted.
# The host's are its nanoseconds times host_ipc_ghz (a modern desktop core at
# about 4 GHz retiring about 3 an cycle: 12 a nanosecond), an estimate the
# report says it is: x86 instructions aren't Thumb's or the 65816's.
FRAME_HZ = 60.0988
TARGET_IPC = {"SNES": 0.25, "GBA": 0.5}

LINE = re.compile(r"^measure: (\w+) (.*)$")


def frames_of(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            match = re.match(r"#\s*frames\s+(\d+)", line)
            if match:
                return int(match.group(1))
    raise SystemExit("%s has no frames line" % path)


def run_movie(native, name):
    path = os.path.join(ROOT, "movies", name + ".ctm")
    command = [native, "--rom", ROM, "--headless", "--mute", "--movie", path, "--frames", str(frames_of(path)),
               "--measure"]
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    report = {"size": {}, "part": {}, "stack": {}, "mode": {}}
    for line in result.stdout.splitlines():
        match = LINE.match(line)
        if not match:
            continue
        kind, rest = match.groups()
        words = rest.split()
        if kind in ("size", "part"):
            report[kind][words[0]] = int(words[1])
        elif kind == "stack":
            report["stack"][int(words[0])] = int(words[1])
        elif kind == "mode":
            values = dict(zip(words[1::2], words[2::2]))
            report["mode"][words[0]] = {key: int(value) for key, value in values.items()}
    return result.returncode, report


def merge(total, report, movie):
    for kind in ("size", "part"):
        for key, value in report[kind].items():
            total[kind][key] = max(total[kind].get(key, 0), value)
    for slot, value in report["stack"].items():
        total["stack"][slot] = max(total["stack"].get(slot, 0), value)
    for mode, values in report["mode"].items():
        have = total["mode"].setdefault(mode, {"frames": 0})
        have["frames"] += values["frames"]
        if values["heaviest_ns"] >= have.get("heaviest_ns", 0):
            for key in ("heaviest_ns", "game", "sound", "render"):
                have[key] = values[key]
            have["movie"] = movie
        for key in ("p99_ns", "game_p99", "sound_p99", "render_p99", "game_max", "sound_max", "render_max", "resources"):
            have[key] = max(have.get(key, 0), values[key])


def optimized_across_files(native):
    """Whether the build optimized across files, whose library holds the
    compiler's own form rather than code `size` can count."""
    cache = os.path.join(os.path.dirname(native), "CMakeCache.txt")
    if not os.path.exists(cache):
        return False
    with open(cache, encoding="utf-8", errors="replace") as f:
        return any(re.match(r"CMAKE_INTERPROCEDURAL_OPTIMIZATION:\w+=(ON|TRUE|1)$", line.strip()) for line in f)


def static_data(native):
    """The game library's code and static data, as `size` counts them."""
    library = os.path.join(os.path.dirname(native), "libct_game.a")
    size = shutil.which("size")
    if not size or not os.path.exists(library) or optimized_across_files(native):
        return None
    result = subprocess.run([size, "-t", library], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    for line in result.stdout.splitlines():
        if "(TOTALS)" in line:
            text, data, bss = (int(word) for word in line.split()[:3])
            return {"code_and_constants": text, "data": data, "bss": bss}
    return None


def kib(n):
    return "%.1f KiB" % (n / KB)


def report(total, statics, host_ipc_ghz):
    sizes, parts = total["size"], total["part"]
    lines = ["Lasting allocations:"]
    for key in ("game_state", "game_state_ram", "renderer", "sound_player", "observer", "arena_used", "resources"):
        if key in sizes:
            lines.append("  %-16s %10d  %s" % (key, sizes[key], kib(sizes[key])))
    lines.append("GameState's work RAM by subsystem:")
    for key, value in sorted(parts.items(), key=lambda kv: -kv[1]):
        lines.append("  %-16s %10d  %s" % (key, value, kib(value)))
    if statics:
        lines.append("Game library (size -t): code and constants %s, data %s, bss %s"
                     % (kib(statics["code_and_constants"]), kib(statics["data"]), kib(statics["bss"])))
    lines.append("Fiber stacks (%d bytes each): deepest use by slot %s" % (
        sizes.get("fiber_stack", 0), ", ".join("%d: %d" % kv for kv in sorted(total["stack"].items()))))
    lines.append("Per mode, host ms a frame (99th percentile / heaviest), and the work as a multiple of a")
    lines.append("frame for each target's CPU (the SNES: the game's frame, its sound on its own processor;")
    lines.append("the GBA: the game's and the sound's; the picture is each target's hardware):")
    for mode, v in sorted(total["mode"].items()):
        if mode == "none":
            continue

        def frames_of_work(ns, name):
            budget = (GBA if name == "GBA" else SNES)["mhz"] * 1e6 / FRAME_HZ * TARGET_IPC[name]
            return ns * host_ipc_ghz / budget
        snes = frames_of_work(v["game_p99"], "SNES"), frames_of_work(v["game_max"], "SNES")
        gba = (frames_of_work(v["game_p99"] + v["sound_p99"], "GBA"),
               frames_of_work(v["game_max"] + v["sound_max"], "GBA"))
        lines.append("  %-7s %7d frames  game %.2f / %.2f  sound %.2f / %.2f  render %.2f / %.2f  resources %s"
                     "  GBA %.0fx / %.0fx  SNES %.0fx / %.0fx"
                     % (mode, v["frames"], v["game_p99"] / 1e6, v["game_max"] / 1e6, v["sound_p99"] / 1e6,
                        v["sound_max"] / 1e6, v["render_p99"] / 1e6, v["render_max"] / 1e6, kib(v["resources"]),
                        gba[0], gba[1], snes[0], snes[1]))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ct-native", required=True)
    parser.add_argument("--movies", nargs="*")
    parser.add_argument("--check", action="store_true", help="one short movie: the report is whole")
    parser.add_argument("--host-ipc-ghz", type=float, default=12.0,
                        help="host instructions a nanosecond, for the targets' comparison (default 12)")
    args = parser.parse_args()
    if not os.path.exists(ROM):
        print("measure: no staged ROM (roms/ct-usa.sfc); nothing measured")
        return 0
    movies = args.movies or (CHECK_MOVIES if args.check else MOVIES)
    total = {"size": {}, "part": {}, "stack": {}, "mode": {}}
    for movie in movies:
        status, measured = run_movie(os.path.abspath(args.ct_native), movie)
        if status != 0 or not measured["size"] or not measured["mode"]:
            print("measure: %s: ct_native exited %d with %d size lines and %d modes"
                  % (movie, status, len(measured["size"]), len(measured["mode"])))
            return 1
        merge(total, measured, movie)
    if args.check:
        whole = all(key in total["size"] for key in ("game_state", "renderer", "sound_player", "arena_used")) \
            and total["stack"] and total["part"]
        print("measure: report %s" % ("whole" if whole else "missing figures"))
        # The fibers' stacks are sized from their deepest use with a margin
        # (runtime/port/fiber.c): a run that takes half of one has eaten it.
        deepest, room = max(total["stack"].values(), default=0), total["size"].get("fiber_stack", 0)
        roomy = deepest <= room // 2
        print("measure: the deepest fiber stack %d bytes of %d%s" % (deepest, room, "" if roomy else ": over half"))
        return 0 if whole and roomy else 1
    print(report(total, static_data(os.path.abspath(args.ct_native)), args.host_ipc_ghz))
    return 0


if __name__ == "__main__":
    sys.exit(main())

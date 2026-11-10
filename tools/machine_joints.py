"""What in `game/` still needs the emulated machine to run, counted from the source.

    python3 tools/machine_joints.py              # the totals
    python3 tools/machine_joints.py --files      # per file, most first

The census (`tools/census.py`) counts how often real play still reaches a port
through the machine; this counts the joints in the source that have to go
before the machine can leave the product (docs/PLAN.md, "What done means,
measured", and "The native program, and the registry taken down"). Each row
is a pattern, so a count is an estimate of sites, not of work: one entry
wrapper is one return through the emulated stack, and it leaves with its
subsystem's registry rows rather than one by one.

The second count is of what `ct_native` links: a subsystem whose machine side
is a file of its own leaves it out of the native program (CMakeLists.txt's
`CT_NATIVE_GAME_SOURCES`, as `registry.c` is), so its joints there are the
gate's, not the product's.
"""

import argparse
import collections
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "game"

# (name, pattern, what retires it)
JOINTS = [
    ("registered entries", None, "ordinary functions their callers call"),
    ("held waits' continuations", None, "their main loops run as C with no machine under them"),
    ("returns through the emulated stack", r"\bm_rt[sli]\(", "the entry wrappers deleted"),
    ("stops at the original's addresses", r"\bm_stop_at(?:_own)?\(|\bm_hold_at\(|\bnative_\w*dont_care_at_stop\(",
     "the next step called as C"),
    ("call-outs to entries by address", r"\bcall_out\w*\(|\bnative_(?:yield_|jump_)?call_dc\(",
     "the callee called as C"),
    ("siblings found in the registry", r"\bnative_hand_entry\(", "the sibling called as C"),
    ("time tags that take the machine", r"\bNATIVE_TIME_AS\(", "a tag with no machine"),
    ("register escapes", r"\bvid_snes_\w+\(|\bau_voices_snes_register\(", "a service call"),
    ("video service calls", r"\bvid_(?!snes_)\w+\(", "inventory, including inline value helpers"),
    ("video calls without host backend", None, "the named service implemented on the host"),
    ("cartridge reads by address", None, "named resource accessors serving the store's bytes"),
    ("scaffolding writes", r"\bORIGINAL_EFFECT\(", "its subsystem retiring the gate"),
]

ENTRY = re.compile(r"0x[0-9A-Fa-f]{6}")


NATIVE_LEAVES_OUT = re.compile(r"list\(REMOVE_ITEM CT_NATIVE_GAME_SOURCES ([^)]*)\)")


def native_left_out():
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    return {f for m in NATIVE_LEAVES_OUT.finditer(text) for f in m.group(1).split()}


def sources():
    return sorted(p for p in GAME.rglob("*") if p.suffix in (".c", ".h"))


def missing_video_backends():
    """Public service declarations without a definition in the host backend.

    Inline encoders are already portable, and the explicitly SNES escapes
    have their own row. This measures implementation presence, not pixels.
    """
    header = (ROOT / "runtime/platform/video.h").read_text(encoding="utf-8")
    declared = set(re.findall(r"PF_API\s+\w+\s+(vid_(?!snes_)\w+)\(", header))
    native = "\n".join(p.read_text(encoding="utf-8")
                       for p in (ROOT / "runtime/platform/native").glob("*.c"))
    native = re.sub(r"/\*.*?\*/|//[^\n]*", "", native, flags=re.S)
    implemented = set(re.findall(r"\b(vid_\w+)\([^;{}]*\)\s*\{", native))
    return declared - implemented


def registry_entries():
    """The addresses in the registry's table (game_natives), not in what
    follows it (the retired subsystems' uncompared cells)."""
    text = (GAME / "registry.c").read_text(encoding="utf-8")
    start = text.index("game_natives[] = {")
    return len(ENTRY.findall(text[start:text.index("\n};", start)]))


def held_wait_rows():
    """The held waits' continuations (game_held_waits): machine wrappers the
    runtime runs by a wait's number rather than finds by address, still a
    joint until their main loops run with no machine."""
    text = (GAME / "registry.c").read_text(encoding="utf-8")
    start = text.index("game_held_waits[] = {")
    return len(ENTRY.findall(text[start:text.index("\n};", start)]))


def count(per_file):
    totals = collections.Counter()
    by_file = collections.defaultdict(collections.Counter)
    missing_video = missing_video_backends()
    for path in sources():
        if path.name == "registry.c":
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        n = cartridge_reads(path, text)
        if n:
            name = "cartridge reads by address"
            totals[name] += n
            by_file[path.relative_to(ROOT).as_posix()][name] += n
        missing = sum(len(re.findall(r"\b" + name + r"\(", text)) for name in missing_video)
        if missing:
            name = "video calls without host backend"
            totals[name] += missing
            by_file[path.relative_to(ROOT).as_posix()][name] += missing
        for name, pattern, _ in JOINTS:
            if pattern is None:
                continue
            n = len(re.findall(pattern, text))
            if n:
                totals[name] += n
                by_file[path.relative_to(ROOT).as_posix()][name] += n
    totals["registered entries"] = registry_entries()
    totals["held waits' continuations"] = held_wait_rows()
    return totals, by_file


def cartridge_reads(path, text):
    """Audited cartridge-capable transfer sources, plus reads outside accessors.

    PF_AT is timing metadata, not a source. RAM-only queues (field sprites,
    tile animations, unpacked scene graphics) are not cartridge reads. The
    expressions below are the dynamic sources whose producers also accept ROM;
    fixed ROM sources are recognized by their constants in the data headers.
    This is a source inventory, not whole-program pointer analysis.
    """
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    n = 0
    if path.name.endswith("_data.h") or path.name == "packed_graphics.h":
        return len(re.findall(r"\bm_read(?:8|16|24)\(", text))
    if path.name not in ("assets.c", "decompress.c", "rom.h"):
        n += len(re.findall(r"\b(?:rom_read(?:8|16|24)|rom_data(?:_mode)?)\(", text))
    constants = r"(?:MENU_(?:VRAM_CLEARING_ZERO|CLEARING_ZERO|MAP_VIEW_(?:MORE_)?TILES)|BATTLE_ZERO_BYTE|ZERO_IN_THE_CARTRIDGE)"
    n += len(re.findall(r"PF_GAME\(\s*(?:0x[C-Fc-f][0-9A-Fa-f]{5}u?\b|" + constants + r"\b)", text))
    dynamic = {
        "game/menu/display.c": [r"PF_GAME\(\(uint32_t\)menu_vram_queue_bank", r"PF_GAME\(\(uint32_t\)bank", r"\.source\s*=\s*MENU_CLEARING_ZERO", r"\.source\s*=\s*0xFF0000"],
        "game/battle/display.c": [r"PF_GAME\(l->source\)"],
        "game/battle/uploads.c": [r"PF_GAME\(source\)", r"PF_GAME\(from\)", r"\.source\s*=\s*d\.source"],
        "game/battle/bridge.c": [r"PF_GAME\(from\)"],
        "game/field/display.c": [r"PF_GAME\(field_transfer_source"],
        "game/scene/screen.c": [r"\.source\s*=\s*source\b"],
        "game/event/memory.c": [r"\bwram_read(?:8|16)\(g, address\)"],
    }
    return n + sum(len(re.findall(p, text)) for p in dynamic.get(path.relative_to(ROOT).as_posix(), []))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--files", action="store_true", help="per file, most joints first")
    args = ap.parse_args()
    totals, by_file = count(args.files)
    if args.files:
        for path, c in sorted(by_file.items(), key=lambda kv: -sum(kv[1].values())):
            print(f"{sum(c.values()):6d}  {path}: " + ", ".join(f"{n} {k}" for k, n in c.most_common()))
        return
    left_out = native_left_out()
    native = collections.Counter()
    for path, c in by_file.items():
        if path not in left_out:
            native.update(c)
    print("in game/, and in what ct_native links of it (not " + ", ".join(sorted(left_out)) + "):")
    for name, _, retires in JOINTS:
        print(f"{totals[name]:6d} {native[name]:6d}  {name}  (goes when: {retires})")


if __name__ == "__main__":
    main()

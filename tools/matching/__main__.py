"""Extract, emit and reassemble the ROM, and check it matches (docs/MATCHING.md, MD0 and MD1).

    python3 -m tools.matching --rom roms/ct-usa.sfc --out build/main/matching \\
        [--hints re/hints.toml] [--registry game/registry.c] \\
        [--coverage traces/coverage.txt ...] [--calls traces/calls.txt ...] [--asar auto|off|PATH]

Verifies the ROM (CRC32 and SHA-1), runs the disassembler's analysis in process,
writes the source into OUT/src (rom.asm, bank_XX.asm, data/raw/bank_XX/*.bin),
assembles it with tools/matching/asm.py into OUT/rom.sfc and fails unless that
is the ROM. With asar available it assembles the same source with asar too.
Everything written is ROM-derived and belongs in the build tree only. Files
whose content didn't change aren't rewritten; stale ones are removed.
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
import time
import zlib
from pathlib import Path

from tools.disasm import coverage as coverage_mod
from tools.disasm.closure import closure, report_lines
from tools.disasm import hints as hints_mod
from tools.disasm.analyze import Analyzer
from tools.disasm.memmap import ROM_SIZE, Rom, fmt

from .asm import AsmError, Assembler, write_symbols
from .emit import Emitter, registry_names
from .ramsrc import build_section, check_ram_coverage, check_section

ROM_CRC32 = 0x2D206BF7
ROM_SHA1 = "de5822f4f2f7a55acb8926d4c0eaa63d5d989312"


def write_if_changed(path: Path, content) -> bool:
    raw = content.encode("utf-8") if isinstance(content, str) else bytes(content)
    if path.exists() and path.stat().st_size == len(raw) and path.read_bytes() == raw:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)
    return True


def identify(data: bytes) -> tuple[int, str]:
    return zlib.crc32(data), hashlib.sha1(data).hexdigest()


def first_difference(a: bytes, b: bytes) -> int | None:
    if a == b:
        return None
    n = min(len(a), len(b))
    lo = 0
    step = 0x10000
    while lo < n:
        hi = min(lo + step, n)
        if a[lo:hi] != b[lo:hi]:
            return next(i for i in range(lo, hi) if a[i] != b[i])
        lo = hi
    return n


def run_asar(asar: str, src: Path, out: Path) -> bytes:
    if out.exists():
        out.unlink()
    cmd = [asar, "--no-title-check", "--fix-checksum=off", "rom.asm", str(out.resolve())]
    p = subprocess.run(cmd, cwd=src, capture_output=True, text=True)
    if p.returncode != 0 or not out.exists():
        raise SystemExit(f"asar failed ({p.returncode}):\n{p.stdout}{p.stderr}")
    return out.read_bytes()


def report(st, times: dict, out: Path, cov=None, data_problems=(), an=None, ram=None, checks=()) -> str:
    total = st.insn_bytes + st.db_bytes + st.incbin_bytes
    lines = ["# Matching source", "",
             "| | Bytes | KiB | Share |", "|---|--:|--:|--:|"]
    for label, n in (("Instructions", st.insn_bytes), ("db", st.db_bytes), ("incbin", st.incbin_bytes)):
        lines.append(f"| {label} | {n} | {n / 1024:.1f} | {n / total:.2%} |")
    lines += ["", f"- {st.insns} instructions, {st.db_regions} db regions, {st.incbin_files} incbin files, "
              f"{st.labels} labels",
              f"- names inside an instruction (not placed): {len(st.unplaced)}"
              + "".join(f"\n  - {nm} at {fmt(0xC00000 | o)}" for nm, o in st.unplaced),
              f"- registry addresses that aren't an instruction start: {len(st.registry_not_insn)}"
              + "".join(f"\n  - {fmt(a)}" for a in st.registry_not_insn),
              f"- overlap hints whose other decodes don't end on an instruction: {len(st.overlap_problems)}"
              + "".join(f"\n  - {p}" for p in st.overlap_problems),
              (f"- executed ROM addresses (coverage): {cov.rom}, {cov.ok} instruction starts with compatible "
               f"widths, {len(cov.problems)} not" + "".join(f"\n  - {fmt(a)}: {t}" for a, t in cov.problems)
               + f"\n- executed addresses outside ROM: {cov.ram}") if cov else "- no coverage given",
              f"- [[data]] hints that don't fit their bytes: {len(data_problems)}"
              + "".join(f"\n  - {p}" for p in data_problems),
              f"- transfers into [[data]] regions: {len(an.forced_data) if an else 0}"
              + "".join(f"\n  - {fmt(a)}" for a in sorted(an.forced_data if an else ())),
              "- time: " + ", ".join(f"{k} {v:.1f} s" for k, v in times.items())]
    code_banks = [b for b, (i, d, n) in enumerate(st.per_bank) if i or d]
    untyped = [(a, n) for a, n in st.untyped_blobs if (a >> 16) - 0xC0 in code_banks]
    lines.append(f"- untyped blobs in the {len(code_banks)} banks with code: {len(untyped)}, "
                 f"{sum(n for _, n in untyped)} bytes")
    kinds = sorted({k for c in st.blob_kinds for k in c})
    lines += ["", "| Bank | Instructions | db | incbin | " + " | ".join(kinds) + " |",
              "|---|--:|--:|--:|" + "--:|" * len(kinds)]
    for b, (i, d, n) in enumerate(st.per_bank):
        lines.append(f"| ${0xC0 + b:02X} | {i} | {d} | {n} | "
                     + " | ".join(str(st.blob_kinds[b][k]) for k in kinds) + " |")
    if ram is not None:
        sections, (total, inside, ram_problems, outside) = ram
        lines += ["", "## RAM sections", "", "| Section | Run at | Bytes | From | Instructions | Data | Problems |",
                  "|---|---|--:|---|--:|--:|--:|"]
        for sec, rs in sections:
            lines.append(f"| {sec.name} | {fmt(sec.base)} | {len(sec.image)} | {sec.format} {fmt(sec.source)} | "
                         f"{rs.insn_bytes} | {rs.data_bytes} | {len(rs.problems)} |")
        lines += [f"- {sec.name}: {p}" for sec, rs in sections for p in rs.problems]
        lines += ["", f"- executed RAM addresses: {total}, {inside} in sections, {len(ram_problems)} of those not "
                      f"instruction starts with compatible widths, {len(outside)} in no section"
                  + (f" ({fmt(min(outside))}-{fmt(max(outside))})" if outside else "")]
        lines += [f"  - {fmt(a)}: {t}" for a, t in ram_problems]
    if checks:
        lines += [""] + report_lines(checks)
    lines += ["", "## Untyped blobs in banks with code", ""]
    lines += [f"- {fmt(a)}, {n} bytes" for a, n in untyped] or ["(none)"]
    text = "\n".join(lines) + "\n"
    write_if_changed(out / "report.md", text)
    return text


def main():
    ap = argparse.ArgumentParser(prog="python3 -m tools.matching")
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--hints", type=Path, default=Path("re/hints.toml"))
    ap.add_argument("--registry", type=Path, default=Path("game/registry.c"))
    ap.add_argument("--coverage", type=Path, action="append", default=[])
    ap.add_argument("--calls", type=Path, action="append", default=[],
                    help="call logs from ct_dev --calls: their targets get sub_ labels")
    ap.add_argument("--asar", default="auto", help="asar executable, 'auto' (asar on PATH if any) or 'off'")
    ap.add_argument("--stamp", type=Path, help="file to touch when the check passes (build systems)")
    ap.add_argument("--closure", choices=("check", "report"), default="check",
                    help="closure checks C1-C7 (docs/MATCHING.md, MD4): fail the run when one fails (check, "
                         "the default) or only report them")
    args = ap.parse_args()

    t_start = time.time()
    data = args.rom.read_bytes()
    if len(data) % 0x8000 == 0x200:
        data = data[0x200:]
    crc, sha1 = identify(data)
    if crc != ROM_CRC32 or sha1 != ROM_SHA1 or len(data) != ROM_SIZE:
        sys.exit(f"{args.rom}: CRC32 {crc:08X}, expected {ROM_CRC32:08X} (Chrono Trigger (USA))")

    times = {}
    t = time.time()
    an = Analyzer(Rom(data), hints_mod.load(args.hints), coverage=coverage_mod.load(args.coverage))
    an.run()
    times["analysis"] = time.time() - t

    t = time.time()
    registry = registry_names(args.registry.read_text(encoding="utf-8")) if args.registry.exists() else {}
    calls = set(coverage_mod.load_calls(args.calls)) if args.calls else set()
    em = Emitter(data, an, registry, calls)
    files = em.build()
    cov = em.check_coverage(an.coverage) if an.coverage else None
    data_problems = em.check_data()
    ram_sections = []
    for sec in an.ram.sections:
        ram_files, rs = build_section(sec, an)
        files.update(ram_files)
        ram_sections.append((sec, rs))
    times["emit"] = time.time() - t

    t = time.time()
    src = args.out / "src"
    changed = sum(write_if_changed(src / rel, content) for rel, content in sorted(files.items()))
    wanted = {(src / rel).resolve() for rel in files}
    removed = 0
    for old in sorted(list(src.glob("*.asm")) + list(src.glob("ram/*.asm")) + list(src.glob("data/**/*.bin"))):
        if old.resolve() not in wanted:
            old.unlink()
            removed += 1
    times["write"] = time.time() - t

    t = time.time()
    try:
        res = Assembler().assemble_file(src / "rom.asm")
    except AsmError as e:
        sys.exit(f"matching: assembly failed: {e}")
    for sec, rs in ram_sections:
        check_section(sec, src / "ram" / f"{sec.name}.asm", rs)
    ram_cov = check_ram_coverage(ram_sections, an.coverage)
    times["assemble"] = time.time() - t
    t = time.time()
    checks = closure(an)
    times["closure"] = time.time() - t

    image = bytes(res.image)
    holes = ROM_SIZE - sum(res.written)
    write_if_changed(args.out / "rom.sfc", image)
    write_symbols(res.symbols, args.out / "rom.sym")
    st = em.stats
    print(report(st, times, args.out, cov, data_problems, an, (ram_sections, ram_cov), checks).split("\n\n| Bank")[0])
    for c in checks:
        print(f"closure: {c.name}: {'pass' if c.ok else 'FAIL'}; " + ", ".join(f"{k} {v}" for k, v in c.counts.items()))
    print(f"wrote {changed} files, removed {removed}, into {src}")

    got_crc, got_sha1 = identify(image)
    ok = holes == 0 and got_crc == ROM_CRC32 and got_sha1 == ROM_SHA1
    if not ok:
        off = first_difference(image, data)
        where = res.locate(off) if off is not None else None
        msg = f"matching: MISMATCH: CRC32 {got_crc:08X}, SHA-1 {got_sha1}; {holes} bytes not assembled"
        if off is not None:
            msg += f"; first difference at {fmt(0xC00000 | off)}"
            msg += f" ({where[2]}:{where[3]})" if where else " (no statement wrote it)"
        sys.exit(msg)
    print(f"matching: {args.out / 'rom.sfc'} matches (CRC32 {got_crc:08X}, SHA-1 {got_sha1})")

    ram_problems = sum(len(rs.problems) for _, rs in ram_sections) + len(ram_cov[2]) + len(an.ram.problems)
    if st.overlap_problems or (cov and cov.problems) or data_problems or an.forced_data or ram_problems:
        sys.exit(f"matching: {len(cov.problems) if cov else 0} executed ROM addresses aren't instructions the "
                 f"source can run, {len(st.overlap_problems)} overlap hints and {len(data_problems)} data hints "
                 f"don't fit, {len(an.forced_data)} transfers into data, {ram_problems} RAM section problems "
                 "(see report.md)")
    print(f"matching: {len(ram_sections)} RAM sections assemble to their images")
    failed = [c for c in checks if not c.ok]
    if failed and args.closure == "check":
        sys.exit("matching: closure checks fail: " + "; ".join(f"{c.name}: {c.problems[0]} ({len(c.problems)} problems)"
                                                             for c in failed) + " (see report.md)")

    asar = shutil.which("asar") if args.asar == "auto" else None if args.asar == "off" else args.asar
    if asar:
        t = time.time()
        other = run_asar(asar, src, args.out / "rom-asar.sfc")
        times["asar"] = time.time() - t
        if other != data:
            off = first_difference(other, data)
            where = res.locate(off) if off is not None and off < ROM_SIZE else None
            sys.exit(f"matching: asar output differs ({len(other)} bytes), first at "
                     f"{fmt(0xC00000 | off) if off < ROM_SIZE else hex(off)}"
                     + (f" ({where[2]}:{where[3]})" if where else ""))
        print(f"matching: asar ({asar}) output matches too ({times['asar']:.1f} s)")
    elif args.asar == "auto":
        print("matching: asar not found on PATH, asar check skipped")

    print(f"matching: total {time.time() - t_start:.1f} s")
    if args.stamp:
        args.stamp.parent.mkdir(parents=True, exist_ok=True)
        args.stamp.write_text("", encoding="utf-8")


if __name__ == "__main__":
    main()

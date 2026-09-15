"""Source for code the game runs from RAM (docs/MATCHING.md, MD1).

Each [[ramcode]] section (tools/disasm/ram.py) becomes its own source file,
`ram/<name>.asm`, assembled at its run address with `base` into a separate
image and compared with the section's image: the decompressed stream, the
copied bytes, or what the writing code stores. The ROM source keeps the stream
or the copied bytes as data; these files only prove that the code the analysis
found in RAM, written as instructions, is exactly what the game puts there.

Instruction starts and widths come from the analysis; every instruction is
decoded again from its own section's image, since sections can share addresses
(two installers writing different stubs at $00:0500).
"""

from dataclasses import dataclass, field

from tools.disasm.analyze import Analyzer, describe_widths
from tools.disasm.cpu import Mode, decode
from tools.disasm.memmap import bus_to_offset, fmt
from tools.disasm.ram import RamSection, canonical_ram

from .asm import AsmError, Assembler
from .emit import HEADER, insn_line, note_text, width_note

SCRATCH_ORG = 0xC00000


@dataclass
class RamStats:
    name: str
    base: int
    size: int
    insns: int = 0
    insn_bytes: int = 0
    data_bytes: int = 0
    starts: dict = field(default_factory=dict)       # offset in image -> instruction size
    problems: list = field(default_factory=list)


def section_reader(sec: RamSection):
    def read(addr: int, n: int):
        i = canonical_ram(addr) - sec.base
        if i < 0 or i + n > len(sec.image):
            return None
        return sec.image[i:i + n]
    return read


def build_section(sec: RamSection, an: Analyzer) -> tuple[dict, RamStats]:
    """{relative path: text or bytes} for one section, and its stats. Code in
    the range of a section written over this one (`over`) isn't this section's."""
    st = RamStats(sec.name, sec.base, len(sec.image))
    read = section_reader(sec)
    starts = {}                                   # offset in image -> (widths set)
    for pc, variants in an.code.items():
        if bus_to_offset(pc) is not None:
            continue
        a = canonical_ram(pc)
        if sec.base <= a < sec.end:
            if not an.ram.covered_by_overlay(sec, a):
                starts.setdefault(a - sec.base, set()).update(variants)
    insns = {}                                    # offset -> (insn, widths set)
    for i in sorted(starts):
        ws = starts[i]
        decoded = {decode(read, sec.base + i, m, x) for m, x in ws}
        sizes = {d.size if d else None for d in decoded}
        if None in sizes or len(sizes) != 1:
            if not sec.patch:     # a patch may cut an instruction of the code it patches
                st.problems.append(f"{fmt(sec.base + i)}: doesn't decode to one length in this image")
            continue
        insns[i] = (next(iter(decoded)), ws)
    # Drop instructions another one overlaps (they belong to another section's
    # bytes at the same address, or are an error); keep the earlier.
    clean, end = {}, 0
    for i in sorted(insns):
        if i < end:
            # an instruction start only in a patch of this code, or in this patch
            # only an instruction of the code it patches
            if not an.ram.patched(sec, sec.base + i) and not sec.patch:
                st.problems.append(f"{fmt(sec.base + i)}: overlaps the instruction before it")
            continue
        clean[i] = insns[i]
        end = i + insns[i][0].size
    labels = {}
    for i, (insn, _) in clean.items():
        if insn.mode in (Mode.REL8, Mode.REL16):
            t = insn.branch_target() - sec.base
            labels.setdefault(t, f"loc_{sec.base + t:06X}")
    for addr, kinds in an.labels.items():
        a = canonical_ram(addr)
        if sec.base <= a < sec.end and bus_to_offset(addr) is None and kinds:
            labels[a - sec.base] = f"{'sub' if 'sub' in kinds else 'loc'}_{a:06X}"
    for i in list(labels):
        if i not in clean:                        # inside an instruction, or outside the section
            del labels[i]
    st.starts = {i: insn.size for i, (insn, _) in clean.items()}

    lines = [HEADER, f"; RAM section {sec.name}: {len(sec.image)} bytes run at {fmt(sec.base)}, "
                     f"{sec.format} from {fmt(sec.source)}", "", "hirom", f"org ${SCRATCH_ORG:06X}",
             f"base ${sec.base:06X}"]
    files = {}
    note = None
    i = 0
    size = len(sec.image)
    stops = sorted(set(clean) | set(labels) | {size})
    while i < size:
        if i in labels:
            lines.append(f"{labels[i]}:")
        if i in clean:
            insn, ws = clean[i]
            ref = None
            pc = sec.base + i
            if insn.mode in (Mode.REL8, Mode.REL16):
                t = insn.branch_target() - sec.base
                ref = labels.get(t)
                if ref is None:
                    if not sec.patch:     # a patch branches into the code it patches
                        st.problems.append(f"{fmt(pc)}: branch target isn't an instruction in the section")
                    ref = f"${insn.branch_target():06X}"
            elif (insn.name, insn.mode) in (("JSR", Mode.ABS), ("JMP", Mode.ABS)):
                t = ((pc & 0xFF0000) | insn.operand) - sec.base
                ref = labels.get(t) if t in clean else None
            elif (insn.name, insn.mode) in (("JSL", Mode.LONG), ("JML", Mode.LONG)):
                t = canonical_ram(insn.operand) - sec.base
                ref = labels.get(t) if t in clean and insn.operand == sec.base + t else None
            wn = width_note(ws)
            if wn != note:
                lines.append(note_text(wn))
                note = wn
            lines.append(f"\t{insn_line(insn.name, insn.mode, insn.size, insn.operand, ref):<31} ; {fmt(pc)}")
            st.insns += 1
            st.insn_bytes += insn.size
            i += insn.size
            continue
        nxt = next(s for s in stops if s > i)
        path = f"data/ram/{sec.name}/{sec.base + i:06X}.bin"
        files[path] = sec.image[i:nxt]
        lines.append(f'\tincbin "../{path}" ; {fmt(sec.base + i)}-{fmt(sec.base + nxt - 1)}, {nxt - i} bytes')
        st.data_bytes += nxt - i
        note = None
        i = nxt
    lines += ["base off", ""]
    files[f"ram/{sec.name}.asm"] = "\n".join(lines)
    return files, st


def check_section(sec: RamSection, source_path, st: RamStats):
    """Assemble ram/<name>.asm and compare with the image; problems go into st."""
    try:
        res = Assembler().assemble_file(source_path)
    except AsmError as e:
        st.problems.append(f"assembly failed: {e}")
        return
    off = bus_to_offset(SCRATCH_ORG)
    got = bytes(res.image[off:off + len(sec.image)])
    written = sum(res.written[off:off + len(sec.image)])
    if written != len(sec.image) or sum(res.written) != len(sec.image):
        st.problems.append(f"assembled {sum(res.written)} bytes, the image has {len(sec.image)}")
    if got != sec.image:
        first = next(k for k in range(len(got)) if got[k] != sec.image[k])
        st.problems.append(f"differs from the image first at {fmt(sec.base + first)}")


def check_ram_coverage(sections: list, coverage: dict) -> tuple:
    """sections: [(RamSection, RamStats)]. Returns (executed RAM addresses, of
    those in some section, problems, addresses in no section): an address in
    sections must be an instruction start in one of them whose decode has the
    written length under every width it ran with."""
    total = inside = 0
    problems = []
    outside = []
    for addr in sorted(a for a in coverage if bus_to_offset(a) is None):
        total += 1
        a = canonical_ram(addr)
        secs = [(s, st) for s, st in sections if s.base <= a < s.end]
        if not secs:
            outside.append(a)
            continue
        inside += 1
        ok = False
        for s, st in secs:
            size = st.starts.get(a - s.base)
            if size is not None and all((d := decode(section_reader(s), a, m, x)) is not None and d.size == size
                                        for m, x in coverage[addr]):
                ok = True
                break
        if not ok:
            problems.append((addr, "not an instruction start with a compatible width in its section"))
    return total, inside, problems, outside


__all__ = ["RamStats", "build_section", "check_section", "check_ram_coverage"]

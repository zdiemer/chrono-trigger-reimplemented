"""Blocks: C functions for statically found code (docs/RECOMP.md, M1 and M3).

The disassembler's analysis (tools.disasm.analyze.Analyzer, already run) says
which addresses are code and with which M/X widths. From it this module
picks the *entries*, the addresses the runtime can arrive at with known widths,
and forms one block per entry: the instructions reachable from it through
fall-through and static jumps without reaching another entry, translated with
tools.recomp.translate. The runtime runs a block only when P's M and X match
the widths it was generated for, and every instruction is decoded from the ROM
with the widths it really runs with, so a wrong analysis costs speed, never
correctness.

Code in RAM (M3) is decoded from the images of the RAM sections
([[ramcode]] hints, tools/disasm/ram.py): each section is a separate *space*,
and a block of a RAM space carries a guard that compares the bytes its
instructions decode from (opcodes, and operands that shape control flow or
widths) with the section's image at run time. Other operands of RAM
instructions are fetched at run time (translate's `live`), so code the program
patches (call targets, immediates, addresses) runs as generated code too.

Everything here works on a Rom and an Analyzer, so tests can use synthetic
code; tools.recomp.__main__ runs it on the real ROM and writes the files.
"""

from dataclasses import dataclass, field

from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import Mode, decode
from tools.disasm.memmap import bus_to_offset, fmt
from tools.disasm.ram import canonical_ram

from .translate import LIVE_OPERAND, Ctx, Operand, Translation, translate

CALLS = {("JSR", Mode.ABS), ("JSL", Mode.LONG), ("JSR", Mode.ABSXIND)}
BACKWARD_JUMPS = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA", "BRL", "JMP", "JML"}
STORES = {"STA", "STX", "STY", "STZ"}
JUMP_MARK = "\x00J"
ROM = -1          # the space of ROM code; RAM sections are numbered from 0
EMULATION_RESET = Ctx(True, True, True)


def ctx_variants(widths) -> list[Ctx]:
    """Concrete native-mode contexts for analysis widths, where None means either."""
    ms = [True, False] if widths[0] is None else [widths[0]]
    xs = [True, False] if widths[1] is None else [widths[1]]
    return [Ctx(m, x) for m in ms for x in xs]


def ctx_order(ctx: Ctx) -> int:
    return (0 if ctx.m8 else 2) + (0 if ctx.x8 else 1) + (4 if ctx.e else 0)


def compatible(widths, ctx: Ctx) -> bool:
    return (widths[0] is None or widths[0] == ctx.m8) and (widths[1] is None or widths[1] == ctx.x8)


def unmasks_irq(insn) -> bool:
    """CLI, or REP clearing I (PLP and RTI end blocks anyway)."""
    return insn.name == "CLI" or (insn.name == "REP" and bool(insn.operand & 0x04))


class MarkFlow:
    """Leaves a marker line for static jumps; the block decides what they become.
    A target only known at run time (a live operand) returns to the runtime, except
    that a same-bank jump whose target is the one the image has (`static`, 24-bit)
    still continues in the block."""

    def __init__(self, bank: int, static: int | None = None):
        self.bank = bank
        self.static = static

    def jump(self, pc, pb=None):
        if isinstance(pc, int) and (pb is None or isinstance(pb, int)):
            return [f"{JUMP_MARK}{(self.bank if pb is None else pb) << 16 | pc:06X}"]
        out = []
        if pb is None and self.static is not None:
            out = [f"if ({pc} == 0x{self.static & 0xFFFF:04X}) {{", f"{JUMP_MARK}{self.static:06X}", "}"]
        out += [] if pb is None else [f"c->pb = {pb if isinstance(pb, str) else f'0x{pb:02X}'};"]
        return out + [f"c->pc = {pc if isinstance(pc, str) else f'0x{pc:04X}'};", "return;"]

    def exit(self):
        return ["return;"]


@dataclass
class Step:
    insn: object
    ctx: Ctx
    t: Translation
    live: bool = False


@dataclass
class Block:
    entry: int
    ctx: Ctx
    space: int = ROM
    section: str | None = None
    steps: dict = field(default_factory=dict)     # pc -> Step
    exits: set = field(default_factory=set)       # (pc, ctx) left to the runtime

    @property
    def name(self) -> str:
        return f"recomp_{self.entry:06X}_{self.ctx.name()}" + (f"_{self.section}" if self.section else "")

    @property
    def guard(self) -> str | None:
        return f"{self.name}_guard" if self.section else None


class BlockBuilder:
    def __init__(self, an: Analyzer, registry: set = frozenset(), call_targets: set = frozenset()):
        self.an = an
        self.rom = an.rom
        self.sections = an.ram.sections
        self.registry = set(registry)
        self.call_targets = set(call_targets)
        self._translations: dict = {}
        self.entries: set = set()          # (addr, Ctx, space)
        self.entry_addrs: set = set()
        self.masks: dict = {}
        self.written = self._written_ram()

    # ---- analysis queries -----------------------------------------------------------

    def _written_ram(self) -> set:
        """Canonical RAM addresses that analysed code stores to with an absolute or
        long address (absolute ones taken in bank $7E): operands of RAM code
        there are patched at run time."""
        out = set()
        for pc, variants in self.an.code.items():
            for widths, insn in variants.items():
                if insn.name not in STORES or insn.mode not in (Mode.ABS, Mode.ABSX, Mode.ABSY, Mode.LONG,
                                                                Mode.LONGX):
                    continue
                base = insn.operand if insn.mode in (Mode.LONG, Mode.LONGX) else 0x7E0000 | insn.operand
                wide = (widths[0] if insn.name in ("STA", "STZ") else widths[1]) is not True
                out.update(canonical_ram(base + k) for k in range(2 if wide else 1))
        return out

    def spaces_at(self, pc: int) -> list[int]:
        if bus_to_offset(pc) is not None:
            return [ROM]
        a = canonical_ram(pc)
        return [i for i, s in enumerate(self.sections) if s.base <= a < s.end]

    def reader(self, space: int):
        if space == ROM:
            return self.rom.read
        sec = self.sections[space]

        def read(addr: int, n: int):
            i = canonical_ram(addr) - sec.base
            return sec.image[i:i + n] if 0 <= i and i + n <= len(sec.image) else None
        return read

    def is_code(self, pc: int, ctx: Ctx, space: int = ROM) -> bool:
        """Whether the analysis has pc as code under widths compatible with ctx (for a
        RAM space: and an instruction decodes there from that section's image, which
        may differ from the one the analysis read where sections are alternatives;
        the block's guard makes the image's decode the one that runs)."""
        variants = self.an.code.get(pc)
        if not variants or not any(compatible(w, ctx) for w in variants):
            return False
        if space == ROM:
            return bus_to_offset(pc) is not None and not self.an.hints.is_data(pc)
        return decode(self.reader(space), pc, ctx.m8, ctx.x8) is not None

    def variants_at(self, pc: int, space: int = ROM) -> list[Ctx]:
        out = set()
        for w in self.an.code.get(pc, {}):
            out.update(c for c in ctx_variants(w) if self.is_code(pc, c, space))
        return sorted(out, key=ctx_order)

    def is_live(self, insn) -> bool:
        """Operands of RAM code read at run time: all but those that shape the block
        (widths from REP/SEP, branch and JMP targets) unless code patches them."""
        if insn.size == 1 or insn.name in ("REP", "SEP"):
            return False
        if insn.mode in (Mode.REL8, Mode.REL16) or (insn.name == "JMP" and insn.mode is Mode.ABS):
            a = canonical_ram(insn.pc)
            return any(a + k in self.written for k in range(1, insn.size))
        return True

    def step(self, pc: int, ctx: Ctx, space: int = ROM) -> Step | None:
        key = (pc, ctx, space)
        s = self._translations.get(key, False)
        if s is False:
            s = None
            insn = decode(self.reader(space), pc, ctx.m8, ctx.x8)
            if insn is not None:
                live = space != ROM and self.is_live(insn)
                operand = Operand(LIVE_OPERAND[:insn.size - 1]) if live else Operand.of(insn.raw[1:])
                static = None
                if insn.mode in (Mode.REL8, Mode.REL16):
                    static = insn.branch_target()
                elif insn.name == "JMP" and insn.mode is Mode.ABS:
                    static = (pc & 0xFF0000) | insn.operand
                t = translate(insn.opcode, operand, pc & 0xFFFF, pc >> 16, ctx, MarkFlow(pc >> 16, static), live)
                s = Step(insn, ctx, t, live)
            self._translations[key] = s
        return s

    # ---- entries ------------------------------------------------------------------------

    def find_entries(self):
        an = self.an
        entries = set()

        def add(pc: int, ctxs=None):
            for space in self.spaces_at(pc):
                for c in (self.variants_at(pc, space) if ctxs is None else ctxs):
                    if self.is_code(pc, c, space):
                        entries.add((pc, c, space))

        # 1. context entries: vectors, call and JML targets, hints, coverage seeds
        for (entry, widths) in an.contexts:
            add(entry, ctx_variants(widths))
        for pc, variants in an.code.items():
            for widths, insn in variants.items():
                name, mode = insn.name, insn.mode
                # 2. call return addresses (with whatever widths the analysis resumed with)
                if (name, mode) in CALLS:
                    target = insn.operand if name == "JSL" else (pc & 0xFF0000) | insn.operand
                    if mode is Mode.ABSXIND or target not in an.hints.noreturn:
                        skip = 0 if mode is Mode.ABSXIND else an.hints.inline_args.get(target, 0)
                        add((pc & 0xFF0000) | ((insn.next_pc + skip) & 0xFFFF))
                # 4. after PLP and XCE, widths are only known at run time
                if name in ("PLP", "XCE"):
                    add(insn.next_pc)
                # 5. loop heads and block moves
                if name in BACKWARD_JUMPS and mode in (Mode.REL8, Mode.REL16, Mode.ABS, Mode.LONG):
                    if mode is Mode.ABS:
                        target = (pc & 0xFF0000) | insn.operand
                    elif mode is Mode.LONG:
                        target = insn.operand
                    else:
                        target = insn.branch_target()
                    if target >> 16 == pc >> 16 and target <= pc:
                        add(target)
                if name in ("MVN", "MVP"):
                    add(pc)
        # 2b. where indirect calls return: PER then JMP, and calls whose operand is written at run time
        for pc in getattr(an, "resumes", ()):
            add(pc)
        # 3. jump table targets (and those derived from data for tables filled at run time)
        for jt in an.tables.values():
            if jt.call:
                continue
            for t in an._table_targets(jt, jt.count or 0) + list(jt.extra) + sorted(an.derived.get(jt.dispatcher, ())):
                add(t)
        # 6. hand ports and idle loops: blocks end there
        for pc in self.registry:
            add(pc)
        # call targets seen at run time
        for pc in self.call_targets:
            add(pc)
        # 9. where a section lies over another one's code for a while (`over`, `patch`)
        #    and right after it: blocks of either don't run across the boundary
        for sec in self.sections:
            if sec.over or sec.patch:
                for a in range(sec.base, sec.end + 1):
                    add(a)
        # the reset vector runs in emulation mode up to its XCE
        reset = self.rom.u16(0x00FFFC)
        if reset is not None and reset >= 0x8000 and self.is_code(reset, EMULATION_RESET):
            entries.add((reset, EMULATION_RESET, ROM))
        self.entries = entries
        self.entry_addrs = {pc for pc, _, _ in entries} | self.registry

    # ---- blocks -------------------------------------------------------------------------

    def includable(self, pc: int, ctx: Ctx, entry: int, space: int = ROM) -> bool:
        if pc != entry and pc in self.entry_addrs:
            return False
        if space not in self.spaces_at(pc):
            return False
        return self.is_code(pc, ctx, space) and self.step(pc, ctx, space) is not None

    def build(self, entry: int, ctx: Ctx, space: int = ROM) -> Block:
        b = Block(entry, ctx, space, None if space == ROM else self.sections[space].name)
        work = [(entry, ctx)]
        b.steps[entry] = self.step(entry, ctx, space)
        while work:
            pc, c = work.pop()
            s = b.steps[pc]
            for target, tctx in self.edges(s):
                if tctx is None:
                    continue
                if target in b.steps:
                    if b.steps[target].ctx != tctx:
                        b.exits.add((target, tctx))
                    continue
                if self.includable(target, tctx, entry, space):
                    b.steps[target] = self.step(target, tctx, space)
                    work.append((target, tctx))
                else:
                    b.exits.add((target, tctx))
        return b

    @staticmethod
    def edges(s: Step):
        """(target, ctx after) for fall-through and static jumps; ctx None if unknown."""
        out = []
        bank = s.insn.pc & 0xFF0000
        for line in s.t.lines:
            if line.startswith(JUMP_MARK):
                out.append((int(line[len(JUMP_MARK):], 16), s.t.next_ctx))
        if s.t.falls_through:
            out.append((bank | ((s.insn.pc + s.t.size) & 0xFFFF), s.t.next_ctx))
        return out

    def build_all(self, max_rounds: int = 8) -> list[Block]:
        """Blocks for every entry. Exits to analysed code without an entry for those
        widths (an address reached with other widths) get entries, until none do."""
        self.find_entries()
        order = lambda e: (e[0], ctx_order(e[1]), e[2])
        for _ in range(max_rounds):
            blocks = [self.build(pc, c, space) for pc, c, space in sorted(self.entries, key=order)]
            missing = set()
            for b in blocks:
                for pc, c in b.exits:
                    for space in self.spaces_at(pc):
                        if (pc, c, space) not in self.entries and self.is_code(pc, c, space) \
                                and self.step(pc, c, space) is not None:
                            missing.add((pc, c, space))
            if not missing:
                break
            self.entries |= missing
            self.entry_addrs |= {pc for pc, _, _ in missing}
        self.masks = self.guard_masks(blocks)
        return blocks

    # ---- guards (RAM blocks) --------------------------------------------------------------

    def guard_masks(self, blocks: list[Block]) -> dict:
        """Per RAM space, a byte mask over the section image: $FF where some block's
        code was decoded from the byte (an opcode, or an operand it doesn't fetch at
        run time), 0 elsewhere. A block's guard compares its span under the mask."""
        masks = {}
        for b in blocks:
            if b.space == ROM:
                continue
            sec = self.sections[b.space]
            mask = masks.setdefault(b.space, bytearray(len(sec.image)))
            for pc, s in b.steps.items():
                off = canonical_ram(pc) - sec.base
                for k in range(1 if s.live else s.insn.size):
                    mask[off + k] = 0xFF
        return masks

    @staticmethod
    def code_ranges(b: Block, gap: int = 16) -> list[tuple[int, int]]:
        """(canonical address, length) of the block's instruction bytes, merging runs
        less than `gap` bytes apart."""
        out = []
        for a, n in sorted((canonical_ram(pc), s.insn.size) for pc, s in b.steps.items()):
            if out and a - (out[-1][0] + out[-1][1]) < gap:
                out[-1] = (out[-1][0], max(out[-1][1], a + n - out[-1][0]))
            else:
                out.append((a, n))
        return out

    # ---- C ----------------------------------------------------------------------------------

    def emit(self, b: Block) -> list[str]:
        order = sorted(b.steps)
        labels = set()
        body = []

        def transfer(target: int, tctx: Ctx | None, src_bank: int) -> list[str]:
            inside = b.steps.get(target)
            if tctx is not None and inside is not None and inside.ctx == tctx:
                if target == b.entry:
                    labels.add(target)
                    return [f"if (RC_YIELD(c)) {{", f"c->pc = 0x{target & 0xFFFF:04X};", "return;", "}",
                            f"goto L_{target:06X};"]
                labels.add(target)
                return [f"goto L_{target:06X};"]
            out = [] if target >> 16 == src_bank >> 16 else [f"c->pb = 0x{target >> 16:02X};"]
            return out + [f"c->pc = 0x{target & 0xFFFF:04X};", "return;"]

        chunks = []
        for i, pc in enumerate(order):
            s = b.steps[pc]
            lines = [f"/* {fmt(pc)}  {s.insn.text()}{'' if s.ctx == b.ctx else f'  ({s.ctx.name()})'} */",
                     f"RC_INSN(c, 0x{pc & 0xFFFF:04X});"]
            if pc != b.entry:
                labels.add(pc)   # a block can stop before any instruction and resume there
            for line in s.t.lines:
                if line.startswith(JUMP_MARK):
                    lines += transfer(int(line[len(JUMP_MARK):], 16), s.t.next_ctx, pc)
                else:
                    lines.append(line)
            if unmasks_irq(s.insn):
                lines.append("RC_IRQ_UNMASKED(c);")
            if s.t.falls_through:
                nxt = (pc & 0xFF0000) | ((pc + s.t.size) & 0xFFFF)
                follows = i + 1 < len(order) and order[i + 1] == nxt and b.steps[nxt].ctx == s.t.next_ctx \
                    and nxt != b.entry
                if not follows:
                    lines += transfer(nxt, s.t.next_ctx, pc)
            chunks.append((pc, lines))
        for pc, lines in chunks:
            if pc in labels:
                body.append(f"L_{pc:06X}:")
            body += lines
        # Start where PC points: the entry, or an instruction the block stopped before.
        resume = []
        if len(order) > 1:
            resume = ["switch (c->pc) {", f"case 0x{b.entry & 0xFFFF:04X}:", "break;"]
            for pc in order:
                if pc != b.entry:
                    resume += [f"case 0x{pc & 0xFFFF:04X}:", f"goto L_{pc:06X};"]
            resume += ["default:", "return;", "}"]
        head = [f"// ${fmt(b.entry)}"]
        start = ["Cpu *const c = &s->cpu;"]
        stop = f"RC_STOP({b.name});"
        if b.section:
            sec = self.sections[b.space]
            ranges = self.code_ranges(b)
            checks = [f"rc_ram_matches(s, 0x{lo & 0x1FFFF:05X}, recomp_ram_image_{b.space} + {lo - sec.base}, "
                      f"recomp_ram_mask_{b.space} + {lo - sec.base}, {n})" for lo, n in ranges]
            head += [f"static const uint32_t {b.name}_code[] = {{ "
                     + ", ".join(f"0x{lo:06X}, {n}" for lo, n in ranges) + " };",
                     f"bool {b.guard}(Snes *s)", "{",
                     *[("return " if i == 0 else "    && ") + chk + (";" if i == len(checks) - 1 else "")
                       for i, chk in enumerate(checks)], "}", ""]
            start += [f"RC_CODE({b.name}_code, {len(ranges)});"]
            stop = f"RC_STOP_GUARDED({b.name}, {b.guard});"
        return [*head, f"void {b.name}(Snes *s)", "{", *start, *resume, *body, stop, "}"]

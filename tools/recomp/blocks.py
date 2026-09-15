"""Blocks: C functions for statically found ROM code (docs/RECOMP.md, M1).

The disassembler's analysis (tools.disasm.analyze.Analyzer, already run) says
which ROM addresses are code and with which M/X widths. From it this module
picks the *entries*, the addresses the runtime can arrive at with known widths,
and forms one block per entry: the instructions reachable from it through
fall-through and static jumps without reaching another entry, translated with
tools.recomp.translate. The runtime runs a block only when P's M and X match
the widths it was generated for, and every instruction is decoded from the ROM
with the widths it really runs with, so a wrong analysis costs speed, never
correctness.

Everything here works on a Rom and an Analyzer, so tests can use synthetic
code; tools.recomp.__main__ runs it on the real ROM and writes the files.
"""

from dataclasses import dataclass, field

from tools.disasm.analyze import Analyzer
from tools.disasm.cpu import Mode, decode
from tools.disasm.memmap import bus_to_offset, fmt

from .translate import Ctx, Operand, Translation, translate

CALLS = {("JSR", Mode.ABS), ("JSL", Mode.LONG), ("JSR", Mode.ABSXIND)}
BACKWARD_JUMPS = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA", "BRL", "JMP", "JML"}
# Reads of registers that change with time on their own: the PPU counters and
# status, the APU ports, and $4210-$4212. A loop polling one of them yields to
# the runtime on every pass in `on` mode, so the time it waits for passes.
POLL_READS = {"LDA", "LDX", "LDY", "CMP", "CPX", "CPY", "BIT", "AND", "ORA", "EOR", "ADC", "SBC"}
POLL_MODES = {Mode.ABS, Mode.ABSX, Mode.ABSY, Mode.LONG, Mode.LONGX}
JUMP_MARK = "\x00J"


def ctx_variants(widths) -> list[Ctx]:
    """Concrete native-mode contexts for analysis widths, where None means either."""
    ms = [True, False] if widths[0] is None else [widths[0]]
    xs = [True, False] if widths[1] is None else [widths[1]]
    return [Ctx(m, x) for m in ms for x in xs]


def ctx_order(ctx: Ctx) -> int:
    return (0 if ctx.m8 else 2) + (0 if ctx.x8 else 1)


def polls_timed_register(insn) -> bool:
    if insn.name not in POLL_READS or insn.mode not in POLL_MODES:
        return False
    if insn.mode in (Mode.LONG, Mode.LONGX):
        bank = insn.operand >> 16
        if bank & 0x40:
            return False
    a = insn.operand & 0xFFFF
    return 0x2137 <= a <= 0x217F or 0x4210 <= a <= 0x4212


class MarkFlow:
    """Leaves a marker line for static jumps; the block decides what they become."""

    def __init__(self, bank: int):
        self.bank = bank

    def jump(self, pc, pb=None):
        return [f"{JUMP_MARK}{(self.bank if pb is None else pb) << 16 | pc:06X}"]

    def exit(self):
        return ["return;"]


@dataclass
class Step:
    insn: object
    ctx: Ctx
    t: Translation


@dataclass
class Block:
    entry: int
    ctx: Ctx
    steps: dict = field(default_factory=dict)     # pc -> Step
    exits: set = field(default_factory=set)       # (pc, ctx) left to the runtime
    polls: bool = False                           # reads a timed register

    @property
    def name(self) -> str:
        return f"recomp_{self.entry:06X}_{self.ctx.name()}"


class BlockBuilder:
    def __init__(self, an: Analyzer, registry: set = frozenset(), call_targets: set = frozenset()):
        self.an = an
        self.rom = an.rom
        self.registry = set(registry)
        self.call_targets = set(call_targets)
        self._translations: dict = {}
        self.entries: set = set()          # (addr, Ctx)
        self.entry_addrs: set = set()

    # ---- analysis queries -----------------------------------------------------------

    def is_code(self, pc: int, ctx: Ctx) -> bool:
        """Whether the analysis has pc as code under widths compatible with ctx."""
        variants = self.an.code.get(pc)
        if not variants or bus_to_offset(pc) is None or self.an.hints.is_data(pc):
            return False
        return any((w[0] is None or w[0] == ctx.m8) and (w[1] is None or w[1] == ctx.x8) for w in variants)

    def variants_at(self, pc: int) -> list[Ctx]:
        out = set()
        for w in self.an.code.get(pc, {}):
            out.update(c for c in ctx_variants(w) if self.is_code(pc, c))
        return sorted(out, key=ctx_order)

    def step(self, pc: int, ctx: Ctx) -> Step | None:
        key = (pc, ctx)
        s = self._translations.get(key, False)
        if s is False:
            s = None
            insn = decode(self.rom.read, pc, ctx.m8, ctx.x8)
            if insn is not None:
                t = translate(insn.opcode, Operand.of(insn.raw[1:]), pc & 0xFFFF, pc >> 16, ctx, MarkFlow(pc >> 16))
                s = Step(insn, ctx, t)
            self._translations[key] = s
        return s

    # ---- entries ------------------------------------------------------------------------

    def find_entries(self):
        an = self.an
        entries = set()

        def add(pc: int, ctxs=None):
            for c in (self.variants_at(pc) if ctxs is None else ctxs):
                if self.is_code(pc, c):
                    entries.add((pc, c))

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
        # 3. jump table targets
        for jt in an.tables.values():
            if jt.call or not jt.count:
                continue
            for t in an._table_targets(jt, jt.count):
                add(t)
        # 6. hand ports and idle loops: blocks end there
        for pc in self.registry:
            add(pc)
        # call targets seen at run time
        for pc in self.call_targets:
            add(pc)
        self.entries = entries
        self.entry_addrs = {pc for pc, _ in entries} | self.registry

    # ---- blocks -------------------------------------------------------------------------

    def includable(self, pc: int, ctx: Ctx, entry: int) -> bool:
        if pc != entry and pc in self.entry_addrs:
            return False
        return self.is_code(pc, ctx) and self.step(pc, ctx) is not None

    def build(self, entry: int, ctx: Ctx) -> Block:
        b = Block(entry, ctx)
        work = [(entry, ctx)]
        b.steps[entry] = self.step(entry, ctx)
        while work:
            pc, c = work.pop()
            s = b.steps[pc]
            if polls_timed_register(s.insn):
                b.polls = True
            for target, tctx in self.edges(s):
                if tctx is None:
                    continue
                if target in b.steps:
                    if b.steps[target].ctx != tctx:
                        b.exits.add((target, tctx))
                    continue
                if self.includable(target, tctx, entry):
                    b.steps[target] = self.step(target, tctx)
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
        for _ in range(max_rounds):
            blocks = [self.build(pc, c) for pc, c in sorted(self.entries, key=lambda e: (e[0], ctx_order(e[1])))]
            missing = set()
            for b in blocks:
                for pc, c in b.exits:
                    if (pc, c) not in self.entries and self.is_code(pc, c) and self.step(pc, c) is not None:
                        missing.add((pc, c))
            if not missing:
                return blocks
            self.entries |= missing
            self.entry_addrs |= {pc for pc, _ in missing}
        return blocks

    # ---- C ----------------------------------------------------------------------------------

    def emit(self, b: Block) -> list[str]:
        order = sorted(b.steps)
        labels = set()
        body = []

        def transfer(target: int, tctx: Ctx | None, src_bank: int) -> list[str]:
            inside = b.steps.get(target)
            if tctx is not None and inside is not None and inside.ctx == tctx:
                if target == b.entry:
                    if b.polls:
                        return [f"c->pc = 0x{target & 0xFFFF:04X};", "return;"]
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
                     "RC_INSN(c);"]
            for line in s.t.lines:
                if line.startswith(JUMP_MARK):
                    lines += transfer(int(line[len(JUMP_MARK):], 16), s.t.next_ctx, pc)
                else:
                    lines.append(line)
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
        return [f"// ${fmt(b.entry)}", f"void {b.name}(Snes *s)", "{", "Cpu *const c = &s->cpu;", *body, "}"]

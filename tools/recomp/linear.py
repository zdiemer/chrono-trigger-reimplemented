"""Translate a linear run of ROM code to C, to read what the generator emits.

    python3 -m tools.recomp.linear C0:0B4E [-n 20] [--m 8] [--x 16]

Decodes like tools.disasm.linear, following REP/SEP, and stops at an
instruction whose widths are only known at run time (after PLP, XCE) or that
doesn't decode. Jumps and branches to instructions in the run become gotos;
everything else sets PC and returns. Prints to stdout: the output is ROM-derived,
so don't commit it.
"""

import argparse
from pathlib import Path

from tools.disasm.cpu import decode
from tools.disasm.memmap import Rom, fmt, parse

from .translate import Ctx, Operand, indent, translate

REPO_ROOT = Path(__file__).resolve().parents[2]


class BlockFlow:
    """Gotos to instructions inside the block, returns to the runtime otherwise."""

    def __init__(self, bank: int, inside: set):
        self.bank = bank
        self.inside = inside

    def jump(self, pc, pb=None):
        target = (self.bank if pb is None else pb) << 16 | pc
        if target in self.inside:
            return [f"goto L_{target:06X};"]
        out = [] if pb is None else [f"c->pb = 0x{pb:02X};"]
        return out + [f"c->pc = 0x{pc:04X};", "return;"]

    def exit(self):
        return ["return;"]


def decode_run(rom: Rom, pc: int, ctx: Ctx, count: int) -> list:
    """(insn, ctx) pairs for up to count instructions."""
    run = []
    for _ in range(count):
        insn = decode(rom.read, pc, ctx.m8, ctx.x8)
        if insn is None:
            break
        run.append((insn, ctx))
        probe = translate(insn.opcode, Operand.of(insn.raw[1:]), insn.pc & 0xFFFF, insn.pc >> 16, ctx,
                          BlockFlow(insn.pc >> 16, set()))
        if probe.next_ctx is None:
            break
        ctx = probe.next_ctx
        pc = insn.next_pc
    return run


def emit_block(run: list) -> list[str]:
    entry, entry_ctx = run[0][0].pc, run[0][1]
    inside = {insn.pc for insn, _ in run}
    translations = []
    targets = set()
    for insn, ctx in run:
        flow = BlockFlow(insn.pc >> 16, inside)
        t = translate(insn.opcode, Operand.of(insn.raw[1:]), insn.pc & 0xFFFF, insn.pc >> 16, ctx, flow)
        translations.append((insn, ctx, t))
        for pc, pb in t.targets:
            targets.add(((insn.pc >> 16) if pb is None else pb) << 16 | pc)
    body = ["Cpu *const c = &s->cpu;"]
    for insn, ctx, t in translations:
        if insn.pc in targets:
            body.append(f"L_{insn.pc:06X}:")
        body.append(f"/* {fmt(insn.pc)}  {insn.text()}{'' if ctx == entry_ctx else f'  ({ctx.name()})'} */")
        body += t.lines
    last_insn, _, last = translations[-1]
    if last.falls_through:
        body += [f"c->pc = 0x{last_insn.next_pc & 0xFFFF:04X};"]
    return [f"// ${fmt(entry)}", f"void recomp_{entry:06X}_{entry_ctx.name()}(Snes *s)", "{", *indent(body), "}"]


def main():
    ap = argparse.ArgumentParser(prog="python3 -m tools.recomp.linear")
    ap.add_argument("addr")
    ap.add_argument("-n", type=int, default=20)
    ap.add_argument("--m", type=int, choices=(8, 16), default=8)
    ap.add_argument("--x", type=int, choices=(8, 16), default=8)
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "roms" / "ct-usa.sfc")
    args = ap.parse_args()
    rom = Rom(args.rom.read_bytes())
    run = decode_run(rom, parse(args.addr), Ctx(args.m == 8, args.x == 8), args.n)
    if not run:
        raise SystemExit(f"{args.addr}: doesn't decode")
    print("\n".join(emit_block(run)))


if __name__ == "__main__":
    main()

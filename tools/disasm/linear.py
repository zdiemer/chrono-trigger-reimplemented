"""Linear disassembly from an address with fixed starting widths, following REP/SEP.

    python3 -m tools.disasm.linear FD:C000 [-n 40] [--m 8] [--x 8]
"""

import argparse
from pathlib import Path

from .analyze import Analyzer
from .cpu import decode
from .memmap import Rom, fmt, parse

REPO_ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("-n", type=int, default=40)
    ap.add_argument("--m", type=int, choices=(8, 16), default=8)
    ap.add_argument("--x", type=int, choices=(8, 16), default=8)
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "roms" / "ct-usa.sfc")
    args = ap.parse_args()

    rom = Rom(args.rom.read_bytes())
    pc = parse(args.addr)
    from .analyze import State
    st = State(args.m == 8, args.x == 8)
    for _ in range(args.n):
        insn = decode(rom.read, pc, st.m, st.x)
        if insn is None:
            print(f"  {fmt(pc)}  <undecodable: width unknown or unmapped>")
            break
        hexbytes = " ".join(f"{b:02X}" for b in insn.raw)
        w = f"{'m8' if st.m else 'm16' if st.m is False else 'm?'} {'x8' if st.x else 'x16' if st.x is False else 'x?'}"
        print(f"  {fmt(pc)}  {hexbytes:<12} {insn.text():<24} ; {w}")
        st = Analyzer.step_state(insn, st)
        pc = insn.next_pc


if __name__ == "__main__":
    main()

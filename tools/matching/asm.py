"""Assembler for the matching source's dialect (docs/MATCHING.md, "Dialect").

    python3 -m tools.matching.asm SRC.asm -o OUT.sfc [--sym OUT.sym]

One closed, explicit dialect, a subset of asar syntax:

- `hirom` (required before the first `org`), `org ADDR`, `warnpc ADDR`,
  `incsrc "file"`, `incbin "file"`, `db`/`dw`/`dl` with comma-separated
  values, `label:` definitions, `;` comments.
- Instructions: a mnemonic from tools/disasm/cpu.py, with the operand size
  always written as a suffix (`LDA.b $12`, `LDA.w $0012`, `LDA.l $000012`,
  `LDA.b #$12`, `LDA.w #$1234`). Only branches, `BRL`, `PER` (a label target)
  and `MVN`/`MVP` (`MVN $src,$dst`) take no suffix, since their operand layout is
  fixed. Nothing is inferred: a missing suffix is an error, never a guess.
- Expressions are numbers (`$hex`, `%bin`, decimal) and labels joined by `+`
  and `-`.
- `;@ mW xW` comments (W = 8, 16 or ?) state the M and X widths the next
  instructions run with; asar ignores them. Until the next `org`, `db`/`dw`/
  `dl` or `incbin`, immediates of width-dependent instructions must have the
  size they state.

It is probably stricter than asar: it rejects writing the same ROM byte twice,
a statement running across a bank boundary, a branch or `JSR.w`/`JMP.w` to a
label in another bank, and a value that doesn't fit its field. 16-bit
displacements (`BRL`, `PER`) wrap within the bank, as the CPU adds them. The
image is HiROM: `org` takes bus addresses in banks $C0-$FF (or a mirror),
mapped as tools/disasm/memmap.py maps them.
"""

import argparse
import bisect
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

from tools.disasm.cpu import OPCODES, OPERAND_SIZE, Mode
from tools.disasm.memmap import ROM_SIZE, bus_to_offset

OPCODE_OF = {(name, mode): op for op, (name, mode) in enumerate(OPCODES)}
MNEMONICS = {name for name, _ in OPCODES}
BRANCH_MODES = (Mode.REL8, Mode.REL16)
# Jumps whose 16-bit operand is an address in the program bank: a label there
# assembles to its low word, a label in another bank is an error.
BANK_LOCAL = {("JSR", Mode.ABS), ("JMP", Mode.ABS), ("JSR", Mode.ABSXIND), ("JMP", Mode.ABSXIND)}

M_ = Mode
# (operand form, size suffix) -> addressing modes it can be.
MODES = {
    ("none", None): (M_.IMP,), ("acc", None): (M_.ACC,),
    ("imm", "b"): (M_.IMM_M, M_.IMM_X, M_.IMM8), ("imm", "w"): (M_.IMM_M, M_.IMM_X),
    ("plain", "b"): (M_.DP,), ("plain", "w"): (M_.ABS, M_.PEA), ("plain", "l"): (M_.LONG,),
    ("plain", None): (M_.REL8, M_.REL16),
    ("x", "b"): (M_.DPX,), ("x", "w"): (M_.ABSX,), ("x", "l"): (M_.LONGX,),
    ("y", "b"): (M_.DPY,), ("y", "w"): (M_.ABSY,),
    ("s", "b"): (M_.SR,), ("sy", "b"): (M_.SRINDY,),
    ("ind", "b"): (M_.DPIND, M_.PEI), ("ind", "w"): (M_.ABSIND,),
    ("xind", "b"): (M_.DPXIND,), ("xind", "w"): (M_.ABSXIND,),
    ("indy", "b"): (M_.DPINDY,),
    ("lind", "b"): (M_.DPLIND,), ("lind", "w"): (M_.ABSLIND,),
    ("lindy", "b"): (M_.DPLINDY,),
    ("pair", None): (M_.MOVE,),
}
FORMS = [
    ("sy", re.compile(r"^\((.+),\s*[sS]\)\s*,\s*[yY]$")),
    ("xind", re.compile(r"^\((.+),\s*[xX]\)$")),
    ("indy", re.compile(r"^\((.+)\)\s*,\s*[yY]$")),
    ("ind", re.compile(r"^\((.+)\)$")),
    ("lindy", re.compile(r"^\[(.+)\]\s*,\s*[yY]$")),
    ("lind", re.compile(r"^\[(.+)\]$")),
    ("x", re.compile(r"^(.+),\s*[xX]$")),
    ("y", re.compile(r"^(.+),\s*[yY]$")),
    ("s", re.compile(r"^(.+),\s*[sS]$")),
    ("pair", re.compile(r"^([^,]+),([^,]+)$")),
]
LIMIT = {1: 0xFF, 2: 0xFFFF, 3: 0xFFFFFF}
SUFFIX_SIZE = {"b": 1, "w": 2, "l": 3}

_LINE = re.compile(r"^\s*(?:([A-Za-z_][A-Za-z0-9_]*):)?\s*(.*?)\s*$")
_WORD = re.compile(r"^([A-Za-z]+)(?:\.([A-Za-z]))?(?:\s+(.*))?$")
_TERM = re.compile(r"\s*([+-])?\s*(\$[0-9A-Fa-f]+|%[01]+|[0-9]+|[A-Za-z_][A-Za-z0-9_]*)\s*")
_WIDTHS = re.compile(r"^;@\s*m(8|16|\?)\s+x(8|16|\?)\s*$")
_STRING = re.compile(r'^"([^"]+)"$')


class AsmError(Exception):
    def __init__(self, msg: str, where: tuple | None = None):
        super().__init__(f"{where[0]}:{where[1]}: {msg}" if where else msg)
        self.where = where


def parse_expr(text: str) -> tuple:
    """`$12 + label - 3` -> ((sign, number or name), ...)."""
    terms = []
    pos = 0
    text = text.strip()
    if not text:
        raise ValueError("missing value")
    while pos < len(text):
        m = _TERM.match(text, pos)
        if not m or (terms and not m.group(1)):
            raise ValueError(f"bad expression {text!r}")
        sign = -1 if m.group(1) == "-" else 1
        tok = m.group(2)
        if tok[0] == "$":
            val = int(tok[1:], 16)
        elif tok[0] == "%":
            val = int(tok[1:], 2)
        elif tok[0].isdigit():
            val = int(tok)
        else:
            val = tok
        terms.append((sign, val))
        pos = m.end()
    return tuple(terms)


def split_comment(line: str) -> tuple[str, str]:
    in_str = False
    for i, ch in enumerate(line):
        if ch == '"':
            in_str = not in_str
        elif ch == ";" and not in_str:
            return line[:i], line[i:]
    return line, ""


def operand_form(text: str) -> tuple[str, list[str]]:
    if not text:
        return "none", []
    if text in ("A", "a"):
        return "acc", []
    if text[0] == "#":
        return "imm", [text[1:]]
    for name, rx in FORMS:
        m = rx.match(text)
        if m:
            return name, list(m.groups())
    return "plain", [text]


@dataclass
class Insn:
    name: str
    mode: Mode
    opcode: int
    size: int
    exprs: tuple


def parse_insn(mnemonic: str, suffix: str | None, operand: str, widths: tuple | None) -> Insn:
    """One instruction's opcode and size, from its text alone."""
    name = mnemonic.upper()
    if name not in MNEMONICS:
        raise ValueError(f"unknown mnemonic {mnemonic!r}")
    sfx = suffix.lower() if suffix else None
    if sfx is not None and sfx not in SUFFIX_SIZE:
        raise ValueError(f"bad size suffix .{suffix} (.b, .w or .l)")
    form, parts = operand_form(operand)
    candidates = MODES.get((form, sfx), ())
    modes = [md for md in candidates if (name, md) in OPCODE_OF]
    if not modes:
        fixed = any((name, md) in OPCODE_OF for md in BRANCH_MODES + (Mode.MOVE,))
        if fixed and sfx is not None:
            raise ValueError(f"{name}.{sfx}: {name} takes no size suffix")
        if not fixed and sfx is None and form not in ("none", "acc", "pair"):
            raise ValueError(f"{name} {operand}: operand size must be explicit (.b, .w or .l)")
        written = f"{name}{'.' + sfx if sfx else ''} {operand}".strip()
        raise ValueError(f"{written}: not an addressing mode of {name}")
    mode = modes[0]
    if mode in (Mode.IMM_M, Mode.IMM_X, Mode.IMM8):
        size = 1 + SUFFIX_SIZE[sfx]
        if widths is not None:
            flag = widths[0] if mode is Mode.IMM_M else widths[1] if mode is Mode.IMM_X else 8
            if flag is None:
                raise ValueError(f"{name}: immediate size with the {'M' if mode is Mode.IMM_M else 'X'} "
                                 "width annotated unknown")
            if flag != 8 * (size - 1):
                raise ValueError(f"{name}.{sfx}: immediate size doesn't match the annotated "
                                 f"{'M' if mode is Mode.IMM_M else 'X'} width ({flag})")
    else:
        size = 1 + OPERAND_SIZE[mode]
    return Insn(name, mode, OPCODE_OF[(name, mode)], size, tuple(parse_expr(p) for p in parts))


@dataclass
class Result:
    image: bytearray
    written: bytearray
    symbols: dict
    spans: list = field(default_factory=list)       # (offset, size, file, line), in ROM order after locate()
    _starts: list | None = None

    def locate(self, offset: int) -> tuple | None:
        """The statement (offset, size, file, line) that wrote a ROM offset."""
        if self._starts is None:
            self.spans.sort()
            self._starts = [s[0] for s in self.spans]
        i = bisect.bisect_right(self._starts, offset) - 1
        if i >= 0 and self.spans[i][0] <= offset < self.spans[i][0] + self.spans[i][1]:
            return self.spans[i]
        return None


def _read_file(path: Path) -> bytes:
    return path.read_bytes()


class Assembler:
    """Two passes: statement sizes never depend on values, so pass 1 lays out
    every statement and defines every label, and pass 2 encodes."""

    def __init__(self, loader=_read_file, predefined: dict | None = None):
        self.loader = loader
        self.symbols: dict[str, int] = dict(predefined or {})
        self.stmts: list = []
        self.hirom = False
        self._pc: int | None = None

    # ---- pass 1 -------------------------------------------------------------

    def assemble_file(self, path: Path) -> Result:
        self._pass1(Path(path), None, [])
        return self._pass2()

    def assemble_text(self, text: str, name: str = "<text>", base: Path = Path(".")) -> Result:
        self._pass1(base / name, text, [])
        return self._pass2()

    def _load_text(self, path: Path, where) -> str:
        try:
            return self.loader(path).decode("utf-8")
        except OSError as e:
            raise AsmError(f"can't read {path}: {e.strerror or e}", where)

    def _pass1(self, path: Path, text: str | None, stack: list):
        if path in stack:
            raise AsmError(f"{path} includes itself")
        if text is None:
            text = self._load_text(path, None)
        widths = None
        fname = str(path)
        for lineno, raw in enumerate(text.splitlines(), 1):
            where = (fname, lineno)
            code, comment = split_comment(raw)
            if comment.startswith(";@"):
                m = _WIDTHS.match(comment.rstrip())
                if not m:
                    raise AsmError(f"bad width annotation {comment.strip()!r} (;@ m8|m16|m? x8|x16|x?)", where)
                widths = tuple(None if g == "?" else int(g) for g in m.groups())
            m = _LINE.match(code)
            label, rest = m.group(1), m.group(2)
            if label:
                if self._pc is None:
                    raise AsmError(f"label {label} before any org", where)
                if label in self.symbols:
                    raise AsmError(f"label {label} defined twice", where)
                if label.upper() in MNEMONICS or label.upper() in ("A", "X", "Y", "S"):
                    raise AsmError(f"label {label} is a reserved word", where)
                self.symbols[label] = self._pc
            if not rest:
                continue
            w = _WORD.match(rest)
            if not w:
                raise AsmError(f"can't parse {rest!r}", where)
            word, suffix, arg = w.group(1), w.group(2), (w.group(3) or "").strip()
            lw = word.lower()
            try:
                if lw == "hirom" and not arg and not suffix:
                    self.hirom = True
                elif lw == "org" and not suffix:
                    if not self.hirom:
                        raise ValueError("org before hirom (the only mapper this dialect has)")
                    self._pc = self._const(arg)
                    if bus_to_offset(self._pc) is None:
                        raise ValueError(f"org ${self._pc:06X} isn't ROM")
                    widths = None
                elif lw == "warnpc" and not suffix:
                    self.stmts.append(("warnpc", where, self._need_pc(), parse_expr(arg)))
                elif lw == "incsrc" and not suffix:
                    sub = path.parent / self._string(arg)
                    self._pass1(sub, None, stack + [path])
                    widths = None
                elif lw == "incbin" and not suffix:
                    sub = path.parent / self._string(arg)
                    try:
                        blob = self.loader(sub)
                    except OSError as e:
                        raise ValueError(f"can't read {sub}: {e.strerror or e}")
                    self._emit(("bytes", where, self._need_pc(), blob), len(blob))
                    widths = None
                elif lw in ("db", "dw", "dl") and not suffix:
                    size = {"db": 1, "dw": 2, "dl": 3}[lw]
                    vals = tuple(parse_expr(v) for v in self._split_values(arg))
                    self._emit(("data", where, self._need_pc(), size, vals), size * len(vals))
                    widths = None
                else:
                    insn = parse_insn(word, suffix, arg, widths)
                    self._emit(("insn", where, self._need_pc(), insn), insn.size)
            except ValueError as e:
                raise AsmError(str(e), where)

    @staticmethod
    def _split_values(arg: str) -> list[str]:
        vals = [v.strip() for v in arg.split(",")]
        if not arg.strip() or any(not v for v in vals):
            raise ValueError("empty value in data list")
        return vals

    @staticmethod
    def _string(arg: str) -> str:
        m = _STRING.match(arg)
        if not m:
            raise ValueError(f'expected a quoted file name, got {arg!r}')
        return m.group(1)

    def _const(self, arg: str) -> int:
        expr = parse_expr(arg)
        return self._eval(expr)

    def _need_pc(self) -> int:
        if self._pc is None:
            raise ValueError("statement before any org")
        return self._pc

    def _emit(self, stmt: tuple, size: int):
        pc = stmt[2]
        if size and (pc & 0xFFFF) + size > 0x10000:
            raise ValueError(f"statement at ${pc:06X} ({size} bytes) runs across a bank boundary")
        self.stmts.append(stmt)
        self._pc = pc + size

    # ---- pass 2 -------------------------------------------------------------

    def _eval(self, expr: tuple) -> int:
        v = 0
        for sign, t in expr:
            if isinstance(t, str):
                if t not in self.symbols:
                    raise ValueError(f"undefined label {t}")
                t = self.symbols[t]
            v += sign * t
        return v

    def _pass2(self) -> Result:
        image = bytearray(ROM_SIZE)
        written = bytearray(ROM_SIZE)
        res = Result(image, written, self.symbols)
        spans = res.spans
        for st in self.stmts:
            kind, where, pc = st[0], st[1], st[2]
            try:
                if kind == "insn":
                    out = self._encode(st[3], pc)
                elif kind == "bytes":
                    out = st[3]
                elif kind == "data":
                    size = st[3]
                    parts = []
                    for e in st[4]:
                        v = self._eval(e)
                        if not 0 <= v <= LIMIT[size]:
                            raise ValueError(f"value ${v:X} doesn't fit in {size} byte(s)")
                        parts.append(v.to_bytes(size, "little"))
                    out = b"".join(parts)
                else:   # warnpc
                    limit = self._eval(st[3])
                    if pc > limit:
                        raise ValueError(f"pc ${pc:06X} is past warnpc ${limit:06X}")
                    continue
            except ValueError as e:
                raise AsmError(str(e), where)
            n = len(out)
            if not n:
                continue
            off = bus_to_offset(pc)
            if off is None or off + n > ROM_SIZE:
                raise AsmError(f"${pc:06X} isn't ROM", where)
            if any(written[off:off + n]):
                first = next(i for i in range(n) if written[off + i])
                prev = res.locate(off + first)
                raise AsmError(f"${pc + first:06X} written twice"
                               + (f" (first by {prev[2]}:{prev[3]})" if prev else ""), where)
            image[off:off + n] = out
            written[off:off + n] = b"\x01" * n
            spans.append((off, n, where[0], where[1]))
            res._starts = None
        return res

    def _encode(self, insn: Insn, pc: int) -> bytes:
        mode, size = insn.mode, insn.size
        if mode in (Mode.IMP, Mode.ACC):
            return bytes([insn.opcode])
        if mode is Mode.MOVE:
            src, dst = (self._eval(e) for e in insn.exprs)
            for v in (src, dst):
                if not 0 <= v <= 0xFF:
                    raise ValueError(f"{insn.name} bank ${v:X} doesn't fit in a byte")
            return bytes([insn.opcode, dst, src])
        v = self._eval(insn.exprs[0])
        if mode in BRANCH_MODES:
            if not 0 <= v <= 0xFFFFFF or v >> 16 != pc >> 16:
                raise ValueError(f"{insn.name} target ${v:06X} isn't in the bank of ${pc:06X}")
            d = v - (pc + size)
            # A 16-bit displacement reaches the whole bank: the CPU adds it
            # modulo $10000, and the original has BRLs that rely on that.
            if mode is Mode.REL8 and not -0x80 <= d < 0x80:
                raise ValueError(f"{insn.name} target ${v:06X} out of range ({d:+d})")
            v = d & LIMIT[size - 1]
        elif (insn.name, mode) in BANK_LOCAL and v > 0xFFFF:
            if v >> 16 != pc >> 16:
                raise ValueError(f"{insn.name} target ${v:06X} isn't in the bank of ${pc:06X}")
            v &= 0xFFFF
        if not 0 <= v <= LIMIT[size - 1]:
            raise ValueError(f"{insn.name} operand ${v:X} doesn't fit in {size - 1} byte(s)")
        return bytes([insn.opcode]) + v.to_bytes(size - 1, "little")


def write_symbols(symbols: dict, path: Path):
    lines = [f"{v >> 16:02X}:{v & 0xFFFF:04X} {k}" for k, v in sorted(symbols.items(), key=lambda kv: (kv[1], kv[0]))]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    ap = argparse.ArgumentParser(prog="python3 -m tools.matching.asm")
    ap.add_argument("source", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--sym", type=Path, help="write label addresses")
    args = ap.parse_args()
    try:
        res = Assembler().assemble_file(args.source)
    except AsmError as e:
        sys.exit(f"error: {e}")
    end = max((o + n for o, n, *_ in res.spans), default=0)
    args.out.write_bytes(bytes(res.image[:end]))
    if args.sym:
        write_symbols(res.symbols, args.sym)
    print(f"{args.out}: {end} bytes, {sum(n for _, n, *_ in res.spans)} assembled, {len(res.symbols)} labels")


if __name__ == "__main__":
    main()

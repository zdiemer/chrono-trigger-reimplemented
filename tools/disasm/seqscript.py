"""Sequence scripts of the attract program, parsed (docs/MATCHING.md, MD4, C3).

The attract, title and ending program decompressed to $7E:3000 runs two
sequence channels: `$7E:3749` and `$7E:3754` set the direct page to $0A00 or
$0B00, DB to $7E, and call the interpreter `$C3:0B03` (through `$C3:0AFF`),
which reads commands from the 16-bit pointer at $20 (so $0A20 or $0B20) in
bank $7E until one yields. The command set below is read from the
interpreter; the handler addresses are those of the table at `$C3:0B74`.

- $00-$7F: a register assignment: the low nibble picks the direct page word
  (0-15, doubled), the high nibble the form; with bit 5 of the high nibble
  clear, a word operand is stored or added (3 bytes), with it set, another
  register (byte operand) is copied or added (2 bytes). `$C3:0B03-0B53`.
- $80-$8F, $90-$9F: a counter loop (`$C3:0C92`): the word at $22 + 2n counts
  up to the word operand at 1; below it, the script continues at the word at 3,
  otherwise after the 5 bytes. $9n also yields.
- $A0-$FF: the low nibble indexes the table at `$C3:0B74`:
  0 `$C3:0C51` call the code at the word operand (stored at $00F0, run by
  `JSR ($00F0,X)` at `$7E:37F6` with X = 0), 3 bytes;
  1 `$C3:0C19` store the byte at 3 at the word address at 1 (bank $00), 4 bytes;
  2 `$C3:0C41` store the word at 3 at the word address at 1, 5 bytes;
  3 `$C3:0C7C` call: $30 = the address after the operand, continue at the word;
  4 `$C3:0C85` return to $30;
  5 `$C3:0C2A` wait for the APU, 2 bytes;
  6 `$C3:0BD4` sound command, 5 bytes;
  7 `$C3:0BCC` remove the word operand from the task list at $0940, 3 bytes;
  8 `$C3:0BC4` add it to that list (run by `JSR ($0940,X)` at `$7E:39CF`), 3 bytes;
  9 `$C3:0BBC` remove it from the list at $0920, 3 bytes;
  10 `$C3:0BAB` add it to that list (run from `$7E:3998`), 3 bytes;
  11 `$C3:0C5F` store the byte at 1 into $21xx (xx at 2), 3 bytes;
  12 `$C3:0BAA` yield, 1 byte; 13 `$C3:0BA6` goto the word and yield;
  14 `$C3:0B9F` goto; 15 `$C3:0B95` stop (stays on the command).

Commands 0 and 2 continue with `$C3:0BB1`, which adds 2 to the pointer after
the handler: a store to the running channel's pointer (command 2 on its own
$0A20/$0B20, or code command 0 calls) continues 2 bytes after the stored
address. Scripts are parsed per channel for that reason.
"""

from dataclasses import dataclass, field

from .memmap import fmt

BANK = 0x7E0000
CHANNELS = (0x0A00, 0x0B00)
POINTERS = (0x0A20, 0x0B20)
# Cells a store command may not write: the channels' return addresses; the task
# lists; the $00F0 call cell; the RAM vector stubs. (Stores to the pointers are
# gotos, handled.)
GUARDED = ({d + o for d in CHANNELS for o in (0x21, 0x30, 0x31)}
           | set(range(0x0920, 0x0970)) | {0x00F0, 0x00F1} | set(range(0x0500, 0x0508)))
# Counter cells: $22 + 2n for n = 0-15; n = 7 is $30, the return address.
COUNTER_BASE = 0x22
LENGTHS = {0: 3, 1: 4, 2: 5, 3: 3, 4: 1, 5: 2, 6: 5, 7: 3, 8: 3, 9: 3, 10: 3, 11: 3, 12: 1, 13: 3, 14: 3,
           15: 1}


@dataclass
class SeqParse:
    commands: dict = field(default_factory=dict)       # address -> (opcode, length)
    calls: dict = field(default_factory=dict)          # code address ($00F0) -> {command addresses}
    tasks_0940: dict = field(default_factory=dict)
    tasks_0920: dict = field(default_factory=dict)
    roots: dict = field(default_factory=dict)          # (address, pointer) -> why
    problems: list = field(default_factory=list)


def _word(read, a):
    b = read(a, 2)
    return None if b is None else b[0] | b[1] << 8


def parse(read, roots: list, effects: dict | None = None) -> SeqParse:
    """read(bus address, n) reads the program image; roots: [(address, pointer,
    why)], pointer $0A20 or $0B20 naming the channel; effects: code address ->
    [(pointer or None for the running channel's, values)] for the pointer stores
    of code that command 0 calls."""
    effects = effects or {}
    res = SeqParse()
    work = []
    seen = set()

    def goto(t, ch, where):
        t = BANK | (t & 0xFFFF)
        if (t, ch) not in res.roots:
            res.roots[(t, ch)] = where
        work.append((t, ch))

    for a, ch, why in roots:
        goto(a, ch, why)
    while work:
        p, ch = work.pop()
        while (p, ch) not in seen:
            seen.add((p, ch))
            op = read(p, 1)
            if op is None:
                res.problems.append(f"{fmt(p)}: sequence script runs outside the program")
                break
            b = op[0]
            where = f"command {b:02X} at {fmt(p)}"
            stop = False
            if b < 0x80:
                length = 2 if b & 0x20 else 3
            elif b < 0xA0:
                length = 5
            else:
                length = LENGTHS[b & 0x0F]
            if read(p, length) is None:
                res.problems.append(f"{where}: runs outside the program")
                break
            res.commands[p] = (b, length)
            if 0x80 <= b < 0xA0:
                if COUNTER_BASE + 2 * (b & 0x0F) == 0x30:
                    res.problems.append(f"{where}: its counter is the return address cell")
                goto(_word(read, p + 3), ch, where)
            elif b >= 0xA0:
                n = b & 0x0F
                w = _word(read, p + 1) if length >= 3 else None
                if n == 0:
                    res.calls.setdefault(BANK | w, set()).add(p)
                    for ptr, values in effects.get(BANK | w, ()):
                        ptr = ch if ptr is None else ptr
                        for v in sorted(values):
                            goto(v + 2 if ptr == ch else v, ptr, f"code {fmt(BANK | w)} called at {fmt(p)}")
                        stop = stop or ptr == ch
                elif n == 2 and w in POINTERS:
                    v = _word(read, p + 3)
                    goto(v + 2 if w == ch else v, w, where)
                    stop = w == ch
                elif n in (1, 2) and any((w + i) & 0xFFFF in GUARDED for i in range(n)):
                    res.problems.append(f"{where}: stores to {w:04X}")
                elif n == 8:
                    res.tasks_0940.setdefault(BANK | w, set()).add(p)
                elif n == 10:
                    res.tasks_0920.setdefault(BANK | w, set()).add(p)
                elif n == 3:
                    goto(w, ch, where)
                elif n in (13, 14):
                    goto(w, ch, where)
                    stop = True
                elif n in (4, 15):
                    stop = True
            if stop:
                break
            p = BANK | (p + length) & 0xFFFF
    return res


__all__ = ["SeqParse", "parse"]

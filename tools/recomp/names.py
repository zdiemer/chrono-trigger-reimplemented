"""Names a draft uses: work RAM from the RAM map, I/O registers, script commands.

- **Work RAM.** An address the draft knows (a constant, or D or DB known) is
  looked up in `re/ram.toml` (tools/ram_map.py) and written with game/ram.h's
  name, `RAM_<NAME>`, plus an offset or element where it falls inside one. The
  map names bytes per subsystem, since work RAM is reused between modes: a
  draft takes a name from its own subsystem, `core`, or the subsystem that
  shares its page (field and event, scene and attract), as the map's coverage
  count does; one from another subsystem says nothing about this use of the
  bytes. Where the routine's D is known and a page of the map has that D and
  the routine's subsystem, that page's variables come first.
- **I/O registers** get their conventional names in a comment: game/ reaches
  them through runtime/platform/, so a draft that touches one is showing the
  porter a platform call to make.
- **Script commands.** A routine that is the handler of an event command
  (tools/disasm/eventscript.py) or a battle animation command, enemy
  condition or enemy action (tools/disasm/battlescript.py) is named after the
  command, with its operand layout in the header.
"""

from tools.disasm import battlescript, eventscript
from tools.ram_map import SHARES, macro

# Subsystem of the code in a bank (the RAM map's names; docs/PORTING.md's layout).
BANK_SUBSYSTEM = {
    0x7E: "scene", 0xC0: "field", 0xC1: "battle", 0xC2: "menu", 0xC3: "scene", 0xC6: "scene",
    0xC7: "core", 0xCC: "battle", 0xCD: "battle", 0xCE: "battle", 0xCF: "battle", 0xD1: "battle",
}

IO_REGISTERS = {
    0x2100: "INIDISP", 0x2101: "OBSEL", 0x2102: "OAMADDL", 0x2103: "OAMADDH", 0x2104: "OAMDATA",
    0x2105: "BGMODE", 0x2106: "MOSAIC", 0x2107: "BG1SC", 0x2108: "BG2SC", 0x2109: "BG3SC", 0x210A: "BG4SC",
    0x210B: "BG12NBA", 0x210C: "BG34NBA", 0x210D: "BG1HOFS", 0x210E: "BG1VOFS", 0x210F: "BG2HOFS",
    0x2110: "BG2VOFS", 0x2111: "BG3HOFS", 0x2112: "BG3VOFS", 0x2113: "BG4HOFS", 0x2114: "BG4VOFS",
    0x2115: "VMAIN", 0x2116: "VMADDL", 0x2117: "VMADDH", 0x2118: "VMDATAL", 0x2119: "VMDATAH",
    0x211A: "M7SEL", 0x211B: "M7A", 0x211C: "M7B", 0x211D: "M7C", 0x211E: "M7D", 0x211F: "M7X", 0x2120: "M7Y",
    0x2121: "CGADD", 0x2122: "CGDATA", 0x2123: "W12SEL", 0x2124: "W34SEL", 0x2125: "WOBJSEL", 0x2126: "WH0",
    0x2127: "WH1", 0x2128: "WH2", 0x2129: "WH3", 0x212A: "WBGLOG", 0x212B: "WOBJLOG", 0x212C: "TM",
    0x212D: "TS", 0x212E: "TMW", 0x212F: "TSW", 0x2130: "CGWSEL", 0x2131: "CGADSUB", 0x2132: "COLDATA",
    0x2133: "SETINI", 0x2134: "MPYL", 0x2135: "MPYM", 0x2136: "MPYH", 0x2137: "SLHV", 0x2138: "RDOAM",
    0x2139: "RDVRAML", 0x213A: "RDVRAMH", 0x213B: "RDCGRAM", 0x213C: "OPHCT", 0x213D: "OPVCT",
    0x213E: "STAT77", 0x213F: "STAT78", 0x2140: "APUIO0", 0x2141: "APUIO1", 0x2142: "APUIO2",
    0x2143: "APUIO3", 0x2180: "WMDATA", 0x2181: "WMADDL", 0x2182: "WMADDM", 0x2183: "WMADDH",
    0x4016: "JOYSER0", 0x4017: "JOYSER1", 0x4200: "NMITIMEN", 0x4201: "WRIO", 0x4202: "WRMPYA",
    0x4203: "WRMPYB", 0x4204: "WRDIVL", 0x4205: "WRDIVH", 0x4206: "WRDIVB", 0x4207: "HTIMEL",
    0x4208: "HTIMEH", 0x4209: "VTIMEL", 0x420A: "VTIMEH", 0x420B: "MDMAEN", 0x420C: "HDMAEN",
    0x420D: "MEMSEL", 0x4210: "RDNMI", 0x4211: "TIMEUP", 0x4212: "HVBJOY", 0x4213: "RDIO",
    0x4214: "RDDIVL", 0x4215: "RDDIVH", 0x4216: "RDMPYL", 0x4217: "RDMPYH", 0x4218: "JOY1L",
    0x4219: "JOY1H", 0x421A: "JOY2L", 0x421B: "JOY2H", 0x421C: "JOY3L", 0x421D: "JOY3H", 0x421E: "JOY4L",
    0x421F: "JOY4H",
}
DMA_FIELDS = {0: "DMAP", 1: "BBAD", 2: "A1TL", 3: "A1TH", 4: "A1B", 5: "DASL", 6: "DASH", 7: "DASB",
              8: "A2AL", 9: "A2AH", 0xA: "NTRL"}
# The multiplier and divider are arithmetic, not hardware a port abstracts
# (docs/PORTING.md: every register but these goes through the platform).
ARITHMETIC = set(range(0x4202, 0x4207)) | set(range(0x4214, 0x4218))


def io_name(a: int) -> str | None:
    off = a & 0xFFFF
    if off in IO_REGISTERS:
        return IO_REGISTERS[off]
    if 0x4300 <= off <= 0x437F and (off & 0xF) in DMA_FIELDS:
        return f"{DMA_FIELDS[off & 0xF]}{(off >> 4) & 7}"
    return None


class RamNamer:
    """Names for work RAM addresses (24-bit, $7E/$7F) as a routine of `subsystem`
    sees them. `d` is the routine's direct page when known, which ranks that
    page's variables first."""

    def __init__(self, ram_map, subsystem: str | None, d: int | None = None):
        self.map = ram_map
        self.subsystem = subsystem
        self.d = d
        self.used: set = set()

    def allowed(self, var) -> bool:
        if self.subsystem is None:
            return True
        return var.subsystem in ({self.subsystem, "core"} | SHARES.get(self.subsystem, set()))

    def name(self, a: int) -> str | None:
        hits = [(v, f) for v, f in self.map.lookup(a, self.subsystem) if self.allowed(v)]
        if not hits:
            return None
        if self.subsystem is None and len({v.name for v, _ in hits}) > 1:
            return None
        hits.sort(key=lambda h: (not (h[0].page is not None and h[0].page.d == self.d and self.d is not None),
                                 h[0].subsystem != self.subsystem, h[0].addr))
        v, f = hits[0]
        off = a - v.addr
        k, r = divmod(off, v.stride)
        base = macro(v.name, f.name) if f is not None else macro(v.name)
        if f is not None:
            r -= f.offset
        self.used.add(v.name)
        text = base
        if k:
            if v.count > 1 and v.stride > 1:
                text += f" + {k} * {macro(v.name, 'stride')}"
            else:
                r += k * v.stride
        if r:
            text += f" + {r}" if r < 10 else f" + 0x{r:X}"
        return text

    def dp_name(self, page, offset: int) -> str | None:
        """A variable of a page whose D isn't fixed, by its offset."""
        for v in self.map.vars:
            if v.page is page and v.dp is not None and v.dp <= offset < v.dp + v.span():
                off = offset - v.dp
                if off == 0:
                    return macro(v.name, "dp")
                return f"{macro(v.name, 'dp')} + {off}"
        return None


# ---- script commands ------------------------------------------------------------------------


def commands_at(addr: int) -> list:
    """(interpreter, description, command) for every script command whose handler
    is at addr; command is an eventscript.Command for event commands."""
    out = []
    if addr >> 16 == 0xC0:
        for op, c in sorted(eventscript.COMMANDS.items()):
            if 0xC00000 | c.handler == addr:
                ops = ", ".join(f"{k}: {kind}" for k, kind in c.operands)
                extra = " + more by its first operand" if op in eventscript.VARIABLE else ""
                out.append(("event", f"event command ${op:02X} `{c.name}`"
                            + (f" ({ops}{extra})" if ops or extra else "") + f", flow {c.flow}", c))
    if addr >> 16 == 0xC1:
        lo = addr & 0xFFFF
        for op, c in sorted(battlescript.ANIM.items()):
            if c.handler == lo:
                length = "a length from its operand" if c.length is None else f"{c.length} byte" + \
                    ("s" if c.length != 1 else "")
                out.append(("battle-animation", f"battle animation command ${op:02X} `{c.name}` ({length}, "
                            f"{c.kind})", None))
        for i, c in sorted(battlescript.COND.items()):
            if c.handler == lo:
                out.append(("enemy-condition", f"enemy script condition ${i:02X} `if_{c.name}`", None))
        for i, a in sorted(battlescript.ACTIONS.items()):
            if a.choose == lo:
                out.append(("enemy-action", f"enemy script action ${i:02X} `{a.name}` (choosing it)", None))
            if a.run == lo:
                out.append(("enemy-action", f"enemy script action ${i:02X} `{a.name}` (running it)", None))
    return out


def event_operand_at(cmd, offset: int) -> str | None:
    """The operand of an event command that covers byte `offset` of it (1 is the
    first operand byte)."""
    if cmd is None:
        return None
    pos = 1
    for key, kind in cmd.operands:
        size = eventscript.SIZES[kind]
        if pos <= offset < pos + size:
            return key if size == 1 or offset == pos else f"{key} + {offset - pos}"
        pos += size
    return None


def command_symbol(interp: str, text: str) -> str:
    name = text.split("`")[1] if "`" in text else "cmd"
    prefix = {"event": "event_cmd_", "battle-animation": "battle_anim_", "enemy-condition": "enemy_",
              "enemy-action": "enemy_action_"}[interp]
    return prefix + name

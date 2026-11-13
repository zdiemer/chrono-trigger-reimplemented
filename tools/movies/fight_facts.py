"""What a carried fight leaves behind, checked against the original's play.

A route carries a fight movie's outcome (tools/fight_routes.txt), retrying a
lost fight from its command menus. A battle that computes otherwise -- other
damage, other enemy HP, other rewards -- could still be won after retries, so
a carried route, when it finishes, reports how many retries its fights took
and the facts the fights leave in the saved game:

- each character's level and experience;
- the party's gold;
- the inventory, every item but the consumables (CONSUMABLES), at the
  reference's counts: what the fights and the story gave, and what they took;
- the story's event memory ($7F:0000-$7F:01FF), as a digest.

tools/fight_facts.txt holds the expected values, taken from the movie's
reference end state: the original's code playing the movie (the frozen
`emulator-oracle` tag's ct_dev), whose saved game matches the ledger's
`game` digest in tools/outcomes.txt. No native run sets them.
"""
import hashlib
import os
import re

from . import addresses as A

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FACTS = os.path.join(REPO, "tools", "fight_facts.txt")

CHARACTERS = 7
# Tonic ($BD) to Lapis ($CA): how many of these a fight uses goes with its
# course -- whose HP ran low, on which turn -- which a route's commands choose
# otherwise than the recording's, so they aren't facts of the fight.
CONSUMABLES = range(0xBD, 0xCB)
CHARACTER_RECORDS, CHARACTER_STRIDE = 0x7E2600, 0x50
LEVEL, EXPERIENCE = 0x12, 0x13          # character_records' fields (re/ram.toml)
GOLD = 0x7E2C53
EVENT_MEMORY_BYTES = 0x200
WORK_RAM = 0x7E0000


def facts(read):
    """The facts from `read(address, length) -> bytes`: a dict of kind -> text."""
    levels, experience = [], []
    for c in range(CHARACTERS):
        record = CHARACTER_RECORDS + c * CHARACTER_STRIDE
        levels.append(read(record + LEVEL, 1)[0])
        e = read(record + EXPERIENCE, 3)
        experience.append(e[0] | e[1] << 8 | e[2] << 16)
    g = read(GOLD, 3)
    ids, counts = read(A.INVENTORY_ITEMS, A.INVENTORY_SIZE), read(A.INVENTORY_COUNTS, A.INVENTORY_SIZE)
    items = {}
    for item, count in zip(ids, counts):
        if item and count:
            items[item] = items.get(item, 0) + count
    return {
        "levels": ",".join(map(str, levels)),
        "experience": ",".join(map(str, experience)),
        "gold": str(g[0] | g[1] << 8 | g[2] << 16),
        "items": items,
        "events": hashlib.sha256(bytes(read(A.EVENT_MEMORY, EVENT_MEMORY_BYTES))).hexdigest()[:16],
    }


def from_state(state):
    """The facts from an end state: work RAM then save RAM (ct_native/ct_dev --end-state)."""
    return facts(lambda address, length: state[address - WORK_RAM:address - WORK_RAM + length])


def items_text(items):
    """`id:count,...` in hex ids, or `-` for none."""
    return ",".join("%02X:%d" % (item, items[item]) for item in sorted(items)) or "-"


def items_from_text(text):
    items = {}
    for entry in text.split(","):
        if entry != "-":
            item, count = entry.split(":")
            items[int(item, 16)] = int(count)
    return items


def kept_items(items):
    """The inventory without the consumables."""
    return {item: count for item, count in items.items() if item not in CONSUMABLES}


def read_expected(path=FACTS):
    """{route: {kind: value}} from `route kind value # source` lines."""
    out = {}
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            fields = line.partition("#")[0].split()
            if not fields:
                continue
            if len(fields) != 3 or fields[1] not in ("levels", "experience", "gold", "items", "events"):
                raise ValueError("%s:%d: expected route kind value" % (os.path.basename(path), number))
            out.setdefault(fields[0], {})[fields[1]] = fields[2]
    return out


def check(observed, expected):
    """What differs between a run's facts and the expected ones, as text lines."""
    problems = []
    for kind, want in expected.items():
        if kind == "items":
            wanted, held = items_from_text(want), kept_items(observed["items"])
            for item in sorted(set(wanted) | set(held)):
                if held.get(item, 0) != wanted.get(item, 0):
                    problems.append("item %02X: %d, expected %d" % (item, held.get(item, 0), wanted.get(item, 0)))
        elif observed[kind] != want:
            problems.append("%s: %s, expected %s" % (kind, observed[kind], want))
    return problems


REPORT = re.compile(r"^fight facts: (.*)$")
TRIES = re.compile(r"^fight tries: (.*)$")


def report(run, tries):
    """At a route's finish: its fights' retries and the facts, as lines
    tools/movies/route_check.py reads."""
    observed = facts(lambda address, length: bytes(run.machine.peek(address, length)))
    run.say("fight tries: %s" % (",".join(map(str, tries)) or "none"))
    run.say("fight facts: levels %s experience %s gold %s events %s items %s" % (
        observed["levels"], observed["experience"], observed["gold"], observed["events"],
        items_text(observed["items"])))
    return observed


def parse_report(lines):
    """(tries list or None, facts dict or None) from a route's output."""
    tries, observed = None, None
    for line in lines:
        m = TRIES.match(line)
        if m:
            tries = [] if m.group(1) == "none" else [int(x) for x in m.group(1).split(",")]
        m = REPORT.match(line)
        if m:
            words = m.group(1).split()
            fields = dict(zip(words[::2], words[1::2]))
            items = items_from_text(fields["items"])
            observed = {"levels": fields["levels"], "experience": fields["experience"],
                        "gold": fields["gold"], "events": fields["events"], "items": items}
    return tries, observed


def verdict(route, lines, bound, expected):
    """What fails a carried route's run, from its output `lines`: a fight that
    took more retries than `bound`, a missing report, or facts other than
    `expected` (its tools/fight_facts.txt entries)."""
    tries, observed = parse_report(lines)
    if tries is None or observed is None:
        return ["no fight report"]
    problems = ["a fight took %d retries, more than %d" % (t, bound) for t in tries if t > bound]
    return problems + check(observed, expected)

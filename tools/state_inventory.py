"""Reproduce the state migration's source and overlap ledgers (no ROM needed).

python3 tools/state_inventory.py writes docs/state-addresses.tsv and
docs/state-overlaps.tsv. Counts are syntactic sites, not runtime accesses.
Dynamic addresses remain candidates; this does not claim to prove their range.
"""
import collections
import itertools
import re

try:
    from . import ram_map
except ImportError:
    import ram_map


PATTERNS = {
    "raw-helper": r"\b(?:wram_\w+|hw_port_\w+)\s*\(",
    "machine-address": r"\bm_(?:read\w*|write\w*|dp\w*|abs\w*)\s*\(",
    "page-view": r'\b\w+_page_of\s*\(|#include\s+"port/vars_pages.h"',
    "video-source": r"\bPF_GAME\s*\(",
    "layout-expression": r"\bRAM_\w+\b|\b0x7[EFef][0-9A-Fa-f]{4}\b|<<\s*16\b|&\s*0x(?:FF0000|FFFF)\b",
    "unchecked-accessor": r"\b\w+_unchecked\s*\(",
}


def source_sites(root=ram_map.GAME):
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".c", ".h") or path.name in ("ram.h", "vars.h"):
            continue
        # Preserve newlines so a ledger row points at the original source.
        source = path.read_text(encoding="utf-8")
        source = re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: "\n" * m[0].count("\n"), source, flags=re.S)
        for category, pattern in PATTERNS.items():
            for match in re.finditer(pattern, source):
                line = source.count("\n", 0, match.start()) + 1
                expression = source[match.start():source.find("\n", match.start())].strip()
                yield path.relative_to(ram_map.ROOT).as_posix(), line, category, expression


def overlaps(m):
    owners = collections.defaultdict(set)
    for v in m.vars:
        for addr in ram_map.variable_bytes(v):
            owners[addr].add(v.name)
    pairs = collections.defaultdict(list)
    for addr, names in sorted(owners.items()):
        for pair in itertools.combinations(sorted(names), 2):
            pairs[pair].append(addr)
    return owners, pairs


def main():
    m = ram_map.RamMap()
    sites = list(source_sites())
    with (ram_map.ROOT / "docs/state-addresses.tsv").open("w", encoding="utf-8", newline="\n") as f:
        print("file\tline\tcategory\texpression", file=f)
        for row in sites:
            print(*row, sep="\t", file=f)
    owners, pairs = overlaps(m)
    with (ram_map.ROOT / "docs/state-overlaps.tsv").open("w", encoding="utf-8", newline="\n") as f:
        print("variable\tshares_with\tbytes\tfirst\tlast", file=f)
        for (a, b), addresses in sorted(pairs.items()):
            print(a, b, len(addresses), ram_map.fmt_addr(addresses[0]), ram_map.fmt_addr(addresses[-1]), sep="\t", file=f)
    aliases = set().union(*(names for names in owners.values() if len(names) > 1))
    internal = []
    for v in m.vars:
        for a, b in itertools.combinations(v.fields, 2):
            shared = min(a.offset + a.size, b.offset + b.size) - max(a.offset, b.offset)
            if shared > 0:
                internal.append((v.name, a.name, b.name, shared * v.count * len(v.bases())))
    with (ram_map.ROOT / "docs/state-field-overlaps.tsv").open("w", encoding="utf-8", newline="\n") as f:
        print("variable\tfield\tshares_with\tbytes", file=f)
        for row in internal:
            print(*row, sep="\t", file=f)
    print("source:", dict(sorted(collections.Counter(row[2] for row in sites).items())))
    print("overlaps: %d variables, %d pairs, %d shared bytes" %
          (len(aliases), len(pairs), sum(len(names) > 1 for names in owners.values())))
    print("within-record overlaps: %d pairs in %d variables" % (len(internal), len({r[0] for r in internal})))
    print("subsystem variables aliased unchecked page-instance")
    for sub in ram_map.SUBSYSTEMS:
        vs = [v for v in m.vars if v.subsystem == sub]
        if vs:
            print(sub, len(vs), sum(v.name in aliases for v in vs), sum(bool(v.unchecked) for v in vs),
                  sum(v.on_instances() for v in vs))
    ram_map.storage_report(m)


if __name__ == "__main__":
    main()

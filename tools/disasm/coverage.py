"""Executed-instruction coverage from `ct_dev --coverage`: lines of `BB:AAAA m x`
with widths 8 or 16. Every address in it ran as code with those widths."""

from collections import defaultdict
from pathlib import Path

from .memmap import parse


def load(paths: list[Path]) -> dict[int, set]:
    seen: dict[int, set] = defaultdict(set)
    for path in paths:
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            try:
                addr, m, x = line.split()
                seen[parse(addr)].add((m == "8", x == "8"))
            except ValueError as e:
                raise SystemExit(f"{path}:{n}: bad coverage line {line!r} ({e})")
    return dict(seen)


def load_calls(paths: list[Path]) -> dict[int, set]:
    """Executed call edges from `ct_dev --calls`: target -> call sites."""
    calls: dict[int, set] = defaultdict(set)
    for path in paths:
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            try:
                site, target = line.split()
                calls[parse(target)].add(parse(site))
            except ValueError as e:
                raise SystemExit(f"{path}:{n}: bad call line {line!r} ({e})")
    return dict(calls)

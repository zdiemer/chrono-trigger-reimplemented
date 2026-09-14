#!/usr/bin/env python3
"""Download the SingleStepTests SPC700 suite and convert it for tests/apu/spc700_runner.

    python3 tests/apu/fetch_sst_spc700.py              # all 256 opcodes
    python3 tests/apu/fetch_sst_spc700.py --ops 8f,cf
    python3 tests/apu/fetch_sst_spc700.py --cycle-table # per-opcode cycle counts seen in the suite

Source: https://github.com/SingleStepTests/spc700 (MIT; per-opcode JSON with
full state before and after one instruction, plus its bus cycles). Converted
files go to tests/apu/data/ (gitignored); the JSON is not kept. Only the cycle
*count* is kept from the cycle list.

Binary format, little-endian, per file: u32 test count, then per test the
initial and final state, each as
    u16 pc, u8 a, u8 x, u8 y, u8 sp, u8 psw, u16 ram_count, ram_count x (u16 addr, u8 value)
followed by u16 cycle count.
"""

import argparse
import gzip
import json
import struct
import sys
import urllib.request
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

URL = "https://raw.githubusercontent.com/SingleStepTests/spc700/main/v1/{op}.json"
OUT = Path(__file__).resolve().parent / "data"

STATE = struct.Struct("<HBBBBBH")


def pack_state(s: dict) -> bytes:
    head = STATE.pack(s["pc"], s["a"], s["x"], s["y"], s["sp"], s["psw"], len(s["ram"]))
    return head + b"".join(struct.pack("<HB", a, v) for a, v in s["ram"])


def fetch(op: str) -> str:
    dest = OUT / f"{op}.bin"
    if dest.exists():
        return f"{dest.name} (cached)"
    req = urllib.request.Request(URL.format(op=op), headers={"Accept-Encoding": "gzip"})
    with urllib.request.urlopen(req, timeout=120) as r:
        body = r.read()
        if r.headers.get("Content-Encoding") == "gzip":
            body = gzip.decompress(body)
    tests = json.loads(body)
    blob = struct.pack("<I", len(tests)) + b"".join(
        pack_state(t["initial"]) + pack_state(t["final"]) + struct.pack("<H", len(t["cycles"]))
        for t in tests)
    tmp = dest.with_suffix(".tmp")
    tmp.write_bytes(blob)
    tmp.replace(dest)
    return dest.name


def read_cycles(path: Path) -> Counter:
    b = path.read_bytes()
    (n,), o = struct.unpack_from("<I", b), 4
    counts = Counter()
    for _ in range(n):
        for _state in range(2):
            ram = STATE.unpack_from(b, o)[6]
            o += STATE.size + 3 * ram
        counts[struct.unpack_from("<H", b, o)[0]] += 1
        o += 2
    return counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ops", help="comma-separated hex opcodes (default: all 256)")
    ap.add_argument("-j", type=int, default=8)
    ap.add_argument("--cycle-table", action="store_true", help="print cycle-count distribution per opcode")
    args = ap.parse_args()

    ops = [o.lower() for o in args.ops.split(",")] if args.ops else [f"{i:02x}" for i in range(256)]
    if args.cycle_table:
        for op in ops:
            p = OUT / f"{op}.bin"
            if p.exists():
                print(op, dict(sorted(read_cycles(p).items())))
        return

    OUT.mkdir(exist_ok=True)
    failed = 0
    with ThreadPoolExecutor(args.j) as pool:
        for op, fut in zip(ops, [pool.submit(fetch, op) for op in ops]):
            try:
                print(fut.result(), flush=True)
            except Exception as e:   # keep going; rerun fetches only what's missing
                failed += 1
                print(f"{op}: {e}", file=sys.stderr, flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

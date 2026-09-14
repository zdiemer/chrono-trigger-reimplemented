#!/usr/bin/env python3
"""Download the SingleStepTests 65816 suite and convert it for tests/cpu/sst_runner.

    python3 tests/cpu/fetch_sst.py                 # all opcodes, native and emulation
    python3 tests/cpu/fetch_sst.py --modes n --ops a9,fc

Source: https://github.com/SingleStepTests/65816 (per-opcode JSON captured from
real hardware). Converted files go to tests/cpu/data/ (gitignored); the JSON is
not kept. Per-cycle bus activity is reduced to a cycle count; register and
memory state before and after each instruction are kept in full.

Binary format, little-endian, per file: magic "SST2", u32 test count, then per
test the initial and final state, each as
    u16 pc, u16 s, u8 p, u16 a, u16 x, u16 y, u8 dbr, u16 d, u8 pbr, u8 e,
    u16 ram_count, ram_count x (u32 addr, u8 value)
followed by u16 cycle count (bus and internal cycles for the instruction).
"""

import argparse
import gzip
import json
import struct
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

URL = "https://raw.githubusercontent.com/SingleStepTests/65816/main/v1/{op}.{mode}.json"
OUT = Path(__file__).resolve().parent / "data"
MAGIC = b"SST2"


def pack_state(s: dict) -> bytes:
    head = struct.pack("<HHBHHHBHBBH", s["pc"], s["s"], s["p"], s["a"], s["x"], s["y"],
                       s["dbr"], s["d"], s["pbr"], s["e"], len(s["ram"]))
    return head + b"".join(struct.pack("<IB", a, v) for a, v in s["ram"])


def fetch(op: str, mode: str) -> str:
    dest = OUT / f"{op}.{mode}.bin"
    if dest.exists() and dest.read_bytes()[:4] == MAGIC:
        return f"{dest.name} (cached)"
    req = urllib.request.Request(URL.format(op=op, mode=mode), headers={"Accept-Encoding": "gzip"})
    with urllib.request.urlopen(req, timeout=120) as r:
        body = r.read()
        if r.headers.get("Content-Encoding") == "gzip":
            body = gzip.decompress(body)
    tests = json.loads(body)
    blob = MAGIC + struct.pack("<I", len(tests)) + b"".join(
        pack_state(t["initial"]) + pack_state(t["final"]) + struct.pack("<H", len(t["cycles"]))
        for t in tests)
    tmp = dest.with_suffix(".tmp")
    tmp.write_bytes(blob)
    tmp.replace(dest)
    return dest.name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ops", help="comma-separated hex opcodes (default: all 256)")
    ap.add_argument("--modes", default="n,e", help="n (native), e (emulation) or n,e")
    ap.add_argument("-j", type=int, default=8)
    args = ap.parse_args()

    ops = args.ops.split(",") if args.ops else [f"{i:02x}" for i in range(256)]
    jobs = [(op.lower(), m) for op in ops for m in args.modes.split(",")]
    OUT.mkdir(exist_ok=True)
    failed = 0
    with ThreadPoolExecutor(args.j) as pool:
        for (op, m), fut in zip(jobs, [pool.submit(fetch, op, m) for op, m in jobs]):
            try:
                print(fut.result(), flush=True)
            except Exception as e:   # keep going; rerun fetches only what's missing
                failed += 1
                print(f"{op}.{m}: {e}", file=sys.stderr, flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

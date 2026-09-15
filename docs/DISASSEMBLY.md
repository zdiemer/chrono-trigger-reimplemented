# Disassembly

`tools/disasm` finds 65816 code in the ROM by tracing from known entry points,
tracking the M and X register widths so immediate operands decode at the right
size. It needs only the Python 3.11+ standard library.

```sh
python3 tools/rom.py stage path/to/ct.sfc      # once
python3 -m tools.disasm                        # writes disasm/out/
python3 -m tools.disasm.linear C0:5900 -n 30 --m 8 --x 16
python3 -m unittest discover tests
```

`disasm/out/` is gitignored: it contains ROM bytes and must never be committed.

Executed-instruction coverage from the development system improves both
coverage and correctness:

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --frames 3000 --movie oracle/movies/title-start.ctm \
    --coverage traces/coverage-title.txt
python3 -m tools.disasm --coverage traces/coverage-title.txt   # repeatable
```

Coverage is used three ways:

- **Seeds.** Executed addresses that static tracing didn't reach start new
  contexts with their observed widths.
- **Return pruning.** When a callee statically returns with widths that its
  return address never ran with, that continuation is dropped for that call
  site and listed in the report. These entries point at the root of width
  over-approximation (a callee with a path that leaves M or X changed only
  under conditions the run never hit, or never at all), which is worth
  confirming and pinning with a `returns` hint.
- **Conflicts.** Executed addresses still decoded statically with different
  widths are listed as errors to fix with hints.

| Output | Contents |
|---|---|
| `listing/bank_XX.asm` | Code in ROM order per bank, labels, widths where ambiguous, gaps summarized |
| `functions.json` | Every entry point with entry/return widths, callers and size |
| `report.md` | Coverage per bank and the work list: everything the analysis couldn't resolve |
| `liveness.json` | Per function: registers and flags read on entry, and those live/dead after it returns |

## How tracing works

- Code is traced per **context**: a function entry plus the widths it was
  entered with. The same routine called with different widths is traced
  separately.
- `REP`/`SEP` change widths. `PHP`/`PLP` are paired on a per-context stack, and
  `SEC; XCE` forces 8-bit.
- A call resumes in the caller with the widths the callee **returned** with. This
  is resolved to a fixpoint, so routines that end in `REP #$20; RTS` work. When a
  callee is never seen to return, the caller's widths are assumed and the call
  is listed in the report.
- `JMP`/`BRA`/`BRL` stay within the context. `JML` is a tail call.
- `JMP (abs,X)` and `JSR (abs,X)` read tables from the program bank. Table
  lengths come from hints, or from a heuristic that stops at known code, other
  tables' starts and targets, or a target that doesn't decode as plausible code.
  The heuristic runs after everything else is traced and iterates to a fixpoint.
- Tracing stops at `BRK`, `COP`, `WDM` and `STP`, which almost always mean data.

## Liveness

`tools/disasm/liveness.py` computes, for every function, which of A, X, Y, the
N/V/Z/C flags, DB and D any caller may read after it returns. It's an
interprocedural backward dataflow over the traced code: calls contribute the
callee's inputs, returns take the union of what's live at every call site, and
unknown control flow keeps everything live. Functions entered as vectors,
hints, coverage seeds or indirect targets, and functions that `--calls` shows
being called from sites static analysis doesn't know, keep everything live
after return. Ported routines use the dead set to skip reproducing values
nobody reads (docs/PORTING.md).

## Hints (`re/hints.toml`)

Hints are the committed knowledge base: facts the tracer can't infer. The
schema is at the top of the file. Every hint should carry a comment saying how
it was established. Typical workflow:

1. Run the tool and read `report.md`.
2. Pick an unresolved indirect jump, a suspicious opcode, or a heuristic table.
3. Inspect it with `tools.disasm.linear` and the listing.
4. Add an `entry`, `jumptable`, `returns`, `indirect` or `data` hint, then rerun.

## Known structure

- Reset `$00:FF00` is `SEI; CLC; XCE; JML $FD:C000`.
- NMI and IRQ jump through JML stubs in RAM at `$00:0500` and `$00:0504`. Each
  game mode installs its own handlers (see `re/hints.toml`). Writing `$40`
  (`RTI`) there disables a vector.
- The event script interpreter dispatches through a 256-entry command table at
  `$C0:5D6E`, fetching command bytes from `$7F:2001,X`.
- Bank C2's object system runs a handler per slot (`$C2:051D`, `JMP ($0050)`)
  and an object script interpreter with an 83-entry command table at
  `$C2:0F91`. Script commands `$34`/`$35`/`$4E` call, spawn or JSL to addresses
  in script data, so their `indirect` hints only list targets seen running.
- The menu in C2 runs cooperative threads: `$C2:8203` starts one on its own
  stack and `JSR $821E` yields. Thread starts are `entry` hints because the
  tracer would treat that `JMP (abs)` as a tail call.

## Limits

Static tracing can't see code reached only through RAM pointers, data-driven
dispatch it hasn't been told about, or stack tricks. Banks C0 and C1 are mostly
covered. C2, C3, FD and the smaller code banks are not, yet. Coverage from
`ct_dev --coverage` closes that gap as input movies reach more of the game.
Movies through the attract sequence and title screen execute about 4,000
distinct instructions, all of which static tracing already finds.

## Census and the matching disassembly

`python3 -m tools.disasm.census [--coverage ...]` classifies every ROM byte:
code and jump tables from the analysis, and in the rest padding, streams in the
decompressor's format, BRR-like samples and pointer-table-like runs, by
heuristics. It prints counts per bank only. It measures the remaining work for
a byte-identical, reassemblable disassembly, scoped in
[MATCHING.md](MATCHING.md): every byte classified, every address symbolic, and
closure checks that make the recompiler's code set provably complete.

`python3 -m tools.matching --rom roms/ct-usa.sfc --out build/main/matching
[--coverage ...] [--calls ...]` (or the `ct_matching` CMake target) is its first
milestone: it writes the analysis's code as assembly source (explicit operand
sizes, `;@ m8 x16` width annotations, labels from hints, `game/registry.c` and
the analysis), `incbin`s every other byte from files extracted into the build
tree, reassembles it with `tools/matching/asm.py` and fails unless the result is
the ROM. Like `disasm/out/`, all of that is ROM-derived and stays in the build
tree.

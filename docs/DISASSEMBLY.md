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
- **Returns the movies never ran.** When a callee statically returns with
  widths its return address never ran with, the continuation is still traced
  and the return is listed in the report. Most are paths the movies didn't
  take; where following one decodes garbage (a width conflict, BRK/COP), the
  callee's static exit is an over-approximation to pin with a `returns` or
  `state` hint. `--prune-returns` drops such continuations instead, for
  exploration only.
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
- Code the game runs from RAM is traced from images that `[[ramcode]]` hints
  describe: a stream decompressed with `tools/disasm/lz.py` (the `$C3:0557`
  format), bytes copied from ROM, the constants a run of code stores
  (`writes`), or what a code generator writes through the WRAM port when run
  with known registers (`run`, `tools/disasm/ram.py`'s small interpreter).
  Addresses outside every section stay unmapped.

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

Kinds added for the matching disassembly (MD1):

| Hint | Fields | Meaning |
|---|---|---|
| `[[data]]` | `start`, `end`, `type`, [`name`], type parameters | never decode as code; `type` is one of `DATA_TYPES` in `tools/disasm/hints.py` (`padding`, `bytes`, `words`, `ptr16`, `ptr24`, `struct` with `size`, `lz`, `brr`, `script` with `format`, `text`, `mixed`, `undetermined`, `header`, `ram_code`, ...); the matching build checks padding, element sizes and lz streams |
| `[[overlap]]` | `addr`, `m`, `x` | an instruction that really runs with widths giving two lengths; the source writes the decode under m/x and checks the other ends on an instruction |
| `[[ramcode]]` | `name`, `base`, `source`, `format` = `lz` / `copy` / `writes` / `run`, [`size`], [`end`, `m`, `x`], [`regs`], [`over`], [`patch`] | a RAM section: its image at run address `base`; `writes` simulates the stores of the code from `source` to `end` (which may itself be in an earlier section; long loads from ROM or a section count as constants); `run` executes the routine at `source` with widths `m`/`x` and registers `regs` (`a`, `x`, `y`, `db`, `d`) and collects what it writes through the WRAM port from `base` (M3); `over` names a section this one is written over; `patch` names a section whose code the program replaces with this one for a while (both are alternatives: the analysis reads the patched section, blocks exist for both) |
| `[[noreturn]]` | `addr` | as `[noreturn] addrs`, one per entry with its comment |
| `[[entry]]` without `m`/`x` | | entered with any widths (interrupt handlers) |
| `[[jumptable]]` `targets` | list | targets of a table filled at run time (`count` may be 0) |
| `[[indirect]]` on a `JSR`/`JSL` | `at`, `targets` | code writes the call's operand at run time; the targets replace the one in the bytes |

Hints the MD1 work added carry `kind = "unreached"` on entries for code nothing
found reaches, and live in sections headed "MD1".

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

`python3 -m tools.disasm.census [--coverage ...] [--list-code-shaped]` classifies
every ROM byte: code and jump tables from the analysis, typed `[[data]]` hints,
and in the rest padding, streams in the decompressor's format, BRR-like samples
and pointer-table-like runs, by heuristics. It prints counts per bank only, and
the code-shaped runs (linear decodes that look like routines calling known
code) left in unknown bytes and in hints typed `mixed` or `undetermined`. It measures the remaining work for
a byte-identical, reassemblable disassembly, scoped in
[MATCHING.md](MATCHING.md): every byte classified, every address symbolic, and
closure checks that make the recompiler's code set provably complete.

`python3 -m tools.matching --rom roms/ct-usa.sfc --out build/main/matching
[--coverage ...] [--calls ...]` (or the `ct_matching` CMake target) is its first
milestone: it writes the analysis's code as assembly source (explicit operand
sizes, `;@ m8 x16` width annotations, labels from hints, `game/registry.c` and
the analysis), `incbin`s every other byte from files extracted into the build
tree, reassembles it with `tools/matching/asm.py` and fails unless the result is
the ROM. Since MD1 it also fails unless every executed ROM address is an
instruction start with compatible widths, every `[[data]]` hint fits its bytes,
and every RAM section, written as `src/ram/<name>.asm` with `base`, assembles to
its image. Like `disasm/out/`, all of that is ROM-derived and stays in the build
tree.

# Static recompiler

`tools/recomp` translates the game's 65816 code into C when the project is
built, from the user's ROM. The generated code replaces the interpreter as
what runs routines nobody has ported yet: hand ports take precedence, generated
code runs the rest, and the interpreter is left for code the generator can't
see. It is scaffolding like the interpreter ([APPROACH.md](APPROACH.md)): a
mechanical image of the ROM, never source. Generated C is ROM-derived, so it
lives only in the build tree, like `disasm/out/`; the generator, its templates
and its tests are what's committed.

| Milestone | What | Status |
|---|---|---|
| M0 | Instruction translation for every opcode, addressing mode and register width | done |
| M1 | Blocks for all statically found code, built from the ROM, registered with the native runtime; 0 shadow mismatches over the movies | designed (below) |
| M2 | Exact timing: interrupts on the interpreter's instruction boundaries, loop re-entry | designed |
| M3 | Coverage feedback: interpreted ROM code seeds the analysis | |
| M4 | Optimizations: dead flags from liveness, direct block-to-block transfers, fast dispatch | |
| M5 | Cleanup workflow from a generated block to a hand port | designed |

## Instruction translation (M0)

| Path | What |
|---|---|
| `tools/recomp/translate.py` | One instruction to C statements |
| `runtime/port/recomp.h` | The helpers those statements call (`rc_*`) |
| `runtime/port/recomp_snes.h` | Binds the helpers to the development system's bus |
| `tools/recomp/linear.py` | Prints a linear run of ROM code as a C function, for reading the output (stdout only) |
| `tools/recomp/sst.py` | Generates the SingleStepTests harness (below) |

`translate(opcode, operand, pc, pb, ctx, flow)` returns the statements, the
instruction size, whether execution falls through, the widths after it and its
static jump and call targets. The statements operate on `Cpu *c` and do exactly
what `runtime/cpu/cpu65816.c` does:

- **Registers and flags**, decimal-mode `ADC`/`SBC` included (the D flag is
  tested at run time; everything width-dependent is fixed at generation).
- **Bus accesses in the interpreter's order** through `RC_READ`/`RC_WRITE`,
  which `recomp_snes.h` maps to `snes_bus_read`/`snes_bus_write`, so I/O side
  effects match. Read-modify-write instructions write the high byte first;
  `JSL` and `JSR (abs,X)` push between their fetches.
- **Fetches.** Generated code doesn't fetch its operands, but the interpreter
  does, and that is visible: bus time, and open bus (`LDA $4210` takes bits 4-6
  from open bus, which the interpreter left holding the operand's high byte
  `$42`). `RC_FETCH` charges the bytes' access
  time at the current FastROM setting and leaves the last byte on the bus.
- **Internal cycles** with `RC_IO`, including the data-dependent ones (direct
  page with D's low byte nonzero, indexing across a page, taken branches, page
  crossing in emulation mode).

Operands, PC and the program bank are values: ints when known at generation
(code from the ROM, which then folds to constants), or C expressions when only
known at run time (the test harness runs every test of an opcode through one
function). Both go through the same templates, so the tests exercise exactly
what the generator emits.

A context is the M and X widths, or emulation mode (8-bit). Within a block,
`REP`/`SEP` with a constant operand give the next instruction's widths; after
`PLP` and `XCE` they're only known at run time, so the block ends there and the
runtime picks the entry matching P.

Control flow goes through a `Flow`. Branches, `BRA`, `BRL`, `JMP abs` and
`JML long` jump to a static target, which the block generator turns into a
`goto` or into setting PC and returning. Everything else that transfers control
sets PC (and PB) on the machine and returns to the runtime: `JSR`/`JSL` (after
pushing the return address), `RTS`/`RTL`/`RTI`, indirect jumps, `BRK`/`COP`
(through the vector), `WAI`/`STP` (setting `waiting`/`stopped`). `MVN`/`MVP`
move one byte per pass and jump back to themselves while bytes remain, as the
interpreter re-executes the instruction, so interrupts can land between passes.

A synthetic example (`REP #$20 ; LDX #$10 ; LDA $1000,X ; STA [$20],Y ; DEY ;
DEX ; BPL ; JSR $9000`), as `python3 -m tools.recomp.linear` prints it:

```c
// $C0:8000
void recomp_C08000_m8x8(Snes *s)
{
    Cpu *const c = &s->cpu;
    /* C0:8000  REP #$20 */
    RC_FETCH(c, 0xC08000, 2, 0x20);
    c->p &= (uint8_t)~0x20;
    RC_IO(c, 1);
    rc_normalize_p(c);
    /* C0:8002  LDX #$10  (m16x8) */
    RC_FETCH(c, 0xC08002, 2, 0x10);
    c->x = 0x10;
    rc_nz8(c, (uint8_t)c->x);
L_C08004:
    /* C0:8004  LDA $1000,X  (m16x8) */
    RC_FETCH(c, 0xC08004, 3, 0x10);
    rc_lda16(c, rc_rd16(c, rc_absx(c, 0x1000, false)));
    /* C0:8007  STA [$20],Y  (m16x8) */
    RC_FETCH(c, 0xC08007, 2, 0x20);
    rc_wr16(c, rc_dplindy(c, 0x20), c->a);
    ...
    /* C0:800B  BPL $8004  (m16x8) */
    RC_FETCH(c, 0xC0800B, 2, 0xF7);
    if (!(c->p & CPU_FLAG_N)) {
        RC_IO(c, 1);
        goto L_C08004;
    }
    /* C0:800D  JSR $9000  (m16x8) */
    RC_FETCH(c, 0xC0800D, 3, 0x90);
    RC_IO(c, 1);
    rc_push16(c, 0x800F);
    c->pc = 0x9000;
    return;
}
```

### Validation

```sh
build/main/sst_recomp_runner tests/cpu/data/*.bin   # must report 0 failures
build/main/recomp_bus_diff [--iterations N] [--seed S]   # must report 0 mismatches
python3 -m unittest discover tests
```

- **`sst_recomp_runner`** is `tests/cpu/sst_runner.c` built with `SST_RECOMP`:
  instead of `cpu_step` it calls the generated function for the opcode at PC and
  the current widths (`tools/recomp/sst.py` writes one per opcode and context,
  1280 in all, into `build/.../recomp_sst/`, reading the operand bytes, PC and
  PB at run time). It compares registers, memory and cycle counts exactly as
  `sst_runner` does. Result: all 5.12M tests pass, native and emulation, with
  cycle counts (which the flat test bus reports as bus accesses plus internal
  cycles).
- **`recomp_bus_diff`** compiles the same generated functions with
  `recomp_snes.h` and runs each against the interpreter on the SNES machine:
  instructions in WRAM, random registers and WRAM, operands, data bank and
  direct page biased toward PPU, APU and CPU I/O registers. After each
  instruction both machines must match in everything shadow validation compares
  plus open bus, bus clocks, internal cycles and interrupt/halt state. The
  flat SST memory can't show those. Result: 256000 instructions (200 per opcode
  and context), 0 mismatches. A deliberately wrong open bus in `RC_FETCH` fails
  about 28% of them.
- **`tests/test_recomp.py`** covers what block generation relies on: every
  opcode translates in every context with constants folding completely, braces
  balance, targets and widths are reported correctly.

### Time per instruction (for M2)

Generated code already accounts for time exactly: an instruction costs the bus
clocks its accesses and fetches add to `bus.clocks` plus 6 clocks per internal
cycle counted in `cpu.io_cycles`, the same two counters the system loop charges
after `cpu_step`. Both are validated per opcode (SST cycle counts, and
`recomp_bus_diff` against the interpreter's clocks), so no per-opcode cost table
is needed: a block's time is `bus.clocks + 6 * io_cycles` at any instruction
boundary.

## Blocks (M1)

The generator runs the disassembler's analysis in process
(`tools.disasm.analyze.Analyzer` with `re/hints.toml` and any coverage and call
logs), which gives every context (entry address and widths), the widths at each
instruction, jump tables and call return widths. `functions.json` has the same
contexts but not the per-instruction states, so it's not the input.

**Entries** are the addresses the runtime can arrive at, with the widths the
analysis found there:

1. every context entry (vectors, call and `JML` targets, hints, coverage seeds);
2. call return addresses, with the widths the callee returns with;
3. jump table and resolved indirect targets;
4. the instruction after a `PLP` or `XCE`, one entry per width variant the
   analysis has there;
5. loop heads (targets of backward branches) and `MVN`/`MVP` instructions, so a
   loop that stops can be resumed;
6. every address registered in `game/registry.c` (hand ports and idle loops),
   so a block never runs past code a port replaces.

A **block** is one C function per entry: the instructions reachable from it
through fall-through, branches, `JMP abs`, `BRL` and same-bank `JML`, with
widths following `REP`/`SEP`. Reaching another entry ends the block (PC set,
return), except its own loop heads. Forward branches inside the block are
`goto`s; a backward branch to its own loop head is a yield point:
`if (RC_YIELD(c)) { c->pc = head; return; } goto head;`. Anything else
(calls, returns, indirect jumps, unknown widths) sets PC and returns.
Instructions that decode differently from the analysis (a width conflict) end
the block before them.

Correctness doesn't depend on the analysis being right about widths. An entry
only runs when P's M and X match the widths it was generated for, and within a
block widths change only through constant `REP`/`SEP`, so the decode always
matches what the interpreter would do. A wrong analysis only produces blocks
that never run or entries that don't match, which fall back to the interpreter.

## Build (M1)

A CMake step runs the generator when a ROM is present:

```sh
python3 -m tools.recomp --rom roms/ct-usa.sfc --hints re/hints.toml --registry game/registry.c \
    [--coverage traces/coverage.txt] --out build/main/recomp
```

- It verifies the ROM's CRC32 (`2D206BF7`) and refuses anything else, then
  writes a fixed set of shards (`blocks_000.c` ... `blocks_063.c`, a few
  thousand instructions each, so the output list is known at configure time and
  builds in parallel) and `table.c` with `const NativeEntry recomp_natives[]`
  and the ROM's CRC. `DEPENDS` covers the ROM, hints, registry, coverage files
  and `tools/recomp`, `tools/disasm` sources. The ROM path is a cache variable
  (`CT_ROM`, default `roms/ct-usa.sfc`); coverage files are `CT_RECOMP_COVERAGE`.
- The analysis finds about 98000 instructions, a few lines of C each. If that
  makes builds slow, generated shards get a lower optimization level than the
  rest (measure first).
- Without a ROM, `ct_recomp` is built from a committed stub with an empty table
  and CMake says so. `ct_dev` then runs hand ports and the interpreter as today.
  The SST harness never needs the ROM.
- `ct_dev` checks the loaded ROM's CRC against the table's before registering
  generated entries and warns and skips them otherwise. `--recomp off` leaves
  them out; `--exclude` works on their names (`recomp_C08000_m8x8`).

## Registration (M1)

Generated entries use `NativeEntry` with:

- `.addr`, `.fn`, `.name`; `.p_mask = CPU_FLAG_M | CPU_FLAG_X` and `.p_value`
  for the widths (native mode only, as for all entries);
- `.generated = true` (new): the function runs to completion on the machine
  directly, without a fiber (it never calls out). `cost_milli` and
  `dont_care` are unused: generated code reproduces every register and flag,
  and its time is exact (above). `idle` and `guard` are unused: hand-written
  idle entries keep precedence at their loop heads, and only ROM code is
  generated.

Runtime changes:

- **Several entries per address.** `find_entry` returns the first entry at the
  address; the table is sorted by address with hand-written entries before
  generated ones, and `intercept` takes the first whose precondition applies.
  So a hand port wins, and a generated entry still runs when the port's
  precondition or guard rejects the call.
- **ON mode.** After a generated block, the debt is
  `bus.clocks + 6 * io_cycles` (exact rather than scaled), and the
  intercept returns 0 so the system loop looks again at the new PC: a
  hand port, another block, or the interpreter. `RC_YIELD` is true once the
  block has spent a loop budget (for M1 a fixed number of master clocks, about
  a scanline), so busy-wait loops let time pass and interrupts arrive.
- **Reports** summarize generated entries in one line (calls, matched,
  mismatched, sampled) plus the first mismatch details, rather than a line per
  entry.

## Calls and returns (M1)

Generated code never calls anything. A `JSR` pushes the return address, sets
PC and returns; the runtime then runs the callee, whatever it is, and the
callee's `RTS` returns to the call's return address, which is an entry. All
state is on the emulated stack, so hand ports, generated blocks and the
interpreter can call and return into one another in any combination, and
interrupts land at block boundaries. Hand ports' `m_jsr`/`m_jsl` call-outs
reach generated callees the same way, and the synchronous `native_call` path
(fuzzer) runs generated entries like any other applicable entry.

## Shadow validation (M1)

A generated block is checked like a hand port that stops early: the block runs
on the clone, and when the original reaches the PC and S where the block
stopped, the machines are compared, `dont_care` empty. Differences from hand
ports:

- **Yield at every loop pass.** In shadow mode `RC_YIELD` is always true, so a
  block stops at its first back edge. Otherwise it would compare at the loop
  head before the original finished the passes the block ran, and a loop waiting
  for an interrupt would spin forever on the clone.
- **No nesting.** A generated entry isn't started while another routine is in
  its segment (native code done, original catching up), so blocks don't nest
  inside ports' or each other's stretches. Checks chain instead: the next block
  starts where the last one stopped.
- **Sampling.** Checking every block entry would clone the machine (about
  290 KB) on the order of a thousand times per frame. Each generated entry is
  checked on its first 16 calls and then 1 in 256 (`--recomp-sample`); skipped
  calls are counted.

M1 is done when the six movies run in shadow mode with 0 mismatches and `on`
mode still plays them. Because timing is approximate until M2 (interrupts at
block boundaries, time charged after each block), `on` frames will differ from
`off` in detail, as with hand ports.

## Exact timing (M2)

Blocks keep `bus.clocks + 6 * io_cycles` current, so the system can run a block
against a horizon instead of charging time afterwards:

- The system computes a horizon: the clocks until the next point anything the
  CPU can observe changes on its own (hblank start and end, the HDMA point, the
  end of the line, H/V timer IRQs, the frame boundary). At each instruction
  boundary a block checks whether it has spent that much; if so it stops, the
  system advances by the time spent (applying the events in order, as
  per-instruction advances would, since only the last instruction crossed
  one), and the next instruction starts with interrupts polled exactly as the
  interpreter polls them.
- I/O accesses sync first: `RC_READ`/`RC_WRITE` on `$2100-$21FF` and
  `$4000-$43FF` advance time to the current instruction before touching the
  PPU counters, `$4210-$4212` or the APU ports, so the SPC700 sees writes and
  produces answers at the same master clock.
- Interrupts can also become deliverable without time passing: a `$4200` write
  enabling NMI while the NMI flag is set, or I cleared (`CLI`, `REP`, `PLP`)
  with an IRQ pending. Those instructions check for a deliverable interrupt and
  stop the block, as the system loop would take it before the next instruction.
- Loop heads use the same check, which replaces M1's fixed budget.

The check for M2 is stronger than shadow mode: with only generated code
(`--recomp only`), `--native on` must produce traces and audio identical to
`--native off`, frame for frame.

## RAM code and unknown code

No generated entry, no change: the interpreter runs code in RAM (the attract
sequence at `$7E:3000`), ROM code the analysis didn't reach, and contexts with
widths it didn't find. M3 closes the ROM part: `ct_dev --coverage` restricted to
interpreted instructions lists the executed ROM addresses and widths without a
generated entry, and feeding that back as coverage seeds gives them entries on
the next build. RAM code could get guarded blocks (hash of the block's bytes,
as `m_code_matches` does for hand ports) from code images captured at build
time; that's left open.

## Cleanup into hand ports (M5)

A port starts from the generated block instead of the listing:

1. `python3 -m tools.recomp.show BB:AAAA --m 8 --x 16` prints the routine as the
   generator builds it (to stdout, like `tools.recomp.linear`), with calls
   rewritten as `m_jsr`/`m_jsl` call-outs, `rc_*` helpers mapped to their
   `machine.h` equivalents, fetch and cycle accounting removed, and flag
   updates dropped where `liveness.json` shows them dead.
2. The port is written from that draft in `game/<subsystem>/` like any other:
   named, structured and commented by what it does. The draft itself isn't
   committed; like a listing, it's a mechanical image of the ROM.
3. It's registered in `game/registry.c`, and precedence takes it from there:
   the generator ends blocks at its address on the next build, and shadow mode
   validates it as today.

## Risks

- **Build cost.** Tens of MB of C, regenerated whenever the ROM, hints,
  coverage or registry change. Mitigation: shards built in parallel, a lower
  optimization level for them if needed, and not rewriting shards whose
  content didn't change (with a stamp file as the custom command's output, so
  Ninja doesn't recompile them).
- **Dispatch overhead.** Every block exit goes back through `intercept`, which
  binary-searches the table. With blocks ending at every call and loop, that's
  frequent; M4 adds direct transfers between blocks and a per-bank page table.
- **Shadow cost and coverage.** Sampling means some paths through a block are
  only checked by the instruction-level tests. Block formation (gotos, exits,
  widths) is where bugs would be; the unit tests and M2's trace identity cover
  it.
- **Approximate timing in M1.** Interrupts and events land late by up to a
  block in `on` mode, so frames can drift from `off`; loops waiting on the APU
  are the most sensitive (see "Syncing with the SPC700" in
  [PORTING.md](PORTING.md)). M2 removes it.
- **Precedence surprises.** A hand port whose precondition rejects a call now
  falls back to a generated block rather than the interpreter; both are exact,
  so this only changes speed.
- **Stale analysis.** Blocks come from the analysis at build time; code the
  movies reach later falls back to the interpreter until coverage is fed back
  (M3). This is a speed issue, never a correctness one.
- **ROM-derived output.** Everything the generator writes is ROM-derived: it
  stays in the build tree, and nothing that prints generated code writes into
  the repository.

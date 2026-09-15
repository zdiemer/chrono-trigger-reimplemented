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
| M1 | Blocks for all statically found code, built from the ROM, registered with the native runtime; 0 shadow mismatches over the movies | done |
| M2 | Exact timing: interrupts on the interpreter's instruction boundaries, loop re-entry | done |
| M3 | Closing the interpreted remainder: miss report, RAM code as guarded blocks, coverage feedback | done |
| M4 | Optimizations: dead flags from liveness, direct block-to-block transfers, fast dispatch | |
| M5 | Cleanup workflow from a generated block to a hand port | designed |

## Instruction translation (M0)

| Path | What |
|---|---|
| `tools/recomp/translate.py` | One instruction to C statements |
| `runtime/port/recomp.h` | The helpers those statements call (`rc_*`) |
| `runtime/port/recomp_snes.h` | Binds the helpers to the development system's bus (through the Cpu's bus hooks) |
| `tools/recomp/linear.py` | Prints a linear run of ROM code as a C function, for reading the output (stdout only) |
| `tools/recomp/sst.py` | Generates the SingleStepTests harness (below) |

`translate(opcode, operand, pc, pb, ctx, flow)` returns the statements, the
instruction size, whether execution falls through, the widths after it and its
static jump and call targets. The statements operate on `Cpu *c` and do exactly
what `runtime/cpu/cpu65816.c` does:

- **Registers and flags**, decimal-mode `ADC`/`SBC` included (the D flag is
  tested at run time; everything width-dependent is fixed at generation).
- **Bus accesses in the interpreter's order** through `RC_READ`/`RC_WRITE`,
  which `recomp_snes.h` maps to the Cpu's bus hooks, `snes_bus_read`/
  `snes_bus_write` on the development system, so I/O side effects match. Read-modify-write instructions write the high byte first;
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
  plus open bus, bus clocks, internal cycles, interrupt/halt state and (since
  M2) the kind and length of the last cycle, where interrupts are sampled. The
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

| Path | What |
|---|---|
| `tools/recomp/__main__.py` | `python3 -m tools.recomp`: runs the analysis, writes the shards and the table |
| `tools/recomp/blocks.py` | Entries, block formation, C emission |
| `cmake/recomp.cmake` | The build step and the `ct_recomp` library (generated, or the stub) |
| `runtime/port/recomp_block.h` | Included by generated shards: `RC_INSN`, `RC_YIELD` (and `RC_IRQ_UNMASKED`, M2) |
| `runtime/port/recomp_table.h`, `recomp_table_stub.c` | The table's declaration, and the empty table built without a ROM |
| `runtime/port/native.c` | Several entries per address, running blocks in `on` mode, checking them in `shadow` mode |
| `tests/test_recomp_blocks.py` | Entries and block boundaries on synthetic code |

The generator runs the disassembler's analysis in process
(`tools.disasm.analyze.Analyzer` with `re/hints.toml` and the coverage files),
which gives every context, the widths at each instruction and jump table
lengths.

Where M1 departs from its design: every other entry ends a block (the design
kept a block's own loop heads inside it), exits to analysed code without an
entry get one (closure), loops polling timed registers return on every pass,
a block's exact time is returned as the step's time rather than charged as
debt, accesses go through the Cpu's bus hooks, and shadow mode replays blocks
against a record of the interpreter instead of cloning the machine for sampled
entries (below).

**Entries** are the addresses the runtime can arrive at, one per width variant
the analysis has there (an unknown width gives both variants):

1. every context entry (vectors, call and `JML` targets, hints, coverage seeds);
2. the return address of every call instruction (`JSR`, `JSL`, `JSR (abs,X)`),
   with the widths the analysis resumed with;
3. `JMP (abs,X)` table targets;
4. the instruction after a `PLP` or `XCE`;
5. loop heads (same-bank targets of backward branches and jumps) and
   `MVN`/`MVP` instructions;
6. every 24-bit ROM address in `game/registry.c` (hand ports and idle loops);
7. call targets from `--calls` logs, with the analysis's widths there;
8. closure: a block exit to analysed code at widths without an entry (an
   address reached with other widths than its entry's) becomes an entry, and
   blocks are rebuilt until there are none.

A **block** is one C function per entry (`recomp_C08000_m8x16`): the
instructions reachable from it through fall-through, branches, `JMP abs`, `BRL`
and `JML`, with widths following `REP`/`SEP`. Every other entry address ends
the block (PC set, return), so each loop has exactly one copy: the block of its
head. A transfer to the block's own entry is its loop's back edge:
`if (RC_YIELD(c)) { c->pc = head; return; } goto L_head;`. An instruction the
analysis doesn't have as code under the block's widths, a second arrival at an
instruction with other widths, a cross-bank target and everything dynamic
(calls, returns, indirect jumps, `BRK`/`COP`, `WAI`/`STP`, widths after
`PLP`/`XCE`) end the block with PC (and PB) set. Each instruction starts with
`RC_INSN`, which counts executed instructions for `--insn-stats` (and, since
M2, is where a block can stop; see "Exact timing").

In M1 a loop that read a register that changes with time on its own
(`$2137-$213F`, the APU ports, `$4210-$4212`) returned to the runtime at every
back edge instead, since time only passed between blocks. M2 removed that.

Correctness doesn't depend on the analysis being right about widths: an entry
only runs when P's M and X match the widths it was generated for, and within a
block widths change only through constant `REP`/`SEP`, so every instruction
decodes as the interpreter decodes it. A wrong analysis only produces blocks
that never run or that stop early.

## Build (M1)

`cmake/recomp.cmake` adds the step when Python 3.11+ is found and `CT_ROM`
exists:

```sh
cmake -S . -B build/main -G Ninja -DCT_ROM=roms/ct-usa.sfc \
    "-DCT_RECOMP_COVERAGE=traces/coverage-*.txt;traces/saves/cov-*.txt" \
    "-DCT_RECOMP_CALLS=traces/calls-*.txt;traces/saves/calls-*.txt"
cmake --build build/main
```

| Cache variable | Default | |
|---|---|---|
| `CT_ROM` | `roms/ct-usa.sfc` | the ROM; without it `ct_recomp` is the empty stub table and CMake says so |
| `CT_RECOMP_COVERAGE` | none | coverage files or glob patterns, `;`-separated |
| `CT_RECOMP_CALLS` | none | call logs or glob patterns |
| `CT_RECOMP_SHARDS` | 64 | generated source files |
| `CT_RECOMP_OPT` | `-O1` | optimization level for the shards |

Coverage and call logs are optional and gitignored (`traces/`). They come from
the development system running movies, for example:

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --movie oracle/movies/gato-battle.ctm --frames 21600 \
    --coverage traces/coverage-gato.txt --calls traces/calls-gato.txt
```

Coverage matters: it adds the code only the movies reach (the analysis goes
from 94400 to 109800 instructions with the 19 coverage files of the main and
save movies).

- `python3 -m tools.recomp` verifies the ROM's CRC-32 (`2D206BF7`), then writes
  `blocks_000.c` ... `blocks_063.c` and `table.c` (`recomp_natives[]` and
  `recomp_rom_crc`) into `build/.../recomp/`. A block's shard is a hash of its
  address's kilobyte, so a change to one routine touches few shards. Output is
  deterministic, and files whose content didn't change aren't rewritten: the
  custom command's output is a stamp and the sources are byproducts, so Ninja
  (which re-checks byproducts' times) recompiles only changed shards.
- It reruns when the ROM, `re/hints.toml`, `game/registry.c`, the coverage and
  call files, or `tools/recomp`/`tools/disasm` change. A new coverage file
  matching a pattern is picked up at the next configure.
- Numbers (this ROM, the 19 coverage files and 18 call logs): 10667 blocks,
  127518 instructions (the analysis has 109832; loop-free code shared by several
  entries appears in each), 20 MB of C. Generation takes about 10 s (analysis
  6-17 s depending on load, blocks 3 s). A clean configure and build of the
  whole project with `-j 4` took 44 s; regenerating with nothing changed
  rewrote no file and recompiled nothing (10 s). `-O2` for the shards took 52 s
  instead of about 30 s to compile and made `on` mode no faster (the renderer
  dominates, below), so the default is `-O1`.
- `ct_dev` and `ct_play` link `ct_recomp`. They register generated entries
  after the hand-written ones (`--recomp on`, the default) only if the loaded
  ROM's CRC-32 is the table's, and warn otherwise. `--recomp off` leaves them
  out; `ct_dev --exclude recomp_C08000` leaves out blocks by name prefix.
  `ct_fuzz` doesn't use them.

## Registration and `on` mode (M1)

Generated entries are `NativeEntry`s with `.generated = true`,
`.p_mask = CPU_FLAG_M | CPU_FLAG_X` and `.p_value` for the widths (native mode
only, and not while the CPU is halted by `WAI`/`STP`). `cost_milli`,
`dont_care` and `idle` are unused; since M3 blocks of RAM code have a `guard`
and the reset vector's block is an `emulation` entry.

- **Several entries per address.** The table is sorted by address with
  hand-written entries before generated ones; `find_entry` returns the first
  entry at the address and `intercept` tries them in order. A hand port wins,
  and a generated block runs when the port's precondition or guard rejects the
  call, or when an idle entry is skipped. Without generated entries the path is
  the same as before: over the six main movies, `--native shadow --recomp off`
  prints exactly the same report as before M1.
- **Running a block.** Blocks run directly on the machine, without a fiber
  (they never call out). The intercept clears `bus.clocks` and `io_cycles`,
  calls the block and returns `bus.clocks + 6 * io_cycles` as the step's time:
  exact, so no debt and no second look at the same PC. The system loop then
  advances time, polls interrupts as after an instruction, and calls the
  intercept again at the new PC: a hand port, another block, or the
  interpreter. In M1 interrupts landed at block boundaries; M2 makes them land
  where the interpreter's do.
- **Loops.** In M1 `RC_YIELD` was true once the block had spent a scanline
  (`ct_dev --recomp-yield`, removed in M2).
- **Bus.** `recomp_snes.h` makes `RC_READ`/`RC_WRITE` call through the Cpu's
  bus hooks (`snes_init` points them at `snes_bus_read`/`snes_bus_write`) and
  `RC_FETCH` checks that the hook is the SNES bus, so shadow mode can serve a
  block's accesses from a record (below). `recomp_bus_diff` and
  `sst_recomp_runner` still pass with 0 mismatches and failures.
- **Calls and returns.** Generated code never calls anything: a `JSR` pushes
  the return address, sets PC and returns; the callee (whatever runs it) returns
  to that address, which is an entry. All state is on the emulated stack, so
  hand ports, blocks and the interpreter call and return into one another in any
  combination, and hand ports' call-outs resume at a block's return as before.
  `native_call` (synchronous, for tools) runs the first applicable entry, a
  block included.
- **Reports.** Generated entries are summarized in one line: entries, how many
  were reached, calls, and in `shadow` mode matched, mismatched, interrupted
  and unfinished, or in `on` mode the instructions executed. Mismatch details
  follow as for hand ports. `ct_dev --insn-stats` prints the instructions the
  interpreter ran (and how many of those were RAM code) against those in
  generated blocks.

## Shadow validation (M1)

The M1 design planned to clone the machine for sampled block entries, as for
hand ports, and not to start a block's check inside another routine's stretch. Checking every block entry that way would copy and compare a few
hundred kilobytes about a thousand times a frame, and sampling leaves paths
unchecked. What M1 does instead checks every block:

- **Record.** While the interpreter runs everything (as for hand ports), the
  CPU's bus hooks record every access it makes: address, direction, value and
  the bus clocks it took (a `$420B` write includes its DMA). HDMA, DMA and the
  APU don't go through the CPU's hooks and aren't recorded. At every step the
  intercept records a boundary: registers, PB:PC, E, master clock, frame, the
  number of accesses so far, and whether the step is an interrupt entry or the
  CPU is halted. Hand ports' clones don't record.
- **Replay.** When the record fills up (131072 boundaries or about 2 million
  accesses) and at the end of the run, it's replayed from the start. At each
  boundary that isn't an interrupt entry, halted or emulation mode, the first
  generated entry whose widths match runs on a scratch CPU loaded with that
  boundary's registers, with bus hooks that serve the record: every read,
  write and fetched byte must be the next recorded access at the same address
  (writes with the same value, the last fetched byte with the instruction's
  byte), and reads return the recorded value. Everything a block can observe
  comes through those accesses. When the block returns, the record must be at
  a boundary (not inside an instruction of the original), and that boundary
  must have the block's registers, P, E, halt state and PC, with
  `recorded clocks + 6 * io_cycles` equal to the master clocks that passed.
  The next check starts at that boundary, so blocks chain as `on` mode
  dispatches them; where no block applies, the next boundary is tried.
- **Interrupts.** A block's accesses are limited to the next interrupt
  boundary. If it runs out there at the first fetch of an instruction, its
  registers and time so far are compared with that boundary (counted as
  interrupted); running out inside an instruction is a mismatch.
- **Every back edge yields** (`RC_YIELD` is true outside `on` mode), so a check ends at
  each loop pass and the next pass is checked from its head.
- Checks don't touch the machine, so they run everywhere, including inside
  hand ports' segments, and hand ports' reports are the same with and without
  generated blocks.
- A mismatch report names the block, the frame the check started in, the
  boundary it stopped at and the first difference, e.g. `block read 7E0152
  where the original wrote 01 to 7E0152` or `P original 24 block A4`.

Shadow runs take about 1.5 times as long as without generated blocks
(`new-game`: 48 s against 32 s).

The check catches translation bugs: with `rc_inc8` deliberately not updating N,
`new-game` reported 150 mismatching blocks (`P original 24 block A4`). While
building it, it also caught its own first bug: the record was replayed before
the boundary of the step whose accesses it had just recorded, which showed as
261 blocks "stopped inside an instruction of the original".

### Results (M1)

Built from the ROM with the 19 coverage files and 18 call logs (10667 blocks).
`shadow` is `--native shadow`; checks are block runs replayed against the
original; `on` is `--native on --insn-stats`, with the share of executed
instructions that ran in generated code (the rest ran on the interpreter; hand
ports' native code counts in neither) and how the frame at the end compares
with an interpreter-only run (`--native off`) at the same frame.

| Movie | Frames | Shadow checks | Mismatched | Interrupted | `on` generated | `on` last sampled frame vs `off` |
|---|---|---|---|---|---|---|
| `new-game` | 7200 | 35428858 | 0 | 4392 | 91.6% | 0.4% of pixels differ |
| `menu` | 8900 | 46825421 | 0 | 5532 | 92.7% | 0.1% |
| `field` | 11400 | 60240190 | 0 | 8726 | 94.9% | identical |
| `fair` | 12450 | 65081101 | 0 | 9703 | 95.5% | 0.4% |
| `fair-companion` | 17900 | 90340864 | 0 | 15594 | 97.2% | identical |
| `gato-battle` | 21600 | 107758656 | 0 | 18618 | 97.4% | 4.0% (after the won fight, same place) |
| `saves/battle-magus` | 18000 | 75790698 | 0 | 12861 | 95.6% | 4.8% |
| `saves/battle-golem-twins-gameover` | 12400 | 42122654 | 0 | 6829 | 88.8% | identical |
| `saves/battle-tyrano-lair-walls` | 8200 | 31672259 | 0 | 5159 | 92.2% | identical |
| `saves/late-omen-battles` | 17100 | 71887400 | 0 | 11253 | 94.2% | 40.1% (another room: the walk drifted) |
| `saves/late-omen-terramutant` | 15500 | 56542207 | 0 | 9106 | 93.2% | 73.3% (the fight went differently) |
| `saves/late-omen-zeal` | 18800 | 74730738 | 0 | 11661 | 93.3% | 39.7% |
| `saves/events-jetbike-race` | 9400 | 25395501 | 0 | 7362 | 84.7% | 3.2% |
| `saves/events-end-of-time-gate` | 5600 | 18447478 | 0 | 3937 | 93.9% | 0.4% |
| `saves/events-prison-escape` | 12900 | 55524789 | 0 | 12745 | 96.9% | 43.5% (same room; the same with hand ports only) |
| `saves/events-keepers-dome` | 5000 | 17580495 | 0 | 2977 | 91.5% | identical |
| `saves/events-blackbird` | 4400 | 15788073 | 0 | 2673 | 88.7% | identical |
| `saves/menus-field-menu` | 10600 | 55228022 | 0 | 4751 | 94.4% | 7.0% |
| `saves/menus-shop-inn` | 9700 | 41264596 | 0 | 6706 | 95.7% | identical |
| `saves/menus-epoch` | 8800 | 34072503 | 0 | 3519 | 96.0% | 0.3% |
| `saves/menus-end-of-time` | 8800 | 29111440 | 0 | 6556 | 96.3% | identical |

- **Shadow:** all 21 movies exit 0 with no mismatch, hand ports included:
  1050833943 block checks, 0 mismatched, 170660 interrupted, 8 unfinished (the
  block running when a movie ends). No block is excluded and no hint was needed.
- **On:** all 21 run to the end without hangs. Sampling every 100th frame,
  1405 of 2447 frames are identical to the interpreter-only run; with hand
  ports only (`--recomp off`) it's 1391, and the late-game battle movies drift
  the same way there (TerraMutant 75% of pixels differ, Zeal 60%). Only
  `late-omen-battles` ends in a different room with generated blocks and not
  without: battles run on real time, and input landing a frame apart changes
  them.
- **Interpreted code** is almost all RAM code: of the 2.3 million
  instructions `gato-battle` still interprets, 2.08 million are in RAM (the
  attract sequence); in `new-game` 2.05 of 2.12 million; in
  `events-jetbike-race` 10.87 of 11.08 million (the race's RAM code).
- **Speed** (`gato-battle`, 21600 frames, on a machine shared with other
  jobs): `--native off` 44.7 s, `--native on --recomp off` 36.5 s,
  `--native on` 33.9 s. A sampling profile of `on` mode puts 81% of the time in
  the scanline renderer (`render_line`, `draw_bg`), 5% in the S-DSP and 2% in
  the SPC700; the CPU side (blocks, dispatch, bus) is about 7%, against about
  20% for the interpreter in `off` mode. Until rendering is faster, generated
  code can't make runs much faster. (Since then the renderer draws a tile at a
  time and skips unchanged lines: `on` runs `gato-battle` in about 7 s, a
  quarter of it in the renderer; see Speed under Renderer in
  [RUNTIME.md](RUNTIME.md).)

## Exact timing (M2)

In `on` mode a generated block now runs with the machine's time right at every
instruction: everything that happens between two of the interpreter's steps
(line events, interrupt recognition, the end of a frame) happens between the
same two instructions, and I/O sees the master clock the interpreter's I/O
sees. With only generated code (`ct_dev --recomp only`), `--native on` produces
the same traces and audio as `--native off` over every movie (Results below).

| Path | What |
|---|---|
| `runtime/snes/system.c` | `snes_horizon`, `snes_sync`, and the last cycle native code reports (`intercept_last_cycle`) |
| `runtime/port/recomp_snes.h` | `RcTiming`; `RC_READ`/`RC_WRITE` sync before I/O; `RC_IO` and `RC_FETCH` keep the last cycle |
| `runtime/port/recomp_block.h` | `RC_INSN(c, pc)`: the stop check, and `RC_STOP(block)` at the end of each block; `RC_YIELD`; `RC_IRQ_UNMASKED` |
| `runtime/port/native.c` | `run_block`, `rc_io_sync`, where blocks stopped |
| `tools/recomp/blocks.py` | Blocks that start at any of their instructions, `RC_IRQ_UNMASKED` after `CLI` and `REP` clearing I |
| `dev/ct_dev.c` | `--recomp only` |

### What the interpreter does between steps

The system loop (`snes_run_frame`) runs one step (an instruction, an interrupt
entry, or native code), advances the machine by the step's time, handles an
NMI request from a `$4200` write, and samples interrupts at the start of the
step's last cycle ([RUNTIME.md](RUNTIME.md), "Interrupt timing"). Advancing is
where time-driven things happen: the APU runs, the H counter and hblank flag
move, and line events fire (the renderer and HDMA at hclock 1104 of visible
lines, the end of the line with vblank, RDNMI, auto-joypad, V-timer and the
frame's end, H-timer IRQs at HTIME).

Advancing by `a` and then by `b` does the same as advancing by `a + b` as long
as the CPU does nothing observable in between: the SPC700 and DSP run the same
cycles against the same port values, and each line event fires once at its
position either way. So several instructions can run back to back and have
their time applied at once, as long as (1) no line event or interrupt
recognition falls between them, (2) whatever they do that depends on time sees
the right time, and (3) nothing they do changes what the system would do.

### The horizon

`snes_horizon` is the latest master clock at which the next instruction may
start with nothing for the system to do before it. A line event at hclock `P`
fires in the advance that goes past `P`, so an instruction starting at `P` or
before still runs before it: the horizon is the smallest of

- the line's end, hclock 1363 (the line wraps when an advance reaches 1364);
- on visible lines, the HDMA point 1104 if it hasn't fired yet (rendering needs
  it even without HDMA channels: the renderer reads the PPU state as of then,
  and HDMA reads tables from WRAM the CPU may be writing);
- the H-timer position minus one, when the timer mode and line make it fire;
- an NMI that is scheduled, and a raised IRQ that hasn't been recognized yet,
  minus one: they're recognized after a step whose last cycle starts at or
  after their time, which only a step ending after it can have.

An IRQ that is recognized but masked by I isn't part of it (see below).

### Stopping

`run_block` gives the block the clocks until the horizon as its limit.
`RC_INSN`, at the start of every instruction, compares the block's time so far
(`bus.clocks + 6 * io_cycles`) with the limit: past it, the block sets PC to
that instruction and returns through `RC_STOP`. The first instruction always runs, so every
block makes progress. The system then advances by the block's time, handles
the NMI request and samples interrupts with the last cycle of the block's last
instruction, exactly as after the interpreter's step for that instruction: all
the instructions before it ended before the horizon, and their poll points
recognized nothing.

The last cycle is what the interpreter tracks with `last_cycle_io` and the
bus's `access_start`: `RC_IO` sets `last_cycle_io`, `RC_READ` and `RC_WRITE`
clear it (the bus sets `access_start`), and `RC_FETCH` clears it and sets
`access_start` for the last byte. Fetches are emitted before the operation in
generated code but interleave with it in the interpreter; `recomp_bus_diff` now
also compares the last cycle's kind and length, and all 256000 instructions
match.

A loop's back edge no longer returns in `on` mode (`RC_YIELD` is false there):
the head's `RC_INSN` stops the loop when time is up. This replaces M1's
one-scanline budget and the return on every pass of loops polling timed
registers. Shadow replays still return at every back edge.

### Resuming

A block can stop before any of its instructions, most of them not entries. So
every block starts where PC points: its function begins with a `switch` on PC
with a case per instruction (the entry's falls through to the first one), and
every instruction has a label. `run_block` remembers the block that stopped,
keyed by the 24-bit address and the M/X widths at the stop, in a direct-mapped
table of 4096 slots; when the intercept finds no entry at PC, it looks there
and continues the block. Only a block that stopped at that address with those
widths is ever found, and its code for that instruction was generated for
those widths, so this is as safe as an entry. A miss (an interrupt handler's
stops took the slot) runs the interpreter up to the next entry, which costs
speed only. Over `gato-battle` with only generated code, blocks stopped 8.9
million times (411 per frame) and 6.4 million of those resumed from the table;
nearly all the rest stopped at their loop heads, which are entries.

### I/O

`RC_READ` and `RC_WRITE` check whether the address is in the CPU I/O area
(`$2000-$5FFF` in banks `$00-$3F` and `$80-$BF`) while an `on` mode block runs.
There `rc_io_sync` first advances the machine to the start of the current
instruction (`snes_sync`, which can't reach a line event: the instruction
started before the horizon), which is the time the interpreter's accesses see,
since it advances after each instruction. That covers the H/V counters and
their latch, `$4210-$4212`, the APU ports (the SPC700 sees writes and has
produced answers at the same master clock) and everything else in the area
except the WRAM port (`$2180-$2183`) and the multiplier and divider
(`$4202-$4206`, `$4214-$4217`), which only CPU writes change.

Two kinds of instruction make an interrupt deliverable, or move one, without
time passing: writes to `$4200-$420A` (enabling NMI while RDNMI is set requests
one; the timer settings move the H/V IRQ) and clearing I (`CLI`, `REP` with bit
2), where a recognized IRQ may be waiting. After those the block's limit drops
to 0, so it stops before the next instruction and the system decides, as after
any stop.
`PLP` and `RTI` end blocks already.

### Where M2 departs from its design

- The design synced on `$2100-$21FF` and `$4000-$43FF`; the whole I/O area
  syncs, except the registers above that only CPU writes change.
- The design had the instructions that make an interrupt deliverable check for
  one and stop; they stop unconditionally at the next instruction and leave the
  check to the system loop, which is the same code as for any step.
- hblank start and end aren't horizon points: nothing changes then except
  `$4212`'s bit, which I/O syncs for.
- The design didn't say how a block continues after stopping inside it:
  resumable blocks and the table of stops above.
- `ct_dev --recomp only` registers the generated blocks without the hand ports;
  `--recomp-yield` is gone.

### Results

Built from the ROM with the 19 coverage files and 18 call logs (10671 blocks,
127536 instructions). "Traces" compares `--native on --recomp only` with
`--native off` over every frame: the WRAM, VRAM, SRAM, CGRAM, OAM and
written-WRAM hashes and the framebuffer hash of each frame. "Generated" is the
share of instructions that ran in generated code in that run (the attract
sequence's RAM program, 13 million instructions, and the other RAM code run on
the interpreter, since hand ports for them are left out). "Stops" is how often
blocks stopped at the horizon.

| Movie | Frames | Traces | Generated | Stops |
|---|---|---|---|---|
| `new-game` | 7200 | identical | 87.4% | 2702393 |
| `menu` | 8900 | identical | 89.8% | 3489959 |
| `field` | 11400 | identical | 92.3% | 4639666 |
| `fair` | 12450 | identical | 92.9% | 5111960 |
| `fair-companion` | 17900 | identical | 95.1% | 7547523 |
| `gato-battle` | 21600 | identical | 95.9% | 8875081 |
| `saves/battle-magus` | 18000 | identical | 94.5% | 6118154 |
| `saves/battle-golem-twins-gameover` | 12400 | identical | 84.3% | 3672714 |
| `saves/battle-tyrano-lair-walls` | 8200 | identical | 87.8% | 2499672 |
| `saves/late-omen-battles` | 17100 | identical | 94.1% | 5436074 |
| `saves/late-omen-terramutant` | 15500 | identical | 93.3% | 4726429 |
| `saves/late-omen-zeal` | 35300 | identical | 97.1% | 11404715 |
| `saves/late-lavos` | 51000 | identical | 98.0% | 16015208 |
| `saves/events-jetbike-race` | 9400 | identical | 82.9% | 3260207 |
| `saves/events-end-of-time-gate` | 5600 | identical | 83.0% | 1877218 |
| `saves/events-prison-escape` | 12900 | identical | 93.1% | 5199595 |
| `saves/events-keepers-dome` | 5000 | identical | 80.5% | 1603579 |
| `saves/events-blackbird` | 4400 | identical | 77.2% | 1351581 |
| `saves/menus-field-menu` | 10600 | identical | 90.8% | 4153296 |
| `saves/menus-shop-inn` | 9700 | identical | 90.3% | 3751780 |
| `saves/menus-epoch` | 8800 | identical | 89.4% | 3330399 |
| `saves/menus-end-of-time` | 8800 | identical | 89.2% | 3228659 |

- **Traces:** all 22 movies (312150 frames) identical. `--wav`
  output of `new-game` and `events-jetbike-race` is byte-identical too.
- **Shadow** (`--native shadow`, hand ports and generated blocks) over the 22
  movies: 1337405091 block checks, 0 mismatched, 211792 interrupted, 9
  unfinished, and every hand port's counts as before; each movie's report is
  byte-identical to the one M1 prints for it (shadow replays don't stop at the
  horizon, and back edges still return there). With `--recomp off` the reports
  are identical to M1's too.
- **Conformance:** `sst_runner` and `sst_recomp_runner` 0 failures,
  `recomp_bus_diff` 0 mismatches (with the last-cycle check).
- **Speed** (`gato-battle`, 21600 frames, process CPU time averaged over four
  alternating runs each on a machine shared with other jobs): `--native on`
  40.8 s with the M1 build and 41.1 s with M2; `--native on --recomp only`
  42.9 s; `--native off` 46.7 s and 46.4 s. The renderer still dominates (see
  M1's results); the stop check before every instruction, the 411 stops per
  frame and the I/O syncs cost about 1%, within the noise. The generated C grew
  from 20 to 27 MB (a label, a stop check and a `switch` case per instruction)
  and `ct_dev` from 15 to 26 MB.

### Hand ports in `on` mode

Hand ports still charge estimated time (`cost_milli` times their bus clocks)
after they stop, a scanline at a time with interrupts held, and sync explicitly
where they talk to the APU ([PORTING.md](PORTING.md)). Their time can't be made
exact without running the original, and any error moves everything after it:
registering the hand ports of one subsystem at a time (with all generated
blocks), `new-game` departs from the interpreter-only run for every subsystem
whose ports it reaches, most within frames 22-40 (`memory` and `menu` at 1750),
so no movie stays identical with hand ports.

How close `on` mode gets, as the share of frames whose framebuffer is the
interpreter-only run's at the same frame ("same"), and whose framebuffer
appears in that run within 120 frames ("near", a looser measure of being in
the same scene), with hand ports and generated blocks:

| Movie | M1: same / near | M2: same / near |
|---|---|---|
| `new-game` | 70.5% / 99.9% | 70.5% / 99.9% |
| `menu` | 74.0% / 99.5% | 74.1% / 99.6% |
| `field` | 75.8% / 99.3% | 75.8% / 99.4% |
| `fair` | 71.8% / 94.7% | 71.8% / 94.2% |
| `fair-companion` | 65.8% / 89.2% | 74.8% / 93.2% |
| `gato-battle` | 56.9% / 77.0% | 64.7% / 81.1% |
| `saves/battle-magus` | 43.9% / 45.1% | 27.6% / 36.1% |
| `saves/battle-golem-twins-gameover` | 36.6% / 53.5% | 41.7% / 50.2% |
| `saves/battle-tyrano-lair-walls` | 43.5% / 47.2% | 54.0% / 58.3% |
| `saves/late-omen-battles` | 29.0% / 34.0% | 34.7% / 37.4% |
| `saves/late-omen-terramutant` | 22.3% / 27.0% | 22.3% / 27.0% |
| `saves/late-omen-zeal` | 9.4% / 11.0% | 9.4% / 11.0% |
| `saves/late-lavos` | 6.4% / 9.0% | 7.3% / 9.9% |
| `saves/events-jetbike-race` | 52.1% / 66.7% | 62.7% / 68.1% |
| `saves/events-end-of-time-gate` | 62.6% / 91.2% | 68.9% / 91.2% |
| `saves/events-prison-escape` | 57.7% / 95.0% | 57.8% / 95.1% |
| `saves/events-keepers-dome` | 73.0% / 87.6% | 73.0% / 87.6% |
| `saves/events-blackbird` | 96.3% / 98.9% | 96.3% / 98.9% |
| `saves/menus-field-menu` | 92.0% / 99.7% | 92.0% / 99.5% |
| `saves/menus-shop-inn` | 90.9% / 95.9% | 90.9% / 96.0% |
| `saves/menus-epoch` | 69.5% / 88.2% | 70.3% / 89.2% |
| `saves/menus-end-of-time` | 66.8% / 79.8% | 62.9% / 78.8% |
| all frames | 44.4% / 56.3% | 45.8% / 56.9% |

Exact blocks help a little overall, and most in movies with battles and
races, where the M1 blocks' late interrupts added to the drift; `battle-magus`
and `menus-end-of-time` went the other way, which is the same drift landing
differently. The late battles still go differently from the interpreter-only
run: in `late-lavos` the party ends up in the tech menu against the outer shell
from about frame 25000 to the end in both M1 and M2 (the movie's input no
longer fits the fight; the menu keeps moving, the game doesn't hang), while
`--native off`, and `--native on --recomp only`, reach Lavos's core. Two ways of
giving hand ports more exact time were tried on six movies (`new-game`, `field`,
`gato-battle`, `late-omen-battles`, `late-omen-terramutant`,
`events-prison-escape`) and not kept:

- **Sync before time-observing I/O**, as generated blocks do: every hand port
  access to the APU ports, `$2137-$213F` and `$4210-$4212` (and in a second
  variant also PPU register and `$4200-$420C` writes) first let the time the
  routine had taken so far pass (`native_sync`). Same-frame agreement changed by
  at most 0.1 point, except `late-omen-battles` with the second variant (34.7%
  to 31.8%). The sound driver ports, which already sync, are nearly all the
  time-observing I/O hand ports do.
- **No idle entries in `on` mode**, so the busy-wait loops run as exact
  generated blocks instead of skipping ahead: worse (`field` 75.8% to 49.9%,
  `gato-battle` 64.7% to 53.0%, `new-game` 70.5% to 57.9%).

So `--recomp only` is the configuration for checking `on` mode against the
interpreter; hand ports are checked by shadow mode as before.

## Closing the interpreted remainder (M3)

After M2 generated blocks ran 77-98% of the instructions; the interpreter ran
the rest: code in RAM (the program decompressed to `$7E:3000`, the code the jet
bike race generates at `$7E:B574`, the `$00:0500` interrupt stubs, the
`$7E:9690` block move stub), the reset vector's first instructions (emulation
mode), and ROM code without an entry. M3 measures that remainder and removes
it: over the 25 movies of the library, `--native on --recomp only` now runs
every instruction in generated code.

| Path | What |
|---|---|
| `dev/misses.c` | `ct_dev --misses`: the miss report |
| `tools/recomp/blocks.py` | Spaces (ROM and each RAM section), guards, live operands, the emulation-mode reset block |
| `tools/recomp/__main__.py` | The instruction index (`recomp_index[]`), section images and guard masks in `table.c` |
| `tools/recomp/translate.py` | `live`: operands fetched at run time (`RC_FETCH_LIVE`) |
| `runtime/port/recomp_block.h`, `recomp_snes.h` | `rc_ram_matches`, `RC_CODE`, `RC_STOP_GUARDED`, `RC_FETCH_LIVE`, the self-write check |
| `runtime/port/native.c` | Guards on generated entries and resumes, `native_set_index` and continuing at any instruction, guards decided while recording in shadow mode, emulation-mode entries |
| `tools/disasm/ram.py`, `re/hints.toml` | Section formats `run` and `writes` from RAM code, `patch` sections |

### The miss report

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --movie oracle/movies/new-game.ctm --frames 7200 \
    --native on --recomp only --misses traces/misses-newgame.txt
```

Every instruction the interpreter runs is counted by address, widths and
emulation mode (from the CPU's instruction hook, so it costs nothing without
the option) and written as `BB:AAAA m x # count kind`, which is also the
coverage format: the ROM lines can seed the analysis directly. The kind is
`ROM`, `RAM` with the section whose image holds the instruction's bytes when it
ran (`recomp_ram_sections[]`, which the generator writes with the images),
`modified` (in a section's range, but with bytes no image has, and whether the
opcode or only operands differ) or `unknown` (in no section). A summary line
per kind goes to stderr. With `--native on --recomp only` everything that isn't
generated code is interpreted, so the report is the closure check C7 of
[MATCHING.md](MATCHING.md).

Before M3 (the M2 build, 20 coverage files, 19 call logs), every movie had
13.2 million RAM instructions (the attract program, which every movie runs up
to the title screen or the load menu) plus:

- **ROM**: 3 to 166021 instructions at 3 to 516 addresses. Three in every
  movie are the reset vector's `SEI`, `CLC`, `XCE` in emulation mode. The rest
  were instructions of existing blocks reached where no entry or stop applied:
  the computed entry into the `LDA`/`STA` run at `$CE:E293` in battles (up to
  400 runs per address), and resumes whose slot an interrupt handler had
  taken. None was code outside the analysis, not even in the three ending
  movies, which had no coverage files then (below).
- **RAM, modified**: the race and the endings patch the program (operands at
  13 addresses, opcodes at `$7E:3634`, `$3635`, `$510D`, `$A074` and `$A075`),
  the `$00:0500` stubs jump to handlers the partial installers write, and menu
  code in C2 and FF writes the `$7E:9690` stub over the program's bytes.
- **RAM, unknown**: the race's generated code, 2065 addresses (2.1 million
  instructions in `events-jetbike-race`, 6.5 million in `endings-beyond-time`).

### RAM code as generated blocks

The analysis already traces the RAM sections ([[ramcode]] hints) from their
images, checked by the matching build (MATCHING.md, MD1). The generator now
forms blocks for them too. Each section is a *space*: an entry is (address,
widths, space), a block's instructions are decoded from its section's image and
never leave it, and blocks are named after the section
(`recomp_7E3000_m16x16_attract_program`). Several sections at one address are
alternatives, each with its own blocks (the seven `$00:0500` stubs, the block
move stub over the program, the patches below).

- **Guards.** A RAM block's entry gets a `guard`: the bytes its instructions
  were decoded from must equal the image. It compares, for each run of the
  block's instructions (merged when less than 16 bytes apart), work RAM with
  the image under a mask (`rc_ram_matches`, 8 bytes at a time): a byte is
  checked where some block of the section decodes an opcode or an operand that
  isn't live (below), so data between instructions and patched operands don't
  matter. Guards read `bus.wram` directly, so they only peek. `entry_applies`
  checks them for generated entries as for hand ports, a resume checks the
  stopped block's guard (`RC_STOP_GUARDED`), and the index checks each
  candidate's.
- **Live operands.** The program patches operands at run time: call targets
  (`$7E:3023`, `$7E:35FC`), immediates and addresses in the race's and the
  endings' matrix code, the JML targets of the interrupt stubs, the stub's
  `MVN` banks. So the operands of RAM instructions are fetched at run time
  (`translate(..., live=True)`: `RC_FETCH_LIVE` charges the fetch like
  `RC_FETCH` and keeps the bytes in `rc_timing.op[]`, from the bus in `on` mode
  and from the record in shadow replays), except those that shape the block:
  `REP`/`SEP` operands, and branch and `JMP` targets unless analysed code
  stores to them. A patched branch (the `BRA` at `$7E:3616`) is live and still
  continues in the block when it has the image's target.
- **Self-modification inside a run.** A block may write its own code (the
  program's stores at `$7E:3596`-`$35F9` patch operands of `$7E:360A`-`$3633`
  just after them). A RAM
  block sets `RC_CODE` to its instruction ranges; a write there, through the
  WRAM port or a DMA transfer ends the block before its next instruction
  (`rc_code_write` sets the limit to 0, as for `CLI`), and it resumes only if
  its guard still holds. Nothing else runs while a block runs, and whatever runs
  between two of its steps is followed by a guard check.
- **Patched opcodes** are `patch` sections: `patch_3634_skip` and
  `patch_3634_swap` (`$7E:45C9` and `$7E:45D1` overwrite `INX`, `NOP` with a
  `BRA` or with `NOP`, `INX`), `patch_510d_return` (`$7E:4880` writes an
  `RTS`), `patch_a074_nops` (the Beyond Time ending replaces a `BCS` with two
  `NOP`s around a call). Their images come from simulating the stores
  (`writes`, now also from code in a section, with long loads from an image as
  constants). Every address a patch or `over` section covers, and the one after
  it, is an entry in every space, so no block runs across the patch boundary.
- **Shadow mode.** A replay can't evaluate a guard (RAM has moved on by then),
  so the intercept decides, while recording, which generated entry applies at
  boundaries whose address has a guarded or emulation-mode entry
  (`Boundary.gen`); other boundaries are decided at replay as before.

`is_code` for a RAM space needs the analysis to have code at the address with
compatible widths and the section's image to decode an instruction there: the
analysis reads one alternative (the overlay, or the first section), and the
guard makes sure the decode a block was built from is the one that runs.

### The race's generated code

`$C3:09E9` writes 1024 `LDA` absolute / `STA` direct page pairs with a `TCD`
every 128 pairs and an `RTS` through the WRAM port. Its three callers in the
program (`$7E:3CFB`, `$7E:799D`, `$7E:8569`) set the port to `$7E:B574`,
X = `$0C00` and Y = `$0200` just before, all constants, and the routine reads
nothing but its table at `$C3:09D9`. So the code is always the same 5153 bytes,
and it is a section: format `run` executes the generator in a small 65816
interpreter (`tools/disasm/ram.py`, `simulate_run`: the instructions such
generators use, binary mode, no calls or interrupts, RAM reads only of what
it wrote) with those registers and collects the port writes. The result equals
the race's work RAM in a trace (frame 7500 of `events-jetbike-race`), the
matching build assembles it like any section, and it becomes one block of 2065
instructions whose operands are live and whose guard checks the opcodes. A
different caller or table would make the guard fail and the interpreter run
it, visible in the miss report. The alternative, leaving it to the interpreter,
would have kept 2.1 to 6.5 million instructions per movie there.

### Continuing at any instruction

A block's function already starts where PC points (M2). The generator now also
writes `recomp_index[]`: for every instruction of every block, one block that
contains it with those widths (the block entered there if there is one; one
per section for RAM code), sorted by address | M, X << 24 | E << 30. When no
entry applies and no stopped block is in the resume slot, `continue_generated`
binary-searches it (behind a bit per 16-bit address) and runs the first
candidate whose guard holds. That covers computed jumps into a block's middle
and resume slot collisions without new block boundaries. `ct_dev` and `ct_play`
register it with `native_set_index` (`ct_dev --exclude` filters it too); shadow
mode doesn't use it. Blocks entered this way are counted as `indexed`.

### The reset vector

The emulation-mode reset vector (`$00:FFFC`) gets a block with the emulation
context (`recomp_00FF00_emu`, up to its `XCE`), registered with
`NativeEntry.emulation` and p_mask 0; `entry_applies` now requires the entry's
emulation flag to match E, resume keys carry E, and shadow replays load E from
the boundary.

### Misses fed back

The miss report is a coverage file, so feeding it back is adding it to
`CT_RECOMP_COVERAGE`. With the M3 generator and only the 20 coverage files the
M2 build had (none for the three ending movies), the ROM part of the report is
already 0 over all 25 movies: the ROM misses of the M2 build were the reset
vector and instructions of existing blocks, which the emulation-mode block and
the index cover. The RAM part isn't: `endings-beyond-time` still interprets
24.7 million instructions of the program at 1757 addresses that only the ending
reaches (task handlers the analysis only knows from call logs), the attract
program's code for the credits. Adding that one movie's miss report as a
coverage file and rebuilding gives 0 misses, and the same blocks (12022, 144095
instructions) as a build with the ending movies' coverage files, which is how
the build below is configured. One iteration was enough; no hint was needed for
ROM code.

### Results

Built from the ROM with 23 coverage files (the main, save and ending movies)
and 22 call logs: 12022 blocks, 144095 instructions (the analysis has 123716),
of which 860 blocks and 9987 instructions are RAM code (803 blocks of the
program at `$7E:3000`, 1 of the race code, 40 of the interrupt stubs, 2 of the
block move stub, 6 of patches), and an index of 123867 instructions. The
generated C grew from 27 to 41 MB (`table.c`, with the index, images and
masks, is 12 MB) and `ct_dev` from 27 to 34 MB; generation takes about 5 s.

"Before" is the M2 build (20 coverage files, 19 call logs), "after" this one,
both `--native on --recomp only --misses`. Before, every movie also
interpreted 13.2 million RAM instructions or more in the program's sections
(the attract sequence or the load menu); RAM misses are given as in a section /
modified / in no section.

| Movie | Frames | Before: generated | ROM misses | RAM misses | After: generated | Misses |
|---|--:|--:|--:|--:|--:|--:|
| `new-game` | 7200 | 87.4% | 3 at 3 | 13178873 / 3830 / 0 | 100.0% | 0 |
| `menu` | 8900 | 89.8% | 3 at 3 | 13180977 / 6399 / 0 | 100.0% | 0 |
| `field` | 11400 | 92.3% | 3 at 3 | 13186255 / 6164 / 0 | 100.0% | 0 |
| `fair` | 12450 | 92.9% | 5 at 4 | 13187995 / 6164 / 0 | 100.0% | 0 |
| `fair-companion` | 17900 | 95.1% | 5 at 4 | 13198652 / 8496 / 0 | 100.0% | 0 |
| `gato-battle` | 21600 | 95.9% | 14 at 6 | 13188137 / 26354 / 0 | 100.0% | 0 |
| `saves/battle-golem-twins-gameover` | 12400 | 84.3% | 66 at 16 | 24997574 / 26873 / 0 | 100.0% | 0 |
| `saves/battle-magus` | 18000 | 94.5% | 66744 at 452 | 13187922 / 32561 / 0 | 100.0% | 0 |
| `saves/battle-tyrano-lair-walls` | 8200 | 87.8% | 1716 at 104 | 13178109 / 22760 / 0 | 100.0% | 0 |
| `saves/events-blackbird` | 4400 | 77.2% | 3 at 3 | 13176279 / 18298 / 0 | 100.0% | 0 |
| `saves/events-end-of-time-gate` | 5600 | 83.0% | 3 at 3 | 13178287 / 16699 / 0 | 100.0% | 0 |
| `saves/events-jetbike-race` | 9400 | 83.0% | 17 at 8 | 17011838 / 2849596 / 2147600 | 100.0% | 0 |
| `saves/events-keepers-dome` | 5000 | 80.5% | 5 at 4 | 13176843 / 18303 / 0 | 100.0% | 0 |
| `saves/events-prison-escape` | 12900 | 93.1% | 3 at 3 | 13193068 / 17658 / 0 | 100.0% | 0 |
| `saves/late-lavos` | 51000 | 98.0% | 77278 at 460 | 13220915 / 65298 / 0 | 100.0% | 0 |
| `saves/late-omen-battles` | 17100 | 94.1% | 7061 at 138 | 13186982 / 31635 / 0 | 100.0% | 0 |
| `saves/late-omen-terramutant` | 15500 | 93.3% | 49400 at 303 | 13198415 / 17162 / 0 | 100.0% | 0 |
| `saves/late-omen-zeal` | 35300 | 97.1% | 166021 at 460 | 13205184 / 49848 / 0 | 100.0% | 0 |
| `saves/menus-end-of-time` | 8800 | 89.2% | 3 at 3 | 13185051 / 66593 / 0 | 100.0% | 0 |
| `saves/menus-epoch` | 8800 | 89.5% | 8 at 4 | 13179037 / 23123 / 0 | 100.0% | 0 |
| `saves/menus-field-menu` | 10600 | 90.9% | 3 at 3 | 13182685 / 67293 / 0 | 100.0% | 0 |
| `saves/menus-shop-inn` | 9700 | 90.4% | 7 at 4 | 13185560 / 73569 / 0 | 100.0% | 0 |
| `saves/endings-beyond-time` | 100000 | 91.2% | 96512 at 516 | 92485911 / 19771791 / 6504750 | 100.0% | 0 |
| `saves/endings-telepod-lavos` | 31000 | 96.8% | 32610 at 256 | 13198753 / 45282 / 0 | 100.0% | 0 |
| `saves/endings-bucket-lavos` | 118400 | 99.1% | 149351 at 512 | 13285673 / 132173 / 0 | 100.0% | 0 |

(The ending movies are `oracle/movies/saves/endings-*.ctm` from technoplaza
saves; all 25 movies were run in full, 561550 frames.)

- **Misses:** 0 ROM and 0 RAM instructions interpreted in every movie, the
  reset vector's included. Nothing is left to the interpreter in the library.
- **Traces:** `--native on --recomp only` against `--native off`, every frame's
  WRAM, VRAM, SRAM, CGRAM, OAM and framebuffer hash (`trace.py diff
  --framebuffer`): identical for `new-game`, `gato-battle`,
  `events-jetbike-race`, `battle-golem-twins-gameover`, `late-omen-zeal`
  (35300 frames) and `endings-beyond-time` (100000 frames, which runs the race
  code and the patches).
- **Shadow** (`--native shadow`, hand ports and generated blocks) over the six
  main movies and all 16 save movies: 1537003643 block checks, 0 mismatched,
  219972 interrupted, 9 unfinished; no hand port mismatched. Also
  `endings-beyond-time` (428248169 checks) and `endings-telepod-lavos`
  (145144310): 0 mismatched. `endings-bucket-lavos` wasn't run in shadow mode
  (118400 frames, about half an hour); its miss report is 0 like the others'.
- **Conformance:** `sst_recomp_runner` 5120000 tests, 0 failures;
  `recomp_bus_diff` 256000 instructions, 0 mismatches. `unittest` passes
  (`tests/test_recomp_m3.py` covers RAM blocks, live operands, the index, the
  emulation block and the new section formats).
- **Speed** (process CPU time, best of three, on a machine shared with other
  jobs): `--native on --recomp only` `new-game` 2.83 s before and after,
  `gato-battle` 9.94 s and 9.86 s, `events-jetbike-race` 5.02 s and 5.14 s;
  `--native on` `gato-battle` 7.03 s and 7.03 s, `events-jetbike-race` 3.44 s
  and 3.56 s. Rendering still dominates; the 13 million instructions the attract
  program ran on the interpreter weren't a large share of the time. The race
  is 2-3% slower in both modes, which is more than its guards cost (below);
  the live operand fetches, which peek through the bus for every operand byte
  of RAM code, are the likely rest.
- **Guards** (measured with a temporary `rdtsc` probe around every guard call,
  less the probe's own cost): 8.7 million calls in `new-game` and `gato-battle`
  costing about 2-4 ms, 9.7 million in `events-jetbike-race` about 33 ms (the
  5153-byte race block's guard is the long one, about 16 ns), 63 million in
  `endings-beyond-time` about 120 ms of a 62 s run. Well under 1% everywhere,
  so no cheaper scheme (a write counter per section, say) is needed.

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
  In M1 the renderer dominates run time anyway (see Results).
- **Shadow coverage.** Every block run the movies reach is checked, but only
  on the paths the movies take; code they don't reach is checked only by the
  instruction-level tests. A flag a block gets wrong and a later instruction in
  the same block overwrites is invisible, as it is to the rest of the machine.
- **Timing.** In M1 interrupts and events landed late by up to a block in `on`
  mode, and I/O inside a block saw the time the block started. M2 made blocks
  exact; what still drifts in `on` mode is the hand ports' estimated time (see
  "Hand ports in `on` mode").
- **Horizon completeness.** Exactness rests on the horizon listing everything
  the system does between steps that the CPU can see without I/O, and on I/O
  syncing. A new time-driven event in `system.c` (or a register whose value
  moves with time outside the synced I/O area) must be added to
  `snes_horizon`; the `--recomp only` trace comparison is the check.
- **Left for later.** `ct_fuzz` doesn't fuzz generated blocks.
- **RAM code nobody enumerated.** Blocks exist only for the RAM sections the
  hints describe and are guarded by their images, so other RAM code, a patch
  the movies don't reach or a caller of `$C3:09E9` with other registers runs on
  the interpreter, correctly and visibly in `--misses`, until a section
  describes it.
- **Live operands.** RAM code fetches most operands at run time, which costs
  constant folding there; a patched branch target outside the block, or a
  store the analysis doesn't see into a branch operand, falls back to the
  runtime or the interpreter.
- **Precedence surprises.** A hand port whose precondition rejects a call now
  falls back to a generated block rather than the interpreter; both are exact,
  so this only changes speed.
- **Stale analysis.** Blocks come from the analysis at build time; code the
  movies reach later falls back to the interpreter until coverage (or the miss
  report, which is a coverage file) is fed back. This is a speed issue, never a
  correctness one.
- **ROM-derived output.** Everything the generator writes is ROM-derived: it
  stays in the build tree, and nothing that prints generated code writes into
  the repository.

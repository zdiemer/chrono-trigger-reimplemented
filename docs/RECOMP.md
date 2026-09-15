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
| M2 | Exact timing: interrupts on the interpreter's instruction boundaries, loop re-entry | designed |
| M3 | Coverage feedback: interpreted ROM code seeds the analysis | |
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

| Path | What |
|---|---|
| `tools/recomp/__main__.py` | `python3 -m tools.recomp`: runs the analysis, writes the shards and the table |
| `tools/recomp/blocks.py` | Entries, block formation, C emission |
| `cmake/recomp.cmake` | The build step and the `ct_recomp` library (generated, or the stub) |
| `runtime/port/recomp_block.h` | Included by generated shards: `RC_INSN`, `RC_YIELD` |
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
`RC_INSN(c)`, which counts executed instructions for `--insn-stats`.

A loop that reads a register that changes with time on its own (`$2137-$213F`,
the APU ports `$2140-$217F`, `$4210-$4212`, through absolute or long addressing)
returns to the runtime at every back edge instead: time only passes between
blocks in M1, so a polling loop must let it pass on every pass.

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
`dont_care`, `idle` and `guard` are unused.

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
  interpreter. Interrupts land at block boundaries.
- **Loops.** `RC_YIELD` is true once the block has spent
  `recomp_yield_clocks` (one scanline, 1364 clocks; `ct_dev --recomp-yield`).
  64 and 256 clocks gave the same frame agreement with interpreter-only runs as
  1364 over the 21 movies (1404 against 1405 of 2447 sampled frames).
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
- **Every back edge yields** (`recomp_yield_clocks` is 0), so a check ends at
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

### Results

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
- Loop heads use the same check, which replaces M1's fixed budget and its
  return on every pass of loops polling timed registers.

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
  In M1 the renderer dominates run time anyway (see Results).
- **Shadow coverage.** Every block run the movies reach is checked, but only
  on the paths the movies take; code they don't reach is checked only by the
  instruction-level tests. A flag a block gets wrong and a later instruction in
  the same block overwrites is invisible, as it is to the rest of the machine.
- **Approximate timing in M1.** Interrupts and events land late by up to a
  block in `on` mode, and I/O inside a block sees the time the block started,
  so frames can drift from `off`; loops waiting on the APU are the most
  sensitive (see "Syncing with the SPC700" in [PORTING.md](PORTING.md)). Over
  the movies the drift is about what hand ports alone cause (see Results). M2
  removes it.
- **Left for M2 and later.** Exact timing (above); RAM code, the largest part
  of what still runs on the interpreter; coverage feedback for the little ROM
  code that does (M3); `ct_fuzz` doesn't fuzz generated blocks.
- **Precedence surprises.** A hand port whose precondition rejects a call now
  falls back to a generated block rather than the interpreter; both are exact,
  so this only changes speed.
- **Stale analysis.** Blocks come from the analysis at build time; code the
  movies reach later falls back to the interpreter until coverage is fed back
  (M3). This is a speed issue, never a correctness one.
- **ROM-derived output.** Everything the generator writes is ROM-derived: it
  stays in the build tree, and nothing that prints generated code writes into
  the repository.

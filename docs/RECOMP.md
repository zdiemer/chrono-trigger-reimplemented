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
| M5 | Cleanup workflow: drafts for hand ports, checked in shadow mode and against the ports that exist | done |

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
  intercept records a boundary: registers, PB:PC, E, the CPU's own time
  (master clocks minus the pauses for work RAM refresh and HDMA), frame, the
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
  `recorded clocks + 6 * io_cycles` equal to the CPU time that passed.
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
- the work RAM refresh point 538 if it hasn't happened yet: the CPU pauses
  there for 40 clocks ([RUNTIME.md](RUNTIME.md), "Exact CPU timing"), and a
  block's clocks count CPU time, so it stops before the first instruction
  starting past a pause (HDMA's pauses are at the HDMA point and, for the
  frame's HDMA setup, at the start of line 0, both already on the list);
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
nearly all the rest stopped at their loop heads, which are entries. (Since the
refresh point joined the horizon, one more stop per line: 13.9 million, 643 per
frame, 10.3 million resumed.)

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

Hand ports still charge estimated time (`cost_milli` times their bus clocks;
since the timing gate, DMA's time as it is plus `cost_milli` times the rest)
after they stop, a scanline at a time (interrupts were held until it was paid;
since the timing gate they are taken during it), and sync explicitly where they
talk to the APU ([PORTING.md](PORTING.md)). The measurements below predate
both. Their time can't be made
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
interpreter; hand ports are checked by shadow mode as before. Whether a hand
port could carry exact time instead is the spike below; it could, only for a
class of calls, and the decision it led to is to ship the readable code with
estimated time and measure the estimates instead.

### Can a hand port be exact? (the timing spike)

Phase 2 of [PLAN.md](PLAN.md) opened with a question: can a hand port carry
its code's exact time, so that `--native on --recomp on` traces identically to
`--native off` and the readable code, rather than the generated blocks, is what
the SNES build runs? The answer was **partly**: the mechanism exists and was
demonstrated, but only for a class of calls, and the class excludes what the
porting rules themselves ask ports to be. The decision it led to is at the end
of this section: timing *differences* from the original are accepted, the
readable code ships, and timing *errors* become a measured, gated quantity.
What was kept is the measurement, off by default:

| Path | What |
|---|---|
| `runtime/port/native.c` | Shadow mode's timing counters for every hand-port entry; `PORT_TIMING_ORACLE` in `on` mode |
| `runtime/port/native.h` | The counters (`NativeStats.t_*`), `native_set_port_timing`, `native_set_port_horizon`, `native_set_timing_report` |
| `runtime/snes/system.c` | `snes_interrupt_horizon`, `snes_span_bound` (the oracle's interrupt horizon) |
| `dev/ct_dev.c` | `--include PREFIX`, `--timing-report`, `--port-timing estimate\|oracle`, `--port-horizon strict\|interrupts` |

`--timing-report` adds per hand-port entry, in shadow mode, over the stretches
(calls, or parts of calls between call-outs) no interrupt fell in: how many,
how many the `cost_milli` estimate got exactly right, its mean and largest
absolute error in master clocks, the mean original time, the `cost_milli` the
measured totals imply ("calibrated"), how many fit the two horizons below, and
the distinct values of the time a port can't see. Shadow mode's original runs
on the interpreter, so these are the same in every run whatever the estimates
are: they are what the timing gate (PLAN.md) is built on. The self-timing
prototype described below was removed from the tree when the decision was
made; it is in the history of the `timing-spike` branch.

**What exactness needs.** Three things, each checked:

1. *The time itself.* A port's bus clocks are the original's data accesses
   only. What it can't see is the time of the instruction fetches and internal
   cycles along the path the original took: branches, loop counts, an indexed
   read whose index carries into the next page, a direct page off a page
   boundary. `--timing-report` in shadow mode records, per port, the distinct
   values of *original time minus the port's bus clocks* (the time the port
   can't see). Over the 26 movies (113.5 million stretches of hand-port code in
   415 reached entries): 167 entries always had one value (16.4% of hand-port
   time), 108 had 2-4 (9.4%), 39 had 5-8 (8.5%) and 100 more than 8 (65.7%).
   The `cost_milli` estimate is exactly right on 7.4% of stretches and off by
   125 master clocks on average.
2. *Where things land.* Even an exact total moves anything the system does
   between two of the original's instructions to the end of the call. What
   that covers decides how many calls can run at all. Against M2's horizon
   (the next refresh, HDMA point or line end) only 51.5% of stretches, and 6.5%
   of hand-port time, fit: the longest gap is 825 master clocks, a few
   dozen instructions. But the refresh, HDMA points and ordinary line ends
   don't observe a routine that does no time-dependent I/O: advancing across
   them in one step does what advancing across them instruction by instruction
   does, as long as the call's span in master clocks, pauses included, is
   bounded (`snes_span_bound`: the refresh and each enabled HDMA channel's
   worst case per line). What can't be absorbed is an interrupt the CPU would
   recognize inside the call and a frame boundary (the trace and the renderer
   see the state there). Against that horizon (`snes_interrupt_horizon`)
   99.4% of stretches and 88.2% of the time fit.
3. *I/O.* A call whose I/O depends on or changes the time (anything in the
   synced area of "I/O" above: the PPU, the APU ports, the counters, `$4200`
   writes) must happen at the original's instruction time, which a port that
   runs as a unit can't give it. The WRAM port and the multiplier and divider
   are exempt, as for blocks.

The stack bytes and registers an interrupt right after the call pushes were a
concern (ports may leave don't-care registers different): over every call the
oracle below ran, no port left any register different from the original's.

**Measured drift** (`--native on --recomp on` against `--native off`, first
divergent frame, `new-game` unless noted). All hand ports: frame 1. By
subsystem (`--include PREFIX`): `field_` 1 (`field_fill_memory` alone), `sound_`
27, `decompress` 27, `display_` 28, `event_` 29, `play_` 34, `palette_` and
`input_` 35, `tile_` 40, `scene_` 82, `attract_` 142, `menu_` 1711, `memory_`
1753, `battle_` not reached. Single ports: `input_map_buttons` 36,
`scene_multiply` 140, `field_update_screen_position` 5839,
`field_link_object_row` 5840. `scene_multiply`'s estimate equals the original's
time on every one of its 3870 calls; it still drifts, because 8 of those calls
contain an interrupt, which the estimate path takes after the whole debt is
paid. The 14 ports of the self-timed set below, with their estimates: frame 35
or 36 in all 23 movies tried.

**Designs.** (a) *Take the time from the generated block*, running it on a copy
of the machine; (c) *run a block only for its timing and interrupts*: both
need the block to run the whole call, reads and writes, to follow its path, so
the port recomputes what the block already computed; the port would execute,
but it would not be what decides anything. (a) is kept as an oracle, to test
the rest. (b) *The port counts its own time*: the port adds the time it can't
see with `m_spend` along its path, and an entry's `time_extra` holds a
constant part (both removed with the prototype). That is the only design in
which the port is what runs, and the one prototyped.

**Mechanism** (`on` mode, per hand-port call). The machine is copied
(incrementally, as shadow mode does); the port runs on the machine and gives
its time; if it did no time-dependent I/O (an I/O observer counted it), didn't
call out or sync, and its span fits the horizon, the step returns that time
with the return's last cycle (an internal cycle for `RTS`, the last pull for
`RTL`). Otherwise the machine is put back from the copy and the generated
block at the same address runs, exact by M2. In the self-timing prototype
(`--port-timing self`, removed) the port gave its own time, and shadow mode
checked it against the original's on every stretch. The oracle
(`--port-timing oracle`, kept) takes the time from the generated code instead,
run on a second copy to where the port stopped, and compares the port's end
state with it.

**Results.**

- *Oracle, all hand ports.* Strict horizon, `new-game`: identical, 17.6% of
  calls run as the port. Interrupt horizon: identical in all 23 movies tried
  (every movie but the three endings: 328808 frames, 23.6 million calls):
  67.5% of calls and about 59% of hand-port time run as the port, with no register or state
  difference from the timed original. The rest fell back: calls that call out
  (24.1% of calls, 20.5% of time; the prototype times whole calls only,
  though a stretch between call-outs is as atomic as a whole call),
  time-dependent I/O (7.1%, 11.1%), no room before an interrupt or frame
  boundary (1.1%, 9.1%: long routines such as the decompressor), and code with
  no generated block (0.3%). A first version compared CPU time with a
  master-clock horizon and diverged in `new-game` at frame 2412, in menu text:
  the refresh and HDMA pauses inside a 25000-clock glyph call carried it past
  a frame boundary; `snes_span_bound` is that fix.
- *Self-timed, 14 ports* (interrupt horizon, `--port-timing self`; tried,
  worked, not adopted): 11 whose
  hidden time was one constant in every movie (`time_extra`:
  `input_map_buttons`, `scene_multiply`, `field_copy_sprite_tile_flipped`,
  `scene_clear_sprite_table`, `menu_reset_sprite_nodes`,
  `menu_hide_all_sprites`, `battle_clear_oam_buffer`,
  `battle_reset_0b40_table`, `battle_multiply_8x8`, `battle_multiply_8x16`,
  `battle_update_scroll_tables`) and three with per-path costs written from
  the listing (`field_link_object_row` 3 paths, `field_unlink_object_row` 4,
  `field_update_screen_position` 1 branch per axis, and the page-crossing
  cycles of their indexed reads). All 26 movies (762693 frames) trace
  identically to `--native off`, and 96.7% of the 5.4 million calls ran as the
  port. The first set had a fifteenth, `menu_reset_sprites`, which looked
  constant over six movies and isn't: shadow's check flagged it in two menu
  movies (`time original 15860 port 16202`), and self mode diverged in exactly
  those two (frames 3623 and 3654); without it they are identical too (the
  other 24 movies were run with it, where its time did match).
  `scene_clear_sprite_table` always falls back: its time is exact, but it
  programs a DMA.
- *Cost.* `gato-battle` without a trace, CPU seconds: `--recomp only` 7.2, the
  14 self-timed ports 8.0, the oracle 31.0, estimated hand ports 5.3,
  interpreter 9.5. The prototype's copy per call is what costs; a write
  journal would be cheaper.

**The answer, and where the line is.** A hand port can be exact if its call
(1) carries the original's hidden time for the path it took, (2) does no
time-dependent I/O, and (3) contains no interrupt recognition or frame
boundary; the generated block has to run every call that fails a test, so the
blocks ship regardless. That line excludes, by construction:

- *Ports behind the platform services.* The rules ([CLAUDE.md](../CLAUDE.md))
  tell a port to keep SNES semantics and drop SNES mechanics, "write order,
  the timing of a write". An exact SNES time for I/O is exactly those
  mechanics, so a port written to the rules is exact only by falling back.
  That is 11% of hand-port time now, and the display and sound code Phase 3
  converts is mostly this.
- *Routines longer than the gap between interrupts* (9%), and calls an
  interrupt happens to fall in, whatever their length.
- *The time itself, for anything with more than a few paths.* The hidden time
  depends on the original's control flow instruction by instruction: which
  branches were taken, how often each loop ran, which indexed reads crossed a
  page, whether D was page aligned. A port that shares one helper between two
  copies of the original's code (`field_move_object`'s two axes: the first
  ends in `BRA`, the second in `RTS`), folds a loop, or uses a service in
  place of a register sequence has to pay each of those back by hand, which
  is carrying the draft's shape in comments that happen to be numbers. Two
  thirds of hand-port time is in entries with more than eight hidden-time
  values. Shadow mode checks the annotation on every call, but only on the
  paths the movies reach, and a "constant" is a hypothesis (as
  `menu_reset_sprites` showed).

So readable code can run exactly where it is small, computational and short:
counted by call, most of today's port calls are that; counted by where the
game's time goes, and by the code Phase 3 will write, most are not.

**The decision** (the user's). Exactness was the
wrong requirement. A timing *difference* from the original -- a battle that
plays out differently -- is acceptable; a timing *error* that shows, such as a
video write landing outside vblank where the original's was inside it, is not.
So:

- The readable code ships: `--recomp on`, hand ports with generated code
  wherever no port exists, becomes the player's default, but only once the
  timing gate exists (PLAN.md).
- `--recomp only` stays as the exact mode and the gate's reference; the movie
  library stays the regression suite on it, since movies desync under
  `--recomp on` and can't test it end to end.
- Timing errors are measured and gated: each port's estimate error per call
  from shadow mode's counters above, which don't drift; a threshold; the worst
  offenders fixed case by case; `cost_milli` recalibrated from the measured
  totals; and a check for video writes outside vblank.
- Ports don't carry cycle costs. The self-timing prototype is not adopted,
  for the reasons above: it exacts per-path numbers the rules tell a port to
  leave behind, it excludes ports behind the platform services by design,
  and every call outside its line still needs the generated block. Its oracle
  and counters are kept as measurement.

### Where video writes land

Timing that differs from the original is acceptable in the shipping mode
(`--native on --recomp on`); timing that damages the picture isn't. One class
of damage is a display update that must land in vblank or forced blank landing
on the picture instead, because a routine ran late or long: the battle's black
bands and missing window before the DMA timing fix (RUNTIME.md) were this.
`tools/vblank_check.py` looks for it. It is one piece of the timing gate, not
yet wired into `tools/suite.py`.

**When a write lands.** `ct_dev --io-log` (PLATFORM.md, "I/O log") now also
counts each access by when it landed, as the PPU sees it rather than by a rule
of thumb: the time is the machine's line when the access happens, which is the
time `ppu.c` itself acts on (`sync_ppu_view`).

- *vblank*: lines 225-261 (`SNES_VBLANK_LINE` on). The PPU accepts VRAM and OAM
  data, RDNMI is set, and nothing is drawn.
- *forced blank*: lines 0-224 with `$2100` bit 7 set, as it was before the
  write. Data lands, and the line is drawn black.
- *visible*: lines 0-224 with the display on: exactly `Ppu.active_display`.
  `ppu.c` drops VRAM (`$2118-$2119`) and OAM (`$2104`) data written then, and
  any other register changes the picture from the next line drawn.

Line 0 is a picture line to the PPU (it locks VRAM, and the frame's HDMA setup
happens on it) though it isn't shown. The renderer draws each line whole at
the line hook (dot 276, where HDMA runs), so for picture-line writes the log
also keeps a histogram by the *first line the write can change*: its own line,
or the next if its own has been drawn. That separates a register write every
shown line sees (line 0 or 1, harmless) from one that splits the picture, and
it finds the write that ends forced blank with a nonzero brightness: the lines
above it were black. Hand ports run to completion at the time they start and
pay their estimated time afterwards, so their writes are timed where the PPU
sees them, at the start of the port or after its last sync or call-out;
generated blocks catch up to each I/O instruction (`rc_io_sync`); a DMA
transfer's bytes all land at the time of the `$420B` write, as `ppu.c` sees
them. HDMA writes are counted apart: they are meant for the picture. They are
told from the CPU's by the master clock of the line hook they follow, which
also stops HDMA writes made during a generated block's catch-up being counted
as the block's (the log did before).

**The comparison.** Run the same movie in the reference (`--native on
--recomp only`, generated code timed exactly like the original) and in the
shipping mode. The two runs desync within a few dozen frames, so a
frame-by-frame diff means nothing; instead the check compares, per movie, what
each *routine* does with each *register group* (display, OAM, BG, VRAM, mode 7,
CGRAM, screens, HDMA enable): a finding is something the shipping mode does on
the picture that the same routine never does in the reference of the same
movie. That survives desync because it asks about a routine's behaviour, not a
frame's contents, and a routine the shipping run reaches that the reference
never ran is reported as `unmatched` rather than judged.

To be the same routine in both runs, a write is keyed by ROM address. A hand
port is its entry; its region of the ROM runs from the entry to its last
service call site (the log records both), and the reference's generated code
in that region is the port's. Other code is keyed by the generated block
containing the address a block keeps in PC (where it started, resumed or last
branched), mapped to its block through the build's generated C. The categories:

| Category | Fails | Meaning |
|---|---|---|
| `visible` | yes | writes with the display on, changing a shown line (or VRAM/OAM data, lost on any picture line), where the reference routine makes none |
| `unblank` | yes | the write ending forced blank on a line after the first shown one, on a line the reference routine never ends it on: black lines above it |
| `lines` | yes | a raster write (display on) landing on lines the reference routine never writes that group on, where it writes it on some |
| `late` | no | other writes in forced blank on a picture line where the reference makes none: the routine ran late, but the screen was blank anyway (the cost appears as `unblank` in whichever routine ends the blank) |
| `unmatched` | no | picture-line writes by a routine the reference never ran |

**Pass/fail.** The check exits 0 only when every failing finding is listed in
`tools/vblank_check_exceptions.txt` (`routine group category reason`), checked
in with reasons. It lists four ("The timing gate", below).

    python3 tools/vblank_check.py --ct-dev build/main/ct_dev -j 2
    python3 tools/vblank_check.py --no-run --failing-only     # re-read the logs in build/vblank

Both runs of all 26 movies take about 730 CPU seconds (6 minutes at `-j 2`;
`on` mode is fast). Logs go to `build/vblank`: they name ROM addresses.
`tests/test_vblank_check.py` covers the comparison on made-up logs.

**What it found** (the 26 movies, before the timing gate's changes). The
shipping mode damaged the picture on this axis in four ways, all of them the
hand ports' estimated time:

- **Late NMIs in battle.** `battle_nmi` (`$D1:E77C`) forces blank and turns
  HDMA off with the display on, from line 2 down: 255 times in the 11 movies with
  battles (the reference: never). The rest of that frame is black. The NMI is
  late because interrupts wait while a port's time is paid off (PORTING.md), the
  cause RUNTIME.md's timing history already named for `gato-battle`'s 11
  one-frame flickers. `field_nmi` (`$C0:EA63`) does the same on 32 frames of 8
  movies, with windows, color math, BG mode and HDMA changing mid-picture.
- **Black bands at the top.** The write that ends forced blank lands on lines
  2-150 instead of in vblank: `field_nmi` 1348 times (mostly line 6, in every
  field movie), `battle_frame_work` (`$CD:088D`, the brightness restore at
  `$CD:0925`) 1023 times, `scene_frame_work` (`$C3:08D0`) 367 times in the
  jetbike race, `battle_setup_display` (`$CD:09F1`) 53 times. The ports that ran
  late with the screen blanked (`late`, 36 findings) are mostly the uploads
  behind them: `battle_upload_vram` (to line 97), `battle_upload_alternating_vram`,
  `battle_load_graphics`, `field_nmi`'s windows and color math, and the jetbike
  race's map uploads.
- **The raster split one line early, every frame.** The line IRQs' forced blank
  under the playfield (`field_irq_after_hblank` `$C0:ECF2`,
  `battle_irq_after_hblank` `$D1:FEBF`) changes line 211 in the shipping mode
  and line 212 in the reference: 144857 and 305627 writes, one a frame, so the last line of
  the field and battle picture is black in nearly every frame (seen in
  screenshots of `gato-battle`: row 210 black in the shipping mode, drawn in the
  reference). The ports run when the idle entry's hblank wait ends (hclock
  1096), 8 clocks before the line is drawn; the original's instructions after
  its `$4212` poll take it past that point.
- **Late line IRQs.** `battle_irq` (`$D1:FE97`) and `battle_irq_after_hblank`
  land on lines 212-224 and above (8292 and about 7800 times): the blank under
  the battle window starts late, or a frame late.
- Smaller: the ending's scene program (`scene_program_restart` `$7E:302A`,
  `scene_show_layers` `$7E:5C98`) moves 4 raster writes each by 1-27 lines.

No port writes VRAM or OAM data with the display on: every data write the
shipping mode makes on a picture line is in forced blank. The 22 `unmatched`
findings are all forced-blank writes too, mostly menu loads (`menu_clear_vram`
and others) whose original does the work in code past the port's last call
site, where the region can't follow it.

The timing gate's changes (next) cleared all of these; four findings of the
same kind as the reference's own are accepted. The $C7 pilot's ports moved the
shipping mode's timing again and three more were accepted, each checked in
screenshots: two field-menu writes that land on frames the field's fade has
already made black (or all but), and the battle's forced blank under the window
on the lines the reference itself uses in the same fights.

### The timing gate

The timing decision (above) accepts timing *differences* in the
shipping mode and bounds timing *errors*. This is the bound: every hand port's
estimated time is measured against the original's over the movie library, on
every suite run, and each entry is held to limits tied to the budgets an error
could break. It is one check of `tools/suite.py`; the vblank check above and a
route runner join it before `ct_play`'s default flips (PLAN.md).

| Path | What |
|---|---|
| `tools/timing_gate.py` | Parses `--timing-report` output, sums it per entry over the movies, checks the limits, `--table`, `--calibrate` |
| `tools/timing_exceptions.txt` | Entries allowed over a limit, each with its reason |
| `tools/suite.py` | Runs the shadow movies with `--timing-report`; its `CHECKS` table runs the trace comparison and then the timing check |
| `runtime/port/native.c` | The report's exact totals, DMA share and error histograms; the estimate (DMA as is); paying debt with interrupts taken; the idle exit |
| `runtime/snes/bus.c` | `SnesBus.dma_clocks`: DMA's time, which the estimate takes as it is |
| `game/registry.c` | Every measured entry's `cost_milli`, calibrated |
| `tests/test_timing_gate.py` | Aggregation, limits, exceptions and calibration on made-up reports |

**What is measured.** Shadow mode's timing counters (the spike above) run on
every shadow run whether or not `--timing-report` is given; the switch only
prints them, a few lines per entry at exit, so the suite's runs carry it at no
cost. The report now also gives the exact totals (the original's time, the
port's bus clocks and their DMA part, the summed error), the error histogram by
bit length, and the same for the stretches that began on a vblank line, with
the largest error there and on picture lines. The original runs on the
interpreter in shadow mode, so none of it moves with the estimates; only the
error does. `tools/timing_gate.py` sums the reports over the 26 movies, weighted
by stretch count: 414 entries, 113.4 million stretches.

**The estimate.** A port's time was `cost_milli`/1000 times all of its bus
clocks. A DMA transfer's time counts among them (the `$420B` write takes the
whole transfer), and DMA takes the same time in the port as in the original,
while the rest of the bus clocks stand for a few times their length in
instructions. One ratio can't fit both in a port that does some of each, so a
port's estimate is now its DMA time as it is plus `cost_milli`/1000 times the
rest (`estimate_clocks`), and the calibrated `cost_milli` leaves DMA out of both
sides. This is a better estimate, not a per-path cost: ports carry nothing new.

**Calibration.** `python3 tools/timing_gate.py build/suite/shadow-*.log
--calibrate game/registry.c` sets every measured entry to the value its totals
imply. The event command and field upload tables' macros take the value as an
argument, so each entry has its own. Over the library:

| | exact | mean \|error\| | share of port time | net drift | off by >= 1024 clocks | entries over drift |
|---|---|---|---|---|---|---|
| Before (hand-set `cost_milli`) | 7.4% | 78.4 clocks | 7.79% | +2.085% | 0.774% | 208 |
| Recalibrated | 8.7% | 41.2 clocks | 4.09% | +0.003% | 0.172% | 0 |
| DMA as is, recalibrated | 9.0% | 38.1 clocks | 3.79% | +0.001% | 0.104% | 0 |

(The spike's "125 clocks on average" was over a different run's counters; these
are from the totals.) What one scalar fixed is the systematic part: 208 entries
had drifted more than the limit below, some by far (`event_cmd_follow_party_member`
+135%, `battle_divide` +69%, `battle_nmi` +33%, `tile_animation_upload` -27%,
`field_vram_transfer` +288%), and all are now within 0.03%. Taking DMA as it is
fixed the ports that mix a transfer with code: `tile_animation_upload` from 31%
mean error to 3.1%, `field_upload_vram_queue` 33% to 8.9%, `battle_upload_vram`
4.1% to 0.7%, `menu_object_nmi` 4.4% to 0.7%, `field_fill_memory` 1.3% to 0.

What a scalar can't fix is the spread: entries whose original time follows
their data or their path (the spike's "more than eight hidden-time values")
keep a large error per call around a mean that is now right.
`field_update_objects` (28.7% of a 115-clock call), `field_nmi` (29%),
`menu_run_objects` (41%), `battle_draw_window` (33%), `battle_draw_menu_window`
(28%) and `field_build_sprite_table` (19%) are the largest; all but the six
exceptions below are within the limits anyway, because their errors are small
in clocks or fall outside vblank. None was split: no split of these reads
naturally as source (their variation is inside one loop or one helper, not
between two entry paths the game names), and none needed one to meet a budget.

**The limits** (`tools/timing_gate.py`), each from what an error of that kind
can break. An error moves the work after the port's call; with interrupts now
taken on time during a port's time (below), what it can move past is the end
of vblank, for work done in vblank, and the end of the frame's work, for the
rest.

| Limit | Bound | Why |
|---|---|---|
| `drift` | \|mean signed error\| <= 1364/357368 = 0.38% of the entry's time | A systematic error adds up over every call in a frame. At this bound, a frame spent entirely in the entry would end its work one scanline off. |
| `vblank-tail` | at most 1% of the stretches that begin in vblank off by >= 1024 clocks | In vblank the NMI handler does the frame's video work, and an error of about a line (the histogram's bound nearest one, 3/4 of a line) moves the handler's later writes a line nearer the picture. |
| `vblank-worst` | no stretch that begins in vblank off by more than 12617 clocks | A quarter of vblank (37 lines, 50468 clocks): a spike that takes a quarter of the handler's time. |
| `worst` | no stretch on a picture line off by more than 50468 clocks | On picture lines an error moves the main loop's work within its frame; interrupts come on time and raster writes are the vblank check's. What it can break is the frame: an error longer than vblank can push work that ended before the NMI past it, and the frame's update slips. |

Mean and worst are kept apart because they break different things: the mean
error (drift) accumulates and is what a scalar fixes, so its bound is tight and
relative; single spikes don't accumulate, so their bounds are absolute and sized
by the budget they could hit. Drift and `vblank-tail` need 100 stretches to
judge.

The drift bound is tight enough that it holds only for a calibrated value: an
entry's drift over one part of the library differs from its drift over all of it
by far more than 0.38% (over the two movies `gato-battle` and `battle-magus` alone, more than 80 entries are over
it, `battle_cycle_party_palettes` by +29%). So adding a movie to the library, or
a port, means running `--calibrate` again, which is the point: `cost_milli` is
the measured value, not a guess. That variation between scenes is real
systematic error in the frames it happens in, and the per-stretch limits are
what bound it where it could matter.

An entry over a limit fails unless `tools/timing_exceptions.txt` lists it
for that limit with a reason; an exception an entry no longer needs is reported,
so the list doesn't go stale.

**Exceptions** (6 entries, 11 limits). Each is an entry whose time follows its
data in a way one scalar can't, and whose error was shown not to reach the
budget: the vblank check finds none of their writes moved.

| Entry | Over | Why it doesn't matter |
|---|---|---|
| `decompress`, `decompress_direct` | `vblank-tail`, `vblank-worst`, `worst` | 350k-395k clocks a call on average (260-290 lines, seven vblanks), mean error 6%, the largest 296k and 333k. Interrupts are taken during its time, so the error moves the end of a load, not a video write. Its time follows the compressed data (literals against copies of every length). |
| `field_rasterize_quad` | `vblank-tail`, `vblank-worst` | Rasterizes a window shape into a RAM table, 35 lines a call (as long as vblank), 7.5% error, at most 14885 clocks. Main-loop work running on into vblank after the NMI, writing RAM, and the original's own time already runs it into the picture. |
| `menu_object_wave_hdma` | `vblank-tail` | Fills a RAM scroll-wave table in 53-line calls, longer than vblank; its error never exceeds 1396 clocks (one line), just over the tail's 1024. |
| `menu_draw_glyph` | `vblank-tail` | Draws a glyph into the menu's tile buffer in RAM, 18-line calls; at most 2658 clocks (two lines). |
| `menu_draw_text` | `vblank-tail` | Draws text into the menu's tilemap buffer in RAM, 19-line calls; at most 4509 clocks in vblank. |

**Interrupts during a port's time.** The vblank check (above) found the
shipping mode's worst damage, and recalibration alone didn't clear it: with the
calibrated values the 16 failing findings were still all there, most of them
smaller (`field_nmi`'s black bands 1348 to 6, `battle_nmi`'s mid-picture blanks
255 to 64). The cause was how a port's time is paid. A port runs to completion
at once and its time is charged afterwards, a scanline at a time, and
interrupts waited until the whole debt was paid: an NMI arriving during a long
port was taken up to that port's length late, and the handler's video writes
went with it. The spike saw the same with `scene_multiply`, whose totals were
exact and still drifted.

Now a debt is owed at a place: the instruction boundary where the port stopped
(its return address, or the first instruction of a callee it called out to),
with S as it was there (`owe`, `pay_debt`). While the machine is at that
boundary the debt is paid, and an interrupt the CPU would recognize is taken
there instead, as the original would take it somewhere inside the routine: the
handler runs on time (seeing the port's work already done, a timing difference
the decision accepts), returns to the boundary, and the rest is paid then. A
payment step also ends just after the next point an interrupt can be raised
(`until_interrupt`, from the spike's `snes_interrupt_horizon`), so the poll sees
it that step; an interrupt already raised and not yet seen (a V-timer IRQ at the
start of the line the step starts on) ends the step after one cycle. Debts nest
as handlers run ports of their own. A port suspended in `native_sync` (between
two APU accesses) owes its time the same way: its PC and S stay put while it
waits, an interrupt taken then returns to them, and the port goes on once the
rest is paid. (At first those debts were paid whole with interrupts held; a
sound driver upload then delayed `field_irq` past line 211 in `late-lavos`,
and its blank landed on line 214.) If the machine leaves the boundary some
other way than by an interrupt, the debt is paid where it is. Idle waits end their steps at the same points, so a line IRQ
arriving during one is no longer taken at the next hblank change.

**The idle exit.** The raster split one line early was a separate cause: an
idle entry's waiting pass skips to the point the polled bits change, so the
exit pass saw the change at the change itself, and the line IRQs' forced blank
after their hblank wait landed at hclock 1102, before the line is drawn (1104).
The original sees a change on the first read of its next pass and then falls
through its branch. An idle entry that waits for a change (no `idle_clocks`)
now charges its exit pass `IDLE_EXIT_CLOCKS`, the shortest such loop's pass
(`LDA abs`/`AND #`/`BEQ`, 54 clocks), so the code after the loop lands no
earlier than the original's could.

**A race it exposed.** With the idle exit, the `beyond_time` route
(`tools/movies/route_check`, shipping mode) stuck in map `$DA01`: the party
reached the passage to the second form's chamber and nothing happened. Only
`field_irq_wait_hblank_loop`'s exit made the difference (bisected by entry
prefix: with the exit on every other idle entry the route passed). What it
changed is the phase of a busy loop. The event command that changes location
now (`$E1`, whose port stops at `$C0:3B71`) waits for vblank before blanking and
loading: `LDA $4210 : BPL`, with the vblank interrupt enabled, and the field NMI
handler reads `$4210` too (`$C0:EA70`), which clears the flag. The loop sees the
flag only if its read lands between the flag going up at line 225 and the NMI
being taken; otherwise the handler clears it first, and the line interrupt at
line 211 returns to the loop at the same point every frame, so it misses it in
every frame after, for good. The original's exact timing wins that race (the
reference changes map 83 frames after the push, as the interpreter does); the
line interrupt ending 48 clocks later lost it, and before the idle exit the
route passed on the same luck. Machine states at frame boundaries were identical
apart from the latched H counter, and so was all of work RAM; the CPU sat at
`$C0:3B71` from then on. Two loops in the ROM race like this (`$C0:3B71` and
`$C0:4181`, both followed by `$C0:0B4E`, which blanks the screen and stops
interrupts); the other two `$4210` loops (`$C0:0B38`, `$FD:EADA`) run with the
NMI disabled. Both are idle entries now (`field_change_location_wait_for_vblank_loop`,
`field_restart_wait_for_vblank_loop`): a pass reads the flag as the original
does and leaves when it is set or, if the handler got there first, when the
blanking status says vblank. That is what the loop waits for, and it can't lose
the race; the reference (`--recomp only`) and shadow mode don't run idle entries,
so nothing they check moves.

**The vblank check, before and after** (failing findings over the 26 movies):

| | Failing findings |
|---|---|
| Before (the hand-set `cost_milli`) | 16 |
| Recalibrated only | 16, most of them smaller |
| + interrupts taken during a port's time | 6: both line IRQs' split at line 211, `battle_irq` a line late 20 times, the boot program's three |
| + the idle exit | 5: `battle_irq` and its blank a line late about 15 times in 5 movies, the boot program's three |
| + the step that sees an IRQ already raised | 3, the boot program's, accepted |

(These were measured with the scalar calibration; the final build, with DMA
taken as it is and recalibrated, has the same three and nothing else, and 4
`late` findings where the first run had 36. After the race fix below changed
the movies' paths, `battle_irq` landed once on line 224 in
`endings-bucket-lavos`, behind the battle's frame work running with interrupts
masked; the reference does the same in that fight on lines 212-223 and in
vblank, so it is accepted too.)

The three left are writes the original also makes on the picture: the boot and
title program at `$7E:3000` shows the title's layers from line 32 instead of 31
(`scene_show_layers`), and blanks from line 40 instead of 38 on the frame before
it restarts (`scene_program_restart`, display and HDMA enable), once each per
movie. They land wherever the frame's main-loop work has got to, which in the
shipping mode is estimated; they are in `tools/vblank_check_exceptions.txt`.

None of this touches `--recomp only` (no hand ports, so no debt and no idle
entries) or shadow mode (no debt), and the trace identity check is unchanged:
`new-game` shows no divergence in 7200 frames.

**Failing.** With `battle_draw_sprite_parts`'s `cost_milli` set from 3011 to
3500 (the entry with the most hand-port time), the whole suite fails and names
it, with its numbers:

    26 of 26 movies clean in 529s
    timing: 1 of 414 entries over the limits  <-- FAILED
      battle_draw_sprite_parts over the drift limit: +16.26% (limit +/-0.38%)
          1855861 stretches in 11 movie(s), mean time 5542, mean |error| 901 (16.3%), drift +16.26%,
          max 2189; in vblank 46330, off by >= 1024 clocks 0.17%, max 2177; cost_milli 3500, calibrated 3011
    FAILED: timing

**Cost.** Nothing measurable: the counters were already there, the report adds a
few lines per entry to each shadow log, and parsing 26 logs takes under a
second. The suite at `-j 2` runs its 26 shadow movies in about 520-550 seconds
(716 in the baseline run, on a busier machine).

    python3 tools/suite.py --ct-dev build/main/ct_dev -j 2        # the gate, timing check included
    python3 tools/timing_gate.py build/suite/shadow-*.log --table # every entry's numbers
    python3 tools/timing_gate.py build/suite/shadow-*.log --calibrate game/registry.c

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
  `NOP`s around a call), `patch_582e_bra` (`$7E:5640` turns the `BMI` choosing
  a star's sprite attribute in the star field task into a `BRA` to the next
  instruction, from the Dream Project ending's staff credits and from mode 4's
  sequence script; found later by the player's misses file, below). Their
  images come from simulating the stores
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

Code that no movie reaches can be covered from ZSNES save states
([RUNTIME.md](RUNTIME.md), "ZSNES save states"): `ct_dev --load-zst <state>`
with `--coverage` and `--calls` (in `--native off`, where every instruction goes
through the instruction hook) writes files that go into `CT_RECOMP_COVERAGE`
and `CT_RECOMP_CALLS` like a movie's, and `--native on --recomp only --misses`
gives the miss report from the same point. The runs aren't replayable, so they
only feed the analysis; shadow validation of what they add still needs a movie
from power-on. The twelve ending states' miss reports were already empty with
the build below; their coverage adds 455 ROM instructions and 28 call edges.

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

## Without the interpreter

M3 left the interpreter nothing to run in the library, but every program still
linked it and the system loop still called it:

| What | Before | Now |
|---|---|---|
| Interrupt entry | the intercept returns -1 when an NMI or unmasked IRQ is deliverable, and `cpu_step` runs the entry sequence | `cpu_service` (`runtime/cpu/cpu_entry.c`) |
| `WAI` and `STP` | no entry applies while the CPU is halted, so `cpu_step` ran the idle steps (and the wake-up by an IRQ that I holds off, which then runs the next instruction in the same step) | `cpu_service`; after that wake-up the system asks the intercept again |
| Power-on | `cpu_init` and `cpu_reset` (which reads the reset vector) were in `cpu65816.c` | moved to `cpu_entry.c` unchanged |
| The system loop | `snes_run_frame` called `cpu_step` | calls `Snes.interpreter`, which hosts set to `cpu_step` (`snes_init` leaves it NULL) |
| `native_call`'s synchronous fallback (tools, `ct_fuzz`) | `cpu_step` | `Snes.interpreter`; without one it gives up with a message. In `on` mode hand ports call out through their fiber and never reach it |
| An instruction no entry, stop or index covers (and a block that returns without running) | `cpu_step` | a miss (below) |

With these, nothing in the runtime, game code or generated code refers to the
interpreter: a program that doesn't set `Snes.interpreter` doesn't link
`cpu65816.c` (static libraries only pull objects something refers to).

### The system loop without the interpreter

`Snes.native_only` (or no interpreter at all) switches `snes_run_frame` to the
path a player without the interpreter runs. The intercept runs first, as
always (hand ports, their debt and call-outs, generated blocks, stops, the
index). When it returns -1, `cpu_service` does what `cpu_step` does before an
instruction: STP idles; a pending NMI, or an IRQ with I clear, runs the entry
sequence (a step that doesn't poll); an IRQ with I set ends WAI; WAI idles.
Otherwise an instruction is due and nothing ran it: `Snes.on_miss` is called
with PC there. If it returns true and an interpreter is set, the interpreter
runs that one instruction as an ordinary step; otherwise `Snes.halted` is set
and the frame ends at once, before the step (a state load clears it). Time,
polling and the last cycle are computed exactly as for the interpreter's steps.

`cpu_service` is a separate implementation of the interpreter's entry
sequence, so `recomp_bus_diff` now also runs it against `cpu_step` from random
states (every context, random NMI and IRQ inputs, I, WAI and STP), comparing
the same things as for instructions (registers, memory, open bus, bus clocks,
internal cycles, the last cycle, halt state) and the result kind: 50180 entries
and idle steps, 0 mismatches (13820 states where an instruction is due are
skipped). With the B flag left set in the emulation-mode push, 282 of them
mismatch.

### Checking it: `ct_dev --no-interpreter`

`ct_dev --native on --no-interpreter strict` runs the strict player's path (a
miss stops the run with exit status 1 and names the address and its section),
`--no-interpreter fallback` the fallback player's (misses run on the
interpreter, the first ten are printed, `--misses` counts them). Built as in M3
(12022 blocks), `--native on --recomp only --no-interpreter strict` against
`--native off` over every frame (`trace.py diff --framebuffer`):

| Movie | Frames | Traces | Exit |
|---|--:|---|---|
| `new-game` | 7200 | identical | 0 |
| `gato-battle` | 21600 | identical | 0 |
| `saves/events-jetbike-race` | 9400 | identical | 0 |
| `saves/battle-golem-twins-gameover` (game over, SPC700 boot ROM restart) | 12400 | identical | 0 |
| `saves/endings-beyond-time` | 100000 | identical | 0 |

Every run ran to its end in strict mode, so no instruction reached an
interpreter, and every NMI, IRQ and WAI went through `cpu_service`. The fallback
path was checked by taking blocks away: `new-game` with `--exclude recomp_C0
--exclude recomp_FD --no-interpreter fallback` interprets 38.3 million
instructions at 10978 addresses through the miss path and its trace is still
identical to `--native off`; `--no-interpreter strict --exclude recomp_FD` stops
in frame 0 at `FD:C000` (the reset code after the emulation-mode block).

### The player builds

`CT_PLAYER_INTERPRETER` ([PLAYING.md](PLAYING.md), "Without the interpreter")
applies to `ct_play` only: `ON` (the default for CMake builds) as before,
`FALLBACK` (`CT_PLAYER_NO_INTERPRETER=1`, what `tools/dist.py` builds) sets
`native_only` and an `on_miss` that logs the miss to
`<rom name>.misses.txt` with `dev/misses.c` and lets the interpreter run it,
and `OFF` (`CT_PLAYER_NO_INTERPRETER=2`, `tools/dist.py --strict`) doesn't set
the interpreter, so `cpu65816.c` isn't linked (CI's `linux-clang` job builds it
and checks with `nm` that `cpu_step` isn't in `ct_play`), and its `on_miss` logs
the miss, stops the game and says where.

- **Size** (MinGW gcc, Release, stripped, with the 12022 blocks): `ON`
  32813568 bytes, `FALLBACK` 32820224 (+6.7 KB: the miss log), `OFF` 32792064
  (-21.5 KB). The interpreter is 28.7 KB of code; the generated blocks are
  nearly all of the 32 MB.
- **Speed** (`ct_play --movie oracle/movies/gato-battle.ctm --frames 21600`
  headless, process CPU time, mean and best of six alternating runs on a
  machine shared with other jobs): the player before these changes 8.73 s and
  8.42 s, `ON` 8.85 s and 8.67 s, `FALLBACK` 8.89 s and 8.44 s, `OFF` 8.78 s
  and 8.53 s. The differences are within the noise: the steps that change (an
  interrupt entry or halt, through a function call either way) are a handful per
  frame. All four end on the same final frame.
- **Misses** on a state patched to loop in RAM no section holds (`NOP`, `INX`,
  `BRA` at `$7E:1F00`): the fallback player ran the loop on the interpreter
  (1.27 million instructions in 60 frames, the NMI handler still generated),
  logged its three addresses and added to their counts on a second run; the
  strict player stopped before the first instruction with exit status 1 and
  wrote `7E:1F00 8 8 # 1 RAM unknown`.

Feeding the misses file back is the same as for `ct_dev --misses` reports
("Misses fed back"): add it to `CT_RECOMP_COVERAGE` and rebuild.

### A patch the library didn't reach

A fallback player replaying an ending logged 26 misses at `$7E:5805-583D`, the
star field task's sprite loop in the program at `$7E:3000`, one of them
`RAM modified (opcode)` at `$7E:582E`: the whole run between the `PLP`s at
`$7E:5804` and `$7E:583D` fell back to the interpreter, since its block's guard
covers the patched `BMI`. Coverage can't fix that, as the code is already
analysed; a patch the hints don't describe needs a section. The run was
`endings-telepod-lavos` re-recorded for the DMA, HDMA and refresh timing change
(the version before it, and `endings-beyond-time`, which now loses its final
battle, don't get that far): `--native on --recomp only --misses` over
140000 frames gave the same 26 addresses, 1298176 runs each, all after the
program dispatches mode `$0D` for the staff credits (frame 129854). A bus write
watch in a scratch build of `ct_dev` (`--native off`) showed `$7E:5643` storing
the word at `$582E` three frames later, from `JSR $5640` at `$7E:8F8B`, and no
other write there while the program ran. The analysis has that `STA` as the only
store addressing `$582E-582F` in RAM code, with a constant value, so
`patch_582e_bra` (a `writes` section of `$7E:5640-5643`, 16-bit) is its only
variant; sequence command `$A0` at `$7E:55EA` (mode 4's script) calls the same
routine.

With the section (12418 blocks, 6 more): the telepod run has 0 misses and its
`--native on --recomp only` trace is identical to `--native off` over all 140000
frames (`trace.py diff --framebuffer`), and in `--native shadow` its 485100616
block checks have 0 mismatched; `new-game` and `gato-battle` have 0 misses,
`new-game`'s trace is identical too, and the six main movies in shadow mode
have 0 mismatched blocks and hand ports.

## Cleanup into hand ports (M5)

A port starts from a draft, not from the listing:

```sh
python3 -m tools.recomp.show C1:423A              # widths from the analysis, or --m 8 --x 16
python3 -m tools.recomp.show C0:62DE C0:5FB6 > <scratch file outside the repo>
```

`tools/recomp/show.py` prints the routine shaped the way the porting rules
(CLAUDE.md, "Writing a port from generated code") ask a port to be, derived
mechanically from the ROM. It prints to stdout like `tools.recomp.linear`: a
draft is a mechanical image of the ROM, so it is never committed, and nothing in
the repository contains one. What is committed is the tool, its tests on
synthetic code, and two checks: `tools.recomp.draftcheck`, which registers
drafts in a scratch tree to shadow them, and `tools.recomp.groundtruth`, which
compares a draft with the hand port of the same routine. The workflow a porter
follows is PORTING.md's "From a draft to a port"; this section is how the tool
is built and how good its drafts are.

### What a draft is

Three parts, for a routine at `$BB:AAAA`:

- A **logic function** that takes the registers the routine reads as C
  parameters and returns the ones its callers read (a struct when there are
  several), computing with C locals: `a`, `x`, `y` and the flags `negative`,
  `overflow`, `zero`, `carry`, the last only where something reads them. Memory
  goes through `m_read8`/`m_write16` and friends with work RAM spelled by its
  `game/ram.h` name. It has no emulated CPU in it: no `c->x`, no `m_lda8`, no
  `m_rep`.
- A **wrapper**, `void name(Snes *s)` under the `// $BB:AAAA` comment, that is
  what registers: it moves the machine's registers into the logic's parameters
  and its results back, sets P's widths and returns as the original does
  (`m_rts`, `m_rtl`, `m_rti`, or setting PC where it hands back). It is the thin
  translation a 65816 build would replace with glue to the original calling
  convention.
- **Call-out stubs**, one per callee and register signature, that do the same
  around each `m_jsr`/`m_jsl`: the logic says `call_battle_multiply_8x16(s,
  0xC1429F, a, y, overflow, carry)` and never touches an emulated register. A
  stub sets only what the callee reads (and what the draft reads back, since a
  callee can leave a register alone), and passes the rest to shadow mode as
  don't-care.

A comment ahead of it says what the tool knows: the command it handles if it is
a script command's handler, the entry widths, D and DB and where they came from,
what callers read after it returns, where and why it hands back to the
interpreter, the I/O registers it touches (which a port reaches through
`runtime/platform/`), the machine state it sets directly (D, DB, S, P's
interrupt and decimal bits), and the registry line it registers with. For a
made-up routine at `$C0:8010` -- `LDA $10 : STA $20 : RTS`, called with D =
`$0100`, whose callers reload A's low byte and the flags afterwards -- the
logic is:

```c
static uint8_t sub_C08010_logic(Snes *s)
{
    uint8_t a;
    a = m_read8(s, RAM_TEST_SOURCE);
    m_write8(s, 0x7E0120, a);
    return a;
}
```

### How it is made

The draft is not made from the generator's C. `tools/recomp/translate.py`
writes what the emulated CPU does -- every flag, every bus access in order, the
fetches and cycles -- which is what makes generated blocks exact, and none of
which a port wants; rewriting that text into values would be parsing C back
into meaning. The draft takes the same inputs the generator does (the
analysis's code and widths, `tools/disasm/liveness.py`, the hints) and lifts
the instructions again, into values.

1. **The routine** is the analysis's context at the address and widths:
   everything reachable from the entry without a call, with the widths each
   instruction runs with, including jump-table targets. An address the analysis
   never entered (a loop head, a mid-routine entry a port registers) gets a
   context of its own. The draft stops -- hands back to the interpreter -- at
   another registered entry, as generated blocks do, since that code is its own
   port.
2. **D and DB** (`tools/recomp/frame.py`). The analysis tracks widths, not D,
   and a direct-page offset has a name only relative to its page. A forward
   pass over each routine follows D, DB, the accumulator's bytes and what the
   stack holds, so `LDA #$1D00 : TCD`, `PEA #$0300 : PLD`, `PHK : PLB` and a
   `PHD ... PLD` pair give values; a call keeps them unless the callee's
   returns show it sets them (a summary per callee, which can be "set on one
   path, kept on another"). Entry values are solved for the whole program at
   once: an entry's D is the set of values its call sites have (the static
   call sites and the ones the call logs saw), each resolved in its caller,
   iterated from nothing to the least fixpoint; D and DB are sets of up to four
   values, so a callee that resets D on one path is still known in a caller
   that had that D. Interrupt vectors, coverage seeds and hints are entered from
   places nobody can see, so they, and whatever only they reach, stay unknown.
   **Which page applies** follows from that: when D is one value, a
   direct-page access is the work RAM address D + offset, and it is named like
   any other address, with the variables of a page of `re/ram.toml` that has
   that D and the routine's subsystem ranked first; when D isn't known the
   access stays `m_dp(s, offset)` with a note in the header, and `--page NAME`
   names the offsets of a page with no fixed D (`caller`, `menu_record`)
   through their `_DP` macros. `--d`/`--db` give values by hand. A draft that
   assumed its callers' D or DB registers with a guard on them, so a caller
   the analysis missed falls back to the original instead of reading the
   wrong page.
3. **Lifting** (`tools/recomp/lift.py`): each instruction becomes statements on
   the locals -- every flag it sets its own assignment -- with addresses as
   constants wherever D and DB are known. The stack, D, DB, S and P's other
   bits stay the machine's (`m_push16`, `s->cpu.d = ...`), so a call-out or the
   interpreter continuing sees what the original would have left. ADC and SBC
   are binary (a draft that has them registers with the `_BINARY`
   precondition). XCE, WAI, STP, BRK, COP, instructions whose widths the
   analysis doesn't know, and jump tables it didn't size hand back at that
   instruction.
4. **Cleanup** (`tools/recomp/draft.py`): basic blocks in address order;
   liveness per flag and per byte of A, from what the routine's callers read
   after it (`ret_live`, over every routine the address is part of), what is
   read where it hands back (`live_at`), and what each callee reads before it
   returns (`callee_reads`; see "Before the bulk" below); dead
   assignments dropped (a read of an I/O register is kept for its side
   effect); constants propagated within a block; and a value computed once and
   read once in the same block written where it is read, when nothing between
   changes what it reads -- which is what turns `LDA $10 : STA $20` into one
   statement and `CMP #$05 : BCC` into `if (a < 5)`. Call-outs read back only
   what is read after them and the callee may change (a may-def summary over
   the callee and everything it calls, SEP #$10's truncation of X and Y
   included). A routine that never reads or writes A's high byte itself works
   on an 8-bit `a`: the machine's high byte then stays exactly the original's
   (only callees change it), and the wrapper and stubs move the low byte alone.
5. **Wait loops.** Native code runs in no time, so a loop that polls an I/O
   register without calling out, or reads only fixed addresses and stores
   nothing (a flag an interrupt sets), would never end in a draft; the draft
   hands back where the loop is entered and says what it waits on, and a
   routine that starts with one says to register it as an idle entry instead.
6. **Structure** (`tools/recomp/structure.py`): the statements come in address
   order with labels and gotos; loops (`do`/`while`, `for (;;)` with `break` and
   `continue`), then `if`, then `else` are formed, smallest first, only where
   nothing outside a region jumps into it. What matches no pattern keeps its
   goto.
7. **Names** (`tools/recomp/names.py`): work RAM from the RAM map, by the rules
   its coverage count uses (a name from the routine's own subsystem, `core`, or
   the subsystem sharing its page; arrays and records as `RAM_X + k *
   RAM_X_STRIDE`); I/O registers by their conventional names in a comment
   (`0x002100 /* INIDISP */`); callees by their port or hint name. A routine
   that is the handler of an event command (`tools/disasm/eventscript.py`) or
   of a battle animation command, enemy condition or enemy action
   (`tools/disasm/battlescript.py`) is named after it (`event_cmd_if_var_const`)
   with the operand layout in the header, and an event handler's reads of its
   own operands, which go through X and Y as the command's offset plus a
   constant, are written `RAM_EVENT_SCRIPT + cmd + 1 /* var */`, `cmd` being
   the offset it was entered with.

### Decisions

**Drafts compile, and register as ports.** A draft that compiles can be
checked the way ports are, by shadow mode, which measures the tool's
correctness mechanically instead of by reading drafts. That shaped the output
less than expected: the logic reads as C either way, and what compiling costs
is at the edges -- the wrapper, the stubs, a `(a & 0xFF00) | ...` where a
routine keeps A's high byte that its callers might read, a cast where a value
wraps. Where exactness and readability pull apart, the draft stays exact and
says so, since a porter can drop what a caller doesn't need once they know the
callers, and a draft that looked cleaner but did something else would cost
them a debugging session. Two departures are deliberate: a direct-page access
indexed past `$1FFF` is drafted in bank `$7E` (the original would reach bank
0's I/O area), and an event handler's operand read at an offset past `$FFFF`
doesn't wrap. No game code does either.

**The workflow gets its own gate.** `python3 -m tools.recomp.draftcheck --out
<scratch>` exports the committed tree to a scratch directory outside the
repository, drafts every routine `game/registry.c` registers, writes the drafts
there and points each registry entry at its draft, with the draft's own
precondition, don't-care set and D/DB guard. Built there and run in
`--native shadow` over the movie library, every call to a drafted routine is
compared with the original, at every call-out and at the return. Idle entries,
entries with a guard of their own, routines the analysis enters with more than
one set of widths and routines that start with a wait loop are left as they
are. Given `BB:AAAA` addresses nothing registers yet, it adds entries for their
drafts, which is how a porter checks a draft before rewriting it.

### Results

Over the 466 distinct routines the registry names that the analysis enters with
one set of widths (25 more need `--m`/`--x`), drafted with the committed hints,
the RAM map and the 75 coverage and call files of the movie library:

| | |
|---|---|
| D known where the routine uses the direct page | 349 of 396 (88%) |
| DB known where it uses absolute addressing | 301 of 357 (84%) |
| Work RAM in the logic by name / as a number | 2017 / 1187 (distinct in each draft, summed) |
| Drafts with no goto | 336 (72%); 577 gotos in the other 130 |
| Loops / ifs formed | 566 / 1531 |
| Emulated-register operations in the logic | none; stack pushes and pulls and 318 machine-state writes (D, DB, S, P) remain |
| Script command handlers named | 180 (179 event commands) |

**Shadow validation.** `draftcheck` registered 347 drafts in place of the hand
ports (144 entries were left: 121 idle or with a guard of their own, 23 entered
with several widths). They compiled with `-Wall -Wextra` without a
warning, and over the whole movie library (26 movies, 762693 frames) in
`--native shadow`,
289 of the 347 drafts were called, 52.0 million times. 51.5 million calls
matched the original at every call-out and at the return, 84203 were
interrupted (not compared, as for ports), and 14 mismatched, all of them in
`menu_nmi_work` at its call to `$C2:8473`: the carry of a CMP against `$4210`,
whose open-bus bits read differently natively (see below). Guards and
preconditions turned away 438811 calls to 11 drafts, 432877 of them to
`field_scroll_layers`: the analysis has the field's main loop calling it with D
= `$0100` and DB = `$00`, but after a battle the loop runs with D = `$0000` and
DB = `$7E`, having been returned to along a path the analysis doesn't see. The
guard on the D and DB the draft assumed is what kept those calls on the
original; the rest are interrupt handlers and scene routines entered with other
widths than the draft's. 58 drafts are never called by the movies.

**Ground truth.** The ports were validated call by call against the original,
so each is a known-correct target for the draft of its address.
`python3 -m tools.recomp.groundtruth BB:AAAA ...` drafts routines that are
already ported and compares, in terms that mean something to a porter: the work
RAM addresses the port's text reads and writes (literals, `RAM_` names and
direct-page offsets resolved with the draft's D) that the draft also accesses,
and how many each spells by name; the port's call-outs the draft also makes;
if/else/loops/switch/goto in each; port lines with a close line in the draft
and a token similarity; and the emulated-register operations each has (`m_lda8`,
`c->x`, `m_push16`, ...). A varied set:

| Port | Accesses in both / port (draft) | Named: port vs draft | Call-outs in both / port (draft) | Structure: port / draft | Port lines close / similarity | Register ops: port / draft | Left in the draft |
|---|--:|--:|--:|---|--:|--:|---|
| `battle_multiply_8x8` `$C1:0089` | 4/4 (5) | 0/4 vs 2/5 | 0/0 (0) | - / - | 0/14, 0.42 | 6 / 0 | - | 3 unnamed |
| `battle_clear_ad9c_table` `$FD:ACEE` | 1/1 (2) | 0/1 vs 1/2 | 0/0 (0) | 2 loop / 2 loop | 0/10, 0.44 | 9 / 0 | - | 1 unnamed |
| `battle_find_due_animation` `$C1:423A` | 2/2 (21) | 0/4 vs 9/21 | 0/0 (2) | 3 if, 4 loop / 11 if, 1 else, 4 loop, 2 goto | 3/29, 0.16 | 28 / 0 | 4269 | 12 unnamed, 2 goto, 5 temps, 2 callouts |
| `menu_draw_glyph` `$C2:5E36` | 2/6 (4) | 0/7 vs 3/4 | 0/0 (1) | 5 loop / 4 loop | 6/45, 0.24 | 28 / 7 | - | 1 unnamed, 3 temps, 3 machine, 4 callouts |
| `field_compute_layer_edges` `$C0:7F9A` | 4/6 (58) | 0/12 vs 54/58 | 0/0 (4) | 2 if, 1 else, 1 loop / - | 3/67, 0.17 | 35 / 12 | - | 4 unnamed, 2 temps, 12 callouts |
| `battle_draw_shadow` `$CF:EA8A` | 11/11 (11) | 0/12 vs 10/11 | 0/0 (0) | 1 if / 2 if | 0/30, 0.14 | 30 / 0 | - | 1 unnamed, 1 temps |
| `battle_update_scroll_dispatch` `$CD:06B4` | 1/1 (36) | 0/1 vs 23/36 | 0/0 (1) | - / 4 if, 4 else, 4 loop, 1 switch, 5 goto | 0/5, 0.04 | 6 / 0 | - | 13 unnamed, 5 goto, 10 temps, 1 callouts, 3 handbacks |
| `event_cmd_if_var_const` `$C0:62DE` | 1/1 (5) | 0/1 vs 3/5 | 0/0 (1) | 4 if, 2 else, 1 switch / 1 if | 2/72, 0.22 | 52 / 0 | - | 2 unnamed, 2 temps, 1 callouts |
| `event_cmd_call` `$C0:5FB6` | 9/9 (16) | 0/12 vs 13/16 | 0/0 (0) | 7 if, 1 else, 2 loop / 6 if, 3 goto | 1/84, 0.20 | 59 / 0 | - | 3 unnamed, 3 goto, 7 temps |
| `menu_object_nmi` `$C2:015E` | 1/2 (75) | 0/4 vs 62/75 | 3/4 (6) | 6 if, 9 loop / 2 if, 1 else | 26/161, 0.18 | 85 / 18 | - | 13 unnamed, 63 io, 6 machine, 6 callouts |

What it says:

- **The memory and the calls are the port's.** 36 of the 43 addresses the
  ports' text shows are in the drafts, and where the two stop at the same
  place, all of them are (`battle_draw_shadow` 11 of 11, `event_cmd_call` 9 of
  9, `battle_multiply_8x8` 4 of 4); shadow mode already says the effects are
  the same. The differences are where the port split or joined code: it inlines a
  callee the draft calls out (`menu_draw_glyph`'s pixel routine,
  `field_compute_layer_edges`'s four edge routines), stops early and hands back
  (`battle_find_due_animation` at `$4269`), or is a dispatcher whose targets
  are ports of their own (`battle_update_scroll_dispatch`, 7 lines against the
  draft's 102 because the two targets that aren't registered are drafted
  inline).
- **The drafts are more named than the ports.** The ports these routines have
  spell work RAM by number (none of the 58 port addresses above uses a `RAM_`
  name; `game/battle/setup.c` is the one file converted so far); the drafts name
  180 of their 233 (40-93% each), and the unnamed rest is what the map doesn't
  have yet (scratch cells like the event commands' `$01D9`).
- **The text isn't the port's, and that is the point.** 0-16% of a port's
  lines have a close line in the draft and token similarity is 0.04-0.44,
  because the ports are transcriptions: 6 to 85 emulated-register operations
  each, against none in the drafts' logic. The draft is closer to the bar
  CLAUDE.md sets than most existing ports are, which is also what Phase 3 of
  PLAN.md says about bringing them up to it.
- **Control structure** matches where the port kept the original's shape
  (`battle_clear_ad9c_table`, two loops each); elsewhere the draft has more
  ifs, since it keeps the early returns and branches the port folded into one
  condition, and some gotos where loops and ifs interleave.

### Found by shadow mode, while building it

Each was a wrong draft that compiled and looked right, found in the first
shadow runs, fixed, and covered by a test on synthetic code: a value
substituted into a branch whose target still read the variable (liveness after
a branch has to include the target's); a callee's `SEP #$10` truncating Y
without being a "definition" of Y; the machine's A written whole at a call-out
while the draft had tracked only the byte it needed; a truncation lost when an
8-bit temporary was replaced by its (wider) expression; a cast rendered around
the wrong part of an expression; and busy-wait loops, which never end in native
code.

### The $C7 pilot

The last step before PLAN.md's bulk conversion: one subsystem converted through the
workflow to the whole bar, to learn what Phase 3 costs. The subsystem was the
sound driver's CPU half, bank `$C7` (`game/core/sound_driver.c`): 2.6 KiB of
code, half of it ported before in the early style (every instruction's effect
on A, X, Y, P and the stack reproduced: 1066 emulated-register operations by
the count below), the rest inlined into those ports but credited to nothing.

**Where it ended.** `python3 tools/burndown.py` for `$C7`: 1.4 KiB ported and
46% left before, 2.6 KiB and 0% after; the one byte left, `$C7:0003`, is a
`NOP` between two jumps that nothing reaches. 43 registry entries (34 before):
the routines the old ports inlined but nothing credited (`play_song` at
`$C7:01A1` and its stages, `$C7:0734`, `$C7:09FD`, `$C7:0A12`), and the code
after wait loops that ran interpreted (`$C7:011E` after the boot's countdown,
which is an idle entry now, `$C7:08DA`, `$C7:09E5`, `$C7:09F7`, the common
exit `$C7:0192`). The file went from 1788 lines to 1416, plus 134 in
`game/core/sound_data.h`. Every routine is a C function of what it does in
values; the emulated-register operations left, 86, are all in the part of the
file that moves the original's registers in and out (the entries, the idle
passes and the call-out stubs) and none in the logic. Over the 26 movies in
shadow mode nothing mismatches and no call is refused; all 43 entries pass
20000 fuzzed states (PORTING.md's row has the numbers).

**What the bar needed that the gate didn't allow.** A port written as logic
apart from the registers doesn't know the flags, A's high byte after 16-bit
index arithmetic, or the stack bytes of the subroutines it inlines, and shadow
mode compared all three, so the first attempt had to choose between
reproducing them (the draft tool does, with a local per flag) and a narrower
comparison. The comparison was narrowed, to what the original's own execution
shows is dead, and nothing else:

- stack bytes the original pushed and pulled again since the last comparison
  (between the lowest S it reached and its S now) aren't compared, in shadow
  mode and in `ct_fuzz` (`native_copy_popped_stack`). Nothing reads below S.
  Every subroutine in `$C7` saves P with `PHP` and restores it, and every
  call-out's callee does, so without this each inlined subroutine and each
  call-out needed its P byte exact;
- a native routine can declare what is dead where it stops
  (`m_stop_dont_care`), on top of its entry's set: an entry that hands back at
  several wait loops leaves different registers live at each;
- `DC_AH`, A's high byte alone;
- `au_driver_call` takes the call-out's don't-care set, like `m_jsr_dc`.

The first two only ever leave out bytes and registers the original itself
overwrites before reading; the suite's other checks (trace identity with
`--recomp only`, timing, vblank, routes) are untouched by them.

**Per routine**, what survived of the draft (`python3 -m tools.recomp.show`,
with the pilot's hints and the library's coverage) and what changed:

| Routine | Draft / port lines | Close lines, similarity | What changed from the draft, and why |
|---|--:|--:|---|
| `$C7:0004` command, `play_song`, commands `$70`, `$71` | 895 / 546 | 31, 0.02 | The draft is one function with 49 gotos over the dispatch, `play_song` and its eight stages; the port is about fifteen functions, one per stage, with the slot tables as named word arrays. Its jump table became `command & 2`. The draft's register stubs (ten variants of the stub for `$C7:09DA` alone, one per register signature) became one stub that sets A and names the rest untaken. D is unknown to the draft after the first call-out, so its accesses fall back to `m_dp`; the port names them all |
| `$C7:0655` upload a block | 128 / 121 | 1, 0.08 | The header retry, the two data loops (the one carrying into the next bank chosen once) and the length prefix became shared protocol helpers used by six routines; the pointer set-up that the retry repeats is done once, since it is idempotent |
| `$C7:0A39` send a song | 28 / 50 | 0, 0.19 | Close to the draft; `$C7:09FD`, a call-out in the draft, is a C call |
| `$C7:0922` upload a `$2F00` set | 91 / 101 | 2, 0.13 | The two sets' duplicated loops became one, the set chosen by an accessor |
| `$C7:00B0` boot, next byte | 50 / 34 | 0, 0.25 | The index the boot ROM counts, which the original keeps in A's high byte across the wait (`XBA`), is a parameter and a result |
| `$C7:0643`, `$C7:0614`, `$C7:08FF`, `$C7:0A7B`, `$C7:011E` | 15-28 / 10-56 | 0-1, 0.09-0.23 | Short; mostly the entry's register handling, now in the wrappers |
| `$C7:0734`, `$C7:09FD`, `$C7:0A12` | 17-28 / 13-26 | 0, 0.14-0.23 | Loops the draft left as gotos |

The draft was right everywhere the port could be checked against it; nothing
in it was wrong. The port differs in text almost completely: token similarity
0.02-0.25, 0-7% of the port's lines with a close draft line. (The ground-truth
tool undercounts the port's memory accesses and call-outs here, since it
doesn't resolve file-local names for `RAM_` constants or call-out stubs;
the similarity figures stand.)

**Found on the way**, each a port bug that compiled, looked right and passed
the first checks: an inlined subroutine's P is live across its call-outs
(`$C7:0655` saves P under the return address of the calls it makes, so the
port pushes it); registering `play_song` on its own broke `$C7:0004`'s shadow
comparison (PORTING.md, Call-outs); and three that only fuzzing reached, since
no movie does: an odd boot-table index (the accessor rounded it to a block),
P restored with 8-bit index registers without truncating X and Y, and
`$C7:09DA`'s `SEP #$20` written as setting both widths.

**The asset seam.** `game/core/sound_data.h` holds every read the driver makes
of the ROM: MD2's `sound_block_ptrs` and `song_ptrs` by name
(`sound_block`, `sound_song`), the hand-typed tables by what the driver does
with them, and the bytes of any resource through `sound_data_byte`. The
resource is still located by its ROM address, because the driver keeps its
upload position in work RAM as a long pointer the game can see; a build that
loads assets from files maps that address in this one header.

**Effort.** Reading the subsystem and the drafts took about as long as writing
the port, and deciding how the logic and wrapper split meets shadow mode
(above) took longer than either: it is decided once, now. The port itself,
1550 lines, was written in one pass from the existing port, the listing and
the drafts; after that, two shadow iterations over one movie, one over the
library, and three fuzz iterations. Machine time dominated the wall clock: a
library shadow run is 6 minutes at four at a time, the fuzzing of 43 entries 3
minutes, the full gate with every route most of an hour. Validation found five
bugs; review of the draft against the listing found two more before the first
run (a loop that must run at least once, an 8-bit read written 16-bit).

**Sound.** The driver broke once in exactly this kind of change (a song
upload scrambled when native time opened a gap between APU writes;
PORTING.md, `native_sync`), so the shipping mode's audio was compared with
`tools/audio_compare.py` (4 s segments) against the reference, `--recomp
only`, for the pilot's build and for the build before it:

| Movie | Audible segments | Spectrum correlation, median (before / after) | Segments below 0.9 (before / after) | Log-spectral distance, median (before / after) |
|---|--:|--:|--:|--:|
| `new-game` | 25 | 0.9990 / 0.9990 | 5 / 5 | 0.53 / 0.53 dB |
| `field` | 43 | 1.0000 / 1.0000 | 5 / 5 | 0.36 / 0.40 dB |
| `gato-battle` | 81 | 1.0000 / 1.0000 | 9 / 10 | 0.33 / 0.34 dB |
| `events-jetbike-race` | 38 | 0.9970 / 0.9985 | 2 / 2 | 0.59 / 0.46 dB |
| `battle-magus` | 69 | 0.9825 / 0.9775 | 13 / 14 | 1.73 / 1.85 dB |
| `menus-shop-inn` | 38 | 1.0000 / 1.0000 | 6 / 6 | 0.30 / 0.35 dB |

The same songs and sounds at shifted times, as before: a scrambled upload
would put a whole song's segments far below 0.9, and no movie has more than
one segment where one side is silent and the other isn't (the same one before
and after, at the edge of a silence). `battle-magus` is lowest in both builds,
where the fight plays out differently in the shipping mode. With
`--audio direct`, `field` gives 0.9997 and 0.38 dB against the reference
(0.9997 and 0.39 dB before), and the direct runs now interpret 5 instructions
where they interpreted 84856, the code after the wait loops being entries.

### Before the bulk: liveness, the don't-care check, the draft's hand-backs

The pilot left three things to do once rather than in every subsystem (PLAN.md,
Phase 3): liveness that sees through a saved P, a check that every don't-care
set a port declares is one the original agrees with, and the draft tool's
remaining gaps. They were done in that order, since each needed the one before.

**Liveness through PHP and PLP.** `tools/disasm/liveness.py` counted `PHP` as
reading every flag, and Chrono Trigger's subroutines save P at entry and
restore it before returning, so nearly every routine "read" N, V, Z and C, and
a draft carried all four through every routine that called one. Saving P
reads nothing by itself: a flag is live at a `PHP` exactly when it is live
after the `PLP` that pulls the byte. So for each `PHP` a walk forward counts
the bytes pushed and pulled above its byte; where every path reaches a `PLP`
that pulls exactly it, the `PHP` reads what is live after those `PLP`s, and
otherwise it stays a read of every flag:

- **S changes**: `TCS`, `TXS`, `XCE`, a push whose width the analysis doesn't
  know, or paths that meet with different depths. `LDX #const : TXS` (the
  mode loaders' new stack) and `CLC : XCE` are understood: a byte under a new
  stack is never read again.
- **Read otherwise**: the byte is pulled by something else or with other
  bytes, a return pulls it, a stack-relative access reaches it, or `TSC`/`TSX`
  copies S while it is on the stack.
- **A callee reaches it**: every routine has a `reach`, how far above its entry
  S it (or what it calls) pulls or reads the stack; 2609 of 3136 routines reach
  exactly a JSR's return address, 262 a JSL's, 228 can't be bounded (tasks that
  switch stacks, code in RAM). A call made while the byte is on the stack, to a
  routine that reaches that far, may read it.
- **Control leaves** with the byte on the stack: a return, a tail jump, an
  indirect jump, a flow out of the traced code. A call that doesn't return
  just ends the path.

Of the 669 `PHP`s the analysis traces (counted once per context; 399
addresses): 566 paired (84.6%, 321 addresses), none where S changes, 13 read
otherwise (5 addresses: the menus' task switch at `$C2:821E` copies S, and
`$C7:0A39`'s refused path pulls its P byte with a `PLA`), 87 where a callee
reaches the byte (71: calls into the menus' task switch, and the sound driver's
`$C7:0A39`, whose refused path drops its caller's frame and leaves through the
driver's common exit), 3 where control leaves (2). Routines whose entry reads
all four flags went from 424 of 3136 to 164; flag bits read at entries, summed,
from 4336 to 3671; flag bits live after returns from 5400 to 5141. The
generated blocks don't use liveness (M4's dead flags aren't built), so `python3
-m tools.recomp` writes byte-identical C before and after (all 65 files), and
the gate passed unchanged (`suite OK` with `--long-routes`; trace identity,
`sst_recomp_runner` and `recomp_bus_diff` 0 failures). What changes is the
drafts, checked by `draftcheck` (below). The draft of a routine that starts
with `PHP` now pushes the machine's P as it is (a `PHP` reached only along a
straight line from the entry through instructions that leave P alone, where the
machine's P is still the original's), so the flags don't enter the draft at
all.

**The don't-care check** (`tools/dontcare_check.py`). Shadow mode doesn't
compare what a port declares dead, so a wrong declaration hides a difference
and ships a wrong register. Each declaration is a claim about the original at
the point `runtime/port/native.c` applies it, and the check verifies it against
liveness there:

| Declaration | Where shadow mode leaves it out | What liveness must say |
|---|---|---|
| a registry entry's `.dont_care` | where the port finishes: at its return, and at every stop | dead after the routine returns (over every routine the entry is part of: `$C1:1C55` is also the fall-through end of `$C1:1C4A`), and dead at each stop the port declares |
| `m_stop_at(s, at, set)`, `m_stop_dont_care(s, at, set)` | at the stop, with the entry's set | `set` and the entry's dead at `at`; or, when `at` is a return instruction the port carries out last, dead after that return |
| `m_jsr_dc`, `m_jsl_dc`, `au_driver_call` | at the callee's entry | not read by the callee before it returns (`Liveness.callee_reads`) |

That needed three runtime changes, so the claims mean what the check verifies:

- **Stops say where.** `m_stop_dont_care` takes the stop's address, and
  `m_stop_at(s, at, set)` sets PC and declares in one call. Shadow mode (and
  `ct_fuzz`) report a port that stops somewhere other than the address it
  declared, and one whose entry has a don't-care set that finishes without
  either returning (`m_rts`, `m_rtl`, `m_rti` now tell the runtime) or
  declaring its stop: the entry's set applies there, so the check has to know
  the address. 24 entries stop somewhere; each stop is now an `m_stop_at`.
- **A call-out's set means what the callee reads, and no more.** When a
  callee returns, shadow mode reloaded the port's machine from the original's,
  so a register the port declared don't-care at the call and the callee
  carried through came back as the original's, while in the shipping mode it
  comes back as the port's. Now a declared register the callee left as it
  found it is the port's own again after the reload (`keep_untouched`): if the
  port relies on it later, shadow mode sees what the shipping mode would. The
  claim at the call is then only that the callee doesn't read the set, which
  is `machine.h`'s documented meaning.
- **Liveness splits A.** `DC_AH` claims A's high byte alone, so liveness has
  `A` (the low byte) and `AH` apart: an 8-bit accumulator reads and writes the
  low byte, `XBA` swaps the two bytes' liveness, `TAX` with 8-bit index
  registers reads the low byte. `DC_A` is dead only when both are.

The declarations are read from the source, not run: registry entries through
the file's macros, the rest with their arguments evaluated from constants,
the function's parameters (every call of it in the same file is followed, so
the sound driver's `stop_at(s, loop, dead)` and `call_driver(...)` helpers
resolve per call), locals, the rows of a table in the file (the wait table's
address and dead set stay paired), and both branches of a condition decided
at run time. A call-out whose target is computed (a jump table's) takes its
targets from the original's call at the site. A declaration the check can't
read is itself a finding. It is a row of `tools/suite.py`'s `CHECKS`
(`--no-dontcare`), fed the call logs the shadow runs just wrote, and takes
about 20 seconds; `--tree` checks another tree, such as a `draftcheck` one.

**What it found on the ports**, and what was done. Every finding was either
liveness being too conservative, fixed where that was sound, or a declaration
wider than the original allows, narrowed:

| Finding | Kind | Resolution |
|---|---|---|
| the sound driver's boot stops (`$C7:00AB`, `$C7:00DB`) declare `DC_AH`; liveness had A's high byte live | liveness | the value there is overwritten on every path after the `XBA`s that move it; with `XBA` swapping the bytes' liveness, it is dead |
| `$C7:0922`'s entry and the stop at `$C7:08FA` declare X and Y; liveness had them read | liveness | `$C7:09DA` carries X and Y back to callers that read them, and a call counted every caller's readers. Liveness is now solved as a function of what callers read (a summary per routine, the value at each instruction packed per register the caller may read), so a call carries only its own caller's through |
| the call-outs through `au_driver_call` declare X and Y (loop counters the port keeps in C) and `$C7:01AE`'s call to `$C7:0A39` the flags | semantics | the callee reads none of them; what the caller does after the call is the port's, now compared as the shipping mode would run it (above). `$C7:0A39`'s `PHP` byte is pulled by a `PLA` whose A is dead: pairing is now for every push, and a pull into another register reads what was pushed only where that register (or N and Z) is live |
| `menu_nmi_work`'s `DC_C` checked at its return | declaration | it never returns: it stops at `$C2:8466`, or after a call-out that changed the widths; the three stops are declared, and the carry is dead at each |
| `battle_build_object_graphics` (`$C1:1C55`) declares X and C | declaration | live after `$C1:1C4A`'s callers resume, which it returns to when `$C1:1C4A` falls into it; narrowed to N and Z |
| `input_read_joypad` declares Y and V | declaration | live at its stops (the joypad wait at `$C2:84F9`, the reset at `$C0:FF00`); narrowed. V is `$4212`'s hblank bit from a `BIT` that native time can't reproduce, and dead after the return: that is now declared at the `RTS` (`$C2:8544`) alone |
| `event_run_object_script` X and C, `field_object_in_the_way` X, `menu_run_objects` Y | declaration | live at the stops each makes after a call-out changes the widths (`$C0:5AB4`, `$C0:5BE6`, `$C2:0541`); narrowed |

Over the 26 movies in shadow mode the narrowed ports still match everywhere:
the early ports reproduce every register they declared, so none of the wrong
declarations was hiding a difference on the paths the movies reach. The one
exception proves the mechanism: with V taken out of `input_read_joypad`'s
set, 416 calls mismatched at the return on V, the hblank bit; declared at the
`RTS`, none do. Today's tree: 0 findings over 284 entries with a don't-care
set, 56 stop and 45 call-out declarations (a value per helper call and table
row).

The liveness those fixes left, from the same analysis (3136 routines):

| | Before | PHP pairing | Now |
|---|--:|--:|--:|
| routines whose entry reads all four flags | 424 | 164 | 123 |
| flag bits read at entries, summed | 4336 | 3671 | 2409 |
| flag bits live after returns, summed | 5400 | 5141 | 3968 |
| routines X is live after | 1635 | 1635 | 1032 |
| routines A's low byte is live after (A whole before) | 2553 | 2553 | 511 |
| routines with something dead after return | 2663 | 2743 | 2820 |

Pushes of every register pair the same way now (1980 in contexts): 1784
paired (1140 addresses), 3 where S changes, 60 read otherwise, 120 where a
callee reaches the byte, 13 where control leaves. The solve takes 15 s.

**The draft tool's fixes**, in the pilot's order, each on the machinery above:

- **Liveness at hand-backs.** A draft stops with `m_stop_at(s, at, set)`,
  `set` being what liveness has dead at `at`, and writes back only what is live
  there (and A's low byte wherever its high byte is, since no set leaves out
  the low byte alone). Its registry line declares what is dead everywhere it
  finishes, after the return and at every stop, and a return declares the
  rest at the return instruction, so a draft suggests declarations the check
  accepts: `tools/dontcare_check.py --tree` on the `draftcheck` tree finds
  nothing over 335 entries, 113 stops and 516 call-outs.
- **Call-out stubs set what the callee reads** (`callee_reads`) and name the
  rest don't-care, rather than everything any caller reads after the callee;
  and a `PLP` that pulls the byte the routine's first `PHP` pushed restores the
  caller's P, so it no longer makes the call "change" every register. Fewer
  inputs and read-backs mean fewer stub signatures (`$C7:0004`: 16 to 11).
- **D kept across call-outs that leave it alone.** A callee's summary of D and
  DB now takes only the returns that come back to its caller (at the depth it
  was entered with): `$C7:0A39`'s refused path, which pulls its caller's frame
  and returns further up, no longer makes D unknown after the call, and the
  rest of `$C7:0004`'s draft names its direct page (135 `m_dp` accesses to
  none).
- **The ground truth reads a port the way the check does.** Its file-local
  names (`COUNTER` for `RAM_ABS(RAM_SOUND_DRIVER_UPLOAD_COUNTER)`) resolve
  through the file's macros, a helper's parameters count as addresses only
  where the helper reads or writes through them, and call-outs are followed
  through the file's stubs to their targets.

Drafts before and after (`python3 -m tools.recomp.show`, the library's coverage
and call logs):

| Routine | Flag assignments | Flag parameters | Call-out stubs | `m_dp` accesses | Logic lines | Stops declared |
|---|--:|--:|--:|--:|--:|--:|
| `$C7:0004` (m8 x16) | 140 → 11 | 4 → 1 | 16 → 11 | 135 → 0 | 893 → 690 | 0 → 7 |
| `$C7:0655` | 15 → 2 | 4 → 0 | 3 → 3 | 43 → 0 | 128 → 90 | - |
| `$C7:0A39` | 5 → 0 | 4 → 0 | 1 → 1 | - | 26 → 19 | 0 → 1 |
| `$C2:015E` | 2 → 0 | 2 → 2 | 6 → 6 | - | 115 → 109 | - |
| `$C1:423A` | 7 → 6 | 1 → 1 | 2 → 2 | - | 112 → 109 | - |
| `$C0:62DE`, `$C0:5FB6` | unchanged | | | | 33, 79 | |

The ground truth on the `$C7` port, which it undercounted:

| Port | Accesses in both / port (before) | Named in the port (before) | Call-outs in both / port (before) |
|---|--:|--:|--:|
| `$C7:0004` | 23 / 23 (0 / 43) | 97 of 97 (0 of 62) | 5 / 5 (0 / 0) |
| `$C7:0655` | 8 / 8 (0 / 5) | 13 of 13 (0 of 5) | 2 / 2 (0 / 0) |
| `$C7:0922` | 5 / 7 (0 / 4) | 11 of 11 (0 of 4) | 2 / 2 (0 / 0) |
| `$C7:0A39` | 2 / 6 (0 / 1) | 13 of 13 (0 of 4) | 0 / 0 (0 / 0) |

The draft reaches every address the port's `$C7:0004` does and calls what it
calls; where the counts still differ, the port inlines a stage the draft calls
out (`$C7:09FD`, `$C7:0734`) or reads a table through the driver's own
accessors. Its other ports read as before (`$C1:423A`, `$C0:62DE`,
`$C2:015E`, `$C0:5FB6` unchanged).

`draftcheck` over the library, with every fix: 188 drafts registered in place
of the ports (134 left as they are), 182 called, 53.1 million calls,
none mismatching, 84824 interrupted. It exports the committed tree by default;
`--rev` takes another (`git stash create` gives one of uncommitted work).

### Phase 3, wave 1: bank `$FD`

The first bulk subsystem after the pilot: bank `$FD`'s field graphics, 15.3
KiB of code of which 1.5 KiB was ported (9 entries, most in the early style).
`python3 tools/burndown.py` for `$FD`: 90% left before, 47% after, 8.1 KiB
ported in 50 entries (9 before). Two coherent parts were converted to the bar, with the
bank's existing ports:

- **The field's per-line effects** (`game/field/line_effects.c`, 871 lines):
  the tables the eight line effects read, rebuilt every frame for where the
  dialog window is (`$FD:C2C1` and six builders, two table sets of three
  layouts, with their pieces), the values the setup fills once (`$FD:C124`,
  with the dialog window's shading through the divider, `$FD:D52D`), and the
  slot programming (`$FD:C1EE`, moved here from `field/display.c`). About
  2400 lines of drafts for 5.1 KiB of 65816, most of it straight-line stores
  twice over; the port is one function per layout, the sets and the window's
  position as parameters.
- **Palette and tile animation** (`game/core/palette.c`, `tile_animation.c`):
  the per-frame animation of every mode (`$FD:E39C`), its eight types'
  handlers and fourteen channel modes as two tables of what each changes, the
  location loader (`$FD:E292`) and the clear (`$FD:E275`), and the animated
  tile upload (`$FD:E022`).

The 48 entries added or rewritten match over the library in shadow mode and
pass fuzzing (PORTING.md's rows). What the part taught, for the next waves:

- **Callers' liveness decides a port's size.** The field's main loop reads
  nothing the analysis can rule out after `$FD:C2C1` returns, so X, A's high
  byte, the carry and the overflow flag are live: the carry the window-2
  builder's `LSR` leaves carries into the wave's sixteen `ADC`s, and the exit's
  overflow flag is one particular `SBC`'s. The port returns those four as an
  exit record, traced by hand where the draft traces them mechanically.
  Liveness through the field loop's calls would drop most of it.
- **The burndown doesn't follow call tables.** `JSR (abs,X)` targets are
  credited only when registered, so the six builders are entries of their own
  (which also fuzzes them apart from the dispatcher).
- **Stack bytes a transfer reads.** Shadow mode leaves out popped stack bytes,
  but a DMA whose source is the stack page reads them before they are popped:
  fuzzing sent an animated tile slot there, so `$FD:E022`'s wrapper pushes what
  the original pushes. Likewise the frame copy of palette type 1 keeps its
  pointer in the direct page, which a count of 0 overwrites; the port reads it
  from memory each time, and fuzzing needed `--step-limit` raised to compare
  those states at all (the default skipped them).
- **Inlining a dispatcher's handlers costs its time estimate.** Each palette
  handler fits its own `cost_milli` within 0.3-4.4%, but `palette_animate`,
  which now runs them as C calls rather than call-outs, spends its time on
  twelve delay counts one frame and a fade through the multiplier the next,
  and one scalar misses by 25.9% on average. The vblank check finds no video
  write moved (it writes RAM), so it and the two fades are timing exceptions
  (`tools/timing_exceptions.txt`), as the decompressor is.
- **Declarations the check can't read**: a registry entry whose precondition
  is a macro argument (`LINE_PIECE(..., NEEDS_M8_X16, ...)`) is a finding of
  `tools/dontcare_check.py`; entries are written out.
### Phase 3, wave 1: the battle's graphics banks ($CF, $D1)

The first subsystem converted in bulk after the pilot, chosen because its code
is video-heavy where the pilot's went through the audio service. What that
turned out to mean, and what the workflow cost:

**Where it ended.** `python3 tools/burndown.py`: `$D1` from 87% left to 17%,
`$CF` from 52% to 7% (the whole ROM from 83% to 81%, 44.6K to 51.7K ported).
New: the battle's screen script commands and the frame's palette and window
effects in `$D1` (`battle/screen_*.c`), the nineteen timed effect slots in
`$CF` (`battle/effect_paths.c`), the afterimages and effect sprites the object
drawing had called out to, and the three window-row transfer requests.
Rewritten to the bar: the NMI and IRQ handlers, the tile row upload and the
second vblank handler in `$D1`; the object drawing and motion
(`battle/draw_objects.c`, `battle/object_motion.c`, out of `battle/sprites.c`),
the vblank uploads and the frame update in `$CF`. Every call over the library
matches and every entry is fuzzed (PORTING.md's rows). What is left is in
docs/TODO.md: `$D1:F1B0` and `$D1:F9E7` (below), and two runs in `$CF`
(`$CF:FADD`, `$CF:FD9E`).

**The video service was the smaller part.** Almost none of `$D1`'s "graphics"
code touches a register: it writes tables in work RAM that the per-line
effects and the uploads read (the two window line buffers, the palette's two
copies, the fade tables, the quadrilateral's corners), so it is ordinary
logic under the RAM map. The register writes are in the handlers and the
uploads, which P2 had already converted, so bringing them to the bar was
restructuring around calls that already existed (`vid_enable_line_effects`,
`vid_set_display`, `vid_raster_display`, `vid_upload_vram`,
`vid_upload_palette`, `vid_upload_oam`, the SNES transfer escape for the
described transfer, the timing service's acknowledgements). No service call
was added. Two things no service expresses were found, and both are left in
the original for now rather than escaped: the window quadrilateral rasterizer
`$D1:F9E7` writes the window table through the work RAM port with 16-bit
stores to `$217F`, so the low byte of every edge also lands on APU port 3 (the
P1 leftover in TODO.md, now located); and the battle's screen setup
`$D1:F1B0` sets the line interrupt's line (`$4209`), for which the timing
service has no call yet (PLATFORM.md's sketch has `tm_set_line_handler`,
never built).

**What cost the time** was A's high byte. Liveness has it live after most of
these routines (their callers' later 16-bit loads and `TAX`es take it along),
and no declaration leaves out A's low byte alone, so a port that is otherwise
pure logic still has to return exactly what the original leaves in A, path by
path: the handler that ran last, the flag byte a skipped slot loaded, the
distance the proximity test measured. For the effect slots that is a return
value on every handler. The same byte enters as an index (`TAX` after an
8-bit load), so the entries whose callers always leave it 0 are guarded on
that instead of carrying it through every table lookup; the handful that
genuinely take it (`$D1:EA4B`'s level lookup, `$D1:ECF3`'s first block,
`$D1:EEA5`'s start line, the animated color `$CF:E58A` indexes with what the
party palette cycle leaves) pass it as a parameter. A routine made of
call-outs, like the frame update, also has to set A's low byte before each
call even though no callee reads it, since the call's comparison includes it.

**What shadow mode and fuzzing found** is the pilot's list again: flags a
caller reads that the early ports reproduced by emulating every instruction
and a rewrite has to set on purpose (the IRQ's pushed P under `$FD:FFFD`, the
carry `$D1:FE4C` hands back through `$CE:EE6E`, the overflow of
`$D1:E8D9`'s last address step, the carry the fade tables' last `CPX`
leaves, the frame update's overflow, which its callees pass on), 16-bit
`INC`/`DEC` read as 8-bit (the script pointer's step back, the wipe's count,
the homing jump's arrival count, which carries into the next slot's byte),
16-bit wraps where the original's indexed access runs into bank `$7F` (a
palette offset, the effect slots' anchor offsets, the rotated colors), a
direct-page index that wraps in bank `$00` where the port had used the work
RAM address, and orders of reads and writes that matter where two tables
overlap: the effect chase's cells, and the object drawing, whose sprites,
written far enough on, land on the drawing page's own cells (the object
number, the counts, the base position), which the original reads back. Two
more were copies that read the stack page: the tile row upload's source can
cover the bytes its `PHB : PHA` leave there and its count on the direct page,
and the sprite tile copy the return address of its own `JSR`, so the ports
write those bytes too, as the deferred uploads' port already did. None of it
is reached by a movie.

**The fuzzer's limits showed.** A native loop can't be stopped the way the
interpreter's step limit stops the original, so the object drawing, which on
random RAM can loop forever reading its draw list back from what its own
sprites overwrote, is fuzzed with the part and sprite counts bounded (PORTING.md
says how). And a copy whose destination covers its own return address sends
the original somewhere the port can't follow; the entries that take a
destination from RAM are fuzzed with it kept above the stack.

**The draft tool** was used for its headers and liveness far more than its
text, as in the pilot; with the listing at hand the logic was written from
the listing. Its dispatch-table handlers draft as separate functions, which
is right, but the burndown only credits a handler that is registered, so each
inlined helper and handler also became a (small) registry entry of its own.

### What drafts don't do yet

These are the tool's next improvements, or its known limits:

- **D or DB unknown** in 12-16% of drafts: interrupt handlers and what only
  they reach, and bank `$FD`'s palette code, whose data bank comes from callers
  the analysis doesn't connect. Those accesses stay `m_dp`/`m_abs` offsets.
- **Names are the map's.** An address the map doesn't have stays a number, and
  one it names for another use of the same bytes is taken as it is (the event
  commands' scratch `$01DB` comes out as a field sprite variable); a name
  landing deep in an array (`RAM_BATTLE_PART_OFFSETS_X + 41 * ..._STRIDE` for
  `$9FEB`) is worth checking against the map's bounds.
- **I/O stays register writes**, named in comments: turning `m_write8(s,
  0x002100 /* INIDISP */, 0x80)` into `vid_set_display` is the porter's, as is
  recognising a multiply through `$4202`/`$4216` (the multiplier and divider
  are left as they are, which ports may do).
- **Operand names** come only for event commands; battle animation and enemy
  script handlers are named, but their operand reads (through a script pointer
  rather than an index register) are not.
- **Open bus.** `menu_nmi_work`'s acknowledge is a CMP against `$4210`, whose
  bits 4-6 are open bus and read differently natively; its port marks the
  carry don't-care at the next call, and liveness can't know to.
- **Structure** stays a goto where regions interleave (28% of drafts have one),
  and a `switch` over a jump table goes to its cases by label.

- **Inlined subroutines and shared shape** (from the $C7 pilot). The draft of
  `$C7:0004` is 690 lines with 49 gotos because it inlines every stage of
  `play_song`; the six copies of the header-retry and upload loops that the
  port shares as helpers appear once per site.
- **Stub signatures still vary by what is read back** after a call (the
  flags a callee may set), so a callee called from several places can still
  get a few stubs (`$C7:09DA`: five in `$C7:0004`'s draft).
- **Liveness stays conservative where a callee leaves through its caller's
  frame** (the sound driver's `$C7:0A39` returns past `$C7:0004` on its refused
  path, so the driver's register saves count as reads of everything, for every
  routine that sends it a command) and across the menus' task switch at
  `$C2:821E`, which copies S. Both are "a callee reaches it" above.

### Phase 3, wave 1: the scene engine and scene data (`$C3`, `$C6`)

The scene engine's bank went from 44% left to 25% by `python3 tools/burndown.py`
(11 entries to 28), and the scene data bank from 100% to 68% (25 entries); what
is left in `$C3` is code the beam's counters decide, one routine nothing reaches
and the edge sequence `field_rasterize_quad` inlines, which the tool can't
credit (PORTING.md's rows). The early ports (the NMI, the sprite lists, the
scaler, the multiply) were rewritten to the bar on the way. Found on the way,
about the workflow rather than the code:

- **A stop inside the original's loop is ambiguous.** Shadow mode and
  `ct_fuzz` take a stop as reached the first time the original passes that
  instruction with the same S. The sequence interpreter's loop head, or any
  instruction a command shares, is passed again and again, so a port that runs
  several commands and then stops there (a command it doesn't handle, a script
  that never yields) was compared against the original's first pass. Such a port
  now puts the script back on the command and makes the rest of the run a
  call-out to the original's loop from the entry's own `JSR` site, which leaves
  the stack exactly as the original has it; the divisor loop `$C3:0E29`, whose
  loop head is its entry, hands a loop that never ends back after its first
  pass, where the machine is also after all 256.
- **An entry on a path the port also runs** is the nesting rule of Call-outs in
  PORTING.md again: `$C3:0C28`, where the sound wait goes on, is also where the
  store command's path continues, so the original entering it inside a run
  started a comparison of its own, which took the run's call to the sound
  driver. It isn't an entry; before registering an address in the middle of a
  routine, check that nothing else a port covers runs through it.
- **Chained carries** (`ADC` without `CLC`) run through the scaler's samples,
  the circle's midpoint steps and the sequence commands, and callers read the
  carry and overflow after them. A `bool carry` threaded through the logic
  reproduces them; fuzzing checked every one first time.
- **Open bus in a status byte kept in A**: `$4211` and `$4212` return open bus
  in the bits they don't define, which the original reads as the instruction's
  last operand byte; where A keeps the byte, the port masks those bits.
- **Everything live after an indirect tail jump** (docs/TODO.md): bank C6's
  routines are reached through `JML [$005B]`, so liveness keeps every register
  and flag live after them, and each port reproduces all of them.
- **Fuzzing long native runs**: `ct_fuzz` gives the original four times the
  call-outs' step budget, but not the port's own work, so a port that runs a
  long script natively can be reported as "original never reached" where the
  original only ran out of steps; `--step-limit` fixes it. Pointers into I/O
  space (object slots, script pointers) need `--fill` to stay in RAM, where the
  game keeps them.
- **The draft tool** names a direct page cell by whichever page has that D: the
  menu's page and the scene's are both `$0000`, so the C6 drafts named the menu
  object mode's cells after the scene program's (`RAM_SCENE_FRAME_READY` for
  `$00`). Entries with more than one entry width need `--m`/`--x`.

Effort: a long working day for 53 entries (1.1 KiB of bank C6 among them,
counting the jump-table copies); the sequence interpreter and the circle were
most of the reading, the ambiguous stops most of the debugging, and machine
time (the library in shadow mode, 10 minutes at two at a time; the gate with
every route about an hour) most of the waiting.

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
  "Hand ports in `on` mode"). The timing spike found that only short,
  computational calls could be made exact, and the decision is to ship hand
  ports with estimates and gate the estimates' error ("Can a hand port be
  exact?").
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

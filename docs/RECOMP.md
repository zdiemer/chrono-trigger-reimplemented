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
| `dev/ct_dev.c` | `--include PREFIX`, `--timing-report`, `--port-timing estimate\|oracle`, `--port-horizon strict\|interrupts`; later `--page-report` ("Page kinds", below) |

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
`tools/vblank_check.py` looks for it. It is one piece of the timing gate, and a
row of `tools/suite.py`'s `CHECKS` (`--no-vblank` skips it).

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
in with reasons. It lists seven ("The timing gate", below, has the first four;
the $C7 pilot added three).

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
could break. It is one check of `tools/suite.py`, beside the vblank check above
and the route runner, which joined it; `ct_play`'s default is the shipping mode
(`--native on --recomp on`) since.

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

**Dispatchers: the time of the routines a port runs** (between Phase 3's
waves 1 and 2). The exceptions went from 11 to 27 in wave 1, and most of the
new ones had one cause: a port that runs a dispatcher's handlers as C calls
rather than calling out to them (`palette_animate` and its eight types, the
battle's effect slots and its fifteen path handlers, the object drawing pass
and the six routines it calls per object) spends its time on whichever
handlers ran, and one `cost_milli` for the whole call can't fit that:
`palette_animate` ran twelve delay counts one frame and a fade of 256 colors
through the multiplier the next, and missed by 25.9% on average while each
handler on its own fitted its own scalar within 0.3-4.4%. Wave 2's banks are
full of dispatchers, so the fix is in the estimate rather than in more
exceptions.

A port now names the routine whose work it is doing, by its address:

```c
NATIVE_TIME_AS(s, 0xCFEDB8, draw_underlays(s));        /* $CF:EDB8's time, not the pass's */

int was = native_time_as(s, type_handlers[type]);      /* around several statements */
x = run_type(s, slot, type);
native_time_end(s, was);
```

and the port's bus clocks in between are estimated with the `cost_milli` of
the hand port registered at that address instead of its own
(`runtime/port/native.c`: the running job keeps the routine it attributes to,
closes a part when that changes, and `estimate_parts` sums DMA's time as it is
and each part's other clocks times its routine's value). Nothing in `game/`
says how long anything takes: the tag is an address, and the value it selects
is calibrated like every other entry's. That works because the original runs
the handlers as routines, so shadow mode already measures each of them on
every call it makes, from its entry to its return, as an entry of its own
(nested inside the dispatcher's comparison); the dispatcher's estimate is then
the sum over the handlers it actually ran, at their own measured rates, plus
its own loop at its rate. Scopes nest (a fade handler attributes its colors to
its channel mode's loop), a scope naming the running port is its own time, and
a helper that is a routine of its own can carry the tag inside it
(`battle_sine_scaled` names `$CF:F9FB` wherever it runs) so every caller
attributes it.

The shadow report lists the attributed clocks per routine (`timing: part
<addr> <name> native-bus N dma M cost_milli C`), and `tools/timing_gate.py`
estimates an entry as its own clocks at its value plus each part at its
routine's. `--calibrate` works the handlers out first, from their own calls,
and then gives the dispatcher's own part what the handlers' new estimates
leave of its time. An attribution to an address with no hand port counts as
the port's own time, and the report names it; the gate fails on it with no
exception possible, and `tests/test_timing_gate.py` checks every literal tag
in `game/` against the registry. `tests/port/time_parts_test.c` runs a made-up
dispatcher and two handlers in shadow mode and checks the parts' clocks, the
handlers' own measurement and the report of a bad tag. Nothing shadow mode
compares changes: a tag only decides which `cost_milli` a port's clocks are
estimated with, in the report and in `on` mode's charge.

A tag needs a hand port at its address to be measured. Where the part of a
dispatcher that varies wasn't one, it became one, as an entry of its own with
its guard, don't-care set and fuzzing (and `tools/dontcare_check.py` agrees
with all four): the underlays' marker `$CF:EE2C` and prepared number sprites
`$CF:EE9F`, which the original jumps to, and the fades' restore paths `$FD:E8AC`
and `$FD:E624`, which it branches to: a fade that ends by restoring its colors
copies up to 255 of them at a rate nothing like the level arithmetic's, which
was the fades' and `palette_animate`'s whole worst case (30817 clocks). An
entry can start at any instruction the original reaches, and shadow mode
measures it from there to its return like any other.

Adopted on the dispatchers behind the wave-1 exceptions: `palette_animate`'s
types and the fades' channel modes and restore paths (`core/palette.c`), the
battle's effect slot handlers with the flicker step, the orbit step, the sine,
multiply, distance and angle helpers (`battle/effect_paths.c`), the object
drawing's six routines per object, the trail's afterimages, the underlays'
three and the tile copy (`battle/draw_objects.c`), and the menu text's tiles
and numbers and the glyph's pixels (`menu/text.c`, `menu/glyphs.c`).
Recalibrated over the library, the entries it changed:

| Entry | mean \|error\| before | after | vblank stretches off by >= 1024, before | after | largest in vblank, before | after |
|---|---|---|---|---|---|---|
| `palette_animate` | 25.9% | 1.8% | 57.66% | 0.46% | 26796 | 1725 |
| `palette_fade_to_white` | 3.3% | 0.4% | 4.45% | 0.00% | 17718 | 889 |
| `palette_fade_to_black` | 1.1% | 0.5% | 1.07% | 0.00% | 30817 | 527 |
| `battle_step_effect_slots` | 18.1% | 4.9% | 7.91% | 1.94% | 18133 | 2838 |
| `battle_draw_objects` | 10.7% | 3.7% | 92.23% | 85.36% | 23882 | 7311 |
| `battle_draw_object_underlays` | 55.4% | 12.7% | 8.96% | 8.11% | 17409 | 1855 |
| `menu_draw_text` | 2.0% | 2.1% | 32.39% | 19.35% | 4509 | 3498 |
| `menu_draw_glyph` | 5.1% | 5.5% | 53.89% | 59.92% | 2658 | 2932 |

Over all 523 entries the estimate's error fell from 95.4 to 52.7 clocks a
stretch on average (7.01% to 3.89% of hand-port time), and the stretches
off by 1024 clocks or more from 1.115% to 0.512% (in vblank, 1.053% to 0.723%).

**The exceptions, before and after**: 27 limits over 16 entries before, and
18 limits over 13 entries after. Removed: all six of `palette_animate`,
`palette_fade_to_white` and `palette_fade_to_black`, and the `vblank-worst` of
`battle_draw_objects`, `battle_draw_object_underlays` and
`battle_step_effect_slots`. What is left, and why each stays:

| Entry | Over | Why |
|---|---|---|
| `decompress`, `decompress_direct` | `vblank-tail`, `vblank-worst`, `worst` | Not a dispatcher: its time follows the compressed data (literals against copies of every length) inside one routine; unchanged (above). |
| `field_rasterize_quad` | `vblank-tail`, `vblank-worst` | Its time follows the quadrilateral's edges; the edge sequence it inlines is no routine the original calls, so there is nothing to attribute to. |
| `menu_object_wave_hdma`, `battle_screen_wave_windows`, `scene_draw_circle`, `scene_draw_circle_jump`, `battle_step_hp_gradient` | `vblank-tail` | One routine each, its time following a path inside it (the sign of each wave step's product, the circle's midpoint steps, the gradient's rows): long calls whose error is 1-5% but over the tail's absolute 1024 clocks. |
| `menu_draw_glyph` | `vblank-tail` | Its pixels are a routine (`$C2:5E89`) and are attributed to it, but that routine's own time follows the pixel's bits (7.1% on its own), so the sum varies as much as before. |
| `menu_draw_text` | `vblank-tail` | Its tiles and numbers are attributed (the share off by a line fell from 32% to 19%); the control codes' handlers are no entries yet, and the code loop's own time follows the string. |
| `battle_draw_objects` | `vblank-tail` | Every routine it runs per object is attributed (10.7% to 3.7%); what is left is its own work, building and sorting the draw list (an exchange sort whose swaps depend on the order) in 37-line calls, where 3.7% is still over a line. |
| `battle_draw_object_underlays` | `vblank-tail` | Its three routines are attributed (55.4% to 12.7%); what is left is its own loop over the object's eight effect records, whose time follows which are set. |
| `battle_step_effect_slots` | `vblank-tail` | Its handlers and the helpers they call are attributed (18.1% to 4.9%, and 7.9% of its vblank calls off by a line to 1.9%); what is left follows its own loop over which of the nineteen slots are active and due, and the handlers' own spread. |

None of these writes a video register: they write RAM the next vblank uploads,
and the vblank check finds no video write moved.

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
   direct-page access is the work RAM address D + offset, named by a variable
   `re/ram.toml` puts on a page with that D (step 7), or else a plain scratch
   name; when D isn't known the
   access stays `m_dp(s, offset)` with a note in the header, and `--page NAME`
   names the offsets of a page kind, whose D is not fixed (`scene_math`,
   `menu_record`), through their `_DP` macros. `--d`/`--db` give values by hand. A draft that
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
   RAM_X_STRIDE`), the subsystem being the bank's or, in the banks several
   share, the callers'; a direct-page cell only from a variable on that page,
   else `SCRATCH_01DB` ("The tooling pass after wave 1: the analysis"); I/O registers by their conventional names in a comment
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
Call-outs are also held to a second solve that trusts no target set the machine
picks at run time, which doubles the solve ("Call-outs into targets the machine
picks", below).

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
`RTS`, none do. The tree as this work left it: 0 findings over 284 entries with
a don't-care set, 56 stop and 45 call-out declarations (a value per helper call
and table row). The counts have grown with every wave since; the check still
reports 0.

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
- **Names are the map's.** An address the map doesn't have stays a number (a
  direct-page cell a `SCRATCH_` name), and an absolute one it names for
  another use of the same bytes by the routine's subsystem is taken as it is;
  a name landing deep in an array (`RAM_BATTLE_PART_OFFSETS_X + 41 *
  ..._STRIDE` for `$9FEB`) is worth checking against the map's bounds.
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
- **Liveness stays conservative where S is saved in memory**: across the
  menus' task switch at `$C2:821E` and the menus' entry `$C2:800E`, which copy
  S (a `PHP` there reads every flag, which is what keeps the overflow flag
  live after the field loop's callees), and in the scene program's task lists,
  whose tasks leave through the loop's own code rather than return ("The
  tooling pass after wave 1: the analysis", below).

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

### Between the waves: the validator

Wave 1's reports named five places where shadow mode and the don't-care check
made a port reproduce what the original leaves dead, or broke on a port shaped
the natural way. Each is fixed in `runtime/port/native.c` and
`tools/dontcare_check.py`, under one rule: anything that lets shadow mode
compare less is shown sound (from liveness, or from the original's own
execution), and a deliberately wrong port is shown still caught. The runtime
side is tested on a synthetic program (`tests/port/shadow_test.c`, in CI), the
check's on synthetic code (`tests/test_dontcare_check.py`), and each on the
ROM below.

**A's low byte alone: `DC_AL`.** Liveness has A's two bytes apart, but only
the high byte could be declared by itself (`DC_AH`), so a routine whose callers
read A's high byte (a later 16-bit load or `TAX` takes it along) had to return
the exact low byte an 8-bit routine leaves, on every path: the battle's effect
handlers returned "what the original leaves in A" as a value, and the frame
update set A's low byte before each of its call-outs although no callee reads
it. `DC_AL` (bit 10) leaves out the low byte alone, at a return, a stop and a
call-out; with it every register byte and flag liveness tracks can be declared
on its own. Sound for the same reason as every declaration: the check maps
`DC_AL` to liveness's low-byte bit and verifies it where shadow mode applies
it, and at a call-out a low byte the callee left as it found it goes back to
being the port's own (`keep_untouched`), so a port that relies on the value it
declared dead there is compared as the shipping mode would run it. Shown: the
synthetic program's routine that returns a wrong low byte under `DC_AL`
matches and one with a wrong high byte doesn't ("A's high byte original 12
native 99"); one that declares the low byte dead at a call-out and then stores
it is caught at its return; on the ROM, `DC_AL` on `battle_sine_scale`'s entry
(its callers read the result) and on the call to `$CD:3E8E` (which pushes the
bank A holds) are findings. `--list` now says, for every declaration, what
else liveness has dead there (`could add`): 312 declarations could add
`DC_AL` before the ports below adopted it, 291 after (295 once merged with
the fuzz and timing pass's new entries).

**A stop's own set: `m_stop_at_own`.** An entry's set applied at its returns
and at every stop it made, so one early exit to code that reads a register the
callers don't shrank the whole entry's set: the frame update's demo exit to the
reset code at `$C0:FF00`, which reads Y and V, kept both out of a set that is
otherwise dead after the return, and `input_read_joypad` declared V at its
`RTS` as a stop to get around it. `m_stop_at_own(s, at, set)` (and
`native_own_dont_care_at_stop`) makes `set` the stop's alone: shadow mode and
`ct_fuzz` leave out exactly that set there, and the check verifies it alone at
`at`; the entry's set then applies only at returns and at stops that aren't
their own, and is checked at each (`native_finish_dont_care` is the one rule
both runtimes use). Nothing is compared less than it is declared, and every
set is checked where it applies. Shown: on the synthetic program, a wrong Y at
an own stop is caught and the same stop declared with `m_stop_at`, which adds
the entry's `DC_Y`, hides it; on the ROM, `scene_run_sequence` with D added to
its entry (dead after its return, read at the sound wait) and a wrong D at the
wait matches all 32854 calls of `endings-beyond-time` as a shared stop, which
the check rejects ("where liveness has D read"), and mismatches 5142 as its own
stop, which the check accepts; an own set with V at `$C0:FF00` is rejected.

**Call-outs compare exactly what the callee reads.** With `DC_AL`, a call-out's
set can be the complement of `Liveness.callee_reads` bit for bit, and the check
verifies it there as before. Making the runtime leave out what no callee reads
by itself, from a liveness table built with the generated code, was considered
and not done: it would put the ROM's analysis into the runtime (and make what
is compared depend on the build's call logs), and it would move the claim out
of the source, where the declaration is reviewed and `--list` shows what else
could be declared.

**Stops and calls at a loop's head.** Shadow mode took a stop or call-out as
reached the first time the original passed its instruction with the same S, so
a port that ran several passes of an interpreter loop and stopped at its head
was compared against the first, and ports worked around it (the scene
sequence interpreter handed the rest of a run back as a call-out from its
entry's own `JSR` site, which is compared at the same first pass and was only
right when no command had run in that stretch). Now every pass is compared,
and the first where the machines are the same counts (`reach_point`). It can't
accept a wrong pass: the comparison covers everything the original's code can
observe except what is dead there (the declared sets, checked against
liveness, and the stack below S) and what changes with time rather than code
(HDMA's registers, the SPC700's port clears, the OAM reload), which the first
pass left out as well. What the original does from a pass is decided by the
machine at that pass, so from a pass where the two are the same the original
goes on exactly as the shipping mode goes on from where the port stopped:
whichever pass it is, what follows is what the original did. A port that is
the same at no pass is reported with the first pass's difference, and the
last's, once the original leaves the point's frame, when an interrupt comes
(its handler runs on the original only, so a later pass can't be compared:
reported as a mismatch, as the first pass's difference was before), after
65536 passes, or when the run ends with it still waiting (`native_failed`).
So nothing the old rule reported as a mismatch passes now unless a later pass
matched exactly. A failed pass costs a comparison per later pass until then.
`ct_fuzz` does the same over the original's passes. Shown: on the synthetic
program, a stop after two passes of a loop and a call-out from a loop's head
after two inlined passes match (under the old runtime, every call mismatched,
"X original 0 native 2"), a stop whose memory matches no pass is caught ("and
at 2 later passes, then the original left"), and in a loop longer than a
frame a port matching no pass is reported at the NMI. On the ROM,
`scene_run_sequence` made to stop at its loop's head `$C3:0B03` after one
command: in `title-start` 235 of its 1500 calls mismatch under the first-pass
rule and none now; the same with the script pointer one byte off, 238 mismatch
("... at the last: WRAM[0A20] original 3B native 3C").
`scene_run_sequence` stopped at `$C3:0B03` in place of the workaround, until
the fuzzer as it is after the fuzz and timing pass found 4793 of 20000 states
where no pass matched, and the comparison was right not to take one: a command
the port gives up on (one no script has) has already written the scratch cell
`$F0` and changed the flags, which the original does only once it runs the
command, so at the loop's head, on every pass, the port's machine is one the
original never has. The port is back to the call-out (which the fuzzer
compares only at the return, after the original has run the command itself),
and putting those back first is in docs/TODO.md.

**An entry on another port's path.** An entry the original reached while it
was catching up with a native routine started a comparison of its own, nested,
and only the innermost comparison was checked, so one still running when the
original got to the point the enclosing routine waited for took that point
for itself (the same call, typically), and the enclosing comparison failed:
the $C7 pilot's `play_song`, and `$C3:0C28`, where the scenes' sound wait goes
on and the store command's path runs through. The runtime handles it rather
than the check rejecting it: at each step the outermost routine waiting at the
original's point comes first (`settle_nested`). The nested comparisons are
checked there first, innermost first, as before, and one that finishes or goes
on (its own point is here too, or its callee unwound past it) is dealt with as
it was; any other is set aside, counted as `abandoned` with a note naming the
enclosing routine, and the enclosing one is compared (a difference a set-aside
one had shown stays a mismatch, and one waiting for a later pass is reported
then). That compares less only
for a call the shipping mode never makes (there the enclosing routine runs
natively and the entry isn't called at all), and the comparison it gives way
to covers everything the stretch did. Nothing that matched before is set
aside: nested comparisons that end inside the stretch, the common case (a
helper the port inlines and the burndown credits as an entry), are compared as
they were, and those that outlasted it made the enclosing one fail. Over the
library the shadow runs' counts are the same as under the old rules for every
entry (calls, 64558726 matched, 64158 interrupted, 489 abandoned, none
mismatched), so both this and the loop heads change nothing that passed. A
first version didn't start an entry inside another's stretch at all, which is
the shipping mode's rule, but that dropped 17.3 million of those comparisons
(the battle's per-object drawing routines and the menus' glyph and number
routines, all inlined by their callers) and moved four entries' timing past
the timing gate's limits, so it was replaced. A check could only reject such a
registration by knowing which of the original's instructions each port stands
for, which the burndown only estimates, and it would forbid the registrations
porters want. Shown: on the synthetic program, an entry registered in the
middle of another's path took the outer one's call-out (all 250 of the outer's
calls mismatched, "original returned without the call"); now both match, the
inner one set aside once per pass inside the outer's stretch and compared at
its own calls, and a callee the outer inlines, registered as well, is still
compared at every call (none abandoned). On the ROM, `$C3:0C28` registered for
the demonstration: under the old rule `events-jetbike-race` reports
`scene_run_sequence: original returned without the call to C70004`, the scene
agent's failure, and `endings-beyond-time` 8 such (2 of `scene_run_sequence`,
6 of the new entry's own); now none, and the new entry is compared at 26 of
its 35 calls there (1 of 2 in the race), the rest set aside where they
outlasted a run's call.

**A reader fix.** The check read a table's struct with two fields in one
declaration (`uint16_t flag, copy;`, `battle_run_queued_scripts`' table) as one
field, which shifted every column after it, so a declaration taken from such a
table would be checked with the wrong address or set; it now reads every
declarator (`struct_fields`, tested on synthetic code).

**The ports that adopted them**, each changed only as far as the declarations
go, fuzzed again and matching over the library (PORTING.md's rows):
`battle_frame_update` declares Y and V dead after its return, its demo exit
its own set, and its call-outs `DC_AL | DC_X | DC_Y | DC_FLAGS`, so the low
byte it set before each call is gone; the fifteen effect handlers, called by
themselves, and `battle_place_formation` declare `DC_AL` and no longer
reproduce what the original leaves in A's low byte; `input_read_joypad`
declares Y and V after its return and its two stops their own sets, in place
of its stop at the `RTS`. (`scene_run_sequence`'s stop at its loop's head was
taken back, above.)

### The tooling pass after wave 1: the analysis

Wave 1's reports (above) named what the analysis side cost the ports:
liveness keeping every register live after bank C6's routines and carry and
overflow live after the field loop's callees, a burndown that credits nothing
a port inlines, and drafts that name scratch cells after unrelated variables.
Before wave 2 takes on the three big banks, each was fixed where it could be
made precise without claiming anything the original doesn't show.

**Liveness through indirect jumps** (`tools/disasm/liveness.py`). A routine an
indirect `JMP`/`JML` reaches counted everything as read after it returns, and
the jump itself used only a hint's targets, not the ones derived from data.
Now the jump goes to every target the analysis follows; a `JMP (abs)` after a
`PER` is a call that returns to the `PER`'s address, any other a tail jump
whose targets return for the routine making it, so their callers' liveness is
that routine's. That holds only if nothing else reaches them, so a routine
keeps everything live where some site that reaches it has a target set nobody
argued complete (neither a `derive` nor a `bound` hint: a movie-only list could
have other targets from anywhere). All eight indirect jump sites in the ROM
are argued, so all 176 routines they reach are now precise, none kept live;
the count is `Liveness.indirect_entries`. After the C6 routines the menu object
scripts reach, X and the carry are what is read.

**The field loop's carry and overflow.** They were the analysis being coarse,
not the loop reading them. Following the reads back from the loop found two
causes. The sound driver's entry `$C7:0004` saves P and X (and the rest) and
calls `$C7:0A39`, which on its refused path pulls its own return address and
runs the entry's exit: its saves "reached by a callee" counted as reads of
everything, for every routine that sends a sound command. A push a callee
reaches is now followed into the callee: where every path either returns
normally or pulls the byte with a pull of its size and then only pulls the
rest of the caller's frame and returns as the caller does, the push reads what
is live after that pull, which is what the caller's callers read. A return
below the depth a routine was entered at is a return through its caller's
frame, and its liveness is carried apart from an ordinary return's (packed
values have a second set of segments for it), so what follows the escape isn't
merged into the callee's other returns. The second cause was the analysis
walking every context's width variants at an instruction: `$FF:F9C4` runs its
`PHX` with 8-bit X from one caller and 16-bit from another, which read as
depths that disagree, so its reach was unbounded and every push around a call
to it read everything. Each context now runs the widths it was traced with.
What stays: the overflow flag, which the menus' entry `$C2:800E` saves with
`PHP` and then copies S into `$0920` for a stack of its own (`TSX`), which no
pairing follows (docs/TODO.md). `RTI` now reads no flag (it pulls P).

**What turned out wrong, and was fixed.** Three places where liveness was *less*
careful than the code: a `JSR (abs,X)` whose table is filled at run time
(count 0, every target derived from data, as the scene program's task lists at
`$7E:3998`, `$7E:39CF` and `$7E:37F6` are) called nothing at all, so the task
loop read nothing its tasks read and the tasks' callers' liveness never
reached them; the derived and hinted targets of any call table were left out
of what callers read after them; and a `PER` then `JMP (abs)` was taken for a
tail jump, dropping the code after it (it happened to return to an `RTS`). The
task loop's register saves, which tasks leave through `JMP $39A7` without
returning, were "paired" because the tasks weren't known; they are now "a
callee reaches it", and a return through a caller's frame counts what is read
after it. With the tasks known, five declarations in `game/` were wider than
the original allows, and were narrowed, the ports changed as little as that
needed: the scene sprite lists `$C3:0D0E`/`$C3:0D5E` now leave Y (the next
free entry) and A (the loop's last value) as the original does, and
`$C3:0DBC` Y, since the scene task loop carries them into the next task, and a
task reached the field's start (through the reset), whose menu call reads Y;
`$C3:094C`'s Y was already the original's and is no longer declared; the
sequence interpreter's call to `$7E:37F3` passes its carry, which a task's
chained `ADC` reads.

| | Before | After |
|---|--:|--:|
| routines everything is live after (all 3136) | 209 | 34 |
| of them: entered by an indirect jump | 175 | 0 |
| register and flag bits live after returns, summed | 14677 (flags 3968) | 11731 (2192) |
| bits read at entries, summed | 11895 (flags 2409) | 10255 (1324) |
| routines A's low byte / X / the carry / the overflow is live after | 511 / 1032 / 1296 / 1884 | 298 / 850 / 998 / 800 |
| routines with something dead after return | 2820 | 3092 |
| bank C6 (41 routines): everything live after | 34 | 0 |
| bank C6: A / Y / any flag live after (routines) | 35 / 37 / 36 | 1 / 0 / 35 (the carry) |
| `$FD:C2C1`: live after it returns | AH X V C DB D | AH V DB D |
| `PHP`s paired / a callee reaches it (per context) | 567 / 87 | 579 / 75 |
| pushes paired through a callee that leaves the caller's frame | - | 12 (`$C7:0140`-`$C7:0149`) |

(Pushes of any register counted per context moved from paired to "a callee
reaches it" in 145 places, every one around the scene task loops' calls, which
were paired only because the tasks weren't known, and the other way in 63.)

**Why it is sound.** Everything the check now accepts that it rejected before
is dead on every path the analysis knows the original takes: a tail jump's
targets return exactly where the routine making the jump returns, and are
reached from nowhere else unless a site whose targets aren't argued complete
reaches them (then nothing changes); a pull in a callee is followed only where
every path from it pulls the rest of the frame and returns as the caller would,
so what is live after it is what the caller's callers read; an instruction runs
in a context only with the widths the analysis traced there; and `RTI` replaces
P. The assumptions are the ones liveness already made for `JSR` targets (that
the analysis sees every caller; the call logs back it up for the movies'
paths). Checked three ways: `tools/dontcare_check.py` finds nothing on
`game/` as the pass left it (417 entries, 67 stops, 73 call-outs); a
declaration the new liveness allows and the old one refused (`menu_object_read_mode` declaring
`DC_A | DC_Y | DC_N | DC_V | DC_Z`) passes, and one that is wrong (the same with
`DC_X`, which its callers read) is still a finding; and the drafts, which are
made from liveness, match the original over the library (below).

**Drafts before and after.** `tools.recomp.draftcheck` over the library (26
movies, shadow mode), with the committed tools and with these: 194 drafts
registered before, 197 after (three entries the analysis enters with several
widths now pick one, below); 166 and 169 called, 39.09 and 39.10 million calls,
none mismatching either time (84819 and 84841 interrupted), and the don't-care
check finds nothing in the drafts' own declarations (471 entries, 124 stops,
519 call-outs). The drafts the fixes were for: the C6 routines' headers went
from "callers read A X Y N V Z C" to "X C" (`$C6:E237` 3 flag assignments to 1,
registering with `DC_A | DC_Y | DC_N | DC_V | DC_Z` where it had 0), and
`$FD:C2C1`'s from "A X V C" to "A V", its logic losing the carry (12 mentions to
none) and X from its result. The gate passes (`suite OK` with
`--long-routes`), and the strict player builds warning-free.

**The burndown** (`tools/burndown.py`) credited a port with its routine's code
only, so every helper a port inlined had to be a registry entry to count, and
it read the registry with a pattern that missed its macro rows (the 180 event
commands, the field uploads). It now reads the entries as the don't-care
check does, and follows a call into its callee where the port inlines it:
where the port makes no call-out at that call, by the call-outs its functions
make, read through the file's helpers, tables and parameters the way the
don't-care check reads declarations. The original makes that call during the
port's call and the port doesn't, so the port does its work, which shadow
mode would otherwise show. It doesn't over-credit where the reading can't be
trusted: a port with a call-out (or a PC it sets) the check can't read, or
one whose site reads as something that isn't a call (a table whose struct
declared two fields in one declaration came out shifted, which the
validator's pass has since fixed: "Between the waves: the validator"),
credits no callee, and neither does a call past a place the port hands back
(an `m_stop_at`, a PC it sets), since the original may be making it; 22 ports
credit none for those reasons, among them the two that would otherwise claim
50 KiB between them (`battle_run_queued_scripts`, `battle_main_loop_body`).
Idle entries credit none either. Per bank (the wave-1 banks and the three big
ones):

| Bank | Before | The macro rows | And inlining (now) |
|---|--:|--:|--:|
| `$C0` | 9.6K, 84% left | 18.3K, 69% | 18.7K, 68% |
| `$C1` | 10.0K, 84% | 10.0K, 84% | 10.7K, 83% |
| `$C2` | 5.2K, 91% | 5.2K, 91% | 5.6K, 90% |
| `$C3` | 3.0K, 25% | 3.0K, 23% | 3.5K, 10% |
| `$C6` | 1.1K, 68% | 1.1K, 68% | 1.1K, 68% |
| `$CF` | 6.3K, 7% | 6.3K, 7% | 6.4K, 7% |
| `$D1` | 4.8K, 17% | 4.8K, 17% | 4.8K, 17% |
| `$FD` | 8.1K, 47% | 8.1K, 47% | 8.1K, 47% |
| total | 60.1K, 78% | 69.0K, 74% | 71.6K, 73% |

`$C0` moves most, and not from inlining: 197 of its 249 entries are macro rows
the old pattern didn't see. `$C3` drops from 25% to 10% left with the edge
sequence `field_rasterize_quad` inlines (`$C3:0F35`), the frame work's
controller check (`$C3:0A8E`) and song update (`$C3:09A4`) credited; its
middle column also counts the targets of `$C3:0F32`'s indirect jump, which the
walk now follows. `$FD`, `$CF` and `$D1` barely move because wave 1 had
already registered what their ports inline, to get it counted.

**Draft names** (`tools/recomp/names.py`). A routine's subsystem was its
bank's, and banks `$C3`, `$C6` and `$FD` serve several (and `$FD` had none,
so any subsystem's name was taken). In those banks it is now the one every
caller comes back to, through the call sites, call logs and followed indirect
jumps (none where they differ or there is no caller, and then only `core`
names it). A direct-page access is named only by a variable the map puts on a
page with that D: of the routine's own subsystem, or of any where the page is
its own subsystem's (field code takes the event engine's cells on the field's
page, event commands on the field's page don't take the field's). Any other
cell gets a plain name, `SCRATCH_01DB`, which the draft defines and undefines.
Over the drafts of every registered routine (194 before, 197 after): 1016
distinct names before, 990 after, 261 scratch names, and 713 addresses left as
numbers before against 466 after. The two that went wrong: `event_cmd_call`
(`$C0:5FB6`) wrote `RAM_FIELD_SPRITE_RANGE_NEXT` for its `$DB`-`$DF` and
`0x7E01D9`/`0x7E01E1`/`0x7E01E3` for the rest, and now has `SCRATCH_01D9` to
`SCRATCH_01E3`; `menu_object_read_mode` (`$C6:E237`) wrote
`RAM_SCENE_FRAME_READY` for the menu's `$00` and a number for `$7E:1BA9`, and
now has `SCRATCH_0000` and `RAM_MENU_SCENE_MODE_BITS` (its subsystem is the
menus', its callers' ). Nine drafts lost names: `$FD`'s
`battle_clear_b328_table` a menu name it had taken for want of a subsystem,
and the C6 irises and motion routines and `$C3:0DF3` (which nothing calls) the
scene's circle and scaler cells, which they use on the menu's page
(docs/TODO.md). A routine the analysis enters with several widths is drafted
with the registry precondition's, then the widths the coverage saw it run with;
if several remain, `show` prints one draft for each rather than stopping.

### Phase 3, wave 2: the menus' Mode 7 screens (`$C2`)

The first slice of bank `$C2`, taken because it was also P2's last leftover
(PLATFORM.md, "P2 as built"): the Epoch's map view and time dial, twenty-six
routines between `$C2:631F` and `$C2:7758` that only `menus-epoch` and
`endings-beyond-time` reach, and the End of Time's gate screen `$C2:CE9D`.
`python3 tools/burndown.py` for `$C2`: 5.6 KiB ported and 90% left before, 10.3
KiB and 83% after, 52 entries to 81. All of it is in `game/menu/epoch.c`, which
is 2208 lines for 4.7 KiB of 65816. `--vid-check` went from 5506 of 8799 frames
to all of them on `menus-epoch`, and from 8234 to all of them on
`menus-end-of-time`, which closes the menus' part of P2 (PLATFORM.md; what is
left there is the gate screen the event engine opens, which is different code
and reached by neither movie).

**What the two screens are.** Both run in the menus' sprite-object mode (the one
at `$C2:0000`): they blank the screen, build a Mode 7 plane in VRAM, put one
object in a slot to drive it and loop on the frame task until the mode byte at
`$027C` changes. The map view packs the world map's tiles into one colour each
at `$7E:AB25`, builds the plane from the two map layers and turns it under a
cursor walking a fixed path, with markers for the party, the Epoch and the map
it would land on; its one per-line effect is a blue gradient. The time dial
spins a triangular gate with a Mode 7 matrix that changes every line, cut out of
the screen by window 1 whose edges change every line too, and seven digit
objects show the periods.

**Two service calls were added**, both additive:

- `vid_upload_vram_half`, for an upload that writes one byte of each VRAM word
  and leaves the other. A Mode 7 plane keeps its tile numbers in the low bytes
  and its pixels in the high ones, so the game builds and uploads each half on
  its own (`$C2:645C` feeds `$2119` alone, `$C2:65B2` and `$C2:722C` `$2118`);
  `vid_upload_vram` is fixed to whole words.
- `VID_LINE_MODE7_A` through `_D`, a Mode 7 matrix value driven per line on its
  own. The service had `VID_LINE_MODE7_AB` and `_CD`, which are the pair a
  transfer mode 3 channel writes; the dial uses four mode 2 channels instead, one
  per value, so the observer had no target for them and read the whole screen
  wrong from the frame they start.

**What it cost, and where.** The screens are mostly ordinary logic, and shadow
mode's findings were all in the same three kinds:

- **Register widths at each exit.** These routines are object handlers reached
  through `JMP ($0050)` and they hand back at several places, each with `REP` or
  `SEP` in a different state: the drift routines return 16-bit A on their working
  path and 8-bit where the objects are frozen, and the dial's object has six
  returns across three widths. The first shadow run's whole mismatch list was
  `P original 4 native 24`.
- **The dispatch leaves the step in X.** `TDC : LDA $0002,X : ASL A : TAX :
  JMP (table,X)` means a path that doesn't load the slot again returns with X
  at twice the step, which the callers read; and the `ASL` is what sets the
  carry a later call-out is compared on. One `dispatch_step` helper does all of
  it.
- **Index registers are 16 bits wide here, so a `STY`/`LDY` swap moves a word.**
  The gate's edge sort swaps its two corners through Y, so the byte after each
  coordinate moves as well, which is what the original leaves at `$09` and `$0F`.

Two more were plain mistakes the check caught: the map packer reaches the work
RAM port and the multiplier long, because it runs with the data bank on the map
(`m_abs` would have put them in bank `$7F`), and the gradient's second loop runs
its counter all the way round rather than sixteen times, writing 1792 bytes past
the 224 the table reads -- which the game never looks at, and shadow mode
compares.

**The don't-care check found two declarations** that liveness refuses: the sound
command the dial sends reads A's high byte, so the call declares `DC_AL`; and the
gate's setup ends by jumping to the object starter with the address in X, so its
stop declares its own set (`m_stop_at_own`) rather than adding the entry's `DC_X`.

**What fuzzing found**, none of it reached by a movie, and all of the same kind
as the pilot's: a routine that takes its first operand from whichever slot the
caller left in X and the rest from `$4E` (`$C2:6736`, the map's matrix); an `ADC`
after three shifts with no `CLC`, so the last bit shifted out counts
(`$C2:688A`); a period doubled with an 8-bit `ASL`, so one of `$80` or more
indexes from the other end of its own byte (`$C2:745A`); six direction buttons
tested one at a time, down and up in turn, rather than all the downs and then all
the ups; a channel pair programmed the wrong way round (`$C2:6F85` puts C on
`$211D` and D on `$211E`), whose only call in a movie was interrupted both times,
so shadow mode had never compared it; an upload that sets the VRAM port before
the address where every other upload on the screen does it the other way round,
and the address latches through the port; and a count read from memory, which
goes all the way round at 0 where a `for` loop would not run at all. Seven in
28 entries, on a chunk that shadow mode already passed over the whole library.

**Bank `$C6`'s object handlers moved.** Wave 1 ported twenty-four routines of
bank `$C6` into `game/scene/objects.c`; they are the menu object scripts'
command `$4E` handlers and two the Epoch's map screen calls, so they are
`game/menu/object_commands.c` now, with `menu_object_`/`menu_epoch_` names. No
behaviour changed.

### Phase 3, wave 2: the menus' object scripts (`$C2`)

The second slice of bank `$C2`: the instruction set of the menus' object
scripts. Every sprite object of the mode at `$C2:0000` can run a command
script, and `menu_run_object_script` `$C2:0F63` was ported long ago with its 83
handlers left as call-outs. They are ports now (`game/menu/object_script.c`),
with the five routines that start an object and make its per-frame move.
`python3 tools/burndown.py` for `$C2`: 10.3 KiB ported and 83% left before,
13.6 KiB and 77% after, 81 entries to 169. All of it is 1700 lines of C for
3.5 KiB of 65816.

**How the chunk was chosen.** `ct_dev --io-log --native on --recomp off` over
`menus-field-menu` and `menus-shop-inn` says that no unported code in `$C2`
touches a PPU, APU or DMA register at all any more: what is left of the bank's
I/O is the multiplier and a handful of `$42xx` writes. So the io-log, which
picked the chunk last time, said only that the display side of the bank is
done; the chunk came from the burndown's largest run instead (`$C2:1037`, 2617
bytes), which turned out to be 80 of the 83 handlers, and from the fact that
they are one thing: a scripting language with an interpreter already ported.

**What the commands are.** A handler hands back a length to move the script on
by, zero to leave the object until the next frame, zero with carry to free its
slot, or a signed byte to branch by -- which the interpreter adds the same way,
so a branch of zero stops the object like any other zero. They reach three
places: a byte of the running object's slot by an offset into it, a byte of low
RAM by a 16-bit address, and their own operand bytes. On top of that they start
objects and scripts, set a sprite's palette and priority, move and scroll the
map, queue VRAM uploads, send the sound driver a command, load a palette row,
copy tilemap rows through the work RAM port, and leave for another location.
`tools/disasm/objscript.py`, which already parsed the scripts for MD4, now
carries the name of each command beside its length, and the ports are
`menu_object_cmd_` plus that name.

**What shadow mode found** was one mistake, and it is the kind a map makes
easier to make than to avoid: the map's origin is not three words `$02` apart,
so the port that saves the party's place before a location change read the
wrong one. Three words `$04` apart was the model that fixed it; what it really
is, two arrays of a word per plane, is in "the menus' map" below. Everything else matched on the first run over eight movies.

**What fuzzing found**, neither reached by a movie: the mode's VRAM upload queue
is longer than a direct page, so a record's offset must not wrap in one -- the
original reaches a record's first byte through the data bank and the rest
through the page, and both run on past `$FF`; the port wrapped it and wrote the
records a page early. And the queue command's second record, `$08` further on,
had the same wrap.

**What fuzzing cost to set up**, which is the part worth writing down. Three
constraints, each for a reason the port's row records: the script pointer has
to lie in an area whose bytes are also constrained, because a command's operands
are slot offsets and 16-bit addresses and an unconstrained one reaches I/O
space, where open bus holds what the interpreter's fetches left and native code
sets nothing; the object slot has to be a real slot for the same reason; and the
slot's position and velocity have to be small, because the map routines a scroll
command calls out to walk a row per tile moved, so a random 32-bit velocity
sends them past any budget. Without the last one, twelve states in five thousand
came back as mismatches rather than as skips, and raising the budget tenfold
moved which ones -- the tell that they were the budget and not the port.

**Which calls shadow mode never compared.** Five of the new entries have
interrupted calls in the library (`menu_object_spawn` 4, `menu_object_start_script`
2, and the two commands that start an object, 2 and 3), so those calls were
counted and not compared; they are the paths fuzzing covers, which is why the
rules now say to fuzz them deliberately.

**One timing exception was added, for two entries.** Starting an object walks
the free slots until it finds one and then copies `$3B` bytes of the caller's
state into it, so the share of a call that is the copy swings with how far the
walk went, which one scalar can't fit; the command that loads a palette row is
almost all one such call, and inherits the spread through the
`NATIVE_TIME_AS` that attributes it. Both drift by 0.01% or less over fourteen
movies, their worst call is 1.2 lines out, and the vblank check finds none of
their writes moved.

**The two dispatchers came up to the bar with the chunk**
(PLAN.md: a subsystem's existing ports are converted with it). `menu_run_objects`
`$C2:051D` and `menu_run_object_script` `$C2:0F63` in `game/menu/objects.c` were
in the early style, moving emulated registers about and spelling the slot and
the script pointer as `$0B30` and `$58`; they read as source now and take their
names from the RAM map.

### Phase 3, wave 2: the menus' map (`$C2`)

The third slice of bank `$C2`: what the mode at `$C2:0000` shows behind its
sprite objects. The two chunks before this one left the mode's object scripts
calling out to three routines that scroll and redraw it (`$C2:0568`,
`$C2:066C`, `$C2:09C5`), which the burndown also had as the bank's largest
unported run; they are ports now, in `game/menu/map.c`, with everything only
they call. `python3 tools/burndown.py` for `$C2`: 13.6 KiB ported and 77% left
before, 15.0 KiB and 75% after, 169 entries to 173. 751 lines of C for 1.4 KiB
of 65816.

**What the map is.** Three planes, each 96 x 64 map tiles of 16 x 16 pixels.
A plane's map is one byte per map tile in work RAM; the byte picks one of 256
tile groups, four background tiles laid out two by two, also in work RAM; and
the group's tiles go to a 64 x 32 tilemap in VRAM, which the SNES holds as two
32 x 32 screens side by side. Three words of bank `$C2` (`$C2:0556`, `$C2:055C`
and `$C2:0562`) say, per plane, where its groups, its map and its tilemap are;
`$C2:055C` is the table the object scripts' write-a-map-byte command already
read, and all three are one accessor now. Scrolling moves the plane's scroll
shadow one 8-pixel tile at a time, carrying its top-left map tile with it, and
whenever a whole tile column or row comes uncovered it builds the 32 tilemap
words that fill it in the mode's upload buffer at `$7E:F000` and queues them
for the next vblank. A redraw is the same work for the whole plane: eighteen
blocks of sixteen map tiles, each two tilemap columns, sent straight to VRAM
with the screen blanked.

**Two things the RAM map had wrong**, both found by reading the routines that
use them:

- `menu_map_origin` was one array of three words `$04` apart at `$7E:00E5`.
  It is two: the planes' columns at `$7E:00E3` and their rows at `$7E:00E7`, a
  word each, indexed by the plane number doubled. The pair the Epoch's screens
  and the change-location command read (`$E5` and `$E9`) is the same pair
  either way, which is why the wrong model passed; but a third plane's column
  would be the first plane's row, and a third plane's tile groups would be the
  first plane's map (`$C2:0556`'s third word is `$C2:055C`'s first), so only
  two of the three planes can be scrolled at all. `$C2:2720`, which puts the
  map where the party is and came with the chunk, writes exactly those two.
- `menu_object_tile_source` at `$7E:00E0` was "a long pointer to sprite tile
  data". It is the mode's upload buffer cursor: the next free bytes of
  `$7E:F000`, which sprite tile columns and map strips are both built in and
  which `menu_object_upload_vram_queue` puts back once the queue is out. The
  routine that reads it, `$C2:0ADC`, was `menu_object_upload_sprite_tiles` in
  `menu/display.c`; its only caller is the redraw, so it is
  `menu_map_upload_block` in `menu/map.c` now, rewritten out of the early style
  (an emulated register for every step of the transfer) at the same time.

**What shadow mode found** was one thing: `$C2:066C`'s callers read X, and the
original leaves the plane number there on a pass that crosses no tile boundary
and the queue record's offset on one that does. The port left the caller's X.
Nothing else mismatched, on the first run over the library.

**What fuzzing found**, neither reached by a movie, and both about what a
caller can see of work the routine does through pointers the caller chose:

- **A queue record's source is read after the record's own bank and port are
  written.** The mode's upload queue is sixteen records at `$7E:0060`, and the
  upload buffer's cursor is right past it at `$7E:00E0`, so a queue that has
  filled up puts a record on top of the cursor; the original reads the cursor
  for the record's source only after storing the record's bank and port, and so
  takes the value those stores left. The port read it first. (The same fix
  had to address the second record a row strip queues as a set of fields `$08`
  further on rather than a record at a tail `$08` further on, so a tail near
  `$FF` doesn't fold the second record onto the first.)
- **The saved data bank is on the stack while the strip is written.** The four
  strip builders put the data bank on `$7E` for their loop and pull it back
  after, and the loop writes through the upload buffer's cursor, so a cursor
  pointing into the stack page takes the saved byte with it and the routine
  comes back with whatever the strip wrote over it. The port had dropped the
  push, since it doesn't need a data bank to address bank `$7E`; it does the
  push and the pull now, and addresses the map through the data bank like the
  original.

**What fuzzing cost to set up**: three constraints, each for a reason, and each
one a statement about how the mode uses these routines. The upload buffer's
cursor is kept as the state has it (`--keep 7E00E0-7E00E2`), because a random
one sends the strip anywhere in bank `$7E` -- over the stack, where the
original loses the return addresses of the subroutines the port inlines and
goes somewhere the port never does, and over I/O space for the redraw's DMA
source, where open bus holds what the interpreter's fetches left and native
code sets nothing. The queue's tail is kept inside the queue
(`--fill 7E005F-7E005F=00-70`), for the same reason at the other end: a tail of
`$FF` puts records at `$7E:015F`, on the stack. And the scroll's step is kept
small (`--fill 7E0001-7E0001=00-08`, and again `F8-FF` for the other
direction), because the step is a count of tiles to move and each tile can
queue a strip, so a step of `$80` walks the buffer's cursor `$2000` bytes on and
round the bank into low RAM -- which the object scripts never ask for, since
the step is the whole part of one frame's move. With those three, 20000 states
each on two seeds for the two scrolls (each direction separately), the block
upload and the map centring, and 5000 for the redraw, none mismatching; and
5000 each of the plane number the routines refuse, which no movie reaches
either.

**The plane number is checked and then spun on.** Both scroll routines and the
redraw take the plane in the mode's first scratch byte and answer a number that
isn't 1, 2 or 3 with a branch to itself (`$C2:059B`, `$C2:069F`, `$C2:0A6A`).
The ports hand those addresses back to the interpreter rather than reproduce
the loop, declaring their own empty set there (`m_stop_at_own`), since the
accumulator and flags the original has at that point are exact and cheap to
keep. The redraw's stop is under the data bank byte it has already pushed,
which is the one piece of SNES mechanics its port keeps.

**What was left at the edge, and why.** The chunk is 1.4 KiB against the 6 KiB
a wave-2 chunk aims at, and the next coherent unit -- the screen's own setup,
`$C2:2720`-`$C2:2D90`, which the burndown puts at `$C2:27DE` (1385 bytes) --
was not taken. Its shape is plain: about fifteen routines that each decompress
one stream into work RAM, picked by a byte of the scene record at `$C6:FD00`
that `$C2:274D` finds from the location (`$1F0`-`$1F7`, the world maps), out of
ten tables of long pointers at `$C6:FDF0`-`$C6:FFFF` that `re/hints.toml`
already types as `lz`. What is missing is what each stream *is*: two of the
fifteen are nameable from this chunk's work (`$C2:295E` loads the first plane's
tile groups to `$7E:3000`, `$C2:298C` its map to `$7E:4000`) and one from MD4
(`$C2:2A16` loads the scene's object scripts to `$7F:0400`), and the rest would
take their names from their destination addresses, which is the transcription
M5 exists to stop. Identifying them is resource typing (MATCHING.md's MD2), not
porting, so that run is worth taking after it rather than before. The two
drivers at the end of it (`$C2:2C1D` and `$C2:2C93`) also run straight through
two entries that already exist in `menu/display.c` (`$C2:2D47`, `$C2:2D70`), so
they belong with that file's loader half, which is its own chunk.

### Phase 3a: the scene subsystem off the machine

The first whole subsystem of PLAN.md's Phase 3a, after the circle pilot
(`game/scene/circle.c`): the other ten files of `game/scene/`, 73 entries, taken
together so the vocabulary they share was written once (75 registered
entries in all, the circle's two among them). Nothing was added to
the burndown; what moved is portstyle's two numbers. Before, scene scored 0.20
operations per line with five files on the transcription backlog (`program.c`
0.52, `race.c` 0.34, `display.c` 0.30, `nmi.c` 0.18, `boot.c` 0.17), and 196 of
its 215 functions took a `Snes *`. After, it scores 0.05, no file is on the
backlog, and 143 of 254 functions take the machine: the 74 functions the 75
entries run, 42 guards, and 27 functions that are either shared pieces of the entry
wrappers (setting the widths, the flags a compare leaves, the page the caller
is on) or bridges around call-outs, all listed below. (`portstyle.py`'s own
column says 90% to 54%; its function matcher misses one-line definitions, so
those are counted here by name.)

**The shape.** Each routine's work is a static function of `GameState *` and,
where it touches the host, `Platform *` -- the two handles apart, as decided
before the rewrite began -- with plain C parameters and results. The entry
wrapper turns the machine into those (`state_of`, `m_platform`), reads the
registers the routine takes, calls it, and puts back only what liveness says
the callers read. That last part is what made most wrappers thin: every entry
now declares the largest don't-care set `tools/dontcare_check.py` accepts, which
for the program's display and race entries is everything but a register or two
(the RAM program's callers are in the analysis since the M3 image work), so the
`LDA #$80 : LDX #$0400 ...` register traffic the display ports reproduced to be
compared is gone rather than moved. `scene_setup_mode7_display`, which
reproduced every load the original made on the way to each store, now leaves
A's high byte and the M flag; the eight race stops leave nothing.

**What the accessors couldn't say, and what was added.**

- *Cells on the caller's page.* The multiply and divide helpers work on
  `$F0-$FF` of whichever page D is on: the scene's own, or a sequence channel's
  when a script calls the program (5412 of the race's 19842 multiplies). The map
  has no accessor shape for a variable on a caller-chosen page, so
  `scene_math_scratch` became a 16-byte buffer and `scene/scratch.c` takes the
  page (`ScenePage`: a channel, or the scene's own) and an offset, reaching the
  bytes through `scene_math_scratch_byte` or `scene_channels_math_scratch_byte`.
  The first guard written for these (`D = 0`) turned those 5412 calls away,
  which is how it was found: a guard's refusals show only as a drop in the
  entry's shadow count, so every rewritten entry's count was compared with the
  predecessor's, movie by movie. The map has that shape now, and the hand
  mapping is gone ("Page kinds", below).
- *The multiplier, divider and work RAM port.* These are CPU registers, not a
  platform service (PLATFORM.md, "Not services"), and not variables either;
  `game/cpu_io.h` names them as the game drives them (`hw_set_multiplicand`,
  `hw_multiply_by`, `hw_quotient`, `hw_divide_by`, `hw_port_seek`,
  `hw_port_read`, `hw_port_write`), taking the state because an operand or a
  cursor left for the next routine is state, with `runtime/port/state_io.c` the
  backing. Every subsystem's arithmetic has the same need (`battle/math.c`,
  `menu/epoch.c`).
- *ROM data from logic that has no machine.* `scene/scene_data.h`'s accessors
  took the machine. They take the state now, through `game/rom.h`
  (`rom_read8`/`rom_read16`, backed in `state_io.c`), which only a `*_data.h`
  accessor is meant to call; the menu's two stream-table readers keep the
  machine until `menu/object_commands.c` is rewritten.
- *Buffers the game locates at run time.* 18 new uses of `game/wram.h`, 22 in
  the subsystem: the scaler's source and output rows (the data bank says where,
  `$7F` in the race), the bank `$FE` copies' destination, the tile source
  `$7E:5E54` reads through the data bank, the next step of a task whose page is
  the caller's (`$7E:5DA7`), the race's line-effect descriptor table, the
  sequence scripts' bytes and their store command's target, and the sprites'
  four-byte sources. Each is data a caller points at, not a cell; the map could
  name the script area and the race's descriptor table as fixed buffers of the
  program image, which is the next step for most of them.
- *Byte offsets into a record.* The sprite writers index the OAM buffer by a
  byte offset and a high-table pointer, not a sprite number, and fuzzing puts
  odd offsets there; the core's `oam_buffer` record can't take them, so the map
  has `scene_sprite_table` and `scene_sprite_high_table` over the same bytes
  (`$0700-$08FF`, `$0900-$091F`) for the scene.

**The guards.** A port that names its cells through `game/vars.h` knows them
where the scene keeps them, while the original reaches them through D and the
data bank; so every such entry now says, in its guard, that it runs on the
page and bank the game always enters it with (`scene/guards.c`, and the
predicates `m_bank_reaches_low_ram`, `m_bank_reaches_io` and `m_direct_page`
added to `runtime/port/machine.h` for every subsystem's page guards). With the
movies' per-entry counts compared against the predecessor's, none turns a call
away except the one noted below.

**What shadow mode found**, over the attract sequence and the race while the
files were rewritten, before the library: the task lists' call-outs push P after the load that finds
the task (so its N and Z are the task word's), and carry the loop's compare's
carry from one task into the next (both only when a task runs, which the fuzz
constraints that empty the tables never let happen); and `scene_frame_work`'s
callers read A's high byte, which the 16-bit task list leaves as the frame work
set it before the call. Then the full library, clean.

**Differential fuzzing, and a tool for it.** Every rewritten entry was fuzzed
against the port it replaced with the same state, seed and constraints
(PORTING.md, "Fuzzing"): `tools/fuzz_compare.py` runs two `ct_fuzz` builds side
by side and reports both counts per routine, from constraints kept in
`tools/fuzz_routines.toml` (one entry per registered routine, with the reason
for each constraint; the ones PORTING.md's rows recorded for the scene's
entries, made exact). A routine can also be listed with `variant = "loose"`,
fewer constraints, which is where a reordered access shows: the sprite writers'
loose runs, with the entry offset and the high-table pointer anywhere in low RAM
a list can't run past, caught the first form of the rewrite -- the named table
wrote to bank `$7E` where the original, through a data bank of 0, reaches I/O
space past `$2000` (19569 of 20000 states against 22, until the cursors were
kept in low RAM, where both agree). Every entry's predecessor and rewrite counts
are in its PORTING.md row; all agree.

**What it cost.** Recalibrating the scene's `cost_milli` rows: a port that
reads a byte cell as the word the accessor has, or writes a result once
instead of a step at a time, makes a different number of bus accesses.
Sixteen of the scene's rows moved. The arithmetic moved most, because it
now keeps its rounds in C variables and touches the scratch cells once at
each end: `scene_divide` 2053 to 140438 (32 rounds of shifts and subtracts in
about 40 bus accesses), `scene_divide_8_8` 2372 to 32485, `scene_multiply` 2866
to 6178, `scene_multiply_signed` 3813 to 5708, and `scene_write_matrix_code`
4467 to 19279, whose 5153 port writes are now nearly all its accesses. The
rest moved by a few hundred where the wrappers stopped reproducing dead
registers or an accessor reads a word where the original read a byte
(`scene_frame_work` 3393 to 3006, the display and race rows, the sprite writers,
the task lists). Nothing needed an exception: one scalar fits each of them, since
every one of these does the same work on every call or scales it with the same
loop the original has. The whole suite over the library, with the shadow logs
of the first run calibrated from, then passes; and the per-entry shadow totals
over the 26 movies -- calls, compared, interrupted, skipped -- are identical to
the predecessor's, entry for entry.

**Left on the machine, and why** (the 27): the bridges around call-outs --
the frame work and the NMI (`run_frame_work`, `read_pause_button`,
`update_song`, `run_task_list`, `send_pause_sound`, `can_continue`), the task
lists (`run_tasks`, `call_task`: the tasks are the program's code), and the
sequence interpreter's loop (`run`, `call_program`, `send_sound`, `past_call`,
`machine_as_expected`, `can_continue`), whose commands are logic but whose
program calls and driver sends are the original's; the pieces the entry
wrappers share (`set_m8`, `load_status`, `set_widths16`, `put_flags`,
`compare8`, `caller_page`, `copy_entry`, the two files' `set_vram_address`,
`upload_column_entry`); two guards that other guards call; and the pilot's
`scene_circle_draw`, the C call `menu/object_commands.c` makes into the circle
while that port is on the machine.

**One call the guards can leave to the original.** `scene_find_divisor` reads
its limit through the data bank; in one iteration's run of the race movie one
call came with a data bank that doesn't reach low RAM, where that cell isn't
the scene's, and its guard left it to the original (the suite's runs, and the
predecessor's, had none).

### Page kinds: cells on a page the caller chooses

The accessor shape scene's rewrite found missing, built before `event`'s
(PLAN.md, "Phase 3a"). A page in `re/ram.toml` either has a fixed `d` or, as a
page kind, lists the `instances` its code runs on -- named pages, arrays of
pages, or both -- and the variables on it take which instance a port means
(DISASSEMBLY.md, "The accessors"). `tools/ram_map.py accessors` writes the
handle types into `game/vars.h`, the per-instance addresses into
`runtime/port/vars_ram.c`, and `runtime/port/vars_pages.h`, a
`<page>_page_of(d, &handle)` per kind for the entry wrappers, which refuses a D
that is no declared instance and counts it.

**The measurement.** `ct_dev --page-report` counts, per hand port, the D each
call was made with, before the guard; `tools/suite.py` passes it, so every gate
run's shadow logs hold the evidence. Over the 26 movies:

| Page | Routine | D at entry (calls) |
|---|---|---|
| `scene_math` | `scene_multiply` `$7E:31BF` | `$0000` 146235, `$0A00` 23842, `$0B00` 1920 |
| | `scene_divide` `$7E:339F` | `$0000` 20115 |
| | `scene_divide_8_8` `$7E:32E2` | `$0000` 16225 |
| | `scene_multiply_signed` `$C3:094C` | `$0000` 21120 |
| | `scene_run_sequence` `$C3:0AFF` | `$0A00` 61680, `$0B00` 18316 |
| | `scene_script_wait_sound_port2` `$C3:0C2A` | `$0A00` 5041, `$0B00` 102 |
| `menu_record` | `menu_draw_record_sprites` `$C2:F871` | `$1800` 3744, `$1840` 3497, `$1880` 3545, `$18C0`-`$1980` 1868 each, `$19C0` 1259 |
| (input) | `input_read_joypad` `$C2:84EC` | `$0000` 453397, `$0100` 195982, `$0D00` 35169, `$1E00` 1596, `$2100` 246, `$1D00` 173, `$0300` 102, `$0500` 1 |
| | `input_map_buttons` `$C2:8545` | `$0000` 651497 |

So `scene_math` is the scene's own page and the two channel pages, which is
also every page the code allows (the attract program has two channels,
`scene_channels`); `$0B00` is reached only in `endings-beyond-time` (multiplies
and sequences) and the race (sequences). The divides and the signed
multiply are only ever called on `$0000`, but their guards accept either
channel, as the predecessor's did. `menu_record` is all eight records of the
table at `$1800`, every one reached (all eight in `menus-end-of-time`).

**The controller cells are one set of bytes.** The map had them on a `caller`
page with no D, from `input_read_joypad`'s entries on five different pages.
Measured, it is entered with eight -- and it sets D = `$0000` itself
(`PEA #$0000 : PLD` at `$C2:84EC`), and `input_map_buttons`, which works on the
caller's page, runs only from `input_update` after it, on `$0000` in all 651497
calls. The event engine's input conditions read the same cells at `$00F2` and
`$00F8` through the data bank. They are on the fixed page `input`, D =
`$0000`, and their accessors are ordinary ones (`input_held(g)`); the menus'
epoch screens and `core/input.c` keep `game/ram.h`'s form until their rewrites.

**Scene's scratch on the generated shape.** `scene_math_scratch` is the
`scene_math` kind's 16 bytes at `$F0`; `scene_channels` lost its `math_scratch`
field, which named the same bytes on the channel pages. `scene/scratch.c` keeps
only the widths the helpers use (8, 16 and 32 bits, byte by byte, low first, as
before), and its hand mapping from D to a page -- `scene_page_of`, `ScenePage`
and the dispatch between two accessors -- is deleted: the guards call
`scene_math_page_of`, the sequence interpreter takes its channel from the
handle's index, and the ports name the scene's own page `scene_math_scene()`
and a channel's `scene_math_channel(ch)`. The per-entry shadow totals over the
26 movies (calls, matched, mismatched, interrupted, skipped) are identical to
`main`'s for every hand port, not only the scene's, and no run printed a `page
refused` line. `tools/fuzz_compare.py`, predecessor against rewrite on seed 1,
with channel-page variants added to `tools/fuzz_routines.toml` for the
arithmetic and the interpreter and a variant on a page that is no instance:

| Routine | States | Mismatched before | after |
|---|--:|--:|--:|
| `scene_multiply` | 20000 | 0 | 0 |
| `scene_multiply` (channel, D = `$0B00`) | 20000 | 0 | 0 |
| `scene_multiply` (other page, D = `$0A80`) | 20000 | all refused by the guard | all refused by the guard |
| `scene_multiply_signed` | 20000 | 0 | 0 |
| `scene_multiply_signed` (channel) | 20000 | 0 | 0 |
| `scene_divide` | 20000 | 0 | 0 |
| `scene_divide` (channel) | 20000 | 0 | 0 |
| `scene_divide_8_8` | 20000 | 0 | 0 |
| `scene_divide_8_8` (channel) | 20000 | 0 | 0 |
| `scene_find_divisor` | 20000 | 0 | 0 |
| `scene_write_matrix_code` | 20000 | 0 | 0 |
| `scene_transfer` | 200 | 0 | 0 |
| `scene_race_begin_tile_uploads` | 20000 | 0 | 0 |
| `scene_run_sequence` | 20000 | 0 of 14061 compared | 0 of 14061 |
| `scene_run_sequence` (channel 1) | 20000 | 0 of 14049 compared | 0 of 14049 |
| `scene_script_wait_sound_port2` | 20000 | 0 | 0 |

The accessors read and write the same bytes in the same order as the hand
mapping did, so no `cost_milli` moved.

**Refusals are visible.** Each `<page>_page_of` counts the D values it turns
away; `ct_dev` prints `page refused: <page> <count> (last D $XXXX)` for a kind
that refused any, and `tools/suite.py` fails a movie whose log has one. That is
the check the scene rewrite did by hand, comparing every entry's shadow count
with its predecessor's movie by movie, after a guard that assumed one page
turned away 5412 of the race's multiplies. `tests/port/vars_test.c` checks the
mapping and the accessors on a machine with no ROM.

**What it unblocks.** `event`: the event engine and its objects are on the
fixed field page (`$0100`), not a page kind, so the only cells it needed were
the input conditions' (`event/input.c`), which now have accessors. `menu`: the
record page, for `objects.c`'s record sprites (whose wrapper's
`menu_record_page_of` gives the record). `core`: `input.c`'s reader and mapper.

### Phase 3a: the event subsystem off the machine

The second subsystem of PLAN.md's Phase 3a: all of `game/event/`, the event
script's engine (2 entries) and its 185 command handlers, 15 files that were
all on the transcription backlog. Before, event scored 0.46 operations per line,
the worst subsystem in the tree, and all 251 of its functions took a `Snes *`;
its shared vocabulary (`script.h`) was one of instructions -- `iny_tyx_sec_rts`,
`ldx_current_object`, `var_offset_here` -- not of the script language. After,
it scores 0.02 and no file is on the backlog. Counted by name (portstyle's
matcher misses one-line definitions and the macro-generated entries, so its
own column says 29%), 255 of its 507 functions take the machine: the 187
entries (149 generated by one macro, 36 hand-written bridges and the engine's
two), 15 guards, the 8 functions of the shared translation, and 44 bridge
helpers around call-outs, 29 of them in `movement.c` (below).

**The protocol, designed first.** Every handler has the same calling
convention in the original -- entered through the table at `$C0:5D6E` with the
command's offset in Y, returning X = where the script goes on and the carry
set to keep running the object's turn -- so it was written once, as C types,
before any handler (`event/script.h`):

- `EventCommand` is where the command is; operands are read by their position
  in it (`operand(g, c, 1)`, `operand_word`), objects, party members and
  variables through helpers that say what the operand names
  (`object_operand`, `member_object`, `variable_operand`, `var8`/`var16`).
  Script offsets are 16-bit and wrap, as the index registers do.
- `EventStep` is what the interpreter does next: `next_command(c, length,
  ...)`, `end_turn`, `wait_here`, `jump_from` (every jump in the language is
  relative to its jump byte, and a jump keeps its distance in the scratch cell
  as the original does), `branch_unless` for the conditions.
- Every handler is then `EventStep handler(GameState *, const EventCommand *)`,
  named for what the command does, and `EVENT_COMMAND(entry, handler)` makes
  its registered entry: one shared translation (`event/bridge.c`,
  `event_command_run`) reads Y, runs the handler and returns as the original
  does, so `game/registry.c` still gets one function per entry and none of them
  is written by hand. The two handlers that read the platform (the sound
  driver's status) use `EVENT_PLATFORM_COMMAND`.

**Where the protocol didn't fit, and what it carries for it.** The convention
turned out not to be uniform in what callers can read, and liveness settles
what the step has to carry:

- *Y.* The original's Y at return -- where the command's parse of its
  operands stopped, which differs from command to command -- is live: the
  map-setup run of object 0 (`$C0:595C`) returns to callers that pass it to the
  menus' dispatcher at `$C2:8C36`, which stores it at `$04C0`. So `EventStep`
  carries it (`cursor`), each constructor taking where the parse stopped; a few
  commands leave something else there (`$E6` the speed it set, `$7B` the
  object, the bridges what their callee left), and the comparisons' operator
  call runs with 8-bit index registers, which clears its high byte -- shadow
  mode found that one on its first run.
- *V.* The overflow of the commands' additions (the jumps' offset
  arithmetic, the variable arithmetic, the calls' saved-position index, the
  block copy, the on-screen test's subtractions) is live too, conservatively:
  the field loop carries it into code it leaves through JMLs. So the step
  carries it (`overflow`, or kept), computed from the operands.
- *A.* A's high byte looked live after every handler, which would have kept A
  in the protocol. It isn't: the dispatch widens the next command byte with
  `AND #$00FF`, which the liveness analysis took for a read of both bytes. The
  analysis now knows a 16-bit `AND #$00xx` (or `ORA #$FFxx`) sets the high
  byte whatever it held (`tools/disasm/liveness.py`, with a test), and every
  handler entry declares `DC_A | DC_N | DC_Z`. The change only makes the
  analysis more precise: nothing else in `game/` needed a change, and several
  field entries could now add `DC_A` too (not done here).
- *The carry, in one handler.* `$D6` (take a character out of the party)
  leaves the carry of its last comparison, or the one it was entered with when
  it compared none; its entry is a two-line bridge that reads that from the
  machine.

Both are a trace of the original's engine, not of what a command means: a
build without that engine drops `cursor` and `overflow` and keeps `next` and
`go_on`.

**The vocabulary** that replaced `script.h`'s instructions: the protocol
above, `current_object`, `slot_of`, `set_var8`, `memory8`/`memory16`, the
scratch cells as `keep8`/`keep16` (they are still written as the original
writes them), and in the files the commands' own -- `start_mode`,
`end_timed_play`, `priority_from_map`, `place_on_tile`, `callable`,
`preempt`, `queue`, `saved_position_index`, `take_latched`, `start_dialog`,
`take_channels`, `face_the_way`, `start_walking`, `tile_arrival`. ROM reads
go through `event/event_data.h` (the bit masks, the random bytes, the facing
for a direction, the command table), backed by `game/rom.h`.
`tools/disasm/eventscript.py` gives 36 commands the names their ports
established (`if_a_held`, `if_b_pressed`, `set_player_control`,
`set_speed`, `set_solidity`, `set_blocking`, `make_object_uncallable`,
`get_party_leader`, `set_result_from_variable`, `store_location`, ...), and
the entries carry them; the `$16` dispatch's derivation follows the rename.

**What the accessors couldn't say, and what was added.** The map gained 20
entries, and four fields of the location triples, for what the commands touch: the script buffer as a
buffer (`event_script`, to the end of bank `$7F`, since offsets reach it), the
scratch cells (`event_scratch`, `$D9-$E4`), the pointers `$48`-`$4D` keep, the
saved position kept across call-outs, the comparisons' resume offset, the
player-control flag (`$1F`: Start pauses the field only while it is set), the
animation update's scanline limit (`$6B`), the choice lines of a dialog, the
tile copy `$E4`/`$E5` describe, a stored location, the object bytes `$0F01`
and `$1001`, and the flags the event hands the battle (`$2A1F`, bit 6 keeping
the song); `field_direction` and `field_target_object` became words, as the
commands write them, and the location triples gained their fields. The
multiplier and the divider are driven through `game/cpu_io.h`, which gained
`hw_set_dividend`. What the map can't name is the memory commands' 24-bit
pointers and `$4E`'s block copy, 8 uses of `game/wram.h` in `memory.c` (event
had none before, because it had no accessors at all): those are buffers the
script locates, anywhere on the bus.

**The guards, and a hazard they found.** The handlers name the object tables
by slot while the original indexes them with the object doubled through the
data bank, so every entry is guarded (`event_guard`): D on the field page, the
data bank `$00` or `$80` (whose upper half is the `$C0` tables the handlers
read), the running object an even offset below `$100`, the command not in the
script's last 16 bytes (whose operands would reach the I/O register past bank
`$7F`), and per command what
its operands must be -- an object operand even, a party member's object even
(or none, for the calls that check), a comparison's operator in its table, a
bit number whose mask stays in its bank, a dialog string table, a return that
finds a level before the search comes round (`$00` never stops otherwise).
These are the first guards that read work RAM, and the first run found that
they cost time: a guard runs between the original's instructions, and its bus
reads landed in shadow mode's record of the original's run, so the generated
blocks checked against that record were 8 or 16 clocks slow (16209 block
mismatches over three movies, none of them a port's). Guards now look through
`vars_peek` (`runtime/port/vars_ram.h`): the generated accessors read with
`snes_bus_peek` while it is on, taking no time and leaving the bus as it was.
After a call-out, each bridge goes on only if the callee left the machine as
the command runs (widths, D, the data bank: `event_machine_back`) and
otherwise stops at the call's return, declared (`m_stop_at_own`), where the
predecessor returned without declaring it -- which fuzzing counted against it
(below).

**The engine** is a C loop over the protocol with the dispatch as its one
bridge: a turn runs commands until one ends it or the budget is spent, and a
command is run through the original's table with the registers the original
has there (the doubled command byte in A and X, the carry the 16-bit shift
clears), since most handlers are ports registered at their entries and the
rest are still the original's code. It hands a turn back to the original when
a handler leaves it where the port's accessors wouldn't reach what the
original's do: another data bank, an odd running object, or the last script
offset, which reads the first byte past bank `$7F`, an I/O register holding
whatever was last on the bus.

**Movement.** The motion is the field's (`field_direction_to_target`,
`field_object_in_the_way`, `field_start_step`, the walking and standing
animations, the line-up routines), still called out to, so the move commands
are bridges: the decisions (where the target is, whether the object is next
to it, on its tile, lined up) are plain C, and the calls are made with what
the original has at each. Two things made that harder than it looks. The
carry: `field_object_in_the_way` and the line-up routines answer in it, and
`field_direction_to_target` passes on the one it is called with, so declaring
the carry dead at those calls let shadow mode hand the port its own stale
carry back (a declared-dead flag the callee leaves as it found it is the
port's again, as ON mode would have it); the port now computes the carry the
original's tile comparisons leave and calls with it. And the call sites: the
don't-care check reads a call-out's site from a helper's parameters, so the
move-to-tile commands' sites are table rows, one table for the moves that
turn and one for those that keep their facing, rather than offsets from a
base.

**What shadow mode found**, beyond the parse position's high byte: the
carry above (the move-to-tile commands in the Blackbird movie called the
standing animation where the original walked on), and the guards' time. Then
the full library, 26 movies, clean; and the per-entry shadow totals (calls,
matched, mismatched, interrupted, skipped, abandoned) are identical to the
predecessor's for all 798 hand ports, event's 187 among them, so no guard
refuses a call the game makes.

**Differential fuzzing.** Every entry is in `tools/fuzz_routines.toml` twice,
on a field state (`field.ctm`, frame 11400): tightly, with the field's page
and data bank, the running object a slot and the script offset below `$8000`;
and loosely, with the offset anywhere, so operands past `$DFFF` are read
through bank `$80`'s mirror of low RAM and can land on the cells a command
writes. `tools/fuzz_compare.py` takes an entry by address now (`at`), since the
rewrite renamed 36 of them. Seed 1, 20000 states each (2000 for the map setup),
summed by file:

| Files | Variant | Entries | States | Mismatched before | of | Mismatched after | of | Turned away by the guards |
|---|---|--:|--:|--:|--:|--:|--:|--:|
| `animation.c` | tight, loose | 7 | 280000 | 0 | 280000 | 0 | 139804 | 140196 |
| `arithmetic.c` | tight, loose | 24 | 960000 | 0 | 960000 | 0 | 473607 | 486393 |
| `calls.c` | tight, loose | 14 | 560000 | 0 | 560000 | 0 | 181234 | 378766 |
| `dialog.c` | tight, loose | 7 | 280000 | 0 | 280000 | 0 | 139372 | 140628 |
| `engine.c` | tight | 2 | 22000 | 0 | 13510 | 0 | 6769 | 10074 |
| `facing.c` | tight, loose | 12 | 480000 | 0 | 480000 | 0 | 175696 | 304304 |
| `flow.c` | tight, loose | 19 | 760000 | 0 | 760000 | 0 | 265897 | 494103 |
| `input.c` | tight, loose | 17 | 680000 | 0 | 680000 | 0 | 339524 | 340476 |
| `locations.c` | tight, loose | 10 | 400000 | 0 | 400000 | 0 | 188123 | 211877 |
| `memory.c` | tight, loose | 16 | 640000 | 0 | 640000 | 0 | 319552 | 320448 |
| `movement.c` | tight, loose | 20 | 800000 | 4 | 799880 | 0 | 285846 | 514096 |
| `properties.c` | tight, loose | 13 | 520000 | 0 | 520000 | 0 | 239988 | 280012 |
| `screen.c` | tight, loose | 13 | 520000 | 2 | 500279 | 0 | 249732 | 260364 |
| `sound.c` | tight, loose | 6 | 240000 | 1 | 179889 | 0 | 89854 | 120168 |
| `variables.c` | tight, loose | 7 | 280000 | 0 | 280000 | 0 | 90460 | 189540 |
| **all** | | 187 | 7422000 | 7 | 7333558 | 0 | 3185458 | 4191445 |

The rewrite mismatches nowhere. The predecessor's seven are its own: in
`$A0` and `$A1` (and `$9E`/`$9F` on seed 4) a callee that came back with other
widths, where it returned without declaring the stop the fuzzer then counts;
in the loose `$EC`, `$E4` and `$E5` operands read through the low RAM mirror
after the command had written the cells they land on, which the predecessor
read in another order. Getting there found, and the rewrite fixed:

- the comparisons took their operator's bit 7, which the original's 8-bit
  doubling drops;
- `$4E`'s block copy onto the stack changed the data bank the original pulls
  back (the port now keeps it on the stack across the copy too);
- the engine stored a turn's end through the data bank a handler left, and
  read the command byte past the end of the script: both handed back to the
  original now;
- the loose runs' reorderings: the rewrite read a command's operands before
  writing the scratch cells and pointers the original writes between them
  (and C's unspecified argument order hid one), so an operand read through
  the mirror saw the old byte. The handlers now read and keep in the
  original's order: the pointers a byte at a time, the tile and location
  commands an operand after each store;
- a hazard in `ct_fuzz` itself: it asked the guard before pushing the return
  address, so a guard reading an operand on the stack saw the byte the call
  then overwrote. It now asks it as the runtime does (PORTING.md, "Fuzzing").

The guards turn away about half of every run -- the odd running objects,
which the game never makes -- and more for the comparisons (the operator)
and the calls; those are counted in the skipped column, and in them the
original runs. Every run is fast: the whole comparison, both builds and both
variants, took under half an hour at two processes.


**What it cost.** Recalibrating event's `cost_milli` rows (`timing_gate.py
--calibrate`, which now reads a table macro with arguments after the cost, as
`EVENT_CMD`'s guard is): 73 of event's 187 rows moved, the rest being
unreached or unchanged. The rewrite reads bytes as the words the accessors
have and writes an 8-bit variable back as its word, which makes more bus
accesses and a lower cost per access (`var_add` 5969 to 4341, `move_to_tile`
3117 to 2560, the engine's per-frame turn 4150 to 3256, the map setup 4476 to
4173), and makes no stack or register traffic, which makes fewer (`call` 3906
to 4599). Nothing needed an exception: the gate's other limits hold for
every event entry at the calibrated values.

**Left on the machine, and why** (the 44 bridge helpers): the move
commands' calls into the field's motion (29 in `movement.c`), the sound
commands' driver calls, which check what the driver left before going on,
the party routines at `$C2:8004`, `$E4`'s tile copy, `$A8`/`$A9`'s facing,
`$AF`'s call into `$B0`, the engine's dispatch, `$E1`'s hand-off to the
original's location change, and each file's `back`. `field` is next and owns
all the motion routines those bridges call; once they are ports, the move
commands' decisions can call them as C and most of `movement.c` leaves the
machine.

### Phase 3a: the event language, and its scaffolding

A review of the rewrite above found it scoring 0.02 and still reading as the
ROM: one function per opcode (`loop_animation`, `loop_animation_0`,
`loop_animation_1`), operands read by position (`operand(g, c, 1)`), an engine
that found its handlers through the ROM's pointer table and an emulated `JSR`,
and emulation carried through every result -- a parse position as the third
number of all ~136 `next_command(c, 3, 2)`s, the V flag in `with_overflow`,
and 84 writes to scratch cells named for their addresses (`SCRATCH_D9`),
"because other code may read them". `portstyle.py` counts operations on
emulated registers, which none of these are. This change settled what reads
each of them, separated what nothing but the gate reads, and reshaped the
subsystem around a language table.

**The evidence.** `ct_dev --event-reads` (dev/event_reads.h, off by default)
follows what a command leaves beyond X and the carry, on the interpreter
(`--native off --recomp off`, so everything that runs is the original's): Y
and V from the handler's return until something overwrites them, through
pushes and pulls, and into memory when Y is stored; and every byte of the
field page's event cells (`$C1-$E4`) a command writes, until code outside a
command writes it again, each read classed by who makes it (the writing
routine, a routine it calls or that called it, a later command, code outside
any command, an interrupt). The call stack is followed from the calls and the
stack pointer; interrupt handlers are skipped for Y and V (they save and
restore what they touch). It needed one hook the bus didn't have, a work-RAM
observer beside the I/O one (`snes_bus_wram_observer`). Over the 26 movies,
1,546,991 commands:

- *Y* is read in one place: `STY $C0` in the menus' dispatcher (`$C2:8C36`),
  after the openings at map setup (99 times, after 20 different opcodes; 60
  following the port's engine at `$C0:5910`, 39 following `$C0:595C`, still
  the original's). It stores the low byte at `$04C0`, which nothing in the
  library reads afterwards. After the per-frame turns (`$C0:5AB1`) and
  `$C0:597D` nothing reads it at all.
- *V* is never tested after a command. Its only read is a `PLA` at
  `$C7:0A90`, the sound driver's error path discarding a pushed P, 69 times.
  Liveness keeps it live (the field loop leaves through JMLs it can't follow).
- *The event cells* are read by the command that wrote them -- its handler,
  or its own subroutines (the comparisons' operator routines at
  `$C0:6487-$64C1`, the pointer helpers `$C0:67A2`/`$C0:67E3`, each part of
  one C operation now) -- and by nothing else, with two exceptions: `$02`
  reads the high byte of `$DD` a `$96` left, and masks it away; and the
  menus' wave handler at `$C2:77C7` (`menus-end-of-time`, 182 reads) advances
  `$01E2` as a phase it never sets, and so starts from what `$02` or `$B5`
  last left there. That is a reader without a reason: a native menus port
  must give its phase a start (TODO.md).

**The classification**, then: none of the three has a meaning to name. The
parse position, the V flag, the scratch cells, the comparisons' resume offset
(`event_condition_resume`), the pointers `$48`-`$4D` keep, and the saved
position the commands keep, are all scaffolding: kept while the gate is
byte-exact, and marked. What stays real: `$E4`'s saved position, which the
tile copy it calls can overwrite; the turn's budget, which commands the
original runs can spend; and `$D6`'s go-on flag, which is the original's
carry but is what the engine goes on by.

**The mechanism** (`game/original_effects.h`, PORTING.md "Scaffolding"):
`ORIGINAL_EFFECT(expr)`, which the default build evaluates and a build with
`CT_WITHOUT_ORIGINAL_EFFECTS` compiles without evaluating.
`ct_game_native_shape` builds every port that way in every build and in CI.
Event's vocabulary for it is `event/leaves.h`: the record of the Y and V a
command leaves, which whoever runs it owns, and the scratch cells by the order
the original fills them (`KEPT_FIRST` ...), each file saying what its commands
keep there. What is the same every time a command runs -- the parse position,
the operands kept in the first cells, which additions leave their V -- is a
table of its own, `event/leaves.c`, by opcode, beside the language's and not
in it; the build without the original's effects compiles it out, and the
engine and the entries reproduce it around the operation, which never
mentions it. Where it varies with a path it is one marked call on that path
(`$06`'s absent member leaves its parse on the operand; `$E6` leaves the speed
in Y). Arithmetic computes `variable + amount`; the V it leaves is derived
after the operation from the amount and the variable as it is then, not by
ALU helpers in the operations. The multiplier and divider the calls, the
return and the jumps drive are the same: the operations compute the product,
and the hardware is left holding it as scaffolding. A service call-out whose
callee doesn't read some registers now says so, `PF_CALL_DC(site, target,
set)` (`runtime/platform/platform.h`), which `tools/dontcare_check.py` checks
like `m_jsr_dc`: the sound commands declare what the driver doesn't read
instead of loading A, X and the flags for it. `tools/portstyle.py` reports
the count: event had 0 marked before (and 59 uses of names taken from
addresses, 45 of them `SCRATCH_D9`), 75 marked after (and 10 such
names, all `field_setting_1e` and the menus' routine `$0A`, whose meanings
aren't established; TODO.md).

**The language table.** Each file ends with its rows: opcode, operation, how
its operands decode into the parameters the operation reads, by name
(`.animation = OPERAND(1)`, `.animation = CONSTANT(0)`, `.value =
VARIABLE_WORD(1)`, `.target = MEMBER_OBJECT(1)`, `.flags = BIT(1)`,
`.object = SELF` ...), length, jump operand, the guard's extra check, and the
handler's address, which names the command's time and its registered entry:

    { 0xAA, loop_animation, .animation = OPERAND(1), .length = 2, .routine = 0xC02E67 },
    { 0xB3, loop_animation, .animation = CONSTANT(0), .length = 1, .routine = 0xC02E87 },

The parameters are one list in `script.h` (`EVENT_PARAMETERS`: each a type and
a name -- `EventVariable variable`, `Width width`, `EventObject target`,
`WindowPlace window`, `LocationRecord record` ...), from which the row's
fields, the decoded `EventCommand` and the engine's decoding and checking
loops are generated. An operation takes the command and reads what it needs
by name (`c->animation`, `c->self`), with no adapter between the row and it,
and says what happens next (`next_command()`, `end_turn()`, `stay()`,
`take_jump()`, `go_to(at)`); the engine does the jumping. A command that
still calls out to the original's code is a row too, whose operation takes the
machine (`.bridge`), and its operands are named in its row the same way: a
bridge reads them decoded by its row (`event_command_of(s, 0x94).target`),
never by offset. The guard's extra checks get the decoded command too
(`compares_bytes` reads `c->comparison`). The table is where every ported
command is, 185 rows. Opcodes that differ only in how an operand is encoded
became one operation each:

| Family | Opcodes | Operations |
|---|---|--:|
| loop an animation (given, 0, 1) | `$AA $B3 $B4` | 1 |
| play a one-off (2 frames, n frames) | `$AB $B7` | 1 |
| assign a variable (number, variable, story, 1, 0; byte, word) | `$4F-$55 $75-$77` | 1 |
| add, subtract (number, variable, one; byte, word) | `$5B $5D-$61 $71-$73` | 2 |
| set, keep, toggle flags (bit number or mask) | `$63 $64 $67 $69 $6B` | 3 |
| record in the story (number, variable; byte, word; progress) | `$56 $58-$5A` | 1 |
| raise, lower a story flag | `$65 $66` | 1 |
| peek, poke (byte, word; number, variable) | `$48-$4D` | 2 |
| compare (variable with number or variable; byte, word) | `$12-$15` | 1 |
| wait (n, 4, 8, 16, 32 frames) | `$AD $B9-$BD` | 1 |
| buttons held, assignments held, pressed | `$2D-$44` (17) | 3 |
| face (self or an object; a direction, operand, variable) | `$0F $17 $1B $1D $1E $1F $25 $26 $A6 $A7` | 1 |
| where an object stands, which way it faces (object or member) | `$21-$24` | 2 |
| call (object or member; now, when taken, and wait) | `$02-$07` | 3 |
| take calls, refuse them | `$08 $09` | 1 |
| speed (number, variable) | `$89 $8A` | 1 |
| stand on a tile (numbers, centred; variables) | `$8B $8C` | 1 |
| sprite shown, hidden, none (self or an object) | `$7C-$7E $90 $91` | 1 |
| say a line, ask a question (where the window goes) | `$BB $C1 $C2`, `$C0 $C3 $C4` | 2 |
| set the result (variable, story) | `$19 $1C` | 1 |
| where the party is, or a place put aside | `$DC-$DE` | 1 |
| wait for the fade, for the tint | `$F2 $F3` | 1 |

150 of the rows are operations, over 70 functions; 35 are bridges. Each
registered entry is named for its operation, with what tells a family's
opcodes apart (`event_cmd_loop_animation_0`, `event_cmd_wait_frames_16`,
`event_cmd_if_pressed_a`). The subsystem's concepts have files of their own:
the language (`script.h`, `engine.h`, `engine.c`), the scripts' variables and
the story (`variables.h`, `variables.c`, `story.c`), the party (`party.h`,
`party.c`), dialog (`dialog.c`), calls between objects (`calls.c`), the moves
(`movement.c`). Where a command changes a field object's state -- animation,
position, layering, whether calls reach it -- it goes through small local
helpers named for what they do, grouped at the top of each file
(`restart_animation`, `interrupt_loop`, `back_to_loop`, `put_on_tile`,
`put_at`, `relayer`, `set_priorities`, `set_steps_per_move`,
`layer_as_the_map`, `calls_skip`, `take_off_the_map`, `stop_moving`, `turn`,
`aim`, `aim_at_object`, `head`, `set_steps`, `count_move`, `mark_launched`),
which field's object module replaces when it lands (TODO.md lists the
definitions a review found duplicated between them).

**The engine** (`engine.c`) is plain C: a turn, with its budget and its stop
at the script's last offset, keeping where the script stopped; and the map's
openings. It runs each command through a runner the caller gives it; on the
development system that is `event/bridge.c`'s, which runs a row's operation
as C, a row's bridge on the machine -- called directly, with the return
address the original's `JSR` would push, and its time named with
`native_time_as` at the row's handler -- and only what has no row, or a state
the port refuses, through the original's table and an emulated call. A bridge
that calls another port of the subsystem calls it the same way
(`event_call_bridge`: `$AF` runs `$B0`'s, `$B5` and `$B6` the moves toward an
object and a party member), never through its ROM address. The port takes a
command when `event_runs_here` does: the running object one, the command at
least 8 bytes from the buffer's end (so no operand is read through bank
`$80`'s mirror of low RAM, which also lets the rows decode every operand up
front), what the operands' kinds say (an object operand even, a member's
object one, a bit number whose mask stays in its bank) and the row's own
check. After each command it hands the machine the X and carry the command
returns and, as scaffolding, the Y and V the original's handler would have
left. Each row's handler stays registered: an operation's by a generic entry
that runs its row, a bridge's by the bridge itself, each with its row's
guard. `$C0:595C` and `$C0:597D`, still the original's, reach the handlers
through the table, and `ct_fuzz` exercises each alone; the call logs show no
other code reaching a handler.

**What shadow mode found.** Over the library nothing mismatches, the engine's
two entries match on every call, and the per-entry totals of every hand port
but the bridges are identical to `main`'s. The bridges' change shape:
shadow mode runs the original's engine, which calls each handler at its
entry, and compares the port's registered entry for it; a handler with no
call-outs is compared there as before, but a bridge the port's engine calls
directly makes its call-outs as the engine's, so shadow mode abandons the
bridge's own comparison ("started inside the engine's stretch"). 506,381 of 518,396
bridge calls over the library are counted abandoned where they were matched
(`event_cmd_behave` 250,698 of 251,565, the moves toward the party 72,101 of
72,121, ...), still checked at every call-out they make and at the end of the
engine's turn, and fuzzed one by one below. What the review expected to find
in the handlers' totals is here, in the bridges'.

**Differential fuzzing.** `tools/fuzz_compare.py` against `main`'s `ct_fuzz`, every
event entry, seed 1, both variants (372 runs, 7,422,000 states):

| Variant | Entries | States | Mismatched before | of | Mismatched after | of |
|---|--:|--:|--:|--:|--:|--:|
| tight | 187 | 3,722,000 | 0 | 1,595,786 | 0 | 1,595,919 |
| loose | 185 | 3,700,000 | 0 | 1,589,672 | 0 | 1,396,820 |

The loose runs compare 192,852 fewer states: the commands within 8 bytes of
the buffer's end, which the guard now refuses (their operands would come
through bank `$80`'s mirror of low RAM). The engine's runs compare a few more
(6,823 turns against 6,695, 79 openings against 74), since a bridge it calls
directly no longer runs past the fuzzer's budget as a call-out. The first
runs found three real differences, each fixed: `$07` (call a party member and
wait) had taken the looser guard of `$05`/`$06`, where its waiting path goes on
with an empty place's marker (1,262 states); `$E4` kept its saved position in
a local where the tile copy's indexed stores can overwrite the cell (274); and
the turn counted its budget in a local where a command the original runs can
spend it (4 of the engine's).

**What it cost.** The engine spends its time on its commands' work, each
attributed to its row's handler (`native_time_as`, the bridges' as well
now), and the rows run differently from the handlers they replaced. So event's
rows were recalibrated (`timing_gate.py --calibrate`, event's rows only):
over the two reviews 87 of the 185 handlers' `cost_milli` moved -- `yield`
4750 to 2375 and `halt` 4000 to 2000, which touch the bus only to decode,
`var_add` 4341 to 3183, `call` 4599 to 5270 -- the per-frame turn 3256 to
4500, and the openings at map setup 4173 to 5936. Decoding the bridges'
operands by their rows, before their first call-out, moved 22 more, all down
(`move_in_direction` 2968 to 2414, `play_effect` 3373 to 2768,
`jump_to_tile` 2606 to 1519, `move_toward_party_member` 4469 to 4151). The openings' own walk over
the object slots (every object cleared, only some run), and the commands they
hand to the original's table, which they look up, are a mix one scalar can't
fit: 3.15% of calls off by a line or more against the 1% vblank-tail limit, at
map load with the screen blanked, writing RAM. That is an entry in
`tools/timing_exceptions.txt`, and the vblank check finds none of its writes
moved. Every other event entry is within the limits at its calibrated value.

**The second review**, of the language table above, rated `sound.c` and
`movement.c` translations and the rest hand-written or nearly, and found what
reads as the original in the rest; its items became review-port's 20-25.
What changed for each:
- *The C table is the only dispatch.* The bridges became rows; the engine
  reads the ROM's handler table only for what has no row; the time tag is the
  row's; `$AF`, `$B5` and `$B6` call their siblings as C.
- *Scaffolding out of the language's rows* into `event/leaves.c`: the parse
  positions, the kept operands (an enum and an index, not `0x80|n`), the V an
  addition leaves.
- *`sound.c`*: the driver's commands are named in `platform/audio.h`
  (`au_play_effect`, `au_fade`, `AU_FADE_MUSIC` ..., `au_music_fade_left`,
  `au_music_playing`), the waits named for what they wait on, and the registers
  the driver doesn't read declared with `PF_CALL_DC` rather than loaded; what
  it hands back to the command's caller (A's high byte, Y) stays, and says so.
- *`movement.c`*: the decisions return plain values; the carries the field's
  motion reads are computed in the helpers that make those calls, each naming
  the callee (`carry_after_tile_test`: what `$C0:ABA2` passes to `$C0:5B95`);
  the V the turn aside, the screen test and the jump's rise leave is
  scaffolding derived from their operands; each move's operands are decoded
  once into a struct (`TileMove`, `ObjectMove`, `DirectionMove`); `$7B`
  became an operation (`launch`).
- *Cells used as registers*: the saved positions the moves, the sound
  commands, the party routines and `$D6` keep across their calls, `$7A`'s
  height in `event_count`, and `$4E`'s count are locals now, written once as
  scaffolding. Two stay cells, for reasons fuzzing found: `$E4`'s saved
  position, which the tile copy's indexed stores can overwrite (the command
  goes on from what it holds after, as the original's does: 274 of 5,001
  states differed with a local), and the turn's budget
  (`event_commands_left`), which commands the original still runs can spend
  (4 of 6,823 turns differed).
- *The turn loop as plain C* (`event_run_turn`, `event_run_openings`), the
  machine behind a runner the entry wrappers supply.
- *Rows name their parameters* (review-port's item 24, from the review's
  follow-up): the positional `args` column and the adapters that shuffled
  `a->arg[n]` into each operation's parameters went; a row says
  `.animation = OPERAND(1)`, the operation reads `c->animation`, and the
  bridges and the guard's checks read the same decoded command, so no command
  of the subsystem reads an operand by its offset outside the engine. The
  scaffolding table names the parameters it keeps (`KEEP(WORD, amount)`).
- *The rest*: one flow for the call commands (`take_the_call`), the tint's
  channels taken once, `count` folded into `add` (and `subtract`), locations
  through `set_location_as`, calls' places as entries of the table rather than
  the original's byte offsets, `copy_data` decoded into a `DataBlock` (and its
  Y as scaffolding), `$D6`'s carry computed in its bridge from the search the
  logic makes, typed operand kinds, named blocking and solidity bits, the
  waiting mark a bool, entry names that are the operations', a typed tint and sprite priority (the field's
  shadows still hold hardware bytes; TODO.md), the dialog flag named for what
  sets it, and `re/ram.toml`'s event sources brought up to date.

**What liveness could still take down.** V would be declarable dead at every
event entry if the analysis followed the field loop's JMLs (a hint per site,
or JML edges in the runtime's call log): 1.5 million commands never test it.
Y at the openings' end is stored by the menus' dispatcher and never read,
which a ported menus dispatcher that establishes it dead for routine `$0B`
would settle -- but `$C0:595C` and `$C0:597D` must be ported too, since they
reach the handlers' entries. The scratch cells can't be declared at all
(don't-cares are registers and flags); they go when the gate does (TODO.md,
PLAN.md "Retiring the gate").


**The second review, and what merged with it.** A fresh reviewer read the
branch at `8e061e8` against the rubric's 31 items and rated no file
translation. `dialog.c`, `input.c`, `story.c`, `party.h`, `variables.*` and
`event_data.h` rated hand-written. The machine glue and the scaffolding table
were judged thin and confined. The C table is the only dispatch for ported
opcodes; the ROM's table is reached only for opcodes with no row and for rows
whose guard declines. Nothing meaningful sits behind `ORIGINAL_EFFECT`.

The review's "mostly" findings were **accepted for the merge**. The user chose
to merge the improvement and fix these as a follow-up checked by the gate, rather
than hold it for another round. They are listed in TODO.md, "Event's follow-up
after its second review". In short:
- **Fixable inside event:**
  - stale `re/ram.toml` sources naming functions that moved;
  - dead helpers;
  - two setter chains left in `animation.c`;
  - a guard that re-runs the search it guards;
  - an uncommented restart of the openings loop from a RAM cell;
  - `party.c`'s carry-reproducing decision;
  - the script read that bypasses its accessor;
  - packed operands still decoded inside operations;
  - the `.routine` and `.bridge` columns in the language's rows;
  - bridge helpers copied into five files;
  - operations taking one ~80-field `EventCommand` instead of the parameters
    each reads.
- **Waiting on the field's object module and the naming pass:**
  - one field-object vocabulary (the same bit named differently across four
    files, `well_on_screen` written twice);
  - `movement.c`'s families, which can only become operations and rows once
    the field's motion is C;
  - the map's layout-shaped names.
- **The platform:** a color-math vocabulary for `tint_math`.

### Phase 3a: the field subsystem off the machine

The third subsystem of PLAN.md's Phase 3a: all of `game/field/`, 82
registered entries (70 in the table and the twelve deferred uploads one macro
makes), 15 files of which ten were on the transcription backlog (16 files
now: the windows' corner tiles are a file of their own). Before, field scored
0.35 operations per line and 93% of its functions took a `Snes *`
(portstyle's count); `display.c` reached the PPU through a direct page of
`$2100`, `sprites.c` wrote the sprite table through the work RAM port with the
direct page on the port's registers, and the object routines were one
function per ROM routine over `c->a` and `c->x`. After, it scores 0.02, no
file is on the backlog, and portstyle's column says 30%. Counted by name,
148 of its 588 functions take the machine: 98 are the entries' functions and
their guards, and the other 50 are listed at the end.

**Objects as a module.** The object routines were rewritten as a domain
module, `field/object.h`, before the files that use it: the part of the game
an event script, the object update and later `event/` itself talk to. An
object is a `FieldObject` (a slot); its state is kept as four concepts, each
with operations that do their bytes' writes inside:

- *its role* (`ObjectRole`, a kind and a disabled flag; `object_is_party_member`,
  `object_may_run`) and whether it is in view (a `bool`): the party's leader
  and members, other objects, objects that only come and go with the view,
  objects left alone -- one definition where four files had their own copy
  of the state byte's values;
- *its animation* (`field/animation.c`): the four modes a script chooses
  between (`ObjectAnimationMode`: with its movement, a loop, a one-off played a
  number of times, a held frame) and the operations a script uses
  (`object_loop_animation`, `object_play_animation`,
  `object_finish_animation`, `object_show_frame`, `object_stop_animating`,
  `object_hurry_animation`), the movement animation the field picks
  (`object_animate_movement`, `object_stand`), and every frame's tick and the
  frame builder's read (`object_animation_tick`, `object_current_frame`);
- *its motion* (`field/motion.c`): `Facing`, `Direction` (a byte to a turn),
  `MapTile`, `FieldPosition`; `direction_between`, `object_head`,
  `object_start_step` (with its `StartOutcome`), `object_in_the_way`,
  `object_line_up_with_tile`/`_with`, and the step every frame
  (`object_take_step`, `party_member_take_step`);
- *its place on the screen* (`field/object_update.c`):
  `object_update_screen_position`, `object_leave_row`, `object_join_row`.

The map's records don't group that way, which is the point of the module:
`field_object_move_state` holds both the one-off animation's plays
(`animation_count`, the animation's) and whether a move may cross its tile
(`tile_mode`, the motion's); `field_object_animation_mode` holds the mode and
the one-off's animation, while the loop's animation is
`field_object_animation_number` and the frame, its delay and the tables are
five more variables; the role is `field_object_state`'s `state`; and the
position is four bytes (`x_fine`, `x_tile`, `y_fine`, `y_tile`) the module
reads as a `FieldPosition`. The accessors keep the map's names (an awkward
one, `field_object_animation_mode_mode`, included); the module is where the
concepts are named. The operations the scripts' commands need are there, but
`game/event/` still has its own copies of the animation's state machine and
still calls the motion routines as call-outs: moving it onto the module is
event's next change (TODO.md).

No register-shaped value is in the module's signatures. Where an entry's
callers read something the original leaves in a register, the wrapper
derives it from the game's state around the call: the fine position before a
step, the plays before a one-off's frame table ended, the direction a blocked
start last looked in. Where it follows only from a value the routine had on
the way (the last distance the in-the-way check measured, whether reading a
frame started the animation over, the wave's last line's sum, the unpacker's
last run, the rasterizer's last sloped edge), the routine records it in a
file-static record (`in_the_way_leaves`, `frame_read_leaves`, `wave_leaves`,
`unpack_leaves`, `shape_leaves`), written only by a `record_...` helper and
read only by the wrapper or a `leave_...` helper (field/scratch.h). It is the
shape of event's `event_leaves`, and moves onto the scaffolding mechanism when
that is in the tree.

**Calling its own ports as C.** The object update, the sprite table's
building and the vblank handler called the field's own ports through their
ROM addresses, as the original does. They now run them as C, each timed as
the routine it is (`NATIVE_TIME_AS`, so the calibrated estimates still
apply): the update's move, view, tick, rows and screen position; the
building's draws and their placing; the vblank's sprite table, sprite tile
queue, the twelve edge uploads and the line effects' programming. Two rules
decide where an entry is still called by address, and both come from how
shadow mode checks a bridge. Where the state is one the entry's guard would
turn away, the entry is called, so the original runs it
(`field_tick_is_exact`, `field_rows_are_exact`, `field_layer_edges_fit`, the
draw's cursors and records). And where the inlined work would itself call the
original's code (an object coming into view or leaving it, a rebuild of an
object's records, the 24-sprite layout), the entry is called too: shadow mode
compares a bridge's calls with the calls the original makes itself, and a
callee's calls made from the caller don't line up with them. The building's
draws go further: every draw of a frame is made from the same two call sites,
so shadow mode can only match them if the port inlines all of a frame's
draws or none (`every_draw_is_own` walks the rows first, asking of each
object only what its draw's guard would). With those rules the
shadow totals stayed the predecessor's; the first try, which inlined a
rebuild and the view's coming and going, mismatched in 105923 draws.

**The machine, replaced.**

- *The direct page on the registers.* `sprites.c` wrote the sprite table
  through `$2180` with D = `$2100`; it now seeks and writes the work RAM port
  through `game/cpu_io.h` (`hw_port_seek_address`, the new
  `hw_port_seek_bank`, `hw_port_write`), and the table and the records are
  byte buffers in the map (`field_sprite_table`, `field_sprite_records`).
  `field/sprites.c` left the platform lint's `UNCONVERTED` list: `game/` is
  now all behind the services bar three documented files. The four windows'
  corner-tile writers (`field/window_corners.c`), which the original runs
  with the direct page on the PPU's registers, write through the video
  service. An object's sprites are read into a list of `Sprite` (a signed
  screen X, the ninth bit made only by the one function that writes a list
  into the table, `write_sprites`); the lists are an object's, not a range's,
  since a range's would be up to 128 sprites of locals.
- *`$0900`,* the sprite table's high part, is `field_sprite_table`'s last
  `$20` bytes, reached by offset.
- *The arithmetic units.* The velocity is `speed * factor >> 8` and the
  shading's step `40 / 8` in C; the multiplier's and divider's registers,
  which the original leaves holding its last operation, are written once in
  a marked helper (`leave_multiplier`, `leave_shading`); `cpu_io.h` gained
  `hw_remainder`.
- *The rasterizer* (`field/window_shape.c`, `$C3:0011`) was a transcript of
  the edge routine with its stack bytes, over 16-bit A and the flags; it is
  now the scan conversion it is: an edge from its upper end down, the X
  moving a pixel at a time while an error term lasts, a level edge writing
  both ends, and the lines outside the shape copied down from the first.
  The one place the original's rounding shows is one named function,
  `edge_start_error`: an edge running left starts a step behind the mirror of
  one running right, so the two sides of the Black Omen's light cone lean a
  pixel. Its guard keeps the table in bank `$7F` clear of the bank's end.
- *The wave.* The line effects' BG3 wave adds its 16 lines as one running
  sum, so a line whose sum wraps the scroll's range moves the next a pixel,
  and the first line moves a pixel while window 2 reads its second table;
  that is one named function (`fill_wave_scroll`), and `send_scroll`'s entry
  is guarded on the carry it is entered with being that choice (the builders
  always enter it so). The wave's line was kept in `field_direction` from
  part to part; it is a local now, and the cell gets its final value.
- *The flags callers read.* Carries and overflows are derived in the entry
  wrappers from the logic's results, through one set of helpers in
  `field/bridge.h` (`field_add_overflows8`, ...), where five files had their
  own.

**Scaffolding.** The cells the original writes for itself (the field page's
`$C1-$C8` and `$D9-$E6`, the layers' page's `$1D50-$1D6F`) are reached through
`field/scratch.h`, and every write of one is in a small helper per file named
for what it leaves (`leave_screen_origin`, `leave_start`, `leave_shading`,
`leave_wave_line`, ...), written once with the final value where the original
wrote a cell several times on the way; the working state is in locals. Two
cells were named by meaning because code reads them (`field_layer_edge_wrap`,
`field_direction_work`); the rest have no reader but the original's own later
instructions.

**What the map gained**: `field_work`, `field_sprite_graphics`,
`field_sprite_tile_offset`, `field_frame_entry`, the layers' work cells, the
window shape's table, edges, top, right side's start and steps,
`field_animation_queue` as a record, `field_direction_work`,
`field_layer_still`, `field_tile_properties`, the sprite table and records as
buffers; `event_scratch` grew to `$E6`. The field's three open addresses are
closed (43 left in the tree). `game/wram.h` is used in two places: the
rasterizer's table, which the caller locates in bank `$7E` or `$7F`, and the
sprite graphics' build buffer.

**What shadow mode found**, beyond the open-bus order of a long pointer (read
its address before its bank, or a pointer to nothing reads another bus
value): the stale-carry rule again (a call-out's don't-care carry comes back
as the port's own where the callee leaves it, so the object update reads
whether the view step left the object in view from the object), the
unpacker's run bytes, the object rows' links read as the 8-bit `$6D`, and a
guard that refused real sprite draws. Over the 26 movies every entry matches,
no page is refused, and the per-entry totals (calls, mismatched, skipped,
abandoned) are the predecessor's for every one of the 619 entries; so are
matched and interrupted, but for four entries, whose calls an interrupt now
lands in more often, since work that ran in a nested entry's call runs in the
caller's time: `field_update_objects` 6 interrupted of 193199 to 17,
`field_build_sprite_table` 267 of 193483 to 336, `field_draw_object_sprites`
837 of 974055 to 1713, and `field_update_object_sprites` 78 to 855. An
interrupted call is one shadow mode didn't compare, so those are what their
fuzzing (below) covers. No guard refuses a call the game makes: the skipped
counts are the predecessor's.

**Differential fuzzing.** Every entry is in `tools/fuzz_routines.toml`, and
all 66 routines listed there (two with a loose variant) were fuzzed against
the port each replaced (`tools/fuzz_compare.py`, seed 1; 20000 states, 10000
for the rasterizer, 2000 for the update loops, the unpacker and the vblank
handler, 200 for the VRAM queue). The rewrite mismatches nowhere. 65 agree
with the predecessor on every compared state; where the rewrite compares
fewer, the rest are its guards' refusals, counted as skipped (an odd object
or link, a delay or frame table past its bank, a stream that would reach the
layers' page, a table at the end of bank `$7F`, the other carry into sending
the scroll), and each PORTING.md row gives both counts. The one that differs
is `field_position_sprites` loose, with the records anywhere: 3452 of 19995
mismatched before, 0 of 13878 after, the records that would run past bank
`$7F` into bank `$80`'s low RAM and I/O turned away by a guard (the
predecessor diverged there). `field_build_sprite_table` compares nothing on
either side: every state's draws run past the call-out budget. Getting there
found the open-bus order of the frame table's pointer, the rasterizer's
stack clobbered by a table in bank `$7E` (now guarded), and carry-chain
quirks at facing 3's tables and the delay entry, now guards.

**What it cost.** Recalibrating field's `cost_milli` rows moved 60 of
them: the rewrite's bus accesses are the game's reads and writes, where the
transcripts' were every operand of the original's, so ports that read words
where the original read bytes cost less per access (`field_line_up_with_tile`
4865 to 2313) and ports that keep their working state in locals cost more
(`field_compute_layer_edges` 2920 to 11237, `field_direction_to_target` 4731
to 8132, `field_position_sprites` 4002 to 4751, `field_nmi` 4023 to 4520).
The bridges that now run their steps as C are timed as those steps plus their
own loops (`field_update_objects` 4165 to 4309, `field_build_sprite_table`
6699 to 4736). Six entries went onto `tools/timing_exceptions.txt` and
`field_rasterize_quad` gained a third limit: their time follows loops whose index steps the original's time
follows and the rewrite doesn't make (the sprite table's leftover-hiding, the
line-effect builders' bands, the unpacker's runs, the rasterizer's pixel
steps), so one scalar fits their average but not each call. All of them write
RAM the next frame reads, the vblank check finds none of their writes moved,
and the errors are at most two lines where they end in vblank
(`field_build_line_effect_tables` 2252 clocks), but for the sprite table's
building, whose draws now run in its own call (7352); the unpacker runs at a map's load, 2.5 frames a call. The sixth is
the object update itself, whose call is now the whole frame's object work
(30848 clocks on average, where its own stretches between call-outs were 115)
and follows how many objects there are: 3% of its calls reach vblank, off by
at most 1915 clocks there, writing object state the next frame's sprite
table reads.

**The review rubric, two rounds.** Checked against
`.claude/skills/review-port` by reviewers who hadn't seen the change. The
first rated `line_effects.c` a translation (the wave's carry chain threaded
through the builders, the wave's line kept in `field_direction`, the wrappers
working the logic backward) and the rest "mostly". What was changed for it:
the wave's line a local and its carry one function (`fill_wave_scroll`,
above); one definition of `field_display_flags`' bits (`field/field.h`), of
the 4.4 scroll fraction, and of the layers (`FIELD_BG1`-`FIELD_BG3`); the
arithmetic units' results computed in C; the map transition's steps named for
what they send; entries renamed where their words disagreed with the
operations' (`field_leave_object_row`, `field_join_object_row`,
`field_tick_object_animation`); and `field_setting_bc` found to be the
screen's shake, which the map now says (its accessor keeps its name).

The second rated `animation.c` and `motion.c` translations by the threshold
rule (their twin functions passing traces to the wrappers, rubric item 26)
and `sprites.c` a translation (its row counter the original's Y, its ranges
and records addresses and byte offsets, its start in the original's write
order). What was changed for it:

- *Traces out of the signatures* (item 26): the twin functions are gone, the
  wrappers derive what they can from the state, and the rest is recorded as
  above. `StartLeftovers`, `PlaceLeftovers` and the rasterizer's and the
  unpacker's leftovers, which the logic filled as it went, are gone too: the
  `leave_...` helpers take the final values once.
- *A start's sign bytes* (`KeptSteps`) are one named quirk,
  `retry_look_across`: a retry with no horizontal step looks a tile left
  where the first try went left. The in-the-way check's borrow is
  `earlier_slot_measured_nearer`, with what the player sees.
- *The sprite table* is rebuilt around `Sprite` and one serializer (above).
  The building's row counter is a local set on the machine only at a call,
  taking back what the original's draw leaves (the original counts rows in Y,
  and its draws can move it); the ranges are offsets into the table,
  converted to the addresses the original keeps only in their storage
  functions; the records' fields are named in one place. The all-or-none
  walk stays (item 29): shadow mode pairs a bridge's calls with the
  original's in order, so drawing one object as C and calling the next would
  pair that call with the original's draw of the first. It shrank to asking
  of each object what its draw's guard asks; the ranges' cursors, which it
  simulated, can't leave low RAM for the layouts it takes.
- *Aliases past the tables* (item 28) are explicit: taking an object out of
  the middle of its row writes the in-view flag of slot `(link - $80) / 2`
  when none follows, in `leave_stray_in_view_write`; the in-the-way search's
  slots past 64 go through `stray_slot_can_block`; the map transition's third
  destination is its own variable, `field_transition_map_base`, not "the word
  after BG3's"; and the object update's guard now turns away a count past 64,
  which would run its loop into the tables beyond.
- *Loop state and call sites* (items 13, 30, 31): the object update's loop
  runs on locals; its call sites are tables by step and by which update
  (`view_sites`, `tick_sites`, ...); the call-outs are named for what they do
  (`GIVE_PALETTE`, `FREE_SPRITE_RECORDS`, `SEND_STAGED_MAP`, `MENUS_FRAME`,
  ...); a call's return is `site + JSR_SIZE`.
- *Scaffolding out of the logic* (items 9, 13): `field_target_object`'s high
  byte, the port's bank and the current object's high byte at the sprite
  table's start, the queue's Y, the placing's ninth bits, all in named
  helpers; `scratch.h`'s value readers are gone (the tile copies' Y is the Y
  they came with, now that the mirrored copy's guard keeps the port's writes
  off the cell it saves Y in; the shading's overflow follows from its
  constants); the rasterizer's cells go through `scratch.h` like the rest.
- *In-band encodings* (items 5, 10): in `animation.c` the `$FF` "nothing to go
  back to" and the half-pass countdown are confined to storage functions, and
  the public count is in passes; the role is a kind and a disabled flag, in
  view a `bool`, the records' state a struct, a move's tile crossing an enum,
  an object's screen position an `int16_t` pair (nine bits only in storage and
  in the sprite table), a row or queue link a `FieldLink`, and a started-over
  animation's frame `BEFORE_THE_FIRST_FRAME`.
- *Structure*: coming into view calls the animation's and the rows'
  operations; `view_window` is shared by the view and its guard; one
  vocabulary for the dialog window's position in `line_effects.c`; the edge
  runs are named fields (`field/layers.h`), and the twelve uploads are named
  for which run they send (`field_upload_layerN_bottom_row`, `_top_row`,
  `_right_column`, `_left_column`, which were `_tiles`, `_tiles_alt`,
  `_columns` and `_columns_alt`); the windows' corner tiles left `display.c`
  for `window_corners.c` (with entries renamed `field_write_window_corners_N`
  and `field_refresh_window_corners`); the interrupt handlers' register save
  is one helper in `runtime/port/machine.h`.

What a reader can still see of the original: the scaffolding helpers name
the cells they leave by the original's direct-page offsets (`$D9`, `$DB`),
since the cells have no reader but the original; the sprite table is still
written through the work RAM port, until a platform sprite service exists;
and `game/event/` still keeps its own copy of the animation state machine
(TODO.md). Deferred: renaming the map's stuttering fields, and the platform's
sprite and line-effect services.

**Left on the machine, and why** (the 50 functions that are neither an entry
nor a guard): the shared pieces of the guards and the entry wrappers in
`bridge.c` (8: which object the registers name, whether a callee came back as
expected, the page and bank checks the guards compose); the frame wait's
pass (1); the wrapper pieces the line effects' and the tile copies' entries
share (9); the steps the object update runs as C where their entries would
run them, which set the flags the entries leave (4: the move, the view, the
window-only check, and the rows' entries' return); and the bridges around the
original's code: the vblank handler's (`nmi.c`, 8: a frame's transitions,
fade and palette are the original's), the object update's (9) and the view's
(2), and the sprite table's (9: an object's records are rebuilt for 8, 12 and
24 sprites by the original's code).


**The fresh review, and what merged with it.** A reader who hadn't seen this
account read the branch at `4972e5d` against the rubric's 33 items, and rated
no file translation. `window_shape.c`, `window_corners.c`, `scroll.c`, `frame.c`
and `interrupts.c` rated hand-written. `animation.c`'s storage and script
operations reach the shape of the review skill's illustration; its frame-read
half does not yet. No logic function passes a register-shaped struct. The
five `*_leaves` records are written only by their record helpers, and the
object update's stray in-view write is the explicit, marked model the other
aliases should follow.

The "mostly" findings were accepted for the merge, on the same terms as
event's: merge now, fix on main as a follow-up checked by the gate (TODO.md,
"Field's follow-up after its review"). The most important are stray accesses
past a table that are still hidden behind clean accessors: a fifth sprite's Y,
the high-table cursor's spill, map tables declared as their first 256-byte
row and indexed across 64 KiB, and the stray-slot reads. They are no problem
while storage is the original's layout, and a hazard the moment it isn't. The
rest are reading:
- sprite records addressed by offset;
- bridge loops that run on the original's Y or on the current-object cell;
- dispatch that compares ROM addresses;
- the bridge helpers copied into four files;
- two guards that re-run what they guard, which shadow mode's call pairing
  forces;
- `object.h`'s link encoding showing through its interface.

The platform gaps (a sprite service, typed line effects, transfers without
write-order arrays) and the map's renames are deferred, as before.

### Phase 3a: event onto the field's objects

The pass PLAN.md scheduled once event's language and field's objects had both
landed: it finishes what the two subsystems' reviews left (TODO.md's lists,
each item deleted as it was done), moves event's commands onto
`field/object.h`, and names the object tables in the RAM map by what they
hold. Five steps, each committed with shadow mode clean over the library.

**1. The stray accesses past a table, made explicit.** Each is one marked
access, the way `leave_stray_in_view_write` already was, and is listed in
TODO.md for the build without the original's memory:
- an object above the screen places a fifth sprite's Y, in the record after
  its four (`leave_stray_fifth_sprite_y`, `ORIGINAL_EFFECT`);
- a range full enough runs its high table cursor past `field_sprite_table`'s
  $220 bytes, and the original writes the high bits there too
  (`leave_stray_high_bits`, by address through `game/wram.h`, since those
  bytes are no variable's);
- the in-the-way search's slots past 64, which the original reaches with no
  objects or 64 of them (it counts round 128), are the original's: no map has
  either, and the guards of the check and of the moves that set off refuse
  them (`object_search_fits`), where step 1 first read the tables after each
  table by their own names (item 34 of review-port's rubric, "emulating inputs
  no game state produces", which came after);
- the openings' word clear of the last slot's solidity clears the first
  object's falling flag (`object_start_on_the_map`);
- `field_layer_maps`, `field_tile_properties` and the two tile attribute
  tables are tables of rows: `tools/ram_map.py` has a new shape for them
  (`rows`, `row_stride`: the variable is one row, repeated a row stride apart
  with other tables' rows between), whose accessors take a row and a column,
  with tests. The attributes are two variables now, the upper and the lower
  (`field_tile_upper_attributes`, `field_tile_lower_attributes`), and a map's
  tile is one `TileAttributes` (`field/layers.h`), where two files had their
  own.

The object guards take only slots inside the tables (`field_names_object`:
below `$80` doubled, where they took anything below `$100`). One exception the
game makes: a command that names a party member without checking takes an
empty place's marker as object 64 (`$95`'s 52,898 calls over the library
include a scene following a party of two; the member's place is empty). This
step first took the marker as "the absent member" and let the port read the
tables' neighbours through the objects' accessors; the fresh reviews asked for
an explicit path instead, and the guards now leave those calls to the
original (below, "The fresh reviews' must-fix items").

**2. `field/object.h`, the only way to an object's state.** The header gained
the operations event needed, each with its storage in one place
(`field/object.c` for what a script sets, `motion.c` for position and moves,
`animation.c` for the animation, `object_view.c` for the view):
- role, reachability and sprite: `object_take_off_the_map`,
  `object_leave_the_party`, `object_character`, `object_runs_a_script`,
  `object_unreachable`/`object_set_unreachable` (the mark is the top bit of the
  move length, which a walk's step count then carries), `object_set_sprite`
  (`ObjectSprite`), `object_can_be_seen`, `object_start_on_the_map`;
- place and look: `object_tile`, `object_place`, `object_put_on_tile`,
  `object_facing`, `object_set_facing`, `object_turn`, `facing_of_direction`,
  `object_set_layering` (`ObjectLayering`), `object_layer_as_the_map`,
  `object_set_sprite_priorities`, `object_take_own_priorities`;
- what a script sets about moves: `object_set_speed`, `object_set_blocked_by`
  (`ObjectBlocking`), `object_set_solidity` (`ObjectSolidity`),
  `object_set_arrival`/`object_lines_up` (`ObjectArrival`),
  `object_set_move_length`/`object_move_length`;
- moves: `object_start_move` (with a `TileCrossing`), `object_start_walk` (the
  scripts' walks store their step count as the crossing, so an unreachable
  object's 128-step walk crosses on every step), `object_start_one_step`,
  `object_moving`, `object_stop_moving`, `object_mark_scripted_move` and its
  pair, `object_launch`, `object_jump`, `object_land`, `object_velocity`,
  `aim_toward_object`;
- animation: `object_play_animation` takes the half-pass countdown `$B7`
  gives (`$AB` passes two), `object_end_frame_time` (a turn shows at once),
  and the frame read split into a finder and a taker
  (`object_take_next_frame`, below);
- the screen: `tile_well_on_screen`, one test where two files had it.

And the four the review asked for: `FieldLink` is a value whose byte only the
storage functions and the guards see (`field_link_to`, `field_linked_object`,
`field_link_is_object`, `field_same_link`); the in-view flag is one
`ObjectView`, since the row removal's stray write can leave a link in the
first object's flag, which the object update reads as out of view and the
in-the-way check, the party's re-sort and `$27` as in view
(`OVERWRITTEN_BY_A_LINK`); `ObjectRole.kind` is an `ObjectKind`; and
`object_make_current`, the original's calling convention, is in
`field/bridge.h`. The step's start takes a tile's two priorities the other
way round from a script's layering (the original's two routines disagree
about which table is which half), one named function
(`take_priorities_for_the_step`). The object update's steps take the object
as a parameter, and its loop end is marked scaffolding.

**3. Event onto the object model.** No file of `game/event/` reaches an
object table's accessor any more; "the field's objects" sections are gone.
`event/animation.c` is four operations over the field's state machine.
`event/movement.c`'s twenty bridges became five operations and a bridge:

| Operation | Opcodes | What the rows say |
|---|---|---|
| `move_to_tile` | `$96 $97 $A0 $A1 $9A` | walking or sliding (`MoveStyle`), the tile from operands or variables, counted |
| `move_toward` | `$94 $95 $9E $9F $98 $99 $8F` | an object or a member, walking or sliding, counted, until next to it or well on screen (`WalkEnd`) |
| `move_in_direction` | `$92 $9C $9D` | walking or sliding, from operands or variables |
| `jump_to_tile`, `launch`, `follow`, `follow_party_member` | `$7A $7B $B5 $B6` | |
| (bridge) `event_cmd_behave` | `$B0` | the field's state routines, still the original's |

They call the field's motion as C (`direction_between`, `object_in_the_way`,
`object_start_walk`, `object_start_step`, `object_animate_movement`,
`object_stand`, `object_line_up_with*`, `object_head`, `object_jump`), and
the carries they handed each other, `s->cpu.a = facing`, the jumper from X and
the call-site tables are gone with the calls. `$A8`/`$A9` (face toward an
object) are operations too. What the original's handlers leave in Y and V is
now the field's routines' own, recorded as scaffolding where the move ends
(`leave_set_off`, `leave_jump`), from the field's record of what each routine
leaves (`field/scratch.h`: `field_leave_aim`, `field_leave_direction`,
`field_step_start_y`/`_overflow`, `field_walk_overflow`, `field_jump_overflow`,
which the field's own wrappers now use too). Only the final Y and V of a path
matter, since shadow mode compares them where the command returns.

**Operations take the parameters they read** (review-port's item 32): an
operation is `static EventFlow loop_animation(GameState *g, FieldObject self,
uint8_t animation)`, and `EVENT_OPERATION(loop_animation, self, animation)`
after it makes the form the table calls, which hands it those of the decoded
command, in its order; a row says `OPERATION(loop_animation)`. The guards'
checks are the same (`EVENT_CHECK`, `CHECK`). `EventCommand` is the engine's
alone. The rows lost `.routine` (the handler's address is the original's
table's, read by the machine side, `handler_of`, which all 185 rows agreed
with) and `.bridge` (`event.h`'s `EVENT_BRIDGES` list is the table of
bridges); `EventParameter`, which only the scaffolding table used, is
`leaves.c`'s and compiles out with it. New operand kinds decode packed
operands into typed parameters: `OPERAND_BITS`/`OPERAND_WORD_BITS` (a fade's
brightness and rate, a tint's `TintMix` and rate, a call's level and
function, a story flag's page and bit, a question's first and last choice)
and `OPERAND_PLUS_ONE` (`$B7`'s count); solidity, blocking, arrival, layering
and the sprite are the field's types. Two stayed bytes on purpose:
`set_player_control` stores the script's byte, which the field tests as
nonzero, and a location's two words are decoded by `location_of` in the
operation (TODO.md).

**One vocabulary.** The bridges of both subsystems call the original through
`field/bridge.h`: `bridge_call`, `bridge_call_long`, `bridge_back` and
`BridgeMachine` (how the bridge runs: A's width, X's, the direct page, the data
bank), with `JSR_SIZE`; the five copies of `back()` and the four of the field's
call-out helper are gone, and `tools/dontcare_check.py` reads `bridge_call` as
it reads `m_jsr_dc` (with a test). The duplicated definitions went:
`DISABLED`/`CALLS_SKIP` (the object's `disabled` and `unreachable`),
`NO_SPRITE_SHOWN`/`SPRITE_HIDDEN` (`ObjectSprite`), `well_on_screen`
(`tile_well_on_screen`), the three aim helpers (`aim_toward_object`,
`field_leave_aim`), `field_requests`' bits (`field/field.h`: a battle is asked
for with a tile copy, `FIELD_REQUEST_BATTLE | FIELD_REQUEST_TILE_COPY`, which
the main loop's `$C0:1770` runs), the field's effect bits, `link_or_none`
(two guards named for what each checks), `TileAttributes`, `JSR_SIZE`,
`SPRITE_TABLE` (`FIELD_SPRITE_TABLE_ADDRESS`), `LEAVE_ROW`/`JOIN_ROW`
(`FIELD_*_ROW_ENTRY`, with the rows' step shared by the update and the view),
`AXIS_X` (`field/field.h`), the random table and its draw
(`field_draw_random`, which a blocked object's turn and `$7F` share), the
overflow helpers (`field/scratch.h`), and "keep the offset across the call"
(`event_keep_offset`).

**4. The RAM map by meaning.** The object tables' records grouped by where
their bytes sit became variables named for what they hold:
`field_object_animation_mode_mode` is `field_object_animation_mode`,
`..._animation_mode_timed_animation` is `field_object_one_off_animation`,
`field_object_animation_number` is `field_object_looped_animation`,
`field_object_move_state_tile_mode` is `field_object_tile_crossing`,
`..._move_state_animation_count` is `field_object_one_off_half_passes`,
`field_object_state_state` is `field_object_role` (and `_character`
`field_object_character`), `field_object_unlinked` (0 meaning in a row) is
`field_object_row_mark`, `field_setting_bc` is `field_screen_shake` (and event
`$F4`'s operation `shake_screen`, in `tools/disasm/eventscript.py` too), and
the byte of script variable `$A8` window 2 reads is
`field_window_2_second_table`. `field_sprite_frame_data` is its $340 bytes, not
its first record. `tools/ram_map.py` rejects a generated name that says a word
twice, and `check` lists the names still shaped like addresses (seven:
`field_setting_1e`, whose meaning isn't cheap to find, and six of the sound
driver's). Every stale `source` line of event's and field's was fixed: 57
named functions that had moved or been renamed.

**5. The rest of the follow-ups.**
- `sprites.c`: a `SpriteRecord` with storage functions (the record's X, Y,
  offsets and look, and the same sprite in the frame's data), which
  `place_sprites` works on; no `high_of` (the placing entry's guard takes the
  object in X only where it is the current one, which the game always has);
  the table's building loops on a local row, hands the draw its row in Y and
  stops where a draw came back with another; the records' steps are a
  `RecordsStep` over one table of each layout's routines, X set only for the
  calls that read it.
- `animation.c`'s frame read is a finder that changes nothing
  (`find_next_frame`) and a taker (`object_take_next_frame`), over
  `field_data.h`'s typed frame tables (`FieldFrameTable`,
  `field_animation_place`, `field_animation_frame_number`,
  `field_animation_frame_time`), where callers built ROM addresses.
- The object update's call sites are one table of the update's steps, and
  `nmi.c`'s three map-transition sends one table.
- Smaller: `wave_part` returns nothing (the wrapper works out the last
  entry's pointer), the view window's widenings are named, the layers are
  `FIELD_BG1`-`FIELD_BG3`, and the sprite graphics' ROM banks are
  `field_data.h`'s (`field_sprite_graphics_rom_bank`).
- The field's scaffolding helpers are called through `ORIGINAL_EFFECT`, as
  event's are, so the native-shape build compiles them out.
- PORTING.md's "Call-outs" says how a bridge's calls are paired with the
  original's, and the bridges' vocabulary.

**Against the rubric's later items** (review-port 34-38, which grew from
scene's review while this pass ran). The leftover records went where the
wrapper can derive the value (item 35): the in-the-way check returns the
object it measured last as well as whether it was blocked, and the wrapper
measures it again for the distance whose high byte the original leaves in A;
the wave's last carry comes from the table the wave wrote and the offsets'
table; whether the frame read started the animation over from the frame and
entry it left. What the wrapper looks at again it reads under `vars_peek`,
which the cartridge's accessors now honour too (`runtime/port/state_io.c`), so
it costs the port no time. Two records stay, for cells whose values follow the
path taken and are nowhere in the final state (the last run the tile
unpacking repeated, the last sloped edge of a window shape), written through
`ORIGINAL_EFFECT`; `field/scratch.h` says which. The in-the-way search's stray
slots are a guard now (item 34, above). No step is kept in the original's
order for the timing estimate's sake (item 36): the orders this pass kept (the
frame table looked up last, the copy of script data a byte at a time) are
behaviour, a read of open bus and a copy onto itself.

Deferred, each in TODO.md with why: the location's decoding, `nmi.c` sharing
`display.c`'s edge runs (the don't-care check reads a call's site only from
its own file), the staging buffers (a transfer service's), the prediction
guards, and the platform's sprite and line-effect services.

**What shadow mode found.** Over the 26 movies nothing mismatches and no page
is refused. The totals of every hand port are `main`'s but for the commands
that stopped being bridges, whose calls the engine used to make as call-outs
(so shadow mode abandoned them) and now runs as C (so it compares them):

| Entry | Calls | Matched before | Matched after |
|---|---:|---:|---:|
| `event_cmd_move_toward_party_member` | 72,121 | 20 | 72,121 |
| `event_cmd_follow_party_member` | 70,971 | 20 | 70,971 |
| `event_cmd_move_to_tile` | 38,113 | 451 | 38,113 |
| `event_cmd_move_toward_object` | 33,893 | 9 | 33,893 |
| `event_cmd_follow_object` | 33,893 | 9 | 33,893 |
| `event_cmd_move_in_direction_keep_facing` | 7,287 | 6,330 | 7,287 |
| `event_cmd_move_to_tile_keep_facing` | 3,074 | 112 | 3,074 |
| `event_cmd_move_in_direction` | 2,661 | 2,362 | 2,661 |
| `event_cmd_jump_to_tile` | 1,932 | 1,810 | 1,932 |
| `event_cmd_face_toward_member` | 1,543 | 0 | 1,543 |
| `event_cmd_face_toward_object` | 251 | 0 | 251 |
| `event_cmd_move_to_tile_from_variables_keep_facing` | 127 | 0 | 127 |
| `event_cmd_move_to_tile_from_variables` | 100 | 0 | 100 |
| `event_cmd_move_toward_party_member_until_on_screen` | 10 | 0 | 10 |

Every call before that didn't match was abandoned, never compared; none
mismatched, before or after. (The reviews' fixes, below, later handed the
calls on an empty party place to the original: 52,898 each of `$95`'s and
`$B6`'s.) `event_cmd_behave`, the one move still a bridge,
keeps its 250,698 abandoned calls.

The field's motion entries (`field_direction_to_target`, `field_start_step`,
...) keep their totals: shadow mode counts an entry where the original reaches
it, and the original still calls them from its own handlers. Renamed entries
(`event_cmd_set_bc`, `event_cmd_set_solid`, `event_cmd_move_by_variables`)
carry their predecessors' totals under their new names.

**What fuzzing found.** `tools/fuzz_compare.py` over event's and field's 438
runs (seed 1, `main`'s `ct_fuzz` against this one's) found one divergence, in
the rewritten frame read: `field_read_animation_frame` mismatched 517 of 3,326
states in A. The finder looked up the frame table (its bank last) before the
animation's number, and where the table points at nothing that answers (bank
`$3C`, `$4600`), the read returns the last byte on the bus: the bank in the
original, the animation's number in the port. The finder now looks the table up
last, as the original does, and reads a restarting animation's first frame
after putting it back before its start (a table in work RAM can hold the frame
number it rewinds; that took two more states). 0 then. Every other run has the
predecessor's mismatches, none, and three kinds of run compare fewer states,
each for a guard, which the skipped column shows:
- every entry whose operand or X names an object compares about half as many
  (`event_cmd_if_in_view` 5,055 to 2,513, `field_move_object` 10,065 to 5,060,
  `field_line_up_with_object` 5,000 to 1,283 with two objects): the guards take
  slots inside the tables, below `$80`, where they took anything below `$100`;
- `event_cmd_copy_data` 9,987 to 6,139: the operation reads the data through
  the script's accessor, and its guard turns away a block that runs past the
  script's end or has no length (the original's block move takes the whole
  bank then); no script has either;
- the sliding moves toward an object or a member (`$9E`, `$9F`) and
  `field_position_sprites`/`field_load_and_position_sprites` compared 55 to
  156 states before their entries were changed: the first check the object
  faces one of the four facings (the in-the-way check reads a table of four),
  the second take the object in X only where it is the current one, which the
  high table bits are the current object's (the game always places the current
  object). Their entries now fill the facings with the four and make the object
  in X the current one: the moves compare half their predecessor's states,
  the object guard's share (2,498 and 1,317), and the placing all 20,000
  (loose 13,878), before and after.
- the moves that set off and `field_object_in_the_way`, once the search's
  stray slots became a guard, compared a quarter of the states: the fuzzer
  randomizes the map's object count, which sits before the script. Their
  entries now keep it 1 to 63, and they compare their predecessors' states
  (the object guard's half where an operand names an object).

**The timing gate.** Event's and field's `cost_milli` rows were recalibrated
over this pass's shadow runs, 57 rows (the two of menu's that the calibration
also moved were put back). The largest moves: `field_set_velocity` 4,384 to
6,305 (122 calls in three movies), `field_update_object_sprites` 3,228 to
4,353, the window table choices `field_set_window2_table_0`/`_1` 2,567 to
3,208 and 2,617 to 3,271, `field_update_objects` 4,309 to 4,963, and among
event's the rows that stopped calling out (`event_cmd_play_animation` 5,323 to
4,737). Three entries went over the vblank tail's limit and are in
`tools/timing_exceptions.txt`: `event_cmd_move_to_tile` (2.24% of its calls in
vblank off by 1024 clocks or more) and `event_cmd_move_toward_party_member`
(5.76%), whose time follows the field's motion they now run inline as C over
the game's state, with no machine at hand to name those routines' time, and
`field_object_in_the_way` (1.08%), whose time follows how far its search over
the slots goes. All three write object state in work RAM, and the vblank check
finds none of their writes moved.

**The fresh reviews' must-fix items.** A reader who hadn't seen this account
read the branch against the rubric, which the reviews grew to 48 items, and
rated no file translation; `object.h` was credited as a domain module the
rest uses. What they would not accept for a merge was fixed on the branch:

- *Accesses past a table* (item 28). The resumption search of `$00` walked 512
  levels of `event_saved_positions`, which holds eight; it searches the levels
  below the object's to the lowest now, and the return's guard refuses an
  object with nothing to resume there (and any level past the eight), so the
  original runs its wrap. A call's level and its target's are checked to be
  among the eight too. An empty party place no longer becomes object 64: it
  names no object (`member_object`), the commands that allow one decode
  `there` false and don't look, and the guards leave the ones that don't to
  the original (`member_named`), the wait of `$07` included.
- *The Y/V record written from inside the logic* (items 26, 35, 39). The
  operations no longer see the record or the command's offset. `event/leaves.c`
  runs the command (`event_perform_leaving`) and works out around it, from its
  parameters, the state before (a `note`) and after (a `derive`), what the
  original leaves: the calls' V (clear wherever the call reached the places,
  whose index additions can't overflow), the absent member's Y, the return's
  V, the face-toward's direction overflow, both screen tests' V, the jump's Y
  and V, the launch's Y, the change of location's Y and kept offset, the
  question's kept choices, the drift's Y, the follows' kept offset and the
  story flag's operand (table entries), and the jump's leftovers, which the
  engine made in `follow` before. `copy_data`'s Y and V are its bridge's,
  from the block. What stays said where it happens, through three functions of
  `leaves.h` that take no offset: how a move set off (the step's start is the
  field's, and nothing after the move says whether it stepped, turned or
  stayed), whether it looked for objects in the way on its way (which keeps
  the offset in a cell), and the walking animation's overflow while it lines
  up. The engine takes no pointer from the runner, and the two screen-test
  helpers are one (`keep_screen_edges`, beside `field_screen_test_overflow`).
- *The object encoding in `field/object.h`* (item 18). `event_names_object`,
  `slot_of`, the engine's copy of `object_make_current`, `handler_of`'s own
  read of the table and `SCRIPT_SIZE` are gone; `field_names_object`,
  `field_link_names_object`, `object_current`, `object_current_is_one` and
  `object_make_current` are `object.h`'s; the party's link and the dialog's
  speaker are operations (`party_place_left_empty`, `dialog_set_speaker`), the
  character byte's empty marker is `NO_CHARACTER`, and scaffolding keeps an
  object through `field_object_offset`. The engine counts `FieldObject`s. The
  openings keep `event_object_end` once, as scaffolding, and loop to the
  count; they store the current object as a word, as the original's map
  setup does with 16-bit index registers ($C0:58FB), which the review read as
  a slip: `leave_current_as_a_word` says so.
- *`object.h` as the field's whole vocabulary.* `sprites.c` reads no object
  byte: the layout, the range, the records and their state, and the high bits
  are operations, and the records' state has one definition. The battle's
  placement byte has one name, and a true one (`object_let_the_battle_place`:
  $FF leaves the place to the battle). Solidity is a `bool` (decoded by a new
  operand kind, `FLAG`, which the guard holds to 0 or 1), the layering a
  struct the script's byte decodes into (`layering_of_byte`; a byte with bit 6
  set is the original's), and the facing is decoded once, in storage, with a
  guard (`object_facing_is_one`) where a facing past the four would read past
  a table.
- *`animation.c`.* Half passes are the scripts' unit as well as the storage's,
  and the header says so; "which animation is current" is one function (the
  tick times the last one-off through a held frame, the frame builder reads
  the loop's frames, which the one function says); the frame table for a
  facing takes the decoded facing; and the frame read's overflow is worked
  out in `field/leaves.c`. The file then read with no finding of its own, and
  its first half, with `event/animation.c`'s command, replaced the review
  skill's illustration.
- *Window 2's tables* are named for what they are, the two halves of
  `field_window_edge_lines` read a byte a line (`window2_edge_table`), and the
  comments that called them a copy are fixed.
- *The word clear's spill* is one marked helper for every slot
  (`leave_word_clear_spill`). The game doesn't depend on it: a build that
  skips it, run with the ports in the shipping mode over `new-game`, `field`,
  `events-prison-escape` and `late-omen-zeal` (68,422 frames with their map
  loads), left every byte of WRAM, VRAM, CGRAM, OAM and SRAM identical to the
  original's, every frame; the next slot's state is already 0 when the
  openings run.
- *Small ones.* The in-the-way search keeps the object it measured once,
  after the search (`leave_target`). The flag derivations exported from the
  logic files moved to `field/leaves.c`, beside the wrappers, with
  `field/parts.h` naming the few parts of the motion and the animation they
  work from. `cpu_io.h` says the port and the math units are for the
  original's leftovers, not for computing. The walk animation of a move is a
  statement of its own, not inside the expression only scaffolding read
  (item 40).

The rest of the reviews' findings are in TODO.md with why they wait.

**What they moved.** Shadow mode over the 26 movies: nothing mismatches. Three
entries' totals moved, all to the guards on an empty party place:
`event_cmd_move_toward_party_member` and `event_cmd_follow_party_member` hand
52,898 calls each to the original (a scene following a party of two, where the
third place is empty; 19,223 and 18,073 still match), and
`event_cmd_note_tile_member` 2,735 of 38,500. Every other entry's total is the
same, the calls, layering, solidity and facing guards refusing no call the
game makes.

Fuzzing against `main` (seed 1, the 438 runs) found three things on the way,
each fixed in the port: the one "current animation" function first took an
animation mode past the four as looping where the tick takes it as a one-off
(10 of 2,898 states; each reader keeps its own test of the stored mode now),
the noted facing wrote the decoded facing where the original notes whatever
byte is kept (now the guard's), and through those the engine (10 of 6,823).
Then 0 differ. The entries the new guards narrow fill what the guards
require, so they compare again: the party's places and the operand that
names one (the member commands, about 2,000 of 20,000 each, where the random
operand named a place past the party's three before), the facings (the noted
facing, 2,498; the frame read, 4,089), the call levels (1,289, the refusing
flags sharing the rows) and the solidity operand (10,035). The derivations
look at the state without the machine's time (`vars_peek`), so an estimate
still counts only what the operation does, and event's and field's rows were
recalibrated.

### Phase 3a: scene reworked to the review

The first qualitative review (`.claude/skills/review-port/`) rated three of
scene's files translation -- `program.c` emulated the arithmetic unit,
`circle.c` kept its X and Y in RAM cells and chained the carry through every
edge, `bitmap.c` passed a struct that was the CPU (`carry, overflow, x, y,
bank`) -- and most of the rest mostly. This rework takes each of its findings
and regroups the subsystem by the game. Nothing was added to the burndown, and
the behaviour is the predecessor's: the per-entry shadow totals over the 26
movies (calls, matched, mismatched, interrupted, skipped, abandoned, and the
pages each entry was called on) are identical to `main`'s for every hand port,
not only the scene's.

**The files, by the game.** `title.c` (the title screen: the pendulum, the
logo, the ripple, and the loader's blocks), `race.c` (the jet bike race's
display and its engine's pitch), `race_matrix.c` (the race's generated matrix
code), `script.c` (the sequence scripts), `frame.c` (the NMI, pause, the song,
the task lists), `screen.c` (every scene's first screen, the per-frame uploads,
the program's transfer and the line effects), `boot.c` (starting a scene, its
interrupt handlers, the resource copies), `sound.c` (the program's own sends to
the sound driver and its wait for the song's marker), and what the scenes
share: `sprites.c`, `circle.c`, `scaler.c` (was `bitmap.c`), `math.c`.
`scene.h` says where each part of the game is. The endings have no routines of
their own: they are the program's scripts, using the iris, the scaler and the
arithmetic. `display.c`, `display.h`, `race.h`, `sound.h`, `program.c`,
`nmi.c`, `sequence.c`, `matrix_code.c` and `scratch.c` are gone.

**The arithmetic is C** (`math.c`). The 32-bit divide is `n / d` and `n % d`,
the 8.8 divide `(dividend & $FF00) / divisor`, the multiplies `a * b`, the
divisor search a loop of `dividend / divisor` in locals. Where the original's
way of computing them would come out otherwise, what the game gives it decides
between a guard and a named input, measured by guarding each and comparing the
per-entry shadow totals with `main`'s:
- no scene divides by a 32-bit divisor of 0 or with its top bit set, and no
  unsigned product's middle partial sums overflow 16 bits (which would put the
  overflow a byte too low); the guards hand those to the original, and turned
  away none of the library's calls;
- the unsigned multiply carries the top bit of the byte after its product into
  it, and leaves that byte shifted a place for the next: the title's pendulum
  and the race multiply with it set (848 of the 3870 multiplies of every title
  screen, 32936 over the library, which a guard turned away), so it is a named
  input (`carries_into_the_product`, `shift_the_byte_after`) and stays in the
  logic, since the next multiply reads it; the 8.8 divide's divisor less one,
  one of the things left in that byte, stays in the logic with it;
- the 8.8 divide by 0: the endings make it (637 calls in `endings-beyond-time`),
  and the original's long division, compared over every divisor, comes out
  exact for all of them with 0 counting as 65536 -- so the fraction is 0, one
  line, and the bit-serial division is gone;
- the signed multiply adds the product cell's third byte as the caller left it
  (`magnitude_with_leftover`), a named input.
The dividend cells the divides leave shifted out with the caller's carry, the
multiplier and divider as the original leaves them, and the divisor search's
time (below) are scaffolding. The wrappers derive what the callers read: the
8.8 divide's carry is always clear, the signed multiply's X its first partial
product, the search's X and A the divisor after the one found and its quotient.
The search takes what its caller left in the divider -- the dividend
(`cpu_io.h`'s `hw_dividend`, which costs no time) and the quotient of the
division the caller started -- and divides by the divisors after in C.

**The circle** keeps the outline in locals (`Octant`: x, y, the decision, the
near line) and writes its four cells, and the center's and radius's cleared
high bytes, once, as scaffolding (nothing reads the high bytes: the callers
write the cells as bytes). The original's chained carries move pixels, and they
are two named functions: `edge_rounding` (the top line a line higher, the far
lines' right edge a pixel further right unless their left edge is off screen, a
near line's left edge a pixel in when the far line reached the right of the
screen) and `decision_step` (the midpoint step, with `y - x` one less while the
near line's right edge is on screen, and one or two more near the diagonal).
Closing lines is `close_lines(g, c, first_line, lines)`, a plain fill; a whole
table closed runs three bytes past it in the original, a marked write of its
own (`spill_past_table`); and the lines above a circle whose top is on the
second line stay open, because the original's count for one line would run its
block move through the bank. A table that could reach the cells the original
reads back is refused by a guard (`scene_circle_table_is_clear`); no table the
game or the fuzzing makes comes near them. `scene_circle_draw` takes the
`GameState`, which the menus' one call site now passes.

**The scaler** (`scaler.c`) has no CPU struct and no carry chain. The
original never clears the carry between its sums; two of them move what the
player sees, and they are named: a row's samples start 1/256 of a pixel further
on when its source row lies further along the bitmap than the last row's
(`sample_nudge`), and a row's source is picked 1/128 of a row further on when
the call before left the carry set (the entry's carry, `row_nudge`): the
positions are fixed point, a sample's in 1/256ths of a pixel (`PIXEL_UNITS`)
and a row's in 1/128ths of a row (`ROW_UNITS`). The rest
come from sums that wrap past the end of the bank -- a sample's position along
its row, the plane pointer -- and the entry's guard hands those bitmaps to the
original, with a source limit a row's position compares with either way and a
source whose samples stay in the bank; no movie's call is refused. A sample's
position is then `(k + 1) * step + nudge`, and its time the samples it reads.
The wrapper derives the flags it leaves where the bitmap ends from the previous
end, this one's and whether the rows ran out, and the carry at the row loop's
head from the output pointer; the only record left is the scratch the original
packs pixels in, a memory write through `ORIGINAL_EFFECT`. Clearing what the
previous bitmap left and repeating a row are a plain fill and copy, their time
charged as the block move's.

**The script** (`script.c`) is a table of rows, each a mask and match over the
opcode, a handler, and what the port needs of the state to take the command
(a store in low RAM, a video setting a call expresses, a task that is on its
list). Each handler decodes its operands and calls an operation with named
parameters -- `assign(g, ch, reg, value, add)`, `count_loop(g, ch, counter,
limit, back_to, after)`, `store`, `gosub(g, ch, target, return_to)`,
`queue_sound`, `make_video_setting`, `stop` -- and says how the command ends.
The script pointer's moves across the original's code are named: while a routine
of the program runs, the script stays on the routine's address (a routine can
move it) and goes on past it from there; a store to the script's own pointer is
a jump; a sound goes unsent, and unheard, while a song is on its way to the
driver, which takes one message at a time. The operations leave nothing for
the original: each row has a kind (set, add, loop, ...), and after each command
the bridge leaves, through `ORIGINAL_EFFECT`, what the original's code for that
kind leaves -- the word it keeps in its channel's scratch (an assignment its
register's address, any other command itself), and its carry and overflow, from
a table of the flags per kind worked out from the command's register and loop
counter before and after it (`note_flags`). The flags live in P, so a call-out's
are the callee's with no copy. They are left because they are read: the attract
and title programs run their channels at `$7E:54B4` and go straight on to
`$7E:5777`, whose `$7E:3443` calls the random number mixer at `$C3:02DD`, which
starts with a `ROL A`; and `$7E:62F0` reaches the `ADC #$40` at `$7E:65B3` with the
carry the run left, `$7E:3448` between them saving and restoring P. No code in
either program or in bank C3 branches on the overflow, but the liveness
analysis counts it as read, so it is left too. A set's flags are both clear,
not the original's sum of the channel's page and the register's place they
come from, which carries and overflows on no channel's page. The A a turn ends
with is the ending command's kind's: the script's place after a loop or a jump,
1 after the stop, the plain end of turn's handler's place otherwise. The
video settings a script names by their register are a table of the scripts'
encoding. The wait for the music is `scene_music_status`/`scene_music_reached`
(`sound.c`), with its flags derived in the idle entry.

**Tasks and pages.** `SceneTask` and `SceneTaskList` (the vblank tasks at
`$0940`, the frame's at `$0920`) are types with operations (`scene_add_task`,
`scene_remove_task`, `scene_task_listed`). The review expected a task page
kind for `$7E:5DA7`'s "task's next step"; the page report says it is called
only on `$0A00`, channel 0's page, from a script's call, and the cell it
writes is that channel's register 2, where the title keeps BG1's vertical
scroll (`scene_title_start_ripple`). So there is no task page: the routine
writes a channel register, and its guard asks for a channel's page.

**Vocabulary in the map.** The sixteen bytes the program's shared routines
take their values in are `scene_call`, on the page kind `scene_program` (the
scene's own page, or a sequence channel's): a record of named, overlapping
fields, one per user (the operands, product, quotient, remainder, the 8.8
fraction and the divisor less one, the byte after the product, the transfer's
mode and register, the matrix generator's offsets, the script's kept word);
`scene_nmi_stub` and `scene_irq_stub` name the stubs' jump and target, so
installing a handler is two writes and no bit surgery. Named by finding their
readers: `$62`/`$64` are the title logo's X and Y (the sprite offsets its lists
are drawn at, `$7E:5D01`; the review found "cell 1" and "cell 2"); `$4B` is the
race's engine pitch (the sound driver's command `$89`, the effect voices'
pitch; the review found "presumably a parameter"); the words the old
`shift_history` moved are BG3's second row, which in mode 2 are the columns'
vertical offsets: the title's ripple. `scene_flags`' bit 7 has one definition,
named for both its meanings, and the pause, the song on its way, Start held and
the reset allowed are switches with one storage function each (`frame.c`). The
scene's DMA channel and modes, its two banks of work RAM, its tilemap buffer,
its blank tile and its two VRAM stepping settings each have one definition, in
`scene.h`.

**The display** writes the video service's structs: `(VidMode){ .mode = 7 }`,
`VID_LAYER_BIT(...)` sets, `.cy = -64`; `init_video` is one value, the first
screen every scene starts from, applied by one function; the race's line-effect
descriptors decode into a `LineEffectDescriptor`. The transfers through the
SNES-only escape name their registers (`VID_SNES_VRAM_DATA_LOW` and the rest,
added to `platform/video.h`); their per-call `steps` stay (TODO.md). No
assembly is quoted in a comment.

**Entries named for what they do**: 30 renames, among them
`scene_title_ripple_columns` (was `scene_shift_c00_words`),
`scene_run_frame_tasks` and `scene_run_vblank_tasks` (`_950`, `_940`),
`scene_wait_for_music_loop` (`scene_wait_apu_port3_loop`),
`scene_race_wait_for_tilemap_upload_loop` (`scene_wait_39_clear_loop`),
`scene_script_wait_for_sound` (`scene_script_wait_sound_port2`),
`scene_copy_resource` and `_to_upper_ram` (`_to_7e`, `_to_7f`), and the
loaders' blocks by what they put in VRAM (`scene_title_load_sprite_graphics`
for `scene_set_vram_4000`, `scene_race_load_bg3_map` for
`scene_race_set_vram_6800`, ...). The loaders themselves aren't ported
(TODO.md).

**The sprite writers** are one loader with a `clipped` parameter and no
in-band X: a sprite past the right edge is at X 257, and the word `$9801` the
original stores there only feeds the A its callers read. The loader leaves no
register record: a small list's A already has the list's high byte, and the
clipped entry's wrapper works out A's high byte and the overflow after the call
from the list's last entry, which the guard keeps clear of every cell the call
writes. The lists are read from work RAM (`wram_read8`), and the port the
original streams them through is left where its reads would leave it, once,
through `ORIGINAL_EFFECT` (the title's plane tiles, the same). The sprite table
is two views in the map, `scene_sprite_table` (the $200 bytes of entries) and
`scene_sprite_high_table` (its $20), with `SCENE_SPRITE_TABLE` and
`SCENE_SPRITE_HIGH_TABLE` in `scene.h`, and the port's high-table cursor is the
byte's place in the table, 0-31. Their guard, which
TODO.md asked for, takes a cursor whose sprites stay in the sprite table, a
list clear of the cell the original counts it down in (fuzzing found 2 and 15
lists of 20000 read over that cell), and a clipped list clear of the table.
The fuzz runs start the cursor where each frame's first list finds it; the
guard turns away the random lists too long for the table, about half.

**The frame** (`frame.c`) names the controller's buttons and the reset
combination, and installs its handlers through the stub's fields. Its bridge
sets only what the original's code it calls reads: A the pause word the frame's
work tested (0) at a task list's call, the carry the acknowledge's or the
controller's compare where the tasks see it, Y where the list's copy left it
(through `ORIGINAL_EFFECT`); the pause sound's and the song's calls declare the
rest dead, which the don't-care check verified (the driver's entry saves A,
which liveness counts as reading its high byte, so those calls keep it). A pause
word other than 0 or 1, which the original tests as a word in one place and as
a byte in another, is refused by a guard; no movie makes one.

**Fuzzing against the predecessor** (`tools/fuzz_compare.py`, seed 1, every
scene entry and variant in `tools/fuzz_routines.toml`, 87 runs): the counts
agree for every one. The runs that didn't, over the two rounds, were each the
rewrite's to fix: the unsigned multiply's middle sum (7 and 11 states, now
guarded), the scaler's samples read past the bank into its own scratch (24),
sprite lists read over the cell the original counts them down in (2 and 15),
the divisor search taking its first quotient from the dividend rather than from
the division its caller made (211: the fuzzer's states don't keep the two
consistent, the game's do), and a routine a script calls reading the A the
original loads it with (60 and 64, which the call-out's don't-care set had
declared dead: liveness can't see into the program's RAM routines, so A is set
and no longer declared). The third round's two: a small list read from work RAM
must read each sprite's Y before shifting its high-table bits, as the original
does, since a list can lie over the high table (7 and 3 states); and a loop's
flags must be worked out from the limit as it was before the loop ran, since a
script a jump sends into its channel's page can lie over the counter (1). What the guards turn away shows in the skipped column:
the sprite lists too long for the table from where the cursor stands (9907 and
10074 of 20000 in the tight runs, 15320 and 15785 in the loose ones, whose
cursors are anywhere in the table and whose lists anywhere in low RAM), the
single sprites whose high-table byte is past the table (112 of 20000, loose),
8 of 2000 plane tiles over the scratch, the 32-bit divides by 0 or past
`$7FFFFFFF` (10092 and 10017 of 20000) and the products whose middle sum
overflows (7 and 11); everything each accepts matches as it did. The scaler's,
the frame's and the sprites' runs are narrowed to what their guards take, with
the guard named in each entry's `why`. The plane tiles mismatch in 5 of 2000
states, and the frame's work in 1 of 10024, before the rework and after it:
the predecessor's residue, not the rework's.

**Time: charged, not kept in the logic's shape.** The review's item 36: the
first round kept the circle's and the scaler's block moves byte by byte, and
the divisor search dividing through the divider, because that shape is what the
timing estimate charged. Now a port charges the time as scaffolding, through a
general mechanism (`game/charge.h`, backed by `runtime/port/state_io.c` on
`native_charge_as` and `native_uncounted_begin`/`_end` in `native.c`):
`charge_block_move(g, bytes)` charges a block move's time at the block move
stub's calibrated rate, after a plain `wram_fill8`/`wram_fill16`/`wram_copy`
(`wram.h`), whose accesses take none of the port's own time; and
`charge_as(g, routine, accesses)` charges that many accesses of a registered
routine's work, the running port's own included -- the divisor search charges
three a divisor tried. Sixteen of the scene's `cost_milli` rows moved over the
two rounds: most by a few percent, the ones whose work is now C by more
(`scene_multiply` 6178 to 8688, `scene_title_expand_plane_tile` 3723 to 27517,
`scene_scale_bitmap_row` 2877 to 20318, `scene_draw_circle` 3051 to 17017,
`scene_find_divisor` 6952 to 5195). The circle's worst error in vblank is 11085
clocks, which its vblank-tail exception covers with the new figure
(`tools/timing_exceptions.txt`). The whole gate, every route included
(`--long-routes`), passes.

**The second review's follow-ups** (the third round took its must-fix items,
above; these it accepted as later work, each in TODO.md): the scaler's carry
into the leftover's measure, threaded through three functions, a block move
charged as `bytes - 1` without saying why, and `in_scaler_bank`'s two
arguments whose roles its callers mix; the iris's `mode` byte across the
menus' call, which wants an `IrisShape` and a `leave_outside` flag, and a
`count` local that exists only for scaffolding; one `scene_queue_sound` for the
frame's and the script's copies; the controller's carry derived twice; the
button names, which belong in `platform/input.h`; `SCENE_VBLANK_TASKS` against
the map's `scene_early_tasks`; the script's register encoding and its operand
offsets in the adapters; the divisor search's `tried`, and `divide_8_8` as
`fraction_along`; `race_matrix.c`'s accumulators; the bank `$FE` resource
accessor; `init_video`'s sites and computed `PF_AT`, which wait for
`vid_apply_screen`; the transfers' `steps[]`, the platform's; the title's logo
sweep and ripple, the program's code still; and `game/attract/`, which is the
same program and belongs here (the two headers now point at each other).


### The accessor bounds checks

The generated backing computed `base + index * stride` with no bound, so an
index past a variable's `count` reached whatever came next in work RAM, and
worked only because the storage is still the original's layout. Code-reading
reviews had found four such aliases; nothing found them otherwise. Every
indexed accessor in `runtime/port/vars_ram.c` now checks each index -- the
element against `count`, a buffer's byte against its size, a table's row and
column, a page kind's instance -- and records a violation without changing the
access, so shadow mode compares the same bytes (DISASSEMBLY.md, "The
accessors"). The check is compiled only with `CT_ACCESSOR_CHECKS`: the backing
is built twice (`ct_vars_checked` for `ct_dev`, `ct_fuzz` and `vars_test`,
`ct_vars` for `ct_play` and `ct_drive`), and `game/` is compiled the same
either way. `ct_dev` prints an `accessor out of range` line per variable, and
the gate fails a movie that prints one; `ct_fuzz` prints the same after its
counts, and `tools/fuzz_compare.py` lists them by routine. The deliberate
stray accesses have a sanctioned path: a variable declares `unchecked = "<why>"`
in the map and gets an `<name>_unchecked` pair with no check, which marked
scaffolding calls; `game/wram.h` has no count and isn't checked.

**What the library found.** Over the 26 movies, before any fix, three
variables' families went out of range, in 26, 12 and 2 movies. None was a
map size error, and none traced to generated code or the runtime (which the
checks don't cover: only the accessors are checked).

| Variable, index (bound) | Where | Movies, accesses | What it was | What was done |
|---|---|---|---|---|
| `field_dialog_shading[100]`, `[101]`... (count 100) | `field_fill_dialog_shading` `$FD:D52D`, run by `$FD:C124` | all 26, 448-3696 accesses each | A hidden alias. The copy for table set 1 is a 256-byte block move from the shading's start into the copy, which lies right after the shading's 100 lines, so its lines 100-127 are read from the copy's own first 28. The port read them as "shading lines 100-127". | Made explicit, not stray: `copy_dialog_shading` reads the copy's own lines past the shading's. Byte-identical; the per-entry totals don't move. |
| `field_sprite_records[at 833]`, `[at 836]` (832 bytes) | the 4-sprite placing `$C0:B788`/`$C0:B8CA`, run inline by the draws `$C0:B271`, `$C0:B309`, `$C0:B701` | 12, 27-837 accesses each | The known stray (`leave_stray_fifth_sprite_y`): an object above the screen gets a fifth sprite's Y, one record past its four, which for the last object's records is past the table, in the objects' high bits. A second read the review hadn't named was found with it: the placing's entry leaves the overflow of that same fifth sum in V, reading the fifth record's Y offset. | The deliberate stray kept, through the new path: `field_sprite_records` declares `unchecked` in the map, and both the write and the V flag's read go through `stray_fifth_y_offset` and the `_unchecked` pair. |
| `field_tile_properties`, `_upper_attributes`, `_lower_attributes`: `[column 64]`, and `[row 255]`/`[column 255]` (64x64) | the event commands that put an object on a tile `$C0:4B74`, `$C0:4C27` on their own and inline under the openings `$C0:58DE` | `endings-beyond-time` 2 commands, `events-prison-escape` 18 | A hidden alias, on calls the game makes: a script puts an object that follows the map at tile column 64, or at (255, 255) to take it out of the way, and the original layers its sprite from the tables' neighbours (another row's tile maps, the other attribute table). | A guard (`tile_on_the_map`, in the rows' checks) leaves those commands to the original: 20 real calls, an unported path in TODO.md. `$C0:4B74` goes from 1013 matched to 1004 matched and 9 skipped, `$C0:4C27` from 520 to 519 and 1; the 10 run inline are handed to the original's handler through the table. |

With those, the gate passes with the checks on, and no other entry's shadow
totals move (798 entries compared with `main`'s). The sprite records' new
bound needs each object's layout, which the build's walk to see whether every
draw is its own now reads once and hands to the check and to the records'
work, rather than reading it again: an extra timed read there had moved three
sprite entries past the timing gate's drift limit (+0.7% to +1.3%).

**What fuzzing found.** `tools/fuzz_compare.py` over all 525 fuzz entries
(`main`'s `ct_fuzz` against this one, 5000 states each, then the event and
field subsystems at full length) found no new mismatch, and listed the
accessors random states drive out of range. The clear-cut ones got guards,
each refusing only what the game never makes, and the fuzz entries' domains
were narrowed to match (so the tight variants compare as many states as
before, and all match; the loose variants show the refusals):

| Routines | Out of range | Guard |
|---|---|---|
| the story commands `$1C`, `$53`, `$54`, `$56`, `$58`, `$59` (`$C0:654E`, `$C0:693D`, `$C0:6964`, `$C0:69A6`, `$C0:69C4`, `$C0:69EB`), and the engine running them | `event_memory` (512 bytes) at any 16-bit offset | `story_holds`: the offset's byte or word inside the story memory. The map was right: every event script the ROM can load (`derive.event_scripts`) gives these commands an offset below `$1EE`. |
| the follows `$B5`, `$B6` (`$C0:3546`, `$C0:354F`) | `field_object_*` up to `[236]` (count 64) | the other moves' `can_look_its_way`: the search for an object in the way runs from the object count down, which random states make past 64 |
| `field_unpack_tile_maps` `$C0:A521` | `field_layer_maps`, `field_tile_*` columns past 64 | `fits_the_tables`: each layer at most 64 tiles each way |
| the sprite records' draw, update and placing (`$C0:B309`, `$C0:B701`, `$C0:B788`, `$C0:B8CA`, and the build's own-draw check) | `field_sprite_records`, `field_sprite_frame_data` at offsets to `$7FFF` | `records_in_the_tables`: the object's records, for its layout, inside the records (it was: inside bank `$7F`) |

Three families are left, listed in TODO.md: the tile ahead of a moving object
off the map (the event moves and `field_start_step`), the line effect tables'
builders past a table's 87 bytes, and the event engine reading the script past
its buffer (widening the off-the-script hand-over mismatched on the openings'
path at `$C0:5900`, so it waits).

**Cost.** Measured a movie at a time, the builds alternating: `gato-battle`
in shadow mode took 41.8 s with `main`'s `ct_dev`, 42.2-42.3 s with the checks
and 41.9 s with this tree linked without them (about 1%);
`battle-golem-twins-gameover` and `fair`, with the suite's own arguments, and
`fair` four at a time, were within their run-to-run noise either way. The
whole gate with `--long-routes -j 5` took 31m38s against `main`'s 32m17s run
straight after it, most of it the routes, which run `ct_drive` and have no
checks. Its shadow movies took 393 s against 317 s in those two runs (and 398 s
against 327 s in an earlier pair); the controlled runs above don't reproduce
that gap, so it is left as an open question in TODO.md rather than put down to
the checks.


### Call-outs into targets the machine picks

Scene's rework declared A dead at the sequence interpreter's call to the scene
program's routines (`$C3:0C58` to `$7E:37F3`); liveness agreed, the don't-care
check passed it, and fuzzing found the callee reading A in 60 and 64 states of
its two variants. The analysis did see the callee: the program is a
`[[ramcode]]` image, traced, and `$7E:37F3` is `LDX #0 : JSR ($00F0,X) : RTL`,
a call through the word script command `$A0` stores at `$00F0`, whose 33
targets `derive = "seqscript.call"` finds in every script the game can run.
None of them reads A, Y or V, so for the game's own scripts the declaration was
right. What liveness couldn't see is that the target is the caller's to
choose: the port stores the script's operand at `$00F0` itself, and a script
the fuzzer makes up (its script pointer lands anywhere in the program) names
any address in bank `$7E`. Instrumenting the fuzzer to print `$00F0` at the
call showed the two states that still mismatched calling `$28F6` and `$14A4`,
routines no script names. The promise that unknown control flow keeps
everything live held; this transfer wasn't unknown, it was followed with a set
that was complete for the original and not for a caller that picks the target.

**The analysis.** `Liveness.dynamic_transfer` names the transfers whose
targets the machine decides at run time: a jump table or pointer in work RAM,
a call whose operand the program writes, a table whose extent comes from data
(29 sites; DISASSEMBLY.md, "Liveness"). `Liveness(strict=True)` solves with
each of them keeping everything live, and `dynamic_sites` says which of them a
routine reaches. Liveness as the analysis uses it elsewhere is unchanged: the
generated code, the drafts and `liveness.json` still follow the argued sets,
which are sound for the original.

**The check.** A call-out's set must be dead in both solves; a call whose
target the port computes at a site that is itself such a transfer counts as
reading everything. A set only the trusting solve supports is a finding that
names the transfer, unless the comment beside the declaration carries a
`dc-evidence:` note for a fuzz run that scrambled the set and found nothing
(PORTING.md, "Declarations liveness can't prove"; the check verifies the note's
form, that the routine it names is a registered, listed fuzz entry that
reaches the declaration, at least 100 scrambled call-outs, and 0 mismatched,
and flags a note liveness doesn't need). The second solve takes the check from
about 38 to 65 seconds.

**The evidence.** Fuzzing hid the scene bug until the port stopped setting A:
a port that passes the original's value never shows a wrong declaration. So
`ct_fuzz --poison-dc` scrambles what each call-out declares dead, each register
to a value other than the port's, and runs the callee twice from the same
machine (clean on a copy, then scrambled), so what the callee left alone can be
given back to the port as shadow mode does; comparing the scrambled value
alone, as `keep_untouched` does, restored a byte the callee happened to write
with the scrambled value in 7 of 1400 states (a false mismatch).
`--poison-site BBAAAA` scrambles one site's calls, and the run reports how many
call-outs it scrambled in the states it compared.

**The audit.** Over `game/` (589 entries with a set, 89 stops, 348
call-outs), the old check had 0 findings and the new one 2, both the scene's:

| Declaration | Strict solve | Fuzzed (`--poison-site`, 2000 states) | Resolution |
|---|---|---|---|
| `script.c`, `$C3:0C58` to `$7E:37F3`: X Y N V Z | Y and V read past `$7E:37F6` (`$00F0`) | 2 of 1390 compared mismatch, both in routines no script names | Y and V passed: the port leaves Y as the caller's and keeps V (`note_flags`), as the original has them; X N Z stay, set by the `LDX #0` |
| `frame.c`, `$C3:08F7` to `$7E:39BE`: A X Y N Z | Y read past `$7E:39CF` (the task list at `$0940`) | 0 of 987, but only 4 call-outs scrambled | Y passed: the port never touches it before the call, so it is the caller's, as in the original |

Both are then proven by the strict solve, with no note needed. Fuzzed with the
scene's constraints (`tools/fuzz_routines.toml`, seed 1, 20000 states), with and
without scrambling: `scene_run_sequence` 14061 matched and 0 mismatched (5745
call-outs scrambled), its channel variant 14049 and 0 (5734), and
`scene_frame_work` 10023 and 1 (20 scrambled), the one being the predecessor's
residue the scene rework recorded, the same state with and without. Nothing in
`game/event/`, `game/field/`, `game/battle/`, `game/core/` or `game/menu/` is a
finding: their call-outs reach no run-time transfer that reads what they
declare. Stops and entries aren't held to the strict solve yet (TODO.md).


### Phase 3a: the menus off the machine

`game/menu/` was the second-largest backlog: ten of its twelve files on
`tools/portstyle_baseline.txt`, 0.35 operations a line and 97% of its functions
on the machine. The rewrite takes all of them but `menu/epoch.c` (the Epoch's two
Mode 7 screens and the End of Time's gate, 1,827 lines and a chunk of its own)
to the bar, in the field's order: a domain module written first, then the files
that use it, and each entry's widest don't-care set declared before its wrapper
is written. Nothing was added to the burndown. The subsystem now scores 0.07
operations a line with 40% of its functions on the machine (the entries'
wrappers, the guards, and the bridges round the call-outs); every rewritten file
is under the bar and off the baseline.

**The files, by the game.** The leaves -- `glyphs.c` (a glyph's pixels into
tile graphics, in the eight packings the font uses), `text.c` (the string and
number drawers, the control codes a switch of named operations), `vram.c` (the upload queue
as records), `save_screen.c` (the load screen's slots, a header and a row per
member), `frame.c` and `sound.c` (the waits) -- are plain functions over the RAM
map with their ROM through `menu/menu_data.h`'s accessors. The sprite-object
mode, the half of bank C2 the Epoch's map and the End of Time run on, is a
module: `object.h`/`objects.c` (the sixteen slots: start,
turn round, move, wrap onto the map, and the per-frame run over the slots'
handlers), `sprites.c` (the sprite list the objects draw through, the sprite
script each runs, and the window records' sprites), `wave.c` (the one object
handler that isn't a script: a wave down the screen's lines), and the object
scripts. `display.c` is both modes' screen setup, vblank handler and loaders
through the video service: the shadows sent as named values, the queued uploads
by record, the colour math and window line tables built in C, and the frame's
block move a `wram_copy`. `object_script.h`, and the old `objects.c` that held
the scripts' commands inline, are gone.

**The object scripts are a language** (`script.h`, `script.c`,
`object_script.c`), on `game/event/`'s pattern. `object_script.c` is 33
operations -- `change_byte`, `copy_byte`, `branch_if`, `for_frames`, `move_to`,
`start_handler`, `mark_table_entry`, `scroll_map_for`, `play_song`, ... --
named for what they do to an object or the game, each with typed parameters,
and a table of 83 rows, one an opcode, saying how its operands decode into
them (`SLOT_BYTE(1)`, `CELL(1)`, `OPERAND_WORD(1)`, `CONSTANT(MENU_ACROSS)`).
The families that were a function an opcode are rows of one operation:
`change_byte` 16 opcodes (clear, count up and down, set, set and clear bits,
add and subtract, on a slot byte or a cell), `branch_if` 16, `start_handler` 8,
`copy_byte` 4, `for_frames` 3 (move and animate, animate, wait), `move_to` 2.
`script.c` is the interpreter, as C: decode the row, run it, follow its flow
(next, branch, go to, wait a frame, end), keep the object's place. What reaches
past the object -- scrolling and redrawing the map, the sound queue, a routine a
script names by address -- goes through `ScriptServices`, so the map's port can
move under it. Every handler stays registered (`menu_object_cmd_*`, generated
from one list in `script_bridge.h`), so fuzzing and the ROM's own dispatch still
find each one.

`script_bridge.c` is the machine side, and the only file of the language that
takes a `Snes *`. It runs each command at its handler's time
(`NATIVE_TIME_AS`), pushes the dispatch's return round it as the original's
`JSR` does, and derives what a command leaves for the interpreter's loop -- X,
the carry -- from a table parallel to the language's, through
`ORIGINAL_EFFECT`; the language's rows carry none of it. A routine a script
names gets the registers its handler has at the call: A the address's word (or
its bank), Y the operand's offset, N and Z from A, and the overflow the
interpreter's last `ADC` left, tracked from command to command. A command the
port doesn't take goes back to the original's interpreter at the top of its
loop (`$C2:0F73`) rather than through its table from inside the engine, which
on a random opcode could re-enter the engine from the machine.

**Shadow mode, and what it found.** Each group was committed shadow-clean over
the whole library (26 movies, 0 mismatched). What it found on the way: the
object mode's vblank handler carries on after the sprite list's reset in the
16-bit accumulator that reset leaves, and the port first handed the rest back
in 8 (the NMI's A and P); the wave's phase is the cell at `$01E2` that event's
calls leave (`menu_wave_phase`; TODO.md); the tilemap row copy sets the data
bank to `$7E`; the upload queue's Y is a byte index, so an 8-bit index register
truncates it; the sprite hiding the vblank handler does before the objects' run
leaves Y a byte and the carry and overflow clear, which the handlers read.
Against `main`'s per-entry totals, every menu entry has the same calls, and
every one compared matched, but for these moves, each explained:
- `menu_draw_save_slot`: 1,139 compared of 15,708 (was 15,707): the load
  screen draws its slot as C from `menu_show_save_slot` (`NATIVE_TIME_AS`), so
  the 14,590 inside it are inside that entry's comparison;
- the commands that call out mid-command (`call_routine` 96,028, `sound_18`
  228, `redraw_map` 36, `play_song_unless_flag` 8, `play_song` 1,
  `call_routine_long` 2, and 2,699 of `scroll_map_for`'s 9,093): the
  interpreter runs them as C, so their calls are inside
  `menu_run_object_script`'s comparison rather than compared on their own;
- interrupted calls: `menu_run_object_script` 9 (was 3), `menu_object_nmi` 1
  and `menu_show_save_slot` 1 (were 0): an interrupt landing in a longer or
  shorter native stretch; the paths are the ones the library compares
  elsewhere, and the fuzzing below covers each entry alone;
- `play_song` refuses 8 of its 9 calls, as it did before: the game reaches it
  from `play_song_unless_flag`, not the dispatch, and the interpreter runs
  both as C.

**Fuzzing** (`tools/fuzz_routines.toml`, the menus' section: 196 entries on two
states, the field menu open and the Epoch's map). `tools/fuzz_compare.py`
ran every menu entry, rewritten or not, against `main`'s ports with the same
seed and domains: 197 runs, 12,863 of 430,672 compared states mismatched
before and 12,362 of 258,516 after. All but 105 of the after's are the
predecessors' own, on the same states in `epoch.c`, `map.c`, the irises of
`object_commands.c`, `menu_init_video_memory` and
`menu_window_set_line_effect` (those of `map.c` and the irises are gone since:
below, "After the review"); the 105 are the two accepted divergences
(`menu_draw_text`'s 97 and the load screen's 8, below). Fewer states are
compared after because the guards turn away what the game never makes, which
is the point of them; where that left an entry with few or none, the domain
was reworked so its guard takes the states (the load screen's summaries, both
upload queues' records, the command script and sprite script built in work
RAM, the upload buffer where the game keeps it), and the animating commands
run twenty thousand. Setting it up took a toml section of 197 entries on two
base states, and the 16-area limit of `ct_fuzz` is why the command entries
pack their script pointers as repeated bytes (`$7E:1C1C`, `$7E:1D1D`).

What the fuzzing found, each fixed in the port or refused by a guard:
- the glyph line takes its byte index as a word, as the original does;
- the object queue's records at an end past `$F8` run on past the queue, as
  the original's page-relative writes do (`_unchecked` accessors);
- the number drawer read a table offset the menus never use, its own cells,
  the I/O registers and the stack (the conversion pushes its place in the
  number as it reads each byte): the predecessor's 223 mismatches, now refused;
- a sprite script's jump forward holds the script on the jump: the original
  tests the offset's sign where it means to test the step, so only a jump back
  goes on (the port had it go on either way);
- a sprite script inside the objects' slots changes under its own reading, and
  one with a command past the eight jumps into code: both refused;
- the object mode's vblank handler writes its frame flag through the page, so
  a call-out that comes back on another page or data bank goes to the
  original; the menus' handler the same;
- the map's commands set the step once for both planes, the second plane
  scrolling by what the first's scroll left in the cell;
- the load screen: a slot drawn without its header clears the era label's
  width (the original clears the row count a word wide, over the width after
  it), and a portrait whose row or character the portrait's guard would
  refuse is drawn by the original's; the menus' upload queue refuses sources
  over the top of the stack, where the original saves its registers first.

Two divergences are accepted, on inputs no game state makes: a string's
control code past `$0C`, or one included deeper than four, draws nothing where
the original jumps or recurses (97 of 1,503); and the load screen's member
rows keep their own place where the original reads it back from its cells
after the window list and the portrait, which a list run over random memory
changes (8 of 3,394). The map's strips parted from the original's where the
upload buffer was near the bank's end, in `map.c`'s port before this rewrite
and in its first pass, so the command fuzzing keeps the buffer where the game
has it; the map module's guards now say so.

**The guards refuse only what the game never makes.** Each is a claim about the
game, checked against the library's totals (none refuses a real call but
`play_song`'s, above): numbers read from the I/O registers or open bus, from
the number drawer's own cells or from the stack, or converted from a table
offset the menus never use; uploads from the I/O banks; fills and block moves
over the stack or the move's own stub; a vblank call-out that comes back on
another page or data bank; a wave entered for another slot; a colour math list
without its end; a script outside the ROM's banks and work RAM, a command
naming the interpreter's own cells, a sprite list not as the mode lays it out,
and an upload or map command with the object queue off a record or the upload
buffer out of work RAM; and a sprite script that doesn't read as the language
where a command runs it (every command it can reach one of the eight, outside
the objects' slots, before it stops, holds for good or goes round). That last
one first refused 12,610 of the interpreter's real calls: objects that never
animate have no sprite script at all, and a show or wait of no frames holds its
script for good with the next script's bytes after it. It is now asked only by
the commands that animate, of the script they run, and stops at a hold; it
refuses none. Where the load screen draws a portrait inline and the portrait's
own guard would refuse it, the original's draws it.

**Don't-care declarations.** The entries' sets were widened to what
`tools/dontcare_check.py --list` offered before their wrappers were written,
and the check agrees with every one (0 findings, the strict solve for
call-outs included). The window command lists' call-outs in `save_screen.c` declare the
carry and overflow on a poisoned fuzz run (`dc-evidence`: 500 states, 3,340
call-outs scrambled, 0 mismatched); the idle waits stop with `m_stop_at` at
their loop's head.

**After the review.** A qualitative review by a reader who hadn't seen this
section rated two files translation, both outside the first pass because they
were under the style bar: `map.c` kept its loop counters in direct-page cells,
multiplied a row by 96 with the original's swaps and shifts and queued its
strips with its own copy of the queue's code; `object_commands.c` emulated
`ADC` with a carry-and-overflow struct and named routines by their addresses
(`clear_7f0062`, `step_28`). Both were rewritten:
- `map.c` is a module over the game's state: `scroll_plane(g, plane, axis,
  step)` moves a plane a pixel at a time and builds each strip that comes into
  view from one table of strip shapes (a column down the map, a row along
  it), `redraw_plane` builds and sends the eighteen blocks, the strips are
  queued through `vram.h` (which gained `MenuObjectUpload` and
  `menu_queue_object_uploads`), the plane tables come from `menu_data.h`, and
  a row starts at `row * MAP_COLUMNS`. The original's cells get their final
  values once, as scaffolding; the thin entries read the plane and step cells
  and set X as the scripts read it. Its guards (the page, a plane that
  scrolls, the tiles on the map, the buffer in work RAM above low RAM, room
  in the queue) refuse none of the library's calls.
- the per-line effects and the irises moved to `menu/line_effects.c`, which
  the wave shares: a slot's table set up in one place, the tables' addresses
  named, the irises' copies as loops over lines. `object_commands.c` keeps
  the scene routines, named for what they do (`read_scene_mode`,
  `clear_scene_flag`, `keep_song`, `bob_second_plane`, `accelerate`, `drift`,
  `load_scene_graphics`), as plain C over `object.h`'s place and velocity:
  a sum is `a + b`, and the only carries are the ones the scripts read, derived
  in the wrappers. Every entry declares all but X and the carry dead (the
  liveness analysis agrees), and the story byte at `$7F:0062` has a name in
  the map, if not yet a meaning.
- `epoch.c`'s gate matrix tables have their name back.
Shadow totals over the library are unchanged, entry by entry. Fuzzed against
`main`'s ports, same seed: the map's entries on a domain their guards take
(scroll across 0 of 1,989, down 0 of 1,012, the redraw 0 of 2,000, the block
upload the predecessor's 2 of 1,998); every bank C6 entry 0 mismatched, the
irises on a new domain (their centre and radius on the screen), where the
predecessor's squashed irises mismatched 11 and 9 on radii under 2 that the
guard now refuses. Fuzzing found one ordering: the block upload's entry wrote
its VRAM cell after sending the block, where the original writes it first,
which only a buffer on the direct page can see.

**What is left** is in TODO.md, "What menu's rewrite left": `epoch.c`; what
the review rated "mostly"; the two accepted divergences; the save RAM flag
read through the ROM accessor; and the glyph drawer's font addresses, which
come from the still-original text engine.

**Timing.** The rewritten ports make far fewer bus accesses than their
predecessors, so the menus' measured `cost_milli` rows were recalibrated from
shadow runs over the library (`tools/timing_gate.py --calibrate`), and only
those rows were taken. Two ports do in a step what the original does at a
length that follows the data, and charge the difference (`charge.h`, through
`ORIGINAL_EFFECT`): the number conversion, bit by bit with an add for each bit
set, which left `menu_draw_text` 78,077 clocks off at worst before it was
charged; and the sprite list's drawing, per node and per sprite dropped across,
dropped down or shown. Two entries went into `tools/timing_exceptions.txt`
with their numbers: `menu_draw_sprite_nodes` (2.97% of its 236 calls in vblank
off by a line or more, at most 3,416 clocks, writing the sprite table in RAM)
and `menu_object_cmd_animate_for`, which runs its object's sprite script
inline (1.54%, at most 2,982 clocks).

### Phase 3a: the controller off the machine

`game/core/input.c` was core's one file on `tools/portstyle_baseline.txt`
(0.54 operations a line): the per-frame input work the menus, the field and
battle share, four registered entries -- `input_update` `$C2:84D2`, which saves
every register and calls the other three; `input_read_joypad` `$C2:84EC`;
`input_map_buttons` `$C2:8545`; and `play_time_tick` `$C2:85AE`. It was the
original's instructions with the registers carried through them (the
controller's two bytes shifted and `TSB`ed together, the assignments `ROR`ed
into their byte eight times, the clock's digits walked with X, and the wrapper
calling its three sibling ports through emulated `JSR`s). The rewrite is 0.08
a line, with only the entries' wrappers on the machine.

**The file, by the game.** Buttons have names now, in two orders: the
controller's (`PadBits`, `IN_BUTTON_*` in `platform/input.h`, where scene's
`frame.c` takes its Start and every later subsystem finds them) and the game's
own (`Buttons`, `BUTTON_*` in `core/input.h`, the order of the words it keeps
them in). One table says where each button goes from the one to the other, and
another which of the game's buttons each of the player's eight assignments
stands for; a set goes through the assignments by testing each group whole, and
the direction pad's byte passes through. The reset combination
(`RESET_BUTTONS`) is defined once, in `core/input.h`, for both it and the
scenes. The repeat is one function: buttons held through repeat when the
countdown runs out, and it starts again from the mode's delay. The clock is a
`PlayTime` of six digits, advanced by `count_up` in a chain that stops at the
first digit that doesn't roll over; running past its last digit marks it.

**The entries.** Each declares the widest don't-care set the liveness analysis
accepts: A added to the controller read; A, D and the data bank to the mapping
and to the clock, so neither wrapper sets anything but the widths; and N and Z
to `input_update`, whose closing `PLB` sets them from the caller's data bank.
`input_update` runs the three parts as C, each at its routine's time
(`NATIVE_TIME_AS`), leaves its caller's registers alone rather than saving and
restoring them, and charges the original's pushes and pulls as scaffolding
(`charge_as`). Only where the controller read stops -- at the original's wait
while the console's automatic read is still busy (`$C2:84F9`, the item in
"Timing without a beam") or at the reset (`$C0:FF00`) -- does it build the
frame its saves leave on the stack. Both stops declare their own sets, now
wider (the wait: A's high byte and the overflow; the reset: everything but Y
and the widths). The mapping is guarded to the controller's page, which it has
in all 651,497 of the library's calls, so the guard refuses none.

**Measured.** Every call in every movie matches: 651,497 `input_update`,
686,666 `input_read_joypad`, 651,497 `input_map_buttons` and 686,666
`play_time_tick` (interrupted 3, 1, 2 and 0). Fuzzed against `main`'s ports with
`tools/fuzz_compare.py` (the four new `core` entries in
`tools/fuzz_routines.toml`, on the `menus` state), seeds 1 and 2, 20,000 states
each: 0 mismatched before and after, for all four, none skipped. The four
`cost_milli` rows were recalibrated from the library's shadow runs and only
they were taken; the read and the mapping now make far fewer bus accesses
than the original's instructions, and the clock, which reads and writes all
six digits, more.

### The absent party member and the object off the map

Two guards from event's and field's combined pass refused calls the game
really makes, so the original ran them, and a build without the original has
nowhere to send them (PLAN.md, "A guard becomes an assertion"). Both are
ported now: what the game does in each case is decided in its own terms, and
what the original reads past its tables to get there is one marked piece of
scaffolding each, which the native-shape build compiles out.

**A member who isn't in the party.** Scripts written for a full party address
its second and third members whether or not anyone is there. The party keeps
an empty place as a link to none, `$80`, and the original takes that byte for
an object's offset like any other: the slot one past the object tables, whose
role is object 0's script position's low byte, whose position is object 0's
vertical position and horizontal velocity. In the game's terms the member is
nowhere: the language has an operand kind for a member by their place
(`MEMBER`, `event/script.h`), and the commands that take one ask for them as
`Someone` (`event/party.h`) -- whether anyone is there, whether they are
disabled, where they stand. A walk or a follow toward nobody is over at once,
as toward a disabled object; noting the tile of nobody leaves the variables as
they were. For the gate, `member_in_place` fills in the original's phantom
through `ORIGINAL_EFFECT(see_what_the_original_finds(...))` (`event/party.c`),
which reads the slot through the unchecked accessors of the role and
position (`field/object.h`, `object_past_the_tables`; `unchecked` in the map
says why). Over the library the phantom mostly reads as disabled, which is
what the game means; 3 calls in each of the stand-in objects' 5,000 (the
objects for members 1 and 2 in the first maps of a new game, out of view)
catch object 0's script at an offset whose top bit is clear, and set off
toward a tile that isn't anywhere, and the ending that notes an absent
leader's tile every frame has nothing read the variables before the map
changes. Neither shows. One read comes later in the original than in the
port -- lining up, it stops the walker before reading the other's position,
which for walker 0 zeroes the phantom's fine vertical position -- and the
scaffolding function makes that correction too. The guard now refuses only an
odd marker, which would read the tables out of step and which the game never
makes. `$24`, `$A9` and `$07` (waiting) still take a member's object without
asking, and leave an empty place to the original; the game makes none of
those calls (TODO.md).

**An object off the map.** Scripts put an object that follows the map at tile
column 64, or at (255, 255), to take it out of the way, and the original
layers its sprite from the bytes past the attribute tables' rows (column 64's
upper attributes are column 0's lower ones; (255, 255) reaches bank `$7F`).
Off the map there is nothing to layer against: `object_layer_as_the_map`
keeps the priorities the object had, and `ORIGINAL_EFFECT(layer_as_past_the_map(...))`
(`field/object.c`, through `map_tile_attributes_past_the_map` in
`field/layers.c` and the attribute tables' unchecked accessors) does what the
original does. The guards on `$8B`, `$8C`, `$8D` and `$8E` are gone, and the
map's openings run those commands as C too. The field's own step onto a tile
off the map (`field_start_step`) is still only reached by fuzzing (TODO.md).

**What moved.** Shadow mode over the 26 movies, against `main`'s logs: no
mismatch, and every call these guards refused is compared.

| Entry | Calls | Refused before | Refused after |
|---|---|---|---|
| `event_cmd_move_toward_party_member` | 72,121 | 52,898 | 0 |
| `event_cmd_follow_party_member` | 70,971 | 52,898 | 0 |
| `event_cmd_note_tile_member` | 38,500 | 2,735 | 0 |
| `event_cmd_stand_on_tile` | 1,013 | 9 | 0 |
| `event_cmd_place` | 520 | 1 | 0 |

Every call of each matches. (The 10 commands the openings ran inline were
handed to the original's handler through the table before, and are the
openings' own C now.)

**Fuzzing.** `tools/fuzz_compare.py` against `main`'s `ct_fuzz`, seed 1,
20,000 states each, over the 60 entries these touch: 0 routines differ in
mismatches. Each member command has an `absent` variant with every place of
the party empty and its marker any byte from `$80` (odd ones refused): `main`
compares none of those states, this tree about 2,400 each, all matching; the
tile commands have an `offmap` variant with the tile from column 64 on, where
`main` compares 4,875 and this tree 9,987, all matching.

**Timing.** The member entries' time now includes calls that were the
original's, and reading `Someone` reads the fine position too, so their rows
were recalibrated. The search for an object in the way, which the moves run
inline, charges the original's per-slot work the port doesn't do
(`field/motion.c`, `charge_search`: accesses for each slot passed, each in
view and each measured, at `field_object_in_the_way`'s own rate, through
`charge_as`). That took `field_object_in_the_way`'s mean error from 69% to
40% of its time, and the vblank tail of it, `event_cmd_move_to_tile` and
`event_cmd_move_toward_party_member` from 1.08%, 3.09% and 1.23% of their
vblank calls off by 1024 clocks or more to none, so their three exceptions
left `tools/timing_exceptions.txt`. `event_run_init_scripts` keeps its own:
its error follows its walk over the slots, which this doesn't touch.

### Phase 3a: the rest of core -- palette animation, animated tiles, the block move, the display

After the controller, the rest of `game/core/` was off the baseline but read
as translation: `palette.c` took the machine in every function, reached its
slots as byte offsets through the original's Y, used the hardware multiplier
to compute and re-read its scratch pointer every color; `tile_animation.c`
did offset arithmetic into the slot records and computed its call sites;
`memory.c` was the block move as `m_mvn`; `display.c`'s blanking mixed its
register work into the logic. All four are rewritten; `sound_driver.c` is
left, since it is the original driver's upload protocol and goes when the C
sound driver becomes the default. Core scores 0.03-0.08 operations a line
per file, and only the entries' wrappers and guards take the machine.

**Palette animation** is a module over the slots: an `AnimationType` for what
a slot does (cycle frames in order or from a table, rotate either way, fade
or pulse toward white or black), named operations (`show_frame`,
`rotate_colors`, `fade_step`, `pulse_step`, `restore_colors`), and the
fourteen fade modes as two tables of what each changes (`FadeChannels`: the
channels it scales, those it keeps, and the byte of the working color the two
one-byte modes leave). A channel is scaled in C at the place in a byte where
the original holds it (blue two bits up, green three), since that sets the
rounding the player sees. The location's animations load from the ROM's sets
through a `SetStream`; `palette_data.h` takes the game's state. What the
original leaves for nobody but shadow mode -- its scratch cells, its long
pointer, the multiplier's operands, the work RAM port's address -- is written
once, as scaffolding, from a table parallel to the game's (`FadeLoop`), and
the handlers' X comes from the slot afterwards in the wrappers (`run_end`,
with `palette_animate` noting which slot ran last). The dispatcher and the
fades keep their time per handler and per mode's loop through a new
`TIMED_AS` (charge.h),
and a mode's loop charges per color what the original spends there on its
scratch and the multiplier, which the port does once (the scaffolding's own
accesses uncounted: `charge_uncounted_begin`). The multiplier's first operand,
which a mode's loop is entered with, is read through `hw_multiplicand`
(cpu_io.h). Each entry is guarded to the slots the game makes (a count of at
least 1, the colors in the palette, a rotation's and a frame's count under
128, a frame table in the cartridge; a mode's loop entered with X its slot's
first color): the original's loops take a count of 0 as 65536 and run on past
the palette, which no game state does. `re/ram.toml` gained `frames_bank`.

**Animated tiles** send a slot's current frame through `vid_send_vram`, with
each slot's copy of the original's code named at its call, and the stack
bytes the original's saves leave are no longer written: a guard refuses the
one state that could send them (a slot reading its tiles from the top of the
stack). **The block move** is a copy through `wram.h` charged per byte at the
stub's own rate, with the registers an `MVN` leaves (`m_leave_mvn`); its guard
also refuses a move over the stub itself. **The display**: blanking and
stopping is a function of the platform, and both entries declare what their
callers don't read (A, and the power-on block's direct page and registers at
the boot), so the wrappers set only the data bank and the interrupt mask.

**Measured.** Over the library every call of every entry matches with the
same counts as before, so no guard refuses a call the game makes. Fuzzed
against `main`'s ports (`tools/fuzz_compare.py`, the `core` entries in
`tools/fuzz_routines.toml`), seeds 1 and 2, 20000 states each: the handlers,
modes, loader, clear, the display entries and the tile upload 0 mismatched
before and after (the tile guard refusing 64 and 59 states that read the
stack); `palette_animate` 0 where `main`'s port mismatched 47 and 44 (a frame
slot with a count of 0 over its own return address), refusing the count-0
states; the block move 0 where `main`'s mismatched 14 and 16 (moves over the
stub), refusing 67 and 74. `palette_animate` and `palette_lighten_all` are in `tools/timing_exceptions.txt` for their vblank tail (3.9% and 3.6% of vblank calls off by a line or more, writing the working palette in RAM). The rows of the rewritten entries were
recalibrated, and those whose work is charged at the block move's rate,
which the rewrite moved: `scene_draw_circle`'s two and three of the Epoch's
(`menu_epoch_build_map`, `menu_epoch_build_gate_window`,
`menu_epoch_time_dial_object`); the time dial's setup call keeps a vblank tail
of 1.58% on that account, and is in `tools/timing_exceptions.txt`.

### Phase 3a: the menus' follow-ups and the Epoch

The menus' rewrite left two things (TODO.md, "What menu's rewrite left"): what
its qualitative review rated "mostly" in nine files, and `menu/epoch.c`, the
last menu file on `tools/portstyle_baseline.txt` (0.16 operations a line over
1,827 of its 2,212 lines, every function on the machine). This pass takes both;
`menu/display.c`'s findings went with the platform pass that landed beside
it (PLATFORM.md, "The services' gaps the reviews found, closed").

**The follow-ups.** Each became what the review asked for:
- `menu/script_bridge.h`: every command's entry is named for its operation in
  `object_script.c`, and where one operation serves several opcodes, for what
  the row fixes (`menu_object_cmd_forget_everything`, `..._change_byte_slot_add`,
  `..._branch_if_cell_at_least`, `..._play_effect_on_voices_67`); the bridge's
  per-opcode facts -- what a command leaves in X and the carry, which of the
  original's routines it calls and from where, how many records it queues --
  are one table parallel to the language's, and the scroll's and the sound
  queue's call sites are picked by the row's kind of call, not by opcode
  literals. `tools/disasm/objscript.py` takes the same names.
- `menu/object_script.c`: the look's bits are `object.h`'s (`MENU_LOOK_*`, one
  definition the sprites use too), and `set_look` takes the part it replaces;
  the story flags that hold the layer over the map still, and the sound
  driver's commands (`SOUND_PLAY_SONG`, `SoundEffectVoices` in
  `game/sound/sound.h`), have names; the palette loader's row and hand-over
  flag are fields of the slot (`palette_row`, `palette_hand_over`); and the
  language's parameters are named for what they are (`x`, `y`, `pixels`,
  `fraction`, `scene_script`, `effect`, `pan`, `song`), so no operation takes
  a `word`. The three operations that took the whole command
  (`copy_map_tiles`, the call, the sounds) take their parameters.
- `menu/sprites.c`: the sprite scripts are read by a table of rows (what a
  command does, how it changes a byte, how long it is), decoded once into
  named operands; the interpreter and the well-formedness walk share it.
- `menu/vram.h`: `menu_queue_object_uploads` is the only way into the object
  mode's queue, `MenuObjectUpload`'s port a `MenuVramPort`.
- `menu/save_screen.c`: the portrait takes its row, character and tilemap as
  parameters, read where the rows' command list left them, and the load
  screen's call of the slot's drawing names its return.
- `menu/text.c`: a control code's operands are decoded by its row's layout
  into named fields.
- `menu/map.c`: the scroll returns nothing; its entries derive X from the
  plane's final scroll and the queue's end, and the last strip's working
  values go through one scaffolding record. The redraw takes the platform and
  times the block uploads through `TIMED_AS` (`charge.h`, new: the tag
  `NATIVE_TIME_AS` gives code that has the machine, for code that hasn't).
- `menu/line_effects.c`: the irises' bars and the capsule's gap are line
  fills (`wram_fill16`, the block move charged), the squashed iris walks lines
  from its centre, the slots' tables are named in the RAM map
  (`menu_slot1_line_values` and five more, the sprite-object mode's own), and
  each copy of the starters programs the channel from its own literal site. A
  slot's small table, its channel and its enable bit are operations the time
  dial and the layer's wave now use too (`menu_lay_out_line_slot`,
  `menu_program_line_channel`, `menu_turn_on_line_slots`).
- `menu/object_commands.c`: the routines the world maps' scenes call, by
  purpose (the scene's state, the song, motion, pictures, and now three that
  put objects on the map at random); the Epoch's area check is in
  `epoch.c`; the scene mode's bits and the loaders (`..._load_mode7_graphics`,
  `..._load_second_mode7_palette`) are named for what they are.

**`menu/epoch.c`** is now the Epoch's screens alone, rewritten on the object
mode's module and the line effects: the map view (the world map sampled a
colour a tile, drawn as a picture a pixel a map tile across, laid out on the
plane; a cursor that walks its path turning and zooming it; three markers; the
blue gradient) and the time dial (the gate's window drawn from three turned
corners, the spin's double-buffered cosines and sines, the dial through its
periods, seven digits, the gate's graphics). Its handlers are functions over
the slot's named fields (`angle`, `target_angle`, `turn`, `period`, `zoom_x`,
`path_step`, new in the RAM map) returning what the object did this frame,
from which the entries set the carry and the accumulator's width the
original's path leaves. What the original leaves in the mode's direct-page
cells, the multiplier, the divider and the work RAM port is written once, at
the end, by `leave_*` scaffolding: 26 of the file's cells, worked out from the
predecessor's register-level port. Three things moved out: the four objects
that move the layer over three world maps (`menu/top_layer.c`, new:
`menu_top_layer_slow_drift`, `_drift`, `_wave_drift`, `_wave`, which the
predecessor called the gate's, as the RAM map did the wave's buffer and table,
now `menu_top_layer_wave_lines`, `_at` and `_table`), the three routines that put a scene's objects
on the map at random (`object_commands.c`: `menu_object_place_under_the_view`,
`_under_the_stretch`, `menu_object_give_random_velocity`), and the End of
Time's gate screen, which stays in `epoch.c` as its own small section. The
sibling routines the screens called through their ROM addresses are C calls
now (the object starts, the sprite scripts, the uploads, the window, the
multiply). The four that call out themselves (the map view's graphics and
its build, which call the menus' loader and the world map's, and the gate's
graphics and its stream loader, which call the decompressor) are still called
through the machine, as the original calls them: each is an entry of its own,
which shadow mode compares and the timing gate measures on its own, where run
inline their time would fall into one stretch of the screen's. `epoch.c`
scores 0.02 and is off the baseline.

**The video service's new calls.** The platform pass that landed alongside
(PLATFORM.md, "The services' gaps the reviews found, closed") left
`epoch.c`, `map.c` and `line_effects.c` on the older forms; they are on
`vid_send_vram`, `vid_send_vram_half` and `vid_set_line_slot` now, and
their 25 sites' write orders are in `runtime/platform/snes/encodings.c`.

**Shadow mode.** Over the 26 movies, against `main`'s logs before the pass:
no mismatch, and every menu entry's calls, compared calls and refusals are
the same, entry by entry. Two guards refused real calls on the way and were
narrowed to what the handlers read: the gate matrix's (the slot in X is a new
object at the cursor path's end, not the running one) and the digits' (a
hidden digit's sprite script is whatever its slot held, and it shows nothing
until it is given one; 4,823 of 6,636 calls were refused until the guard
checked the script it would run). The map view's graphics and build, the
gate's graphics and its stream loader went through a stage of running inline,
where shadow mode compares them only inside their screens; they are entries
again, compared on their own.

**Fuzzing.** `tools/fuzz_compare.py` against `main`'s `ct_fuzz`, seed 1, over
the menus' 197 entries: every entry but one has the same counts before and
after or fewer mismatches after. `menu_epoch_load_gate_stream` mismatches 16
of 173 compared states against 17 of 174 before, the same states: with a
random bank the decompressor reads through I/O space, where open bus holds
what the interpreter's fetches left (PORTING.md). The differences fuzzing
found in the rewrite, all fixed in the port: the map matrix takes its sine's
angle from the object in X (the cursor, or the object it has just started)
and the rest from the cursor, as the predecessor had found; the map
picture's tile reads both palettes before drawing either pixel, and the last
tile group it looked at is the last one drawn, not the last on the map; the
path's zooms and the map view's graphics number are read with the bank's
24-bit carry; the gate tilemap's row table is read as a word, its low byte
kept, which matters where the table is pointed at hardware registers; the
dial and its window draw into the tables the dial's first step put in bank
`$7E` or `$7F`. New constraints: the dial's window bank, the row upload's
table in ROM and rows in bank `$7F`, and a slot running for the gate's
graphics (which counts the digits' periods in it).

**Timing.** The rewritten entries were recalibrated (`menu_epoch_*`,
`menu_top_layer_*`, the three placements, `menu_start_gate_screen`), and
nothing else. The inlined siblings are named with `NATIVE_TIME_AS` where
they have the machine and with `TIMED_AS` (`game/charge.h`, new) where the
logic has only the game state; block moves and work RAM fills charge the
original's per-byte time (`charge_block_move`), and the gate window's drawing
charges its per-line accesses. That took `menu_epoch_time_dial_object`'s
vblank tail from 14.24% of its vblank calls off by 1024 clocks or more to
0.53%, and its worst from 45,506 clocks to 4,271, so both its exceptions left
`tools/timing_exceptions.txt`; `menu_epoch_build_gate_window`'s tail went from
88.8% to 2.0% and keeps its exception, its time following the triangle's
lines. The load screen's portrait is timed as before: the check whether it is
the port's to draw stays outside its `NATIVE_TIME_AS`, since inside it charged
the portrait's time to the rows it hands to the original.

### Phase 3a: the battle off the machine

`game/battle/` was the largest backlog: twenty of its twenty-seven files on
`tools/portstyle_baseline.txt`, 3,367 operations in 10,310 lines (0.33 a
line), 99% of its functions on the machine and 53 names shaped like addresses.
The rewrite takes every file to the bar. The subsystem now scores 0.02
operations a line with 31% of its functions on the machine -- the
entries' wrappers, the guards, and the bridges round the call-outs to the
original's code that is still generated -- and none of its files is on the
baseline. It was done in six groups on branches cut from `battle-3a` and
merged back one by one, each shadow-clean over the library before its merge:
the combatants, timers, arithmetic, scripts, main loop, handlers, waits and
sound first (they set the pattern the others follow), then the objects'
drawing and effects' motion, the screen effects, the windows, the combatants'
animation and graphics, the setup, and the frame's display work.

**The foundation.** `battle/battle.h` is the subsystem's vocabulary (the
eleven battle slots, the party and the enemies, where a combatant stands);
`battle/entries.h` holds every registered entry's prototype, which is what the
old `battle.h` was. `battle/bridge.h` is the machine side, and says so: the
guards the entries run under (the battle's page, D = 0 with the data bank at
`$7E`; the sprite drawers' page `$1D00`; a combatant's stat block in X; A's
high byte clear where the original indexes with all of A) and `battle_call` /
`battle_call_long`, the one way a port calls the original's code: the call with
its dead set (which `tools/dontcare_check.py` reads as it reads `m_jsr_dc`),
and a check that the callee came back as the battle runs (8-bit A, 16-bit index
registers, binary, the same page and bank), stopping at the call's return for
the original to carry on where it didn't. `battle_run` does the same for a
sibling port run as the original's `JSR` runs it, its return pushed. Each
group's shared concepts have a header of their own: `combatant.h` (a
combatant's statuses as a set, the kept ones beside them), `timers.h` (the
thirteen timer kinds: run, stopped, followed, run out), `arith.h`, `effects.h`
(the eight effects, the eight anchors, the movers the paths move, the effect
records' encoding), `animation.h`, `window.h` (a tilemap cell, the layouts, a
member's line, digits, the menu's "nobody"), `screen.h`, `frame.h`; and every
ROM read is a named accessor in a `*_data.h` (`sprite_data.h`,
`animation_data.h`, `window_data.h`, `screen_data.h`, `setup_data.h`,
`frame_data.h`), on `game/rom.h`.

**The files, by the game.**
- *The fight* -- `combatants.c` (was `stats.c`: a combatant's statuses followed
  every pass, its low HP noted, its script restarted or its turn stopped as its
  statuses say), `timers.c` (the timers counted down and the run-out ones
  handed to their kinds' handlers through a C table), `main_loop.c` (the
  battle's start, drawing the timers' and the enemies' orders from a pool; a
  pass: the damage cleared, the combatants updated, the battle lost, won or run
  from, then the timers or the turn; the turn: an enemy's script picks its
  action, a member's menu opens or its status picks for it, the party's queued
  actions carried out one at a time, and the enemies it hit react through
  their scripts' reaction blocks; the rewards), `damage.c`, `battlefield.c`
  (the grid rebuilt with the combatants' cells marked), `reset.c` (what
  animates the battlefield put back to nothing at the start: the scripts, the
  animations, the effects and anchors, the trails, the palette animations, the
  rising numbers), `arith.c` (was `math.c`: multiply and divide as C, the
  hardware unit's registers left as scaffolding), `buttons.c`, `scripts.c`
  (the animation script channels, the commands through a C table).
- *The combatants on the battlefield* -- `animation.c` (a combatant's pose
  chosen from its statuses, its animation stepped a frame at a time, three
  combatants a frame by group, the party turning to face its nearest enemy),
  `objects.c` (screen bounds, the animation scheduler's search), `sprites.c`
  (a frame's tile list built into the object's tiles, the palette animations,
  the effect animations' index, the bitmap shapes), `tiles.c` (the
  battlefield's tiles decoded), `draw_objects.c` (the drawing pass: the
  combatants sorted by how far down they stand, then each one's front effects,
  tiles, frame, back effects, shadow and afterimages, a leaf per part
  returning the next free sprite), `object_motion.c` (the damage numbers
  rising, scripted moves, anchors following, trails, the party's colour
  cycles), `effect_paths.c` (the nineteen movers' paths, one operation per kind
  in a C table: line, jump, spiral, orbit, homing jump, flat orbit, flicker,
  chase; the arithmetic plain C), `effect_animation.c`.
- *The screen effects in bank D1* -- `screen_palette.c`, `screen_gradient.c`
  (the HP gradient), `screen_graphics.c` (an effect's graphics loaded),
  `screen_window.c` (the window line table as a concept: the top border, the
  coming and shown buffers, bands, the wipe, waves, nudges, fills through a C
  table), `screen_beam.c` (the beam's two window quadrilaterals),
  `screen_motion.c` (jitter, swing, the effect settings as a C table),
  `screen_setup.c` (the fade tables as rising and falling pairs).
- *The windows* -- `window.c` (the whole battle window redrawn: the members'
  lines by layout, the enemies' names, the command windows and their frames),
  `status.c` (a member's HP and MP a frame, the gauges), `digits.c`,
  `menu_window.c` (the command and tech pages, the cursors over the
  battlefield, whose combination techs are called off, the frames' reset),
  `numbers.c` (the script's number commands), `window_leaves.c` (the carries
  the original's callers read, worked out beside the wrappers).
- *The setup* -- `setup.c` (the two entries and the one call-out left, the
  name converter), `setup_reset.c`, `setup_screen.c`, `setup_names.c`,
  `setup_party.c`, `setup_enemies.c` (an enemy from its record and its
  `EnemyHeader`, the formation, the enemies' rules, the kinds).
- *The frame* -- `frame.c` (the vblank's frame work and the main loop's frame
  update: the dispatch on the battle's mode a C table, the window's shadows,
  the slide as a state -- still, starting in, sliding in, sliding out -- the
  sprites hidden, the blink, the cursors' animated colour, the palette
  rotation, the frame count, a demo's exit, L and R held to run), `scroll.c`
  (the layers' per-line scroll over a `LayerScroll` a layer, the scroll mode's
  dispatch, the screen shake per axis), `uploads.c` (everything the vblank
  sends: the palette, a queued transfer, a requested block, a message's tiles,
  the sprite table, the window's rows, the rotating queued uploads, a
  combatant's built graphics), `display.c` (the display setup, the window's
  line table, the graphics load, the last vblank handler). Every upload and
  line slot is on the value calls (`vid_send_*`, `vid_set_line_slot`), the
  write orders in `runtime/platform/snes/encodings.c`, so no battle file is on
  the lint's `WRITE_ORDER` any more.
- *The handlers and waits* -- `nmi.c` (the vblank handler's frame count,
  message countdown, pause and frame advance, and the line handler's
  alternating-frame setting, on the game's state; the interrupted code's
  registers, direct-page scratch and math operands saved and put back in the
  entries, through `m_save_interrupted_registers_bank_first` and
  `m_block_move`, two additions to `machine.h`), `wait.c` and `sound.c` (the
  waits by what they wait for, the pause's silence and the music's fade on the
  audio service; `tm_beam_line`, the beam's line, is a new timing call).

**The RAM map.** About two hundred battle variables were added or reshaped,
by the groups as they needed them: the stat block's fields, the timers as
blobs by kind, the scripts' variables, marks, point and per-combatant and
per-effect command cells, the direct-page work cells (`battle_work`, `$80-$9D`,
"each routine's own", written only through named leave helpers), the tech
records (sixty, twenty a member), the setup's cells, the sprite drawers' page.
Stray accesses the original makes past a table -- an actor or a turn that
names no combatant, a partner past the three menu slots, Magus's slot (a word),
the grid's marking with the party off the grid, the effects' done flags that a
word increment carries into -- go through `_unchecked` accessors, each variable
saying why.

**Shadow mode.** Every group was merged shadow-clean over the library (26
movies, none mismatched, no page refused, no accessor out of range), and the
per-entry totals equal `main`'s for every battle entry in calls: no guard
refuses a call the game makes. Matched moves only with interrupted, where work
that was a chain of call-outs is now C with fewer comparison points:
`battle_draw_menu_window` 489 to 7,231 interrupted of 104,644 (it runs the
redraw, the party status and the frames' reset inline), the party status 60
to 437, the combination check 896 to 1,373, the combatants' step 10 to 124,
`battle_build_object_graphics_checked` 0 to 786 (it now runs the build as C); and `battle_frame_work`, `battle_frame_update` and
`battle_setup_display` (431,221, 431,221 and 1,174 calls) now run inside the
frame dispatch's own native run through its C table, so the original entering
them starts a nested comparison that is set aside when the dispatch reaches
its next point: the dispatch compares their work.
Those entries are fuzzed alone. What shadow found, each fixed in the port:
- the main loop: the Y the enemies' script start reads (the enemies placed),
  the carry the step to an enemy's stat block leaves before its statuses are
  followed and the one `slot - 3` leaves before an enemy's turn, the members'
  checks as a subroutine whose return sits under its calls, and the flags the
  rewards' callees don't read (`late-omen-zeal` gives an item);
- the menu window's inline frames reset leaves the carry its compare sets;
- the map's extents were too small where the game reads further
  (`battle_effect_sprite_data` to 48 sets, `battle_bitmap_counts`,
  `battle_object_sprite_entries`);
- the effect graphics loader's callers read the carry and overflow the
  decompressor and the transfer pass back (the endings movies).

**Fuzzing** (`tools/fuzz_routines.toml`, the battle section, on one base
state, `gato-battle` at frame 20,000). `tools/fuzz_compare.py` ran every battle
entry against `main`'s ports with the same seed: in the tight constraints no routine differs, and every
mismatch left is on a loose variant, one of the accepted divergences below;
the predecessor's own 225 of 20,000 on `battle_load_graphics` (it wrote its
inlined `JSR`'s return address into the stack) are gone. The per-entry counts
are in PORTING.md's battle row. What it found,
fixed or refused:
- the actor's status and chosen action read past their tables for an actor
  that names no combatant (the chosen action at four times the actor, a byte):
  now read as the original indexes them; a turn's place in the timers' order
  that names no combatant is refused;
- A's random high byte feeding the original's indexes (the party status, the
  cursors, the pose choice, the builds, the effects' paths): guarded, as every
  game call has it clear;
- a tech list entry past a member's twenty records, a slot's member past the
  party, Frog's slot off a member's: refused; a tech record's partner past the
  three slots, Magus's slot and a target entry past the combatants: read as
  the original indexes them;
- the anchor chase leaves its points in the angle's cells when it is already
  close; a wrapped cursor request leaves a partial offset in `$80`; the
  enemies' step leaves its place at each enemy, where a frame list read over
  it sees it;
- BG2's scroll took BG1's extra offset (20,000 of 20,000 on both fills; the
  movies always have the offset 0): only BG1 takes it; the colour rotation
  from colour 0 stores a bank on, at `$7F:2200`, reproduced through the
  working palette's unchecked accessors.
The accepted divergences, on inputs no battle makes, are each written at their
routine: the drawing pass past the sprite buffer's 128 sprites (the order of
writes shows there; 20 of 5,000 random states, `main` 7), and a graphics set
built over the direct page's working cells or the stack (the loose variants
only).

**The guards refuse only what the game never makes**, and each says so at its
definition: the battle's page and bank; A's high byte where the original
indexes with all of A; a combatant in X, in Y, or in the cell the caller
leaves; the turn's place naming a combatant; a divisor over `$8000` (the
original's bit-serial division loses the remainder's top bit); the window's
menu state (members, slots, cursors, targets, tech lists as the menus build
them); the setup's inputs (party characters 0-6, enemy slots empty, at most
three kinds of enemy in view, parts that fit in the eleven slots); the effect
paths' kinds; the drawing pass's sprite places. None refuses a call of the
library's.

**Don't-care declarations.** Every entry's set was widened to what
`tools/dontcare_check.py --list` offered before its wrapper was written, and
each call-out's to its callee's; the check agrees with every one (0 findings).
Where liveness has a register dead only by trusting a target table the game
fills at run time (the turn's `$C1:B762` and `$C1:AC5E`, which reach the
scripts' handlers), the call passes the overflow as the original has it.

**Timing.** The rewritten ports make far fewer bus accesses than their
predecessors (locals where the original re-read its cells, arithmetic in C
where it drove the multiplier, a loop's work in one place), so the battle's
119 measured `cost_milli` rows were recalibrated from shadow runs over the
library (`tools/timing_gate.py --calibrate`), and only those rows were taken.
A port that runs a sibling's work inline names it (`NATIVE_TIME_AS`): the
frame dispatch its three routines, the drawing pass each part, the movers'
loop each path by kind, the menu window the redraw, the party status and the
frames' reset. Three entries went into `tools/timing_exceptions.txt` with their
numbers, all writing RAM, with the vblank check finding no write moved:
`battle_draw_objects`' vblank-worst (25,095 clocks, a sort of the full list),
`battle_effect_combatant_jump`'s vblank tail (4.61%, at most 2,562 clocks) and
the frame dispatch's drift (+0.83%, the table step's share, at `cost_milli`
1); `battle_draw_object_underlays`' exception left, its successor
`battle_draw_in_front` within the limits. The new timing moved the
`bucket_lavos` route's shell fight onto a frame where the route's bot pressed A
on a member's menu before the menu had put its remembered command under the
cursor, so a list other than the one it wanted opened and the bot, not
expecting it, waited for good; `main` passes because its timing lands
elsewhere. The bot now backs out of a list it didn't ask for
(`tools/movies/bot.py`), and the route passes in the shipping mode.

**What is left** is in TODO.md, "What battle's rewrite left": the original's
routines the battle still calls out to, the meanings not worked out, the
quirks reproduced on purpose, and the accepted divergences.

### Phase 3, menus: the load screen, the window language and the opening

The first chunk of Phase 3's bank `$C2` wave: the screen every game starts
from, and what it is built with. `python3 tools/burndown.py` for `$C2`: 17.2
KiB ported and 71% left with 173 entries before, 19.8 KiB and 67% left with
216 entries before the opening's two came out (TODO.md); about 2.2 KiB of new code in 41 entries, and about 0.7 KiB of
earlier ports (the save screen's slot, `menu_window_set_line_effect`, the
sprite reset) rewritten onto what the chunk built. All of it to the bar:
`game/menu/` scores 0.03 operations a line with 35% of its functions on the
machine, and none of the new files is on the baseline.

**The files, by the game.**

- `windows.c`: the window command language every menu screen is drawn with
  (the interpreter `$C2:ED31`, its eleven commands and their handlers). The
  commands are one C table (`LANGUAGE`) that decodes operands into places,
  sizes, styles and strings, and names the handler whose time each command's
  work is charged at; a window style is the tile its twenty tiles start at,
  in the player's design or the one a list names. The frame, the inside's
  four-by-four pattern, the colours of a design and the label strips are
  functions the load screen and the save screen call directly.
- `cursor.c`: the cursor's reading of the pad (`$C2:E984`: a push moves it to
  the place's neighbour in that direction, the buttons it reports), the
  sprite table's reset under it, and the menus' sounds (`$C2:EABA`-`$EB03`,
  through the audio service with the original's three-frame wait left to the
  driver's call).
- `load_screen.c`: screen 14's steps after its opening (choosing, which game a
  New Game+ carries on from, and the erase question) and a slot's row in the
  design its game was saved with. The opening step itself is still the
  original's (below).
- `save_screen.c` gained the slots' reading as the menus open (`$C2:D156`):
  the save code's check of each slot, the summary read from the cartridge
  through the save service, the chapter's and the slot's name labels; and
  `saves.h` is the cartridge's record of the three games (the slot saved
  last, times finished, whether the game has been finished, each slot's mark),
  which also retires `menu_saves_finished_game`'s read of save RAM through the
  ROM accessor.
- `opening.c`: what the menus do each time they open (`$C2:8096`): take the
  interrupts, quiet the sound effects, blank the screen, read the slots, list
  the party, unpack the settings. Its graphics, sprites and items are still
  call-outs (TODO.md).
- `bridge.c`: the menus' one way to call the original's code
  (`menu_call`, `menu_call_long`, and `menu_call_returning` for a callee
  that comes back with widths of its own), `menu_run` for a sibling port run as
  the original's `JSR` reaches it, and the frame of a routine a bridge stands
  in for. Unlike the battle's and the field's, the menus' routines change
  widths from routine to routine and save their caller's with `PHP`, so a
  call's check is the widths, the page and the data bank it was made with.

**A liveness hint for the task switch.** The menus' tasks yield with
`JSR $C2:821E` and wait with `JSR $C2:822B`, which save the caller's
registers on the task's own stack and return on another task's; the pairing
of pushes can't follow it, so every register was live after every call to
them and no mode handler could declare anything dead. `re/hints.toml` now has
`[[keeps]]` (DISASSEMBLY.md): a call that comes back with the listed
registers as its caller had them, reading only `reads`. Liveness takes the
call as that summary (`tools/disasm/liveness.py`, `_kept_summary`), and
`tests/test_disasm.py` checks it on a task switch built in the test.

The first cut of the hint was wrong about DB, which the review found: neither
entry saves or restores it, and the scheduler resumes the next task with the
yielding task's DB, setting it only after every task has had its turn
(`$C2:81C1`). The hint had DB dead everywhere, which let a false `DC_DB` on the
load screen's yield past the don't-care check, and five entries' declarations
with it. The hint now names DB in `reads`, and `tools/disasm/keeps.py` checks
every `[[keeps]]` hint against the code: the entry pushes exactly the kept
registers, the hint's `resume` pulls them back in reverse before it returns,
and every register the entry doesn't save that some path to the next task
reads before setting, or doesn't set at all, is in `reads`. The don't-care
check runs it, so a wrong hint fails the gate; `tests/test_disasm.py` has a
switch built in the test that a wrong hint of each kind fails on. The check
takes `$822B`'s `XBA` as reading both bytes of A, so its hint names `AH` as
well. The six declarations are the smaller sets liveness now gives.

**Found by shadow mode.** The cursor's reading leaves its loop's bit in the
menus' first cell; a label strip steps its tile after setting bit 4, not
before; the list handlers that run with any accumulator width (`$C2:F378` is
reached with a 16-bit one from `$C2:C773`); `$C2:968D` comes back with an
8-bit accumulator; the slots' reading leaves the slot count at 3 and the time
played's cell holds the game's start as the bus addresses it. A guard that
read save RAM through the service cost the machine 8 clocks and moved the
generated code's timing: guards peek (`snes_bus_peek`).

**Found by fuzzing** (tools/fuzz_routines.toml; PORTING.md has the counts):
the settings' clearing clears a byte past the last setting; the slots' count
starts from the menus' zero word, and the gold a summary hides is written
from it; the summary reads the checked slot's game from the cell the save
code leaves (`menu_slot_game_at`), not from the slot's number, and steps
through the party a byte at a time within its page; a time played's digit
past 9 carries through the original's shifts that multiply by ten, which no
clock of the game's makes, so the time's guard and the save screen's (whose
header draws the cartridge's time) take only decimal digits and the port
multiplies by ten; a window list's
string reads the number conversion's zero word, so the save screen's guard
now asks for it too; the header's "taken" count is a byte; loading a design's
colours counts a palette change a word wide, so the drawing's guards leave
room in the count; and several entries were refused widths the game never
calls them with (the index registers 16-bit), where the original loops. The
save screen's rewrite against its predecessor (`tools/fuzz_compare.py`): 0
mismatched of 2,000 both before and after, once the summaries' names are
letters, as the game's name entry makes them; with control codes in the
names, the strings drew through the text port's accepted divergences (TODO.md).

**A port that spans frames.** The load screen's opening (`$C2:E613`, which
draws the whole screen through `$C2:E7C3`) was ported and matched wherever
shadow mode could compare it, but every call of it in the library is
interrupted, and in the shipping mode it broke the `beyond_time` route: loading
the Final Battle save, the screen never appeared and the game ran into a `BRK`
two frames later. Its drawing takes the original more than a frame, with the
menus' vblank handler taken inside it; as one port, the time is owed at its
return and the handler taken there, twice, and with the time charged cheaply
(an experiment, not a fix) the screen appears. The route passes with the two
entries left to the original. Why it went wrong was the runtime's: the
debt was paid inside the menus' vblank handler, which moves the stack, and
the next vblank nested in it (fixed; "The tooling pass after Phase 3's first
chunks", below). The port came back after that fix, unchanged but for
`$C2:E613` stopping with `m_stop_at` at the task start it jumps to
(`$C2:83B2`), which starts the screen's fade-in: `beyond_time` passes in the
shipping mode, and both entries are fuzzed (2,000 states each, none
mismatching), which is their only check, since every call of theirs in the
library is interrupted. For the same reason the timing gate has no stretch of
theirs to calibrate from: their `cost_milli` is left at the menus' ports'
usual 5000, and nearly all their time is their callees' (the window lists,
the slot's drawing and showing), each at its own calibrated rate.

**What the effects check found** ("The tooling pass after Phase 3's first
chunks", below): reads of cells the menus' ports wrote only in scaffolding.
The load screen's member rows write the row and the member the portrait's
own entry (`$C2:D466`) reads as plain state, the member as a byte of its own
(`menu_scratch_save_member_character`), as the original's `STA` does; a
window's frame leaves the inside's size as plain state wherever it is drawn
(the frame's entry, a list's window with or without its inside, the save
slot's window `$C2:ECDB`); the slots' rows keep the next label's first tile
plainly. `menu_object_wave_hdma` reads a phase another subsystem's scaffolding
leaves in the field page's cell, as the original's does; that one has a
reason in `tools/effects_baseline.txt`.

**Timing.** Every entry is calibrated from the library but the ones only
`menu_open` runs, whose error its own rate takes up (TODO.md), and a window list charges each command at its handler's
rate (the handlers are in the `LANGUAGE` table: the first cut took their
addresses from the ROM's table without their bank, so their time went to no
hand port). The inside's pattern is drawn a row and repeated with block moves
as the original does, which is where its time goes. Three entries are
exceptions (`tools/timing_exceptions.txt`): the list interpreter and the
string command carry the text engine's spread in vblank, which the text port
already has. In the shipping mode the menus' vblank work lands on line 41-44
once in each of four save movies (two picture lines on one frame;
`tools/vblank_check_exceptions.txt`).

### Phase 3, wave 2: the enemies' minds (`$C1`)

The first chunk of bank `$C1`: how the enemies choose what they do and carry
it out. Their scripts' interpreter -- the choosing `$C1:8C09`/`$C1:AFD2`, the
reactions `$C1:B488`, the 41 conditions, the 23 actions' choosing and turns,
the 57 selectors and the target lists (`$C1:8C09`-`$C1:B092`, `$C1:B488`,
`$C1:B4AA`) -- the target search and the battlefield's layout questions
(`$C1:28B0`-`$C1:2D80`), the direction between two points `$C1:0222` and
the scaled sine `$C1:01F9`, the random numbers `$C1:AF22`/`$C1:AF79`, a
combatant's knock-out in the turn's update (`$C1:B279`, but for a member that
comes back once), and the beaten enemies' finish `$C1:AC5E`. About 11 KiB of
65816. `python3 tools/burndown.py` for `$C1`: the battle
subsystem from 84% left (10.6 KiB ported, 0.5 KiB inlined, 42 entries) to
52% (31.1 KiB, 10.9 KiB, 202 entries). That is more than the chunk: the loop's
entries now reach, without a call-out, the code they run as C and as siblings,
which the tool credits to them (its docstring says which way it is wrong).

**The engine is C on the game's state.** `battle/enemy_scripts.c` is the
choosing: every enemy that has nothing chosen looks through its script's
turn section, block by block, until a block's conditions hold; the reactions
look through the reaction section the same way for the actor. A choosing's
state is a `ScriptChoosing` its starter makes and passes down -- the enemy,
the pair of conditions, the outcome, how far the script has been read, the
block and its actions -- and the original's cells for it are written once,
as scaffolding, where it ends. The conditions (`enemy_conditions.c`) and the
actions' choosing (`enemy_actions.c`) are C tables, a row a command: its
operands' layout, which decodes them into the test's named parameters, how
far the original reads the script for it, and the original's routine it is
timed as, a literal. A condition returns how it came out (a
`ConditionResult`: fails, holds, holds with nothing chosen, chooses and
fails, and whether it took a target) and the block's testing acts on it; a
choice returns how far it read and what it does to the block. The selectors (`targets.c`) a table of
what each picks; the search (`positions.c`) a function of the question and the
three combatants, with the grid, the boxes and the direction as operations of
their own. None of it takes a `Snes *`: the entries' wrappers put the answers
where the original's callers look for them, and the machine side is the
enemy's turn (`enemy_turn.c`), which calls the original's code for what is
still generated -- the effect's working out, the attacks' and techs' setting
up and targeting, an enemy's coming in, the message, the showing -- and runs
its siblings (`battle_run`) where the original makes a JSR: the action's turn
from its C table, the showing.

**The main loop calls its own subsystem's ports as C.** The choosing at the
battle's start and after every action, the timers' order drawn at random, the
damage table's and the numbers' clears, the combatants' low HP and statuses,
the reactions' conditions: C where they were call-outs through ROM addresses.
The enemy's turn, the update of every combatant, the combination check, the
turn itself and the beaten enemies' finish are siblings the loop runs as its
JSRs would. The turn is the port's where its guard holds and the original's
where the turn's place names no combatant (`battle_take_turn`, from the loop
and from the timers' dispatch), since running a sibling skips the entry's
guard.

**The guards follow what the entry takes up.** A script holds only the
commands the tables have, but a place the game never leaves (a fuzzed one)
can land on bytes that aren't commands, which the original dispatches past its
tables into the next table or into code. So each entry's guard looks at what
it would take up from the place: a condition entry at the selector it names
and the actions after it (past the first end marker after each byte the
condition can leave the place on), an action entry at its selectors, a draw
among four at the four, the choosing at the actions it keeps, the enemy's turn
at the action it carries out. Each entry's guard is made with it by the same
macro and knows its command as a constant, and the layouts it checks are the
tables' rows. None refuses a call of the library's (over the 26 movies every entry's calls
match, none refused and none mismatching).

**The review's rework.** The first review rated the three script files a
translation: the engine kept its working state in the original's cells (the
script's place written and read back, the outcome counted in a cell, the
enemy at work a cell used as the loop's variable), read operands by position
with the layout derived again in the guards, called the next step from inside
the handlers, and knew its entries by the machine's PC. They were rewritten as
above. Three cells the ports wrote only as scaffolding turned out to be read
back as game input, and are now kept plainly by every port that writes them:
the long arithmetic's leftover high word `$2E` (the random draw divides with
it: `battle_long_high_word`), the high byte `$0B` of the stat offset the
stat test reads (whatever the last word kept at `$0A` left), and `$0E`,
which an enemy tech's hits are noted as where its targeting doesn't set it
(the tech's number, or whatever an action, a condition or a selection last
left there; every write of it in the scripts and turns is kept). The
native-shape build had read them stale. The re-review added the cells a
candidate past the combatants reads its stats from, wherever the table of
the stat blocks' places points: the conditions that read candidates' stats
keep what they write at `$08`, `$0A` and `$0E` too (the stat test's count
among them), since its doubled walk goes past the list. `tools/fuzz_compare.py` against the
reviewed ports (135 routines, 5,000 states each, seed 1): 133 the same, and
two differ where the walk's stray box read lands on the choosing's cells,
which the rework writes when a choosing ends (writing them before each
condition, as a check, brings both to 0; an accepted divergence, docs/TODO.md).
The rework moves where the time of the conditions, the choices and the
turns' parts falls, so the battle's rows the library measures were
calibrated again (44 of them, only battle's taken); the library is clean
under shadow mode and the gate passes.

**What shadow mode and fuzzing found**, all reproduced rather than tidied:

- The target search's finds store the slot found as a word, so the byte after
  it is cleared (`battle_target_found_high`).
- The side tests (below, to the right, level, the halves and edges) count
  their failure cell down from what the search left in it, rather than set
  it.
- The stat blocks are reached two ways, and the table of their places is
  indexed with the slot doubled in a byte by most routines and in a word by
  the stat tests (`$C1:9130`, `$C1:918E`, `$C1:91F9`, `$C1:95FA`) and the HP
  test `$C1:90BE`; an index past the combatants reads wherever the table's
  word points, into the next bank where it carries.
- The test of a stat's value (`$C1:95FA`) goes on from a candidate to the one
  at twice its slot in the list: it keeps the doubled slot in X for the list
  as well.
- A condition that finds nobody holds without choosing for the side tests,
  fails for others; the side tests set the search's question and searcher
  whether or not a search runs; the side test's search is the third operand
  plus six, in a byte.
- An enemy present in the fight is looked for until the enemies' count, from
  the first, by a 16-bit index: with no enemies it reads on for 256.
- The selector in A (`$C1:AC2C`) is doubled in a byte, so its top bit is
  lost.
- A party member picked at random has the party updated again for each
  draw; the draw among four drops its own return and goes on with the
  choosing's loop at `$C1:8C90`, leaving the script's place on a byte it
  stepped over until the loop puts it back.
- The carry the attacks', techs' and their targeting's setting up read is the
  one the original's code before them leaves: the attack's number compared
  with 1, the marking of the targets from a first target compared with 3, a
  stat change's.
- The turn wait taken away to exactly 0 stays 0; to below it, 1.
- A member that comes back once is the original's from its test of the
  revival byte, with A's high byte and the carry clear.

**Fuzzing.** Every new entry, seed 1, on the `gato-battle` state with the
constraints in `tools/fuzz_routines.toml` (161 routines,
none mismatching: the selectors, the search and the reach tests 20,000
states each (18,700-20,000 compared, a member that would come back turned
away; the selector in A 8,836, where its guard takes the table's), the
conditions 20,000 each (12,900-15,700 compared, the rest turned away as above:
bytes that aren't commands), the actions' choosing and the choosing's entries
16,900-19,900 (all of them for the choosing of every enemy, 5,000), the
actions' turns 20,000 each (8,300-18,700 compared, a message or a tech's
setting up running past the call-out budget), the enemy's turn, the tech's
turn and body and the showing 5,000 each (3,400-4,250 compared), and the
beaten enemies' finish 5,000 without an enemy beaten and 5,000 in a demo
battle, where the battle's step that waits for frames is skipped). The ports rewritten
around it compare with `main`'s with `tools/fuzz_compare.py`: 47 routines,
none differing.

**Timing.** The battle's 89 rows the library measures were calibrated from
the shadow runs (`tools/timing_gate.py --calibrate`), only battle's taken: the
new entries from a default of 3,000, `battle_main_loop_body` from 7,292 to
33,695 (the choosing and the timers' orders drawn run in its time now),
`battle_update_all_combatants` from 11,157 to 17,210 (the knock-out in C).
Every entry is within the limits, with no exception added; the entries the
library doesn't reach keep 3,000. Running the turn inside its caller's stretch
costs shadow mode some comparisons: `battle_give_turn` is compared within the
timers' dispatch and the loop's pass rather than on its own (4,613 of 4,626
calls), and those two are interrupted more often (3,620 of 27,532 passes and
5,704 of 17,952 dispatches against `main`'s 299 and 3,383); fuzzing covers
them, as the predecessor's counts do

**What is left** of the enemies is in TODO.md, "What battle's rewrite left":
the call-outs from an enemy's turn, the timers' kinds' handlers, and the
quirks and the one accepted divergence (narrowing the candidates by the aim,
which the original never leaves where every candidate is marked).

### Phase 3, menus: the typesetter, the graphics and the items

The menus' second chunk: the engine every string of the game's text is set
with, and the rest of the menus' opening but for the characters' sprites.
`python3 tools/burndown.py` for `$C2`: 19.8 KiB ported and 67% left after the
first chunk, 22.8 KiB and 62% left after this one, 226 entries.

**The typesetter** (`menu/typeset.c`, `$C2:57DF`-`$C2:6262`). The dialog, the
battle's messages and the menus' labels all set their text with it: a caller
names the string (a table and a number), the tiles and their layout, puts the
string's numbers and names on the text page, starts it (`$C2:57DF`) and then
calls it (`$C2:5823`) with a budget of glyphs, a glyph or two a frame for the
dialog's typing. A call stops at the string's end, a wait, a line or page
break the caller acts on, or the budget's end, and says which; where it has
got to lives in the text page's cells (`menu_text_reading`), so the next call
takes it up in the middle of a word, a name or a number. The string's bytes
are glyphs (and the font's two-byte half), words of the dictionary, and
control codes: the breaks, the numbers (decimal without leading zeros, or hex
where the page's hex setting is on) and the names (a character's, the
party's, an item's, a tech's, an enemy's). The glyph drawer (`menu/glyphs.c`)
now draws from parameters, into four- or sixteen-colour tiles, and its three
entries take them from the page's cells as before; the menus' label setting
(`$C2:F90C`), which sets a label a line of 32 glyphs at a time, is a port too,
taking the label's table, string and tiles and giving back its width, and the
load screen and the save screen call it as C, the chapter label leaving what
it leaves in the registers.

Three things the gate made the port keep. What a call leaves in X and Y is
read after the label setting: the save screen's slot rows return into the
menus' task yield, which keeps X and Y for the next screen handler, and two of
those read Y. The logic doesn't keep how a call ended; the entries' wrappers
work it out as scaffolding, reading the string again from where the call began
to find what it read last (a glyph, an insert, or a control code), with the
insert's source for whether some of it is left. A first version passed a small
`Ending` back through the reading functions for this, and review rated it a
translation: it told apart only what the original's registers do. A name past
the pointer table's seven is read through the table's neighbours, which can
put it on the text page itself: the original counts the name's glyphs with the
insert already begun, pointer and source set, so the port does too (fuzzing
found it: a party member with an id past the characters'). And the budget is
counted down on the page glyph by glyph, not in hand: such a name reads the
budget's cell as a glyph, and a label long enough to wrap its bank draws over
it (fuzzing found a chapter label that differed when the budget was kept in
a local).

**The graphics** (`menu/graphics.c`, `$C2:968D`, `$C2:C511`). The tile sets
every screen draws with, built in work RAM from the ROM's graphics and sent
to VRAM as the menus open: four-colour tiles laid out as sixteen-colour ones,
three-plane tiles given a fourth, extra planes made from the ink of the ones
there, then the menus' colours and the window design the player chose (which
the settings screen loads on its own too). The display's queue sending, its
palette, and its two transfers into work RAM became functions on the game's
state (`menu_send_upload_queue`, `menu_send_palette`, `menu_clear_memory`,
`menu_copy_rom_to_memory`) for this.

**The items** (`menu/items.c`, `$C2:9875`, and the lookups `$C2:8881`,
`$C2:88AD`, `$C2:88D8`). As the menus open, the inventory loses the places no
item fills, the story's and the place's say in what the item lists offer is
noted, and every item's kind, price, users, amount, attack or defence and
name place is copied into the menus' tables; the save code's list of what the
party carries (`$FF:F813`) and each member's figures (`$C2:99B2`) are still
the original's, called from the port.

**What is left of the opening** is the characters' sprites (`$C2:F3CA`): each
of the menus' eight records loaded with a character's sprite (`$C2:F422`,
which decompresses its tiles) and started on its animation (`$C2:F69D`-
`$C2:F870`, which the records' task runs every frame). They belong with
`menu/sprites.c`'s record sprites and are the next chunk's (PLAN.md).

**Found by fuzzing**: the names' count on the text page, above; a number's
leading zeros are shifted out of its digit cells as the original shifts them,
leaving the cells past the number as they were; the lookups leave Y's high
byte clear (they run with 8-bit index registers). **Found by shadow mode**:
the lookup leaves the category in A; the item tables leave the second place
note's test and item $C8's kind in the flags the save code's list saves; the
graphics leave the tile loops' counts in the menus' first cells, and the
flags of the design's setting.

**The effects check** found the glyph drawer's entries reading cells only
the original's setter writes now; the baseline gives the reason (only the
original's code calls those entries). The queue's count is written a word
wide as game state, since the byte after it aliases the sprite-object mode's
slots.

**Timing.** Calibrated from the library. `menu_text_go_on` and
`menu_set_label` carry the glyph drawer's spread (the pen's offset decides
its shifts), and are timing exceptions like `menu_draw_text`. The graphics and
the items run once as the menus open, with the screen blanked.
The new timing moved the `bucket_lavos` route's Lavos Core fight onto a
frame where the route's bot, backing out of a tech list it had opened
before the list took input, held B across its four presses, which the game
reads as one: the list stayed open and the bot, waiting for the command
menu, waited for good. `tools/movies/bot.py` now releases B between presses,
and the three long routes pass in the shipping mode.

### Phase 3, field and events: the party on the map

The first chunk of bank `$C0` in wave 2. PLAN.md's table of what event's
bridges still called in the original chose it: the field's state routines
behind `$B0` (`$C0:9E29`, `$C0:A26B`, `$C0:A2CE`, `$C0:5B8D`, `$C0:3154`) and
the tile copy behind `$E4` (`$C0:AF4E`), with the unported runs they lie in.
Reading them showed what they are: the leader leaving a trail and the members
walking it, the leader touching and pushing what it walks into, and the map
changing under a script. The chunk took those and what is the same concept
beside them: the player talking to an object (`$C0:5AC5`, from the field's
confirm button), the party's regrouping commands (`$D9` `$C0:326C`, `$DA`
`$C0:345A`), and a member sent back to the leader after a battle
(`$C0:5B1F`). `python3 tools/burndown.py` for `$C0`: 19.0 KiB ported, 0.7
inlined, 68% left and 249 entries before; 21.7 KiB, 1.4 inlined, 63% and 258
after -- 3.4 KiB, against the 8-12 a wave-2 chunk aims at; what fills it out by
concept is the leader's walk, the next chunk (below, and PLAN.md).

**The party as a module** (`field/party.h`, `field/party_trail.c`). Every
frame the leader moves it leaves a footprint on a ring of 128 in bank `$7F`:
how far it moved, how the pad walked it, how its sprite was layered, and the
ground under it (a climb, a moving floor). Each member walks the trail behind
the one ahead of it, a footprint a frame, keeping 24 footprints back while the
leader moves and closing to 16 when it stands, so the party bunches up and
strings out. What each shows follows the footprint: standing, walking or
running and facing its way, climbing, the leader running on the spot against
a wall, a pose while the player picks an answer, a fidget when the party has
stood about. The original's state routines were one function a gait and a
facing (sixteen of them, `$C0:A079`-`$C0:A15C`, reached through a table of
64 words at `$C0:9FF7`); they are one `Pose` -- a gait and a facing, or a
diagonal's two -- looked up from the two axes' gaits. The lookup keeps the
original's index arithmetic, one named function: it runs the vertical speed
and an eighth of the horizontal together into one byte, which muddles speeds
other than the pad's, and a member waiting on a moving floor looks up what the
floor's pull leaves of the leader's walk, which can be any. `$B0` and `$AF` are
one operation (`step_with_the_party`, `once` the short form's row), with no
call-out left; the entries were renamed for it (`event_cmd_walk_with_the_party`
and `_once`, `$C0:3002` and `$C0:2FFD`) and carry their predecessors' totals.

**The leader meeting objects** (`event/contact.c`). The field notes the object
the leader is up against (`field_leader_contact`): the one its walk ran into,
or the one before it when the confirm button is pressed. Talking and touching
are calls the field makes on the object's script, the same as objects make on
each other, so `event/calls.h` came out of `calls.c`: an object's function's
start, whether it takes a call now, and running a script from a place at a
level (`object_run_script_from`), which the calls, the talk, the touch, the
push and the regrouping all use. A pushable object (bit 1 of the byte `$84`
stores beside solidity) goes a tile the leader's way where that tile is open,
through the party's push script; the decision is one function
(`leaders_touch`) that both the touch and what its derivations work out call.

**The party's scripts and the regrouping** (`event/party.c`). Every map load
copies three runs of commands after the map's own script (`$C0:5CEC`): a
member walking up to the leader, a member walking to a tile and facing a way,
and an object walking to a tile. `$D9` and `$DA` hand them to the party at the
most urgent level, writing the tile and facing into the scripts' operands,
and wait on a count two of the map's variables keep (`FORMATION_COUNT`,
`GATHERING_COUNT`); then the members pick the trail up from the leader's newest
footprint (`party_trail_regroup`). A formation's place is decoded once, by its
row, into a `FormationPlace` (a tile, and a facing from the top bits of its
column and row): the language's rows now decode into records as well as
numbers (`EVENT_RECORD_PARAMETERS`, `FORMATION_PLACE(n)`), which is what
TODO.md asks of the location's decoding too.

**The tile copy** (`field/tile_copy.c`) copies a rectangle over another place
in the tables its description names: each layer's tiles, the tiles' properties
and their two bytes of attributes, or of the properties only the bit that says
which half of the tile set BG1's or BG2's tile comes from (read from the tile
builders at `$C0:762D` and `$C0:79EA`). It goes a tile at a time, top to
bottom and left to right, as the original's block moves do, so a copy onto
itself a little right or down repeats its first tiles. `$E4` is an operation
calling it; the main loop's copy (`$C0:1790`) reaches the same entry.

**What the map gained**: the trail (seven tables at `$7F:0C00`-`$7F:0F7F`)
and where each of the party is on it (`field_trail_position`, `$01AB`); the
walk's outputs on the layers' page (`field_leader_walk` `$1D2C`,
`field_leader_movement` `$1D32`, `field_leader_ground` `$1D34`); the idle
count the frame wait keeps (`field_idle_frames`, `$0158`, masked `$83FF` at
`$C0:EC60`); the leader's contact (`$01EB`); and `field_state_routine_hold`
renamed for what `$0138` is, the screen recentring on the leader before a
queued tile copy (`field_screen_recentring`). `field/field.h` names
`field_mode_flags`' bits (the party gathering, an object being pushed, a
dialog the field opened, which `event/dialog.c` had to itself) and
`dialog_choice_active`'s states. `field/object.h` gained an object's step
(`object_set_step`), its sprite priorities as a value to hand on
(`SpritePriorities`), whether it can be pushed, the party's animations beside
the movement ones, and `object_show_movement`, which was `animation.c`'s own.

**What shadow mode found.** The first library run mismatched on two things:
the gap between two places on the trail was worked out as a C remainder of a
negative difference (so a member whose place had wrapped past the one ahead
took the next footprint when it should have waited), and the regrouping's
hand-overs leave Y's low byte (they run with 8-bit index registers), which the
derivation had as the whole offset. After those, over the 26 movies every entry
matches, none is refused, and every other entry's totals are the
predecessor's. The one move that was a bridge is compared now:
`event_cmd_walk_with_the_party` 251,565 calls, 867 matched before (the rest
abandoned inside the engine's stretch), all matched after. The new entries:
`field_leader_steps` 116,079 calls, `field_first_member_follows` 83,032,
`field_second_member_follows` 51,587, `event_touch_contact` 1,871,
`event_cmd_take_formation` 716, `field_copy_tiles` 261,
`event_cmd_gather_party` 166, `event_talk_to_contact` 99,
`event_member_back_to_the_leader` 10; none interrupted. `$AF` has no call in
the library, nor does a push.

**What fuzzing found.** The original adds the leader's contact and the
party's objects into a place's offset (and a function's) as 16-bit words, so
the byte after each is read too: `$01EC`, and `$0198`, `$019A`, `$019C`
after the party's places. The game stores those objects with 16-bit index
registers and keeps the bytes 0, which random states don't: 14,926 of 20,000
talks mismatched until the map named them (`field_leader_contact_high`,
`field_party_objects_high`) and the guards required them 0. The trail's gap
showed on the members' own entries before the library did (344 of 20,000).
Then every entry fuzzes clean (PORTING.md's row), with variants that fix the
paths random state rarely reaches: a leader up against an object that takes
calls (touch and push), a member in its place with its role, the leader
standing with the player's control (fidgets) and during a choice, footprints
on plain ground, a formation and a gathering with members missing, and tile
copies small enough to overlap themselves. The predecessors of `$B0`, `$AF`
and `$E4` compare the same states with the same mismatches (none); the new
guards turn away a few more (a contact's high byte, a copy off the map).

**What was left at the edge, and why.** The trail's start at a map's setup
(`$C0:9DC3`, where the openings hand back) and the gathering after a battle
(`$C0:034B`) stay the original's: the first reads the ground under the leader
through the walk's own routines (`$C0:9AC8`, `$C0:9AD3`), and both go with
the walk. The inventory and membership commands (`$C7`-`$D7`) call the
battle bank's item routine and the menus' party routines, and would have been
bridges only. The objects' own loading (`$57`-`$83`, their sprites through
`$C0:448D`) is a concept of its own, objects coming onto the map.

**The timing gate.** The new rows were calibrated over the library, and two
event rows moved with them: `event_cmd_walk_with_the_party` 3,374 to 4,641 (its
time is now the party's step as C, where it was its bridge's own loop) and
`event_run_object_script` 4,596 to 4,723 (the engine runs `$B0` as C inside
its stretch). The tile copy's time follows the rectangle and the tables it
takes -- whole tables by the original's block move, a tile set's half a byte
at a time -- which one scalar fits only on average: `field_copy_tiles` and
`event_cmd_copy_tiles` are in `tools/timing_exceptions.txt` (at most 26,414
clocks in a call starting in vblank, writing the map's tables in RAM, which the
vblank check finds moves no video write).

### Phase 3, field and events: the leader's walk and the confirm button

The second chunk of bank `$C0`: what feeds the party's trail, and what the
player does with the leader besides walking it. `python3 tools/burndown.py`
for `$C0`: 21.7 KiB ported, 1.4 inlined, 63% left and 258 entries before;
25.9 KiB, 5.0 inlined, 56% and 262 after -- 4.2 KiB, counted with the
tool's fix below, which leaves the "before" as the first chunk reported it.
The concept's other half,
the screen's edges streaming the map in as it scrolls (`$C0:93E1`, the row and
column builders), is 5.5 KiB of its own and the next chunk (PLAN.md).

**The walk** (`field/walk.c`, `$C0:881E`, every field frame). The pad asks
for a walk or, with the dash held, a run, on top of what a moving floor pulls
(`$C0:88EE`, the eight directions `$C0:8925`-`$C0:8A6C`). The leader stands
in a box against the map, its feet at its position; walking one way, the
box's leading side is looked at at both corners (`$C0:8AB5`-`$C0:8E20`), and
walking aslant its leading corner and both sides (`$C0:8E21`-`$C0:9174`, four tables of which side
nudges which way), where a corner meeting another level nudges the leader
round it and a wall stops it. Before the map, the other objects: the first,
from the last slot down, standing close to where the leader is going
(`$C0:9923`) is what it is up against, and stops it if solid. The map has
two levels; each tile's shape (a slope from a table of 22, a half, a
quarter) says which parts of it are on which (`$C0:9AA1`-`$C0:9C36`), and the
leader walks on its level, onto stairs joining the two, over or under a
crossing. What it steps onto takes effect (`$C0:9C5C`): an exit, a step
event, a moving floor's pull for the next frame, a climb. It stays within the
map's edges (`$C0:9175`), and the screen follows it while it is past the
screen's middle the way it walks (`$C0:99DE`). The recentring (`$C0:885A`)
scrolls the screen back onto the 16-pixel grid a pixel a frame while a
script waits on it. Both still call the layers' scroll (`$C0:91AC`, a port)
and the screen's edges (`$C0:93E1`, the original's) on the layers' page.

**The party's start and its gathering** (`$C0:9DC3`, `$C0:034B`), which the
first chunk left at its edge: at a map's setup the trail starts afresh and
the leader takes the level and ground of its tile (the map's openings' entry,
`event/bridge.c`, runs it as C and hands back only where its guard fails);
after a battle the members walk back to the leader through the gathering
script (`event/party.c`, `party_back_together`), timed as
`event_member_back_to_the_leader`.

**The confirm button** (`event/contact.c`, `$C0:1AAC`; `event/chests.c`).
Where a dialog waits for the button, the press is the dialog's. Otherwise the
leader talks to the object it walked into this frame or, failing that, the
first object near enough before it (`$C0:1CFC` and its four ways: under 28
pixels along the way it faces, 12 across), and then opens the chest before
it. That search measures as the objects-in-the-way search does, the
original's borrow included, so `field/motion.c`'s measure became
`object_bearing` in `field/object.h` for both. A chest (`$C0:1DF4`-`$C0:1F23`)
is a tile the leader can't walk onto whose walkable byte names it; the
game's chests are one list in bank `$F5` (`field_chest_at`), and a chest's
number in it is its story flag, one bit each from the story memory's second
byte. Opening one raises the flag, asks the main loop to show it open, and
gives the party an item (nine bits, into variable 0) or gold (into
`field_chest_gold`) through the battle bank's inventory service
(`$C1:8003`, a call-out, which keeps the flags), then the field's own message
(`event/dialog.c`, `field_message_opens`); an opened chest's message says it
is empty, and a chest marked as holding nothing of its own gives nothing and
says nothing (its location's script does the rest).

**What the review of the first chunk asked, folded in.** A party place is
one type (`PartyPlace`, `object_party_place`); the leader's ground an enum
(`GroundKind`, the stored byte inside `LeaderGround`); one setter for a party
script's destination and the facing encoding in `event/party.c`; a tile
copy's rectangle as its size, the table addresses in the leaves; `Touch` the
outcome alone; named setters for `field_mode_flags`; the fidget flag decoded
in the idle count's storage. The contact the leader is up against is one
concept in `field/field.h` (`leader_contact`, `set_leader_contact`,
`leader_loses_contact`), which the walk, the search and the touch share.

**Chunk 2's review.** It rated `field/walk.c` a translation on one count:
the screen's check carried "what the original leaves A with" out of the
check, for a scaffolding re-run of it. The check returns whether the screen
follows now, and what the original's cells get, the check turned back up
included, is worked out from the state by the leaves beside the check
(`leave_screen_check`, `leave_check_down_turned_back`). With it: the tile's
shape is an enum in `field/layers.h` (`TileShape`), the level a `MapLevel`;
the pad reads `core/input.h`'s buttons; the recentring's step is named;
whether the player has the party is `party_is_the_players` and
`hand_the_party_to_the_player` (`field/field.h`); and the call-outs whose
site depended on a ternary are written out per case, so the checks read a
literal each. What it left for later is in TODO.md.

**What the game's buttons are.** The walk dashes on the fourth of the
player's assignments and the confirm button is the first, which settles what
the scripts' button conditions ask (`$30`/`$31`, `$3B`/`$3C`, renamed
`event_cmd_if_dash_held`, `_if_confirm_held`, `_if_pressed_dash`,
`_if_pressed_confirm`); `core/input.h` names them `ASSIGNED_DASH` and
`ASSIGNED_CONFIRM`.

**The searches' start.** The original doubles the object count in a byte, so
a count past 127 starts the searches from what is left of it: the endings'
and some battles' maps keep 143, 148 or 163 at `$7F:2000`. The three searches
(the walk's, the confirm button's and `field_object_in_the_way`) start at
`object_search_start`; the guards had required a count under 64, which turned
away 541 of the walk's calls in the library before the fix.

**The burndown's bridges.** `tools/burndown.py` read the call inside
`bridge_call`'s own body (`game/field/bridge.c`, and the battle's
`battle_call`) as an unreadable call-out of every port that uses a bridge, and
so credited those ports no callee at all. It reads the bridge at each
delegate's call now, as `tools/dontcare_check.py` does. On main it moves
`$C1` from 10.5 to 20.6 KiB (the battle's ports call out through
`battle_call`) and leaves `$C0` at 21.7; the walk, which calls out through
`bridge_call`, would have been credited 0.3 KiB of its 3.6 without it.

**What shadow mode found.** The recentring's state moved to "under way" before
its call-outs, where the original steps it after them (`WRAM[0138]` at the
call to `$C0:91AC`, in five movies). Then, over the 26 movies: every entry matches and none is interrupted:
`field_walk_the_leader` 194,763 calls (20 refused, above and TODO.md),
`field_press_confirm` 190,882, `field_recentre_the_screen` 174,
`event_party_back_together` 25, and `event_run_init_scripts` 243 with the
start as C; every other entry's totals are the first chunk's, the renamed
button conditions included. `ct_dev --native on --recomp only --misses`
interprets nothing on `field`.

**What fuzzing found** (seed 1, 20,000 states each, `tools/fuzz_routines.toml`):
a chest's item is nine bits, not eight (9,829 of 20,000 mismatched on the
item variant, at variable 0's high byte and the inventory's copy of Y). Then
every new entry fuzzes clean (PORTING.md's row), with variants that fix
what random state rarely reaches: the leader walking into the map's middle
with the player's control, objects close enough to talk to (the window-only
kind included), and the leader facing a chest from the game's list that holds
an item, gold or nothing of its own, each with its flag random. Against the
predecessors (5,000 states each), every field and event entry keeps its
mismatches (none); the searches' start compares more states of the moves
that search (`event_cmd_follow_object` loose 282 where 132).

**The timing gate.** The walk's time follows the map: standing, the
original spends about 1,000 clocks, walking into it ten times that, working
each point out at length where the port reads a few bytes. One scalar fit
that badly (4.5% of its stretches off by 1,024 clocks or more, the worst
16,456), so the port charges each look at a point, and more for the ground
where the point is no wall, at the walk's own rate (`charge_as`, as `field/motion.c`'s
search does): calibrated, the walk is within the gate's limits with no
exception. The new rows were calibrated over the library, and three of the
subsystem's moved with them: `event_run_init_scripts` 4,878 to 4,961 (the
party's start now runs as C in its call), `event_touch_contact` 4,250 to
3,351 (the review's changes to the touch moved its accesses) and
`event_cmd_walk_with_the_party` 4,641 to 4,634.

### Phase 3, field and events: the map scrolling in

The third chunk of bank `$C0`, the other half of the camera: what the
layers' tile maps hold as the screen moves over a map larger than itself.
`python3 tools/burndown.py` for `$C0`: 25.9 KiB ported, 5.0 inlined, 56% left
and 262 entries before; 30.0 KiB, 8.6 inlined, 49% and 266 after -- 4.1 KiB.
With it, the walk's 20 refused calls.

**The map's looks** (`field/streaming.c`). A map tile is 16 pixels and a
layer's tile map entries 8, so each layer's tile of the map has a look: the
four entries of its quarters (`field_tile_looks`, BG1's and BG2's, 512 of
them, the map's tile set decompressed over the map's data once that is
unpacked; `field_bg3_tile_looks`, BG3's 256). BG1 and BG2 take theirs from
one half of the looks or the other as the tile's properties say (bits 0 and
1, which the tile copy already knew as the tile set halves). A line of
entries along a row or down a column is built from the map tiles it crosses,
within each layer's mask of where its map repeats (`field_map_index_mask`,
`field_layer_index_masks`), into the layer's staging buffer
(`field_layer_staging`), which the vblank's deferred uploads send
(`field/display.c`). The six builders (`$C0:7612`-`$C0:7F57`: each layer's
rows and columns) are one function. Where a line starts on an odd 8-pixel
tile they differ: BG1's and BG2's rows count its first half as one of the
line's tiles and end with a half of the tile after the last -- its second
half, where the first would continue the line, which lies past the screen's
right edge (TODO.md); BG3's rows and every column add the half on top.

**The layers' moves** (`$C0:93E1`, every frame after the layers' scroll).
Each layer takes its move a pixel at a time, right, left, up and down: its
scroll moves, and its place in its 16-pixel tile; each time it crosses an
8-pixel tile the screen's edges in 8-pixel tiles move with it and the row or
column it crossed onto is due (`field_layer_edges_crossed`, a bit per layer a
side). BG2 may go at half the pace (`field_bg2_half_scroll`, from the map's
flags): it counts a place in 32 pixels, scrolls every other one, and keeps
edges of its own (`field_bg2_screen_left`...). A layer drawn whole at the
map's load (`field_layer_drawn_whole`) has nothing due, and BG3's waves move
their phase with it down the screen. Then each side's due lines are built
(the row above the view first, then below, left and right), each layer's
place in its tile map moves on, the uploads are asked for, and the edge runs
are worked out (`$C0:7F9A`, timed as itself). The original writes the lines
a byte at a time through the work RAM port; the port writes the staging
through its accessors and leaves the port's cursor where the original's is
(`cpu_io.h`: the port is memory access, which docs/PLATFORM.md keeps out of
the platform services, so nothing was added there). A move is taken in
locals and stored once, and a map tile's look worked out once for its two
entries.

**The layers at a map's load** (`$C0:75E9`, `$C0:78EC`, `$C0:7CB5`): BG1 is
drawn as the screen stands, 32 rows of 16 map tiles into the staging from the
first buffer's start; BG2 and BG3 whole where they are drawn whole (the map's
first 16 by 16 tiles, then the tile map's other half: the next 16 columns of
a wider map, the next 16 rows of a taller one, which makes the tile map two
screens tall, or the same tiles again), and BG2 at half the pace from its
own edges. The loop the screen's drawing runs down keeps its count in the cell
the edge runs read as their wrap (`field_layer_edge_wrap`), and leaves it 0,
which the edges then read: a real write, not scaffolding.

**The walk's refused calls.** The original doubles the map's object count
into a byte to start the walk's search for objects in the way, so a count
of 64 to 127 starts it on the slots past the tables, where it reads the next
tables' bytes as a slot's: whether it is in view, disabled or solid, and
where it stands (the objects' vertical positions as its horizontal one, their
velocities as its vertical one). In the game's terms there is nothing there,
and the search looks at the map's objects from the last slot down; the
original's reads are one marked piece of scaffolding
(`what_the_original_meets_past_the_tables`, `field/walk.c`, through the
tables' unchecked accessors), which sets the outcome where one of those slots
reads as close. The library makes such a count on a frame or two in six
movies (112 and 123 at `$7F:2000`): 20 calls, all compared and matching now,
and on none of them does the walk look for objects at all -- a debug build
that reported every search past the tables, run over the nine movies with
such counts, reported none -- because the leader isn't stepping into
anything then, so the game can't observe the reads there. Where it could -- a step that met such a slot -- the original would
stop there, and the leader would be up against nothing (the slot's byte has
a link to none's top bit) and stopped where it reads as solid.

**What the map gained.** The looks, the staging, BG2's own edges, the other
layers' index masks, the half-pace and drawn-whole flags, the edges crossed;
`in_view` and `solid` got unchecked accessors for the walk's scaffolding. The
map's data (`field_map_data`) and the looks share their bytes at different
times, which the map's overlap check refused: `re/ram.toml` gained a `reuses`
key for a variable that takes over another's bytes (`tools/ram_map.py`).

**What shadow mode and fuzzing found.** Over the 26 movies every call of the
new entries matches: `field_stream_the_layers` 194,853 calls (1 interrupted),
the drawings 308, 296 and 106 (49, 37 and 7 interrupted: a whole screen's
drawing runs over a frame's NMI, the paths fuzzing covers); `field_walk_the_leader` 194,763 calls, all compared and matching,
none refused. Fuzzing (seed 1, `tools/fuzz_routines.toml`) found the
drawings' loops: the screen's rows counted from a top edge near 255 wrapped
(615 of 5,000 mismatched before counting the rows rather than comparing
them), and BG2's rows at half the pace between edges the other way round,
or to a bottom of 255, run past the staging and the bank (the original's for
ever), which its guard leaves to the original. Then, 20,000 states each:
the streaming 0 of 20,000 (and 0 of 20,000 with every layer moving up to 18
pixels each way, and with BG2 at half the pace and the layers drawn whole or
not), the drawings 0 of 20,000, 0 of 16,596 and 0 of 20,000; the walk with a
count of 64-127 0 of 10,159 (its predecessor's guard compared 1). Against
the predecessors every field and event entry keeps its mismatches (none).

**The timing gate.** The streaming's time is a steady part and a
part the lines and the moves set, which the port's reads and writes don't
follow: charged at one rate, frames that built rows were over by up to
50,000 clocks and frames with moves drifted with their pixels. The lines'
and the moves' own accesses are left out of the estimate
(`charge_uncounted_begin`) and each entry (rows more than columns) and each
pixel is charged at the streaming's own rate instead, and the edge runs are
timed as their own routine: calibrated, 0.16% of its calls are off by 1024
clocks or more, the worst by 1,714. The drawings charge their entries the
same way, so a whole layer's drawing and a screen's scale alike. All four
rows are calibrated, and none is an exception.

### The tooling pass after Phase 3's first chunks

The first chunk of each of the three big banks (battle's `$C1`, field and
events' `$C0`, the menus' `$C2`) found gaps that made every chunk slower or let
a bug past the gate. This pass closed the ones that were tools.

**The native shape, checked.** Two of the three chunks wrote a value the game
reads only inside `ORIGINAL_EFFECT`: the battle's random draws divided by the
long arithmetic's leftover high word, and the menus drew a window's inside to
the size the frame's routine left, both written only as scaffolding. The
native-shape build compiles that out and was never run, and shadow mode runs
the default build, so nothing saw either. Two checks look now, one in the
source and one in a running game, and the gate runs both as a ratchet
(`tools/effects_check.py`, `tools/effects_baseline.txt`):

- *In the source.* For every write of a work RAM cell made inside an
  `ORIGINAL_EFFECT`, or in `#if ORIGINAL_EFFECTS` code, or in a function
  called from either (the `leave_*` and `original_cell*` helpers, to any
  depth), the check finds the reads of the same bytes by code outside all of
  that, in any function the game reaches (a registry entry, a table's row, a
  callback, and what they call). A cell is bytes, not a name: constants are
  followed through the helpers' parameters, enum constants and `#define`s, so
  `set_work_byte(g, LEFT_TURNED, 0)` is `battle_work`'s element 0 and a read
  of element 1 isn't reported; a helper's branches are all taken and an
  element past the variable's end drops out, which is what keeps
  `original_cell8`'s two accessors apart; a read and a write of different
  variables meet only where their bytes overlap within one subsystem (or the
  families sharing a page), since work RAM is reused between modes. Reads
  while a guard peeks (`vars_peek`) and in registry guards are the runtime's
  question, not the logic's, and don't count. It is textual (it borrows
  `tools/dontcare_check.py`'s reader of C) and takes two seconds.
- *In a run.* `ct_dev --native on --scaffold-reads` marks every work RAM byte
  with whether its latest write was made inside an `ORIGINAL_EFFECT`
  (`SnesBus.wram_scaffold`; the macro now counts its depth,
  `original_effect_depth`), and the accessors' reads report a marked byte a
  port reads outside one, by port (`runtime/port/scaffold_reads.h`). A write
  the original's code makes clears the mark. That is the question with its
  order: the native build, at that read, would have found what was there
  before the scaffolding's write. The vblank check's shipping-mode runs take
  the flag (they are the library in `--native on` already), so the gate gets
  it for nothing, and `effects_check.py --runs` reads their logs. The one gap
  is `event/leaves.c`, whose runner is `#if ORIGINAL_EFFECTS` code around the
  operation rather than an `ORIGINAL_EFFECT`: the source check sees it, the
  run doesn't mark it. The other way to run the native shape, a
  `-DCT_ORIGINAL_EFFECTS=OFF` `ct_dev` compared with the default build at the
  level of game outcomes, would find less for more: the shipping mode's timing
  already drifts from the reference, so outcomes part for reasons that have
  nothing to do with scaffolding, and a difference names no port. A mark names
  the reading port, the byte and the port whose scaffolding wrote it.

**Which cells count.** The source check reports a read whether or not some
port also writes the cell plainly, and says which (`[plain]` or `[only]`):
the check has no order, and the battle's case was exactly a cell other ports
wrote plainly, read after the one routine that kept it only as scaffolding. A
rule of "flag only cells nothing writes plainly" would have missed it. Nor can
it see the original's still-generated code, which sets most of the operand
cells the entry wrappers read (`[wrapper]` marks a read in a function that
takes the machine: usually the calling convention, sometimes a real input).
So the source check is broad, 131 findings on main before this pass, and the run check is the
precise one: over the library it found 15 port-and-variable pairs, each a read
the native build would get wrong or would have to show it doesn't. One was the
scene's and is fixed: the race's matrix generator left its column step in the
scene's cells only as scaffolding, and the column step's cell is the product's
third byte, which the next signed multiply adds into its product (once in the
jet bike race's 21,120 signed multiplies); the step is written plainly now,
and the multiply reads that byte through a field of its own
(`scene_call.product_third_byte`) rather than the whole product. Three are the
scene circle's, answered below; the rest are battle's, the field's and
events', and the menus' (docs/TODO.md, by subsystem).

Both are ratchets: `--check` fails on a finding not in the baseline, which
holds main's findings, the source findings of the conversion branch still in
flight (so its merge doesn't break on what it already had) and the library's
run findings. A source finding's line names the read's file and function,
what it reads (with the element, where the source names one) and the variable
written as scaffolding, and says `only` where no port writes the cell plainly,
so a plain write turned into scaffolding is a new line rather than the old
one. That is what makes the ratchet hold the battle's first chunk as it was
before its fixes: with the leftover high word, the stat offset's high byte
and the tech's hits kept only as scaffolding again (a scratch copy of main
with the three writes put back inside `ORIGINAL_EFFECT`), the check reports
the reads of the high word (`battle_wide_math[6]` and `[7]`, in `wide_word`,
which the random draw calls) and of the tech's hits (`battle_low_scratch[14]`)
as new; the stat offset's high byte (`[11]`) is a finding on main already,
another routine writing that byte as scaffolding too, and is in the baseline.
Its first cut missed the high word, which the arithmetic writes through a
setter it passes to a helper as a function pointer, now followed. The re-review's later cases (stats read through
the stat blocks' table wherever it points, which can be the direct page's
working cells) are aliasing through a computed address, which the source can't
see and the library's runs don't reach: a build of main with that fix taken
out finds no new run finding. A new finding is answered
before it merges, by making the write plain (it is the game's) or by showing
where the value the read takes comes from and adding its line with the reason
after a `#`. A line nothing produces any more is reported, not failed, since
the baseline carries the branches' lines until they merge; `--prune` drops
them. The scene's fifteen source findings left are answered in the baseline:
the scene program's operand cells (`scene_call`, one record whose fields are
each call's operands and results, so one call's leftovers sit where the next
reads its operands, which the program sets before each call; no run finds
one), and the circle's center and radius, whose reads take the low byte the
callers write, which the scaffolding writes back unchanged while it clears the
high byte (the runs do see those: the circle's jump draws it again without its
callers writing it). The core's one is the block-move stub's own port, which
reads what its caller wrote into the stub. 130 source findings were left on
main then; with the battle's and the menus' first chunks merged, 252 (counted
by element, as the baseline now keys them), and the
battle's chunk added eight run findings of its own (docs/TODO.md).

**A port that spans frames.** The menus' chunk ported the load screen's
opening (`$C2:E613`, which draws the screen through `$C2:E7C3`), which matched
where shadow mode could compare it (every call interrupted) but broke the
`beyond_time` route in the shipping mode: black screen, then a `BRK`. To
reproduce it without the lost port, `ct_dev --as-port BBAAAA` runs a routine
of the original as one hand port would run in the shipping mode: all at once,
the CPU stepped with time stopped and interrupts held, and its bus and
internal clocks owed afterwards at a `cost_milli` of 1000 (`dev/as_port.h`).
The drawing owes about 1.2 frames. The first vblank's NMI was taken at the
port's boundary, correctly; but the menus' handler (`$C2:840E`) moves the
stack up to its own at `$1DFD`, and `pay_debt` took a machine above the
boundary's S for one that had left the boundary, so it paid the remaining
436,911 clocks inside the handler, before its vblank work and with interrupts
held. The next NMI was then taken inside the first handler, whose stack switch
overwrote the S the outer one had saved, and the outer handler returned
through garbage. A debt now remembers that a handler was taken at its
boundary and waits for it wherever the stack goes, for up to a frame, after
which it is paid where the machine is (a handler that never comes back, such
as a task switch); coming back to the boundary clears it. So ports need no
rule for spanning frames. The same path had been delaying the menus' vblank
work on main by up to 11 lines, 12 times in `menus-field-menu`.
`tests/port/debt_test.c` covers a handler on its own stack, one on the
interrupted stack and one that never returns (with the fix taken out it finds
nine nested handlers and the main loop starved). The load screen's port can
come back unchanged; `$C2:E613` ends in the task yield (`JMP $83B2`), where it
stops with `m_stop_at`.

**Fuzzing.** `tools/fuzz_compare.py` runs an entry the `--before` binary
doesn't have on `--after` alone and reports it `new` with its own counts
(before, every new entry was a "before error"), and without `--before` runs
every routine that way, which is how a chunk's new ports go through the same
constraints file. `ct_fuzz`'s areas take a stride (`AREA:STEP`, a field of
each record in a table), a set of values to pick each element from
(`=V,V,...`, as wide as the widest written, so a script place or a record
offset is a real one) and a tie (`=@BBAAAA`, another cell's bytes, applied
after every other area), and it takes 32 areas (as the menus' branch had it).
A port that waits for the next frame through a menu task's yield (`$C2:821E`)
had every state skipped, the yield never coming back on the stack it was
called on; `--stop-at-call BBAAAA` ends each state's comparison at the port's
call-out to that routine instead, comparing the machines at the callee's entry
with the call's don't-care set (`menu_erase_slot` is the case). Running on
past the yield is still out of reach: that is the other task's frame, and the
fuzzer runs one stack.

**Shadow mode's report.** Mismatches are described apart from notes
(abandoned comparisons, timing), each up to its own limit, and mismatches
first, so a run of `abandoned` lines can't push the mismatch the gate fails on
out of the report; `ct_dev --shadow-details N` raises the limit (16, at most
256), and a line says how many more there were. An entry that runs only
inside another port's stretch (the menus' labels in `menu_open`) is compared
on its own where the original enters it, up to its first point: if that point
is the enclosing routine's too, it is compared there and goes on; otherwise it
is set aside at the enclosing routine's point, counted `abandoned`. What it
did to then is timed, so its `cost_milli` is calibrated from those stretches
(`menu_read_save_slots` has its stretches in `menus-field-menu`); what it
isn't is compared past that point, which would need shadow mode to follow two
comparisons waiting on different points of the original at once, the enclosing
one resumed by its callee's return while the nested one waits for a later
instruction: a checker over every active comparison rather than the innermost.
The enclosing comparison covers the stretch, and the shipping mode never calls
the nested entry there, so that is left (docs/TODO.md).

### Phase 3, field and events: the larger sprite layouts

The fourth chunk of bank `$C0`, and chunk 3's review. Of the three runs
PLAN.md named, the sprite layouts are the one the ports called out to most
(ten call sites from `field/sprites.c`, against three from the main loop's
ports and none into the frame builder). `python3 tools/burndown.py` for
`$C0`: 30.0 KiB ported, 8.6 inlined, 49% left and 266 entries before; 33.5,
8.7, 44% and 277 after -- 3.5 KiB.

**The larger layouts** (`field/sprites.c`, `place_larger`). An object's
sprites are 4, 8, 12 or 24 records; the 8-, 12- and 24-sprite layouts
(`$C0:BA65`, `$C0:BCDC`, `$C0:BFF2`, `$C0:C2BF`, `$C0:C73A` with its fours
`$C0:C6E7`) are placed as the 4-sprite one is: each four sprites' X at the
object's screen X and their ninth bits as one of the object's high bytes
(`object_set_sprites_high_bits`), then every sprite's Y; a rebuild takes the
offsets, and after the Y the looks, from the object's new frame. What
differs is which sprites show and which are hidden on the table's line. The
table keeps a sprite's Y as a byte, so one above the screen wraps round to
the table's last lines or, further off, onto the screen, and each of the
original's routines hides its own by where the object stands -- above the
screen's top, in its lower half or its upper half. The port works from the
sprite's signed top line and names each rule by what it shows (`ShowRule`:
"shown reaching down onto the screen", "hidden just below the screen",
"shown in the top half" and so on); each routine's three are a row of one
table (`ShowRules`), the 4-sprite layout's among them, each row's comment
saying what differs on the screen (the 8-sprite placing shows the sprites
below an object in the upper half wherever they land, where its rebuild
hides them past the middle). The 24-sprite layout is always placed, and an
object of it above the screen whose last sprite starts on the screen or less
than 256 lines above it isn't drawn that frame (the original answers
"nothing to draw" with its last sum's carry). That is the game's behaviour,
kept as plain logic, which returns only whether the object shows; the
overflow the original's code reads after a placing (its last sum's, or the
24-sprite layout's step past its records) is worked out from the state by
the entries (`placing_overflow`), which look before the update at what it
will do (`records_work`) to know whether it placed and whether it rebuilt.

**The frame's tile areas** (`claim_frame_areas`, `free_frame_areas`). The
frame builder draws an object's new frame into areas in bank `$7F` before
it goes to VRAM (`field_frame_staging`, four from `$7F:3800`, `$200` bytes
each, which chunk 4 first took for VRAM areas: the builder hands the address
to the WRAM port): one for the 4-sprite layout, two side by side for the 8,
three for the 12, which it claims for the object
(`field_frame_staging_claims`, `$C0:E952`, `$C0:E97A`, `$C0:E9AA`, recording
where the object's frame goes, `field_object_frame_staging`) and which are free
again once the records are rebuilt from the frame (`$C0:E9E2`, `$C0:E9FF`,
`$C0:EA1F`); a map's start frees them all (`$C0:E935`). The frees clear Y's
high byte (they take 8-bit index registers) and the update's callers read Y,
which the entries hand on.

**The draws are C.** With every layout's records its own, the sprite table's
building draws every object as C (it drew as C only where every object's
records needed nothing of the original's before), timed as the draw, the
update and the layouts' routines. The 4-sprite rebuild is C too.

**Chunk 3's review, folded in.** The walk's search stays typed in objects
(`in_the_party`, `stands_close` take a `FieldObject`), and what the original
meets past the tables overrides its outcome in one marked line,
`ORIGINAL_EFFECT(stops = stopped_past_the_tables(...))` (review rubric item
81). The current object's word is made current as a word plainly where the
map setup and the sprite table's building keep it so
(`object_make_current_recognised`, `object_recognise_current`), which the
effects check found read as a word by logic but cleared only as scaffolding;
its ten baseline lines are gone, and the contact's two carry their reason.
`field/field.h` names the field's geometry once -- the map tile, the entry,
the tile map's size, the screen's, the streamed lines' lengths -- and the
staging's lines (`field_staged_line`), which `layers.c`, `streaming.c`,
`display.c`, `walk.c` and `object_update.c` take; the tile looks' quarters
are fields in the map.

**What shadow mode and fuzzing found.** Over the 26 movies every call of
every entry matches: `field_place_8_sprites` 43,339, `field_place_12_sprites`
24,238 (54 interrupted), the rebuilds 554 and 569, the claims 37,507, 563 and
591, the frees 37,502 (8 interrupted), 554 and 569, the clearing 786; the
update, the draw and the table's building keep their calls, and the
building's interrupted calls rose from 336 to 1,472 of 193,483 (it runs as one
stretch now, which the frame's interrupt falls in more often; its fuzzing
stops at the budget on random rows, as before). Fuzzed (seed 1, 20,000
states, `tools/fuzz_routines.toml`): each layout's placing and rebuilding 0 of
5,000 on random positions and 0 of 20,000 with the object above the screen,
the claims 0 of 20,000 each, the frees and the clearing 0 of 5,000 and 20,000,
the update and the draw with the larger layouts 0 of 1,252; a deliberately
wrong rule (the 8-sprite placing's upper half as its rebuild's) mismatched 592
of 3,000, which is what the variants are for. Against the predecessors every
field and event entry keeps its mismatches (none).

**Chunk 4's review, folded in.** The placing's logic returns only whether
the object shows, and the entries work out the overflow and Y the original's
callers read from the state (review rubric items 82-84); the show rules are
named for what they show and are one table, the 4-sprite layout's row moved
out of the platform (`vid_oam_y` is gone; `VID_OAM_HIDDEN_Y` stays). The
frame's areas turned out to be staging in bank `$7F`, not VRAM (the builder
hands their address to the WRAM port), and are mapped so
(`field_frame_staging`); their owners are links to objects. The objects'
sprite high bytes are byte tables reached through one table of the three,
since they lie apart in memory. Over the 26 movies every call still matches;
fuzzed against chunk 4's build with the same seed, all 26 sprite entries and
variants keep their counts (0 mismatched), and seven rows' `cost_milli` were
recalibrated.

**The timing gate.** The new rows and the update, the draw, the building and
`event_run_init_scripts` were calibrated over the library; no exception.

### Phase 3, field and events: the frame builder

The fifth chunk of bank `$C0`: the frame builder, which the previous chunk's
tile areas are drawn by. `python3 tools/burndown.py` for `$C0`: 33.5 KiB
ported, 44% left and 277 entries before; 37.2, 37% and 278 after -- 3.7 KiB.

**What it is** (`field/frames.c`, `field_draw_next_frame` `$C0:CAD9` with
its layouts `$C0:CBDC`, `$C0:CEF5`, `$C0:D4F7` and their parts). When an
object's animation moves on to a frame its tiles don't show, the frame queue
(`$C0:CA76`, still interpreted: it stops at a scanline) has the builder draw
it: the next frame (the animation's, through the frame read, or the one a
script holds) is claimed tile areas in bank `$7F`, its tiles are copied there
from the object's graphics set as its description names them (straight or
mirrored, by the copies `field/graphics.c` already had), the areas are
queued to be sent to the object's tiles in VRAM, and the frame's sprites --
offsets, tiles, attributes -- go into the frame's data, which the records are
rebuilt from. The 8- and 12-sprite frames are drawn a part at a time, one
part each time the queue comes to the object, and the parts are counted in
the records' state byte, which is what holds the records' rebuild until the
frame is whole. The original's six near-copies (a layout, and for the
12-sprite layout whether its tiles start half way along a row of VRAM's, at
tile `$68`) are four rows of one table of plans: which of the description's
tiles each part draws where, the two stretches sent to VRAM, where the
sprites' tile numbers skip a row, and which sprites take the object's upper
half's priority (for the 4-sprite layout, by where each starts).

**What it taught the map.** `$0F01` was named for the battle; it is the frame
the object's tiles show (`field_object_frame_drawn`: coming into view and a
script's layering forget it, `object_forget_frame_drawn`, so the next frame is
drawn whatever it is; the battle keeps it for the party). The object's
graphics set and frame descriptions (`$1200`/`$1280`, `$1300`/`$1380`), its
tiles in VRAM (`$0D00`) and its palette (`$0F81`) are mapped, and so are the
builder's own cells: the queue's end (`$79`), the description's pointer and
the count on the field's page (whose `field_work` stops at `$C8`).

**What the callers read.** The frame queue reads the carry (more of the frame
to draw: the object stays at the queue's head) and hands the overflow on;
liveness keeps the overflow, so the entry works it out after the logic from
what the logic came to and the state (`build_overflow`: the frame read's,
the last copy's step along the areas, the second stretch's place, or the
tiles' skip of a row), with the overflow and the animation's frame from
before the call.

**What shadow mode and fuzzing found.** The first run mismatched a fifth of
the calls on `$D9`, the attributes' cell the original leaves (now
scaffolding); the guard then refused one real call, an object with no areas
yet ($FFFF) in the middle of its frame, which only a first part's claim
would give it: the guard asks only of the parts after the first that their
areas lie inside the bank. Fuzzing found graphics built in bank `$7F` whose
tiles the frame names run past the bank, where the copies read low RAM
through bank `$80` (the builder's own cells among it); the guard looks ahead
at the frame the builder will draw (`object_peek_next_frame`) and refuses
those. The look ahead first read the frame tables' bank before their offset,
which moved the frame read's open-bus byte; `field_read_animation_frame`'s
fuzzing caught it (604 of 4,089) and the bank is read last again.
Over the 26 movies every call matches: 231,605, none interrupted and none
refused. Fuzzed (seeds 1 and 2, 20,000 states each, `tools/fuzz_routines.toml`):
object 8 animating with the 4-, 8- and 12-sprite layouts 0 of 13,837 and 0 of
13,743, holding a frame 0 of 13,795 and 0 of 13,724, the 12-sprite layout
with its tiles at `$68` 0 of 13,766 and 0 of 13,624 (the rest turned away by
the guard); a deliberately wrong plan
(the split layout's last run a byte off) mismatched 1,749 of 3,465. Against
chunk 4's build every other field entry keeps its counts.

**The timing gate.** The new row was calibrated over the library; no
exception.

### Phase 3, field and events: the switches and the main loop's requests

The sixth chunk of bank `$C0`, and chunk 5's review.

**Chunk 5's review, folded in.**
- *The overflow past the frame queue* (the port-routine skill's third trap).
  Liveness keeps it live round the main loop, but the only readers it
  reaches are the menus' entry, which saves the flags (`$C2:8013`'s `PHP`)
  and puts them back on leaving; banks `$C0` and `$FD` have no branch on
  it. With no port call-out there to poison, it stays derived, from a
  status of game conditions (`FrameBuild`, field/parts.h) in field/leaves.c,
  outside the logic file.
- *The copies take their set and place.* `field_draw_frame_tile(g, set,
  tile, to)`; the work RAM port's final place and the cells only the
  original's code reads (the set, the place, the frame tables' bank) are
  scaffolding, and the copies' entries read them as their calling
  convention (baseline lines with that reason).
- *One frame read.* `object_take_next_frame` takes the tables' bank, which
  the `$C0:CB3A` wrapper reads; its guard refuses a bank where an address
  doesn't answer (`field_bank_always_answers`), so the read order no longer
  matters and the twin is gone. The mirrored copy's guard does the same for
  the tile's bytes, which fuzzing showed carry out of bank `$FF` into bank
  `$00`'s open bus (38 of 9,944 before the guard).
- The tile's constants live once in field/graphics.h; the 4-sprite
  priority rule and the `$68` plan say what they are; `object_holds_frame`;
  `sprite_offset` takes an `Axis`.

**What it is** (field/requests.c, field/switches.c). The main loop's
`$C0:0C76` moves the brightness and tint changes a step each frame, and
answers the requests. The port answers a step event itself: the step is
remembered on the map, and a floor switch stepped onto is pressed -- its
tiles move on to the next in the tile set, in one of three shapes (a tile
and the one above, two by two, two across by three down, `SwitchShapeTiles`,
where the original has five near-copies), its sound is played, and the
VRAM addresses of its tiles' entries in BG1's tile map are queued. The
line interrupt's job after the picture (`$C0:ED15`) draws the queued looks
(field_redraw_changed_tiles), a chest's as well, and refreshes the windows'
corners. The location change, the menus, a chest's opening, the tile copy
and the battle are handed to the original's code where it reads the
requests (`m_stop_at_own` at `$C0:0C8A` and `$C0:1770`). A chest's opening
went with the stop: at its sound the original has A's high byte and the
overflow from the path into it, which only the path gives, so porting it
means porting the path (TODO).

**What it taught the map.** The redraws' flags (`field_tiles_to_redraw`,
`$5F`), the switch's shape, the chest's kind, the two sounds, the entries'
VRAM addresses (`$09B2`, `$09CA`), the steps remembered (`$7F:1CC8`), and
the bytes after the screen's left and top edges (read as words).

**What shadow mode and fuzzing found.** A chest's opening mismatched once
(telepod-lavos): the sound driver's call came back through a path the
original reaches with other registers; the chest went to the stop. The
switch's sound reads A's high byte (the driver keeps it), which the port
works out from the switch's tiles (field/leaves.c). Over the 26 movies every call of the three matches: the requests 191,044,
the redraw after the picture 98,342, the frame builder 231,605; none
interrupted or refused. Fuzzed (seeds 1 and 2, 20,000 states each):
`field_handle_requests` 0 of 19,944 and 0 of 19,946 with any requests but
those it hands on at once, and a step onto each switch's bottom tiles 0 of
9,866 and 0 of 9,882 (the rest past the call-out budget); the redraw 0 of
20,000 each. A deliberately wrong shape (one of the two-by-two's tiles off
by one) mismatched 289 of 1,433 in the requests and 492 of 3,000 in the
redraw. `python3 tools/burndown.py` now credits `$C0` with 56.4 KiB and 5%
left, which is wrong the way its docstring warns: it walks
`field_handle_requests`' region through the branches the port hands to the
original at `$C0:0C8A` and `$C0:1770`, into the location change's reset to
the main loop and everything that reaches. What the chunk made C is the
step event's switches and the redraw, about 2.6 KiB.

**The timing gate.** The new rows, and the two chunk 5's review moved (the
mirrored copy, whose estimate no longer counts the port's writes through
the port, and the builder), were calibrated over the library; no
exception.

### Phase 3, field and events: the drawn frame put on the records

The seventh chunk of bank `$C0`, and chunk 6's review.

**Chunk 6's review, folded in.**
- *The switch sound's A-high byte* (the port-routine skill's third trap). Its
  reader is named: the sound driver saves A on the stack as it starts
  (`$C7:0147`'s `PHA`), so the byte stays in the stack below the caller's
  frame, which shadow mode compares. Liveness has the driver read it, so it
  can't be declared dead; the derivation stays in field/leaves.c and reads
  the map through switches.c's own decoding (`field_map_byte`), where it had
  read column 63 as BG1's (review rubric item 94).
- *Origin tags* (item 95): `redraw_quarter` takes no site; each branch tags
  its uploads' start with a literal.
- The guard refuses a one-by-two switch whose upper tile is the tile set's
  last, and the word-wide move is gone; `leave_entries` leaves its cells
  through `original_cell16`, and `$D9` is one named cell
  (`FIELD_LAST_WORKED_CELL`); the redraw after the picture is in display.c
  with `field_windows_need_refresh`; one `pressed_shape`; the chest kinds are
  named (`ChestKind`, by where each is in the tile set). The switch sound keeps a site a switch: the call's
  return is where the port stops if the call comes back otherwise.
- The mirrored copy takes a place in the build bank, like the straight
  copy, and its guard takes a port there alone, where the frame builders put
  it. The fuzzer can't set the port (its saved states have it in bank `$7E`,
  where the sprite table's building leaves it), so the entry's row compares
  nothing; the copy is fuzzed through `field_draw_next_frame`.
- *The burndown's overcount.* `tools/burndown.py` no longer walks on from a
  place a port hands back at, and a stop at a call's return counts as a
  hand-back where it declares what is dead there (a bare one is still a
  bridge giving up); `tests/test_burndown.py` covers both. `$C0` goes from
  5% left to 36%, and the total from 41% to 51%.

**What it is** (field/frames.c, `field_put_drawn_frame` `$C0:CB0A` with its
layouts `$C0:CDC8`, `$C0:D124`, `$C0:DD28`). The frame builder's twin, which
the field's return from a battle calls for each of the party and the enemies
it puts back ($C0:07EC, $C0:08A0, by `BRL` from their own code): it puts the
frame the object's tiles already show straight onto its records, built, and
draws nothing, since the tiles in VRAM are as the battle found them. It is
the builder's frame sprites (the same plans, the `$68` split's too) going to
the records instead of the frame's data (`sprites_set_record_sprite`). A
registry address inside a routine is a block boundary for the generated
code, so the entry takes the `BRL`s as calls; its return goes to its
callers' own callers, which read none of the registers.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (84, none interrupted or refused). Fuzzed (seeds 1 and 2, 20,000
states each): each layout 0 of 20,000, the 12-sprite layout at `$68` 0 of
20,000; the 4-sprite priority rule a line off mismatched 4 of 3,000.
`$C0` stands at 39.4 KiB ported, 34% left.

**The timing gate.** The new row, and the mirrored copy the review moved,
were calibrated over the library; no exception.

**Battle's findings, answered.** The long arithmetic's cells (`$28`-`$33`,
the 8-bit multiplies' operands and the multiplier's copies) are game state:
the original's callers multiply and divide what the last call left there, so
`game/battle/arith.c` writes them plainly and only the multiplier's and
divider's own state is scaffolding. So are the reach tests' product
(`battle_product`), the direction's cells, the fade tables' last intensities
and the tile decoder's row cells (`$53`-`$57`, which a screen effect's settings
read where its command doesn't set them), and the enemy at work
(`battle_enemy_turn`) as the choosing leaves it, written after its scaffolding
so that the plain write is the last. What is left in the baseline for battle
is the entries' operands, each line with its reason. `battle_run` now runs a
sibling as C only where the registry gives that sibling's entry for the state
(`native_hand_entry`, guard and preconditions included); otherwise it makes
the call as the original's, with nothing declared don't-care, and the runtime
runs whatever the registry has there. The library's gate passes unchanged,
with `battle_enemy_scripts_choose` recalibrated (8,036 to 7,902) for the
plain writes it makes now.

### Phase 3, field and events: the field put back after a battle

The eighth chunk of bank `$C0`, and chunk 7's review.

**Chunk 7's review, folded in.**
- The switch sound's A-high comment gives the real reason the byte is
  compared: the sound driver keeps its saved-register frame, A with it, on
  the stack while it waits on the sound CPU, so shadow mode compares it as
  the live stack it is there, and liveness counts `$C7:0147`'s `PHA` as a
  read (not "a byte below S", which PORTING.md says is not compared).
- `sound_sites` is indexed by the switch's shape and column with
  designated rows, and `switch_number` is gone.
- frames.c has one test for whether an object is drawn
  (`object_on_screen`), shared by the builder and the put-back; the
  24-sprite object is drawn, its frame is just not put back.
  `object_put_drawn_frame_back` returns nothing, and the header says what the
  after-battle put-back is.
- `$D9` has one name (`FIELD_SCRATCH_CELLS`, with `FIELD_LAST_WORKED_CELL`
  as the same cell by its use); the graphics port's guard asks whether the
  bank is `FIELD_BUILD_BANK`.
- `tests/test_burndown.py` asserts the code after a stop that declares a
  dead set isn't credited, that code reached around a stop is, and that a
  callee a port stops at is not inlined (the burndown now leaves those out).

**What it is** (field/battle_return.c, `field_return_from_battle` `$C0:0715`
with `$C0:076B`-`$C0:0904`). When a battle on the field ends, every enemy it
took over goes back to its tile on the map, the one the battle kept from the
screen's corner, into the rows the sprite table walks, showing the frame it
showed (field/frames.c's put-back, the `BRL`s at `$C0:086E` and `$C0:0902`);
an enemy the battle took away (its kept kind 0) is made unreachable and
hidden, giving up its palette if it has none of its own sprite. The party's
places follow (the first taken without looking, as the original does), the
sprite table is built at once (`$C0:B271`, a call-out), and where the party
was defeated each member holds the frame it shows. That last part is also an
entry of its own, `field_hold_party_if_defeated` `$C0:072E`, the return from
the sprite table's building, which is where the original goes on when the
guard sends the return to it, and what fuzzing compares apart from the call.
The battle's kept enemy objects (`$7E:01A3`) are named in the RAM map
(`field_battle_enemy_objects`).

**What shadow mode and fuzzing found.** Fuzzing found one guard: the
original makes each object current by its link's byte alone and the frame
put-back reads the whole word, so a byte after the link makes the put-back
see no object; the guard takes a return whose current object's word is clear
above the link (`object_making_current_recognises`), as every return in the
library has it. Shadow mode found the other: the original gives up a
taken-away enemy's palette from two subroutines down (`$C0:076B`, then
`$C0:07EC`, which saves the enemy's place and object over the call), and
shadow mode pairs a call by its stack pointer and compares that frame, so the
port pushes the two return addresses and the saved bytes around the call-out
(PORTING.md, "A callee's frame that is still on the stack"). The fuzzer
compares the machines when both have returned, not at each call, so only `late-omen-battles`, where a
battle takes away an enemy with no sprite of its own, showed it. Over the 26 movies every call matches
(`field_return_from_battle` 25, none interrupted or refused;
`field_hold_party_if_defeated` 25). Fuzzed (seeds 1 and 2, 20,000
states each): the return 0 of 20,000 each, compared up to the sprite table's
building (whose own call-outs can't finish on the fuzz state, TODO.md), with
the enemies taken away or kept, with a sprite or without; the defeat's end 0
of 20,000 each, won, lost or fled. The rows are sensitive: an enemy's kind
read the other way mismatched 2,993 of 3,000, its sprite test 1,357, and the
defeat test 1,915. `$C0` stands at 39.9 KiB ported, 33% left.

**The frame builder's overflow, through battle's V check.**
`tools/v_readers.py --after C0:CAD9` has V read after the builder returns,
so its derivation (field/leaves.c) stays: the sprite table's building
after the frame queue (`$C0:00D9`) leaves V as it is, the frame wait passes
it on, and the next frame's pause and menu opening (`$C0:18D9`) reach the
sound driver's saved flags (`$C7:0A39`'s `PHP`, taken back other than by
`PLP`) and the menus' `BVC` at `$C2:F7D9`.

**The timing gate.** The two rows were calibrated over the library; no
exception.

### Phase 3, field and events: the pause and the menus opened from the field

The ninth chunk of bank `$C0`, and chunk 8's review.

**Chunk 8's review, folded in.**
- *The palette give-up is a port* (field/palettes.c, `field_give_up_palette`
  `$C0:734C`). The field's eight sprite palette slots are shared by objects
  with the same colors, each slot's sharers a list in the order they took it
  (`field_object_palette_older`/`_newer` at `$0B00`/`$0B01`, the newest by
  slot at `$0B80`), and an object giving its slot up leaves the list; while a
  script has loaded palettes itself (`$0154` bit 7, event `$33`) objects keep
  out of the slots. The battle's return and the view update
  (field/object_view.c) run it as C, so the rebuilt JSR frame, the call-site
  arithmetic and the `Snes *` in `bring_back_enemy` are gone, and so is the
  view update's call-out. Fuzzed (seeds 1 and 2): 0 of 20,000 each; breaking
  either branch mismatched 159 and 1,623 of 3,000. Over the 26 movies every
  call matches (350, from the view update and the return).
- The return's work is a `GameState` function (`put_back_after_battle`);
  the entry keeps only the sprite table's bridge and the defeat's hold.
- *The `$016E` guard, named.* The frame put-back's layouts read the current
  object's whole word under 16-bit index registers (`$C0:CE57`, `$C0:D21C`,
  `$C0:DE91`, `$C0:E08C`), so a byte after the link makes them draw another
  object's tiles. The condition stays, with its readers named, and the
  put-back's own guard (`field_frames_can_put`) takes it too; it refuses no
  library call.
- One `BattleResult` (battle/battle.h), which battle/main_loop.c sets and the
  field reads; the party's places are `put_party_back`, with `PARTY_PLACES`
  from field/party.h and the leader's place left to the guard; the taken-away
  kind is named. The screen's corner stays in `field_own_tile` through its
  accessor: `original_cell8` reaches the field page's scratch run
  (`$C1`-`$E8`), and `$F0` is the direction routine's named tile.

**What it is** (field/menus.c). Once a frame the field's main loop hands
the buttons to `field_pause_and_menus` (`$C0:18D9`): Start pauses the field
while the player has the party and no script has locked the pause (the
picture at half its brightness, the play time clock stopped, until Start
again); the Start assignment (`field_press_start`, `$C0:1985`) moves an open
dialog window between the top of the screen and the bottom, or, with none
open and the story's progress at `$49` or past, opens the menus at their mode
5; X opens the main menu (mode 1); otherwise a waiting choice takes the
buttons. A menu opens only while the player has the party, no choice waits and
no script has locked the menus: the field fades out a step a frame, keeps the
party's places, and calls the menus; after them it sets itself up again,
brings back the characters of a party the menu changed
(`field_bring_the_party_back`, `$C0:1A03`: each character's object starts
again in its script, Frog's before Robo's as the original has them, and the
party goes back where it was kept), and fades in. The Start assignment's menu
also takes a menu a script asked for as shown. Most of the work is still the
original's routines, called out to from their own calls (the frame wait, the
menus, the field's setup and fade in); the two menus' call sequences are one
function over a table of their sites. The RAM map names the pause's and the
menus' locks (`$0110`, `$0111`), the brightness kept over the pause, the
characters' objects (`$8D`-`$93`), the party as last loaded, and the party's
places kept over a menu (`$7F:1D09`-`$7F:1D1D`).

A's high byte and the overflow are read after both entries, so the port
leaves A's high byte as the routines it called left it and declares only its
low byte dead; the Start assignment's subtraction of the story's progress
sets the overflow, which field/leaves.c works out (`field_story_test_overflow`:
`tools/v_readers.py --after C0:1985` has it read).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches: `field_pause_and_menus` 191,044, `field_press_start` 1,
`field_bring_the_party_back` 3, none interrupted or refused. Fuzzed (seeds 1
and 2, 20,000 states each), compared up to the first frame wait, the blanking
before a menu or a choice's call, since the pause and the menus run for
frames: the buttons 0 of 19,450 and 19,424 (550 and 576 past the call-out
budget, where the Start assignment's own calls run on), the Start assignment 0
of 20,000 each, the party put back 0 of 20,000 each, and the characters'
starts, compared up to the first, 0 of 20,000 each. The rows are sensitive: a
pause at a quarter brightness mismatched 342 of 2,922, the menus unlocked 126
and the story's overflow a step off 155 of 3,000, the trail left alone 2,945,
and the player's control ignored 163.

**The timing gate.** The three rows were calibrated over the library; no
exception.

### Phase 3, field and events: the field's start

The tenth chunk of bank `$C0`, and chunk 9's review.

**Chunk 9's review, folded in.**
- *Ports reached through the machine* (review rubric item 61). menus.c
  blanks the screen and takes the interrupts back as C
  (`display_blank_and_stop`, `field_take_the_vblank_interrupt`,
  `field_take_the_line_interrupt`, each timed as its routine), setting the
  CPU's interrupt mask and data bank the original's blanking leaves. Three
  ports stay machine calls on purpose, said beside their addresses: the
  line effects' builder, whose entry works out the registers it leaves (the
  overflow among them is read after the pause), and the Start assignment and
  the party's return, whose own call-outs must be made under their return
  address, where shadow mode pairs them. The file header says which is which.
  The menu path after the menus' return runs on the machine for frames, so
  the fuzz rows stop before it (now at the field's keeping, `$C0:011B`);
  shadow mode compares it (the blanking 549 calls and the handlers 335 over
  the library, the menus opened in `menus-field-menu`).
- *Shared vocabulary.* `WindowPosition` is field.h's, and the Start
  assignment swaps the window between the top and the bottom (its guard takes
  a window at one of the three places); `FIELD_SCRIPT_MENU_SHOWN` is
  `field_effect_status` bit 6 (event `$C8` waits on it, `$C0:3757`);
  `party_is_the_players`, `dialog_choice_waiting`, `field_pause_is_locked`,
  `field_menus_are_locked`; `FIELD_START_MENU_FROM_STORY` in field.h.
- The two openings are one `MenuOpener` and one table row each, with what
  differs between them (the menu, the X it is given, whether a script's
  menu counts as shown) in the row; `open_menu` says why N and Z are clear.
- The kept priorities are two byte fields; `battle_how_it_ended` types the
  result; the pause's display flags are named; the eight palette slots; the
  Frog-before-Robo order can't be seen in play (each start writes its own
  object and character entry); `object_palette_links_in_the_tables`.

**What it is** (field/start.c, `field_start` `$C0:0B86`). Before the field
sets up its first map, after the title, a load or another mode, its state is
put as a map's arrival expects it, in eight operations named by what they
leave: the location also put aside; nothing under way (requests, status,
locks, screen jobs, tile copies, transitions); a dark plain screen; no
dialog; the party the player's; no objects in the party's places or standing
for the characters; the sprite tables started from scratch
(`sprites_hide_all_next_frame`, and the sprite loading's four tables from the
words at `$E4:FFE0`, through field_data.h); the usual sounds; and the random
numbers from the play time. It calls nothing. The cells the map hadn't named
are named from their readers (a CPU-side watch over three movies for the
readers, then the listing): `dialog_speaker_high` (`$2F`, which `$C0:1FDD`
reads with the speaker as a word), `field_screen_job` (`$39`),
`field_built_sprite_object` (`$AE`), `field_sprite_data_tables`
(`$AF`-`$BA`), `field_map_has_no_bg3_set` (`$BB`), `field_battle_song`
(`$FA`, which the battle's setup plays through `$C0:1BAB`) and
`field_bg3_map_size` (`$7E:0BE9`).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (243, none interrupted or refused). Fuzzed (seeds 1 and 2) over
random states: 0 of 20,000 each; leaving one cell unwritten mismatched 997 of
1,000.

**The timing gate.** The row was calibrated over the library; no exception.

### Phase 3, battle chunk 2: an effect worked out (`$C1:D132`-`$C1:F1EF`)

An enemy's turn is C to its end now, but for the original's code it calls out
to (the tech's targeting, messages, bringing enemies in and the showing): the
attack's and the tech's effect is worked out in C (`game/battle/effect_work.c`,
`$C1:D7C4`, `$C1:D8D1`), and the notes of the action for its targets
(`$FD:AAD2`, `$FD:AB01`) are C inline in the turn. The work is in four files:

- `effect_work.c`: going through the targets, the effect's record decoded
  (`EffectRecord`, `effect_data.h`) and its kind run by a switch, each case
  timed as its own routine (`$C1:D9D7`'s kinds with work, each a registered
  entry for the party's code, which still runs them from `$C1:D523`); what
  acted on each target noted (`$C1:D508`), the hits' numbers added up
  (`$C1:EB68`), and the entries with their guards;
- `effect_amounts.c`: healing (`$C1:DA37`), draining (`$C1:E77B`), a hit's
  tests of landing (`$C1:DE87`, `$C1:DE9E`, `$C1:DEF0`, `$C1:DC64`), its
  damage by the eight formulas the dispatch reaches (`$C1:E0F5`; its formulas
  9 and 10 are behind a test that sends every kind but 8 to the weapon's), the
  defence (`$C1:E65F`), and the damage applied (`$C1:DCAD`), with the damage
  table and the numbers shown recorded (`$C1:EBF8`, `$C1:E89F`, `$C1:E8E2`);
- `effect_statuses.c`: curing, inflicting and the status timers (`$C1:DB5E`,
  `$C1:DBAA`, `$C1:E006`, `$C1:E976`);
- `effect_modifiers.c`: the modifier record decoded into its routine and what
  its two numbers mean for it (a percent, a stat, the families singled out,
  ...), applied by a switch (`$C1:FA61`'s routines, `$C1:EB1C`, `$C1:EA6F`).

The statuses' bits and the stat block's offsets the work reads are named once,
in `battle/combatant.h`.

**What stays the original's.** Stealing is the party's alone (no enemy's
action steals): its item goes to the party's inventory or its battle records
(`$FD:B655`), which is the party's chunk, so the steal kind isn't registered
and the entries turn away a hit that steals, and the rare item's chance with
it. The clock modifier (`$C1:EE27`) reads the controller mid-work, a read that
waits on the console (the port leaves that wait to the original, as
`core/input.c` does), and no enemy's action has it: the entries turn it away.
So does a modifier routine past the table's 21, a kind past `$2C` (none in the
ROM), and a drain where the byte the original reads its stat's offset's high
byte from (`$1B`, which nothing in the effect code sets and the game leaves
clear) isn't clear. With that, the work makes no call to the original's code.

**The arithmetic.** The work multiplies and divides plainly (`effect_times`,
`effect_over`), with no carry in or out. The long arithmetic's cells are game
state (`arith.h`): a division divides the high word the last call left with
the number it is given, which is how a product just made is divided whole,
and the random draws divide by that high word too. The carry the original
comes into a call with matters to the game in one place, and the port names
it: the evasion's level bonus (`level_bonus` in `effect_amounts.c`) divides
twice with the carry set, so the second division divides a number of over two
billion and the bonus is its quotient's low byte, not the levels over twenty
(`battle_divide_leftover_marked`). Two more leftovers move a result and are
kept with a comment: the defence's quarter steps multiply with the high byte
of the last number the arithmetic worked on, and the element's scale of 4 is
set a byte at a time over the high byte the hit's division by 256 left, so it
is 260 as a rule. The shares a formula's random part is drawn up to are
multiplies (77/256, 205/1024), but for the tenth of a word, which the
original's shifts round part by part.

**What the machine still wants, as scaffolding.** A multiply leaves the carry
its caller came in with in its second number's cell, which only the
original's code reads (a party caller divides by it, which the effects
check's runs found): the multiply leaves 0 there, its entry the machine's
carry, and the few callers in the work whose carry is set and whose multiply
is the last before the work returns say so in `ORIGINAL_EFFECT`
(`battle_leave_multiply_carry`). The kinds hand the party's code back the
carry of their last note (set), A's high byte clear and Y past the note's
bytes: the entry wrapper sets them. The hits' numbers added up leave the carry
of the compare that chose the last kind to move, worked out once after the
adding from the two records' kinds (`leave_last_compare`), and a target after
the first is taken with the carry the note before it leaves
(`take_next_target`).

**The overflow flag, declared dead.** The original's adds leave the processor's
overflow flag as their last result had it, and the effect worked out for an
enemy hands it on to the action's showing (`$C1:BFA4`), the battle's step
(`$C1:0003`) and what follows the action (`$C1:B762`), which save it with the
flags across their frame waits. Nothing tests it: there is no `BVS` or `BVC`
in the battle's banks (the menus' are the ROM's only ones), and the one `PHP`
the code after reaches whose byte isn't put back by `PLP` (the sound driver's,
`$C7:0A39`) throws it away. The liveness analysis agrees where it can follow
the code, and the work-outs declare V don't-care (`DC_V`) on that; but the
strict solve the call-outs are held to gives up at the jump tables the
animation scripts (`$C1:75D1`, `$C1:7633`) and the enemies' scripts
(`$C1:874E`, `$C1:B006`) dispatch through, whose targets come from data, and a
fuzz run can't settle it (the showing waits on frames past any budget). Two
new pieces of evidence do. `tools/v_readers.py` solves liveness as strictly,
but follows each of those four tables to every word its dispatch can index:
each is `ASL A` on an 8-bit A, `TAX` and the dispatch, with nothing jumping
past the `ASL`, so where A's high byte is clear X is at most `$FE`, 128 words,
each code the analysis traced. The bound is an `index_reach` hint on each
table in `re/hints.toml`, and the tool checks every part of it, A's high byte
too: `tools/disasm/high_byte.py` follows it through every path and call the
analysis traced, and has it clear at all four `ASL`s, resting only on D's
high byte being zero at the `TDC`s that clear it (the battle's direct page is
`$0000`). `tests/test_v_readers.py` holds the hints to the ROM. The two
condition dispatches after the first (`$C1:8769`, `$C1:B021`) carry no bound:
44 of the routines the first's words reach return with A's high byte unknown,
and V is dead into the three callees without them. `ct_dev --index-check`
records X at the four dispatches over the library on the interpreter alone:
never past `$FE` (135,810 runs, the largest X `$7E`). With the bound, V is dead
into all three callees. `ct_dev --poison-site` inverts V at a call-out's own
site in the shipping mode; the library with it at the three declarations'
sites traces the same as without in every movie, the picture included, but
for the stack's page, where the inverted flag the callees push lies after they
pull it. Per site: 441 calls into the showing, 6,440 into what follows the
action, and of the step's six sites 285 from the menu's (`$C1:856D`) and 4 to
32 from the other five, which rest on the static result alone. The three
call-outs declare V don't-care with a `dc-evidence:` note naming both and the
per-site counts, which `tools/dontcare_check.py` reads and re-checks
(PORTING.md, "Declarations liveness can't prove"); `tools/dc_evidence.py`
makes the runs again. The work computes no overflow.

**What it found.** The drain reads its stat's offset as a word from the cell
its kind is handed in; a modifier that fells its target outright marks it by
battle slot from `$AEB0`, so a party member's mark lands in two bytes before
the enemies' (`battle_beaten_marks_low`) or on "any enemy beaten"; about a
tenth (`$C1:C1B2`) is three subtractions, not four; formula 8 works nothing
out and takes whatever the quotient cell holds; a number's place is a byte,
so from the fourth hit of an effect that hits repeatedly a set of numbers
reaches past the records into the effect's own flags, as the original's does;
and once one of those hits lands, the rest are made sure by setting the
effect's flags to "can't miss" alone, so they lose the status chance and the
barrier and shield passes the first had.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the enemies' work-outs 347 techs and 23 attacks (175 and 9 compared whole,
the rest interrupted), and through the party's code the kinds 765 hits, 86
items' restorations, 60 restorations, 39 statuses, 26 drains, and a few cures
(229, 48, 52, 35, 23 compared; 32 hits turned away, the party's steals and
clock). The repeated hits, the item's status, the lost condition bit and the
showing of the actor's HP aren't reached by any movie.

**Fuzzing.** Every new entry, seed 1, 5,000 states each on `gato-battle`
(`tools/fuzz_routines.toml`: an actor and one to eleven listed combatants at
a set of numbers and a hit the showing has, the modifiers among those the port
has, the actions from the ROM's records, `$1B` clear), none mismatching; all
5,000 compared but the hits (2,508) and the repeated hits (2,546, one to three
of them), whose other states are steals turned away. The enemies' turns around
them (`fuzz_compare.py --subsystem battle`, 356 routines) none mismatching.
Fuzzing found what shadow mode couldn't: the drain's word read, the beaten
mark, the tenth's subtractions, the record decoded by the kind the entry runs
(not its first byte), the defence's quarters taken from the flags as the work
goes (not the record's), and a set of numbers from the fourth repeated hit on
reaching the effect's flags.

**Timing.** Calibrated from the shadow runs, battle's rows only (the kinds
22,770-45,350, the work-outs 14,758 and 16,958, the notes 21,091,
`battle_multiply_16x16` 29,285, the enemy's attack's turn 8,519); every entry
within the limits with no exception added. `tools/timing_gate.py` takes an
entry some report measured no stretch of (all interrupted) without failing.

**The routes.** The new timing moved the `bucket_lavos` route's core fight onto
a turn where the bot's A in a tech's target cursor didn't take, and its backing
out pressed B four times back to back, which the game reads as one press: it
left the tech list open and waited on it for good (the game takes the tech
from the same state, in the original's code too). The bot now releases between
presses and waits for each to move the menu (`tools/movies/bot.py`), and every
route reaches its outcome in the shipping mode.

### Phase 3, battle chunk 3: the statuses' timers (`$C1:8876`-`$C1:8C08`)

The third battle chunk was picked from what the enemy turn and the main loop
still called out to for being whole on its own: the twelve handlers the main
loop runs for a status's timer that has run out (the original's table at
`$C1:B92D`), 914 bytes, whose callees were ports already (the damage and
number records, the long division, the numbers' clearing) but for the
action's showing and the effect's start. The party's working out
(`$C1:D523`) was the other candidate the coordinator named; it brings the
steal's item and the clock modifier's controller read with it, each a step
of its own, and waits for chunk 4.

`game/battle/status_timers.c` is the handlers as one module: a C table says
what each kind of timer does when it runs out -- wears its status off on the
tenth time (stop, haste, evading much better, the shield, the barrier; the
slow's timer, which wears off without taking the slow away, as the
original's does), poison hurts, a point of HP is lost, MP comes back on the
tenth time, or nothing (three kinds) -- and which status bit it wears off.
Each run the timer starts again, at its length, or paced like a turn (half
as long hasted, twice slowed; the MP's timer asks only about kept haste: the
original loads the status byte and drops it). What a status does is recorded
where the action's showing finds it (`effect_show_status_damage`,
`effect_show_magic_regained`, `effect_record_status_damage`, in
`effect_amounts.c`, as a hit's is), and the handler's wrapper calls the
showing (`$C1:AC57`, a sibling, with the frame of `$C1:895B`, which calls it,
on the stack) or the effect's start (`$C1:EC7F`, the original's). The search
(`battle/timers.c`) runs them as siblings, as the loop's `JSR` calls them;
the combatant a handler works on is the one before where the kind's search
goes on in the order (`battle_timer_found_run_out`), and seeing to it clears
the kind's due bit (`battle_timer_seen_to`). New in the map: the runs left
before a status wears off (`battle_timer_runs_left`, `$B045`), and two
status bits named for what the battle does with them (the first byte's `$10`,
a point of HP each run; the fourth's `$20`, 5 MP every tenth), which also
names the bits `start_timer_for` (`effect_statuses.c`) tested by number.

Two leftovers are kept, as the game plays them: poison divides the long
arithmetic's leftover high word with the most HP, by a divisor whose high
byte is what the last long arithmetic left in its cell (the original sets its
low byte alone, `battle_wide_math_second_high`). And two things the original
leaves are scaffolding: the stat block's place in `$10` (the handlers that
act), and the carry the damage is recorded with, which the multiply leaves in
its cell (a shown number's place's add, or the point of HP's pacing shift;
finding the stat block's place clears it otherwise).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the barrier's 214 calls (210 compared), the shield's 168 (166), the haste's
112 (111), poison's 62 (51, the rest compared in the search that ran them),
the point of HP's 165 (all compared in the search), the stop's 12. Shadow
mode first found the `$10` the handlers leave. The evading much better's
handler is called four times (`gato-battle`, `endings-telepod-lavos`) for no
combatant: the search, reading past the order's last filled place, takes the
empty place's `$FF` as a slot whose timer (past the kind's row, in the
lengths' table) has run out, and the original's handler then writes past
every table it touches. The guard turns those away to the original
(docs/TODO.md). No movie reaches the slow's, the MP's or the three that do
nothing. **Fuzzing**: every handler, seed 1, 5,000 states on `gato-battle`
(`tools/fuzz_routines.toml`: each kind's search one to eleven places on in
an order of the eleven slots), none mismatching; all compared but poison
(3,767; its showing waits on frames past the budget) and the MP's (4,991).
Fuzzing found the point of HP's recorded carry: the pacing's shift, or clear.
The search that runs them (`battle_dispatch_expired_timers`), 5,000, none
mismatching.

**Timing.** Calibrated from the shadow runs, battle's rows only: the
handlers that wear off 4,853-5,116, poison 25,065, the point of HP 15,635;
the three the movies don't reach take their siblings' (the slow's and the
evading much better's the shield's, the MP's poison's); the three that do
nothing keep the default. The overflow flag the showing is called with was
already declared dead at its own site (`$C1:AC57`), so the handlers' showing
needed no new evidence; `tools/suite.py --evidence` ran the notes' library
runs again with the handlers in C, and every note's sites still made what
they cite.

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

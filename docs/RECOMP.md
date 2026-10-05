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

## What is here, and what moved to docs/history/

This file holds the **mechanisms**: what the generator does, how a draft is
made, what shadow mode and the timing gate measure, what the checks in the gate
are for, and the newest as-built section of each porting stream as the shape a
new one follows. It is meant to be read.

The per-chunk and per-subsystem record -- the account of each wave, each
subsystem's move off the machine and each chunk of routines as it became C --
is history rather than contract, and it grew past what anyone reads. It now
lives beside this file, and a citation of the form "RECOMP.md, *section*"
resolves through here:

| File | What it records |
|---|---|
| [history/recomp-waves.md](history/recomp-waves.md) | the `$C7` pilot and Phase 3's waves 1 and 2, bank by bank |
| [history/recomp-phase3a.md](history/recomp-phase3a.md) | Phase 3a: each subsystem taken off the machine |
| [history/recomp-chunks.md](history/recomp-chunks.md) | Phase 3's per-chunk sections for the menus, the field, the battle and the scene program |

A new as-built section goes in **this** file, under the phase it belongs to;
when the next chunk of the same stream lands, the one before it moves to its
history file in the same commit. What stays here is one worked example per
stream, not the whole run of them.

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
original never has. The port went back to the call-out (which the fuzzer
compares only at the return, after the original has run the command itself);
it now hands such a command to the original's code for that command, entered
as its dispatch enters it (RECOMP.md, "Phase 3, the scene program: the staff
roll").

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

### Phase 3, field and events: the console's start and the field's frame

The sixteenth chunk of bank `$C0`, with the console's own entries: what runs
from power-on to the field's main loop, the loop's frame, a location's
loading, and the field put aside and taken up again around a menu and a
battle.

**How it was chosen.** The plan named the power-on path and the interrupt
entries (Phase 4a: `ct_play --recomp off --interpreted stop` stopped in frame
0) and, for `$C0`, the field's setup and keeping (`$C0:0000`, `$01A5`,
`$038F`, `$011B`). By concept that is the field mode's skeleton: its two
starts, its frame, the location's loading the starts and the menus share, and
both sides of the hand-over to a battle. The fade in (`$C0:2824`-`$2C40`)
belongs to it too but is mostly calls into routines no port covers yet (the
window tile maps, the screen jobs' transfers, the map's unpacking), so it was
left for the next chunk; about 2.6 KiB in all, short of the plan's 8-12.

**What it is.** `core/console.c`: the reset stub (`$00:FF00`, entered in
emulation mode at power-on and in native mode by the game's own resets, and
its copy at `$C0:FF00`), the boot (`$FD:C000`: the console's registers as the
game starts them, three timing and input calls added for what had none,
`tm_fast_rom`, `tm_reset_timers`, `in_set_io_lines`, and the picture's
power-on screen, which moved here from `core/display.c`), the vectors
(`$00:FF10`, `$FF14`) and the two stubs in work RAM (`$00:0500`, `$0504`),
which go to the handler the running mode installed (`console_install_*`, the
stub's encoding in one place now) or return where the mode turned the
interrupt away. `field/main_loop.c`: the field's first start (`$C0:000E`:
work RAM cleared, the sound driver started, the menus asked about the
saves), its entry from the other modes (`$C0:0000`/`$005D`, the menus' sprite
screens past location `$1F0` handed to them), one pass of the main loop
(`$C0:0097`, a stop at its own head, so a save state never holds it), the
frame's work (`$00BF`), the effects' refresh (`$00DE`), a frame of the
leader's walk (`$00EB`) and the start of the frame wait (`$C0:EC60`).
`field/location.c`: the location's record (MD2's `location_records`, named
fields in `field_data.h`), its song (`$C0:1B53`), its tile set's graphics
(`$C0:0960`/`$6D2F`/`$6D65`, `$6DCF`) and looks (`$09DD`, `$0A14`), and the
field put aside for a menu (`$011B`) and set up after it (`$01A5`): the state
block moved to the event script's place and back, the party's places, the
party scripts' destinations the script's unpacking writes over, a gathering
the menu cut short. `field/battle_start.c` (`$C0:024C`, `$038F`): the enemies
on the screen found, the battle's ground, places, kinds and screen handed
over; `field/battle_return.c` gained the field after the battle (`$C0:0283`:
the game over, the place put aside, or the field taken up a frame at a time);
`field/palettes.c` the enemies' palette slots (`$C0:72B4`).

**Third trap, settled first.** Every call-out was checked against liveness
before the logic was written; three registers liveness keeps that the
callees never use were given the original's value as marked scaffolding
rather than threaded: A's high byte at the sound driver's start and the
menus' routine (the field's page, which the original set D through), and at
the song's driver calls (the record's place, which the original worked out
in A). Where a callee reads the overflow (the scripts, the dialog window,
the frame wait), the ports leave it as the steps before them do.

**What the shipping mode found.** Shadow mode passed a main loop that the
long routes (`suite.py --long-routes`) did not. A pass that called the
frame wait was a native routine held over every frame's end, which a
machine copy can only rebuild by replaying input from the last frame where
none was held (ct_drive, docs/MOVIES.md), so the routes' every probe
replayed from the field's entry; the pass now stops at the wait
(`$C0:00BA`), which sets A for the next pass itself. And a step the
registry turns away (the requests' guard, at a location change it can't
run) is a real call from the bottom of the field's stack, where the
original's location change resets the stack to its top and goes back to the
field's entry: that leaves the stack just as the call's return would, so
the runtime couldn't tell it from one, and the pass waited under the next
pass for good (eight deep after the Lavos cavern's fight in
`bucket_lavos`, which then spun in replays). The main loop's steps are
`field_run_or_hand_over` (`field/bridge.h`): where the sibling won't run as
C, the port makes the call and stops at it, and the rest of the pass is the
runtime's. The tile graphics' transfer cells are described
for each of the six places whether the set has a block there or not. And a
runtime limit: the object update run as C inside the frame's work, inside
the frame, made the original's own entry into it a third nested comparison
waiting at the same multi-pass call (the views' `$C0:A88D`) as the frame,
which settles it as a mismatch; the update is a call of the frame's work's
(docs/PORTING.md, "Call-outs"), and runs as its own port.

**What fuzzing found and cost.** Each new entry has a `tools/fuzz_routines.toml`
entry (seed 1; 0 mismatched on all of them, the counts in `docs/ports.tsv`).
The battle's start was interrupted on all 31 of its library calls and the
field after a battle on 25 of 26, so fuzzing is their check: it found the
current object the original names before an enemy without a sprite takes its
palette slot. The battle's start needed constraints to reach its body (the
objects' roles, sprite tiles and view, the party's places); the setting up
after a menu needs the dialog window where the game puts it, since elsewhere
the original's line effects' builder runs off its table. The callers whose
call-outs became C calls (the requests, the menus, the screen effects, the
party commands) were fuzzed against `main` with the same seeds: the same
counts.

**What is left at the edges.** The fade in and the objects' and sprite
lists' setup (`$C0:2848`, `$C0:56A6`, `$C0:B192`) are call-outs from the
entry, the map's unpacking (`$C0:A33B`) and the script's (`$C0:56D4`) from
both loadings, the dialog window's step (`$C0:1F87`) and the scheduler
(`$C0:59D9`, `$C0:5A46`, `$C0:CA76`, beam-coupled, PLAN.md Phase 4b) from the
frame; and the menus' routine `$C2:8004` from the first start, where
`ct_play --recomp off --interpreted stop` now stops, in frame 2 (it stopped
at the reset, in frame 0). The palettes' jumps `$FD:FFF4`/`$FFF7` stay
call-outs: registering them as the routines they jump to would make the
battle's own calls through them calls into a registered port. `$C0` stands
at 52.1 KiB ported, 12% left.

**The timing gate.** The new rows were calibrated over the library, and
thirteen entries have exceptions (`tools/timing_exceptions.txt`). The console's
vectors are a jump with no access of its own to scale. The field's frame, its
entry, the loadings and the returns run nearly all their work as C at their
callees' calibrated times, and what is left as their own is a remainder one
scalar can't fit, or a data-dependent unpacking with the screen blanked. The
leader's walk, now run as C under the frame, has two kinds of call: the
value that removes its drift leaves the short ones' tail. The vblank check
finds none of their writes moved.

### Phase 3, battle: the screen script's shapes (`$CD:1F47`-`$CD:3BFC`) and the frame work's line moves

What `python3 tools/burndown.py --uncovered CD` listed after the cuts and
bands: the window shapes the screen script draws (`$CD:31AA`-`$CD:3BD4`), its
special effects (`$CD:2A4A`-`$CD:2DCA`), the commands whose handlers were still
the original's, and the frame work's remainder. The chunk took the first three
whole and the frame work's table moves, and stopped where the frame work
waits for frames (below). `$CD` went from 36% left to 6%; every command of the
screen script language is a row of its table now, and only `$C1`'s wait for a
line of the picture still hands the machine to the original (a bridge).

What it is, in the game's terms:

- **A window's straight sides** (`battle/screen_sides.c`): a shape is a top
  line, the two sides' columns there and up to three stretches, each side
  following one edge down a stretch at a fixed 8.8 slope; the quadrilaterals
  (`screen_beam.c`) and the triangles share it, and with it the one running
  sum both sides step in, whose carry the comment explains for both signs of
  a slope (review rubric 119).
- **Triangles** (`battle/screen_triangles.c`, `$CD:367C`): sorted down the
  screen, a flat top, a flat bottom, or a side that turns at the middle
  corner's line; the right triangle hung from the top of the screen (`$B8`,
  `$CD:3924`), always flat-topped, the window above it filled to the top edge;
  and a triangle's center where two medians cross (`$AD`, `$B4`, `$CD:21C0`):
  each median a line `y = slope * x + intercept`, the crossing the intercepts'
  difference times 256 over the slopes', as one word over another
  (`crossing_of` says how the original shares the 256 between them). The
  triangle commands draw in C now, so the runner's hand-off to the original's
  rasterizer and its flow kind went; what the sizing leaves only the machine
  knows (the controller's byte) is the runner's, in a table by command.
- **Circles** (`battle/screen_circles.c`, `$8B`, `$CD:2FB7`-`$CD:3656`, and the
  circle commands moved from `screen_shapes.c`): the half widths of the
  rows by the midpoint circle (a byte-wide error term), laid out about the
  middle of the worked rows and copied into the window table about the
  circle's line; the six kinds are an enum the place and kind commands take
  (round, flattened below, hung from the top, tall, wide, twice the size).
  `$B5`/`$B6` draw a square the same way.
- **The special effects** (`battle/screen_specials.c`, `$80`, `$CD:2A4A`):
  29 named operations in a C table: bands of light (the fixed color's
  per-line tables, written through `vid_line_fixed_color`), wedges of the
  first triangle, a layer bobbing on a wave, the waves' lines marked at
  random, the palette effects `screen_palette.c` already had, the enemies
  hidden. The waves' lines and marks are records in the map now
  (`battle_wave_lines`, `battle_wave_marks`).
- **The rest of the language**: `$A1` draws a triangle at its corners,
  `$DA` a wedge between positions and `$CC` measures a direction
  (`screen_between.c`), `$D7` drifts the layer (`screen_motion.c`), `$F9`
  pokes work RAM (`screen_script.c`, through `wram.h`). `$C1` is a row whose
  bridge calls the original's wait for a line (`$CD:3B82`).
- **The frame work's table moves** (`battle/display.c`, `frame.c`,
  `scroll.c`): the slide's line moves (`$CD:053C`/`$0577`, the runs of the
  nine per-line tables), the two line-wave scroll modes (`$CD:07E2`/`$07B9`,
  the Lavos fights'), and the rule's hold on the battle's palette animations
  (`$CD:0B1E`), all called as C where they were call-outs.

**Narrowed.** The frame work's waits and what they call: the battle's
closing (`$CD:0006`, `$03B4`-`$04A5`), the messages (`$CD:0021`, `$01B3`), the
start settings' copy (`$CD:0C9D`/`$0CF6`), the frame wait's tail's callees,
and `$CD:3B82` itself; they wait on frames, the sound and the menus' text,
and are the next chunk (docs/PLAN.md).

**The third trap, settled first.** `tools/v_readers.py` has V dead after the
one call-out the chunk adds (`$CD:3B82`, from `$C1`'s bridge); the
don't-care check found the line wave's fill leaving X where the finer wave
goes on, and the slide's moves leaving Y for the frame work, which the
entries now keep. The triangle's center works in the cells `$23`-`$2A`, over
`battle_party_defeated` and the random cells: written plainly, as the
direction's measure writes them (docs/TODO.md).

**What shadow mode found.** Circles the scripts grow past radius 127 (the
Lavos fights'): the original works the rows out for the radius's low seven
bits and copies the run for the whole byte, so such a circle is a small one
with older rows above and below it, and one of 128 draws nothing. The
rule's hold is on slots 8-11, not 0-3 (the slots' table starts at `$0520`).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the circles 53,267 calls, the triangles drawn at their corners 27,096, the
hung wedges 2,127, the special effects 3,470, the line wave 7,619 and the
finer one 631, the slide's moves 713 and 598, the rule's hold 432,395. No
movie has `$AD`, `$B4`, `$B5`/`$B6`, `$C1`, `$CC`, `$D7`, `$DA` or `$F9`; no row
turns away a call the library makes.

**Fuzzing**, seed 1 (`tools/fuzz_routines.toml`), none mismatching: the
triangles' drawing 0 of 16,762, `$A1` 0 of 8,173, `$B8` 0 of 8,616, `$AD` and
`$B4` 0 of 9,775, the circles 0 of 10,069, the square 0 of 10,061, the
special effects 0 of 9,464 through the command and each effect's own entry
0 of 20,000 (the nudge 0 of 10,029, with the carry the dispatch leaves
clear), `$F9` 0 of 9,775, `$D7` 0 of 6,724, `$DA` 0 of
8,383, and the frame work's five 0 of 20,000 each; the 167 screen entries
against the tree before the chunk, the same counts every one. Fuzzing found
the nudge wave leaving the multiplier's operands the other way round (352 of
9,464), and random corners and runs past the window table on odd frames,
which the rows' needs turn away (`screen_triangle_fits_the_table`, the
circles' `run_fits`), as the quadrilaterals' did.

**Timing.** The special effects differ in their work a hundredfold under one
handler, so each is a registered entry of its own and runs timed as its
original routine (`TIMED_AS`, a table beside the effects'); the circle's rows,
which the port works out in locals where the original works each at length,
are charged at a rate a row (`charge_as`), and its copy is timed as
`$CE:E7C8`'s. The new entries and the screen script's runner were calibrated
from the shadow runs; what is left over the limits is the circles' spread
(4.1% error, at most 14,483 clocks) and the marked lines' (5.6%, at most
11,823), and the runner and the frame work above them, all writing RAM, in
`tools/timing_exceptions.txt`.

### Phase 3, battle: the targeting (`$C1:C1DD`-`$C1:CBC2`)

Chosen by the fallback measure, not the burndown: the burndown counted bank
`$C1` as done, but every tech and item the battle carries out was still aimed
by the original's code, a call-out from the member's turn, the enemy's turn
and (through the original's code they reach) the status timers: 1.5 KiB of
the reference play's fallbacks (`$C1:C1DD` alone 1,122 bytes, the largest
single routine on `tools/fallback_baseline.txt`). It was also what the native
shape's three whole routes failed on: the original targeting read two cells
the member's turn wrote only as scaffolding (the member's action record's
place, `$06`, and an item's area rules, `$0C`).

**What it is** (`battle/aim.c`, `battle/aim_data.h`). An action is aimed in
three steps. Whether it can be made: the actor, a party tech's partners, or
the enemy an enemy's tech brings in, must be able to take part (not knocked
out, none of the statuses that keep them from acting, nor two bits of the
second status byte whose meaning isn't worked out). The targets its aim mode
takes: the modes are the records' bytes, a C table maps each to one of twelve
ways of taking targets (`Take`) and how the targets are settled after (`Finish`),
so the twenty-four modes the ROM uses are rows, not branches. Then its area
rules, for a tech that has them (`$CC:2AB0`): the area is placed on the
combatants (around the tech's first member and toward the target, around the
target, around the actor and toward the target: a table of the seven
placings), the original's area search (the battle's step 7, `$C1:1FDD`, still
a call-out) finds who stands in it, and the finds become the targets, settled
as the mode says. The original's `$C1:C741`, which hands the search its shape,
is its own entry (`battle_search_area`), so the call-out is made under the
same return the original's is without a frame pushed by hand. Its helpers
(`$C1:C72B`-`$C1:C82C`, `$C1:C82D`, `$C1:C8F7`, `$C1:CB93`) are C inside it.

**What the game does that the port keeps.** Reading the original closely found
four things the game really does, which the port reproduces and says so:

- The "random" aim (mode 9, two items) doesn't roll: it draws a number and
  then reads its roll from the arithmetic's cell the draw puts back, where the
  tech members' lookup at the start left the item's party-tech record's
  number, so each such item always takes the same branch (a foe at random,
  for both). One branch would bound the party's places by the byte of buttons
  the player holds; no item rolls it, and the guard refuses it.
- The settling's walk over the targets steps past a gap and closes up over the
  place after it, so a gap takes the target after it off the list too, and the
  list's last place is never closed over.
- The area's finds end with `$FF`, which the test for an enemy in reserve takes
  as a combatant past the eight, reading a timer's flags there; a revive's
  target is tested for reserve by its battle slot rather than its enemy
  number. Both read past `battle_enemy_standing`, which now declares
  `unchecked` for them.
- The area's finds are walked with the multiply's product cell as the counter,
  which the original leaves past the last place.

**The cells.** The targeting's direct-page cells `$00`-`$0E` are read after it
as game input (battle.h: the clock's word, the member cell, the stat offsets,
an enemy tech's hits), so the port keeps them plainly as the original leaves
them; `$10`-`$14` and the new `battle_aim_*` cells at `$3A`-`$3D` are
scaffolding, but for the rule's shape, which its sub-entry reads. The member's
turn's writes of `$06` and `$0C` became plain: the targeting's guard reads them
(the action record's place, an item's area rules), and with them the native
shape's battle failures went. The event scheduler's step count, cleared only
as scaffolding by `object_run_script_from` and read by the original's
scheduler (`$C0:59F0`), is plain too, which was the third route's failure:
`tools/native_shape_exceptions.txt` is empty.

**What the guard turns away**, none of it a state real play makes (the
refusals check holds it): a party member whose action record isn't where its
caller hands it, an item handed area rules, a chosen target that isn't a
combatant (none, but for a revive), a tech whose members aren't the party's
characters, a set of area rules with more than one rule or with the target
search's rule (`$C1:2986`, the battle's step 5; the ROM's sets have one rule
each, all the area search's), an enemy's rule placed on party members, the
roll's button-bounded branch.

**What the gate found.** Fuzzing found two: the steps' routine picks its step by
the whole of A, so the area search needs A's high byte clear, as the original's
loads leave it (the sub-entry's guard now asks for it, and the targeting clears
A before it); and the product cell's leftover above. The don't-care check found
A's high byte read after the targeting returns (`DC_AL`, the port clearing it
as the original does, but after the "no target" showing, which hands back its
own), and V at the area search's call-out unproven through the steps' tables,
which `tools/v_readers.py` has dead and a poisoned fuzzing run confirms.

**Validation.** The library's 28 movies in shadow mode, none mismatching: the
targeting 841 calls, 699 compared (96 interrupted, 46 set aside inside its
callers' comparisons), the area search's entry 46 (each compared within the
targeting's); its callers as before, the member's turn 548 calls, the enemy's
tech 300. **Fuzzing**, seed 1, none mismatching (`tools/fuzz_routines.toml`):
the party's techs 0 of 4,935, every party tech with area rules 0 of 4,628,
items 0 of 5,000, the enemies' techs 0 of 5,000 and those with area rules 0 of
4,999, the area search's entry 0 of 5,000; the callers (the member's turn, its
techs, the enemy's tech and its stat-changing turns, the status timers) fuzz
the same as on main, 0 mismatched each. With the native shape passing every
route, `tools/native_shape_exceptions.txt` is empty; the reference play's
fallbacks fell from 46.5 KiB to 45.2 KiB, ten routines leaving the baseline.

**Timing.** The two new entries, and the battle's entries whose time moved with
them (the member's turn, the enemy's turns and tech, the turn's giving), were
calibrated from the shadow runs.

### Phase 3, overworld: the world map on foot (`$C2:2F94`-`$C2:42E5`, `$C2:23CF`-`$C2:26A7`, `$C2:5628`-`$C2:57AF`, `$C2:62AE`-`$C2:6331`)

The eleventh chunk of bank `$C2` (history file, "Phase 3, menus: the party's
exchange", for the one before) and PLAN's first item for what was left there:
the world map as the party walks it. It is its own subsystem, `game/overworld/`,
sharing the sprite-object mode's objects and scripts with `game/menu/`
(`tools/ram_map.py`'s `SHARES`, `tools/effects_check.py`'s families).
`python3 tools/burndown.py` for `$C2`: 40.6K ported and 31% left with 450
entries before, 46.1K and 22% left with 462 after: about 5.6 KiB of new code
in 12 entries, all of it to the bar (`tools/portstyle.py --subsystem
overworld`: 0.01 machine operations a line, 19% of its functions on the
machine, all of them entry wrappers or the main loop's bridges;
`tools/portsmell.py`: no finding).

**What it is.** Each walker is an object of the sprite-object mode, its
handler run once a frame by the objects' loop (`menu_run_objects`):

- `overworld.h` and `party.c`, the walkers' vocabulary: the way a walker
  faces (the field's `Facing`), its spot on the map (`WorldSpot`), a step
  (`OverworldStep`, the step table's row), standing and walking with the
  animations the ROM keeps for them (`overworld_data.h`), the idle animation
  after standing long enough, heading for a goal and halting there, and the
  trail the leader lays (`TRAIL_STEPS` spots, each follower a fixed count
  behind) and the tile the party last stood on.
- `walk.c`, the leader (`$C2:3404`): the pad's direction read into a way to
  walk, a step of a tile when the ground lets it (`terrain.c`), the trail laid
  as it goes, and the travel's other steps (boarding, standing) where the
  vehicles leave it.
- `followers.c`, the followers (`$C2:3AE2`): each decides its way after the
  leader along the trail, and walks a leg across or down; where a wall is in
  the way it searches round it (left and right of the leg, `$C2:41B5`,
  `$C2:4205`, `$C2:425A`), and close to the leader it keeps its place
  (`$C2:4060`).
- `terrain.c`, the ground (`$C2:62AE`, `$C2:39D5`, `$C2:62ED`): the kind of
  an 8x8 tile of the map, the four that meet at a walker's feet, and the
  region a spot is in.
- `places.c`, the watcher of the ground under the leader (`$C2:3264`): the
  map's three lists of places (exits, events, halts) looked up where the
  leader stands still on a place's ground, the place's name shown, an exit
  taken through `overworld_leave_for`, which `menu/object_script.c`'s change
  of location now shares.
- `music.c`, the region's song (`$C2:2F94`) and the request a script or the
  music leaves passed on to the driver (`$C2:2ED9`).
- `sign.c`, the place's name on a sign (`$C2:5628`): the label cleared, the
  name typeset into it (`menu/typeset.c`), and the sign's sprites shown while
  the leader stands there.
- `main_loop.c`, the mode's main loop (`$C2:23CF`), a frame at a time: what
  the mode asks for, entering a place, leaving for a location, or the menus,
  the map view and the time dial with the overworld put aside before
  (`$C2:2E23`) and brought back after (`$C2:2E72`). The menus move the stack,
  so the port stops at their call and starts again where the original comes
  back from them (`overworld_back_from_the_menus`, `$C2:255A`).

**The third trap, settled first.** The handlers return to the objects' loop,
which reads the carry (clear: the object goes on) and P's M width: the
leader leaves the accumulator 16-bit only mid-step, which the wrapper derives
from the step (`leader_mid_step`), not from the logic. The main loop's
call-outs read nothing but the frames to wait (X and A's high byte, given as
the original gives them) and the driver's result; the menus' entry at
`$C2:8000` reads A's low byte and X (the past-the-last-piece offset the loop
computes), A's high byte, Y and the carry declared dead. Each declaration is
`tools/dontcare_check.py`'s.

**An event's scripts and the halt.** An event the party steps onto gives
each walker a script (`menu_member_scripts`), which it takes up and runs a
frame at a time (`$C2:3772`, `$C2:37A1`, `$C2:3D9C`, `$C2:3DC1`): the
walkers' logic asks for the frame's run through a service
(`WalkerScripts`, `overworld.h`), and the entries give it the interpreter's
bridge called as from the walker's own call (`menu_run_object_script_from`,
`menu/script_call.h`), with that call's return on the stack for a command the
bridge hands to the original's interpreter. The accumulator's width the
walkers leave the objects' loop is worked out from the frame as it starts, as
a script may change anything after. The halt (`$C2:250D`) is an arm of the
loop and an entry of its own: the screen stilled and the story's flags given
back, a pass at a time that comes back to itself, as the original spins.

**What the guards refuse.** The vehicles' controller (`$C2:309E`) is the next
chunk's. A mode the loop doesn't know, or entering a place past the map's
lists, hands over to the original's loop at `$C2:23D2`, before it touches a
register: neither is a state the game makes.

**Where the party stood.** Leaving the world map keeps what the camera looks
at, which the original works out as its top-left tile plus sixteen each way,
half its 256-pixel width. That is the camera's convention, not the screen's
size: `menu/map.c` keeps it beside the centring that puts the camera there
(sixteen columns and fourteen rows off the party, half the original's
256x224), as `menu_map_camera_looks_at`, so a screen of another size draws
round the same camera and a place kept doesn't depend on the screen it was
played on. The leader's own spot gives the same tile on foot but not where a
scene's script has moved the camera (9 of the library's 20 leaves differ, all
of them a script's), so the camera it stays. Elsewhere the guards keep
to states the game makes: sprite scripts well formed
(`menu_given_sprite_script_is_well_formed`), the map scrolled within its
room, the lists' lengths and an exit's offset whole records, the trail's next
spot even and inside it, a spot on the region map.

**What the gate found.** Fuzzing found the region lookup reading before the
region map for a spot in the map's first row (the guard keeps the party off
it); the sign's clear one byte too long (the original's block move clears
exactly the label); and the guards refusing the frame a walker sets off on,
before its sprite script is set (they now take the setting-off step). Shadow
mode found the step table's row the dispatch leaves in `$00`, which the
walkers' callers don't read and the gate compares
(`overworld_leave_step_table_row`, scaffolding); and the follower's wall
search reading past the map's last row, as the original does, through
`overworld_map`'s `unchecked` pair (docs/TODO.md); and bringing the
overworld back leaving A all ones, its block moves' count run out.

**Validation.** The library's 27 movies in shadow mode, none mismatching: the
leader 14,892 calls (2 interrupted), the followers 29,784, the watcher, the
region's song and the sign 14,892 each, the ground's lookup 14,579 (2
interrupted), the region's 9,584, the main loop 24,017 frames (3 of them
abandoned where the menus unwind the stack), the overworld put aside and
brought back 6 times each, the return from the menus 2. **Fuzzing**, seed 2,
none mismatching (`ct_fuzz` from `menus-epoch`'s frame 3300,
`tools/fuzz_routines.toml`'s `overworld` state): the leader 4,931 of 5,000
(the rest refused), the followers 4,913, the watcher 5,000, the region's song
4,993, the sign 5,000, the ground's lookup 5,000, the region's 4,925, the
return from the menus 2,000 up to its first call-out, putting the overworld
aside and bringing it back 2,000 each, and the main loop 200 up to the frame's
wait (`$C2:0346` spins on the vblank count, which no fuzzed state changes, so
the arms are shadow mode's alone). What no movie runs
(`docs/movies-wanted.md`): an exit to an event, the exchange opened from the
map, walking into a new region's song, the dactyls, a halt, the walkers' event
steps, the sign cleared from the menus. The event steps are fuzzed in their own
variant (`tools/fuzz_routines.toml`, `event`), 5,000 states each for the
leader and the followers on three seeds, none mismatching, with scripts of
the game's commands: a script of random bytes can copy the map over the stack,
which the original survives differently.

**Timing.** Every entry is calibrated from the shadow runs. Putting the
overworld aside and bringing it back are block moves of about 24 KiB,
which the main loop runs as C: each is charged at its own routine's rate
(`charge_as`, `$C2:2E23` and `$C2:2E72`, registered for it), not the
loop's. The leader's frames that scroll the map are off by what
`menu_map_scroll_x` and `_y` are off by, timed as them (`TIMED_AS`): a
frame that builds a new column or row of tiles is 7,000-14,000 clocks
off, writing RAM, which `tools/timing_exceptions.txt` accepts for the
leader's vblank-tail and vblank-worst. The world map's HDMA, stopped as
the menus open, stops on the fade-out's last frame at brightness 1
rather than in forced blank (`tools/vblank_check_exceptions.txt`).

### Phase 3, the scene program: the staff roll (`$7E:8EA8`, `$7E:9E7B`-`$7E:9F2A`), and the scene engine's leftovers

The third chunk of bank `$7E`, after the scene engine's and the scene data's
last unported runs (history file, "Phase 3, the scene program: the rest of
the jet bike race", for the one before).

**How it was chosen.** The brief: the last runs `tools/burndown.py --uncovered`
listed in `$C3` and `$C6`, then the telepod ending. Finding out what each run
was came first, and most were ported already:

- `$C3:0B03`, `$C3:0B95`, `$C3:0C41` (467 bytes) are the sequence interpreter
  (`scene/script.c`), uncredited because the entry handed a command it doesn't
  take back to the original's whole loop, a call-out to `$C3:0B03` the walk
  can't go past. It no longer calls the loop. A command no script has (a store
  outside low RAM, a video setting the service doesn't express, a task taken
  off a list it isn't on or put on a full one) is handed to the original's
  code for that command, entered as the dispatch enters it: the script past
  the opcode, A and X the handler's place in the dispatch table with the flags
  of the shift that made it, and the dispatch's scratch word kept, so the
  stop is exactly the machine the original has there (what TODO.md asked for
  at the loop's head, one step further on). A call-out that comes back with
  the machine otherwise stops where the call returns. A script that never
  ends its turn now hangs as the original's does (the old limit of 65536
  commands, which handed it back, is gone: review item 113). What stays the
  original's are those handlers and the task searches they call (about 160
  bytes, which only what no script has reaches) and `$C3:0BEF`-`$C3:0C18`,
  which nothing reaches (`re/hints.toml`, "unreached").
- `$C3:09A4` (53 bytes) is the NMI's song send (`scene/frame.c`), uncredited
  because the burndown read `PF_CALL_DC(site, target, set)` as `(target,
  set)` and took the port as unreadable (fixed in the tool, which moved
  several banks a little); its stop where the data bank doesn't reach the
  registers was dead, since the handler goes on only where the frame's work
  left a bank that does, and is gone.
- `$C3:0A95` (106 bytes) is the frame's controller read past the console's
  busy wait, ported, behind the stop at that wait; it stays the census's
  `in_ready` rewrite (PLATFORM.md, "Timing without a beam").
- `$C6:E6D0` (126 bytes) was unported: a sprite-object scene's routine that
  jolts the running object a random step in a random direction, which a
  script at `$C3:B5AF` calls and no movie reaches. `menu_object_jolt`
  (`menu/object_commands.c`) takes two of the menus' random bytes, the
  direction and the size, and moves the object's pixel along each axis by the
  sine's size times the jolt's in 256ths; the menus' sine (`$C2:2273`/`$2277`,
  the table at `$C0:F800` with the quarter turns put in by hand) is inlined.
  The carry the scripts read is the place down wrapping, worked out in the
  wrapper from before and after.

Then the telepod ending, mode 13, which does not go through the shared ending
at `$7E:8538`: its setup, the credits' roll and upload (which the Beyond Time
ending shares, reaching them through `$7E:8538` at the slow speed), the
sprites' high bits, and the two routines its script calls. Left at the edge:
the stars (the scatter `$7E:5777`, the flight `$7E:579B`, and The End's drift
`$7E:9897`/`$7E:98AC` with `$7E:98E3`), whose work is to hand the random mixer
`$C3:02DD` the registers it mixes into the seed (A, X and the carry): they wait
for the mixer as game code (TODO.md, "The scene program's randomness and
parity"). The unpacker and the first script step stay call-outs, as in the
race. `$7E` stands at 12.6 KiB ported and 1.9 KiB inlined, 49% left; `$C3` at
8% and `$C6` at 0% (52%, 15% and 4% before, with the tool's fix).

**What it is.** `scene/staff_roll.c` is the scene: the staff credits rolling up
over a field of stars, then The End. The setup plays the music fast (the
driver's fast forward through the program's own send), sets the stars coming
and scatters them, stops the palette's animations (`palette_stop_animations`,
the call through `$FD:FFEE` made C), copies two palettes out of the program's
resources and unpacks four pictures into VRAM (a table, as the race's), sets
the letters' colours, blanks the credits' map (both halves of each word
filled, through the SNES-only escape, as the title's low-byte fill), sets up
the per-line effect, patches the star field's drawing to put every star
behind the layers and the credits' upload to scroll BG3 (both program
operands, now variables in `re/ram.toml`), starts the script, sets the layers
and starts the song (`scene_bridge_start_song`, now from any call). The roll
writes the next line of text into a row buffer as tiles when one is due
(every 16 frames, every frame fast) and moves the map's row on; at the end of
the text it takes the upload off the vblank list and itself off the frame's.
Its scroll (`$7E:9ED6`) is an entry of its own: a row a frame fast, otherwise a
pixel on the odd frames, which it asks the timing service through
`tm_odd_frame`, new, only where the roll is slow, as the original reads it
(the SNES backend makes the original's 16-bit read of `$213E`-`$213F` and
keeps the word for the scaffolding that leaves it in A, the SNES-only
`tm_snes_odd_frame_read`). The upload sends the row into the low bytes of its map row and the
scroll. The script's test sends it on to The End once the credits are over;
The End's routine starts a fade of the picture in from white
(`palette_start_fade_to_white`, core/palette.c).

**The registers, settled first.** Every call-out's registers were settled
before the logic was written. The setup's: the sound send reads nothing, and
what it leaves in A and the carry is what the stars' scatter mixes in, so
nothing touches them between the two; the unpacker takes its resource; the
first script step reads Y where the per-line effects' descriptors end and the
overflow clear, on the program's bank with the pushed word's other byte under
it, and comes back on its channel's page (the setup puts the scene's back);
the song's driver call takes A's high byte as the step left it. Liveness has
A's high byte and Y live after a frame task: the list's loop loads the next
slot's task over A, but past its last slot the frame's work returns to the
NMI with it, whose song send enters the sound driver, and the driver saves A
(the NMI's own restore then overwrites it). An entry's set can't rest on
evidence, so the roll stops at its scroll rather than scroll itself: the
line's Y and data bank are left there as scaffolding, and the scroll's own
entry leaves in A the word it read, shifted, where the roll is slow, and the
scroll where it is fast. Which way the roll ended its wrapper works out from
the state, the credits counted over or the text moved on. The
overflow after both is dead (liveness), as is everything the vblank tasks and
the script's routines leave but Y and the overflow, which they don't touch.

**What shadow mode and fuzzing found.** Over the 26 movies every call matches,
none interrupted and none refused: in `endings-telepod-lavos` the setup 1,
the roll 240, its scroll 239, the upload 240, the high bits' clear 3027, the
script's test 242 and The End's fade 1; in `endings-beyond-time` the slow roll
3281, its scroll 3280, the upload 3281, the clear 784; the interpreter 79996
over the library, the NMI 61618, the frame's work 61649. Shadow mode found the
clear's 16-bit A at its return and the direct page the first step leaves at
the song's driver call; the don't-care check found that the scroll reads the
data bank (its stop declares its own set). Fuzzed seed 1 from a state in the
telepod ending's credits (`tools/fuzz_routines.toml`, `staff_roll`), the
program kept: the roll 20000 (the text at one of the credits' lines, the last
the end; the speed fast or slow), the scroll 20000, the upload 20000, the
clear 20000, the test 20000, the fade 20000, the setup 300 (the driver's echo
looped back), 0 mismatched; fuzzing found the scroll's overflow, dead after a
frame task, declared. `menu_object_jolt` 20000 from the `objects` state, 0
mismatched. Against main's ports (the same seed): the interpreter 0 of 17459
and 0 of 17466 on its two channels (0 of 14061 and 0 of 14049 before, where
the call-out to the loop left more states uncompared), the frame's work, the
NMI, the race's setup, the resource copies and the palette's clear unchanged.

**Timing.** The new rows and the NMI's (whose song send no longer reads the
song playing) were calibrated over the library. The roll carries a timing
exception (`tools/timing_exceptions.txt`, vblank-tail): its time follows the
line it writes, every 16th frame, writing only RAM.

### The reading cost of a chunk

By field chunk 13 and battle chunk 6, three streams were landing a chunk and a
review every couple of hours, and most of what each one cost was reading rather
than porting. The fixed reading was measured: docs/PORTING.md 240 KB,
docs/RECOMP.md 556 KB, docs/TODO.md 108 KB, the review rubric 49 KB and 112
items, against a chunk's own diff of 2-3 thousand lines. Four changes cut it,
none of them to the bar:

- **The rubric's decidable items are a tool.** `tools/portsmell.py` checks
  eleven of them -- quoted assembly (15), an admitted "presumably" (17), twin
  functions per operand encoding (2), a named emulation of the arithmetic unit
  (12), a register carried out to a wrapper (26), the scaffolding switch as a
  value (23), the PC read for identity (72), the sign bit of a difference (19),
  a call-out into one of our own registered ports (22, 61), a `source` line
  citing a file that is gone (25), and an enum nothing takes as a type (33);
  two more came later, a flag noted inside the logic's loop or game branch
  (93, 105) and a return frame pushed by hand outside a bridge (85, 101). It
  is a ratchet against `tools/portsmell_baseline.txt` and the suite runs it, so
  each of those items is caught before a port is submitted instead of coming
  back from a review a day later, and the rubric names the tool in one line
  instead of spelling the item out. Written against the tree it found 47
  standing instances, all on the baseline and in docs/TODO.md: 20 stale map
  citations and 17 sibling ports called through the machine among them. It says
  nothing about whether a port reads as hand-written -- that is still
  `portstyle.py` and the review.
- **RECOMP.md holds mechanisms, not a log.** The per-wave, per-subsystem and
  per-chunk accounts moved to docs/history/ (waves, phase3a, chunks), leaving
  the mechanisms, the measurements and the newest section of each stream as the
  shape a new one follows: 8,420 lines to about 3,300, with an index here that a
  citation of the form "RECOMP.md, *section*" resolves through.
- **The ports ledger is a file to grep.** docs/PORTING.md's Status table, a row
  of prose per entry, was 140 KB of the doc's 240 KB; it is docs/ports.tsv now,
  and PORTING.md is 63 KB.
- **One review per merge, not per chunk.** The gate runs at every commit because
  behaviour has to hold there; the review's cost is fixed per reading, so it runs
  once on a branch's accumulated chunks before it merges. Same files, same
  rubric, read when there is more to read.

### The fallback measure

The goal is a program with no emulator in it, and four things keep the
original's code running beside the ports: a guard that refuses so the original
runs, a port that stops and hands the rest back, a call-out to code that is
still the original's, and code no port covers. Until this pass nothing measured
how much real play still ran on them, and the native-shape build had only ever
been compiled. Three gate checks look now (tools/suite.py's `fallbacks`,
`refusals` and `native-shape`).

**The measure.** `ct_dev --fallbacks` and `ct_drive --fallbacks`
(`dev/fallbacks.c`) count every instruction the original's code executes in
place of hand ports, interpreted (the CPU's instruction hook) or generated
(`recomp_insn_hook`, a test per instruction in `RC_INSN` when it is unset),
by address, by the routine it belongs to, and by why it ran. The runtime
tells the tool when it hands off (`NativeObserver`, `runtime/port/native.h`):
every hand entry at an address refused (with which condition: the P
precondition, the CPU's mode, or the guard), a port entered, called out,
stopped, returned from an interrupt, or was skipped at the nesting limit. The
tool follows the stack from there: a refused call lasts until S rises above
where it was entered, a call-out until its callee returns, a stop until the
port's routine returns; an interrupt opens an extent of its own that an RTI
ends wherever the stack is; and a hand port entered at or above an extent's
level ends it (the original went on to a port without returning: a main loop's
next pass). Each instruction is then `refused`, `handback`, `skipped` or
`unported`, filed under the target of the latest call on record (the port's
entry for what it hands back after a stop).

**Measured where it holds still.** The shipping mode can't carry a ratchet: a
port's estimated time moves every movie's presses to other frames, so a movie
goes somewhere else with every timing change. Running the library with the
native-shape build, whose only difference is timing (the scaffolding's bus
accesses), moved 99 of 1068 (kind, routine, port) keys and doubled the wait
loops' counts. So the ratchet is taken over the **reference play**: the shadow
runs the gate makes anyway, where the original runs everything at its own
timing. There the runtime's same calls mark what a port stands for -- from the
original's arrival at a port's entry until it reaches the port's call-out,
stop or return, the original is running the port's work and isn't counted --
and an idle entry's pass of its wait loop, counted as the original runs it, is
taken back when the loop comes round to its head (the pass that leaves stays
counted). That count changes only when a port does. It costs the shadow runs
nothing measurable (gato-battle 47 s with it, 49 s without). The shipping
mode's count is still made, over every movie (`--native on --recomp on
--sound-driver c`, 43 s for the library) and the routes (`route_check
--fallbacks`, `$CT_DRIVE_FALLBACKS` through `tools/movies/ctdrv.py`), and
reported beside it. The shadow runs use the original sound driver, so the CPU
half's waits for its answers count there and not in the shipping mode.

**The ratchet.** `tools/fallbacks.py --check` against
`tools/fallback_baseline.txt`, a line per routine with its instructions,
distinct bytes and reason. A routine the baseline doesn't have, or more
distinct bytes in one (a new address run in place of a port), fails; so do
more instructions past 5% and 1000. Kinds are summed per routine so that a
port landing doesn't move its callees (what was `unported` becomes `handback`,
same routine); the report keeps them apart. `--update` takes a gain and keeps
the reasons of the lines it keeps.

**On main**, when this came in, the reference play ran 46.5 KiB of distinct
code and 347 million instructions in place of hand ports, the same in two runs
to the instruction; by kind (a byte can be in two):

| | Reference play (28 movies, shadow) | Shipping mode (28 movies and 3 routes) |
|---|--:|--:|
| refused | 2.4 KiB, 10.3 M | 2.6 KiB, 40.6 M |
| handback | 33.8 KiB, 296.0 M | 36.5 KiB, 1,789 M |
| skipped | 0.0 KiB, 0.3 M | 0 |
| unported | 13.8 KiB, 40.8 M | 11.8 KiB, 134 M |
| all | 46.5 KiB, 347 M | 45.0 KiB, 1,964 M |

By subsystem (the reference play, KiB of code and millions of instructions):
field engine and events 9.8 KiB, 121.8 M; scene program 8.1 KiB, 68.1 M;
battle 8.8 KiB, 36.6 M, its frame work 3.2 KiB, 49.5 M, sprites 0.1 KiB,
4.4 M, NMI 3.3 M; menus, text and overworld 13.8 KiB, 21.8 M; field graphics
3.7 KiB, 31.6 M; scene engine 1.0 KiB, 6.1 M; the sound driver's CPU half
0.9 KiB, 3.8 M. Almost all of it is `handback`: the ports are dispatchers
around code that is still the original's. The largest are the
field frame's call-outs to the beam-coupled waits (`$C0:59D9`, `$C0:5A46`,
`$C0:CA76`; PLATFORM.md, "Timing without a beam"), the scene program's frame
tasks, and the battle frame work's call-outs.

**Refusals as failures** (`tools/fallbacks.py --refusals`,
`tools/refusal_allowlist.txt`). A native build has no original to hand a
refused call to ("Retiring the gate, deliberately", PLAN.md), so a refusal in
real play -- the shadow runs, the movies in the shipping mode, the routes --
fails unless its entry and condition have an allowlist line with a reason.
Each is printed with its count and its first four states (registers, the run,
and for code in work RAM whether its bytes are the image the generated blocks
were made from, `misses_describe`, which is how the code-presence guards that
turn away another program's code are told from gaps). On main, 23 entries
refuse in real play, 7,609 times: four are another program's code at a RAM
address a port was written for (the attract program's), and 19 are gaps,
listed in docs/TODO.md, "Refusals in real play".

**The native shape, run.** `ct_dev_native_shape` and `ct_drive_native_shape`
link `ct_game_native_shape`, the ports with `ORIGINAL_EFFECT` compiled out
(now a static library rather than objects only). Nothing can compare them byte
for byte -- the scaffolding is what they leave out, and their timing differs
from the default build's by its accesses -- so the check is the outcomes: every
movie in the shipping mode exits 0 within its time (a hang), with no `halted:`
line (`ct_dev` prints one where the machine ended stopped), `accessor out of
range` or `page refused` line that the default build's run of the same movie
doesn't show; and the routes reach their outcomes (the fast set; all three
with `--long-routes`). A failure is traced through what the original's code
reads of scaffolding: `ct_dev --scaffold-reads original` (the default build's
shipping runs take it) notes each read the original's code makes of a byte
whose latest write was a port's `ORIGINAL_EFFECT`, by the instruction, and
`tools/effects_check.py --originals` gives each the source of the write; the
check writes the list beside its logs. On main, 101 such (byte, instruction)
pairs over the library.

When the check came in, all 28 movies ran to their end in the native shape,
and the fast routes passed, but all three whole routes failed, each traced and
confirmed by making the writes plain in a scratch build and running the route
again. The targeting's port fixed all three ("Phase 3, battle: the
targeting"), and `tools/native_shape_exceptions.txt` is empty:

- **Battle items land on the wrong side** (`beyond_time`, `bucket_lavos`):
  `battle_carry_out_member_action` wrote the acting member's record
  (`$0006`, game/battle/member_turn.c:499) and the item targeting's cell
  (`$000C`, :420) only as scaffolding, and the original's targeting, which
  the port called out to, read them (`$C1:C24A`, `$C1:C57F`). A revive healed an
  enemy instead; the final fights ended in wipes. With both writes plain, both
  routes reach their endings.
- **A walk stops short** (`telepod_lavos`): `object_run_script_from` cleared an
  object's step count only as scaffolding (game/event/calls.c:173), and the
  original's event scheduler reads it (`$C0:59F0`). The walk to the reaper
  statue in the developers' room ended a tile and a half away, and the seventh
  round was missed. With that and the battle's two plain, the route reaches its
  ending.

The movies don't see either: their presses drift in the shipping mode
anyway, and nothing in them checks an outcome.

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

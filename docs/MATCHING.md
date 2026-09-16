# Matching disassembly (scope)

This is a scoping document: what a byte-identical, reassemblable disassembly of
Chrono Trigger would take in this project, what it would prove, and how it
would plug into the disassembler, the static recompiler and the runtime. Built
so far: the measuring tool, `tools/disasm/census.py`, milestone MD0, the naive
split with its assembler and round trip (`tools/matching`, see
[MD0 as built](#md0-as-built)), and most of MD1, all code in source form (see
[MD1 as built](#md1-as-built)), most of MD4, closure checks in the build (see
[MD4 as built](#md4-as-built)), and the first part of MD2, the resource pointer
tables and the streams, strings and records they name (see
[MD2 as built](#md2-as-built-first-part)).

The motivation is completeness. The recompiler ([RECOMP.md](RECOMP.md)) turns
the code the analysis finds into C, and the interpreter runs whatever it
doesn't find. Movies show that almost everything they reach is found, but
nothing shows that nothing is missing. If every ROM byte is classified (code
with its widths, or typed data), every address in it is symbolic, and
reassembling the classification reproduces the ROM (CRC32 `2D206BF7`), the
code set is a checked artifact rather than a by-product of tracing. With
closure checks on top, it can be shown to be all of the game's code.

## 1. What "provably complete" means here

### Terms

- **Classification.** A map of all 4 MiB: each byte is in exactly one region,
  and each region is either *code* (a sequence of instructions, each with the
  M/X widths it runs with) or *data* of a named type (a table of 24-bit
  pointers, a compressed stream, BRR samples, an event script, ...). A RAM
  program (code the game decompresses or copies into RAM and runs there) is a
  data region in ROM whose decoded contents are a code section at its run
  address.
- **Matching.** Assembling the source generated from the classification gives
  a file with CRC32 `2D206BF7` and SHA-1 `de5822f4...` (APPROACH.md).
- **Symbolic.** Every operand or data field that holds an address (or a part of
  one: a bank byte, a low word, an offset from a base) is written as an
  expression over labels, not as a number.
- **Closed.** The code set is exactly the least set that contains the reset
  and interrupt entries and is closed under every control transfer the code
  can make, where every indirect transfer has an enumerated target set with a
  stated argument for why it's complete.

The claim at the end (MD4 below) is: **the recompiler's code set equals the
source's code set, which is closed; every byte outside it is data that no
transfer reaches.** It's a proof relative to the committed hints, each of
which is an assumption with the evidence written next to it, as in
`re/hints.toml` today.

### What byte identity proves, and what it doesn't

A source generated from the ROM by our own decoder matches almost by
construction. So the round trip is a weaker check than it sounds:

| Byte identity catches | Byte identity doesn't catch |
|---|---|
| Emitter and assembler bugs, non-canonical encodings (`LDA $0012` absolute vs direct page, `JMP` vs `JML`, `BRA` vs `BRL`) | Data emitted as instructions: data that happens to decode reassembles to the same bytes |
| Code/data boundaries that disagree with instruction lengths (a width that makes the last instruction of a region run into the next) | Width annotations that don't change an instruction's length (793 addresses are decoded with more than one width today, all but one with the same length) |
| Typed data whose parser doesn't consume its region exactly, if parsers re-serialize (below) | Code hidden in a data blob: a blob of the whole ROM matches too |
| | Numbers that should be symbols: they reassemble identically until something moves |

The checks that give the classification its meaning are therefore separate:

1. **Format round trips.** Every data type has a parser and a serializer, and
   each region must parse completely and serialize back to its bytes. An
   event script region must decode into whole commands with the lengths the
   interpreter uses; a stream must be consumed exactly by the decompressor.
   Misclassified bytes fail here, not at the CRC.
2. **The shift test** (included in the plan, MD3). pret-style projects prove
   pointers are symbolic by inserting padding and checking the game still
   works. Here:
   - *Static:* assemble with padding inserted at region boundaries (within a
     bank, using that bank's trailing padding, and by moving a region to
     another bank). The assembler records which emitted fields came from
     symbol expressions. A literal field whose value equals the old address of
     a moved label (a "pointer-shaped literal") is an error unless a hint says
     it isn't an address.
   - *Dynamic:* the shifted ROM plays the movie library on `ct_dev --native off
     --recomp off` with framebuffer hashes and audio identical to the original,
     frame for frame. WRAM isn't compared: pointers stored in RAM legitimately
     differ.
   - Some addresses can't move: the cartridge header and vectors
     (`$C0:FFB0-FFFF`), the reset stub at `$00:FF00`, and anything read
     through the `$00-$3F:8000-FFFF` mirrors. The assembler asserts those.
3. **Closure checks** (MD4):
   - **C1, indirect transfers.** Every `JMP (abs,X)`, `JSR (abs,X)`,
     `JMP (abs)` and `JML [abs]` (134 instructions today), every return used
     as a jump (`PER` + `JMP`, as at `$C2:EFD0`), every stack switch (the menu
     threads, `$C2:8203`) and every write to the RAM vector stubs
     (`$00:0500`/`$00:0504`) has an enumerated target set. Jump tables need a
     bound derived from the code (an `AND` mask, a compare and branch) instead
     of the length heuristic, which sizes 108 of the 126 tables today.
   - **C2, widths.** Each instruction has one width state per context. No
     continuation rests on an assumed return (26 today) or on pruning by
     coverage (13); the one length conflict (`$C2:8C52`) and the 9 overlapping
     instructions are modelled explicitly.
   - **C3, data-driven dispatch.** Where scripts name code, the targets come
     from parsing all the scripts in the ROM, not from the movies. That covers
     the event interpreter (`$C0:5910`, 256 commands at `$C0:5D6E`), the object
     script interpreter (`$C2:0F63`, 83 commands at `$C2:0F91`, whose commands
     `$34`, `$35` and `$4E` carry code addresses; their `indirect` hints say
     "only what runs in the movies"), the object handler words (`$C2:0553`),
     battle animation scripts (`$C1:7A6B`), enemy scripts (`$C1:B80D`), window
     command lists (`$C2:ED31`) and text control codes (`$C2:EFD4`). Closure is
     a fixpoint: code finds interpreters, typed data gives operands, operands
     give code.
   - **C4, RAM code.** Every instruction the game runs in RAM belongs to an
     enumerated RAM section: the program decompressed to `$7E:3000` (one
     stream, which the attract sequence, the jet bike race, the game over and
     the endings run; the race and the endings patch it at run time: operands,
     and the opcodes the `patch` sections list), the code `$C3:09E9` generates
     at `$7E:B574` (a `run` section), the `MVN`/`RTL` stub at `$7E:9690` that
     code in C2 and FF writes and patches, the JML stubs at `$00:0500`. This is
     the weakest check: proving statically that no other executed RAM bytes
     exist needs a model of every write to an executed range. The plan settles
     for enumerated sources plus the miss report.
   - **C5, disjointness.** No proven transfer targets a data region, and no
     code byte lies in one. Bytes that are both code and read as data are
     annotated, not an error.
   - **C6, reachability.** Every code region is reachable from the entries
     through proven edges, or annotated as unreachable with a reason.
     Unreachable code stays in the source as code.
   - **C7, empirical.** The miss report is zero over the movie library.

### The miss report

`ct_dev --misses FILE` (built in RECOMP.md's M3) lists every instruction the
interpreter runs, with its widths and count, as ROM code or RAM code, and RAM
code by the enumerated section whose image holds its bytes, "modified" (in a
section's range, with bytes no section has, and whether the opcode or only
operands differ) or "unknown" (in no section). With `--native on --recomp only`
everything generated code doesn't run is listed, so it is this check. The file
is also a coverage file, so its ROM lines can seed the analysis. Zero misses on
every movie is necessary but not sufficient: it only covers the paths the
movies take. C1-C6 are the sufficient part; the miss report catches a wrong
argument in them. Since M3 it is zero over the movie library (25 movies, 561550
frames; RECOMP.md, M3).

## 2. Current state, measured

Measured from this worktree with the analysis run as documented (hints, the 20
coverage files and 19 call logs of the main and save movies):

```sh
python3 -m tools.disasm --out build/disasm --coverage ... --calls ...
python3 -m tools.disasm.census --coverage ...
```

`tools/disasm/census.py` runs the analysis and classifies every byte. Code and
jump tables come from the analysis. In the rest, it claims, in this order: runs
of 16 or more `$00` or `$FF` (padding); byte ranges the decompressor at
`$C3:0557` (format in `game/data/decompress.c`) would consume exactly, with
segment ends met exactly, every back-reference inside the output so far, and
at least 64 bytes out (LZ streams); runs of 24 or more plausible 9-byte BRR
blocks ending in an end flag (BRR-like); runs of at least four same-bank 24-bit
values, three 16-bit values that land on instruction starts in their bank, or
eight increasing 16-bit values (pointer-like). Everything else is unknown. It
prints aggregates only.

### Summary

The tables below are the MD0-era measurement, kept because they sized the work.
Current numbers, after MD1, MD4 and the first part of MD2, are in
[MD2 as built](#md2-as-built-first-part): 249341 bytes of code, 194741 unknown
(4.6%), 1123163 in typed `[[data]]` regions and 2357173 in streams.

| | Code | Tables, header | Padding | LZ streams | BRR-like | Pointer-like | Unknown |
|---|--:|--:|--:|--:|--:|--:|--:|
| All 64 banks (KiB) | 232.8 | 3.1 | 126.5 | 2249.6 | 204.1 | 65.2 | 1214.6 |
| Share of ROM | 5.7% | 0.1% | 3.1% | 54.9% | 5.0% | 1.6% | 29.7% |
| C0-C2 (3 banks) | 173.5 | 2.6 | 0.7 | - | - | 4.6 | 10.6 |
| C3, C6, C7, CC-CF, D1, FD, FF (10 banks with some code) | 59.4 | 0.5 | 31.7 | 128.0 | 37.6 | 34.7 | 348.1 |
| The other 51 banks (no code found) | - | - | 94.1 | 2121.6 | 166.5 | 25.9 | 856.0 |

### Per bank (KiB)

| Bank | Code | Tables, header | Padding | LZ streams | BRR-like | Pointer-like | Unknown |
|---|--:|--:|--:|--:|--:|--:|--:|
| $C0 | 59.0 | 0.9 | 0.2 | - | - | 1.5 | 2.4 |
| $C1 | 59.0 | 0.9 | - | - | - | 1.3 | 2.8 |
| $C2 | 55.4 | 0.8 | 0.6 | - | - | 1.9 | 5.4 |
| $C3 | 3.8 | 0.0 | 2.0 | 24.7 | 1.1 | 0.9 | 31.4 |
| $C4 | - | - | 0.1 | 63.4 | - | - | 0.6 |
| $C5 | - | - | 3.2 | 59.3 | - | - | 1.6 |
| $C6 | 0.1 | 0.0 | 2.3 | 50.8 | 0.2 | 0.6 | 10.0 |
| $C7 | 2.6 | 0.0 | 4.0 | - | 29.6 | 3.1 | 24.6 |
| $C8 | - | - | 0.0 | - | 59.1 | - | 4.9 |
| $C9 | - | - | - | - | 63.9 | - | 0.1 |
| $CA | - | - | - | - | 34.7 | 2.8 | 26.6 |
| $CB | - | - | 0.1 | - | 2.3 | 5.1 | 56.4 |
| $CC | 4.8 | - | 3.1 | - | 1.5 | 4.2 | 50.3 |
| $CD | 15.4 | 0.4 | 3.4 | - | 2.3 | 9.8 | 32.7 |
| $CE | 3.4 | - | 1.1 | - | - | 2.8 | 56.7 |
| $CF | 6.8 | 0.0 | 1.1 | - | 0.5 | 8.9 | 46.5 |
| $D0 | - | - | 2.0 | 6.4 | 2.2 | 0.4 | 53.1 |
| $D1 | 5.7 | 0.0 | 4.2 | 12.7 | 1.1 | 1.8 | 38.5 |
| $D2 | - | - | 1.8 | 2.5 | 0.3 | 0.1 | 59.5 |
| $D3 | - | - | 1.1 | 7.1 | 1.2 | 0.1 | 54.5 |
| $D4 | - | - | 1.1 | 0.7 | 0.2 | 0.1 | 61.9 |
| $D5 | - | - | 0.4 | 29.0 | - | - | 34.6 |
| $D6 | - | - | 1.5 | 61.0 | - | - | 1.5 |
| $D7 | - | - | 0.3 | 58.1 | - | - | 5.6 |
| $D8 | - | - | 0.3 | 51.5 | - | 0.8 | 11.4 |
| $D9 | - | - | 0.9 | 59.1 | 0.3 | - | 3.7 |
| $DA | - | - | 1.0 | 62.6 | - | - | 0.4 |
| $DB | - | - | 32.0 | 32.0 | - | - | 0.0 |
| $DC | - | - | 8.4 | - | - | - | 55.6 |
| $DD | - | - | 0.9 | - | - | 0.3 | 62.8 |
| $DE | - | - | 2.7 | - | - | 1.0 | 60.2 |
| $DF | - | - | 0.1 | 63.9 | - | - | 0.0 |
| $E0 | - | - | 0.2 | 63.8 | - | - | 0.0 |
| $E1 | - | - | 3.1 | 55.4 | - | 0.0 | 5.5 |
| $E2 | - | - | 0.4 | 60.6 | 0.3 | - | 2.7 |
| $E3 | - | - | 4.0 | - | - | 0.2 | 59.8 |
| $E4 | - | - | 14.7 | - | 1.8 | 2.3 | 45.2 |
| $E5 | - | - | 2.5 | 54.4 | - | 0.7 | 6.4 |
| $E6 | - | - | 0.3 | 61.9 | - | - | 1.8 |
| $E7 | - | - | 0.2 | 60.4 | - | - | 3.3 |
| $E8 | - | - | 0.2 | 61.7 | - | - | 2.1 |
| $E9 | - | - | 0.0 | 60.4 | - | - | 3.5 |
| $EA | - | - | 0.2 | 61.0 | - | - | 2.8 |
| $EB | - | - | 0.3 | 62.5 | - | - | 1.2 |
| $EC | - | - | 0.3 | 61.5 | - | 0.0 | 2.1 |
| $ED | - | - | 0.2 | 63.8 | - | - | 0.0 |
| $EE | - | - | 0.1 | 63.9 | - | - | - |
| $EF | - | - | 0.2 | 57.0 | - | 0.0 | 6.7 |
| $F0 | - | - | 2.5 | 61.5 | - | - | - |
| $F1 | - | - | 0.5 | 63.5 | - | - | - |
| $F2 | - | - | 0.4 | 63.6 | - | - | - |
| $F3 | - | - | 0.5 | 63.5 | - | - | - |
| $F4 | - | - | 0.5 | 63.5 | - | - | - |
| $F5 | - | - | 0.5 | 59.5 | - | 0.0 | 4.0 |
| $F6 | - | - | 3.4 | 21.9 | 0.2 | 2.3 | 36.1 |
| $F7 | - | - | 0.3 | 1.6 | - | 4.1 | 58.0 |
| $F8 | - | - | 0.0 | 38.2 | - | 1.6 | 24.2 |
| $F9 | - | - | - | 44.0 | - | 1.1 | 19.0 |
| $FA | - | - | - | 63.7 | - | - | 0.3 |
| $FB | - | - | - | 64.0 | - | - | 0.0 |
| $FC | - | - | - | 46.5 | - | 2.5 | 15.0 |
| $FD | 15.1 | 0.0 | 1.9 | 39.2 | 1.3 | 0.4 | 6.0 |
| $FE | - | - | 0.7 | 61.8 | - | 0.3 | 1.3 |
| $FF | 1.5 | - | 8.6 | 0.7 | - | 2.0 | 51.2 |

### What the numbers say

- **Code is concentrated.** Banks C0-C2 are 87-92% code; the analysis found
  no code at all in 51 banks. Of the 238421 code bytes, 177628 are in C0-C2.
  The 10 banks with some code hold 348 KiB of unknown bytes, which is where
  undiscovered 65816 code would be; how much of it is code isn't measured.
- **Over half the ROM is compressed.** 1449 streams in the `$C3:0557` format,
  2.20 MiB decompressing to 3.76 MiB (ratio 1.71), none larger than 32 KiB out.
  The format check alone could accept chance byte ranges, but 1353 of the 1449
  streams (93%) have a 24-bit pointer to their first byte somewhere in the ROM,
  which chance wouldn't give. Whole banks (DF-E0, E6-EE, F0-F4, FA-FB) parse
  as consecutive streams. Caveat: `$C3:8000-CFFF`, where the object script
  operands in the hints were found, parses as 12 streams (20 KiB); either those
  scripts are stored compressed (literals keep operand bytes intact) or that is
  a false positive, to be settled in MD2.
- **Samples.** 119 BRR-like runs, 204 KiB, 190 KiB of it in C7-CB next to the
  sound driver interface in C7. The smaller hits in code banks are probably
  chance.
- **The unknown 1.19 MiB is mostly structured data.** By Shannon entropy of the
  unknown bytes per 4 KiB page: 7.7 KiB below 3 bits/byte, 218 KiB at 3-5,
  763 KiB at 5-6.5 (tables, maps, scripts, text, uncompressed tiles), 216 KiB
  at 6.5-7.5 (dense graphics, or packed formats the census doesn't know), none
  above 7.5, and 9.4 KiB in sparsely unknown pages. D2-D4 are 55-62 KiB
  unknown each, which fits the uncompressed 32-byte sprite tiles that
  `field/graphics.c` copies from D2-D5.
- **Pointer-like runs are candidates, not findings.** 913 same-bank 24-bit runs
  (19 KiB) are fairly specific. The 16-bit rules have many false positives in
  code banks, where about every other address is an instruction start.
- **Code-side work list** (from the analysis): 8082 contexts over 7913 entries,
  109841 instructions, 126 jump tables (108 sized by heuristic), 1 unresolved
  indirect jump (`$C2:8203`, covered by entry hints), 26 calls continued on
  assumed widths, 13 returns pruned by coverage, 1 width conflict, 9
  instructions overlapping another instruction's start, 5351 coverage seeds
  static tracing didn't reach, and 5259 executed RAM addresses.
- **Symbolization work list** (operands of the found code): 10228 branches,
  5287 `JSR`/`JMP abs` and 500 `JSL`/`JML` are already rendered with labels.
  To do: 1485 long data operands into ROM; 23190 absolute data operands whose
  bank is the data bank (9940 of them `>= $8000`, which is ROM or bank
  `$7E`/`$7F` RAM depending on DB); 6237 16-bit immediates, some of them
  pointer halves; 372 `MVN`/`MVP` bank operands; 46 `PEA`, 2 `PER`. Of the
  1485 long operands into ROM, 992 point into bytes the census leaves unknown.

## 3. Work breakdown

### Toolchain

The assembler must handle:

- **HiROM mapping** with FastROM: `org` in `$C0-$FF` bus form, file offset
  `(bank & $3F) << 16 | addr`, and assertions for regions used through the
  `$00-$3F`/`$80-$BF` mirrors.
- **Explicit encodings.** The original uses absolute addressing for values
  below `$100` and long addressing for bank-local addresses; every operand
  size must be forced, never inferred (asar `.b/.w/.l` suffixes, ca65 `z:`/
  `a:`/`f:` prefixes).
- **Widths.** Immediate sizes follow M/X. Generated source states each
  immediate's size explicitly; `REP`/`SEP` tracking in the assembler (ca65
  `.smart`, WLA-DX `.accu`/`.index`) is only needed for hand-edited source.
- **Bank crossing.** Regions that run across a bank boundary (streams can, the
  decompressor carries into the next bank), `BRL`/`JML` across banks, and
  bank-byte/low-word expressions (`^label`, `label & $FFFF`, `label - base`).
- **Binary includes** with offset and length, **assertions** (`warnpc`-style
  end checks per region), and a map/listing output that says which fields came
  from symbols (for the shift test).

| Option | For | Against |
|---|---|---|
| asar | Built for SNES: HiROM mapper modes, size suffixes, `warnpc`, `incbin` ranges, familiar to the ROM hacking community | Oriented to patching; single pass over SNES addresses rather than relocatable objects; an external C++ dependency |
| ca65/ld65 | Mature, relocatable objects and a linker config per bank (natural for moving regions), `.smart` width tracking, map files | 65816 support has rough edges (address size inference, `MVN`/`MVP` operand syntax differs between versions); HiROM layout has to be spelled out in the linker config |
| WLA-DX | 65816 with SNES header and HiROM directives, sections and a linker | Less common for 65816 disassembly projects; another dependency |
| bass | Table-driven, small | Little maintenance, few users |
| Our own (Python) | No new dependency (the build already runs Python for the recompiler); reuses `tools/disasm/cpu.py`, whose opcode table the recompiler's 5.12M SingleStepTests already exercise; exact control of encodings and of the symbol-field record the shift test needs | A new tool to write and test; a bug shared by emitter and assembler cancels in the round trip |

**Chosen (no strong preference either way, so the recommendation stands):**
generate one closed, explicit dialect (every operand size
written out, no inference, no macros) that is a subset of asar syntax, and
assemble it with a small stdlib Python assembler in `tools/matching/` as the
build's round trip. Where asar is installed, the same source assembled with
asar must also match; that guards against cancelling bugs and gives anyone who
wants to edit the source a standard tool. ca65/ld65 is the alternative if
relocatable objects turn out to matter more than a dependency-free build.

### Source layout and naming

- The source is generated (see section 6): `build/<cfg>/matching/src/`. MD0
  writes one file per bank; modules come with the layout map.
- A committed layout map, `re/layout.toml`, assigns ROM ranges to modules
  named like `game/` (`field/object_update`, `battle/sprites`,
  `data/decompress`), defaulting to `bank_XX` for unassigned ranges. A
  top-level `rom.asm` places the modules in ROM order, since matching fixes the
  order.
- Labels come from, in order: `[[name]]`/`[[entry]]` hints, the names in
  `game/registry.c` (`field_move_object` at `$C0:AA07`), data region names from
  the new data hints, then generated `sub_`, `loc_`, `jt_` and per-type
  prefixes (`lz_`, `brr_`, `ptrs_`). The build checks that every registry
  address is an instruction start in the source with widths compatible with
  the entry's precondition (MD0 reports registry addresses that aren't
  instruction starts in `report.md`, without the width check).
- Generated parts: instructions, labels, region directives, `incbin` lines.
  Hand-curated parts are all in `re/`: names, types, pointer facts, layout,
  comments. Hint comments carry over as source comments.

### Build-time extraction

The extraction step checks the ROM's CRC32 and writes every
data region to the build tree (`matching/src/data/<type>/<label>.<ext>`; MD0
writes only `raw/bank_XX/<address>.bin`): raw
blobs, and decoded forms where a type has one (decompressed streams, BRR as
is, graphics as planar tiles). Source refers to them with `incbin`. The
reassembled ROM is also written only to the build tree. Nothing extracted,
decoded or reassembled is ever in the repository, and the build fails with a
clear message without a ROM, as `tools.recomp` does.

Compressed regions whose contents become symbolic (a RAM program, a script
with pointers) are assembled from their decoded form. The matching build
doesn't need a compressor that reproduces the original encoder's choices: it
checks that the assembled contents equal the original stream decompressed, and
emits the original stream bytes. Only shifted builds recompress, with any
valid encoder.

### Data classification

| Type | Known in the repo today | Work |
|---|---|---|
| LZ streams (`$C3:0557`) | Decoder ported and validated (`game/data/decompress.c`); 1449 candidate streams, 2.20 MiB | Confirm each by its referencing pointer; type every stream's decompressed contents (below) |
| RAM programs | Scene program at `$7E:3000` (three copies), `$7E:9690` stub, `$00:0500` stubs | Code sections assembled at their run address; find which streams they come from; model patched operands as fields |
| Event scripts | Interpreter `$C0:5A93`, table `$C0:5D6E`, fetched from `$7F:2001,X`; 185 routines ported, most of them command handlers with their operand layouts (`game/event/`) | Command length table from the ports; parse every location's script; operands that are pointers (the `$B8` dialog table) |
| Dialog and menu text | Menu strings: tile bytes, control codes `$01-$0F` (`$C2:EFD4`), digit tiles `$D4-$DD` | Dialog encoding, string tables, their pointers |
| Object and sprite scripts | Interpreters `$C2:0F63` (83 commands), `$C2:0E1D`; `$34`/`$35`/`$4E` carry code addresses | Command lengths; parse all scripts; feeds C3 |
| Battle scripts and tables | Animation script channels (`$C1:4BBE`, handlers `$C1:7A6B`), enemy scripts from bank CC (`$C1:8461`, handlers `$C1:B80D`), enemy table `$E4:F600`, pose table `$CC:F77F` | Formats, record sizes, table lengths |
| Graphics | Sprite tiles copied from D2-D5, flip table `$C0:FD00`, sine table `$C0:F900`, tile layers unpacked by `$C0:A521`, glyph bitplanes (`$C2:5E36`), battle graphics sets | Region bounds and tile formats; most need no pointers, only proof of who reads them |
| Palettes, tilemaps | Uploads in the NMI/vblank ports | Region bounds, formats |
| Audio | Sound driver interface in C7 (`$C7:0004`, uploads from `$C7:0655`); 204 KiB BRR-like | SPC700 driver bytes (SPC700 code; a typed blob unless its closure is wanted), sequence data, sample directory |
| Pointer and offset tables | 913 candidate 24-bit runs; jump tables from the analysis | Lengths and element types |
| Header, vectors, padding | `$C0:FFB0-FFFF`; 126.5 KiB of fill runs | Trivial |

"Typed" means the type states which fields are addresses (possibly none) and
the evidence for the region's bounds (the code that reads it, its table
length). For graphics and samples that's the reader and the length; nobody
needs to understand the pictures.

### Pointer symbolization

1. **Control flow**: branches, `JSR`/`JMP abs`, `JSL`/`JML` are labels already.
2. **Long data operands** (1485): a label, or `label + n` inside a region.
3. **Data-bank operands** (23190 absolute operands): the analysis tracks the
   data bank like it tracks widths (`PHK`/`PLB`, `LDA #bank : PHA : PLB`,
   `PEA`, and the entry state per context). Where DB is a ROM bank the operand
   becomes `label & $FFFF` with an assertion that the label's bank is that DB.
   Instructions reached with several data banks keep a number and a hint
   saying why.
4. **Immediates** (6237 16-bit): candidates are values whose pairing gives a
   ROM address (a low word and a bank byte stored into the same pointer cell,
   as for the decompressor's source at `$0300`), confirmed by a `[[pointer]]`
   hint with the site and the target expression.
5. **Bank operands**: `MVN`/`MVP` (372), `PEA` (46), bank-byte immediates.
6. **Tables**: jump tables and indirect-hint tables as label lists; 24-bit and
   16-bit data tables once typed.
7. **Inside typed data**: script operands, handler words, pointer fields of
   records, including those in decompressed contents.
8. **Computed addresses**: expressions like the entry into the `LDA`/`STA`
   run at `$CE:E293` (`$E7C7 - 6n`) are written as expressions over labels.
9. **Lint**: any remaining literal, in code or data, equal to a label's address
   is reported until a hint marks it as not an address.

### Round trip

`python3 -m tools.matching` (and the `ct_matching` CMake target when `CT_ROM`
exists, like `cmake/recomp.cmake`): extract, emit, assemble, compare CRC32 and
SHA-1. On a mismatch it prints the first differing address and the source file
and line that wrote it, never the bytes. From MD2, every format parser also
re-serializes its region in the same run.

### MD0 as built

```sh
python3 -m tools.matching --rom roms/ct-usa.sfc --out build/main/matching \
    --coverage traces/coverage-gato.txt ... --calls traces/calls-gato.txt ... [--asar auto|off|PATH]
python3 -m tools.matching.asm build/main/matching/src/rom.asm -o out.sfc [--sym out.sym]
cmake --build build/main --target ct_matching     # with the recompiler's CT_ROM, CT_RECOMP_COVERAGE, CT_RECOMP_CALLS
```

| Path | What |
|---|---|
| `tools/matching/asm.py` | The assembler for the dialect below (stdlib only) |
| `tools/matching/emit.py` | Layout, labels and source from an `Analyzer` that has run |
| `tools/matching/__main__.py` | ROM check, analysis, writing the tree, assembly, CRC32/SHA-1 check, optional asar run |
| `cmake/matching.cmake` | `ct_matching`: in the default build when `CT_MATCHING` is on (default); `CT_ASAR` if found |
| `tests/test_matching_asm.py` | Encodings, directives, errors, the emitter on synthetic code |

The command verifies the ROM's CRC32 and SHA-1, runs the analysis in process
with hints, coverage and call logs (call targets get `sub_` labels), and writes
into the build tree only:

- `src/rom.asm` (`hirom`, then `incsrc` of each bank in ROM order),
  `src/bank_C0.asm` ... `src/bank_FF.asm` (one `org $XX0000` each, ending in
  `warnpc`), `src/data/raw/bank_XX/<address>.bin` (the extracted blobs);
- `rom.sfc` (the reassembled ROM), `rom.sym` (labels), `report.md` (the byte
  breakdown per bank, counts only).

Files whose content didn't change aren't rewritten and stale ones are removed;
two runs give identical trees. The build step fails unless the result has the
ROM's CRC32 and SHA-1 and every ROM byte was assembled exactly once.

**Layout.** Instructions come from `Analyzer.code`, merged by ROM offset (the
reset stub runs at `$00:FF00` and is written at `$C0:FF00` with a comment).
Instructions whose byte ranges intersect, and addresses decoded with more than
one length, form clusters written as `db`, with the decodes in comments and a
label at every instruction start. Everything else is `incbin`, cut at bank
boundaries and at every label, so labels always fall between statements.

**Labels**, in order: `[[name]]`/`[[entry]]` hints, `game/registry.c` names
(all of them, as extra labels), then generated `sub_`, `jt_`, `loc_` by address.
Symbolic operands: branch, `BRL` and `PER` targets (a `PER` into the middle of
an instruction is `label+n`, as at `$C2:EFCD`), `JSR`/`JMP abs`, `JSR`/`JMP
(abs,X)` onto a labelled table, and `JSL`/`JML long` whose operand is the
label's `$C0-$FF` address. All other operands are numbers until MD3. Each
context entry gets a comment with its widths and how it was found. The analysis
doesn't track the data bank yet (MD3), so there are no DB annotations.

**Result** (this ROM, hints, the 20 coverage files and 19 call logs):

| | Bytes | KiB | Share |
|---|--:|--:|--:|
| Instructions | 238402 | 232.8 | 5.68% |
| `db` (4 clusters, 16 instruction starts) | 19 | 0.0 | 0.00% |
| `incbin` (393 files) | 3955883 | 3863.2 | 94.32% |

108924 instructions and 11355 labels; instructions plus `db` are exactly the
analysis's 238421 code bytes. The clusters are `$C1:AFD6-AFDD`, `$C1:AFF6-AFF9`
and `$C1:B001-B004` (overlaps) and `$C2:8C52-8C54` (the length conflict). Every
registry address is an instruction start. Time: analysis 5.5-7 s, emit 0.8 s,
write 0.2 s, assembly 1.4 s, about 8-9 s in all. CRC32 `2D206BF7` and SHA-1
`de5822f4...` match. asar wasn't available, so the asar build is unchecked (see
the dialect's open points).

### Dialect

One closed dialect, meant to be a subset of asar syntax, so that the Python
assembler and asar read the same source:

- **Directives:** `hirom` (required, the only mapper), `org ADDR` (bus address,
  HiROM mapping), `base ADDR` / `base off` (assemble at a logical address while
  bytes go where `org` put them, for RAM sections; `org` also ends it),
  `warnpc ADDR`, `incsrc "file"`, `incbin "file"` (whole files, no ranges; paths
  relative to the including file), `db`/`dw`/`dl` with comma-separated values,
  `label:`, `;` comments.
- **Expressions:** numbers (`$hex`, `%binary`, decimal) and labels joined by
  `+`/`-`. No other operators, no defines, no macros.
- **Instructions:** mnemonics as in `tools/disasm/cpu.py` (case-insensitive),
  operand size always a suffix: `LDA.b $12`, `LDA.w $0012`, `LDA.l $000012`,
  `LDA.b #$12`, `LDA.w #$1234`, `JMP.w ($1234,X)`, `JML.w [$0000]`,
  `PEA.w $2100`, `PEI.b ($30)`, `LDA.b ($03,S),Y`. `JML.l`/`JSL.l` for long
  jumps and calls. Only operands whose layout the mnemonic fixes take no
  suffix: branches, `BRL` and `PER` (a label), `MVN`/`MVP` (`MVN $src,$dst`,
  which encodes the destination bank first). A missing suffix, a suffix on
  those, or a size that isn't an addressing mode of the mnemonic is an error.
- **Width annotations:** `;@ m8 x16` (`8`, `16` or `?`) comments state the
  widths the following instructions run with. Until the next `org`, `db`/`dw`/
  `dl` or `incbin`, immediates of width-dependent instructions must have the
  annotated size, and an unknown width with such an immediate is an error. The
  emitter writes one whenever the widths change. asar ignores them.
- **Stricter than asar:** a ROM byte written twice, a statement across a bank
  boundary, a value that doesn't fit its field (`dw label` with a 24-bit label
  is an error; write the difference), a branch or `JSR.w`/`JMP.w` label in
  another bank, and an 8-bit branch out of range are errors.
- **16-bit displacements wrap.** `BRL` and `PER` reach the whole bank: the
  displacement is `(target - next) & $FFFF`, as the CPU adds it. The original
  needs this: 7 `BRL`s in C0 (`$C0:00DB`, `$C0:00F1`, `$C0:086E`, `$C0:0902`,
  `$C0:17C2`, `$C0:17EB`, `$C0:185F`) jump forward past `$8000` bytes by
  wrapping backwards.

The asar command is `asar --no-title-check --fix-checksum=off rom.asm out.sfc`
run in `src/`. Open points to confirm on a machine with asar: that it accepts
the suffix on `JML.w [$0000]` (1 in the source), `PEA.w` (46) and `PEI.b` (3),
and on `BRK`/`COP`/`WDM.b #imm` (none in the source today); that its
`MVN`/`MVP` operand order is source first; that it takes the wrapping `BRL`s
without a range error (otherwise those 7 need another spelling); and that it
doesn't pad or change the checksum of a new file. Each is a change to the
emitter's spelling, not to the layout.

**Next (MD1).** MD0 listed what MD1 needed first: a check that every coverage
address is an instruction start with a compatible width, typed `[[data]]` hints
that cut and name `incbin` regions, hints for the heuristic tables and the
assumed and pruned returns, a look at the C1 overlap clusters, and a `base`
directive with a Python decompressor for RAM programs. All of it is built; see
[MD1 as built](#md1-as-built).

### MD1 as built

Same commands as MD0 (`python3 -m tools.matching ...`, the `ct_matching`
target). The run now also fails unless:

- **Coverage.** Every executed ROM address in the coverage files is an
  instruction start in the source, and every width pair it ran with decodes
  there to the written length and is among the widths the analysis gives the
  instruction (`Emitter.check_coverage`). An `[[overlap]]` hint names the
  decode the source writes where an instruction really runs with two lengths;
  the other decode must end on an instruction boundary.
- **Data.** Every `[[data]]` region holds no instruction, has its type's shape
  (one fill value for `padding`, whole elements for `words`/`ptr16`/`ptr24`/
  `struct`, exactly one stream for `lz`) and receives no traced transfer. The
  emitter cuts `incbin`s at region bounds, labels them with the hint's name (or
  `type_ADDRESS`) and writes them as `data/<type>/<label>.bin`. The report
  lists incbin bytes by kind per bank and every untyped blob in a bank with
  code.
- **RAM.** Every `[[ramcode]]` section is written as `src/ram/<name>.asm`
  (`org $C00000` then `base` at its run address), assembled into its own image
  and compared with the section's image, and every executed RAM address inside
  a section is an instruction start there with a compatible width
  (`tools/matching/ramsrc.py`).

Analysis changes (`tools/disasm`):

- **No pruning by coverage.** Every static return is followed; returns the
  coverage never ran with are only reported (`--prune-returns` keeps the old
  behavior for exploration). Of MD0's 13 pruned returns, 12 add width variants
  without any conflict and stay; the thirteenth (`C0:7180`) came from a real
  16-bit exit at `C0:71EB` that would turn the object code after it into
  garbage, and `returns` hints on `C0:7199`, `C0:7212`, `C0:7217` pin it.
- **RAM sections.** `[[ramcode]]` hints give an image at a run address: a
  stream decoded by `tools/disasm/lz.py` (the `$C3:0557` format, written from
  `game/data/decompress.c`), bytes copied from ROM, or the constants a run of
  code stores (`writes`, simulated from `source` to `end`). The analysis traces
  them like ROM code; the recompiler still skips non-ROM code.
- **Hint kinds**: `[[data]]` gains `type` and `name`; `[[overlap]]`;
  `[[ramcode]]`; `[[noreturn]] addr` per entry; `[[entry]]` without widths
  (interrupt handlers, entered with any); `jumptable` `targets` for tables
  filled at run time; `indirect` on a `JSR`/`JSL` whose operand code writes at
  run time. See docs/DISASSEMBLY.md.
- **Census** counts typed hints (padding, lz and brr in their columns, the rest
  as typed data) and scans unknown bytes, and separately `mixed` and
  `undetermined` regions, for code-shaped runs: six or more instructions
  without BRK/COP/WDM/STP, ending in a return or an unconditional jump, calling
  or jumping to two or more known entries. The rule finds nothing in the 2.2 MiB
  of candidate streams and about 40% of known C1 code; it is evidence, not
  proof.

**Hints added** (sections headed "MD1" in `re/hints.toml`):

- **Jump tables** (the 108, plus 2 in code found later and 4 in the attract
  program): 22 bounded by an index mask, a compare or a loop bound, 14 by ending
  where another dispatcher's table begins, the rest by running up to their first
  handler or other code. 19 counts changed from the heuristic's; for 5 the calls
  files show entries the heuristic cut off called (`C0:21EA`, `C0:5AB1`,
  `C1:004A`, `C1:29AE`, `CF:EFE6`), and `C1:8333`'s bound (13,
  not 22) removed the three C1 overlap clusters, which were data decoded as
  code through that table.
- **Returns and widths**: 5 `noreturn` in C0 (error screen, stack resets), the
  `C3:0011` chain returning through `C3:0F83`'s RTL, 3 attract task handlers,
  generated `$7E:B574` returning with the caller's widths, two task-loop
  `state` hints, `C2:EFD3` entered 8-bit, and unknown-width entries for 15
  interrupt handlers (the 8 MD0 knew of had one width pair; coverage entered
  three of them with three or four).
- **The width conflict**: `C2:8C52` runs with both widths (coverage has m8 x8
  and m16 x16), as a 2-byte `LDA` plus `XBA` or a 3-byte `LDA`; an `overlap`
  hint writes the 8-bit decode.
- **RAM**: the attract/title/race program (`$7E:3000`, 34164 bytes of the
  34175-byte output of the stream at `C3:1313`, which `C3:0056` loads through
  the resource word at `FE:0003`), the `MVN`/`RTL` stub at `$7E:9690`
  (`over` the program's range), and 7 NMI/IRQ stub images from the installers
  in C0 (two), C2 (two), C3, D1 and FD; the attract program's two self-modified
  `JSR` operands and three run-time task tables.
- **Code nothing found reaches**: 131 entries (`kind = "unreached"`): bytes
  between known code that decode completely with the surrounding widths and
  only call known code, jump-list slots, and runs of routines in C2:77F2-7B5A,
  C6:E237-EC7C and FF:FF3E found by the code-shape scan. Most are probably
  reached through script data the analysis doesn't parse yet (MD4, C3).
- **Data**: 171 typed regions cover every other byte of the 13 banks with code:
  72 `bytes` and 32 `words` tables with their long indexed readers, 16 `ptr16`,
  14 `script` (object scripts passed to `sub_C204D9`, window descriptors passed
  to `C2:8385` and `C2:ED31`), 1 `lz`, `padding`, `header`, `text`,
  `ram_code`, 12 `undetermined` (3.4 KiB, each with why it isn't code) and 19
  `mixed` (565 KiB: large regions typed by their readers and census
  composition, not split).

**Result** (this ROM, hints, the 20 coverage files and 19 call logs):

| | MD0 | MD1 |
|---|--:|--:|
| Jump tables (heuristic) | 126 (108) | 132 (0) |
| Calls continued on assumed widths | 26 | 0 |
| Call returns pruned by coverage | 13 | 0 (55 returns the movies never ran, traced) |
| Width conflicts | 1 | 0 (1 declared by an overlap hint) |
| `db` clusters in the source | 4 | 0 |
| Executed ROM addresses that are instruction starts with compatible widths | not checked (9 in clusters, 6 with other widths) | 84877 of 84877 |
| Code bytes | 238421 | 245680 |
| Instructions in the ROM source | 108924 | 112150 |
| RAM sections assembled and matching their images | 0 | 9 |
| Executed RAM addresses in sections, compatible | - | 3194 of 3194 (2065 more in no section) |
| Untyped blob bytes in the 13 banks with code | - | 0 |
| Census unknown bytes in C0-C2 / the other 10 code banks | 10.6 / 348.1 KiB | 0 / 0 KiB |
| Code-shaped runs in unknown bytes / in `mixed` and `undetermined` hints | - | 0 / 3 (135 bytes) |

CRC32 `2D206BF7` and SHA-1 `de5822f4...` still match; the run takes about 5 s.
The recompiler builds from the same analysis (11160 blocks), and the six main
movies (`new-game` 7200, `menu` 8900, `field` 11400, `fair` 12450,
`fair-companion` 17900, `gato-battle` 21600 frames) in `--native shadow` report 0
mismatched generated blocks and no mismatching hand port.

**Since (recompiler M3).** Five sections more: `race_matrix_code` (format
`run`) and four `patch` sections for the opcodes the race and the Beyond Time
ending write into the program. With the 23 coverage files of the main, save
and ending movies, the build checks 14 RAM sections against their images and
7304 of 7304 executed RAM addresses (none in no section); `ramsrc.py` accepts
an instruction start that exists only in a patch of the code (and, in a patch,
instructions of the patched code it cuts or branches to).

**Left for MD1:**

- `$7E:B574-C994`: the jet bike race runs code at 2065 addresses that `C3:09E9` writes
  through the WRAM port at run time (it emits `LDA` immediate, `TCD`, `LDA`
  absolute, `STA` direct page and a final `RTS`). A section format that runs
  the generator would make them a checked section. Done in M3 of the
  recompiler: the `run` format, section `race_matrix_code` (5153 bytes, all
  instructions, assembled and compared like the others).
- The partial vector installers (`C3:08DF`, `C3:093E`, `CD:041D`) only
  replace the target of a stub another installer wrote; their handlers are
  entries, their images aren't sections.
- Three code-shaped runs inside typed data: `C2:FEE9` (a routine at `C2:FEFB`
  whose entry widths conflict with the code it calls), `FF:FC6E` and `FF:FF0F`
  (routine-like bytes inside regions typed `mixed`).
- The `mixed` regions (565 KiB, nearly all of it in C3 and C6-FF) are typed by readers only; MD2
  splits and parses them. The `unreached` entries and the "only what runs in
  the movies" hints (object script operands, the attract program's task
  tables) wait for script parsing (MD4, C3).
- Table bounds from layout ("runs up to its first handler") hold for the code
  as written; an index guard would make them independent of it.
- asar is still unchecked; `base` is spelled as asar's.

### MD2 as built (first part)

Same commands as MD0, MD1 and MD4. The analysis, the census and the matching
build now expand `[[resource]]` hints into typed data regions before tracing
(`tools/disasm/resources.py`, DISASSEMBLY.md), and the matching run fails if any
of them doesn't fit.

| Path | What |
|---|---|
| `tools/disasm/resources.py` | `[[resource]]` expansion: the tables, their targets (`lz`, `text`, `blob`), merging into the hand hints and `fill` regions |
| `tools/disasm/hints.py` | the `[[resource]]` hint and `fill = true` on `[[data]]` |
| `tests/test_resources.py` | 20 tests on synthetic data (no ROM): each target kind, `skip`, repeated entries, `slots`, splitting, fill, idempotence |

**Resource pointer tables.** The loaders all have one shape: a resource id is
multiplied by three (or doubled) into X, two long indexed loads read the
pointer's low word and its bank byte, and the pair is either stored at
`$0300`/`$0302`, where the decompressor at `$C3:0002` takes its source, or kept
as a data pointer. 36 tables are declared with the reading code in their
comments. They give, with 0 problems:

| Kind | Tables | Regions | Bytes |
|---|--:|--:|--:|
| Streams (`lz`), consumed exactly by `tools/disasm/lz.py` | 19 | 1371 | 2354164 (4007103 out) |
| Strings (`text`), each to its zero terminator | 15 | 4405 | 170097 |
| Blobs (`blob`), sized by the next entry or by the hint | 3 | 617 | 279808 |
| The tables themselves (`ptr24`, `ptr16`) | 36 | 36 | 7267 |

- **Streams.** 6645 entries over the 36 tables (66 skipped, each with a comment,
  186 repeating another entry's target) confirm 1371 streams by a pointer a
  loader reads. Before this the census claimed 1251 by shape alone; 18
  candidates (9534 bytes) are left for it to claim, 10 of them with a 24-bit
  pointer somewhere in the ROM. The largest tables are `$FC:F9F0` (513 entries,
  read at `$C0:56D4` with the id from the location record's byte +8,
  decompressed to `$7F:2000`), `$E4:2000` (248, battle sprites), `$F6:2220`
  (219), `$F6:1E00` (202) and `$FE:0000` (73, the attract program's).
- **Strings.** Fifteen self-describing tables of 16-bit offsets into their own
  bank (`$D8:D000`, `$D8:DD80`, `$DE:C000`, `$DE:E300`, `$F6:A000`, `$F6:B230`,
  `$F7:0000`, `$F7:4900`, `$F8:4650`, `$F9:B000`, `$FC:BA00`, `$FF:4460`,
  `$FF:5860`, `$FF:6B00`, `$FF:8400`): entry 0 points just past the table, the
  entries never decrease, and the last points one past the last terminator. The
  4405 distinct strings tile their ranges with **58 bytes** left over in three
  places (9 in `$DE:E300`, 48 in `$F8:4650`, 1 in `$FC:BA00`), which is the
  format's exact-consumption check. What reads them isn't established: no
  instruction in the code set sets its data bank to those banks or holds a long
  operand into them, so the readers take a pointer out of RAM.
- **Blobs.** `$E4:2300` (231 entries, the second pointer of a battle sprite,
  `$1C78,Y`) covers banks DC, DD, DE, E1 and E3 end to end; `$E4:2600` and
  `$E4:2800` (193 records each, 16-bit offsets with the bank byte from the
  immediate at `$CC:E9CE`) cover `$E4:3000-A5EF` and `$E4:A800-CC56`. Record
  lengths come from the next entry, since `$C1:1C8B` adds a frame offset to the
  pointer and reads through the sum.

**Other regions typed.** `fill = true` gives banks D2-D5 the type `gfx`: the
unrolled 32-byte tile copies at `$C0:E7C3`, `$C0:E846`, `$C0:E8C9` and
`$C0:E94C` read one 4bpp tile from any offset of one bank, and the rest of each
bank is the compressed battle sprites of the `$E4:2000` table, which carve the
fill regions up. `$F6:0000-1BFF` is 512 records of 14 bytes, one per location:
`$C0:0945` builds the index with the hardware multiplier (`$4203 = $0E`) and
adds `$0E00` for indices over 255, which is 256 * 14, and the fields read
through it are the resource ids of five of the stream tables.

**Result** (this ROM and the hints, no coverage):

| Census class | Before (MD4) | After | |
|---|--:|--:|---|
| Unknown | 877473 | 194741 | 20.9% -> 4.6% of the ROM |
| Typed `[[data]]` | 572058 | 1123163 | |
| Compressed | 2198893 | 2357173 | 1251 by shape -> 1371 by pointer, 18 by shape |
| Padding | 96893 | 85806 | fill inside typed regions counts as its type |
| Pointer-like runs | 26514 | 15358 | |

Unknown bytes per bank, before and after: `$C8` 5943, `$C9` 120, `$CA` 27209,
`$CB` 57769, `$D0` 54359, `$E4` 46325 -> 9925, `$E5` 6509 -> 6018, `$EF` 6902 ->
3704, `$F5` 4064, `$F6` 37002 -> 20507, `$F7` 59363 -> 135, `$F8` 24736 -> 755,
`$F9` 19414 -> 1456, `$FC` 15409 -> 64, `$D8` 11690 -> 17, `$DE` 61669 -> 882,
`$E1` 5601 -> 249, `$FE` 1294 -> 1247, the rest unchanged and small. Banks DC,
DD and E3 went from wholly unknown to wholly typed.

**Validation.** `unittest` passes (119 tests). CRC32 `2D206BF7` and SHA-1
`de5822f4...` still match, every `[[data]]` region fits its bytes, the closure
checks C1, C2, C3, C5, C6 and C7 pass, the 15 RAM sections assemble to their
images, and the six main movies in `--native shadow` report 0 mismatched
generated blocks and no mismatching hand port.

**Left for MD2:**

- 194741 unknown bytes: the audio banks (`$CB` 57769, `$CA` 27209, `$C8` 5943;
  the sound driver's tables at `$C7:0BA4`-`$C7:0D18` point into them, but their
  layout isn't read yet, and the block format the SPC700 upload uses only
  parses some of the targets), `$D0:0000-DFFF` (54 KiB, no reader found; the
  palettes at `$D0:FA00` and `$D0:FD00` and the 6-entry stream table at
  `$D0:E000` are the only parts tied to code), `$F6` (20507: the location
  record table's tail `$F6:1C00-1DFF` and the blocks the skipped `$F6:2220`
  entries point at), `$E4` (9925: `$E4:0000-17B7`, `$E4:2A00-2FFF` and the
  tables at `$E4:F000`, `$E4:F600`, `$E4:FFE0`), and smaller pieces in E5, EF,
  F5, FE.
- 445 KiB is still typed `mixed` (C0, C1, C3, C6, C7, CC-CF, D1, FD, FF): the
  streams and strings split those regions where they fall, but what is left has
  a reader and a census composition, not a parser.
- The scripts MD4 parses (object, sequence, window command lists) are still not
  `[[data]]` regions with a round trip, and the event, battle animation and
  enemy script formats aren't parsed at all.
- The strings' encoding isn't decoded: the regions are bounds, not glyphs. The
  control codes at their starts (`$14`-`$17` dominate) and the menu encoding in
  `game/menu/text.c` should agree.
- The 66 skipped stream-table entries and the 18 stream candidates no table
  names are unexplained.
- Nothing recompresses, so an `lz` region's round trip is "the decompressor
  consumes exactly these bytes", not "re-encoding gives them back" (MD3's
  encoder would close that).

### Shift test

`python3 -m tools.matching shift` builds a set of shifted ROMs: padding at the
start of each module (within its bank's trailing padding), plus a few
cross-bank moves of whole modules. For each: the pointer-shaped literal lint,
then `ct_dev --native off --recomp off --rom <shifted>` over the movies
compared with the original's framebuffer hashes and audio. Hand ports and
generated blocks are keyed to addresses, so shifted runs use the interpreter
only. The dynamic part runs movies and belongs in a manual or nightly job, not
every build.

### Closure

C1-C7 from section 1, as a build step over the source model: every indirect
site lists its target-set argument (a derived bound, a parsed data set, or a
hint marked complete); no coverage address outside the code set; no transfer
into data; reachability from the entries. Built in MD4, below.

### MD4 as built

Same commands as MD0 and MD1. The matching run now also runs the closure
checks (`tools/disasm/closure.py`), writes them to `report.md` and fails when
one fails (`--closure report` only reports them); the recompiler's generator
fails unless its blocks' instruction variants are the analysis's. See
DISASSEMBLY.md for the new hint fields, parsers and derivations.

| Path | What |
|---|---|
| `tools/disasm/objscript.py` | Object command scripts (`$C2:0F63`): the 83 commands read from their handlers, the eight scenes' streams, the parse |
| `tools/disasm/seqscript.py` | The attract program's sequence scripts (`$C3:0B03`): the command set, a parse per channel |
| `tools/disasm/derive.py` | Derivers: object scripts, sequence scripts, operands written in RAM code, window command lists; roots from constant arguments, with lists of the dynamic sources and why they're covered |
| `tools/disasm/codeargs.py` | Constant register values at an instruction over every path into it |
| `tools/disasm/closure.py` | Checks C1, C2, C3, C5, C6, C7 and the code set |
| `tools/disasm/analyze.py` | `derive` sites traced to a fixpoint in rounds, `PER` calls, header vectors, `patch` sections' entries, `follow = false` |
| `tools/recomp/blocks.py`, `__main__.py` | Entries at `PER` call returns and derived table targets; the code set check |
| `tests/test_md4.py` | Arguments, `PER` calls, derivations and their rounds, sequence scripts, closure checks |

**C3, data-driven dispatch.** The "only what runs in the movies" target lists
are gone; each is derived from the data, and every target the old list had is
among the derived ones:

| Site | Interpreter | Parsed | Old list | Derived |
|---|---|---|---|---:|
| `$C2:0553` object handler words | object scripts | 425 roots, 8452 commands: 231 in ROM (747 bytes: `$C2:2EC1`, `31A8`, `4A9B`, `4C8F`, `69F8`, `74AE`) and the rest in the eight scenes' `$7F:0400` images (27757 bytes, of which 26622 are reached) | 28 (12 static loads, 16 movie-only) | 35 |
| `$C2:1819` command `$34` JSR | object scripts | | 6 | 39 |
| `$C2:1836` command `$4E` JSL | object scripts | | 1 | 34 |
| `$C2:0F7A` command table | object scripts | 75 of the 83 commands are used; none above `$52` | layout | 75 |
| `$7E:37F6` sequence call through `$00F0` | sequence scripts | 301 roots (by channel), 1307 commands (4363 bytes) | 4 | 33 |
| `$7E:3998` task list `$0920` | sequence scripts and 17 constant adds | | 26 | 82 |
| `$7E:39CF` task list `$0940` | sequence scripts and 6 constant adds | | 11 | 40 |
| `$7E:3023`, `$7E:35FC` JSR operands | stores in the program | 12 stores | 3, 2 ("complete": the STX stores and `$7E:96FE` had been missed) | 5, 4 |
| `$7E:5E43` JSR operand | stores in the program | a long store of two bytes | (not known) | 2 |
| `$C2:ED50` window commands | window command lists | 56 lists, 400 commands, entries 0-4, 6, 7 of 11 | layout | 7 (bound confirmed) |

The parses found code MD1 had typed as data: object handlers with their state
tables in `$C2:4CC5-5627` (MD1 typed `$C2:4A9B-5627` as scripts), routines the
scenes call in `$C2:7BD1-7DE3`, `$C6:E000-E236`, `$C6:E7FA-E90E` and
`$C6:EE23-EE37`; the data hints are split around it, with 4 new jump table
hints. 48 of MD1's 131 `unreached` entries are now reached through derived
edges and removed. The other interpreters:

- **Event commands** (`$C0:5910`, 256 handlers): no command names code. The
  indirect sites reachable from the handlers are the tables and derivations of
  the modes events start (battle, menu, attract program); the tables an event
  operand indexes (`$C0:6307`, `6398`, `6433`, `633D`, `63D7`) are bounded by
  layout only. Parsing every location's event script would turn those bounds
  into data-derived ones; not done.
- **Battle animation scripts** (`$C1:4CD4`, table `$C1:7A6B`): the command byte
  is guarded (`$DB` and up branch away). The sub-dispatches on script operands
  (`$C1:75D1`, `$C1:7633`) are bounded by layout; not parsed.
- **Enemy scripts** (`$C1:874E` and three more, table `$C1:B80D`): the
  command byte from bank CC isn't checked; the 41-entry bound is layout. Not
  parsed.
- **Text control codes** (`$C2:EFD0`): guarded by the only caller (`CMP #$10`).

**C1, indirect transfers.** 147 sites (JMP/JSR `(abs,X)`, JMP `(abs)`, JML
`[abs]`, calls with written operands), every one with an argument: 11 derived,
38 guard (a mask, a compare, a loop bound), 96 layout (the table ends where
code or another table begins; the index isn't checked), 2 constant (`$C3:0F32`,
the menu threads at `$C2:8203`); none without. Both `PER`s are followed as
calls (`$C2:8289`, `$C2:EFCD`). 19 stack switches: 14 reset S to a constant, 3
restore an S a `TSX` saved, 2 are the thread scheduler's. 36 stores to the RAM
vector stubs: all in `writes` sections or the listed partial installers (whose
handlers are entries). The native NMI, IRQ and reset vectors come from the
header (`header_vectors`); BRK and COP aren't in the code set.

**C2, widths.** 0 undeclared length conflicts (1 declared), 0 calls on assumed
widths (two needed hints: `$C3:0EFA` returns like `$C3:0011`, which let
`$7E:9A0C` return), 0 pruned returns, 0 immediates decoded with an unknown
width. 1298 addresses are traced with more than one width pair in one context
(loop and join variants of one length); not a failure.

**C5, disjointness.** 0 transfers into data, 0 instruction bytes in data, 0
table entries into data.

**C6, reachability.** 123695 of 124664 instruction addresses are reachable from
the roots (vectors, hints, patches) through proven edges; the 969 others are
only from the 83 `unreached` entries, each with its comment.

**C7 and "coverage adds no address".** With the 23 coverage files, 0 seeds:
static analysis alone finds every executed ROM and RAM address (MD1 needed 66
seed contexts, 1899 addresses). The analysis has 127044 instructions without
coverage (MD1: 121760 without, 123716 with).

**Code set.** The recompiler's blocks hold exactly the analysis's 127178
instruction variants (address and widths, unknown widths as both; RAM code
compared by address and widths across alternative sections), 12412 blocks.

**Result** (this ROM, hints, the 23 coverage files and 22 call logs): CRC32
`2D206BF7` and SHA-1 `de5822f4...` match, 14 RAM sections assemble to their
images, all closure checks pass; the run takes about 10 s (analysis 5-7 s in
two derivation rounds, closure 0.6 s).

**Validation.** `unittest` passes (99 tests). The build with the coverage files
and call logs gives 12412 blocks (148491 instructions); the six main movies in
`--native shadow` report 0 mismatched generated blocks and no mismatching hand
port (`new-game` 44087249 block checks, `menu` 55489410, `field` 68908992,
`fair` 73751643, `fair-companion` 99024398, `gato-battle` 116449525), and
`--native on --recomp only --misses` reports 0 misses on `new-game` and
`gato-battle`. **Without any coverage file or call log** (static analysis and
hints only) the generator writes the same blocks, byte for byte (`table.c` is
identical), the matching source is the same, and the miss report is 0 on
`new-game` and `gato-battle` too: the movies need nothing static tracing
doesn't find.

**Left for MD4:**

- 96 tables are bounded by layout only: the table ends where code or another
  table begins, and nothing checks the index. The data that indexes some of
  them isn't parsed yet: event scripts (the operand tables `$C0:6307`, `6398`,
  `6433`, `633D`, `63D7`; all locations' scripts are compressed), enemy scripts
  (`$C1:B80D`, 41 entries, and the action tables `$C1:B85F`, `B88D`, `B8BB`),
  battle animation script operands (`$C1:75D1`, `$C1:7633`). The object
  handlers' state tables are indexed by state bytes the handlers set.
- `$C2:828E` (menu modes): modes 8-10 and 15 point at bytes that aren't handler
  tables; that no code sets them is assumed, not shown.
- `$7E:3998` dispatches through `$0950,X` in the image while the lists it runs
  and clears are `$0920`; nothing in the program fills `$0950`, so the derived
  set is complete either way, but the operand's value at run time isn't
  explained.
- C4 still rests on the enumerated sections and the miss report; the whole
  library's miss report wasn't rerun for this change (new-game and gato-battle
  were).
- The parsed scripts aren't typed regions with a round trip yet, except the
  object scripts in `$C2:4A9B-4CC4` (`format = "object"`); the scene streams in
  C3 and C6 are still inside `mixed` regions.
- asar is still unchecked.

## 4. How it plugs into the existing tools

- **`re/hints.toml`** grows data and pointer kinds, and the large maps move to
  `re/data/*.toml` and `re/layout.toml`:
  - `[[data]] start, end, type, name` with per-type parameters (`lz`, `brr`,
    `ptr24`, `ptr16 bank=`, `words`, `event_script`, `object_script`, `text
    table=`, `tiles bpp=`, `palette`, `spc700`, `ram_program base=`,
    `padding`); today's `[[data]]` (start, end) becomes the untyped case.
  - `[[pointer]] at, target` (an expression) for immediates and literal
    fields; `[[not_pointer]] at` for lint exceptions.
  - `[[ramcode]] base, source` for RAM sections.
  - Jump table and indirect hints gain `complete = true` with the argument, as
    against "observed in movies".
- **`tools/disasm`** gains data-bank tracking, bounds derived from index
  guards, and format parsers in the tracing fixpoint (script operands become
  entries). `census.py` reads the typed hints, so its unknown column is the
  progress meter for MD1 and MD2.
- **`tools/matching`**: `emit`, `asm` and the round trip in `__main__` exist
  (MD0); to come: typed extraction, `shift`, `lz` (decoder and encoder),
  `formats/`.
- **`tools/recomp`** takes its code set from the source model instead of
  `Analyzer.code`, and the build checks the two agree (at MD4, exactly). RAM
  sections are guarded blocks since M3 (RECOMP.md).
- **Runtime**: `ct_dev --misses` (section 1, built in M3).
- **Coverage movies** stop being seeds at MD4 and become checks: an executed
  address outside the closed code set is a closure failure, not new input.

## 5. Milestones

Relative effort is in rough units against MD0, from the measurements above.
No dates.

| | Goal | Done when | Main drivers | Effort | Status |
|---|---|---|---|---|---|
| MD0 | Naive split: toolchain and extraction | The build extracts, emits and assembles a ROM with CRC32 `2D206BF7` and the SHA-1 from APPROACH.md; code the analysis finds (238421 bytes) is instructions, everything else `incbin` from the build tree; the 10 length conflicts and overlaps are `db` with labels; assembler encodings are unit-tested for every opcode and width; with asar available, the asar build matches too; nothing ROM-derived is tracked by git | Assembler, emitter, extraction, CMake target | 1 | done, except the asar build: unchecked, asar wasn't available ([MD0 as built](#md0-as-built)) |
| MD1 | All code in source form | Every executed ROM address in the coverage files is in the source's code; 0 heuristic jump tables (108 at MD0), 0 assumed returns (26), pruned returns and the width conflict pinned by hints; RAM programs and stubs are code sections checked against their decompressed or copied images; each blob in the 13 banks with code has a data type or a reason; census shows no unknown bytes in C0-C2 (10.6 KiB at MD0) | Code discovery in 348 KiB of unknown bytes in C3, C6, C7, CC-CF, D1, FD and FF; derived table bounds; RAM sections; the SPC700 driver as a typed blob | 4-6 | done except the partial vector installers (the code C3:09E9 generates at `$7E:B574` became a section in recompiler M3); 565 KiB of the code banks are typed `mixed`, for MD2 ([MD1 as built](#md1-as-built)) |
| MD2 | All data typed | Census unknown is 0; every region has a parser that consumes it exactly and re-serializes it; all 1449 stream candidates confirmed or rejected, their contents typed; all scripts parse with command lengths | 1.19 MiB unknown, 2.20 MiB of streams with 3.76 MiB of contents, 204 KiB of samples, undocumented formats (dialog, battle and enemy scripts, sequences) | 10-15 | in progress: 36 resource pointer tables type 1371 streams, 4405 strings and 617 records; census unknown 877473 -> 194741 bytes (4.6%); left are the audio banks, `$D0`, 445 KiB still typed `mixed`, and the script formats ([MD2 as built](#md2-as-built-first-part)) |
| MD3 | All pointers symbolic | Lint finds no unexplained pointer-shaped literal; every shifted build's symbol fields move with their labels; shifted ROMs play every movie with identical frames and audio | Data-bank tracking; 1485 long, 23190 absolute and 6237 immediate operands to resolve or clear; 913 pointer runs and pointers inside typed data; LZ encoder; shift harness | 6-10 | |
| MD4 | Closure proven | C1-C6 pass in the build; the recompiler's code set equals the source's; `--misses` reports 0 on all 21 movies; coverage adds no address | 134 indirect sites with arguments; script-derived targets for 7 interpreters; RAM code enumeration | 3-5 | C1, C2, C3, C5, C6, C7 checks pass and fail the build; code set equal; static analysis alone reaches every executed address; left: layout-bounded tables (96) and the event, battle animation and enemy script parses that would bound some of them, C4 beyond the miss report ([MD4 as built](#md4-as-built)) |

**Order.** MD0 and MD1 first. For the goal that motivates this (a provably
complete code set for the recompiler), MD4's checks need only the data that
feeds control flow typed: scripts and handler tables with code addresses, and
the RAM programs. That subset of MD2 plus MD4 (about 5-8 units) could come
before typing graphics, maps and audio, and before MD3. MD2 and MD3 in full
turn "the code set is closed" into "the whole ROM is understood and
relocatable", which is what mods, a data-driven port and the independent
consistency check of the shift test need.

## 6. Repository and legal constraints

CLAUDE.md forbids committing ROM bytes or anything derived from them,
including disassembly listings with data bytes. The design keeps to that: the
source refers to data only through `incbin` of files extracted at build time
from the user's ROM, checked by CRC, as the recompiler does.

That isn't the whole question. A symbolic source without a single data byte
is still a transcription of the game's code: every opcode and operand, and so
every code byte, can be recovered from it. The same holds for `tools/recomp`
output, which RECOMP.md keeps in the build tree for that reason. There are two
ways to handle it:

**Decided: A.** Only tools and knowledge are committed, on the condition that
the full source can always be derived from them and the user's ROM.

- **A. Generate the source at build time.** Commit the tools, the
  format parsers and the knowledge in `re/`: names, types, bounds, pointer
  facts, layout and comments, keyed by address. The source is generated from
  the user's ROM into the build tree, like `disasm/out/` and the recompiler's
  shards, and the round trip, shift test and closure checks run there. This
  fits the current rules unchanged. The cost is that the source can't be
  browsed, diffed or reviewed on the host; review happens on the hints, and
  the knowledge base has to be expressive enough that nobody wants to edit
  generated files.
- **B. Commit the symbolic source** (as pret projects do), with data `incbin`ed
  from the user's ROM. It's readable, reviewable and editable directly, but it
  distributes the program's code in assembly form, contradicts the "anything
  derived" rule, and would need that rule changed deliberately.

Under A the knowledge base itself must not become a transcription: hints state
facts the tools can't infer (a bound, a width, a type), not per-instruction
contents, and comments should avoid quoting ROM bytes. A few existing comments
quoted short byte sequences (the `$C2:ED50` table note, the `$C3:994B`
operand); they were reworded, and new comments shouldn't quote bytes.

## Risks

- **Matching by construction.** Without format round trips and closure, a
  matching build proves little (section 1). The plan puts those checks in the
  same build step as the CRC.
- **Heuristic census.** The numbers size the work; they aren't classification.
  The `$C3:8000-CFFF` ambiguity shows one way they can be wrong.
- **Undocumented formats.** Dialog text, battle and enemy scripts and music
  sequences are the bulk of MD2 and the least known. Community documentation
  can guide the reverse engineering; the facts go into hints, not copied text.
- **RAM code.** C4 rests on enumerating RAM programs and on the miss report;
  code that writes and runs something no movie reaches could stay unseen.
  Patched stubs (`$7E:9690`'s bank operands) need operand fields in their
  sections.
- **Data bank ambiguity.** Absolute operands reached with different data
  banks can't be one symbol; each needs a hint, and the shift test is the
  check.
- **Hardware-fixed addresses.** Header, vectors, the reset stub and mirror
  access limit what can move; the shift test must respect them without
  exempting too much.
- **Build cost.** Extraction, emission and assembly of 4 MiB add seconds to a
  build with a ROM; shifted builds and their movie runs don't belong in every
  build.
- **Scope.** Disassembling the SPC700 driver or matching the original
  compressor is optional; neither is on the path to a closed 65816 code set.

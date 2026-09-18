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
[MD2 as built](#md2-as-built-first-part): 249341 bytes of code, 106951 unknown
(2.5%), 1381381 in typed `[[data]]` regions and 2357173 in streams.

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

Text regions are assembled from their decoded form: the extraction writes a
region of a text format as `data/text/<label>.txt`, and the `.bin` its `incbin`
names is what the format's encoder makes of that file as it reads back from the
build tree, so the CRC check is the round trip (MD2, "The string encodings").

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
| Dialog and menu text | Dialog decoder `$C2:57DF`/`$C2:5823` with its dictionary `$DE:FA00`, menu drawer `$C2:EF65`, name records; 21 string tables, codecs in `tools/disasm/text.py` (MD2, "The string encodings") | Menu strings outside `$C2:A3BA` (window command lists, computed addresses); the two tables left untyped |
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
| `tests/test_resources.py` | 22 tests on synthetic data (no ROM): each target kind, `skip`, repeated entries, `slots`, splitting, fill, bank crossing, idempotence |

**Resource pointer tables.** The loaders all have one shape: a resource id is
multiplied by three (or doubled) into X, two long indexed loads read the
pointer's low word and its bank byte, and the pair is either stored at
`$0300`/`$0302`, where the decompressor at `$C3:0002` takes its source, or kept
as a data pointer. 38 tables are declared with the reading code in their
comments. They give, with 0 problems:

| Kind | Tables | Regions | Bytes |
|---|--:|--:|--:|
| Streams (`lz`), consumed exactly by `tools/disasm/lz.py` | 19 | 1371 | 2354164 (4007103 out) |
| Strings (`text`), each to its zero terminator | 15 | 4405 | 170097 |
| Sound blocks (`sized`), a 16-bit length then the data | 2 | 145 | 294487 |
| Blobs (`blob`), sized by the next entry or by the hint | 2 | 617 | 279808 |
| The tables themselves (`ptr24`, `ptr16`) | 38 | 38 | 7702 |

- **Streams.** 6790 entries over the 38 tables (66 skipped, each with a comment,
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
- **Sound blocks.** `$C7:0AEA` (62 entries) and `$C7:0D18` (83, the songs, its
  count the byte at `$C7:0AE9` the dispatch compares) name length-prefixed
  records the driver uploads to the SPC700: `sound_driver_upload_block`
  (`$C7:0655`, ported in `game/core/sound_driver.c`) reads a 16-bit length and
  sends that many bytes. The 62 blocks run end to end from `$C7:730D` to
  `$CA:8ACB` (202687 bytes) and the 83 songs from `$CA:8ACC` to `$CB:F163`
  (91800), which is the whole of banks C8, C9 and CA and most of CB, including
  everything the census's BRR rule used to claim there. Three blocks run into
  the next bank; each becomes one region per bank. The five side tables around
  them (`$C7:0BA4`, `$C7:0C20`, `$C7:0C9C`, one word per block; `$C7:0E11`, 16
  block ids per song; `$C7:1871`, 12 three-byte entries) are typed by hand, and
  the MD1 `mixed` hint over `$C7:0AE9-FFFF` carries `fill = true` so they and
  the blocks carve it up.
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
| Unknown | 877473 | 106951 | 20.9% -> 2.5% of the ROM |
| Typed `[[data]]` | 572058 | 1381381 | |
| Compressed | 2198893 | 2357173 | 1251 by shape -> 1371 by pointer, 18 by shape |
| BRR-like | 169506 | 2232 | the samples are inside the driver's typed blocks |
| Padding | 96893 | 85790 | fill inside a typed region counts as its type |
| Pointer-like runs | 26514 | 7810 | |

Unknown bytes per bank, before and after: `$C8` 5943 -> 0, `$C9` 120 -> 0,
`$CA` 27209 -> 0, `$CB` 57769 -> 3251, `$D0` 54359 (unchanged), `$D8` 11690 ->
17, `$DE` 61669 -> 882, `$E1` 5601 -> 249, `$E4` 46325 -> 9925, `$E5` 6509 ->
6018, `$EF` 6902 -> 3704, `$F5` 4064 (unchanged), `$F6` 37002 -> 20507, `$F7`
59363 -> 135, `$F8` 24736 -> 755, `$F9` 19414 -> 1456, `$FC` 15409 -> 64,
`$FE` 1294 -> 1247, the rest small. Banks C8, C9, CA, DC, DD and E3 went from
wholly or mostly unknown to wholly typed.

6787 typed regions in all: 1371 `lz`, 4406 `text`, 691 `bytes`, 149 `spc700`,
38 `mixed` (388374 bytes), 33 `ptr16`, 20 `ptr24`, 39 `words`, 8 `gfx`
(219431), 14 `script`, 11 `undetermined`, and the header, padding and RAM-code
regions.

**Validation.** `unittest` passes (121 tests). CRC32 `2D206BF7` and SHA-1
`de5822f4...` still match, every `[[data]]` region fits its bytes, the closure
checks C1, C2, C3, C5, C6 and C7 pass, the 15 RAM sections assemble to their
images, and the six main movies in `--native shadow` report 0 mismatched
generated blocks and no mismatching hand port.

**MD2 data-typing follow-up.** The following regions now have reader-backed
formats in `tools/disasm/dataformats.py`, exercised by the matching data check:

- `$D0:0000-DFFF` is an eight-byte-aligned graphics pool. The previously
  missed reader is `$CD:1509`: it masks a descriptor to 13 bits and scales it
  by eight. `$CD:1488` reads two interleaved planes; `$CD:1525-15CB` optionally
  reads a third plane and synthesizes a fourth. The `plane-pool` codec
  round-trips storage units as rows of bits. It does not yet enumerate tile
  descriptors or prove which offsets are used by every map.
- `$CF:0000-E2FF`, previously `mixed`, is 2bpp graphics read by `$CD:12B8-12F6`.
  Its codec decodes MSB-left pixels and re-encodes interleaved SNES planes.
- `$E5:0000-1A43` contains 513 bank-relative offsets and 814 seven-byte exits
  for 512 locations. `$C0:A695-A70D` supplies the count, adjacent-offset bounds
  and stride. Parsing rejects descending offsets, partial records, a wrong
  first offset or a sentinel that does not consume the region; serialization
  rebuilds offsets from the record lists, including empty lists. The rest of
  `$E5:0000-1FFF` is checked padding.
- `$E4:F000-F522` contains 263 five-byte sprite records: the second reader
  at `$C0:4595` uses base `$F023`, seven records beyond the first reader's
  `$F000`, with an eight-bit id. `$E4:F600-FFD7` contains 252 ten-byte battle
  sprite records; this count remains **layout-bounded**, not a proven bound
  on script ids. Both have field parsers and serializers. The intervening
  padding and `$E4:FFE0-FFED` bank-relative resource bases are separate regions.

Without coverage, the census unknown count falls **106951 -> 43481 bytes**;
`mixed` falls **388374 -> 330262 bytes**. Newly declared region bytes and
unknown-byte reductions differ because the census previously guessed padding,
BRR and pointer-like runs inside these regions.

Validation: 159 Python tests pass; matching reproduces CRC32 `2D206BF7` and
SHA-1 `de5822f4f2f7a55acb8926d4c0eaa63d5d989312`, with no data or resource
problems and all 15 RAM sections matching. With identical generator flags
(no coverage/calls), main at `74d0992` and the follow-up branch both generate
12419 blocks, 148499 instructions, 127045 analyzed instructions and 127181
instruction variants. All 65 generated C files are byte-identical. This is
static validation; the full shadow/trace/misses movie suite remains an
integration gate and was not rerun for this follow-up.

**Left for MD2:**

- 42590 census-unknown bytes (43481 before the string work below), chiefly
  `$F6:24B1-7FFF`; that range still lacks
  a reader-backed format. Other leftovers include `$F6:1C00-1DFF`,
  `$E4:0000-1FFF`, `$E4:2A00-2FFF`, `$E4:CC57-EFFF`, bank D0 outside the
  graphics pool and known streams, `$CB:F164-FFFF`, `$EF:B168-BFFF`,
  `$F5:EE02-FFFF`, and smaller pieces. Do not infer types from the CRC alone.
- 228.3 KiB is still typed `mixed` (C0, C1, C3, C6, C7, CC-CE, D1, FD, FF;
  322.5 KiB before the string work and 311.9 before the battle scripts below):
  the streams, strings, sound blocks and scripts split those regions where they
  fall,
  but what is left has a reader and a census composition, not a parser.
- The scripts MD4 parses (object, sequence, window command lists) are still not
  `[[data]]` regions with a round trip, and the event script format isn't
  parsed at all (the battle animation and enemy formats are, below).
- The 66 skipped stream-table entries and the 18 stream candidates no table
  names are unexplained.
- Nothing recompresses, so an `lz` region's round trip is "the decompressor
  consumes exactly these bytes", not "re-encoding gives them back" (MD3's
  encoder would close that).

### MD2 as built: the string encodings

Same commands. The game's strings are now decoded to text and assembled back
from it: the matching build writes every region of a text format decoded
(Build-time extraction, above) and fails unless the bytes the text encodes to
reassemble the ROM.

| Path | What |
|---|---|
| `tools/disasm/text.py` | the codecs: the glyph set, the control-code tables with their operands, `dialog`, `dialog-dictionary`, `menu`, `names` |
| `tools/disasm/resources.py` | `format` on a `text` target: where a string ends, and the format its regions carry |
| `tools/disasm/dataformats.py` | `check_format` decodes and re-encodes a text region, with the dictionary as context |
| `tools/matching/emit.py`, `__main__.py` | text regions written as `.txt`, their `.bin` encoded from the file read back |
| `tests/test_text.py` | 19 tests on synthetic data: every control code of both encodings, the dictionary, names, the resource and emitter paths |

Two readers interpret strings, the dialog decoder and the menu drawer; the
decoder's dictionary and the name records both draw from are shapes of their
own, so there are four formats. Nothing in them comes from outside
documentation; the tables in `text.py` are what the code at each address does
with a byte.

**Dialog** (`dialog`). `$C2:57DF` (entered through `$C2:0003`) takes a table
address from `$020D-$020F` and an 8-bit index from `$020C`, loads the string's
address from `[$020D],Y` with Y twice the index, and `$C2:5823` (through
`$C2:0009`) draws from it a few glyphs per call. The field dialog sets it up at
`$C0:20F2` from `$2A-$2D`, which the event commands in `game/event/dialog.c`
fill; that is why no instruction holds a long operand into the text banks. A
string byte (`$C2:58B2`) is:

| Byte | Meaning | Handler |
|---|---|---|
| `$00` | end of the string | `$C2:594F` |
| `$01 xx`, `$02 xx` | a 16-bit glyph code `$01xx`/`$02xx` | `$C2:5B06` |
| `$03 xx` | a delay: `$C0:21A8` waits `xx` x 15 frames; `xx = 0` goes to the exit the terminator takes (`$C0:21D8`), so `$03 $00` ends a field dialog (`[end]`) | `$C2:5945` |
| `$04-$0C` | state `$0215` = the byte, no operand. `$C2:5851` starts the next line at x 8 (odd) or 20 (even); `$C0:2126` clears the box first for `$09-$0C`, waits for a key for `$07`, `$08`, `$0B`, `$0C` and when the box is full for `$05-$08`. `$04`, the decoder's starting state, draws nothing | `$C2:594F` |
| `$0D`, `$0E`, `$0F` | an 8-, 16- or 24-bit number, from the argument pointer `$023D`, not the string | `$C2:5953`, `$C2:5985`, `$C2:59C8` |
| `$10` | a bare RTS | `$C2:5B14` |
| `$11` | a character's name, the index from the argument pointer | `$C2:5A26` |
| `$12 xx` | `xx` 0 or 1: an 11-byte name record from `$CC:15C4` or `$CC:6500`, the index from the argument pointer; `xx` of 8 and up: dictionary entry `xx + $17` | `$C2:5A4C` |
| `$13-$19` | the seven characters' names (`$7E:2C23 + 6n`, through `$C2:5FD8`) | `$C2:5B15` |
| `$1A` | the name at `$7E:2C23` again, through its own handler | `$C2:5B3B` |
| `$1B-$1D` | the name of the party member in slot n (`$7E:2980`) | `$C2:5B70` |
| `$1E` | the five glyphs at `$C2:6146` | `$C2:5B56` |
| `$1F` | the name of the item whose id is at `$7F:0200` (`$CC:0B5E + 11n`, after its icon) | `$C2:5B9B` |
| `$20` | the name at `$7E:2C4D`, slot 7 of `$C2:5FD8`'s layout | `$C2:5BD6` |
| `$21-$9F` | dictionary entry `byte - $21` | `$C2:58D0` |
| `$A0-$FF` | a glyph | `$C2:58BE` |

A glyph code indexes the 12x12 font at `$FF:2060` (24 bytes a glyph; the
right four columns from `$FF:3860`), its width from `$C2:60E6` (`$C2:5DC4`).
The characters for `$A0-$FF` were read off that font: capitals, small letters,
digits (the `$D4-$DD` that `$C2:5C3E` and the menu's `$C2:F20B` add to a
digit), then 22 punctuation, symbol and space glyphs (`$DE-$F3`). `$EF` is the
dialog's space and `$FF` the menus' blank tile (written `_`); `$F4-$FE` are
blank and unused, so they keep hex escapes.

**The dictionary** (`dialog-dictionary`, `$DE:FA00-FD00`). `$C2:58DD` reads
entry n's 16-bit pointer at `$DE:FA00 + 2n`, bank `$DE` from the immediate at
`$C2:58E8`; the entry is a length byte and that many glyph bytes, which
`$C2:5BF5` draws (a `$01`/`$02` there also takes the next byte). The table has
128 pointers, the entries start right after them at `$DE:FB00` and run end to
end through `$DE:FD00`, 2 to 11 bytes each, not in index order; the parser
requires exactly that and re-derives the pointers from the storage order.
Entry `$7F` has no byte of its own: only `$12 $68` reaches it. The 128
expansions are distinct.

The menus use the same decoder for location names, item descriptions and help
text: their callers set `$020C-$020F` themselves and step it from `$C2:F905` or
`$C2:F90C`, which carry on past a `$05` (moving down four rows, `$C2:F92F`) and
stop at any other code, so the same format, and the same string ends, serve
them.

**Menu strings** (`menu`). The drawer `$C2:EF65`/`$C2:EF7E` (ported in
`game/menu/text.c`): `$00` ends a string, as does the tile limit in `$87`;
`$10-$FF` is a tile, written with the attribute in `$7E` (`$C2:EF97`); `$01-$0F`
index the handler table at `$C2:EFD4`: `$01` new line, `$02` a position word,
`$03` a number (address and format words, `$C2:F114`), `$04` a substring at a
long address, `$05` a name from `$7E` (word), `$06` palette bits from `$7E`
(word), `$07` a tile word from `$7E` (word), `$08` a count of blanks (byte),
`$09` `$88` (byte), `$0A` the attribute (byte), `$0B` a number through
`$C2:F227` (two words), `$0C` a limited number (byte, word); `$0D-$0F` point at
the handlers' RTS. With `width`, a region is fixed records drawn with that
limit, each ending at the limit or at a terminator that fills it out.

**Name records** (`names`). 11 bytes each, glyphs padded with `$EF` and, for
items and some techniques, an icon tile below `$A0` in front. The menus draw
them with a limit of 11 tiles (`LDA #$CC0B` at `$C2:B05E` and `$C2:BDE3`); the
dialog's name codes skip the icon and stop at the first `$EF` (`$C2:5D23`,
`$C2:5D34`). The text drops the trailing padding and the encoder restores it.

**Readable form.** A glyph with a character is that character and any other
byte `[$XX]` (a 16-bit glyph `[$01XX]`); a control code is `[name]` or
`[name $op ...]`; a dictionary byte is its expansion in braces, `{...}`, which
names the entry because the expansions are distinct. Line and page codes are
followed by a newline the encoder ignores. The files are written to the build
tree only; the repository has the codecs, not the text.

**The regions.**

| Format | Tables | Regions | Bytes |
|---|--:|--:|--:|
| `dialog` | 21 | 4697 | 173651 |
| `dialog-dictionary` | | 1 | 769 |
| `menu` | | 1 | 40 |
| `names` | | 3 (610 records) | 6710 |

- The fifteen MD2 tables are `dialog`, plus a sixteenth at `$DE:FF00` (three
  strings), which `$C0:20FD` loads instead of `$2B-$2D` while bit 5 of `$54` is
  set. `format = "dialog"` ends a string at `$03 $00` as well as at `$00`: in
  the fifteen, 64 strings end that way, and 62 of them are followed directly by the string the
  next entry names, which is how the tables were laid out. Because the operand
  is the first zero, the bounds are the ones MD2's zero scan found.
- The menus' tables are `$C6:F400` (112 entries, strings before it from
  `$C6:F000`; `$C2:69A0`), `$CC:2EB1` (242, one per item; `$C2:F317`),
  `$FF:CF3B` (7), `$FF:D03E` (27) and `$FF:D396` (8): 283 more strings, 3337
  bytes. All but the first are self-describing like MD2's; none has an end
  sentinel, and none of their strings holds a `$03`.
- MD2's 58 bytes between strings, and a 59th in `$CC:2EB1`, are typed by hand:
  three fragments of text before an entry's start that read on into it
  (`terminated = false`), and two strings after a string ending in `$03 $00` in
  `$F8`. The string after `$DE:E300`'s sentinel, at `$DE:F917`, is typed too.
- Names: `$CC:0B5E` (242 records, the items; the next table starts right after
  them), `$CC:15C4` (117, read at `$C2:BDD9` and by `$12 $00`) and `$CC:6500`
  (251, by `$12 $01`); the counts are layout bounds, the next record not
  reading as glyphs. `$CC:0000-DFFF` became `fill = true` so the hand regions
  carve it.
- Menu strings: only `$C2:A3BA` (four 10-tile records, `$C2:A39D`, index x 10),
  retyped from `script`.

Codes no typed dialog string uses: `$01`, `$04`, `$07`, `$08`, `$0F`, `$10`,
`$11`, `$12`. In the fifteen MD2 tables the most common are `$06` (3821 times)
and `$05` (1535).

**Result.** 4702 regions, 181170 bytes, are assembled from text, all of them
round-tripping; none of a text format is kept raw. Census unknown 43481 ->
42590 bytes (`$DE` 882 -> 19); `mixed` 330262 -> 319422 bytes (322.5 -> 311.9
KiB). The generator's input is unchanged: `python3 -m tools.recomp` without
coverage gives 12431 blocks, 148301 instructions, 127045 in the analysis and a
code set of 127181 instruction variants on `main` and on this work, and the 65
generated C files are byte-identical. `unittest` passes (178 tests); CRC32
`2D206BF7` and SHA-1 `de5822f4...` match; the matching run writes 4702 more
files (a `.txt` beside each encoded `.bin`), which costs a few seconds. With the
build's coverage and call logs the generated C is identical to `main`'s too, and
`tools/suite.py --only core` passes (7 of 7 movies clean, trace identity on
`new-game`).

**Left for the strings:**

- `$FF:D3FE` is loaded by the same decoder (`$C2:D2E0`, index + `$0E`, and
  `$C2:F958`), but its first entry points an odd number of bytes past it and
  entries point into each other's strings, so its extent isn't the table's.
- `$CC:3A09` (`$C2:BE63`): 120 entries tile their strings and the 121st is
  their sentinel, but entry 0 points 18 bytes further, over 9 words that don't
  read as pointers into the strings. Which bound is right is open.
- Menu strings passed to `$C2:EF65` by address in code (window command lists,
  `$C2:8385`, and computed addresses) aren't enumerated: the drawer has 10
  `JSR` sites in C2 besides the lists, and only `$C2:A3BA` is a region.
- The names of `$05-$0C` describe what the field caller `$C0:2126` does with
  them; the menus' loop stops at all of them but `$05`. `$04`, `$10` and the
  bytes with no handler keep hex escapes rather than names.

### MD2 as built: the battle script formats

Same commands. The battle's two script formats are decoded and assembled back
from the decoded form the way the strings are: the matching build writes each
region decoded and fails unless the bytes its text encodes to reassemble the
ROM.

| Path | What |
|---|---|
| `tools/disasm/battlescript.py` | both codecs: the animation command table read from its 115 handlers, the enemy condition and action tables, the parses from every root, the decoders and encoders |
| `tools/disasm/dataformats.py` | `check_format` decodes and re-encodes a `script` region of either format, with the animation headers as context |
| `tools/matching/emit.py` | the two formats written as `.txt` and encoded back, beside the text formats |
| `tests/test_battlescript.py` | 17 tests on synthetic data: every animation command's length and round trip, `$80`'s count, headers, the call table, labels, bytes at a boundary the parse disagrees with, the call-return rule, every condition and action, a block that ends at `$FF`, a script that starts inside a section, the evaluators' reading of a script, the length table check, the `check_format` path |

Nothing comes from outside documentation: every length is what a handler
passes on, every root a table a loader reads. Neither format is compressed:
the scripts sit in banks CC, CD and CE as the interpreters read them, so the
round trip is the whole check and `lz` re-encoding doesn't come into it.

**Animation scripts.** `$C1:4BBE` runs up to sixteen channels a frame
(`game/battle/scripts.c`); channel Y's 24-bit script pointer is
`$5DBD + 4Y`. `$C1:4C9E` fetches a command byte, stops for the frame on `$DB`
and up without moving (`$C1:4CC9`), and calls the handler from the word table
`$C1:7A6B` (`JSR ($7A6B,X)` at `$C1:4CD4`). A handler moves the pointer on by
the A it passes to `$C1:75BB`, or not at all to run again next frame, so a
command's length is what its handler passes on every path; the waiting
handlers keep it in `$8E` and pass it when their wait is over, and `$80` passes
its first operand's low nibble plus one. 149 of the 219 table entries have
handlers of their own (`$50-$55` is `game/battle/numbers.c`); the other 70
share `$C1:4CF7`, which leaves the channel on the command for good (counting
`$9876` up each frame). `$DA` reads a byte after it but passes 1, so that
byte is also the next command.

Five loaders start channels (`$C1:4401`, `$C1:450A`, `$C1:45EC`, `$C1:46C6`,
`$C1:47A4`), each through a table of header offsets in bank CD (`$CD:5EC6`,
21 words; `$CD:5EF0`, 128; `$CD:5FF0`, 256; `$CD:61F0`, 256; `$CD:63F0`, 96,
counts to the next table) whose headers and scripts are in bank CE, CE, CD,
CD and CE. A header is two channel masks, big-endian (bit 15 is channel 0),
then a pointer in its bank for each set bit of the first mask and then of the
second: with `$A0B3` (bit 6 of `$AE94`, `$C1:4099`) clear the first mask's
channels start and the second's pointers are never read; set, the first's
are skipped. Command `$1E` calls through the word table at `$CE:0000` (bank CE
whichever bank the caller is in; 78 words, then `$FFFF` to the 128th an 8-bit
index reaches), keeping the return at `$A569 + 4Y`; `$1F` returns and `$21`
returns when `$5D9F` is zero. `$00` stops the channel, and `$25` does when
`$A018 +` its operand is negative.

The parse starts from every header of the five tables and every entry of the
call table. 239 table entries give no header: 162 are `$FFFF`, and the unused
ids at the end of `$CD:5FF0` (15) and `$CD:61F0` (62) repeat a word that points
into script bytes and the table after the scripts; the parser names both. A
call continues after itself only when its subroutine can return, which the
parse settles by iterating over the subroutines (all 78 can). It reaches 464
headers, 2434 scripts and 32047 commands; 127 command bytes are used, `$01`
the only one of `$4CF7`'s, and nothing at `$DB` or above.

**Enemy scripts.** `$CC:8B08` holds a word per enemy id (`$C1:AFD6`,
`$C1:B48C`), and the scripts follow the table in bank CC. A script is two
sections, each ending with `$FF`: the attack section, which `$C1:8C09`
evaluates on each enemy's turn, and the reaction section, which `$C1:B488`
finds by scanning for the first `$FF` and `$C1:8461` evaluates when the party
acts (`game/battle/main_loop.c`). A section is blocks: conditions of four
bytes, the first dispatched through `$C1:B80D` (`$C1:B006`, `$C1:874E`), `$FE`,
then actions, `$FE`. The evaluators read the first condition and, unless byte
4 is `$FE`, the second (`$C1:B021`, `$C1:8769`); a failing block is skipped by
scanning for `$FE` twice (`$C1:B054`, `$C1:B066`), and a block whose only
condition is `$00` always passes (`$C1:8EA7` sets no failure in `$AF24`). An
action is a byte below `$80` (`$C1:8CE3` stops at any other), dispatched
through `$C1:B85F` when chosen and `$C1:B88D` when run; its length is the
byte at `$FD:BA4A` plus the action (`$C1:8E68`), 23 entries. `$04` is `$FF`
there but one byte in the data: `$C1:98C5` steps over it to one of the four
actions after it at random. `$03` (0) and `$05` (`$FF`) have no encoding;
nothing uses them. Byte 1 of 14 conditions and byte 2 of 7 is a target
selector, dispatched through `$C1:B8BB` from `$C1:AC14`.

The sections run end to end from the pointer table to `$CC:CBC9`, where
another table begins: 514 of them, the grammar accepting every byte. One
block's actions end at the `$FF` closing its section instead of their own
`$FE`, which the dispatcher allows. One enemy's script starts at a block in
the middle of another's section, so it shares that section's tail. The four
sections before the first pointer's target are no enemy's.

**Readable form.** One line per header (`header $8000 $0000 L_CD6E14`), call
table row (`calls`), command (`name $op ...`), condition (`if_name $a $b $c`)
or action (`name $op ...`); `then` and `end` are the `$FE` after a block's
conditions and after its actions, `done` the `$FF` after a section, and
`scripts` the enemy pointer table. A pointer that lands in the region is a
label named by its address, which the encoder checks; one outside is a word.
Names say what the handler does where that was read (`move_place`,
`wait_var`, `call`, `if_hp_half`, `attack`); the rest keep their number
(`cmd_6C`, `if_0A`, `act_07`), and an opcode sharing another's handler takes
the name with its own number (`halt_4B`). Animation bytes the parse doesn't
reach decode as commands too, when they fit before the next header or command
the parse starts; bytes that don't are `db`. Files go to the build tree only.

**The regions.**

| Format | Region | Bytes |
|---|---|--:|
| `battle-animation` | `$CD:6E0C-EE22`: the headers and scripts of `$CD:5FF0` and `$CD:61F0` | 32791 |
| `battle-animation` | `$CE:0000-009B`: the call table | 156 |
| `battle-animation` | `$CE:0380-886F`: the headers and scripts of the other three | 34032 |
| `battle-animation` | `$CE:D428-DC19`: the subroutines the call table names | 2034 |
| `enemy-ai` | `$CC:8B08-CBC8`: the pointer table and the sections | 16577 |

`$CE:009C-00FF` is padding. The CD and CE `mixed` regions became `fill = true`
so these carve them. 32602 animation commands are decoded, 555 of them in
bytes no root reaches (script tails after a stop), and none of the five
regions has a `db` line. In one place the parse and the layout overlap: a
script whose last call returns runs on into the header stored after it, whose
first four bytes are also that script's last two commands. The region writes
the header, so the second of those is the one command start the parse reaches
that the text doesn't show.

**Result.** 5 regions, 85590 bytes, are assembled from their decoded form,
all round-tripping. `mixed` 319422 -> 233732 bytes (311.9 -> 228.3 KiB); the
census's unknown count doesn't move (42590), since these bytes were typed
`mixed` already. The generator's input is unchanged: `python3 -m
tools.recomp` gives 12431 blocks, 148301 instructions, 127045 in the analysis
and a code set of 127181 variants on `main` and on this work, without and with
the coverage and call logs, and the 65 generated C files are byte-identical.
`unittest` passes (206 tests); CRC32 `2D206BF7` and SHA-1 `de5822f4...`
match, all closure checks pass.

**Left for the battle scripts:**

- Names: 56 animation handlers, 32 conditions and 12 actions keep their
  numbers; what they do is in their handlers, not read closely enough yet to
  name.
- `$C1:B8BB` (57 target selectors, dispatched from condition and action
  operands through `$C1:AC14`/`$C1:AC2C`) and the action tables `$C1:B85F` and
  `$C1:B88D` (23, guarded by the sign only) could be derived from the parse
  too: the actions used are known, the offset of each target operand isn't
  pinned down for every condition and action.
- `$C1:7A6B`'s bound stays the guard (219); the data uses 127 bytes of it.
- Whether `$A0B3` is ever set for a header whose second mask is empty isn't
  shown; the parse follows both masks' pointers either way.

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
| MD2 | All data typed | Census unknown is 0; every region has a parser that consumes it exactly and re-serializes it; all 1449 stream candidates confirmed or rejected, their contents typed; all scripts parse with command lengths | 1.19 MiB unknown, 2.20 MiB of streams with 3.76 MiB of contents, 204 KiB of samples, undocumented formats (dialog, battle and enemy scripts, sequences) | 10-15 | in progress: 38 resource pointer tables type 1371 streams, 4405 strings, 145 sound blocks and 617 records; census unknown 877473 -> 106951 bytes (2.5%); left are `$D0`, `$F6:24B1-7FFF`, 379 KiB still typed `mixed`, and the script formats ([MD2 as built](#md2-as-built-first-part)) |
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
- **Undocumented formats.** Battle and enemy scripts and music sequences are
  the bulk of what MD2 has left and the least known (the dialog text is decoded:
  "The string encodings"). Community documentation
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

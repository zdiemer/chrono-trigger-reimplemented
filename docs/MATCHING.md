# Matching disassembly (scope)

This is a scoping document: what a byte-identical, reassemblable disassembly of
Chrono Trigger would take in this project, what it would prove, and how it
would plug into the disassembler, the static recompiler and the runtime. None
of it is built yet except the measuring tool, `tools/disasm/census.py`.

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
     enumerated RAM section: the programs decompressed to `$7E:3000` (the
     attract, jet bike race and game over copies, which differ in 26 bytes),
     the `MVN`/`RTL` stub at `$7E:9690` that code in C2 and FF writes and
     patches, the JML stubs at `$00:0500`. This is the weakest check: proving statically
     that no other executed RAM bytes exist needs a model of every write to an
     executed range. The plan settles for enumerated sources plus the miss
     report.
   - **C5, disjointness.** No proven transfer targets a data region, and no
     code byte lies in one. Bytes that are both code and read as data are
     annotated, not an error.
   - **C6, reachability.** Every code region is reachable from the entries
     through proven edges, or annotated as unreachable with a reason.
     Unreachable code stays in the source as code.
   - **C7, empirical.** The miss report is zero over the movie library.

### The miss report

Today `ct_dev --insn-stats` counts instructions interpreted in `on` mode and
how many of those were in RAM (RECOMP.md: 84.7-97.4% generated per movie, the
rest mostly RAM code). RECOMP.md's M3 turns that into a list of interpreted ROM
addresses. The matching work defines it as `ct_dev --misses FILE`: every
instruction interpreted at a ROM address with no hand port or generated entry,
and every one in RAM whose surrounding bytes don't hash to an enumerated RAM
section (the check `m_code_matches` already does for guarded ports). Zero
misses on every movie is necessary but not sufficient: it only covers the paths
the movies take. C1-C6 are the sufficient part; the miss report catches a
wrong argument in them.

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

**Recommendation:** generate one closed, explicit dialect (every operand size
written out, no inference, no macros) that is a subset of asar syntax, and
assemble it with a small stdlib Python assembler in `tools/matching/` as the
build's round trip. Where asar is installed, the same source assembled with
asar must also match; that guards against cancelling bugs and gives anyone who
wants to edit the source a standard tool. ca65/ld65 is the alternative if
relocatable objects turn out to matter more than a dependency-free build.

### Source layout and naming

- The source is generated (see section 6): `build/<cfg>/matching/src/`.
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
  the entry's precondition.
- Generated parts: instructions, labels, region directives, `incbin` lines.
  Hand-curated parts are all in `re/`: names, types, pointer facts, layout,
  comments. Hint comments carry over as source comments.

### Build-time extraction

`python3 -m tools.matching extract` checks the ROM's CRC32 and writes every
data region to the build tree (`matching/data/<type>/<label>.<ext>`): raw
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

`python3 -m tools.matching check` (and a CMake target when `CT_ROM` exists,
like `cmake/recomp.cmake`): extract, emit, assemble, compare CRC32 and SHA-1.
On a mismatch it prints the first differing file offset, its region and type,
and the source file and line, never the bytes. Every format parser also
re-serializes its region in the same run.

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
into data; reachability from the entries.

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
- **`tools/matching`** (new): `extract`, `emit`, `asm`, `check`, `shift`,
  `lz` (decoder and encoder), `formats/`.
- **`tools/recomp`** takes its code set from the source model instead of
  `Analyzer.code`, and the build checks the two agree (at MD4, exactly). RAM
  sections become guarded blocks, the option RECOMP.md leaves open.
- **Runtime**: `ct_dev --misses` (section 1).
- **Coverage movies** stop being seeds at MD4 and become checks: an executed
  address outside the closed code set is a closure failure, not new input.

## 5. Milestones

Relative effort is in rough units against MD0, from the measurements above.
No dates.

| | Goal | Done when | Main drivers | Effort |
|---|---|---|---|---|
| MD0 | Naive split: toolchain and extraction | The build extracts, emits and assembles a ROM with CRC32 `2D206BF7` and the SHA-1 from APPROACH.md; code the analysis finds (238421 bytes) is instructions, everything else `incbin` from the build tree; the 10 length conflicts and overlaps are `db` with labels; assembler encodings are unit-tested for every opcode and width; with asar available, the asar build matches too; nothing ROM-derived is tracked by git | Assembler, emitter, extraction, CMake target | 1 |
| MD1 | All code in source form | Every executed ROM address in the coverage files is in the source's code; 0 heuristic jump tables (108 now), 0 assumed returns (26), pruned returns and the width conflict pinned by hints; RAM programs and stubs are code sections checked against their decompressed or copied images; each blob in the 13 banks with code has a data type or a reason; census shows no unknown bytes in C0-C2 (10.6 KiB now) | Code discovery in 348 KiB of unknown bytes in C3, C6, C7, CC-CF, D1, FD and FF; derived table bounds; RAM sections; the SPC700 driver as a typed blob | 4-6 |
| MD2 | All data typed | Census unknown is 0; every region has a parser that consumes it exactly and re-serializes it; all 1449 stream candidates confirmed or rejected, their contents typed; all scripts parse with command lengths | 1.19 MiB unknown, 2.20 MiB of streams with 3.76 MiB of contents, 204 KiB of samples, undocumented formats (dialog, battle and enemy scripts, sequences) | 10-15 |
| MD3 | All pointers symbolic | Lint finds no unexplained pointer-shaped literal; every shifted build's symbol fields move with their labels; shifted ROMs play every movie with identical frames and audio | Data-bank tracking; 1485 long, 23190 absolute and 6237 immediate operands to resolve or clear; 913 pointer runs and pointers inside typed data; LZ encoder; shift harness | 6-10 |
| MD4 | Closure proven | C1-C6 pass in the build; the recompiler's code set equals the source's; `--misses` reports 0 on all 21 movies; coverage adds no address | 134 indirect sites with arguments; script-derived targets for 7 interpreters; RAM code enumeration | 3-5 |

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

- **A. Generate the source at build time (proposed).** Commit the tools, the
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
quote short byte sequences (the `$C2:ED50` table note, the `$C3:994B` operand);
that should stay rare.

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

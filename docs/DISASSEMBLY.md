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

`tools/disasm/liveness.py` computes, for every function, which of A's low
byte, A's high byte, X, Y, the N/V/Z/C flags, DB and D any caller may read
after it returns. It's an interprocedural backward dataflow over the traced
code, solved as a function of what the callers read: each function has a
summary (what it reads whatever its callers do, and what it carries through
to its return for each register they might read), a call applies the callee's
summary to what is live after the call, returns take the union of what's live
at every call site, and unknown control flow keeps everything live. A push
reads the register it saves only as far as the pull that restores it is
followed by a read: `PHP`/`PLP` and the other push/pull pairs are followed on
the stack, and where the pairing can't be established the push reads
everything it saves (docs/RECOMP.md, "Before the bulk"). Functions entered as
vectors, hints, coverage seeds or indirect targets, and functions that
`--calls` shows being called from sites static analysis doesn't know, keep
everything live after return. Ported routines use the dead set to skip
reproducing values nobody reads (docs/PORTING.md), and
`tools/dontcare_check.py` checks every don't-care set they declare against it;
`liveness.json` gives each function's as `dont_care`, in `DC_*` terms.

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
| `[[data]]` | `start`, `end`, `type`, [`name`], type parameters | never decode as code; `type` is one of `DATA_TYPES` in `tools/disasm/hints.py` (`padding`, `bytes`, `words`, `ptr16`, `ptr24`, `struct` with `size`, `lz`, `brr`, `script` with `format`, `text` with an optional `format` from `tools/disasm/text.py`, `mixed`, `undetermined`, `header`, `ram_code`, ...); the matching build checks padding, element sizes and lz streams |
| `[[overlap]]` | `addr`, `m`, `x` | an instruction that really runs with widths giving two lengths; the source writes the decode under m/x and checks the other ends on an instruction |
| `[[ramcode]]` | `name`, `base`, `source`, `format` = `lz` / `copy` / `writes` / `run`, [`size`], [`end`, `m`, `x`], [`regs`], [`over`], [`patch`] | a RAM section: its image at run address `base`; `writes` simulates the stores of the code from `source` to `end` (which may itself be in an earlier section; long loads from ROM or a section count as constants); `run` executes the routine at `source` with widths `m`/`x` and registers `regs` (`a`, `x`, `y`, `db`, `d`) and collects what it writes through the WRAM port from `base` (M3); `over` names a section this one is written over; `patch` names a section whose code the program replaces with this one for a while (both are alternatives: the analysis reads the patched section, blocks exist for both) |
| `[[noreturn]]` | `addr` | as `[noreturn] addrs`, one per entry with its comment |
| `[[entry]]` without `m`/`x` | | entered with any widths (interrupt handlers) |
| `[[jumptable]]` `targets` | list | targets of a table filled at run time (`count` may be 0) |
| `[[indirect]]` on a `JSR`/`JSL` | `at`, `targets` | code writes the call's operand at run time; the targets replace the one in the bytes |

Hints the MD1 work added carry `kind = "unreached"` on entries for code nothing
found reaches, and live in sections headed "MD1".

Kinds and fields added for closure (MD4, [MATCHING.md](MATCHING.md#md4-as-built)):

| Hint | Fields | Meaning |
|---|---|---|
| `derive` on `[[indirect]]` / `[[jumptable]]` | `"deriver.part"` | the site's targets come from a deriver in `tools/disasm/derive.py` (below), traced to a fixpoint; `targets` becomes optional |
| `bound` on `[[indirect]]` / `[[jumptable]]` | `"guard"`, `"layout"`, `"constant"` | how the target set is argued: code bounds the index before the dispatch; the table ends where code or another table begins (the index isn't checked); the targets are the constants every writer of the cell loads |
| `follow = false` on `[[indirect]]` | | the targets are entries, not traced from the site (the menu threads at `$C2:8203`, which run on their own stacks) |
| `header_vectors = true` | top-level key | the native NMI and IRQ and the reset vector of the header are entries |

The analysis also follows a `PER` into the `JMP (abs)` / `JMP (abs,X)` it
precedes as a call returning after the pushed address, and treats the
instruction starts and branch targets of `patch` sections as entries.

Kinds and fields added for data typing (MD2, [MATCHING.md](MATCHING.md#md2-as-built-first-part)):

| Hint | Fields | Meaning |
|---|---|---|
| `[[resource]]` | `at`, `count`, [`name`], [`kind` = `ptr24` \| `ptr16` + `bank`], [`slots`], `target` = `lz` \| `text` \| `sized` \| `blob`, [`type`], [`size`], [`extent = "next"`, `end`], [`skip`], [`format`] | a table of pointers a loader indexes by a resource id: `tools/disasm/resources.py` types the table (`ptr24`/`ptr16`, `slots` entries, `count` of them walked) and what its entries point at |
| `fill = true` on `[[data]]` | | a background type for a range: every byte in it that no other region claims is of this type (bank D2-D5's sprite tiles). Other regions carve it up; one crossing its edge is an error |
| `format = "event-commands"`, `entries` on a `script` `[[data]]` | | a run of event commands with no header, entered at the `entries` addresses (the built-in commands at `$C0:5D37`); decoded and assembled from the decoded form (MATCHING.md, "The event scripts") |

`[[resource]]` targets:

- `lz`: each entry must be a stream `tools/disasm/lz.py` consumes exactly, and
  the region is exactly those bytes. An entry that isn't is an error unless
  `skip` lists it (with the comment saying why). `format = "event"` says the
  output is an event script (`tools/disasm/eventscript.py`), which the regions
  carry and the matching build decodes (MATCHING.md, "The event scripts").
- `text`: each entry names a zero-terminated string, and the region runs to the
  terminator. The report counts the bytes between one string's terminator and
  the next string, which is zero when the table's strings tile their range.
  `format` names the encoding (`dialog`, `tools/disasm/text.py`), which the
  regions carry and which says where a string ends: the parse skips operands,
  and a string also ends at `$03 $00` (MATCHING.md, "The string encodings").
- `sized`: the entry's first two bytes are the record's length, as the sound
  driver's blocks are. A record that runs into the next bank becomes one region
  per bank.
- `blob`: a region of the hint's `type`, `size` bytes long, or (`extent =
  "next"`) running to the next entry of the same table in the same bank, with
  `end` (one address per bank) for the last one.

Entries that repeat a target claim one region. A derived region that lands
inside a hint typed `mixed` or `undetermined`, or inside a `fill` region,
splits it; any other overlap is an error. The expansion runs once per `Hints`
object, before tracing, so `census.py`, the analysis and the matching build all
see the same regions; `tools/disasm/resources.py` has no state beyond the ROM
and the hints, and `tests/test_resources.py` exercises it on synthetic data.

## Parsers and derivations (MD4)

Where code dispatches through addresses taken from data, the targets are
derived by parsing that data from every place the code starts it, not from
the movies. Each parser's command set is read from its interpreter's handlers
(the module docstrings give them handler by handler); command lengths and
effects are facts about the code, the scripts stay in the ROM.

| Module | Interpreter | Roots | Gives |
|---|---|---|---|
| `tools/disasm/objscript.py` | object command scripts, `$C2:0F63` (83 commands) | constant arguments of `$C2:04D9`/`$C2:04FB` and constant stores to slot +$07; the eight scenes' script streams (`$C6:FFE0`, run at `$7F:0400`) and their `$7E:8480` word tables (`$C6:FFC0`); commands `$08`, `$09`, `$43`, gotos, calls, branches | `$34` JSR targets (`$C2:1819`), `$4E` JSL targets (`$C2:1836`), `$35`/`$42` handler words (`$C2:0553`), the commands used (`$C2:0F7A`) |
| `tools/disasm/eventscript.py` (`derive.py` `eventscript`) | event scripts, `$C0:5900`/`$C0:5AA3` (256-entry table `$C0:5D6E`) | the script each location record names (the word at `$F6:0008`, the stream table `$FC:F9F0`), every function of every object; the built-in commands at `$C0:5D37` | the comparison operators of `$12`-`$16` (`$C0:6307`, `633D`, `6398`, `63D7`, `6433`) and `$FF`'s modes (`$C0:4009`) |
| `tools/disasm/seqscript.py` | the attract program's sequence channels, `$C3:0B03` | constant stores to `$0A20`/`$0B20`, pointer stores in code the scripts call, command `$A2` on a pointer | code calls through `$00F0` (`$7E:37F6`), tasks added to `$0920` (`$7E:3998`) and `$0940` (`$7E:39CF`) |
| `tools/disasm/battlescript.py` | battle animation scripts, `$C1:4C9E` (219 commands), and enemy scripts, `$C1:AFE6`/`$C1:8706` (41 conditions, 23 actions) | the five loaders' header tables (`$CD:5EC6`, `5EF0`, `5FF0`, `61F0`, `63F0`) and the call table `$CE:0000`; the enemy pointer table `$CC:8B08`, each script read as the evaluators read it | the place and direction handlers (`$C1:7633`, `$C1:75D1`), the condition handlers (`$C1:874E`, `8769`, `B006`, `B021`); also the typed regions' codecs (MATCHING.md, "The battle script formats") |
| `derive.py` `operands` | calls in RAM code whose operand the program writes | absolute and long stores (words, or byte pairs) into a transfer's operand in the same section | the JSR targets at `$7E:3023`, `$7E:35FC`, `$7E:5E43` |
| `derive.py` `winlist` | window command lists, `$C2:ED31` | constant X at the calls, the word table at `$C2:DFC5` | the handlers the lists use (confirms the 11-entry bound of `$C2:ED50`) |
| `tools/disasm/resources.py` | the loaders that index a 24-bit or 16-bit pointer table by a resource id | `[[resource]]` hints, each with the reading code in its comment | typed regions for the tables and for the streams, strings and blobs their entries name (MD2) |

`tools/disasm/codeargs.py` gives the constant values a register can hold at an
instruction over every path into it (none when a path reaches an entry, a
table target or a non-constant setter). A deriver reports a problem for every
source it can't resolve and doesn't list with a reason. Derivation runs in
rounds: each restarts the analysis with the targets the previous round derived
last, until a round derives what it started from, so a partial code set can't
leave spurious targets behind.

`tools/disasm/closure.py` holds the closure checks C1-C7 the matching build
runs (MATCHING.md).

## The RAM map (`re/ram.toml`)

`re/hints.toml` says what the ROM's bytes are; `re/ram.toml` says what work
RAM's bytes mean: each game variable's address, width and layout, a name, one
or two sentences of meaning, the subsystem that owns it and where that was
established. It is knowledge about the console's memory, worked out from the
code, so it is committed; no initial value or table content read out of the ROM
goes in it. The schema is at the top of the file:

| Entry | Fields | Meaning |
|---|---|---|
| `[[page]]` | `name`, [`d`], `subsystem`, `meaning`, `source`, [`why`] | a direct page: D while that code runs, or no `d` and a `why` when D isn't known statically |
| `[[var]]` | `name`, `addr` or `dp`, [`size`], [`count`, `stride`, `index`], [`page`], `subsystem`, `meaning`, `source` | a variable, an array (`count` elements `stride` bytes apart, selected by `index`), or with `[[var.field]]`s (`name`, `offset`, [`size`], `meaning`) an array of records |
| `[[image]]` | `name`, `start`, `end`, `subsystem` (a list), `meaning`, `source` | work RAM holding a program the game runs from there (the attract program at `$7E:3000`): routines, named by their origin like ROM code, and left out of the count |
| `[[not_ram]]` | `file`, `values`, `why` | literals in one `game/` file that look like work RAM and aren't (SPC700 addresses, program counters, VRAM word addresses, counts), for the count only |
| `[[offset]]` | `file`, `values`, `bank`, `why` | four-digit literals the file adds to a bank's base (`0x7F0000u + 0x0980`), which the count reads as that bank's addresses |

Work RAM is reused from one game mode to the next, so two entries may cover the
same bytes when their subsystems differ (the battle's `$7E:2000` is not the
field's); within a subsystem the map rejects an overlap. State every mode
shares -- the party, inventory, story flags -- is `core`.

One directory holds two such modes, so it has two tags. Bank `$C2` is the menus
proper and the sprite-object mode started at `$C2:0000`, and they reuse the same
bytes: the mode's `$40`-byte object slots at `$7E:0B30` cover the menus'
records at `$7E:1800`, and its palette buffer starts where the menus keep the
caller's stack pointer. The mode's variables are `menu_object`, which is not a
directory -- nothing in `game/` is scanned as one -- but shares with `menu`, so
a file in `game/menu/` is named by either. A record of the *sharing* subsystem
names only the bytes its fields cover, so the object slots' unread bytes don't
swallow the menus' own cells inside their span.

**Direct page.** A direct-page offset has no meaning of its own: `$6D` is the
event engine's current object with D = `$0100` and something else in battle,
where D = `$0000`. So a variable the code reaches through the direct page names
its page, and its address is the page's D plus the offset. The pages come from
measurement, not assumption: every ported routine's D and data bank at entry,
logged over the whole movie library, agree with the constant `TCD` and
`PEA`/`PLD` sites listed in each page's `source` (field and events `$0100`;
battle, menus and scenes `$0000`; `$1D00` scratch pages in field and battle;
the menus' `$0D00` and `$0200`; the sound driver's `$1E00`; the decompressor's
`$0300`). Two pages have no D, and the map says so rather than choose one:
`caller`, the controller reader's cells, which run on whichever page the caller
set (five different ones are measured), and `menu_record`, where the menus
point D at the `$40`-byte record being drawn. Variables on those pages have an
offset (`dp`) and no address. The I/O pages (`$2100`, `$4200`, `$4300`) are
registers, which the platform services own, and aren't in the map.

**The header.** `python3 tools/ram_map.py header` writes `game/ram.h`, which
ports include (`#include "ram.h"`):

- `RAM_<NAME>`: the long address (`$7E`/`$7F`), for `m_read8` and friends; for
  an array or record, element 0's, with `RAM_<NAME>_COUNT` and
  `RAM_<NAME>_STRIDE`, and a record's fields as `RAM_<NAME>_<FIELD>`;
- `RAM_<NAME>_DP`: the direct-page offset of a variable with a page, for
  `m_dp`, and `RAM_PAGE_<PAGE>` the page's D;
- `RAM_ABS(addr)`: the 16-bit operand of an absolute access, for code that
  still reaches work RAM through the data bank (`m_abs`).

The header is committed rather than generated into the build tree: it is
source ports include, the CI builds have no ROM (the map needs none, but
nothing else in the build runs Python), and a reviewer should see the names a
change adds. `tests/test_ram_map.py` fails when it is out of date with the map,
and checks that `tools/movies/addresses.py`, which reads the same memory from
Python, names nothing the map doesn't.

**The accessors.** `python3 tools/ram_map.py accessors` writes the same
knowledge a second way, and this is the one a port uses: `game/vars.h`, the
game's variables as values to read and write, with `runtime/port/vars_ram.c`
backing them. A port includes `vars.h` and `state.h` and never an address:

```c
uint8_t battle_menu_member(const GameState *g);
void set_battle_menu_member(GameState *g, uint8_t v);

enum { FIELD_OBJECT_FACING_COUNT = 64 };   /* by object slot */
uint16_t field_object_facing(const GameState *g, unsigned slot);
void set_field_object_facing(GameState *g, unsigned slot, uint16_t v);
```

The shape follows the map: `size` 1, 2, 3 or 4 gives the C type; `count` gives
an index parameter, which means what `index` says it means (a slot, a line) and
never a byte offset, and a named `_COUNT` beside it; each `[[var.field]]` of a
record is its own pair, indexed the same way; and a variable wider than four
bytes has no one C value, so it gets `<name>_byte(g, at)` and a `_BYTES`
constant. Each variable's `meaning` is carried through as the comment above its
pair, which is most of what makes a call site readable. The twelve variables on
a page whose D isn't known statically have no address, so they have no
accessor; they wait for a shape that takes the page base from the caller.

**No layout reaches `game/`** -- no address, offset or stride -- and that is the
point: a port written against the accessors keeps working when the storage
moves. `runtime/port/vars_ram.c` is the backing, and it reads and writes exactly
the addresses the original's code does, so a port using the accessors writes the
same bytes and shadow mode still compares them. The seam is the platform
services', for state rather than hardware: the second backing, which holds the
same values in its own storage, replaces that file per subsystem once no
generated code touches that subsystem's state, and nothing in `game/` changes
with it (docs/PLAN.md, "Phase 3a"). `game/ram.h` stays for generated code and
the ports not converted yet.

Both generated files are committed, for the reasons the header is, and
`tests/test_ram_map.py` fails when either is out of date. What the map has no
name for -- a buffer the game locates at run time, like the per-line window
table `$C3:0077` draws into -- is reached through `game/wram.h`, which is an
escape by address and so a finding: `grep -rn wram_ game/` is the list, and it
is meant to empty.

Three more seams, found by the first subsystem rewritten against the accessors
(`game/scene/`, RECOMP.md, "Phase 3a: the scene subsystem off the machine"),
sit beside `vars.h` with the same backing arrangement, in
`runtime/port/state_io.c`: `game/cpu_io.h`, the multiplier, divider and work RAM
port as the game drives them (registers, not variables, but an operand or a
cursor one routine leaves for the next is state all the same);
`game/rom.h`, the cartridge by address, which only the named resource
accessors of the `*_data.h` headers call; and, for the twelve variables above
and their kind, nothing generated yet -- `scene/scratch.c` maps a page and an
offset onto the accessors of each page it knows, by hand (docs/TODO.md). A
record the game indexes by a byte offset rather than an element number (the
OAM buffer, as the scene's sprite writers see it) gets a second, byte-buffer
entry in the subsystem that uses it that way (`scene_sprite_table`), since an
index parameter can't carry an odd offset.

**Adding to it.** When a port establishes what a byte means, add the entry
with the port as its `source`, run `python3 tools/ram_map.py header` and
`accessors`, and use the name. `python3 tools/ram_map.py lookup 7E:95D5` says
what the map knows about an address, and `check` validates the file.

**Coverage.** `python3 tools/ram_map.py coverage` reports how much of what
`game/` touches has a name. It counts four-digit literals `$0100-$1FFF` (low
RAM) and six-digit literals in banks `$7E`/`$7F`, and `RAM_` names (plus a
constant offset) at the address they stand for, by distinct address and by
use, so renaming a file doesn't move the total. A use is named by an entry of
its own subsystem, of `core`, or of the one it shares a page with (field and
event, scene and attract): the battle's name for `$7E:0B40` says nothing about
the field's. `--open [--subsystem S]` lists what is left by the subsystem using
it. Absolute operands of `$2000` and up are outside the total -- a literal
`0x95D5` can't be told from a constant without reading the code -- and are
reported beside it as the ones the map names (`--abs` lists them); most battle
and menu variables are there. Direct-page offsets aren't counted. A literal an
`&` reaches (`x & 0x01F8`, `addr &= 0x1FFF`) is a bit mask and is skipped
without a `[[not_ram]]` entry: nothing ANDs with an address, and masking one to
a range leaves the mask a mask.

**As built.** The map was harvested from what the repo already said: the port
comments in `game/`, PORTING.md's status table, MOVIES.md and
`tools/movies/addresses.py`, PLATFORM.md's "as built" sections, and the enums
some ports already had. It holds 14 pages, 601 variables and 3 program images,
and 297 literals in 41 files are marked as not work RAM (the sound driver's
`$C7` program counters are most of them). Of the literals `game/` used, it
names 338 of the 370 distinct addresses (91%, 906 of 951 uses): 258 of 285 in
low RAM and 80 of 85 long. By the plan's first count, which took every
four-digit literal in range for an address, 523 low-RAM addresses in 1222 uses
and 230 long ones in 361 -- the difference is the literals marked as something
else (297 addresses) and the attract program's routines at `$7E:3000` (143).
Beyond the total, it names 535 distinct absolute operands of `$2000` and up
(1289 uses). The
32 open addresses are listed by `--open`: menu 13, field 6, battle 5, event 4,
scene 4, core 2, each a byte whose use the code shows but whose meaning it
doesn't (a call-out's parameters, a flag only ever tested). (Those counts are
from the harvest. Every wave since adds ports and addresses to both sides, so
run `coverage` for the current ones; docs/TODO.md tracks what is still open.)

`game/battle/setup.c`, the plan's example of a transcription, was renamed with
it as the demonstration: its data-bank helpers take a work RAM address
(`RAM_ABS` inside), and every literal the map names became the name, 221 of
them, e.g. `STORE_DOWN(s, 0x0004, RAM_BATTLE_MENU_MEMBER)` for
`STORE_DOWN(s, 0x0004, 0x95D5)`. The rewrite is mechanical -- a literal is
replaced only as an address argument of a known helper -- and what stays a
literal is the 56 addresses whose meaning no port establishes yet and the
direct-page scratch cells. The gate didn't move: `tools/suite.py --only core` passes, and
`battle-magus`, `battle-golem-twins-gameover`, `battle-tyrano-lair-walls` and
`late-omen-battles` run clean in shadow mode. Shadow mode alone doesn't reach
this file, though: every call of `battle_setup_state` and
`battle_setup_graphics` in the library is interrupted by NMI, and interrupted
calls aren't compared. So the check that does is the port running: `--native
on --recomp off` over `gato-battle` (21600 frames) and `battle-magus` (18000)
traces identically before and after the rename. A side effect: `$7E:213A`, a palette destination that the
platform lint had to be told was not the register `$213A`, is now
`RAM_PALETTE_BASE + 0x13A`, so setup.c left the lint's exception list.

## Known structure

- Reset `$00:FF00` is `SEI; CLC; XCE; JML $FD:C000`.
- NMI and IRQ jump through JML stubs in RAM at `$00:0500` and `$00:0504`. Each
  game mode installs its own handlers (see `re/hints.toml`). Writing `$40`
  (`RTI`) there disables a vector.
- The event script interpreter dispatches through a 256-entry command table at
  `$C0:5D6E`, fetching command bytes from `$7F:2001,X`. The scripts are parsed
  and typed (`tools/disasm/eventscript.py`, MATCHING.md "The event scripts").
- Bank C2's object system runs a handler per slot (`$C2:051D`, `JMP ($0050)`)
  and an object script interpreter with an 83-entry command table at
  `$C2:0F91`. Script commands `$34`/`$35`/`$4E` call, spawn or JSL to addresses
  in script data; since MD4 their targets are derived from every script
  (`tools/disasm/objscript.py`).
- The menu in C2 runs cooperative threads: `$C2:8203` starts one on its own
  stack and `JSR $821E` yields. Thread starts are `entry` hints (and the
  `follow = false` targets of `$C2:8203`) because the tracer would treat that
  `JMP (abs)` as a tail call.

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

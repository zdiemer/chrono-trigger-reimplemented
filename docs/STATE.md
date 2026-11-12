# State storage and address dependencies

`python3 tools/ram_map.py storage` measures Phase 4a by **variables** in
`re/ram.toml`: each `[[var]]` counts once, regardless of fields, array length,
page instances or bytes. Before migration all 2,263/2,263 (100%) use the work-RAM
image in `ct_native`. Emulator-backed builds must retain that backing while
generated code and byte comparisons use it.

## Inventory

`python3 tools/state_inventory.py` reproduces [state-addresses.tsv](state-addresses.tsv)
and [state-overlaps.tsv](state-overlaps.tsv) without a ROM. The source ledger
covers `.c` and `.h` in `game/`, excluding generated `ram.h` and `vars.h`, with
comments removed and original line numbers retained. It includes declarations
as well as calls; these are **syntactic sites**, not execution counts. Categories
can overlap. Expressions containing a variable address are candidates, not a
claim that every value they produce is RAM. Follow the listed helper's callers
when selecting another module.

| Address dependency | Sites |
|---|---:|
| Raw WRAM helpers and work-RAM streaming port | 307 |
| Machine read/write and direct-page/absolute helpers | 4 |
| Resource reads/transfers whose address interface can fall back to RAM | 713 |
| Page instance conversion and explicit vars_pages include | 4 |
| PF_GAME video/DMA sources | 72 |
| RAM names, long WRAM literals, bank shifts/masks | 2,142 |
| Unchecked accessor uses/declarations | 163 |

The video ledger includes fixed `$7E/$7F` sources, RAM constants, low-bank
mirrors and computed sources. Computed descriptors (tile animation frame
sources, queued field transfers, menu tilemaps and line effects) cannot be
classified solely from a literal. They remain address dependencies even when
their current data happens to be cartridge data. Native video obtains these
bytes from `Platform`'s `read_game` callback, which the frontend binds to
`state_host_read8`; it must see the same values as typed accessors.

The overlap ledger expands rows, elements, record fields and every declared
page instance, omitting padding with no accessor. It records **6,535 pairs**,
**2,011 variables** and **122,208 distinct shared bytes**. A pair's first/last
addresses bound its shared bytes; holes are possible. Both cross-mode reuse
and simultaneous aliases count: separate structs would change either one's
initial contents unless the transition explicitly establishes them. Fields
within a record can overlap too (e.g. `character_records.record` and `hp`);
those need a single owner even though the variable count is one.
[state-field-overlaps.tsv](state-field-overlaps.tsv) lists **6,019 within-record
field pairs in 21 variables**, counted separately from the variable pairs.

The resource-address sites include named resource accessors and their backend
declarations; most read cartridge data, but the `rom_cartridge_read` contract
also serves RAM-selected pointers. Counting all of them avoids silently losing
that path. `runtime/port/rom_host.c` already delegates RAM reads to
`state_host_read8`. The inventory makes no claim that all 713 read work RAM.

| Map subsystem | Variables | With another variable's bytes | Unchecked | On page instances |
|---|---:|---:|---:|---:|
| core | 89 | 32 | 6 | 0 |
| data | 8 | 0 | 0 | 0 |
| event | 20 | 17 | 0 | 0 |
| field | 308 | 218 | 16 | 0 |
| menu | 467 | 420 | 8 | 27 |
| menu_object | 39 | 39 | 1 | 0 |
| overworld | 42 | 31 | 2 | 0 |
| battle | 922 | 909 | 53 | 0 |
| scene | 346 | 327 | 1 | 1 |
| warp | 22 | 18 | 1 | 0 |

Unchecked tables are explicit overrun dependencies, not permission to allocate
independent arrays: the map's `unchecked` reasons and the source ledger identify
their destination cells. Checked accessors also preserve the original address
on an invalid index; the gate diagnoses those. Shared scratch and page instances
use the explicit owners described below. Raw operations include bulk boot
and mode clears, unpacking, event-selected memory, save projection, copying and
DMA staging; replacing only typed accessors without routing these paths loses
writes. `hw_port_read/write` is a separate path and must use the address view too.

## Cartridge save projection

`menu/saves.c` projects eight runs (the layout resource at `$FF:FA7B`) in order
into each of three `$A00`-byte slots of the 8 KiB SRAM. These are format facts,
not extracted asset contents:

| Work-RAM start (canonical) | Bytes | Slot offset |
|---|---:|---:|
| `$7E:2400` | 1,456 | `$000` |
| `$7E:2C23` | 51 | `$5B0` |
| `$7E:0400` | 16 | `$5E3` |
| `$7E:0100` (bank-zero mirror) | 16 | `$5F3` |
| `$7F:0000` | 512 | `$603` |
| `$7E:027E` (bank-zero mirror) | 2 | `$803` |
| `$7E:0290` (bank-zero mirror) | 17 | `$805` |
| `$7E:2C7C` | 30 | `$816` |

The projection writes 2,100 bytes; the checksum covers the entire 2,560-byte
slot, including its preserved tail. Marks, sums, last slot and completion
metadata occupy SRAM's end (`menu/saves.h`). New Game+ copies two selected
runs and the unlocked-tech bitset instead of restoring the story. Save memory
itself stays a byte buffer: it is a persistent file format, not game state.
`sv_host_bytes` hands this buffer to the driver; disk names and atomic file
replacement remain frontend responsibilities.

Saving and loading use `wram_read8/write8`, so an address-to-field view must
serialize and deserialize moved fields in little-endian order, including
bank-zero mirrors. Route `peek`/`watch`, end-state output, generic clearing,
copying and video read through that same view. None may read a stale duplicate
of a moved field in the residual image.

## Native structs and the compatibility view

`host_module` selects storage in a generated module struct, independently of
the broader map subsystem. `ram_map.py accessors` generates `vars_host.h` and
`vars_host.c` alongside the existing outputs. Ordinary scalar fields use
`uint8_t`, `uint16_t` or `uint32_t`; buffers remain byte arrays. There is no
packed layout, pointer into WRAM, or second copy to synchronize. Arrays use
element indices, not the original stride; record padding is not state.

With `CT_VARS_HOST`, an owner's accessors read/write its fields directly.
A variable's `host_alias = "owner_variable"` declares a view of another
selected variable's storage, with no field of its own. A list of owner names
composes a view across several owners. A record field's `host_alias` likewise
names one or several fields in that record. All declared bytes must be covered,
excluding padding; missing owners, chains/cycles, partial coverage and duplicate
owners are errors. `reuses` documents mode reuse, not independent storage.

`[[owner]]` declares storage with the same layout schema as a variable, but
without generating game accessors. It separates the native model from existing
API views: `battle_combatants` owns typed stats while `battle_stats.block` and
the named stat accessors view those fields. Owner-only records are excluded
from the variable measure and checked for duplicate names and storage.

A page owner's storage is an array in the declared instance order. A named
page handle, array index or mixed handle selects an ordinal; the projection
maps each instance's original addresses to that record. Padding and distance
between direct pages allocate no storage. Bounds fallbacks remain available
even when `CT_ACCESSOR_CHECKS` is disabled, so safety never depends on diagnostics.

For example, the 12 palette-animation records own their `frames` words.
`fade_mode` and `fade_level` view the two bytes of those words. The warp's
pixel masks view these same records: the masks deliberately survive the warp
and seed the next location's animations. Single-owner fixed-address aliases call the
named owner's byte-view functions with a relative offset; composed and page
views use the indexed struct projection. Both paths extract/merge
typed fields explicitly in little-endian order. They never cast a host struct
to the cartridge's layout. The character record similarly owns its `record`
buffer, with HP, MP and stat views sharing it. These are persistent record
formats, not independent copies of each typed field.

Indexed accessors retain `CT_ACCESSOR_CHECKS`' `IN_RANGE` diagnostics before
their host access. Invalid indices take the compatibility path, so they cannot
index outside a C object. `_unchecked` accessors take that same safe path
without reporting a fault. The inventory's fourteen tail cells belong to the
menu arrays; the demo's eighth character record reaches the tech storage.
The map's existing `unchecked` reasons remain the contracts for those reaches.
Emulator builds compile the original RAM accessor bodies; no game logic changed.

The address projection selects a field with two indexed table reads (page,
then byte), followed by a field switch. Only the 510 populated pages have byte
tables; holes select the residual image. It does not search all fields for each
byte of a bulk transfer. `state_host_read8` and `state_host_write8` canonicalize
WRAM mirrors and use this projection. The WRAM streaming port uses them too.
Native route `peek`/`watch` already call this API: reads are untimed, while game
accessors charge the same per-byte amount as before. Bulk helpers retain their
existing timing and bank wrap rules.

Saves therefore read from and load into the struct fields through their
existing projection. The save code, SRAM file format, video callbacks and
end-state writer keep their interfaces. `vars_host_test` runs without a ROM:
it supplies save-layout metadata, loads a committed cartridge `.srm` through
the actual slot checker and loader, edits struct fields, saves them with the
actual checksum writer, and loads the exported bytes into a fresh state.
It also checks independent states, mirrored and partial writes, cross-field
copy/fill, internal and cross-mode aliases, diagnosed and unchecked tails,
the streaming port and video transfers sourced from moved fields. Its backing
is compiled with both `CT_VARS_HOST` and `CT_ACCESSOR_CHECKS`. A full-address
pattern fill/read checks dispatch boundaries and holes. `unpack_time_test`
unpacks a synthetic stream into both residual storage and the sprite owner,
checking literal/overlapping-backreference writes and equal native time.

## Migrated modules and remaining clients

The third slice moves 1,919 more variables, leaving **27/2,263 (1.19%)**
image-backed. The 2,236 migrated variables comprise 278 owner variables and
1,958 views. Another 48 storage-only records supply semantic owners without
adding game accessors or changing the variable-count denominator. Together
these owners hold **129,365 serialized bytes**, with 207 internal field views.
The counts include compatibility state; they do not measure live computation.

| Map subsystem | Image-backed before | Image-backed after | Total variables |
|---|---:|---:|---:|
| battle | 904 | 0 | 922 |
| menu | 415 | 0 | 467 |
| scene | 307 | 0 | 346 |
| field | 178 | 0 | 308 |
| menu_object | 39 | 0 | 39 |
| core | 51 | 27 | 89 |
| overworld | 29 | 0 | 42 |
| event | 11 | 0 | 20 |
| warp | 12 | 0 | 22 |
| data | 0 | 0 | 8 |
| **Total** | **1,946 (85.99%)** | **27 (1.19%)** | **2,263** |

Ownership follows game records and resources, including their reuse by other
modes. Combatants have typed HP, MP, status, experience and equipment fields;
script-selected stat offsets serialize those same fields. Nine menu records
hold sprite/animation state, and the scene arithmetic record has its own and
two channel instances. Menu task records own their task fields. The scratch
views retain shared initial contents immediately, without transition copies.

Large resources crossing those records are composed views, with separately
owned portions where needed. For example, field map rows before and after the
combatants own their layer bytes; two boundary portions retain the map bytes
on either side of the combatant array. The rows through the array view the
combatants themselves. The physical stride remains projection metadata, while
the C arrays omit the intervening cells owned by other resources. This does
not create a replacement array spanning an entire overlap component.

The same rule covers battle item rows around menu direct pages, object records
around scene channels, shared screen/palette scratch and interleaved field
attributes. Resource buffers remain buffers: event scripts, decoded graphics,
map rows and staging bytes do not become invented scalar fields. Unchecked
inventory, combatant and table tails still reach the next owner through the
address projection, with the original index arithmetic and diagnostics.

The raw-client audit retains format/dynamic clients as address views:
`menu/saves.c`'s projection, new-game initialization and bulk clears;
decompression and palette streaming; video's `read_game` callback;
`rom_host.c`'s RAM-selected resource pointers; and native route and end-state
reads. All already reach `state_host_read8/write8`; the private residual image
has no game client bypassing the projection. No game algorithm needed a new
raw-address escape or an accessor conversion to make this migration coherent.
Their remaining address dependence is separate from their storage ownership.

The remaining 27 variables are retired sound-driver state, covering 257
bytes. They include the old pointer/list group as well as isolated driver
cells. Resolve their compatibility clients with driver/machine removal rather
than allocating new native owners for the obsolete driver. The residual
128 KiB allocation remains: 1,707 addresses have no owner, including these
257 bytes and 1,450 unnamed bytes/padding. Removing that allocation requires
an audit of dynamic clears, script-selected accesses and unchecked tails into
those holes; zero named variables alone is insufficient.

## Validation

`vars_host_test` checks typed combatants against whole-record script views,
all menu and scene page instances, 24-bit initialization/truncation, a view
crossing two owners and a view crossing two fields, independent instances,
invalid-index fallbacks, full-address projection, video sourcing and the real
save/load/checksum routines with a committed cartridge `.srm`. The Python
map tests reject missing owners, chains, duplicate ownership and incomplete
compositions, and verify storage-only records do not add API variables.

The Windows MinGW Release build passes with `CT_WERROR=ON`. All 629 Python
unit tests pass, as do `vars_host_test`, `vars_test`, `native_time_test` and
`unpack_time_test`.
The generated-file checks and both `portstyle.py --check` and
`portsmell.py --check` pass. An additional generated accessor sweep checks
713,158 reads and writes against the serialized address view with zero
failures, both with accessor diagnostics enabled and disabled.

The sampled gate with `-j 2 --evidence` ends `suite OK`: all 58 shadow movies
are clean, all 58 strict and native movies reach their ends, and both fast
routes reach their outcomes in shipping, native and scaffolding-free builds.
The native route outcomes are Beyond Time's second form at frame 3,770 and
the telepod's Lavos shell at frame 5,162, with no route exceptions. Trace
identity, recorded-outcome ratchets and the remaining gate checks pass;
no baseline or exception was changed. Validation used neither `--all-movies`
nor `--long-routes`.

The prior slices' measurements are in
[history/state-storage-second-slice.md](history/state-storage-second-slice.md)
and [history/state-storage-first-slice.md](history/state-storage-first-slice.md).

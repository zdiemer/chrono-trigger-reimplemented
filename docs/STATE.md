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
stay in the image until ownership is resolved. Raw operations include bulk boot
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
selected variable's storage, with no field of its own. A record field's
`host_alias = "owner_field"` does the same within its record. All of a view's
declared bytes must belong to that owner, excluding padding. The generator
rejects missing owners, alias chains/cycles, partial coverage, duplicate
owners and overlaps without explicit aliases. `reuses` still documents when
one game mode takes over another's cells; it does not allocate storage.

For example, the 12 palette-animation records own their `frames` words.
`fade_mode` and `fade_level` view the two bytes of those words. The warp's
pixel masks view these same records: the masks deliberately survive the warp
and seed the next location's animations. Generated alias accessors call the
named owner's byte-view functions with a relative offset; these extract/merge
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
Page-instance variables still wait for ownership to be resolved. Emulator
builds compile the original RAM accessor bodies; no game logic changed.

The address projection selects a field with two indexed table reads (page,
then byte), followed by a field switch. Only the 23 populated pages have byte
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

## Migrated slices and remaining clients

The second slice moves 313 variables: 245 additional owners and 68 alias views.
Together with the four location records, there are **249 owner variables and
68 aliases**, with nine internal field aliases and **4,072 serialized bytes**.
The owner-module grouping is finer than the map's subsystem grouping: live
sound has four variables; palette owners and their views have nine, and the
sprite buffer and its views have five. These counts include compatibility
cells that no longer carry live computation; they do not measure live logic.

| Map subsystem | Image-backed before | Image-backed after | Total variables |
|---|---:|---:|---:|
| core | 85 | 51 | 89 |
| data | 8 | 0 | 8 |
| event | 20 | 11 | 20 |
| field | 308 | 178 | 308 |
| menu | 467 | 415 | 467 |
| menu_object | 39 | 39 | 39 |
| overworld | 42 | 29 | 42 |
| battle | 922 | 904 | 922 |
| scene | 346 | 307 | 346 |
| warp | 22 | 12 | 22 |
| **Total** | **2,259 (99.82%)** | **1,946 (85.99%)** | **2,263** |

The map's explicit metadata is the per-variable ledger. The selected slices
include field/dialog control, menu controls and glyph state, overworld vehicle
state, battle setup and remembered state, scene controls, and warp's palette
reuse. Field screen/movement cells share the battle's X/Y owners, and scene
fade cells share the field's window-corner owner. Shared initial contents are
preserved immediately, without a mode-transition copy.

Data's eight cells are decompression inputs, its output length, and retired
decoder scratch. Its output buffers belong to the callers, not to those cells:

| Output family | Classification and storage boundary |
|---|---|
| Field map tables, scripts and object graphics | Decoded resources and mutable map/script buffers, still in the large field/event overlap groups |
| Battle animation and screen buffers, menu/overworld staging | Decoded graphics, still in shared resource scratch |
| Scene programs, text, pictures and staging | Decoded programs/resources; shared buffers remain image-backed |
| Palettes and sprite output | Mutable rendering data; selected owners are reached through the same output view |

`data/decompress.c` writes literals through `hw_port_write` and backreferences
through `wram_read8/write8`; both already reach the address view. Moving its
parameter cells does not claim that all decoded buffers have moved. The live
sound slice is `current_song`, `sound_command`, `sound_command_parameters` and
`sound_driver_effects_asked`; obsolete upload-driver state stays separate.

The raw-client audit retains format/dynamic clients as compatibility views:
`menu/saves.c`'s projection, new-game initialization and bulk clears; palette
streaming writes; video's `read_game` callback (including low-bank sprite
sources); `rom_host.c`'s RAM-selected resource pointers; and native route and
end-state reads. Each reaches `state_host_read8/write8`, with no direct read of
a migrated cell in the residual image. Typed game callers need no edits.
This resolves their storage coherence, not their dependence on addresses.

Of the remaining variables, **1,921 overlap others** and 25 are isolated
retired sound-driver cells. The small vector-stub and old sound-pointer/list
alias groups are retained for the machine/driver removal rather than given
new compatibility owners. The five largest overlap components contain 1,839
variables: battle/menu graphics scratch (965), object/menu pages (381), shared
low scratch (299), scene/field buffers (99), and bank-$7F resources (95).
None has an existing variable containing every declared byte. They need
semantic owners or views composed from several owners, including page
instances, rather than an anonymous array replacing each address span.

The residual 128 KiB allocation remains; migrated addresses in it are unused,
not mirrors. This measures ownership moved, not memory saved. Removing the
allocation also requires the remaining unnamed address clients to move.

## Validation

The warning-as-error build passed. `ram_map.py header --check` and
`ram_map.py accessors --check`, `vars_host_test`, `native_time_test`,
`unpack_time_test` and the emulator-backed `vars_test` passed. Both
`portstyle.py --check` and `portsmell.py --check` passed. No baseline or
exception was added. The Python unit suite passed all 626 tests without skips.
The sampled gate's 58 shadow movies were clean; the 7,200-frame generated-only
trace matched the reference, and the timing, video-write, fallback and refusal
checks passed. Both fast routes reached their outcomes in the shipping build
and in `ct_native`, including Lavos's shell and Beyond Time's second form.
All 58 movies and both routes passed with scaffolding compiled out. All 58
movies reached their end in strict mode and in `ct_native` with visible output;
both recorded-outcome ratchets passed. These are baseline checks, not a claim
that every final state or picture equals the reference (28 strict and 23 native
end states matched). Sound fuzzing retained both expected digests.
The sampled `tools/suite.py --ct-dev build/main/ct_dev.exe -j 2 --evidence`
gate ended `suite OK`; neither the whole-library nor long-route option was used.
Gate logs and derived artifacts remain under the ignored
build directory; the evidence cache is local to the worktree.

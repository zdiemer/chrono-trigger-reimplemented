# Native state storage

`python3 tools/ram_map.py storage` counts each `[[var]]` once, regardless of
fields, elements or page instances. It reports three mutually exclusive kinds:

- **Typed struct-backed:** every declared byte belongs to a named scalar field
  or an array of game elements. Serialized views of these fields share that
  classification; no separate value is allocated for a view.
- **Blob-backed:** at least one byte still belongs to a raw buffer. A scalar
  accessor assembled from buffer offsets remains blob-backed. A partially typed
  record is counted entirely here, including its whole-record view.
- **Image-backed:** storage is still in the residual work-RAM image.

`storage --list` names each untyped variable and the actual buffers behind it.
The measure follows owning fields, not `host_alias` labels. Fields marked
`buffer = true` remain byte arrays even when only one to four bytes wide;
`host_blob` explains opaque byte-indexed arrays whose public API already has
scalar elements. Neither small size nor moving bytes into a module makes them
typed state. Arrays of actual game elements (inventory items, party coordinates,
palette colors) are typed; arrays indexed by opaque byte positions are not.

## Typed migration

The earlier ownership measure called 2,236 variables struct-backed because they
had left the residual image. Auditing both wide buffers and narrow anonymous
remnants gives the honest baseline below. This round promotes 1,168 variables
to typed storage, without changing the 2,263-variable denominator.

| Module | Typed before | Typed after | Blob before | Blob after | Image after |
|---|---:|---:|---:|---:|---:|
| Core | 43 | 55 | 19 | 7 | 27 |
| Data | 8 | 8 | 0 | 0 | 0 |
| Event | 8 | 16 | 12 | 4 | 0 |
| Field | 167 | 241 | 141 | 67 | 0 |
| Menu | 243 | 393 | 224 | 74 | 0 |
| Menu objects | 8 | 26 | 31 | 13 | 0 |
| Overworld | 31 | 34 | 11 | 8 | 0 |
| Battle | 186 | 859 | 736 | 63 | 0 |
| Scene | 81 | 303 | 265 | 43 | 0 |
| Warp | 10 | 18 | 12 | 4 | 0 |
| **Total** | **785** | **1,953** | **1,451** | **283** | **27** |

Sprite submission now owns `scene_sprite_index`, `scene_sprite_high_pointer`,
`scene_sprite_offset_x` and `scene_sprite_offset_y` as named integers.
`field_kept_party_x` and `field_kept_party_y` own three coordinate words each.
Character records own HP/MP fields and the OAM records own x/y/tile/attributes;
their byte-oriented consumers serialize the same fields. Battle scratch,
menu drawing state, scene effects and event controls likewise use named scalar
owners wherever their overlapping layouts allow a complete field or array.

The 283 remaining blob-backed variables include graphics, map and script
buffers, saved raw mode memory and record views containing unknown bytes.
Some scalar arrays span several modes' records: only some instances currently
have typed ownership, so the entire array remains blob-backed. Converting
those requires further record/instance decomposition. `storage --list` is the
current list, including every partial dependency; none is counted as typed.
The 27 image-backed variables are obsolete sound-driver state (257 bytes).
Another 1,450 unnamed/padding bytes lack owners. Audit dynamic script writes,
clears and unchecked tails before removing the residual 128 KiB allocation.

## Owners and address views

`host_module` selects a native owner. `host_alias` names one owner or a list
composing several owners. `[[owner]]` declares storage without adding a public
variable or changing the measure. Internal field views can likewise name
several fields. Every declared byte must be covered exactly once: missing
owners, partial coverage, alias chains and duplicate storage are errors.

Page owners are arrays in declared instance order. Named, indexed and mixed
page handles choose records, independently of C padding or the original
spacing of direct pages. Whole-record accessors and named-field accessors share
the same owner. When a resource crosses typed state, its remaining unstructured
portions stay explicit buffers; only those bytes are allocated there. Repeated
remaining layouts keep an array of records. No duplicate backing is maintained.

The generated projection selects a field by page/byte tables and serializes
it explicitly in little-endian order. `state_host_read8/write8` canonicalize
address mirrors and route every owned byte through it; only holes use the
residual image. Save/load, raw bulk operations, resource reads targeting RAM,
video callbacks, route peek/watch and end-state queries use the same view.
Invalid or unchecked indices fall back to this address view rather than index
outside a C object. Accessor timing remains independent of storage choice.
The generator emits only native accessors. The emulator read/write branches,
alternate per-variable backing and `CT_VARS_HOST` build switch were removed
when merging the native-only main branch.

The source inventories in [state-addresses.tsv](state-addresses.tsv),
[state-overlaps.tsv](state-overlaps.tsv) and
[state-field-overlaps.tsv](state-field-overlaps.tsv) describe address dependencies
and overlapping views; they are distinct from the typed-storage measure.

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

## Validation

The map tests check typed, blob and partial-record classification, narrow
buffers, composed ownership and page-instance arrays. Regression tests inspect
the five review examples' generated native fields directly. `vars_host_test`
checks byte views, all-address projection, partial writes, independent states,
page instances, unchecked tails, video sources and the real cartridge save/load
and checksum routines. The Windows MinGW Release build passes with warnings
treated as errors. All 211 Python tests and 15 native C tests pass. A generated
accessor sweep also passes 713,158 read/write comparisons against the serialized
address view. Generated-file checks confirm the committed outputs are current.

After merging native-only main, `tools/suite.py --ct-native build/main/ct_native.exe
-j 2` ends `suite OK` in 354.8 seconds, including unit tests. All 58 sampled
movies complete, recorded outcomes and picture checks pass, and both fast
routes reach their outcomes. Portstyle and portsmell pass without baseline or
exception changes.

The ownership-only results from the retired emulator gate are superseded by
the typed/blob/image measure above and the native gate.

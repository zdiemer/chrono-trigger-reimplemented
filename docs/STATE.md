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

## First slice selection

The location module owns four records: `location`, `location_previous`,
`location_stored`, `location_next` (20 declared bytes). Location logic already
uses typed accessors; these records have no aliases, unchecked accessors, page
instances or video sources. They cross module boundaries through those
accessors, with `core` as their map subsystem because all modes share them.
Generic memory operations remain compatibility clients, not additional owners.
Three records lie in the saved-game projection, while the next location does
not. This exercises persistence and the route driver's most useful address
without first rewriting any ports. Whole subsystems with scratch reuse are
deliberately deferred; TODO.md lists their prerequisites.

## Native structs and the compatibility view

The four records now have `host_module = "locations"` in the map. This names
their storage owner, independently of the broader subsystem used by the RAM
knowledge and port inventory. `ram_map.py accessors` generates `vars_host.h`
and `vars_host.c` alongside the existing outputs. `VarsHostLocations` has
ordinary `uint16_t` map IDs and `uint8_t` coordinates/flags. There is no packed
layout, pointer into WRAM, or second copy to synchronize. Arrays use element
indices, not the original stride; record padding is not state.

With `CT_VARS_HOST`, migrated accessors read/write these fields directly.
The generator rejects any declared overlap (including fields within a record),
reuse, unchecked table or page-instance variable selected for migration.
Array bounds fall back to the compatibility address path rather than indexing
outside a C object. Supporting page instances as independent owners waits for
their aliases to be resolved. Other builds still compile the original RAM
accessor bodies. No game logic changed.

The generated address view explicitly extracts/merges bytes of typed fields,
so C padding and host byte order do not define persistence. `state_host_read8`
and `state_host_write8` canonicalize WRAM mirrors, project migrated addresses,
and use the residual image only for everything else. The WRAM streaming port
uses these functions too. This is the API native route `peek`/`watch` should
call: reads are untimed, while game accessors charge the same per-byte amount
as before. Bulk helpers retain their existing timing and bank wrap rules.

Saves therefore read from and load into the struct fields through their
existing projection. The save code, SRAM file format, video callbacks and
end-state writer keep their interfaces. `vars_host_test` runs without a ROM:
it supplies save-layout metadata, loads a committed cartridge `.srm` through
the actual slot checker and loader, edits struct fields, saves them with the
actual checksum writer, and loads the exported bytes into a fresh state.
It also checks independent states, mirrored and partial writes, cross-field
copy/fill, the streaming port and a video transfer sourced from a moved field.

**Measure:** 2,263/2,263 (100%) before; **2,259/2,263 (99.82%) image-backed**
after, four struct-backed variables (20 serialized bytes). The residual
128 KiB allocation remains during migration; migrated addresses in it are
unused, not mirrors. This measures ownership moved, not memory saved. Deleting
the residual allocation requires all variables and unnamed address clients
to move. The overlap ledgers and TODO.md describe that remaining work.

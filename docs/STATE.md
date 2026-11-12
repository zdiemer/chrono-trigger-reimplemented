# Native state storage

Every live map variable has module-owned C storage, and 2,083 of the 2,236 have
typed storage; the other 153 are still blob-backed. `GameState` has no work-RAM
image; the generated address view serializes the owning fields directly. Generated
files come only from `tools/ram_map.py` and `re/ram.toml`.

## Storage measure

`python3 tools/ram_map.py storage` counts each public `[[var]]` once, regardless
of its fields, elements or page instances. A variable is typed only when each
value it names (a scalar, or each element of a buffer that declares its
`elements`) is exactly one typed value of its own width and place: one scalar
owner field, or one whole element of an owner buffer whose elements are that
width. Everything else is blob-backed: a byte buffer with no elements; a value
split across two owner fields or elements; a fragment of a wider one or a value
misaligned with it (a byte of a word field, a word over two byte fields, a byte
read out of half a word element); and a reader buffer that declares no elements
of its own, since a label on the owner does not type bytes nothing says the
meaning of. The rule holds for aliases too: a mode that reads memory the
original reuses at another mode's widths is blob-backed, though the bytes sit
in typed C fields. `storage --list` names each blob-backed variable and the
owner fields it depends on.

| Module | Blob-backed with shared owners | Blob-backed with overlays | Typed / total |
|---|---:|---:|---:|
| menu | 245 | 40 | 427 / 467 |
| battle | 234 | 30 | 892 / 922 |
| scene | 132 | 38 | 308 / 346 |
| field | 55 | 24 | 284 / 308 |
| menu_object | 23 | 6 | 33 / 39 |
| core | 12 | 5 | 57 / 62 |
| overworld | 12 | 2 | 40 / 42 |
| warp | 7 | 5 | 17 / 22 |
| event | 5 | 3 | 17 / 20 |
| all | 725 | 153 | 2,083 / 2,236 |

The first column is the map before overlays (every mode reading another's
owner fields at its own widths); the second is after them, by the same rule.
632 owners now keep their own fields beside other modes' (see "Overlays");
four tilemap and palette buffers among them declare their word elements. The
153 left are, by `storage --list`:

- 57 byte buffers with storage of their own and no element structure (sprite
  tables, line-effect tables, tile graphics, scratch runs). A byte label would
  not type them; records or word elements would, where the meaning has them.
- 48 views of their own mode's owners at another width (a word and its high
  byte, a record field read as two). Splitting them changes the ports' reads.
- 30 variables that share another mode's bytes at the same time or hand data
  across a mode change, kept as `host_alias` views (examples under "Overlays").
- 11 owners with a field read at two widths inside the record, and 7 records
  with a byte-buffer field.

The denominator fell by 27 because the retired sound loader has no live clients.
Its declarations and generated accessors were deleted. Its 257 bytes and 1,138
other unused/padding bytes have no storage. Of the former 1,450 unnamed bytes,
312 remain as named cartridge-save reserved runs, required for lossless `.srm`
round trips. A sampled-movie audit with the old fallback instrumented found no
gameplay reads of the 1,395 dropped bytes; whole-address inspection in native
tests and route snapshots did read them. The image-free gate verifies the
observable results after their removal. Unowned inspection addresses return
zero and bulk writes across them are ignored.

## Owners, elements and records

`host_module` selects an owner. `host_alias` names one owner or a list composing
several owners. `[[owner]]` declares storage without adding public accessors or
changing the measure. Internal field views can name several owning fields.
Every declared byte has exactly one owner; missing coverage, alias chains and
duplicate ownership are rejected.

A serialized byte API need not imply byte storage. `elements` declares its
actual element name, width and meaning. Tilemap entries, palette colors, scene
interpreter registers and loop counters are word arrays; glyph codes, map tile
identifiers, collision flags, graphics bitplane rows and encoded script units
are byte-sized elements. Script buffers retain the encoded opcode/operand
stream, not decoded C instructions. Those buffers belong to their modules.
Opaque storage without an element declaration still counts as blob-backed;
`buffer = true` preserves a byte accessor even for a narrow serialized slice.

Overlapping modes share one owner unless the later declares `overlays`: then
it has fields of its own beside the owners it names (see "Overlays").
The projection performs explicit
little-endian serialization, independent of C padding. A slice can start inside
an element when another owner holds its other byte; its `elements.start` says
which byte, and the projection exposes only the declared slice. Mixed records
have named scalar fields: for example, character records now own base stats,
level, experience, equipment and derived figures alongside HP/MP. Whole-record
accessors serialize those same fields, including partial writes.

`[[record]]` gathers selected owners into a named C struct and array. Its member
bindings and optional flattened fields determine the C layout independently of
the original address spacing. The generator checks member counts, unique field
names and exclusive ownership. Accessor names and parameters remain unchanged.

The authored record declarations currently emit:

| C record | Native owner | Elements |
|---|---|---:|
| `FieldObjectState` | `field.objects` | 64 |
| `BattleDrawObject` | `battle.draw_objects` | 23 |
| `Combatant` | `battle.combatants` | 11 |
| `Character` | `core.characters` | 7 |
| `MenuTask` | `menu.tasks` | 8 |
| `MenuWindow` | `menu.window` | 1 |
| `SceneSpriteState` | `core.sprites` | 128 |

The 23 drawing objects include effect objects; the 11 combatants are the party
and enemy slots with stats. Their different counts are deliberate. A character
combines its stats, equipment, name, learned techs and remembered menu cursor.
A menu window also owns its twelve line values as an array member.

For example, field coordinates, role, movement and animation were spread over
parallel arrays and overlapping menu fragments. They now live together in each
`FieldObjectState`. `field_object_x_tile(g, slot)` reads that object's `x.tile`.
The menu record's three-byte graphics pointer is an alternate serialized view
that can span two objects' coordinate fields. Partial writes use the same
projection, and the native regression test checks the fields themselves. The
obsolete menu/channel storage fragments and their generated byte helpers were
removed as those bytes acquired their field-object owner. Smaller record
families still to gather are listed in TODO.md.

## Overlays

The original reuses memory from one mode to the next because the SNES had
little of it; the reimplementation need not. An owner that declares
`overlays` (re/ram.toml's schema) keeps its own typed fields beside the owners
it names, and its accessors read and write only those. What still reaches the
memory by address keeps the original's one byte: the address view writes a
shared byte to every owner of it and answers a read from the owner that wrote
it last (`VarsHost.overlay_writer`, a placement number per shared byte, 0 when
every owner holds the same value). Cartridge saves, route peeks and video
sources therefore see what the original would.

Separate storage changes behaviour exactly where a mode reads bytes another
mode wrote last: the original answered with that mode's value, the separated
owner answers with its own. Every owner accessor over shared bytes checks
this, and `ct_native` prints `overlay crossed: <reader> read bytes <writer>
wrote, N time(s)` at exit. The line is a runtime fault (tools/outcomes.py), so
a sampled or library movie that crosses fails the gate. A variable whose mode
really reads another's bytes is not separated: it keeps a `host_alias` view of
the owner whose bytes it reads, which names the hand-off.

The cross-mode views became overlays by mechanical conversion (`host_alias`
to `host_module` plus `overlays`; a record's field read at two widths became a
view of the wider), except five scratch records whose fields cross each
other's bounds. Over the sampled movies 31 of them then crossed, and each went
back to a view:

- The same cell under two names. `battle_joypad` is the core's held buttons
  (`input_mapped[1]`); `menu_member_pointed` and `menu_tech_list_shown_before`,
  `menu_save_slot_era` and `menu_description_item`, `battle_turn_member` and
  `battle_acting_for_itself`, `battle_effect_sprite_attributes` and
  `battle_step_member` are one byte the two screens or routines both use. Each
  first name is now an alias of the second, which types it.
- Data handed across a mode change: the title leaves how it was left in event
  memory for the scripts (`event_memory` over `scene_title_left_with_a_button`),
  a steal's item reaches the scripts as event variable 0's low byte
  (`battle_message_item`), and the warp's pixel masks stay as palette
  animation parameters (`warp_pixel_clear_masks`, `warp_pixel_set_masks`).
- Deliberate overruns: the menus copy the three bytes past the names, which are
  the party's gold (`menu_past_the_names`); the rewards' level and tech marks
  share a byte.
- Modes running together over the same memory: the menus' sprite nodes over the
  field's tile looks, the field's layer maps under the battle's decoded tiles
  and the load screen's tilemap, the menus' top-layer wave over battle effect
  and field attribute cells, the field's line waves over the battle's drawing
  page, and within one mode the record coordinate and tile words read as bytes.

96 `[[owner]]` fragments that only gave the views storage (field map rows cut
around battle records, menu object remainders) had no reader left and were
deleted; the overlays at their bytes name each other instead.

## What still uses addresses

- Cartridge `.srm` projection retains the original eight-run layout below.
- Route `peek`/`watch`, end-state outcomes and replay checksums inspect the same
  address view to preserve the tools' vocabulary and recorded checks.
- Dynamic script destinations, bulk clears/copies, video source callbacks and
  deliberate unchecked indices still select serialized positions. They resolve
  through the generated projection; none indexes a work-RAM allocation.
- Legacy entry/page adapters still translate original calling conventions.
  Removing those adapters is separate from storage ownership.

The address tables are immutable dispatch metadata, not another copy of state.
Source and overlap inventories in [state-addresses.tsv](state-addresses.tsv),
[state-overlaps.tsv](state-overlaps.tsv) and
[state-field-overlaps.tsv](state-field-overlaps.tsv) describe remaining address
users and alternate views; they are not the storage measure.

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

Saving and loading use the generated address view through `wram_read8/write8`.
Five reserved runs (312 bytes) have explicit cartridge-format owners so loading
and saving preserves values the game does not interpret, as well as the slot
tail already retained in SRAM. The native test checks those reserved runs
against an actual cartridge save after loading, editing and saving it.

## Validation

Map tests cover typed elements, sliced words, records, aliases, overlays and
their linking rules, page instances, invalid declarations and generated-file
freshness. `vars_host_test` fills all
owned addresses before reading them back, verifies that all 1,395 unowned
addresses discard writes, and exercises aliases, partial writes, independent
states, unchecked tails and video sources. Record tests mutate the native
structs directly and verify both accessors and cross-mode address aliases; an
overlay test checks that an owner's write leaves the other mode's fields alone,
that the address view answers with the last writer, that an address write
reaches every owner and that a read of another owner's bytes is counted. The
test invokes the actual cartridge save/load/checksum routines on a committed `.srm`, edits typed state, saves,
checks reserved bytes and loads the result into a fresh state.

The Windows MinGW Release build uses warnings as errors. The lean gate runs
Python/native tests, 58 sampled movies, saved outcomes, picture fingerprints,
both fast routes, portstyle and portsmell. No baseline or exception changes.

With the overlays, the lean gate ends `suite OK` in 354.9 seconds (movies 160.8
seconds, against 149.7 before them), with no `overlay crossed:` line.

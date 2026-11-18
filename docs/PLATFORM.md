# Native platform services

Game code calls video, audio, timing, input and save interfaces in
`runtime/platform/`. Their implementations in `runtime/platform/native/`
are linked into ct_native. The frontend owns host events and files. Services
express tiles, palettes, priority and window logic; game code does not write
hardware registers.

## Video

`video.h` defines the scene state, uploads, sprites and line effects. Calls
update a `VidObserver`; the renderer consumes that state and owns its framebuffer
between frames. VRAM remains a 64 KiB tile store, with palette and sprite memory
and per-line effects. The native renderer implements background modes, priority,
windows and color math. Dirty tracking avoids rebuilding unchanged material.
No emulated PPU is used, and no call names a register: the SNES-only escapes
(a raw copy and a register write) and the older forms that took the SNES's
write order (an upload per transfer, a transfer record) are gone. Two transfer
descriptions the game keeps in its state still hold the VRAM data port's low
address byte as the original stored it (issue #206).

## Audio

The C sound program receives game commands through the audio service. Voices
supply sample playback, envelopes, pitch, noise and echo to the native mixer.
The SDL driver pulls buffered output; game code never polls an audio device.
A snapshot keeps the whole sound driver and mixer (docs/PLAYING.md,
"Snapshots"). See SOUND.md.

No register write reaches the mixer. The driver's command `$FD`, which sets
one setting by its S-DSP register number, is decoded in the driver
(`game/sound/driver.c`, `set_by_hand`) into the voice calls that mean it; a
write to part of a setting reads what the voices are set to
(`au_voice_settings`, `au_mix_settings`) and sets the whole. The mixer's
switches became separate calls (`au_output_muted`, `au_voices_reset`) and the
echo's buffer can be placed by hand (`au_echo_buffer`), as the command can.
`sound_setting_test` checks each setting against the mixer.

## Timing without a beam

The game advances in frames. Its main thread and menu tasks use the coroutine
interface in `runtime/port/fiber.h`; the host resumes work at frame boundaries.
Native work charges accumulate debt, with fractional carry and excluded reads
handled by the state host. Existing rates in `runtime/port/time_rates.c` are
authored estimates, changed only with evidence from native play (the battle's
were calibrated against the original's clocks, REIMPLEMENTATION.md). Each lag
frame gives the main thread the time that frame's own handlers leave it. A
handler's transfers and its waits for a line (`tm_wait_line`) are the
handlers' time, which the main thread then doesn't have; a wait for a line
from the main thread charges nothing.
The timing, native-time, task and unpack-time tests exercise this accounting
without a ROM. Core owns typed mode callbacks in game state; the frame driver
dispatches them in the established order. The program test checks callback
isolation, replacement and mode hand-offs. Removing a state read preserves its
counted work explicitly where pacing depended on it (REIMPLEMENTATION.md).
A long stretch of the main thread's work with the frame's handlers off --
a location, the world map or the race loading behind a dark screen --
says between its steps where the SNES's frames went by in the middle of it
(`tm_let_frames_pass`). The time model keeps those frames at the work's
end; a host keeping time of its own (`TmHostProgram.passes`, the GBA played
by hand) may let them go by there, its sound and picture going on through
the load. Nothing of the game's runs in them.

## Input, saves and resources

Input arrives as controller words each frame. Persistence transfers bytes under
names; the frontend supplies files and handles errors. Cartridge saves retain
their slot/checksum format, tested through the real game routines.

`re/assets.tsv` is the resource catalogue and `re/asset_views.toml` defines typed
views. `tools/assets.py generate` emits the committed lookup table and the
resources' names (`game/data/asset_names.h`). On first run, the frontend
verifies the user's cartridge and imports a local pack. Game code loads named
resources from that store, never an embedded ROM. Packs and decoded assets are
not committed.

A pack is held to a reference made apart from the import: `tools/assets.py crc
ROM` records the CRC-32 of every resource's bytes, one after another in the
catalogue's order, read from the ROM file by the catalogue's own addresses
(`# bytes crc32` in `re/assets.tsv`, `asset_catalogue_bytes_crc32`), and
`tools/assets.py check` holds the record to the catalogue where the ROM is
staged. The import refuses a pack whose resources don't come to it, and
`ct_native` imports again over a pack on disk that doesn't
(`asset_pack_is_the_cartridges`); a pack remade from edited text isn't held
to it. `assets_test` checks both ways with the ROM staged.

The game reads its resources through a source that answers by the
catalogue's row (`AssetSource`, `assets_use`): a pack's directory is in the
catalogue's order, so a row is its entry, and the game keeps no table of its
own per resource (a target keeps the pack's directory in its ROM). The
battle's animation commands' handlers, which name their timing, are a table
in `battle/scripts.c` rather than a resource read at run time.

An accessor reads its resource by name (`game/rom.h`): `asset_read8(g,
ASSET_LOCATION_RECORDS, place + 3)`, a place in the resource, which the
store finds by the resource's name, so a pack may keep it anywhere; an
offset past the resource's end reads on by address, as the original's
indexing does. Where the data gives a 16-bit place (a table of places, a
record's link, a script's offset), `asset_read8_at_place` reads that place
in the named resource, and a place outside it that place of the resource's
bank. 582 of the game's reads (call sites) name their resource. No routine
names a cartridge address: where the game keeps a pointer to a resource (a
table of strings it hands the text code, a script it starts, a stream it
unpacks, a bank it leaves in a cell or compares), it takes it from
`asset_address` or `asset_place_address` of the named resource
(`menu_time_dial_script`, `field_sprite_graphics_first_bank`), and a run of
colours or tiles from several resources is a table of resource and offset
(`menu_rom_colour_past`). What the data's own pointers lead to (a stream a
table points to, a script's text, a sprite's graphics and frames) is still
read by the address the original keeps it at, 38 call sites, the address a
key the catalogue's rows are looked up by; 11 more read a place the data
gives in a bank of tables that link to one another, in a named resource's
bank (`rom_read8_in_bank_of`: the equipment's records, the menus' upload
records, the sprites' tables). A pack can't move those resources until it
moves the pointers to them too. `docs/state-addresses.tsv` counts the ROM
reads in `game/` as `resource-address` rows: 185 before the routines'
addresses were named, 87 after. The scene program's resources it copies
as they are (the scenes' colours, the planet's order of tiles) are read by
their number in its table at `$FE:0000` through a list of the rows they lie
in (`SCENE_RESOURCE_ROWS`, `game/scene/scene_data.h`); the streams it
unpacks are found where the table, read by name, points, and a number no
scene copies is read where the table says in the table's own bank, so no
scene code names bank `$FE`. A pointer in the data may name RAM (a
script or text the game copied or built in work RAM, `$7E:2C35`,
`$7F:8020`): those reads are the game's RAM's (`wram_data_read8`,
`game/wram.h`), sent there by `rom_read8` before any cartridge store is
asked, at the time a cartridge read takes, so a backing's cartridge read
answers only the cartridge (`runtime/port/rom_host.c`); `assets_test` holds
a work-RAM and a low-RAM read to it. A region the catalogue hasn't
typed is cut where each accessor's table starts, the piece named for its
accessor, so a read names its own table rather than the region it shares;
then typed as its readers show it: `bytes` where every read of it is a
byte, `words` where every read is a word (139 rows), and a table of places
at its start `ptr16` up to the first place it names, what follows its own
row (`screen_script_start` and the screen scripts, `battlefield_strip_place`
and the strips, the demo's actions, the windows' strings). The regions that held several tables were then split where their readers
divide them, each table a row of its own type: bank `$CC`'s and `$CD`'s
battle records (the characters' start, the equipment and its boosts, the
techs, the enemies' attacks and their effects and animations), the battle's
messages and fades, the menus' colours, tiles and window designs, the sound's
songs' settings, boot blocks and sets of effects, the scripts the map view,
the time dial and the Epoch run. Left `mixed`, `untyped` or `undetermined`:
41 rows, 12,657 bytes, every one `unread` in its row's `found`: no accessor
names it, and no read of the movies or the routes lands in it (mixed was 36
rows, 117,842 bytes). `tools/assets.py check` fails a row of those types
that doesn't say it is `unread`. A read that lands in one at run time is
counted the way a read outside the catalogue is (`asset_unread_reads`, the
first resources it read kept by name), and `ct_native` prints a `strays:`
line for it, which fails the run in the gate and is a finding in
`suite.py --long`. The first long batch found eight such readers, now
typed: the accessories' data records the menus copy (`accessory_data_records`,
$CC:0A1C), an enemy's message before its table's first entry
(`enemy_messages_cd91`), a string past the table at $F6:B230
(`strings_f6c7e5`), a palette the menus copy in a playthrough leg
(`menu_colors_ce50`), tiles the gate screen sends after its swirl's stream
(`menu_gate_screen_tiles`), and three tables the routes read further than the movies had
(`battle_palette_frames`, `battle_effect_frames`, `combination_tech_asks`).

### Palettes, the first decoded resource

A catalogue row whose codec is `palette:N` (or `palette:NxM`) is BGR555
colors, N a row and M rows a palette: the field's location palettes
(`palette:15x7`, 88 of them) and sprite palettes (`palette:12`), the battle
sprites' three- and seven-color sets, the battle screen's effect, gradient and
background colors, and the party's color cycles; the menus' colors and the
portraits' (`menu_colors`, `$FF:CB9C`), the windows' designs' (`$FF:B210`),
the battle's cursor, number, flash, member and background-set colors
(`$D1:38F0`, `$D1:4D80`, `$D1:4D90`, `$D1:4B00`, `$D1:6000`), the world map
view's and its last sprite palette (`$C6:F540`, `$C6:F620`), the palette
animations' frames (`palette_animation_frames`, `$F6:6CF0`, read through
`palette_frame_color`; zero from `$F6:7B80` to the bank's end) and the
attract sequence's colors copied as they are (`$FE:4B7A`, `$FE:8D15`,
`$FE:F91E`). Each was split out of the mixed or untyped region that held it,
its extent where the accessor's indexing and the data agree (every word
below `$8000`; the cycle's `$FFFF` tail is its own padding row). `tools/assets.py
check` holds a palette row to whole words.

`frontend/palettes.c` writes every palette resource a store serves as text, a
color a line (`red green blue`, 0-31, and `+` where the word's unused top bit
is set, so any word survives), under a `[name]` line and row comments, and
reads such text back into a store over the pack. The text is from the user's
cartridge and, like the pack, never committed.

Measured: `palettes_test` (ROM-free) writes and reads back a made-up store's
six palettes, serves an edited color and only the palettes a text names, and
refuses wrong text at its line; with `roms/ct-usa.sfc` staged it reads back
every cartridge palette as its bytes. The lean gate runs every movie, route
and snapshot on the decoded palettes, since each run imports its pack and
text. Swapping red and blue in the field's palettes shows in the first field
scene's picture.

### Decoded resources

What a decoder needs besides its own format is shared, in
`frontend/decoded.c`: a kind of decoded resource (`DecodedKind`) says which
rows of the catalogue it decodes, how one is written as lines and how a line
is read back, and the shared code writes the text (the kind's header, then
each resource under `[name]`), reads it with comments and blank lines dropped
and refuses wrong text at its line, serves what it read over the store
beneath, and checks that every resource written and read again gives back its
bytes. `ct_native` keeps each kind's text beside the pack (`<pack>.palettes`,
`<pack>.fonts`, `<pack>.graphics`, `<pack>.maps`, `<pack>.scripts`, `<pack>.commands`, `<pack>.text`,
`<pack>.records`): where there is none it checks the round trip, writes the file
(create-only, so two players starting on one pack don't overwrite each
other), and on every start stacks each kind's store over the one before and
remakes the pack in memory from the top, so the game's existing accessors
read the decoded resources with `game/` unchanged. A text it can't read or
write leaves the pack's own bytes to play, after a warning, and so does a
text written from other rows of the catalogue than its kind decodes now
(its opening comment stamps them, `# rows XXXXXXXX`, a hash of those rows'
names, types, codecs and lengths, and for scripts of the event table's
rows the commands are written in), whose names and forms may no longer be
these; the warning says the file's edits aren't applied and to move it
away, have it written afresh and make them again. A change to other rows
leaves a kind's text and its edits good. A text that is
wrong stops the start, naming its line (a resource that is wrong as a whole,
too short or too big to pack, at its `[name]` line).

### Fonts

A catalogue row whose codec is `font:8x8` or `font:12x12` is glyphs of two
bitplanes, and `font-widths:FONT:XX` the widths of FONT's glyphs from `$XX`
on, a byte each. Four fonts were split out of the mixed regions that held
them, each where its accessor's indexing and the data agree:

- `dialog_font` (`$FF:2060`, 256 glyphs, `font:12x12`): the proportional font
  the dialog, the menus' text and the overworld's signs set with
  (`game/menu/glyphs.c`). Each glyph's left eight pixels are 24 bytes of its
  own from `$FF:2060`; their right four share a byte with the next glyph's,
  24 bytes a pair from `$FF:3860`. Glyphs `$00-$9F` are blank: the text's
  glyphs start at `$A0`. Its widths are `dialog_font_widths` (`$C2:60E6`,
  glyphs `$A0-$FF`), split from the menu's table of name places.
- `small_font` (`$FF:8E60`, 224 glyphs, `font:8x8`): the menus' four-color
  tiles, the 8x8 letters, digits and kana with the icons, window marks and
  cursors among them; and `small_font_replacements` (`$FF:C9FC`, 16), the
  tiles the menus put over its second sixteen.
- `battle_number_font` (`$D1:4C80`, 16 glyphs, `font:8x8`): the battle's
  damage and healing numbers and MISS.

`frontend/fonts.c` writes a glyph a block: `glyph XX` (its number in hex) and
`width N` where its font has one, then a line of characters for each of its
lines, `.` for color 0 and `1`-`3` for the rest, so a person edits a glyph by
drawing it. A font is read back whole, its glyphs in order, and its widths
with it. `tools/assets.py check` holds a font row to whole glyphs and a
widths row to its font's glyphs. `tools/sheets.py` draws each font of a
text as a picture of its glyphs under their numbers, written where it is
told, never into the repository. The credits' letters (`$FD:0000`) are glyphs
too but packed, so they are graphics (below).

Measured: `fonts_test` (ROM-free) writes and reads back made-up glyphs of
every font and their widths, serves an edited pixel and width and only the
fonts a text names, and refuses wrong text at its line (a pixel that isn't
one, a short line, a glyph out of order, a width where there is none or none
where there is one, a font cut short); with the ROM staged every font reads
back as its bytes. The lean gate runs on the decoded fonts. Drawing a box
over the dialog font's `e` and a checkerboard over the small font's `A` shows
both in the Battle Mode screen of a new game (frame 2000 of
`movies/new-game.ctm`).

### Graphics

A catalogue row whose codec is `tiles:2bpp` or `tiles:4bpp` is 8x8 tiles of
two or four bitplanes as the SNES keeps them, and `lz:2bpp` or `lz:4bpp` the
same packed, which the game unpacks as it loads them
(`game/data/decompress.c`). The packed graphics are the field's tile blocks
(`graphics_block_address.N`, from `$F6:2220`: blocks 0-18 two planes, since
the locations' records load them into BG3, the rest four), the characters'
sprites (`setup_character_graphics.N`), the world maps' tiles
(`overworld_map_graphics.N`, the top layer's two planes), the menus' scenes'
(`overworld_sprite_graphics.N`) and the credits' letters
(`field_credits_letters_packed`). The tiles kept as they are are the
battlefield's two-plane sheet (`battlefield_tiles`, `$CF:0000`, split from
the descriptors and palettes after it) and the sprites' four banks
(`sprite_tiles_d2` to `_d5`). `tools/assets.py check` holds a tiles row to
whole tiles and a packed row to the `lz` type.

`frontend/lz.c` unpacks a stream and packs one. Okumura's LZSS (1989) with
its binary-tree search, a 4096-byte ring and runs of 3-18 bytes, or 2048
and 3-34 for a stream with long runs, is what packed the cartridge: with
two of its packer's habits kept (a control byte's unused bits set; the
bytes ahead of the last items 0 in a stream exactly a ring long) every one
of the cartridge's 1371 packed streams, scripts and maps as well as
graphics, packs again to its own bytes. So no packed resource needs its
original bytes kept beside the text.

`frontend/tiles.c` writes a resource's tiles sixteen a row, a character a
pixel (`.` for color 0, then `1`-`3`, or `1`-`9` and `A`-`F`), a space
between tiles and each row's first tile's number in a comment, so a sprite
or a block reads as the picture it is; a packed resource says `packed
short` or `packed long` first. Text was chosen over an indexed PNG because
a tile has no palette of its own (the tilemap or the sprite picks one as
the game draws it), because it round-trips exactly through the reader the
other kinds share with no image decoder in the host, and because the
palettes and fonts are already edited this way. Read back, a packed
resource whose tiles are the store's beneath, unpacked, is served as that
store has it, which is what packing it again gives, only sooner; one that
was edited is packed again and must fit the cartridge's room for it, and
have no more tiles than the stream beneath unpacks to, since the game
unpacks it into a buffer made for those; otherwise the text is refused. `tools/sheets.py` draws a graphics text's
resources as it draws fonts, a grey ramp for four planes, refusing a name
that isn't the catalogue's kind (so no sheet lands outside its directory)
and pixels before a font's first glyph, at their line.

Measured: `lz_test` (ROM-free) packs and unpacks made-up bytes of every
size and kind, short runs and long, and refuses streams that aren't one;
with the ROM staged it packs every packed stream of the catalogue again to
its bytes. `tiles_test` (ROM-free) writes and reads back made-up tiles,
packed and not, serves an edited pixel of each (the packed one packed
again; of eight planes, in every plane's bit, and of a chunky tile, in its
byte's low nibble) and only the resources a text names, and refuses wrong text at its
line (a pixel that isn't one, a tile short a line or a pixel, a short row
that isn't the last, how it packs unsaid, too few tiles, more tiles
than the stream beneath, an edit that doesn't fit); with the ROM staged every graphics resource reads back as
its bytes. The lean gate runs on the decoded graphics. Filling the first
tile of the field's block 135 with color 15 darkens the walls and floor of
Crono's room at frame 6000 of `movies/new-game.ctm`, the block packed
again.

The attract sequence's resources (`attract_resource_ptrs.N`, the table at
`$FE:0000`) are typed by what the scene code does with each
(`game/scene/`): sprite and layer tiles `lz:4bpp` and `lz:2bpp`, tilemaps
`lz:tilemap:32`, palettes `lz:colors:16`, mode 7's byte-a-pixel tiles
`lz:chunky` and its maps `lz:bytes:64`, and the race's, the Epoch's and the
endings' byte-a-pixel pictures `lz:bytes:64` (64 a row, as the scaler reads
them). The planet's tiles of eight bit planes (`.54`, `lz:8bpp`) are
written two hex digits a pixel (`..` for color 0), and the two pictures of
Mode 7 tiles of four bits a pixel, packed two to a byte for the scene to
spread (`.15`, Lavos's, `game/scene/lavos_falls.c`; and `.46`), are
`lz:4bpp-chunky`, a hex digit a pixel, the left one the byte's low nibble.
Still bytes in hex, having no form of their own: the dive panel's
attribute bytes (`.34`) and the mosaic's tile list (`.45`); `.0`, the
program (`.1`) and `.50` no port reads.

The battlefield's pool (`battlefield_tile_pool`, `$D0:0000`,
`tiles:plane-pool`) is tiles of two bit planes (16 bytes) or three (the
third a byte a line after the first two, 24), each starting where one of
the descriptors at `$CF:A000` says (`game/battle/tiles.c`). The cartridge's
descriptors cut it exactly, 3203 tiles end to end with none overlapping and
320 bytes after the last that no tile is cut from. `frontend/tiles.c` writes
the pool's tiles sixteen a row as the other graphics, a three-plane tile's
pixels `.` and `1`-`7`, and the bytes after them as `rest` lines in hex; it
reads them back cut by the descriptors the store under the text has, so an
edited tile keeps its planes and place. `tools/sheets.py` draws it in eight
greys.

### Maps and tilemaps

The other packed resources are the maps' kind, `frontend/maps.c`, kept as
`<pack>.maps`. Their codecs say what a cell of the unpacked grid is:
`lz:field-map` a location's map (the 201 of `field_packed_map.N`, N the
map's number: `$F5:0000`, once taken for the chests' table, is map 165);
`lz:looks` a tile set's looks, a 16x16 tile's four tilemap entries two by
two (the field's 64 sets and BG3's 19, and the world maps' tile groups);
`lz:tilemap:32` tilemap entries (the world maps' top layer, the battle
effects' layer); `lz:bytes:96` the world maps' planes, a byte a map tile;
`lz:nibbles:96` their regions and `lz:nibbles:4` their ground kinds, a hex
digit a cell; `lz:colors:16` the world maps' packed palettes
(`overworld_palettes.N`); and `lz` for the three attract streams
whose layout no reader has shown yet, in hex. A
tilemap entry is `TTT:P:hvp` (the tile, its palette, and each flag or `-`),
so a map is edited by its tile numbers, flips, priority and palettes.

A location's map is written as the game reads it (`game/field/map.c`): its
header as named lines (each layer's size, BG3's `none`, `half`, `still`,
the drifts, and the screens' and color math's bytes), each layer's map a
byte a 16x16 tile, and then a cell of three bytes of attributes a tile.
Read back, the attributes are packed into runs again; packing every run as
long as it goes, up to 256, is what the cartridge did, so all 201 maps come
back as their bytes. Five maps' attributes stop short of BG1's tiles (maps
29, 58, 98, 191 and 193): the game reads the rest from what follows the
map, and the text keeps them short. A grid whose bytes don't come out in
whole cells ends with a `rest` line (a tile set's look of one byte). Each
resource packs again as the graphics do (`frontend/packed.c`, which both
kinds share): an edited one must fit its slot and unpack to no more than
the stream beneath, except a location's map, which may grow into the
buffer every map is unpacked into, from `$7E:B500` to the end of the bank
(the cartridge's biggest, map 107, reaches `$7E:FE20`). `tools/sheets.py`
draws a location's map assembled, as the field shows it
(`game/field/streaming.c`): BG2 under BG1, each map tile its look's four
entries -- a look of the tile set's first 256, or of its second where the
tile's first attribute byte says so for that layer (bit 0 BG1, bit 1 BG2)
-- each entry the tile set's tile (its six blocks from tile `$100`) in the
location's palette, flipped and in front as it says; the location's record,
its set's blocks and the pointers come from the pack, the rest from the
texts. A world map (`--location` from `$1F0`) is drawn the same way from its
two planes, 96 by 64 map tiles, each byte one of its plane's 256 tile groups
(`game/menu/map.c`), with the seven blocks of tiles, the groups, the planes
and the palette its record names. It draws a tilemap with a graphics
resource's tiles the same way.

Measured: `maps_test` (ROM-free) writes and reads back made-up resources
of every codec, serves an edited map tile, attributes, tilemap entry, look,
region and color (the map's edited attributes leaving their run), reads a
map whole from text and only the resources a text names, and refuses wrong
text at its line (a cell that isn't one, a short row not last, a header
line out of order, a layer's row short of its width, too many tiles'
attributes, a map too big for its slot); with the ROM staged every map of
the cartridge reads back as its bytes. The lean gate runs on the decoded
maps. Clearing eight rows of BG1 in map 60 changes Crono's house at frame
6000 of `movies/new-game.ctm`, the map packed again.

A round trip says a text keeps every byte, not that its fields are the
game's. `tests/frontend/map_pictures.py` (a ctest, with the ROM staged)
says that: at six frames of the gate's movies -- Crono's room and the floor
below it, the 1000 A.D. world map, Leene Square and two rooms of Heckran's
Cave -- it draws BG1, BG2 and the two together from the texts as
`tools/sheets.py` does, placed where the game has scrolled them, and
compares them pixel for pixel with ct_native's picture of the same layers
alone (the route protocol's `layershot`: no sprites, windows or color
math, at full brightness, with the frame's colors and scrolls), in the
colors the game has loaded; all 18 pictures are the same. The frames are
ones where nothing has changed the map since it loaded: an event's copied
tiles, an animated tile or a dialog window drawn into BG1 differ there, as
the game means them to.

### Scripts

The locations' event scripts (`lz:event`, the 462 `field_packed_script.N`)
and the world maps' lists of places (`lz:place-lists`,
`overworld_place_lists.N`) are the scripts kind, `frontend/scripts.c`, kept
as `<pack>.scripts`. A script is written as the event engine runs it
(`game/event/engine.c`), from the language's own table
(`game/event/script.h`): its objects' count and each object's sixteen
functions (`functions 00 L0060 ...`), then a command a line, its
operation's name (the table's `OPERATION`, or the command's own for a row
that reads its operands) and each parameter the row decodes, `name=value`,
the value as many hex digits as its operand has bytes and marked with what
it names (`var:`, `var16:`, `obj:`, `member:`, `story:`, `bit:`; `self`,
`progress`), so the text can't say anything the port doesn't decode:

    L0280:
    peek width=00 address=7F0000 variable=var:06
    if_story offset=5C value=01 comparison=80 to=L0297
    say window=00 line=12

A command whose length its operands decide (the colour effects, the tint's
short form, the screen commands, the data a copy carries) has its row's
`MEASURED` length, the same function the engine's operations take it from.
The names and measures are the text's alone: a build without it (the
65816's) leaves them out of the table with `CT_SCRIPT_TEXT 0`
(`game/script_text.h`), which `ct_languages_without_text` compiles every
build, the battle's and the menus' languages with it.
Bits of an operand byte no parameter reads would be kept as byteN=XX (N
its operand) and a command's own data after its operands as
`data=XX.XX...`; the places a
function or a jump names are labels (`L0297:`), so commands may be added
or taken away, a jump still reaching its place within a byte's distance;
and bytes no command could be read from would be `bytes`. Read back, a
line is the shortest of its name's rows its parameters fit, a jump to a
place already named going back; where the cartridge used another (a long
form whose value a short form has, `wait_frames frames=08` as `$AD 08`
rather than `$BA`), the line keeps it as `@AD`. Every byte of the
cartridge's scripts is read as a command; 6,800 of their 190,000 lines carry
an opcode, and one jump lands where no command starts (`to=+06`).

A command that says a line (`say`, `ask`) shows it beside the command as a
comment, which reading leaves alone: the line of the table of strings the
script last chose (`use_text`, followed in the script's order), as the
pack's text spells it, cut at 95 bytes:

    say window=00 line=13   # "LARA: Oh no…! Help!!"

5,257 of the cartridge's commands show their line.

The commands the field keeps whole to copy into a script
(`built_in_command_byte`, `script:event-commands`, 55 bytes) are written as
a script of no objects, `objects 0` and the commands, and keep their
length, since the field copies them by place.

A world map's places are its lists, each a record a line in the game's
order and layout (`overworld_place_streams`, `game/overworld/places.h`, from
the work-RAM map's fields, which the map's load and the places' lookups go
by too): `exit` (column, row, name, location, x, y), `event`, `halt`,
`script`, then the bytes after the lists as `rest`;
each list's count is its records', and a list with none is refused, since
the game reads a count of 0 as 65,536.

An edited resource is packed again and must fit its slot, as the other
packed kinds; a script may unpack to as much as the game's buffer for it
(`$1700` bytes), which is as long as a command made from its line may be:
its bytes are made in a scratch the reading allocates, not on the stack.
A jump's distance is a byte inside its command; the table has no row whose
jump a measure could move outside it, and the text reads none there. Packed slots are exact, so an edit that packs no smaller
rarely fits until a pack can give a resource more room (issue #128).

Measured: `scripts_test` (ROM-free) writes and reads back a made-up script
and places, serves an edited script (a command put in before a condition,
moving its jump's place and a function's on; a parameter changed) and
edited places, only what a text names, and refuses wrong text at its line
(a parameter no form of the command has, a command that isn't one, a place
not named or named twice, a jump out of reach, an opcode not the command's,
a short table, an empty list, lists out of order); with the ROM staged every
script and list of places of the cartridge reads back as its bytes. It also
checks the table's jumps lie inside their commands and the places' fields
are bytes and words filling their records. The
lean gate runs every movie on the decoded scripts.

### Commands

The battle's animation scripts (`script:battle-animation`, four resources),
the menus' object scripts (`script:object`, three), the world maps' scenes'
object scripts (`lz:object`, the eight `scene_script_ptrs.N`, packed) and
the enemies' scripts (`script:enemy-ai`) are the commands kind,
`frontend/commands.c`, kept as `<pack>.commands`. The first three are
written from their interpreters' own tables as the event scripts are: the
battle's (`battle/script_language.h`) and the menus' (`menu/script.h`)
languages share their C names, so `frontend/battle_language.c` and
`frontend/menu_language.c` each read one table into a common description
(`frontend/languages.h`), a row an opcode with its operation's name (the
table's `OPERATION`, or `NAMED` for the battle's sound rows, which have no
operation) and its parameters: operand bytes, words and addresses (`slot:`
before a byte of the running object's, `cell:` before a cell of low RAM),
and the ones the opcode fixes, which a line gives too and reading matches:

    wait_frames frames=08
    play_sound voices=18 sound=4E
    change_byte change=03 byte=cell:0045 value=02
    jump to=046C

The screen effect, whose settings it carries are as many as its first says,
is `MEASURED` like the event commands, its settings after the first as
`data=`; a screen effect giving none reads its count from the next byte, so
reading measures it over zeros. Where two rows read a line alike (the
battle's halts, its two forms of `play_and_wait`) `@XX` names the opcode:
3,513 of the 52,900 lines. Bytes no row reads are `bytes` (the battle's
commands the port leaves to the original, 569 lines).

The enemies' scripts aren't a table's language (`frontend/enemy_scripts.c`):
where each enemy's starts is a row of words, then each script's two
sections, blocks of conditions by their test's name (the conditions' table,
`enemy_condition_name`) and the actions they choose by name
(`enemy_action_name`), as many bytes as the battle's table of their lengths
says; `then` and `done` end a block's conditions and actions, `end` a
section, and a comment names the enemies whose script starts there:

    # enemy 00
    if always 00 00 00
    then
    do attack 01 05 05
    done
    end

Every byte of the enemies' scripts reads as one of these. The names and
measures are the text's, left out without it (`CT_SCRIPT_TEXT`).

Other tables point into these scripts by place, and the scenes' scripts name
places in themselves by address, so a script keeps its length: an edit must
leave it as many bytes as the cartridge's (and the scenes' must pack into
their slot). Measured: `commands_test` (ROM-free) writes and reads back
made-up scripts of each language and an enemy's, serves an edited command,
and refuses wrong text at its line (a command or action that isn't one, a
value too long, an opcode not the command's, a condition short of its
bytes, a script a byte longer, a byte shorter); with the ROM staged every
script of the cartridge reads back as its bytes. The upload records the
menus queue are rows of their own, `struct:records:7` (a run of them the
queue's eight bytes apart, `struct:records:8`), each named for what it
sends (`menu_upload_front_layer`, `menu_uploads_window_sets`): the code
names a record by its row (`MENU_UPLOAD_FRONT_LAYER`) and
`menu_upload_record` reads it by name, no place in bank `$C2`.

### Text

The dialog's and the menus' strings (`text:dialog`, 4695 of them), the
battle's names and the equipment screen's slot labels (`text:names:11`,
`text:names:10`) and the dictionary the strings share words from
(`text:dialog-dictionary`) are the text kind, `frontend/text.c`, kept as
`<pack>.text`. A string is written as it reads (`game/menu/typeset.c`): a
glyph its letter, digit or sign (the dialog font's `“ ” ♪ ♥ … ∞` in UTF-8),
a word of the dictionary its glyphs, and a control code its name in braces
(`{line indent}`, `{page read}`, `{wait 0A}`, `{Crono}`, `{member 2}`,
`{tech}`), each break ending a line of the text; anything else in hex in
braces (`{F3}`, `{01 7F}`), and a space at a line's ends `{space}`, so every
byte has a spelling. Read back, a string's glyphs are packed with the
dictionary's words: from the left, at each glyph the longest word starting
there, the first kept of those. Where the cartridge packed a string
otherwise, the text says so where it must, so an unedited string reads
back as its bytes: a word in angle brackets is that word (`<you>r`), `|`
ends a run that is packed on its own, a glyph in hex is that glyph, and
`{unpacked}` keeps the run after it as its glyphs (the menus' strings,
which the cartridge doesn't pack). The writer reads each string back as it
writes it and, should that ever differ, writes the string's bytes in hex.
Of the cartridge's 20,700 lines, 172 carry a word and 47 runs are
`{unpacked}`; none falls back to hex. A name is
its glyphs in quotes, in the small font, whose signs past `%` differ from
the dialog font's and are written in hex. The dictionary is its 128 words a
line, `XX "glyphs"`, in the order they are kept. A string is packed with
the words the store under the text has (the pack's), so an edited
dictionary changes every string that uses the word, as in the game; two
words alike would be ambiguous and are written in hex.

Read back, a string may be no longer than the cartridge's slot for it, its
end included; a shorter one is ended there and the rest of its slot is
zero. An edited string may not end in a code the typesetter takes the next
byte with (`{01}`, `{02}`, `{03}`, `{12}`), which would take its end as
that byte and set on into what follows; some of the cartridge's strings do,
and are kept as they are. A name table has as many names as the cartridge's, and the
dictionary's words must fit its slot. `decoded_beneath` asks the store
under a text for each resource once, which keeps reading the strings'
words from the dictionary quick.

Measured: `text_test` (ROM-free) writes and reads back made-up strings of
every code, words, signs and bytes no glyph spells, names and a dictionary
with two words alike; packs plain text (the longest word, a run ended by
`|`, one `{unpacked}`); serves a string written afresh (a word, a name, a
wait, a two-byte glyph, a tech) and an edited name, and only what a text
names; and refuses wrong text at its line (a code that isn't one, braces
not closed, a word that isn't the dictionary's or is ambiguous, a sign that
isn't a glyph, a string too long, an edited string ending in a code wanting
its byte (at its last line), a short name, too few names, a word given
twice or missing). With the ROM staged every text resource reads back as
its bytes. Changing Crono's mother's first line in `<pack>.text` shows at
frame 6500 of `movies/new-game.ctm`.

### Records

The sprites' records (`struct:sprite-records:5`, the field's; `:10`, the
enemies', the same five fields and then the enemy's header), other tables
of records of N bytes (`struct:records:N`: the tile sets' eight graphics
blocks at `$F6:1C00`, the battles' rules at `$E1:DE00`, the enemies' stats at
`$CC:4700`, 23 bytes an enemy), and the locations'
exits and chests (`struct:location-exits`, `struct:location-chests` at
`$F5:F000`, a chest its tile and contents, two words) are the records kind,
`frontend/records.c`, kept as `<pack>.records`. A sprite record is a line
of its bytes in hex under a comment naming them; the exits are written a
location at a time, `location NNN` and an exit a line (`x y reach location
to_x to_y`), only the locations that have exits. The table's words that
say where each location's exits start are made again from the lines, so a
location can be given more exits or fewer, so long as the whole table fits
its slot (the cartridge's 814 exits fill it; its 249 chests too).

Measured: `records_test` (ROM-free) writes and reads back made-up records
and exits, serves an edited record and a location given an exit (the
locations after it moved on), and refuses wrong text at its line; with the
ROM staged both tables read back as their bytes.

## Further targets

Widescreen reaches the game as the video service's view, `vid_view`: how much
of the picture is shown, the original's 256x224 unless the driver chose another
with `vid_host_set_view` (a snapshot keeps the driver's choice). The gate runs
at the original's (docs/PLAN.md, "Layering"). A port asks it where it decides
how much to show or where to lay something out, never where a rule of the
game's uses the screen's size: the field's object window at `$C0:A88D` decides
which objects run, so a wider picture draws the rest beside it rather than
widening the window; the sprite-object mode's in-view test
(`menu_scene_object_in_view`) is such a rule too, its scripts branching on it.

### The wider view, as built

`ct_native --view WIDTHx224` (256 to `VID_MAX_VIEW_WIDTH`, 480) chooses the
view; the window and the screenshots take its width. A wider view keeps the
original's screen coordinates and puts that screen in its middle: it reaches
from `vid_view_left` (negative) to short of `vid_view_right`.

- **The renderer** draws `VidFrame.view`'s width. Each layer is sampled from
  its scroll plus the view's left, but only as far across as the frame says
  it holds the picture (`vid_show_a_layer_across`): every frame starts with
  each layer holding the original's screen alone, so a tilemap never repeats
  or shows stale columns in the margins unless its mode built them, and what
  lies under it shows there instead -- in the end the backdrop, the colour the
  original shows where no layer is opaque. The backdrop too can be held to a
  span (`VID_BACK`), past which the view is black: each frame starts with it
  across the whole view. A line whose tilemap is one screen
  across (32 tiles: the menus', the battle's and the dialog's panels, which
  line effects switch to partway down) keeps to the original's screen. The
  original's screen itself is always drawn as before. Mode 7 continues its
  plane.
- **Windows** keep byte edges, as the game's line tables hold them. An edge on
  the screen's first or last dot reaches on to the view's side (`VidWindows`):
  a full window covers the view, a half-screen wipe its half, and a shape
  inside the screen stays where the original drew it. The tables themselves are
  the game's memory and are not widened.
- **Sprites** carry a place beside the table (`VidSpritePlace`, sent with it by
  `vid_send_oam`): the whole signed X a builder put a sprite at, or that it hid
  it. A place holds while the entry keeps the X and Y it was given with; a
  sprite without one reads the table's nine bits as the original does, so it
  never shows in the right margin. The field's objects give their whole X
  (they run only near the screen, so nine bits far left of it are right of
  it), the battle's combatants theirs, hiding what leaves the screen up or
  down; the battle's cursors, the scenes' leftover group entries and the time
  warp's spare sprites are hidden. The sprite limits count the original's
  screen. Each mode takes the frame over with `program_hand_the_vblank_to`,
  which forgets the last mode's places, so none outlives its mode to place an
  entry the next writes with the same X and Y.
- **Sprites in the margins** are what a mode leaves out of its table because
  they are past the original's screen: each frame a mode may show some
  beside the table (`vid_show_a_sprite_in_the_margins`), from VRAM's tiles in
  a sprite palette as an entry does, or from a 16-dot picture of their own
  with their own colours (`VidSpritePicture`). The renderer draws them only
  past the original's screen, behind the table's sprites, the first shown
  over the rest, and counts them toward no sprite limit; every frame starts
  with none, and a mode that shows them lists them beside the game's memory
  as its table is built, and shows that list every vblank until the next.
- **The field** keeps its screen's edges, which decide which objects run
  (`$C0:A88D`), and draws past them: as its edges come in, and for two frames
  after a map's screen is sent, `field_fill_the_views_margins` builds the
  layers' columns either side of the screen from the map into a line of its
  own and sends them in the vblank (`vid_send_view_vram`, below) -- in the
  same vblank where a map transition's send of a whole tile map ends the
  frame's work, since that send fills the columns past the screen from the
  map round from its other side (Crono's mother opening the blinds showed the
  next room for two frames), and with the screen's own last column where it
  lies past the room unscrolled into: the columns
  of the room the screen is held to, the map's edges it doesn't scroll past
  (`field_map_edges`) -- an interior lays several rooms side by side in one
  map (Truce's houses are one), and the original never shows a room's
  neighbours beside it -- with the screen's own where a script has moved it
  past them; a backdrop scrolling at half the pace goes by the whole map. Past
  the room a column is built as its edge's, and shown only where the edge is
  one entry all the way down (a void), which is judged as the margins are
  built and kept for the frames between, when a battle's data may lie over
  the map's looks; elsewhere the layer stops at the room. Past where the
  ground's layers (BG1 and BG2) stop, BG3 stops too and the view is black:
  BG3's edge is often one entry all the way down, a fill the map never meant
  to be seen (Denadoro's grey-blue, which its battles moved into the view),
  and so is the backdrop's colour there -- unless BG3 is a veil, which goes
  on across the view with the backdrop under it (Death Peak's fog). Each frame
  `field_show_the_layers_across` says how far each layer holds the map: a
  streamed layer those columns, a layer drawn whole its one copy of the map
  within the room -- unless it is a veil, a layer drawn whole that the
  color math lays over the picture at half strength or that lies on the sub
  screen alone (Death Peak's drifting fog): a pattern the original repeats as
  it drifts, which goes on across the map. Objects within the original's activation window
  (two map tiles past the screen) run and are drawn as on the original's
  screen. Past it, as each finished frame is sent, `field/margin_objects.c`
  lists the objects the field updates that can be seen but are out of view
  and stand in the room, and shows them as margin sprites every vblank,
  nearest first: each in the frame its animation stands on
  (`object_frame_shown`; out of view it doesn't
  tick, so it is the frame it was last placed in, or its animation's first),
  laid out as the frame builder would (`object_frame_look`), its tiles read
  from its graphics set and its colours from the ROM, or from its slot where
  the slot still holds them, and at its place on the map. Nothing of it is
  run: no timer, no frame queued, no VRAM, no palette slot.
- **The battle** is fought over the field's layers and shows them as far as
  the field last built them, moved with its own scroll
  (`field_show_the_layers_under_a_battle`); a battlefield that brings its own
  BG3 keeps that layer to the original's screen until the battle puts the
  field's back (`field_battle_gives_the_layer_back`, as the start's screen
  settings return), when the field's layer -- the Denadoro forest's shade --
  shows across again.
- **The world maps** fill their two scrolling planes' margins the same way
  each frame (`menu_map_fill_the_views_margins`) and show them across the view,
  and the layer over them (the clouds the opening flies over) too: it is laid
  out whole across its tilemap's two screens, which its drift wraps round.
  The sprite list's drawing culls to the original's screen, and its counts,
  time and noise are the game's, so it is left as it is: beside it,
  `menu_list_the_sprites_past_the_screen` lists the map's sprites it left
  out across, and they show as margin sprites from VRAM's tiles.
- **Panels** -- the battle window's and the menus' tilemaps, one screen (32
  columns) across -- are laid out across the view rather than stretched
  (`vid_lay_out_a_map_across`): each frame a mode says, row by row of a map,
  which of its columns each 8-dot column of the view shows, or nothing. It
  only moves the map's own columns along their row, so what a panel shows and
  everything the game decides stay the original's. The windows' edges can be
  parted with it (`vid_part_the_windows`: an edge left of a row's parting moves
  left, any other right), and a sprite place can carry a move across
  (`VidSpritePlace.moves`), so clipping windows, highlights and cursors keep to
  what they framed. `game/core/panel_layout.h` builds layouts: columns shown
  at a place, a window stretched with its inside's four-column pattern
  repeated.
- **The battle window** (`game/battle/wide_window.c`) keeps its windows the
  size the original draws them. Where the frames have a border between
  windows, those left of it (the enemies' list, a member's command window
  over it, the tech page's list) go to the view's left side and the one right
  of it (the party's, the tech page's MP) to its right side, with their text.
  Between them the renderer shows the battlefield the window hides on the
  original's screen: the nearest lines without the window, drawn at the
  window's own lines, wherever every laid-out layer shows nothing; where that
  picture has nothing of its own (an arena drawn only above the window) the
  gap stays black. A command window drawn over the enemies' list stands alone: the
  rest of the list, whose names it covers, is left out rather than shown as
  an empty piece of a window. The item list and the message window, a screen
  across, stay in the middle. What the window's rows and frames show is noted as the vblank sends them
  (from the transfer's source, and the frames' window corners), so the layout
  always fits what VRAM holds.
- **The menus** (`game/menu/panels_across.c`, the mode at `$C2:800E`) take
  a layout per screen from an explicit list in that file. Most screens are
  framed: where a layer runs on in a four-column pattern across the edge of
  its outer columns (a window's inside, the backdrop) -- and the pattern
  holds none of the window sides or corners further out, as where the save
  screen's slot number and title are two boxes side by side -- the columns
  outside it move out to the view's sides and the pattern fills the gap; the lists, text
  and figures inside stay where the original drew them, and the windows'
  edges at the screen's sides move with the frame. The main menu, the
  equipment, the items, the techs, the settings, the party exchange, the save
  screen and its prompts, the shops and the save point are framed. The load
  screen is parted: each row is parted in two near its middle, where no layer
  has two tiles side by side that stand out (letters, a window's side), and
  each side moves out with its windows, text and sprites. Naming and the
  gate's exchange keep the original's arrangement in the middle of the view
  until they are checked (issue #284). Layouts are read back from what the
  mode sent (`vid_read_vram`) and the rows the last picture showed
  (`vid_layer_last_shown`). A line that scrolls a one-screen map partway
  across (a window sliding in) keeps it to the original's screen, and a
  column repeated into a margin shows its layer without Mode 2's per-column
  offsets (`VID_LAYOUT_AS_IT_LIES`).
- **A panel over the picture**: where a line switches a layer the frame
  holds past the screen to a one-screen tilemap no layout spreads -- the
  dialog window, which the field's line effects draw with BG1 and BG3 over a
  band of the screen -- the panel keeps to the original's screen, and the
  renderer draws those lines' margins as the nearest lines without a panel
  show the picture (above it, or below a window at the top), so the map goes
  on behind the window across the view.
- **Every other mode** (the scenes, the time warp's tunnel aside, which is
  Mode 7) keeps its layers to the original's screen, the backdrop in the
  margins, until it builds what lies past it.
- **The scenes** hold nothing past the original's screen but their Mode 7
  planes, which go on as they are (the title's pendulum, the race's track, the
  Epoch's dive, the Beyond Time flight). Every flat layer they set is a
  tilemap one screen across -- the race's sky, the dive's dashboard, the
  endings', staff roll's and credits' pictures and text, the rising palace,
  the planet, Lavos's fall, the turning world -- which the original wraps, so
  past the screen it could only repeat; the title's one wider layer (its logo,
  BG1) is blank past the logo. So they keep the backdrop beside them. The
  attract demo plays the field's maps, which go on as the field's do.

A wider view's own work reads the game's state outside its time
(`charge_uncounted_begin`) and sends in the vblank, where a send takes none;
its state (`SpritePlaces`, `FieldMargins`, `FieldObjectsPast`,
`MenuSpritesPast`, `PanelLayout`, `BattleWideWindow`, `MenuPanels`) is the
program's, beside the game's
memory. Its columns go to VRAM through `vid_send_view_vram`, which shows them
but keeps them out of what the game reads back (`vid_vram_word`: the palace
rising copies the field's map out of VRAM) and leaves the VRAM port's step and
address where the game's own sends left them. So a movie played at a wider
view ends in the same work RAM and save RAM as at the original's.
`tools/wide_check.py` plays each gate movie at 256x224 and 480x224 and
compares the whole end state, work RAM and save RAM: all 58 end the same,
`attract-demo` through the palace's rise among them. It plays the gate's
movies twice over, about six minutes at `-j 2` beside the gate's own four and a half, so
it runs in the non-blocking `suite.py --long` batch rather than the gate.

### The smaller view, as built

`ct_native --view 240x160` (any size from `VID_MIN_VIEW_WIDTH` by
`VID_MIN_VIEW_HEIGHT`, 160x144, to the original's 256x224) shows a handheld's
screen: the GBA's, without scaling, as part of the original's screen.
Coordinates stay the original's. What part is the game's choice each frame,
made in `game/` as presentation and never as a rule:

- **The camera** (`vid_aim_the_view`, `vid_view_aim`): the screen point at
  the view's top left, held to the original's screen (16 dots across and 64
  lines down at 240x160). It stays where it was aimed and starts in the
  middle. `core/view_framing.c` moves it a quarter of the way a frame (at
  least a dot): `view_follow` keeps a point in the middle third of the view,
  `view_centre_on` puts it in the middle, and `view_start_centred_on` puts
  it there at once. It keeps nothing beside the game: the camera's place is
  the view's own, so a mode that frames nothing shows what the last one
  aimed at. The title, which frames nothing, starts it in the screen's
  middle as at power-on: after the attract demo it was left where the
  demo's last scene aimed it, and the logo and pendulum showed 22 lines
  higher and 8 dots further left (issue #572).
- **Panels** (`vid_keep_a_panel_in_view`, `VidViewPanel`): bands of the
  screen's lines shown at lines of the view's own over the camera's picture,
  whole as the original draws them (every layer, sprite, window and colour),
  stated each frame. A band may have a run of its dots cut from inside it
  (`cut_at`, `cut_dots`), and a column moved by its own lines: its dots
  short of `column_end` show the screen's lines from `column_top`
  (`view_keep_lines_beside`). Lines past the screen's show black.
- **A drawn panel** (`vid_draw_a_panel_in_view`, `VidDrawnPanel`): a window
  laid out again, which the screen has no room for. Its lines take the
  looks of a band of the screen's (colours, windows, colour math, spread
  over them) and draw their own layers from the panel's own memory: VRAM as
  the game's sends left it, with that frame's `vid_send_panel_vram` sends
  over it, which nothing else shows and the game never reads back.
- **The renderer** draws the original's screen into its own buffer and
  composes the view from it, a line at a time from the camera or a panel,
  then draws the drawn panel's lines.
- **The field** (`field/camera.c`) follows the party leader, where it can be
  seen on the screen, or the screen's middle (a scene the scripts pan to);
  the field's own screen already follows the leader and stops at the room's
  edges. The camera keeps its lines clear of the screen's own dialog band,
  which the view shows elsewhere (`ViewLines.clear_of`).
- **The dialog window** (`field/dialog_box.c`) is drawn afresh at the view's
  top or foot, 240 dots across with both borders. Its lines of text reach
  240 dots (measured over six dialog-heavy movies), so a line that passes
  the narrower window's reach is set again: the paragraph it is in (a
  speaker's lines, indented under the first) has its words set again
  across its lines, a word going on to the next line where it would pass.
  Measured over the 5,266 pages of the dialog's 4,411 strings (the pack's
  text and the font's widths), 856 pages are set again and 189 then need
  more than four lines: 150 need one more, 34 two and 3 three, and two
  staff-roll-like strings far more. The game types the page as it does at
  256x224, a glyph a frame, into its own tiles. The window reads them back,
  finding words where the ink leaves a gap a space wide, so names and items
  set into the text break as their glyphs do. It reads only the line the
  game sends to VRAM that frame or the last (`dialog_text_sending`), and
  keeps the rest in the view's memory (`vid_view_memory`) with the page as
  it last set it; it sends again only the tiles whose pieces or dots
  changed (a glyph's, while it types), and everything where the string,
  the choice, the window's place or its lines change. The page is set
  again only where a line's ink moved, and a frame in which no dot changed
  sends nothing: in `gato-battle` on the GBA the window went from 4.3% of
  the samples to 0.9% (`new-game` 3.1% to 0.7%), what is left reading the
  line the game types into. While the
  page is being typed the window shows four lines, the newest at its foot.
  Once the page waits for the player, it grows to show every line. It opens
  and shuts from its middle as the screen's band does. The game's text,
  timing and inputs are unchanged.
- **A battle** (`battle/camera.c`) keeps the battle window's 48 lines whole
  at the view's foot, or at its top where the window's arrangement lays it
  out at the screen's top (lines 8-55 rather than 160-207: the attract
  demo's battle in Magus's castle, which showed only the window's last line
  before, issue #552), with two of its columns cut from inside a window so
  both borders show. The two cut are the middle pair that holds no text and
  sits in the frames' repeating pattern, found again only where the frames'
  rows change or text comes into them (the view's memory keeps them). The camera centres the box round
  the combatants in the fight in the view's other 112 lines.
- **The menus** (`menu/camera.c`). Their screens keep within 224 dots
  across, which fit, and run up to 200 lines down, of which the view shows
  160. Each is laid out again as runs of its lines (a table a screen):
  every window's top and foot edges, a few lines of each, and its insides
  without the blank lines between an edge and the first or last line of
  text, nor the blank lines both columns share. A run starts and ends only
  between rows of text in every column, so no row shows sliced. The load
  and save screens (with their erase question whole over the slots), the
  techs, the settings and both party exchanges fit whole. The items and
  the shops are laid out as bands of their own (`items_bands`,
  `shop_bands`) with every window and every row in view: the items' list
  keeps each row with two of the four blank lines under it (the cursor's
  row its hand's), or, with the members' or the users' window beside it,
  all but the blank lines both columns share; a shop's list leaves out the
  blank lines under its first and third rows, where every window beside it
  is blank whatever it shows, the party's window kept with room for the
  jump. The equipment's two columns have rows 8 and 12 lines apart with
  almost no blank lines in common, so each column leaves out its own
  (`equipment_bands`): the left one, left of the screen's X 112, is moved
  by its own lines beside the right, as the original moves it by mode 2's
  offsets. Each keeps 160 of its 192 lines, every window and row with the
  slots' and the lists' rows whole for the hand, a few lines less of the
  windows' edges and blank lines and the figures' rows 8 apart; three
  tables a column, by where the cursor is (the members, the slots, what
  fits the slot, whose description window across the foot both columns
  keep alike). A band starts wherever either column's next run does, 12
  at most. The main menu keeps its window whole over the option's panel,
  the equipment's panel from its top down to the figures. The naming screen is laid out
  again as bands of its lines: the
  name's window, every row of letters with most of the blank lines between
  them left out (the cursor's row with room for its hand), and the letters'
  window's foot. While the first naming's instructions are up, they are
  kept whole in their place. A band starts no higher than where the last
  ended, so that the view shows the original's lines in their order: with
  the cursor on the last row, the row's room for its hand ran a line into
  the foot's band, and the GBA, which draws its lines down the original's,
  showed the camera's view of the naming screen instead of its bands until
  the cursor left the row (issue #520). The world map is framed at its middle.

`tools/wide_check.py --view 240x160` plays the gate movies at 256x224 and
240x160 and compares the whole end state: all 59 end the same. The long
batch runs it (`handheld`); `video_test` checks the camera's hold, the
composed view, a panel's one frame, a cut panel and a moved column.
The GBA follows the same camera and bands, and draws the same panel and cut
("The GBA build, as built"). A drawn panel's memory stays from frame to
frame until the host clears the view's memory, and a host may take the
sends as they come, so the dialog states its panel first, then sends its
tiles (a blank one, then each line's where its pieces reach), then the rows
of its maps the window shows; it sends nothing while the window is shut.
Played every 5 frames of `gato-battle` and every 3 of `new-game`, its
pictures are the same as when it sent the whole window every frame.

GPU backends must preserve line effects and color math. Smaller targets need measured
memory and coroutine stack budgets; a callback-based frontend needs an appropriate
fiber backend or resumable game tasks.

### Memory, stacks and per-frame work against small targets

`ct_native --measure` (frontend/measure.h) reports what a run holds and does,
and `tools/measure.py --ct-native <ct_native>` runs it headless over eight
movies that between them put every mode on screen (the attract sequence, a new
game, `gato-battle`, `menu`, `field`, `events-epoch-lavos`, `battle-magus`,
`endings-telepod-lavos`), taking the largest of each figure. The ctest
`measure` runs one movie and checks the report is whole. Measuring changes
nothing the game does: the resources are served by the same store through a
counting wrapper.

What it measures, and how:

- **Sizes**: `sizeof` the GameState and its work RAM's parts, the renderer,
  the sound player and the picture's observer (with the copies of memory it has
  taken for the frames' lines), the arena's bytes taken, and the game library's
  code, constants and static data (`size -t` of the `ct_game` library).
- **Stacks**: the fiber stacks start zeroed in the arena and grow down, so
  the lowest byte not zero in a slot is the deepest any fiber in it went
  (`fiber_stack_high_water`): stack painting, over every frame the movies run.
- **Resources a mode needs**: each stay in a mode (the vblank handler it
  installed: the field, a battle, the menus, a scene, the time warp) starts the
  store's cache afresh, and the bytes of the catalogue's resources the stay
  reads are summed: the cartridge data a mode reads, in the catalogue's form.
- **Per-frame work**: host nanoseconds a frame (the host's monotonic clock:
  QueryPerformanceCounter on Windows, clock_gettime(CLOCK_MONOTONIC) elsewhere),
  split into the game's frame (its threads and handlers), the sound
  player and the renderer; the 99th percentile and the heaviest frame per mode.
  The targets' figures are an estimate, and say so: host time times 12
  instructions a nanosecond (a desktop core near 4 GHz retiring about three a
  cycle; `--host-ipc-ghz`), against a frame's worth of the target's: the
  GBA's ARM7TDMI at 16.78 MHz retiring about half an instruction a cycle from
  the cartridge or EWRAM (about 140,000 a frame), the SNES's 65816 at 3.58 MHz
  about one every four cycles (about 15,000 a frame). x86 instructions are
  neither Thumb's nor the 65816's, and the host was shared with other work, so
  the heaviest frames are noisy; the 99th percentile is the representative one.

Measured on the gate's build (2026-11-13; the fiber stacks, the arena, the
lean build and the work a frame 2026-11-14):

| What | Host | Fits the SNES (128 KiB WRAM, 64 KiB VRAM, 3.58 MHz)? | Fits the GBA (256 KiB EWRAM, 32 KiB IWRAM, 96 KiB VRAM, 16.78 MHz)? |
|---|---|---|---|
| GameState | 529.1 KiB: work RAM's parts 372.0 KiB, the overlays' last writers 116.5 KiB (`overlay_writer`), the crossings' tally 19.7 KiB (`crossed`), save RAM and the rest 21 KiB; lean, 509.4 KiB | No: the parts alone are 2.9x WRAM (244 KiB over); the original fits them by overlaying the modes' memory | No as a whole (372 KiB is 116 KiB over); a mode's set does (below) |
| A mode's work RAM at once (core, event, palettes and the small parts, plus the mode's) | field 164.1, field with the menus 229.4, field with a battle 210.2, world map 150.9, scene 146.8, time warp 142.2 KiB | No: each is over 128 KiB (by 14-101 KiB), lean or not | Yes, each under 256 KiB (27-114 KiB to spare), the modes overlaid as the original overlays them |
| Largest parts | event 110.5 (its script buffer 56.0), field 49.6, battle 46.1, world map 36.4, sprite-object mode 33.2, scene 32.3, menus 32.1, warp 27.7 KiB | Event alone is 86% of WRAM | Fits |
| Game library | code and constants 1947.2 KiB, static data 117.5 KiB | Code: a ROM's, not WRAM; the static data is 92% of WRAM besides | Code in the cartridge's 32 MiB; static data 46% of EWRAM besides |
| Renderer, observer | 253.6, 186.8 KiB | Not needed: the PPU draws | Not needed: the GBA's PPU draws, from VRAM the port fills |
| Sound player | 126.4 KiB (the sound processor's 64 KiB and its mixer) | Not needed: the APU | Over budget as is: a GBA needs its own player in EWRAM |
| Picture memory | the SNES's own: 64 KiB VRAM, 512 B CGRAM, 544 B OAM, kept by the observer | Fits: it is the SNES's | Fits 96 KiB VRAM, 32 KiB to spare, where 2bpp layers are widened to the GBA's 4bpp (BG3 doubles) |
| Resources one stay reads | battle 565.8, field 518.5, menus 451.4, scene 379.0, warp 270.6 KiB; the catalogue 3.67 MiB | ROM: no RAM | Cartridge ROM: no RAM |
| Fiber stacks | 16 slots of 64 KiB (1 MiB; the arena 5.7 MiB in all, from 12.8 with 512 KiB slots); deepest use 3.4-3.7 KiB (the main thread), then 1.8 KiB and under 1.3 KiB in the six task slots used; 20.1 KiB in an unoptimized build | 8 KiB of stack would do; the 65816's are in WRAM | Seven slots of 4 KiB fit IWRAM (28 KiB) |
| Work a frame, 99th percentile, median of three runs (game + sound) | normal: battle 0.14+0.15 ms, field 0.26+0.16, menus 0.40+0.12, scene 0.09+0.14, warp 0.03+0.15; lean: battle 0.09+0.12, field 0.16+0.13, menus 0.13+0.13, scene 0.07+0.15, warp 0.01+0.12; lean with the address view resolved once (2026-11-14, below): battle 0.06, field 0.11, menus 0.10, scene 0.05, warp 0.01 against the same host's 0.08, 0.14, 0.12, 0.06 and 0.01 before | No | No: normal 15-45x a frame's ARM7 work (field 36x, menus 45x); lean 11-25x (field 25x, menus 22x), of which the game alone 1-14x |
| Work a frame, heaviest | game up to 20 ms (single frames, noisy on a shared host) | No | No |

The fibers' stacks are sized from these marks: 64 KiB keeps three times the
deepest use of an unoptimized build, and the lowest 256 bytes of each slot
are a guard that a switch away from its fiber checks (runtime/port/fiber.c).
The ctest `measure` fails if a run takes over half a slot.

#### The lean build

`-DCT_LEAN=ON` configures what a small target needs of the program and
nothing else. It leaves out:

- the overlays' crossings tally: `vars_host_check` on every read of shared
  bytes, `VarsHost.crossed` and `crossed_writer` (19.7 KiB), and the owners'
  names behind the `overlay crossed:` report;
- the port's own checks: `PORT_CHECK(check)` is `true` and the check is not
  made (runtime/port/vars_ram.h), so the contract it guards is trusted;
- the flag every access asks whether a guard looks by: the game's own looks
  (`vars_peek(g, on)`) are uncounted stretches in every build, their
  accesses counted and then dropped as the look ends (runtime/port/state_host.c),
  and only `PORT_CHECK`, which has no state to hand, looks by the flag. The
  looks' reads go through a constant state, so nothing in one charges time
  another way. Without the flag an accessor's charge is one add to the
  account, which the compiler merges across a run of accesses: on the GBA
  the program's code is 205 KiB smaller and the game's frame 8-15% shorter
  ("The GBA build, as built");
- the timing report (`--timing-report`, `state_host_record_time`), the
  per-routine totals the time model feeds;
- the accessors' bounds checks, which only `CT_ACCESSOR_CHECKS` builds make.

It also optimizes across files (CMake's interprocedural optimization), as a
small target's build would, so the generated accessors inline into the game;
`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF` leaves that out. It plays every
gate movie as the normal build does: `python3 tools/lean_check.py --ct-native
build/main/ct_native --lean build/lean/ct_native` plays each with both and
compares how it ended and the whole of work RAM and save RAM at its end (59
of 59 the same, about six minutes at `-j 2`). The native tests run in it too,
less the parts that look at what it leaves out.

Two pieces of what looked like bookkeeping are the game's, and stay:

- **The time model** (`state_host_game_access`, the `TIMED_AS` scopes): a
  build that charges no time shows no picture in any movie, since the game's
  frames and lag frames are made from it. Only its decisions reach the game,
  though, and a build can take them from elsewhere: the GBA's keeps no time
  (`CT_UNTIMED`) and replays ct_native's ("The GBA build, as built").
- **The overlays' last writer** (`overlay_writer`, `vars_host_claim`): the
  address view answers a read of shared bytes from the owner that wrote them
  last, and the video uploads, saves and end states read by address. A build
  without the claim goes dark at frame 1637 of every movie.

So GameState's diagnostics drop by 19.7 KiB, not 136 KiB: the 116.5 KiB of
last writers is how separate owners keep the original's one byte, and goes
where a target shares the owners' storage: the flat state (below), which a
small target builds. No mode's
resident set fits the SNES's 128 KiB with or without it.

Per frame, the lean build's game takes a half to two thirds of the normal
build's time (menus 0.40 to 0.13 ms, field 0.26 to 0.16, battle 0.14 to 0.09).
Leaving out the diagnostics alone halves the menus' (0.18 ms) and changes the
other modes by less than the runs vary; optimizing across files does the
rest. The lean game and sound together are still 11-25 GBA frames' work.

#### Where the lean build's frame goes

Sampled by stopping the host's thread every 40 microseconds and reading which
part of the frame it was in (the game's or the sound's), over the `menu`,
`field` and `gato-battle` movies, all frames and the heaviest 2% of each
mode, with gprof's call counts for who calls what:

| Share of the lean game frame | Field | Menus | Battle | Scene | Issue |
|---|---|---|---|---|---|
| The state's address view: `vars_host_read8`/`write8` and a 3,360-case switch on the owner field, for alias accessors and reads by address; since resolved once and the sends read a span at a time (below) | 35%, now 15% | 48%, now 22% | 33%, now 10% | 48%, now 20% | #424 |
| The typed accessors' own calls (inlined into the game once optimized across files) | 23% | 16% | 25% | 17% | |
| The time model: `charge_time_as_begin`'s search of the rates at every scope, `state_host_game_access` at every access; since charged inline in 32 bits (below), 2-4% in its own functions, the rest inside the accessors | 10% | 8% | 10% | 7% | #426 |
| The overlays' last writer (`vars_host_claim`); since written out as stores (below), 0-1%, battle 6%, scene 6-7% | 3% | 3% | 10% | 5% | |
| The video uploads' own part; their byte reads are in the address view above, 9.6 million of the menu movie's 11.6 million reads by address, since read a span at a time (below) | 2% | 3% | 6% | 4% | |
| Cartridge reads, each byte looked up in the store; since found among the resources read lately, and read on through a run (below) | 4%, now 3-4% | 4%, now 4-6% | 4%, now 5% | 4%, now 3-4% | #429 |
| The game's own logic, the script interpreters' decoding (the field's event engine, the menus' scripts) the largest of it; the interpreters since decode only what a row names (below) | 20% | 16% | 10% | 11% | |

The heaviest frames share the same parts, with more of the address view's
writes (a resource unpacked into work RAM, `wram_fill8`). Optimized across
files the address view is half the frame (49-50%) and the time model 8%. The
sound player costs as much again as the lean game: 95-97% of it is the
host's mixer (`mixer_frame`: eight voices, Gaussian interpolation and the
echo at 32 kHz), the sound driver itself 1-3% (the GBA mixes its own, "The GBA build, as built").

So most of the 11-25x is the state's machinery and the mixer rather than the
game: an address decode for every byte an alias view or a transfer touches,
the time model's pricing of every access, and a mixer built to be exact. The
game's own logic is a fifth of its frame or less, about 1-3 GBA frames' work
by these estimates. Run `tools/measure.py` against a lean build to remeasure.

**The overlays' last writer, written out** (issue #427). An owner accessor
whose bytes other owners keep beside it says it wrote them last. Before, it
called `vars_host_claim` with its place, which looked up each byte's set of
owners and searched it. For 2,626 of the 2,831 places an overlay touches,
every index the accessor takes stores the same writer at a slot that moves
with the address. `tools/ram_map.py` (`direct_claim`) writes out those
stores (`h->overlay_writer[246u + (uint32_t)i * 0x02u] = 2;`); the other 205
(pages' instances, sets that change with the index) still call the claim.
Sampled, the claim falls from 5% of the field's and the menus' game frame to
0-1%, from 14% of the battle's to 6%, and from 9-11% of a scene's to 6-7%.
The flat state, a small target's, has no claim at all (below).

**The time model, priced a stretch at a time** (issue #426). The account
an access is charged to (`StateHostAccount`) follows the storage at the
front of the state, so `state_host_game_access` is inline in every
accessor, and all it does is count the access's bus clocks. The count is
priced where its price could change (`price_accesses`): as a `TIMED_AS`
scope begins or ends, as an uncounted or retired stretch begins (its end
puts the count back where it began), and as the timing backend takes the
work, at the rate the accesses were made at. The work is whole master
clocks plus thousandths of one, and the rate is split the same way, so a
stretch's clocks times each part is exactly the sum each access's 64-bit
product in thousandths gave, in 32-bit sums; the thousandths are made whole
as they pass 2^30, and once more when the backend takes the work; the
clocks go in runs of at most 65,535, whose products with a rate's whole
clocks (16 bits) can't pass 2^32, so a pricing needs no division (the
ARM7TDMI's Thumb has none, and the C library's took 2.5% of the battle's
samples). The account keeps the rate split in one word (whole clocks
above `STATE_HOST_RATE_MILLI_BITS`, thousandths below). A scope, and a
charge at a routine's rate (`charge_as`), finds its row in a 256-entry table
of the rows named lately (`row_of`, inline; `row_looked_up` on a miss), with
the rate already split, instead of a binary search of all 2,318 rows. In the
lean build, which keeps no totals, a scope's start hands its end the rate it
found rather than the row, so the end reads no table and divides nothing; the
other builds hand the row, whose totals the work goes to. On the GBA that
takes the game's frame from 0.66, 0.86, 1.17 and 1.25 GBA frames to 0.65,
0.82, 1.11 and 1.20 (`title-start`, `new-game`, `gato-battle`,
`events-jetbike-race`); a table of 1,024 rows gains nothing more, and what
the scopes still cost (5% of `gato-battle`'s samples) is the account's
loads and stores in work RAM as the rate changes.
Every movie plays to the same lag frames and end states (the gate,
`tools/lean_check.py`). Priced at every access, the time model was 15-20%
of the GBA's samples, its pricing out of line at the accessors' 6,400 call
sites; counted inline, the program grows by 210 KiB. Reads of work RAM by
address go straight to its banks: the flat state's bytes no owner keeps
hold 0 and are never written, so a read needs no look at the owners
(`vars_flat_host.c`). Only the timing
report's totals (`StateHostTime`, outside the lean build) are still 64-bit.

**The interpreters decode what a row names, and reads go on through a run**
(issue #429). The field's event engine and the menus' scripts decode a
command by its row in the opcode table, and the language has 110 and 55
parameters. A row names a handful of them; the others are `ARG_NONE`, which
read nothing and decode to 0. The decoders and the event engine's own check
(`event_runs_here`) now pass those over instead of running each through its
switch. Sampled, the interpreters' functions fall from 11-12% of the field's
game frame to 4-5%, and from 14-15% of the menus' to 5-6%. Passing them over
still looked at each of the 110 and 55 in turn, twice a command in the event
engine; now each opcode's row has the list of the parameters it names, in
the language's order (`event_named_parameters`, `menu_named_parameters`, in
game/event/ and game/menu/ `named_parameters.c`), and the check and the
decoding walk only those. The lists are written from the rows by
`tests/port/named_parameters_test --write .`, and the test, run with the
others, fails where the built lists aren't what the rows name. In mGBA the
game's frame falls from 0.98, 1.34 and 1.43 GBA frames to 0.90, 1.21 and
1.37 (`new-game`, `gato-battle`, `events-jetbike-race`; `title-start` stays
0.78), and the frame in all from 2.05, 2.90 and 3.27 to 1.96, 2.76 and 3.21
(1.64 for `title-start` either way). The battle's two languages decode every
parameter by its row with no switch to pass over and are a tenth of a
percent of the samples; they are left as they are.

The menus' scripts decode a command into the caller's command
(`menu_script_decode`) rather than returning it, which copied its 150 bytes
a command, and find the routine a command calls (`script_routine`) among
the 16 found lately before searching the routine tables row by row; a
scene's scripts call the same few each frame. In mGBA the game's frame in
`new-game` falls from 0.79 GBA frames to 0.75 and the movie from 11,496 GBA
frames to 11,219; `gato-battle` from 46,241 to 45,880, and `title-start`
and `events-jetbike-race` stay as they were (2,251 and 26,640 against 26,629).

The cartridge's reads find their resource among those read lately: the last
resource found in each 4 KiB of the cartridge (`asset_containing`), and the
store's bytes for the last 16 resources asked for, each held with its row of
the catalogue: where it lies, how long it is, whether it is left untyped
(`held_resource`). A read
by address keeps the two answers together, the resource it found last in
each 4 KiB with where it lies and its bytes (`served`, `found`), so the next
read in it is one comparison and a load; on the GBA that takes the game's
frame from 0.65, 0.82, 1.11 and 1.20 GBA frames to 0.64, 0.79, 1.07 and 1.15
(`title-start`, `new-game`, `gato-battle`, `events-jetbike-race`), with
`served` and `held_resource` in fast RAM. A run of the cartridge
(`rom_run_from`) and a read of a resource by its id (`asset_read8`) are
answered from the same, with no look at the catalogue in the cartridge. A new
source (`assets_use`, which `ct_native --measure` also calls at each stay, so
it counts each stay's resources afresh) asks again. A reader that goes on
through the data from one address finds its run of the store's bytes once
(`rom_run_from`, `rom_run_read8`), and each byte inside it is a load and the
read's time. The unpacker's streams and the menus' commands read this way. The
cartridge reads fall from 4-7% of the game frame to 3-6%; what is left is each
read's own call and time charge. In mGBA the game's frame falls from 2.3, 3.0
and 4.1 GBA frames to 2.2, 2.5 and 3.4 (`title-start`, `new-game`,
`gato-battle`). The heaviest frames are the ones that unpack a location, and
they fall from 475 to 332 GBA frames (`new-game`) and from 785 to 598
(`gato-battle`).

**The address view, resolved once** (issue #424). The typed state's address
view no longer decodes an address at each access. When the first state is
made, it resolves each of work RAM's 128 KiB to the byte of the storage that
keeps it (`place_byte`, `byte_kept`: 512 KiB). Where owners keep a byte beside
each other, it also lists every owner's byte (1.9 MiB: 114,389 of the 129,656
owned bytes are shared). A read is then a table's load and the byte's. It
reads the first owner's byte unless `overlay_writer` names another owner, so
it walks the list only then. A shared write reaches every owner. The flat
state goes further: an accessor whose bytes an owner keeps reads and writes
them where the banks keep them (`get16`, `put16` in `vars_flat.c`), with no
lookup at all. Which addresses an owner keeps is a 16 KiB bitmap
(`vars_flat_owned`), not a search of the free runs, and the storage is
reached with no call (`state_host_vars`, the state's first member).

Sampled again over the same three movies, the address view is 22-24% of the
field's game frame, 27-31% of the menus', 23% of the battle's and 30-33% of
a scene's, down from 35-48%. Most alias views name bytes that more than one
owner keeps, and owners that change from one index to the next: 952 of the
971 views the generator can't resolve to one field. So what remains is the
reads by address: the video sends' and the menus' `wram_*` reads, fills
and copies.

**Sends read their sources a span at a time** (issue #425). A send names the
game's bytes by address (`PF_GAME`). The video service's host now reads them
through the driver's `PfReadGame`, `len` bytes in one call
(`state_host_read_span`, then `vars_host_read_span`); before, it was a call a
byte. The flat state copies the span straight out of its banks. Its unowned
bytes hold 0 from the start and are never written, so the copy reads what the
address view reads. The typed state reads each byte through the resolved
view inside the one call. The observer takes the sprite table in one read,
and the palettes and VRAM sends 256 bytes at a time. On the GBA, a word send
through a plain port (no remapping, a word's step after the high byte) goes
straight into the kept VRAM. With both changes the address view is 14-16% of
the field's game frame, 21-24% of the menus', 10% of the battle's and 18-21%
of a scene's. What remains is the typed state's alias views of shared bytes
and the menus' `wram_*` reads, fills and copies (issue #424).

**Work RAM's fills run a span at a time.** `wram_fill8`, `wram_clear` and
`wram_port_fill` in work RAM's own banks fill the storage in runs
(`vars_host_fill_span`), a bank's run at once, rather than a
`state_host_write8` a byte; the flat state writes every owned byte of the run
where it is kept, and the typed state writes each through
`vars_host_write8` inside the one call. A scene hides every sprite each frame
(`scene_hide_every_sprite`, 512 bytes): it was 11% of the GBA's work in
`title-start` and 2.7% in `events-jetbike-race`. In mGBA the game's frame
went from 0.64 to 0.54 GBA frames in `title-start`, 0.79 to 0.77 in
`new-game`, 1.07 to 1.06 in `gato-battle` and 1.16 to 1.11 in
`events-jetbike-race`.

A battle's way in was the GBA's heaviest game frame, the original's own
pause (ct_native lags 29-30 frames there too) done in one GBA frame of
work: 28-30 GBA frames in each of `attract-demo`'s battles (game frames
3884, 6598, 9413, 14933, 17649, 20462; issue #550) and 46 in
`gato-battle`'s. Its three largest parts now run as spans:
`wram_fill16` of a word of two bytes the same is `wram_fill8` (the
battle's 32 KiB cleared on the way in, `battle/from_field.c`); the flat
state's fill sets runs of eight bytes owners keep whole at once
(`vars_flat_host.c`, `fill_owned`); and the sound driver reads a song's
sequence and an effect set from the cartridge as a run (`rom_read_span`),
each byte still timed as a cartridge read, the song's with the three reads
of its pointer each byte was read with before, so ct_native's lag frames
are as they were (1,133 in `attract-demo`'s first 10,000). Each battle's
way in fell to under 17 GBA frames; the heaviest of `attract-demo` are now
its location loads (24 GBA frames at 7537 and 18586), and the movies take
1,899, 7,981, 25,980, 14,197, 8,198 and 34,595 GBA frames (`title-start`,
`new-game`, `gato-battle`, `events-jetbike-race`, `menus-sweep`,
`attract-demo`) against 1,919, 7,999, 26,066, 14,386, 8,228 and 34,806,
`gato-battle`'s heaviest frame 39 against 46.

### The GBA build, as built

`ct_gba` is the game on the GBA, with its picture and its sound: the
lean build's game and state, compiled for the ARM7TDMI in Thumb (the state's
accesses in ARM) by arm-none-eabi GCC with newlib, at -O3 and optimized
across files: one ROM, `ct.gba`, played from the keypad with its save in the
cartridge's SRAM, and driven from outside to play the movies. Everything GBA
lives in `targets/gba/`, whose README says how its pieces fit (the toolchain,
the driver and its system, the services' backends, the host's tools), the
controls, and how to build it, drive it and play a movie in mGBA against
ct_native's end state (`host/run.py`). The C sound driver plays as it does
natively; the GBA's own mixer (`services/mixer.c`) plays its voices (below,
"The sound").

**The game's frames without the time model** (issue #513). The time model
reaches the game only through what it decides of the game's frames, in
order (`TmHostDecision`, platform/native/host.h): whether the main thread
sits a frame out (a lag frame), whether it stops in the middle of its work
to go on in the next frame (`tm_host_sit_out_owed`), and whether a budget
has time left in the frame (`tm_frame_time_left`). The same decisions in the
same order make the same game. The GBA build keeps none of the original's
time (`CT_UNTIMED`): the accessors count no accesses, a `TIMED_AS` scope is
its statement, the charges are empty (runtime/port/state_host.c), and the
time model's rows aren't in the program. Its host decides instead
(`TmHostProgram.decide`, `system/frames.c`):

- **Driven with a movie, it replays ct_native's decisions.** `ct_native
  --decisions FILE` writes them as its time model makes them, a bit each
  (`TmHostProgram.decided`); `host/run.py` has ct_native play the movie
  first, and `mgba_run --decisions` puts the file in the cartridge far past
  the program and its pack (`DRIVE_DECISIONS_AT`), where the driver finds
  it when a runner drives. The GBA then plays ct_native's frames exactly,
  lag frames and all, so a movie's end state checks the GBA against
  ct_native as before: `title-start`, `new-game`, `gato-battle`,
  `events-jetbike-race` and `menus-sweep` end as ct_native's with 1,991,
  19,431, 126,559, 38,407 and 6,585 decisions replayed.
- **Played by hand, the GBA's own time decides**, as the SNES's beam did.
  Where the game's part of a frame (its handlers and main thread, not the
  picture or the mixing, which the SNES's own processors did) runs past a
  GBA frame, what it ran over is owed, and the frames after sit it out a
  whole frame at a time; the main thread stops where it stands once it has
  run a whole GBA frame; and a budget has time left while the game's time
  so far in the frame, a GBA frame read as the SNES's 262 lines from its
  vertical blank, is inside its window. The game's state then differs from
  ct_native's where the GBA's speed differs from the SNES's, as a slower
  SNES's would; what the game does with a frame doesn't. Driven with no
  decisions (`mgba_run --drive`, a movie without them), it decides so too.
  The window was first read from the GBA's line counter, which a frame
  starting late (after a heavy picture, at line 0-60 rather than in the
  vertical blank) found already shut: the field's scripts and animations
  ran a few turns a frame where the original runs a dozen, and the attract
  demo, left on the title, panned past Magus's castle into unbuilt tiles
  and stood for ever in its hall with no party (issue #553).
  `host/run.py --own-time` plays a movie so and checks that the GBA goes
  through the same places as ct_native in the same order (`ct_native
  --locations`, the GBA's `location_id` read every 4 frames, the last
  allowed to differ): `attract-demo` through its 50 and `new-game` its 6,
  where before the attract stopped at its 13th. In the castle's pan the
  gargoyle's tiles were drawn some 35 frames after the pan reached them
  (issue #555): its frames are drawn from graphics built in bank $7F, a
  word at a time through the state, and the frame builder's tile copy was
  12.6% of the GBA's work over frames 5880-5990. A tile now goes in one
  read and one write (`game/field/graphics.c`, `wram_read_span`,
  `wram_write_span`, and from the cartridge `rom_read_span`, served in
  one copy by `rom_cartridge_read_span`), each byte's time as before,
  where its places neither leave the bank nor overlap; the copy falls to
  0.9% and the frames' work over the window by 11%. Played so, the
  gargoyle is drawn as the pan reaches it, the attract going through
  ct_native's 50 places.

In mGBA, with a late frame's picture left out (`Pictures`, below) and the
fibers' stacks sized from the library as before, the movies (`title-start`,
`new-game`, `gato-battle`, `events-jetbike-race`, `menus-sweep`) take 2,141,
8,603, 29,770, 16,690 and 9,174 GBA frames against 2,165, 9,771, 35,667,
18,769 and 9,817 with the time model (1.1-16.5% fewer; 1.19-1.78 GBA frames
a game frame), with 35, 1,362, 6,600, 3,317 and 1,545 pictures left out
against 61, 1,658, 8,136, 3,673 and 1,647. Measured alone, before the
pictures were left out, the game's frame fell from 0.53, 0.72, 1.02, 1.11
and 0.69 GBA frames to 0.51, 0.61, 0.83, 0.95 and 0.62. The program falls
from 1.92 to 1.72 MB, and fast RAM keeps 2,048 bytes spare rather than 528.
`new-game` played by hand to its 7,200th frame lags 387 frames against
ct_native's 244. A movie whose decisions run out stops there, saying at
which frame, and one that leaves some unread fails `host/run.py`.

**Input, saves and driving.** The driver gives the game the keypad through
the input service (`in_host_set`), as the desktop drivers give it their
keys: the pad, A, B, L, R, Start and Select as the SNES's own, and X and Y as
a shoulder held with A or B (`system/keypad.c`; a key keeps the button it
was pressed as until let go, so the shift's order doesn't matter). The
game's 8 KiB of save RAM are the cartridge's SRAM, read and written where
they are as the SNES's battery-backed RAM is (`state_host_create_lasting_over`),
so nothing copies them out to keep a save; a mark after them says a save is
there, and a cartridge without it starts erased (`services/cartridge_save.c`).
After the mark the SRAM holds the top of work RAM (below, "Work RAM"). The image names its SRAM
(`SRAM_V113`) for mGBA and flash carts. To be driven, the driver names a box
in work RAM in its debug log as it starts; `host/mgba_run.c`, which reads
the log through libmgba, arms it as the line is written, and from then on
puts each frame's pads in the box's ring, from a movie or from commands on
stdin modelled on `ct_native --drive` (run, movie, peek, wram, shot, end,
mGBA's own snapshots in memory or a file, the SRAM as a `.sav`). Between
frames with the ring empty the driver waits and answers the runner's reads
of the game's memory and its asks for the report; the runner keeps the ring
ahead of the game as a movie plays, so the wait isn't in the game's frames.
The driver reads a frame's pads out of the ring before it counts the frame
taken (`system/drive.c`): the runner refills the ring between mGBA's frames,
wherever the program is, and with the count published first, a frame of
mGBA's that ended between the two let it put the pads of the frame 64 on
into the slot the driver had yet to read. Which frame that hit depended on
nothing but how long the GBA took over its frames, so a change to the
picture's or the mixing's speed could change a movie's end: an idle loop of
10,000 turns in `gba_video_present` put the ends of the picture's work near
mGBA's frame ends and `events-jetbike-race` ended with 220 lag frames for
ct_native's 1,732. Read first, the pads are the movie's whatever the GBA's
speed: with idle loops of 60, 300 and 10,000 turns in `gba_video_present`,
or 300 and 3,000 in the mixer's chunk, `title-start`, `new-game`,
`gato-battle`, `events-jetbike-race` and `attract-demo` each end with
ct_native's end state and lag frames (issue #492).
A movie's starting save goes in as the `.sav` (`mgba_run --sav`). Before,
the packing step put a movie's pads and its save in each image, one `.gba` a
movie.

**The state as the original's work RAM.** The typed state is 488 KiB, twice
the GBA's work RAM. `CT_FLAT_STATE` keeps the game's state as the original
does instead, a byte at each of work RAM's 128 KiB of addresses:
tools/ram_map.py generates `runtime/port/vars_flat.c`, the same accessors in
their address form, and `runtime/port/vars_flat_host.c` keeps the two banks
in the arena, but for the top a target keeps in memory of its own
(`VARS_FLAT_COLD_FROM`, `vars_host_cold`: the GBA's, below, "Work RAM"). Owners the typed state keeps apart (re/ram.toml's `overlays`)
share the one byte, as in the original, so there is no writer table and no
claim: a small target's build of the state shares the overlaid owners'
storage. What the game reaches by address rather than by a name is read
and written in the two banks in place: a byte at `$7E`-`$7F` (the alias
views, `game/wram.h`'s escape) with no look at where its address lies, and
a send's source in work RAM, at its own addresses or in the first 8 KiB of
a bank that maps the system area, in one read of the bank.
A native lean, flat ct_native
plays all 59 gate movies to the same work RAM and save RAM as the normal
build (`tools/lean_check.py --lean build/flat/ct_native`, configured
`-DCT_LEAN=ON -DCT_FLAT_STATE=ON`; CI builds and tests it as
`linux-gcc-lean-flat`).

**The picture.** `services/video.c` keeps the SNES's video memory as the game's calls
leave it (VRAM in work RAM, the palette, the sprite table, each written as
the native backend's observer writes it), so what the game reads back of
VRAM (`vid_vram_word`: the jet bike race's and the rising palace's maps) is
what it reads natively, and makes the GBA's picture from it once a frame, in
mode 0's four tiled layers and its sprites:

- **Palettes** are the same colours (both BGR555): the 4bpp layers' eight are
  the GBA's background palettes 0-7, a 2bpp layer's eight of four colours
  are 8-15, the sprites' eight are its sprite palettes 0-7. A GBA's screen
  shows them darker than a television, so the colours can be packed brighter
  (`pack.py --profile`): `original`, the default, leaves them as they are;
  `port-style` gives each channel a gamma of 0.7 and then 15% more
  saturation around the colour's luma (0.299, 0.587, 0.114), rounded and
  kept in range. The saturation mixes the channels, so the profile is one
  table of the 32,768 BGR555 colours' own, 64 KiB that pack.py works out and
  puts in the cartridge, where room is plentiful; the driver reads a written
  colour's own from it as it fills the GBA's palette, and each line's
  backdrop after its colour math. A colour is one cartridge read, and only
  a frame whose palette changed reads any. The GBA's fades and blending
  (the SNES's brightness, the dialog's shading, a darkened scene) then work
  on the colours shown, as the SNES's work on what a television shows. In
  mGBA, port-style against original over `title-start`, `new-game`,
  `gato-battle`, `events-jetbike-race` and `menus-sweep`: 156, 507, 1,236,
  239 and 219 more cycles a game frame (0.05% to 0.44% of a GBA frame,
  `gato-battle`'s palette changing most), the pictures left out 1, 3, 50,
  22 and 0 more, the end states the same. The game is never told: the
  colours are the driver's, not `game/`'s.
- **Maps** are converted where the SNES has them (an entry's palette, flips
  and tile moved to the GBA's bits), 32 bytes at a time, the ones whose
  bytes the game changed: a send of what VRAM already holds (much of what
  the game sends, frame after frame) changes nothing. A send of one byte
  of each word, or a fill, through a port that doesn't remap puts each byte
  straight at its place, stepping the address as the port does, rather
  than through the port a byte at a time (the game's frame in
  `gato-battle` 1.05 GBA frames to 1.02). Maps a line effect shows part of
  the way down (the field's dialog, whose two layers show one map as their
  own) are converted for their layer into room the GBA has spare: seven
  of them, a map a band shows cut taking no more than six and giving its
  place up to one a line shows whole. With six, the battles that keep three
  layers' maps for the field and three for the window left the third
  layer's second map out (`attract-demo`'s Magus castle battles, where it
  holds the candles' flames), and its lines showed whatever the GBA's VRAM
  held there, a window's border in rows down the whole battle; the
  census's cluster there (frames 6644-7254 and 17680-18424) fell from 6.6
  and 6.8 million pixels to 2.5 and 2.8, what is left the flames' tint, a
  colour the fixed colour adds that the GBA's blending can't (issue #554).
- **Tiles** live in caches, one for each set of tiles: a window of 1,024
  tiles from a 16 KiB block of the GBA's background VRAM, whose places no
  map takes are handed out to the tiles the maps name as they name them,
  converted from bitplanes to the GBA's packed nibbles (2bpp widened to
  4bpp, a table's lookup a bitplane's byte) then and again as the game
  changes them. A tile no place is left for shows blank until a layout
  gives the caches' places again, a second on; where the maps name more
  tiles than there are places and no tile has missed since the last, eight
  seconds on. A layout keeps each cache whose set of tiles it still shows
  in the window it had, with the places of the tiles its maps still name
  and of those the drawn panel's maps show, but those its maps and rooms
  take; the others' places it gives back. What the GBA shows stays as it
  was while the layout's work goes on, and a layout converts the maps and
  the sprites' tiles rather than all of VRAM; a cache's tiles are converted
  as the maps name them (one started afresh names none until then, and
  converting its every unit as well was a sixth of a scene's first
  picture's work).
  A cache whose window the rooms took so much of that another has more
  places free starts afresh in that one, and the picture is hidden, black,
  while that layout's picture is made: started afresh at every layout, the
  caches gave the tiles new places the maps still on screen didn't name
  yet, and `new-game` showed Crono's room in stray tiles as Mom's first
  words opened their window (issue #503). Its frame is shown whole from
  the next vertical blank, the line counter's interrupt setting its first
  line's registers there, rather than from the next picture's, which a
  scene's loading frames put off by a dozen GBA frames and more.
  Windows overlap (each is 1,024 places from its char block, the next 512
  on), so the caches are placed beside one another: a cache started afresh
  takes the window with the most places free where its tiles fit there
  beside the tiles the others have yet to place (`room_beside`,
  `window_for`), the one with the most tiles first, and a cache with no
  tile placed yet moves where its window can't hold them beside the
  others' and another can; a cache with tiles placed moves only where
  some window can hold them, since moving one that fits nowhere hides the
  picture and its tiles miss as before. In the lab before the jet bike race
  the dialog's map, shown on the first and third layers for a frame as each
  dialog opened, named 347 tiles beside the floor's 475 and the text's, and
  the floor's cache started afresh in the window where the text's cache
  had its tiles: the floor went black in 16-dot squares from frame 4461 to
  5447 as the text's tiles took its places (issue #589). With the bands
  above, the census's pictures in `events-jetbike-race` fell from 10.7
  million pixels to 3.2 (the scene's cluster gone; what is left there is
  in clusters of 0.15 million and less) and `gato-battle`'s from 3.2 to
  1.4, the other four movies within 0.02 million; each movie ends as
  ct_native's. Two clusters came in: a frame or two of shading in Crono's
  room (`new-game` 6164, 0.07 million) and eight frames of a
  dialog's rows a line apart as it opens (`gato-battle` 15625, 0.11
  million).
  A dialog's opening and closing show the screen's band of its window for a
  few pictures around the drawn panel, and so its two layers' map (the
  first layer's frame and the third's text, some 300 tiles of 2bpp) beside
  the scene's maps and the panel's room. In Guardia's courtroom the tiles
  outnumbered the places: a layout kept a line map while any line had shown
  it since the last, so the window's maps, shown for five pictures, held
  their tiles' places for a second and more after the window had opened,
  and the panel's frame showed black holes and the floor by the throne
  black squares (`events-cronos-trial` frames 5792-6013, 564,067 pixels).
  Each line map now counts the pictures the lines showed it in on end and
  those since (`LineMapUse`); where the scene's maps or the panel name a tile
  that finds no place, a line map shown briefly (under 16 pictures) and not
  for four is put to rest at once by a layout that keeps the windows: kept
  where it is, its tiles named by nothing and its copy not converted, until
  a line shows it again, which converts it before that picture is shown.
  Left out of the layout instead, the maps came back with each closing, a
  layout each time, and the census's GBA frames rose 2.6%. Over
  `events-cronos-trial` the census's pictures fell from 6.04 million pixels
  to 4.04 (clusters of 50,000 and more from 31 to 23; the courtroom's at
  5792, 8568 and 12210 gone or halved) in 25,045 GBA frames against
  25,118; over the census's default movies 31.64 million against 31.75,
  each movie's within 3% and none with a new cluster.
- **A layout's work** is what the picture stops for, so it is kept short.
  Sampled only while a layout's picture is made (`mgba_run`'s sampler kept
  to a flag the program sets), it was the maps' conversion, a unit at a
  time from the cartridge, and the tiles'. Now each map entry's place is
  looked up where its cache keeps it, from fast RAM (`convert_map`), a
  block's units of a map one after another there (`convert_map_units`: a
  call from the cartridge for each unit cost more than its work), the
  tile's place given from the cartridge only where it has none
  (`slot_for`). What a block of VRAM is for is worked out once for its 16
  units (`convert_block`), and a block with nothing to convert, a scene's
  tiles none of which has a place yet, is passed over. The sprites' tiles
  are converted again only where their tables moved, rather than all
  16 KiB at every layout. A cache's tiles are converted from fast RAM with
  a nibble's table there (the byte's table in work RAM took longer to read
  than the rest of the work); the sprites' keep the byte's, since fast RAM's
  code called from the cartridge a tile at a time took the five movies
  longer. A layout notes its maps' priorities a kilobyte at a time from fast
  RAM, and finds free places and room a byte or a word of places at a time.
  The heaviest picture a layout makes in `title-start`, `new-game`,
  `gato-battle`, `events-jetbike-race` and `menus-sweep` went from 13.81,
  20.46, 20.46, 18.35 and 16.36 GBA frames to 6.31, 7.19, 7.93, 10.07 and
  6.83, the five movies from 2,002, 8,544, 28,282, 15,611 and 9,042 GBA
  frames to 1,926, 8,252, 27,299, 15,243 and
  8,579, their end states ct_native's. Watched against the 240x160 view,
  which shows no black there, the black at the attract sequence's scene
  changes (game frames 3925, 6641, 9448, 9568 and 9954) went from 18, 17,
  18, 22 and 45 GBA frames to 5, 5, 7, 8 and 7, and as the curtain opens on
  Crono's room from 32 to none.
- **Layouts the lines ask for** are made before the frame is shown. A
  layout's lines are worked out before its maps are converted, and a frame
  whose lines name a map the layout hasn't (a line effect's, a dialog
  window's), or another mode, is laid out again then and there rather than
  in the next frame, which converted the scene a second time under the
  picture shown (the dialog window's first row in stray tiles as the
  opening's narration and Mom's dialog opened). A layout converts again only
  the maps it put somewhere else than the last did, those of a cache that
  started afresh or lost a place, or all of them where tiles had missed: a
  line effect's map coming in beside the scene's converts that map alone. A
  line map whose tiles in front found no room for their copy doesn't ask
  for a layout each frame (Gato's battle was laid out, and black, a dozen
  GBA frames a frame). A layout made only because tiles missed keeps a
  cache whose tiles still fit its window rather than starting it afresh
  where another has more room, which hid the picture (the attract
  sequence's battle blinked black at 9954). And where the game darkens the
  screen a second time in a frame with no light between, it is black at
  once, rather than the last picture staying up through the frame's work
  and the layout's (the title's last frame, in the new scene's colours, as A
  was pressed while it drew in). Watched again, the black at the attract
  sequence's scene changes went from 5, 5, 7, 8 and 7 GBA frames to 0, 0, 6,
  0 and 0, the pictures the 240x160 view shows there; the opening's first
  narration (game frames 5100-5120) shows 21 black GBA frames where it
  showed 15 and then the window's first row in stray tiles for 7 (ct_native
  shows 5 black): its two layouts, the panel's and then the window's map,
  come two game frames apart and are each a few GBA frames of work. The five
  movies took 1,924, 8,085, 26,487, 14,921 and 8,580 GBA frames (from 1,926,
  8,074, 26,448, 14,967 and 8,563), their heaviest picture 5.79, 7.31,
  8.97, 10.16 and 7.84, every end state ct_native's. Fast RAM keeps 616
  bytes free, work RAM 352 above its floor (the maps' places kept from the
  last layout take 160 bytes).
- **The picture kept up through a layout where it can be.** A layout hides
  the picture only where a cache starting afresh writes where the picture
  shown has something: its free places, its blank, or a map naming its tiles,
  converted again with their new places under the shown frame's registers;
  one that gives the plane's room back, as
  one made only for tiles that missed, keeps the caches where their tiles
  still fit. The line DMA (the plane's lines, the blending's strength) is
  armed again at every vertical blank from the table of the frame shown,
  even with one change of registers: a game frame that runs on for GBA
  frames left it reading past its table, and a layout that gave the plane's
  room back took the table from under it, and the table the frame shown
  reads stays taken through the layout that gives the room back. As the title's logo began to draw
  in (game frame 870) the screen went black twice and then the pendulum,
  a part of the plane, fell apart for a few GBA frames; now the picture stays
  up. The five movies took 1,925, 8,078, 26,486, 14,933 and 8,584 GBA frames,
  their end states ct_native's.
- **The backdrop**, the first colour, is each line's own, set with its
  registers.
- **Sprites**: the SNES's two tables of tiles one under the other in the
  GBA's sprite VRAM (its two-dimensional layout, 32 tiles a row, so that a
  sprite's rows are 16 names apart on both), and the sprite table converted
  in a frame it changed (sizes, flips, palette, position). Each sprite finds
  the band its top or foot is in from a table of the SNES's lines made once
  for the frame's sprites, and the size setting's two sizes become the
  GBA's shapes once a frame, not for each of the 128. A sprite whose foot
  runs past the SNES's last line finds it at the top, where its head is
  above the screen (Crono's feet at the top of the attract demo's Guardia
  forest, missing before).
  The tiles are on two pages, the first 16 and the last 16 of each GBA row
  (`turn_the_sprite_page`): a picture converts the tiles that changed onto
  the page not shown, with those the other page took last, and its sprites
  name that page, so the page turns as its sprite table is copied in. Before,
  the tiles were converted in place as the picture was made, and a picture
  shown a GBA frame late (`show_the_frame`) showed the last frame's sprites
  in the new frame's tiles: an object's pieces moved about for a frame each
  time its frame changed (issue #550). Matched against `ct_native --view
  240x160` a GBA picture at a time (colours at the SNES's five bits, dots
  that differ in most pictures, a panel's style or the colour math, set
  aside): `gato-battle` 9600-9900, Crono and Mom in his house, 25 of 302
  pictures differed, now none; `new-game` 6900-7199, Crono walking, 19 of
  309, now none; `attract-demo` 4880-5030, 84 of 174, now 25, all at its
  fades; `events-jetbike-race` 3000-3150, 75 of 180, now 14, Marle hidden
  by the sprite mask (issue #560). The five movies and `attract-demo` end as
  ct_native's in 1,918, 8,010, 26,120, 14,504, 8,265 and 34,962 GBA frames
  against 1,919, 7,999, 26,066, 14,386, 8,228 and 34,806 (the tiles that
  change converted twice); work RAM keeps 384 bytes above its floor (the
  pages' two 64-byte masks), fast RAM as it was.
- **Priority**: each SNES tile stands in front of or behind the sprites by
  its map entry's priority bit, where a GBA layer has one priority. A layer
  whose map has tiles of both is split across two GBA layers, as many as
  the four leave spare (mode 1's three layers leave one; mode 2's two, or
  three with the third, less the one kept for the first layer's moved
  columns, below): its map is converted twice, at the SNES's place with the tiles in
  front left blank and into room the GBA has spare with the others blank,
  each unit of 32 bytes as the game changes it, both halves at the
  priorities that put them among the sprites as the mode does (per mode, a
  table of each layer's halves and each sprite priority). The GBA's layers
  are numbered front to back, since between two of one priority the GBA
  shows the lower-numbered in front, and each line's registers are worked
  out for them. Which units of each layer's map have tiles in front and
  which have tiles behind is kept as they are converted; the first layer is
  split before the second (the second's tiles in front alone would cover
  the first's: the Fair's stalls hide their wares), the second before the
  third (mode 1's third is the text and the panels), and those with more of
  each before those with fewer. A split layer stays split while it still
  has both, since splitting is a layout's work; one not split stands where
  most of its tiles do, mode 1's third in front where the mode puts it in
  front and it has any tiles there. A line effect's map on a split layer
  has its copy of the tiles in front only where it has some (the next
  layout gives it one when it gains them), and one that is another layer's
  own map, its tiles from the same cache (the field's second layer shows
  the first's), is shown from that layer's copy rather than a copy of its
  own, which leaves the caches the room a split takes. A map a band shows
  cut isn't split: all of it stands where its own entries put it, decided
  at the layout as a layer's stand is (`cut_map_stands_in_front`), on its
  layer's GBA layer of tiles behind, split or not (the battle's windows'
  text in front). Its layer's stand was taken before, made from the layer's
  other maps: in Magus's battle the third layer's second map, the candles'
  flames, is all behind, so the menu's text stood behind the first layer's
  window and the windows showed no words (`battle-menu-buttons` frames
  3632-3934, 787,954 pixels summed in the census (replayed), now 17,521 in
  seven clusters of a frame or two; 799,627 to 23,189 with `--colours
  port-style`). Shown on both GBA layers of a split layer, the copy in
  front blended with nothing under it, and the windows lost their shading.
  Over the nine movies of the colour window's census (below, "Colour
  math") no picture cluster came in, `battle-menu-buttons`' picture
  mismatches fell from 3.56M to 2.79M (4.33M to 3.67M), and the other
  movies' moved by under 5%, `events-jetbike-race`'s falling most. The
  field's layers show their line
  effects' maps on every line and never their own, so which units have
  tiles in front is taken from those maps too: counted from the own maps
  alone, the field's first layer had none, wasn't split, and stood behind
  the sprites, so walking down the stairs in Crono's house drew Mom and
  Crono over the railing (issue #504). A layer split in its line effects'
  maps alone has their copies of the tiles in front only where background
  VRAM has room for them beside a place for each tile the maps name, each
  cache's in its window (while Mom's dialog held the panel's room, the
  copies left the caches short and the room showed holes); that is decided
  by a scene's first layout and kept while the scene lasts, since a split
  that came and went moved what the last frame's lines showed. The copies
  go in from the bottom of background VRAM, after the caches have kept
  their places and off the places the last layout's tiles had, so the
  copies and the panel's room made from the top stay where they were. A
  layout in which a tile a map still names lost its place to a map or a
  room hides the picture while its work goes on, as one whose caches start
  afresh does. Where the layer kept for mode 2's
  moved columns leaves none to split, the first layer that would be split
  has its tiles in front shown again on that layer, from a copy of them,
  on the lines no run takes it (the menus' lines below their lists are
  mode 1 and take none), at their priority; a run of that layer stands in
  front where its map's units under the run have tiles in front. The
  naming screen's help window covered by the alphabet's rows, and the
  equipment's list of what fits covered by the members' rows (issue #502,
  issue #483), were a second layer's windows in front of a first layer's
  text behind, the second standing behind the first on the GBA.
- **Sprite masks**: where sprites overlap, the SNES shows the one first in
  its table and then sets that one's priority against the layers', where
  the GBA shows the one of the higher priority. The game masks with that:
  Crono's covers in his bed are sprites of the lowest priority before his,
  which hide him and are hidden by the bed. A sprite with one of a higher
  priority after it in the table overlapping it is raised to that priority
  where that leaves fewer of its dots wrong (`raise_shows_more`): where the
  GBA layers between the two priorities have dots under fewer than half of
  its own, as the picture on the screen shows those layers (the last
  frame's line registers, windows included, over the GBA's maps and tiles,
  every fourth line looked at). Else it is the GBA's sprite window, inside
  which a line shows no sprites. The overlaps are looked for only where a
  32-dot square of the screen has a sprite in front of it, at most 16 looks
  a frame. A look is kept with the sprite in the GBA's sprite table (its
  second attribute's unused bits 9-11: that it was looked at, and the
  priority the table doesn't show) and stands while the sprite, both
  priorities, the lines shown and video memory stay as they were; a sprite
  masked past the budget has no look and is looked at again. Masked alone (until 2026-11-16), Crono stepping out of the
  time gate's light showed as the room the light's window cuts away
  (`events-end-of-time-gate` 4473-4714, 368,065 dots summed, now 62,025),
  and Marle was missing beside him in the hall before the jet bike race
  (`events-jetbike-race` 3120-3129, 821 dots, now 20; issue #560). Crono's
  covers stay masked. The looks cost `new-game` 7 GBA frames over 2 more
  (90 against 83) and `events-end-of-time-gate` 34 (907 against 873).
- **Line by line**: the line effects' tables (HDMA) are read down the frame
  as the observer reads them (32 bytes at a time, a transfer's bytes at once
  where they lie in one 32, passing over a slot's lines between its
  transfers), and the windows, layers, maps, scroll,
  brightness and colour math each of the GBA's 160 lines should show are
  worked out where they change and kept where the GBA's registers change.
  Only the lines something happens at are visited: each slot is walked on
  to its next transfer that changes what it last applied (where no two
  slots' targets set the same register, as a battle's eight don't, a
  transfer of the unit a slot last applied changes nothing), the display's
  change below a line, and the line shown after a change; where only a
  layer's scroll changed, a line's registers are the last change's with
  its scroll. The walk is kept cheap a line: once a slot's transfer has
  changed nothing, the rest of a repeating entry's run of the same unit is
  passed over in one go (a battle's windows and scroll stand still down
  most of their lines), units are compared a word at a time and applied a
  transfer at a time, only the slots with transfers left are looked at,
  and the lines visited whatever the slots do (the display's change, the
  bands' first lines, the lines a mode asked of) and the band each line
  is in are found in order rather than searched for at every line. Over
  `events-jetbike-race` that took the picture from 1.20 GBA frames a frame
  to 0.94, and `gato-battle`'s from 0.57 to 0.49. The line counter's interrupt fires a line above each such line and the
  horizontal blank's at its end sets the registers, two interrupts a change
  rather than one a line. A frame whose lines start from what the last
  frame's did (the registers, the slots, the changes below a line, the
  layers shown) and whose tables still hold what the last frame read of
  them (each 32 bytes it read, up to 64 of them, read again; both compared
  by a hash, kept in fast RAM) keeps the last frame's changes rather than working them
  out again; a frame the layout was made in works them out. Where the tables
  change every frame (a battle's, the jet bike race's), one frame in sixteen
  tries again, since keeping each frame's reads costs more than it saves.
  A frame's picture is shown all from one vertical blank
  (`show_the_frame`): its changes are worked out into one end of their
  table while the lines show the last frame's from the other, its sprites
  are kept aside (768 bytes of work RAM) and copied into the GBA's sprite table
  then, and the first line's registers are set and the line effects' DMA
  armed there too. A frame whose work runs on past the vertical blank it
  began in (the naming screen's, as the cursor moves down a row) is shown
  from the next vertical blank, the last frame's staying whole until then,
  unless it shows the same as the last: the line counter's interrupt there
  shows it (`show_pending_at_blank`), arming itself where the frame shown
  had no changes for it. Until 2026-11-16 it waited for the next picture to
  be made, which where a game frame takes two GBA frames (the world map's
  flyover, the jet bike race) is a game frame later: each picture went up
  a game frame late, the flyover's layers a dot or two off the sprites
  drawn over them and the racers in the last frame's places. The driver
  tells a runner the frame whose picture went up as it goes up
  (`gba_video_tell_shown`), not as it is made. Before, the GBA showed such a frame's
  sprites over the last frame's lines for a frame (the hand on the row
  below while the bands still made room for it on the row above, issue
  #520). A frame with more changes than the other end of the table has
  room for (the jet bike race's, up to 160) takes the shown frame's places
  as before. Watched in mGBA as the naming screen's cursor moves over every
  letter, every frame matches `ct_native --view 240x160` but for the
  shading; the five movies take 2,091, 8,665, 28,570, 15,477 and 8,801 GBA
  frames against 2,090, 8,644, 28,402, 15,433 and 8,737 (`title-start`,
  `new-game`, `gato-battle`, `events-jetbike-race`, `menus-sweep`), and
  fast RAM 144 bytes more. With the title's copies (above) too, work RAM
  keeps 29,240 bytes free, 568 above its floor.
  A line effect that sets the mode down the picture (the jet bike race's
  scenery in mode 1 above its track in Mode 7) has the layout made for the
  mode the lines other than Mode 7 show (mode 2 where any shows it), not the
  mode set. The SNES's two windows are the GBA's two; its brightness is the
  GBA's fade to black, by the sixteenths nearest it: rounded down, a fade's
  dimmest steps showed at twice ct_native's brightness, and with them the
  stray tiles of a scene loading under its fade (four GBA frames at the
  opening's first narration, two at several of the attract sequence's
  scene changes).
- **Mode 2's moved columns**: in mode 2 the SNES scrolls each column of the
  first two layers by its own offsets, from two rows of the third layer's
  map, which the mode doesn't show. The menus use it to scroll a list a line
  at a time beside a column that stays put: the techs' members beside their
  list, the equipment's members beside the figures, a shop's count beside
  its wares, the settings' two columns of icons. The GBA has no such thing.
  On each changed line of a mode 2 frame (a line where only the scroll
  changed is worked out whole, since the scroll moves the columns), the
  runs of a layer's columns that the offsets move alike are found, up to
  two a layer. Each run is shown on a GBA layer free on the line, with the
  same map and control as the layer and scrolled as the run is: the layers
  the layout leaves spare, and the third layer's, which a mode 2 line
  doesn't show. The run is shown through a window that
  the SNES's windows leave free (one covering nothing, or showing what the
  line shows outside), with the layer itself left out inside it. Where a
  line below that isn't mode 2 shows the third layer (the menus' lines below
  their lists are mode 1, and show their descriptions on it), mode 2 is laid
  out with it, in front as those lines put it. A frame whose offsets' map
  changed works its lines out again. Finding and showing the runs takes 0.8%
  more of the GBA's frames over `title-start`, 0.6% over `new-game`, 0.1%
  over `gato-battle` and `events-jetbike-race`, all of it on the title and
  the menus, and 12% over `menus-sweep`, whose screens are mostly mode 2
  (2269, 11564, 46285, 26665 and 10896 GBA frames).
  A band whose column is moved by its own lines (the 240x160 view's
  equipment screen, `VidViewPanel.column_end`) moves the columns left of
  the column's end that the offsets move, as runs even where they match
  the layer's own scroll, and the sprites that start there with them; the
  other columns there show the band's own line. The column's state beside
  the panels and bands is in work RAM, so fast RAM is as it was. Over
  `menus-sweep` it takes 2.3% more GBA frames, all of it on the equipment
  screen's 12 bands (10,796 against 10,550); `title-start`, `new-game`,
  `gato-battle` and `events-jetbike-race` take 2,194, 11,019, 43,234 and
  23,704 against 2,196, 11,073, 43,276 and 23,674.
  Where a layer's columns move up and down more ways than two runs show
  (the title's logo, whose every column the ripple moves by its own
  offset), the layer is shown from a copy of its map made in the frame,
  each column the lines show moved down the copy as its offset moves it,
  so that the copy at the layer's own scroll shows what the columns show
  (`show_columns_copied`). A column moved by part of a tile takes new
  tiles, each eight rows of the two tiles it lies across: the columns'
  tiles are kept flipped as their map has them, each run of a column's
  rows with tiles one after another (the strips, made again only as the
  map or its tiles change), so a run's new tiles are its strip from the
  row its offset starts at, one copy a run. A new tile takes the palette
  of whichever of the two gives it more rows. There are two copies, the
  one the lines show and the one made next, in room no cache has (21 KiB
  at the title: two maps of 4 KiB, two runs of 128 tiles and 160 tiles of
  strips), made only where 8 KiB more stay free for the caches, and given
  back by the first layout no line wants it in. A split layer's columns, and columns
  moved across, are runs as before. Watched in mGBA at the GBA's speed,
  the logo had stood still while natively it ripples, jumping each time
  the two runs it could show changed (issue #518); its ripple now matches
  ct_native's frame by frame. A copy is about 29,000 cycles (0.1 GBA
  frames), and the strips about 160,000 in each of the 50 frames the logo
  is drawn in; the five movies take 2,120, 8,147, 28,500, 15,824 and 8,803 GBA
  frames against 2,114, 8,131, 28,437, 15,810 and 8,792 (`title-start`,
  `new-game`, `gato-battle`, `events-jetbike-race`, `menus-sweep`), and
  work RAM keeps 30,240 bytes free against 30,824.
  A line whose scroll alone changed, and which shows exactly what one of
  the last two such lines worked out whole in its band showed, takes the
  change made for that line again (`made_before`, `keep_the_line`): the
  Ocean Palace's scenes in the attract sequence step the first layer's
  scroll back and forth a few dots each line over the moved columns, and
  every one of their 160 lines was worked out whole, the moved columns'
  runs found again each time, about 20 GBA frames a picture (issue #568's
  heaviest pictures). Lines whose registers are their own line's as well
  as their looks' (Mode 7's plane, a drawn panel, runs in front of a layer
  shown again, a band cutting its maps, whose cut follows each line's
  scroll) and lines showing a copy of moved columns aren't kept. The
  heaviest of those pictures came down from about 20 GBA frames to 12;
  the rest of their cost is the lines where the palace's edge window also
  moves, each a new look. Over the attract sequence's two Ocean Palace
  stretches of slow pictures (game frames 8,796-10,067 and 19,845-21,083)
  the census counts 2,497 and 2,456 GBA frames against 3,161 and 3,119,
  and its clusters' visibility falls 16%; what it finds new are frames
  that moved by one and a strip at the screen's right edge for one GBA
  frame as each scene opens, which comes of the picture's new timing:
  each line taken again was checked against the line worked out whole,
  over 15,000 of them in `attract-idle`, and none differed.
  `attract-demo` takes 32,389 GBA frames against 33,708, its heaviest
  picture 13.99 against 20.21 and its pictures' 99th percentile under 4
  GBA frames against 10.25. The kept lines live on the driver's stack
  (3,162 bytes at most against 2,994), and the work stays out of the
  lines' loop, which inlined took the menus and the race half a percent
  longer; the five movies take 1,869, 7,724, 24,641, 12,872 and 7,799 GBA
  frames against 1,869, 7,723, 24,631, 12,879 and 7,742.
- **Colour math with the fixed colour** (the sub screen added where it has
  no layer on it is the fixed colour too, which is how the game does most
  of it: the dialog's and the battle window's shading, the race's fog at
  its horizon, a scene's fall into darkness): on the backdrop it is worked
  out exactly. Added to layers with nothing behind them but the backdrop
  (a window's background, the text in front of it: the shading's top half),
  it is exact too, any colour: the GBA's blending of those layers with the
  backdrop at full weight (half where the SNES halves), the line's
  backdrop the fixed colour, adds each channel held at 31 as the SNES does
  (`add_by_blending`). Packed with a colour profile, the GBA blends colours
  already shown, so the backdrop takes what the sum adds to a window's dark
  colours as shown (`added_colour`) rather than the fixed colour shown: in
  `port-style` the census's colour mismatches then fall 23% in
  `gato-battle` and `menus-sweep`, where the fixed colour shown had raised
  them 2-6%. Its steps are changes rather than strength steps,
  about eight more a frame while a window shows. In the census (replayed,
  fast waits) the colour mismatches fell 24% in `gato-battle`, 30% in
  `menus-sweep`, 16% in `new-game`, 11% in `attract-demo` and 5% in
  `events-jetbike-race`, with no new picture cluster; the GBA's frames for
  `gato-battle` and `menus-sweep` rose 0.9% and 2.2% with the extra
  changes. Elsewhere on layers, where
  the colour is a grey, it is the GBA's
  brightening or darkening of them by sixteenths, a grey of k added
  brightening by 16k/24 (against a window's dark colours, 7 of 31) and
  subtracted darkening by 16k/20 (against a scene's brighter ones). Where
  the SNES halves the sum (the fixed colour's math with `half`, which the
  save screen dims its other slots with), it is darkening by 8 -+ 8k/20, a
  colour of 20 halved with the grey, and so where several of the math's
  layers stand over one another, which blending with the backdrop leaves
  whole (`halved_shading_steps`). Math the colour window limits to part of
  the line is kept to that part by the GBA's windows (below, "Colour
  math"). In the census (replayed) the save screen's
  picture clusters in `events-jetbike-race` and `menus-sweep` (1.6M and
  1.5M pixels summed) fell to 0.1M; with `--colours port-style`
  `menus-sweep`'s picture mismatches fell from 18.7M to 2.3M, `new-game`'s
  from 2.4M to 0.9M, `events-jetbike-race`'s from 7.5M to 3.1M and
  `gato-battle`'s from 7.2M to 5.3M, the menus' windows' feet now steps
  off rather than whole shades; the colour mismatches that took their
  place are smaller (a menu frame's mean step 3.8 to 2.9) though about 7%
  more pixels. A line where only
  the colour math changed takes the last change's registers with its
  blending, and where only its strength (BLDY) changed it is no change at
  all: DMA copies each line's strength in the horizontal blank above it
  (a 322-byte table in work RAM), so the shading's step every few lines
  costs no interrupt. In a battle with Gato the window's shading took the
  frame's changes from 19 to 8.
- **Mode 7**: the GBA's affine layer (BG2 in its mode 2) reads a 128x128
  map of bytes and 256 tiles of 8-bit pixels as the SNES's plane does, so a
  layout made while a line shows Mode 7 keeps the GBA's first 32 KiB of
  background VRAM for it, the SNES's first 32 KiB split into the tiles
  (char block 0) and the map (screen block 8) as the units change, and the
  tiles' caches take the rest. A layer no screen shows whose map lies there
  is left out; one shown there leaves the plane unshown. The upper 128 of
  the GBA's background colours are the SNES's own where no 2bpp layer needs
  them for its copies. A Mode 7 line is the GBA's mode 2 with the plane on
  its third layer; the plane's matrix and its point at each line's left end
  are worked out as the native renderer does (`plane_line`) into a table of
  the 160 lines (2.5 KiB, kept in background VRAM above the tile caches,
  which work RAM and fast RAM haven't to spare), those no change sets stepped from the line above by the
  matrix's second column as the GBA would, and DMA copies each line's into
  the affine registers in the horizontal blank above it. The race's
  perspective changes its matrix every line; as changes, two interrupts a
  line, its picture took 1.67 GBA frames a frame, with the table 1.19. What
  lies outside the plane is the plane again or nothing, at the priority
  between the sprites of priority 1 and 0. A layout made while no line
  shows Mode 7 gives the room back. The title's clock hands and the race's
  track are Mode 7. The screen's whole state as a mode sets it up
  (`vid_apply_screen`) includes Mode 7's, the windows and the colour math,
  as natively (the GBA's had left them as they stood).
- **A frame's changes stop at its 160 lines**: a change a line on every
  band line with the black bands' changes beside them can ask for more,
  and past the list they would write over what follows it in work RAM.
- **The view** is a smaller view, 240x160 ("The smaller view, as built"):
  the game aims the camera and keeps panels in view as it does natively, and
  the GBA works the frame's bands out from them (`band_up_the_view`): runs
  of its lines that each show a run of the SNES's lines from a dot across,
  or black. The lines are worked out down the SNES's picture, each GBA line
  given its band's scroll (the band's dot across, and how far its SNES line
  lies from the GBA's); a sprite takes the band its top line, or its foot's,
  is in; a line the SNES blanks shows black, not the GBA's white forced
  blank. A layer's row is its line's and its scroll, one further down than
  the GBA's (the SNES's first line shown is its line 1). Every layer the
  layout has is enabled on every line, and a line shows the ones it does
  through the windows (empty where the SNES has none): the GBA shows a
  layer enabled part of the way down only from three lines on, which cut
  the top rows off the dialog's first line. Its state is in fast RAM
  (IWRAM).
- **The drawn panel** (the dialog laid out again): its lines take the looks
  of the SNES lines they are spread over and draw its two layers from the
  panel's own memory, no sprites. That memory is the GBA's VRAM alone:
  each `vid_send_panel_vram` is converted as it comes, by the panel the
  frame stated first, a map's entries into a screen block of the panel's
  and the tiles it sends (2bpp widened) into a run of places, the first
  sent since the room was made the run's first; the frame's tiles are the
  screen's, from its cache. What was sent stays, and the dialog sends only
  what changed (above, "The smaller view, as built"): the view's memory
  (`vid_view_memory`, 640 bytes of fast RAM) is cleared by every layout,
  which has it send the window afresh. A layout makes the room once a
  mode asks `vid_view_draws_panels` (until then the answer is false and
  the dialog keeps the screen's band): two screen blocks and a run as long
  as the panel has sent, 256 tiles at first and 600 at most (a 240x160
  screen's), within one char block's reach, 12 to 23 KiB in all. Later
  layouts keep it until the settings move, and make it where it was where
  that is still free, so that what was sent stays shown; a map of it made
  elsewhere is blanked by the first send to it, rather than showing what
  its places held (rows of stray tiles in the opening's first window as
  it opened). A layer whose tiles the panel
  sends draws only those. A send is converted as it comes, so a glyph may
  show a GBA frame early.
- **A cut** (the battle window, two columns cut from inside it): the maps
  its lines show are kept a second time with the columns left out (a
  32-column map's, whole columns where the layer's scroll puts the cut),
  converted again only as the game changes them or the cut moves, and its
  windows' edges and sprites move across as the cut moves the dots.
  The table of line effects' maps holds ten, nine of them cut at most: a
  battle's line effects show six and the cut takes three more, which a
  table of six or seven left no room for (the window shown whole, 16 dots
  right of ct_native's, its gauges past the screen's edge). A band that cuts a map notes its cut copy alone, the
  map whole only where the copy found no room: the whole map, shown by no
  line, was left out by each layout and noted again, a layout a frame in
  the battle's menus.
- **The bands** are worked out a run of lines at a time, between the lines
  where a panel starts or ends or a band passes the SNES's first or last
  line, rather than a line at a time: at the title that took the picture
  from 0.21 GBA frames a frame to 0.12. A band's lines whose rows a band
  above showed already are black, the rest of it shown from the next row
  (`black_out_rows_shown_above`); only where a drawn panel's rows would
  be shown again does the view fall back to the camera's band alone. As a
  dialog's window opens, the drawn panel looks like the rows the window
  grows over and the camera's band below it reached back into them: each
  opening fell back for four frames, the dialog's window where the SNES
  has it, and the dialog's map those lines showed came into the layout
  with its tiles (issue #589).
- **Room.** The panel's room comes out of the tile caches' places. To give
  some back, a layout leaves out the maps no line showed since the last one
  (the field's layers' own maps, which its line effects replace on every
  line, and the screen's dialog's while the panel is drawn), and the tiles
  only they name; a map left out that a line shows again has the next
  layout bring it back. In Crono's room with Mom's dialog the caches went
  from full (tiles shown blank until a layout a second on) to 470 places
  spare; in the busier screens at the Millennial Fair they are still full
  during a dialog, as they were before it.

Approximated, or not shown yet (issue #483):

- Per-tile priority where more layers have tiles of both priorities than
  the GBA has layers to spare: the field, with three layers that have them,
  splits one, and mode 0 none. Mode 1's third layer in front, where the mode
  puts it in front of every sprite, is in front of every layer but the
  sprites of the lowest priority show in front of its tiles behind too.
  With no tiles in front it stands behind the first two, as on the SNES,
  even where only the lines show its map: the light circles 12,000 BC's
  cave and Lucca's house keep on it out of sight covered both rooms, and
  the End of Time's lamp lit its floor in streaks. Where mode 1 splits the
  first layer and the second stands unsplit behind, the first's tiles
  behind covered the second's in front; where the two share their tiles
  and every line that shows the first's map shows the second's beside it
  alike (scroll, screens, windows and colour math: `layers_alike`), as a
  battle's lines do, the second's entries in front go into the first's copy
  of its tiles in front wherever the first's entry stands behind
  (`MapPair`, `convert_merged`), converted again as either map changes, and
  a line that shows the first map otherwise undoes it. The candelabra's
  tops in Magus's castle's battle show (the census's clusters at
  `attract-demo` frames 6629-7230 and 17680-18277, 420,287 and 447,594
  pixels, now none; `menus-new-game-plus`'s pictures 4.36 million pixels
  against 0.32 and `battle-menu-buttons`'s 7.09 against 8.89; over the
  census's default movies 28.7 million against 34.7, in 0.2% more GBA
  frames, no movie's more than 0.7%). A
  split takes its copies' room from the tile caches. The layers are
  assigned for the mode the frame starts in; a line effect that sets the
  mode again part of the way down sets the third layer's priority with it
  (`third_layer_as_the_line_puts_it`). The battle's windows keep it and the
  battlefield below them drops it: there the third layer stands behind
  every sprite, or, where the line's colour math adds the sub screen it is
  on to the first two layers, in front of them as the blending's first
  target and behind the sprites of the highest priority (the battle's),
  the same sum. Before, Flea's red cross in the attract demo covered Crono
  and the damage shown; sprites of the lower priorities still stand behind
  it there. The flash across the battlefield as the cross begins is the
  fixed colour, added where the sub screen's third layer is windowed to the
  cross and so shows nothing: there the first two layers are first targets
  too, over the backdrop at the fixed colour. A line effect that sets both
  screens' layers sets the sub screen's windows with them; the GBA's lines
  had kept only the main screen's, so the flash was missing (1.15M and
  1.13M pixel-frames in the census at attract-demo frames 4098-4108 and
  15150-15160, now none). Where the third layer keeps its priority but is
  the light the sub screen adds to the first two layers, and isn't split,
  it stands behind every layer on those lines, as the blending's second
  target, its few tiles in front with it
  (`third_layer_behind_where_it_lights`): in Guardia's throne room its glow,
  nearly all tiles behind, had covered the stained glass in front of it (the
  census's clusters at `attract-demo` frames 3281-3504 and 14330-14553,
  428,178 and 427,116 pixels, now none).
- A sprite mask hides every sprite inside it, not only those after it in
  the table, and doesn't show itself where no layer covers it; a sprite
  raised instead shows over the dots of the layers between where they
  cover fewer than half of its own.
- Colour math: the fixed colour added to layers with only the backdrop
  behind them is exact, but where the SNES's backdrop shows on those lines
  the GBA's shows the fixed colour (the backdrop is one colour a line),
  except where the colour window keeps the math to part of the line and a
  GBA layer is free there (below). Math the colour window keeps to inside
  or outside itself is kept there:
  each of the GBA's window regions blends (its effects bit) only where the
  SNES's colour window lets the math through (`math_in_region`), the
  windows taken for it where the layers' windows leave them free. Where
  they don't, the math is done across the line as before: a frame laid out
  for mode 2, whose moved columns take the windows, and a line whose
  layers' and colour windows together make four different regions. Magus's
  spell in `battle-menu-buttons` (frames 4464-4588) draws a thin ring of
  red, two windows' difference; added across the line, the whole
  battlefield had turned red. In the census (replayed) that cluster fell
  from 5.65M pixels summed to 1.84M, `battle-menu-buttons`' picture
  mismatches from 7.37M to 3.56M (8.31M to 4.33M with `--colours
  port-style`), and the other eight movies' within 1%, none with a new
  picture cluster.
  What was left was the backdrop's red outside the ring: the battlefield's
  black corners. On such a line a GBA layer that no region shows (there, the
  battle's text layer, which shows only in its window above) shows the
  fill instead (`fill_behind_the_added`): one colour, the fixed colour's, in
  the regions with the math alone, behind every other layer, as the
  blending's second target in the backdrop's place, and the backdrop keeps
  its own colour. The fill is a screen block every byte of which is one
  nibble N of 8-15 twice (`FillRoom`): each entry then names tile N*0x111
  in palette N, a 2bpp copy's palette whose colours past the fourth no tile
  uses, and for a char block that puts that tile inside the screen block
  itself, the tile is solid colour N, so the block is both the map and its
  tile, 64 places taken only while a line wants it. Its colour rides in the
  line's strength, which blending two targets doesn't read, and the line
  sets it in the palette. A line after one with the fill is worked out
  whole where the math changes, the fill taken away. In the census
  (replayed) the cluster fell from 1.84M to 55,417 (1.87M to 74,563 with
  `--colours port-style`), `battle-menu-buttons`' picture mismatches from
  3.56M to 1.79M (4.33M to 2.53M), the other movies' within 3%, none with
  a new picture cluster. Inside the math the backdrop's own dots still show the
  fixed colour, and a line with no layer free keeps it on the backdrop.
  Elsewhere the fixed colour on layers is a grey's
  brightening or darkening (above), so a colour that isn't a grey isn't
  added to them (Magus's castle's candle flames, issue #554), and the
  shading taken away is near the SNES's rather than its own: a window's
  foot a few steps lighter or darker than natively, the race's fog a little
  deeper. The sub
  screen added to the main screen is the GBA's blending at full weight
  where the sub screen's layers are on the main screen too, and halved only
  with those: where the sub screen shows nothing the SNES adds the fixed
  colour unhalved, so the Millennial Fair's ground, halved with a sub screen
  of its own, stays as bright as natively (halved with the backdrop, the
  whole Fair was drawn darker). The sub screen's own layers (those the main
  screen doesn't show: the Fair's light, a room's light added to its floor)
  are shown on such a line behind every layer, at the GBA's last priority,
  where the math's layers show, as the blending's second target
  (`show_the_sub_screens_own`): under the math's tiles they are added as on
  the SNES; where another layer stands between them the tiles show
  unblended, and where no main layer covers them they show themselves. In
  the census (replayed, fast waits) `gato-battle`'s colour mismatches fell
  4.6% and its picture mismatches 12%, what is left of the Fair's clusters
  the dialog's shading, at no cost to measure; where the light layer moves,
  it now shows a frame late in places (`gato-battle` 8697-8793 and
  10164-10537, `frame` clusters). A sub screen's layers subtracted and the
  colour window on the sub screen's math isn't (the time warp shows its
  waves without the glow).
- Mode 7: tile 0 outside the plane shows as nothing; EXTBG's second layer,
  the plane's mosaic and direct colour aren't shown; a frame whose plane's
  room was not yet made (the first frame a line shows Mode 7) shows its
  sprites alone on those lines. Mosaic and mode 0's fourth layer aren't
  shown. Mode 2's columns moved across past what the GBA's two windows and
  its free layers show (above), or where the SNES's windows use both of the
  GBA's, and those moved up and down on a split layer past them, show as
  their layer lies; the clouds before the Ocean Palace in
  the attract sequence, mode 2 too, still show black.
- Where both windows cover a dot, the GBA shows its first window's choice,
  so `window_regions` gives that one whichever SNES window shows the same
  there. Where neither does, it carries the windows' meeting, and the second
  carries whatever of the rest differs from outside them. The palace rising
  in the attract sequence masks its layers outside both windows (its
  ragged edges and its curtain), so it shows only where they meet; it had
  shown nothing but a sliver at the left (issue #565). Its census picture
  cluster went from 16,797 pixels a frame to 6,191, what is left being its
  shimmer and ripple. A line whose four regions all show differently keeps
  window 1's choice where they meet.
- A sprite whose tiles run past a SNES tile table's row or table end takes
  the next row's on the GBA.

**The sound.** The C sound driver (`game/sound/`) runs as natively and
plays its eight voices through the platform's voices interface; the GBA
answers it with `services/mixer.c` in the native mixer's place
(frontend/sound_native.c's `SOUND_NATIVE_MIXER`, and
`SOUND_NATIVE_MIXER_PLAYS`, which hands it the driver's time a pass at a time
rather than a call for each of the S-DSP's 32,000 frames a second). The
driver's time passes in its timer's steps, but a stretch of them with no
command on the ports, no echo to wait out and no tick due passes at once
(`quiet_cycles`): a handful of steps a frame rather than 134, the same ticks
at the same steps, natively too. The driver's time only sets what the voices
play; the mix is made in the GBA's time, as the playback asks for it (below,
"Out"). What the
driver reads back of the voices and the mix (their settings) is what it set,
masked as natively, so the game's state is the same; the four movies measured below
end as ct_native's with the sound playing.

- **The samples are converted when the cartridge is packed.** The native
  mixer's 64 KiB of sound memory is more than the GBA's work RAM has spare,
  so sound memory isn't kept. `host/samples.py` decodes every sample the
  driver can write (the songs' 62 sample blocks and the boot's own nine) from
  BRR as the S-DSP plays it, its loop decoded a second time with the samples
  before it as they are when the loop comes round, then resamples it to the
  mix's rate at its own pitch (`resample`): low-passed first, a windowed sinc
  cut at 0.9 of the mix's half rate, so that what the mix can't hold is left
  out rather than folded back, and a loop made a whole number of the mix's
  samples, resampled as it sounds coming round, with the rate that took
  (`MixerSample.rate`) beside it so the voice keeps its pitch. Each is scaled
  to fill 8 bits, keeping its peak, and stored offset by 128, and a sample
  that loops (and the noise) is followed by 256 more samples of its loop
  (`MIXER_LOOP_SPARE`), so the mix reads on through a chunk without going
  back: 161 KiB of PCM where the S-DSP's own rate took 395 KiB, and the
  noise generator's whole sequence, played from the cartridge (the packed
  cartridge 13.88 MB, 231 KB smaller). The table says which rate it was made
  for, and a program mixing at another plays nothing rather than every note
  out of tune. A song's load sends each sample
  block into sound memory whole, as a run of the cartridge's bytes
  (`au_sample_memory_send`), and the mixer knows which block by where the
  run comes from (each block's length, a hash of it and its bus address are
  in the table), reading none of its bytes; the boot's samples, written a
  byte at a time, it knows by their hash. It follows the
  blocks as the driver moves them, so when the directory points a voice at
  a place, it plays the packed sample there. Only the driver's own sample,
  45 bytes it rewrites as it plays, is decoded on the GBA.
- **The mix** (issue #544). 10,512 samples a second, 176 a GBA frame (a
  sample every 1,596 cycles), mixed a quarter of the ring (88) at a time.
  What the voices play is planned once a GBA frame, two chunks (`plan`):
  the keys taken, each sounding voice's envelope moved on by the S-DSP
  frames the plan lasts (352 samples every 1,065 frames, so a GBA frame's
  176 samples are a game frame's 532.5), and its step and both sides' gains
  set; a silent voice is passed over. The envelope takes its steps one at a
  time where a step can change its phase, and where the level only falls on
  (a sustain, an exponential gain) all at once, a band of 256 levels at a
  time (`falls_by`, the same as the steps one by one). The voices' state is
  in fast RAM (`playing`), the mix reading it every chunk. Each voice adds
  its sample times its gains, both sides in one word with one multiply. The
  loops are ARM in fast RAM (`services/mix.s`): `mix_pair` adds two voices
  whose waves go on through the chunk at once, three samples a turn, so the
  mix's words are loaded and stored once for the two; `mix_wave` adds one
  (the odd one, or one that ends or loops in the chunk), four samples a
  turn. The PCM's offset lets the processor's unsigned byte load take the
  position's whole part as its index with no shift of its own, and the
  offset is taken off where the mix starts (`mix_start`, with the 0x4000
  each side keeps its range by); `to_8_bits` then tests four samples at once
  for a clip and, where none is, as nearly always, masks each side's bytes
  out of the words four to a word straight into the ring.
- **Out** (`services/direct_sound.c`): the GBA plays the sound by itself.
  Timer 0 plays Direct Sound A on the left and B on the right from their
  FIFOs, and DMA 1 and 2 feed the FIFOs from a ring of 352 samples a side
  (two GBA frames) as they run low, as the hardware's sound DMA does, with
  no work a sample. Timer 1, counting timer 0, interrupts every quarter of
  the ring (88 samples): after the last quarter it gives the DMA the ring's
  start again, with the FIFOs emptied of what it read past the end, and each
  time it has the mixer fill what was played (`mixer_fill`). The fill runs
  from the interrupt with the other interrupts on (`gba_interrupt_nested`,
  `system/nested.s`), on a stack of its own, so the lines' interrupts don't
  wait behind it, and so the sound is made as fast as it plays whatever the
  game's frame takes. The driver's calls (`begin_write`) keep the mix from
  voices half set: an interrupt that comes in one leaves the ring to the
  next, a quarter on, with three quarters still queued; a song's load keeps
  only the block it then knows from the mix.
  A quarter about to play that wasn't filled is silence, never the ring's
  last turn again. So the sound no longer gaps or drops where the game's
  frame runs long (issue #488): before, the mix was made as the driver's
  time passed, 224 samples a game frame and bursts in the game's waits on
  the driver, and a slow game's ring ran dry (21-60% of the time silent in
  the movies below) and overflowed in the bursts (1.4-5% of the mix
  dropped). Where the game's frames take longer than the GBA's, the music's
  steps come as the game's frames do while the notes sound on; at full speed
  the two are the same. The mixing costs its share of every GBA frame
  rather than of every game frame: the movies take 0.5-6.1% more GBA frames
  where the game runs slow, and nothing more once it keeps its time.
  `host/run.py --wav` records what mGBA plays; `--stream` records every
  sample the program queued, from the ring the program names in its log
  with its length, sides and rate. Neither plays anything aloud.

  What it costs and how it sounds (`run.py --profile`, the mixer's
  functions together: `plan`, `run_envelope`, `falls_by`, `take_keys`,
  `mixer_fill`, `mix_chunk`, `pass_wave`, `mix_start`, `mix_pair`,
  `mix_wave`, `to_8_bits`; the playback's interrupt is 0.13% more), against
  the 13,379-sample mixer of 32 at a time before: `gato-battle` 5.17% of the
  GBA's samples against 12.53%, `title-start` 4.91% against 12.00%,
  `new-game` 4.35% against 11.53%, `events-jetbike-race` 5.24% against
  13.01%, `menus-sweep` 5.78% against 13.59%, `attract-demo` 5.64% against
  13.53%; GBA frames over the movie 25,157, 1,878, 7,809, 13,529, 7,685 and
  34,368 against 26,160, 1,881, 7,951, 14,146, 8,107 and 35,435, each ending
  as ct_native's, with the same 176 samples of silence at the start. Of
  `gato-battle`'s 5.17%, the voices' loops are 2.95% (12 cycles a voice's
  sample in `mix_pair`, of which the cartridge's byte read is 6 and the
  multiply 3), the 8 bits and the mix's start 0.9%, the planning and the
  chunks' rounds 1.3%. Under 4% needs fewer samples (about 7,500 a second at
  these costs) or the voices' reads out of the cartridge (issue #544). The
  sound against ct_native's (`ct_native --wav`, the GBA's `--stream`), each
  movie's whole spectrum in bands, dB against 100-4,032 Hz: in
  `gato-battle`, ct_native, the mix before and the mix now are within 0.6 dB
  of each other from 60 to 3,200 Hz (100-200 Hz: -10.3, -10.1 and -9.8;
  800-1,600: -3.6, -3.7 and -3.7); from 4,032 to 5,000 Hz -23.3, -18.6 and
  -19.0 (both GBA mixes carry 4-5 dB of folded or 8-bit noise there); from
  5,000 to 8,000 Hz -20.8, -16.9 (folded back from above) and -23.7 (cut
  above the mix's 5,256 Hz). Aligned 4 seconds at a time, the loudness
  envelopes correlate with ct_native's 0.802 now and 0.802 before, and
  differ by 3.1 dB on average now, 3.3 before. The census over its 16
  movies and two runs by hand, against the mixer before: every movie ends
  as ct_native's, no run hangs, and every run takes fewer GBA frames (0.2-6%,
  `attract-idle` 26,700 against 27,769), its stretches of slow or left-out
  frames cost less (8-25%) and its clusters' pixels times GBA frames are
  fewer (but `title-start`, 1% more); only `attract-demo` has more slow
  frames (2,792 against 2,760) and `field-12kbc-1f4` more pictures left out
  (580 against 573).

Approximated, or not heard (the S-DSP's own detail the GBA can't afford; issue #457):

- The Gaussian interpolation: the nearest sample of PCM low-passed and
  resampled to the mix's rate when the cartridge was packed, so what lies
  above 4.7 kHz at a note's own pitch is lost; a note played above its
  sample's pitch reads past samples and can fold back.
- 8 bits: each sample at its own scale, and the mix.
- The echo (its delay, its eight taps and its feedback) isn't heard, nor
  what its buffer writes over the samples under it; the voices sent to it
  are heard dry, the mix a little quieter where a song leans on it.
- Pitch modulation isn't heard. Noise is the generator's sequence played at
  its clock.
- Timing within a GBA frame: key ons and offs take effect at the next plan
  (16.7 ms apart), in the order the driver asked, without the S-DSP's five
  frames to start a note, and the ring keeps up to two GBA frames mixed
  ahead of what plays, so a note is heard up to 50 ms after the driver starts
  it; envelopes and volumes change once a GBA frame, their steps not on the
  S-DSP's shared clock. The driver's own sample is decoded again when a
  voice starts it.
- A mix louder than twice what 8 bits holds wraps rather than clips.
- The meter's output (command `$FF` `$01`, docs/SOUND.md) is a voice's
  sample at its level, not the S-DSP's output; the game never turns the
  meter on.

**A picture left out where the frame runs late** (`system/main.c`,
`Pictures`). A frame's picture is due at the vertical blank a GBA frame
after the last picture's. Where the game's frame and the sound driver's end
past it, the driver leaves that frame's picture out and starts the next
frame at once, so the game catches up on the GBA's time rather than waiting
for the next blank and then converting; never two in a row, so the picture
shown is at most one game frame behind. Nothing the game does depends on
its picture: the picture is made from the video memory the game's calls
leave, a picture left out leaves its changes for the next, and the view's
framing reads only what the game's frames set. The movies end as ct_native's
and their lag frames are the same. In mGBA, the GBA frames over the movie
and the pictures left out (`title-start`, `new-game`, `gato-battle`,
`events-jetbike-race`, `menus-sweep`): 2,180, 9,753, 35,849, 19,109 and
9,943 against 2,214, 11,181, 43,752, 24,352 and 11,173 (1.5-21.5% fewer),
with 68, 1,668, 8,157, 3,678 and 1,657 of 1,800, 7,200, 21,600, 9,400 and
6,424 pictures left out (4-39%). Where the game keeps the GBA's time no
picture is left out. Allowing two in a row took `gato-battle` to 34,523 and
`events-jetbike-race` to 17,147, with half the pictures left out.

**The pictures' pacing** (`system/main.c`, `Pacing`; issue #521). The
report counts how many GBA frames apart each picture shown is from the last
(a histogram, 0 to 15 and more) and keeps the eight longest gaps, with the
game's frame whose picture ended each and whose work it was: the last
picture's making after its blank, and the heaviest game and sound of the
frames between. A picture waits for the blank it is due at, never the one
the last picture was shown in: a short frame that ended inside that blank
showed its picture in it too, so the GBA never showed the last one and the
game ran a frame ahead of the GBA's time, two game frames in a GBA frame and
then none. Played as by hand (`mgba_run` with no decisions to replay, the
GBA's own time deciding the game's frames), `new-game` showed 345 pictures
in the same blank as the one before (of 5,900), now none: its pictures
apart 0, 4,676, 507, 517 and 185 GBA frames (0, 1, 2, 3, 4) against 345,
4,329, 496, 533 and 183. The game's frames no longer run ahead of the GBA's,
so the movies take longer in mGBA, replaying ct_native's decisions
(`title-start`, `new-game`, `gato-battle`, `events-jetbike-race`,
`menus-sweep`): 2,219, 9,030, 30,318, 16,831 and 9,345 GBA frames against
2,140, 8,580, 29,625, 16,580 and 9,229 (1.3-5.2% more), each ending as
ct_native's. The gaps left are the game's own long frames
as a map, a menu, a battle or a song loads (below, "Loads"), and a
layout's picture of up to 28 GBA frames.

**The census** (`host/census.py`, targets/gba/README.md). Every picture the
GBA shows, as a player sees it at the GBA's speed, against ct_native's at
the GBA's view for the game's frame the driver last put on the screen (the
drive box's `shown`), at 5 bits a channel with no tolerance: a mismatch is
torn, a whole frame off, off by at most 8 steps of 31 (`colour`) or drawn
wrong (`picture`), and mismatches make clusters ranked by pixels times GBA
frames; the same records give every game frame over 2 GBA frames, every
picture left out and every hang. On main d9177e1d over 16 movies (110,000
game frames, in about 14 minutes) every movie ends as ct_native's, nearly every
GBA frame differs somewhere, and no run hangs, five minutes of the attract
played by hand included. The largest clusters: the Fair drawn darker
(`gato-battle` 11,685-18,648, issue #562), the gates' white circles (#561),
the attract's clouds black (mode 2, issue #483), the battle windows laid out
16 pixels right (#563), the End of Time's lamp in streaks (#564), the race's
sky and racers (#565), the attract battles' missing sprites (#550), and the
menus' and load screen's window shading (#554). The costliest stretches of
pacing: the race (2,704 game frames in 6,165 GBA frames), the End of Time,
and the attract's battles and the Ocean Palace's clouds (#550).

**Loads** (issue #521). A frame in which a map, a menu, a battle or a song
loads stops the picture for as long as it takes, and those were the GBA's
longest frames: 123 GBA frames (two seconds) as the title's song loads,
88 as the menus open in `new-game`, 226 as a map with its song loads in
`gato-battle`. Profiled over those frames alone (`run.py --profile FILE
--profile-frames FROM-TO`, the runner's samples limited to a run of the
game's frames), three causes stood out, each now
gone or shortened: a song's samples were sent to sound memory reading each
byte's block's place in the cartridge again and the byte through the
catalogue's search (the block was then found once and read on through,
rom.h's `RomRun`), 70% of the title's song load; the menus'
clear of all 64 KiB of VRAM went a byte at a time through the port (a fill
through a port that takes words in order is compared and put a piece at a
time, `vram_fill`, as a send's bytes are); and the unpacker, which a load
runs over tens of KiB a byte at a time, ran Thumb from the cartridge (it was
in fast RAM after, 1.2 KiB, until the streams were kept unpacked, below). The heaviest frames
fell from 123, 123, 226, 133 and 123 GBA frames to 49, 53, 108, 71 and 56
(`title-start`, `new-game`, `gato-battle`, `events-jetbike-race`,
`menus-sweep`), and the movies take 2,031, 8,382, 28,800, 16,166 and 9,096
GBA frames against 2,144, 8,609, 29,794, 16,717 and 9,285, each ending as
ct_native does. What was left of them was mostly the unpacker's own work, a
byte at a time through the state's accessors, and the mixer's matching of
a song's bytes against its packed samples (issue #525).

**Streams kept unpacked** (issue #524). The unpacker was still about
10,900 of the 15,000 samples of the frame that opens the menus in
`new-game` (game frame 3248) and 11,000 of the 30,300 of the map load at
`gato-battle` frame 11580. The packing step now unpacks the cartridge's
packed streams itself (`host/streams.py`, the game's unpacking step for
step), and keeps each one whose unpacking reads nothing but its own resource
and no byte from before where it starts (1,493 of the catalogue's 1,502
`lz` resources, 3.9 MiB unpacked) with the counts its unpacking's time is
worked out from. Where a stream is kept and the place it goes takes it as
it is (in one bank, in pages owners keep whole: `wram_put_unpacked`, which
the flat state answers from a bit a 256 bytes, `vars_flat_whole_pages`),
the game copies it in (`memcpy`, or a byte at a time into the SRAM's cold
top, where the menus unpack their graphics), charges what unpacking it
takes and leaves the port where unpacking leaves it
(`game/data/decompress.c`, `put_unpacked`); ct_native's sources keep none,
so its unpacking and its time are as before. The rest is unpacked as
before, a literal run going out through the port as one span
(`hw_port_write_span`) and a reference as one copy (`wram_copy`, a span of
the flat state's storage), each charged as the bytes a time were. Profiled
over those frames alone: the menus' frame takes 4,466 samples, the
unpacker's part about 1,150, and the map load 19,321 (the gap before its
picture 69 GBA frames, from 108), the unpacker's about 770. The heaviest
frames fell from 49, 53, 107, 71 and 57 GBA frames to 43, 43, 69, 46 and 51,
and the movies take 2,034, 8,599, 28,485, 15,770 and 9,080 GBA frames
against 2,068, 8,708, 28,895, 16,004 and 9,067 (`title-start`, `new-game`,
`gato-battle`, `events-jetbike-race`, `menus-sweep`), each ending as
ct_native does. The pack grows by 3.94 MiB; the unpacker leaves fast RAM,
where its copy (`put_span`) takes 228 bytes.

**Samples known by where they come from** (issue #525). A song's load still
sent its samples a byte at a time, and the mixer hashed each byte to find
the packed block they made (`written`): about a third of the map load at
`gato-battle` frame 11580 and most of the title's song load. The voices now
take a run (`au_sample_memory_send`), which a song's load sends a block at
a time, the block's bytes in place (`rom_bytes_on`, read on into the next
bank as the original's reads go, where blocks 12, 33 and 48 cross one),
charging the reads of its bytes as reading them did; the native mixer puts the bytes in
place one after another as before, and the GBA's knows the block from the
run's address, which the packing step lists with each block
(`host/samples.py`), and reads none of it. The title's song load (the gap
before game frame 90) fell from 35 GBA frames to under 5 and the map load
at `gato-battle` frame 11580 from 69 to 34; the heaviest frames are 43, 43,
49, 46 and 51 GBA frames against 43, 43, 69, 46 and 51, and the movies take
2,002, 8,544, 28,282, 15,611 and 9,042 GBA frames against 2,034, 8,599,
28,485, 15,770 and 9,080 (`title-start`, `new-game`, `gato-battle`,
`events-jetbike-race`, `menus-sweep`), each ending as ct_native does. The
pack grows by 508 bytes (13,824,576 in all), and fast RAM gains 496 bytes,
the hashing's loop gone from it.

**The menus' opening** (the load screen's first frame). As the menus open
they build their tiles from the cartridge, read the save slots and set
each slot's text a glyph at a time; on the GBA that frame took 36-45 GBA
frames, one picture held for most of a second in every movie that starts
from power-on. Profiled over that frame alone, most of it was reads of the
cartridge a byte at a time: the glyph drawer reads its glyph's 24 bytes and
the tiles' places (`menu_text_column_offset`) again for each of a glyph's
24 lines, tens of thousands of lookups for the three slots' text; and the
tile building's block moves from the cartridge (`wram_copy`) went a byte
at a time by address. Now a glyph's bytes and the places are found once a
glyph (`menu_font_bytes`, `menu_text_column_offsets`), each line charged
the reads the original makes (`charge_rom_accesses`); a block moved from
one of the cartridge's resources is written as a span of its bytes
(`rom_run_from`), and bytes in hand through the work RAM port too
(`wram_port_copy`); and `asset_read16` looks its resource up once for its
two bytes. The game's time and outcome are as before. The heaviest game
frame fell from 36.5 to 18.8 GBA frames in `title-start` and from 44.2,
36.5, 41.5 and 39.9 to 23.0, 22.3, 24.6 and 20.7 in `menus-sweep`,
`new-game`, `gato-battle` and `events-jetbike-race` (each a menu's
opening), and the load screen's longest gap from 36-39 GBA frames to
18-25; over the census's 16 movies 3-7% fewer GBA frames
(`menus-sweep` 8,588 to 8,113) and fewer mismatching frames, every end
state ct_native's. The title's fade-in (game frames 870-923) left out 6
pictures and leaves out 4, its game frames shorter. Fast RAM keeps 624
bytes spare (the new paths that run seldom kept in the cartridge,
`copy_from_elsewhere`); the program is 10 KiB smaller.

**A load's frames between its steps** (issue #568). A location's load runs
with the frame's handlers off and the original's frames went by in the
middle of it (36 at `attract-demo` frame 7535 in ct_native, which keeps
them as lag frames at the load's end), while the GBA did the whole load in
one game frame of 24 GBA frames, its sound's frames and picture stopped.
Its steps now say where frames may go by (`tm_let_frames_pass`, "Timing
without a beam"): the location's graphics, map, tile looks and script, the
entering steps, each layer and object drawn as the screen is set up; the
world map's graphics, maps and planes; each of the race's graphics. Played
by hand, the GBA's own time lets them go by there once its load has run a
GBA frame (`frames_pass`); replaying ct_native's decisions, and in
ct_native, nothing changes. The map's unpack, about 9 GBA frames of the
load, went through the work RAM port and the state's accessors a byte at a
time: the stream is now read a row of a layer's map, or a tile's three
attribute bytes, at a time (`hw_port_read_span`), and the tables written a
row at a time (`set_<table>_run`, `fill_<table>_run`, STATE.md "The flat
state"), each charged as the single reads and writes were. Replayed, the
location loads in `attract-demo` take 21.1 and 20.5 GBA frames (from 24.9
and 24.3) and the return from a battle 7.2 (from 13.5). Played by hand
(the census's `attract-idle` and `new-game-by-hand`), the longest frames of
the stretches round loads fall from 16, 22, 21 and 15 GBA frames to 8, 7, 8
and 11; what is left over 8 is the battle's way in (28-30), power-on and
the title's set-up and song. The census over `title-start`, `new-game`,
`gato-battle`, `events-jetbike-race`, `menus-sweep` and `attract-demo`
finds the same clusters as before, none new over a single GBA frame, each
run ending as ct_native's, whose movies and lag frames are as before. Work
RAM is 512 bytes above its floor, fast RAM 616 spare.

**The world map's load** (issue #568). A world map's load took 16 GBA
frames in one game frame (`attract-demo` frames 2210, 10668, 13259 and
21718, `run.py --profile-frames`): its graphics are unpacked into a
buffer at `$7F:9000` that runs on into the SRAM's cold top and sent from
there to VRAM, and both planes are drawn whole. Three things made it long.
The last of the map tiles' seven blocks ends at `$7F:FFFF`, which a
stream kept unpacked wasn't let reach, so it went through the unpacker a
byte at a time, 0.6M cycles; it goes in as one copy now, the port left at
`$7E:0000` after it, where writing through it leaves it
(`wram_put_unpacked`, `put_unpacked`). Runs into and out of the cold top
(the unpacks' copies and the sends' reads, about 30 KiB each way) went a
byte at a time in a C loop that spent as long on itself as on the SRAM:
they go through the target's copies now (`vars_host_cold_write`,
`vars_host_cold_read`, `services/cartridge_save.c`), two words at a time
on the other end; they run from the cartridge, fast RAM having no room for
them (as ARM in fast RAM they took the load to 11.4 GBA frames, but left
fast RAM under its floor). And a plane's redraw built each block's two
strips a byte at a time through the state's accessors: a block reads each
map tile's group whole and puts both strips in the upload buffer at once
(`build_block_whole`), where the buffer lies apart from the map and its
groups (every screen's does; otherwise a word at a time as before), the
same bytes charged. The load fell from 15.9 GBA frames to 12.6 (its
samples, over the four loads); what is left is mostly the picture's taking
the sends (`vram_take`, `vram_put_bytes` and their `memcpy`, about 55%,
`services/video.c`), the column strips a byte at a time through a port
that steps a row. The six movies end as ct_native's, in as many GBA frames
or a few fewer (`attract-demo` 33,648 against 33,703, `gato-battle` 24,669
against 24,680), `attract-demo`'s heaviest frame now a field map's load
(15.3 GBA frames against the world map's 16.0); fast RAM keeps 528 bytes spare (600 before: the copies' veneers and
the larger `put_span`). The census finds
the clusters it found before, but for `attract-demo`'s two scenes with
noise over the screen (frames 7931-8209 and 18980-19260), whose
mismatches already spanned them and now fall on more GBA frames with
fewer pixels each, about as many in all (719 clusters against 626).

**A field map's attributes** (issue #568). The heaviest frames left in
`attract-demo` were field loads, 15.3 and 15.1 GBA frames (game frames
7536 and 18585; `gato-battle`'s 11579 too). Timed a step at a time, the
largest step was the attributes' unpacking, 1.0M cycles for a 64 by 64
map: each tile's bytes read through the port as a span of three, and each
tile of a run placed and stepped past by a call of its own. The stream is
now looked at a window ahead of the port (`hw_port_peek_span`, the port
moved on and charged as the reads were with `hw_port_skip`; the stream
never reaches the tables it fills), and a run's tiles are laid as many at
a time as the row has room for (`put_tiles`), each row written when it
fills as before: 0.6M cycles. ct_native reads and charges the same bytes
in the same order. The rest of the load is the map's script run to its
first return (0.7M), the layers drawn at the load (0.5M), the tile set's
sends (0.35M, `services/video.c`) and the map's unpacking. The heaviest
frame of `attract-demo` falls from 15.3 GBA frames to 13.7 and `gato-battle`'s
map load at 11580 from 17.7 to 16.5; `attract-demo` takes 33,627 GBA
frames against 33,648, `gato-battle` 24,663 against 24,669, each ending as
ct_native does.

**The game's loops charged once** (issue #568). What was left of the
longest frames past the video service and the cartridge's reads looked like
the game's own work held to the original's time: loops whose every access
is charged as the original's. It isn't held there. An access only adds its
bus clocks to the account (`state_host_game_access`), priced at the rate of
the routine it is timed as where that rate changes or the time model takes
the work, so a run of accesses at one rate is the same time charged once as
one at a time; and the GBA (`CT_UNTIMED`) charges nothing, so what the loops
cost it is the accessors' calls. Three loops now do their work a block at a
time in an uncounted stretch and charge the original's accesses at once
(`charge_accesses`, `charge_rom_accesses`), at the same rate, before the
time model next looks: the menus' tiles inked a tile at a time
(`ink_tiles`, `game/menu/graphics.c`), a glyph's lines put as a run in each
column's tile wherever the runs lie apart (`put_glyph_runs`,
`game/menu/glyphs.c`), and the field's layer entries held and staged a run
at a time (`set_field_layer_staging_byte_run`, `game/field/streaming.c`;
`runs` now marks a buffer of bytes as well as a table of rows). ct_native's
account is the same to the clock and its movies end as before. The heaviest
game frames (`run.py --fast-rom`, GBA frames, against before): the field
menu's opening (`menus-sweep` 1705 and 2640) 11.98 and 12.63 against 12.84
and 13.56, the load screen's (`new-game` 1742) 10.67 against 11.46, a
location load (`new-game` 5801) 11.27 against 11.81, `gato-battle`'s
heaviest (11580) 15.21 against 15.76 and its loads at 9123 and 12832 11.41
and 11.78 against 12.25 and 12.38, and `attract-demo`'s heaviest 11.78
against 12.22; each movie ends as ct_native does. The rest of those frames
is the video service's conversions, the cartridge's reads, work RAM's spans
copied, and loops whose cost is their own arithmetic rather than their
accesses (a slot's sum, `game_sum`, 0.9 GBA frames of the menus' opening).

**Measured** (2026-11-14, mGBA 0.10.5):

| What | On the GBA |
|---|---|
| The outcome, driven by `mgba_run` | `title-start` (1,800 frames, 124 lag frames, end state CRC-32 `F711BBF1`), `new-game` (7,200, 244, `9126C7F9`), `gato-battle` (21,600, `A2944878`) and `events-jetbike-race` (9,400, `68804B38`, its track read back from VRAM) and `menus-save-point` (end state `497AE9A0`, which saves the game: the SRAM afterwards `DE9ED421`): each the same as ct_native's |
| Pictures | the title, the Millennial Fair, Crono's room with Mom's dialog and the battle with Gato as ct_native shows them, but for the approximations above; the 240x160 view's dialog at the top and foot (a page set again included: `new-game` frames 6000 and 6500, `gato-battle` 9400 and 19000) and the battle window with its two columns cut (`gato-battle` 19400) as `ct_native --view 240x160` shows them; per-tile priority and the sprite masks: Crono under his covers (`new-game` 5900), the naming screen's instructions in front of the alphabet (2500), the Fair's stalls with their wares (`gato-battle` 12400 and 14800) and the battle (20000 and 20800) as `ct_native --view 240x160` shows them, but for the colour math |
| Cartridge | the program 1.81 MiB (code 1.17, constants 0.63; 2.01 MiB, code 1.37, before the accesses stopped asking whether a guard looks), the pack 11.79 MiB after it (4.12 MiB, the scripts read ahead 3.74 MiB, the event scripts' 3.47 of it, and the packed streams unpacked 3.94 MiB); 14.11 MB in all |
| Work RAM (EWRAM, 256 KiB) | about 225.9 KiB used, 30.1 KiB free (GCC 13.3, 2026-11-15, 2 KiB fewer with no time model kept, `CT_UNTIMED`, with the tiles' conversion table moved here from fast RAM and the equipment screen's column state; the link fails with under 28 KiB free). By owner: the game's work RAM 108 KiB (`$7E:0000`-`$7F:AFFF`; the rest is in the SRAM, below); the picture 83.8 KiB (the SNES's video memory 65.6 KiB, the line registers 7.5 KiB, with the GBA's fourth layer's since per-tile priority, 1.3 KiB more in all, the tile caches 6 KiB, its line and band state 1.5 KiB, the tiles' conversion table 1 KiB, `services/video.c`); GameState 12.9 KiB (work RAM's page table 128 bytes, the menus' panels 5.8 KiB, the panels' layout 4 KiB, the past sprites and objects 2 KiB; save RAM is the SRAM's); the sound 15.6 KiB (the driver about 13.6 KiB, `frontend/sound_native.c`; the ring 0.7 KiB, `services/direct_sound.c`; the mixer under 1 KiB, its voices' state in fast RAM since issue #544, 280 bytes fewer); the game's own tables 2 KiB (the interpreters' opcode tables; the time model's recent rows are left out with the time model); the driver 1.1 KiB (its report's histograms 0.4 KiB, to 16 GBA frames rather than 64 since Mode 7's stretches of lines took fast RAM's room, below; the drive box 0.5 KiB); the C library 3.3 KiB (its state, stdio, and the heap's first take). Before (2026-11-14): 247.5 KiB, 0.2 KiB free, with work RAM's two banks 128 KiB and save RAM 8 KiB in GameState, and the drive box 4.3 KiB |
| Work RAM's top in the SRAM | `$7F:B000`-`$7F:FFFF`, 20 KiB, after the save and its mark (`VARS_FLAT_COLD_FROM`, `services/cartridge_save.c`), at the SRAM's 4 waits (5 cycles a byte; EWRAM's are 3), which the start now sets in place of the 8 a cartridge's flash chip would need (an SRAM takes 4, the GBA's own from its start): of the game's reads and writes of work RAM, none in `title-start` and `menus-save-point`, 0.3% in `new-game`, 0.4% in `gato-battle`, 0.2% in `events-jetbike-race` (counted a 4 KiB page at a time); the movies take 0.1-1.0% more GBA frames than before (`title-start` 2,828 against 2,799, `new-game` 13,337 against 13,304, `gato-battle` 52,417 against 52,346, `events-jetbike-race` 28,331 against 28,222). No value read or written in place runs across `$7F:B000` (`VARS_FLAT_CROSSED_STEPS`, which an assert checks) |
| Fast RAM (IWRAM, 32 KiB) | the fibers' stacks 10.5 KiB, nine slots: the main thread's and one for each of the menus' eight tasks, which `menus-sweep` has all running at once (deepest use over the whole movie library, 84 movies, 1,844 of the main thread's 2,560 bytes in `battle-rust-tyrano` and 712 of a task's 1,024 in `menus-sweep`; before, 3,072 and 1,536, and a task's deepest 1,792 when eight slots stopped `menus-sweep` at frame 3,838, and 2,048 before the view's memory), the code that runs most 11.1 KiB (the line interrupts 656 bytes of it, the mixing and the sound's interrupt 3 KiB with its chunk's mix (352 bytes) and the voices' state (288 bytes) beside it, issue #544, the line effects' transfers 2.5 KiB, in ARM; the time model's scopes left with the time model, `CT_UNTIMED`), the line effects' reads of their tables 0.3 KiB, the view's framing, bands and drawn panel 0.3 KiB (its memory, 0.6 KiB, and the last lines' state went to work RAM for Mode 7's stretches of lines, 1.4 KiB of Thumb: "The plane's lines in stretches", below), the interrupts' stack 256 bytes (deepest use 220 in `menus-sweep`, a line's interrupt taken inside the sound's; 192 until faster pictures moved which handlers came inside which) and the mixing's 448 (deepest use 328 in `gato-battle`), the driver's stack the 7 KiB above them (the link fails with under 5 KiB, which leaves 640 bytes spare for the next change, after the mixer took 1.5 KiB and gave back what the five functions below earned least (issue #544), 632 before it, 1,296 before a song's samples were known by where they come from, 584 before the streams were kept unpacked and the unpacker left, 1,952 before the unpacker went in, 2,048 before the division's own and 528 before the time model left; it was 6 KiB with the mixing on it), a canary at its end checked each frame (deepest use 2,312 bytes over the whole movie library, a layout's and the dialog's panel drawn on it) |
| Video RAM (96 KiB) | background 64 KiB (maps, tile caches and, while a dialog is drawn, its panel's room of 12-23 KiB), sprites 16 KiB of 32 |
| The sound, GBA frames a frame, mean and 99th percentile | Mixed as the playback asks (2026-11-15, "Out" above): the driver 0.07, 0.07, 0.07 and 0.09 (99th percentile 0.25-0.5), and the mixing, in the playback's interrupt, 0.12 of every GBA frame: 0.13, 0.18, 0.24 and 0.32 a game frame where the game's frame takes 1.2-2.6 GBA frames; the GBA frames over the movie 2,206, 11,193, 43,776 and 24,255 (`menus-sweep` 11,229) against 2,194, 10,907, 41,923 and 22,867 (10,713), with no sample dropped and 224 of silence, the ring's first two quarters, against 15,232-117,248 dropped and 21-60% of the time silent. Before, the driver and its mix: `title-start` 0.20 and 0.5, `new-game` 0.18 and 0.5, `gato-battle` 0.20 and 0.5, `events-jetbike-race` 0.21 and 0.5; of it the mixing 0.12, 0.11, 0.12 and 0.12 (the heaviest 0.25-0.31), so the driver and its passes about 0.08. Before the mixer's loops in `services/mix.s` and the chunks counted down: 0.26, 0.24, 0.26 and 0.27, the mixing 0.15, 0.13, 0.15 and 0.15. Before a quiet stretch of the driver's time passed at once: 0.35, 0.34, 0.36 and 0.36, the driver and its passes 0.2 |
| Work a frame, GBA frames (280,896 cycles), mean and 99th percentile | With the line effects' transfers in fast RAM as ARM ("Code placement", below): `title-start` 2,198 (picture 0.20), `new-game` 11,155 (0.28), `gato-battle` 42,168 (0.42), `events-jetbike-race` 22,845 (0.83), `menus-sweep` 10,487 (0.45), against 2,196, 11,073, 43,276 (0.48), 23,674 (0.93) and 10,550 (0.46); each frame's registers, sprites, palettes and video memory the same (hashed after every frame's picture). With the lines' walk kept cheap ("Line by line", above), the sprites' bands found by row ("Sprites") and VRAM's byte sends put straight in place ("Maps"): `title-start` 2,196 (picture 0.20), `new-game` 11,073 (0.29), `gato-battle` 43,276 (0.48, game 1.02), `events-jetbike-race` 23,674 (0.93), `menus-sweep` 10,550 (0.46), against 2,230 (0.21), 11,279 (0.32), 45,951 (0.57, game 1.05), 26,372 (1.20) and 11,045 (0.53); each frame's registers, sprites, palettes and video memory the same (hashed after every frame's picture). With work RAM's fills a span at a time (above): `title-start` 2,216 (game 0.54), `new-game` 11,459 (0.77), `gato-battle` 46,191 (1.06), `events-jetbike-race` 26,365 (1.11), against 2,251 (0.64), 11,496 (0.79), 46,241 (1.07) and 26,640 (1.16). With the menus' commands decoded in place and their routines found among those found lately (the interpreters, above): `title-start` 2,251, `new-game` 11,219 (game 0.75), `gato-battle` 45,880 (game 1.06), `events-jetbike-race` 26,629, against 2,251, 11,496 (0.79), 46,241 (1.07) and 26,640 (measured before the fills). With the dialog's and the battle window's shading and Mode 7 shown (the title's clock hands, the race's track; 2026-11-15), GBA frames over the whole movie, against before them: `title-start` 2,251 against 2,222, `new-game` 11,493 against 11,433, `gato-battle` 46,238 against 46,179, `events-jetbike-race` 26,640 against 25,712 (picture 1.19 against 1.07, where it showed no track), `attract-demo` 57,334 against 57,383. Before them: `title-start`: game 0.64 and 0.75, sound 0.20 and 0.5, picture 0.15 and 1.25, in all 1.23; `new-game`: game 0.79 and 2.5, picture 0.30 and 1.25, in all 1.59; `gato-battle`: game 1.07 and 2.5, picture 0.58 and 2.75, in all 2.14; `events-jetbike-race`: game 1.15 and 3.0, picture 1.07 and 3.25, in all 2.74 ("in all" counts the vertical blanks waited for: 2,222, 11,438, 46,175 and 25,716 GBA frames). Before the mixer's loops in `services/mix.s` and the chunks counted down (the sound, above): in all 1.27, 1.66, 2.21 and 2.79 (2,291, 11,943, 47,720 and 26,252 GBA frames). Before a read by address kept the resource it found (the cartridge's reads, above): game 0.65, 0.82, 1.11 and 1.20, in all 1.29, 1.69, 2.25 and 2.84 (2,317, 12,177, 48,552 and 26,716 GBA frames). Before a scope's start handed its end the rate (the time model, above): game 0.66, 0.86, 1.17 and 1.25, in all 1.30, 1.73, 2.31 and 2.88 (2,346, 12,432, 50,000 and 27,102 GBA frames). Before the accesses stopped asking whether a guard looks (the lean build, above): game 0.78, 0.94, 1.27 and 1.42, in all 1.52, 1.83, 2.42 and 3.04 (2,736, 13,197, 52,199 and 28,582 GBA frames), the sound and the picture the same; before per-tile priority, the sprite masks and that, picture 0.12, 0.26, 0.54 and 0.98, in all 1.48, 1.81, 2.39 and 2.97. Per-tile priority and the sprite masks cost 64, 178, 675 and 612 GBA frames over the four movies (2,737 against 2,673, 13,198 against 13,020, 52,201 against 51,526, 28,582 against 27,970: 1.3-2.4%), most of it the lines' registers for four layers rather than three and the layouts a split asks for. The dialog's panel costs the game's column 0.04-0.07 (outside the game's time); the bands worked out a run of lines at a time give the picture back more than the panel and the cut take. Before the drawn panel and the cut: game 0.78, 0.90, 1.21 and 1.37, picture 0.21, 0.38, 0.64 and 1.07, in all 1.56, 1.85, 2.42 and 3.00; before a frame's lines were visited only where something happens (above), with the 240x160 view's camera and bands: picture 0.27, 0.53, 1.01 and 1.27, in all 1.64, 2.05, 2.90 and 3.27; before the camera and bands, picture 0.16, 0.35, 0.77 and 1.04, in all 1.49, 1.90, 2.66 and 3.02. Before the sound driver's quiet stretches passed at once: in all 1.61, 1.98, 2.75 and 3.13. Before reads by address reached work RAM in place (below): game 1.01, 1.13, 1.50 and 1.71, in all 1.80, 2.11, 2.91 and 3.40. Before the time model was priced a stretch at a time and a frame's lines could keep the last frame's (below): game 1.45, 1.8, 2.45 and 2.8, picture 0.21, 0.49, 0.88 and 1.14, in all 2.4, 3.0, 4.0 and 4.6. Before the code's placement and settings (below): in all 3.0, 3.4 and 4.7 (`events-jetbike-race` 6.0 before the interpreters decoded only what a row names, #429); before the picture was made where it changes (below): picture 1.6, 2.9, 4.5 and 4.8; before the flat accessors read in place (issue #424), the sends read a span at a time (#425), the time model charged in 32 bits (#426) and the interpreters decoded only what a row names (#429): game 6.7, 9.5 and 13.4, in all 8.9, 12.9 and 18.5 |

Without the native mixer the game's own frame is most of the cost: 0.8-1.4
GBA frames, from 7-13 before the flat accessors read in place, the sends
read spans, the time model was priced a stretch at a time and the interpreters decoded
only what a row names; the picture is 0.1-1.2 more, made in the vertical blank after
the game's frame so that no frame the GBA shows tears between two pictures
(the wait isn't counted as work).

**Code placement and compiler settings.** Measured as the game's frame in
GBA frames, mean, over `new-game` and `gato-battle`, before the time model
charged in 32 bits (#426; since, the first row is 2.97 and 4.13 and the
last 2.23 and 3.14):

| Build | `new-game` | `gato-battle` |
|---|---|---|
| Thumb from the cartridge, -O2, each file apart | 3.49 | 4.86 |
| -Os, optimized across files | 3.70 | |
| -O2, optimized across files | 2.54 | 3.57 |
| -O3, optimized across files | 2.41 | 3.35 |
| and the code that runs most in fast RAM, in Thumb | 2.28 | 3.17 |
| and the state's accesses (state_host.c, vars_flat.c) in ARM | 2.15 | 2.98 |

Optimizing across files is most of it, as on the host: the generated
accessors inline into the game. It needs `system/system.c`'s system calls compiled
apart (only the C library calls them), and its deeper inlining took the
main thread past 3 KiB at -O2 (3,048 bytes in `gato-battle`); at -O3 it is
1,792. Fast RAM's 32-bit bus with no waits fetches an instruction in one
cycle where the cartridge's takes two to four; `toolchain/fast_code.ld` lists the
functions with the most of the work a byte (`host/run.py --profile` over
`new-game` and `gato-battle`): the time model's scopes, the state's reads
and writes, the event interpreter's decoding, the cartridge's reads and the
C library's copies, about 40% of the samples in 4.3 KiB. The copy and the
fill are the target's own (`system/copies.s`): ARM moving eight words with
one load-multiple and one store-multiple, in 256 bytes where newlib's Thumb
pair took 320, a hundredth of a GBA frame off each part of the frame. The event
interpreter's decoding (`decode`, `names_what_it_says`) was there too, but
its callers stay in the cartridge and the calls' long-branch veneers took 3%
of the work back. The rest of the profile
is flat (the next 5 KiB would hold 7% more), and the data those functions
reach is in work RAM, whose waits fast RAM doesn't shorten, so it gives 5%.
ARM rather than Thumb gives as much again for the state's files; for the
event interpreter's file (`game/event/engine.c`), 1%, since its cold code
then runs as ARM from the cartridge's 16-bit bus. The room comes from the
tasks' smaller stacks.

For the mixer's loops and its voices' state (issue #544), five functions
went back to the cartridge as Thumb, each under 0.2% of the samples in any of
the six movies above (the most in any): `wram_copy` (0.03%),
`sprite_table_set_byte` (0.01%), `r16` (0.04%), `state_host_write8`
(0.06%) and `vars_host_fill_span` (0.17%, `title-start`); with them the
sound's ring's restart and its silence for a quarter not filled in time,
which come once every two GBA frames or seldom.

The fibers' stacks are sized from the GBA's own deepest use over the whole
movie library (84 movies in mGBA, `system/fiber.c`): 1,844 bytes of the
main thread's and 712 of a task's, which keep 2,560 and 1,024 bytes, a
third or more of their room spare for paths no movie takes, with the guard
stopping the program at a switch from a fiber that went into it. The 4.5
KiB that frees holds the next 16 functions by the GBA's work a byte, less
the room fast RAM keeps spare (528 bytes)
(`toolchain/fast_code.ld`: the work RAM fills, the sprite table's and the
palettes' sends, the state's byte accessors, the sprite lists, the sound's
sample writes, the plane's lines, the event script's reads): in mGBA the
movies take 2,168, 11,087, 43,105, 23,898 and 11,014 GBA frames
(`title-start`, `new-game`, `gato-battle`, `events-jetbike-race`,
`menus-sweep`) against 2,214, 11,181, 43,752, 24,352 and 11,173, 0.8-2.1%
fewer, each ending as ct_native does.

Once the picture's work was cut down, the line effects' transfers
(`next_transfer`, `table_unit`, `line_bytes`, `slot_entry`, 1.8 KiB in
Thumb) were 5-9% of the samples in `gato-battle` and the race, Thumb from
the cartridge, reading the tables through 8 lines of 32 bytes in work RAM.
They are in fast RAM now, compiled as ARM (2.5 KiB), and the lines they read
are too (0.3 KiB). The room came from what earned least of it a byte: the
tiles' conversion table (1 KiB) went to work RAM, and the sound's envelope
step, the cartridge's address check and the catalogue's search to the
cartridge (`vram_take`, which matches the game's sends against VRAM, took
the search's place). Measured as GBA frames over the movie, and in brackets
the work alone (the samples outside the driver's wait for the vertical
blank), each step on the one above:

| Step | `gato-battle` | `events-jetbike-race` |
|---|---|---|
| Before | 43,276 (35,889) | 23,674 (20,620) |
| The tiles' table in work RAM | 43,514 | 23,690 |
| and the transfers in fast RAM, Thumb | 42,656 (35,172) | 23,303 (20,186) |
| and the lines they read in fast RAM | 42,313 (34,936) | 23,183 |
| and the envelope step and the address check in the cartridge | 42,598 (35,023) | 23,197 (20,067) |
| and the transfers in ARM | 42,310 (34,827) | 22,906 (19,790) |
| and `vram_take` for the search (as built) | 42,168 (34,729) | 22,845 (19,737) |

The other three: `title-start` 2,198 (work 1,551) against 2,196 (1,556),
`new-game` 11,155 (8,221) against 11,073 (8,326), `menus-sweep` 10,487
(8,045) against 10,550 (8,150). `new-game` does less work in more frames:
the driver begins a frame's game straight after the last frame's picture,
so where a frame ends inside the vertical blank the next one shares it,
and how many do depends on where in the frame the work ends, not only on
how much there is (the tiles' table alone took `new-game` to 11,277, the
transfers then to 11,024, with the work the same and 1% less). A table of
four dots a lookup, 64 bytes, kept the conversion in fast RAM but cost more
than the whole table in work RAM (`gato-battle` 42,911 against 42,656).
What goes in next makes room the same way.

**Division.** The ARM7TDMI has no divide, and Thumb no multiply that keeps
a product's high word, so every `/` and `%` in Thumb, by a constant too, is
a call to the C library: libgcc's Thumb unsigned divide in fast RAM, its signed
pair Thumb from the cartridge, each through a long-branch veneer. The program has its own now
(`system/divide.s`): ARM in fast RAM, four bits of the quotient a turn by
conditional execution, the remainder by a multiply, the signed pair on the
unsigned (280 bytes, which take 96 more of fast RAM than libgcc's). Where
a profile of the callers (the return address at each sample in the divide)
named the game's own, the divides went: the field's vblank asked its
layers' edge flags by `k / 3` and `k % 3` twelve times a frame, now by group
and layer; the menus' and the race's numbers count each decimal digit out
by subtracting its power of ten (`game/core/decimal.h`) rather than sixteen
and ten divides a number; the picture's shading takes its sixteenths by a
multiply (`small_quotient`). With no time model kept (`CT_UNTIMED`), the
divides took 0.9-1.6% of the samples over the five movies and take 0.4-0.6%
now. In mGBA (`title-start`, `new-game`, `gato-battle`,
`events-jetbike-race`, `menus-sweep`) the movies take 2,137, 8,576, 29,603,
16,567 and 9,132 GBA frames against 2,141, 8,603, 29,770, 16,690 and 9,174
(up to 0.7% fewer), each ending as ct_native does. Fast RAM kept 1,952
bytes spare above the driver's 5 KiB.

**The event scripts read ahead** (issue #508). The event engine reads a
command's operands by its row each time the command runs: the parameters
the row names, each by its kind, the script's bytes one at a time through
the state's accessor, once for the checks and again to decode. What the
bytes alone decide is the same every time, so a builder reads it once:
`ct_native --script-records FILE` walks every location's script (from its
code's start, and from each place its functions' table and its jumps name)
and, with the engine's own decoding (`event_record_read`,
game/event/records.h), writes a record for each command: its opcode, the
bytes it was read from, and a value for each parameter its row names, the
value itself where the bytes decide it, the operand where it reads the
game's state (a variable's value, a party member's object), the kind in the
value's top byte. `pack.py --script-records` puts them in the cartridge after
the pack (3.47 MiB: each script's index of its offsets, two bytes an
offset, and its records), by the packed script's resource; the driver's
asset source answers `asset_form` from them, the script's load finds its
own (`event_records_use`), and the engine takes a command's record where
the script's bytes there are still the record's, does the checks that need
the game's state, and looks up what reads it. A command with no record, or
whose bytes changed (the party's scripts after the location's, which the
game rewrites, or a battle's or the race's data over the buffer), runs from
its bytes as before; so does every command in ct_native, which keeps no
records and charges the same time. The records are only used where the
rows that read them are the program's (`event_records_language`, a hash of
what each row decodes from where). A ROM-free test
(`tests/port/event_records_test.c`) holds each record to what the engine
decodes from the same bytes, for every opcode the records cover. The
command's handler, which only the time model's pricing and a broken
contract read, is no longer looked up for every command on the GBA. Of the
samples, the event engine's own (the turn's commands read, checked and
decoded) went from 0.92%, 2.64% and 1.11% to 0.55%, 1.63% and 0.65%
(`new-game`, `gato-battle`, `events-jetbike-race`); what is left is the
engine's dispatch, Thumb from the cartridge, which fast RAM has no room
for. In mGBA the movies (`title-start`, `new-game`, `gato-battle`,
`events-jetbike-race`, `menus-sweep`) take 2,106, 8,772, 28,943, 16,199
and 9,163 GBA frames against 2,105, 8,806, 29,330, 16,280 and 9,162 (1.3%
fewer in `gato-battle`, where the scripts run most; packed without the
records, the same program takes 2,105, 8,812, 29,361, 16,295 and 9,155),
each ending as ct_native does. Fast RAM and its 728 bytes spare are as
they were; the game's state takes 16 bytes more of work RAM for the
loaded script's records.

**The menus' object scripts read ahead** (issue #533), the same way. The
object scripts' interpreter (`menu_run_script`, $C2:0F63) read each
command's parameters by its row as the object's script reached it, most of
them a world map's scene's scripts, which the map's load unpacks to
`$7F:0400` (over `new-game` and `gato-battle`, all but two of the 186,000
commands run). Every parameter of the object scripts' language is decided
by the command's bytes, so a record holds each named parameter's value
itself (`menu_record_read`, game/menu/records.h): the cartridge's
`script:object` scripts and the scenes' unpacked `lz:object` streams, each
walked a command after a command as their text is, go in the same file,
now `ct_native --script-records FILE` (`pack.py --script-records`; the
form is `ASSET_FORM_SCRIPT_RECORDS` for both languages, each record block
headed by its language's hash). The scene's load finds its scripts'
records (`menu_records_use`), a cartridge script's are found by its
resource as the interpreter asks, and the interpreter takes a command's
record only where the script's bytes are still the record's, read as a
look that takes no time, then charges the reads of the script the decoding
would have made (`charge_rom_accesses`, the same clocks a cartridge or
work RAM read through the data's pointer takes), so a timed build with
records keeps ct_native's time. ct_native keeps no records and runs and
charges as before. A ROM-free test (`tests/port/menu_records_test.c`)
holds the command taken from each opcode's record to the one decoded from
the same bytes, and the record's use to its bytes. The interpreter's share
of the samples (its loop, the commands' work inlined in it, and the
decoding or the records) went from 4.02%, 1.67% and 0.35% to 3.46%, 1.44%
and 0.31% (`new-game`, `gato-battle`, `menus-sweep`); what is left is
mostly the commands' own work and the checks, Thumb from the cartridge.
In mGBA the movies take 2,002, 8,472, 28,184, 15,608 and 9,035 GBA frames
against 2,002, 8,544, 28,282, 15,611 and 9,042 (0.8% fewer in `new-game`,
the heaviest frames unchanged), each ending as ct_native does. The
records take 0.27 MiB more of the cartridge (14.11 MB in all), the game's
state 16 bytes more of work RAM; fast RAM is as it was.
The cartridge's waits are 3 and 1 with the prefetch by default, as
commercial games set them and every cartridge handles. `host/pack.py
--fast-rom` sets a flag in the word after the header (`toolchain/start.s`'s
`options`), and the start sets 2 and 1 instead: most flash carts (EZ-Flash,
EverDrive) and the Analogue Pocket take it, some cheap reproduction
cartridges don't, so it is the player's choice and not the default. In
mGBA, the game's, the sound's and the picture's GBA frames a game
frame added up, with both settings ending in ct_native's work RAM and save RAM:

| Waits | Field | Menus | Battle | Scenes |
|---|---|---|---|---|
| 3 and 1 | 1.96 | 1.72 | 2.62 | 3.23 |
| 2 and 1 (`--fast-rom`) | 1.86 | 1.64 | 2.50 | 3.06 |

About 5% everywhere: only the code and constants still read from the
cartridge (the first access of a run, the prefetch's misses) gain, and the
code that runs most is already in fast RAM.

Work RAM (EWRAM) is 2 waits by default. `host/pack.py --fast-work-ram` sets
the options word's second bit, and the start then writes `0E000020` to the
internal memory control register (`$0400:0800`), which puts work RAM at 1
wait. A GBA and a GBA SP take it. A GBA Micro, a DS in its GBA mode and some
emulators don't, and a ROM packed with it can crash there, so it is off by
default like `--fast-rom`. mGBA honours it: with `--fast-rom` added, the
same movies take fewer GBA frames, and the end states are ct_native's. In
GBA frames (`run.py --fast-rom`, replayed), against `--fast-rom` alone:

| Frame | `--fast-rom` | and `--fast-work-ram` |
|---|---|---|
| `new-game` 1742 (the load screen's opening) | 10.67 | 9.92 |
| `new-game` 5801, 3248 (a location's load) | 11.27, 10.28 | 10.74, 9.55 |
| `gato-battle` 11580 (a location's load) | 15.21 | 14.55 |
| `gato-battle` 7690, 12832 | 11.56, 11.78 | 11.03, 11.33 |
| Whole movie: `new-game`, `gato-battle` | 7,693, 24,352 | 7,651, 24,105 |

That is 4-7% off the heaviest frames. They are mostly code from the
cartridge and copies into and out of work RAM, and a copy's word takes two
16-bit accesses either way.

A location's load unpacks its tile attributes three bytes a tile, mostly a
tile at a time (`game/field/layers.c`, `put_tiles`). Each tile used to call
`memset` three times to fill one byte. A lone tile is now set in place, and
the stream is read ahead 128 bytes at a time rather than 48, so the work RAM
port is read ahead three-eighths as often. Together with `--fast-work-ram`
(`run.py --fast-rom`, against the first column above): `new-game` 5801
10.27, `gato-battle` 11580 14.28. `attract-demo`'s heaviest game frame
went from 11.78 to 10.99, its heaviest picture from 12.29 to 12.02, and the
whole movie from 31,701 GBA frames to 31,140. Its heaviest frames are now
pictures, not the game's work (issue #568). `run.py --own-time attract-demo`
goes through ct_native's 50 places with either packing. A census of
`title-start`, `new-game`, `gato-battle`, `menus-sweep` and `attract-demo`
(`--fast-rom`, against the same without `--fast-work-ram` or the unpacking's
change) finds 1,279 picture clusters rather than 1,267. The 38 with no
cluster beside them in the base run each last 1-3 GBA frames: an edge of
the top lines or one frame of the title's clock, a picture shown a GBA
frame earlier or later than before. Fast RAM and work RAM are as they were.

A GBA's BIOS starts a cartridge only where the header's bytes $04-$9F are
the logo it keeps its own copy of, so the logo is the one thing a ROM for
hardware needs that the project can't ship. `host/pack.py --bios FILE` takes
it from the player's own dump of the BIOS (16 KiB, CRC-32 81977335): the
BIOS keeps its reference copy compressed as the header holds it, and
compares the two byte for byte before unpacking the header's to show it, so
the copy goes in as it is, checked by its CRC-32 (`D0BEB55E`, the one mGBA
checks too). Without `--bios` the logo is blank and pack.py says so:
emulators and flash carts that patch the header boot it, a GBA doesn't.
`mgba_run --bios FILE` (and `run.py --bios`) starts mGBA in that BIOS rather
than its own, which goes straight to the program, and fails where mGBA
skipped it for the logo; `title-start` played that way, through the BIOS's
logo and check, ends in ct_native's work RAM and save RAM. Nothing of the
BIOS is in the repository; the test that reads one runs where `CT_GBA_BIOS`
names it.

#### Where the GBA's frame goes

`host/run.py --profile FILE` has the runner note where the processor is every
997 cycles as the movie plays (`mgba_run --samples`) and names the function
of each place from the program's symbols. Over the `field`, `menu`,
`gato-battle` and `attract-demo` (the opening's scenes) movies, the shares
of all the GBA's work, before the picture was made where it changes and
after (both before the time model charged in 32 bits, #426):

| Share of the GBA's work | Field | Menus | Battle | Scenes | Issue |
|---|---|---|---|---|---|
| The picture: its conversion, the lines' registers and their interrupts (the BIOS's dispatch included) | 41% to 8% | 43% to 10% | 42% to 12% | 39% to 16% | #367 |
| The time model: `state_host_game_access`, its 64-bit multiply, `charge_time_as_begin` | 14% to 24% | 13% to 23% | 15% to 25% | 14% to 21% | #426 |
| The state's reads and writes: the flat accessors, `state_host_read8`, `vars_flat_owned` | 11% to 19% | 11% to 19% | 10% to 16% | 13% to 20% | #424 |
| The script interpreters' decoding | 12% to 20% | 11% to 18% | 11% to 18% | 8% to 13% | #429 |
| The game's own logic | 10% to 17% | 10% to 17% | 10% to 17% | 10% to 15% | |
| Cartridge reads, the sends, the C library | 11% to 11% | 11% to 11% | 12% to 11% | 14% to 14% | |
| All of it, GBA frames a game frame | 7.9 to 4.7 | 7.2 to 4.1 | 10.5 to 6.1 | 10.8 to 7.0 | |

The picture was made in full every frame, and it was the largest part of
the GBA's work. What changed, each measured over the four movies above:

- **The lines' registers where they change.** Each of the 160 lines' 18
  registers was worked out again every frame, and an interrupt at every
  line's horizontal blank, 228 a frame with the BIOS's dispatch, set them:
  9% of all the GBA's work, at any speed. Now a line's registers are worked
  out only where what the line effects set has changed, and set only where
  they change, by two interrupts a change (the line counter's on the line
  above, the horizontal blank's at its end). Where the horizontal blank's
  comes late, another handler having run on past the line, it sets the last
  change it passed rather than skipping it: skipped, a band showed the band
  above's lines down to the next change, and the naming screen's rows came
  six lines low as the cursor moved (issue #520; a sweep of the cursor over
  every letter now shows the rows where ct_native's 240x160 view does, but
  a frame's lag as it changes row). The line effects' tables are
  read 32 bytes at a time, not a call a byte, and a slot's idle lines
  between transfers are passed over.
- **Only what changed is converted.** VRAM is followed in 32-byte units
  rather than 512-byte blocks, and a send of bytes VRAM already holds marks
  nothing; the sprite table is converted in a frame it changed.
- **A tile's row from a table.** The bitplanes' bytes become the GBA's packed
  nibbles by four lookups in a table of 256 words, not a loop
  over dots.
- **Layouts that can't help aren't made.** Where the maps name more tiles
  than a cache has places, the layout itself misses some, and starting
  afresh a second on missed the same ones: in `gato-battle`, 220 layouts at
  about 10 GBA frames each, its 99th percentile. A layout is made again
  only for a tile missing after it.

The picture went from 1.6, 2.9, 4.5 and 4.8 GBA frames a frame to 0.2, 0.5,
0.9 and 1.2 on `title-start`, `new-game`, `gato-battle` and
`events-jetbike-race`, with the same end states and the same pictures but
for where each capture falls in the frame. Converting at build time doesn't
apply to what remains: the tiles and maps converted are the ones the game
unpacks into VRAM as it plays, and the copies DMA would take over (the
palettes in a frame they change, the sprite table) are under a twentieth of
the picture.

**Reads by address.** With the runner noting the caller where the processor
was in `state_host_read8` (the return address in place of the place), most
of its 4-5% of the GBA's work was the video service reading its sends'
sources a byte at a time: sources in work RAM's mirror in bank `$00`, which
the span's one read took only at `$7E`-`$7F`. The flat build now reads such
a span in one piece, and the alias views and `game/wram.h`'s escape read
and write work RAM's banks in place (above, "The state as the original's
work RAM"); a write by address to `$7E`-`$7F` no longer asks where it lies.
A time scope's end restores the rate it kept rather than looking its row up
again, and a scope at the rate already in force prices nothing, which adds
the same thousandths. Over the four movies the game's frame went from 1.01,
1.13, 1.50 and 1.71 GBA frames to 0.78, 0.97, 1.33 and 1.42, the whole
from 1.80, 2.11, 2.91 and 3.40 to 1.61, 1.98, 2.75 and 3.13 (`field` 2.28 to
2.13), each ending as ct_native does. The time model's scopes
(`charge_time_as_begin`, `charge_time_as_end` and their row lookup) are
6% of the battle's work and 5% of the field's: a scope that names its row at
build time would leave out the lookup, 1.1-1.4%, at the cost of rows in
place of routines in some 90 tables and 100 named routines (#426).

**The clock.** The build with no time model reads the GBA's cycle count
(`cycles_now`, two chained timers) only for the driver's pacing and its
report: a few times a frame and twice a mixing chunk. The 1.4% of
`gato-battle`'s samples it took (3% of `title-start`'s) was the driver
idling until a picture was due, calling it from the cartridge into fast RAM
through a veneer on every turn of the wait. It is now read in place
(`system/debug_log.h`, a few loads), so the samples are the wait's own, in
`main`; the movies' GBA frames are the same to within 0.1%.

**The field's sprites.** Building the field's sprite table
(`field_sprite_table_built`) is 4.3% of `gato-battle`'s samples and 2.6% of
`events-jetbike-race`'s: the original's work every frame, each object's
records placed and copied into the table a byte at a time through the
accessors, whose every read and write ct_native's time model counts, so
the port can't read fewer. Its entries now go through the work RAM port in
one piece (`hw_port_write_span`, the same time), 6% of it; a fifth of the
rest is the walk over the 64 rows, twice (the port's check that it draws
every object, then the drawing), and the rest the objects' own.

**The heaviest frames.** Where a scene sets itself up in a single game
frame, that frame took 15-20 GBA frames, and the picture stood still for
a third of a second (issue #568). `run.py --profile` with the runner's
samples kept to a few frames finds what each does. The jet bike race's
set-up (the attract demo's frames 8236 and 19285, `events-jetbike-race`'s
5872) sent the Mode 7 plane's pixels and map into one half of each VRAM
word a byte at a time, each byte compared and marked as a change of its
own, 42% of the frame; mirrored Johnny's pictures from Crono's a pixel at a
time through the state; and wrote the routine that gathers the scaled
racers into tiles through the work RAM port a byte at a time. A half send
now compares and puts a unit's bytes together and marks the unit once
(`services/video.c`, `vram_put_run`); the mirroring reads and writes a row
at a time (`scene_mirror_rows`, `wram_read_span` and `wram_write_span`),
where the row neither runs round its bank nor overlaps where it goes; and
the routine goes through the port 64 bytes at a time
(`hw_port_write_span`), each byte's time as before. The frame falls from
19.7 GBA frames to 11.3 in the attract and from 19.5 to 11.3 in the race,
the end states as before. Played in its own time, the GBA stays 8 frames
at `$06B` after the attract's second race, where ct_native stays 23 (the
base build too), so `run.py --own-time` reads the location every 4 frames
rather than 8, which the faster set-up's frames had moved past that stay.

**The menus' opening.** The menus open in a single game frame (the title's
load screen, the field's menu, the save point's), 17-20 GBA frames, the
heaviest of `title-start`, `new-game`, `gato-battle` and `menus-sweep`
(issue #568). Timed a step at a time with the GBA's cycle count, the
largest steps were clearing VRAM and 58 KiB of work RAM (1.0M cycles),
building the menus' tiles (1.9M; the three-plane tiles' last planes a byte
read from the cartridge and two written at a time, 0.64M), the items'
tables (0.9M; each item's data record a byte at a time by address) and the
save slots' sums where a slot holds a game (1,280 words a slot through the
save service, each a call). Work RAM's fills now fill the rest of a page
owners keep whole at once and look each eight bytes' owners up once
(`runtime/port/vars_flat_host.c`, `fill_owned`); a three-plane tile's
last planes go in one write from one read of its third plane
(`menu_three_plane_tile_bytes`, `wram_write_span`); an item's data record
is read whole (`menu_item_table_bytes`); and a slot's sum reads 32 bytes a
call (the save service's `sv_read_span`); each byte's time as before. The
opening falls from 17.4 GBA frames to 15.3 in `title-start` and
`new-game`, 17.4-18.5 to 15.3-16.2 in `gato-battle` and 20.0 to 17.5 in
`menus-sweep`.

**The longest frames, again.** Timed a step at a time with the GBA's cycle
count, and profiled with the runner's samples kept to the frame, what was
left of the longest frames (issue #568) was mostly the video service taking
the sends and the cartridge's lookups, both spread over every load:

- **VRAM a unit at a time.** A send compared each word with VRAM's and
  copied what differed with a call of its own; the menus' clear of all 64
  KiB took 0.6M cycles of their opening. Whole units now go through
  `services/units.s`, ARM in fast RAM, four words a load-multiple from
  each side and stored only where they differ, answering which units
  changed (a fill the same 16 bytes again and again); the clear takes
  0.24M.
- **Sends read where they lie.** A send of the game's work RAM was copied
  into a piece and then compared; where the flat state keeps the bytes in
  one run (`state_host_span_in_place`, which the driver gives the video
  service through `gba_video_read_in_place`), they are compared from there.
- **The resources held with their rows.** A read of a resource by its id
  and a run of the cartridge each looked the catalogue up in the cartridge
  again; they are now answered from the resources held (above, "The
  cartridge's reads"): a read of the menus' tables from about 250 cycles to
  140, a run from about 600 to 360.
- **The title's plane a row at a time.** The title's set-up wrote the
  plane's 10,240 pixels and its map through the port a byte a call;
  `vid_write_vram_bytes` takes a tile's pixels or a row of the map in one,
  as that many `vid_write_vram_byte`'s, and the GBA puts them as a send's
  half of each word.
- **A battle's cut rows.** A band's map cut (`convert_cut_row`) was
  converted a row for each of its two units, from the cartridge; a row is
  now converted once, from fast RAM, where `note_if_unread` (asked now only
  for a row left untyped) gave up its room.

With `--fast-rom`, the heaviest game frames, in GBA frames, before and
after, the end states as ct_native's throughout:

| Frame | Before | After |
|---|---|---|
| The title's load screen opening (`new-game`, `title-start` 1742) | 15.28 | 11.46 |
| The field's menu opening (`menus-sweep` 1705, 2640) | 16.68, 17.54 | 12.84, 13.56 |
| The title's set-up (frame 65) | 11.75 | 5.94 |
| A location's load (`new-game` 5801, 3248) | 13.06, 12.06 | 11.79, 10.28 |
| `gato-battle`'s heaviest (11580) | 16.89 | 15.74 |
| `attract-demo`'s heaviest game frame / picture | 13.71 / 13.99 | 12.20 / 12.67 |

`attract-demo` takes 31,791 GBA frames rather than 32,361, `menus-sweep`
7,443 rather than 7,793; a census of seven movies finds the same 1,511
picture clusters, and `run.py --own-time` the attract's 50 places. Fast RAM keeps 536 bytes spare. What remains of
the loads and the openings is the game's own work through the accessors,
each access charged as the original's, a few hundred cycles a block move or
a run read from the cartridge (issue #568).

**Reads by address, spans into the cold top, tiles without a table**
(issue #568). Profiled again, with the runner's samples kept to the frame
and the calls counted, `gato-battle`'s heaviest frame (a location's load,
11580) spent about 3 GBA frames reading the data's pointers a byte at a
time (`rom_read8`, about 3,700 of them) and 0.8 writing a 3 KiB span a byte
at a time; the pictures after a load spent about a GBA frame converting
tiles:

- **The last run.** A read by address converted the address, found its
  4 KiB's resource and checked the row again for each byte. The run the
  last read found its byte in is kept now (`last_run`, `remember_run`,
  `game/data/assets.c`): the resource's bytes in that bank, within the part
  of the bank that is the cartridge's, for a typed resource the store
  holds. A read in it takes one comparison, in `rom_read8` before anything
  else and in `served`, which the sends' bytes go through. An untyped
  row's reads are still noted, each, and the time charged is as before.
  The lookups that miss stay in the cartridge (`find_anew`,
  `hold_resource`), and `held_resource` keeps only its comparison in fast
  RAM.
- **Spans across the cold top's start.** A location's load writes 3 KiB
  from below `$7F:B000` into it, which went byte by byte through the
  owners' bits. Each run's part of a span goes as one copy now where owners
  keep all of it (`runtime/port/vars_flat_host.c`), in order, as a byte at
  a time would.
- **Tiles in ARM, without a table.** The caches' tiles were looked up in a
  table a plane's byte at a time, and the sprites' through a kilobyte's
  table in work RAM. `services/tiles.s` reads a tile a word at a time and
  puts each row's bits in the GBA's order with four exchanges of bit groups
  and a swap of halves, with no table. The kilobyte's table went, and with
  it 1 KiB of work RAM.

Faster pictures moved which interrupts came inside which, and the
interrupts' stack reached 220 bytes in `menus-sweep`, past its 192: it has
256 now. A sprite at the screen's right edge marked a byte past the
squares that find the sprites in front of it (`over_squares`), on the
stack; the squares have an eighth column. With `--fast-rom`, in GBA
frames, before and after, the end states as ct_native's throughout:

| Frame | Before | After |
|---|---|---|
| `gato-battle`'s heaviest (11580, a location's load) | 15.76 | 13.88 |
| The field's menu opening (`menus-sweep` 1705, 2640) | 12.84, 13.56 | 12.56, 13.27 |
| The title's load screen opening (`new-game` 1742) | 11.46 | 11.17 |
| Location loads (`attract-demo` 6390, 13940, 17441) | 9.04, 9.10, 9.32 | 7.41, 7.24, 7.65 |
| `attract-demo`'s heaviest game frame / picture | 12.21 / 12.29 | 11.99 / 11.93 |
| The picture after a load (`gato-battle` 9210) | 7.40 | 7.15 |

`gato-battle` takes 24,246 GBA frames rather than 24,370, `attract-demo`
31,464 rather than 31,793, `menus-sweep` 7,415 rather than 7,512 and
`new-game` 7,660 rather than 7,702. `run.py --own-time` finds the attract's
50 places. A census of the seven movies finds 1,591 picture clusters
against 1,549: all but 21 of them overlap one the base run had, and those
21 last 1-3 GBA frames with at most 378 pixels each (an animated tile or a
line's edge a GBA frame apart). Fast RAM keeps 568 bytes spare and work RAM
is 1,168 bytes above its floor. What is left of the menus' openings is
the menus' tiles and glyphs, a slot's sum and the sends; of the loads, the
field's drawing and unpacking through the accessors and the sends.

**Mode 2's lines kept down a frame, spans and rows read whole** (issue
#568). Timed a part at a time with the GBA's cycle count, the attract
demo's heaviest pictures were the rising palace's curtain (frames
8865-8885 and 19914-19936): mode 2 with a window and a scroll that change
every line, 2.5M cycles of lines a picture, each of 160 lines worked out
whole. What a line reads that stands down the frame is now worked out once
and kept until the frame's lines are done (`work_out_lines` forgets it):

- **The third layer's offsets** (`line_offsets`): its map and scroll stand
  while the first layer's scroll and the windows change, and the two rows
  of offsets were read from the SNES's VRAM again each line.
- **The runs of moved columns** (`moved_columns_kept`), four kept by layer,
  scroll and moved column: the shimmer pushes every other line across by
  the same strength, so most lines find runs a line before them found.
- **The windows' four regions** (`regions`), by the layers shown and the
  layers the windows mask: line effects move the windows' edges and change
  which layers they mask, never how.
- **Each layer's control** for the map a line shows it from, where no band
  cuts it (`layer_control_kept`): the maps a line notes for the next layout
  are noted by the first line that shows them.

The palace's lines fell to 1.9M cycles a picture. Two of the game's reads
went a byte at a time where they needn't:

- **Spans by address** (`rom_read_span`, `game/data/assets.c`): a span into
  work RAM (the field's sprite tiles, unpacked there a tile at a time) is
  read where it lies (`wram_data_read_span`), and a span that runs past its
  resource's end (a song read with its length, two or three bytes past) as
  each resource's run, a byte at a time only where no run holds it. Each
  byte is charged as before.
- **A field line's map row** (`game/field/streaming.c`): a line along a row
  reads the map tiles it takes and their properties as runs of the tables
  (`field_layer_maps_run`, `field_tile_properties_run`, a read run beside
  the write runs, STATE.md "The flat state"), the same bytes as many times,
  rather than two reads a map tile.

`read_game`, which the picture reads the game's memory through, had
`state_host_read_span` inlined into it in fast RAM; with a second caller it
wasn't, and the span's read is named in `toolchain/fast_code.ld` itself.
With `--fast-rom`, in GBA frames, before and after, the end states as
ct_native's throughout:

| Frame | Before | After |
|---|---|---|
| `attract-demo`'s heaviest game frame / picture | 11.57 / 11.93 | 11.47 / 10.43 |
| The rising palace's curtain (`attract-demo` 8867), its picture | 11.93 | under the battles' 10.43, out of the longest gaps |
| `gato-battle`'s heaviest (11580, a location's load) | 13.34 | 12.06 |
| A location's load (`new-game` 5801) | 11.21 | 10.92 |
| The field's menu opening (`menus-sweep` 2640) | 12.48 | 12.45 |

`attract-demo` takes 30,940 GBA frames rather than 31,362, `gato-battle`
24,227 rather than 24,228, `new-game` 7,652 rather than 7,647 and
`menus-sweep` 7,288 rather than 7,404. `run.py --own-time` finds the
attract's 50 places. A census of seven movies (`title-start`, `new-game`,
`gato-battle`, `menus-sweep`, `attract-demo`, `events-jetbike-race`,
`menus-save-point`) finds 1,559 picture clusters against 1,560; the 17 that
overlap none of the base run's each last a GBA frame (a line or two at the
screen's top, the most 735 pixels). Fast RAM keeps 576 bytes spare and work
RAM is 848 bytes above its floor. The heaviest pictures left are a battle's
first, laid out twice: its first pass's lines note a map the layout lacks,
and the second lays out and converts the maps again (about 0.7M and 1.3M
cycles). The loads' heaviest are copies bound by work RAM's bandwidth
(`memcpy`, `vars_host_read_span`), the SRAM's cold top and the field's
entries staged a byte pair at a time; the menu's opening, its tiles, a
slot's sum read from SRAM and the sends.

**The steady state** (issue #568). Past the loads, what keeps the GBA short
of 60 pictures a second is the ordinary frame that runs into a second GBA
frame. The report now counts the GBA frames each game frame's work fills:
its game, its sound and the picture made in the blank before it, which
takes the start of the same GBA frame. `run.py --profile-spans 2` samples
only the game frames that took two GBA frames from their start to the
next's. Over `gato-battle`'s such frames, a quarter of the samples were the
driver waiting for a blank (the frame after a late one), and the rest was
spread thin: the lines (`make_lines`, the transfers, their table reads and
compares, 11%), the sprites (`present_sprites`, 4.7%), the field's sprite
table (4%), the sound (5%), the menus' scripts and the reads through the
accessors. Timed a part at a time, an ordinary battle picture took 16K
cycles converting VRAM's changes, 18K the sprites and 58K the lines: a
third of a GBA frame out of the next game frame's.

Most of that touches nothing the GBA shows, so where the game frame ends
before its blank the driver has it made then (`gba_video_prepare`,
`services/video.c`, "The picture begun before the blank"): the sprites'
tiles turned onto the page not shown, the sprites into the table kept
aside, the lines into the changes and strengths the screen isn't reading
(the strengths are now kept at two ends, as the changes are). The blank
converts VRAM's changes and shows the picture; the lines stand unless the
conversion noted new priorities in a map. Laid-out pictures, Mode 7's
plane, mode 2's columns and copied columns are made whole in the blank as
before, and a picture is begun only where the time left, with an eighth of
a GBA frame of the blank's own, holds the longest one lately: one that ran
past the blank's lines would wait a GBA frame. The pictures' due blank is
now taken from the line count where the picture's making began inside the
blank, rather than from where it began, which drifted the pictures off the
GBA's frames. `offset_row` left fast RAM for the room: since #609 it runs
once a frame at most.

With `--fast-rom` (and `--fast-work-ram`), main against this, each end
state as ct_native's; the share of game frames whose work fills one, two,
three or more GBA frames:

| Movie | Speed | Pictures a second | 1 / 2 / 3 / 4+ GBA frames |
|---|---|---|---|
| `new-game` | 93.6% (94.4%), the same | 47.0 → 47.8 (47.8 → 48.7) | 81.1 / 17.8 / 0.6 / 0.5% → 82.8 / 16.3 / 0.5 / 0.5% |
| `gato-battle` | 89.0% → 88.8% (89.8%, the same) | 41.0 → 41.9 (41.7 → 42.9) | 73.7 / 22.8 / 2.1 / 1.4% → 76.6 / 19.9 / 2.0 / 1.6% |
| `attract-demo` | 72.6% (73.6%), the same | 33.4 → 33.8 (34.0 → 34.5) | 71.1 / 16.8 / 5.3 / 6.8% → 71.8 / 15.3 / 6.4 / 6.5% |
| `menus-sweep` | 88.1% → 87.6% (89.2% → 89.0%) | 41.3 → 42.7 (44.8 → 44.6) | 79.9 / 13.2 / 5.5 / 1.3% → 81.3 / 11.6 / 5.7 / 1.3% |
| `events-jetbike-race` | 76.1% → 75.8% (76.6% → 76.5%) | 31.0 → 32.4 (33.3 → 33.8) | 63.7 / 20.1 / 10.8 / 5.4% → 66.1 / 17.7 / 11.5 / 4.7% |

A census of the seven movies finds 2,249 clusters against 2,257 and the
pictures left out fall in all but `title-start` (`gato-battle` 5,684 to
4,823, `attract-demo` 5,804 to 5,158); four picture clusters overlap none of
main's, each 1-3 GBA frames of 11-36 pixels at an edge. `run.py --own-time`
finds the attract's 50 places. Fast RAM keeps 648 bytes spare and work RAM
is 336 bytes above its floor.

Two leads didn't pay. Reading the lines' tables where the game keeps them
(`gba_video_read_in_place`) rather than through `read_game` saved little:
the transfers read each byte several times, and from work RAM rather than
the fast RAM copy. A battle's second layout, a frame or two after its
first, as its lines name a map the first lacked (`gato-battle` 13848 and
13850, 0.9M then 1.4M cycles), costs as much keeping the caches' windows:
the new map's tiles take places that send the scene's tiles to new ones.
**Line effects that stand** (issue #568). A battle whose line effects run
all eight slots indirect (`gato-battle` 19313-21239) took 1.2-1.4 GBA
frames a picture working its lines out (333K cycles timed, for 15 changes:
about 40 lines visited, 200 transfers and 590 passed over, 124 table reads),
so every other picture was left out. Its transfers are the same frame after
frame, yet no frame's lines stood as the last one's (`make_lines`): the
frame read 62 places of the tables, each kept once for each time the eight
places of the read cache let it go, which overflowed the 64 kept and left
the frame unkept; and the windows' table flips between two copies every
few frames, which changed the starting point's hash. Now a place is kept
once, and only the indirect slots' values are kept by where they lie; each
slot's own table (its counts and pointers, a direct slot's values) is kept
by how many bytes the frame read from its start, and their hash, so that
either copy of the windows' table stands where both say the same. Through
that battle a picture's lines stand at about 30K cycles, the check of what
they read, and the stretch takes 14% fewer of the GBA's cycles. The
transfers' readers left the transfer loop rather than being inlined three
times into it, which left fast RAM 856 bytes more, and a transfer takes its
unit from the read its last one took where the cache still holds it.

With `--fast-rom`, main against this, each end state as ct_native's:

| Movie | Speed | Pictures a second | 1 / 2 / 3 / 4+ GBA frames |
|---|---|---|---|
| `gato-battle` | 88.8% → 90.3% | 41.9 → 43.1 | 76.6 / 19.9 / 2.0 / 1.6% → 77.8 / 19.0 / 2.4 / 0.8% |
| `attract-demo` | 72.6% → 73.3% | 33.8 → 34.2 | 71.8 / 15.3 / 6.4 / 6.5% → 72.5 / 15.6 / 5.6 / 6.2% |
| `events-jetbike-race` | 75.8% → 76.1% | 32.4 → 33.3 | 66.1 / 17.7 / 11.5 / 4.7% → 67.5 / 16.5 / 11.6 / 4.5% |

A census of the seven movies (`--fast-rom`) finds 2,211 clusters against
2,249 with the original colours and 2,029 against 1,995 with
`port-style`'s; the torn clusters are main's. The picture clusters that
overlap none of main's are each one to four GBA frames, mostly in
`gato-battle`'s battles and a menu list's page turn (`menus-sweep` 2773):
pictures main left out and this shows, the same wrong pictures main shows
at other frames (`gato-battle` 13174). With the lines never standing, the
same build shows the page turn wrong too. `run.py --own-time` finds the
attract's 50 places. Fast RAM keeps 1,504 bytes spare and work RAM is 280
bytes above its floor.

Two leads didn't pay: two places for each of four sets of the read cache,
and keeping what every frame read (the 1-in-16 tries kept): where the
tables change every frame the check costs more than it saves. The largest
line cost left is the jet bike race's (`events-jetbike-race` from 8221):
its plane's matrix tables don't change between frames, only its scroll's
does, yet each picture works out every line's transfers again, about 500K
cycles, two-thirds of it the slots' transfers. Lines that only one slot
changes could reuse the others' transfers, which needs room work RAM
hasn't to spare (issue #568).

**The plane's lines in stretches** (issue #568). The race's lines were
worked out again every picture at about 480K cycles (timed in a build that
printed each picture's), all of it the walk: its five slots are the
matrix's two rows and the first layer's scroll, a unit every line of the
track, the fog's strength every eight lines or so, and the mode. The scroll
changes every frame, so no picture's lines can stand as the last one's, and
caching the matrix's 160 lines (1.3 KiB) wouldn't spare the walk its other
two transfers a line. What costs is the walk's generality, not the plane:
its step a line was 2-3K cycles from the cartridge. Now a line whose
transfers set only the plane (`plane_registers`: the matrix, the centre or
the first layer's scroll) and whose visit shows nothing new but the plane
is taken in a stretch (`plane_stretch`, in fast RAM): every slot that sets
the plane reads its units straight from where the game keeps the table
(`gba_video_read_in_place`), each line's plane is worked out as the walk's
step for the plane alone does, and the stretch ends where another slot's
transfer, a line the walk visits whatever the slots do, the band's end or a
slot's entry's end comes, each slot left as next_transfer would leave it.
Lines outside the view (the track below the 160 lines shown) go the same
way with no plane. A frame that keeps what it reads (the 1-in-16 tries)
walks the lines as before, so the stand check stays whole. The race's
pictures take about 350K cycles in place of 480K; the stretches about 900
cycles a line, of which the plane's own sum about 300. The struct copies
GCC made into calls to the C library's `memmove`, which the walk paid for
on every line, are a field at a time.

The room: fast RAM's 1.4 KiB came from the view's memory (0.6 KiB), which
the dialog and the battle camera copy in and out once a frame, and the
last lines' state (80 bytes), both to work RAM; and work RAM's from the
report's histograms, to 16 GBA frames from 64 (1.1 KiB; no part's 99th
percentile has come near 16). Fast RAM keeps 576 bytes spare and work RAM
is 728 above its floor.

With `--fast-rom`, main against this, each end state as ct_native's:

| Movie | Pictures a second | 1 / 2 / 3 / 4+ GBA frames |
|---|---|---|
| `events-jetbike-race` | 33.2 → 34.1 | 67.4 / 16.6 / 10.9 / 5.2% → 67.3 / 21.8 / 8.9 / 1.9% |
| `gato-battle` | 43.1 → 42.8 | 77.7 / 19.0 / 2.5 / 0.8% → 77.5 / 19.2 / 2.6 / 0.7% |
| `attract-demo` | 34.2 → 34.2 | 72.5 / 15.7 / 5.8 / 6.0% → 72.5 / 15.8 / 5.7 / 6.1% |

The race takes 12,032 GBA frames against 12,370. `gato-battle`'s 59 GBA
frames more (23,986 against 23,927) are mostly the view's memory in work
RAM (23,946 with it kept in fast RAM and the stretches in the cartridge:
fast RAM hasn't the room for both). A census of seven movies
(`--fast-rom`, `--hand`) finds 2,366 clusters against 2,259 with the
original colours and 2,169 against 2,089 with `port-style`'s; the race's
Mode 7 clusters are main's, a little less seen (`port-style` 5946-8511,
1.69M pixels against 1.76M). The picture clusters that overlap none of
main's are a GBA frame or a few, of kinds main shows at other frames, where
the pictures left out moved: the world map's dots before the race, the
attract's and the battles' window edges, a battle's cut panel. `run.py
--own-time` finds the attract's 50 places.
Reading a slot's units in place outside the stretches, a transfer at a
time, cost more than the reads it spared (`gato-battle` 24,015).

## Scene map fills

`vid_fill_vram_half` fills the chosen low or high byte of VRAM words, preserving
the other byte and wrapping addresses. Native tests cover byte selection,
wraparound and untouched bytes. The title's BG1 clear, the unused-plane fill
and the credits' map (its tiles, then its in-front bytes) use it. The title
clear removes stale field tiles after game over and attract playback; three
native picture fingerprints protect this change (REIMPLEMENTATION.md).

## Memory moves and VRAM read-backs

Eighteen of the last nineteen register escapes were video transfers and a
setting the original programmed from its own tables (the nineteenth, the
sound driver's register write, is under Audio). Each is now one of four things:

- **Memory the game moves itself.** The scenes' clears copied the Mode 7
  multiplier's product over memory, and the menus filled and copied work RAM
  through its port. The game now writes the memory (`wram_fill8`,
  `wram_port_fill`, `wram_port_copy`) and charges the move with
  `tm_charge_transfer`, which costs the main thread what the SNES's transfer
  of as many bytes did and leaves a handler's moves to the frame, as the
  escape's host half did. The matrix values the original multiplied with are
  still set: they are video state.
- **Sends from where VRAM's address stands.** The scene program's loaders set
  the address, unpack, then send in the mode their call cells keep.
  `vid_continue_vram` and `vid_continue_vram_half` send whole words or one
  byte of each word from the cursor, stepping as the port says; the scene
  decodes its cells' mode and port into one or the other. The race's plane
  halves and column use the half forms, the battle's described transfer
  `vid_send_vram_in_mode`.
- **VRAM read back.** `vid_vram_word` answers a word as the game's uploads
  left it, never a wider view's own (`vid_send_view_vram`).
  The race copies its plane's column and the palace its map out of VRAM with
  it, keeping what the original's port reads give: the first read after the
  address is set hands back the prefetched word, which the next read fetches
  again, so each copy's first entry is doubled and the rest lands one place on.
- **A setting the game's data names by address.** The menus' object scripts
  name the sprites' size and tile-place byte by its SNES address;
  `vid_setting_at` says which setting an address is, and the script's write
  becomes `vid_set_obj_config`.

Natively, the race's plane halves and column, the palace's read-back and the
credits' map were counted and dropped before; they now draw. The palace's
blank rows were filled with the product's low byte, 0; the original copies the
product a word at a time, tile `$100`, which the port now writes. The gate's
`attract-demo` fingerprint moved with these pictures, twenty samples in the scene
program's two passes (tools/native_picture_baseline.txt).
`tests/port/video_test.c` covers the cursor sends and the read-back,
`tests/port/timing_test.c` the transfers' time.

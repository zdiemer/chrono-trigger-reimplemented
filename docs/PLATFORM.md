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

An accessor reads its resource by name (`game/rom.h`): `asset_read8(g,
ASSET_LOCATION_RECORDS, place + 3)`, a place in the resource, which the
store finds by the resource's name, so a pack may keep it anywhere; an
offset past the resource's end reads on by address, as the original's
indexing does. 491 of the data headers' reads (call sites) name their
resource. What the data's own pointers lead to (a stream a table points
to, a script's text, a bank and a 16-bit place the data gives) is still
read by the address the original keeps it at, 200 call sites, the address
a key the catalogue's rows are looked up by. A region the catalogue hasn't
typed is cut where each accessor's table starts, the piece named for its
accessor and still `mixed`, so a read names its own table rather than the
region it shares (issue #127 types them).

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
(`menu_scene_stream.N`) and the credits' letters
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
(`menu_scene_stream_c6fea0.N`); and `lz` for the streams that have no
text of their own yet (the world maps' scene scripts, issue #130), in hex. A
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
menus queue (`script_c2bece` and nine more) were typed `script` and are
records, `struct:records:7`.

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
  original shows where no layer is opaque. A line whose tilemap is one screen
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
  own and sends them in the vblank (`vid_send_view_vram`, below): the columns
  of the room the screen is held to, the map's edges it doesn't scroll past
  (`field_map_edges`) -- an interior lays several rooms side by side in one
  map (Truce's houses are one), and the original never shows a room's
  neighbours beside it -- with the screen's own where a script has moved it
  past them; a backdrop scrolling at half the pace goes by the whole map. Past
  the room a column is built as its edge's, and shown only where the edge is
  one entry all the way down (a void), which is judged as the margins are
  built and kept for the frames between, when a battle's data may lie over
  the map's looks; elsewhere the layer stops at the room and the backdrop
  shows. Each frame
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
- **The battle window** (`game/battle/wide_window.c`) keeps the windows at its
  left (the command windows, the enemies' list) at the view's left side,
  stretches the party's window to its right side and spreads each member's
  name, numbers and gauge across it; the tech page's two windows each stretch
  to their side, and the item list and the message window across the view.
  What the window's rows and frames show is noted as the vblank sends them
  (from the transfer's source, and the frames' window corners), so the layout
  always fits what VRAM holds.
- **The menus** (`game/menu/panels_across.c`, the mode at `$C2:800E`) take
  a layout per screen from an explicit list in that file. Most screens are
  framed: where a layer runs on in a four-column pattern across the edge of
  its outer columns (a window's inside, the backdrop), the columns outside it
  move out to the view's sides and the pattern fills the gap; the lists, text
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

GPU backends must preserve line effects and color math. Smaller targets need measured
memory and coroutine stack budgets; a callback-based frontend needs an appropriate
fiber backend or resumable game tasks.

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

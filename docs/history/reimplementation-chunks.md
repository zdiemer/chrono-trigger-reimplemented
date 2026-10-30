# Phase 3, chunk by chunk

The per-chunk record for the three big banks -- the menus, the field and
events, and the battle -- and for the scene program in bank `$7E`, as each
chunk of routines became C. The newest section
stays in docs/REIMPLEMENTATION.md as the shape a new one follows (the field's and the
battle's, at the split; the menus' next chunk will keep its section there until
the one after it lands).

Split out of docs/REIMPLEMENTATION.md, which keeps the mechanisms, the current
phase and an index of these files.

## Phase 3, menus: the load screen, the window language and the opening

The first chunk of Phase 3's bank `$C2` wave: the screen every game starts
from, and what it is built with. `python3 tools/burndown.py` for `$C2`: 17.2
KiB ported and 71% left with 173 entries before, 19.8 KiB and 67% left with
216 entries before the opening's two came out (TODO.md); about 2.2 KiB of new code in 41 entries, and about 0.7 KiB of
earlier ports (the save screen's slot, `menu_window_set_line_effect`, the
sprite reset) rewritten onto what the chunk built. All of it to the bar:
`game/menu/` scores 0.03 operations a line with 35% of its functions on the
machine, and none of the new files is on the baseline.

**The files, by the game.**

- `windows.c`: the window command language every menu screen is drawn with
  (the interpreter `$C2:ED31`, its eleven commands and their handlers). The
  commands are one C table (`LANGUAGE`) that decodes operands into places,
  sizes, styles and strings, and names the handler whose time each command's
  work is charged at; a window style is the tile its twenty tiles start at,
  in the player's design or the one a list names. The frame, the inside's
  four-by-four pattern, the colours of a design and the label strips are
  functions the load screen and the save screen call directly.
- `cursor.c`: the cursor's reading of the pad (`$C2:E984`: a push moves it to
  the place's neighbour in that direction, the buttons it reports), the
  sprite table's reset under it, and the menus' sounds (`$C2:EABA`-`$EB03`,
  through the audio service with the original's three-frame wait left to the
  driver's call).
- `load_screen.c`: screen 14's steps after its opening (choosing, which game a
  New Game+ carries on from, and the erase question) and a slot's row in the
  design its game was saved with. The opening step itself is still the
  original's (below).
- `save_screen.c` gained the slots' reading as the menus open (`$C2:D156`):
  the save code's check of each slot, the summary read from the cartridge
  through the save service, the chapter's and the slot's name labels; and
  `saves.h` is the cartridge's record of the three games (the slot saved
  last, times finished, whether the game has been finished, each slot's mark),
  which also retires `menu_saves_finished_game`'s read of save RAM through the
  ROM accessor.
- `opening.c`: what the menus do each time they open (`$C2:8096`): take the
  interrupts, quiet the sound effects, blank the screen, read the slots, list
  the party, unpack the settings. Its graphics, sprites and items are still
  call-outs (TODO.md).
- `bridge.c`: the menus' one way to call the original's code
  (`menu_call`, `menu_call_long`, and `menu_call_returning` for a callee
  that comes back with widths of its own), `menu_run` for a sibling port run as
  the original's `JSR` reaches it, and the frame of a routine a bridge stands
  in for. Unlike the battle's and the field's, the menus' routines change
  widths from routine to routine and save their caller's with `PHP`, so a
  call's check is the widths, the page and the data bank it was made with.

**A liveness hint for the task switch.** The menus' tasks yield with
`JSR $C2:821E` and wait with `JSR $C2:822B`, which save the caller's
registers on the task's own stack and return on another task's; the pairing
of pushes can't follow it, so every register was live after every call to
them and no mode handler could declare anything dead. `re/hints.toml` now has
`[[keeps]]` (DISASSEMBLY.md): a call that comes back with the listed
registers as its caller had them, reading only `reads`. Liveness takes the
call as that summary (`tools/disasm/liveness.py`, `_kept_summary`), and
`tests/test_disasm.py` checks it on a task switch built in the test.

The first cut of the hint was wrong about DB, which the review found: neither
entry saves or restores it, and the scheduler resumes the next task with the
yielding task's DB, setting it only after every task has had its turn
(`$C2:81C1`). The hint had DB dead everywhere, which let a false `DC_DB` on the
load screen's yield past the don't-care check, and five entries' declarations
with it. The hint now names DB in `reads`, and `tools/disasm/keeps.py` checks
every `[[keeps]]` hint against the code: the entry pushes exactly the kept
registers, the hint's `resume` pulls them back in reverse before it returns,
and every register the entry doesn't save that some path to the next task
reads before setting, or doesn't set at all, is in `reads`. The don't-care
check runs it, so a wrong hint fails the gate; `tests/test_disasm.py` has a
switch built in the test that a wrong hint of each kind fails on. The check
takes `$822B`'s `XBA` as reading both bytes of A, so its hint names `AH` as
well. The six declarations are the smaller sets liveness now gives.

**Found by shadow mode.** The cursor's reading leaves its loop's bit in the
menus' first cell; a label strip steps its tile after setting bit 4, not
before; the list handlers that run with any accumulator width (`$C2:F378` is
reached with a 16-bit one from `$C2:C773`); `$C2:968D` comes back with an
8-bit accumulator; the slots' reading leaves the slot count at 3 and the time
played's cell holds the game's start as the bus addresses it. A guard that
read save RAM through the service cost the machine 8 clocks and moved the
generated code's timing: guards peek (`snes_bus_peek`).

**Found by fuzzing** (tools/fuzz_routines.toml; PORTING.md has the counts):
the settings' clearing clears a byte past the last setting; the slots' count
starts from the menus' zero word, and the gold a summary hides is written
from it; the summary reads the checked slot's game from the cell the save
code leaves (`menu_slot_game_at`), not from the slot's number, and steps
through the party a byte at a time within its page; a time played's digit
past 9 carries through the original's shifts that multiply by ten, which no
clock of the game's makes, so the time's guard and the save screen's (whose
header draws the cartridge's time) take only decimal digits and the port
multiplies by ten; a window list's
string reads the number conversion's zero word, so the save screen's guard
now asks for it too; the header's "taken" count is a byte; loading a design's
colours counts a palette change a word wide, so the drawing's guards leave
room in the count; and several entries were refused widths the game never
calls them with (the index registers 16-bit), where the original loops. The
save screen's rewrite against its predecessor (`tools/fuzz_compare.py`): 0
mismatched of 2,000 both before and after, once the summaries' names are
letters, as the game's name entry makes them; with control codes in the
names, the strings drew through the text port's accepted divergences (TODO.md).

**A port that spans frames.** The load screen's opening (`$C2:E613`, which
draws the whole screen through `$C2:E7C3`) was ported and matched wherever
shadow mode could compare it, but every call of it in the library is
interrupted, and in the shipping mode it broke the `beyond_time` route: loading
the Final Battle save, the screen never appeared and the game ran into a `BRK`
two frames later. Its drawing takes the original more than a frame, with the
menus' vblank handler taken inside it; as one port, the time is owed at its
return and the handler taken there, twice, and with the time charged cheaply
(an experiment, not a fix) the screen appears. The route passes with the two
entries left to the original. Why it went wrong was the runtime's: the
debt was paid inside the menus' vblank handler, which moves the stack, and
the next vblank nested in it (fixed; "The tooling pass after Phase 3's first
chunks", below). The port came back after that fix, unchanged but for
`$C2:E613` stopping with `m_stop_at` at the task start it jumps to
(`$C2:83B2`), which starts the screen's fade-in: `beyond_time` passes in the
shipping mode, and both entries are fuzzed (2,000 states each, none
mismatching), which is their only check, since every call of theirs in the
library is interrupted. For the same reason the timing gate has no stretch of
theirs to calibrate from: their `cost_milli` is left at the menus' ports'
usual 5000, and nearly all their time is their callees' (the window lists,
the slot's drawing and showing), each at its own calibrated rate.

**What the effects check found** ("The tooling pass after Phase 3's first
chunks", below): reads of cells the menus' ports wrote only in scaffolding.
The load screen's member rows write the row and the member the portrait's
own entry (`$C2:D466`) reads as plain state, the member as a byte of its own
(`menu_scratch_save_member_character`), as the original's `STA` does; a
window's frame leaves the inside's size as plain state wherever it is drawn
(the frame's entry, a list's window with or without its inside, the save
slot's window `$C2:ECDB`); the slots' rows keep the next label's first tile
plainly. `menu_object_wave_hdma` reads a phase another subsystem's scaffolding
leaves in the field page's cell, as the original's does; that one has a
reason in `tools/effects_baseline.txt`.

**Timing.** Every entry is calibrated from the library but the ones only
`menu_open` runs, whose error its own rate takes up (TODO.md), and a window list charges each command at its handler's
rate (the handlers are in the `LANGUAGE` table: the first cut took their
addresses from the ROM's table without their bank, so their time went to no
hand port). The inside's pattern is drawn a row and repeated with block moves
as the original does, which is where its time goes. Three entries are
exceptions (`tools/timing_exceptions.txt`): the list interpreter and the
string command carry the text engine's spread in vblank, which the text port
already has. In the shipping mode the menus' vblank work lands on line 41-44
once in each of four save movies (two picture lines on one frame;
`tools/vblank_check_exceptions.txt`).

## Phase 3, menus: the typesetter, the graphics and the items

The menus' second chunk: the engine every string of the game's text is set
with, and the rest of the menus' opening but for the characters' sprites.
`python3 tools/burndown.py` for `$C2`: 19.8 KiB ported and 67% left after the
first chunk, 22.8 KiB and 62% left after this one, 226 entries.

**The typesetter** (`menu/typeset.c`, `$C2:57DF`-`$C2:6262`). The dialog, the
battle's messages and the menus' labels all set their text with it: a caller
names the string (a table and a number), the tiles and their layout, puts the
string's numbers and names on the text page, starts it (`$C2:57DF`) and then
calls it (`$C2:5823`) with a budget of glyphs, a glyph or two a frame for the
dialog's typing. A call stops at the string's end, a wait, a line or page
break the caller acts on, or the budget's end, and says which; where it has
got to lives in the text page's cells (`menu_text_reading`), so the next call
takes it up in the middle of a word, a name or a number. The string's bytes
are glyphs (and the font's two-byte half), words of the dictionary, and
control codes: the breaks, the numbers (decimal without leading zeros, or hex
where the page's hex setting is on) and the names (a character's, the
party's, an item's, a tech's, an enemy's). The glyph drawer (`menu/glyphs.c`)
now draws from parameters, into four- or sixteen-colour tiles, and its three
entries take them from the page's cells as before; the menus' label setting
(`$C2:F90C`), which sets a label a line of 32 glyphs at a time, is a port too,
taking the label's table, string and tiles and giving back its width, and the
load screen and the save screen call it as C.

What a call leaves in X and Y nothing reads, and the entries declare them
dead. For a while the analysis had them read after the label setting: the
save screen's slot rows end in a `JMP` into the menus' task yield
(`$C2:CF5B` to `$C2:821E`), and the yield's `[[keeps]]` hint covered only
calls to it, so the analysis followed the yield's own code in the handler
and took its `PHY` for a read. Liveness now takes a jump into a routine with
a `[[keeps]]` hint as a tail call with the hint's summary, where the jumping
routine returns by `RTS` with nothing of its own on the stack, so that the
switch's `RTS` is the routine's own (review asked for that condition; the
screen handlers the dispatch enters, which return by `RTS` alone, count)
(`tools/disasm/liveness.py`, tests/test_disasm.py, DISASSEMBLY.md), and X
and Y after `$C2:57DF`, `$C2:5823`, `$C2:F90C` and `$C2:D296` are dead. Over
the whole tree only ten declarations' accepted sets changed, all the menus'
(the glyph drawer's three entries, the cursor's input, the slot rows', the
label setting's, the chapter label's and the typesetter's two), each growing
by X or Y. Before that the port worked the registers out as scaffolding: a
first version passed a small `Ending` back through the reading functions,
which review rated a translation (it told apart only what the original's
registers do), and a second read the string again in `menu/leaves.c`, now
gone.

Two things the gate made the port keep. A name past the pointer table's seven is read
through the table's neighbours, which can put it on the text page itself: the
original counts the name's glyphs with the insert already begun, pointer and
source set, so the port does too (fuzzing found it: a party member with an id
past the characters'). And a call keeps its budget in hand, where the
original counts it down in its cell on the page: the two part only where a
call reads that cell (as a string, an argument or an insert: such a name,
more than 19 arguments' bytes, a pointer onto the page) or a label long
enough to wrap its bank draws over it, so the guards read a text ahead,
a piece and its arguments at a time, and turn those away (fuzzing found a chapter label that differed
before they did).

**The graphics** (`menu/graphics.c`, `$C2:968D`, `$C2:C511`). The tile sets
every screen draws with, built in work RAM from the ROM's graphics and sent
to VRAM as the menus open: four-colour tiles laid out as sixteen-colour ones,
three-plane tiles given a fourth, extra planes made from the ink of the ones
there, then the menus' colours and the window design the player chose (which
the settings screen loads on its own too). The display's queue sending, its
palette, and its two transfers into work RAM became functions on the game's
state (`menu_send_upload_queue`, `menu_send_palette`, `menu_clear_memory`,
`menu_copy_rom_to_memory`) for this.

**The items** (`menu/items.c`, `$C2:9875`, and the lookups `$C2:8881`,
`$C2:88AD`, `$C2:88D8`). As the menus open, the inventory loses the places no
item fills, the story's and the place's say in what the item lists offer is
noted, and every item's kind, price, users, amount, attack or defence and
name place is copied into the menus' tables; the save code's list of what the
party carries (`$FF:F813`) and each member's figures (`$C2:99B2`) are still
the original's, called from the port.

**What was left of the opening** was the characters' sprites (`$C2:F3CA`),
which the next chunk took (below).

**Found by fuzzing**: the names' count on the text page, above; a number's
leading zeros are shifted out of its digit cells as the original shifts them,
leaving the cells past the number as they were; the lookups leave Y's high
byte clear (they run with 8-bit index registers). **Found by shadow mode**:
the lookup leaves the category in A; the item tables leave the second place
note's test and item $C8's kind in the flags the save code's list saves; the
graphics leave the tile loops' counts in the menus' first cells, and the
flags of the design's setting.

**The effects check** found the glyph drawer's entries reading cells only
the original's setter writes now; the baseline gives the reason (only the
original's code calls those entries). The queue's count is written a word
wide as game state, since the byte after it aliases the sprite-object mode's
slots.

**Timing.** Calibrated from the library. `menu_text_go_on` and
`menu_set_label` carry the glyph drawer's spread (the pen's offset decides
its shifts), and are timing exceptions like `menu_draw_text`. The graphics and
the items run once as the menus open, with the screen blanked.
The new timing moved the `bucket_lavos` route's Lavos Core fight onto a
frame where the route's bot, backing out of a tech list it had opened
before the list took input, held B across its four presses, which the game
reads as one: the list stayed open and the bot, waiting for the command
menu, waited for good. `tools/movies/bot.py` now releases B between presses,
and the three long routes pass in the shipping mode.

## Phase 3, menus: the characters in the records

The menus' third chunk: the characters the menus show, in the records the
screens draw their sprites from (`menu/sprites.c`, "The characters the
records show"). `python3 tools/burndown.py` for `$C2` (as main's tree counts it
after the battle's second chunk): 21.5 KiB ported and 64% left after the
second chunk, 22.3 KiB and 62% left after this one, 231 entries.

**What a record shows.** A record ($40 bytes at `$7E:1800` on, the direct
page its routines run on; nine of them, the ninth `$1A00` only marked empty)
names a sprite of one of three sets, the playable characters' and the
others'. Its load (`$C2:F422`) looks the sprite's assignment up in bank `$E4`:
where its graphics and its frames are, its colours, its animations and its
size (one, two, three or six blocks of 32 by 32 pixels). It puts the twelve
colours in the record's sprite palette and marks the palette changed; and for
all but the playable characters, whose graphics the ROM keeps as they are, it
has the unpacker put the graphics at `$7E:B000` + `$800` a record, a call to
the decompressor's port from the load's entry, the one call-out the chunk
keeps. The opening (`$C2:F3CA`) loads the first seven records with the
characters in their standing animation facing the viewer and marks the last
two empty.

**The animation** (`$C2:F69D`, and `$C2:F69B` a frame on, which the records'
task runs every frame) is a list of frames for each of four facings, each
frame held for a time; an animation's bit 7 has it loop from its loop frame
rather than its start, and one with no frames is left past its end. A change
of frame (`$C2:F75C`, registered so it is timed and fuzzed alone) copies the
frame's sprites' offsets into the record and builds its tiles into the frame
buffer at `$7E:8000` (a blank tile, a tile of the graphics, or one mirrored
left to right through the ROM's table of reversed bytes), `$200` bytes a block
past the blocks queued this frame, and queues them for VRAM: an even number of
blocks in one upload, an odd number a block's two rows of tiles at a time. The
records' task holds a record's next frame back once a frame's blocks reach its
limit (`menu_record_block_limit`).

**Kept for the original.** The frame's tile list, which the original copies
over the save summary and reads back as it builds the tiles, is scaffolding:
the port reads the frame's data in place. So are the block move's stub (its
source bank), the multiplier the original looks the assignment and the frame
up with, the upload record at `$7E:9A88` and the frame's loop counts in the
menus' first cells. The effects check found the save screen's readers of the
save summary meeting the tile list; the baseline gives the reason (the save
screen copies the summary there afresh before it reads it), and the frame
building's own entry, which takes the tiles where the original's animation
leaves them, the same. Over the shipping-mode runs, an upload the original
builds over the save summary is queued with its unused eighth byte, which
the tile list left: nothing reads the queue's eighth bytes (baseline). The opening counts its records in the time played's
hours cell (`menu_opening_records_loaded`), which the save screen's header
then takes for a time it has drawn: written plainly.

**Guards.** The frame buffer, reached through `wram.h` since its place is
worked out, must end clear of the block move's stub (`$7E:9690`); a sprite
has at most three blocks (six-block sprites' offsets would run past the
record, and no menu shows one); a record's pointers name banks that are ROM
or work RAM throughout; the facing is one of four; a looping animation's loop
frame has a time; the VRAM queue counts whole records; a record's tiles
start on an upper row of tiles in VRAM, where its frame's blocks go a row
pair at a time; a record's sprite is of one of the three sets; and a facing
has fewer than `$4000` frames. None refused a call over the library.

**Arithmetic.** The original's sums of an animation's place carry a flag
from one add into the next (a facing's place is a multiply by three with its
shift's carry; the times' place takes in what the division by four left).
The port writes them as plain sums: they can't carry for any sprite the
ROM's tables have (`tests/test_menu_sprites.py` reads every entry: every
span of frames is whole facings, fewer than `$4000` a facing), and the
guards keep the records' own cells within the same bounds. A review of the
first version rated it a translation for writing the carries out.

**Shadow and fuzzing.** Over the 26 movies every call compared, none
mismatching: `menu_load_record_sprites` 37, `menu_load_record_sprite` 262,
`menu_animate_record` 576, `menu_step_record_animation` 317 (the frame
building runs inside them as C, its time its own over 571 frames). Fuzzing
found the VRAM queue's count
unaligned (the queue's own entries refuse it; the records' guards now do too)
and pointers into banks the I/O registers share (the tiles read open bus
there), and the frame building's own entry, which copies with the stub the
animation leaves, needs the stub in place (its guard asks it).

**Timing.** Calibrated from the library. The port fills and copies a
frame's tiles itself and charges the bytes at the block move's rate; a
mirrored tile it builds a byte at a time. A frame's time follows its mix of
blank, copied and mirrored tiles, which that leaves to calibration (a first
version charged per-tile cycle estimates, numbers review took out): a mean
error of about 6-7% and at most 11,300 clocks, so the frame building and the
two animation entries are vblank-tail timing exceptions
(`tools/timing_exceptions.txt`), writing work RAM and the VRAM queue only. The block move's own rate
(`memory_move_stub`) was recalibrated: the animation's copies no longer go
through it on the machine, which changed the calls it is measured on.

## Phase 3, menus: what the menus' ports called most

The menus' fourth chunk, picked by value rather than by run: a profile of
`--native on --generated off --misses` over the menus' movies, the interpreted
instructions summed by the function they are in, put five routines the
existing ports call out to at the top once the task scheduler (`$C2:821E`
and its neighbours, about 9%, left for fibers) is set aside. The window
helpers at `$C2:EC38`-`$C2:ECDA` took about 12% together, the records'
task (`$C2:F657`) 6.7% and the menus' per-frame input (`$C2:8473`) 4.9%.
`python3 tools/burndown.py` for `$C2`: 22.3 KiB ported, 62% left and 231
entries before; 22.6 KiB, 62% and 236 after.

**The frame's input** (`menu/cursor.c`, "The frame's buttons"): the menus'
vblank takes the controller, the second controller, a frame of play time,
the held buttons' repeat pacing (a long wait before the first repeat, short
ones after, back to the long wait when nothing but Start is held; a screen
can ask for a frame of repeating every frame, `menu_fast_repeat`) and the
going-back hold (the going-back assignment's buttons with Start sends every
screen back from then on). The controller read and the play time's tick are
the shared input's parts, called as C (`core/input.h`: the read, the tick,
and where a read that didn't take the buttons stops); only where the console
is still reading the controller, or the reset buttons are held, does the
port build the stack the original has there and stop. A first version called
the two ports as call-outs, the original's saves round them rebuilt, with a
check after each that the machine came back as expected; the check after the
tick held the tick's page to the controller's, which the tick run as the
original's code leaves at its clock page (`$0400`), so the port handed the
rest of nearly every call to the original, comparing everything, and 6 calls
a movie differed in A's high byte, which is dead there.

**The records' task** (`menu/sprites.c`, "The records' task"): once a frame,
each of the nine records that shows a sprite has its animation carried on
where its frame's time has run out or another is asked for, unless the
frame's uploads have reached their share, and is drawn unless hidden; then
the task yields (`m_stop_at` `$C2:F696`). The byte after the uploads' count,
which the original clears with it as a word, is written plainly: the
sprite object mode's slots share it.

**The tilemap touches** (`menu/tiles.c`): a character's large portrait (36
tiles six by six, numbered on from its first in the menus' first cell, whose
high byte the callers set to the attribute, and its sixteen colours loaded
into the palette the attribute names after the tiles' count has carried into
it); a stretch of a row pair given one attribute; and a block of rows given
a palette and priority over their tiles' low bits, the drawing origin moving
down as it goes (`$C2:ECC2`, the one row, inlined). The counts are an 8-bit
tile count and a 16-bit row count counted down in 16-bit registers, so 0
counts the whole bank; the guards turn those away with any stretch that
would leave the bank. The row pair's callers go on with the count in A's
high byte, which the entry leaves. The portrait's word increment of the
palette's changed count (`$0D15`) carries into the next byte, which the
guard keeps short of, as the window colours' does.

**Shadow and fuzzing.** Over the 26 movies every call compared, none
mismatching or refused: `menu_take_frame_input` 35,169 (every movie),
`menu_run_record_task` 4,637 of 4,638 (one interrupted),
`menu_draw_large_portrait` 235, `menu_recolour_rows` 1,314,
`menu_set_row_pair_attribute` 2,776. Fuzzed from the `menus` state, seed 1,
none mismatching: the input 2,000 of 2,000, the task 2,000 of 2,000, the
portrait 1,893 of 2,000, the rows 1,890 and the row pair 1,987 (the rest
refused). Fuzzing found the portrait's carry into `$0D16`. The menus' vblank
work, which calls the input, is unchanged at 415 of 500 matched.

**Timing.** Calibrated from the library. The task carries the animation's
and the frame building's spread (mean error 1.2%, at most 10,872 clocks), so
it is a vblank-tail exception as they are, writing work RAM and the VRAM
queue only.

## Phase 3, menus: the equipment screen and its member list (`$C2:9C31`-`$C2:A2C5`)

The largest run the plan named for `$C2`, 1,685 bytes: the equipment screen
(the menus' screen 1), the main menu's panel for it, and the member list both
draw from, with a member's loading (`$C2:8820`) that the list and the card
need. An earlier attempt at the same run was reviewed as a translation (each
step the original's branch tree in a `Snes *` body, flag notes threaded
through every step, JSR returns pushed by hand); this one was written again
from the drafts.

**What it is.** `menu/members.c` is the member list and the card: a member's
loading, the rows (`$C2:A1B2`, a row `$C2:A216` with its face and low-HP mark
`$C2:A1EF`, an empty one `$C2:A1E9`), the rows' colours (`$C2:9E76`,
`$C2:9EB0`, through a `menu_recolour_block` that `menu/tiles.c` now exports),
and the card (`$C2:A22F`, again with the top row `$C2:A21F`): the time
played, the figures and stats (the original's `$C2:9137` and `$C2:A6F0`,
called out to), the large portrait and the element's label (`$C2:A273`, the
element now a `MenuElement` in `menu_data.h`). `menu/equipment.c` is the
screen as the game sees it, no machine in it: each step is a decision
(`menu_equip_choose_member` returns `MEMBER_CHOSEN`, `MEMBER_LEFT_FOR_THE_MAIN_MENU`
or `MEMBER_POINTED_AT`, and so on for the slots and the items) and each part
between the original's calls an operation in the game's terms (the member list
going round or sliding, the shoulder assignments switching members, the item
put on and the one taken off given back, the list of what fits paged or slid,
its rows moved). `menu/equipment_bridge.c` runs them: per step it reads the
cursor, asks equipment.c, and sequences the sounds and the card as siblings
(`menu_run`; `menu_run_any_widths` for the list's move, which leaves a 16-bit
accumulator after a slide) and the original's drawings, inventory, paging
and frame waits as call-outs, stopping where the original jumps on. The
member slide's frames (`$C2:9DAF`) are a loop of `menu_equip_slide_members_a_frame`
around the task's yield; the slide's thumb follows the step's sign, where the
original reads the line's carry (equal for the lines the slide's start
sets, which its guard holds it to).

**The flags, settled first.** Every call the steps keep was checked against
liveness before the logic was written (`tools/v_readers.py`, and the strict
solve): the yields, the inventory, the paging and the list's slide read
nothing; the drawings reached through the window language may read the
overflow and the carry. What liveness can't declare is the P byte a sibling
saves as it starts (the sounds, the card, the slots' pointing, the member
switch), which shadow mode compares under the sibling's own calls out. Three
things keep those few:
- where a step **jumps** on to a routine that is itself a port (the member
  list's move `$C2:9D17`, the slots' pointing `$C2:9ECD`, the screen's
  set-up `$C2:A11A`, the card after the move, the refusal's sound), the
  bridge stops there and the runtime enters it from the original's own state,
  so its saved P is the original's (stops checked against liveness like any
  other);
- where a routine's saved P would carry a note and nothing else in it needs
  one, the routine stays the original's: the member slide's start
  (`$C2:9D5B`, whose P is saved after the new first row is counted, under
  every frame's yield) and the member list's scroll bar (`$C2:A170`, under
  its call to the bar's task);
- what is left is one leaves helper per decision, at the end of the bridge
  file, worked out from the state the decision left (`leave_member_pointed`,
  `leave_slot_step_tested`, `leave_member_switched`, ...): the comparison or
  load the original made last before a sibling saves P, and the overflow of a
  sum before a drawing that may read it. A call-out declaring a flag
  don't-care and returning it as it found it leaves the port's own value, so
  a tick after such a call still needs the original's flags: the paged and
  the slid list's ticks and the card's stats call have their own.

The sounds' driver call now declares Y dead (`menu/cursor.c`; the driver
reads A's high byte alone, which the don't-care check confirms), which a tick
after the inventory's calls needed. `ram_map.py`'s `reuses` may name a list
(the list of items over two of the menus' cells).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches, none refused: a member's loading 1,360 calls, the rows' colouring
219 (each row 657), the card 223, the member step 216, its move 212, the
slots' step 470, their pointing and the member switch 460 each, the item
step 564 (the list's move inside it, 556), the screen's opening and set-up
2 each, the weapon's byte 8, the panel's dimmed colours 8; one row drawn
alone of 33 interrupted. The library never slides the member list, switches
members with the shoulder assignments, slides the list of what fits or
refuses a slot, and the panel's one call is interrupted; a scratch movie from
the same save (the field menu's, kept outside the repository: it slides the
members both ways, switches members, slides and pages the accessory list, puts
an item on and returns to the main menu) matched every call over 6,296
frames: 7 slides, 7 switches, 12 rows moved. It found two flags the library
didn't: the list's tick after a page turned (the paging's flags kept through
the list's drawing, which declared them dead) and the card's stats after the
member's sum. Fuzzed from the `equipment` state (`menus-field-menu` at frame
3800, low RAM, the tilemaps and the screen's tables kept and the decisions'
cells filled; `tools/fuzz_routines.toml`), seed 1, 2,000 states each, none
mismatching; compared: every state for the loading, the rows, the colours,
the element, the set-up, the member move, both slides (up to the first
frame's wait), the rows' move, the weapon's byte, the panel (up to its first
frame's wait) and its colours; the card 1,642 and its redraw 1,619 (the rest
the figures' call past the fuzzer's budget), the member step 828 and the
slots' step 958 (the slots' drawing past the budget), the slots' pointing
1,477 and the switch 1,473 (the rest the guard's and the budget's), the item
step 1,611 and the list's move 1,599, the face 1,763. The first runs found the list's paging read as its new first
row (the paging answers $FF for any turn), the element's label indexed by the
element's half (an odd table byte past the seventh character), the card and
the rows needing 16-bit index registers at entry, and a row redrawn after the
card from a record the figures' routine had left naming no character (the
bridge now hands that row to the original). One state in 1,671 of the card
differs where the original's figures leave the upload queue's count
off a record (TODO.md).

**Timing.** The new entries were calibrated over the library, with the
tiles' recolouring (which now writes the drawing origin once) and the
cursor's tick; no exception. The entries whose work is nearly all another
routine's (a row is the window language's, the colouring the rows') carry
large scalars on few accesses of their own. `$C2` stands at 24.2 KiB
ported, 59% left.

## Phase 3, menus: the items screen and the equipment screen's windows (`$C2:A2CD`-`$C2:B352`)

The rest of the equipment screen, which its steps called out to (`$C2:A2CD`-
`$C2:AA3F`: the slots window and the list window, each sliding in and out),
and the items screen beside it in the bank (`$C2:AA40`-`$C2:B352`, the menus'
screen 2, with the main menu's panel for it): 3.9 KiB, the two screens that
show the party's items. `$C2` went from 24.2 KiB ported (59% left, 261
entries) to 28.1 KiB (53%, 300 entries). The plan's `$C2:B520` is the techs
screen's, the next run along; it shares the member window and the list's
slide written here, so it is the next chunk, taken whole.

**What it is.** `menu/item_screen.c` is the screen as the game sees it: the
commands over the list (the list, the sort), the list with an item picked up
and put down elsewhere (a second cursor holds its row), the sort (the party's
items gathered, used-up places dropped, the categories in order with what the
party keeps rather than uses after the rest), who can equip a weapon or
armour (the names of those who have joined, its attack or defence), and an
item used from the menu: what it does looked up in its figures (a tonic, one
by chance, the party's rest, a tab), the member window it is used on, a tab's
figure raised in the record, one more taken from the party's items each use.
`menu/item_screen_bridge.c` runs the steps on the machine, the restoring of
HP and MP (`$C2:8F55`), the cursor among the members a tonic is used on
(`$C2:B83E`, the techs screen's too), the description (`$C2:F2F3`), the
lists' frame, paging and slide, and the second cursor's and the scroll bar's
tasks still the original's. `menu/equipment_windows.c` is the two windows:
what a member has on (the card's too, which no longer calls out for it), the
slot taken and the slots' marks, the slots window sliding in with the member
rows moving aside (and back), the list window sliding in and out, the list of
what fits laid out and drawn; their slides keep their state in a
`MenuWindowSlide` or `MenuListSlide` the bridge holds between the frames it
waits, and stage the band table a frame at a time. `menu/lists.c` holds what
the two screens' lists share: whether a list scrolls, the way it slid, and
its rows moved in their buffer once it has (the equipment's `$C2:9FDE` now
one call of it). `menu/cursor.h` names the frame's buttons and pushes once
(`MenuButton`, `MenuPush`, `menu_asked`, `menu_pushed`), which the two screens'
three private copies became.

**The flags, settled first.** Every call-out was checked against the strict
solve before the logic: all of them read nothing but memory, Y (the list's
frame) or A (the description, the tasks' stop, the members' pointing, whose
sound reads A's high byte). What liveness can't declare is again the P a
routine saves as it starts, compared under its own calls out: the screens'
ticks, and here every routine a step reaches with a JSR that calls out
itself. Those are entries of their own run through `menu_run` (the
description of the cursor's item `$C2:AD2E` and its caller `$C2:AC59`, the
use of an item `$C2:B0AB` and its four handlers, the member window `$C2:AF42`,
a tab's member `$C2:B24A`/`$C2:B275`, a tab's raise `$C2:B2A7`, one more used
`$C2:B31C`, the windows' slides), so the original's frames stand under their
calls; before each, one leaves helper per decision (`leave_use_found`,
`leave_member_choice_started`, `leave_slide_frames`, ...) sets what the
original's last test left. The equipment screen's preview of an item's
figures (`$C2:A5C3`) would need its P after the list's move returns, which
that port's paths don't work out, so it stays the original's (TODO.md).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches, none refused (each entry's counts are its ports.tsv row): the
commands' step 336 calls, the list's 417, an item picked up 190, the cursor's
item described 599 (inside those steps), a slot taken 462 and the slots'
marks 468, a row of what fits 272, what a member has on 225. The library opens the
items screen twice (`menu` and `menus-field-menu`), sorts once, picks up,
swaps and uses a tonic, and goes through the equipment screen's windows
eight times; it never slides the list of items, shows who can equip an item,
or uses a tab, an item by chance or the party's rest. Fuzzed from two states
of `menus-field-menu` (the `items` state, frame 5150, and the `equipment`
state), seed 1, 2,000 states each, the menus' tables (`$7E:7700`-`$7FFF`) kept
with low RAM and the tilemaps, every routine up to its first wait for a
frame. The first runs found: the slide's end leaving a 16-bit accumulator and
the step's both bytes clear (the rows' move returns 16-bit); the list
window's slide filling 28 words of the rows' table, not 27; the original's
16-bit count of the scroll bar's hold and the cursor's hiding taken down
together as the slots window closes; the carry an item's use leaves its
handlers (the bit search's, not the handler's own) and the overflow a tonic's
strength test leaves; the drawing entries needing 16-bit index registers.
Random item tables made the original's strings run anywhere, which the kept
tables answer; the equipment screen's own entries now keep them too, and
their counts are unchanged against main's (`tools/fuzz_compare.py --before`:
0 of 326 routines differ).

**Timing.** The new entries were calibrated over the library (the menus'
rows only); the drawings charge their text at their own rates rather than
the text drawer's, which their few long strings fit better, and the
equipment screen's rows' move charges a block move for its copy. One
exception: the item list's ten rows (`menu_draw_item_rows`), whose time
follows which rows are empty, on the panel's five calls in vblank
(`tools/timing_exceptions.txt`).

## Phase 3, field and events: the party on the map

The first chunk of bank `$C0` in wave 2. PLAN.md's table of what event's
bridges still called in the original chose it: the field's state routines
behind `$B0` (`$C0:9E29`, `$C0:A26B`, `$C0:A2CE`, `$C0:5B8D`, `$C0:3154`) and
the tile copy behind `$E4` (`$C0:AF4E`), with the unported runs they lie in.
Reading them showed what they are: the leader leaving a trail and the members
walking it, the leader touching and pushing what it walks into, and the map
changing under a script. The chunk took those and what is the same concept
beside them: the player talking to an object (`$C0:5AC5`, from the field's
confirm button), the party's regrouping commands (`$D9` `$C0:326C`, `$DA`
`$C0:345A`), and a member sent back to the leader after a battle
(`$C0:5B1F`). `python3 tools/burndown.py` for `$C0`: 19.0 KiB ported, 0.7
inlined, 68% left and 249 entries before; 21.7 KiB, 1.4 inlined, 63% and 258
after -- 3.4 KiB, against the 8-12 a wave-2 chunk aims at; what fills it out by
concept is the leader's walk, the next chunk (below, and PLAN.md).

**The party as a module** (`field/party.h`, `field/party_trail.c`). Every
frame the leader moves it leaves a footprint on a ring of 128 in bank `$7F`:
how far it moved, how the pad walked it, how its sprite was layered, and the
ground under it (a climb, a moving floor). Each member walks the trail behind
the one ahead of it, a footprint a frame, keeping 24 footprints back while the
leader moves and closing to 16 when it stands, so the party bunches up and
strings out. What each shows follows the footprint: standing, walking or
running and facing its way, climbing, the leader running on the spot against
a wall, a pose while the player picks an answer, a fidget when the party has
stood about. The original's state routines were one function a gait and a
facing (sixteen of them, `$C0:A079`-`$C0:A15C`, reached through a table of
64 words at `$C0:9FF7`); they are one `Pose` -- a gait and a facing, or a
diagonal's two -- looked up from the two axes' gaits. The lookup keeps the
original's index arithmetic, one named function: it runs the vertical speed
and an eighth of the horizontal together into one byte, which muddles speeds
other than the pad's, and a member waiting on a moving floor looks up what the
floor's pull leaves of the leader's walk, which can be any. `$B0` and `$AF` are
one operation (`step_with_the_party`, `once` the short form's row), with no
call-out left; the entries were renamed for it (`event_cmd_walk_with_the_party`
and `_once`, `$C0:3002` and `$C0:2FFD`) and carry their predecessors' totals.

**The leader meeting objects** (`event/contact.c`). The field notes the object
the leader is up against (`field_leader_contact`): the one its walk ran into,
or the one before it when the confirm button is pressed. Talking and touching
are calls the field makes on the object's script, the same as objects make on
each other, so `event/calls.h` came out of `calls.c`: an object's function's
start, whether it takes a call now, and running a script from a place at a
level (`object_run_script_from`), which the calls, the talk, the touch, the
push and the regrouping all use. A pushable object (bit 1 of the byte `$84`
stores beside solidity) goes a tile the leader's way where that tile is open,
through the party's push script; the decision is one function
(`leaders_touch`) that both the touch and what its derivations work out call.

**The party's scripts and the regrouping** (`event/party.c`). Every map load
copies three runs of commands after the map's own script (`$C0:5CEC`): a
member walking up to the leader, a member walking to a tile and facing a way,
and an object walking to a tile. `$D9` and `$DA` hand them to the party at the
most urgent level, writing the tile and facing into the scripts' operands,
and wait on a count two of the map's variables keep (`FORMATION_COUNT`,
`GATHERING_COUNT`); then the members pick the trail up from the leader's newest
footprint (`party_trail_regroup`). A formation's place is decoded once, by its
row, into a `FormationPlace` (a tile, and a facing from the top bits of its
column and row): the language's rows now decode into records as well as
numbers (`EVENT_RECORD_PARAMETERS`, `FORMATION_PLACE(n)`), which is what
TODO.md asks of the location's decoding too.

**The tile copy** (`field/tile_copy.c`) copies a rectangle over another place
in the tables its description names: each layer's tiles, the tiles' properties
and their two bytes of attributes, or of the properties only the bit that says
which half of the tile set BG1's or BG2's tile comes from (read from the tile
builders at `$C0:762D` and `$C0:79EA`). It goes a tile at a time, top to
bottom and left to right, as the original's block moves do, so a copy onto
itself a little right or down repeats its first tiles. `$E4` is an operation
calling it; the main loop's copy (`$C0:1790`) reaches the same entry.

**What the map gained**: the trail (seven tables at `$7F:0C00`-`$7F:0F7F`)
and where each of the party is on it (`field_trail_position`, `$01AB`); the
walk's outputs on the layers' page (`field_leader_walk` `$1D2C`,
`field_leader_movement` `$1D32`, `field_leader_ground` `$1D34`); the idle
count the frame wait keeps (`field_idle_frames`, `$0158`, masked `$83FF` at
`$C0:EC60`); the leader's contact (`$01EB`); and `field_state_routine_hold`
renamed for what `$0138` is, the screen recentring on the leader before a
queued tile copy (`field_screen_recentring`). `field/field.h` names
`field_mode_flags`' bits (the party gathering, an object being pushed, a
dialog the field opened, which `event/dialog.c` had to itself) and
`dialog_choice_active`'s states. `field/object.h` gained an object's step
(`object_set_step`), its sprite priorities as a value to hand on
(`SpritePriorities`), whether it can be pushed, the party's animations beside
the movement ones, and `object_show_movement`, which was `animation.c`'s own.

**What shadow mode found.** The first library run mismatched on two things:
the gap between two places on the trail was worked out as a C remainder of a
negative difference (so a member whose place had wrapped past the one ahead
took the next footprint when it should have waited), and the regrouping's
hand-overs leave Y's low byte (they run with 8-bit index registers), which the
derivation had as the whole offset. After those, over the 26 movies every entry
matches, none is refused, and every other entry's totals are the
predecessor's. The one move that was a bridge is compared now:
`event_cmd_walk_with_the_party` 251,565 calls, 867 matched before (the rest
abandoned inside the engine's stretch), all matched after. The new entries:
`field_leader_steps` 116,079 calls, `field_first_member_follows` 83,032,
`field_second_member_follows` 51,587, `event_touch_contact` 1,871,
`event_cmd_take_formation` 716, `field_copy_tiles` 261,
`event_cmd_gather_party` 166, `event_talk_to_contact` 99,
`event_member_back_to_the_leader` 10; none interrupted. `$AF` has no call in
the library, nor does a push.

**What fuzzing found.** The original adds the leader's contact and the
party's objects into a place's offset (and a function's) as 16-bit words, so
the byte after each is read too: `$01EC`, and `$0198`, `$019A`, `$019C`
after the party's places. The game stores those objects with 16-bit index
registers and keeps the bytes 0, which random states don't: 14,926 of 20,000
talks mismatched until the map named them (`field_leader_contact_high`,
`field_party_objects_high`) and the guards required them 0. The trail's gap
showed on the members' own entries before the library did (344 of 20,000).
Then every entry fuzzes clean (PORTING.md's row), with variants that fix the
paths random state rarely reaches: a leader up against an object that takes
calls (touch and push), a member in its place with its role, the leader
standing with the player's control (fidgets) and during a choice, footprints
on plain ground, a formation and a gathering with members missing, and tile
copies small enough to overlap themselves. The predecessors of `$B0`, `$AF`
and `$E4` compare the same states with the same mismatches (none); the new
guards turn away a few more (a contact's high byte, a copy off the map).

**What was left at the edge, and why.** The trail's start at a map's setup
(`$C0:9DC3`, where the openings hand back) and the gathering after a battle
(`$C0:034B`) stay the original's: the first reads the ground under the leader
through the walk's own routines (`$C0:9AC8`, `$C0:9AD3`), and both go with
the walk. The inventory and membership commands (`$C7`-`$D7`) call the
battle bank's item routine and the menus' party routines, and would have been
bridges only. The objects' own loading (`$57`-`$83`, their sprites through
`$C0:448D`) is a concept of its own, objects coming onto the map.

**The timing gate.** The new rows were calibrated over the library, and two
event rows moved with them: `event_cmd_walk_with_the_party` 3,374 to 4,641 (its
time is now the party's step as C, where it was its bridge's own loop) and
`event_run_object_script` 4,596 to 4,723 (the engine runs `$B0` as C inside
its stretch). The tile copy's time follows the rectangle and the tables it
takes -- whole tables by the original's block move, a tile set's half a byte
at a time -- which one scalar fits only on average: `field_copy_tiles` and
`event_cmd_copy_tiles` are in `tools/timing_exceptions.txt` (at most 26,414
clocks in a call starting in vblank, writing the map's tables in RAM, which the
vblank check finds moves no video write).

## Phase 3, field and events: the leader's walk and the confirm button

The second chunk of bank `$C0`: what feeds the party's trail, and what the
player does with the leader besides walking it. `python3 tools/burndown.py`
for `$C0`: 21.7 KiB ported, 1.4 inlined, 63% left and 258 entries before;
25.9 KiB, 5.0 inlined, 56% and 262 after -- 4.2 KiB, counted with the
tool's fix below, which leaves the "before" as the first chunk reported it.
The concept's other half,
the screen's edges streaming the map in as it scrolls (`$C0:93E1`, the row and
column builders), is 5.5 KiB of its own and the next chunk (PLAN.md).

**The walk** (`field/walk.c`, `$C0:881E`, every field frame). The pad asks
for a walk or, with the dash held, a run, on top of what a moving floor pulls
(`$C0:88EE`, the eight directions `$C0:8925`-`$C0:8A6C`). The leader stands
in a box against the map, its feet at its position; walking one way, the
box's leading side is looked at at both corners (`$C0:8AB5`-`$C0:8E20`), and
walking aslant its leading corner and both sides (`$C0:8E21`-`$C0:9174`, four tables of which side
nudges which way), where a corner meeting another level nudges the leader
round it and a wall stops it. Before the map, the other objects: the first,
from the last slot down, standing close to where the leader is going
(`$C0:9923`) is what it is up against, and stops it if solid. The map has
two levels; each tile's shape (a slope from a table of 22, a half, a
quarter) says which parts of it are on which (`$C0:9AA1`-`$C0:9C36`), and the
leader walks on its level, onto stairs joining the two, over or under a
crossing. What it steps onto takes effect (`$C0:9C5C`): an exit, a step
event, a moving floor's pull for the next frame, a climb. It stays within the
map's edges (`$C0:9175`), and the screen follows it while it is past the
screen's middle the way it walks (`$C0:99DE`). The recentring (`$C0:885A`)
scrolls the screen back onto the 16-pixel grid a pixel a frame while a
script waits on it. Both still call the layers' scroll (`$C0:91AC`, a port)
and the screen's edges (`$C0:93E1`, the original's) on the layers' page.

**The party's start and its gathering** (`$C0:9DC3`, `$C0:034B`), which the
first chunk left at its edge: at a map's setup the trail starts afresh and
the leader takes the level and ground of its tile (the map's openings' entry,
`event/bridge.c`, runs it as C and hands back only where its guard fails);
after a battle the members walk back to the leader through the gathering
script (`event/party.c`, `party_back_together`), timed as
`event_member_back_to_the_leader`.

**The confirm button** (`event/contact.c`, `$C0:1AAC`; `event/chests.c`).
Where a dialog waits for the button, the press is the dialog's. Otherwise the
leader talks to the object it walked into this frame or, failing that, the
first object near enough before it (`$C0:1CFC` and its four ways: under 28
pixels along the way it faces, 12 across), and then opens the chest before
it. That search measures as the objects-in-the-way search does, the
original's borrow included, so `field/motion.c`'s measure became
`object_bearing` in `field/object.h` for both. A chest (`$C0:1DF4`-`$C0:1F23`)
is a tile the leader can't walk onto whose walkable byte names it; the
game's chests are one list in bank `$F5` (`field_chest_at`), and a chest's
number in it is its story flag, one bit each from the story memory's second
byte. Opening one raises the flag, asks the main loop to show it open, and
gives the party an item (nine bits, into variable 0) or gold (into
`field_chest_gold`) through the battle bank's inventory service
(`$C1:8003`, a call-out, which keeps the flags), then the field's own message
(`event/dialog.c`, `field_message_opens`); an opened chest's message says it
is empty, and a chest marked as holding nothing of its own gives nothing and
says nothing (its location's script does the rest).

**What the review of the first chunk asked, folded in.** A party place is
one type (`PartyPlace`, `object_party_place`); the leader's ground an enum
(`GroundKind`, the stored byte inside `LeaderGround`); one setter for a party
script's destination and the facing encoding in `event/party.c`; a tile
copy's rectangle as its size, the table addresses in the leaves; `Touch` the
outcome alone; named setters for `field_mode_flags`; the fidget flag decoded
in the idle count's storage. The contact the leader is up against is one
concept in `field/field.h` (`leader_contact`, `set_leader_contact`,
`leader_loses_contact`), which the walk, the search and the touch share.

**Chunk 2's review.** It rated `field/walk.c` a translation on one count:
the screen's check carried "what the original leaves A with" out of the
check, for a scaffolding re-run of it. The check returns whether the screen
follows now, and what the original's cells get, the check turned back up
included, is worked out from the state by the leaves beside the check
(`leave_screen_check`, `leave_check_down_turned_back`). With it: the tile's
shape is an enum in `field/layers.h` (`TileShape`), the level a `MapLevel`;
the pad reads `core/input.h`'s buttons; the recentring's step is named;
whether the player has the party is `party_is_the_players` and
`hand_the_party_to_the_player` (`field/field.h`); and the call-outs whose
site depended on a ternary are written out per case, so the checks read a
literal each. What it left for later is in TODO.md.

**What the game's buttons are.** The walk dashes on the fourth of the
player's assignments and the confirm button is the first, which settles what
the scripts' button conditions ask (`$30`/`$31`, `$3B`/`$3C`, renamed
`event_cmd_if_dash_held`, `_if_confirm_held`, `_if_pressed_dash`,
`_if_pressed_confirm`); `core/input.h` names them `ASSIGNED_DASH` and
`ASSIGNED_CONFIRM`.

**The searches' start.** The original doubles the object count in a byte, so
a count past 127 starts the searches from what is left of it: the endings'
and some battles' maps keep 143, 148 or 163 at `$7F:2000`. The three searches
(the walk's, the confirm button's and `field_object_in_the_way`) start at
`object_search_start`; the guards had required a count under 64, which turned
away 541 of the walk's calls in the library before the fix.

**The burndown's bridges.** `tools/burndown.py` read the call inside
`bridge_call`'s own body (`game/field/bridge.c`, and the battle's
`battle_call`) as an unreadable call-out of every port that uses a bridge, and
so credited those ports no callee at all. It reads the bridge at each
delegate's call now, as `tools/dontcare_check.py` does. On main it moves
`$C1` from 10.5 to 20.6 KiB (the battle's ports call out through
`battle_call`) and leaves `$C0` at 21.7; the walk, which calls out through
`bridge_call`, would have been credited 0.3 KiB of its 3.6 without it.

**What shadow mode found.** The recentring's state moved to "under way" before
its call-outs, where the original steps it after them (`WRAM[0138]` at the
call to `$C0:91AC`, in five movies). Then, over the 26 movies: every entry matches and none is interrupted:
`field_walk_the_leader` 194,763 calls (20 refused, above and TODO.md),
`field_press_confirm` 190,882, `field_recentre_the_screen` 174,
`event_party_back_together` 25, and `event_run_init_scripts` 243 with the
start as C; every other entry's totals are the first chunk's, the renamed
button conditions included. `ct_dev --native on --generated only --misses`
interprets nothing on `field`.

**What fuzzing found** (seed 1, 20,000 states each, `tools/fuzz_routines.toml`):
a chest's item is nine bits, not eight (9,829 of 20,000 mismatched on the
item variant, at variable 0's high byte and the inventory's copy of Y). Then
every new entry fuzzes clean (PORTING.md's row), with variants that fix
what random state rarely reaches: the leader walking into the map's middle
with the player's control, objects close enough to talk to (the window-only
kind included), and the leader facing a chest from the game's list that holds
an item, gold or nothing of its own, each with its flag random. Against the
predecessors (5,000 states each), every field and event entry keeps its
mismatches (none); the searches' start compares more states of the moves
that search (`event_cmd_follow_object` loose 282 where 132).

**The timing gate.** The walk's time follows the map: standing, the
original spends about 1,000 clocks, walking into it ten times that, working
each point out at length where the port reads a few bytes. One scalar fit
that badly (4.5% of its stretches off by 1,024 clocks or more, the worst
16,456), so the port charges each look at a point, and more for the ground
where the point is no wall, at the walk's own rate (`charge_as`, as `field/motion.c`'s
search does): calibrated, the walk is within the gate's limits with no
exception. The new rows were calibrated over the library, and three of the
subsystem's moved with them: `event_run_init_scripts` 4,878 to 4,961 (the
party's start now runs as C in its call), `event_touch_contact` 4,250 to
3,351 (the review's changes to the touch moved its accesses) and
`event_cmd_walk_with_the_party` 4,641 to 4,634.

## Phase 3, field and events: the map scrolling in

The third chunk of bank `$C0`, the other half of the camera: what the
layers' tile maps hold as the screen moves over a map larger than itself.
`python3 tools/burndown.py` for `$C0`: 25.9 KiB ported, 5.0 inlined, 56% left
and 262 entries before; 30.0 KiB, 8.6 inlined, 49% and 266 after -- 4.1 KiB.
With it, the walk's 20 refused calls.

**The map's looks** (`field/streaming.c`). A map tile is 16 pixels and a
layer's tile map entries 8, so each layer's tile of the map has a look: the
four entries of its quarters (`field_tile_looks`, BG1's and BG2's, 512 of
them, the map's tile set decompressed over the map's data once that is
unpacked; `field_bg3_tile_looks`, BG3's 256). BG1 and BG2 take theirs from
one half of the looks or the other as the tile's properties say (bits 0 and
1, which the tile copy already knew as the tile set halves). A line of
entries along a row or down a column is built from the map tiles it crosses,
within each layer's mask of where its map repeats (`field_map_index_mask`,
`field_layer_index_masks`), into the layer's staging buffer
(`field_layer_staging`), which the vblank's deferred uploads send
(`field/display.c`). The six builders (`$C0:7612`-`$C0:7F57`: each layer's
rows and columns) are one function. Where a line starts on an odd 8-pixel
tile they differ: BG1's and BG2's rows count its first half as one of the
line's tiles and end with a half of the tile after the last -- its second
half, where the first would continue the line, which lies past the screen's
right edge (TODO.md); BG3's rows and every column add the half on top.

**The layers' moves** (`$C0:93E1`, every frame after the layers' scroll).
Each layer takes its move a pixel at a time, right, left, up and down: its
scroll moves, and its place in its 16-pixel tile; each time it crosses an
8-pixel tile the screen's edges in 8-pixel tiles move with it and the row or
column it crossed onto is due (`field_layer_edges_crossed`, a bit per layer a
side). BG2 may go at half the pace (`field_bg2_half_scroll`, from the map's
flags): it counts a place in 32 pixels, scrolls every other one, and keeps
edges of its own (`field_bg2_screen_left`...). A layer drawn whole at the
map's load (`field_layer_drawn_whole`) has nothing due, and BG3's waves move
their phase with it down the screen. Then each side's due lines are built
(the row above the view first, then below, left and right), each layer's
place in its tile map moves on, the uploads are asked for, and the edge runs
are worked out (`$C0:7F9A`, timed as itself). The original writes the lines
a byte at a time through the work RAM port; the port writes the staging
through its accessors and leaves the port's cursor where the original's is
(`cpu_io.h`: the port is memory access, which docs/PLATFORM.md keeps out of
the platform services, so nothing was added there). A move is taken in
locals and stored once, and a map tile's look worked out once for its two
entries.

**The layers at a map's load** (`$C0:75E9`, `$C0:78EC`, `$C0:7CB5`): BG1 is
drawn as the screen stands, 32 rows of 16 map tiles into the staging from the
first buffer's start; BG2 and BG3 whole where they are drawn whole (the map's
first 16 by 16 tiles, then the tile map's other half: the next 16 columns of
a wider map, the next 16 rows of a taller one, which makes the tile map two
screens tall, or the same tiles again), and BG2 at half the pace from its
own edges. The loop the screen's drawing runs down keeps its count in the cell
the edge runs read as their wrap (`field_layer_edge_wrap`), and leaves it 0,
which the edges then read: a real write, not scaffolding.

**The walk's refused calls.** The original doubles the map's object count
into a byte to start the walk's search for objects in the way, so a count
of 64 to 127 starts it on the slots past the tables, where it reads the next
tables' bytes as a slot's: whether it is in view, disabled or solid, and
where it stands (the objects' vertical positions as its horizontal one, their
velocities as its vertical one). In the game's terms there is nothing there,
and the search looks at the map's objects from the last slot down; the
original's reads are one marked piece of scaffolding
(`what_the_original_meets_past_the_tables`, `field/walk.c`, through the
tables' unchecked accessors), which sets the outcome where one of those slots
reads as close. The library makes such a count on a frame or two in six
movies (112 and 123 at `$7F:2000`): 20 calls, all compared and matching now,
and on none of them does the walk look for objects at all -- a debug build
that reported every search past the tables, run over the nine movies with
such counts, reported none -- because the leader isn't stepping into
anything then, so the game can't observe the reads there. Where it could -- a step that met such a slot -- the original would
stop there, and the leader would be up against nothing (the slot's byte has
a link to none's top bit) and stopped where it reads as solid.

**What the map gained.** The looks, the staging, BG2's own edges, the other
layers' index masks, the half-pace and drawn-whole flags, the edges crossed;
`in_view` and `solid` got unchecked accessors for the walk's scaffolding. The
map's data (`field_map_data`) and the looks share their bytes at different
times, which the map's overlap check refused: `re/ram.toml` gained a `reuses`
key for a variable that takes over another's bytes (`tools/ram_map.py`).

**What shadow mode and fuzzing found.** Over the 26 movies every call of the
new entries matches: `field_stream_the_layers` 194,853 calls (1 interrupted),
the drawings 308, 296 and 106 (49, 37 and 7 interrupted: a whole screen's
drawing runs over a frame's NMI, the paths fuzzing covers); `field_walk_the_leader` 194,763 calls, all compared and matching,
none refused. Fuzzing (seed 1, `tools/fuzz_routines.toml`) found the
drawings' loops: the screen's rows counted from a top edge near 255 wrapped
(615 of 5,000 mismatched before counting the rows rather than comparing
them), and BG2's rows at half the pace between edges the other way round,
or to a bottom of 255, run past the staging and the bank (the original's for
ever), which its guard leaves to the original. Then, 20,000 states each:
the streaming 0 of 20,000 (and 0 of 20,000 with every layer moving up to 18
pixels each way, and with BG2 at half the pace and the layers drawn whole or
not), the drawings 0 of 20,000, 0 of 16,596 and 0 of 20,000; the walk with a
count of 64-127 0 of 10,159 (its predecessor's guard compared 1). Against
the predecessors every field and event entry keeps its mismatches (none).

**The timing gate.** The streaming's time is a steady part and a
part the lines and the moves set, which the port's reads and writes don't
follow: charged at one rate, frames that built rows were over by up to
50,000 clocks and frames with moves drifted with their pixels. The lines'
and the moves' own accesses are left out of the estimate
(`charge_uncounted_begin`) and each entry (rows more than columns) and each
pixel is charged at the streaming's own rate instead, and the edge runs are
timed as their own routine: calibrated, 0.16% of its calls are off by 1024
clocks or more, the worst by 1,714. The drawings charge their entries the
same way, so a whole layer's drawing and a screen's scale alike. All four
rows are calibrated, and none is an exception.

## Phase 3, field and events: the larger sprite layouts

The fourth chunk of bank `$C0`, and chunk 3's review. Of the three runs
PLAN.md named, the sprite layouts are the one the ports called out to most
(ten call sites from `field/sprites.c`, against three from the main loop's
ports and none into the frame builder). `python3 tools/burndown.py` for
`$C0`: 30.0 KiB ported, 8.6 inlined, 49% left and 266 entries before; 33.5,
8.7, 44% and 277 after -- 3.5 KiB.

**The larger layouts** (`field/sprites.c`, `place_larger`). An object's
sprites are 4, 8, 12 or 24 records; the 8-, 12- and 24-sprite layouts
(`$C0:BA65`, `$C0:BCDC`, `$C0:BFF2`, `$C0:C2BF`, `$C0:C73A` with its fours
`$C0:C6E7`) are placed as the 4-sprite one is: each four sprites' X at the
object's screen X and their ninth bits as one of the object's high bytes
(`object_set_sprites_high_bits`), then every sprite's Y; a rebuild takes the
offsets, and after the Y the looks, from the object's new frame. What
differs is which sprites show and which are hidden on the table's line. The
table keeps a sprite's Y as a byte, so one above the screen wraps round to
the table's last lines or, further off, onto the screen, and each of the
original's routines hides its own by where the object stands -- above the
screen's top, in its lower half or its upper half. The port works from the
sprite's signed top line and names each rule by what it shows (`ShowRule`:
"shown reaching down onto the screen", "hidden just below the screen",
"shown in the top half" and so on); each routine's three are a row of one
table (`ShowRules`), the 4-sprite layout's among them, each row's comment
saying what differs on the screen (the 8-sprite placing shows the sprites
below an object in the upper half wherever they land, where its rebuild
hides them past the middle). The 24-sprite layout is always placed, and an
object of it above the screen whose last sprite starts on the screen or less
than 256 lines above it isn't drawn that frame (the original answers
"nothing to draw" with its last sum's carry). That is the game's behaviour,
kept as plain logic, which returns only whether the object shows; the
overflow the original's code reads after a placing (its last sum's, or the
24-sprite layout's step past its records) is worked out from the state by
the entries (`placing_overflow`), which look before the update at what it
will do (`records_work`) to know whether it placed and whether it rebuilt.

**The frame's tile areas** (`claim_frame_areas`, `free_frame_areas`). The
frame builder draws an object's new frame into areas in bank `$7F` before
it goes to VRAM (`field_frame_staging`, four from `$7F:3800`, `$200` bytes
each, which chunk 4 first took for VRAM areas: the builder hands the address
to the WRAM port): one for the 4-sprite layout, two side by side for the 8,
three for the 12, which it claims for the object
(`field_frame_staging_claims`, `$C0:E952`, `$C0:E97A`, `$C0:E9AA`, recording
where the object's frame goes, `field_object_frame_staging`) and which are free
again once the records are rebuilt from the frame (`$C0:E9E2`, `$C0:E9FF`,
`$C0:EA1F`); a map's start frees them all (`$C0:E935`). The frees clear Y's
high byte (they take 8-bit index registers) and the update's callers read Y,
which the entries hand on.

**The draws are C.** With every layout's records its own, the sprite table's
building draws every object as C (it drew as C only where every object's
records needed nothing of the original's before), timed as the draw, the
update and the layouts' routines. The 4-sprite rebuild is C too.

**Chunk 3's review, folded in.** The walk's search stays typed in objects
(`in_the_party`, `stands_close` take a `FieldObject`), and what the original
meets past the tables overrides its outcome in one marked line,
`ORIGINAL_EFFECT(stops = stopped_past_the_tables(...))` (review rubric item
81). The current object's word is made current as a word plainly where the
map setup and the sprite table's building keep it so
(`object_make_current_recognised`, `object_recognise_current`), which the
effects check found read as a word by logic but cleared only as scaffolding;
its ten baseline lines are gone, and the contact's two carry their reason.
`field/field.h` names the field's geometry once -- the map tile, the entry,
the tile map's size, the screen's, the streamed lines' lengths -- and the
staging's lines (`field_staged_line`), which `layers.c`, `streaming.c`,
`display.c`, `walk.c` and `object_update.c` take; the tile looks' quarters
are fields in the map.

**What shadow mode and fuzzing found.** Over the 26 movies every call of
every entry matches: `field_place_8_sprites` 43,339, `field_place_12_sprites`
24,238 (54 interrupted), the rebuilds 554 and 569, the claims 37,507, 563 and
591, the frees 37,502 (8 interrupted), 554 and 569, the clearing 786; the
update, the draw and the table's building keep their calls, and the
building's interrupted calls rose from 336 to 1,472 of 193,483 (it runs as one
stretch now, which the frame's interrupt falls in more often; its fuzzing
stops at the budget on random rows, as before). Fuzzed (seed 1, 20,000
states, `tools/fuzz_routines.toml`): each layout's placing and rebuilding 0 of
5,000 on random positions and 0 of 20,000 with the object above the screen,
the claims 0 of 20,000 each, the frees and the clearing 0 of 5,000 and 20,000,
the update and the draw with the larger layouts 0 of 1,252; a deliberately
wrong rule (the 8-sprite placing's upper half as its rebuild's) mismatched 592
of 3,000, which is what the variants are for. Against the predecessors every
field and event entry keeps its mismatches (none).

**Chunk 4's review, folded in.** The placing's logic returns only whether
the object shows, and the entries work out the overflow and Y the original's
callers read from the state (review rubric items 82-84); the show rules are
named for what they show and are one table, the 4-sprite layout's row moved
out of the platform (`vid_oam_y` is gone; `VID_OAM_HIDDEN_Y` stays). The
frame's areas turned out to be staging in bank `$7F`, not VRAM (the builder
hands their address to the WRAM port), and are mapped so
(`field_frame_staging`); their owners are links to objects. The objects'
sprite high bytes are byte tables reached through one table of the three,
since they lie apart in memory. Over the 26 movies every call still matches;
fuzzed against chunk 4's build with the same seed, all 26 sprite entries and
variants keep their counts (0 mismatched), and seven rows' `cost_milli` were
recalibrated.

**The timing gate.** The new rows and the update, the draw, the building and
`event_run_init_scripts` were calibrated over the library; no exception.

## Phase 3, field and events: the frame builder

The fifth chunk of bank `$C0`: the frame builder, which the previous chunk's
tile areas are drawn by. `python3 tools/burndown.py` for `$C0`: 33.5 KiB
ported, 44% left and 277 entries before; 37.2, 37% and 278 after -- 3.7 KiB.

**What it is** (`field/frames.c`, `field_draw_next_frame` `$C0:CAD9` with
its layouts `$C0:CBDC`, `$C0:CEF5`, `$C0:D4F7` and their parts). When an
object's animation moves on to a frame its tiles don't show, the frame queue
(`$C0:CA76`, still interpreted: it stops at a scanline) has the builder draw
it: the next frame (the animation's, through the frame read, or the one a
script holds) is claimed tile areas in bank `$7F`, its tiles are copied there
from the object's graphics set as its description names them (straight or
mirrored, by the copies `field/graphics.c` already had), the areas are
queued to be sent to the object's tiles in VRAM, and the frame's sprites --
offsets, tiles, attributes -- go into the frame's data, which the records are
rebuilt from. The 8- and 12-sprite frames are drawn a part at a time, one
part each time the queue comes to the object, and the parts are counted in
the records' state byte, which is what holds the records' rebuild until the
frame is whole. The original's six near-copies (a layout, and for the
12-sprite layout whether its tiles start half way along a row of VRAM's, at
tile `$68`) are four rows of one table of plans: which of the description's
tiles each part draws where, the two stretches sent to VRAM, where the
sprites' tile numbers skip a row, and which sprites take the object's upper
half's priority (for the 4-sprite layout, by where each starts).

**What it taught the map.** `$0F01` was named for the battle; it is the frame
the object's tiles show (`field_object_frame_drawn`: coming into view and a
script's layering forget it, `object_forget_frame_drawn`, so the next frame is
drawn whatever it is; the battle keeps it for the party). The object's
graphics set and frame descriptions (`$1200`/`$1280`, `$1300`/`$1380`), its
tiles in VRAM (`$0D00`) and its palette (`$0F81`) are mapped, and so are the
builder's own cells: the queue's end (`$79`), the description's pointer and
the count on the field's page (whose `field_work` stops at `$C8`).

**What the callers read.** The frame queue reads the carry (more of the frame
to draw: the object stays at the queue's head) and hands the overflow on;
liveness keeps the overflow, so the entry works it out after the logic from
what the logic came to and the state (`build_overflow`: the frame read's,
the last copy's step along the areas, the second stretch's place, or the
tiles' skip of a row), with the overflow and the animation's frame from
before the call.

**What shadow mode and fuzzing found.** The first run mismatched a fifth of
the calls on `$D9`, the attributes' cell the original leaves (now
scaffolding); the guard then refused one real call, an object with no areas
yet ($FFFF) in the middle of its frame, which only a first part's claim
would give it: the guard asks only of the parts after the first that their
areas lie inside the bank. Fuzzing found graphics built in bank `$7F` whose
tiles the frame names run past the bank, where the copies read low RAM
through bank `$80` (the builder's own cells among it); the guard looks ahead
at the frame the builder will draw (`object_peek_next_frame`) and refuses
those. The look ahead first read the frame tables' bank before their offset,
which moved the frame read's open-bus byte; `field_read_animation_frame`'s
fuzzing caught it (604 of 4,089) and the bank is read last again.
Over the 26 movies every call matches: 231,605, none interrupted and none
refused. Fuzzed (seeds 1 and 2, 20,000 states each, `tools/fuzz_routines.toml`):
object 8 animating with the 4-, 8- and 12-sprite layouts 0 of 13,837 and 0 of
13,743, holding a frame 0 of 13,795 and 0 of 13,724, the 12-sprite layout
with its tiles at `$68` 0 of 13,766 and 0 of 13,624 (the rest turned away by
the guard); a deliberately wrong plan
(the split layout's last run a byte off) mismatched 1,749 of 3,465. Against
chunk 4's build every other field entry keeps its counts.

**The timing gate.** The new row was calibrated over the library; no
exception.

## Phase 3, field and events: the switches and the main loop's requests

The sixth chunk of bank `$C0`, and chunk 5's review.

**Chunk 5's review, folded in.**
- *The overflow past the frame queue* (the port-routine skill's third trap).
  Liveness keeps it live round the main loop, but the only readers it
  reaches are the menus' entry, which saves the flags (`$C2:8013`'s `PHP`)
  and puts them back on leaving; banks `$C0` and `$FD` have no branch on
  it. With no port call-out there to poison, it stays derived, from a
  status of game conditions (`FrameBuild`, field/parts.h) in field/leaves.c,
  outside the logic file.
- *The copies take their set and place.* `field_draw_frame_tile(g, set,
  tile, to)`; the work RAM port's final place and the cells only the
  original's code reads (the set, the place, the frame tables' bank) are
  scaffolding, and the copies' entries read them as their calling
  convention (baseline lines with that reason).
- *One frame read.* `object_take_next_frame` takes the tables' bank, which
  the `$C0:CB3A` wrapper reads; its guard refuses a bank where an address
  doesn't answer (`field_bank_always_answers`), so the read order no longer
  matters and the twin is gone. The mirrored copy's guard does the same for
  the tile's bytes, which fuzzing showed carry out of bank `$FF` into bank
  `$00`'s open bus (38 of 9,944 before the guard).
- The tile's constants live once in field/graphics.h; the 4-sprite
  priority rule and the `$68` plan say what they are; `object_holds_frame`;
  `sprite_offset` takes an `Axis`.

**What it is** (field/requests.c, field/switches.c). The main loop's
`$C0:0C76` moves the brightness and tint changes a step each frame, and
answers the requests. The port answers a step event itself: the step is
remembered on the map, and a floor switch stepped onto is pressed -- its
tiles move on to the next in the tile set, in one of three shapes (a tile
and the one above, two by two, two across by three down, `SwitchShapeTiles`,
where the original has five near-copies), its sound is played, and the
VRAM addresses of its tiles' entries in BG1's tile map are queued. The
line interrupt's job after the picture (`$C0:ED15`) draws the queued looks
(field_redraw_changed_tiles), a chest's as well, and refreshes the windows'
corners. The location change, the menus, a chest's opening, the tile copy
and the battle are handed to the original's code where it reads the
requests (`m_stop_at_own` at `$C0:0C8A` and `$C0:1770`). A chest's opening
went with the stop: at its sound the original has A's high byte and the
overflow from the path into it, which only the path gives, so porting it
means porting the path (TODO).

**What it taught the map.** The redraws' flags (`field_tiles_to_redraw`,
`$5F`), the switch's shape, the chest's kind, the two sounds, the entries'
VRAM addresses (`$09B2`, `$09CA`), the steps remembered (`$7F:1CC8`), and
the bytes after the screen's left and top edges (read as words).

**What shadow mode and fuzzing found.** A chest's opening mismatched once
(telepod-lavos): the sound driver's call came back through a path the
original reaches with other registers; the chest went to the stop. The
switch's sound reads A's high byte (the driver keeps it), which the port
works out from the switch's tiles (field/leaves.c). Over the 26 movies every call of the three matches: the requests 191,044,
the redraw after the picture 98,342, the frame builder 231,605; none
interrupted or refused. Fuzzed (seeds 1 and 2, 20,000 states each):
`field_handle_requests` 0 of 19,944 and 0 of 19,946 with any requests but
those it hands on at once, and a step onto each switch's bottom tiles 0 of
9,866 and 0 of 9,882 (the rest past the call-out budget); the redraw 0 of
20,000 each. A deliberately wrong shape (one of the two-by-two's tiles off
by one) mismatched 289 of 1,433 in the requests and 492 of 3,000 in the
redraw. `python3 tools/burndown.py` now credits `$C0` with 56.4 KiB and 5%
left, which is wrong the way its docstring warns: it walks
`field_handle_requests`' region through the branches the port hands to the
original at `$C0:0C8A` and `$C0:1770`, into the location change's reset to
the main loop and everything that reaches. What the chunk made C is the
step event's switches and the redraw, about 2.6 KiB.

**The timing gate.** The new rows, and the two chunk 5's review moved (the
mirrored copy, whose estimate no longer counts the port's writes through
the port, and the builder), were calibrated over the library; no
exception.

## Phase 3, field and events: the drawn frame put on the records

The seventh chunk of bank `$C0`, and chunk 6's review.

**Chunk 6's review, folded in.**
- *The switch sound's A-high byte* (the port-routine skill's third trap). Its
  reader is named: the sound driver saves A on the stack as it starts
  (`$C7:0147`'s `PHA`), so the byte stays in the stack below the caller's
  frame, which shadow mode compares. Liveness has the driver read it, so it
  can't be declared dead; the derivation stays in field/leaves.c and reads
  the map through switches.c's own decoding (`field_map_byte`), where it had
  read column 63 as BG1's (review rubric item 94).
- *Origin tags* (item 95): `redraw_quarter` takes no site; each branch tags
  its uploads' start with a literal.
- The guard refuses a one-by-two switch whose upper tile is the tile set's
  last, and the word-wide move is gone; `leave_entries` leaves its cells
  through `original_cell16`, and `$D9` is one named cell
  (`FIELD_LAST_WORKED_CELL`); the redraw after the picture is in display.c
  with `field_windows_need_refresh`; one `pressed_shape`; the chest kinds are
  named (`ChestKind`, by where each is in the tile set). The switch sound keeps a site a switch: the call's
  return is where the port stops if the call comes back otherwise.
- The mirrored copy takes a place in the build bank, like the straight
  copy, and its guard takes a port there alone, where the frame builders put
  it. The fuzzer can't set the port (its saved states have it in bank `$7E`,
  where the sprite table's building leaves it), so the entry's row compares
  nothing; the copy is fuzzed through `field_draw_next_frame`.
- *The burndown's overcount.* `tools/burndown.py` no longer walks on from a
  place a port hands back at, and a stop at a call's return counts as a
  hand-back where it declares what is dead there (a bare one is still a
  bridge giving up); `tests/test_burndown.py` covers both. `$C0` goes from
  5% left to 36%, and the total from 41% to 51%.

**What it is** (field/frames.c, `field_put_drawn_frame` `$C0:CB0A` with its
layouts `$C0:CDC8`, `$C0:D124`, `$C0:DD28`). The frame builder's twin, which
the field's return from a battle calls for each of the party and the enemies
it puts back ($C0:07EC, $C0:08A0, by `BRL` from their own code): it puts the
frame the object's tiles already show straight onto its records, built, and
draws nothing, since the tiles in VRAM are as the battle found them. It is
the builder's frame sprites (the same plans, the `$68` split's too) going to
the records instead of the frame's data (`sprites_set_record_sprite`). A
registry address inside a routine is a block boundary for the generated
code, so the entry takes the `BRL`s as calls; its return goes to its
callers' own callers, which read none of the registers.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (84, none interrupted or refused). Fuzzed (seeds 1 and 2, 20,000
states each): each layout 0 of 20,000, the 12-sprite layout at `$68` 0 of
20,000; the 4-sprite priority rule a line off mismatched 4 of 3,000.
`$C0` stands at 39.4 KiB ported, 34% left.

**The timing gate.** The new row, and the mirrored copy the review moved,
were calibrated over the library; no exception.

**Battle's findings, answered.** The long arithmetic's cells (`$28`-`$33`,
the 8-bit multiplies' operands and the multiplier's copies) are game state:
the original's callers multiply and divide what the last call left there, so
`game/battle/arith.c` writes them plainly and only the multiplier's and
divider's own state is scaffolding. So are the reach tests' product
(`battle_product`), the direction's cells, the fade tables' last intensities
and the tile decoder's row cells (`$53`-`$57`, which a screen effect's settings
read where its command doesn't set them), and the enemy at work
(`battle_enemy_turn`) as the choosing leaves it, written after its scaffolding
so that the plain write is the last. What is left in the baseline for battle
is the entries' operands, each line with its reason. `battle_run` now runs a
sibling as C only where the registry gives that sibling's entry for the state
(`native_hand_entry`, guard and preconditions included); otherwise it makes
the call as the original's, with nothing declared don't-care, and the runtime
runs whatever the registry has there. The library's gate passes unchanged,
with `battle_enemy_scripts_choose` recalibrated (8,036 to 7,902) for the
plain writes it makes now.

## Phase 3, field and events: the field put back after a battle

The eighth chunk of bank `$C0`, and chunk 7's review.

**Chunk 7's review, folded in.**
- The switch sound's A-high comment gives the real reason the byte is
  compared: the sound driver keeps its saved-register frame, A with it, on
  the stack while it waits on the sound CPU, so shadow mode compares it as
  the live stack it is there, and liveness counts `$C7:0147`'s `PHA` as a
  read (not "a byte below S", which PORTING.md says is not compared).
- `sound_sites` is indexed by the switch's shape and column with
  designated rows, and `switch_number` is gone.
- frames.c has one test for whether an object is drawn
  (`object_on_screen`), shared by the builder and the put-back; the
  24-sprite object is drawn, its frame is just not put back.
  `object_put_drawn_frame_back` returns nothing, and the header says what the
  after-battle put-back is.
- `$D9` has one name (`FIELD_SCRATCH_CELLS`, with `FIELD_LAST_WORKED_CELL`
  as the same cell by its use); the graphics port's guard asks whether the
  bank is `FIELD_BUILD_BANK`.
- `tests/test_burndown.py` asserts the code after a stop that declares a
  dead set isn't credited, that code reached around a stop is, and that a
  callee a port stops at is not inlined (the burndown now leaves those out).

**What it is** (field/battle_return.c, `field_return_from_battle` `$C0:0715`
with `$C0:076B`-`$C0:0904`). When a battle on the field ends, every enemy it
took over goes back to its tile on the map, the one the battle kept from the
screen's corner, into the rows the sprite table walks, showing the frame it
showed (field/frames.c's put-back, the `BRL`s at `$C0:086E` and `$C0:0902`);
an enemy the battle took away (its kept kind 0) is made unreachable and
hidden, giving up its palette if it has none of its own sprite. The party's
places follow (the first taken without looking, as the original does), the
sprite table is built at once (`$C0:B271`, a call-out), and where the party
was defeated each member holds the frame it shows. That last part is also an
entry of its own, `field_hold_party_if_defeated` `$C0:072E`, the return from
the sprite table's building, which is where the original goes on when the
guard sends the return to it, and what fuzzing compares apart from the call.
The battle's kept enemy objects (`$7E:01A3`) are named in the RAM map
(`field_battle_enemy_objects`).

**What shadow mode and fuzzing found.** Fuzzing found one guard: the
original makes each object current by its link's byte alone and the frame
put-back reads the whole word, so a byte after the link makes the put-back
see no object; the guard takes a return whose current object's word is clear
above the link (`object_making_current_recognises`), as every return in the
library has it. Shadow mode found the other: the original gives up a
taken-away enemy's palette from two subroutines down (`$C0:076B`, then
`$C0:07EC`, which saves the enemy's place and object over the call), and
shadow mode pairs a call by its stack pointer and compares that frame, so the
port pushes the two return addresses and the saved bytes around the call-out
(PORTING.md, "A callee's frame that is still on the stack"). The fuzzer
compares the machines when both have returned, not at each call, so only `late-omen-battles`, where a
battle takes away an enemy with no sprite of its own, showed it. Over the 26 movies every call matches
(`field_return_from_battle` 25, none interrupted or refused;
`field_hold_party_if_defeated` 25). Fuzzed (seeds 1 and 2, 20,000
states each): the return 0 of 20,000 each, compared up to the sprite table's
building (whose own call-outs can't finish on the fuzz state, TODO.md), with
the enemies taken away or kept, with a sprite or without; the defeat's end 0
of 20,000 each, won, lost or fled. The rows are sensitive: an enemy's kind
read the other way mismatched 2,993 of 3,000, its sprite test 1,357, and the
defeat test 1,915. `$C0` stands at 39.9 KiB ported, 33% left.

**The frame builder's overflow, through battle's V check.**
`tools/v_readers.py --after C0:CAD9` has V read after the builder returns,
so its derivation (field/leaves.c) stays: the sprite table's building
after the frame queue (`$C0:00D9`) leaves V as it is, the frame wait passes
it on, and the next frame's pause and menu opening (`$C0:18D9`) reach the
sound driver's saved flags (`$C7:0A39`'s `PHP`, taken back other than by
`PLP`) and the menus' `BVC` at `$C2:F7D9`.

**The timing gate.** The two rows were calibrated over the library; no
exception.

## Phase 3, field and events: the pause and the menus opened from the field

The ninth chunk of bank `$C0`, and chunk 8's review.

**Chunk 8's review, folded in.**
- *The palette give-up is a port* (field/palettes.c, `field_give_up_palette`
  `$C0:734C`). The field's eight sprite palette slots are shared by objects
  with the same colors, each slot's sharers a list in the order they took it
  (`field_object_palette_older`/`_newer` at `$0B00`/`$0B01`, the newest by
  slot at `$0B80`), and an object giving its slot up leaves the list; while a
  script has loaded palettes itself (`$0154` bit 7, event `$33`) objects keep
  out of the slots. The battle's return and the view update
  (field/object_view.c) run it as C, so the rebuilt JSR frame, the call-site
  arithmetic and the `Snes *` in `bring_back_enemy` are gone, and so is the
  view update's call-out. Fuzzed (seeds 1 and 2): 0 of 20,000 each; breaking
  either branch mismatched 159 and 1,623 of 3,000. Over the 26 movies every
  call matches (350, from the view update and the return).
- The return's work is a `GameState` function (`put_back_after_battle`);
  the entry keeps only the sprite table's bridge and the defeat's hold.
- *The `$016E` guard, named.* The frame put-back's layouts read the current
  object's whole word under 16-bit index registers (`$C0:CE57`, `$C0:D21C`,
  `$C0:DE91`, `$C0:E08C`), so a byte after the link makes them draw another
  object's tiles. The condition stays, with its readers named, and the
  put-back's own guard (`field_frames_can_put`) takes it too; it refuses no
  library call.
- One `BattleResult` (battle/battle.h), which battle/main_loop.c sets and the
  field reads; the party's places are `put_party_back`, with `PARTY_PLACES`
  from field/party.h and the leader's place left to the guard; the taken-away
  kind is named. The screen's corner stays in `field_own_tile` through its
  accessor: `original_cell8` reaches the field page's scratch run
  (`$C1`-`$E8`), and `$F0` is the direction routine's named tile.

**What it is** (field/menus.c). Once a frame the field's main loop hands
the buttons to `field_pause_and_menus` (`$C0:18D9`): Start pauses the field
while the player has the party and no script has locked the pause (the
picture at half its brightness, the play time clock stopped, until Start
again); the Start assignment (`field_press_start`, `$C0:1985`) moves an open
dialog window between the top of the screen and the bottom, or, with none
open and the story's progress at `$49` or past, opens the menus at their mode
5; X opens the main menu (mode 1); otherwise a waiting choice takes the
buttons. A menu opens only while the player has the party, no choice waits and
no script has locked the menus: the field fades out a step a frame, keeps the
party's places, and calls the menus; after them it sets itself up again,
brings back the characters of a party the menu changed
(`field_bring_the_party_back`, `$C0:1A03`: each character's object starts
again in its script, Frog's before Robo's as the original has them, and the
party goes back where it was kept), and fades in. The Start assignment's menu
also takes a menu a script asked for as shown. Most of the work is still the
original's routines, called out to from their own calls (the frame wait, the
menus, the field's setup and fade in); the two menus' call sequences are one
function over a table of their sites. The RAM map names the pause's and the
menus' locks (`$0110`, `$0111`), the brightness kept over the pause, the
characters' objects (`$8D`-`$93`), the party as last loaded, and the party's
places kept over a menu (`$7F:1D09`-`$7F:1D1D`).

A's high byte and the overflow are read after both entries, so the port
leaves A's high byte as the routines it called left it and declares only its
low byte dead; the Start assignment's subtraction of the story's progress
sets the overflow, which field/leaves.c works out (`field_story_test_overflow`:
`tools/v_readers.py --after C0:1985` has it read).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches: `field_pause_and_menus` 191,044, `field_press_start` 1,
`field_bring_the_party_back` 3, none interrupted or refused. Fuzzed (seeds 1
and 2, 20,000 states each), compared up to the first frame wait, the blanking
before a menu or a choice's call, since the pause and the menus run for
frames: the buttons 0 of 19,450 and 19,424 (550 and 576 past the call-out
budget, where the Start assignment's own calls run on), the Start assignment 0
of 20,000 each, the party put back 0 of 20,000 each, and the characters'
starts, compared up to the first, 0 of 20,000 each. The rows are sensitive: a
pause at a quarter brightness mismatched 342 of 2,922, the menus unlocked 126
and the story's overflow a step off 155 of 3,000, the trail left alone 2,945,
and the player's control ignored 163.

**The timing gate.** The three rows were calibrated over the library; no
exception.

## Phase 3, field and events: the field's start

The tenth chunk of bank `$C0`, and chunk 9's review.

**Chunk 9's review, folded in.**
- *Ports reached through the machine* (review rubric item 61). menus.c
  blanks the screen and takes the interrupts back as C
  (`display_blank_and_stop`, `field_take_the_vblank_interrupt`,
  `field_take_the_line_interrupt`, each timed as its routine), setting the
  CPU's interrupt mask and data bank the original's blanking leaves. Three
  ports stay machine calls on purpose, said beside their addresses: the
  line effects' builder, whose entry works out the registers it leaves (the
  overflow among them is read after the pause), and the Start assignment and
  the party's return, whose own call-outs must be made under their return
  address, where shadow mode pairs them. The file header says which is which.
  The menu path after the menus' return runs on the machine for frames, so
  the fuzz rows stop before it (now at the field's keeping, `$C0:011B`);
  shadow mode compares it (the blanking 549 calls and the handlers 335 over
  the library, the menus opened in `menus-field-menu`).
- *Shared vocabulary.* `WindowPosition` is field.h's, and the Start
  assignment swaps the window between the top and the bottom (its guard takes
  a window at one of the three places); `FIELD_SCRIPT_MENU_SHOWN` is
  `field_effect_status` bit 6 (event `$C8` waits on it, `$C0:3757`);
  `party_is_the_players`, `dialog_choice_waiting`, `field_pause_is_locked`,
  `field_menus_are_locked`; `FIELD_START_MENU_FROM_STORY` in field.h.
- The two openings are one `MenuOpener` and one table row each, with what
  differs between them (the menu, the X it is given, whether a script's
  menu counts as shown) in the row; `open_menu` says why N and Z are clear.
- The kept priorities are two byte fields; `battle_how_it_ended` types the
  result; the pause's display flags are named; the eight palette slots; the
  Frog-before-Robo order can't be seen in play (each start writes its own
  object and character entry); `object_palette_links_in_the_tables`.

**What it is** (field/start.c, `field_start` `$C0:0B86`). Before the field
sets up its first map, after the title, a load or another mode, its state is
put as a map's arrival expects it, in eight operations named by what they
leave: the location also put aside; nothing under way (requests, status,
locks, screen jobs, tile copies, transitions); a dark plain screen; no
dialog; the party the player's; no objects in the party's places or standing
for the characters; the sprite tables started from scratch
(`sprites_hide_all_next_frame`, and the sprite loading's four tables from the
words at `$E4:FFE0`, through field_data.h); the usual sounds; and the random
numbers from the play time. It calls nothing. The cells the map hadn't named
are named from their readers (a CPU-side watch over three movies for the
readers, then the listing): `dialog_speaker_high` (`$2F`, which `$C0:1FDD`
reads with the speaker as a word), `field_screen_job` (`$39`),
`field_built_sprite_object` (`$AE`), `field_sprite_data_tables`
(`$AF`-`$BA`), `field_map_has_no_bg3_set` (`$BB`), `field_battle_song`
(`$FA`, which the battle's setup plays through `$C0:1BAB`) and
`field_bg3_map_size` (`$7E:0BE9`).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (243, none interrupted or refused). Fuzzed (seeds 1 and 2) over
random states: 0 of 20,000 each; leaving one cell unwritten mismatched 997 of
1,000.

**The timing gate.** The row was calibrated over the library; no exception.

## Phase 3, field and events: the menus a script asks for

The eleventh chunk of bank `$C0`, and chunk 10's review.

**Chunk 10's review, folded in.**
- *Siblings through the machine for their guards* (rubric item 102).
  `field_run` and `field_run_long` (field/bridge.c) run a sibling port as C
  under the frame the original's call pushes, timed as its routine, where the
  registry would run that entry for the state (`native_hand_entry`: its guard
  and widths); elsewhere they make the call on the machine with the declared
  set, as `bridge_call` does. tools/dontcare_check.py reads them as it reads
  `bridge_call`. menus.c runs the line effects' builder, the Start assignment
  and the party's return through them; the second enum and its stated reasons
  are gone, and the header says what runs how.
- The two openings are a game table (the menu, whether its screen is given no
  argument, whether a script's menu counts as shown) and a parallel table of
  their sites (rubric item 100).
- field.h has one definition each of the line effects' slots (moved from
  line_effects.c) and the ones the field keeps on (`FIELD_LINE_EFFECTS_KEPT_ON`,
  built from them), no tint (`FIELD_NO_TINT`, from the platform's
  `VID_LINE_FIXED_COLOR_ALL_CHANNELS`, which event/screen.c and
  battle/screen_setup.c now take too), the starting color math (through the
  new `vid_math_control`, the encoder beside `vid_color_math_decode`), no
  window marked, and `MapLevel` with `FIRST_LEVEL` (moved from walk.c).
- `party_taken_as_loaded` (party.h), used by the start and the party's
  return; the start calls `count_as_on_the_move` and
  `screen_counts_as_recentred`; its switches are written as bools through
  named operations (`field_lock_pause`, `field_leader_meets_the_map`,
  `field_line_interrupt_works`, `field_map_names_a_bg3_set`); the backdrop is
  named; `field_sprite_data_table` returns the long address, with the RAM
  map's count; `display_go_dark` is core/display.c's one blanking helper, the
  entry's and menus.c's.

**What it is** (field/menus.c, `field_open_script_menu` `$C0:0CC7`, from the
main loop's requests). A script asks for a menu with event `$C8`, its byte
kept in `field_script_menu` (`$25`): the field fades out a step a frame
while the leader walks on without the player's control (the walk run as C
through `field_run`, the control kept on the stack where the original keeps
it, since the walk's own calls are paired by where the stack is), keeps its
places, and opens the menu: a byte of 0 goes on as the Start assignment's
menu does (`$C0:19C7`); $01-$7F opens mode 0 or 6 by its low bit; $80-$FF
opens the mode in its top two bits with the screen's argument in its low six.
After it the field is set up again, the script's wait is over
(`FIELD_SCRIPT_MENU_SHOWN`), and the port stops at the fade in, which the
original jumps to. The location change and a chest still go to the original
(TODO.md). The port now hands them back where the original takes each up (the
location change at its fade, `$C0:0C91`; a chest at the step event's test
before it, `$C0:0D3E`, with the requests in A as the test reads them), not at
the requests' first test, so the burndown credits the requests' dispatch and
the script's menus: a stop the port makes on some paths stops the walk on all
of them. The burndown also no longer counts `field_run`'s PC, the sibling it
runs, as an unreadable hand-back (`tests/test_burndown.py`). `$C0` stands at 40.8 KiB ported, 31% left.

**What shadow mode and fuzzing found.** Over the 26 movies every call of
`field_handle_requests` matches (191,044, none interrupted or refused); the
script's menus with an odd byte (mode 0) run in every movie, and with an
argument in eight (the fair's, the shop's, the new game's among them); no
movie asks for mode 6 or the Start assignment's menu, which fuzzing covers up
to the menus' call. Fuzzed
(seeds 1 and 2, 20,000 states each): up to the fade's first frame wait or the
keeping of the field 0 of 20,000 each, and, dark already, compared at the
menus' call 0 of 20,000 each; with the menus' Z left clear for mode 0,
736 of 3,000 mismatched; a location change handed back 0 of 20,000 each, and
the chest's hand-back without the requests in A mismatched 1,445 of 2,996. The chunk's other rows are unchanged.

**The timing gate.** `field_handle_requests` was calibrated again over the
library; no exception.

## Phase 3, field and events: the 24-sprite figure drawn whole

The twelfth chunk of bank `$C0`, and chunk 11's review.

**Chunk 11's review, folded in.**
- The script menu's fade keeps the player's control in a local and runs the
  walk through `field_run_holding` (field/bridge.c), which pads the call's
  frame with the byte the original holds on the stack and drops it after, so
  menus.c touches no stack. The reason, corrected: shadow mode compares the
  stack pointer and page at the walk's own call-outs and stops.
  tools/dontcare_check.py reads it as it reads `field_run`, and `run_entry`
  carries battle's comment on the guard's fallback.
- One `dim_a_step`, which the menus' fade (with the clock stopped) and the
  script's share.
- Comments: the menu button's tail is `$C0:195E`; `screen_counts_as_recentred`
  sits above `$C0:885A`'s comment and the recentring uses it; menus.c's
  header names the script's menus and the palette animation's jump at
  `$FD:FFF7`; TODO.md names the stops `$C0:0C91`, `$C0:0D3E`, `$C0:1770`.
- requests.c tests the script's menus before the chest; `table_of` takes a
  `FieldLineSlot`; the `LINE_INTERRUPT_OFF`, `NO_WINDOW_SELECTED` aliases and
  `ScriptMenu.the_start_menu` are gone. The menus' modes a script asks for
  stay numbered: the menus turn them into their own modes 14, 12, 13 and 11
  through `$C2:808F`, which the menus' side hasn't named either.
- The line effects' fuzzing's accessor-out-of-range predates the chunks: the
  same run on a build before chunk 11 reports it (TODO.md).

**What it is** (field/frames.c, `field_draw_whole_figure` `$C0:E12A`). An
object of the 24-sprite layout, a large figure a script loads, draws no frames
through the frame queue: when it is loaded (`$C0:47E9`) and when the field is
set up after a menu (`$C0:0212`), its one frame is drawn whole, its 96 tiles
from its graphics set (field/graphics.c's copies, straight or mirrored) into
the build place and sent at once to VRAM word `$0400` (a new site encoding,
`$C0:E19A`), and its 24 sprites' records laid out from its description's
offsets, eight sprites a row of tiles. It returns with 16-bit index registers
whatever it came with, the overflow clear and Y at the description's last
byte, which the callers read.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (1, in `endings-bucket-lavos`, with its graphics set built in bank $7F; none interrupted or refused). Fuzzed (seeds 1 and 2): 0 of
20,000 each with the set in the ROM, and with it built in bank $7F 0 of 1,296
and 1,372 (the guard turns away the random descriptions whose tiles run past
the bank or over the build place); the VRAM word, the sprites' attributes or their tiles' step off
by one mismatched 500 of 500 each. Fuzzing found the cell the builder's start
writes that this doesn't (the frame table's bank) and the Y the callers read;
shadow mode found the library's one figure refused, its set built in bank $7F,
which the guard now takes where the tiles are clear of the build place.

**The timing gate.** The row was calibrated over the library; no exception.
`$C0` stands at 41.8 KiB ported, 30% left.

## Phase 3, field and events: the main loop's requests answered

The thirteenth chunk of bank `$C0`, and chunk 12's review.

**Chunk 12's review, folded in.**
- The figure's place is a named view of the frame builder's areas and the
  bytes after them (`field_figure_tiles`, `$7F:3800`, `$C00` bytes, which
  re/ram.toml marks as reusing `field_frame_staging`); the figure and its
  guard's check against it read that name.
- The layout is derived: the VRAM word from the first tile and the words a
  tile, the tiles' steps from the builder's `NEXT_SPRITE_TILE` and two tile
  rows, the attributes from palette 1 and priority 2 (the figure ignores its
  object's palette), and the offsets through `sprite_offset` with a sprite
  count, as the frames' plans use it.
- `leave_figure` is built from the builder's leaves (`leave_drawing_from`,
  `leave_description_place`, `leave_run`); `field_set_tile_start`
  (graphics.h) is where a set's tile starts, for both guards and the copies;
  menus.c says why the control byte is put back raw (event `$E3` sets any
  value); the entry names the readers of its V and Y.

**What it is** (field/requests.c, `field_handle_requests` `$C0:0C76`). Every
request the main loop answers is now the port's, one kind a frame:
- *A location change* (`$C0:0C8F`): the picture fades a step a frame
  (`field_dims_a_step`, field.h, which the menus' fades share); dark, the
  party's place is kept as the one it came from (the location, the leader's
  tile, and the facing it will come back with, the opposite of its own), the
  next place becomes the party's, and the port stops where the field starts
  again on a fresh stack (`$C0:0CC0`), which is the original's to do. The
  guard takes the change only with a leader (the original reads its place
  with the link as a 16-bit index, past the tables for none: three calls in
  `field`, at the title's changes, go to the original).
- *A chest* (`$C0:16DC`): its sound from the original's call, the chest shown
  open (field/switches.c `field_open_chest`, with the switches' entries), and
  its overflow for the code after it (field/leaves.c). With a step event
  before it, its sound reads A's high byte as the step event leaves it: a
  pressed switch's last redraw entry's, else the remembering's
  (`field_step_event_high_a`).
- *A tile copy* (`$C0:1779`): the screen scrolls back onto the grid a frame at
  a time, the tiles are copied (`field_copy_tiles` through `field_run`), and
  the layers they changed drawn again on the layers' page
  (`field_run_on_page`, a new bridge that keeps the field's page on the stack
  under the call as the original's `PHD` does) and sent in the vblank, BG1,
  BG2 or BG3 alone from a table of their calls, or BG1 and BG2 over three
  frames; the port stops at the frame wait the original jumps to.
- *A battle* (`$C0:188C`): the layers scrolled into place, the effects built
  with the field's setup's values, the battle fought (`$C1:0000`, given A as
  the frame wait before it leaves it, which liveness has it read), the field's
  interrupts taken back as C, and the field's objects and turn taken up again.
The guard's switch test now covers every tile a switch presses (one two
columns wide, stepped into at a row's first column, reaches the row above's
last), which fuzzing found; a chest inside the map's rows.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (190,914 of 191,044 compared, the rest the guard's; none mismatched); 55 calls interrupted, the tile copies and battles whose
stretches span frames, which fuzzing reaches only up to their first long
call-out (TODO.md). Fuzzed (seeds 1 and 2, 20,000 states each): a location
change 0 of 17,473 and 17,497 (the rest the guard's leaderless states), a
chest 0 of 10,089 and 9,865, a step event and a chest together 0 of 9,899
and 10,060 (the rest run a sound past the call-out budget), a tile copy 0 of
20,000 each up to its frame work; the rows before unchanged. Breaking the
chest's entries' order mismatched 677 of 1,015, and the facing's flip 714 of
1,462. `$C0` stands at 44.8 KiB ported, 25% left.

**The timing gate.** `field_handle_requests` and `field_recentre_the_screen`
(now also run as C from the tile copy) were calibrated over the library; no
exception.

## Phase 3, field and events: the screen effects and the credits

The fourteenth chunk of bank `$C0`, and chunk 13's review folded in.

**How it was chosen.** The plan named the screen effects' steps (`$C0:220E`).
By concept that is the step the main loop runs every frame (`$C0:21E1` and its
sixteen handlers), the event command that starts and waits on the effects
(`$FF`, `$C0:3FAF`, and its sub-table at `$C0:400E`) and the searchlight's
setup (`$FE`); and beside them in the main loop, the layer work scripts leave
it (`$C0:274D`) with the commands that ask for it (`$29`, `$2A`, `$2B`, `$2C`,
`$2F`, `$32`): BG2's halves cleared and the endings' credits written into BG3.
That is the whole of the concept, 2.6 KiB, short of the plan's 8-12: the rest
of `$C0`'s runs are other concepts (the characters' objects, the items and the
party, the field's setup), and the fade in (`$C0:2824`) is the field's setup's
caller more than an effect.

**What it is.** `field/screen_effects.c`: an effect is a window's shape drawn
into the per-line table the line effects aren't reading, and a cut (which
layers the windows hide on which screen, where the colour blending applies),
as data: a hole in BG1 and BG2, an oval of blending, both opening to a radius
and closing; the searchlight's four moving corners; the curtain, window 2's
ragged edge swept across by bank `$C3`'s routine (a call-out); BG3 in a circle
at the top of the screen, or blended round it, drawn into both table sets over
two frames; the ripple, a waving copy of BG1 through a circle; and the field's
close into scene `$85`. The circles are `scene/circle.c`'s and the shape
`field/window_shape.c`'s, run as C (`TIMED_AS` their entries).
`field/script_layers.c`: the credits laid out a line a frame in BG3's staged
row from the texts at `$FD:A000` (`field_data.h`) and sent by the vblank's BG3
piece; BG2's halves asked of the vblank and the staging blanked (a call-out);
and the message the field opened for a chest closed once the chest is off the
screen. Event `$FF` (`event/screen.c`) is a bridge: below `$90` the field fades
out a step a frame and goes dark, and the port stops where the original starts
the scene on a fresh stack (`$C0:3FD6`); from `$90` a C table of the sixteen
screen commands (start an effect, open a circle at a place, a curtain or the
ripple waited on, the push ended), with the field's restart (`$96`) handed to
the original at the command's dispatch and the menus' party routine 8 (`$9F`)
called through the dispatch's frame. `$FE` decodes its sixteen operands into a
record of the language (`SearchlightMove`); its leaves (the divider, the
working cells, the overflow of the last distance) are `event/leaves.c`'s.

**Chunk 13's review, folded in.** The tile copy's single-layer table is typed
(`FieldLayer`, designated rows, the move as a `FieldMapMove`, BG3's own frame
wait a column), and the queued maps no longer double as a transition; the map
transitions' moves and sends are `field.h`'s (`nmi.c` used private copies);
`field_is_dark`, one map-rows constant (`FIELD_MAP_ROWS`), the figure's size
from the map, `field_step_kept_at` asked rather than the search repeated, the
chest's kind forgotten in `ORIGINAL_EFFECT`, the party's keeping and its move
split, the header's request order, `switch_tile_row` without a negative shift.
The review's call to drop `field_frame_wait_leaves_a` was wrong and is
answered in its comment: the frame wait leaves A the idle count's high byte,
and where A held that before the wait shadow mode keeps the port's own A
across the call, so without the line the battle's call mismatched (9 calls in
`endings-bucket-lavos`). Removing the requests' `== 0` test the review called
dead put five reads of the requests in every frame's estimate (+85% drift);
it stays, as the test that skips the answers.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches: the step 190,882, the layer work 190,882, `$FF` 56 (32 inside the
event engine's stretch), `$FE` 74, the credits' commands 36-38 each; no call
interrupted, none refused. The curtain, the ripple, the field's close and the
BG3 circles no movie reaches. Shadow mode found the scene's stop leaving the
carry (the fade's last comparison clears it, now declared dead) and the
credits line leaving its column in the scratch cell. Fuzzing (seeds 1 and 2)
found the curtains' order wrong: `$93` uncovers leftward and `$94` covers
rightward (57 of 10,154), and `$29` reading its operand before the letters'
unpacking, which reaches the script's bank (79 of 2,525); fixed, every entry
is 0 mismatched: the step 0 of 19,996 and 19,993 (one state of seed 1 differs
in OAM bytes only, TODO.md), the curtain 0 of 5,000 each, the layer work 0 of
20,000 each, `$FF` 0 of 10,154 and 9,999, the scene 0 of 9,986 and 10,054,
`$FE` 0 of 9,943 and 10,091, `$29` 0 of 2,525 and 2,499, the rest 0 of
9,987-10,133. `$C0` stands at 46.5 KiB ported, 22% left.

**The timing gate.** The new rows and `event_run_init_scripts` (whose
openings now run `$FF`'s and the credits' rows) were calibrated over the
library. `field_move_screen_effect_on` carries `field_rasterize_quad`'s three
exceptions: the searchlight's shape is timed as that routine and is off by
what it is off by.

## Phase 3, field and events: who the objects are, their colours, and the party's belongings

The fifteenth chunk of bank `$C0`: the event language's last commands, so
every opcode a script uses now has a row (the ROM's handler table is only the
fallback for a state a row's needs turn away), and the chest's gift as C.

**How it was chosen.** The plan named the commands at `$C0:4186` (the
characters' objects) and `$C0:3711` (items, gold and the party). By concept
that is everything the language had left: an object's casting -- `$57`-`$6D`
and `$80` (a playable character, in its party place or set aside), `$81` (one
apart from the party), `$82` (one of the people and creatures), `$83` (an
enemy), with the shared-graphics search `$C0:5C90` -- its colours (`$33`, a
palette slot of its own; `$88` and `$2E`, effects on its colours and the
screen's), the inventory and gold (`$C7`, `$C9`-`$CE`), the menu a script
opens (`$C8`), and the party's record (`$CF`-`$D4`, `$D7`). About 3.2 KiB,
short of the plan's 8-12 like chunk 14: what `$C0` has left besides is the
field's setups and its keeping (`$C0:0000`, `$01A5`, `$038F`, `$011B`, the
fade in `$2824`), another concept.

**What it is.** `field/cast.c` casts an object: its role and character
(`field/object.h` gained the operations: a member in a place, set aside, one of
the other kinds, the enemies' two), a sprite record put on it (MD2's
`field_sprite_records` and `battle_sprite_records`, `field_data.h`: graphics
set, assembly, palette, animations, layout, through the four tables
`field_sprite_data_tables` points at), its graphics -- a character's from the
ROM, the others' unpacked into bank `$7F` after the sets already there or
shared with an object that has the same set -- and its block of sprite tiles
in VRAM (`field_sprite_block_owner`). A party member stands where the
location says, or on the leader; a member put on the map right after a
character joins shows its first frame at once (`field_character_joining`,
the meaning `field_animation_queue_front` had half of). `event/characters.c`
has the rows: `$57`-`$81` operations, `$82`/`$83` bridges whose unpacking
(`$C3:0002`, a machine-shaped port) is a call-out between their halves of C.
`event/colours.c`: `$33` (`field/palettes.c`, `object_take_palette_slot`),
and `$88`/`$2E` decoded into a `ColourEffect` record by their form: a
rotation stopped (the object's colours loaded again) or started, a fade, or
colours of the script's written over the palette; with the first eight
animation slots busy the effect is dropped and the object's turn ends there
(`end_turn_after`, a flow the language didn't have). `event/items.c`: the
inventory's and the gold's commands call `battle/inventory.c` as C, timed as
its routines; `$C8` asks the main loop for a menu and waits. `event/party.c`:
the party's record is the menus' (nine places, the active three and the
reserve), so `$CF`-`$D4` and `$D7` are bridges to `$C2:8004`, now named by
what each routine does (read from `$C2:8C75`-`$C2:8E1E`): whether a character
is active or recruited, bring one in, send one to the reserve, recruit,
dismiss, count an item. Bringing one in makes its object the next member's;
sending one away has the members behind close up, roles, places and palettes
(slots 5-7), with the original's two quirks kept (an empty first place closes
up whatever it held; a second member sent away is only marked out). The
chest's gift (`event/contact.c`) gives the item or the gold as C now, and its
guard leaves an item past 255 to the original.

**Third trap, settled first.** The call-outs kept are the unpacker (it saves
and restores every register: `DC_A | DC_X | DC_N | DC_Z | DC_C`, Y passed
through to the engine, which leaves it to the menus' dispatcher) and the
menus' routines, which the existing bridges already called. Shadow mode found
one register those read that liveness can't drop: `$D4`'s carry at the call,
which the dispatcher's `PLP` hands back as whether the object's turn goes on.
It is the carry of the last character comparison the original makes, worked
out as `$D6`'s already was (`goes_on_after_sending`), not threaded.

**What shadow mode and fuzzing found.** Over the 26 movies every compared call
matches: the engine's turns and openings with the new rows inline, and the
entries the original's own loops reach (the casts 81-657 calls each, the
colours 89-640, the menu 131, the record's 1-213, the chest 190,882). Shadow
mode found the enemy's Y (the original works the block out with 8-bit index
registers, which clear Y's high byte) and `$D4`'s carry. Fuzzing (seeds 1 and
2, 20,000 states each, `tools/fuzz_routines.toml`) found the leader read
through a word whose high byte the game keeps clear (now the guard's) and the
enemy's scratch cell's high byte; every entry is then 0 mismatched on both
seeds but the loose casts, `$82` 1 and 2 of ~8,600 and `$83` 8 and 8 of
~6,700: random unpacking places that carry a set past bank `$7F`'s end into
low RAM, where the original reads its cells again (an accepted divergence,
written at `event_cmd_cast_enemy`; with the place in the bank's upper half the
loose `$83` is 0 of 6,792). `$C7` and `$CB`, `$CD`, `$D7` no movie reaches.
`$C0` stands at 49.6 KiB ported, 16% left.

**The timing gate.** The new rows were calibrated over the library, with the
engine's two entries (whose turns now run the rows inline); no exception.

## Phase 3, battle chunk 2: an effect worked out (`$C1:D132`-`$C1:F1EF`)

An enemy's turn is C to its end now, but for the original's code it calls out
to (the tech's targeting, messages, bringing enemies in and the showing): the
attack's and the tech's effect is worked out in C (`game/battle/effect_work.c`,
`$C1:D7C4`, `$C1:D8D1`), and the notes of the action for its targets
(`$FD:AAD2`, `$FD:AB01`) are C inline in the turn. The work is in four files:

- `effect_work.c`: going through the targets, the effect's record decoded
  (`EffectRecord`, `effect_data.h`) and its kind run by a switch, each case
  timed as its own routine (`$C1:D9D7`'s kinds with work, each a registered
  entry for the party's code, which still runs them from `$C1:D523`); what
  acted on each target noted (`$C1:D508`), the hits' numbers added up
  (`$C1:EB68`), and the entries with their guards;
- `effect_amounts.c`: healing (`$C1:DA37`), draining (`$C1:E77B`), a hit's
  tests of landing (`$C1:DE87`, `$C1:DE9E`, `$C1:DEF0`, `$C1:DC64`), its
  damage by the eight formulas the dispatch reaches (`$C1:E0F5`; its formulas
  9 and 10 are behind a test that sends every kind but 8 to the weapon's), the
  defence (`$C1:E65F`), and the damage applied (`$C1:DCAD`), with the damage
  table and the numbers shown recorded (`$C1:EBF8`, `$C1:E89F`, `$C1:E8E2`);
- `effect_statuses.c`: curing, inflicting and the status timers (`$C1:DB5E`,
  `$C1:DBAA`, `$C1:E006`, `$C1:E976`);
- `effect_modifiers.c`: the modifier record decoded into its routine and what
  its two numbers mean for it (a percent, a stat, the families singled out,
  ...), applied by a switch (`$C1:FA61`'s routines, `$C1:EB1C`, `$C1:EA6F`).

The statuses' bits and the stat block's offsets the work reads are named once,
in `battle/combatant.h`.

**What stays the original's.** Stealing is the party's alone (no enemy's
action steals): its item goes to the party's inventory or its battle records
(`$FD:B655`), which is the party's chunk, so the steal kind isn't registered
and the entries turn away a hit that steals, and the rare item's chance with
it. The clock modifier (`$C1:EE27`) reads the controller mid-work, a read that
waits on the console (the port leaves that wait to the original, as
`core/input.c` does), and no enemy's action has it: the entries turn it away.
So does a modifier routine past the table's 21, a kind past `$2C` (none in the
ROM), and a drain where the byte the original reads its stat's offset's high
byte from (`$1B`, which nothing in the effect code sets and the game leaves
clear) isn't clear. With that, the work makes no call to the original's code.

**The arithmetic.** The work multiplies and divides plainly (`effect_times`,
`effect_over`), with no carry in or out. The long arithmetic's cells are game
state (`arith.h`): a division divides the high word the last call left with
the number it is given, which is how a product just made is divided whole,
and the random draws divide by that high word too. The carry the original
comes into a call with matters to the game in one place, and the port names
it: the evasion's level bonus (`level_bonus` in `effect_amounts.c`) divides
twice with the carry set, so the second division divides a number of over two
billion and the bonus is its quotient's low byte, not the levels over twenty
(`battle_divide_leftover_marked`). Two more leftovers move a result and are
kept with a comment: the defence's quarter steps multiply with the high byte
of the last number the arithmetic worked on, and the element's scale of 4 is
set a byte at a time over the high byte the hit's division by 256 left, so it
is 260 as a rule. The shares a formula's random part is drawn up to are
multiplies (77/256, 205/1024), but for the tenth of a word, which the
original's shifts round part by part.

**What the machine still wants, as scaffolding.** A multiply leaves the carry
its caller came in with in its second number's cell, which only the
original's code reads (a party caller divides by it, which the effects
check's runs found): the multiply leaves 0 there, its entry the machine's
carry, and the few callers in the work whose carry is set and whose multiply
is the last before the work returns say so in `ORIGINAL_EFFECT`
(`battle_leave_multiply_carry`). The kinds hand the party's code back the
carry of their last note (set), A's high byte clear and Y past the note's
bytes: the entry wrapper sets them. The hits' numbers added up leave the carry
of the compare that chose the last kind to move, worked out once after the
adding from the two records' kinds (`leave_last_compare`), and a target after
the first is taken with the carry the note before it leaves
(`take_next_target`).

**The overflow flag, declared dead.** The original's adds leave the processor's
overflow flag as their last result had it, and the effect worked out for an
enemy hands it on to the action's showing (`$C1:BFA4`), the battle's step
(`$C1:0003`) and what follows the action (`$C1:B762`), which save it with the
flags across their frame waits. Nothing tests it: there is no `BVS` or `BVC`
in the battle's banks (the menus' are the ROM's only ones), and the one `PHP`
the code after reaches whose byte isn't put back by `PLP` (the sound driver's,
`$C7:0A39`) throws it away. The liveness analysis agrees where it can follow
the code, and the work-outs declare V don't-care (`DC_V`) on that; but the
strict solve the call-outs are held to gives up at the jump tables the
animation scripts (`$C1:75D1`, `$C1:7633`) and the enemies' scripts
(`$C1:874E`, `$C1:B006`) dispatch through, whose targets come from data, and a
fuzz run can't settle it (the showing waits on frames past any budget). Two
new pieces of evidence do. `tools/v_readers.py` solves liveness as strictly,
but follows each of those four tables to every word its dispatch can index:
each is `ASL A` on an 8-bit A, `TAX` and the dispatch, with nothing jumping
past the `ASL`, so where A's high byte is clear X is at most `$FE`, 128 words,
each code the analysis traced. The bound is an `index_reach` hint on each
table in `re/hints.toml`, and the tool checks every part of it, A's high byte
too: `tools/disasm/high_byte.py` follows it through every path and call the
analysis traced, and has it clear at all four `ASL`s, resting only on D's
high byte being zero at the `TDC`s that clear it (the battle's direct page is
`$0000`). `tests/test_v_readers.py` holds the hints to the ROM. The two
condition dispatches after the first (`$C1:8769`, `$C1:B021`) carry no bound:
44 of the routines the first's words reach return with A's high byte unknown,
and V is dead into the three callees without them. `ct_dev --index-check`
records X at the four dispatches over the library on the interpreter alone:
never past `$FE` (135,810 runs, the largest X `$7E`). With the bound, V is dead
into all three callees. `ct_dev --poison-site` inverts V at a call-out's own
site in the shipping mode; the library with it at the three declarations'
sites traces the same as without in every movie, the picture included, but
for the stack's page, where the inverted flag the callees push lies after they
pull it. Per site: 441 calls into the showing, 6,440 into what follows the
action, and of the step's six sites 285 from the menu's (`$C1:856D`) and 4 to
32 from the other five, which rest on the static result alone. The three
call-outs declare V don't-care with a `dc-evidence:` note naming both and the
per-site counts, which `tools/dontcare_check.py` reads and re-checks
(PORTING.md, "Declarations liveness can't prove"); `tools/dc_evidence.py`
makes the runs again. The work computes no overflow.

**What it found.** The drain reads its stat's offset as a word from the cell
its kind is handed in; a modifier that fells its target outright marks it by
battle slot from `$AEB0`, so a party member's mark lands in two bytes before
the enemies' (`battle_beaten_marks_low`) or on "any enemy beaten"; about a
tenth (`$C1:C1B2`) is three subtractions, not four; formula 8 works nothing
out and takes whatever the quotient cell holds; a number's place is a byte,
so from the fourth hit of an effect that hits repeatedly a set of numbers
reaches past the records into the effect's own flags, as the original's does;
and once one of those hits lands, the rest are made sure by setting the
effect's flags to "can't miss" alone, so they lose the status chance and the
barrier and shield passes the first had.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the enemies' work-outs 347 techs and 23 attacks (175 and 9 compared whole,
the rest interrupted), and through the party's code the kinds 765 hits, 86
items' restorations, 60 restorations, 39 statuses, 26 drains, and a few cures
(229, 48, 52, 35, 23 compared; 32 hits turned away, the party's steals and
clock). The repeated hits, the item's status, the lost condition bit and the
showing of the actor's HP aren't reached by any movie.

**Fuzzing.** Every new entry, seed 1, 5,000 states each on `gato-battle`
(`tools/fuzz_routines.toml`: an actor and one to eleven listed combatants at
a set of numbers and a hit the showing has, the modifiers among those the port
has, the actions from the ROM's records, `$1B` clear), none mismatching; all
5,000 compared but the hits (2,508) and the repeated hits (2,546, one to three
of them), whose other states are steals turned away. The enemies' turns around
them (`fuzz_compare.py --subsystem battle`, 356 routines) none mismatching.
Fuzzing found what shadow mode couldn't: the drain's word read, the beaten
mark, the tenth's subtractions, the record decoded by the kind the entry runs
(not its first byte), the defence's quarters taken from the flags as the work
goes (not the record's), and a set of numbers from the fourth repeated hit on
reaching the effect's flags.

**Timing.** Calibrated from the shadow runs, battle's rows only (the kinds
22,770-45,350, the work-outs 14,758 and 16,958, the notes 21,091,
`battle_multiply_16x16` 29,285, the enemy's attack's turn 8,519); every entry
within the limits with no exception added. `tools/timing_gate.py` takes an
entry some report measured no stretch of (all interrupted) without failing.

**The routes.** The new timing moved the `bucket_lavos` route's core fight onto
a turn where the bot's A in a tech's target cursor didn't take, and its backing
out pressed B four times back to back, which the game reads as one press: it
left the tech list open and waited on it for good (the game takes the tech
from the same state, in the original's code too). The bot now releases between
presses and waits for each to move the menu (`tools/movies/bot.py`), and every
route reaches its outcome in the shipping mode.

## Phase 3, battle chunk 3: the statuses' timers (`$C1:8876`-`$C1:8C08`)

The third battle chunk was picked from what the enemy turn and the main loop
still called out to for being whole on its own: the twelve handlers the main
loop runs for a status's timer that has run out (the original's table at
`$C1:B92D`), 914 bytes, whose callees were ports already (the damage and
number records, the long division, the numbers' clearing) but for the
action's showing and the effect's start. The party's working out
(`$C1:D523`) was the other candidate the coordinator named; it brings the
steal's item and the clock modifier's controller read with it, each a step
of its own, and waits for chunk 4.

`game/battle/status_timers.c` is the handlers as one module: a C table says
what each kind of timer does when it runs out -- wears its status off on the
tenth time (stop, haste, evading much better, the shield, the barrier; the
slow's timer, which wears off without taking the slow away, as the
original's does), poison hurts, a point of HP is lost, MP comes back on the
tenth time, or nothing (three kinds) -- and which status bit it wears off.
Each run the timer starts again, at its length, or paced like a turn (half
as long hasted, twice slowed; the MP's timer asks only about kept haste: the
original loads the status byte and drops it). What a status does is recorded
where the action's showing finds it (`effect_show_status_damage`,
`effect_show_magic_regained`, `effect_record_status_damage`, in
`effect_amounts.c`, as a hit's is), and the handler's wrapper calls the
showing (`$C1:AC57`, a sibling, with the frame of `$C1:895B`, which calls it,
on the stack) or the effect's start (`$C1:EC7F`, the original's). The search
(`battle/timers.c`) runs them as siblings, as the loop's `JSR` calls them;
the combatant a handler works on is the one before where the kind's search
goes on in the order (`battle_timer_found_run_out`), and seeing to it clears
the kind's due bit (`battle_timer_seen_to`). New in the map: the runs left
before a status wears off (`battle_timer_runs_left`, `$B045`), and two
status bits named for what the battle does with them (the first byte's `$10`,
a point of HP each run; the fourth's `$20`, 5 MP every tenth), which also
names the bits `start_timer_for` (`effect_statuses.c`) tested by number.

Two leftovers are kept, as the game plays them: poison divides the long
arithmetic's leftover high word with the most HP, by a divisor whose high
byte is what the last long arithmetic left in its cell (the original sets its
low byte alone, `battle_wide_math_second_high`). And two things the original
leaves are scaffolding: the stat block's place in `$10` (the handlers that
act), and the carry the damage is recorded with, which the multiply leaves in
its cell (a shown number's place's add, or the point of HP's pacing shift;
finding the stat block's place clears it otherwise).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the barrier's 214 calls (210 compared), the shield's 168 (166), the haste's
112 (111), poison's 62 (51, the rest compared in the search that ran them),
the point of HP's 165 (all compared in the search), the stop's 12. Shadow
mode first found the `$10` the handlers leave. The evading much better's
handler is called four times (`gato-battle`, `endings-telepod-lavos`) for no
combatant: the search, reading past the order's last filled place, takes the
empty place's `$FF` as a slot whose timer (past the kind's row, in the
lengths' table) has run out, and the original's handler then writes past
every table it touches. The guard turns those away to the original
(docs/TODO.md). No movie reaches the slow's, the MP's or the three that do
nothing. **Fuzzing**: every handler, seed 1, 5,000 states on `gato-battle`
(`tools/fuzz_routines.toml`: each kind's search one to eleven places on in
an order of the eleven slots), none mismatching; all compared but poison
(3,767; its showing waits on frames past the budget) and the MP's (4,991).
Fuzzing found the point of HP's recorded carry: the pacing's shift, or clear.
The search that runs them (`battle_dispatch_expired_timers`), 5,000, none
mismatching.

**Timing.** Calibrated from the shadow runs, battle's rows only: the
handlers that wear off 4,853-5,116, poison 25,065, the point of HP 15,635;
the three the movies don't reach take their siblings' (the slow's and the
evading much better's the shield's, the MP's poison's); the three that do
nothing keep the default. The overflow flag the showing is called with was
already declared dead at its own site (`$C1:AC57`), so the handlers' showing
needed no new evidence; `tools/suite.py --evidence` ran the notes' library
runs again with the handlers in C, and every note's sites still made what
they cite.

**After the review.** The review passed the module ("mostly") and asked for
one vocabulary. The timers are named by the status each times (`timers.h`;
kinds 6, 7 and 11 keep neutral names, docs/TODO.md), and starting one, its
count and its length are operations of `timers.c` (`battle_start_timer`,
`battle_set_timer_count`, `battle_timer_length`), which `effect_statuses.c`
calls too; `battle_timer_lengths` is two-dimensional in the map, kind by
slot, as the counts are. The handlers' table says what each kind does, which
status it follows and whether wearing off clears it (the slow's doesn't), and
the search dispatches through a table of the handlers beside it rather than
a switch (rubric item 100). The number's showing, `$C1:895B`, is an entry
of its own, run from poison's and the MP's handlers with `battle_run`, as the
original's `JSR` calls it (item 101): 12 calls over the library, the rest
compared in the handler that ran them. The empty place of the order is
ported the way the absent party member was: a timer's slot of `$FF` reaches
past its kind's row through the unchecked accessors (`battle_timer_counts`,
`battle_timer_runs_left`, `battle_timer_lengths` declare why), so the
evading much better's four calls for nobody run in C and are compared, none
mismatching, and the guard turns away nothing real. Fuzzed again, every
handler with the order's last place empty as well (`variant = "nobody"`),
none mismatching.

## Phase 3, battle chunk 4: the party's working out (`$C1:BF79`, `$C1:D523`)

The fourth battle chunk is the party's side of an action's effect: a party
member's tech or attack worked out (`$C1:BF79`, which runs `$C1:D523` and the
check of whether the members can take part, `$C1:D74F`), the item a member
uses (`$C1:BF46`), and the two pieces the effect work turned away until now,
the steal and the clock's modifier. With it every action's effect is C from
the member's or the enemy's turn to its showing.

`game/battle/party_work.c` works a party tech out as the game has it. A tech's
record (`PartyTech`, `battle/effect_data.h`) names its members and has a part
for each, an effect of the party's table with a modifier. Each member must be
able to take part: not kept from techs, with the MP their part costs, halved
or quartered by the two accessories that cut it (`battle_member_accessory`,
new in the map, kept as the battle starts). A tech that can't be done does
nothing unless the member's action goes ahead whatever
(`battle_action_goes_ahead`, bit 7, where a status picked it); what the check
found is kept for the member's turn (`battle_tech_blocked`). Each part then
runs its kind as the enemy's effect does (`effect_run_kind`), its member the
actor; a member's attack, worked out as the character's attack tech, aims
anew at an enemy still in the fight where its own is out, and ends the work
where there is none. The hits' numbers are added up as the tech says. The
work returns how it came out (`TechOutcome`): every part, can't be done,
stopped at a miss, nothing to aim at, a member in no slot; the entry derives
the carry it hands back from it. The item a member uses is one kind's work,
chosen by the item's use byte.

`game/battle/effect_steal.c` is the steal (`$C1:E9D5`): its chance tripled by
the thief's accessory, then the steal's modifier -- a charm's chance
(`$C1:EFC3`, both of the party's steals have one) -- picks the enemy's charm
where it still has it, or else the item it drops; the enemy's marks say
which it still has. What it takes goes to the party (`$FD:B655`): most items
to the inventory, and the few whose item record says so kept apart for the
thief (`battle_member_kept_item`, `$A0B8`, which the battle's end moves on).
The steal kind (`$C1:D3BF`) is an entry beside the other kinds.
`game/battle/inventory.c` is the battle's add to the inventory (`$C1:D005`),
which the rewards and the field's chests reach as well: one more of a held
item up to 99, else the first empty place, over 256 places as the original
searches (the inventory's own 242 and the fourteen bytes after, which the
map's inventory declares `unchecked`); a full inventory puts the item in the
first place, over what is there, as the original's does.

The clock's modifier (`$C1:EE27`) runs the frame's input work again mid-work
(the controller read, the button assignments and the play time's tick,
`$C2:8002`), then scales the damage by the last digit of the seconds. That is
the one piece of an effect's work that reaches the host, so the effect work
takes the platform (`effect_run_kind(g, pf, kind)`), and core's input has
`input_take_frame` for it (additive): where the console is still reading the
controller or the reset buttons are held, the port goes on where the
original would wait or reset (docs/TODO.md). The modifier's 5-6 band adds
half of the word in the direct page's first cell, where the damage is meant:
kept, and the cell's writers before it (the healing's amount, the tech's
members, the item's use, the steal's giving) keep it plainly
(`BATTLE_CLOCK_READS`, battle.h).

Two of the original's slips are kept. After a part that missed, the tech
stops where bit 7 is set in a byte the original reads from work RAM by the
tech's record's offset (`$7E:B1F5` plus eleven times the tech, through
`wram_read8`), where no table is. And a member can take part with exactly
the MP their part costs whatever their status. The MP check leaves each
member's MP over the second and third combatants' steal marks, the word the
original compares with; that is written plainly, since a steal reads the
marks of whatever it aims at.

New in the map: the tech's members (`battle_tech_members`, which the enemies'
further targets' ids share), the members' record's place and the part's
member (scaffolding), the accessories, the tech's check and its going ahead,
the kept items, the inventory search's cells (scaffolding), and why the
battle's character slots are read for an id of none. The battle's shared
flag at `$AF23` (`battle_member_acts_for_itself`) is also where the work says
an attack had nothing to aim at, and the steal's giving clears it.

**Validation.** The library's 26 movies in shadow mode, none mismatching, no
guard turning a real call away (counted with the guards instrumented over the
library): the member's tech or attack 494 calls (53 compared whole, the rest
interrupted by the frame's interrupt, which is why the fuzzing below matters),
the items used 94 (51), the inventory's add 16 (all). No movie steals; the
kinds run inside the party's work, no longer as calls of their own. **Fuzzing**, seed 1, 5,000
states each, none mismatching: the member's tech (4,817 compared; the rest
turned away by its guard), the
party's two steals with and without the thief's accessory and the inventory
at random (all), the item used (all), the steal kind (all), a hit with the
clock's and the charm's modifiers (2,492; the rest turned away, the random flags
making them hits that steal as well),
the inventory's add, at random and full (all). A mutation (the steal landing
on a draw equal to its chance) mismatched 2 of 500, so the runs see the
steal's edges.

**Timing.** Calibrated from the shadow runs, battle's rows only: the
member's tech 22,647, the item 21,593, the inventory's add 20,514; the steal
kind, which no movie reaches, takes the hit's.

**After the review.** A sentinel no longer flows through plain logic as a
bare byte (rubric item 103). The search for run-out timers says whose timer
it found as a `TimerOwner`, nobody where it took the order's empty place, and
the handlers' guards leave nobody's to the original again (the evading much
better's 4 calls over the library): the only read past the kinds' rows left
is the search's own, in one place. A party tech's part with no member ends
the tech in the logic; the byte the original reads for it past the character
slots is asked in the guard alone. Both, and the tech's misread byte, are in
docs/TODO.md's "What only the original's memory has, in the battle". The
party's effects are records (`party_effect_record`, `.kind`, `.flags`, an
attack's showing its `damage.formula`), the clock's word has a reader and its
four writers listed (`battle_clock_word`, battle.h), the timed statuses' bits
are one table (`battle_timed_status`, timers.c) that the combatants' timers
and the handlers share, and the MP check writes what it leaves once, as it
ends (`battle_tech_mp_compared`, the steal marks' second use).

## Phase 3, battle chunk 5: a party member's action carried out (`$C1:B967`)

The fifth battle chunk is what calls the party's working out: a party
member's chosen action carried out when its turn comes (`$C1:B967`), the
member's attack (`$C1:BFAA`), the waits after a tech (`$C1:BDB3`), the end of
a member's turn (`$C1:BCE1`, which the main loop calls too for a member whose
status picked its action) and the battle's step for a called-off action
(`$C1:B961`).

`game/battle/member_turn.c` carries the action out. The member is the first
round the party, from the one the main loop names, whose action waits its
turn; it and its combination tech's partners take part, and where any of
them can't act (`combatant_cant_act`, now the combatants' own, which the
combination check shares) the tech is called off for them all (the
original's `$FD:A93C`, a call-out). Otherwise what the member chose decides:
an attack, aimed at its choice and worked out as the character's attack tech
(`$CC:2583`, `character_attack_tech`), then shown; an item, aimed by the
original's targeting (`$C1:C1DD`, a call-out), worked out and shown; a tech
the record aims as one the turn carries out, aimed likewise, worked out, its
MP paid by each member (`$C1:CC71`: cut by the member's accessory, and any
member past the second paying from the third's MP, as the original does),
shown, and a steal's outcome told in a message (`$CD:0021`) naming the item,
which the steal now keeps plainly in `$06` (`BATTLE_ITEM_TAKEN`). After a
tech every member waits longer before its next turn, by tenths of its turn
wait the tech gives each part (`tech_wait_tenths`, `$CC:2BDC`/`$CC:2BEB`); the
original divides the first member's by an empty divisor cell, so it adds
nothing, and sets only that member's full gauge (kept). The turn ends with
the member's turn timer starting again and its record and its partners'
cleared; a choice of only bit 4 (`CHOICE_HELD`, not worked out) leaves
them. The showing (`$C1:AC57`) and the attack are siblings run with
`battle_run`, as the original's `JSR`s call them.

The targeting is a call-out mid-body, so the bridge asks after it what the
port needs: where the targeting left more targets than the list holds, a
target past the combatants, or (a tech it found nothing for) last tech's
members that aren't the party's, which the waits index by, the port stops at
the call's return; and before the item's, the tech's and the attack's
working out it asks their entries' guards (`effect_item_use_ported`,
`effect_member_tech_ported`) and stops at the call where they refuse. Over
the library neither stop fires and no guard turns a real call away (counted
with the guards and the stops instrumented). A member's attack aims anew at
whoever stands in its target's place: the party work's guard now asks that
the place is a combatant's as well.

New in the map: the record's aim, target and what (`battle_member_actions`),
the member's choice as the effect code keeps it (`battle_action_choice`,
which was `battle_action_goes_ahead`: bit 7 is an attack), the partners kept
while marked, two write-only flags, the kept item waiting its move, and the
item a message names (`battle_message_item`, `$7F:0200`).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the action carried out 542 calls (69 compared whole, the rest interrupted by
the frame's interrupt, as the showing waits frames), the attack 131 (10;
the rest compared in the action that ran them, or interrupted), the waits
after a tech 363 (273), the turn's end 223 (218), the called-off step 7 (all).
**Fuzzing**, seed 1, 5,000 states each, none mismatching: the action (4,910
compared; the rest guarded or past the budget), with every choice a tech
(4,921), the attack (2,050; the rest past the budget in the showing), the
waits (all), the turn's end (all), the step (3,224). Fuzzing found the choice
of bit 4 leaving the turn as it is, the turn's end indexing by A's high byte
(its guard now asks it clear, as the main loop leaves it), the attack's
stray place, and that the showing takes A's high byte along.

**Timing.** Calibrated from the shadow runs, battle's rows only: the action
9,762, the attack 17,584, the waits 21,164, the turn's end 7,665, the step
2,350.

**After the review.** One bridge mechanism (rubric item 104): the item's
use, the tech's working out and the attack's tech are siblings run with
`battle_run`, as the showing is, their guards deciding C or the original's
code, and the turn goes on in C either way; the one stop left is after the
targeting (`battle_stop_after_call`, bridge.c), where what it left is past
what the turn reads. The three cells the turn uses for something other than
their names say are named for it in the map (`battle_action_came_to_nothing`,
`battle_turn_member`, `battle_members_taking_part`, each `reuses` the other);
the choice's bits, the partners, the battle's steps (`BattleStep`), the stop
at the first target and cell `$06` (`BATTLE_MEMBER_CELL`) have one definition
each in battle.h, and the steal's outcome one decoder (`effect_last_steal`).
The tech's payment leaves the carry it hands the last multiply once, derived
after the three payments (`leave_payment_carry`). With the attack's tech run
as a sibling, the attack's fuzzing now reaches the showing in nearly every
state, past the budget: stopped at the showing's call, the one difference
left is A's low byte, which the showing doesn't read.

## Phase 3, battle chunk 6: the setup's party helpers, the inventory and the gold (`$C1:CC0B`)

The run chunk 5 named "the party's side of the showing" (`$C1:CC0B`, 1,018
bytes) is not the showing (`$C1:AC57` was C already) but the helpers the
battle's setup (`$C1:FA8B`, the original's) calls for the party, and the
party's inventory and gold behind them. All of it is C now, with the bank
`$FD` routines those helpers reach.

`game/battle/tech_records.c` fills the members' combination techs in their
tech records: what each of the party's slots pays for a double or triple
tech's part, cut by its member's accessory (`$C1:CC0B`, `$C1:CC3E`, entries
the double techs' builder `$C1:CA1A`, still the original's, calls), and a
member's triple tech, the first learned one whose three members are all in
the battle (`$C1:CCCB`), with its partners. The multiply that finds each
part's cost and the tech's members (`$C1:CB93`, now `effect_take_tech_members`)
are party_work.c's.

`game/battle/member_stats.c` sets a member's stats as it fights (`$C1:CE3A`):
its own seven, then each of its weapon, armor and helmet adds the boost its
second record names (`$FD:B141`: some stats by an amount, each up to its most,
the sum taken in a byte as the original does) and something to each of the
four elemental affinities (`$C1:CF69`). It also copies the equipment's own
records into the whole party's blocks, a piece at a time (`$C1:CF15`, with
the records' ranges, `$FD:B033`). The records are bank CC's, through
`battle/equipment_data.h`.

`game/battle/inventory.c`, which had the adding of an item, has the rest of
the inventory and the gold: whether an item is held (`$C1:CFE9`), one fewer
(`$C1:D056`), whether the party has some gold, gold given up to 9,999,999 and
taken (`$C1:D086`, `$C1:D0A2`, `$C1:D0ED`; the gold as one 24-bit number,
and the giving's hold to the most kept as the original does it: a total of
`$990000` or more has only its high byte brought down to `$98`, so the party
can end below 9,999,999), and the request by number the rest
of the game asks them through (`$C1:8003`: a chest, an event, a shop). And
the battle's item list (`battle_item_list`, new in the map): built from the
inventory's 256 places as the battle starts (`$C1:CDFF`), an empty place
keeping a clear record and an item the battle doesn't use (`$CC:0ABC`'s top
bit) left out, the rest moving up (`$FD:AF80`, `$FD:B01C`, which the stolen
item's adding, the original's, calls too). The flag that says an item was
left out is the cell chunk 5 named twice, named a third time
(`battle_item_not_listed`); the map's `reuses` now follows a chain.

Every routine here is an entry, the helpers too (rubric item 101). The cells
the setup works in are the battle's usual ones: those later work reads as it
finds them are written plainly (the stat block's place, the record's, what
the last boost added, the item and its count), the rest through
`ORIGINAL_EFFECT`. New in the map: the party's gold (`party_gold`), the
learned triple techs (`party_triple_techs_learned`, read one past its end as
the original's search ends), the item list; in combatant.h the equipment's
and the rest of the stats' offsets.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the
request 33 calls (all compared), the double and triple tech costs 904 (902)
and 198, the triple tech 93 (87), the item list 31 (3; the rest interrupted,
fuzzed), an item listed 2,410 (2,399) and an empty place 5,929 (5,919), the
member's stats 93 (89), the boost 267 (266), the affinities 267, the records'
copy 124 (110) and places 372 (359), an item held 19, gold covered 1, given 25
and taken 1; one fewer of an item no movie reaches. No guard turned a real
call away. **Fuzzing**, seed 1, 5,000 states each, none mismatching: the item
list over a mixed and a random inventory (all), an item listed (3,760; the
rest at places between records, guarded), an empty place, the stats, the
boost, the affinities, the records' copy and places (all), the inventory's
and the gold's operations (all, the gold about its most and a borrow as
variants), the request from the field's page (all, and the gold's with any
amount), the triple tech (all), the costs (753 each; the rest at places no
records are, guarded). Fuzzing found that the records' place leaves V as its
add does (declared dead); the don't-care check, that the list's entries hand
A's high byte back clear, which the stolen item's adding reads. Running the rewards' gold and
item as siblings (`battle_run`) mismatched the flags the next call-out is
entered with, where the call-outs declare them dead; they stay call-outs,
reaching the ports through the machine.

**Timing.** Calibrated from the shadow runs, battle's rows only: the request
37,631 for its own part (it saves and restores its caller's registers around
the request), each request timed as the routine that answers it
(`NATIVE_TIME_AS`: timed as one row, the requests' different lengths put it
past the timing gate's limit), the costs 5,227 and 5,250, the triple tech 24,425, the item list
15,025, an item listed 3,667, an empty place 2,977, the stats 21,859, the
boost 3,062, the affinities 2,406, the copy 27,611, the places 41,379, an
item held 14,109 (and one fewer, which nothing measures, the same), gold
covered 4,208, given 5,066, taken 4,042. The member's action (7,890), its
attack (11,057) and the party's tech (23,069) moved with what they call now
running as C.

## Phase 3, battle chunk 7: the fight's start (`$C1:FA8B`)

The plan named the battle's setup (`$C1:FA8B`) and the members' tech records
(`$C1:C96A`, `$C1:CA1A`) as the next chunk, on the understanding that every
helper they call was C. It wasn't: the setup calls eighteen routines in bank
`$FD`, part of the battle's run at `$FD:A8A5`-`$B7EB` that wave 1 left for this
bank's chunks. They came with it, so the chunk is the fight's start whole --
3.2 KiB of the original, `$C1` from 30% left to 28% and `$FD` from 39% to 27% --
and the main loop now runs it as C (`battle_run`, as the combination check).

What it is, in the game's terms (`battle/fight_start.c` has the order):

- **Who is in the fight** (`battle/fight_enemies.c`): every stat block cleared,
  the party's members in their slots with each character's slot noted, the
  eight placed enemies with those in reserve out of the fight for now
  (`$FD:B22E`); each enemy's block filled from its 23-byte record in bank CC
  (`$FD:B438`, `$FD:B4D6`), a new accessor, `enemy_record_byte`; and what each
  enemy carries for a steal to take (`$C1:FD60`).
- **The party** (`battle/fight_party.c`): a demo battle's party
  (`$FD:B2DE`, `$FD:B33F`: the characters as a new game starts them, an
  inventory of every item, the demo's HP and MP); each member's block copied
  from its character's record (`$FD:AE52`), Ayla's fists by her level
  (`$C1:FDAE`); the party listed again without a member sitting the fight out;
  the elemental affinities (`$FD:B3FE`); the accessories noted (`$FD:AE99`),
  the weapons carried (`$FD:AEF2`), the defence and attack (`$C1:FC4B`); what
  the accessories and armor do (`$FD:B555`, `$FD:B3EB`, `$FD:B363`,
  `$FD:AEC4`: a modifier, a status to start with, more HP, sharper weapons,
  confusion warded off).
- **The techs** (`battle/tech_records.c`): the records blank (`$FD:AD09`), a
  member's eight single techs (`$C1:C96A`) and its first six double techs
  with a partner in the fight (`$C1:CA1A`), beside the triple tech chunk 6
  ported, sharing one way of filling a record (`start_record`).
- **The timers** (`battle/fight_timers.c`): every timer stopped
  (`$FD:B0D5`), each status's length as a table (`$FD:B4E7`), and each
  combatant's first turn, the soonest one tick away (`$FD:B7EB`).
- **The turns' and rewards' state cleared** (`$FD:B732`), the places
  numbered (`$FD:B223`).

The map gained the characters' records (`character_records`, laid out as a
stat block's first `$50` bytes, the core subsystem's), the single and double
techs learned, the experience and tech points won, the carried weapon's
fields, and eleven cells the start clears that no traced code reads, named as
spares (TODO.md). Two of the original's quirks are kept, each in one place:
the attack takes a byte of bank CC at the member's block's place, where the
original meant the weapon's record, and a demo battle's eighth record lands on
the learned techs.

**The review** rated `tech_records.c` translation and the rest mostly or
hand-written, and the chunk was reworked before it merged. The builders'
logic no longer takes the original's carry: a part's cost is a cost, and the
carries the multiplies are entered with are worked out in leaves helpers
(`leave_single_cost_carry`, `leave_double_cost_carry`, `leave_speed_carry`).
The direct page's cells that only the start writes and overwrites are
scaffolding in each piece, one leaves helper per piece worked out from where
it ended; the start's header names the five cells that survive it and their
last, plain writers (rubric item 109). combatant.h's names for three stat
bytes were wrong, which the start's use showed (the defence is built from
`$37`, Marle's and Lucca's attack from `$3A`): `$37` is now `STAT_STAMINA`,
`$3A` `STAT_HIT` and `$3D` `STAT_ATTACK`, with the effect code, the statuses'
timers and the boosts renamed to match, and the equipment records' bytes in
the block are named there too (`STAT_ARMOR_DEFENCE`, `STAT_ACCESSORY_FLAGS`,
...). The data came through records: `enemy_stats_record`, `armor_modifier`
(the modifier table read as a stat byte and bits, rubric item 110),
`first_turn_wait(setting, speed)`, `weapon_made_for(weapon)`; the byte after
the action's targets is `battle_action_targets_end`; the carried weapon's
three marks are fields, set inside their scaffolding. Accepted as they are:
the six turn-work words `fight_clear_turn_state` clears, written by their
numbers because nothing names what they hold; the attack's sum left in the
arithmetic's cells plainly, which the effects check requires of those cells
though the first turns overwrite them; and `battle_enemy_animation_work`
indexed by enemy for its first eight bytes and past them as the original
clears them, until its reader is ported (TODO.md). A scoped re-review of the
rework rated no file translation; its last three findings (the records'
copies' per-row keeps, a weapon's record size borrowing another table's
name, the TODO line) were fixed. After the rework: shadow mode over the library and every entry
fuzzed again (seed 1), none mismatching, with chunk 6's triple tech, member
stats and boost.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the
fight's start 31 calls, every one interrupted (it spans a frame), so it is
fuzzed; its pieces as the original calls them, the enemies' records 254 calls
(222 compared) and their blocks cleared 254 (229), the members' records 93
(82), the single techs 93 (68) and the double techs 93 (36), the affinities
and bonuses 93 each (all), the combatants listed 31 (13), the first turns 31
(22), the timers cleared 31 (21), the rest 31 each, nearly all compared;
Ayla's fists 1. The demo's characters and the sharpened weapons no movie
reaches. No guard turned a real call away. **Fuzzing**, seed 1, none
mismatching: the whole start 2,000 states, the enemies' records 648 (the
rest the guard's, X and its cell differing), the members' records 3,442,
Ayla's fists 2,504 (the carry clear refused), the affinities 1,791, every
other entry all of 1,000 to 5,000. Shadow mode found one difference, the
double techs' record place, which the original keeps without the tenth
record's offset (its stores carry it); the don't-care check, that the enemy's
loader hands A back to what reads its high byte.

**Timing.** Calibrated from the shadow runs, the new rows only: the pieces
from 3,438 (the demo party's test) to 28,297 (the first turns), the single
and double techs 19,433 and 22,885, Ayla's fists 111,641 (the original divides
bit by bit). The whole start, which shadow mode never compares, takes its
measured ratio over the library, 18,554; the demo's characters and the
sharpened weapons, which nothing measures, a sibling's.

## Phase 3, battle chunk 8: the rewards of a won battle (`$C1:F93E`)

The plan named the combatants' animation commands and `$C1:F205` next; this
chunk took `$C1:F205` with what it belongs to, the rewards a won battle gives,
whole but for their messages: about 1.9 KiB of the original, `$C1` from 28%
left to 25% and `$FD` from 27% to 26%. The main loop now runs all of it as C
(`battle_run`), in fifteen entries.

What it is, in the game's terms (`battle/rewards_levels.c`'s header has the
order):

- **The experience** (`battle/rewards_levels.c`): turned to gold where a
  member wears the accessory that does that (`$FD:B201`); otherwise every
  member still fighting gets the whole of it (`$C1:F93E`), up to 9,999,999
  (`$C1:F908`), and gains levels with it one at a time (`$C1:F623`,
  `$C1:F7B9`): a level grows the maximum HP and MP by the character's steps
  (`$C1:F871`) and six stats by its growth rates, speed not at all. The
  characters in reserve get three quarters and a level at most (`$C1:F9EB`).
  A level-up works on a record, a stat block or a character's record, which
  lay their fields out alike (`Grower`).
- **The techs** (`battle/rewards_techs.c`): the tech points towards each
  member's next single tech, a locked one waiting for its character's
  unlock (`$C1:F205`, `$C1:F39A`, `$C1:F45F`, `$C1:F4DF`); then each pair's and
  the trio's combination techs, once each character knows as many single techs
  as a tech asks (`$C1:F2DB`, `$C1:F533`).
- **The items** (`battle/inventory.c`): the counts of the battle's item list
  back into the inventory, with the items the members chose and never used
  (`$C1:B4E9`, `$C1:B6D1`), as a battle is won or run from.

The ROM's tables come through `battle/rewards_data.h` (the experience per
level, the growth rates and steps, the characters' techs and their costs, the
combination records) and `party_tech_locked`; the map gained the rewards'
cells, three of them reusing the effect code's and the reactions' (the
rewards' member, the combination counted, the third character bit), whose
last values those readers find, so they are written plainly (the effects
check found both). What the rewards note for their messages got its meaning:
the levels and techs noted for each member (`battle_rewards_levels_noted`,
`battle_rewards_techs_noted`), the double techs taught and their count, the triple tech. Kept as
the original has them, each in one place and in TODO.md: the tech points paid
again where a leftover doesn't reach the next tech's cost, the reserve's level
noted in the first member's techs byte, and its count carried as a word.

**The review** rated `rewards_levels.c` and `rewards_techs.c` translation, each
for one function: the experience and the tech points added a word and a byte
at a time with a carry by hand, capped twice, differently. Both now go through
a three-byte record accessor and one named cap each, whose comment says how
the original's differs from a plain minimum (a tech point total just past the
most, with a low word under the most's, stays). The rework besides: the
experience left to share is a pot passed to the level-up rather than the
cell, which only the entries load and store, and the members' loop stops on a
member knocked out where the original would offer it the rest for ever (so the
guard lost that condition); the single techs' place in the messages' list and
the member a combination is noted for are parameters; every loop's
scaffolding is worked out after it and written once, by a leaves helper; the
shared multiply's carry is scaffolding; the growth steps and the combination
records are read by number; the addresses the pieces leave behind, the
character bits and the cell offsets are in one internal header
(`rewards_work.h`); `battle_rewards_noted` is two named views, the levels
noted and the techs noted; and the single techs' cursor is a byte. Accepted
as they are: the reserve's count of levels carried as a word, since the
reserve's byte isn't cleared between battles and gets to `$FF`; the rewards'
member a word, as the effect code's block offset it reuses is; and a
combination's asks as the place in bank CC its record gives. Three rubric
items came of it (111-113). A scoped re-review of the rework rated no file
translation; its residual findings (a knocked-out test made twice, a constant
defined twice, three places built inline, two comments above the wrong
declaration) were fixed, and the arithmetic's first cell, written on each pass
of the stats' and the costs' loops, stays plain, as the effect code reads it.
After the rework, every entry fuzzed again with seed 1, none mismatching.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the experience's share 25 calls (23 compared, 2 interrupted), its level-ups
130 and experience added 129, the HP and MP grown 1, the reserve's share 25
(24), the techs 25 (23), the tech points 8, the combination techs 86, the
items put back 25 (16) and a choice returned 82 (81), the gold 25; a single
tech learned no movie reaches. No guard turned a real call away. Shadow mode
found the carry the share leaves, which the messages' call compares, and the
flags the main loop hands them (their overflow is dead, which the don't-care
check agrees with). **Fuzzing**, seed 1, none mismatching: the share 5,000
states, the level-ups and levels 10,000 each, the techs 10,000, the tech
points 6,657 of 20,000 (the guard's: the member's block and the one noted
differing) and the next tech 10,000, a single tech 5,000, the
combination techs 10,000, the rest 2,000 to 20,000. It found two of the
original's ways the movies never meet: a count of techs known of `$FF` ends
the combination's list, and A's high byte comes into a single tech's index.

**Timing.** A level-up's time follows its path: one that grows the stats
divides six times, and the original's divide goes bit by bit, far longer than
the port's. One ratio put the level-ups over the worst limit (97,287 clocks),
so each piece's work is timed as its own routine's (`TIMED_AS`), the shared
multiply and divide among them, and the new rows calibrated from the shadow
runs: from 135 (the reserve's share, whose work is nearly all its pieces') to
33,106 (the techs); a single tech learned, which nothing measures, takes the
next tech's, 21,182. The gate then passes with no new exception.

## Phase 3, battle chunk 9a: the animation script language, its table and its first half (`$C1:4CF0`)

The plan named the combatants' animation commands next. They are a language:
219 command bytes, 115 handlers from `$C1:4CF0` to `$C1:7A6A`, one channel
per combatant, the other targets and each effect, run by `battle/scripts.c`
since chunk 1 through the ROM's pointer table. This chunk is its first half:
the table in C and the commands that need neither places nor paths, 154 of
the 219 bytes (70 of them the halt), in 69 entries; `$C1` from 25% left to
20%. The other half (places and facings, the
moves, sound and the screen effect) is chunk 9b.

What it is, in the game's terms (`battle/script_language.h` has the shared
vocabulary):

- **The rows** (`battle/script_language.h`, the event engine's shape): each
  file of commands has a table, a row a command byte, with the operation, how
  each of its typed, named parameters comes from the command (an operand
  byte, or fixed by the byte: a speed's number, on or off, a value's kind, a
  slot), the command's length and what the operation needs of the state.
  The runner decodes the command by its row into a `ScriptCommand` and the
  operation takes the parameters it names (`play_animation(g, channel,
  animation, mode)`); it returns a `ScriptFlow` (on past the command this
  frame or next, stay, again, or moved by a call), which the runner takes
  (the original's `$C1:75BB` and its continue flag). The original's handler
  addresses are a separate dispatch table in `battle/scripts.c`, for the
  call-outs and the timing: a command with no row, or whose needs the state
  doesn't meet, runs the original's handler. `$01` and the 69 other bytes
  the ROM's table gives its handler are one halt.
- **The flow and the values** (`battle/script_flow.c`): end, halt, call and
  return (a return place per channel), waits, and the script's 27 variables,
  two marks and its point, which the waiting commands test.
- **Poses** (`battle/script_poses.c`): an animation played looping, or once
  while the channel waits for it, with the effect shown or with a count; a
  mover's own speed or one of eight; drawn on top, hidden, off the
  battlefield's plane, the sprite glided or put back, a shadow, a trail of
  afterimages, attribute bits.
- **Effects and colors** (`battle/script_effects.c`, `script_colors.c`): what
  an anchor follows, the effects shown, an effect drawn with a combatant; the
  flash, its restore and blink, the rotations, the palette animations' slots
  and frames.

A command acts on what its channel animates, one of three kinds
(`ScriptChannel`): a combatant, the other targets' list, or the channel's
effect (`battle_script_effect`, named for the effects' channels it was
first taken for the enemies'). The map gained the four cells starting an
animation resets (the facing an object's and an effect's frames were last
taken for, an effect's animation's place and length), the return places and
the pointer's pad byte; the per-combatant and per-effect cells the commands
keep, declared as row-indexed arrays, are split into what each row is (the
frame an animation is on, the two waits' marks, the blink's countdown and
whether it flashes). The effect animation tables,
anchor sources and effect records take unchecked accessors, for the operands
the original doesn't check. Reproduced on purpose, in TODO.md: the list waits
leaving their mark set, the shadows' and the rotation's lists starting at the
wrong subject, the glide's equality, the counts that count up, the blink
running itself again.

**What the needs are for.** A guard alone isn't enough here: the runner calls
the operations without one. The endings' movie runs a script on a channel
whose subject is none (`$FF`), whose original writes land past the tables (a
scheduled list at `$A2E6`); a row whose needs aren't met there goes to the
original's handler, from the runner and the registry alike.

**What the gate found.** Shadow mode: the overflow flag a C command leaves at
the next call-out, which no handler reads (declared at the call-out, with a
`ct_dev --poison-dc` run as its evidence); the out-of-range subject above.
Fuzzing: a script in work RAM lying over the direct page, whose cells a
command writes before the original reads its next operand there; the
original runs those (`script_over_the_direct_page`), as the event engine
does for a command at its buffer's end.

**The review** rated every file "mostly", none translation, and asked for two
things first: the table to decode operands into typed parameters (the first
version handed every operation a polymorphic `form` byte and read operands
by position), and the work cells to be scaffolding (they were written
plainly for one reader, stopping a trail, which itself only sets dead
periods). Both are done as above: everything the commands leave in the first
work cells is written by a leaves helper under `ORIGINAL_EFFECT`, and
stopping a trail sets its periods from them only there, the trail's two
readers answered in the effects baseline (they read only while the trail is
on). Besides: the three waits are one `play_and_wait_as` with a `WaitStyle`;
`$26` on an effect channel, which no script does, is the original's to run;
the flash's, the palette sets' and the palette frames' accessors take a set
or a frame, not a byte offset; the cycled background's painting is the rule
it is, its leftover count worked out after; one `ScriptPlace` arithmetic for
the advance and the call's return; the held addition a clamp; the registry
names follow the operations. Accepted: the number display's commands
(`numbers.c`, chunk 1) keep their own pointer advance, which differs only for
a script past the end of a bank; they move to the rows when 9b reaches them.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the 69 entries were entered 1,978,926 times (1,973,533 compared, 4,973
interrupted; the rest turned to the original by their needs, the endings'
subject among them), the waits most (the second mark's 311,758, the shown
wait's 105,129), the channel runner 1,017,429 (3,363 interrupted). Eleven
entries no movie reaches: the first mark's set, count down and sums, a
variable stored from it, the swaps, an anchor let go, the return at the top
and the skip. **Fuzzing**, seed 1, none mismatching after the fixes: every
entry, 186 to 469 states each of 8,000 (the guard's: the channel and its
offset differ in fifteen of sixteen), 30,157 in all. It found the blink's
byte turned over by its low bit (a leftover 3 turns to 2, which flashes) and
the direct page's mirrors in the upper banks.

**Timing.** The entries' costs come from the shadow runs, from 1,938 (the
halt) to 6,943 (a flash); the eleven no movie reaches take a sibling's (a
mark's sum a variable's). The full gate then passes with no new exception.

## Phase 3, battle chunk 9b: the animation script language's second half (`$C1:762E`)

Chunk 9a put the language's table in C with the commands that need neither
places nor paths (docs/history/reimplementation-chunks.md). This chunk is the rest: the
places and facings (`$C1:762E`, `$C1:75CC`, their 97 handlers), every move,
the grid walk with its search (`$C1:4A71`), the sound commands, the screen
effect `$80`, and the number commands, which moved onto the rows: 64 command
bytes in 46 entries, about 8 KiB of `$C1`. Every command byte now has a row;
the ROM's handler table is only the fallback for a state a row's needs turn
away.

What it is, in the game's terms:

- **Places and facings** (`battle/script_places.c`): a place is one of 72
  points a command names -- where a channel's subject stands or is aimed at,
  halfway between two, the middle of three, part of the way from one to
  another (eighths from a ROM table), 24 pixels along a mover's heading, the
  battlefield's middle, where the first three began, where an anchor draws, a
  point kept in the variables. The battle keeps the place last found (one
  place names it), and the facing likewise. The commands that set the
  script's point, an animation slot's place and a mover's heading are here;
  `battle/objects.c` now works the animation slots' places out as C.
- **Moves** (`battle/script_moves.c`): lines, chases, jumps and short hops,
  spirals (open, counted, and stopping at walls and other combatants),
  orbits and flat orbits, the throw (aimed as a jump onto a place, an arc's
  top worked out, a rise in a line, the 36 frames shared between the waits
  before and after), the flicker, and a point some steps along the line to a
  place. A move sets up the mover's path (`battle/effect_paths.c` runs it) and
  waits for its end; the mover vocabulary (`Mover`, `PathKind`, where a
  mover is as a `BattlePoint`) moved to `battle/effects.h` so the paths and
  the commands share it.
- **The walk** (`battle/script_walk.c`): a ring-by-ring search out over the
  battlefield's 16 by 16 grid for the way to a point, marked back from the
  goal, and the walker's own copy of the marked grid, stepped a scripted
  move at a time. The search's state is locals and what each ring returns
  (`Ring`); a step returns where it saw the goal, if it did (`WalkStep`);
  the cells the original leaves are worked out from those results, once, by
  a leaves helper.
- **Sound and the screen** (`battle/script_sound.c`): the sound commands are
  the language's first bridges: their rows only decode them, and a table
  beside the dispatch in `battle/scripts.c` names the bridge each runs as its
  handler's entry through `battle_run` (the driver called through the audio
  service), so the language's rows stay free of the machine; `$80` hands the
  screen effect its settings, whose core `battle/screen_motion.c` now has as
  C (`battle_set_screen_effect_to`).

**The third trap, settled first.** The chunk's only call-out left is the
sound driver's (`$C7:0004`), whose don't-care set is liveness's (`V` dead
into it and after, by `tools/v_readers.py`). The arithmetic, the direction,
the boxes and the grid were already C and are called as C.

**What the original leaves.** Its handlers keep operands, loop places and
partial sums in the battle's first work cells, written by leaves helpers as
scaffolding; three cells turned out to be read back as game input:

- the velocity a line is worked out with (`$A880`/`$A882`), only within the
  set-up: scaffolding;
- the jump lift (`$90`, `battle_jump_lift`): the original's jumps to the
  script's point take what is there, a cell shared with the animation slots'
  loop count and the walk's search; a probe over the library found it always
  6, the count the loop ends at each frame. The rows lift by that as a named
  constant (`POINT_JUMP_LIFT`), and their needs leave the jump to the
  original while the cell holds anything else, so every write of the cell
  stays scaffolding;
- the operands a chase and an orbit at a place keep in the cells the place's
  working out uses: a place that uses them overwrites the heading, the circle
  and the quarter turns the command then reads. Those places, which no movie
  has a chase or an orbit take, are left to the original
  (`NEEDS_SUBJECTS_AND_A_QUIET_PLACE`).

Reproduced on purpose, in TODO.md: the middle of three's Y taken from the
X sums; a channel's move length kept per channel and gone past whatever
command it is at; the other targets' forms (lines set every target off and go
on at once, spirals out set them off without marking them moving, orbits go
on when none has ended, the chase's own heading taken from the first for the
rest); the anchor's rise in a throw reading its speed at twice its number;
the place along the heading reading the running channel's subject as a word.

**What the gate found.** Shadow mode found the cells the original works a line's velocity out in (`$A880`/`$A882`), which only the set-up reads, and a put's length left in its work cell. Fuzzing found the anchor's rise leaving
its moving flag alone (the combatant's is stopped), the chase's and orbit's
operands overwritten by a place, and the slots' places loop reading anchors
past the eight for a running channel past the sixteen (now the original's).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the 46 new entries were entered 234,643 times (230,367 compared, 542
interrupted), the line to a place most (44,382), the number commands next
(55,134); the channel runner 1,017,429 times and the animation slots' places
281,498, both compared as before. Three kinds of call aren't compared on
their own: the sound bridges' (3,472), set aside inside the channel runner's
comparison while the driver's call-out runs past them (the runner's matched);
the 180 calls of the throw's last step entered by the throw's wait falling
into it, which its guard leaves to the original as the handler its byte
names isn't it; and 82 jumps in `endings-beyond-time` on a channel whose
subject is none, which the rows' needs leave to the original (TODO.md). Five
entries no movie reaches: the chases and walks to coordinates and to the
point, and the point along a line. **Fuzzing**, seed 1, none mismatching after
the fixes: every entry, 90 to 509 states each of 8,000 (the guard's), and the
search alone 8,000.

**Timing.** The entries' costs come from the shadow runs, from 2,667 (a
sound effect's own work, the driver's being its own) to 7,858 (the numbers);
the five no movie reaches take a sibling's. The walk's search is timed as its
own routine (`TIMED_AS`, `$C1:4A71` registered for it, 10,757), and so are the
screen effect's three kinds, which put its worst stretch and the channel
runner's within the gate's limits; the runner and the slots' places loop were
recalibrated. The full gate then passes with no new exception.

## Phase 3, the scene program: the jet bike race running (`$7E:416D`-`$7E:5220`, `$7E:B574`)

The first chunk of bank `$7E`, the program the field and the game over
decompress to `$7E:3000` and run there (DISASSEMBLY.md, `[[ramcode]]`), on
top of the scene engine (`game/scene/`).

**How it was chosen.** The program dispatches sixteen modes through its table
at `$7E:3043`: 0 and 9 the race (9 the attract demo's), 2 the title, 10, 11, 14
and 15 one ending's code (`$7E:8538`), 12 and 13 another's, and modes 1 and 3-8
no movie reaches. Of `$7E`'s 24.9 KiB, 92% was generated; by the coverage
files the race is reached by `events-jetbike-race`, the two endings by three
movies. The race while it runs is the largest coherent piece: the tasks its
scripts put on the two lists between `$7E:416D` and `$7E:5220`, and the
routine it generates at `$7E:B574` to regroup its scaled pictures (5,153
bytes, which the endings run too). Left at the edge, to be the race's next
chunk (PLAN.md): the bikes placed in perspective (`$7E:4D0D`, which searches
for its divisors through the CPU's divider with the carries of a dozen sums
running through it), the main loop's scaling of the pictures (`$7E:51D1`,
which stops at its own wait and hands a carry to a loop that reads it), the
perspective table (`$7E:357B`, whose code the race and the endings patch), and
the setup and its loader.

**What it is.** `scene/race.h` names what the parts share: the two bikes as a
record of `$25` bytes each on the scene's page (`scene_race_racers`), channel
0's registers as the camera (height, place, distance, angle, pitch, speed,
lean) and channel 1's as the racers' sizes. `scene/race_rules.c` is the bikes:
speed lost and gained toward a top speed that the lead picks (Johnny's is
higher behind), the boosts on B with the gauge flashing in the road strip,
Johnny's controls (blocking while he leads, swerving away when Crono comes
alongside ahead), the drifts across the track, the lead in 32nds, whether they
touch and how they bump (a bump along swaps speeds, across swaps drifts), the
score, both finishes (Johnny's: the markers stopped, the camera following and
the score drained; Crono's: the result and what the score earns kept for the
story at `$0380`-`$0383`, the victory song, the coast to a lane), and the end
of the scene. `scene/race_camera.c` is the camera: its place along the track,
its offset turned by its angle (`scene_multiply_signed16`, the program's
`$7E:33D6`, now in `math.c`), the flat layers' scroll and the horizon's split
of the per-line tables, the pad's directions given to the lanes by the eighth
of a turn the camera faces, the demo's turning and raising, the perspective's
check (it stops at `$7E:357B`), the controller kept, the settling after the
finish and the opening picked at random. `scene/race_drawing.c` asks for each
racer's scaled picture and draws its sprite list over the tiles, the nearer
first, and the panel: the distance bar's markers (placed by the multiplier),
the boosts, the distance to go and the score (`$7E:39F5`, a number as
sprites). `scene/race_matrix.c` gains the generated routine itself: the
regrouping of a scaled picture into sprite tiles, as a loop over the
generator's own table (its header's account of what the code is for was
wrong, and is rewritten). `scene_race_move_camera` now runs to its return (it
stopped at `$7E:442E`). The race's effects are the platform's driver call
from `race.c`, the one file of the race the lint allows it; the sprite
routines and the arithmetic are C calls, each timed as its own entry
(`TIMED_AS`).

**The registers, settled first.** Liveness has A's high byte and Y live after
every frame task: the next task on the list may read Y, and the last slot's A
goes out through the frame's work. So each task leaves A as the original does,
worked out after the logic from the state it left (scaffolding), and Y alone
but where the drawing's sprite routines leave the next free sprite. Two tasks
take in the carry the list's loop leaves, which is clear but in its first
slot; the drawing's guard asks for it clear (the race's list never puts it
first). The effects' calls through the program's send (`$7E:377E`, which waits
for the driver) declare A, X and the flags dead, which liveness proves at both
sites. The stops into the list's own code (`$7E:39B1`, `$7E:39A7`) and the
perspective's keep what those read, as their own sets (`m_stop_at_own`).

**The carries.** The race never clears the carry between the sums of a frame's
motion, so a drift's build-up and loss, the lead, the touching band, the boost
and both kinds of bump each take a unit from the sum or comparison before
them. The first version threaded that carry through the rules; the review
rated `race_rules.c` a translation for it, and it was rewritten. Each rule is
plain arithmetic with one term for its unit (a drift a unit stronger or slower
to die, the lead a 32nd shorter, a bumped bike a unit farther), and one
section, "The units the motion hands on", works each unit out from the values
the step before used, in the order the original's sums run (`drift_units`,
`contact_units`, `bump_along_units`, `bump_across_units`). A bump along is one
32-bit distance; the original also takes the push's high word, 0 in any real
bump, off both halves of the other bike's distance, which stays as a term.
Three units that are constant wherever the game's values are became accepted
divergences, written at the entries and held by the fuzz constraints: the
boost's (whether Johnny is still speeding up, where the original compares his
speeds unsigned), pulling over after the finish, and the markers' place on
the distance bar. The same review had `race_drawing.c` work out the
arithmetic cells it leaves from the state afterwards instead of threading
them, compute the markers' product in C (the multiplier's last state left as
scaffolding before the panel's numbers use the divider), and name the pose's
backward turn that lengthens the bar by a pixel.

**Program data and patched code.** The race's tables live in the program
image, and the race and its setup write some of its operands: the demo's
finish lanes (`$7E:46CE`, `$7E:46E4`), the distance bar's ends (`$7E:4F74`,
`$7E:50DA`), the distance shown's lag (`$7E:5171`), the return Johnny's win
writes over the markers' call (`$7E:510D`), the horizon's line counts in the
per-line tables. Each is a variable in `re/ram.toml`, as are the racers'
record and the arithmetic cells' fields for the bump's push and the pose; the
guards hash the code around them.

**What shadow mode and fuzzing found.** Over the 26 movies every call matches,
none refused: in `events-jetbike-race` the camera's tasks, the horizon and the
drawing 2,640 calls each, the ride 2,276, the pad's directions 2,349, the
following camera 163, the score's draining 3, the opening 1; the regrouping
948 there and 3,025 in `endings-beyond-time`, of which 1,389 are interrupted.
The movie loses the race, so Crono's finish, the coasting, the victory song,
the settling, the end of the scene and the demo's race are fuzzed only.
Shadow mode found the accumulator's width at two returns (the bump's call
leaves 8 bits, the follower returns in 16; the two waits after the finish,
which no movie reaches, return in 8 too). The first fuzz runs found
the demo's turning clamped unsigned (the original compares by the sign), the
opening's guard on a data bank of 0 (the script calls it with the program's,
`$7E`, and the fuzz runs now do), and the drawing indexing the program's data
with a lean past 2 (the guard refuses it). Fuzzed from the `race` state with
the program kept (`--keep 7E3000-7EC994`; `tools/fuzz_routines.toml`), seeds 1
and 2, 20,000 states each, none mismatching: every entry, the ride three ways
(as it comes, with both bikes' distances at the finish's word, the bikes level
so they bump every way; at most 3 states refused, a task list without room),
the coast with the finish margin near 0 and Crono's lane at the middle, the
victory song's wait with the music playing (two thirds of its states reach
its arrival), the leaving at its last frame, the drawing with up to three
boosts. Johnny's speeds are kept within $8000 of his top speeds and the
coast's lanes in the game's, where the accepted divergences hold.

**Timing.** The new rows were calibrated over the library, with
`scene_race_move_camera` and `scene_race_set_engine_pitch` (which now reads
the speed's high byte as a word). Two vblank-tail exceptions: the drawing
(22-line calls, the sprite routines timed as theirs) and the perspective's
check (the divisors' clear charged as the block move it is), both writing
RAM. `$7E` stands at 10.2 KiB ported, 59% left.
## Phase 3, battle chunk 10: the enemies' movements and the main loop's last callees (`$C1:3772`, `$FD:AB30`)

What was left of the battle in bank `$C1` after chunk 9b, and of its routines
in bank `$FD`, by `python3 tools/burndown.py --uncovered C1`: the enemies'
per-animation effects (`$C1:3772`-`$C1:4021`, 2,278 bytes, the bank's largest
run), and every callee the main loop still called out to but the steps, the
messages and the rewards': a member's status turn (`$FD:AB30`), the counters
(`$FD:AC6E`), the calling off of a combination (`$FD:A8CE`-`$A95F`), the
dropped action (`$C1:B3F9`), the enemies an action knocked out (`$C1:B762`),
the demo's party (`$C1:B70E`, `$B725`), the standing down (`$C1:B7F2`) and the
battle's end's bookkeeping (`$C1:EA9D`, `$EAE8`, `$B442`, `$B3BB`, `$B3D2`,
`$FD:AA98`, `$AAB0`): about 3.4 KiB, all there was by concept. The bank's
burndown went from 7% left to 3%; what remains are runs inside ports that hand
back to the original (PLAN.md).

What it is, in the game's terms:

- **The enemies' movements** (`battle/enemy_movement.c`): an animation names
  a movement, a step of eight pixels every few frames, looked at before it is
  taken (the grid, the other combatants' boxes) and then drifted
  (`battle/effect_paths.c`): approaching to a reach, charging, lining up,
  keeping a distance from the target (the spot on the enemy's side, backed
  away from or closed in on), circling, weaving round a loop below where the
  weave started, marching one way, making for the middle; and stopping to act,
  with waiting for the target to move on. The animation picks each one's
  variant; the drift's cells were renamed for what they hold
  (`battle_enemy_drifting`, `_heading`, `_drift_from_*`). The loop in
  `battle/animation.c` runs them as C, each timed as its routine, with the
  ROM's table only for an animation that names no movement.
- **The party's turns** (`battle/party_turns.c`): a member driven to fight or
  confused attacks at random; a member kept from acting loses its waiting
  action; the counters; the combination called off; the knocked-out enemies
  with no last reaction counted and shown beaten; the demo's party; standing
  down. The steps and the attack run between decisions, so these are bridges,
  run by the main loop as siblings (`battle_run`), each registered.
- **The battle's end** (`battle/battle_end.c`): the statuses cleared, the
  party stood up and kept in its records, the enemies taken off the field but
  for those marked to stay (bit 6 of the placement's kind, which the field
  reads), the enemies counted out when the party runs, and the beaten noted for
  the story (`battle_enemies_beaten_for_the_story`, event memory `$1EE`), all
  C the main loop calls as C.

**The third trap, settled first.** `tools/v_readers.py` found V dead into the
steps (`$C1:0003`), the called-off step (`$C1:B961`), the attack (`$C1:BFAA`)
and the reactions' finding (`$C1:B488`), and after every routine the main loop
called. The library reaches the steps' sites too seldom for a run there to
count (19 calls at the most), so each step's V rests on a `ct_fuzz
--poison-dc` run of its routine, the called-off step's with A's high byte
clear as its callers leave it: random high bytes send the step's dispatch
where V is read (the notes in `party_turns.c` and `member_turn.c`). Where
the main loop's next call-out reads A's high byte after a routine that is now
C, the loop clears it as the original's code there does (the victory's step,
the running away's message, the field's return).

**What the gate found.** Shadow mode found that a sibling run through
another sibling must keep the original's stack: the called-off member
(`$FD:A8FE`) became its own entry so the step's call meets the original's at
its depth. Fuzzing found that the movements and the dropped action find their
cells with A's high byte, clear as their callers leave it (now their guards'),
and that the charge leaves three past the slot the boxes' test handed back.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the
movements' loop 55,081 calls (27 interrupted), the movements 1,985 (waiting
473, approaching 1,244, circling 155, weaving 93, keeping a distance 20; no
movie charges, lines up, marches or makes for the middle); the dropped action
39,836, the combination called off 15,580 and its members 2,522, the knocked
out 4,611 (350 interrupted), the counters 4,154, the status turn 2,810, the
end's bookkeeping 26-29 each (one running away). **Fuzzing**, seed 1, none
mismatching: every entry, 5,000 or 8,000 states (the status turn 666 and the
attack from bank `$FD` 24 compared, the rest past the budget in the attack's
showing).

**Timing.** The new entries were calibrated from the shadow runs; those no
movie reaches take a sibling's cost. The effects' loop, the main loop's body,
the turn, the timers' dispatch and the combination check were recalibrated.
With the turn's callees timed as their own routines, the timers' dispatch (a
whole turn) attributes its time to more than the 32 routines a stretch held,
so `MAX_SEGMENT_PARTS` (`runtime/port/native.c`) is 48.

## Phase 3, menus: the techs screen (`$C2:B35D`-`$C2:BF2E`)

The menus' screen 3, the run the burndown had largest left in the bank
(`$C2:B520`), taken whole with the main menu's panel for it: 2.8 KiB, and the
pointing among the members a healing is used on (`$C2:B83E`, `$C2:B870`), which
the items screen's tonics shared as a call-out. `$C2` went from 28.1 KiB ported
(53% left, 300 entries) to 30.9 KiB (48%, 326 entries).

**What it is.** `menu/techs.c` is the techs as the menus list them: a
member's techs of a kind (each tech group the member is one of, in the groups'
order, and each group's techs the party has learned), what one costs the
member (its part's cost, which two accessories halve or quarter), whether it
heals and what it restores, and a tech's row in a list in the colours of one
usable here or still being learned. `menu/tech_screen.c` is the screen as the
game sees it: the member rows down the left (and their slide past the ends,
the view moved four lines a frame through the screen's offset-per-tile
table), the kinds' tabs, the list of eight rows with its slide and paging, the
tech on the cursor with its other members' rows and their costs, the
description, a healing tech used on the window of active members (the user's
casting pose, the cost taken from its MP), and the panel. `tech_screen_bridge.c`
runs it on the machine: the scroll bars' frames and task, the cursor's moves
within a list, the paging, the list's slide frames, the description's
setting, the stopping of tasks, the restoring and the cursors on every member
stay the original's. The rows' drawing reuses the shared vocabulary
(`menu_draw_windows` and `menu_queue_fixed_upload`, now in `windows.h` and
`vram.h` for every screen; `menu_load_character` beside `menu_load_member`).

**The flags, settled first.** Every call-out the chunk keeps was checked
before the logic: they read memory, Y (the lists' frames) or A (the stopping
of tasks, the wait for frames); the one register a port reads back from one
(`$C2:EA53`'s N, a push past a list's edge) is worked out from the state. What
needed care is the P each routine saves as it starts, compared under its
calls out, and the flags a call-out declared don't-care carries through
unchanged where its callee saves and restores them (`$C2:8255`, `$C2:8AD5`,
`$C2:F643`): the bridge leaves the original's flags before each (the leaves at
the end of `tech_screen_bridge.c`), with the shifts in the healing's targets
(the carry clear), the kind's switch (the carry of the compare before it goes
round) and the rows' copies (the accumulator's high byte, `$34` down the list
and `$FF` up it) the ones shadow mode and fuzzing found.

**What shadow mode and fuzzing found.** Two movies reach the screen (`menu`
and `menus-field-menu`): every call matches, none refused (each entry's counts
are its ports.tsv row), among them 403 frames among the members, 390 on the
list, 222 in the window a tech is used from and 430 pointings at a member to
restore. The library never slides the member list or the tech list. Fuzzed
from four states of `menus-field-menu` (on the members, on the list, in the
window, on the triple techs), seed 1, 2,000 states each, the menus' tables
(`$7E:7700`-`$7FFF`) kept with low RAM and the tilemaps, every entry up to its
first wait for a frame: 0 mismatching. The first runs found the double and
triple techs' cost read and kept a word wide (the next part's cost lands in
the byte after, now `menu_tech_cost_beside`), the list's slide copying with
the copy's direction swapped, and entries the fuzzer entered with 8-bit index
registers where the game never does. The parts table is declared from
`$7E:1607` so a tech's index into it needs no subtraction; a stale list as the
screen opens can't read past it. Changing the items screen's call-out into a
`menu_run` of the shared pointing, and its two member-window loops into one,
left its fuzz counts unchanged against main's (`tools/fuzz_compare.py
--before`).

**Timing.** The rows' drawing is charged at registered entries of its own
(`menu_show_member_tech_list` `$C2:BC6F`, `menu_draw_tech_rows` `$C2:BD6B`,
`menu_show_pointed_tech` `$C2:BB1F`, `menu_draw_tech_member_row` `$C2:BA2F`,
0.2% error), which took the members' step's worst vblank error from 48518
clocks to 10470. Two exceptions remain (`tools/timing_exceptions.txt`): the
pointed tech's rows (`menu_show_pointed_tech`, and `menu_choose_tech`, which
carries it), whose empty rows inherit the window lists' strings' spread (over
by about 1600 clocks a row) while the triple techs' member rows fall under,
holding the routine's own time at the floor.
## Phase 3, battle chunk 11: the screen script language (`$CD:1609`-`$CD:31A9`)

The largest runs `python3 tools/burndown.py --uncovered CD` listed in the
battle's frame work, `$CD:17A9`-`$CD:319D`, are one thing: the 122 handlers
of the screen script language, whose runner (`$CD:1609`) the frame work
called out to, and whose window, motion and palette bodies in bank `$D1`
were already C (battle chunk on the screen effects, docs/ports.tsv). The
chunk is the runner and every command whose handler stays in that range;
it stops where a command leaves the language for a renderer (the circles'
and shapes' at `$CD:31AA`-`$CD:3656`, the triangles' rasterizer `$CD:367C`)
or for bank `$CE`. `$CD` went from 70% left to 39% (4.7K of code).

What it is, in the game's terms (`battle/screen_script.h` says it at
length): a spell that dresses the whole screen runs a script from bank `$D1`
on one of two channels, each with a layer of its own. A byte below `$7F`
shows a frame of the effect's animated graphics and ends the channel's step,
`$7F` ends it, `$FF` ends the script, and the rest are commands that run
within the step: the channel's pace, loops (one counts the frames on),
calls, jumps and switches on the animation scripts' variables, and waits
that jump back to the step's end; the layer nudged eight ways, put at an
animation position, swung, or set on an orbit of three sines
(`screen_layers.c`); two circles placed, widened and narrowed by the
animation scripts' variables, and two triangles, flat (corners at angles
and distances about a center, stretched) or turning in space, or one of the
ROM's models of three points, laid out for the rasterizer with their size
(`screen_shapes.c`); waves and a sway across the layers' lines, and a ripple
down both windows (`screen_waves.c`, `screen_window.c`); and the screen's
settings, colors, palette animations, sounds and the frame work's switches
(`screen_settings.c`). The language is a C table: each file ends with its
rows, which decode the operands into named parameters (a shape, an angle per
corner, a distance back) and say what an operation needs to run as the
original does (a shape one of the two, a position one of the six); the rows
of the commands that are still the original's have no operation, and the
runner calls their handlers as the original's dispatch does.

**The third trap, settled first.** `tools/v_readers.py` found V dead into
every handler that stays the original's and into the rasterizer, and after
the runner returns; they are called with A, X, Y, the carry and N and Z as
the dispatch leaves them, V declared. What the runner reads back of a
command is A's high byte, which the original shifts into the next frame's
place in the graphics: `tools/disasm/high_byte.py` shows every handler
returning it clear but the window fill, the beam and the quadrilaterals,
and of the fills only filling window 1 sets it, which no script of the game's
asks for (a scan of the scripts the table at `$D1:646C` points at), so the
runner hands that fill to the original's handler; where a handler
that is still the original's leaves the byte set, the C runner hands the
step to the original's from the dispatch's return. A byte past the last
command sends the original's dispatch past its table; the runner hands it
back at the loop's head.

**Call-outs where the original makes them.** The commands' call-outs (the
original's handlers, the rasterizer) are compared at the original's stack, so
the runner's three routines are siblings run through `battle_run`, each
registered: the frame's run (`$CD:1609`) pushes the count of ended channels
as the original's `PHY` does, a channel's step (`$CD:1654`) keeps its offset
on the stack, and the dispatch (`$CD:16A6`) runs one command, so a call from
a command meets the original's at `$CD:16AF`. The generic entry
`battle_screen_command` stands at each of the 104 C handlers, so each is
fuzzed alone; one that draws stops at the rasterizer, as the handler's jump
does.

**What the gate found.** Shadow mode found the original's leftovers in the
battle page's cells: a wait's distance back as a word in `$45`, the operand
the change of a variable and the corners' limit leave there, and the
palette animation's raw operands. The triangles' sizing (`$CD:211B`) stores
its operands to the multiplier as words where the corner moves out: the
product is read as a word, so the controller's low byte (`$4218`) comes along
as the high byte of the corner's word in `$4B`, and the size's word store
puts `$22` in the divider; the runner reads the controller for that one
cell, as scaffolding. The models' rotation is handed the size as a word whose
high byte clears the low byte of `battle_turn_slot` (`$22`), a real write the
port keeps. Fuzzing found that the runner must hand a channel's step the pace
in A, as the original's `TDC` leaves its high byte, and that the jitter takes
the dispatch's carry into its first pick, which the commands' guard now
requires.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the frame's run 431,221 calls, the channels' steps 129,354, the dispatch
403,323, the commands 1,260,637 in all, none interrupted; 22 commands no
movie reaches (the ones the scripts of the library's battles don't use, among
them the orbits, the swing's start and the models that spin). **Fuzzing**,
seed 1, 5,000 states each, from the `battle` state, with the operands as
random bytes in work RAM and the guards' bounds kept (the shape, circle,
slot and position one the language has): every command 0 mismatching of
about 2,500 compared (half of the states turned away by the dispatch's carry);
the dispatch 0 of 4,778; the frame's run 0 of 4,819 and a channel's step 0
of 4,876 over random scripts. The step's first run had one mismatch: a
random `$8C` with an operand of `$0F`, whose original handler jumps through
its table of four swing words by the operand and on to `$CD:6020`, code that
isn't a handler, which ran off with the interrupt flag cleared; no script of
the game's has `$8C` or `$8D`, so a step that comes to one naming no word is
handed to the original's runner at the dispatch's call. The commands' constraints are in `tools/fuzz_routines.toml`.

**Timing.** The new entries were calibrated from the shadow runs; the 23 no
movie reaches (or whose stretches end in the rasterizer) take a sibling's
cost.
## Phase 3, the scene program: the rest of the jet bike race (`$7E:3B48`-`$7E:3F13`, `$7E:357B`, `$7E:4D0D`, `$7E:51D1`)

The second chunk of bank `$7E`: what the first left at its edge (history
file, "Phase 3, the scene program: the jet bike race running"), so that the
race is C from its setup to its end.

**How it was chosen.** The race's remainder, all reached by
`events-jetbike-race`: the bikes placed in perspective (`$7E:4D0D`, with
`$7E:4D4E`-`$7E:4F53`), the perspective table (`$7E:357B`, with its patched
call's two targets, the race's `$7E:45B0` and the endings' `$7E:9B46`), the
main loop's scaling of the racers' pictures (`$7E:51D1`-`$7E:5220`), and the
setup (`$7E:3B48`-`$7E:3F13` with the bikes' start `$7E:45D6` and the
divisors' clear `$7E:4309`) ported whole, which retires the eleven entries
the first chunk had registered a block at a time (the buffers' clear, the
flat layers, the racers' map and the loader's eight blocks). With it the
curtain the race without its panel ends behind (`$7E:3D72`, a task no movie
runs). Left at the edge: the program's unpacker by resource (`$7E:3691`,
before the decompressor) and its first script step (`$7E:3749`, before the
sequencer), both call-outs of the setup; the curtain's step (`$C3:000E`, the
field's, still the original's); and the ending shared by modes 10, 11, 14
and 15 (`$7E:8538` on), which is a loader of the same kind and the next
chunk (PLAN.md). `$7E` stands at 11.4 KiB ported and 1.2 KiB inlined, 54%
left (59% before).

**What it is.** `scene/perspective.c` is the Mode 7 plane's perspective for
any scene: each screen line's matrix, the camera's cosine and sine times its
height over the line's distance, from the scene's first line (the routine the
scene's setup writes into the program's call: the race's from the horizon,
the endings' from the line above it) to the screen's bottom, a line at a time
or in pairs. The loop's own operands (the products, the branch over the
negation, the stores' tables) are variables in `re/ram.toml` and left as the
loop leaves them; the steps and starts other scenes write (modes 1 and 3-8,
which no movie reaches) are refused by the guard, so the original builds
those. The race's camera check now builds it as C rather than stopping at it.
`scene/race_projection.c` places the bikes: their places along and across the
track turned by the camera's angle onto the plane, then each in perspective,
the farther first: its distance ahead of the camera, the divisor kept for that
distance (or searched for, from the last one found, through
`scene_find_divisor_from`), the line under the horizon, and from the line the
picture's size, height and width and the place across the screen. The main
loop's scaling (`scene/race_matrix.c`) hands a waiting request to the bitmap
scaler and, after the upload, gathers the picture: its four entries are the
main loop's call (`$7E:51D1`), the two returns into it after each racer
(`$7E:51DF`, `$7E:51F0`) and the gathering after the upload's wait
(`$7E:5215`). The setup (`scene/race.c`) is a list of the race's graphics,
each unpacked and sent where it goes, the racers' pictures (Crono's in two
parts, one's blocks placed in the other, Johnny's mirrored from them), the
buffers, the per-line effects, the bikes' start (`race_start`,
`scene/race_rules.c`), the scripts and the race's song. Where the original's
calls stay, `scene/bridge.c` holds the machine: the scaling routine's frames
and data bank under the scaler's first row, the inlined loader's frame and the
song routine's under their call-outs.

**The registers, settled first.** Liveness has A's high byte and Y live
after the placing (a frame task, as the first chunk's), and the carry after
the main loop's call; the setup's callers read the overflow and the data
bank. The placing hands its entry the bike it placed last and the distance it
looked up, from which the wrapper leaves A (that bike's last edge, or
Johnny's mark where he is gone) and Y; the perspective's registers are worked
out from its tables after it; the main loop's carry is the last request's
compare, worked out from the state, or the scaler's, untouched; the setup's
overflow is the matrix code writer's, clear. The call-outs pass what their
callees read: the unpacker its resource with the direct page, the data bank
and the overflow the original has there (`$2100`, `$7F` and clear while the
pictures are worked on, kept on the stack around the third part's
unpacking), the first script step Y as the divisors' clear leaves it.

**The carries.** The placing never clears the carry between its sums either.
Those whose carry is the same wherever the horizon is above the plane's top
(the gap's borrow, the screen line's unit) are plain arithmetic, a bike's top
the line plus the horizon less the plane's top, and the entry's guard keeps
the horizon there (the race has it at $38 over $18). The rest move a bike by a
pixel or a distance step and are rules with one term each, worked out in one
section, "The units the placing hands on", from the values the sums before
used: Johnny's place across the plane takes a unit where Crono's place down it
wrapped, and his place down one where his own place across did; a bike's
distance takes a unit from the order's comparison (the first) or from the
first bike's last edge running past the screen's coordinates (the second), and
is a step farther where the camera's distance less the place wrapped; Johnny
is a pixel to the right at his nearest (a size of 1). The rotation's products
are plain arithmetic (the low word of the signed product), the program's own
multiplies timed with the placing. What the last bike leaves in the scene's
working cells is written once, plainly, after the placing: the field's
controller read takes those cells over. The wrapper rebuilds A from the bike
that comes last and Y from the distance it was looked up at, which the placing
notes as scaffolding; the drawing, the next task, hands Y on where it draws
nothing.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches, none refused: in `events-jetbike-race` the placing 2,640, the camera
check (now building the perspective) 2,640, the perspective's own entry 66 (the
camera script calls it), the main loop's call and its two returns 474 each
(2 and 1 interrupted), the gathering 948 (372 interrupted), the setup 1; the
perspective 1,521 in `endings-beyond-time` (1 interrupted). Shadow mode found
the setup's scratch the shadow compares at its call-outs: the block copy's row
count in the scene's first cell, the mirroring's offset and count at the start
of the unpack buffer, and the work RAM port's bank (the one bit the mirroring
writes, then the unpacker's `$7F`); fuzzing the setup found the curtain's
window on BG4 and the color window as well (the race without its panel, which
no movie runs). The gate's shipping-mode run with `--scaffold-reads` found the
placing's working cells read after the race: the scene's cells share their
page with the controller's (`input_held`, the repeat delay, the last frame's
buttons), so what the last placing leaves there is what the field's first
read of the controller takes over, and those writes are plain. The placing's
fuzz runs keep the horizon above the plane's top, as the guard asks. Fuzzed from the `race` state with the program kept, seeds 1
and 2 (`tools/fuzz_routines.toml`), 0 mismatched everywhere: the placing as it
comes and with the divisors forgotten (Johnny shown or gone; under 2% refused,
a camera height of `$FF`, where the search never ends, or nothing ahead of the
camera to look up), the perspective on channel 0 with the race's start (about a
quarter refused, a horizon off the screen or an odd count of paired lines) and
on channel 1 with the endings', the scaling's four entries, the curtain's task
(the curtain's state as its setup makes it), the setup 300 states (the driver's
echo looped back), and the first chunk's rewritten tasks again.

**Timing.** The new rows were calibrated over the library (the scene's rows
only). Two entries carry timing exceptions (`tools/timing_exceptions.txt`):
the perspective, which builds up to 224 lines' matrices in calls longer than
vblank, writing only RAM (vblank-tail), and the setup, which runs with the
display blanked and the frame's interrupt off, its error moving when the race
starts (vblank-worst, worst); the camera check's exception now says it builds
the perspective, timed as the perspective's own.

## Phase 3, battle: cuts, bands and the screen's start (`$CE:E000`-`$CE:EF10`, `$D1:F1B0`, `$D1:F9E7`)

What `python3 tools/burndown.py --uncovered CE` and `--uncovered D1` listed
after the screen script language: all but one routine of bank `$CE`'s code,
the battle's screen setup `$D1:F1B0` and the window quadrilaterals'
rasterizer `$D1:F9E7`; and the screen script commands that called into them,
which became rows of the language once their callees were C. `$CE` went from
97% left to 5% (the one unrolled copy nothing reaches), `$D1` from 17% to 0%.

What it is, in the game's terms:

- **A window cut through the middle** (`battle/screen_divide.c`, command
  `$97`, `$CE:EA7E`): the side of a line through the screen's middle at an
  angle a channel keeps and turns, drawn down one half from where it leaves
  the screen's edge (the ROM's table of those points, `$CE:F28E`) and turned
  about the middle for the other; a frame at a time it sweeps round like a
  clock's hand. `$B2` (`$CE:E000`) carries a window's shape over into the
  coming frame's buffer. The line's columns are `fallen * across / down`: the
  original's byte-wide running remainder comes to exactly that wherever its
  sum fits a byte, which the guard of its own entry asks.
- **Bands between positions** (`battle/screen_between.c`, `$AC`, `$D9`): the
  direction between two animation positions, measured in eighths through the
  ROM's table of slopes' angles (`$CE:E7CF`), and a band of window
  quadrilateral along a pair of them, its half width the variable that sizes
  the circles.
- **The quadrilaterals' rasterizer** (`battle/screen_beam.c`, `$D1:F9E7`):
  each side followed down its stretches of lines in 8.8 columns, the two
  sides' steps one running sum whose carry moves an edge a column
  (`step_down_a_line`); the work RAM port it wrote through, and port 3 its
  word stores spill onto, are scaffolding now (`hw_port_leave_spill`, a new
  call in `cpu_io.h`). The beam (`$F4`) became a row with it.
- **The line effects' arrangements and the ripple** (`battle/display.c`):
  `$CE:EE6E` lays the nine per-line tables out for one of the ROM's 32
  arrangements, and `$CE:E18E` steps a rippling background's BG2 sway; the
  frame work, the line handler and the setup call both as C. The circles'
  copy `$CE:E7C8`, an unrolled run the circles' drawing jumps into partway,
  is a loop over the run's lines (`battle/screen_shapes.c`).
- **The screen as a battle starts** (`battle/screen_setup.c`, `$D1:F1B0`):
  the handlers, the line handler's line (`tm_set_line`), everything the frame
  work, effects and messages keep starting again, the battle window's lines
  for both its layouts, and the arrangement; some 30 cells it writes were
  given names in `re/ram.toml`.

**Narrowed.** `$CC` measures along cells the dispatch's index for it reaches
past the positions (the battle's stats), and `$DA` calls the triangles'
rasterizer from inside its handler; no script of the game's has either, and
both stay the original's (their callee `$CE:E7CF` is C). The setup's
call-outs are the start settings' copy `$CD:0C9D`, the message tiles' clear
`$CD:0235` and the music's start `$C0:0008`, whose frame of the data bank,
page and flags the setup keeps on the stack as the original does.
`$CE:E1C7`-`$E292`, an unrolled byte copy nothing reaches, isn't code any
port stands for.

**The third trap, settled first.** `tools/v_readers.py` and the don't-care
check over every call-out and entry: the line handler's alternating-frame step
(`$D1:FE4C`) handed its caller the carry and overflow of the arrangement's sum
for `$CE:EE6E` to carry back, and liveness has all of A and the flags dead
after it, so it now declares them; the window slide's first step reads the
arrangement's Y (the end of its block moves), which the frame work's bridge
leaves (`battle_leave_arrangement_registers`). The direction's work cells
`$23`-`$2A` lie over the turn slot's high byte, the party's defeat and the
random cells; they are written plainly, as the original leaves them
(`battle_direction_work`, a `reuses`).

**What shadow mode found.** The direction's entry returns A as the sign of
the run down rather than the direction on one of its four ways out, which its
only caller overwrites: `DC_AL`. The circles' worked edges run past 224
lines (the run starts partway in), so `battle_circle_edges` holds 256.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the cut
8,004 calls and its command 8,062, the carrying 11,546, the bands 2,482 and
1,690 (the direction 4,172 from the original's own handlers, the rasterizer
4,172), the circles' copy 49,528, the ripple 141,044, the arrangements 3,233,
the line handler's step 216,480, the screen's start 31; the beam (`$F4`) no
movie reaches.

**Fuzzing.** Seed 1, from the `battle` state
(`tools/fuzz_routines.toml`): every new entry 0 mismatching, 20,000 states
where nothing is refused. Fuzzing found three things the library never
showed: the setup called the map layers' scroll, a JSL, as a JSR's sibling
(`battle_run_long` now: until then shadow mode counted every setup as
interrupted, compared none, and the next run found the flags the scroll keeps
on the stack, which the setup now leaves as the original's last store does); the
arrangement's and the ripple's indexes take A's high byte along, which the
game always has clear there, so their entries' guards and the window slide's
and the line handler's ask it; and random corners reach past the window
table on an odd frame, which the original writes on into the window's
registers' images: the quadrilaterals' entries and the commands that draw
them refuse that (`screen_quad_fits_the_table`), and no real call is
refused.

**Timing.** The new entries were calibrated from the shadow runs; the beam,
which no movie reaches, takes the quadrilateral's cost. The cut is in
`tools/timing_exceptions.txt` for the vblank's limits, with `$97` and the
command dispatch it runs under: the original's time follows the line's run
across, a step of its loop a column, where the port works each line's column
out at once (5.4% error, at most 19,014 clocks, writing RAM; the vblank check
finds no video write moved).

## Phase 3, menus: the save code and the rest of bank `$FF` (`$FF:F548`-`$FF:FBE7`)

The whole of bank `$FF`'s code but one routine nothing reaches: 1.6 KiB in 11
entries. `$FF` went from 0.1 KiB ported (95% left, 1 entry) to 1.5 KiB (10%
left, 12 entries); what is left is `$FF:FF3E`-`$FFF4`, a menu record's routine
behind a return that the analysis finds no caller of (docs/TODO.md).

**What it is.** Five things that share a bank:

- `menu/saves.c`, the save code: a slot's check (`$FF:F9C4`), the save
  (`$FF:F9FB`), the load (`$FF:FAA3`) and a New Game+'s load (`$FF:FAED`),
  with the sum (`$FF:FBE7`) and the slots' places (`$FF:FBD3`) inline. A saved
  game is the eight runs of work RAM the ROM's table lists, one after the other
  from the slot's start; the save marks the slot and keeps a sum beside the
  mark. Save memory is reached through the save service only (a guard's look
  through the new `sv_peek8`), and the layout and the sum are kept to the bit,
  so a cartridge's saves load and the original loads ours. The menus'
  opening, the load screen and the save point call it as C now; the New
  Game+'s load is a bridge, since equipping each character's starting
  equipment and filling HP and MP are the menus' services (`$C2:8004`), still
  the original's.
- `menu/techs.c`, the party's techs as the menus open (`$FF:F813`): how many
  members the screens offer, the healing techs marked, the techs listed of
  every group whose characters are all present, each character's next single
  tech, each character's part in each double and triple tech, and which of
  the rocks' triple techs the party could do (`$FF:F958`, which the services
  also run). The parts table is now declared where it starts (`$7E:1640`, a
  row a character), not from below it.
- `menu/digits.c`: a screen's small number tiles, two digits to a tile
  (`$FF:F548`), and the items screen's amounts' digits (`$FF:F7D9`).
- `menu/sprites.c`: a character's sprite colours, dimmed or not (`$FF:F628`).
- `menu/gate_screen.c`: the End of Time's gate screen, its swirl drawn
  (`$FF:F677`) and turned every frame (`$FF:F716`), with the screen's start
  (`$C2:CE9D`) moved out of `epoch.c`, which it was never part of.

**The registers, settled first.** The check hands its answer back in A, a
real result. The loads and the swirl leave A's high byte to the menus' exit
and the screen's task: the wrapper derives it (the load's is clear, the
swirl's the last band's scroll); their low bytes are declared dead. The New
Game+'s calls to the services can't drop A's high byte (the equipping stores
it where an item is no equipment), so the bridge leaves the original's, the
high byte of the character's starting record's place, in the one scaffolding
call that also leaves the multiplier and the record's place; the flags the
services save and restore are declared dead, which liveness agrees with. The
check runs on its caller's page, the menus' or the clock's (`$0400`, the
power-on search), a new page kind (`save_check`).

**What shadow mode and fuzzing found.** Over the 26 movies every call matches and
none is refused: the check 152 calls on both its pages, the techs' noting 37
and the rocks' 590, the swirl's turn 270, the load 19, the amounts' digits 6,
the save 2. The New Game+'s load and a character's colours are reached by no
movie, and the number tiles' and the swirl's drawing once each, interrupted;
for those fuzzing is the check. Fuzzed from the `menus` state, seed 1, 2,000
states each (500 for the swirl's drawing, which unpacks its graphics every
time), 0 mismatching. The first runs found what shadow mode couldn't: the
number tiles' start tiles shifted a bit short (their only call is
interrupted); the New Game+'s count of a character's equipment stored a word
wide; and three inputs the game always gives the same, now the guards': the
menus' zero word, which the save code and the tiles start their tables from,
the high byte of the word the triple techs' parts are counted in, and a
palette change count with room. The rewritten callers against main's
(`tools/fuzz_compare.py --before`, seed 1): the load screen's steps, the slots'
reading, the items' tables and the gate screen's start match the same states
as before; the techs screen's member steps now refuse the fuzzed states whose
list holds techs of another kind than the one it shows (no game state), which
read the parts table from below its start.

**Timing.** Each new entry is calibrated on its own calls (`menu_load_game`
25141, `menu_note_party_techs` 17807, `menu_note_rock_techs` 11016, the rest
between 3400 and 6780), and the callers that now run them as C carry their
time tagged as theirs (`TIMED_AS`): `menu_open` went from 15116 to 4737 and
`menu_read_save_slots` from 3477 to 27720 as the work moved between them. The
runs copied to and from save memory are charged at the block move's rate, as
the original moves them with it. No exception was needed; the two entries no
movie reaches keep the default.
# Phase 3, chunk by chunk

The per-chunk record for the three big banks -- the menus, the field and
events, and the battle -- and for the scene program in bank `$7E`, as each
chunk of routines became C. The newest section
stays in docs/REIMPLEMENTATION.md as the shape a new one follows (the field's and the
battle's, at the split; the menus' next chunk will keep its section there until
the one after it lands).

Split out of docs/REIMPLEMENTATION.md, which keeps the mechanisms, the current
phase and an index of these files.

## Phase 3, menus: the load screen, the window language and the opening

The first chunk of Phase 3's bank `$C2` wave: the screen every game starts
from, and what it is built with. `python3 tools/burndown.py` for `$C2`: 17.2
KiB ported and 71% left with 173 entries before, 19.8 KiB and 67% left with
216 entries before the opening's two came out (TODO.md); about 2.2 KiB of new code in 41 entries, and about 0.7 KiB of
earlier ports (the save screen's slot, `menu_window_set_line_effect`, the
sprite reset) rewritten onto what the chunk built. All of it to the bar:
`game/menu/` scores 0.03 operations a line with 35% of its functions on the
machine, and none of the new files is on the baseline.

**The files, by the game.**

- `windows.c`: the window command language every menu screen is drawn with
  (the interpreter `$C2:ED31`, its eleven commands and their handlers). The
  commands are one C table (`LANGUAGE`) that decodes operands into places,
  sizes, styles and strings, and names the handler whose time each command's
  work is charged at; a window style is the tile its twenty tiles start at,
  in the player's design or the one a list names. The frame, the inside's
  four-by-four pattern, the colours of a design and the label strips are
  functions the load screen and the save screen call directly.
- `cursor.c`: the cursor's reading of the pad (`$C2:E984`: a push moves it to
  the place's neighbour in that direction, the buttons it reports), the
  sprite table's reset under it, and the menus' sounds (`$C2:EABA`-`$EB03`,
  through the audio service with the original's three-frame wait left to the
  driver's call).
- `load_screen.c`: screen 14's steps after its opening (choosing, which game a
  New Game+ carries on from, and the erase question) and a slot's row in the
  design its game was saved with. The opening step itself is still the
  original's (below).
- `save_screen.c` gained the slots' reading as the menus open (`$C2:D156`):
  the save code's check of each slot, the summary read from the cartridge
  through the save service, the chapter's and the slot's name labels; and
  `saves.h` is the cartridge's record of the three games (the slot saved
  last, times finished, whether the game has been finished, each slot's mark),
  which also retires `menu_saves_finished_game`'s read of save RAM through the
  ROM accessor.
- `opening.c`: what the menus do each time they open (`$C2:8096`): take the
  interrupts, quiet the sound effects, blank the screen, read the slots, list
  the party, unpack the settings. Its graphics, sprites and items are still
  call-outs (TODO.md).
- `bridge.c`: the menus' one way to call the original's code
  (`menu_call`, `menu_call_long`, and `menu_call_returning` for a callee
  that comes back with widths of its own), `menu_run` for a sibling port run as
  the original's `JSR` reaches it, and the frame of a routine a bridge stands
  in for. Unlike the battle's and the field's, the menus' routines change
  widths from routine to routine and save their caller's with `PHP`, so a
  call's check is the widths, the page and the data bank it was made with.

**A liveness hint for the task switch.** The menus' tasks yield with
`JSR $C2:821E` and wait with `JSR $C2:822B`, which save the caller's
registers on the task's own stack and return on another task's; the pairing
of pushes can't follow it, so every register was live after every call to
them and no mode handler could declare anything dead. `re/hints.toml` now has
`[[keeps]]` (DISASSEMBLY.md): a call that comes back with the listed
registers as its caller had them, reading only `reads`. Liveness takes the
call as that summary (`tools/disasm/liveness.py`, `_kept_summary`), and
`tests/test_disasm.py` checks it on a task switch built in the test.

The first cut of the hint was wrong about DB, which the review found: neither
entry saves or restores it, and the scheduler resumes the next task with the
yielding task's DB, setting it only after every task has had its turn
(`$C2:81C1`). The hint had DB dead everywhere, which let a false `DC_DB` on the
load screen's yield past the don't-care check, and five entries' declarations
with it. The hint now names DB in `reads`, and `tools/disasm/keeps.py` checks
every `[[keeps]]` hint against the code: the entry pushes exactly the kept
registers, the hint's `resume` pulls them back in reverse before it returns,
and every register the entry doesn't save that some path to the next task
reads before setting, or doesn't set at all, is in `reads`. The don't-care
check runs it, so a wrong hint fails the gate; `tests/test_disasm.py` has a
switch built in the test that a wrong hint of each kind fails on. The check
takes `$822B`'s `XBA` as reading both bytes of A, so its hint names `AH` as
well. The six declarations are the smaller sets liveness now gives.

**Found by shadow mode.** The cursor's reading leaves its loop's bit in the
menus' first cell; a label strip steps its tile after setting bit 4, not
before; the list handlers that run with any accumulator width (`$C2:F378` is
reached with a 16-bit one from `$C2:C773`); `$C2:968D` comes back with an
8-bit accumulator; the slots' reading leaves the slot count at 3 and the time
played's cell holds the game's start as the bus addresses it. A guard that
read save RAM through the service cost the machine 8 clocks and moved the
generated code's timing: guards peek (`snes_bus_peek`).

**Found by fuzzing** (tools/fuzz_routines.toml; PORTING.md has the counts):
the settings' clearing clears a byte past the last setting; the slots' count
starts from the menus' zero word, and the gold a summary hides is written
from it; the summary reads the checked slot's game from the cell the save
code leaves (`menu_slot_game_at`), not from the slot's number, and steps
through the party a byte at a time within its page; a time played's digit
past 9 carries through the original's shifts that multiply by ten, which no
clock of the game's makes, so the time's guard and the save screen's (whose
header draws the cartridge's time) take only decimal digits and the port
multiplies by ten; a window list's
string reads the number conversion's zero word, so the save screen's guard
now asks for it too; the header's "taken" count is a byte; loading a design's
colours counts a palette change a word wide, so the drawing's guards leave
room in the count; and several entries were refused widths the game never
calls them with (the index registers 16-bit), where the original loops. The
save screen's rewrite against its predecessor (`tools/fuzz_compare.py`): 0
mismatched of 2,000 both before and after, once the summaries' names are
letters, as the game's name entry makes them; with control codes in the
names, the strings drew through the text port's accepted divergences (TODO.md).

**A port that spans frames.** The load screen's opening (`$C2:E613`, which
draws the whole screen through `$C2:E7C3`) was ported and matched wherever
shadow mode could compare it, but every call of it in the library is
interrupted, and in the shipping mode it broke the `beyond_time` route: loading
the Final Battle save, the screen never appeared and the game ran into a `BRK`
two frames later. Its drawing takes the original more than a frame, with the
menus' vblank handler taken inside it; as one port, the time is owed at its
return and the handler taken there, twice, and with the time charged cheaply
(an experiment, not a fix) the screen appears. The route passes with the two
entries left to the original. Why it went wrong was the runtime's: the
debt was paid inside the menus' vblank handler, which moves the stack, and
the next vblank nested in it (fixed; "The tooling pass after Phase 3's first
chunks", below). The port came back after that fix, unchanged but for
`$C2:E613` stopping with `m_stop_at` at the task start it jumps to
(`$C2:83B2`), which starts the screen's fade-in: `beyond_time` passes in the
shipping mode, and both entries are fuzzed (2,000 states each, none
mismatching), which is their only check, since every call of theirs in the
library is interrupted. For the same reason the timing gate has no stretch of
theirs to calibrate from: their `cost_milli` is left at the menus' ports'
usual 5000, and nearly all their time is their callees' (the window lists,
the slot's drawing and showing), each at its own calibrated rate.

**What the effects check found** ("The tooling pass after Phase 3's first
chunks", below): reads of cells the menus' ports wrote only in scaffolding.
The load screen's member rows write the row and the member the portrait's
own entry (`$C2:D466`) reads as plain state, the member as a byte of its own
(`menu_scratch_save_member_character`), as the original's `STA` does; a
window's frame leaves the inside's size as plain state wherever it is drawn
(the frame's entry, a list's window with or without its inside, the save
slot's window `$C2:ECDB`); the slots' rows keep the next label's first tile
plainly. `menu_object_wave_hdma` reads a phase another subsystem's scaffolding
leaves in the field page's cell, as the original's does; that one has a
reason in `tools/effects_baseline.txt`.

**Timing.** Every entry is calibrated from the library but the ones only
`menu_open` runs, whose error its own rate takes up (TODO.md), and a window list charges each command at its handler's
rate (the handlers are in the `LANGUAGE` table: the first cut took their
addresses from the ROM's table without their bank, so their time went to no
hand port). The inside's pattern is drawn a row and repeated with block moves
as the original does, which is where its time goes. Three entries are
exceptions (`tools/timing_exceptions.txt`): the list interpreter and the
string command carry the text engine's spread in vblank, which the text port
already has. In the shipping mode the menus' vblank work lands on line 41-44
once in each of four save movies (two picture lines on one frame;
`tools/vblank_check_exceptions.txt`).

## Phase 3, menus: the typesetter, the graphics and the items

The menus' second chunk: the engine every string of the game's text is set
with, and the rest of the menus' opening but for the characters' sprites.
`python3 tools/burndown.py` for `$C2`: 19.8 KiB ported and 67% left after the
first chunk, 22.8 KiB and 62% left after this one, 226 entries.

**The typesetter** (`menu/typeset.c`, `$C2:57DF`-`$C2:6262`). The dialog, the
battle's messages and the menus' labels all set their text with it: a caller
names the string (a table and a number), the tiles and their layout, puts the
string's numbers and names on the text page, starts it (`$C2:57DF`) and then
calls it (`$C2:5823`) with a budget of glyphs, a glyph or two a frame for the
dialog's typing. A call stops at the string's end, a wait, a line or page
break the caller acts on, or the budget's end, and says which; where it has
got to lives in the text page's cells (`menu_text_reading`), so the next call
takes it up in the middle of a word, a name or a number. The string's bytes
are glyphs (and the font's two-byte half), words of the dictionary, and
control codes: the breaks, the numbers (decimal without leading zeros, or hex
where the page's hex setting is on) and the names (a character's, the
party's, an item's, a tech's, an enemy's). The glyph drawer (`menu/glyphs.c`)
now draws from parameters, into four- or sixteen-colour tiles, and its three
entries take them from the page's cells as before; the menus' label setting
(`$C2:F90C`), which sets a label a line of 32 glyphs at a time, is a port too,
taking the label's table, string and tiles and giving back its width, and the
load screen and the save screen call it as C.

What a call leaves in X and Y nothing reads, and the entries declare them
dead. For a while the analysis had them read after the label setting: the
save screen's slot rows end in a `JMP` into the menus' task yield
(`$C2:CF5B` to `$C2:821E`), and the yield's `[[keeps]]` hint covered only
calls to it, so the analysis followed the yield's own code in the handler
and took its `PHY` for a read. Liveness now takes a jump into a routine with
a `[[keeps]]` hint as a tail call with the hint's summary, where the jumping
routine returns by `RTS` with nothing of its own on the stack, so that the
switch's `RTS` is the routine's own (review asked for that condition; the
screen handlers the dispatch enters, which return by `RTS` alone, count)
(`tools/disasm/liveness.py`, tests/test_disasm.py, DISASSEMBLY.md), and X
and Y after `$C2:57DF`, `$C2:5823`, `$C2:F90C` and `$C2:D296` are dead. Over
the whole tree only ten declarations' accepted sets changed, all the menus'
(the glyph drawer's three entries, the cursor's input, the slot rows', the
label setting's, the chapter label's and the typesetter's two), each growing
by X or Y. Before that the port worked the registers out as scaffolding: a
first version passed a small `Ending` back through the reading functions,
which review rated a translation (it told apart only what the original's
registers do), and a second read the string again in `menu/leaves.c`, now
gone.

Two things the gate made the port keep. A name past the pointer table's seven is read
through the table's neighbours, which can put it on the text page itself: the
original counts the name's glyphs with the insert already begun, pointer and
source set, so the port does too (fuzzing found it: a party member with an id
past the characters'). And a call keeps its budget in hand, where the
original counts it down in its cell on the page: the two part only where a
call reads that cell (as a string, an argument or an insert: such a name,
more than 19 arguments' bytes, a pointer onto the page) or a label long
enough to wrap its bank draws over it, so the guards read a text ahead,
a piece and its arguments at a time, and turn those away (fuzzing found a chapter label that differed
before they did).

**The graphics** (`menu/graphics.c`, `$C2:968D`, `$C2:C511`). The tile sets
every screen draws with, built in work RAM from the ROM's graphics and sent
to VRAM as the menus open: four-colour tiles laid out as sixteen-colour ones,
three-plane tiles given a fourth, extra planes made from the ink of the ones
there, then the menus' colours and the window design the player chose (which
the settings screen loads on its own too). The display's queue sending, its
palette, and its two transfers into work RAM became functions on the game's
state (`menu_send_upload_queue`, `menu_send_palette`, `menu_clear_memory`,
`menu_copy_rom_to_memory`) for this.

**The items** (`menu/items.c`, `$C2:9875`, and the lookups `$C2:8881`,
`$C2:88AD`, `$C2:88D8`). As the menus open, the inventory loses the places no
item fills, the story's and the place's say in what the item lists offer is
noted, and every item's kind, price, users, amount, attack or defence and
name place is copied into the menus' tables; the save code's list of what the
party carries (`$FF:F813`) and each member's figures (`$C2:99B2`) are still
the original's, called from the port.

**What was left of the opening** was the characters' sprites (`$C2:F3CA`),
which the next chunk took (below).

**Found by fuzzing**: the names' count on the text page, above; a number's
leading zeros are shifted out of its digit cells as the original shifts them,
leaving the cells past the number as they were; the lookups leave Y's high
byte clear (they run with 8-bit index registers). **Found by shadow mode**:
the lookup leaves the category in A; the item tables leave the second place
note's test and item $C8's kind in the flags the save code's list saves; the
graphics leave the tile loops' counts in the menus' first cells, and the
flags of the design's setting.

**The effects check** found the glyph drawer's entries reading cells only
the original's setter writes now; the baseline gives the reason (only the
original's code calls those entries). The queue's count is written a word
wide as game state, since the byte after it aliases the sprite-object mode's
slots.

**Timing.** Calibrated from the library. `menu_text_go_on` and
`menu_set_label` carry the glyph drawer's spread (the pen's offset decides
its shifts), and are timing exceptions like `menu_draw_text`. The graphics and
the items run once as the menus open, with the screen blanked.
The new timing moved the `bucket_lavos` route's Lavos Core fight onto a
frame where the route's bot, backing out of a tech list it had opened
before the list took input, held B across its four presses, which the game
reads as one: the list stayed open and the bot, waiting for the command
menu, waited for good. `tools/movies/bot.py` now releases B between presses,
and the three long routes pass in the shipping mode.

## Phase 3, menus: the characters in the records

The menus' third chunk: the characters the menus show, in the records the
screens draw their sprites from (`menu/sprites.c`, "The characters the
records show"). `python3 tools/burndown.py` for `$C2` (as main's tree counts it
after the battle's second chunk): 21.5 KiB ported and 64% left after the
second chunk, 22.3 KiB and 62% left after this one, 231 entries.

**What a record shows.** A record ($40 bytes at `$7E:1800` on, the direct
page its routines run on; nine of them, the ninth `$1A00` only marked empty)
names a sprite of one of three sets, the playable characters' and the
others'. Its load (`$C2:F422`) looks the sprite's assignment up in bank `$E4`:
where its graphics and its frames are, its colours, its animations and its
size (one, two, three or six blocks of 32 by 32 pixels). It puts the twelve
colours in the record's sprite palette and marks the palette changed; and for
all but the playable characters, whose graphics the ROM keeps as they are, it
has the unpacker put the graphics at `$7E:B000` + `$800` a record, a call to
the decompressor's port from the load's entry, the one call-out the chunk
keeps. The opening (`$C2:F3CA`) loads the first seven records with the
characters in their standing animation facing the viewer and marks the last
two empty.

**The animation** (`$C2:F69D`, and `$C2:F69B` a frame on, which the records'
task runs every frame) is a list of frames for each of four facings, each
frame held for a time; an animation's bit 7 has it loop from its loop frame
rather than its start, and one with no frames is left past its end. A change
of frame (`$C2:F75C`, registered so it is timed and fuzzed alone) copies the
frame's sprites' offsets into the record and builds its tiles into the frame
buffer at `$7E:8000` (a blank tile, a tile of the graphics, or one mirrored
left to right through the ROM's table of reversed bytes), `$200` bytes a block
past the blocks queued this frame, and queues them for VRAM: an even number of
blocks in one upload, an odd number a block's two rows of tiles at a time. The
records' task holds a record's next frame back once a frame's blocks reach its
limit (`menu_record_block_limit`).

**Kept for the original.** The frame's tile list, which the original copies
over the save summary and reads back as it builds the tiles, is scaffolding:
the port reads the frame's data in place. So are the block move's stub (its
source bank), the multiplier the original looks the assignment and the frame
up with, the upload record at `$7E:9A88` and the frame's loop counts in the
menus' first cells. The effects check found the save screen's readers of the
save summary meeting the tile list; the baseline gives the reason (the save
screen copies the summary there afresh before it reads it), and the frame
building's own entry, which takes the tiles where the original's animation
leaves them, the same. Over the shipping-mode runs, an upload the original
builds over the save summary is queued with its unused eighth byte, which
the tile list left: nothing reads the queue's eighth bytes (baseline). The opening counts its records in the time played's
hours cell (`menu_opening_records_loaded`), which the save screen's header
then takes for a time it has drawn: written plainly.

**Guards.** The frame buffer, reached through `wram.h` since its place is
worked out, must end clear of the block move's stub (`$7E:9690`); a sprite
has at most three blocks (six-block sprites' offsets would run past the
record, and no menu shows one); a record's pointers name banks that are ROM
or work RAM throughout; the facing is one of four; a looping animation's loop
frame has a time; the VRAM queue counts whole records; a record's tiles
start on an upper row of tiles in VRAM, where its frame's blocks go a row
pair at a time; a record's sprite is of one of the three sets; and a facing
has fewer than `$4000` frames. None refused a call over the library.

**Arithmetic.** The original's sums of an animation's place carry a flag
from one add into the next (a facing's place is a multiply by three with its
shift's carry; the times' place takes in what the division by four left).
The port writes them as plain sums: they can't carry for any sprite the
ROM's tables have (`tests/test_menu_sprites.py` reads every entry: every
span of frames is whole facings, fewer than `$4000` a facing), and the
guards keep the records' own cells within the same bounds. A review of the
first version rated it a translation for writing the carries out.

**Shadow and fuzzing.** Over the 26 movies every call compared, none
mismatching: `menu_load_record_sprites` 37, `menu_load_record_sprite` 262,
`menu_animate_record` 576, `menu_step_record_animation` 317 (the frame
building runs inside them as C, its time its own over 571 frames). Fuzzing
found the VRAM queue's count
unaligned (the queue's own entries refuse it; the records' guards now do too)
and pointers into banks the I/O registers share (the tiles read open bus
there), and the frame building's own entry, which copies with the stub the
animation leaves, needs the stub in place (its guard asks it).

**Timing.** Calibrated from the library. The port fills and copies a
frame's tiles itself and charges the bytes at the block move's rate; a
mirrored tile it builds a byte at a time. A frame's time follows its mix of
blank, copied and mirrored tiles, which that leaves to calibration (a first
version charged per-tile cycle estimates, numbers review took out): a mean
error of about 6-7% and at most 11,300 clocks, so the frame building and the
two animation entries are vblank-tail timing exceptions
(`tools/timing_exceptions.txt`), writing work RAM and the VRAM queue only. The block move's own rate
(`memory_move_stub`) was recalibrated: the animation's copies no longer go
through it on the machine, which changed the calls it is measured on.

## Phase 3, menus: what the menus' ports called most

The menus' fourth chunk, picked by value rather than by run: a profile of
`--native on --generated off --misses` over the menus' movies, the interpreted
instructions summed by the function they are in, put five routines the
existing ports call out to at the top once the task scheduler (`$C2:821E`
and its neighbours, about 9%, left for fibers) is set aside. The window
helpers at `$C2:EC38`-`$C2:ECDA` took about 12% together, the records'
task (`$C2:F657`) 6.7% and the menus' per-frame input (`$C2:8473`) 4.9%.
`python3 tools/burndown.py` for `$C2`: 22.3 KiB ported, 62% left and 231
entries before; 22.6 KiB, 62% and 236 after.

**The frame's input** (`menu/cursor.c`, "The frame's buttons"): the menus'
vblank takes the controller, the second controller, a frame of play time,
the held buttons' repeat pacing (a long wait before the first repeat, short
ones after, back to the long wait when nothing but Start is held; a screen
can ask for a frame of repeating every frame, `menu_fast_repeat`) and the
going-back hold (the going-back assignment's buttons with Start sends every
screen back from then on). The controller read and the play time's tick are
the shared input's parts, called as C (`core/input.h`: the read, the tick,
and where a read that didn't take the buttons stops); only where the console
is still reading the controller, or the reset buttons are held, does the
port build the stack the original has there and stop. A first version called
the two ports as call-outs, the original's saves round them rebuilt, with a
check after each that the machine came back as expected; the check after the
tick held the tick's page to the controller's, which the tick run as the
original's code leaves at its clock page (`$0400`), so the port handed the
rest of nearly every call to the original, comparing everything, and 6 calls
a movie differed in A's high byte, which is dead there.

**The records' task** (`menu/sprites.c`, "The records' task"): once a frame,
each of the nine records that shows a sprite has its animation carried on
where its frame's time has run out or another is asked for, unless the
frame's uploads have reached their share, and is drawn unless hidden; then
the task yields (`m_stop_at` `$C2:F696`). The byte after the uploads' count,
which the original clears with it as a word, is written plainly: the
sprite object mode's slots share it.

**The tilemap touches** (`menu/tiles.c`): a character's large portrait (36
tiles six by six, numbered on from its first in the menus' first cell, whose
high byte the callers set to the attribute, and its sixteen colours loaded
into the palette the attribute names after the tiles' count has carried into
it); a stretch of a row pair given one attribute; and a block of rows given
a palette and priority over their tiles' low bits, the drawing origin moving
down as it goes (`$C2:ECC2`, the one row, inlined). The counts are an 8-bit
tile count and a 16-bit row count counted down in 16-bit registers, so 0
counts the whole bank; the guards turn those away with any stretch that
would leave the bank. The row pair's callers go on with the count in A's
high byte, which the entry leaves. The portrait's word increment of the
palette's changed count (`$0D15`) carries into the next byte, which the
guard keeps short of, as the window colours' does.

**Shadow and fuzzing.** Over the 26 movies every call compared, none
mismatching or refused: `menu_take_frame_input` 35,169 (every movie),
`menu_run_record_task` 4,637 of 4,638 (one interrupted),
`menu_draw_large_portrait` 235, `menu_recolour_rows` 1,314,
`menu_set_row_pair_attribute` 2,776. Fuzzed from the `menus` state, seed 1,
none mismatching: the input 2,000 of 2,000, the task 2,000 of 2,000, the
portrait 1,893 of 2,000, the rows 1,890 and the row pair 1,987 (the rest
refused). Fuzzing found the portrait's carry into `$0D16`. The menus' vblank
work, which calls the input, is unchanged at 415 of 500 matched.

**Timing.** Calibrated from the library. The task carries the animation's
and the frame building's spread (mean error 1.2%, at most 10,872 clocks), so
it is a vblank-tail exception as they are, writing work RAM and the VRAM
queue only.

## Phase 3, menus: the equipment screen and its member list (`$C2:9C31`-`$C2:A2C5`)

The largest run the plan named for `$C2`, 1,685 bytes: the equipment screen
(the menus' screen 1), the main menu's panel for it, and the member list both
draw from, with a member's loading (`$C2:8820`) that the list and the card
need. An earlier attempt at the same run was reviewed as a translation (each
step the original's branch tree in a `Snes *` body, flag notes threaded
through every step, JSR returns pushed by hand); this one was written again
from the drafts.

**What it is.** `menu/members.c` is the member list and the card: a member's
loading, the rows (`$C2:A1B2`, a row `$C2:A216` with its face and low-HP mark
`$C2:A1EF`, an empty one `$C2:A1E9`), the rows' colours (`$C2:9E76`,
`$C2:9EB0`, through a `menu_recolour_block` that `menu/tiles.c` now exports),
and the card (`$C2:A22F`, again with the top row `$C2:A21F`): the time
played, the figures and stats (the original's `$C2:9137` and `$C2:A6F0`,
called out to), the large portrait and the element's label (`$C2:A273`, the
element now a `MenuElement` in `menu_data.h`). `menu/equipment.c` is the
screen as the game sees it, no machine in it: each step is a decision
(`menu_equip_choose_member` returns `MEMBER_CHOSEN`, `MEMBER_LEFT_FOR_THE_MAIN_MENU`
or `MEMBER_POINTED_AT`, and so on for the slots and the items) and each part
between the original's calls an operation in the game's terms (the member list
going round or sliding, the shoulder assignments switching members, the item
put on and the one taken off given back, the list of what fits paged or slid,
its rows moved). `menu/equipment_bridge.c` runs them: per step it reads the
cursor, asks equipment.c, and sequences the sounds and the card as siblings
(`menu_run`; `menu_run_any_widths` for the list's move, which leaves a 16-bit
accumulator after a slide) and the original's drawings, inventory, paging
and frame waits as call-outs, stopping where the original jumps on. The
member slide's frames (`$C2:9DAF`) are a loop of `menu_equip_slide_members_a_frame`
around the task's yield; the slide's thumb follows the step's sign, where the
original reads the line's carry (equal for the lines the slide's start
sets, which its guard holds it to).

**The flags, settled first.** Every call the steps keep was checked against
liveness before the logic was written (`tools/v_readers.py`, and the strict
solve): the yields, the inventory, the paging and the list's slide read
nothing; the drawings reached through the window language may read the
overflow and the carry. What liveness can't declare is the P byte a sibling
saves as it starts (the sounds, the card, the slots' pointing, the member
switch), which shadow mode compares under the sibling's own calls out. Three
things keep those few:
- where a step **jumps** on to a routine that is itself a port (the member
  list's move `$C2:9D17`, the slots' pointing `$C2:9ECD`, the screen's
  set-up `$C2:A11A`, the card after the move, the refusal's sound), the
  bridge stops there and the runtime enters it from the original's own state,
  so its saved P is the original's (stops checked against liveness like any
  other);
- where a routine's saved P would carry a note and nothing else in it needs
  one, the routine stays the original's: the member slide's start
  (`$C2:9D5B`, whose P is saved after the new first row is counted, under
  every frame's yield) and the member list's scroll bar (`$C2:A170`, under
  its call to the bar's task);
- what is left is one leaves helper per decision, at the end of the bridge
  file, worked out from the state the decision left (`leave_member_pointed`,
  `leave_slot_step_tested`, `leave_member_switched`, ...): the comparison or
  load the original made last before a sibling saves P, and the overflow of a
  sum before a drawing that may read it. A call-out declaring a flag
  don't-care and returning it as it found it leaves the port's own value, so
  a tick after such a call still needs the original's flags: the paged and
  the slid list's ticks and the card's stats call have their own.

The sounds' driver call now declares Y dead (`menu/cursor.c`; the driver
reads A's high byte alone, which the don't-care check confirms), which a tick
after the inventory's calls needed. `ram_map.py`'s `reuses` may name a list
(the list of items over two of the menus' cells).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches, none refused: a member's loading 1,360 calls, the rows' colouring
219 (each row 657), the card 223, the member step 216, its move 212, the
slots' step 470, their pointing and the member switch 460 each, the item
step 564 (the list's move inside it, 556), the screen's opening and set-up
2 each, the weapon's byte 8, the panel's dimmed colours 8; one row drawn
alone of 33 interrupted. The library never slides the member list, switches
members with the shoulder assignments, slides the list of what fits or
refuses a slot, and the panel's one call is interrupted; a scratch movie from
the same save (the field menu's, kept outside the repository: it slides the
members both ways, switches members, slides and pages the accessory list, puts
an item on and returns to the main menu) matched every call over 6,296
frames: 7 slides, 7 switches, 12 rows moved. It found two flags the library
didn't: the list's tick after a page turned (the paging's flags kept through
the list's drawing, which declared them dead) and the card's stats after the
member's sum. Fuzzed from the `equipment` state (`menus-field-menu` at frame
3800, low RAM, the tilemaps and the screen's tables kept and the decisions'
cells filled; `tools/fuzz_routines.toml`), seed 1, 2,000 states each, none
mismatching; compared: every state for the loading, the rows, the colours,
the element, the set-up, the member move, both slides (up to the first
frame's wait), the rows' move, the weapon's byte, the panel (up to its first
frame's wait) and its colours; the card 1,642 and its redraw 1,619 (the rest
the figures' call past the fuzzer's budget), the member step 828 and the
slots' step 958 (the slots' drawing past the budget), the slots' pointing
1,477 and the switch 1,473 (the rest the guard's and the budget's), the item
step 1,611 and the list's move 1,599, the face 1,763. The first runs found the list's paging read as its new first
row (the paging answers $FF for any turn), the element's label indexed by the
element's half (an odd table byte past the seventh character), the card and
the rows needing 16-bit index registers at entry, and a row redrawn after the
card from a record the figures' routine had left naming no character (the
bridge now hands that row to the original). One state in 1,671 of the card
differs where the original's figures leave the upload queue's count
off a record (TODO.md).

**Timing.** The new entries were calibrated over the library, with the
tiles' recolouring (which now writes the drawing origin once) and the
cursor's tick; no exception. The entries whose work is nearly all another
routine's (a row is the window language's, the colouring the rows') carry
large scalars on few accesses of their own. `$C2` stands at 24.2 KiB
ported, 59% left.

## Phase 3, menus: the items screen and the equipment screen's windows (`$C2:A2CD`-`$C2:B352`)

The rest of the equipment screen, which its steps called out to (`$C2:A2CD`-
`$C2:AA3F`: the slots window and the list window, each sliding in and out),
and the items screen beside it in the bank (`$C2:AA40`-`$C2:B352`, the menus'
screen 2, with the main menu's panel for it): 3.9 KiB, the two screens that
show the party's items. `$C2` went from 24.2 KiB ported (59% left, 261
entries) to 28.1 KiB (53%, 300 entries). The plan's `$C2:B520` is the techs
screen's, the next run along; it shares the member window and the list's
slide written here, so it is the next chunk, taken whole.

**What it is.** `menu/item_screen.c` is the screen as the game sees it: the
commands over the list (the list, the sort), the list with an item picked up
and put down elsewhere (a second cursor holds its row), the sort (the party's
items gathered, used-up places dropped, the categories in order with what the
party keeps rather than uses after the rest), who can equip a weapon or
armour (the names of those who have joined, its attack or defence), and an
item used from the menu: what it does looked up in its figures (a tonic, one
by chance, the party's rest, a tab), the member window it is used on, a tab's
figure raised in the record, one more taken from the party's items each use.
`menu/item_screen_bridge.c` runs the steps on the machine, the restoring of
HP and MP (`$C2:8F55`), the cursor among the members a tonic is used on
(`$C2:B83E`, the techs screen's too), the description (`$C2:F2F3`), the
lists' frame, paging and slide, and the second cursor's and the scroll bar's
tasks still the original's. `menu/equipment_windows.c` is the two windows:
what a member has on (the card's too, which no longer calls out for it), the
slot taken and the slots' marks, the slots window sliding in with the member
rows moving aside (and back), the list window sliding in and out, the list of
what fits laid out and drawn; their slides keep their state in a
`MenuWindowSlide` or `MenuListSlide` the bridge holds between the frames it
waits, and stage the band table a frame at a time. `menu/lists.c` holds what
the two screens' lists share: whether a list scrolls, the way it slid, and
its rows moved in their buffer once it has (the equipment's `$C2:9FDE` now
one call of it). `menu/cursor.h` names the frame's buttons and pushes once
(`MenuButton`, `MenuPush`, `menu_asked`, `menu_pushed`), which the two screens'
three private copies became.

**The flags, settled first.** Every call-out was checked against the strict
solve before the logic: all of them read nothing but memory, Y (the list's
frame) or A (the description, the tasks' stop, the members' pointing, whose
sound reads A's high byte). What liveness can't declare is again the P a
routine saves as it starts, compared under its own calls out: the screens'
ticks, and here every routine a step reaches with a JSR that calls out
itself. Those are entries of their own run through `menu_run` (the
description of the cursor's item `$C2:AD2E` and its caller `$C2:AC59`, the
use of an item `$C2:B0AB` and its four handlers, the member window `$C2:AF42`,
a tab's member `$C2:B24A`/`$C2:B275`, a tab's raise `$C2:B2A7`, one more used
`$C2:B31C`, the windows' slides), so the original's frames stand under their
calls; before each, one leaves helper per decision (`leave_use_found`,
`leave_member_choice_started`, `leave_slide_frames`, ...) sets what the
original's last test left. The equipment screen's preview of an item's
figures (`$C2:A5C3`) would need its P after the list's move returns, which
that port's paths don't work out, so it stays the original's (TODO.md).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches, none refused (each entry's counts are its ports.tsv row): the
commands' step 336 calls, the list's 417, an item picked up 190, the cursor's
item described 599 (inside those steps), a slot taken 462 and the slots'
marks 468, a row of what fits 272, what a member has on 225. The library opens the
items screen twice (`menu` and `menus-field-menu`), sorts once, picks up,
swaps and uses a tonic, and goes through the equipment screen's windows
eight times; it never slides the list of items, shows who can equip an item,
or uses a tab, an item by chance or the party's rest. Fuzzed from two states
of `menus-field-menu` (the `items` state, frame 5150, and the `equipment`
state), seed 1, 2,000 states each, the menus' tables (`$7E:7700`-`$7FFF`) kept
with low RAM and the tilemaps, every routine up to its first wait for a
frame. The first runs found: the slide's end leaving a 16-bit accumulator and
the step's both bytes clear (the rows' move returns 16-bit); the list
window's slide filling 28 words of the rows' table, not 27; the original's
16-bit count of the scroll bar's hold and the cursor's hiding taken down
together as the slots window closes; the carry an item's use leaves its
handlers (the bit search's, not the handler's own) and the overflow a tonic's
strength test leaves; the drawing entries needing 16-bit index registers.
Random item tables made the original's strings run anywhere, which the kept
tables answer; the equipment screen's own entries now keep them too, and
their counts are unchanged against main's (`tools/fuzz_compare.py --before`:
0 of 326 routines differ).

**Timing.** The new entries were calibrated over the library (the menus'
rows only); the drawings charge their text at their own rates rather than
the text drawer's, which their few long strings fit better, and the
equipment screen's rows' move charges a block move for its copy. One
exception: the item list's ten rows (`menu_draw_item_rows`), whose time
follows which rows are empty, on the panel's five calls in vblank
(`tools/timing_exceptions.txt`).

## Phase 3, field and events: the party on the map

The first chunk of bank `$C0` in wave 2. PLAN.md's table of what event's
bridges still called in the original chose it: the field's state routines
behind `$B0` (`$C0:9E29`, `$C0:A26B`, `$C0:A2CE`, `$C0:5B8D`, `$C0:3154`) and
the tile copy behind `$E4` (`$C0:AF4E`), with the unported runs they lie in.
Reading them showed what they are: the leader leaving a trail and the members
walking it, the leader touching and pushing what it walks into, and the map
changing under a script. The chunk took those and what is the same concept
beside them: the player talking to an object (`$C0:5AC5`, from the field's
confirm button), the party's regrouping commands (`$D9` `$C0:326C`, `$DA`
`$C0:345A`), and a member sent back to the leader after a battle
(`$C0:5B1F`). `python3 tools/burndown.py` for `$C0`: 19.0 KiB ported, 0.7
inlined, 68% left and 249 entries before; 21.7 KiB, 1.4 inlined, 63% and 258
after -- 3.4 KiB, against the 8-12 a wave-2 chunk aims at; what fills it out by
concept is the leader's walk, the next chunk (below, and PLAN.md).

**The party as a module** (`field/party.h`, `field/party_trail.c`). Every
frame the leader moves it leaves a footprint on a ring of 128 in bank `$7F`:
how far it moved, how the pad walked it, how its sprite was layered, and the
ground under it (a climb, a moving floor). Each member walks the trail behind
the one ahead of it, a footprint a frame, keeping 24 footprints back while the
leader moves and closing to 16 when it stands, so the party bunches up and
strings out. What each shows follows the footprint: standing, walking or
running and facing its way, climbing, the leader running on the spot against
a wall, a pose while the player picks an answer, a fidget when the party has
stood about. The original's state routines were one function a gait and a
facing (sixteen of them, `$C0:A079`-`$C0:A15C`, reached through a table of
64 words at `$C0:9FF7`); they are one `Pose` -- a gait and a facing, or a
diagonal's two -- looked up from the two axes' gaits. The lookup keeps the
original's index arithmetic, one named function: it runs the vertical speed
and an eighth of the horizontal together into one byte, which muddles speeds
other than the pad's, and a member waiting on a moving floor looks up what the
floor's pull leaves of the leader's walk, which can be any. `$B0` and `$AF` are
one operation (`step_with_the_party`, `once` the short form's row), with no
call-out left; the entries were renamed for it (`event_cmd_walk_with_the_party`
and `_once`, `$C0:3002` and `$C0:2FFD`) and carry their predecessors' totals.

**The leader meeting objects** (`event/contact.c`). The field notes the object
the leader is up against (`field_leader_contact`): the one its walk ran into,
or the one before it when the confirm button is pressed. Talking and touching
are calls the field makes on the object's script, the same as objects make on
each other, so `event/calls.h` came out of `calls.c`: an object's function's
start, whether it takes a call now, and running a script from a place at a
level (`object_run_script_from`), which the calls, the talk, the touch, the
push and the regrouping all use. A pushable object (bit 1 of the byte `$84`
stores beside solidity) goes a tile the leader's way where that tile is open,
through the party's push script; the decision is one function
(`leaders_touch`) that both the touch and what its derivations work out call.

**The party's scripts and the regrouping** (`event/party.c`). Every map load
copies three runs of commands after the map's own script (`$C0:5CEC`): a
member walking up to the leader, a member walking to a tile and facing a way,
and an object walking to a tile. `$D9` and `$DA` hand them to the party at the
most urgent level, writing the tile and facing into the scripts' operands,
and wait on a count two of the map's variables keep (`FORMATION_COUNT`,
`GATHERING_COUNT`); then the members pick the trail up from the leader's newest
footprint (`party_trail_regroup`). A formation's place is decoded once, by its
row, into a `FormationPlace` (a tile, and a facing from the top bits of its
column and row): the language's rows now decode into records as well as
numbers (`EVENT_RECORD_PARAMETERS`, `FORMATION_PLACE(n)`), which is what
TODO.md asks of the location's decoding too.

**The tile copy** (`field/tile_copy.c`) copies a rectangle over another place
in the tables its description names: each layer's tiles, the tiles' properties
and their two bytes of attributes, or of the properties only the bit that says
which half of the tile set BG1's or BG2's tile comes from (read from the tile
builders at `$C0:762D` and `$C0:79EA`). It goes a tile at a time, top to
bottom and left to right, as the original's block moves do, so a copy onto
itself a little right or down repeats its first tiles. `$E4` is an operation
calling it; the main loop's copy (`$C0:1790`) reaches the same entry.

**What the map gained**: the trail (seven tables at `$7F:0C00`-`$7F:0F7F`)
and where each of the party is on it (`field_trail_position`, `$01AB`); the
walk's outputs on the layers' page (`field_leader_walk` `$1D2C`,
`field_leader_movement` `$1D32`, `field_leader_ground` `$1D34`); the idle
count the frame wait keeps (`field_idle_frames`, `$0158`, masked `$83FF` at
`$C0:EC60`); the leader's contact (`$01EB`); and `field_state_routine_hold`
renamed for what `$0138` is, the screen recentring on the leader before a
queued tile copy (`field_screen_recentring`). `field/field.h` names
`field_mode_flags`' bits (the party gathering, an object being pushed, a
dialog the field opened, which `event/dialog.c` had to itself) and
`dialog_choice_active`'s states. `field/object.h` gained an object's step
(`object_set_step`), its sprite priorities as a value to hand on
(`SpritePriorities`), whether it can be pushed, the party's animations beside
the movement ones, and `object_show_movement`, which was `animation.c`'s own.

**What shadow mode found.** The first library run mismatched on two things:
the gap between two places on the trail was worked out as a C remainder of a
negative difference (so a member whose place had wrapped past the one ahead
took the next footprint when it should have waited), and the regrouping's
hand-overs leave Y's low byte (they run with 8-bit index registers), which the
derivation had as the whole offset. After those, over the 26 movies every entry
matches, none is refused, and every other entry's totals are the
predecessor's. The one move that was a bridge is compared now:
`event_cmd_walk_with_the_party` 251,565 calls, 867 matched before (the rest
abandoned inside the engine's stretch), all matched after. The new entries:
`field_leader_steps` 116,079 calls, `field_first_member_follows` 83,032,
`field_second_member_follows` 51,587, `event_touch_contact` 1,871,
`event_cmd_take_formation` 716, `field_copy_tiles` 261,
`event_cmd_gather_party` 166, `event_talk_to_contact` 99,
`event_member_back_to_the_leader` 10; none interrupted. `$AF` has no call in
the library, nor does a push.

**What fuzzing found.** The original adds the leader's contact and the
party's objects into a place's offset (and a function's) as 16-bit words, so
the byte after each is read too: `$01EC`, and `$0198`, `$019A`, `$019C`
after the party's places. The game stores those objects with 16-bit index
registers and keeps the bytes 0, which random states don't: 14,926 of 20,000
talks mismatched until the map named them (`field_leader_contact_high`,
`field_party_objects_high`) and the guards required them 0. The trail's gap
showed on the members' own entries before the library did (344 of 20,000).
Then every entry fuzzes clean (PORTING.md's row), with variants that fix the
paths random state rarely reaches: a leader up against an object that takes
calls (touch and push), a member in its place with its role, the leader
standing with the player's control (fidgets) and during a choice, footprints
on plain ground, a formation and a gathering with members missing, and tile
copies small enough to overlap themselves. The predecessors of `$B0`, `$AF`
and `$E4` compare the same states with the same mismatches (none); the new
guards turn away a few more (a contact's high byte, a copy off the map).

**What was left at the edge, and why.** The trail's start at a map's setup
(`$C0:9DC3`, where the openings hand back) and the gathering after a battle
(`$C0:034B`) stay the original's: the first reads the ground under the leader
through the walk's own routines (`$C0:9AC8`, `$C0:9AD3`), and both go with
the walk. The inventory and membership commands (`$C7`-`$D7`) call the
battle bank's item routine and the menus' party routines, and would have been
bridges only. The objects' own loading (`$57`-`$83`, their sprites through
`$C0:448D`) is a concept of its own, objects coming onto the map.

**The timing gate.** The new rows were calibrated over the library, and two
event rows moved with them: `event_cmd_walk_with_the_party` 3,374 to 4,641 (its
time is now the party's step as C, where it was its bridge's own loop) and
`event_run_object_script` 4,596 to 4,723 (the engine runs `$B0` as C inside
its stretch). The tile copy's time follows the rectangle and the tables it
takes -- whole tables by the original's block move, a tile set's half a byte
at a time -- which one scalar fits only on average: `field_copy_tiles` and
`event_cmd_copy_tiles` are in `tools/timing_exceptions.txt` (at most 26,414
clocks in a call starting in vblank, writing the map's tables in RAM, which the
vblank check finds moves no video write).

## Phase 3, field and events: the leader's walk and the confirm button

The second chunk of bank `$C0`: what feeds the party's trail, and what the
player does with the leader besides walking it. `python3 tools/burndown.py`
for `$C0`: 21.7 KiB ported, 1.4 inlined, 63% left and 258 entries before;
25.9 KiB, 5.0 inlined, 56% and 262 after -- 4.2 KiB, counted with the
tool's fix below, which leaves the "before" as the first chunk reported it.
The concept's other half,
the screen's edges streaming the map in as it scrolls (`$C0:93E1`, the row and
column builders), is 5.5 KiB of its own and the next chunk (PLAN.md).

**The walk** (`field/walk.c`, `$C0:881E`, every field frame). The pad asks
for a walk or, with the dash held, a run, on top of what a moving floor pulls
(`$C0:88EE`, the eight directions `$C0:8925`-`$C0:8A6C`). The leader stands
in a box against the map, its feet at its position; walking one way, the
box's leading side is looked at at both corners (`$C0:8AB5`-`$C0:8E20`), and
walking aslant its leading corner and both sides (`$C0:8E21`-`$C0:9174`, four tables of which side
nudges which way), where a corner meeting another level nudges the leader
round it and a wall stops it. Before the map, the other objects: the first,
from the last slot down, standing close to where the leader is going
(`$C0:9923`) is what it is up against, and stops it if solid. The map has
two levels; each tile's shape (a slope from a table of 22, a half, a
quarter) says which parts of it are on which (`$C0:9AA1`-`$C0:9C36`), and the
leader walks on its level, onto stairs joining the two, over or under a
crossing. What it steps onto takes effect (`$C0:9C5C`): an exit, a step
event, a moving floor's pull for the next frame, a climb. It stays within the
map's edges (`$C0:9175`), and the screen follows it while it is past the
screen's middle the way it walks (`$C0:99DE`). The recentring (`$C0:885A`)
scrolls the screen back onto the 16-pixel grid a pixel a frame while a
script waits on it. Both still call the layers' scroll (`$C0:91AC`, a port)
and the screen's edges (`$C0:93E1`, the original's) on the layers' page.

**The party's start and its gathering** (`$C0:9DC3`, `$C0:034B`), which the
first chunk left at its edge: at a map's setup the trail starts afresh and
the leader takes the level and ground of its tile (the map's openings' entry,
`event/bridge.c`, runs it as C and hands back only where its guard fails);
after a battle the members walk back to the leader through the gathering
script (`event/party.c`, `party_back_together`), timed as
`event_member_back_to_the_leader`.

**The confirm button** (`event/contact.c`, `$C0:1AAC`; `event/chests.c`).
Where a dialog waits for the button, the press is the dialog's. Otherwise the
leader talks to the object it walked into this frame or, failing that, the
first object near enough before it (`$C0:1CFC` and its four ways: under 28
pixels along the way it faces, 12 across), and then opens the chest before
it. That search measures as the objects-in-the-way search does, the
original's borrow included, so `field/motion.c`'s measure became
`object_bearing` in `field/object.h` for both. A chest (`$C0:1DF4`-`$C0:1F23`)
is a tile the leader can't walk onto whose walkable byte names it; the
game's chests are one list in bank `$F5` (`field_chest_at`), and a chest's
number in it is its story flag, one bit each from the story memory's second
byte. Opening one raises the flag, asks the main loop to show it open, and
gives the party an item (nine bits, into variable 0) or gold (into
`field_chest_gold`) through the battle bank's inventory service
(`$C1:8003`, a call-out, which keeps the flags), then the field's own message
(`event/dialog.c`, `field_message_opens`); an opened chest's message says it
is empty, and a chest marked as holding nothing of its own gives nothing and
says nothing (its location's script does the rest).

**What the review of the first chunk asked, folded in.** A party place is
one type (`PartyPlace`, `object_party_place`); the leader's ground an enum
(`GroundKind`, the stored byte inside `LeaderGround`); one setter for a party
script's destination and the facing encoding in `event/party.c`; a tile
copy's rectangle as its size, the table addresses in the leaves; `Touch` the
outcome alone; named setters for `field_mode_flags`; the fidget flag decoded
in the idle count's storage. The contact the leader is up against is one
concept in `field/field.h` (`leader_contact`, `set_leader_contact`,
`leader_loses_contact`), which the walk, the search and the touch share.

**Chunk 2's review.** It rated `field/walk.c` a translation on one count:
the screen's check carried "what the original leaves A with" out of the
check, for a scaffolding re-run of it. The check returns whether the screen
follows now, and what the original's cells get, the check turned back up
included, is worked out from the state by the leaves beside the check
(`leave_screen_check`, `leave_check_down_turned_back`). With it: the tile's
shape is an enum in `field/layers.h` (`TileShape`), the level a `MapLevel`;
the pad reads `core/input.h`'s buttons; the recentring's step is named;
whether the player has the party is `party_is_the_players` and
`hand_the_party_to_the_player` (`field/field.h`); and the call-outs whose
site depended on a ternary are written out per case, so the checks read a
literal each. What it left for later is in TODO.md.

**What the game's buttons are.** The walk dashes on the fourth of the
player's assignments and the confirm button is the first, which settles what
the scripts' button conditions ask (`$30`/`$31`, `$3B`/`$3C`, renamed
`event_cmd_if_dash_held`, `_if_confirm_held`, `_if_pressed_dash`,
`_if_pressed_confirm`); `core/input.h` names them `ASSIGNED_DASH` and
`ASSIGNED_CONFIRM`.

**The searches' start.** The original doubles the object count in a byte, so
a count past 127 starts the searches from what is left of it: the endings'
and some battles' maps keep 143, 148 or 163 at `$7F:2000`. The three searches
(the walk's, the confirm button's and `field_object_in_the_way`) start at
`object_search_start`; the guards had required a count under 64, which turned
away 541 of the walk's calls in the library before the fix.

**The burndown's bridges.** `tools/burndown.py` read the call inside
`bridge_call`'s own body (`game/field/bridge.c`, and the battle's
`battle_call`) as an unreadable call-out of every port that uses a bridge, and
so credited those ports no callee at all. It reads the bridge at each
delegate's call now, as `tools/dontcare_check.py` does. On main it moves
`$C1` from 10.5 to 20.6 KiB (the battle's ports call out through
`battle_call`) and leaves `$C0` at 21.7; the walk, which calls out through
`bridge_call`, would have been credited 0.3 KiB of its 3.6 without it.

**What shadow mode found.** The recentring's state moved to "under way" before
its call-outs, where the original steps it after them (`WRAM[0138]` at the
call to `$C0:91AC`, in five movies). Then, over the 26 movies: every entry matches and none is interrupted:
`field_walk_the_leader` 194,763 calls (20 refused, above and TODO.md),
`field_press_confirm` 190,882, `field_recentre_the_screen` 174,
`event_party_back_together` 25, and `event_run_init_scripts` 243 with the
start as C; every other entry's totals are the first chunk's, the renamed
button conditions included. `ct_dev --native on --generated only --misses`
interprets nothing on `field`.

**What fuzzing found** (seed 1, 20,000 states each, `tools/fuzz_routines.toml`):
a chest's item is nine bits, not eight (9,829 of 20,000 mismatched on the
item variant, at variable 0's high byte and the inventory's copy of Y). Then
every new entry fuzzes clean (PORTING.md's row), with variants that fix
what random state rarely reaches: the leader walking into the map's middle
with the player's control, objects close enough to talk to (the window-only
kind included), and the leader facing a chest from the game's list that holds
an item, gold or nothing of its own, each with its flag random. Against the
predecessors (5,000 states each), every field and event entry keeps its
mismatches (none); the searches' start compares more states of the moves
that search (`event_cmd_follow_object` loose 282 where 132).

**The timing gate.** The walk's time follows the map: standing, the
original spends about 1,000 clocks, walking into it ten times that, working
each point out at length where the port reads a few bytes. One scalar fit
that badly (4.5% of its stretches off by 1,024 clocks or more, the worst
16,456), so the port charges each look at a point, and more for the ground
where the point is no wall, at the walk's own rate (`charge_as`, as `field/motion.c`'s
search does): calibrated, the walk is within the gate's limits with no
exception. The new rows were calibrated over the library, and three of the
subsystem's moved with them: `event_run_init_scripts` 4,878 to 4,961 (the
party's start now runs as C in its call), `event_touch_contact` 4,250 to
3,351 (the review's changes to the touch moved its accesses) and
`event_cmd_walk_with_the_party` 4,641 to 4,634.

## Phase 3, field and events: the map scrolling in

The third chunk of bank `$C0`, the other half of the camera: what the
layers' tile maps hold as the screen moves over a map larger than itself.
`python3 tools/burndown.py` for `$C0`: 25.9 KiB ported, 5.0 inlined, 56% left
and 262 entries before; 30.0 KiB, 8.6 inlined, 49% and 266 after -- 4.1 KiB.
With it, the walk's 20 refused calls.

**The map's looks** (`field/streaming.c`). A map tile is 16 pixels and a
layer's tile map entries 8, so each layer's tile of the map has a look: the
four entries of its quarters (`field_tile_looks`, BG1's and BG2's, 512 of
them, the map's tile set decompressed over the map's data once that is
unpacked; `field_bg3_tile_looks`, BG3's 256). BG1 and BG2 take theirs from
one half of the looks or the other as the tile's properties say (bits 0 and
1, which the tile copy already knew as the tile set halves). A line of
entries along a row or down a column is built from the map tiles it crosses,
within each layer's mask of where its map repeats (`field_map_index_mask`,
`field_layer_index_masks`), into the layer's staging buffer
(`field_layer_staging`), which the vblank's deferred uploads send
(`field/display.c`). The six builders (`$C0:7612`-`$C0:7F57`: each layer's
rows and columns) are one function. Where a line starts on an odd 8-pixel
tile they differ: BG1's and BG2's rows count its first half as one of the
line's tiles and end with a half of the tile after the last -- its second
half, where the first would continue the line, which lies past the screen's
right edge (TODO.md); BG3's rows and every column add the half on top.

**The layers' moves** (`$C0:93E1`, every frame after the layers' scroll).
Each layer takes its move a pixel at a time, right, left, up and down: its
scroll moves, and its place in its 16-pixel tile; each time it crosses an
8-pixel tile the screen's edges in 8-pixel tiles move with it and the row or
column it crossed onto is due (`field_layer_edges_crossed`, a bit per layer a
side). BG2 may go at half the pace (`field_bg2_half_scroll`, from the map's
flags): it counts a place in 32 pixels, scrolls every other one, and keeps
edges of its own (`field_bg2_screen_left`...). A layer drawn whole at the
map's load (`field_layer_drawn_whole`) has nothing due, and BG3's waves move
their phase with it down the screen. Then each side's due lines are built
(the row above the view first, then below, left and right), each layer's
place in its tile map moves on, the uploads are asked for, and the edge runs
are worked out (`$C0:7F9A`, timed as itself). The original writes the lines
a byte at a time through the work RAM port; the port writes the staging
through its accessors and leaves the port's cursor where the original's is
(`cpu_io.h`: the port is memory access, which docs/PLATFORM.md keeps out of
the platform services, so nothing was added there). A move is taken in
locals and stored once, and a map tile's look worked out once for its two
entries.

**The layers at a map's load** (`$C0:75E9`, `$C0:78EC`, `$C0:7CB5`): BG1 is
drawn as the screen stands, 32 rows of 16 map tiles into the staging from the
first buffer's start; BG2 and BG3 whole where they are drawn whole (the map's
first 16 by 16 tiles, then the tile map's other half: the next 16 columns of
a wider map, the next 16 rows of a taller one, which makes the tile map two
screens tall, or the same tiles again), and BG2 at half the pace from its
own edges. The loop the screen's drawing runs down keeps its count in the cell
the edge runs read as their wrap (`field_layer_edge_wrap`), and leaves it 0,
which the edges then read: a real write, not scaffolding.

**The walk's refused calls.** The original doubles the map's object count
into a byte to start the walk's search for objects in the way, so a count
of 64 to 127 starts it on the slots past the tables, where it reads the next
tables' bytes as a slot's: whether it is in view, disabled or solid, and
where it stands (the objects' vertical positions as its horizontal one, their
velocities as its vertical one). In the game's terms there is nothing there,
and the search looks at the map's objects from the last slot down; the
original's reads are one marked piece of scaffolding
(`what_the_original_meets_past_the_tables`, `field/walk.c`, through the
tables' unchecked accessors), which sets the outcome where one of those slots
reads as close. The library makes such a count on a frame or two in six
movies (112 and 123 at `$7F:2000`): 20 calls, all compared and matching now,
and on none of them does the walk look for objects at all -- a debug build
that reported every search past the tables, run over the nine movies with
such counts, reported none -- because the leader isn't stepping into
anything then, so the game can't observe the reads there. Where it could -- a step that met such a slot -- the original would
stop there, and the leader would be up against nothing (the slot's byte has
a link to none's top bit) and stopped where it reads as solid.

**What the map gained.** The looks, the staging, BG2's own edges, the other
layers' index masks, the half-pace and drawn-whole flags, the edges crossed;
`in_view` and `solid` got unchecked accessors for the walk's scaffolding. The
map's data (`field_map_data`) and the looks share their bytes at different
times, which the map's overlap check refused: `re/ram.toml` gained a `reuses`
key for a variable that takes over another's bytes (`tools/ram_map.py`).

**What shadow mode and fuzzing found.** Over the 26 movies every call of the
new entries matches: `field_stream_the_layers` 194,853 calls (1 interrupted),
the drawings 308, 296 and 106 (49, 37 and 7 interrupted: a whole screen's
drawing runs over a frame's NMI, the paths fuzzing covers); `field_walk_the_leader` 194,763 calls, all compared and matching,
none refused. Fuzzing (seed 1, `tools/fuzz_routines.toml`) found the
drawings' loops: the screen's rows counted from a top edge near 255 wrapped
(615 of 5,000 mismatched before counting the rows rather than comparing
them), and BG2's rows at half the pace between edges the other way round,
or to a bottom of 255, run past the staging and the bank (the original's for
ever), which its guard leaves to the original. Then, 20,000 states each:
the streaming 0 of 20,000 (and 0 of 20,000 with every layer moving up to 18
pixels each way, and with BG2 at half the pace and the layers drawn whole or
not), the drawings 0 of 20,000, 0 of 16,596 and 0 of 20,000; the walk with a
count of 64-127 0 of 10,159 (its predecessor's guard compared 1). Against
the predecessors every field and event entry keeps its mismatches (none).

**The timing gate.** The streaming's time is a steady part and a
part the lines and the moves set, which the port's reads and writes don't
follow: charged at one rate, frames that built rows were over by up to
50,000 clocks and frames with moves drifted with their pixels. The lines'
and the moves' own accesses are left out of the estimate
(`charge_uncounted_begin`) and each entry (rows more than columns) and each
pixel is charged at the streaming's own rate instead, and the edge runs are
timed as their own routine: calibrated, 0.16% of its calls are off by 1024
clocks or more, the worst by 1,714. The drawings charge their entries the
same way, so a whole layer's drawing and a screen's scale alike. All four
rows are calibrated, and none is an exception.

## Phase 3, field and events: the larger sprite layouts

The fourth chunk of bank `$C0`, and chunk 3's review. Of the three runs
PLAN.md named, the sprite layouts are the one the ports called out to most
(ten call sites from `field/sprites.c`, against three from the main loop's
ports and none into the frame builder). `python3 tools/burndown.py` for
`$C0`: 30.0 KiB ported, 8.6 inlined, 49% left and 266 entries before; 33.5,
8.7, 44% and 277 after -- 3.5 KiB.

**The larger layouts** (`field/sprites.c`, `place_larger`). An object's
sprites are 4, 8, 12 or 24 records; the 8-, 12- and 24-sprite layouts
(`$C0:BA65`, `$C0:BCDC`, `$C0:BFF2`, `$C0:C2BF`, `$C0:C73A` with its fours
`$C0:C6E7`) are placed as the 4-sprite one is: each four sprites' X at the
object's screen X and their ninth bits as one of the object's high bytes
(`object_set_sprites_high_bits`), then every sprite's Y; a rebuild takes the
offsets, and after the Y the looks, from the object's new frame. What
differs is which sprites show and which are hidden on the table's line. The
table keeps a sprite's Y as a byte, so one above the screen wraps round to
the table's last lines or, further off, onto the screen, and each of the
original's routines hides its own by where the object stands -- above the
screen's top, in its lower half or its upper half. The port works from the
sprite's signed top line and names each rule by what it shows (`ShowRule`:
"shown reaching down onto the screen", "hidden just below the screen",
"shown in the top half" and so on); each routine's three are a row of one
table (`ShowRules`), the 4-sprite layout's among them, each row's comment
saying what differs on the screen (the 8-sprite placing shows the sprites
below an object in the upper half wherever they land, where its rebuild
hides them past the middle). The 24-sprite layout is always placed, and an
object of it above the screen whose last sprite starts on the screen or less
than 256 lines above it isn't drawn that frame (the original answers
"nothing to draw" with its last sum's carry). That is the game's behaviour,
kept as plain logic, which returns only whether the object shows; the
overflow the original's code reads after a placing (its last sum's, or the
24-sprite layout's step past its records) is worked out from the state by
the entries (`placing_overflow`), which look before the update at what it
will do (`records_work`) to know whether it placed and whether it rebuilt.

**The frame's tile areas** (`claim_frame_areas`, `free_frame_areas`). The
frame builder draws an object's new frame into areas in bank `$7F` before
it goes to VRAM (`field_frame_staging`, four from `$7F:3800`, `$200` bytes
each, which chunk 4 first took for VRAM areas: the builder hands the address
to the WRAM port): one for the 4-sprite layout, two side by side for the 8,
three for the 12, which it claims for the object
(`field_frame_staging_claims`, `$C0:E952`, `$C0:E97A`, `$C0:E9AA`, recording
where the object's frame goes, `field_object_frame_staging`) and which are free
again once the records are rebuilt from the frame (`$C0:E9E2`, `$C0:E9FF`,
`$C0:EA1F`); a map's start frees them all (`$C0:E935`). The frees clear Y's
high byte (they take 8-bit index registers) and the update's callers read Y,
which the entries hand on.

**The draws are C.** With every layout's records its own, the sprite table's
building draws every object as C (it drew as C only where every object's
records needed nothing of the original's before), timed as the draw, the
update and the layouts' routines. The 4-sprite rebuild is C too.

**Chunk 3's review, folded in.** The walk's search stays typed in objects
(`in_the_party`, `stands_close` take a `FieldObject`), and what the original
meets past the tables overrides its outcome in one marked line,
`ORIGINAL_EFFECT(stops = stopped_past_the_tables(...))` (review rubric item
81). The current object's word is made current as a word plainly where the
map setup and the sprite table's building keep it so
(`object_make_current_recognised`, `object_recognise_current`), which the
effects check found read as a word by logic but cleared only as scaffolding;
its ten baseline lines are gone, and the contact's two carry their reason.
`field/field.h` names the field's geometry once -- the map tile, the entry,
the tile map's size, the screen's, the streamed lines' lengths -- and the
staging's lines (`field_staged_line`), which `layers.c`, `streaming.c`,
`display.c`, `walk.c` and `object_update.c` take; the tile looks' quarters
are fields in the map.

**What shadow mode and fuzzing found.** Over the 26 movies every call of
every entry matches: `field_place_8_sprites` 43,339, `field_place_12_sprites`
24,238 (54 interrupted), the rebuilds 554 and 569, the claims 37,507, 563 and
591, the frees 37,502 (8 interrupted), 554 and 569, the clearing 786; the
update, the draw and the table's building keep their calls, and the
building's interrupted calls rose from 336 to 1,472 of 193,483 (it runs as one
stretch now, which the frame's interrupt falls in more often; its fuzzing
stops at the budget on random rows, as before). Fuzzed (seed 1, 20,000
states, `tools/fuzz_routines.toml`): each layout's placing and rebuilding 0 of
5,000 on random positions and 0 of 20,000 with the object above the screen,
the claims 0 of 20,000 each, the frees and the clearing 0 of 5,000 and 20,000,
the update and the draw with the larger layouts 0 of 1,252; a deliberately
wrong rule (the 8-sprite placing's upper half as its rebuild's) mismatched 592
of 3,000, which is what the variants are for. Against the predecessors every
field and event entry keeps its mismatches (none).

**Chunk 4's review, folded in.** The placing's logic returns only whether
the object shows, and the entries work out the overflow and Y the original's
callers read from the state (review rubric items 82-84); the show rules are
named for what they show and are one table, the 4-sprite layout's row moved
out of the platform (`vid_oam_y` is gone; `VID_OAM_HIDDEN_Y` stays). The
frame's areas turned out to be staging in bank `$7F`, not VRAM (the builder
hands their address to the WRAM port), and are mapped so
(`field_frame_staging`); their owners are links to objects. The objects'
sprite high bytes are byte tables reached through one table of the three,
since they lie apart in memory. Over the 26 movies every call still matches;
fuzzed against chunk 4's build with the same seed, all 26 sprite entries and
variants keep their counts (0 mismatched), and seven rows' `cost_milli` were
recalibrated.

**The timing gate.** The new rows and the update, the draw, the building and
`event_run_init_scripts` were calibrated over the library; no exception.

## Phase 3, field and events: the frame builder

The fifth chunk of bank `$C0`: the frame builder, which the previous chunk's
tile areas are drawn by. `python3 tools/burndown.py` for `$C0`: 33.5 KiB
ported, 44% left and 277 entries before; 37.2, 37% and 278 after -- 3.7 KiB.

**What it is** (`field/frames.c`, `field_draw_next_frame` `$C0:CAD9` with
its layouts `$C0:CBDC`, `$C0:CEF5`, `$C0:D4F7` and their parts). When an
object's animation moves on to a frame its tiles don't show, the frame queue
(`$C0:CA76`, still interpreted: it stops at a scanline) has the builder draw
it: the next frame (the animation's, through the frame read, or the one a
script holds) is claimed tile areas in bank `$7F`, its tiles are copied there
from the object's graphics set as its description names them (straight or
mirrored, by the copies `field/graphics.c` already had), the areas are
queued to be sent to the object's tiles in VRAM, and the frame's sprites --
offsets, tiles, attributes -- go into the frame's data, which the records are
rebuilt from. The 8- and 12-sprite frames are drawn a part at a time, one
part each time the queue comes to the object, and the parts are counted in
the records' state byte, which is what holds the records' rebuild until the
frame is whole. The original's six near-copies (a layout, and for the
12-sprite layout whether its tiles start half way along a row of VRAM's, at
tile `$68`) are four rows of one table of plans: which of the description's
tiles each part draws where, the two stretches sent to VRAM, where the
sprites' tile numbers skip a row, and which sprites take the object's upper
half's priority (for the 4-sprite layout, by where each starts).

**What it taught the map.** `$0F01` was named for the battle; it is the frame
the object's tiles show (`field_object_frame_drawn`: coming into view and a
script's layering forget it, `object_forget_frame_drawn`, so the next frame is
drawn whatever it is; the battle keeps it for the party). The object's
graphics set and frame descriptions (`$1200`/`$1280`, `$1300`/`$1380`), its
tiles in VRAM (`$0D00`) and its palette (`$0F81`) are mapped, and so are the
builder's own cells: the queue's end (`$79`), the description's pointer and
the count on the field's page (whose `field_work` stops at `$C8`).

**What the callers read.** The frame queue reads the carry (more of the frame
to draw: the object stays at the queue's head) and hands the overflow on;
liveness keeps the overflow, so the entry works it out after the logic from
what the logic came to and the state (`build_overflow`: the frame read's,
the last copy's step along the areas, the second stretch's place, or the
tiles' skip of a row), with the overflow and the animation's frame from
before the call.

**What shadow mode and fuzzing found.** The first run mismatched a fifth of
the calls on `$D9`, the attributes' cell the original leaves (now
scaffolding); the guard then refused one real call, an object with no areas
yet ($FFFF) in the middle of its frame, which only a first part's claim
would give it: the guard asks only of the parts after the first that their
areas lie inside the bank. Fuzzing found graphics built in bank `$7F` whose
tiles the frame names run past the bank, where the copies read low RAM
through bank `$80` (the builder's own cells among it); the guard looks ahead
at the frame the builder will draw (`object_peek_next_frame`) and refuses
those. The look ahead first read the frame tables' bank before their offset,
which moved the frame read's open-bus byte; `field_read_animation_frame`'s
fuzzing caught it (604 of 4,089) and the bank is read last again.
Over the 26 movies every call matches: 231,605, none interrupted and none
refused. Fuzzed (seeds 1 and 2, 20,000 states each, `tools/fuzz_routines.toml`):
object 8 animating with the 4-, 8- and 12-sprite layouts 0 of 13,837 and 0 of
13,743, holding a frame 0 of 13,795 and 0 of 13,724, the 12-sprite layout
with its tiles at `$68` 0 of 13,766 and 0 of 13,624 (the rest turned away by
the guard); a deliberately wrong plan
(the split layout's last run a byte off) mismatched 1,749 of 3,465. Against
chunk 4's build every other field entry keeps its counts.

**The timing gate.** The new row was calibrated over the library; no
exception.

## Phase 3, field and events: the switches and the main loop's requests

The sixth chunk of bank `$C0`, and chunk 5's review.

**Chunk 5's review, folded in.**
- *The overflow past the frame queue* (the port-routine skill's third trap).
  Liveness keeps it live round the main loop, but the only readers it
  reaches are the menus' entry, which saves the flags (`$C2:8013`'s `PHP`)
  and puts them back on leaving; banks `$C0` and `$FD` have no branch on
  it. With no port call-out there to poison, it stays derived, from a
  status of game conditions (`FrameBuild`, field/parts.h) in field/leaves.c,
  outside the logic file.
- *The copies take their set and place.* `field_draw_frame_tile(g, set,
  tile, to)`; the work RAM port's final place and the cells only the
  original's code reads (the set, the place, the frame tables' bank) are
  scaffolding, and the copies' entries read them as their calling
  convention (baseline lines with that reason).
- *One frame read.* `object_take_next_frame` takes the tables' bank, which
  the `$C0:CB3A` wrapper reads; its guard refuses a bank where an address
  doesn't answer (`field_bank_always_answers`), so the read order no longer
  matters and the twin is gone. The mirrored copy's guard does the same for
  the tile's bytes, which fuzzing showed carry out of bank `$FF` into bank
  `$00`'s open bus (38 of 9,944 before the guard).
- The tile's constants live once in field/graphics.h; the 4-sprite
  priority rule and the `$68` plan say what they are; `object_holds_frame`;
  `sprite_offset` takes an `Axis`.

**What it is** (field/requests.c, field/switches.c). The main loop's
`$C0:0C76` moves the brightness and tint changes a step each frame, and
answers the requests. The port answers a step event itself: the step is
remembered on the map, and a floor switch stepped onto is pressed -- its
tiles move on to the next in the tile set, in one of three shapes (a tile
and the one above, two by two, two across by three down, `SwitchShapeTiles`,
where the original has five near-copies), its sound is played, and the
VRAM addresses of its tiles' entries in BG1's tile map are queued. The
line interrupt's job after the picture (`$C0:ED15`) draws the queued looks
(field_redraw_changed_tiles), a chest's as well, and refreshes the windows'
corners. The location change, the menus, a chest's opening, the tile copy
and the battle are handed to the original's code where it reads the
requests (`m_stop_at_own` at `$C0:0C8A` and `$C0:1770`). A chest's opening
went with the stop: at its sound the original has A's high byte and the
overflow from the path into it, which only the path gives, so porting it
means porting the path (TODO).

**What it taught the map.** The redraws' flags (`field_tiles_to_redraw`,
`$5F`), the switch's shape, the chest's kind, the two sounds, the entries'
VRAM addresses (`$09B2`, `$09CA`), the steps remembered (`$7F:1CC8`), and
the bytes after the screen's left and top edges (read as words).

**What shadow mode and fuzzing found.** A chest's opening mismatched once
(telepod-lavos): the sound driver's call came back through a path the
original reaches with other registers; the chest went to the stop. The
switch's sound reads A's high byte (the driver keeps it), which the port
works out from the switch's tiles (field/leaves.c). Over the 26 movies every call of the three matches: the requests 191,044,
the redraw after the picture 98,342, the frame builder 231,605; none
interrupted or refused. Fuzzed (seeds 1 and 2, 20,000 states each):
`field_handle_requests` 0 of 19,944 and 0 of 19,946 with any requests but
those it hands on at once, and a step onto each switch's bottom tiles 0 of
9,866 and 0 of 9,882 (the rest past the call-out budget); the redraw 0 of
20,000 each. A deliberately wrong shape (one of the two-by-two's tiles off
by one) mismatched 289 of 1,433 in the requests and 492 of 3,000 in the
redraw. `python3 tools/burndown.py` now credits `$C0` with 56.4 KiB and 5%
left, which is wrong the way its docstring warns: it walks
`field_handle_requests`' region through the branches the port hands to the
original at `$C0:0C8A` and `$C0:1770`, into the location change's reset to
the main loop and everything that reaches. What the chunk made C is the
step event's switches and the redraw, about 2.6 KiB.

**The timing gate.** The new rows, and the two chunk 5's review moved (the
mirrored copy, whose estimate no longer counts the port's writes through
the port, and the builder), were calibrated over the library; no
exception.

## Phase 3, field and events: the drawn frame put on the records

The seventh chunk of bank `$C0`, and chunk 6's review.

**Chunk 6's review, folded in.**
- *The switch sound's A-high byte* (the port-routine skill's third trap). Its
  reader is named: the sound driver saves A on the stack as it starts
  (`$C7:0147`'s `PHA`), so the byte stays in the stack below the caller's
  frame, which shadow mode compares. Liveness has the driver read it, so it
  can't be declared dead; the derivation stays in field/leaves.c and reads
  the map through switches.c's own decoding (`field_map_byte`), where it had
  read column 63 as BG1's (review rubric item 94).
- *Origin tags* (item 95): `redraw_quarter` takes no site; each branch tags
  its uploads' start with a literal.
- The guard refuses a one-by-two switch whose upper tile is the tile set's
  last, and the word-wide move is gone; `leave_entries` leaves its cells
  through `original_cell16`, and `$D9` is one named cell
  (`FIELD_LAST_WORKED_CELL`); the redraw after the picture is in display.c
  with `field_windows_need_refresh`; one `pressed_shape`; the chest kinds are
  named (`ChestKind`, by where each is in the tile set). The switch sound keeps a site a switch: the call's
  return is where the port stops if the call comes back otherwise.
- The mirrored copy takes a place in the build bank, like the straight
  copy, and its guard takes a port there alone, where the frame builders put
  it. The fuzzer can't set the port (its saved states have it in bank `$7E`,
  where the sprite table's building leaves it), so the entry's row compares
  nothing; the copy is fuzzed through `field_draw_next_frame`.
- *The burndown's overcount.* `tools/burndown.py` no longer walks on from a
  place a port hands back at, and a stop at a call's return counts as a
  hand-back where it declares what is dead there (a bare one is still a
  bridge giving up); `tests/test_burndown.py` covers both. `$C0` goes from
  5% left to 36%, and the total from 41% to 51%.

**What it is** (field/frames.c, `field_put_drawn_frame` `$C0:CB0A` with its
layouts `$C0:CDC8`, `$C0:D124`, `$C0:DD28`). The frame builder's twin, which
the field's return from a battle calls for each of the party and the enemies
it puts back ($C0:07EC, $C0:08A0, by `BRL` from their own code): it puts the
frame the object's tiles already show straight onto its records, built, and
draws nothing, since the tiles in VRAM are as the battle found them. It is
the builder's frame sprites (the same plans, the `$68` split's too) going to
the records instead of the frame's data (`sprites_set_record_sprite`). A
registry address inside a routine is a block boundary for the generated
code, so the entry takes the `BRL`s as calls; its return goes to its
callers' own callers, which read none of the registers.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (84, none interrupted or refused). Fuzzed (seeds 1 and 2, 20,000
states each): each layout 0 of 20,000, the 12-sprite layout at `$68` 0 of
20,000; the 4-sprite priority rule a line off mismatched 4 of 3,000.
`$C0` stands at 39.4 KiB ported, 34% left.

**The timing gate.** The new row, and the mirrored copy the review moved,
were calibrated over the library; no exception.

**Battle's findings, answered.** The long arithmetic's cells (`$28`-`$33`,
the 8-bit multiplies' operands and the multiplier's copies) are game state:
the original's callers multiply and divide what the last call left there, so
`game/battle/arith.c` writes them plainly and only the multiplier's and
divider's own state is scaffolding. So are the reach tests' product
(`battle_product`), the direction's cells, the fade tables' last intensities
and the tile decoder's row cells (`$53`-`$57`, which a screen effect's settings
read where its command doesn't set them), and the enemy at work
(`battle_enemy_turn`) as the choosing leaves it, written after its scaffolding
so that the plain write is the last. What is left in the baseline for battle
is the entries' operands, each line with its reason. `battle_run` now runs a
sibling as C only where the registry gives that sibling's entry for the state
(`native_hand_entry`, guard and preconditions included); otherwise it makes
the call as the original's, with nothing declared don't-care, and the runtime
runs whatever the registry has there. The library's gate passes unchanged,
with `battle_enemy_scripts_choose` recalibrated (8,036 to 7,902) for the
plain writes it makes now.

## Phase 3, field and events: the field put back after a battle

The eighth chunk of bank `$C0`, and chunk 7's review.

**Chunk 7's review, folded in.**
- The switch sound's A-high comment gives the real reason the byte is
  compared: the sound driver keeps its saved-register frame, A with it, on
  the stack while it waits on the sound CPU, so shadow mode compares it as
  the live stack it is there, and liveness counts `$C7:0147`'s `PHA` as a
  read (not "a byte below S", which PORTING.md says is not compared).
- `sound_sites` is indexed by the switch's shape and column with
  designated rows, and `switch_number` is gone.
- frames.c has one test for whether an object is drawn
  (`object_on_screen`), shared by the builder and the put-back; the
  24-sprite object is drawn, its frame is just not put back.
  `object_put_drawn_frame_back` returns nothing, and the header says what the
  after-battle put-back is.
- `$D9` has one name (`FIELD_SCRATCH_CELLS`, with `FIELD_LAST_WORKED_CELL`
  as the same cell by its use); the graphics port's guard asks whether the
  bank is `FIELD_BUILD_BANK`.
- `tests/test_burndown.py` asserts the code after a stop that declares a
  dead set isn't credited, that code reached around a stop is, and that a
  callee a port stops at is not inlined (the burndown now leaves those out).

**What it is** (field/battle_return.c, `field_return_from_battle` `$C0:0715`
with `$C0:076B`-`$C0:0904`). When a battle on the field ends, every enemy it
took over goes back to its tile on the map, the one the battle kept from the
screen's corner, into the rows the sprite table walks, showing the frame it
showed (field/frames.c's put-back, the `BRL`s at `$C0:086E` and `$C0:0902`);
an enemy the battle took away (its kept kind 0) is made unreachable and
hidden, giving up its palette if it has none of its own sprite. The party's
places follow (the first taken without looking, as the original does), the
sprite table is built at once (`$C0:B271`, a call-out), and where the party
was defeated each member holds the frame it shows. That last part is also an
entry of its own, `field_hold_party_if_defeated` `$C0:072E`, the return from
the sprite table's building, which is where the original goes on when the
guard sends the return to it, and what fuzzing compares apart from the call.
The battle's kept enemy objects (`$7E:01A3`) are named in the RAM map
(`field_battle_enemy_objects`).

**What shadow mode and fuzzing found.** Fuzzing found one guard: the
original makes each object current by its link's byte alone and the frame
put-back reads the whole word, so a byte after the link makes the put-back
see no object; the guard takes a return whose current object's word is clear
above the link (`object_making_current_recognises`), as every return in the
library has it. Shadow mode found the other: the original gives up a
taken-away enemy's palette from two subroutines down (`$C0:076B`, then
`$C0:07EC`, which saves the enemy's place and object over the call), and
shadow mode pairs a call by its stack pointer and compares that frame, so the
port pushes the two return addresses and the saved bytes around the call-out
(PORTING.md, "A callee's frame that is still on the stack"). The fuzzer
compares the machines when both have returned, not at each call, so only `late-omen-battles`, where a
battle takes away an enemy with no sprite of its own, showed it. Over the 26 movies every call matches
(`field_return_from_battle` 25, none interrupted or refused;
`field_hold_party_if_defeated` 25). Fuzzed (seeds 1 and 2, 20,000
states each): the return 0 of 20,000 each, compared up to the sprite table's
building (whose own call-outs can't finish on the fuzz state, TODO.md), with
the enemies taken away or kept, with a sprite or without; the defeat's end 0
of 20,000 each, won, lost or fled. The rows are sensitive: an enemy's kind
read the other way mismatched 2,993 of 3,000, its sprite test 1,357, and the
defeat test 1,915. `$C0` stands at 39.9 KiB ported, 33% left.

**The frame builder's overflow, through battle's V check.**
`tools/v_readers.py --after C0:CAD9` has V read after the builder returns,
so its derivation (field/leaves.c) stays: the sprite table's building
after the frame queue (`$C0:00D9`) leaves V as it is, the frame wait passes
it on, and the next frame's pause and menu opening (`$C0:18D9`) reach the
sound driver's saved flags (`$C7:0A39`'s `PHP`, taken back other than by
`PLP`) and the menus' `BVC` at `$C2:F7D9`.

**The timing gate.** The two rows were calibrated over the library; no
exception.

## Phase 3, field and events: the pause and the menus opened from the field

The ninth chunk of bank `$C0`, and chunk 8's review.

**Chunk 8's review, folded in.**
- *The palette give-up is a port* (field/palettes.c, `field_give_up_palette`
  `$C0:734C`). The field's eight sprite palette slots are shared by objects
  with the same colors, each slot's sharers a list in the order they took it
  (`field_object_palette_older`/`_newer` at `$0B00`/`$0B01`, the newest by
  slot at `$0B80`), and an object giving its slot up leaves the list; while a
  script has loaded palettes itself (`$0154` bit 7, event `$33`) objects keep
  out of the slots. The battle's return and the view update
  (field/object_view.c) run it as C, so the rebuilt JSR frame, the call-site
  arithmetic and the `Snes *` in `bring_back_enemy` are gone, and so is the
  view update's call-out. Fuzzed (seeds 1 and 2): 0 of 20,000 each; breaking
  either branch mismatched 159 and 1,623 of 3,000. Over the 26 movies every
  call matches (350, from the view update and the return).
- The return's work is a `GameState` function (`put_back_after_battle`);
  the entry keeps only the sprite table's bridge and the defeat's hold.
- *The `$016E` guard, named.* The frame put-back's layouts read the current
  object's whole word under 16-bit index registers (`$C0:CE57`, `$C0:D21C`,
  `$C0:DE91`, `$C0:E08C`), so a byte after the link makes them draw another
  object's tiles. The condition stays, with its readers named, and the
  put-back's own guard (`field_frames_can_put`) takes it too; it refuses no
  library call.
- One `BattleResult` (battle/battle.h), which battle/main_loop.c sets and the
  field reads; the party's places are `put_party_back`, with `PARTY_PLACES`
  from field/party.h and the leader's place left to the guard; the taken-away
  kind is named. The screen's corner stays in `field_own_tile` through its
  accessor: `original_cell8` reaches the field page's scratch run
  (`$C1`-`$E8`), and `$F0` is the direction routine's named tile.

**What it is** (field/menus.c). Once a frame the field's main loop hands
the buttons to `field_pause_and_menus` (`$C0:18D9`): Start pauses the field
while the player has the party and no script has locked the pause (the
picture at half its brightness, the play time clock stopped, until Start
again); the Start assignment (`field_press_start`, `$C0:1985`) moves an open
dialog window between the top of the screen and the bottom, or, with none
open and the story's progress at `$49` or past, opens the menus at their mode
5; X opens the main menu (mode 1); otherwise a waiting choice takes the
buttons. A menu opens only while the player has the party, no choice waits and
no script has locked the menus: the field fades out a step a frame, keeps the
party's places, and calls the menus; after them it sets itself up again,
brings back the characters of a party the menu changed
(`field_bring_the_party_back`, `$C0:1A03`: each character's object starts
again in its script, Frog's before Robo's as the original has them, and the
party goes back where it was kept), and fades in. The Start assignment's menu
also takes a menu a script asked for as shown. Most of the work is still the
original's routines, called out to from their own calls (the frame wait, the
menus, the field's setup and fade in); the two menus' call sequences are one
function over a table of their sites. The RAM map names the pause's and the
menus' locks (`$0110`, `$0111`), the brightness kept over the pause, the
characters' objects (`$8D`-`$93`), the party as last loaded, and the party's
places kept over a menu (`$7F:1D09`-`$7F:1D1D`).

A's high byte and the overflow are read after both entries, so the port
leaves A's high byte as the routines it called left it and declares only its
low byte dead; the Start assignment's subtraction of the story's progress
sets the overflow, which field/leaves.c works out (`field_story_test_overflow`:
`tools/v_readers.py --after C0:1985` has it read).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches: `field_pause_and_menus` 191,044, `field_press_start` 1,
`field_bring_the_party_back` 3, none interrupted or refused. Fuzzed (seeds 1
and 2, 20,000 states each), compared up to the first frame wait, the blanking
before a menu or a choice's call, since the pause and the menus run for
frames: the buttons 0 of 19,450 and 19,424 (550 and 576 past the call-out
budget, where the Start assignment's own calls run on), the Start assignment 0
of 20,000 each, the party put back 0 of 20,000 each, and the characters'
starts, compared up to the first, 0 of 20,000 each. The rows are sensitive: a
pause at a quarter brightness mismatched 342 of 2,922, the menus unlocked 126
and the story's overflow a step off 155 of 3,000, the trail left alone 2,945,
and the player's control ignored 163.

**The timing gate.** The three rows were calibrated over the library; no
exception.

## Phase 3, field and events: the field's start

The tenth chunk of bank `$C0`, and chunk 9's review.

**Chunk 9's review, folded in.**
- *Ports reached through the machine* (review rubric item 61). menus.c
  blanks the screen and takes the interrupts back as C
  (`display_blank_and_stop`, `field_take_the_vblank_interrupt`,
  `field_take_the_line_interrupt`, each timed as its routine), setting the
  CPU's interrupt mask and data bank the original's blanking leaves. Three
  ports stay machine calls on purpose, said beside their addresses: the
  line effects' builder, whose entry works out the registers it leaves (the
  overflow among them is read after the pause), and the Start assignment and
  the party's return, whose own call-outs must be made under their return
  address, where shadow mode pairs them. The file header says which is which.
  The menu path after the menus' return runs on the machine for frames, so
  the fuzz rows stop before it (now at the field's keeping, `$C0:011B`);
  shadow mode compares it (the blanking 549 calls and the handlers 335 over
  the library, the menus opened in `menus-field-menu`).
- *Shared vocabulary.* `WindowPosition` is field.h's, and the Start
  assignment swaps the window between the top and the bottom (its guard takes
  a window at one of the three places); `FIELD_SCRIPT_MENU_SHOWN` is
  `field_effect_status` bit 6 (event `$C8` waits on it, `$C0:3757`);
  `party_is_the_players`, `dialog_choice_waiting`, `field_pause_is_locked`,
  `field_menus_are_locked`; `FIELD_START_MENU_FROM_STORY` in field.h.
- The two openings are one `MenuOpener` and one table row each, with what
  differs between them (the menu, the X it is given, whether a script's
  menu counts as shown) in the row; `open_menu` says why N and Z are clear.
- The kept priorities are two byte fields; `battle_how_it_ended` types the
  result; the pause's display flags are named; the eight palette slots; the
  Frog-before-Robo order can't be seen in play (each start writes its own
  object and character entry); `object_palette_links_in_the_tables`.

**What it is** (field/start.c, `field_start` `$C0:0B86`). Before the field
sets up its first map, after the title, a load or another mode, its state is
put as a map's arrival expects it, in eight operations named by what they
leave: the location also put aside; nothing under way (requests, status,
locks, screen jobs, tile copies, transitions); a dark plain screen; no
dialog; the party the player's; no objects in the party's places or standing
for the characters; the sprite tables started from scratch
(`sprites_hide_all_next_frame`, and the sprite loading's four tables from the
words at `$E4:FFE0`, through field_data.h); the usual sounds; and the random
numbers from the play time. It calls nothing. The cells the map hadn't named
are named from their readers (a CPU-side watch over three movies for the
readers, then the listing): `dialog_speaker_high` (`$2F`, which `$C0:1FDD`
reads with the speaker as a word), `field_screen_job` (`$39`),
`field_built_sprite_object` (`$AE`), `field_sprite_data_tables`
(`$AF`-`$BA`), `field_map_has_no_bg3_set` (`$BB`), `field_battle_song`
(`$FA`, which the battle's setup plays through `$C0:1BAB`) and
`field_bg3_map_size` (`$7E:0BE9`).

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (243, none interrupted or refused). Fuzzed (seeds 1 and 2) over
random states: 0 of 20,000 each; leaving one cell unwritten mismatched 997 of
1,000.

**The timing gate.** The row was calibrated over the library; no exception.

## Phase 3, field and events: the menus a script asks for

The eleventh chunk of bank `$C0`, and chunk 10's review.

**Chunk 10's review, folded in.**
- *Siblings through the machine for their guards* (rubric item 102).
  `field_run` and `field_run_long` (field/bridge.c) run a sibling port as C
  under the frame the original's call pushes, timed as its routine, where the
  registry would run that entry for the state (`native_hand_entry`: its guard
  and widths); elsewhere they make the call on the machine with the declared
  set, as `bridge_call` does. tools/dontcare_check.py reads them as it reads
  `bridge_call`. menus.c runs the line effects' builder, the Start assignment
  and the party's return through them; the second enum and its stated reasons
  are gone, and the header says what runs how.
- The two openings are a game table (the menu, whether its screen is given no
  argument, whether a script's menu counts as shown) and a parallel table of
  their sites (rubric item 100).
- field.h has one definition each of the line effects' slots (moved from
  line_effects.c) and the ones the field keeps on (`FIELD_LINE_EFFECTS_KEPT_ON`,
  built from them), no tint (`FIELD_NO_TINT`, from the platform's
  `VID_LINE_FIXED_COLOR_ALL_CHANNELS`, which event/screen.c and
  battle/screen_setup.c now take too), the starting color math (through the
  new `vid_math_control`, the encoder beside `vid_color_math_decode`), no
  window marked, and `MapLevel` with `FIRST_LEVEL` (moved from walk.c).
- `party_taken_as_loaded` (party.h), used by the start and the party's
  return; the start calls `count_as_on_the_move` and
  `screen_counts_as_recentred`; its switches are written as bools through
  named operations (`field_lock_pause`, `field_leader_meets_the_map`,
  `field_line_interrupt_works`, `field_map_names_a_bg3_set`); the backdrop is
  named; `field_sprite_data_table` returns the long address, with the RAM
  map's count; `display_go_dark` is core/display.c's one blanking helper, the
  entry's and menus.c's.

**What it is** (field/menus.c, `field_open_script_menu` `$C0:0CC7`, from the
main loop's requests). A script asks for a menu with event `$C8`, its byte
kept in `field_script_menu` (`$25`): the field fades out a step a frame
while the leader walks on without the player's control (the walk run as C
through `field_run`, the control kept on the stack where the original keeps
it, since the walk's own calls are paired by where the stack is), keeps its
places, and opens the menu: a byte of 0 goes on as the Start assignment's
menu does (`$C0:19C7`); $01-$7F opens mode 0 or 6 by its low bit; $80-$FF
opens the mode in its top two bits with the screen's argument in its low six.
After it the field is set up again, the script's wait is over
(`FIELD_SCRIPT_MENU_SHOWN`), and the port stops at the fade in, which the
original jumps to. The location change and a chest still go to the original
(TODO.md). The port now hands them back where the original takes each up (the
location change at its fade, `$C0:0C91`; a chest at the step event's test
before it, `$C0:0D3E`, with the requests in A as the test reads them), not at
the requests' first test, so the burndown credits the requests' dispatch and
the script's menus: a stop the port makes on some paths stops the walk on all
of them. The burndown also no longer counts `field_run`'s PC, the sibling it
runs, as an unreadable hand-back (`tests/test_burndown.py`). `$C0` stands at 40.8 KiB ported, 31% left.

**What shadow mode and fuzzing found.** Over the 26 movies every call of
`field_handle_requests` matches (191,044, none interrupted or refused); the
script's menus with an odd byte (mode 0) run in every movie, and with an
argument in eight (the fair's, the shop's, the new game's among them); no
movie asks for mode 6 or the Start assignment's menu, which fuzzing covers up
to the menus' call. Fuzzed
(seeds 1 and 2, 20,000 states each): up to the fade's first frame wait or the
keeping of the field 0 of 20,000 each, and, dark already, compared at the
menus' call 0 of 20,000 each; with the menus' Z left clear for mode 0,
736 of 3,000 mismatched; a location change handed back 0 of 20,000 each, and
the chest's hand-back without the requests in A mismatched 1,445 of 2,996. The chunk's other rows are unchanged.

**The timing gate.** `field_handle_requests` was calibrated again over the
library; no exception.

## Phase 3, field and events: the 24-sprite figure drawn whole

The twelfth chunk of bank `$C0`, and chunk 11's review.

**Chunk 11's review, folded in.**
- The script menu's fade keeps the player's control in a local and runs the
  walk through `field_run_holding` (field/bridge.c), which pads the call's
  frame with the byte the original holds on the stack and drops it after, so
  menus.c touches no stack. The reason, corrected: shadow mode compares the
  stack pointer and page at the walk's own call-outs and stops.
  tools/dontcare_check.py reads it as it reads `field_run`, and `run_entry`
  carries battle's comment on the guard's fallback.
- One `dim_a_step`, which the menus' fade (with the clock stopped) and the
  script's share.
- Comments: the menu button's tail is `$C0:195E`; `screen_counts_as_recentred`
  sits above `$C0:885A`'s comment and the recentring uses it; menus.c's
  header names the script's menus and the palette animation's jump at
  `$FD:FFF7`; TODO.md names the stops `$C0:0C91`, `$C0:0D3E`, `$C0:1770`.
- requests.c tests the script's menus before the chest; `table_of` takes a
  `FieldLineSlot`; the `LINE_INTERRUPT_OFF`, `NO_WINDOW_SELECTED` aliases and
  `ScriptMenu.the_start_menu` are gone. The menus' modes a script asks for
  stay numbered: the menus turn them into their own modes 14, 12, 13 and 11
  through `$C2:808F`, which the menus' side hasn't named either.
- The line effects' fuzzing's accessor-out-of-range predates the chunks: the
  same run on a build before chunk 11 reports it (TODO.md).

**What it is** (field/frames.c, `field_draw_whole_figure` `$C0:E12A`). An
object of the 24-sprite layout, a large figure a script loads, draws no frames
through the frame queue: when it is loaded (`$C0:47E9`) and when the field is
set up after a menu (`$C0:0212`), its one frame is drawn whole, its 96 tiles
from its graphics set (field/graphics.c's copies, straight or mirrored) into
the build place and sent at once to VRAM word `$0400` (a new site encoding,
`$C0:E19A`), and its 24 sprites' records laid out from its description's
offsets, eight sprites a row of tiles. It returns with 16-bit index registers
whatever it came with, the overflow clear and Y at the description's last
byte, which the callers read.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (1, in `endings-bucket-lavos`, with its graphics set built in bank $7F; none interrupted or refused). Fuzzed (seeds 1 and 2): 0 of
20,000 each with the set in the ROM, and with it built in bank $7F 0 of 1,296
and 1,372 (the guard turns away the random descriptions whose tiles run past
the bank or over the build place); the VRAM word, the sprites' attributes or their tiles' step off
by one mismatched 500 of 500 each. Fuzzing found the cell the builder's start
writes that this doesn't (the frame table's bank) and the Y the callers read;
shadow mode found the library's one figure refused, its set built in bank $7F,
which the guard now takes where the tiles are clear of the build place.

**The timing gate.** The row was calibrated over the library; no exception.
`$C0` stands at 41.8 KiB ported, 30% left.

## Phase 3, field and events: the main loop's requests answered

The thirteenth chunk of bank `$C0`, and chunk 12's review.

**Chunk 12's review, folded in.**
- The figure's place is a named view of the frame builder's areas and the
  bytes after them (`field_figure_tiles`, `$7F:3800`, `$C00` bytes, which
  re/ram.toml marks as reusing `field_frame_staging`); the figure and its
  guard's check against it read that name.
- The layout is derived: the VRAM word from the first tile and the words a
  tile, the tiles' steps from the builder's `NEXT_SPRITE_TILE` and two tile
  rows, the attributes from palette 1 and priority 2 (the figure ignores its
  object's palette), and the offsets through `sprite_offset` with a sprite
  count, as the frames' plans use it.
- `leave_figure` is built from the builder's leaves (`leave_drawing_from`,
  `leave_description_place`, `leave_run`); `field_set_tile_start`
  (graphics.h) is where a set's tile starts, for both guards and the copies;
  menus.c says why the control byte is put back raw (event `$E3` sets any
  value); the entry names the readers of its V and Y.

**What it is** (field/requests.c, `field_handle_requests` `$C0:0C76`). Every
request the main loop answers is now the port's, one kind a frame:
- *A location change* (`$C0:0C8F`): the picture fades a step a frame
  (`field_dims_a_step`, field.h, which the menus' fades share); dark, the
  party's place is kept as the one it came from (the location, the leader's
  tile, and the facing it will come back with, the opposite of its own), the
  next place becomes the party's, and the port stops where the field starts
  again on a fresh stack (`$C0:0CC0`), which is the original's to do. The
  guard takes the change only with a leader (the original reads its place
  with the link as a 16-bit index, past the tables for none: three calls in
  `field`, at the title's changes, go to the original).
- *A chest* (`$C0:16DC`): its sound from the original's call, the chest shown
  open (field/switches.c `field_open_chest`, with the switches' entries), and
  its overflow for the code after it (field/leaves.c). With a step event
  before it, its sound reads A's high byte as the step event leaves it: a
  pressed switch's last redraw entry's, else the remembering's
  (`field_step_event_high_a`).
- *A tile copy* (`$C0:1779`): the screen scrolls back onto the grid a frame at
  a time, the tiles are copied (`field_copy_tiles` through `field_run`), and
  the layers they changed drawn again on the layers' page
  (`field_run_on_page`, a new bridge that keeps the field's page on the stack
  under the call as the original's `PHD` does) and sent in the vblank, BG1,
  BG2 or BG3 alone from a table of their calls, or BG1 and BG2 over three
  frames; the port stops at the frame wait the original jumps to.
- *A battle* (`$C0:188C`): the layers scrolled into place, the effects built
  with the field's setup's values, the battle fought (`$C1:0000`, given A as
  the frame wait before it leaves it, which liveness has it read), the field's
  interrupts taken back as C, and the field's objects and turn taken up again.
The guard's switch test now covers every tile a switch presses (one two
columns wide, stepped into at a row's first column, reaches the row above's
last), which fuzzing found; a chest inside the map's rows.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches (190,914 of 191,044 compared, the rest the guard's; none mismatched); 55 calls interrupted, the tile copies and battles whose
stretches span frames, which fuzzing reaches only up to their first long
call-out (TODO.md). Fuzzed (seeds 1 and 2, 20,000 states each): a location
change 0 of 17,473 and 17,497 (the rest the guard's leaderless states), a
chest 0 of 10,089 and 9,865, a step event and a chest together 0 of 9,899
and 10,060 (the rest run a sound past the call-out budget), a tile copy 0 of
20,000 each up to its frame work; the rows before unchanged. Breaking the
chest's entries' order mismatched 677 of 1,015, and the facing's flip 714 of
1,462. `$C0` stands at 44.8 KiB ported, 25% left.

**The timing gate.** `field_handle_requests` and `field_recentre_the_screen`
(now also run as C from the tile copy) were calibrated over the library; no
exception.

## Phase 3, field and events: the screen effects and the credits

The fourteenth chunk of bank `$C0`, and chunk 13's review folded in.

**How it was chosen.** The plan named the screen effects' steps (`$C0:220E`).
By concept that is the step the main loop runs every frame (`$C0:21E1` and its
sixteen handlers), the event command that starts and waits on the effects
(`$FF`, `$C0:3FAF`, and its sub-table at `$C0:400E`) and the searchlight's
setup (`$FE`); and beside them in the main loop, the layer work scripts leave
it (`$C0:274D`) with the commands that ask for it (`$29`, `$2A`, `$2B`, `$2C`,
`$2F`, `$32`): BG2's halves cleared and the endings' credits written into BG3.
That is the whole of the concept, 2.6 KiB, short of the plan's 8-12: the rest
of `$C0`'s runs are other concepts (the characters' objects, the items and the
party, the field's setup), and the fade in (`$C0:2824`) is the field's setup's
caller more than an effect.

**What it is.** `field/screen_effects.c`: an effect is a window's shape drawn
into the per-line table the line effects aren't reading, and a cut (which
layers the windows hide on which screen, where the colour blending applies),
as data: a hole in BG1 and BG2, an oval of blending, both opening to a radius
and closing; the searchlight's four moving corners; the curtain, window 2's
ragged edge swept across by bank `$C3`'s routine (a call-out); BG3 in a circle
at the top of the screen, or blended round it, drawn into both table sets over
two frames; the ripple, a waving copy of BG1 through a circle; and the field's
close into scene `$85`. The circles are `scene/circle.c`'s and the shape
`field/window_shape.c`'s, run as C (`TIMED_AS` their entries).
`field/script_layers.c`: the credits laid out a line a frame in BG3's staged
row from the texts at `$FD:A000` (`field_data.h`) and sent by the vblank's BG3
piece; BG2's halves asked of the vblank and the staging blanked (a call-out);
and the message the field opened for a chest closed once the chest is off the
screen. Event `$FF` (`event/screen.c`) is a bridge: below `$90` the field fades
out a step a frame and goes dark, and the port stops where the original starts
the scene on a fresh stack (`$C0:3FD6`); from `$90` a C table of the sixteen
screen commands (start an effect, open a circle at a place, a curtain or the
ripple waited on, the push ended), with the field's restart (`$96`) handed to
the original at the command's dispatch and the menus' party routine 8 (`$9F`)
called through the dispatch's frame. `$FE` decodes its sixteen operands into a
record of the language (`SearchlightMove`); its leaves (the divider, the
working cells, the overflow of the last distance) are `event/leaves.c`'s.

**Chunk 13's review, folded in.** The tile copy's single-layer table is typed
(`FieldLayer`, designated rows, the move as a `FieldMapMove`, BG3's own frame
wait a column), and the queued maps no longer double as a transition; the map
transitions' moves and sends are `field.h`'s (`nmi.c` used private copies);
`field_is_dark`, one map-rows constant (`FIELD_MAP_ROWS`), the figure's size
from the map, `field_step_kept_at` asked rather than the search repeated, the
chest's kind forgotten in `ORIGINAL_EFFECT`, the party's keeping and its move
split, the header's request order, `switch_tile_row` without a negative shift.
The review's call to drop `field_frame_wait_leaves_a` was wrong and is
answered in its comment: the frame wait leaves A the idle count's high byte,
and where A held that before the wait shadow mode keeps the port's own A
across the call, so without the line the battle's call mismatched (9 calls in
`endings-bucket-lavos`). Removing the requests' `== 0` test the review called
dead put five reads of the requests in every frame's estimate (+85% drift);
it stays, as the test that skips the answers.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches: the step 190,882, the layer work 190,882, `$FF` 56 (32 inside the
event engine's stretch), `$FE` 74, the credits' commands 36-38 each; no call
interrupted, none refused. The curtain, the ripple, the field's close and the
BG3 circles no movie reaches. Shadow mode found the scene's stop leaving the
carry (the fade's last comparison clears it, now declared dead) and the
credits line leaving its column in the scratch cell. Fuzzing (seeds 1 and 2)
found the curtains' order wrong: `$93` uncovers leftward and `$94` covers
rightward (57 of 10,154), and `$29` reading its operand before the letters'
unpacking, which reaches the script's bank (79 of 2,525); fixed, every entry
is 0 mismatched: the step 0 of 19,996 and 19,993 (one state of seed 1 differs
in OAM bytes only, TODO.md), the curtain 0 of 5,000 each, the layer work 0 of
20,000 each, `$FF` 0 of 10,154 and 9,999, the scene 0 of 9,986 and 10,054,
`$FE` 0 of 9,943 and 10,091, `$29` 0 of 2,525 and 2,499, the rest 0 of
9,987-10,133. `$C0` stands at 46.5 KiB ported, 22% left.

**The timing gate.** The new rows and `event_run_init_scripts` (whose
openings now run `$FF`'s and the credits' rows) were calibrated over the
library. `field_move_screen_effect_on` carries `field_rasterize_quad`'s three
exceptions: the searchlight's shape is timed as that routine and is off by
what it is off by.

## Phase 3, field and events: who the objects are, their colours, and the party's belongings

The fifteenth chunk of bank `$C0`: the event language's last commands, so
every opcode a script uses now has a row (the ROM's handler table is only the
fallback for a state a row's needs turn away), and the chest's gift as C.

**How it was chosen.** The plan named the commands at `$C0:4186` (the
characters' objects) and `$C0:3711` (items, gold and the party). By concept
that is everything the language had left: an object's casting -- `$57`-`$6D`
and `$80` (a playable character, in its party place or set aside), `$81` (one
apart from the party), `$82` (one of the people and creatures), `$83` (an
enemy), with the shared-graphics search `$C0:5C90` -- its colours (`$33`, a
palette slot of its own; `$88` and `$2E`, effects on its colours and the
screen's), the inventory and gold (`$C7`, `$C9`-`$CE`), the menu a script
opens (`$C8`), and the party's record (`$CF`-`$D4`, `$D7`). About 3.2 KiB,
short of the plan's 8-12 like chunk 14: what `$C0` has left besides is the
field's setups and its keeping (`$C0:0000`, `$01A5`, `$038F`, `$011B`, the
fade in `$2824`), another concept.

**What it is.** `field/cast.c` casts an object: its role and character
(`field/object.h` gained the operations: a member in a place, set aside, one of
the other kinds, the enemies' two), a sprite record put on it (MD2's
`field_sprite_records` and `battle_sprite_records`, `field_data.h`: graphics
set, assembly, palette, animations, layout, through the four tables
`field_sprite_data_tables` points at), its graphics -- a character's from the
ROM, the others' unpacked into bank `$7F` after the sets already there or
shared with an object that has the same set -- and its block of sprite tiles
in VRAM (`field_sprite_block_owner`). A party member stands where the
location says, or on the leader; a member put on the map right after a
character joins shows its first frame at once (`field_character_joining`,
the meaning `field_animation_queue_front` had half of). `event/characters.c`
has the rows: `$57`-`$81` operations, `$82`/`$83` bridges whose unpacking
(`$C3:0002`, a machine-shaped port) is a call-out between their halves of C.
`event/colours.c`: `$33` (`field/palettes.c`, `object_take_palette_slot`),
and `$88`/`$2E` decoded into a `ColourEffect` record by their form: a
rotation stopped (the object's colours loaded again) or started, a fade, or
colours of the script's written over the palette; with the first eight
animation slots busy the effect is dropped and the object's turn ends there
(`end_turn_after`, a flow the language didn't have). `event/items.c`: the
inventory's and the gold's commands call `battle/inventory.c` as C, timed as
its routines; `$C8` asks the main loop for a menu and waits. `event/party.c`:
the party's record is the menus' (nine places, the active three and the
reserve), so `$CF`-`$D4` and `$D7` are bridges to `$C2:8004`, now named by
what each routine does (read from `$C2:8C75`-`$C2:8E1E`): whether a character
is active or recruited, bring one in, send one to the reserve, recruit,
dismiss, count an item. Bringing one in makes its object the next member's;
sending one away has the members behind close up, roles, places and palettes
(slots 5-7), with the original's two quirks kept (an empty first place closes
up whatever it held; a second member sent away is only marked out). The
chest's gift (`event/contact.c`) gives the item or the gold as C now, and its
guard leaves an item past 255 to the original.

**Third trap, settled first.** The call-outs kept are the unpacker (it saves
and restores every register: `DC_A | DC_X | DC_N | DC_Z | DC_C`, Y passed
through to the engine, which leaves it to the menus' dispatcher) and the
menus' routines, which the existing bridges already called. Shadow mode found
one register those read that liveness can't drop: `$D4`'s carry at the call,
which the dispatcher's `PLP` hands back as whether the object's turn goes on.
It is the carry of the last character comparison the original makes, worked
out as `$D6`'s already was (`goes_on_after_sending`), not threaded.

**What shadow mode and fuzzing found.** Over the 26 movies every compared call
matches: the engine's turns and openings with the new rows inline, and the
entries the original's own loops reach (the casts 81-657 calls each, the
colours 89-640, the menu 131, the record's 1-213, the chest 190,882). Shadow
mode found the enemy's Y (the original works the block out with 8-bit index
registers, which clear Y's high byte) and `$D4`'s carry. Fuzzing (seeds 1 and
2, 20,000 states each, `tools/fuzz_routines.toml`) found the leader read
through a word whose high byte the game keeps clear (now the guard's) and the
enemy's scratch cell's high byte; every entry is then 0 mismatched on both
seeds but the loose casts, `$82` 1 and 2 of ~8,600 and `$83` 8 and 8 of
~6,700: random unpacking places that carry a set past bank `$7F`'s end into
low RAM, where the original reads its cells again (an accepted divergence,
written at `event_cmd_cast_enemy`; with the place in the bank's upper half the
loose `$83` is 0 of 6,792). `$C7` and `$CB`, `$CD`, `$D7` no movie reaches.
`$C0` stands at 49.6 KiB ported, 16% left.

**The timing gate.** The new rows were calibrated over the library, with the
engine's two entries (whose turns now run the rows inline); no exception.

## Phase 3, battle chunk 2: an effect worked out (`$C1:D132`-`$C1:F1EF`)

An enemy's turn is C to its end now, but for the original's code it calls out
to (the tech's targeting, messages, bringing enemies in and the showing): the
attack's and the tech's effect is worked out in C (`game/battle/effect_work.c`,
`$C1:D7C4`, `$C1:D8D1`), and the notes of the action for its targets
(`$FD:AAD2`, `$FD:AB01`) are C inline in the turn. The work is in four files:

- `effect_work.c`: going through the targets, the effect's record decoded
  (`EffectRecord`, `effect_data.h`) and its kind run by a switch, each case
  timed as its own routine (`$C1:D9D7`'s kinds with work, each a registered
  entry for the party's code, which still runs them from `$C1:D523`); what
  acted on each target noted (`$C1:D508`), the hits' numbers added up
  (`$C1:EB68`), and the entries with their guards;
- `effect_amounts.c`: healing (`$C1:DA37`), draining (`$C1:E77B`), a hit's
  tests of landing (`$C1:DE87`, `$C1:DE9E`, `$C1:DEF0`, `$C1:DC64`), its
  damage by the eight formulas the dispatch reaches (`$C1:E0F5`; its formulas
  9 and 10 are behind a test that sends every kind but 8 to the weapon's), the
  defence (`$C1:E65F`), and the damage applied (`$C1:DCAD`), with the damage
  table and the numbers shown recorded (`$C1:EBF8`, `$C1:E89F`, `$C1:E8E2`);
- `effect_statuses.c`: curing, inflicting and the status timers (`$C1:DB5E`,
  `$C1:DBAA`, `$C1:E006`, `$C1:E976`);
- `effect_modifiers.c`: the modifier record decoded into its routine and what
  its two numbers mean for it (a percent, a stat, the families singled out,
  ...), applied by a switch (`$C1:FA61`'s routines, `$C1:EB1C`, `$C1:EA6F`).

The statuses' bits and the stat block's offsets the work reads are named once,
in `battle/combatant.h`.

**What stays the original's.** Stealing is the party's alone (no enemy's
action steals): its item goes to the party's inventory or its battle records
(`$FD:B655`), which is the party's chunk, so the steal kind isn't registered
and the entries turn away a hit that steals, and the rare item's chance with
it. The clock modifier (`$C1:EE27`) reads the controller mid-work, a read that
waits on the console (the port leaves that wait to the original, as
`core/input.c` does), and no enemy's action has it: the entries turn it away.
So does a modifier routine past the table's 21, a kind past `$2C` (none in the
ROM), and a drain where the byte the original reads its stat's offset's high
byte from (`$1B`, which nothing in the effect code sets and the game leaves
clear) isn't clear. With that, the work makes no call to the original's code.

**The arithmetic.** The work multiplies and divides plainly (`effect_times`,
`effect_over`), with no carry in or out. The long arithmetic's cells are game
state (`arith.h`): a division divides the high word the last call left with
the number it is given, which is how a product just made is divided whole,
and the random draws divide by that high word too. The carry the original
comes into a call with matters to the game in one place, and the port names
it: the evasion's level bonus (`level_bonus` in `effect_amounts.c`) divides
twice with the carry set, so the second division divides a number of over two
billion and the bonus is its quotient's low byte, not the levels over twenty
(`battle_divide_leftover_marked`). Two more leftovers move a result and are
kept with a comment: the defence's quarter steps multiply with the high byte
of the last number the arithmetic worked on, and the element's scale of 4 is
set a byte at a time over the high byte the hit's division by 256 left, so it
is 260 as a rule. The shares a formula's random part is drawn up to are
multiplies (77/256, 205/1024), but for the tenth of a word, which the
original's shifts round part by part.

**What the machine still wants, as scaffolding.** A multiply leaves the carry
its caller came in with in its second number's cell, which only the
original's code reads (a party caller divides by it, which the effects
check's runs found): the multiply leaves 0 there, its entry the machine's
carry, and the few callers in the work whose carry is set and whose multiply
is the last before the work returns say so in `ORIGINAL_EFFECT`
(`battle_leave_multiply_carry`). The kinds hand the party's code back the
carry of their last note (set), A's high byte clear and Y past the note's
bytes: the entry wrapper sets them. The hits' numbers added up leave the carry
of the compare that chose the last kind to move, worked out once after the
adding from the two records' kinds (`leave_last_compare`), and a target after
the first is taken with the carry the note before it leaves
(`take_next_target`).

**The overflow flag, declared dead.** The original's adds leave the processor's
overflow flag as their last result had it, and the effect worked out for an
enemy hands it on to the action's showing (`$C1:BFA4`), the battle's step
(`$C1:0003`) and what follows the action (`$C1:B762`), which save it with the
flags across their frame waits. Nothing tests it: there is no `BVS` or `BVC`
in the battle's banks (the menus' are the ROM's only ones), and the one `PHP`
the code after reaches whose byte isn't put back by `PLP` (the sound driver's,
`$C7:0A39`) throws it away. The liveness analysis agrees where it can follow
the code, and the work-outs declare V don't-care (`DC_V`) on that; but the
strict solve the call-outs are held to gives up at the jump tables the
animation scripts (`$C1:75D1`, `$C1:7633`) and the enemies' scripts
(`$C1:874E`, `$C1:B006`) dispatch through, whose targets come from data, and a
fuzz run can't settle it (the showing waits on frames past any budget). Two
new pieces of evidence do. `tools/v_readers.py` solves liveness as strictly,
but follows each of those four tables to every word its dispatch can index:
each is `ASL A` on an 8-bit A, `TAX` and the dispatch, with nothing jumping
past the `ASL`, so where A's high byte is clear X is at most `$FE`, 128 words,
each code the analysis traced. The bound is an `index_reach` hint on each
table in `re/hints.toml`, and the tool checks every part of it, A's high byte
too: `tools/disasm/high_byte.py` follows it through every path and call the
analysis traced, and has it clear at all four `ASL`s, resting only on D's
high byte being zero at the `TDC`s that clear it (the battle's direct page is
`$0000`). `tests/test_v_readers.py` holds the hints to the ROM. The two
condition dispatches after the first (`$C1:8769`, `$C1:B021`) carry no bound:
44 of the routines the first's words reach return with A's high byte unknown,
and V is dead into the three callees without them. `ct_dev --index-check`
records X at the four dispatches over the library on the interpreter alone:
never past `$FE` (135,810 runs, the largest X `$7E`). With the bound, V is dead
into all three callees. `ct_dev --poison-site` inverts V at a call-out's own
site in the shipping mode; the library with it at the three declarations'
sites traces the same as without in every movie, the picture included, but
for the stack's page, where the inverted flag the callees push lies after they
pull it. Per site: 441 calls into the showing, 6,440 into what follows the
action, and of the step's six sites 285 from the menu's (`$C1:856D`) and 4 to
32 from the other five, which rest on the static result alone. The three
call-outs declare V don't-care with a `dc-evidence:` note naming both and the
per-site counts, which `tools/dontcare_check.py` reads and re-checks
(PORTING.md, "Declarations liveness can't prove"); `tools/dc_evidence.py`
makes the runs again. The work computes no overflow.

**What it found.** The drain reads its stat's offset as a word from the cell
its kind is handed in; a modifier that fells its target outright marks it by
battle slot from `$AEB0`, so a party member's mark lands in two bytes before
the enemies' (`battle_beaten_marks_low`) or on "any enemy beaten"; about a
tenth (`$C1:C1B2`) is three subtractions, not four; formula 8 works nothing
out and takes whatever the quotient cell holds; a number's place is a byte,
so from the fourth hit of an effect that hits repeatedly a set of numbers
reaches past the records into the effect's own flags, as the original's does;
and once one of those hits lands, the rest are made sure by setting the
effect's flags to "can't miss" alone, so they lose the status chance and the
barrier and shield passes the first had.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the enemies' work-outs 347 techs and 23 attacks (175 and 9 compared whole,
the rest interrupted), and through the party's code the kinds 765 hits, 86
items' restorations, 60 restorations, 39 statuses, 26 drains, and a few cures
(229, 48, 52, 35, 23 compared; 32 hits turned away, the party's steals and
clock). The repeated hits, the item's status, the lost condition bit and the
showing of the actor's HP aren't reached by any movie.

**Fuzzing.** Every new entry, seed 1, 5,000 states each on `gato-battle`
(`tools/fuzz_routines.toml`: an actor and one to eleven listed combatants at
a set of numbers and a hit the showing has, the modifiers among those the port
has, the actions from the ROM's records, `$1B` clear), none mismatching; all
5,000 compared but the hits (2,508) and the repeated hits (2,546, one to three
of them), whose other states are steals turned away. The enemies' turns around
them (`fuzz_compare.py --subsystem battle`, 356 routines) none mismatching.
Fuzzing found what shadow mode couldn't: the drain's word read, the beaten
mark, the tenth's subtractions, the record decoded by the kind the entry runs
(not its first byte), the defence's quarters taken from the flags as the work
goes (not the record's), and a set of numbers from the fourth repeated hit on
reaching the effect's flags.

**Timing.** Calibrated from the shadow runs, battle's rows only (the kinds
22,770-45,350, the work-outs 14,758 and 16,958, the notes 21,091,
`battle_multiply_16x16` 29,285, the enemy's attack's turn 8,519); every entry
within the limits with no exception added. `tools/timing_gate.py` takes an
entry some report measured no stretch of (all interrupted) without failing.

**The routes.** The new timing moved the `bucket_lavos` route's core fight onto
a turn where the bot's A in a tech's target cursor didn't take, and its backing
out pressed B four times back to back, which the game reads as one press: it
left the tech list open and waited on it for good (the game takes the tech
from the same state, in the original's code too). The bot now releases between
presses and waits for each to move the menu (`tools/movies/bot.py`), and every
route reaches its outcome in the shipping mode.

## Phase 3, battle chunk 3: the statuses' timers (`$C1:8876`-`$C1:8C08`)

The third battle chunk was picked from what the enemy turn and the main loop
still called out to for being whole on its own: the twelve handlers the main
loop runs for a status's timer that has run out (the original's table at
`$C1:B92D`), 914 bytes, whose callees were ports already (the damage and
number records, the long division, the numbers' clearing) but for the
action's showing and the effect's start. The party's working out
(`$C1:D523`) was the other candidate the coordinator named; it brings the
steal's item and the clock modifier's controller read with it, each a step
of its own, and waits for chunk 4.

`game/battle/status_timers.c` is the handlers as one module: a C table says
what each kind of timer does when it runs out -- wears its status off on the
tenth time (stop, haste, evading much better, the shield, the barrier; the
slow's timer, which wears off without taking the slow away, as the
original's does), poison hurts, a point of HP is lost, MP comes back on the
tenth time, or nothing (three kinds) -- and which status bit it wears off.
Each run the timer starts again, at its length, or paced like a turn (half
as long hasted, twice slowed; the MP's timer asks only about kept haste: the
original loads the status byte and drops it). What a status does is recorded
where the action's showing finds it (`effect_show_status_damage`,
`effect_show_magic_regained`, `effect_record_status_damage`, in
`effect_amounts.c`, as a hit's is), and the handler's wrapper calls the
showing (`$C1:AC57`, a sibling, with the frame of `$C1:895B`, which calls it,
on the stack) or the effect's start (`$C1:EC7F`, the original's). The search
(`battle/timers.c`) runs them as siblings, as the loop's `JSR` calls them;
the combatant a handler works on is the one before where the kind's search
goes on in the order (`battle_timer_found_run_out`), and seeing to it clears
the kind's due bit (`battle_timer_seen_to`). New in the map: the runs left
before a status wears off (`battle_timer_runs_left`, `$B045`), and two
status bits named for what the battle does with them (the first byte's `$10`,
a point of HP each run; the fourth's `$20`, 5 MP every tenth), which also
names the bits `start_timer_for` (`effect_statuses.c`) tested by number.

Two leftovers are kept, as the game plays them: poison divides the long
arithmetic's leftover high word with the most HP, by a divisor whose high
byte is what the last long arithmetic left in its cell (the original sets its
low byte alone, `battle_wide_math_second_high`). And two things the original
leaves are scaffolding: the stat block's place in `$10` (the handlers that
act), and the carry the damage is recorded with, which the multiply leaves in
its cell (a shown number's place's add, or the point of HP's pacing shift;
finding the stat block's place clears it otherwise).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the barrier's 214 calls (210 compared), the shield's 168 (166), the haste's
112 (111), poison's 62 (51, the rest compared in the search that ran them),
the point of HP's 165 (all compared in the search), the stop's 12. Shadow
mode first found the `$10` the handlers leave. The evading much better's
handler is called four times (`gato-battle`, `endings-telepod-lavos`) for no
combatant: the search, reading past the order's last filled place, takes the
empty place's `$FF` as a slot whose timer (past the kind's row, in the
lengths' table) has run out, and the original's handler then writes past
every table it touches. The guard turns those away to the original
(docs/TODO.md). No movie reaches the slow's, the MP's or the three that do
nothing. **Fuzzing**: every handler, seed 1, 5,000 states on `gato-battle`
(`tools/fuzz_routines.toml`: each kind's search one to eleven places on in
an order of the eleven slots), none mismatching; all compared but poison
(3,767; its showing waits on frames past the budget) and the MP's (4,991).
Fuzzing found the point of HP's recorded carry: the pacing's shift, or clear.
The search that runs them (`battle_dispatch_expired_timers`), 5,000, none
mismatching.

**Timing.** Calibrated from the shadow runs, battle's rows only: the
handlers that wear off 4,853-5,116, poison 25,065, the point of HP 15,635;
the three the movies don't reach take their siblings' (the slow's and the
evading much better's the shield's, the MP's poison's); the three that do
nothing keep the default. The overflow flag the showing is called with was
already declared dead at its own site (`$C1:AC57`), so the handlers' showing
needed no new evidence; `tools/suite.py --evidence` ran the notes' library
runs again with the handlers in C, and every note's sites still made what
they cite.

**After the review.** The review passed the module ("mostly") and asked for
one vocabulary. The timers are named by the status each times (`timers.h`;
kinds 6, 7 and 11 keep neutral names, docs/TODO.md), and starting one, its
count and its length are operations of `timers.c` (`battle_start_timer`,
`battle_set_timer_count`, `battle_timer_length`), which `effect_statuses.c`
calls too; `battle_timer_lengths` is two-dimensional in the map, kind by
slot, as the counts are. The handlers' table says what each kind does, which
status it follows and whether wearing off clears it (the slow's doesn't), and
the search dispatches through a table of the handlers beside it rather than
a switch (rubric item 100). The number's showing, `$C1:895B`, is an entry
of its own, run from poison's and the MP's handlers with `battle_run`, as the
original's `JSR` calls it (item 101): 12 calls over the library, the rest
compared in the handler that ran them. The empty place of the order is
ported the way the absent party member was: a timer's slot of `$FF` reaches
past its kind's row through the unchecked accessors (`battle_timer_counts`,
`battle_timer_runs_left`, `battle_timer_lengths` declare why), so the
evading much better's four calls for nobody run in C and are compared, none
mismatching, and the guard turns away nothing real. Fuzzed again, every
handler with the order's last place empty as well (`variant = "nobody"`),
none mismatching.

## Phase 3, battle chunk 4: the party's working out (`$C1:BF79`, `$C1:D523`)

The fourth battle chunk is the party's side of an action's effect: a party
member's tech or attack worked out (`$C1:BF79`, which runs `$C1:D523` and the
check of whether the members can take part, `$C1:D74F`), the item a member
uses (`$C1:BF46`), and the two pieces the effect work turned away until now,
the steal and the clock's modifier. With it every action's effect is C from
the member's or the enemy's turn to its showing.

`game/battle/party_work.c` works a party tech out as the game has it. A tech's
record (`PartyTech`, `battle/effect_data.h`) names its members and has a part
for each, an effect of the party's table with a modifier. Each member must be
able to take part: not kept from techs, with the MP their part costs, halved
or quartered by the two accessories that cut it (`battle_member_accessory`,
new in the map, kept as the battle starts). A tech that can't be done does
nothing unless the member's action goes ahead whatever
(`battle_action_goes_ahead`, bit 7, where a status picked it); what the check
found is kept for the member's turn (`battle_tech_blocked`). Each part then
runs its kind as the enemy's effect does (`effect_run_kind`), its member the
actor; a member's attack, worked out as the character's attack tech, aims
anew at an enemy still in the fight where its own is out, and ends the work
where there is none. The hits' numbers are added up as the tech says. The
work returns how it came out (`TechOutcome`): every part, can't be done,
stopped at a miss, nothing to aim at, a member in no slot; the entry derives
the carry it hands back from it. The item a member uses is one kind's work,
chosen by the item's use byte.

`game/battle/effect_steal.c` is the steal (`$C1:E9D5`): its chance tripled by
the thief's accessory, then the steal's modifier -- a charm's chance
(`$C1:EFC3`, both of the party's steals have one) -- picks the enemy's charm
where it still has it, or else the item it drops; the enemy's marks say
which it still has. What it takes goes to the party (`$FD:B655`): most items
to the inventory, and the few whose item record says so kept apart for the
thief (`battle_member_kept_item`, `$A0B8`, which the battle's end moves on).
The steal kind (`$C1:D3BF`) is an entry beside the other kinds.
`game/battle/inventory.c` is the battle's add to the inventory (`$C1:D005`),
which the rewards and the field's chests reach as well: one more of a held
item up to 99, else the first empty place, over 256 places as the original
searches (the inventory's own 242 and the fourteen bytes after, which the
map's inventory declares `unchecked`); a full inventory puts the item in the
first place, over what is there, as the original's does.

The clock's modifier (`$C1:EE27`) runs the frame's input work again mid-work
(the controller read, the button assignments and the play time's tick,
`$C2:8002`), then scales the damage by the last digit of the seconds. That is
the one piece of an effect's work that reaches the host, so the effect work
takes the platform (`effect_run_kind(g, pf, kind)`), and core's input has
`input_take_frame` for it (additive): where the console is still reading the
controller or the reset buttons are held, the port goes on where the
original would wait or reset (docs/TODO.md). The modifier's 5-6 band adds
half of the word in the direct page's first cell, where the damage is meant:
kept, and the cell's writers before it (the healing's amount, the tech's
members, the item's use, the steal's giving) keep it plainly
(`BATTLE_CLOCK_READS`, battle.h).

Two of the original's slips are kept. After a part that missed, the tech
stops where bit 7 is set in a byte the original reads from work RAM by the
tech's record's offset (`$7E:B1F5` plus eleven times the tech, through
`wram_read8`), where no table is. And a member can take part with exactly
the MP their part costs whatever their status. The MP check leaves each
member's MP over the second and third combatants' steal marks, the word the
original compares with; that is written plainly, since a steal reads the
marks of whatever it aims at.

New in the map: the tech's members (`battle_tech_members`, which the enemies'
further targets' ids share), the members' record's place and the part's
member (scaffolding), the accessories, the tech's check and its going ahead,
the kept items, the inventory search's cells (scaffolding), and why the
battle's character slots are read for an id of none. The battle's shared
flag at `$AF23` (`battle_member_acts_for_itself`) is also where the work says
an attack had nothing to aim at, and the steal's giving clears it.

**Validation.** The library's 26 movies in shadow mode, none mismatching, no
guard turning a real call away (counted with the guards instrumented over the
library): the member's tech or attack 494 calls (53 compared whole, the rest
interrupted by the frame's interrupt, which is why the fuzzing below matters),
the items used 94 (51), the inventory's add 16 (all). No movie steals; the
kinds run inside the party's work, no longer as calls of their own. **Fuzzing**, seed 1, 5,000
states each, none mismatching: the member's tech (4,817 compared; the rest
turned away by its guard), the
party's two steals with and without the thief's accessory and the inventory
at random (all), the item used (all), the steal kind (all), a hit with the
clock's and the charm's modifiers (2,492; the rest turned away, the random flags
making them hits that steal as well),
the inventory's add, at random and full (all). A mutation (the steal landing
on a draw equal to its chance) mismatched 2 of 500, so the runs see the
steal's edges.

**Timing.** Calibrated from the shadow runs, battle's rows only: the
member's tech 22,647, the item 21,593, the inventory's add 20,514; the steal
kind, which no movie reaches, takes the hit's.

**After the review.** A sentinel no longer flows through plain logic as a
bare byte (rubric item 103). The search for run-out timers says whose timer
it found as a `TimerOwner`, nobody where it took the order's empty place, and
the handlers' guards leave nobody's to the original again (the evading much
better's 4 calls over the library): the only read past the kinds' rows left
is the search's own, in one place. A party tech's part with no member ends
the tech in the logic; the byte the original reads for it past the character
slots is asked in the guard alone. Both, and the tech's misread byte, are in
docs/TODO.md's "What only the original's memory has, in the battle". The
party's effects are records (`party_effect_record`, `.kind`, `.flags`, an
attack's showing its `damage.formula`), the clock's word has a reader and its
four writers listed (`battle_clock_word`, battle.h), the timed statuses' bits
are one table (`battle_timed_status`, timers.c) that the combatants' timers
and the handlers share, and the MP check writes what it leaves once, as it
ends (`battle_tech_mp_compared`, the steal marks' second use).

## Phase 3, battle chunk 5: a party member's action carried out (`$C1:B967`)

The fifth battle chunk is what calls the party's working out: a party
member's chosen action carried out when its turn comes (`$C1:B967`), the
member's attack (`$C1:BFAA`), the waits after a tech (`$C1:BDB3`), the end of
a member's turn (`$C1:BCE1`, which the main loop calls too for a member whose
status picked its action) and the battle's step for a called-off action
(`$C1:B961`).

`game/battle/member_turn.c` carries the action out. The member is the first
round the party, from the one the main loop names, whose action waits its
turn; it and its combination tech's partners take part, and where any of
them can't act (`combatant_cant_act`, now the combatants' own, which the
combination check shares) the tech is called off for them all (the
original's `$FD:A93C`, a call-out). Otherwise what the member chose decides:
an attack, aimed at its choice and worked out as the character's attack tech
(`$CC:2583`, `character_attack_tech`), then shown; an item, aimed by the
original's targeting (`$C1:C1DD`, a call-out), worked out and shown; a tech
the record aims as one the turn carries out, aimed likewise, worked out, its
MP paid by each member (`$C1:CC71`: cut by the member's accessory, and any
member past the second paying from the third's MP, as the original does),
shown, and a steal's outcome told in a message (`$CD:0021`) naming the item,
which the steal now keeps plainly in `$06` (`BATTLE_ITEM_TAKEN`). After a
tech every member waits longer before its next turn, by tenths of its turn
wait the tech gives each part (`tech_wait_tenths`, `$CC:2BDC`/`$CC:2BEB`); the
original divides the first member's by an empty divisor cell, so it adds
nothing, and sets only that member's full gauge (kept). The turn ends with
the member's turn timer starting again and its record and its partners'
cleared; a choice of only bit 4 (`CHOICE_HELD`, not worked out) leaves
them. The showing (`$C1:AC57`) and the attack are siblings run with
`battle_run`, as the original's `JSR`s call them.

The targeting is a call-out mid-body, so the bridge asks after it what the
port needs: where the targeting left more targets than the list holds, a
target past the combatants, or (a tech it found nothing for) last tech's
members that aren't the party's, which the waits index by, the port stops at
the call's return; and before the item's, the tech's and the attack's
working out it asks their entries' guards (`effect_item_use_ported`,
`effect_member_tech_ported`) and stops at the call where they refuse. Over
the library neither stop fires and no guard turns a real call away (counted
with the guards and the stops instrumented). A member's attack aims anew at
whoever stands in its target's place: the party work's guard now asks that
the place is a combatant's as well.

New in the map: the record's aim, target and what (`battle_member_actions`),
the member's choice as the effect code keeps it (`battle_action_choice`,
which was `battle_action_goes_ahead`: bit 7 is an attack), the partners kept
while marked, two write-only flags, the kept item waiting its move, and the
item a message names (`battle_message_item`, `$7F:0200`).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the action carried out 542 calls (69 compared whole, the rest interrupted by
the frame's interrupt, as the showing waits frames), the attack 131 (10;
the rest compared in the action that ran them, or interrupted), the waits
after a tech 363 (273), the turn's end 223 (218), the called-off step 7 (all).
**Fuzzing**, seed 1, 5,000 states each, none mismatching: the action (4,910
compared; the rest guarded or past the budget), with every choice a tech
(4,921), the attack (2,050; the rest past the budget in the showing), the
waits (all), the turn's end (all), the step (3,224). Fuzzing found the choice
of bit 4 leaving the turn as it is, the turn's end indexing by A's high byte
(its guard now asks it clear, as the main loop leaves it), the attack's
stray place, and that the showing takes A's high byte along.

**Timing.** Calibrated from the shadow runs, battle's rows only: the action
9,762, the attack 17,584, the waits 21,164, the turn's end 7,665, the step
2,350.

**After the review.** One bridge mechanism (rubric item 104): the item's
use, the tech's working out and the attack's tech are siblings run with
`battle_run`, as the showing is, their guards deciding C or the original's
code, and the turn goes on in C either way; the one stop left is after the
targeting (`battle_stop_after_call`, bridge.c), where what it left is past
what the turn reads. The three cells the turn uses for something other than
their names say are named for it in the map (`battle_action_came_to_nothing`,
`battle_turn_member`, `battle_members_taking_part`, each `reuses` the other);
the choice's bits, the partners, the battle's steps (`BattleStep`), the stop
at the first target and cell `$06` (`BATTLE_MEMBER_CELL`) have one definition
each in battle.h, and the steal's outcome one decoder (`effect_last_steal`).
The tech's payment leaves the carry it hands the last multiply once, derived
after the three payments (`leave_payment_carry`). With the attack's tech run
as a sibling, the attack's fuzzing now reaches the showing in nearly every
state, past the budget: stopped at the showing's call, the one difference
left is A's low byte, which the showing doesn't read.

## Phase 3, battle chunk 6: the setup's party helpers, the inventory and the gold (`$C1:CC0B`)

The run chunk 5 named "the party's side of the showing" (`$C1:CC0B`, 1,018
bytes) is not the showing (`$C1:AC57` was C already) but the helpers the
battle's setup (`$C1:FA8B`, the original's) calls for the party, and the
party's inventory and gold behind them. All of it is C now, with the bank
`$FD` routines those helpers reach.

`game/battle/tech_records.c` fills the members' combination techs in their
tech records: what each of the party's slots pays for a double or triple
tech's part, cut by its member's accessory (`$C1:CC0B`, `$C1:CC3E`, entries
the double techs' builder `$C1:CA1A`, still the original's, calls), and a
member's triple tech, the first learned one whose three members are all in
the battle (`$C1:CCCB`), with its partners. The multiply that finds each
part's cost and the tech's members (`$C1:CB93`, now `effect_take_tech_members`)
are party_work.c's.

`game/battle/member_stats.c` sets a member's stats as it fights (`$C1:CE3A`):
its own seven, then each of its weapon, armor and helmet adds the boost its
second record names (`$FD:B141`: some stats by an amount, each up to its most,
the sum taken in a byte as the original does) and something to each of the
four elemental affinities (`$C1:CF69`). It also copies the equipment's own
records into the whole party's blocks, a piece at a time (`$C1:CF15`, with
the records' ranges, `$FD:B033`). The records are bank CC's, through
`battle/equipment_data.h`.

`game/battle/inventory.c`, which had the adding of an item, has the rest of
the inventory and the gold: whether an item is held (`$C1:CFE9`), one fewer
(`$C1:D056`), whether the party has some gold, gold given up to 9,999,999 and
taken (`$C1:D086`, `$C1:D0A2`, `$C1:D0ED`; the gold as one 24-bit number,
and the giving's hold to the most kept as the original does it: a total of
`$990000` or more has only its high byte brought down to `$98`, so the party
can end below 9,999,999), and the request by number the rest
of the game asks them through (`$C1:8003`: a chest, an event, a shop). And
the battle's item list (`battle_item_list`, new in the map): built from the
inventory's 256 places as the battle starts (`$C1:CDFF`), an empty place
keeping a clear record and an item the battle doesn't use (`$CC:0ABC`'s top
bit) left out, the rest moving up (`$FD:AF80`, `$FD:B01C`, which the stolen
item's adding, the original's, calls too). The flag that says an item was
left out is the cell chunk 5 named twice, named a third time
(`battle_item_not_listed`); the map's `reuses` now follows a chain.

Every routine here is an entry, the helpers too (rubric item 101). The cells
the setup works in are the battle's usual ones: those later work reads as it
finds them are written plainly (the stat block's place, the record's, what
the last boost added, the item and its count), the rest through
`ORIGINAL_EFFECT`. New in the map: the party's gold (`party_gold`), the
learned triple techs (`party_triple_techs_learned`, read one past its end as
the original's search ends), the item list; in combatant.h the equipment's
and the rest of the stats' offsets.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the
request 33 calls (all compared), the double and triple tech costs 904 (902)
and 198, the triple tech 93 (87), the item list 31 (3; the rest interrupted,
fuzzed), an item listed 2,410 (2,399) and an empty place 5,929 (5,919), the
member's stats 93 (89), the boost 267 (266), the affinities 267, the records'
copy 124 (110) and places 372 (359), an item held 19, gold covered 1, given 25
and taken 1; one fewer of an item no movie reaches. No guard turned a real
call away. **Fuzzing**, seed 1, 5,000 states each, none mismatching: the item
list over a mixed and a random inventory (all), an item listed (3,760; the
rest at places between records, guarded), an empty place, the stats, the
boost, the affinities, the records' copy and places (all), the inventory's
and the gold's operations (all, the gold about its most and a borrow as
variants), the request from the field's page (all, and the gold's with any
amount), the triple tech (all), the costs (753 each; the rest at places no
records are, guarded). Fuzzing found that the records' place leaves V as its
add does (declared dead); the don't-care check, that the list's entries hand
A's high byte back clear, which the stolen item's adding reads. Running the rewards' gold and
item as siblings (`battle_run`) mismatched the flags the next call-out is
entered with, where the call-outs declare them dead; they stay call-outs,
reaching the ports through the machine.

**Timing.** Calibrated from the shadow runs, battle's rows only: the request
37,631 for its own part (it saves and restores its caller's registers around
the request), each request timed as the routine that answers it
(`NATIVE_TIME_AS`: timed as one row, the requests' different lengths put it
past the timing gate's limit), the costs 5,227 and 5,250, the triple tech 24,425, the item list
15,025, an item listed 3,667, an empty place 2,977, the stats 21,859, the
boost 3,062, the affinities 2,406, the copy 27,611, the places 41,379, an
item held 14,109 (and one fewer, which nothing measures, the same), gold
covered 4,208, given 5,066, taken 4,042. The member's action (7,890), its
attack (11,057) and the party's tech (23,069) moved with what they call now
running as C.

## Phase 3, battle chunk 7: the fight's start (`$C1:FA8B`)

The plan named the battle's setup (`$C1:FA8B`) and the members' tech records
(`$C1:C96A`, `$C1:CA1A`) as the next chunk, on the understanding that every
helper they call was C. It wasn't: the setup calls eighteen routines in bank
`$FD`, part of the battle's run at `$FD:A8A5`-`$B7EB` that wave 1 left for this
bank's chunks. They came with it, so the chunk is the fight's start whole --
3.2 KiB of the original, `$C1` from 30% left to 28% and `$FD` from 39% to 27% --
and the main loop now runs it as C (`battle_run`, as the combination check).

What it is, in the game's terms (`battle/fight_start.c` has the order):

- **Who is in the fight** (`battle/fight_enemies.c`): every stat block cleared,
  the party's members in their slots with each character's slot noted, the
  eight placed enemies with those in reserve out of the fight for now
  (`$FD:B22E`); each enemy's block filled from its 23-byte record in bank CC
  (`$FD:B438`, `$FD:B4D6`), a new accessor, `enemy_record_byte`; and what each
  enemy carries for a steal to take (`$C1:FD60`).
- **The party** (`battle/fight_party.c`): a demo battle's party
  (`$FD:B2DE`, `$FD:B33F`: the characters as a new game starts them, an
  inventory of every item, the demo's HP and MP); each member's block copied
  from its character's record (`$FD:AE52`), Ayla's fists by her level
  (`$C1:FDAE`); the party listed again without a member sitting the fight out;
  the elemental affinities (`$FD:B3FE`); the accessories noted (`$FD:AE99`),
  the weapons carried (`$FD:AEF2`), the defence and attack (`$C1:FC4B`); what
  the accessories and armor do (`$FD:B555`, `$FD:B3EB`, `$FD:B363`,
  `$FD:AEC4`: a modifier, a status to start with, more HP, sharper weapons,
  confusion warded off).
- **The techs** (`battle/tech_records.c`): the records blank (`$FD:AD09`), a
  member's eight single techs (`$C1:C96A`) and its first six double techs
  with a partner in the fight (`$C1:CA1A`), beside the triple tech chunk 6
  ported, sharing one way of filling a record (`start_record`).
- **The timers** (`battle/fight_timers.c`): every timer stopped
  (`$FD:B0D5`), each status's length as a table (`$FD:B4E7`), and each
  combatant's first turn, the soonest one tick away (`$FD:B7EB`).
- **The turns' and rewards' state cleared** (`$FD:B732`), the places
  numbered (`$FD:B223`).

The map gained the characters' records (`character_records`, laid out as a
stat block's first `$50` bytes, the core subsystem's), the single and double
techs learned, the experience and tech points won, the carried weapon's
fields, and eleven cells the start clears that no traced code reads, named as
spares (TODO.md). Two of the original's quirks are kept, each in one place:
the attack takes a byte of bank CC at the member's block's place, where the
original meant the weapon's record, and a demo battle's eighth record lands on
the learned techs.

**The review** rated `tech_records.c` translation and the rest mostly or
hand-written, and the chunk was reworked before it merged. The builders'
logic no longer takes the original's carry: a part's cost is a cost, and the
carries the multiplies are entered with are worked out in leaves helpers
(`leave_single_cost_carry`, `leave_double_cost_carry`, `leave_speed_carry`).
The direct page's cells that only the start writes and overwrites are
scaffolding in each piece, one leaves helper per piece worked out from where
it ended; the start's header names the five cells that survive it and their
last, plain writers (rubric item 109). combatant.h's names for three stat
bytes were wrong, which the start's use showed (the defence is built from
`$37`, Marle's and Lucca's attack from `$3A`): `$37` is now `STAT_STAMINA`,
`$3A` `STAT_HIT` and `$3D` `STAT_ATTACK`, with the effect code, the statuses'
timers and the boosts renamed to match, and the equipment records' bytes in
the block are named there too (`STAT_ARMOR_DEFENCE`, `STAT_ACCESSORY_FLAGS`,
...). The data came through records: `enemy_stats_record`, `armor_modifier`
(the modifier table read as a stat byte and bits, rubric item 110),
`first_turn_wait(setting, speed)`, `weapon_made_for(weapon)`; the byte after
the action's targets is `battle_action_targets_end`; the carried weapon's
three marks are fields, set inside their scaffolding. Accepted as they are:
the six turn-work words `fight_clear_turn_state` clears, written by their
numbers because nothing names what they hold; the attack's sum left in the
arithmetic's cells plainly, which the effects check requires of those cells
though the first turns overwrite them; and `battle_enemy_animation_work`
indexed by enemy for its first eight bytes and past them as the original
clears them, until its reader is ported (TODO.md). A scoped re-review of the
rework rated no file translation; its last three findings (the records'
copies' per-row keeps, a weapon's record size borrowing another table's
name, the TODO line) were fixed. After the rework: shadow mode over the library and every entry
fuzzed again (seed 1), none mismatching, with chunk 6's triple tech, member
stats and boost.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the
fight's start 31 calls, every one interrupted (it spans a frame), so it is
fuzzed; its pieces as the original calls them, the enemies' records 254 calls
(222 compared) and their blocks cleared 254 (229), the members' records 93
(82), the single techs 93 (68) and the double techs 93 (36), the affinities
and bonuses 93 each (all), the combatants listed 31 (13), the first turns 31
(22), the timers cleared 31 (21), the rest 31 each, nearly all compared;
Ayla's fists 1. The demo's characters and the sharpened weapons no movie
reaches. No guard turned a real call away. **Fuzzing**, seed 1, none
mismatching: the whole start 2,000 states, the enemies' records 648 (the
rest the guard's, X and its cell differing), the members' records 3,442,
Ayla's fists 2,504 (the carry clear refused), the affinities 1,791, every
other entry all of 1,000 to 5,000. Shadow mode found one difference, the
double techs' record place, which the original keeps without the tenth
record's offset (its stores carry it); the don't-care check, that the enemy's
loader hands A back to what reads its high byte.

**Timing.** Calibrated from the shadow runs, the new rows only: the pieces
from 3,438 (the demo party's test) to 28,297 (the first turns), the single
and double techs 19,433 and 22,885, Ayla's fists 111,641 (the original divides
bit by bit). The whole start, which shadow mode never compares, takes its
measured ratio over the library, 18,554; the demo's characters and the
sharpened weapons, which nothing measures, a sibling's.

## Phase 3, battle chunk 8: the rewards of a won battle (`$C1:F93E`)

The plan named the combatants' animation commands and `$C1:F205` next; this
chunk took `$C1:F205` with what it belongs to, the rewards a won battle gives,
whole but for their messages: about 1.9 KiB of the original, `$C1` from 28%
left to 25% and `$FD` from 27% to 26%. The main loop now runs all of it as C
(`battle_run`), in fifteen entries.

What it is, in the game's terms (`battle/rewards_levels.c`'s header has the
order):

- **The experience** (`battle/rewards_levels.c`): turned to gold where a
  member wears the accessory that does that (`$FD:B201`); otherwise every
  member still fighting gets the whole of it (`$C1:F93E`), up to 9,999,999
  (`$C1:F908`), and gains levels with it one at a time (`$C1:F623`,
  `$C1:F7B9`): a level grows the maximum HP and MP by the character's steps
  (`$C1:F871`) and six stats by its growth rates, speed not at all. The
  characters in reserve get three quarters and a level at most (`$C1:F9EB`).
  A level-up works on a record, a stat block or a character's record, which
  lay their fields out alike (`Grower`).
- **The techs** (`battle/rewards_techs.c`): the tech points towards each
  member's next single tech, a locked one waiting for its character's
  unlock (`$C1:F205`, `$C1:F39A`, `$C1:F45F`, `$C1:F4DF`); then each pair's and
  the trio's combination techs, once each character knows as many single techs
  as a tech asks (`$C1:F2DB`, `$C1:F533`).
- **The items** (`battle/inventory.c`): the counts of the battle's item list
  back into the inventory, with the items the members chose and never used
  (`$C1:B4E9`, `$C1:B6D1`), as a battle is won or run from.

The ROM's tables come through `battle/rewards_data.h` (the experience per
level, the growth rates and steps, the characters' techs and their costs, the
combination records) and `party_tech_locked`; the map gained the rewards'
cells, three of them reusing the effect code's and the reactions' (the
rewards' member, the combination counted, the third character bit), whose
last values those readers find, so they are written plainly (the effects
check found both). What the rewards note for their messages got its meaning:
the levels and techs noted for each member (`battle_rewards_levels_noted`,
`battle_rewards_techs_noted`), the double techs taught and their count, the triple tech. Kept as
the original has them, each in one place and in TODO.md: the tech points paid
again where a leftover doesn't reach the next tech's cost, the reserve's level
noted in the first member's techs byte, and its count carried as a word.

**The review** rated `rewards_levels.c` and `rewards_techs.c` translation, each
for one function: the experience and the tech points added a word and a byte
at a time with a carry by hand, capped twice, differently. Both now go through
a three-byte record accessor and one named cap each, whose comment says how
the original's differs from a plain minimum (a tech point total just past the
most, with a low word under the most's, stays). The rework besides: the
experience left to share is a pot passed to the level-up rather than the
cell, which only the entries load and store, and the members' loop stops on a
member knocked out where the original would offer it the rest for ever (so the
guard lost that condition); the single techs' place in the messages' list and
the member a combination is noted for are parameters; every loop's
scaffolding is worked out after it and written once, by a leaves helper; the
shared multiply's carry is scaffolding; the growth steps and the combination
records are read by number; the addresses the pieces leave behind, the
character bits and the cell offsets are in one internal header
(`rewards_work.h`); `battle_rewards_noted` is two named views, the levels
noted and the techs noted; and the single techs' cursor is a byte. Accepted
as they are: the reserve's count of levels carried as a word, since the
reserve's byte isn't cleared between battles and gets to `$FF`; the rewards'
member a word, as the effect code's block offset it reuses is; and a
combination's asks as the place in bank CC its record gives. Three rubric
items came of it (111-113). A scoped re-review of the rework rated no file
translation; its residual findings (a knocked-out test made twice, a constant
defined twice, three places built inline, two comments above the wrong
declaration) were fixed, and the arithmetic's first cell, written on each pass
of the stats' and the costs' loops, stays plain, as the effect code reads it.
After the rework, every entry fuzzed again with seed 1, none mismatching.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the experience's share 25 calls (23 compared, 2 interrupted), its level-ups
130 and experience added 129, the HP and MP grown 1, the reserve's share 25
(24), the techs 25 (23), the tech points 8, the combination techs 86, the
items put back 25 (16) and a choice returned 82 (81), the gold 25; a single
tech learned no movie reaches. No guard turned a real call away. Shadow mode
found the carry the share leaves, which the messages' call compares, and the
flags the main loop hands them (their overflow is dead, which the don't-care
check agrees with). **Fuzzing**, seed 1, none mismatching: the share 5,000
states, the level-ups and levels 10,000 each, the techs 10,000, the tech
points 6,657 of 20,000 (the guard's: the member's block and the one noted
differing) and the next tech 10,000, a single tech 5,000, the
combination techs 10,000, the rest 2,000 to 20,000. It found two of the
original's ways the movies never meet: a count of techs known of `$FF` ends
the combination's list, and A's high byte comes into a single tech's index.

**Timing.** A level-up's time follows its path: one that grows the stats
divides six times, and the original's divide goes bit by bit, far longer than
the port's. One ratio put the level-ups over the worst limit (97,287 clocks),
so each piece's work is timed as its own routine's (`TIMED_AS`), the shared
multiply and divide among them, and the new rows calibrated from the shadow
runs: from 135 (the reserve's share, whose work is nearly all its pieces') to
33,106 (the techs); a single tech learned, which nothing measures, takes the
next tech's, 21,182. The gate then passes with no new exception.

## Phase 3, battle chunk 9a: the animation script language, its table and its first half (`$C1:4CF0`)

The plan named the combatants' animation commands next. They are a language:
219 command bytes, 115 handlers from `$C1:4CF0` to `$C1:7A6A`, one channel
per combatant, the other targets and each effect, run by `battle/scripts.c`
since chunk 1 through the ROM's pointer table. This chunk is its first half:
the table in C and the commands that need neither places nor paths, 154 of
the 219 bytes (70 of them the halt), in 69 entries; `$C1` from 25% left to
20%. The other half (places and facings, the
moves, sound and the screen effect) is chunk 9b.

What it is, in the game's terms (`battle/script_language.h` has the shared
vocabulary):

- **The rows** (`battle/script_language.h`, the event engine's shape): each
  file of commands has a table, a row a command byte, with the operation, how
  each of its typed, named parameters comes from the command (an operand
  byte, or fixed by the byte: a speed's number, on or off, a value's kind, a
  slot), the command's length and what the operation needs of the state.
  The runner decodes the command by its row into a `ScriptCommand` and the
  operation takes the parameters it names (`play_animation(g, channel,
  animation, mode)`); it returns a `ScriptFlow` (on past the command this
  frame or next, stay, again, or moved by a call), which the runner takes
  (the original's `$C1:75BB` and its continue flag). The original's handler
  addresses are a separate dispatch table in `battle/scripts.c`, for the
  call-outs and the timing: a command with no row, or whose needs the state
  doesn't meet, runs the original's handler. `$01` and the 69 other bytes
  the ROM's table gives its handler are one halt.
- **The flow and the values** (`battle/script_flow.c`): end, halt, call and
  return (a return place per channel), waits, and the script's 27 variables,
  two marks and its point, which the waiting commands test.
- **Poses** (`battle/script_poses.c`): an animation played looping, or once
  while the channel waits for it, with the effect shown or with a count; a
  mover's own speed or one of eight; drawn on top, hidden, off the
  battlefield's plane, the sprite glided or put back, a shadow, a trail of
  afterimages, attribute bits.
- **Effects and colors** (`battle/script_effects.c`, `script_colors.c`): what
  an anchor follows, the effects shown, an effect drawn with a combatant; the
  flash, its restore and blink, the rotations, the palette animations' slots
  and frames.

A command acts on what its channel animates, one of three kinds
(`ScriptChannel`): a combatant, the other targets' list, or the channel's
effect (`battle_script_effect`, named for the effects' channels it was
first taken for the enemies'). The map gained the four cells starting an
animation resets (the facing an object's and an effect's frames were last
taken for, an effect's animation's place and length), the return places and
the pointer's pad byte; the per-combatant and per-effect cells the commands
keep, declared as row-indexed arrays, are split into what each row is (the
frame an animation is on, the two waits' marks, the blink's countdown and
whether it flashes). The effect animation tables,
anchor sources and effect records take unchecked accessors, for the operands
the original doesn't check. Reproduced on purpose, in TODO.md: the list waits
leaving their mark set, the shadows' and the rotation's lists starting at the
wrong subject, the glide's equality, the counts that count up, the blink
running itself again.

**What the needs are for.** A guard alone isn't enough here: the runner calls
the operations without one. The endings' movie runs a script on a channel
whose subject is none (`$FF`), whose original writes land past the tables (a
scheduled list at `$A2E6`); a row whose needs aren't met there goes to the
original's handler, from the runner and the registry alike.

**What the gate found.** Shadow mode: the overflow flag a C command leaves at
the next call-out, which no handler reads (declared at the call-out, with a
`ct_dev --poison-dc` run as its evidence); the out-of-range subject above.
Fuzzing: a script in work RAM lying over the direct page, whose cells a
command writes before the original reads its next operand there; the
original runs those (`script_over_the_direct_page`), as the event engine
does for a command at its buffer's end.

**The review** rated every file "mostly", none translation, and asked for two
things first: the table to decode operands into typed parameters (the first
version handed every operation a polymorphic `form` byte and read operands
by position), and the work cells to be scaffolding (they were written
plainly for one reader, stopping a trail, which itself only sets dead
periods). Both are done as above: everything the commands leave in the first
work cells is written by a leaves helper under `ORIGINAL_EFFECT`, and
stopping a trail sets its periods from them only there, the trail's two
readers answered in the effects baseline (they read only while the trail is
on). Besides: the three waits are one `play_and_wait_as` with a `WaitStyle`;
`$26` on an effect channel, which no script does, is the original's to run;
the flash's, the palette sets' and the palette frames' accessors take a set
or a frame, not a byte offset; the cycled background's painting is the rule
it is, its leftover count worked out after; one `ScriptPlace` arithmetic for
the advance and the call's return; the held addition a clamp; the registry
names follow the operations. Accepted: the number display's commands
(`numbers.c`, chunk 1) keep their own pointer advance, which differs only for
a script past the end of a bank; they move to the rows when 9b reaches them.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the 69 entries were entered 1,978,926 times (1,973,533 compared, 4,973
interrupted; the rest turned to the original by their needs, the endings'
subject among them), the waits most (the second mark's 311,758, the shown
wait's 105,129), the channel runner 1,017,429 (3,363 interrupted). Eleven
entries no movie reaches: the first mark's set, count down and sums, a
variable stored from it, the swaps, an anchor let go, the return at the top
and the skip. **Fuzzing**, seed 1, none mismatching after the fixes: every
entry, 186 to 469 states each of 8,000 (the guard's: the channel and its
offset differ in fifteen of sixteen), 30,157 in all. It found the blink's
byte turned over by its low bit (a leftover 3 turns to 2, which flashes) and
the direct page's mirrors in the upper banks.

**Timing.** The entries' costs come from the shadow runs, from 1,938 (the
halt) to 6,943 (a flash); the eleven no movie reaches take a sibling's (a
mark's sum a variable's). The full gate then passes with no new exception.

## Phase 3, battle chunk 9b: the animation script language's second half (`$C1:762E`)

Chunk 9a put the language's table in C with the commands that need neither
places nor paths (docs/history/reimplementation-chunks.md). This chunk is the rest: the
places and facings (`$C1:762E`, `$C1:75CC`, their 97 handlers), every move,
the grid walk with its search (`$C1:4A71`), the sound commands, the screen
effect `$80`, and the number commands, which moved onto the rows: 64 command
bytes in 46 entries, about 8 KiB of `$C1`. Every command byte now has a row;
the ROM's handler table is only the fallback for a state a row's needs turn
away.

What it is, in the game's terms:

- **Places and facings** (`battle/script_places.c`): a place is one of 72
  points a command names -- where a channel's subject stands or is aimed at,
  halfway between two, the middle of three, part of the way from one to
  another (eighths from a ROM table), 24 pixels along a mover's heading, the
  battlefield's middle, where the first three began, where an anchor draws, a
  point kept in the variables. The battle keeps the place last found (one
  place names it), and the facing likewise. The commands that set the
  script's point, an animation slot's place and a mover's heading are here;
  `battle/objects.c` now works the animation slots' places out as C.
- **Moves** (`battle/script_moves.c`): lines, chases, jumps and short hops,
  spirals (open, counted, and stopping at walls and other combatants),
  orbits and flat orbits, the throw (aimed as a jump onto a place, an arc's
  top worked out, a rise in a line, the 36 frames shared between the waits
  before and after), the flicker, and a point some steps along the line to a
  place. A move sets up the mover's path (`battle/effect_paths.c` runs it) and
  waits for its end; the mover vocabulary (`Mover`, `PathKind`, where a
  mover is as a `BattlePoint`) moved to `battle/effects.h` so the paths and
  the commands share it.
- **The walk** (`battle/script_walk.c`): a ring-by-ring search out over the
  battlefield's 16 by 16 grid for the way to a point, marked back from the
  goal, and the walker's own copy of the marked grid, stepped a scripted
  move at a time. The search's state is locals and what each ring returns
  (`Ring`); a step returns where it saw the goal, if it did (`WalkStep`);
  the cells the original leaves are worked out from those results, once, by
  a leaves helper.
- **Sound and the screen** (`battle/script_sound.c`): the sound commands are
  the language's first bridges: their rows only decode them, and a table
  beside the dispatch in `battle/scripts.c` names the bridge each runs as its
  handler's entry through `battle_run` (the driver called through the audio
  service), so the language's rows stay free of the machine; `$80` hands the
  screen effect its settings, whose core `battle/screen_motion.c` now has as
  C (`battle_set_screen_effect_to`).

**The third trap, settled first.** The chunk's only call-out left is the
sound driver's (`$C7:0004`), whose don't-care set is liveness's (`V` dead
into it and after, by `tools/v_readers.py`). The arithmetic, the direction,
the boxes and the grid were already C and are called as C.

**What the original leaves.** Its handlers keep operands, loop places and
partial sums in the battle's first work cells, written by leaves helpers as
scaffolding; three cells turned out to be read back as game input:

- the velocity a line is worked out with (`$A880`/`$A882`), only within the
  set-up: scaffolding;
- the jump lift (`$90`, `battle_jump_lift`): the original's jumps to the
  script's point take what is there, a cell shared with the animation slots'
  loop count and the walk's search; a probe over the library found it always
  6, the count the loop ends at each frame. The rows lift by that as a named
  constant (`POINT_JUMP_LIFT`), and their needs leave the jump to the
  original while the cell holds anything else, so every write of the cell
  stays scaffolding;
- the operands a chase and an orbit at a place keep in the cells the place's
  working out uses: a place that uses them overwrites the heading, the circle
  and the quarter turns the command then reads. Those places, which no movie
  has a chase or an orbit take, are left to the original
  (`NEEDS_SUBJECTS_AND_A_QUIET_PLACE`).

Reproduced on purpose, in TODO.md: the middle of three's Y taken from the
X sums; a channel's move length kept per channel and gone past whatever
command it is at; the other targets' forms (lines set every target off and go
on at once, spirals out set them off without marking them moving, orbits go
on when none has ended, the chase's own heading taken from the first for the
rest); the anchor's rise in a throw reading its speed at twice its number;
the place along the heading reading the running channel's subject as a word.

**What the gate found.** Shadow mode found the cells the original works a line's velocity out in (`$A880`/`$A882`), which only the set-up reads, and a put's length left in its work cell. Fuzzing found the anchor's rise leaving
its moving flag alone (the combatant's is stopped), the chase's and orbit's
operands overwritten by a place, and the slots' places loop reading anchors
past the eight for a running channel past the sixteen (now the original's).

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the 46 new entries were entered 234,643 times (230,367 compared, 542
interrupted), the line to a place most (44,382), the number commands next
(55,134); the channel runner 1,017,429 times and the animation slots' places
281,498, both compared as before. Three kinds of call aren't compared on
their own: the sound bridges' (3,472), set aside inside the channel runner's
comparison while the driver's call-out runs past them (the runner's matched);
the 180 calls of the throw's last step entered by the throw's wait falling
into it, which its guard leaves to the original as the handler its byte
names isn't it; and 82 jumps in `endings-beyond-time` on a channel whose
subject is none, which the rows' needs leave to the original (TODO.md). Five
entries no movie reaches: the chases and walks to coordinates and to the
point, and the point along a line. **Fuzzing**, seed 1, none mismatching after
the fixes: every entry, 90 to 509 states each of 8,000 (the guard's), and the
search alone 8,000.

**Timing.** The entries' costs come from the shadow runs, from 2,667 (a
sound effect's own work, the driver's being its own) to 7,858 (the numbers);
the five no movie reaches take a sibling's. The walk's search is timed as its
own routine (`TIMED_AS`, `$C1:4A71` registered for it, 10,757), and so are the
screen effect's three kinds, which put its worst stretch and the channel
runner's within the gate's limits; the runner and the slots' places loop were
recalibrated. The full gate then passes with no new exception.

## Phase 3, the scene program: the jet bike race running (`$7E:416D`-`$7E:5220`, `$7E:B574`)

The first chunk of bank `$7E`, the program the field and the game over
decompress to `$7E:3000` and run there (DISASSEMBLY.md, `[[ramcode]]`), on
top of the scene engine (`game/scene/`).

**How it was chosen.** The program dispatches sixteen modes through its table
at `$7E:3043`: 0 and 9 the race (9 the attract demo's), 2 the title, 10, 11, 14
and 15 one ending's code (`$7E:8538`), 12 and 13 another's, and modes 1 and 3-8
no movie reaches. Of `$7E`'s 24.9 KiB, 92% was generated; by the coverage
files the race is reached by `events-jetbike-race`, the two endings by three
movies. The race while it runs is the largest coherent piece: the tasks its
scripts put on the two lists between `$7E:416D` and `$7E:5220`, and the
routine it generates at `$7E:B574` to regroup its scaled pictures (5,153
bytes, which the endings run too). Left at the edge, to be the race's next
chunk (PLAN.md): the bikes placed in perspective (`$7E:4D0D`, which searches
for its divisors through the CPU's divider with the carries of a dozen sums
running through it), the main loop's scaling of the pictures (`$7E:51D1`,
which stops at its own wait and hands a carry to a loop that reads it), the
perspective table (`$7E:357B`, whose code the race and the endings patch), and
the setup and its loader.

**What it is.** `scene/race.h` names what the parts share: the two bikes as a
record of `$25` bytes each on the scene's page (`scene_race_racers`), channel
0's registers as the camera (height, place, distance, angle, pitch, speed,
lean) and channel 1's as the racers' sizes. `scene/race_rules.c` is the bikes:
speed lost and gained toward a top speed that the lead picks (Johnny's is
higher behind), the boosts on B with the gauge flashing in the road strip,
Johnny's controls (blocking while he leads, swerving away when Crono comes
alongside ahead), the drifts across the track, the lead in 32nds, whether they
touch and how they bump (a bump along swaps speeds, across swaps drifts), the
score, both finishes (Johnny's: the markers stopped, the camera following and
the score drained; Crono's: the result and what the score earns kept for the
story at `$0380`-`$0383`, the victory song, the coast to a lane), and the end
of the scene. `scene/race_camera.c` is the camera: its place along the track,
its offset turned by its angle (`scene_multiply_signed16`, the program's
`$7E:33D6`, now in `math.c`), the flat layers' scroll and the horizon's split
of the per-line tables, the pad's directions given to the lanes by the eighth
of a turn the camera faces, the demo's turning and raising, the perspective's
check (it stops at `$7E:357B`), the controller kept, the settling after the
finish and the opening picked at random. `scene/race_drawing.c` asks for each
racer's scaled picture and draws its sprite list over the tiles, the nearer
first, and the panel: the distance bar's markers (placed by the multiplier),
the boosts, the distance to go and the score (`$7E:39F5`, a number as
sprites). `scene/race_matrix.c` gains the generated routine itself: the
regrouping of a scaled picture into sprite tiles, as a loop over the
generator's own table (its header's account of what the code is for was
wrong, and is rewritten). `scene_race_move_camera` now runs to its return (it
stopped at `$7E:442E`). The race's effects are the platform's driver call
from `race.c`, the one file of the race the lint allows it; the sprite
routines and the arithmetic are C calls, each timed as its own entry
(`TIMED_AS`).

**The registers, settled first.** Liveness has A's high byte and Y live after
every frame task: the next task on the list may read Y, and the last slot's A
goes out through the frame's work. So each task leaves A as the original does,
worked out after the logic from the state it left (scaffolding), and Y alone
but where the drawing's sprite routines leave the next free sprite. Two tasks
take in the carry the list's loop leaves, which is clear but in its first
slot; the drawing's guard asks for it clear (the race's list never puts it
first). The effects' calls through the program's send (`$7E:377E`, which waits
for the driver) declare A, X and the flags dead, which liveness proves at both
sites. The stops into the list's own code (`$7E:39B1`, `$7E:39A7`) and the
perspective's keep what those read, as their own sets (`m_stop_at_own`).

**The carries.** The race never clears the carry between the sums of a frame's
motion, so a drift's build-up and loss, the lead, the touching band, the boost
and both kinds of bump each take a unit from the sum or comparison before
them. The first version threaded that carry through the rules; the review
rated `race_rules.c` a translation for it, and it was rewritten. Each rule is
plain arithmetic with one term for its unit (a drift a unit stronger or slower
to die, the lead a 32nd shorter, a bumped bike a unit farther), and one
section, "The units the motion hands on", works each unit out from the values
the step before used, in the order the original's sums run (`drift_units`,
`contact_units`, `bump_along_units`, `bump_across_units`). A bump along is one
32-bit distance; the original also takes the push's high word, 0 in any real
bump, off both halves of the other bike's distance, which stays as a term.
Three units that are constant wherever the game's values are became accepted
divergences, written at the entries and held by the fuzz constraints: the
boost's (whether Johnny is still speeding up, where the original compares his
speeds unsigned), pulling over after the finish, and the markers' place on
the distance bar. The same review had `race_drawing.c` work out the
arithmetic cells it leaves from the state afterwards instead of threading
them, compute the markers' product in C (the multiplier's last state left as
scaffolding before the panel's numbers use the divider), and name the pose's
backward turn that lengthens the bar by a pixel.

**Program data and patched code.** The race's tables live in the program
image, and the race and its setup write some of its operands: the demo's
finish lanes (`$7E:46CE`, `$7E:46E4`), the distance bar's ends (`$7E:4F74`,
`$7E:50DA`), the distance shown's lag (`$7E:5171`), the return Johnny's win
writes over the markers' call (`$7E:510D`), the horizon's line counts in the
per-line tables. Each is a variable in `re/ram.toml`, as are the racers'
record and the arithmetic cells' fields for the bump's push and the pose; the
guards hash the code around them.

**What shadow mode and fuzzing found.** Over the 26 movies every call matches,
none refused: in `events-jetbike-race` the camera's tasks, the horizon and the
drawing 2,640 calls each, the ride 2,276, the pad's directions 2,349, the
following camera 163, the score's draining 3, the opening 1; the regrouping
948 there and 3,025 in `endings-beyond-time`, of which 1,389 are interrupted.
The movie loses the race, so Crono's finish, the coasting, the victory song,
the settling, the end of the scene and the demo's race are fuzzed only.
Shadow mode found the accumulator's width at two returns (the bump's call
leaves 8 bits, the follower returns in 16; the two waits after the finish,
which no movie reaches, return in 8 too). The first fuzz runs found
the demo's turning clamped unsigned (the original compares by the sign), the
opening's guard on a data bank of 0 (the script calls it with the program's,
`$7E`, and the fuzz runs now do), and the drawing indexing the program's data
with a lean past 2 (the guard refuses it). Fuzzed from the `race` state with
the program kept (`--keep 7E3000-7EC994`; `tools/fuzz_routines.toml`), seeds 1
and 2, 20,000 states each, none mismatching: every entry, the ride three ways
(as it comes, with both bikes' distances at the finish's word, the bikes level
so they bump every way; at most 3 states refused, a task list without room),
the coast with the finish margin near 0 and Crono's lane at the middle, the
victory song's wait with the music playing (two thirds of its states reach
its arrival), the leaving at its last frame, the drawing with up to three
boosts. Johnny's speeds are kept within $8000 of his top speeds and the
coast's lanes in the game's, where the accepted divergences hold.

**Timing.** The new rows were calibrated over the library, with
`scene_race_move_camera` and `scene_race_set_engine_pitch` (which now reads
the speed's high byte as a word). Two vblank-tail exceptions: the drawing
(22-line calls, the sprite routines timed as theirs) and the perspective's
check (the divisors' clear charged as the block move it is), both writing
RAM. `$7E` stands at 10.2 KiB ported, 59% left.
## Phase 3, battle chunk 10: the enemies' movements and the main loop's last callees (`$C1:3772`, `$FD:AB30`)

What was left of the battle in bank `$C1` after chunk 9b, and of its routines
in bank `$FD`, by `python3 tools/burndown.py --uncovered C1`: the enemies'
per-animation effects (`$C1:3772`-`$C1:4021`, 2,278 bytes, the bank's largest
run), and every callee the main loop still called out to but the steps, the
messages and the rewards': a member's status turn (`$FD:AB30`), the counters
(`$FD:AC6E`), the calling off of a combination (`$FD:A8CE`-`$A95F`), the
dropped action (`$C1:B3F9`), the enemies an action knocked out (`$C1:B762`),
the demo's party (`$C1:B70E`, `$B725`), the standing down (`$C1:B7F2`) and the
battle's end's bookkeeping (`$C1:EA9D`, `$EAE8`, `$B442`, `$B3BB`, `$B3D2`,
`$FD:AA98`, `$AAB0`): about 3.4 KiB, all there was by concept. The bank's
burndown went from 7% left to 3%; what remains are runs inside ports that hand
back to the original (PLAN.md).

What it is, in the game's terms:

- **The enemies' movements** (`battle/enemy_movement.c`): an animation names
  a movement, a step of eight pixels every few frames, looked at before it is
  taken (the grid, the other combatants' boxes) and then drifted
  (`battle/effect_paths.c`): approaching to a reach, charging, lining up,
  keeping a distance from the target (the spot on the enemy's side, backed
  away from or closed in on), circling, weaving round a loop below where the
  weave started, marching one way, making for the middle; and stopping to act,
  with waiting for the target to move on. The animation picks each one's
  variant; the drift's cells were renamed for what they hold
  (`battle_enemy_drifting`, `_heading`, `_drift_from_*`). The loop in
  `battle/animation.c` runs them as C, each timed as its routine, with the
  ROM's table only for an animation that names no movement.
- **The party's turns** (`battle/party_turns.c`): a member driven to fight or
  confused attacks at random; a member kept from acting loses its waiting
  action; the counters; the combination called off; the knocked-out enemies
  with no last reaction counted and shown beaten; the demo's party; standing
  down. The steps and the attack run between decisions, so these are bridges,
  run by the main loop as siblings (`battle_run`), each registered.
- **The battle's end** (`battle/battle_end.c`): the statuses cleared, the
  party stood up and kept in its records, the enemies taken off the field but
  for those marked to stay (bit 6 of the placement's kind, which the field
  reads), the enemies counted out when the party runs, and the beaten noted for
  the story (`battle_enemies_beaten_for_the_story`, event memory `$1EE`), all
  C the main loop calls as C.

**The third trap, settled first.** `tools/v_readers.py` found V dead into the
steps (`$C1:0003`), the called-off step (`$C1:B961`), the attack (`$C1:BFAA`)
and the reactions' finding (`$C1:B488`), and after every routine the main loop
called. The library reaches the steps' sites too seldom for a run there to
count (19 calls at the most), so each step's V rests on a `ct_fuzz
--poison-dc` run of its routine, the called-off step's with A's high byte
clear as its callers leave it: random high bytes send the step's dispatch
where V is read (the notes in `party_turns.c` and `member_turn.c`). Where
the main loop's next call-out reads A's high byte after a routine that is now
C, the loop clears it as the original's code there does (the victory's step,
the running away's message, the field's return).

**What the gate found.** Shadow mode found that a sibling run through
another sibling must keep the original's stack: the called-off member
(`$FD:A8FE`) became its own entry so the step's call meets the original's at
its depth. Fuzzing found that the movements and the dropped action find their
cells with A's high byte, clear as their callers leave it (now their guards'),
and that the charge leaves three past the slot the boxes' test handed back.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the
movements' loop 55,081 calls (27 interrupted), the movements 1,985 (waiting
473, approaching 1,244, circling 155, weaving 93, keeping a distance 20; no
movie charges, lines up, marches or makes for the middle); the dropped action
39,836, the combination called off 15,580 and its members 2,522, the knocked
out 4,611 (350 interrupted), the counters 4,154, the status turn 2,810, the
end's bookkeeping 26-29 each (one running away). **Fuzzing**, seed 1, none
mismatching: every entry, 5,000 or 8,000 states (the status turn 666 and the
attack from bank `$FD` 24 compared, the rest past the budget in the attack's
showing).

**Timing.** The new entries were calibrated from the shadow runs; those no
movie reaches take a sibling's cost. The effects' loop, the main loop's body,
the turn, the timers' dispatch and the combination check were recalibrated.
With the turn's callees timed as their own routines, the timers' dispatch (a
whole turn) attributes its time to more than the 32 routines a stretch held,
so `MAX_SEGMENT_PARTS` (`runtime/port/native.c`) is 48.

## Phase 3, menus: the techs screen (`$C2:B35D`-`$C2:BF2E`)

The menus' screen 3, the run the burndown had largest left in the bank
(`$C2:B520`), taken whole with the main menu's panel for it: 2.8 KiB, and the
pointing among the members a healing is used on (`$C2:B83E`, `$C2:B870`), which
the items screen's tonics shared as a call-out. `$C2` went from 28.1 KiB ported
(53% left, 300 entries) to 30.9 KiB (48%, 326 entries).

**What it is.** `menu/techs.c` is the techs as the menus list them: a
member's techs of a kind (each tech group the member is one of, in the groups'
order, and each group's techs the party has learned), what one costs the
member (its part's cost, which two accessories halve or quarter), whether it
heals and what it restores, and a tech's row in a list in the colours of one
usable here or still being learned. `menu/tech_screen.c` is the screen as the
game sees it: the member rows down the left (and their slide past the ends,
the view moved four lines a frame through the screen's offset-per-tile
table), the kinds' tabs, the list of eight rows with its slide and paging, the
tech on the cursor with its other members' rows and their costs, the
description, a healing tech used on the window of active members (the user's
casting pose, the cost taken from its MP), and the panel. `tech_screen_bridge.c`
runs it on the machine: the scroll bars' frames and task, the cursor's moves
within a list, the paging, the list's slide frames, the description's
setting, the stopping of tasks, the restoring and the cursors on every member
stay the original's. The rows' drawing reuses the shared vocabulary
(`menu_draw_windows` and `menu_queue_fixed_upload`, now in `windows.h` and
`vram.h` for every screen; `menu_load_character` beside `menu_load_member`).

**The flags, settled first.** Every call-out the chunk keeps was checked
before the logic: they read memory, Y (the lists' frames) or A (the stopping
of tasks, the wait for frames); the one register a port reads back from one
(`$C2:EA53`'s N, a push past a list's edge) is worked out from the state. What
needed care is the P each routine saves as it starts, compared under its
calls out, and the flags a call-out declared don't-care carries through
unchanged where its callee saves and restores them (`$C2:8255`, `$C2:8AD5`,
`$C2:F643`): the bridge leaves the original's flags before each (the leaves at
the end of `tech_screen_bridge.c`), with the shifts in the healing's targets
(the carry clear), the kind's switch (the carry of the compare before it goes
round) and the rows' copies (the accumulator's high byte, `$34` down the list
and `$FF` up it) the ones shadow mode and fuzzing found.

**What shadow mode and fuzzing found.** Two movies reach the screen (`menu`
and `menus-field-menu`): every call matches, none refused (each entry's counts
are its ports.tsv row), among them 403 frames among the members, 390 on the
list, 222 in the window a tech is used from and 430 pointings at a member to
restore. The library never slides the member list or the tech list. Fuzzed
from four states of `menus-field-menu` (on the members, on the list, in the
window, on the triple techs), seed 1, 2,000 states each, the menus' tables
(`$7E:7700`-`$7FFF`) kept with low RAM and the tilemaps, every entry up to its
first wait for a frame: 0 mismatching. The first runs found the double and
triple techs' cost read and kept a word wide (the next part's cost lands in
the byte after, now `menu_tech_cost_beside`), the list's slide copying with
the copy's direction swapped, and entries the fuzzer entered with 8-bit index
registers where the game never does. The parts table is declared from
`$7E:1607` so a tech's index into it needs no subtraction; a stale list as the
screen opens can't read past it. Changing the items screen's call-out into a
`menu_run` of the shared pointing, and its two member-window loops into one,
left its fuzz counts unchanged against main's (`tools/fuzz_compare.py
--before`).

**Timing.** The rows' drawing is charged at registered entries of its own
(`menu_show_member_tech_list` `$C2:BC6F`, `menu_draw_tech_rows` `$C2:BD6B`,
`menu_show_pointed_tech` `$C2:BB1F`, `menu_draw_tech_member_row` `$C2:BA2F`,
0.2% error), which took the members' step's worst vblank error from 48518
clocks to 10470. Two exceptions remain (`tools/timing_exceptions.txt`): the
pointed tech's rows (`menu_show_pointed_tech`, and `menu_choose_tech`, which
carries it), whose empty rows inherit the window lists' strings' spread (over
by about 1600 clocks a row) while the triple techs' member rows fall under,
holding the routine's own time at the floor.
## Phase 3, battle chunk 11: the screen script language (`$CD:1609`-`$CD:31A9`)

The largest runs `python3 tools/burndown.py --uncovered CD` listed in the
battle's frame work, `$CD:17A9`-`$CD:319D`, are one thing: the 122 handlers
of the screen script language, whose runner (`$CD:1609`) the frame work
called out to, and whose window, motion and palette bodies in bank `$D1`
were already C (battle chunk on the screen effects, docs/ports.tsv). The
chunk is the runner and every command whose handler stays in that range;
it stops where a command leaves the language for a renderer (the circles'
and shapes' at `$CD:31AA`-`$CD:3656`, the triangles' rasterizer `$CD:367C`)
or for bank `$CE`. `$CD` went from 70% left to 39% (4.7K of code).

What it is, in the game's terms (`battle/screen_script.h` says it at
length): a spell that dresses the whole screen runs a script from bank `$D1`
on one of two channels, each with a layer of its own. A byte below `$7F`
shows a frame of the effect's animated graphics and ends the channel's step,
`$7F` ends it, `$FF` ends the script, and the rest are commands that run
within the step: the channel's pace, loops (one counts the frames on),
calls, jumps and switches on the animation scripts' variables, and waits
that jump back to the step's end; the layer nudged eight ways, put at an
animation position, swung, or set on an orbit of three sines
(`screen_layers.c`); two circles placed, widened and narrowed by the
animation scripts' variables, and two triangles, flat (corners at angles
and distances about a center, stretched) or turning in space, or one of the
ROM's models of three points, laid out for the rasterizer with their size
(`screen_shapes.c`); waves and a sway across the layers' lines, and a ripple
down both windows (`screen_waves.c`, `screen_window.c`); and the screen's
settings, colors, palette animations, sounds and the frame work's switches
(`screen_settings.c`). The language is a C table: each file ends with its
rows, which decode the operands into named parameters (a shape, an angle per
corner, a distance back) and say what an operation needs to run as the
original does (a shape one of the two, a position one of the six); the rows
of the commands that are still the original's have no operation, and the
runner calls their handlers as the original's dispatch does.

**The third trap, settled first.** `tools/v_readers.py` found V dead into
every handler that stays the original's and into the rasterizer, and after
the runner returns; they are called with A, X, Y, the carry and N and Z as
the dispatch leaves them, V declared. What the runner reads back of a
command is A's high byte, which the original shifts into the next frame's
place in the graphics: `tools/disasm/high_byte.py` shows every handler
returning it clear but the window fill, the beam and the quadrilaterals,
and of the fills only filling window 1 sets it, which no script of the game's
asks for (a scan of the scripts the table at `$D1:646C` points at), so the
runner hands that fill to the original's handler; where a handler
that is still the original's leaves the byte set, the C runner hands the
step to the original's from the dispatch's return. A byte past the last
command sends the original's dispatch past its table; the runner hands it
back at the loop's head.

**Call-outs where the original makes them.** The commands' call-outs (the
original's handlers, the rasterizer) are compared at the original's stack, so
the runner's three routines are siblings run through `battle_run`, each
registered: the frame's run (`$CD:1609`) pushes the count of ended channels
as the original's `PHY` does, a channel's step (`$CD:1654`) keeps its offset
on the stack, and the dispatch (`$CD:16A6`) runs one command, so a call from
a command meets the original's at `$CD:16AF`. The generic entry
`battle_screen_command` stands at each of the 104 C handlers, so each is
fuzzed alone; one that draws stops at the rasterizer, as the handler's jump
does.

**What the gate found.** Shadow mode found the original's leftovers in the
battle page's cells: a wait's distance back as a word in `$45`, the operand
the change of a variable and the corners' limit leave there, and the
palette animation's raw operands. The triangles' sizing (`$CD:211B`) stores
its operands to the multiplier as words where the corner moves out: the
product is read as a word, so the controller's low byte (`$4218`) comes along
as the high byte of the corner's word in `$4B`, and the size's word store
puts `$22` in the divider; the runner reads the controller for that one
cell, as scaffolding. The models' rotation is handed the size as a word whose
high byte clears the low byte of `battle_turn_slot` (`$22`), a real write the
port keeps. Fuzzing found that the runner must hand a channel's step the pace
in A, as the original's `TDC` leaves its high byte, and that the jitter takes
the dispatch's carry into its first pick, which the commands' guard now
requires.

**Validation.** The library's 26 movies in shadow mode, none mismatching:
the frame's run 431,221 calls, the channels' steps 129,354, the dispatch
403,323, the commands 1,260,637 in all, none interrupted; 22 commands no
movie reaches (the ones the scripts of the library's battles don't use, among
them the orbits, the swing's start and the models that spin). **Fuzzing**,
seed 1, 5,000 states each, from the `battle` state, with the operands as
random bytes in work RAM and the guards' bounds kept (the shape, circle,
slot and position one the language has): every command 0 mismatching of
about 2,500 compared (half of the states turned away by the dispatch's carry);
the dispatch 0 of 4,778; the frame's run 0 of 4,819 and a channel's step 0
of 4,876 over random scripts. The step's first run had one mismatch: a
random `$8C` with an operand of `$0F`, whose original handler jumps through
its table of four swing words by the operand and on to `$CD:6020`, code that
isn't a handler, which ran off with the interrupt flag cleared; no script of
the game's has `$8C` or `$8D`, so a step that comes to one naming no word is
handed to the original's runner at the dispatch's call. The commands' constraints are in `tools/fuzz_routines.toml`.

**Timing.** The new entries were calibrated from the shadow runs; the 23 no
movie reaches (or whose stretches end in the rasterizer) take a sibling's
cost.
## Phase 3, the scene program: the rest of the jet bike race (`$7E:3B48`-`$7E:3F13`, `$7E:357B`, `$7E:4D0D`, `$7E:51D1`)

The second chunk of bank `$7E`: what the first left at its edge (history
file, "Phase 3, the scene program: the jet bike race running"), so that the
race is C from its setup to its end.

**How it was chosen.** The race's remainder, all reached by
`events-jetbike-race`: the bikes placed in perspective (`$7E:4D0D`, with
`$7E:4D4E`-`$7E:4F53`), the perspective table (`$7E:357B`, with its patched
call's two targets, the race's `$7E:45B0` and the endings' `$7E:9B46`), the
main loop's scaling of the racers' pictures (`$7E:51D1`-`$7E:5220`), and the
setup (`$7E:3B48`-`$7E:3F13` with the bikes' start `$7E:45D6` and the
divisors' clear `$7E:4309`) ported whole, which retires the eleven entries
the first chunk had registered a block at a time (the buffers' clear, the
flat layers, the racers' map and the loader's eight blocks). With it the
curtain the race without its panel ends behind (`$7E:3D72`, a task no movie
runs). Left at the edge: the program's unpacker by resource (`$7E:3691`,
before the decompressor) and its first script step (`$7E:3749`, before the
sequencer), both call-outs of the setup; the curtain's step (`$C3:000E`, the
field's, still the original's); and the ending shared by modes 10, 11, 14
and 15 (`$7E:8538` on), which is a loader of the same kind and the next
chunk (PLAN.md). `$7E` stands at 11.4 KiB ported and 1.2 KiB inlined, 54%
left (59% before).

**What it is.** `scene/perspective.c` is the Mode 7 plane's perspective for
any scene: each screen line's matrix, the camera's cosine and sine times its
height over the line's distance, from the scene's first line (the routine the
scene's setup writes into the program's call: the race's from the horizon,
the endings' from the line above it) to the screen's bottom, a line at a time
or in pairs. The loop's own operands (the products, the branch over the
negation, the stores' tables) are variables in `re/ram.toml` and left as the
loop leaves them; the steps and starts other scenes write (modes 1 and 3-8,
which no movie reaches) are refused by the guard, so the original builds
those. The race's camera check now builds it as C rather than stopping at it.
`scene/race_projection.c` places the bikes: their places along and across the
track turned by the camera's angle onto the plane, then each in perspective,
the farther first: its distance ahead of the camera, the divisor kept for that
distance (or searched for, from the last one found, through
`scene_find_divisor_from`), the line under the horizon, and from the line the
picture's size, height and width and the place across the screen. The main
loop's scaling (`scene/race_matrix.c`) hands a waiting request to the bitmap
scaler and, after the upload, gathers the picture: its four entries are the
main loop's call (`$7E:51D1`), the two returns into it after each racer
(`$7E:51DF`, `$7E:51F0`) and the gathering after the upload's wait
(`$7E:5215`). The setup (`scene/race.c`) is a list of the race's graphics,
each unpacked and sent where it goes, the racers' pictures (Crono's in two
parts, one's blocks placed in the other, Johnny's mirrored from them), the
buffers, the per-line effects, the bikes' start (`race_start`,
`scene/race_rules.c`), the scripts and the race's song. Where the original's
calls stay, `scene/bridge.c` holds the machine: the scaling routine's frames
and data bank under the scaler's first row, the inlined loader's frame and the
song routine's under their call-outs.

**The registers, settled first.** Liveness has A's high byte and Y live
after the placing (a frame task, as the first chunk's), and the carry after
the main loop's call; the setup's callers read the overflow and the data
bank. The placing hands its entry the bike it placed last and the distance it
looked up, from which the wrapper leaves A (that bike's last edge, or
Johnny's mark where he is gone) and Y; the perspective's registers are worked
out from its tables after it; the main loop's carry is the last request's
compare, worked out from the state, or the scaler's, untouched; the setup's
overflow is the matrix code writer's, clear. The call-outs pass what their
callees read: the unpacker its resource with the direct page, the data bank
and the overflow the original has there (`$2100`, `$7F` and clear while the
pictures are worked on, kept on the stack around the third part's
unpacking), the first script step Y as the divisors' clear leaves it.

**The carries.** The placing never clears the carry between its sums either.
Those whose carry is the same wherever the horizon is above the plane's top
(the gap's borrow, the screen line's unit) are plain arithmetic, a bike's top
the line plus the horizon less the plane's top, and the entry's guard keeps
the horizon there (the race has it at $38 over $18). The rest move a bike by a
pixel or a distance step and are rules with one term each, worked out in one
section, "The units the placing hands on", from the values the sums before
used: Johnny's place across the plane takes a unit where Crono's place down it
wrapped, and his place down one where his own place across did; a bike's
distance takes a unit from the order's comparison (the first) or from the
first bike's last edge running past the screen's coordinates (the second), and
is a step farther where the camera's distance less the place wrapped; Johnny
is a pixel to the right at his nearest (a size of 1). The rotation's products
are plain arithmetic (the low word of the signed product), the program's own
multiplies timed with the placing. What the last bike leaves in the scene's
working cells is written once, plainly, after the placing: the field's
controller read takes those cells over. The wrapper rebuilds A from the bike
that comes last and Y from the distance it was looked up at, which the placing
notes as scaffolding; the drawing, the next task, hands Y on where it draws
nothing.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches, none refused: in `events-jetbike-race` the placing 2,640, the camera
check (now building the perspective) 2,640, the perspective's own entry 66 (the
camera script calls it), the main loop's call and its two returns 474 each
(2 and 1 interrupted), the gathering 948 (372 interrupted), the setup 1; the
perspective 1,521 in `endings-beyond-time` (1 interrupted). Shadow mode found
the setup's scratch the shadow compares at its call-outs: the block copy's row
count in the scene's first cell, the mirroring's offset and count at the start
of the unpack buffer, and the work RAM port's bank (the one bit the mirroring
writes, then the unpacker's `$7F`); fuzzing the setup found the curtain's
window on BG4 and the color window as well (the race without its panel, which
no movie runs). The gate's shipping-mode run with `--scaffold-reads` found the
placing's working cells read after the race: the scene's cells share their
page with the controller's (`input_held`, the repeat delay, the last frame's
buttons), so what the last placing leaves there is what the field's first
read of the controller takes over, and those writes are plain. The placing's
fuzz runs keep the horizon above the plane's top, as the guard asks. Fuzzed from the `race` state with the program kept, seeds 1
and 2 (`tools/fuzz_routines.toml`), 0 mismatched everywhere: the placing as it
comes and with the divisors forgotten (Johnny shown or gone; under 2% refused,
a camera height of `$FF`, where the search never ends, or nothing ahead of the
camera to look up), the perspective on channel 0 with the race's start (about a
quarter refused, a horizon off the screen or an odd count of paired lines) and
on channel 1 with the endings', the scaling's four entries, the curtain's task
(the curtain's state as its setup makes it), the setup 300 states (the driver's
echo looped back), and the first chunk's rewritten tasks again.

**Timing.** The new rows were calibrated over the library (the scene's rows
only). Two entries carry timing exceptions (`tools/timing_exceptions.txt`):
the perspective, which builds up to 224 lines' matrices in calls longer than
vblank, writing only RAM (vblank-tail), and the setup, which runs with the
display blanked and the frame's interrupt off, its error moving when the race
starts (vblank-worst, worst); the camera check's exception now says it builds
the perspective, timed as the perspective's own.

## Phase 3, battle: cuts, bands and the screen's start (`$CE:E000`-`$CE:EF10`, `$D1:F1B0`, `$D1:F9E7`)

What `python3 tools/burndown.py --uncovered CE` and `--uncovered D1` listed
after the screen script language: all but one routine of bank `$CE`'s code,
the battle's screen setup `$D1:F1B0` and the window quadrilaterals'
rasterizer `$D1:F9E7`; and the screen script commands that called into them,
which became rows of the language once their callees were C. `$CE` went from
97% left to 5% (the one unrolled copy nothing reaches), `$D1` from 17% to 0%.

What it is, in the game's terms:

- **A window cut through the middle** (`battle/screen_divide.c`, command
  `$97`, `$CE:EA7E`): the side of a line through the screen's middle at an
  angle a channel keeps and turns, drawn down one half from where it leaves
  the screen's edge (the ROM's table of those points, `$CE:F28E`) and turned
  about the middle for the other; a frame at a time it sweeps round like a
  clock's hand. `$B2` (`$CE:E000`) carries a window's shape over into the
  coming frame's buffer. The line's columns are `fallen * across / down`: the
  original's byte-wide running remainder comes to exactly that wherever its
  sum fits a byte, which the guard of its own entry asks.
- **Bands between positions** (`battle/screen_between.c`, `$AC`, `$D9`): the
  direction between two animation positions, measured in eighths through the
  ROM's table of slopes' angles (`$CE:E7CF`), and a band of window
  quadrilateral along a pair of them, its half width the variable that sizes
  the circles.
- **The quadrilaterals' rasterizer** (`battle/screen_beam.c`, `$D1:F9E7`):
  each side followed down its stretches of lines in 8.8 columns, the two
  sides' steps one running sum whose carry moves an edge a column
  (`step_down_a_line`); the work RAM port it wrote through, and port 3 its
  word stores spill onto, are scaffolding now (`hw_port_leave_spill`, a new
  call in `cpu_io.h`). The beam (`$F4`) became a row with it.
- **The line effects' arrangements and the ripple** (`battle/display.c`):
  `$CE:EE6E` lays the nine per-line tables out for one of the ROM's 32
  arrangements, and `$CE:E18E` steps a rippling background's BG2 sway; the
  frame work, the line handler and the setup call both as C. The circles'
  copy `$CE:E7C8`, an unrolled run the circles' drawing jumps into partway,
  is a loop over the run's lines (`battle/screen_shapes.c`).
- **The screen as a battle starts** (`battle/screen_setup.c`, `$D1:F1B0`):
  the handlers, the line handler's line (`tm_set_line`), everything the frame
  work, effects and messages keep starting again, the battle window's lines
  for both its layouts, and the arrangement; some 30 cells it writes were
  given names in `re/ram.toml`.

**Narrowed.** `$CC` measures along cells the dispatch's index for it reaches
past the positions (the battle's stats), and `$DA` calls the triangles'
rasterizer from inside its handler; no script of the game's has either, and
both stay the original's (their callee `$CE:E7CF` is C). The setup's
call-outs are the start settings' copy `$CD:0C9D`, the message tiles' clear
`$CD:0235` and the music's start `$C0:0008`, whose frame of the data bank,
page and flags the setup keeps on the stack as the original does.
`$CE:E1C7`-`$E292`, an unrolled byte copy nothing reaches, isn't code any
port stands for.

**The third trap, settled first.** `tools/v_readers.py` and the don't-care
check over every call-out and entry: the line handler's alternating-frame step
(`$D1:FE4C`) handed its caller the carry and overflow of the arrangement's sum
for `$CE:EE6E` to carry back, and liveness has all of A and the flags dead
after it, so it now declares them; the window slide's first step reads the
arrangement's Y (the end of its block moves), which the frame work's bridge
leaves (`battle_leave_arrangement_registers`). The direction's work cells
`$23`-`$2A` lie over the turn slot's high byte, the party's defeat and the
random cells; they are written plainly, as the original leaves them
(`battle_direction_work`, a `reuses`).

**What shadow mode found.** The direction's entry returns A as the sign of
the run down rather than the direction on one of its four ways out, which its
only caller overwrites: `DC_AL`. The circles' worked edges run past 224
lines (the run starts partway in), so `battle_circle_edges` holds 256.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the cut
8,004 calls and its command 8,062, the carrying 11,546, the bands 2,482 and
1,690 (the direction 4,172 from the original's own handlers, the rasterizer
4,172), the circles' copy 49,528, the ripple 141,044, the arrangements 3,233,
the line handler's step 216,480, the screen's start 31; the beam (`$F4`) no
movie reaches.

**Fuzzing.** Seed 1, from the `battle` state
(`tools/fuzz_routines.toml`): every new entry 0 mismatching, 20,000 states
where nothing is refused. Fuzzing found three things the library never
showed: the setup called the map layers' scroll, a JSL, as a JSR's sibling
(`battle_run_long` now: until then shadow mode counted every setup as
interrupted, compared none, and the next run found the flags the scroll keeps
on the stack, which the setup now leaves as the original's last store does); the
arrangement's and the ripple's indexes take A's high byte along, which the
game always has clear there, so their entries' guards and the window slide's
and the line handler's ask it; and random corners reach past the window
table on an odd frame, which the original writes on into the window's
registers' images: the quadrilaterals' entries and the commands that draw
them refuse that (`screen_quad_fits_the_table`), and no real call is
refused.

**Timing.** The new entries were calibrated from the shadow runs; the beam,
which no movie reaches, takes the quadrilateral's cost. The cut is in
`tools/timing_exceptions.txt` for the vblank's limits, with `$97` and the
command dispatch it runs under: the original's time follows the line's run
across, a step of its loop a column, where the port works each line's column
out at once (5.4% error, at most 19,014 clocks, writing RAM; the vblank check
finds no video write moved).

## Phase 3, menus: the save code and the rest of bank `$FF` (`$FF:F548`-`$FF:FBE7`)

The whole of bank `$FF`'s code but one routine nothing reaches: 1.6 KiB in 11
entries. `$FF` went from 0.1 KiB ported (95% left, 1 entry) to 1.5 KiB (10%
left, 12 entries); what is left is `$FF:FF3E`-`$FFF4`, a menu record's routine
behind a return that the analysis finds no caller of (docs/TODO.md).

**What it is.** Five things that share a bank:

- `menu/saves.c`, the save code: a slot's check (`$FF:F9C4`), the save
  (`$FF:F9FB`), the load (`$FF:FAA3`) and a New Game+'s load (`$FF:FAED`),
  with the sum (`$FF:FBE7`) and the slots' places (`$FF:FBD3`) inline. A saved
  game is the eight runs of work RAM the ROM's table lists, one after the other
  from the slot's start; the save marks the slot and keeps a sum beside the
  mark. Save memory is reached through the save service only (a guard's look
  through the new `sv_peek8`), and the layout and the sum are kept to the bit,
  so a cartridge's saves load and the original loads ours. The menus'
  opening, the load screen and the save point call it as C now; the New
  Game+'s load is a bridge, since equipping each character's starting
  equipment and filling HP and MP are the menus' services (`$C2:8004`), still
  the original's.
- `menu/techs.c`, the party's techs as the menus open (`$FF:F813`): how many
  members the screens offer, the healing techs marked, the techs listed of
  every group whose characters are all present, each character's next single
  tech, each character's part in each double and triple tech, and which of
  the rocks' triple techs the party could do (`$FF:F958`, which the services
  also run). The parts table is now declared where it starts (`$7E:1640`, a
  row a character), not from below it.
- `menu/digits.c`: a screen's small number tiles, two digits to a tile
  (`$FF:F548`), and the items screen's amounts' digits (`$FF:F7D9`).
- `menu/sprites.c`: a character's sprite colours, dimmed or not (`$FF:F628`).
- `menu/gate_screen.c`: the End of Time's gate screen, its swirl drawn
  (`$FF:F677`) and turned every frame (`$FF:F716`), with the screen's start
  (`$C2:CE9D`) moved out of `epoch.c`, which it was never part of.

**The registers, settled first.** The check hands its answer back in A, a
real result. The loads and the swirl leave A's high byte to the menus' exit
and the screen's task: the wrapper derives it (the load's is clear, the
swirl's the last band's scroll); their low bytes are declared dead. The New
Game+'s calls to the services can't drop A's high byte (the equipping stores
it where an item is no equipment), so the bridge leaves the original's, the
high byte of the character's starting record's place, in the one scaffolding
call that also leaves the multiplier and the record's place; the flags the
services save and restore are declared dead, which liveness agrees with. The
check runs on its caller's page, the menus' or the clock's (`$0400`, the
power-on search), a new page kind (`save_check`).

**What shadow mode and fuzzing found.** Over the 26 movies every call matches and
none is refused: the check 152 calls on both its pages, the techs' noting 37
and the rocks' 590, the swirl's turn 270, the load 19, the amounts' digits 6,
the save 2. The New Game+'s load and a character's colours are reached by no
movie, and the number tiles' and the swirl's drawing once each, interrupted;
for those fuzzing is the check. Fuzzed from the `menus` state, seed 1, 2,000
states each (500 for the swirl's drawing, which unpacks its graphics every
time), 0 mismatching. The first runs found what shadow mode couldn't: the
number tiles' start tiles shifted a bit short (their only call is
interrupted); the New Game+'s count of a character's equipment stored a word
wide; and three inputs the game always gives the same, now the guards': the
menus' zero word, which the save code and the tiles start their tables from,
the high byte of the word the triple techs' parts are counted in, and a
palette change count with room. The rewritten callers against main's
(`tools/fuzz_compare.py --before`, seed 1): the load screen's steps, the slots'
reading, the items' tables and the gate screen's start match the same states
as before; the techs screen's member steps now refuse the fuzzed states whose
list holds techs of another kind than the one it shows (no game state), which
read the parts table from below its start.

**Timing.** Each new entry is calibrated on its own calls (`menu_load_game`
25141, `menu_note_party_techs` 17807, `menu_note_rock_techs` 11016, the rest
between 3400 and 6780), and the callers that now run them as C carry their
time tagged as theirs (`TIMED_AS`): `menu_open` went from 15116 to 4737 and
`menu_read_save_slots` from 3477 to 27720 as the work moved between them. The
runs copied to and from save memory are charged at the block move's rate, as
the original moves them with it. No exception was needed; the two entries no
movie reaches keep the default.

## Phase 3, menus: the settings screen and the shop (`$C2:BF35`-`$C2:C620`, `$C2:D52B`-`$C2:E012`)

The seventh chunk of bank `$C2` (history file, "Phase 3, menus: the save code
and the rest of bank `$FF`", for the one before). `python3 tools/burndown.py`
for `$C2`: 30.9 KiB ported and 48% left with 326 entries before, 33.9 KiB and
43% left with 358 after: about 3 KiB of new code in 32 entries, all of it to
the bar (`tools/portstyle.py`: 0.00-0.04 machine operations a line in the
four new files; `tools/portsmell.py`: no finding).

**What it is.** Two screens, each a logic file on the game's state and a
bridge holding the entries:

- `menu/settings_screen.c`, the settings: twelve options, each a cell of the
  settings (`OPTION_CELLS`), flipped, stepped as a meter or opening a
  sub-step; the meters (battle and message speed, the window's design) and
  the battle gauge's three layouts lit among dimmed tiles; the buttons'
  assignment, where giving one assignment a button makes the assignments
  that share it give theirs up in turn (`give_button`, twice: the one that
  gave it up settles too); the description of the option under the cursor,
  started as the screen's typing task; and the main menu's panel for the
  screen, its frame, showing and putting away. `menu_pack_settings`
  (`$C2:934F`, `menu/opening.c`) packs the cells back into the game's three
  settings bytes as the screen closes.
- `menu/shop.c`, the shop: the goods of the shop's number (`menu_shop_good`,
  `$CC:2C6D`) priced by the story's scale (`shop_price_scale`, `$7F:01CD`,
  in 256ths); the party's items for sale at half price, rounded up; the
  panel beside a list, the item's owned and worn counts and, for each member
  who could use it, the member cheering and the figures it would change; the
  quantity, limited by what the gold buys or the purse has room for
  (9,999,999 at most), the total stepping with it; and the deal, the gold
  paid or received and the item put in or taken out.

`menu.h` gained `menu_start_task` (`$C2:8249`), `cursor.h`
`menu_cursor_kept_among` (the guards' question whether a kept cursor stands
on one of a screen's places), and `items.h` the inventory's place reads the
shop's list for sale is built from.

**The registers, settled first.** Every entry takes the menus' page with
M 8-bit and X 16-bit and leaves nothing but memory, except two whose callers
read A's high byte after them (`menu_take_setting_option`,
`menu_show_shop_item_panel`: `DC_AL`, which `tools/dontcare_check.py`
asked for), and the list's frame (`$C2:DD40`), entered with X in either
width and Y the frame's place. The ticks of sound inside the steps save P
with `PHP` and the driver reads A's high byte, so the flags and A at each
tick are left as the original has them (`leave_*`, scaffolding). The call
into the shop's equipping of what was bought (`$C2:E017`) declares V and C
live: its handler is picked by a step number, and liveness can't see past
it.

**Narrowed, not threaded.** The shop's equipping (its steps 6-8) is still
the original's: no movie takes it, and porting it would have carried the
equipment screen's member card into the shop. The shop's steps call it as a
call-out and stop where the original goes to it; TODO.md lists the routines.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches and none is refused. `menus-field-menu` reaches the settings screen
(the options' step 1,878 calls, the options taken 1,876, the meters 720)
and `menus-shop-inn` the shop (the goods' step 500 calls, the items for sale
277, the tabs 229, the quantities 220 and 100, the panel 28). The buttons'
assignment and three of the on-off options are reached by no movie, and the
shop's opening, set-up and list openings are compared only within the
opening's call, which a frame's wait interrupts: fuzzing is their check. Fuzzed seed 1,
20,000 states each from three shop states (its tabs, buying, selling) and a
settings state, and each step again compared at the first call of the sound
driver (`--stop-at-call C70004`, where the flags each tick saves are on the
stack): 0 mismatching of 47 routines and variants. The settings' steps skip
about half the states, whose description is half typed (an odd count; the
guard turns them away). What the fuzzing found, before the guards and
leaves settled: the meters' tick leaves N set from the original's
`LDX #$FBE3`; the cursor's row is compared with the list's start the other
way round; the price's scaling rounds the product's middle byte up where
its low byte is set; the division's spare byte goes into the quotient's top;
the panel's member and place stand in cells at each call out; and every
reading of the buttons leaves the overflow of the original's `BIT`.

**Scaffolding the effects check asked about.** Six writes first made as
scaffolding are the game's and are plain now: the drawing origin the meters
and the shop's rows leave, the eighth assignment's stray swaps (the game's
settings change), the item's power read past the figures' table, the
member the panel works on (`menu_screen_argument`, which the member screens
read), the quantity before a step and the settings' spare cell. The 59 reads
left in `tools/effects_baseline.txt` are cells another screen or the sprite
object mode sets before it reads them, each with its reason.

**Timing.** The 26 entries the library reaches are calibrated on their own
calls (from 1 for the two whose work is all in parts timed as their own
routines, to 10902 for the goods list's opening); the six it doesn't keep
the default, and `menu_play_cursor_sound`, whose calls the new steps add,
moved from 3015 to 3062. Nine entries are exceptions: a step that mostly
reads the buttons and now and then redraws a row, a description or a list
(the options, the meters, the shop's tabs and quantity), and the settings
panel, a frame's drawing on two calls, 6% short; all write RAM only, and
the vblank check finds none of their writes moved.

**The map.** The story's two bytes three ports spelled by offset are named
once: `story_party_rules` (`$7F:00BA`, the party held as it is) and
`story_game_notes` (`$7F:01CF`, a carried-over game), read by `items.c`,
`techs.c`, `saves.c` and `battle/from_field.c`.

## Phase 3, menus: the party's services, its figures and its healing (`$C2:8004`, `$C2:8C36`-`$C2:93BF`), the screens' records and held cursors

The eighth chunk of bank `$C2` (history file, "Phase 3, menus: the settings
screen and the shop", for the one before). `python3 tools/burndown.py` for
`$C2`: 33.9 KiB ported and 43% left with 358 entries before, 36.4 KiB and 38%
left with 385 after: about 2.5 KiB of new code in 28 entries, all of it to the
bar (`tools/portstyle.py`: 0.00-0.07 machine operations a line in the six new
files; `tools/portsmell.py`: no finding, and the cursor header's line left
the backlog). The first of PLAN's two items for `$C2` whole, and of the
second the pieces the ported screens call: the records' sprites and colours
(`$C2:F566`-`$C2:F643`) and the held cursors (`$C2:8A98`-`$C2:8BA5`). The
rest of the second item -- the list slide, the description's typing and the
four screens -- is the next chunk (PLAN.md).

**What it is.**

- `menu/party.c`, the party as the menus keep it: the record of who is in it
  (the three active places and the six of the reserve, an emptied place
  keeping its character with the top bit set), and the services that change
  it -- a character recruited, dismissed, brought into the active party or
  sent to the reserve, the reserve taking someone at its front by moving
  everyone back as far as its first empty place; a character equipped
  (what comes off given back to the party's items) or stripped; every
  character's HP or MP filled, an earring raising the greatest HP; and what
  every service ends with, the party's weapons' colours in its members'
  palettes and the rocks' triple techs following who wears a rock.
  The field's scripts, the power-on and a New Game+ call the operations
  themselves; `menu/party_bridge.c` is the services as the original's code
  calls them, by number in A through a C table of rows, for the registered
  entry.
- `menu/figures.c`, a member's figures: the stats with every boost of what
  it wears, held to their caps; the attack, by how the character fights
  (a weapon wielded, a gun or bow aimed, Ayla's fists by her level); the
  defence; the greatest HP; and the party's items set aside where the story
  holds them.
- `menu/healing.c` and `healing_bridge.c`, a tonic's or a tech's healing --
  fixed, drawn at random around its power, or all of it -- and the numbers
  that rise from each healed member's sprite along a path of heights, in a
  task of their own that the screen waits out.
- the inventory's add and take (`$C2:8791`, `$C2:87D5`, `menu/items.c`),
  who can use an item (`$C2:93A8`), a list's paging (`$C2:93BF`,
  `menu/lists.c`), the power-on's look at the cartridge and a new game's
  state (`$C2:8D6A`, `$C2:956E`, `$C2:95BB`, `menu/saves.c`), the records a
  screen of members shows (`menu/sprites.c`) and the cursors a screen holds
  (`menu/held_cursors.c`).

The callers lost their glue: event's party commands, the field's power-on,
the New Game+ load, the load screen's new game, and the equipment, items,
techs and shop screens call these as C, each with the routine's time tagged
as its own (`NATIVE_TIME_AS`, `TIMED_AS`); the paging hands the new first
row back instead of through its cell, and the inventory's routines take the
item and how many as parameters.

**The registers, settled first.** The dispatcher's PLP gives back the flags
its caller had, so nothing is threaded: A is the service's answer, a word
from the three that work 16 bits wide, and X and Y are what the party's looks
leave them, worked out from the state after (the last rock a character wears
or past the rock techs' marks). The operations hand back only what the game
asks of them (whether a character joined, was there, was sent); what the
dispatcher keeps -- the call's Y and X, which a script's command fills with
its offset as often as with a character, and the answer, worked out from the
record as it was before -- is `menu/party_leaves.h`, which a port standing
where the original's call was runs under `ORIGINAL_EFFECT` with its own
registers. Event's commands leave the Y, which only the dispatcher's next
call keeps (TODO.md, the event leaves). The healing's showing and its task yield through the original's task
switch; the task stops at the scheduler's end of a task.

**What shadow mode and fuzzing found.** Over the 26 movies every call
matches and none is refused: 7,062 calls over 26 of the 28 entries, 0
mismatching and none interrupted (the members' cursors for a healing of
every member, `$C2:8AD5`/`$C2:8AFD`, no movie reaches). The healing's showing and its
kinds span frames and are compared within the call that encloses them. Shadow
mode found the new game's party fill one place shorter than the asm read
seemed (nine places, not ten), the index registers the party's looks leave
(the rocks' clearing's block move), the shop's panel's A taken from the
figures' divider before the figures' drawing divides again, and the healing's
bookkeeping cells. Fuzzed seed 1 (`tools/fuzz_routines.toml`): 0 of 81,953 compared calls
mismatching, 3,000 an entry (the services' 2,894 and, loose, 2,562; a
kind's rising numbers 1,497, the rest refused for a kind with no path).
What fuzzing found: the number's conversion converts only the bytes it is
given (an MP amount is a byte), the showing is entered with 16-bit index
registers only, and the services, event's party commands and the New Game+
load need the active party to be characters (a guard each; the game's always
is). The before-and-after comparison of the menus' and the touched event
entries (`tools/fuzz_compare.py` against main, 440 routines) found one:
the menus' item tables now work every character's figures out as C, and a
record wearing an item past the menus' tables (none the game has) read
past them where the original's reads differ, so the tables' guard takes
the figures' (`menu_everyones_figures_are_notable`) and its fuzzing fills
the characters' equipment; after it, 0 differ.

**Timing.** The new rows and the callers whose time moved with them
(the screens' steps, event's party commands, the field's first start)
are calibrated over the library. One rate can't fit thirteen services
(the power-on checks three slots' sums, a search looks at nine bytes), so
the power-on is registered on its own (`menu_power_on`, `$C2:8D6A`) and
the dispatcher tags it with its own time; the rest share the services'
rate. The techs list's step, whose own share is at the floor, carries a
drift exception for its parts' 2.3%, and the shop's tabs' exception moved
from vblank-worst to worst: with the list's opening charged at the
list's own rates, its one call over the limit is the frame a tab opens a
list, on the picture (`tools/timing_exceptions.txt`).

**The map.** The services' cells (`menu_service_*`, `$04C0`-`$04C5`), the
item a search found (`$04C7`), the figures' boosts and parts (`$0DAE`-`$0DB6`,
`menu_figures_slot` narrowed to its byte), the greatest HP (`$9B25`), the
items set aside (`$9B96`), the paging's rows (`$0DD8`, `$0DDA`), the
healing's amounts and its numbers' cells (`$0022`, `$0F53`-`$0F61`), the
held cursors' blinks (`$0DA0`, `$0DA3`), the weapon looked up (`$041B`), and
the status record's stats, level, figures and greatest HP as fields.

## Phase 3, menus: the main menu, the save screen's opening, name entry, the description, the list slide and the shop's equipping

The ninth chunk of bank `$C2` (history file, "Phase 3, menus: the party's
services, its figures and its healing", for the one before). `python3
tools/burndown.py` for `$C2`: 36.4K ported and 38% left with 386 entries
before, 39.8K and 33% left with 440 after: about 3.4 KiB of new code in 54
entries, all of it to the bar (`tools/portstyle.py`: 0.00-0.02 machine
operations a line in the new files; `tools/portsmell.py`: no finding).
PLAN's first item for `$C2` but one screen: the party's exchange (screens 5
and 7, `$C2:C627`-`$C2:CDCA`, with its main-menu panel `$C2:C9E5`/`$C2:CAE6`)
is the next chunk's, since it waits on the sound CPU's ports and drives the
members' sprites, a concept of its own; the main menu calls its panel out.

**What it is.**

- `menu/lists.c`, the list slide (`$C2:EB9B`, `$C2:EB1F`): a list scrolled a
  row is shown sliding, the screen's per-line table split into bands that
  move a few lines a frame, staged for the vblank's block move; the screens
  put the bands back at rest as they close.
- `menu/description.c`, the description window: what a screen's cursor is
  on, typed a few glyphs a frame by its own task. An item's (`$C2:F2F3`, the
  items, equipment and shop screens'), a tech's (the techs screen asks with
  its own table, row and pace through the same `menu_describe`), a setting's
  with its value a tile after it (`$C2:F943`), and the battle mode's; the
  typing's three task starts, its frame (`$C2:FAA8`, yielding through the
  task switch) and its sending (`$C2:FAE9`).
- `menu/held_cursors.c` gains the second cursor held on a list's row and on
  the member an item is used on (`$C2:89E4`-`$C2:8A91`), and `menu/frame.c`
  the tasks' stop (`$C2:8255`), which the screens call as C.
- `menu/main_menu.c`, screen 0: the backdrop, the six options' icons and
  colours, the arrows, choosing and moving among the options, each option's
  panel shown and put away through a C table of the screens' panel entries.
- `menu/saving.c`, the save screen's opening from the main menu and its
  frame (`$C2:D065`-`$C2:D155`, `$C2:D4F8`, `$C2:D506`), over the load
  screen's slot rows.
- `menu/naming.c`, name entry (screen 13): the seven characters' names and
  the Epoch's as one concept (a name of five letters, `NO_LETTER` and
  `SPACE` named), the letters' grid the cursor moves about, a letter typed or
  taken back, the name kept with its spaces dropped and refused where another
  has it; and the battle mode's screen on the same screen number.
- `menu/shop_equipment.c`, the shop's steps 6-8: down from the tabs to the
  party (the members coloured by whether they can use the goods), a member's
  slots, the list of what fits each with its figures compared, and putting it
  on.

The screens lost their glue: the items, equipment, techs, settings and shop
screens call the slide, the description, the held cursors and the tasks'
stop as C, each with the routine's time tagged as its own (`TIMED_AS`);
the review items that sat in the touched files went with them (TODO.md).

**The registers, settled first.** Every call-out and stop was checked
against liveness before the logic was written, and the gate's
`tools/dontcare_check.py` agrees with each. What the original's callers read
is left under `ORIGINAL_EFFECT` from the state after: A clear where the
description started typing (the text's start leaves it), A's high byte all
ones after a block move (the main menu's drawing, the queued uploads), the
X and Y the slide's staging leaves for the task switch, which keeps them and
the flags across its wait (its stop is `m_stop_at_own`); the flags a sound's
tick saves as it starts (the grid's compare of the cursor's places and the
add that placed it, the members' compare). The main menu's call into the
exchange's panel declares the carry and the overflow read, as the panel's
pick leaves them.

**What shadow mode and fuzzing found.** Over the 27 movies every call matches and none is refused: 40,052 calls over
52 of the 54 entries, 0 mismatching and 46 interrupted (the typing's task and
frame, the naming's and the main menu's openings, which span frames and are
compared inside the calls that enclose them; the save screen's own opening
and frame, `$C2:D4F8`/`$C2:D506`, no movie reaches: the library saves from
the main menu's panel). Fuzzed seed 1
(`tools/fuzz_routines.toml`, 54 new entries, 2,000 to 8,000 states each):
0 of 102,241 compared states mismatching. What fuzzing found: the index registers' width at the entries
that load them with immediates (the main menu, the save panel, the naming,
the shop's steps), the window lists' and the panels' guards a caller in C
must check for them, the typing's even places along a line, the naming's
record kept for the named one (`menu_naming_keeps_the_named_ones_record`),
and the flags and A the original's ticks and buzzes save. The
before-and-after comparison against main (`tools/fuzz_compare.py`, 495
routines) found no routine that differs; the nine that mismatch on some
fuzzed states mismatch the same on main.

**The new movie.** `menus-shop-equip` (RUNTIME.md's table) goes down from
the shop's tabs to the party in Medina's market and changes members'
equipment from there, the steps no movie took.

**Timing.** The new rows, and the rows the new movie moved (the steps of
the settings, items, techs and shop screens whose call-outs became C, the
records' animation, the figures, the sound driver's two slot routines), are
calibrated over the library. Six entries keep `cost_milli = 5000`: the
naming's and the main menu's openings and the shop's member opening, whose
every call spans frames, and the save screen's opening and frame, which no
movie reaches. Eighteen exceptions (`tools/timing_exceptions.txt`) carry the
screens' steps that now charge the slide, the description and the window
lists at those routines' rates, and the field's frame in Medina's crowded
market, which runs into vblank on 101 frames.

**The map.** The slide's bands, the staged table and the table at rest
(`$9790`, `$969A`, `$9710`, `$0DA9`), the description's cells (`$0DC1`-`$0DD6`,
the value's line and tile at `$0026`/`$0028`), the names' cells (`$0F00`-`$0F0D`,
the grid's text and the name kept at `$9890`, `$98A0`), the names given
(`$299F`), the Epoch's name (`$2C4D`), the main menu's option (`$0411`), box
and backdrop (`$5780`, `$3E00`), the shop's figure marks (`$0D4D`), the upload
record built at `$9890`, and the glyph tiles (`$5E00`).

## Phase 3, menus: the party's exchange (`$C2:C627`-`$C2:CE93`) and the main menu's panels as C

The tenth chunk of bank `$C2` (history file, "Phase 3, menus: the main menu,
the save screen's opening, name entry, the description, the list slide and
the shop's equipping", for the one before), and the last menu screen in the
bank. `python3 tools/burndown.py` for `$C2`: 39.8K ported and 33% left with
440 entries before, 40.6K and 31% left with 450 after: about 2.2 KiB of new
code in 12 entries (two of them the gate's song's), all of it to the bar
(`tools/portstyle.py`: 0.00-0.02 machine operations a line in the new files;
`tools/portsmell.py`: no finding). The chunk is PLAN's first item for `$C2`,
screens 5 and 7 with the main menu's panel for them, and the routines only
they call (the members' sprites placed, `$C2:CDE3`-`$C2:CE93`, past the
range PLAN gave).

**What it is.**

- `menu/exchange.c`, the party exchange: the party listed in rows (the
  active party's three, the reserve's six, each a place of the party's
  record, marked where the story keeps its member in the active party,
  `story_party_kept`), drawn as status windows with each member's sprite
  standing in its row; the cursor among the active party's rows and the
  reserve's four shown, stepping off rows nobody may be chosen on; a member
  chosen first (a second cursor held on it, its pose), then a second, and
  the two changing places in the party (`party_exchange`, `menu/party.c`),
  their rows swapped on the tilemap; the reserve's scroll a row with a
  six-frame slide; who stays dimmed. From the main menu (screen 5) it
  reorders the active party alone, in the middle: the menus' opening leaves
  `menu_exchange_layout` set; the gate's screen (screen 7, which the field's
  Start assignment and a script's menu open) lays it out as the event asks,
  over the gate's swirl (`menu/gate_screen.c`). The panel, the same rows
  under the main menu or under the gate's label, is the screen's too.
- `menu/sound.c` and `menu/sound_bridge.c`, the gate's song: the place's
  song faded out and the gate's started, the driver saving the one it
  replaces, by a task that waits for the fade; the place's song back,
  resumed where it was, as the screen closes. What the waits wait for is
  the driver's status (`au_music_fade_left`, `au_music_playing`): the fade's
  steps left, a frame at a time, and the song's tracks playing, which the
  original spins on without a frame passing (an idle entry, `$C2:CBE0`;
  TODO.md, "Timing without a beam"). The audio service gains the two song
  starts the screen sends (`AU_START_SONG_SAVING`, `AU_START_SONG_SECOND_SET`).
- `menu/main_menu_bridge.c` calls every option's panel put away as C
  (`put_the_panel_away`, each timed as its screen's entry), and the main
  menu's put away as C from the step that comes back to it; showing a panel
  waits for frames, so it stays each screen's entry run as the original's
  JSR reaches it. The exchange's panel is one of those entries now, so the
  main menu calls nothing out.

**The registers, settled first.** Every call-out and stop the chunk keeps
was checked against liveness, and `tools/dontcare_check.py` agrees with each
declaration (it caught the screen's fade out reading A's high byte). What a
call compares is left from the state after the logic: the flags the
original's P bytes hold where they are on the stack under a call out (the
cursor's move's, the reserve's scroll's and the slide's; the panel's and its
window routine's round the gate sprite's unpacking; the gate song's tests
the panel keeps), worked out from the counts and sums they come from
(`leave_counted`, `leave_bar_worked_out`, `leave_scroll_started`), and A's
high byte at the ticks after the members are stood (clear). The routines the
slide and the gate's sprite run inside are pushed as their frames (a return
and a P each), the driver's two calling routines as their returns
(`sound_bridge.c`).

**What shadow mode and fuzzing found.** Over the 27 movies every call
matches and none is refused: the twelve entries' 3,283 calls, 0
mismatching (14 of them the song's task passing back through its own start,
which the entry's widths turn away); the gate's opening and the panel span frames on every call (4
interrupted), and the gate song's change back is compared only inside its
caller. `menus-field-menu` reorders the active party from the main menu,
`menus-end-of-time` and `menus-shop-equip` open the gate's screen (in
Medina its song changes). The shipping mode's traces over those three
(`--native on --generated only` against `--native off`) are the same at every
frame. Fuzzed seed 1 (`tools/fuzz_routines.toml`, 14 entries, 2,000 states
each, the whole party listed in both columns as a variant): 0 of 27,764
compared states mismatching. What fuzzing found: the overflow and carry of
the counts' add at the tick, the scroll bar's sums under the move, the
panel's pick and the sprite's place loaded before the unpacking, the song's
task's widths at its start, and the drawing origin the gate's panel leaves.
Two things it showed that the game can't fix by guarding: the main menu's
exchange takes the reserve's count as the screen before left it, and with
a count a push across sends the cursor round the reserve's empty rows,
where the original searches for ever (the port stops after 64 steps; the
plain fuzz entries keep the count at none, the state's); and a slide needs
more than four members in the reserve, which seven characters never make,
so only its first frame is compared (TODO.md). The comparison of the main
menu's and the screens' put-away entries against main
(`tools/fuzz_compare.py`, 13 routines) finds no routine that differs.

**Timing.** The twelve rows and the main menu's that now run the panels as
C (its put away, and the step back to it) are calibrated over the library;
no exception was added.

**The map.** The exchange's rows, counts, choices and the song it replaced
(`$0F00`-`$0F0F`), its layout over the menus' place notes (`$0D36`), the
places' and the slide's cells (`$0022`, `$0024`), the menus' first cells'
uses, the story's kept members (`$7F:01DF`), window 1's edges alone
(`$9683`), the held cursor's offset's low byte (`$0D9E`) and the place
song's volume (`$04FB`). The palette's accessors take an `unchecked` pair
for the dimming of an emptied place's byte, which reaches past it.

## Phase 3, battle: the leftovers (`$C1`, `$CF:FB65`, `$CC:F2A9`)

What `python3 tools/burndown.py --uncovered C1`, `CF` and `CC` listed after
battle chunk 10: about 1.9 KiB of runs, most of them inside ports that handed
back to the original at a stop. Two kinds of run in the list weren't work:

- **Code nothing runs.** `$C1:C09A` (five fraction routines), `$C1:BC62`,
  `$C1:EFD3`, `$C1:BF81`, `$C1:AED3`, `$C1:E07B`, `$C1:EB49`, `$C1:CF51`,
  `$C1:15B6`, `$C1:0015` and `$CC:E290` are `kind = "unreached"` entries in
  `re/hints.toml`: bytes that decode as code, which nothing in the ROM calls and
  no movie's coverage has. They need no port, so the burndown no longer counts
  what only an `unreached` entry starts (`reached_code`,
  `tools/disasm/closure.py`, the reachability the C6 check already solved).
- **Cases the game never makes.** A battle set up without enemies (`$C1:8030`)
  sends the original's order lists running on through memory, and an enemy's
  action past its table (`$C1:8D88`) jumps through a word of the next table:
  guards are the answer for both. The first stays a stop, at the step's return,
  declaring nothing dead (the burndown counts a stop where a call returns,
  declaring nothing, as the port going on); the second is the turn's guard
  already (`battle_enemy_turn_ready` takes only actions the table has), so
  the dispatch's stop went, an action past the table doing nothing.

The rest is ported, as what it does in the game:

- **Coming in from the field and going back** (`battle/from_field.c`,
  `$C1:0000`/`$CF:FB65` and `$C1:0006`/`$CF:FBE5`): the field's registers are
  kept on the stack the whole battle (`m_save_caller_registers` and
  `m_return_to_caller_long`, `runtime/port/machine.h`); the battle's memory is
  cleared, the screen set up (the original's `$CD:0000`), the window
  arrangement, the sprites' priority and the story's rules on who sits the fight
  out taken in, and the main loop started. On the way out the members' colors
  are put back, where everyone stood and the menu cursors the settings remember
  are kept for the field, the last uploads let out over a few frames (the
  original's frame wait `$CF:FCFB`), and every character with no HP left gets
  up with 1.
- **The setup's last part** (`battle/opening.c`, `$C1:0042`, `$C1:34DB`,
  `$C1:350F`): each member turns to face its nearest enemy, and the members
  taking part take the opening's poses, which the original's routine the
  victory and the defeat share runs (`$C1:35DD`). Its cells reuse the script
  channels' (`battle_party_scene_*`).
- **The scheduler's animation step** (`battle/objects.c`, `$C1:4269`): the
  object due takes its animation's next frame, its graphics built for the way
  it faces; and **an effect's next frame** (`battle/effect_animation.c`,
  `$CC:F2A9`), its own entry, which the effects' loop runs as C. The original
  stores the anchor's facing back where it read it instead of noting it as the
  facing shown; the port says so.
- **A member that comes back once** (`battle/combatants.c`, `$C1:B316`): five
  times its magic in HP, shown as healing (`effect_show_hp_regained`, the
  status timers' recorder), a tech shown on it, and the message; the showing
  and the message stay the original's, so the update is run as a sibling
  (`battle_run`) where a member comes back.
- **The draw among four's tail** (`$C1:8C90`): the draw's entry, reached when
  the original's choosing runs, goes on with the choosing in C
  (`enemy_choose_after_draw`) instead of handing back into it.

**The third trap, settled first.** `tools/v_readers.py` has V dead into every
call-out kept (`$CD:0000`, `$CD:0006`, `$CD:0021`, `$C1:0045`, `$C1:AC57`,
`$CF:FCFB`), and liveness has them read nothing but A (and its high byte),
which each is given as the original gives it; the poses at `$C1:35DD` take A's
high byte, clear as the guard has it at the setup's end.

**What the gate found.** Fuzzing found the draw's entry taking a script place
that isn't a draw: the guard now wants the draw's byte there, as the choosing
always has it. It found the come-back's multiply leaving the carry of the
number's place in its cell, which the status timers' recorder already left (so
the come-back records through it). The return's colors read past the palette
for a member palette no member has: the guard takes the party's. The
don't-care check found A's high byte read after the draw's entry now that it
returns, and at the poses.

**Validation.** The library's 26 movies in shadow mode, none mismatching: the
scheduler's pass 281,498 calls (684 interrupted), the effects' loop 281,498
(1,773), an effect's next frame 152,304 (264), the setup's end 31, the return
to the field 26 (1 interrupted), the draw's entry 96 (5); the start from the
field's 31 calls are all interrupted (its screen setup waits for frames), and
no movie has a member come back. **Fuzzing**, seed 1, none mismatching: the
start up to the screen's setup 0 of 20,000, the return up to its first frame
wait 0 of 16,509, the setup's end 0 of 5,000, an effect's next frame 0 of
5,000, the effects' loop 0 of 5,000, the scheduler 0 of 4,584, the draw 0 of
467 (80,000 states, most refused for a place that isn't a draw), the update 0
of 1,852 as before. A member coming back is compared up to its showing: no
memory differs in 6,599 states, but `ct_fuzz` has no runtime to run the showing
as a sibling, so the call is a real one, declaring nothing, and X, Y and the
flags it doesn't read differ there (docs/TODO.md).

**Timing.** The new entries and those whose work moved were calibrated from
the shadow runs: the effects' loop, the scheduler, the draw.

## Phase 3, battle: the targeting (`$C1:C1DD`-`$C1:CBC2`)

Chosen by the fallback measure, not the burndown: the burndown counted bank
`$C1` as done, but every tech and item the battle carries out was still aimed
by the original's code, a call-out from the member's turn, the enemy's turn
and (through the original's code they reach) the status timers: 1.5 KiB of
the reference play's fallbacks (`$C1:C1DD` alone 1,122 bytes, the largest
single routine on `tools/fallback_baseline.txt`). It was also what the native
shape's three whole routes failed on: the original targeting read two cells
the member's turn wrote only as scaffolding (the member's action record's
place, `$06`, and an item's area rules, `$0C`).

**What it is** (`battle/aim.c`, `battle/aim_data.h`). An action is aimed in
three steps. Whether it can be made: the actor, a party tech's partners, or
the enemy an enemy's tech brings in, must be able to take part (not knocked
out, none of the statuses that keep them from acting, nor two bits of the
second status byte whose meaning isn't worked out). The targets its aim mode
takes: the modes are the records' bytes, a C table maps each to one of nine
ways of taking targets (`Take`) and one of four ways the targets are settled
after (`Finish`), so the twenty-four modes the ROM uses are rows, not
branches; the number the original leaves for its finish in a dead cell is a
column of its own. Then its area
rules, for a tech that has them (`$CC:2AB0`): the area is placed on the
combatants (around the tech's first member and toward the target, around the
target, around the actor and toward the target: a table of the seven
placings), the original's area search (the battle's step 7, `$C1:1FDD`, still
a call-out) finds who stands in it, and the finds become the targets, settled
as the mode says. The original's `$C1:C741`, which hands the search its shape,
is its own entry (`battle_search_area`), so the call-out is made under the
same return the original's is without a frame pushed by hand. Its helpers
(`$C1:C72B`-`$C1:C82C`, `$C1:C82D`, `$C1:C8F7`, `$C1:CB93`) are C inside it.

**What the game does that the port keeps.** Reading the original closely found
four things the game really does, which the port reproduces and says so:

- The "random" aim (mode 9, two items) doesn't roll: it draws a number and
  then reads its roll from the arithmetic's cell the draw puts back, where the
  tech members' lookup at the start left the item's party-tech record's
  number (the port takes that number from the record), so each such item
  always takes the same branch (a foe at random,
  for both). One branch would bound the party's places by the byte of buttons
  the player holds; no item rolls it, and the guard refuses it.
- The settling's walk over the targets steps past a gap and closes up over the
  place after it, so a gap takes the target after it off the list too, and the
  list's last place is never closed over.
- The area's finds end with `$FF`, which the test for an enemy in reserve takes
  as a combatant past the eight, reading a timer's flags there; a revive's
  target is tested for reserve by its battle slot rather than its enemy
  number. Both read past `battle_enemy_standing`, which now declares
  `unchecked` for them.
- The area's finds are walked with the multiply's product cell as the counter,
  which the original leaves past the last place.

**The cells.** The original works in the direct page's first cells as it goes,
each holding several things in turn. The port works out what each holds at
the end from how far the aim went -- the action's records read, the roll
taken, the area's anchors gathered, its rule placed -- and writes them once,
before the area search (`leave_aim_cells`): plainly those a later reader takes
as game input (battle.h: `$00` the clock's word, `$06` the member cell, `$08`,
`$0A` and `$0E` read as stats), the others (`$02`, `$04`, `$0C`, `$10`-`$14`)
and the new `battle_aim_*` cells at `$3A`-`$3C` as scaffolding; the rule's
shape at `$3D`, which its sub-entry reads, is plain. The member's
turn's writes of `$06` and `$0C` became plain: the targeting's guard reads them
(the action record's place, an item's area rules), and with them the native
shape's battle failures went. The event scheduler's step count, cleared only
as scaffolding by `object_run_script_from` and read by the original's
scheduler (`$C0:59F0`), is plain too, which was the third route's failure:
`tools/native_shape_exceptions.txt` is empty.

**The review** rated `aim.c` and `aim_data.h` "mostly", none translation, and
asked first for four things, all done: the cells written once from what the
steps decided rather than pass by pass (above); the ways of taking and
settling only what the game does (four pairs that differed only in the dead
finish cell's number became one each, the number a column); the roll taken
from the tech record, not the leftover cell; and the logic in functions of
the game's state (`aim_up_to_the_area`, `settle_after_search`), the machine
kept to the entry and its two call-outs, the area search and the "no target"
showing. An item's area rules are none in the logic; only the guard asks its
caller's cell. Left for later, in TODO.md: the duplicated status tests beside
`combatant_cant_act`, the cell hand-offs between the member's turn and the
targeting, and the ROM tables `$CC:1ACB` and `$CC:1BB5` described in several
headers.

**What the guard turns away**, none of it a state real play makes (the
refusals check holds it): a party member whose action record isn't where its
caller hands it, an item handed area rules, a chosen target that isn't a
combatant (none, but for a revive), a tech whose members aren't the party's
characters, a set of area rules with more than one rule or with the target
search's rule (`$C1:2986`, the battle's step 5; the ROM's sets have one rule
each, all the area search's), an enemy's rule placed on party members, the
roll's button-bounded branch.

**What the gate found.** Fuzzing found two: the steps' routine picks its step by
the whole of A, so the area search needs A's high byte clear, as the original's
loads leave it (the sub-entry's guard now asks for it, and the targeting clears
A before it); and the product cell's leftover above. The don't-care check found
A's high byte read after the targeting returns (`DC_AL`, the port clearing it
as the original does, but after the "no target" showing, which hands back its
own), and V at the area search's call-out unproven through the steps' tables,
which `tools/v_readers.py` has dead and a poisoned fuzzing run confirms.

**Validation.** The library's 28 movies in shadow mode, none mismatching: the
targeting 841 calls, 699 compared (96 interrupted, 46 set aside inside its
callers' comparisons), the area search's entry 46 (each compared within the
targeting's); its callers as before, the member's turn 548 calls, the enemy's
tech 300. **Fuzzing**, seed 1, none mismatching (`tools/fuzz_routines.toml`):
the party's techs 0 of 4,935, every party tech with area rules 0 of 4,628,
items 0 of 5,000, the enemies' techs 0 of 5,000 and those with area rules 0 of
4,999, the area search's entry 0 of 5,000; the callers (the member's turn, its
techs, the enemy's tech and its stat-changing turns, the status timers) fuzz
the same as on main, 0 mismatched each. With the native shape passing every
route, `tools/native_shape_exceptions.txt` is empty; the reference play's
fallbacks fell from 46.5 KiB to 45.2 KiB, ten routines leaving the baseline.

**Timing.** The two new entries, and the battle's entries whose time moved with
them (the member's turn, the enemy's turns and tech, the turn's giving), were
calibrated from the shadow runs.

## Phase 3, battle: the player choosing (`$C1:1153`-`$C1:17DC`, `$C1:1F79`-`$C1:283C`, `$C1:095D`-`$C1:0C2C`)

Chosen by the fallback measure: the largest of the battle's handbacks was the
menu's frame (`$C1:1153`), the code that takes the player's buttons in a
fight, which the battle's own frame step (`$C1:106E`, `$C1:10E3`,
`$C1:10FA`, reached from the member's turn, the timers, the main loop and the
frame work) ran as the original's every frame a menu was open: 1.5 KiB and 5.2
million instructions of the reference play, with the lists it lays out, the
targets it lists and the area searches it asks behind it (`$C1:0B0B`,
`$C1:09B0`, `$C1:25A3`, `$C1:2701`, the next by size). The chunk takes the
whole of what the player does with the battle menu.

**What it is** (`battle/choosing.h` says how the files fit):
- `menu_input.c`, the menu's frame: the command page (the cursor among
  Attack, Tech and Item, the other members waiting to choose, X's attack with
  the remembered commands), the tech page and the item page (moving and
  scrolling the lists, L and R turning the item list a page), and choosing a
  target (moving among them, confirming, calling it off), down to queuing the
  member's action and the next member's menu. Before the buttons a stolen item
  joins the item list (`$CF:FAE2`) and a frame of four sprites surrounds the
  member choosing (`$C1:1115`); choosing Attack forgets the enemy the message
  line named (`$CD:002D` with nothing to name, which only notes it).
- `target_choice.c`, whom the action can be aimed at: the menu's 33 target
  kinds are a C table of rows (a member, an enemy, a side, everyone, the
  member itself, a fallen member, a particular character, or an area placed on
  the member, on an enemy the player picks or on a character), and the cursor
  among those listed.
- `area_search.c`, who stands in an area: the beam, the circle on the grid,
  the row and the party's triangle, which the battle's step 7 (`$C1:1FDD`)
  picks by the rules' shape number; the targeting (`aim.c`) now calls it as C.
- `menu_lists.c`, the tech list and the item list laid out as tiles, and
  `name_tiles.c`, a name as its two rows of tiles (`$C1:01A9`), which the
  battle's setup now calls as C for the characters', members' and enemies'
  names.
- `menu_bridge.c`, the machine side: the click, and the carries the
  original's callers go on with.

**The clicks.** Every move of a cursor plays the driver's click, a call to the
original's sound driver from `$C1:1B55`. Shadow mode compares that call where
the original makes it, with the stack the original has there, so the logic
calls its host's click at the original's point in the work (`MenuHost`), and
the bridge makes the call from the original's frames: the click routine's,
called from the menu frame's site for that move, and for the cursor moved over
the enemy an area goes on, the choosing's and its kind's frames too. A table
in the bridge keeps the sites; the logic says only which move clicked. Which
of the frame's four calls to the choosing a click is under the bridge works
out from where the menu stood as the frame began, and the kind's own sites
from the kind the choosing was given.

**The carries.** Liveness has the menu's frame's carry read: through
`$C1:17DD`, which passes it on where it places no cursor, and the frame step
`$C1:10E3`, into a battle message's frame waits (`$CD:0021`), whose caller in
the main loop then clears the party's statuses (`$C1:EA9D`), whose first
multiply (`$C1:C90B`) takes the carry into a product's high word the next
pass clears. Nothing acts on it, which no fuzz run can show (it compares up to
the return) and the library can: `ct_dev --poison-return C11153 --poison-set C`
inverts it at every return in the shipping mode, 63,268 times over the
library with every trace the same, and the entry declares it with that note,
a new form `tools/dontcare_check.py` and `tools/dc_evidence.py` check
(PORTING.md, "Declarations liveness can't prove"). The choosing's, the rows'
marking's and the item list's carries, which the original's code reaches only
where the menu's frame is the original's, are still worked out in the bridge,
from the logic's own decoders (`battle_target_pick`,
`battle_tech_entry_is_a_tech`).

**The walk with nobody leading.** The new timings moved a movie's presses
(late-omen-terramutant, shipping mode) onto a scene with the party's leader's
place empty and a direction held, which the field walk's guard refused. The
original walks the slot the empty place's marker names, past the object
tables, and the screen follows it; `field/walk.c` now does the same, the
slot's reads and writes in four functions (`Walker`), and the guard's end of
the 16-bit range is exact (the box's right side inside it), where it had a
margin the slot's place fell in. A native build has nobody to walk (TODO.md).

**What the gate found.** The first shadow runs showed the clicks played after
the frame's work don't compare (the bridge above); the stat block noted for
nobody reads the ROM's table past its eleven (`battle_stat_block_place`); an
item's name in the list starts past its icon and shows ten tiles, a colon
after; the beam's targets are taken together before its click; and the
choosing's last test looks at the targets, not the list. Fuzzing found the
rest of the item list's top (a top past `$FA` reads past the records: the
guard takes the game's) and the choosing's cursor past the list's eleven.

**What the guards take on trust**, none of it refused in real play: the
menu's member, slots and cursors as the window's guard has them, a member's
tech list no longer than twenty, the item list's top no further than `$FA`,
the cursor among the eleven targets, a stolen item a place to go, the queue
room for another action, a tech's partners party slots, the chosen item's
record's place, and A's high byte clear where the original carries it into
an index. The rows the tech list is laid out with set a cell no frame sees
(`battle_tech_list_being_laid_out`); a state with it set is the original's.

**Validation.** The library's 28 movies in shadow mode, none mismatching: the
menu's frame 105,265 calls (668 interrupted), the choosing 3,105 (74), the
tech list 6,626 (75), its rows' marking 470, the item list 912, the converter
23,673, the stat block 110,172, the frame around the member 17,786, the
click 5,486 (each but three compared within its caller's), the area search's
entry 46; the menu window and its cursors, the targeting and the setup as
before. **Fuzzing**, seed 1 (`tools/fuzz_routines.toml`), none mismatching: the
menu's frame on its pages 0 of 4,258, choosing a target 0 of 4,250 and with
the window turned over 0 of 4,241 (the driver answering at once,
`--apu-loopback`, so the clicks' calls come back), every target kind 0 of
10,000, every area shape 0 of 10,000, the lists, the converter, the stat
block, the frame around the member and the click 0 of 2,000-5,000 each; the
callers (the targeting's five variants, the menu window, the cursors, the
setup, the member's turn) fuzz as on main. The menu window's guard now asks A's
high byte clear, with which the original notes the member's stat block (its
fuzz runs give A a clear high byte).
The reference play's fallbacks fell from 45.2 KiB and 347.2 million
instructions to 41.9 KiB and 321.9 million, 39 routines leaving the baseline
(`$C1:1153`, `$C1:0B0B`, `$C1:09B0`, `$C1:25A3`, `$C1:2701`, the step 7, the
targeting kinds, the name converter and its dispatch from the setup); the
battle's native coverage rose from 85% to 91% of the code real play runs.

**Left for later.** The battle's frame step that calls the menu (`$C1:106E`,
`$C1:10FA`), the action's showing step (`$C1:4058`, which spans
frames on the frame wait), and the animation commands (`$C1:45A0`,
`$C1:432C`) are the next by size; `$CD:0027`/`$002D`/`$0030`, the messages
the menu window still calls, wait for the battle messages' port.

## Phase 3, field and events: the console's start and the field's frame

The sixteenth chunk of bank `$C0`, with the console's own entries: what runs
from power-on to the field's main loop, the loop's frame, a location's
loading, and the field put aside and taken up again around a menu and a
battle.

**How it was chosen.** The plan named the power-on path and the interrupt
entries (Phase 4a: `ct_play --generated off --interpreted stop` stopped in frame
0) and, for `$C0`, the field's setup and keeping (`$C0:0000`, `$01A5`,
`$038F`, `$011B`). By concept that is the field mode's skeleton: its two
starts, its frame, the location's loading the starts and the menus share, and
both sides of the hand-over to a battle. The fade in (`$C0:2824`-`$2C40`)
belongs to it too but is mostly calls into routines no port covers yet (the
window tile maps, the screen jobs' transfers, the map's unpacking), so it was
left for the next chunk; about 2.6 KiB in all, short of the plan's 8-12.

**What it is.** `core/console.c`: the reset stub (`$00:FF00`, entered in
emulation mode at power-on and in native mode by the game's own resets, and
its copy at `$C0:FF00`), the boot (`$FD:C000`: the console's registers as the
game starts them, three timing and input calls added for what had none,
`tm_fast_rom`, `tm_reset_timers`, `in_set_io_lines`, and the picture's
power-on screen, which moved here from `core/display.c`), the vectors
(`$00:FF10`, `$FF14`) and the two stubs in work RAM (`$00:0500`, `$0504`),
which go to the handler the running mode installed (`console_install_*`, the
stub's encoding in one place now) or return where the mode turned the
interrupt away. `field/main_loop.c`: the field's first start (`$C0:000E`:
work RAM cleared, the sound driver started, the menus asked about the
saves), its entry from the other modes (`$C0:0000`/`$005D`, the menus' sprite
screens past location `$1F0` handed to them), one pass of the main loop
(`$C0:0097`, a stop at its own head, so a save state never holds it), the
frame's work (`$00BF`), the effects' refresh (`$00DE`), a frame of the
leader's walk (`$00EB`) and the start of the frame wait (`$C0:EC60`).
`field/location.c`: the location's record (MD2's `location_records`, named
fields in `field_data.h`), its song (`$C0:1B53`), its tile set's graphics
(`$C0:0960`/`$6D2F`/`$6D65`, `$6DCF`) and looks (`$09DD`, `$0A14`), and the
field put aside for a menu (`$011B`) and set up after it (`$01A5`): the state
block moved to the event script's place and back, the party's places, the
party scripts' destinations the script's unpacking writes over, a gathering
the menu cut short. `field/battle_start.c` (`$C0:024C`, `$038F`): the enemies
on the screen found, the battle's ground, places, kinds and screen handed
over; `field/battle_return.c` gained the field after the battle (`$C0:0283`:
the game over, the place put aside, or the field taken up a frame at a time);
`field/palettes.c` the enemies' palette slots (`$C0:72B4`).

**Third trap, settled first.** Every call-out was checked against liveness
before the logic was written; three registers liveness keeps that the
callees never use were given the original's value as marked scaffolding
rather than threaded: A's high byte at the sound driver's start and the
menus' routine (the field's page, which the original set D through), and at
the song's driver calls (the record's place, which the original worked out
in A). Where a callee reads the overflow (the scripts, the dialog window,
the frame wait), the ports leave it as the steps before them do.

**What the shipping mode found.** Shadow mode passed a main loop that the
long routes (`suite.py --long-routes`) did not. A pass that called the
frame wait was a native routine held over every frame's end, which a
machine copy can only rebuild by replaying input from the last frame where
none was held (ct_drive, docs/MOVIES.md), so the routes' every probe
replayed from the field's entry; the pass now stops at the wait
(`$C0:00BA`), which sets A for the next pass itself. And a step the
registry turns away (the requests' guard, at a location change it can't
run) is a real call from the bottom of the field's stack, where the
original's location change resets the stack to its top and goes back to the
field's entry: that leaves the stack just as the call's return would, so
the runtime couldn't tell it from one, and the pass waited under the next
pass for good (eight deep after the Lavos cavern's fight in
`bucket_lavos`, which then spun in replays). The main loop's steps are
`field_run_or_hand_over` (`field/bridge.h`): where the sibling won't run as
C, the port makes the call and stops at it, and the rest of the pass is the
runtime's. The tile graphics' transfer cells are described
for each of the six places whether the set has a block there or not. And a
runtime limit: the object update run as C inside the frame's work, inside
the frame, made the original's own entry into it a third nested comparison
waiting at the same multi-pass call (the views' `$C0:A88D`) as the frame,
which settles it as a mismatch; the update is a call of the frame's work's
(docs/PORTING.md, "Call-outs"), and runs as its own port.

**What fuzzing found and cost.** Each new entry has a `tools/fuzz_routines.toml`
entry (seed 1; 0 mismatched on all of them, the counts in `docs/ports.tsv`).
The battle's start was interrupted on all 31 of its library calls and the
field after a battle on 25 of 26, so fuzzing is their check: it found the
current object the original names before an enemy without a sprite takes its
palette slot. The battle's start needed constraints to reach its body (the
objects' roles, sprite tiles and view, the party's places); the setting up
after a menu needs the dialog window where the game puts it, since elsewhere
the original's line effects' builder runs off its table. The callers whose
call-outs became C calls (the requests, the menus, the screen effects, the
party commands) were fuzzed against `main` with the same seeds: the same
counts.

**What is left at the edges.** The fade in and the objects' and sprite
lists' setup (`$C0:2848`, `$C0:56A6`, `$C0:B192`) are call-outs from the
entry, the map's unpacking (`$C0:A33B`) and the script's (`$C0:56D4`) from
both loadings, the dialog window's step (`$C0:1F87`) and the scheduler
(`$C0:59D9`, `$C0:5A46`, `$C0:CA76`, beam-coupled, PLAN.md Phase 4b) from the
frame; and the menus' routine `$C2:8004` from the first start, where
`ct_play --generated off --interpreted stop` now stops, in frame 2 (it stopped
at the reset, in frame 0). The palettes' jumps `$FD:FFF4`/`$FFF7` stay
call-outs: registering them as the routines they jump to would make the
battle's own calls through them calls into a registered port. `$C0` stands
at 52.1 KiB ported, 12% left.

**The timing gate.** The new rows were calibrated over the library, and
thirteen entries have exceptions (`tools/timing_exceptions.txt`). The console's
vectors are a jump with no access of its own to scale. The field's frame, its
entry, the loadings and the returns run nearly all their work as C at their
callees' calibrated times, and what is left as their own is a remainder one
scalar can't fit, or a data-dependent unpacking with the screen blanked. The
leader's walk, now run as C under the frame, has two kinds of call: the
value that removes its drift leaves the short ones' tail. The vblank check
finds none of their writes moved.

## Phase 3, overworld: the world map on foot (`$C2:2F94`-`$C2:42E5`, `$C2:23CF`-`$C2:26A7`, `$C2:5628`-`$C2:57AF`, `$C2:62AE`-`$C2:6331`)

The eleventh chunk of bank `$C2` (history file, "Phase 3, menus: the party's
exchange", for the one before) and PLAN's first item for what was left there:
the world map as the party walks it. It is its own subsystem, `game/overworld/`,
sharing the sprite-object mode's objects and scripts with `game/menu/`
(`tools/ram_map.py`'s `SHARES`, `tools/effects_check.py`'s families).
`python3 tools/burndown.py` for `$C2`: 40.6K ported and 31% left with 450
entries before, 46.1K and 22% left with 462 after: about 5.6 KiB of new code
in 12 entries, all of it to the bar (`tools/portstyle.py --subsystem
overworld`: 0.01 machine operations a line, 19% of its functions on the
machine, all of them entry wrappers or the main loop's bridges;
`tools/portsmell.py`: no finding).

**What it is.** Each walker is an object of the sprite-object mode, its
handler run once a frame by the objects' loop (`menu_run_objects`):

- `overworld.h` and `party.c`, the walkers' vocabulary: the way a walker
  faces (the field's `Facing`), its spot on the map (`WorldSpot`), a step
  (`OverworldStep`, the step table's row), standing and walking with the
  animations the ROM keeps for them (`overworld_data.h`), the idle animation
  after standing long enough, heading for a goal and halting there, and the
  trail the leader lays (`TRAIL_STEPS` spots, each follower a fixed count
  behind) and the tile the party last stood on.
- `walk.c`, the leader (`$C2:3404`): the pad's direction read into a way to
  walk, a step of a tile when the ground lets it (`terrain.c`), the trail laid
  as it goes, and the travel's other steps (boarding, standing) where the
  vehicles leave it.
- `followers.c`, the followers (`$C2:3AE2`): each decides its way after the
  leader along the trail, and walks a leg across or down; where a wall is in
  the way it searches round it (left and right of the leg, `$C2:41B5`,
  `$C2:4205`, `$C2:425A`), and close to the leader it keeps its place
  (`$C2:4060`).
- `terrain.c`, the ground (`$C2:62AE`, `$C2:39D5`, `$C2:62ED`): the kind of
  an 8x8 tile of the map, the four that meet at a walker's feet, and the
  region a spot is in.
- `places.c`, the watcher of the ground under the leader (`$C2:3264`): the
  map's three lists of places (exits, events, halts) looked up where the
  leader stands still on a place's ground, the place's name shown, an exit
  taken through `overworld_leave_for`, which `menu/object_script.c`'s change
  of location now shares.
- `music.c`, the region's song (`$C2:2F94`) and the request a script or the
  music leaves passed on to the driver (`$C2:2ED9`).
- `sign.c`, the place's name on a sign (`$C2:5628`): the label cleared, the
  name typeset into it (`menu/typeset.c`), and the sign's sprites shown while
  the leader stands there.
- `main_loop.c`, the mode's main loop (`$C2:23CF`), a frame at a time: what
  the mode asks for, entering a place, leaving for a location, or the menus,
  the map view and the time dial with the overworld put aside before
  (`$C2:2E23`) and brought back after (`$C2:2E72`). The menus move the stack,
  so the port stops at their call and starts again where the original comes
  back from them (`overworld_back_from_the_menus`, `$C2:255A`).

**The third trap, settled first.** The handlers return to the objects' loop,
which reads the carry (clear: the object goes on) and P's M width: the
leader leaves the accumulator 16-bit only mid-step, which the wrapper derives
from the step (`leader_mid_step`), not from the logic. The main loop's
call-outs read nothing but the frames to wait (X and A's high byte, given as
the original gives them) and the driver's result; the menus' entry at
`$C2:8000` reads A's low byte and X (the past-the-last-piece offset the loop
computes), A's high byte, Y and the carry declared dead. Each declaration is
`tools/dontcare_check.py`'s.

**An event's scripts and the halt.** An event the party steps onto gives
each walker a script (`menu_member_scripts`), which it takes up and runs a
frame at a time (`$C2:3772`, `$C2:37A1`, `$C2:3D9C`, `$C2:3DC1`): the
walkers' logic asks for the frame's run through a service
(`WalkerScripts`, `overworld.h`), and the entries give it the interpreter's
bridge called as from the walker's own call (`menu_run_object_script_from`,
`menu/script_call.h`), with that call's return on the stack for a command the
bridge hands to the original's interpreter. The accumulator's width the
walkers leave the objects' loop is worked out from the frame as it starts, as
a script may change anything after. The halt (`$C2:250D`) is an arm of the
loop and an entry of its own: the screen stilled and the story's flags given
back, a pass at a time that comes back to itself, as the original spins.

**What the guards refuse.** The vehicles' controller (`$C2:309E`) is the next
chunk's. A mode the loop doesn't know, or entering a place past the map's
lists, hands over to the original's loop at `$C2:23D2`, before it touches a
register: neither is a state the game makes.

**Where the party stood.** Leaving the world map keeps what the camera looks
at, which the original works out as its top-left tile plus sixteen each way,
half its 256-pixel width. That is the camera's convention, not the screen's
size: `menu/map.c` keeps it beside the centring that puts the camera there
(sixteen columns and fourteen rows off the party, half the original's
256x224), as `menu_map_camera_looks_at`, so a screen of another size draws
round the same camera and a place kept doesn't depend on the screen it was
played on. The leader's own spot gives the same tile on foot but not where a
scene's script has moved the camera (9 of the library's 20 leaves differ, all
of them a script's), so the camera it stays. Elsewhere the guards keep
to states the game makes: sprite scripts well formed
(`menu_given_sprite_script_is_well_formed`), the map scrolled within its
room, the lists' lengths and an exit's offset whole records, the trail's next
spot even and inside it, a spot on the region map.

**What the gate found.** Fuzzing found the region lookup reading before the
region map for a spot in the map's first row (the guard keeps the party off
it); the sign's clear one byte too long (the original's block move clears
exactly the label); and the guards refusing the frame a walker sets off on,
before its sprite script is set (they now take the setting-off step). Shadow
mode found the step table's row the dispatch leaves in `$00`, which the
walkers' callers don't read and the gate compares
(`overworld_leave_step_table_row`, scaffolding); and the follower's wall
search reading past the map's last row, as the original does, through
`overworld_map`'s `unchecked` pair (docs/TODO.md); and bringing the
overworld back leaving A all ones, its block moves' count run out.

**Validation.** The library's 27 movies in shadow mode, none mismatching: the
leader 14,892 calls (2 interrupted), the followers 29,784, the watcher, the
region's song and the sign 14,892 each, the ground's lookup 14,579 (2
interrupted), the region's 9,584, the main loop 24,017 frames (3 of them
abandoned where the menus unwind the stack), the overworld put aside and
brought back 6 times each, the return from the menus 2. **Fuzzing**, seed 2,
none mismatching (`ct_fuzz` from `menus-epoch`'s frame 3300,
`tools/fuzz_routines.toml`'s `overworld` state): the leader 4,931 of 5,000
(the rest refused), the followers 4,913, the watcher 5,000, the region's song
4,993, the sign 5,000, the ground's lookup 5,000, the region's 4,925, the
return from the menus 2,000 up to its first call-out, putting the overworld
aside and bringing it back 2,000 each, and the main loop 200 up to the frame's
wait (`$C2:0346` spins on the vblank count, which no fuzzed state changes, so
the arms are shadow mode's alone). What no movie runs
(`docs/movies-wanted.md`): an exit to an event, the exchange opened from the
map, walking into a new region's song, the dactyls, a halt, the walkers' event
steps, the sign cleared from the menus. The event steps are fuzzed in their own
variant (`tools/fuzz_routines.toml`, `event`), 5,000 states each for the
leader and the followers on three seeds, none mismatching, with scripts of
the game's commands: a script of random bytes can copy the map over the stack,
which the original survives differently.

**Timing.** Every entry is calibrated from the shadow runs. Putting the
overworld aside and bringing it back are block moves of about 24 KiB,
which the main loop runs as C: each is charged at its own routine's rate
(`charge_as`, `$C2:2E23` and `$C2:2E72`, registered for it), not the
loop's. The leader's frames that scroll the map are off by what
`menu_map_scroll_x` and `_y` are off by, timed as them (`TIMED_AS`): a
frame that builds a new column or row of tiles is 7,000-14,000 clocks
off, writing RAM, which `tools/timing_exceptions.txt` accepts for the
leader's vblank-tail and vblank-worst. The world map's HDMA, stopped as
the menus open, stops on the fade-out's last frame at brightness 1
rather than in forced blank (`tools/vblank_check_exceptions.txt`).

## The absent party member and the object off the map

Two guards from event's and field's combined pass refused calls the game
really makes, so the original ran them, and a build without the original has
nowhere to send them (PLAN.md, "A guard becomes an assertion"). Both are
ported now: what the game does in each case is decided in its own terms, and
what the original reads past its tables to get there is one marked piece of
scaffolding each, which the native-shape build compiles out.

**A member who isn't in the party.** Scripts written for a full party address
its second and third members whether or not anyone is there. The party keeps
an empty place as a link to none, `$80`, and the original takes that byte for
an object's offset like any other: the slot one past the object tables, whose
role is object 0's script position's low byte, whose position is object 0's
vertical position and horizontal velocity. In the game's terms the member is
nowhere: the language has an operand kind for a member by their place
(`MEMBER`, `event/script.h`), and the commands that take one ask for them as
`Someone` (`event/party.h`) -- whether anyone is there, whether they are
disabled, where they stand. A walk or a follow toward nobody is over at once,
as toward a disabled object; noting the tile of nobody leaves the variables as
they were. For the gate, `member_in_place` fills in the original's phantom
through `ORIGINAL_EFFECT(see_what_the_original_finds(...))` (`event/party.c`),
which reads the slot through the unchecked accessors of the role and
position (`field/object.h`, `object_past_the_tables`; `unchecked` in the map
says why). Over the library the phantom mostly reads as disabled, which is
what the game means; 3 calls in each of the stand-in objects' 5,000 (the
objects for members 1 and 2 in the first maps of a new game, out of view)
catch object 0's script at an offset whose top bit is clear, and set off
toward a tile that isn't anywhere, and the ending that notes an absent
leader's tile every frame has nothing read the variables before the map
changes. Neither shows. One read comes later in the original than in the
port -- lining up, it stops the walker before reading the other's position,
which for walker 0 zeroes the phantom's fine vertical position -- and the
scaffolding function makes that correction too. The guard now refuses only an
odd marker, which would read the tables out of step and which the game never
makes. `$24`, `$A9` and `$07` (waiting) still take a member's object without
asking, and leave an empty place to the original; the game makes none of
those calls (TODO.md).

**An object off the map.** Scripts put an object that follows the map at tile
column 64, or at (255, 255), to take it out of the way, and the original
layers its sprite from the bytes past the attribute tables' rows (column 64's
upper attributes are column 0's lower ones; (255, 255) reaches bank `$7F`).
Off the map there is nothing to layer against: `object_layer_as_the_map`
keeps the priorities the object had, and `ORIGINAL_EFFECT(layer_as_past_the_map(...))`
(`field/object.c`, through `map_tile_attributes_past_the_map` in
`field/layers.c` and the attribute tables' unchecked accessors) does what the
original does. The guards on `$8B`, `$8C`, `$8D` and `$8E` are gone, and the
map's openings run those commands as C too. The field's own step onto a tile
off the map (`field_start_step`) is still only reached by fuzzing (TODO.md).

**What moved.** Shadow mode over the 26 movies, against `main`'s logs: no
mismatch, and every call these guards refused is compared.

| Entry | Calls | Refused before | Refused after |
|---|---|---|---|
| `event_cmd_move_toward_party_member` | 72,121 | 52,898 | 0 |
| `event_cmd_follow_party_member` | 70,971 | 52,898 | 0 |
| `event_cmd_note_tile_member` | 38,500 | 2,735 | 0 |
| `event_cmd_stand_on_tile` | 1,013 | 9 | 0 |
| `event_cmd_place` | 520 | 1 | 0 |

Every call of each matches. (The 10 commands the openings ran inline were
handed to the original's handler through the table before, and are the
openings' own C now.)

**Fuzzing.** `tools/fuzz_compare.py` against `main`'s `ct_fuzz`, seed 1,
20,000 states each, over the 60 entries these touch: 0 routines differ in
mismatches. Each member command has an `absent` variant with every place of
the party empty and its marker any byte from `$80` (odd ones refused): `main`
compares none of those states, this tree about 2,400 each, all matching; the
tile commands have an `offmap` variant with the tile from column 64 on, where
`main` compares 4,875 and this tree 9,987, all matching.

**Timing.** The member entries' time now includes calls that were the
original's, and reading `Someone` reads the fine position too, so their rows
were recalibrated. The search for an object in the way, which the moves run
inline, charges the original's per-slot work the port doesn't do
(`field/motion.c`, `charge_search`: accesses for each slot passed, each in
view and each measured, at `field_object_in_the_way`'s own rate, through
`charge_as`). That took `field_object_in_the_way`'s mean error from 69% to
40% of its time, and the vblank tail of it, `event_cmd_move_to_tile` and
`event_cmd_move_toward_party_member` from 1.08%, 3.09% and 1.23% of their
vblank calls off by 1024 clocks or more to none, so their three exceptions
left `tools/timing_exceptions.txt`. `event_run_init_scripts` keeps its own:
its error follows its walk over the slots, which this doesn't touch.

## The refusals in real play, retired

Of the 21 entries whose guards turned away calls real play makes (7,300
refusals over the gate's runs when the batch began), 17 were gaps and 4 the
attract program's own code at an address a port of another program's reads.
Each gap's state was found from `tools/fallbacks.py --refusals -v` and from
the guard's own conditions printed at the refusal, and is ported now. What
the four have in common, and what the gaps fell into:

**A handler that goes on into another's.** Four of the refusals were never
the game's calls at all in a build without the original: shadow mode runs the
original as the reference for a port, and where the original's handler jumps
or falls into another routine's code the runtime meets that routine's entry
in the middle of the first one's work. A fifth is the game's own: a task that
comes back round to its head. The menus' song unless kept (`$3D`)
goes on into the song's handler (`$C2:18F9`) with an 8-bit accumulator; the
battle's end unless listed (`$25`) into the end's (`$C1:4CF0`) with Y past
its byte; the throw that waits first (`$D4`) into the plain throw's
(`$C1:7364`) once its wait is over; the screen script's two model commands
into the sized triangle's (`$CD:2173`) with X the shape and the carry clear;
and the gate's song task (`$C2:CBBF`) comes back to its own head with an
8-bit accumulator after each frame's wait. Each guard now takes the state
the other handler leaves -- the command at the script pointer is the one
that goes on into this one, where what it found makes it do what this one
does (`menu/script_bridge.c`, `goes_on_into`; `battle/scripts.c`, the same)
-- and the triangle has an entry of its own that draws the command's
triangle whatever X holds (`battle_screen_draw_triangle`). The precondition
of a routine that sets its widths itself no longer asks for them
(`NEEDS_X16_BINARY`: the gate's song, the description).

**A guard narrower than the game.** The scroll bar's start asked for the
menus' data bank where its one cell is low RAM any bank reaching it gives;
the shop's member opening asked that the member worked on be a reachable
one, where the opening takes the member from the cursor (the equip screen
reaches it with the last one past them); the event command `$84` took only
0 and 1 where the script sets the object's contact byte whole, a pushable
object's bit included (`ObjectContact`, `field/object.h`); and the jump to a
place took only the 72 places, where the original picks a place's kind by
its byte doubled in a byte: `$80` is place 0's kind, channel `$80`'s subject
(`script_places.c`, `decode_place`).

**Nobody where the original reads a slot.** Four states have the original
read or write past a table for an empty place, kept in one marked function
each, as typed "nobody"s (rubric item 103), and in docs/TODO.md: a timer of
nobody's wearing off (`battle_timer_wears_off_past_the_rows`, whose writes
land on the poison's length for the third slot among others); an enemy whose
target is nobody waiting on where nobody stands
(`where_the_original_finds_nobody`); a combatant channel for a partner the
party doesn't have playing and waiting on that slot, its indexes doubled and
tripled in a byte (`play_nobodys_animation`); and the confirm button pressed
with nobody leading, searching before the slot past the object tables and
looking for a chest on the tiles off the map before it (`FieldLeader`, now
the walk's too, and `walkable_past_the_map`). The game's state reads them on,
so they are plain writes, the same in both builds.

**The machine's own state.** The battle's vblank handler in bank D1 sends the
palette without setting the data bank, so on code with work RAM's bank the
transfer's register writes land in work RAM and the palette isn't sent
(`uploads.c`, `send_palette_through_work_ram`, through the SNES escape
`vid_snes_send_palette_into_memory`, which makes the site's writes in the
data bank's memory: the palette change shows a frame late, and ten stray
bytes land at `$7E:2121`, `$7E:4370`-`$4377` and `$7E:420B`). And an
ending's menu object script sets the sprites' sizes by writing `$2101`
through its byte command: a SNES escape for a register a game's data names
by its address (`vid_snes_takes_register`, `vid_snes_write_register`,
registered in the lint test for `menu/script.c` and its bridge), which takes
only that register. The attract program's frame wait (`$7E:3023`) calls the
routine its call's operand names, which the attract demo rewrites to its own
frame work: the port reads the operand and runs that routine as the attract
program's.

**What stays.** The four code-presence refusals left are the attract
program's own routines at `$7E:424D`, `$7E:4D0D`, `$7E:357B` and `$7E:36CC`
(their bytes match its image, `attract_program`, and not the jet bike race's
or the title's the ports read): the scene program's port is what retires them
(PLAN.md).

**Measured.** Shadow mode over the library: 0 mismatches, every refused
entry compared where it was refused (`script_play_and_wait` 1,959 calls in
the Bucket ending, 2 interrupted; `battle_upload_palette` 277 in the attract
demo). Refusals in real play: 1,043 at 4 entries, all four the attract program's (7,300 at 21 before). Fallbacks, reference play:
29.1 KiB of code and 171.6 million instructions in place of hand ports, refused 0.8 KiB and 14,521 (30.3 KiB, 181.5 M, refused 2.2 KiB and 10.1 M before); the shipping mode 27.0 KiB, 391 M, refused 0.8 KiB and 44,314 (28.0 KiB, 410 M, refused 2.4 KiB and 40.3 M). The attract demo's frame wait moved its passes from `$7E:3023` to the scene engine's `$C3:0000` in the baseline: a pass that runs the attract program's frame routine is longer than the idle entry's pass takes back. **Fuzzing**, seed 1, `tools/fuzz_compare.py` against `main`'s
`ct_fuzz` over the 422 entries the batch touches (the menus' object script's
commands, the battle's script and screen script commands, the timers, the
movements, the confirm button, the walk), 5,000 states each: none mismatch
where `main` didn't, and the 13 new variants (one per state the batch takes:
`tools/fuzz_routines.toml`, `nobody`, `unlisted`, `waited`, `from_model`,
`work_ram`, `register`, `nobody_leads`, `bank00`, `stale`, `m8`, `m16`,
`unless_kept`) compare every state their guards take with 0 mismatched.
`main`'s triangle took 764 of the from-model states and mismatched 754; the
end unless listed first mismatched where its script pointer read I/O, whose
operand the port reads and the end's handler doesn't, and the guard now asks
for the script where the game keeps them. A `high_places` variant of each of
the 15 commands that name a place forces the places `$80`-`$88` from a script
in work RAM: 0 mismatched, after one finding -- a sound played from a place
whose subject is a slot far past the combatants reads its position from the
sound driver's command, which the original has already written the sound
into; no script names one, and the sound commands' guard turns it away
(`script_place_reads_the_sound_command`).

**The native shape's New Game+.** The native-shape build took 58 frames from
the load screen's menus closing to the Battle Mode question, against 14
(docs/TODO.md had it):
`TIMED_AS` was compiled out with the scaffolding, so the work a port runs
inline as another routine's was estimated at the outer port's rate, and the
New Game+'s start runs inside the menus' ports. A dump of work RAM each
frame over the two builds (`ct_drive`'s `wram`) showed the time spent in one
stretch with the menus closed and nothing in work RAM moving: 60 frames to the
default build's 17. With `TIMED_AS` kept the native shape takes 13 (the rest
is the scaffolding's own charges and accesses, which it leaves out). The tag
isn't scaffolding -- it says whose rate the logic's accesses are estimated
at -- and both builds keep it now (`game/charge.h`).

**Timing.** Six rows moved and were recalibrated (`--drifted`): the sized
triangle's from 5,899 to 9,906 (its stretches now include the ones entered
from the model commands), and the script commands that ask for a channel for
nobody or take a fall-through by a few percent (`script_end`,
`script_play_and_wait` and its two siblings, `script_throw_moves`).

## Phase 3, battle: an action shown (`$C1:4058`-`$C1:4A70`, `$C1:305C`-`$C1:3215`, `$C1:106E`-`$C1:1114`, `$C1:1B67`, `$C1:EC39`-`$C1:ED88`, `$FD:ABA2`, `$CC:F102`, `$CC:F156`)

Chosen by the fallback measure: after the player's choosing, the battle's
largest handbacks were all the original's code its ports called into to show
an action -- the showing's step (`$C1:4058`, which spans frames), the loaders
of the action's animation (`$C1:432C`, `$C1:45A0`, `$C1:475B`), the hits
landing (`$C1:EC7F`), the combatants' poses (`$C1:305C`, `$C1:308C`) and the
frame step the showing and the effects' loading take between their waits
(`$C1:10FA`, with `$C1:106E`, the battle's step 3). The chunk takes the whole
of showing an action, from the battle's step 4 to the damage applied.

**What it is** (each file's header says how its pieces fit):
- `action_showing.c`, the showing: the objects reset, the action's subjects
  listed for the scripts' channels (the actor, the two others taking part or
  for a member alone the other two members, the targets), the animation
  loaded and played a frame at a time until its scripts signal, the effects'
  sounds cut off, the screen's scripts let finish, the effects stopped, the
  enemies drifting on from where they stand, every combatant's pose taken
  afresh, and a party member the damage shown takes to nothing falling at
  once.
- `action_animation.c` and `action_animation_data.h`, the action's animation:
  a record of seven parts (script set, two graphics sets, sprite tiles,
  palette animation, animation records, background) chosen by the kind of
  action -- a party member's weapon in one of three views (from afar, up close
  where it finds the way to its target, a critical hit), an enemy's first or
  second attack, a tech of either side, an item -- each kind's tables
  (`AnimationKeeper`) behind named accessors; the script set's channels
  started (a missed action shows the set's second half); the graphics loaded,
  the background changed, the tiles unpacked and the bitmap shapes built over
  the frames that follow.
- `pose_looks.c`, what a pose shows beside its frames: a marker (the clock,
  confusion, sleep, the sealed techs, blindness, poison), the palette cycling
  (haste, slow, shield, barrier), or the held pose's tint, as a C table of the
  poses (`Pose`, named from the status that gives each, `$CC:F77F`); and the
  poses' round. The party's animation step (`$C1:2D9F`, `animation.c`) now
  calls the look as C, so it is C whole.
- `frame_step.c`, the battle's own frame: the menu's turn every fourth frame
  (the next member in line takes the menu, `$C1:1B67`, which moves the line up
  as the original does, the third place keeping its member), the frame work's
  tail, and the step 3 of waiting (the combatants' and the enemies' effects'
  steps, and L and R refused where the battle can't be run from).
- `damage.c`, the hits landing: a target any of whose hits missed takes none
  of them, the rest come off HP or MP held between nothing and the maximum, a
  combatant brought to nothing is knocked out, a felled enemy's spoils go to
  the winnings (`$FD:ABA2`: experience, gold, tech points, its charm or, nine
  times in ten where it still has it, the item it drops). The enemy's turn and
  the status timers now apply them as C.

**The third trap, settled first.** Every call-out the chunk keeps was checked
with `tools/dontcare_check.py --list`: the frame waits (`$C1:007E`) read A's
high byte alone, the frame work's loads (`$CD:0015`, `$CD:002A`, `$CD:0018`)
A whole, the messages A's low byte, the sound driver nothing. The registers
the original's callers read after the new entries are worked out in their
wrappers from what the work leaves: the showing's carry (the poses' round's
last count, set), the step 3's (the boxes' last test), the loaders' A's high
byte. One declaration needed evidence: the scripts' channels
(`battle/scripts.c`, `$C1:4C9E`) read neither X nor the overflow they are
called with, which the showing's C steps no longer leave as the original's
did; `ct_fuzz --poison-dc battle_run_scripts` scrambled 15,072 call-outs over
3,000 states with none differing, and the call carries the note.

**Narrowed.** The step dispatch (`$C1:0003`) and the showing's own stub
(`$C1:BFA4`) stay the original's: their callers' declarations carry the
library evidence for the overflow they hand the steps, and the gain is 0.1
KiB. The menu's turn through the frame work's wait (`$C1:0012`, `$C1:10E3`)
has every register live after it. The frame work's loads (`$CD:0015`,
`$CD:002A`) and the background's change (`$CD:0018`, `$CD:0EBD` and on) are
the next chunk: they wait on the vblank handler to send VRAM.

**What the gate and fuzzing found.** The script set's halves are read in long
addressing, a set at the bank's end running on into the next bank (a random
item's); the 16-bit flag-and-pointer writes that start a channel clear the
next channel's flag and the pointer's pad, kept in one storage function
(`start_channel`); the poses' palette-cycle and marker tables are by battle
slot, eleven, not the party's three (`re/ram.toml`); a party member's palette
past the sprites' would run the colours on into the working palette (the
looks' guards take only the game's five to seven). The felled enemy's spoils
read their enemy's cell (`$0E`) as a word with the one after it, which ten
landings of hits in the Lavos fights find set: the port reads it as the
original does (`felled_enemy`, the standing and id that many pages on), after
a guard that asked for it clear refused those real calls. None of the ten
fells an enemy, so the quirk changes no spoils over the library. The native shape found the
loaders' guards first asking for cells the showing writes only as
scaffolding (its note of the second half, the animation's cells forgotten):
compiled out, they refused, and the original's loader ran on stale cells and
hung two of the late movies; the guards now ask only for the combatants.

**Validation.** The library's 28 movies in shadow mode, none mismatching: the
showing 984 calls (853 interrupted, every one of them spanning frames; its
call-outs are compared at each), the poses' round 10,791 and the looks 134,063,
the hits 1,146 and the missed targets' 1,146, the spoils 70, the step 3 27,547,
the next member's menu 105,265, the subjects' listing and the poses forgotten
984 each, the falling 504, the up close 131, the party's animation step 27,547;
the loaders (984, by kind 154, 736 and 94) and the frame step (296,180) run
nested in the showing and the frame work and are compared within theirs. No
real-play refusal.
The reference play's fallbacks fell from 41.9 KiB and 321.9 million
instructions to 38.9 KiB and 311.4 million, 26 routines leaving the baseline
(the showing, its loaders and the up close, the poses' round and looks with
five of their handlers, the hits, the missed targets and the spoils with the
stubs they were called through, the step 3, the frame step and the next
member's menu, `$CC:F102`, `$CC:F156`); the battle's native coverage rose
from 91% to 97% of the code real play runs. What the showing still hands
back is the frame work's: the background's change (29 million instructions),
the graphics sets' loads and the frame waits.

**Fuzzing**, seed 1 (`tools/fuzz_routines.toml`), none mismatching: the
showing up to its first wait 0 of 3,000, the loading up to the graphics' load
0 of 2,453 and with a missed action up to its kind's loader 0 of 5,000 (the
dispatch leaves the kind doubled in A and X, as the original's loaders are
called with), each kind's loader 0 of 3,000 (entered with the animation's cells
forgotten, as the loading leaves them; the loaders' guards ask only for
a combatant acting, since those cells are the showing's scaffolding), the up close, the
subjects, the falling, the next member's menu, the hits, the missed targets
and the spoils 0 of 20,000 each, the poses' round 0 of 4,102 and the looks 0 of
4,135 (the rest a party palette past the sprites', which the guards refuse),
the frame step up to the frame work's tail and the step 3 up to its first
wait 0 of 3,000. Against the tree before the chunk, all 804 battle entries the
same counts but the party's animation step, whose guard now refuses the same
palettes (896-962 states, the same mismatches), and the timers' HP loss, the
same with the felled cell read as the original reads it. A fuzz run calls a
sibling port as the original's call, so the bridges leave the registers the
original's code leaves at the calls it makes into siblings (the menu's count
tested before the frame work's tail, the kind doubled).

**Timing.** The new entries and the battle's entries whose time moved with
them were calibrated from the shadow runs; the showing's worst stretch (81,509
clocks, one in 2.3 million) is in `tools/timing_exceptions.txt`.

**Tools.** `tools/effects_check.py` cached a function's summary made under its
depth cut, so a function first reached deep in the call graph lost its plain
writes for every later caller: the window's digits showed as written only as
scaffolding once this chunk's calls moved the order. A summary made under the
cut is no longer kept.

## Phase 3, menus and overworld: the vehicles, the screen's objects and the calls made where they go (`$C2:309E`-`$C2:324B`, `$C2:2345`-`$C2:23A7`, `$C2:42DD`-`$C2:4A9A`, `$C2:1CF5`-`$C2:2259`, `$C2:8953`, `$C2:7849`, `$C2:78A1`)

The twelfth chunk of bank `$C2` (history file, "Phase 3, overworld: the world
map on foot", for the one before), and the first picked by native coverage:
PLAN's first item, `$C2` at 71%, the lowest of the subsystems. Its two largest
fallbacks were trampolines rather than code: the objects' loop called every
handler through the original's `JMP ($0050)` at `$C2:0553`, and the scripts'
call commands every routine they name through `JMP ($005B)` at `$C2:1819` and
`JML [$005B]` at `$C2:1836`, so whatever those reached was filed under them.
Behind them were the handlers nothing had ported -- the vehicles, and the
objects a script starts to change the screen -- and the routines the scenes'
scripts call. The equipment screen's list of what fits (`$C2:8953`), the
largest single routine still called out to, came with it.

**What it is.**

- `menu/objects.c` and `menu/script_bridge.c`: the loop calls a handler, and a
  script's call command its routine, at the routine's own address, as the
  original's call reaches it after the jump (the stack is the same, and shadow
  mode pairs the call where the original arrives). A ported one runs as C
  through its registered entry; the jump itself no longer runs.
- `menu/screen_objects.c`, the objects that change the screen: the fades out
  and in (`$C2:20A2`, `$C2:2105`, one of each at a time, `menu_fades_running`),
  the mosaic growing and clearing (`$C2:21F8`, `$C2:2194`), the palette loader
  (`$C2:1DD4`) and its fade of a row over 32 steps (`$C2:1E38`-`$C2:2061`: each
  colour's part that falls moves on the first steps, one that rises on the
  last, by a count kept in the object), the tile streamer (`$C2:1CF5`), and
  clearing the palette as the mode starts (`$C2:1DB5`). The display's two
  bytes are fields now (`menu_object_display_blank`, `_brightness`), and the
  loader's "hand-over" flag was the fade's speed (`palette_frames_a_step`).
- `overworld/vehicles.c`, the vehicles' controller (`$C2:309E`): the confirm
  button next to the Epoch or the dactyl boards it, all aboard the party rides
  it, the vehicle letting the party out gets it off; and the test of whether
  two things on the map are within reach of each other (`$C2:2345`), each by
  its reach record (`overworld_data.h`).
- `overworld/epoch.c`, the Epoch on the world map (`$C2:42DD`): standing,
  rising, flying a tile at a time with the map carried under it, coming down
  where the ground under it is open, the time gate's dial and the scenes it
  leaves and arrives in; its second sprite (`$C2:495E`), the watch for the
  Black Omen under it (`$C2:49CF`, the sign `$4C`), and the two routines its
  scenes' scripts call (`$C2:48A9`, `$C2:48CD`). The song of what the view
  shows as it lands (`$C2:2F4E`) and the tempo put back as it arrives through
  time are `music.c`'s; the two tiles under it below the four a walker's feet
  stand on, `terrain.c`'s (`$C2:4A58`).
- `menu/map_scenes.c`: whether an object is in view (`$C2:78A1`, which the
  scenes' scripts call most) and an object drifting the map along
  (`$C2:7849`).
- `menu/equipment.c`, the list of what fits the slot and the loaded member
  (`$C2:8953`), called as C from the equipment screen's two sites.

**The third trap.** A handler's caller, the objects' loop, reads the carry
(done) and nothing else: it sets the accumulator's width again before it uses
A (`$C2:0545`, `$C2:054C`), so the width a handler leaves is declared dead.
That needed a new don't-care bit, `DC_WIDTH` (P's M bit), which shadow mode
leaves out of P's comparison at an entry's return and
`tools/dontcare_check.py` checks against `Liveness.width_read_after_return`
(PORTING.md); the walkers' derivation of the width went with it. The routines
the scenes' scripts call leave X and a carry that the interpreter after the
call command never reads (the command's length is never 0), but liveness
follows the interpreter's branch for a length of 0 and keeps them live, so the
wrappers leave them as scaffolding for shadow mode's comparison. The Epoch's
scenes go through the interpreter's bridge as the walkers' scripts do.

**What the gate found.** Shadow mode: the Epoch's step stored as a word on
reaching a tile, which clears the frame count beside it; and setting off for
`$1F2` without its wings, the original keeps the Epoch's spot through the
register it has just loaded with the arrival's place, the fourth object's
goal (`keep_a_stray_spot`, plain: the spot is the game's, and is set again
before it is seen). Fuzzing: the step note cleared as a word on one path only;
and commands and colours aliasing the slots, the mode's page or the work RAM
port, which the guards now keep apart (none of the game's are). The refusals
a first guard made of the game's commands in work RAM (event scripts) showed
in the library's refusal check and went with it.

**Numbers.** Over the reference play the code run in place of hand ports fell
from 33.1 KiB and 187.3 million instructions to 29.9 KiB and 179.1 million;
the menus and the overworld's handbacks from 6.2 KiB and 11.7 million to 2.9
KiB and 3.6 million, and `$C2`'s native coverage from 71.9% to 79.1% (47.1 KiB
run). Every new entry matches in every movie, and is fuzzed (seed 1,
`tools/fuzz_routines.toml`; counts in `docs/ports.tsv`). Calibrated rows:
the new entries, `menu_run_objects`, `menu_object_nmi` and the scripts' two
call commands, whose time moved with the trampolines, and the equipment
screen's and the shop's steps that make the list inline. Timing exceptions
(`tools/timing_exceptions.txt`): the Epoch's and the drift's frames that
build a strip of the map, as the leader's; and the list of what fits, 35
lines a call whose time follows the inventory, with the three steps that
make it.

**Left.** The dactyl's flight (`$C2:4CDA`-`$C2:5627`, about 2.4 KiB, reached
by no movie). The objects' loop still runs the handlers through the machine at
their own addresses, so each keeps its shadow comparison and calibration; a C
table of the handlers is the native program's (TODO.md). The rest of what the
scenes' scripts call (`$C2:2F0F`, `$C2:786F`, `$C2:79E0`-`$C2:7A55`,
`$C2:7B44`, `$C2:7BC4`, `$C6:E241`, `$C6:E27E`) is filed under itself now
rather than the trampoline, the same code; and the two menu refusals in these
files (`$C2:18F9` entered by the fall-through from `$C2:18F1`, `$C2:12B8`'s
cell in the endings) stay on the allowlist (TODO.md).

## Phase 3, the scene program: the staff roll (`$7E:8EA8`, `$7E:9E7B`-`$7E:9F2A`), and the scene engine's leftovers

The third chunk of bank `$7E`, after the scene engine's and the scene data's
last unported runs (history file, "Phase 3, the scene program: the rest of
the jet bike race", for the one before).

**How it was chosen.** The brief: the last runs `tools/burndown.py --uncovered`
listed in `$C3` and `$C6`, then the telepod ending. Finding out what each run
was came first, and most were ported already:

- `$C3:0B03`, `$C3:0B95`, `$C3:0C41` (467 bytes) are the sequence interpreter
  (`scene/script.c`), uncredited because the entry handed a command it doesn't
  take back to the original's whole loop, a call-out to `$C3:0B03` the walk
  can't go past. It no longer calls the loop. A command no script has (a store
  outside low RAM, a video setting the service doesn't express, a task taken
  off a list it isn't on or put on a full one) is handed to the original's
  code for that command, entered as the dispatch enters it: the script past
  the opcode, A and X the handler's place in the dispatch table with the flags
  of the shift that made it, and the dispatch's scratch word kept, so the
  stop is exactly the machine the original has there (what TODO.md asked for
  at the loop's head, one step further on). A call-out that comes back with
  the machine otherwise stops where the call returns. A script that never
  ends its turn now hangs as the original's does (the old limit of 65536
  commands, which handed it back, is gone: review item 113). What stays the
  original's are those handlers and the task searches they call (about 160
  bytes, which only what no script has reaches) and `$C3:0BEF`-`$C3:0C18`,
  which nothing reaches (`re/hints.toml`, "unreached").
- `$C3:09A4` (53 bytes) is the NMI's song send (`scene/frame.c`), uncredited
  because the burndown read `PF_CALL_DC(site, target, set)` as `(target,
  set)` and took the port as unreadable (fixed in the tool, which moved
  several banks a little); its stop where the data bank doesn't reach the
  registers was dead, since the handler goes on only where the frame's work
  left a bank that does, and is gone.
- `$C3:0A95` (106 bytes) is the frame's controller read past the console's
  busy wait, ported, behind the stop at that wait; it stays the census's
  `in_ready` rewrite (PLATFORM.md, "Timing without a beam").
- `$C6:E6D0` (126 bytes) was unported: a sprite-object scene's routine that
  jolts the running object a random step in a random direction, which a
  script at `$C3:B5AF` calls and no movie reaches. `menu_object_jolt`
  (`menu/object_commands.c`) takes two of the menus' random bytes, the
  direction and the size, and moves the object's pixel along each axis by the
  sine's size times the jolt's in 256ths; the menus' sine (`$C2:2273`/`$2277`,
  the table at `$C0:F800` with the quarter turns put in by hand) is inlined.
  The carry the scripts read is the place down wrapping, worked out in the
  wrapper from before and after.

Then the telepod ending, mode 13, which does not go through the shared ending
at `$7E:8538`: its setup, the credits' roll and upload (which the Beyond Time
ending shares, reaching them through `$7E:8538` at the slow speed), the
sprites' high bits, and the two routines its script calls. Left at the edge:
the stars (the scatter `$7E:5777`, the flight `$7E:579B`, and The End's drift
`$7E:9897`/`$7E:98AC` with `$7E:98E3`), whose work is to hand the random mixer
`$C3:02DD` the registers it mixes into the seed (A, X and the carry): they wait
for the mixer as game code (TODO.md, "The scene program's randomness and
parity"). The unpacker and the first script step stay call-outs, as in the
race. `$7E` stands at 12.6 KiB ported and 1.9 KiB inlined, 49% left; `$C3` at
8% and `$C6` at 0% (52%, 15% and 4% before, with the tool's fix).

**What it is.** `scene/staff_roll.c` is the scene: the staff credits rolling up
over a field of stars, then The End. The setup plays the music fast (the
driver's fast forward through the program's own send), sets the stars coming
and scatters them, stops the palette's animations (`palette_stop_animations`,
the call through `$FD:FFEE` made C), copies two palettes out of the program's
resources and unpacks four pictures into VRAM (a table, as the race's), sets
the letters' colours, blanks the credits' map (both halves of each word
filled, through the SNES-only escape, as the title's low-byte fill), sets up
the per-line effect, patches the star field's drawing to put every star
behind the layers and the credits' upload to scroll BG3 (both program
operands, now variables in `re/ram.toml`), starts the script, sets the layers
and starts the song (`scene_bridge_start_song`, now from any call). The roll
writes the next line of text into a row buffer as tiles when one is due
(every 16 frames, every frame fast) and moves the map's row on; at the end of
the text it takes the upload off the vblank list and itself off the frame's.
Its scroll (`$7E:9ED6`) is an entry of its own: a row a frame fast, otherwise a
pixel on the odd frames, which it asks the timing service through
`tm_odd_frame`, new, only where the roll is slow, as the original reads it
(the SNES backend makes the original's 16-bit read of `$213E`-`$213F` and
keeps the word for the scaffolding that leaves it in A, the SNES-only
`tm_snes_odd_frame_read`). The upload sends the row into the low bytes of its map row and the
scroll. The script's test sends it on to The End once the credits are over;
The End's routine starts a fade of the picture in from white
(`palette_start_fade_to_white`, core/palette.c).

**The registers, settled first.** Every call-out's registers were settled
before the logic was written. The setup's: the sound send reads nothing, and
what it leaves in A and the carry is what the stars' scatter mixes in, so
nothing touches them between the two; the unpacker takes its resource; the
first script step reads Y where the per-line effects' descriptors end and the
overflow clear, on the program's bank with the pushed word's other byte under
it, and comes back on its channel's page (the setup puts the scene's back);
the song's driver call takes A's high byte as the step left it. Liveness has
A's high byte and Y live after a frame task: the list's loop loads the next
slot's task over A, but past its last slot the frame's work returns to the
NMI with it, whose song send enters the sound driver, and the driver saves A
(the NMI's own restore then overwrites it). An entry's set can't rest on
evidence, so the roll stops at its scroll rather than scroll itself: the
line's Y and data bank are left there as scaffolding, and the scroll's own
entry leaves in A the word it read, shifted, where the roll is slow, and the
scroll where it is fast. Which way the roll ended its wrapper works out from
the state, the credits counted over or the text moved on. The
overflow after both is dead (liveness), as is everything the vblank tasks and
the script's routines leave but Y and the overflow, which they don't touch.

**What shadow mode and fuzzing found.** Over the 26 movies every call matches,
none interrupted and none refused: in `endings-telepod-lavos` the setup 1,
the roll 240, its scroll 239, the upload 240, the high bits' clear 3027, the
script's test 242 and The End's fade 1; in `endings-beyond-time` the slow roll
3281, its scroll 3280, the upload 3281, the clear 784; the interpreter 79996
over the library, the NMI 61618, the frame's work 61649. Shadow mode found the
clear's 16-bit A at its return and the direct page the first step leaves at
the song's driver call; the don't-care check found that the scroll reads the
data bank (its stop declares its own set). Fuzzed seed 1 from a state in the
telepod ending's credits (`tools/fuzz_routines.toml`, `staff_roll`), the
program kept: the roll 20000 (the text at one of the credits' lines, the last
the end; the speed fast or slow), the scroll 20000, the upload 20000, the
clear 20000, the test 20000, the fade 20000, the setup 300 (the driver's echo
looped back), 0 mismatched; fuzzing found the scroll's overflow, dead after a
frame task, declared. `menu_object_jolt` 20000 from the `objects` state, 0
mismatched. Against main's ports (the same seed): the interpreter 0 of 17459
and 0 of 17466 on its two channels (0 of 14061 and 0 of 14049 before, where
the call-out to the loop left more states uncompared), the frame's work, the
NMI, the race's setup, the resource copies and the palette's clear unchanged.

**Timing.** The new rows and the NMI's (whose song send no longer reads the
song playing) were calibrated over the library. The roll carries a timing
exception (`tools/timing_exceptions.txt`, vblank-tail): its time follows the
line it writes, every 16th frame, writing only RAM.

## Phase 3, the scene program: the shared endings (`$7E:8538`-`$7E:8E94`, `$7E:8983`)

The fourth chunk of bank `$7E`, after the staff roll ("Phase 3, the scene program: the staff roll", above, for the one before).

**How it was chosen.** PLAN.md's next: the endings that share `$7E:8538`.
Ranked by `tools/fallbacks.py --by routine` over the Beyond Time ending, the
group was the setup (408 bytes), the main loop's scaling `$7E:8930` (refused
by the idle entry, 121,332 instructions, 1.5 million passes), the picture's
placing `$7E:8850` (182,861), its upload `$7E:89C0` (65,439) and frame
`$7E:891B` (38,203), and the scripts' scene routines and vblank tasks
between them; about 1.9 KiB. The helpers from `$7E:7FC4` and the routines
at `$7E:8A01`-`$7E:8B51` no movie reaches and are left for a movie or a
chunk of their own; `$7E:8EA8` and `$7E:9E7B` on were the staff roll's.

**What it is.** `scene/endings.c`. The setup (`scene_ending_set_up`) loads
the captions' letters (2-bit tiles widened to 4, `$7E:66B0`, ported inline),
the regrouping code for the zooming picture, the Mode 7 ground (its pixels,
its name table in two halves, each row followed by 64 empty tiles,
`$7E:37CA`, and blanked past it), mode 10's picture laid out from tile numbers
(`$7E:860C`) with its vblank task added, the ground's line tables and the
scene's colors, and picks the two scripts by mode (modes 10 and 11 start the
captions' text). The zooming picture is a module: two shapes (32 rows as
eight sprites, 64 as sixteen), the main loop's scaling (`$7E:8930`) handing
the animation's frame to the scaler at the zoom channel 1 holds unless the last
picture waits to be sent, its gathering after the scaler returns (`$7E:8983`,
a new entry: the list cut to the sprites the picture covers at its zoom), its
placing round channel 1's point with the window table round it (`$7E:8850`,
a frame and a vblank task), its frame (`$7E:891B`) and its upload
(`$7E:89C0`). The scripts' scenes: a new picture (`$7E:87CC`) or a picture
over a new ground (`$7E:8790`), its frames mirrored into place as the race's
are (`scene_mirror_rows`); the stars (`$7E:8BAF`); a Mode 7 picture
(`$7E:8C14`, through the title's name-table loader, now a C call); the
credits made ready (`$7E:8CD0`: a 4x4 pattern of tiles across a 32x32 map,
mode 15 instead going on with its script) and shown (`$7E:8D56`, the staff
roll's blanking with its own site for the in-front fill); and the last
scene's colors and window (`$7E:8DF3`, `$7E:8E44`), with BG2's scroll in the
credits (`$7E:8E88`). Shared with the race and the staff roll and made C
calls: the resource copy `$7E:36AA` and the palette's `$7E:38AF` (screen.c),
the plane's halves `$7E:8752` and `$7E:37A6`, the mirroring `$7E:3ED3`, the
regrouping `$7E:B574` and the letters' colors `$7E:8CB2`. `attract_frame_pass`
takes the passes that name the endings' scaling as it took the race's.
Left at the edge, as call-outs: the unpacker and a script's first step as in
the race, the stars' scatter and the captions' first timing (`$7E:5777`,
`$7E:9BB8`, the program's random numbers, TODO.md), and the Mode 7 picture's
tiles' expansion (`$7E:5E3C` through `$7E:5EF8`, the call-out's set resting on
a `--poison-dc` run since the loop's call is patched at run time).

**The registers.** Settled from liveness before the logic: the scripts'
routines leave Y and V, read after the call command; each leaves Y where the
original's last block move or copy left it (named in the wrapper) and V as
the last call-out did, or clear after the mirroring's last step. The frame
and vblank tasks leave A's high byte and Y as the sprite lists' code does
(scaffolding), the scaling the carry as it came and the gathering the carry
of its last compare (the picture under half its size). The script routines
keep the calling page and a byte of the bank on the stack, as the original
does, under their call-outs; they run with 8-bit A between.

**What shadow mode and fuzzing found.** Over the 35 movies every call matches; only `endings-beyond-time` reaches
the group, in modes 11 and 14: the setup 2, the main loop's scaling 1,547,154
(901 interrupted), its gathering 3,025 (1,020 interrupted), the placing 5,375,
the frame 3,473, the upload 6,698, BG2's scroll 3,211, the picture's loads 1
and 2 (one interrupted), the stars 1 and the Mode 7 picture 1 (both
interrupted), the credits' preparation, their screen, the last colors and the
last screen 1 each, and `attract_frame_pass` 1,547,826 compared. Shadow mode found the
script routines' 8-bit A at their call-outs, the rows' counts the plane's
padding and doubling leave on their page, the data bank the darkening's block
move leaves before the first script step (and the one the captions' timing
leaves at the setup's return), and the task lists the stops clear (the
list at `$0920`, not the frame's copy). Fuzzed seed 1 from a state in the ending (`tools/fuzz_routines.toml`,
`ending`), the program kept: the setup in all four modes 300 (202 compared,
98 refused by the guard where the random vblank list had no room for mode
10's task), the scripts' scenes 300 each, the frame and vblank tasks, the
scaling, the gathering and the endings' pass 20,000 each; 0 mismatched. The
expansion's call-out, `--poison-dc`, 300 states and 1,200 call-outs
scrambled, 0 mismatched. Fuzzing found the
sprites' Y offset's high byte the placing clears (the movie's is 0 already)
and the Y mode 15's credits leave (past the task list the stop cleared). Only
fuzzing reaches mode 10 (its mosaic and captions) and mode 15 (no credits):
the movie runs modes 11 and 14.

**Numbers.** Over the reference play the code run in place of hand ports
(the baseline's lines summed) fell from 29.8 KiB and 140.5 million
instructions to 27.7 KiB and 139.8 million; the scene program's from 8.2 KiB
to 6.2 KiB and 61.4 to 60.8 million (its native share from about 56% to about
67%). One line is new: `$7E:3023`, the main loop's test after the passes the
endings' pass doesn't take (8 bytes, 29,631 instructions), filed under the
pass's port where the boot's call held them before (15,785 passes then,
10,448 now). Calibrated: the new rows (`--drifted`, and the setups by hand
from the measure); the setup and the two picture loaders carry worst-error
timing exceptions, as the race's setup does.

## Phase 3, field and events: the screen set up, the map unpacked, the objects started

The seventeenth chunk of bank `$C0`, picked by native coverage rather than by
bytes left (PLAN.md, "The order now"): the field's largest handbacks over the
reference play were the routines its ports still called out to at a map's
start and in its frame -- the screen's setup and fade in, the map's unpacking,
the objects' setup, the dialog window's step and its looks.

**What it is.** Six concepts, each a module:

- `field/screen_setup.c`: a map's screen put together from nothing
  (`$C0:286C`, with `$C0:0A50` the map's drawing and `$C0:0B28` the line
  effects readied): the switches pressed again, the chests marked, the video
  mode and screens, the screen placed around the party
  (`$C0:74A6`/`$7506`/`$7553`), each layer drawn whole and sent, the columns
  right of the view; the objects let go and those in view drawn at once
  (`$C0:B0E6`); the dialog window's looks; then the handlers on at the next
  vblank, through the timing service's new `tm_wait_frame_then_enable`. And
  the fade in (`$C0:2848`, and `$C0:2824` after a menu): a step of brightness
  a frame, the leader walking on the spot.
- `field/map.c`: the map's unpacking (`$C0:A33B`): the packed map
  decompressed, its header taken in (`field_map_header`: sizes, flags, BG2's
  half pace, the screens, colour math, the layers' drift, and the repeat
  masks and edges, `$C0:7399`), the tile maps unpacked as C where they fit,
  and the exits marked (`$C0:A66B`). The chests' marking (`$C0:A712`) is
  `event/chests.c`'s, with `field_chests` split into the game's list and the
  location's run.
- `field/objects_start.c`: the objects at a map's start (`$C0:56A6`):
  whatever the last map's objects held let go, each object as its script
  first finds it (`$C0:5709`: at its opening, the lowest level, off the map),
  the slots past them disabled, the scripts' variables and saved places
  cleared; then the openings (`$C0:58DE`, a port already), the map's own
  script (`$C0:595C`, still a call-out) and the party's looks
  (`party_wear_it_shows`, the menus' service `$0B`, timed as `$C2:8004`).
- `field/dialog_window.c`: the dialog window's step (`$C0:1F87`) as a state
  machine (`DialogState`: the text cleared a row a frame, the window opened,
  the text set a glyph a frame, read, the next page cleared and set, the
  window closed), a choice's buttons (`$C0:1ADF`), the text's row sent
  (`$C0:6ECB`), the choice pointer (`$C0:F05E`, the old "window corners",
  which are the hand pointing at a choice's line), and the window's looks:
  its frame and text area tile maps (`$C0:29F7`, `$2B78`), their transfers
  and the window design's (`$C0:6E5C`-`$6F5E`), its colours (`$C0:70E9`).
  The dialog's opening and closing are operations in its header
  (`dialog_begin`, `dialog_shut`, ...) that the events, the location, the
  battle's start and the views call.
- The objects coming into view take their palette slot (`$C0:7170`) and sprite
  block (`$C0:6F9A`/`$6FBA`, given up `$C0:7056`) as C, and the sprite
  lists are built afresh as a map starts (`$C0:B192`).
- The vblank's screen jobs after a battle and the map's transfers
  (`$C0:7F58`-`$7F77`, `$C0:EC77`) are the video service's calls.

**Third trap, settled first.** The right columns' bringing in (`$C0:87F1`)
ends with what the edges' working out leaves (`field_edges_leave`,
field/leaves.c: the last layer's right column's start and row in X and Y,
and the carry and overflow of its VRAM address). Nothing in the game reads
them, but the requests' stop at the frame wait (`$C0:EC60`) can't declare
them: the battle's start waits there too and goes on into the battle bank's
entry, which liveness keeps everything live into. The wait for the vblank
with the handlers off (`$C0:0B38`) was no routine liveness knew; an
`[[entry]]` hint names it, with a new field, `tail_of = "C0:0B28"`, that
has liveness take its returns as `$C0:0B28`'s (DISASSEMBLY.md), so the
setup's call to it and the A `field_turn_the_handlers_on` leaves are
declared dead rather than reproduced. The objects' start leaves Y and the overflow as the
defaults' writes do for the openings; and the party's start after the
openings (`$C0:9DC3`, event/bridge.c) now leaves X as the original does, the
second member's link word, which the map's own script can carry unchanged to
the menus' service after it (`endings-bucket-lavos` found it, once the
objects' setup compared X there).

**What it took in.** Two refusals in real play went with it: the field put
aside for a menu, and a location change, with no leader on the map (a save's
load, the attract demo's scenes) read the leader's place from the first slot
past the tables as the original does (`field_leaders_place`, the accessors'
`unchecked` notes saying where). A script's menu now runs the fade in after it
as C rather than stopping at it.

**Numbers.** `burndown.py` for `$C0`: 52.2 KiB ported and 12% left in 349
entries before, 57.5 KiB and 3% in 384 after. Over the reference play the
code run in place of hand ports fell from 38.6 KiB and 199.0 million
instructions to 33.1 KiB and 187.3 million; the field engine's share from 9.5
KiB and 16.0 million to 1.2 KiB and 4.3 million (native coverage from 86% to
about 98%), and the refusals in real play from 23 entries to 21. The one new
line in the baseline, `$C0:0B38`, is the original's wait for the vblank the
setup still runs on the SNES backend (630 instructions), which `$C0:0B28`'s
fallback held before.

**What fuzzing found.** Every new entry has a `tools/fuzz_routines.toml`
row (seed 1, 0 mismatched; the counts in `docs/ports.tsv`). It found four
things no movie reaches: a dialog text row past 127 carries its doubling's top
bit into the VRAM address, and a map number past `$7FFF` its doubling's carry
into the packed maps' index, as the original's additions take them; a party
place's palette slot held by an object of other colours makes the original
give up with its accumulator left 16-bit, which its callers then run with, so
the guards turn that state away; and the objects in view drew their frames'
parts with the builder's core rather than its entry, which left the overflow
the sprite table reads unset. The fades (`$C0:2848`, `$2824`) are checked by
shadow mode only: the setup they call first waits for the vblank, which no
random state reaches.

**The timing gate.** The new rows were calibrated over the library, with the
rows of the callers whose work moved (`field_nmi`, `field_after_battle`,
`field_walk_the_leader`, `event_run_init_scripts`); six entries have
exceptions (`tools/timing_exceptions.txt`): the setup, the fades,
the objects drawn at once and the map's unpacking run with the screen blanked,
their parts attributed, and what is left of them one scalar can't fit; the
dialog window's step differs in its own work by its state.

**What is left at the edges.** The map's own script (`$C0:595C`) and the
script's unpacking (`$C0:56D4`) are call-outs; the switch's sound
(`$C0:1B90`) is the largest run left in `$C0` (`burndown.py --uncovered
C0`); the line effects' builders in `$FD` stay their own ports, run through
the machine.

## Phase 4a, timing: the frame's budgets and the menus' tasks (`$C0:59D9`-`$C0:5A92`, `$C0:CA76`-`$C0:CAD8`, `$C2:81B9`-`$C2:8247`)

The first chunk of the timing service's stream (PLATFORM.md, "Timing without
a beam, as built", has the service, the host backend and the runtime's part).

**How it was chosen.** By what real play ran as the original
(`tools/fallbacks.py --by routine`): the field's three budgets were 67% of
every instruction the reference play and the routes ran in place of ports
(`$C0:59D9` 819 million, `$C0:5A46` 446 million, `$C0:CA76` 123 million), and
the menus' task switch `$C2:821E` came next among the waits. The battle's
`$CD:107D`, `$CD:1061`, `$CD:1052` and its loads through `$CD:0015`,
`$CD:002A` and `$CD:0018` turned out to be its frame work's loads and fills,
not waits on the display, and stay with the battle's frame work.

**What it is.** `field/script_turns.c` (the objects' turns, counted down and
queued, and run while the frame has time), the frame queue's loop in
`field/frames.c` with its queue's operations in `field/animation.c`, and
`menu/tasks.c` (the menus' scheduler). The turn countdown at `$1001` was
"not established" in the map: it is the frames until the object's script's
next turn, restarted from the object's move length, which is why a script's
turn comes as its walk's steps run out (`field_object_turn_countdown`). The
per-object work stays a call-out to its port, so the beam is read after the
turn's time has passed; `native_await_latch` makes the read exact in shadow
mode.

**What shadow mode found.** The frame queue's first run mismatched on 20% of
calls: the turn's carry, the builder's answer, was declared don't-care at the
call, so shadow mode gave the port its own carry back wherever the builder's
equalled the original's before the call. Then 2 of 11,498 calls disagreed on
the beam across a line boundary, which the latch wait removed. The menus'
scheduler first worked a task's stack top out with the carry its loop leaves;
the original's three shifts before the add always clear it.

**Fuzzing.** Seed 1 (`tools/fuzz_routines.toml`): the turns' countdown 0 of
20,000; the turns 0 of 348 compared and the frame queue 0 of 424 (the guards
take only a queue whose links run from its head to its tail, which random
links rarely make); the menus' wait, wait for frames and end 0 of 20,000 each,
the frame's turns 0 of 20,000, the first wait 0 of 20,000. The menus' guards
take only waiting tasks whose stacks lie in low RAM (past it the original
takes the registers it resumes with from open reads) and starting tasks whose
start is one of the game's fifteen, at each of which liveness finds every
register dead but A's high byte, so a start declares the rest rather than
building them.

**Timing.** `field_queue_due_turns` and, through it, `field_frame_work`
exceed the vblank tail (3.6% of their vblank calls off by a line), their time
following which objects are due; both are exceptions with that reason.

## Phase 3, field graphics: the time warp (`$FD:DA00`-`$FD:F28F`)

Picked by the fallback measure: over the reference play, the largest code
still run as the original in bank `$FD` was one mode, the tunnel the party
falls through between eras -- its frame wait `$FD:EAEB` (18.7 million
instructions), the light's drawing `$FD:ECD5` (3.9 M), its start `$FD:DB97`
(2.6 M), the colours' pulse `$FD:F123` (2.0 M) and the rest of its helpers:
31.6 M of the bank's 31.7 M. Three movies reach it: `events-end-of-time-gate`
(the shortest kind, a gate), `endings-telepod-lavos` (the short kind, twice)
and `endings-bucket-lavos` (the long, flickering kind).

**What it is.** A mode of its own, so a subsystem of its own, `game/warp/`
(`re/ram.toml`'s and the tools' subsystem lists take `warp`): event `$FF`
with a scene of `$80`-`$83` jumps to it from the field (`$C0:3FD6`), it runs
on the palette fades' page with its own vblank handler, and it ends at the
field's entry.

- `warp.c`: the mode. The start (`$FD:DB97`): everything stopped, the
  handlers installed, the story flags kept aside (the plane's map is built
  over `$7F:0000`), the picture drawn, the circle's tables built and the line
  effects started, then the screen on through `tm_wait_frame_then_enable`
  (its wait `$FD:EADA` an idle entry, the turning on `$FD:EADF` a port). A
  frame: one function for the four kinds, entered after each kind's wait
  (`$FD:DA06`, `$DA65`, `$DAC4`, `$DB27`), a row of `kinds` each (how long,
  whether it flickers): the plane moved, the light's palette turned, the
  phase (opening, running, closing, gone) choosing the circle and where the
  tunnel shows, the circle built, the colours pulsed. The waits (`$FD:EAEB`,
  and its loop `$FD:EAED` an idle entry), the vblank handler (`$FD:DE15`:
  the palette and the sprites sent, the circle's table built last shown, the
  frame counted) and the end (`$FD:EAC8` dark, `$FD:EAB7` the story flags
  back).
- `picture.c`: the screen (one `VidScreen`), the plane's tiles (tile n all
  colour n) and its map (a gradient a sine bends), the palette, the light's
  sprites, the light drawn as a quarter turn of rays in widening bands, and the
  line effects with the plane's perspective scales.
- `circle.c`: the circle window, a quarter of it traced Minsky's way in
  256ths of a pixel, built into the table the vblank handler doesn't show.

**The services**, additive: `VID_LINE_MODE7_CENTER` (Mode 7's centre per
line, which the warp sets a half of the picture at a time),
`VID_STEP_INDIRECT_BANK_IS_SOURCE` (the vblank's two uploads write their own
bank into the indirect bank's register too), `TM_LINE_UNCHANGED` (the warp's
screen comes on with no line handler), and in the SNES backend's tables the
warp's screen (`$FD:EB13`), its five line effects and four transfers.

**What shadow mode and fuzzing found.**

- *The circle's builder.* The point that traces the circle leaves the quarter
  on a line it never gives a width to; the port first took that line for the
  last one written and left a byte past it. Shadow mode saw it twice in the
  gate's warp, and fuzzing every frame of the shortest kind from a state in
  it narrowed it to the radius of 6.
- *The end.* The original goes dark from its main loop by calls, partway
  through the picture, and the registers the line effects drive keep what the
  last line before it wrote; a port that went dark at the frame's start left
  the previous frame's. The frame stops at the end's calls, and going dark and
  putting the flags back are entries of their own.
- *The first flickering frame.* Its brightness's index takes A's high byte
  along, which every frame leaves clear but the start leaves holding the bottom
  line's scale, so the first frame's brightness comes from far past its
  pattern (`$FE:1CF5`). Kept, from the state: the pattern's place is 0 only on
  the first frame.
- *What the field reads after.* The light's drawing keeps a pixel's bit masks
  on the page (`$0525`-`$0534`), which the field's palette animations share
  and read on the next location (the effects check's run found it): they are
  plain writes, not scaffolding. So is the zero the warp keeps on its page,
  which the original's routines index from and the ports' guards ask for:
  written as scaffolding, the build without it turned the frames away, the
  original ran on a page with no zero, and the bucket's route reached the Day
  of Lavos standing in the wrong place (the native-shape check's long routes).
- Kept as the original has it: the start's two empty builds of the circle both
  land on the second table (the count is cleared, then stepped between them);
  the builder's half widths are words, so the largest radius's last fraction
  lands on the first table's first byte (an `unchecked` accessor); A's high
  byte at the start's return and each frame's stop is the original's, which
  the next frame reads and shifts out.

**Numbers.** Shadow over the library: 0 mismatches; `warp_set_up` 4 calls,
`warp_shortest_frame` 159, `warp_short_frame` 510, `warp_flickering_frame`
767, `warp_vblank` and `warp_wait_for_frame` 1,436 each, the end's and the
screen's 4 each, none interrupted; `warp_long_frame` no movie reaches.
Fuzzing, seed 1 (`tools/fuzz_routines.toml`): every entry 0 mismatched, the
frames 5,000 states each with the frame count anywhere, so every phase and
the end; the start 200, compared up to its wait. `burndown.py` for `$FD`:
11.8 KiB ported and 22% left in 87 entries before, 14.3 KiB and 5% in 99
after. The reference play's code in
place of hand ports fell from 24.1 KiB and 90.5 million instructions to 21.7
KiB and 60.3 M, field graphics' from 3.5 KiB and 31.7 M to 1.1 KiB and 1.5 M
(its native share from 74% to 92% of the 13.5 KiB the movies run); the
shipping mode from 18.9 KiB and 98.6 M to 16.5 KiB and 78.8 M. Left of the
warp: each frame's call of its wait and the end's calls (13 bytes a kind, a
JSR a frame), the idle waits' leaving passes, its empty line handler
`$FD:DE87` (never enabled) and a palette turn `$FD:DD1D` no code reaches.

**Timing.** Calibrated over the three movies: the start at 24,782, the frames
at 8,303-8,332 (the long kind, which no movie plays, at 8,320), the vblank
handler at 6,710. The original steps the circle's point a line at a time with
the multiplier and its waits, where the port works the widths out in locals,
so a frame's time followed the circle's radius: up to 104,051 clocks off at a
radius of 127, against the gate's 50,468. Each frame now charges eight of its
own accesses a step (`charge_as`), and its worst is 8,805. No exceptions.

## Phase 3, the scene program: the star field, the captions and the random numbers (`$7E:5777`-`$7E:5854`, `$7E:64CA`, `$7E:9897`-`$7E:9AC0`, `$7E:9B4F`, `$7E:3870`, `$C3:02DD`)

The fifth chunk of bank `$7E`, after the shared endings (history file, "Phase
3, the scene program: the shared endings", for the one before).

**How it was chosen.** The frame tasks `scene_run_frame_tasks` and
`scene_run_vblank_tasks` still handed to the original, ranked by
`tools/fallbacks.py --by routine` over the reference play: the stars' flight
`$7E:579B` (27.2 million instructions, all of it the telepod ending's), the
captions' drawing `$7E:64CA` (11.9 million), the Beyond Time ending's star
field `$7E:999C` (3.5 million), the sprites' last group `$7E:3870` (2.9
million, every movie: the title runs it), the window shapes' edges `$C3:1025`
under the swept window `$7E:9A0C` (2.7 million) and the captions' timing
`$7E:9B4F`. The stars waited on TODO.md's "randomness and parity": the stir
`$C3:02DD` mixes the beam position into the program's seed, and the routines
that hand it their registers stayed the original's with it. So the stir came
first.

**The random numbers, and how they are checked.** `scene/random.c`. A draw
(`scene_draw_random`) stirs a seed with where the beam is, the frame
counter, the scene's call cells, a salt and the draw before it: a byte or a
word as the original's 8- and 16-bit callers draw, and one bit more, the bit
its last sum spilled (`SceneDraw`), which the next draw starts from, turned
over by a bit. The stir is a ones'-complement sum -- what one step spills
over the top comes back in at the bottom of the next, and a value is taken
away by adding its complement -- which is the original's mix exactly, so
given the same noise it draws the same numbers. `tm_entropy` changed to give
the four values it stirs (`TmEntropy`, bytes or words): the SNES backend
reads them as `$C3:02DD` does -- `$2137`, whose read latches the counters and
gives no counter but the bus's last byte (`$21`, and for a word a byte of
sprite memory), then `$213C` and `$213D` twice -- and in shadow mode waits
for the original's latch (`native_await_latch`), so the port reads the beam
the original latched; a host draws all four from its seeded generator. The
call is shaped by this one stir, its only caller. **The choice: exact
reproduction, compared by shadow mode**, not `--native on` alone as TODO.md
suggested: every star placed, every drift and the seed itself are compared
on every call, where the shipping mode would only show the stars look
random. What it cost: a star's byte sums each take in the bit the sum
before spilled (`sum_after`, nine-bit sums), as the original's do -- for a
negative drift, speed or offset that bit is nearly always set -- and the
salt is what the original has at hand (the task's slot; 4 after the
sprites' last group). The runtime needed one change: an entry the original
reaches while a port waits for its latch is not compared on its own
(`runtime/port/native.c`), since only the innermost comparison watches the
latches and the nested one hid them (the scatter, registered, inside the
setups that run it inline); such calls show on the entry's line as `in a
latch wait N (not compared)`, and their guards don't run. Over the library
that is the scatter's 2 calls and, inside the captioned field's flight, the
flight's and the last group's 557 each. The stir's other caller, the noise
effect `$C3:01E4`, is still the original's.

**What it is.** `scene/stars.c`: the field of 128 stars the staff roll and
the shared endings fly through, each a place across and down and a depth
(`scene_stars`, `scene_star_depths`, new in the map over the tilemap
buffer). The scatter (`$7E:5777`, now a C call from both setups, and an
entry), the flight (`$7E:579B`: each star moved by the field's speed, one
that leaves coming back at a new random place, drifted, and drawn as a
sprite in one of sixteen sizes at its place over its distance, the nearer
half in front of the layers unless the staff roll's patch puts all behind),
the Beyond Time ending's field after its captions (`$7E:999C`, which first
puts the rest of the captions' last group of sprites off the screen) and
The End's drift (`$7E:9897`, `$7E:98AC`, `$7E:98E3`: across, down and the
speed each hold a random time, then take a new value; the field never
stands still). `scene/captions.c`: the captions' text drawn as sprites
(`$7E:64CA`: letters 16x16 or two 8x8, spaces, lines, a new place, which
the program keeps in two operands of its own code, new in the map) and
their turns (`$7E:9B4F`: a caption waits for the screen to come up, stays,
fades out over its last 32 frames, and the gap before the next fades it in,
through a new `palette_start_fade_to_black`). In `scene/sprites.c`, the
sprites' last group hidden (`$7E:3870`), a sprite at the cursor and one at
an entry. In `scene/endings.c`, the last scene's swept window (`$7E:9A0C`):
the four-sided window drawn by the field's window shapes (whose entry
`$C3:0EFA` is now registered as well as `$C3:0011`, its guard taking the
endings' table in bank `$7E` past low RAM), the rider sprite list through a
call-out to `$7E:3A97`, the corners moved, and BG2 drifting a line every
other frame by `tm_odd_frame` (`$7E:9AB3`, the parity TODO.md listed).

**The registers.** Settled from liveness first: a frame task leaves A's
high byte and Y as the original (the NMI's song send may read them), worked
out in the wrappers from the state; the scatter leaves the overflow of its
last draw, which the scripts' interpreter may read
(`tools/v_readers.py`); `$C3:0EFA`'s Y is read after the swept window, so
the window shapes' entry leaves where the original's fill leaves it;
`$7E:3870` leaves X at 4, which the flight after it stirs in.

**What shadow mode and fuzzing found.** Over the 35 movies every call matches and none is refused: in
`endings-telepod-lavos` the flight 3027, The End's drift 2468 and its start
1, the staff roll's setup 1; in `endings-beyond-time` the flight 227, the
field after the captions 784 (1 interrupted), the captions' turns 8605 and
their drawing 6711, the swept window 785; the sprites' last group 79,861
over every movie; the window shapes' entry at its start 3457 (the endings'
and the Black Omen's searchlights, which reach it too). The scatter as an
entry is never called: both setups run it inline. Shadow mode found the
nested comparison hiding the latches, the carry `$7E:999C` hands the flight
(always set) and its salt (4 where it made room), the farthest star whose
distance wraps to 0 (the divider's `$FFFF`; a division by zero in the first
port), and the carry and X `$7E:3870` leaves. Fuzzed seed 1 (`tools/fuzz_routines.toml`, the `staff_roll` and `ending`
states, the program kept), 0 mismatched: the flight, the field after the
captions, the drift's start and frames, the last group 20000 each, the
flight's loose variant 43 compared (19957 turned away: a first entry off a
star's place, or another branch), the scatter 2000, the captions' drawing
16703 and turns 18363 compared (the rest turned away: text whose letters
don't fit the table), the swept window 19595 and the window shapes at their
start 19594 (the rest turned away: a corner on the last line), the two
setups 300 each. Fuzzing found the data bank the captions' drawing goes
back with (its pushed word's other byte, 0), and the original's fill of a
window whose corner is on the last line, which runs round bank `$7E` into
low RAM: the guards turn those away. Only fuzzing reaches the scatter alone,
a caption's text from anywhere in it, and the sweep's every corner.

**Numbers.** Over the reference play the code run in place of hand ports
fell from 24.8 KiB and 139.3 million instructions to 24.1 KiB and 90.5
million; the scene program's from 7.2 KiB and 60.3 million to 4.6 KiB and
14.2 million (its native share from about 67% to about 75%), the scene
engine's from 1.0 KiB and 5.9 million to 0.6 KiB and 2.6 million. Calibrated:
the new rows, and the two setups that run the scatter inline, timed as the
scatter (whose rate is its measured original/native ratio, since no movie
calls it alone), and the field's entry, whose wrapper now works out the Y
it leaves. The window shapes' entry at its start carries a worst-error
exception, as the field's entry does: the original steps an edge's run a
pixel at a time and the port a line at a time, so the error follows the
shapes' widths (54% on average, at most 80,345 clocks). In the shipping
mode, the movies and the routes run 21.5 KiB and 190 million instructions
of the original's code in place of hand ports, from 22.1 KiB and 342
million.

## Phase 4a, the native frame: the field's first start with nothing interpreted (`$C0:7084`, `$C0:7F7E`, `$C0:56D4`-`$C0:5D6D`, `$C0:595C`, `$C0:00BA`-`$C0:00BE`, `$C0:0CC0`, `$C0:3FD6`, `$C0:2643`, `$FD:DE98`-`$FD:E021`, `$FD:FFEE`-`$FD:FFFF`)

The first chunk of the native frame's stream (PLAN.md, Phase 4a, "The native
frame"), after the field's stream's time warp (history file, "Phase 3, field
graphics: the time warp").

**How it was chosen.** By the strict player: `ct_play --generated off
--native on --interpreted stop`, which runs only hand ports and stops at the
first instruction none covers, stopped at frame 2 on `$C0:7084`, inside the
field's first location load, and `--interpreted log` over 3,000 frames from
power-on listed 1,164 addresses still interpreted (the title's program in
`$7E` 758, the scene engine's curtain in `$C3` 150, the field 158, bank `$FD`
54, the menus' attract scene in `$C6` and `$C2` 44). The chunk is everything
of the field and `$FD` that the run met before the title: the rest of a
location's load, the main loop's turn, and the field's ends.

**What it is.**
- A location's load (`field/location.c`) is C from end to end: its palette
  (`$C0:7084`, the record's palette into background palettes 1-7, `location_palette_colour`),
  the layers' page cleared (`$C0:7F7E`), the event script unpacked
  (`$C0:56D4`), with the original's check of it kept as a stop at its error
  screen (a script that doesn't fit, or a text file, which none is) and the
  built-in commands (`$C0:5D37`) copied after it (`built_in_command_byte`),
  then the location's animated tiles and palette animations as C calls.
- The animated tiles' loader (`$FD:DE98`, `core/tile_animation.c`): a set of
  twelve slots from the tile set's number, each unused or two, four or eight
  distinct frames played round the slot's eight, the tables written in the
  order of the slots in use, as the original's work RAM port leaves them.
  Its steps went to the work RAM port with 16-bit stores just below it, so
  each step's low byte also landed on the sound CPU's port 3 (PLATFORM.md,
  "P1 as built"); the last of those is scaffolding (`hw_port_leave_spill`),
  and this was the last such loader in `$FD` (TODO.md's P1 leftovers).
- The map's own script (`$C0:595C`, `event/engine.c`,
  `event_run_map_script_commands`): object 0's second function to its first
  zero byte, the engine's third runner beside the turns and the openings, on
  the same rows and bridges (`event/bridge.c`, the dispatch `IN_THE_MAP_SCRIPT`).
  Its three callers (the objects' setup, the return from a menu and from a
  battle) run it as C.
- The main loop's wait (`$C0:00BA`) and its turn (`$C0:00BD`): the wait an
  entry of its own whose start runs as C and stops where it waits, the turn
  a stop at the loop's top, so the pass still stops at the wait rather than
  holding a native routine across the frame.
- The field's ends: the restart on a fresh stack for a new location
  (`$C0:0CC0`), the end for a scene an event command asked for (`$C0:3FD6`,
  the scene engine below `$80`, bank `$FD`'s four from `$80`; its number now
  a named cell, `field_scene_to_start`) and the field's close into scene
  `$85` (`$C0:2643`).
- Bank `$FD`'s jumps to its ports (`$FD:FFEE`-`$FD:FFFD`) are entries of the
  ports they jump to, and the ports that called through them (the field's
  frame, the event screen's fade, the battle's return, the line handler's
  uploads, the battle's line and vblank handlers) run them as C.

**What shadow mode found.** The wait's first form ran the frame wait as C at
the end of the main loop's pass, which made the pass's stop the wait's and
left X undeclared where the loop never reads it: 30 of 34 passes of
`title-start` mismatched on X. As an entry of its own the wait declares what
the loop doesn't read. Over the 35 movies, 0 mismatching: the main loop's
wait 204,326 calls (28 interrupted) and its turn 204,311; the palettes'
animation through its jump 718,955 (16 interrupted) and the tiles' uploads
through theirs 325,215; the loads' palette, layers and tiles 352, 352 and
331; the script 383 (21 interrupted); the map's script 383 (29 abandoned,
where a load the original reaches inside a load already running); the
restart 173, the scenes' start 61. The field's close no movie reaches.

**Fuzzing** (seed 1, 20,000 states unless given, compared and mismatched):
the palette, the layers' clear, the wait, the turn, the restart, the scenes'
start and the field's close 0 of 20,000 each (the close's first form went by
the scene's number and mismatched all 20,000: the close always starts the
scene engine); the animated tiles' loader and its jump 0 of 20,000, the
uploads' jump 0 of 19,936 (64 refused by the guard), the palettes' jumps 0 of
20,000 and 0 of 18,386 (1,614 refused); the script 0 of 961 of 1,000 (a
location record every sixteenth, each a whole stream; 39 past the budget);
the location's load 0 of 839 of 1,000 and the main loop's pass 0 of 2,000.
The map's script compares few states: from Crono's room's own script 0 of
348 of 2,000, from anywhere in the script a state left behind 0 of 93, and
near the end of the buffer and past it 0 of 1,362; the rest are skipped,
commands run through the table that go on past the budget. **What only
fuzzing checks**: the field's close, the script's error screen, the tile sets
whose unused slots come before used ones (no set has one), and the map's
script past the buffer's end, which the engine hands to the original (its
first form read the script accessor out of range there).

**Measured.** The strict run now goes from power-on through the location's
load, its objects and its own script, the main loop's frames and the field's
end into the title's scene, to frame 55, and stops at `$7E:3000`, the scene
program's first instruction. Over 3,000 frames `--interpreted log` lists 952
addresses (from 1,164), none in `$C0` or `$FD`. The reference play's fallbacks went from 36.25 M
instructions to 34.46 M: the location's palette, script and animated tiles'
loads and bank `$FD`'s jumps went, and the frame wait's loop, 2.4 M, is
counted as the main loop's wait's handback now rather than the wait's start's.

**Timing.** Calibrated: the new entries, and the callers whose stretches now
take the animations' work inline (the line handlers, the location's load,
the objects' setup, the battle's return, two event commands). Two are
exceptions: the main loop's turn and the field's restart make no bus access
of their own (they set the stack and the PC), so their estimates are 0
against the 18 and 54 clocks the original's branch and stack reset take.

## Phase 4a, the native frame: the title (`$7E:3000`-`$7E:303B`, `$7E:30F6`, `$7E:3177`-`$7E:31AF`, `$7E:3691`, `$7E:3749`-`$7E:375E`, `$7E:37FA`-`$7E:380D`, `$7E:5978`-`$7E:5AAA`, `$7E:5B99`-`$7E:5E53`, `$7E:600E`)

The second chunk of the native frame's stream, after the field's first start
(history file, "Phase 4a, the native frame: the field's first start with
nothing interpreted").

**How it was chosen.** By the strict player again: it stopped at frame 55 on
`$7E:3000`, the scene program's first instruction, and each stop after it was
ported with what it needs, until the run reached the title's scripts: the
program's start, the title's setup, the frame's task lists and the title's
tasks. `--interpreted log` over 3,000 frames listed 952 addresses before, 758
of them the title's program.

**What it is.**
- The program's start and end (`scene/program.c`): the state a scene starts
  with, its work RAM cleared, the sound put back to normal (`scene/sound.c`,
  `$7E:3177`: the five driver commands, each sent and waited for, with the
  last one's wait the original's), every sprite hidden and the first screen,
  then the scene's setup from the program's table of sixteen by its mode,
  each a call on the machine the registry answers (the title's, the race's,
  the endings' and the staff roll's are C), and the first frame: the palette
  sent, a frame waited out, the frame's work once and the interrupts on. The
  waits the original makes in the middle cut it into entries: the sound's last
  command returns to `$7E:30F6`, a setup that waits to `$7E:3003` or
  `$7E:300D`. At the end, the program hands the game back to the field's
  start on a fresh stack (`$7E:303B`).
- The program's unpacker by resource (`scene/boot.c`, `$7E:3691`) and a
  channel's first script step from a setup (`scene/script.c`, `$7E:3749`,
  `$7E:3754`).
- The title's setup whole (`scene/title.c`, `$7E:5978`): its five graphics
  unpacked and sent, the plane's pixels and name table, BG1's fill, the
  palette, channel 0's first script, the title's starting state (named cells
  now: the name's five sliding pieces, the pendulum's phase and swing, the
  wait and the button's two flags), the pendulum's screen and the song, up to
  the original's wait for it to start.
- The frame's task lists run the ported tasks from a C table
  (`scene/frame.c`) and take a task off its list, or hand its slot on, where
  it leaves through the list's exits (`$7E:39A7`, `$7E:39B1`), which before
  were the original's code run through the stack: the title's pendulum
  (`$7E:5BCF`), its logo's whole sweep along its curve (`$7E:5CBA`, with the
  shared sine and multiply), the clock, its hands, the copyright and the name
  sliding in (`$7E:5D01`, `$7E:5D20`, `$7E:5E1E`, `$7E:5D71`), the bottom
  right picture's wait (`$7E:5D32`), the watch for a button that ends the
  title (`$7E:5B99`), the ripple's first column (`$7E:600E`), and the frame's
  own palette animation and sprites' finish (`$7E:37FA`, `$7E:380B`).
- The sine and cosine the race's camera had are the program's, in
  `scene/math.c`, beside a 32-bit divide that returns its quotient.

**What shadow mode found.** The setups are reached through a table in RAM,
so the dispatch hands them every register as the original does; the title's
setup leaves Y where the next task reads it, and the song's call the
register page in A's high byte. The task lists' exits pull the task's frames
off the stack, so they can't be entries of their own (their comparisons ended
at a return no caller made): the list's runner handles them. A guard that
read work RAM through the machine's accessors added to the run's time and
moved a generated block's timing; the guards peek. The full library run found
the race's setup patching the program's main loop (its call of the frame's
own routine, `$7E:3024`, becomes the race's): the start's guard leaves those
two bytes out of its hash, which had turned the race's first frame away six
times. Over the 35 movies, 0 mismatching: the start and its going on 57 calls each, the setup's entry 57 (17 abandoned, inside the start's stretch), the first frame 56 (all inside the setup's stretch, so compared as its), the end 53, the sound back to normal 93 (78 inside the start's), the unpacker 280 (4 interrupted), the channels' first steps 79,681 and 23,096, the title's setup 38; the tasks: the palette animation 72,776, the sprites' finish 79,634, the logo 2,415, the pendulum 26,266, the clock 32,670, its hands 17,130, the copyright 8,730, the name 27,245, the wait 4,027, the watch for a button 51,117, the ripple 53,511; the lists that run them 80,684 each.

**Fuzzing** (seed 1, `tools/fuzz_routines.toml`, compared and mismatched):
0 mismatched everywhere: the start 359 of 400, the setup's entry 350 of 400, the first frame, the start's going on and the title's setup 400 of 400, the end 20,000, the sound 20,000, the unpacker 1,711 of 2,000 (the rest past the budget), the channels' first steps 13,728 and 13,729 of 20,000 (a script pointer inside the program; the rest run past the budget), and the tasks 20,000 each (the pendulum 19,992, 8 turned away by its guard; the palette animation 19,999), from a state 300 frames into `new-game` with the program kept, the sprite table's cursor at its start and the frame's first slots free where a task's guard asks for them. The start, its setup's entry and the first frame are compared up
to the first frame's wait (`--stop-at-call 7E31B0`), with the sound's port
read back; the setup's entry is guarded on A's high byte being 0, which
fuzzing found the dispatch takes into its index (the start always leaves it
0). The unpacker over numbers past the resource table's 73 entries mismatched
30 of 2,000, inside the decompressor's port (TODO.md). **What only fuzzing
checks**: the program's start where the scene sends no songs, the first frame
(every shadow call is inside the setup's stretch), the sound's commands one
by one, and the tasks' exits on states the title never makes (a full sprite
table, no free slot).

**Measured.** The strict run now goes through the program's start, the
title's setup and its frames -- the pendulum, the logo's sweep, the sprites,
the ripple -- to frame 827, and stops at `$7E:37F3`, where channel 0's script
calls one of the program's routines (the ripple's start, the fades to black
and to white). Over 3,000 frames `--interpreted log` lists 536 addresses
(from 952): the title's program 342, the curtain in `$C3` 150, the menus'
attract scene 44. The reference play's fallbacks went from 34.46 M instructions to 24.95 M: the title's tasks and its setup went, and the frame's lists no longer run the tasks' exits as the original's.

**Timing.** Calibrated: the new entries, and the task lists and tasks whose
stretches now take the title's tasks inline. One exception: the unpacker,
whose own part is a few stores (calibrated 1) and whose stretch carries the
decompressor's error on the scenes' streams, at most 333,201 clocks in loads
of 405,159 on average, with the screen blanked.

## Phase 4a, the native frame: the scene's tables as C, the title's routines and the screen's fades (`$7E:300A`, `$7E:347A`-`$7E:34FA`, `$7E:37F3`, `$7E:398A`-`$7E:39BD`, `$7E:5BBF`, `$7E:5DB4`-`$7E:5E1D`, `$C0:3FD6`, `$C2:0000`)

The third chunk of the native frame's stream, after the title (history file,
"Phase 4a, the native frame: the title").

**How it was chosen.** The title's review asked for the scene's three
dispatches through addresses to be C tables (TODO.md, "What the boot path's
review left"); the strict player then stopped at `$7E:37F3`, where channel
0's script calls one of the program's routines, and each stop after it was
ported until the run left the title for the menus' attract scene.

**What it is.**
- The task lists run their tasks from C tables of rows (`SceneTaskPort`,
  scene.h): each task by a named constant (the value the lists hold, the
  program's routine's address, the lists' own encoding), its frame as
  `TaskEnd (*)(GameState *, Platform *)` -- it goes on, is done, or hands its
  slot over -- its guard, and a leaves helper for what it leaves in A and Y,
  the scaffolding the frame's work and the original's tasks read. The list
  empties or hands over the slot itself; a task's registered entry
  (`scene_task_alone`) is a one-line wrapper for callers by address and
  fuzzing. The title's rows are in `scene/title.c`, the frame's own and the
  fades' in `scene/screen.c`.
- The scene's setups by named mode (`scene/program.c`): the race's, the
  title's, the endings' and the staff roll's as C, called directly; a mode
  without a port stops at the program's own dispatch, which picks from its
  table. The field's scene starts by named scene (`field/main_loop.c`): the
  time warp's four starts by the trip's length, not bank `$FD`'s jump table.
- The routines the title's scripts call (`$7E:37F3`), from a C table of rows
  (`SceneScriptRoutine`): the ripple's start, and two fades of sprite palettes
  in the palette animations' first slot, one of them toward a tint
  (`PaletteTint`, core/palette.h: a fade's mode is the color it moves toward).
- The screen's fades (`$7E:347A`-`$7E:34FA`): up to full brightness, down to
  dark, and down to dark at a scene's end, after the music has faded out and
  with the sound stopped (`AU_STOP_ALL`), or as it is; the ripple calming
  (`$7E:5BBF`); bank `$C2`'s jump to the menus' object mode (`$C2:0000`).

**What shadow mode found.** The setups, called without the dispatch's return
under them, made their calls of the original's code (the unpacker, a
script's first step) at a stack 2 bytes higher than the original's, and
shadow mode pairs a call-out with the original's by the stack it is made at:
the race's loader's calls paired with its pictures', and mismatched on X. So
the setups run under the dispatch's return, as the stack is at those calls,
until they make no call-outs. Run as C, each setup also leaves Y where the
first frame's vblank tasks may read it (the race's as its matrix code's writer
leaves it, past its eight strips; the endings' past their line effects'
descriptors), which as their own entries shadow mode had taken from the
original. The two fade tasks that call the sound driver (`$7E:34AB`,
`$7E:34C2`) make that call in 8-bit A, and, for the same pairing, with the
frames the list keeps under a task's return on the stack (`calls_out` in their
rows). The unported setups are called by the routine the program's own table
names (`scene_program_setups`, a cell of the program's image), where a stop at
the dispatch had left its one instruction to the original. Over the 35
movies, 0 mismatching: the lists 80,684 calls each, every task on them now compared in their stretch; the start 57 and the setup's entry 57 (17 inside the start's stretch); the fades up 525, down 240, down at a scene's end 165, the music's fade-out 36 (all inside the list's stretch, compared as its) and the darkening in silence after it 540; the ripple calming 7,726; the scripts' three routines 35 each; bank $C2's jump 47; the field's scene start 61.

**Fuzzing** (seed 1, `tools/fuzz_routines.toml`, compared and mismatched):
0 mismatched everywhere: the five fades, the ripple calming and the three script routines 0 of 20,000 each; the palette animation task 0 of 18,386 of 20,000 (1,614 turned away by the animations' guard), with the animations' slots as palette_animate's own constraints make them; the title's tasks again through their one-line entries, 0 of 20,000 each (the pendulum 19,992); the start and the setup's entry 0 of 375 of 400 each, up to the first frame's wait; bank $C2's jump 0 of 500, the field's scene start 0 of 20,000. The fades' entries alone found that a fade leaves A's high byte as it was called, the list's task, and the two palette fades that they come back with the data bank on the program's (their stack imbalance), which the leaves helpers now say. **What only fuzzing checks**: the tasks' leftovers on paths
the title doesn't take (a fade's countdown run past its pace, a brightness
past full), and the two palette fades' entries alone.

**Measured.** The strict run now goes through the title to its end -- its
scripts' routines, the ripple calming, the music fading out and the screen
going dark -- and on through the scene's end and the field's start of the
menus' attract scene, to frame 2140, and stops at `$C6:E241`, in that scene.
Over 3,000 frames `--interpreted log` lists 417 addresses (from 536): the
the attract sequence's other scenes in the program 224, the curtain in `$C3` 150, the menus' attract scene 43.
The reference play's fallbacks went from 24.95 M instructions to 25.12 M, with the title's fades, its ripple calming and its routines' calls gone: what rose is the measure seeing the title's main loop's pass that the frame's interrupt cuts each frame (the idle entry's, `$7E:3023`-`$7E:3028` and the empty routine's RTS, `$7E:3176`) and the title's wait for its song (`$7E:5AAA`), which the first frame's comparison had covered while it never finished (its setup stopped inside it); they are counted now, as the race's passes were.

**Timing.** Calibrated: the new entries, the lists (whose stretches take the tasks inline, without the call's frames), the start's first frame, and the field's scene start. The race's and the endings' setups' worst-case exceptions moved to the program's start and the setup's entry, whose stretches now run them as C (tools/timing_exceptions.txt), and the race's late screens write to the setup's entry (tools/vblank_check_exceptions.txt).

## Phase 4a, the native frame: the curtain, and the fallback measure's unfinished comparisons (`$C3:000E`, `$C3:01BD`-`$C3:02DC`, `$C6:E241`-`$C6:E2AE`, `$C2:0346`, `$C2:046E`)

The fourth chunk of the native frame's stream, after the scene's tables
("Phase 4a, the native frame: the scene's tables as C").

**How it was chosen.** The strict player stopped at frame 2140 on `$C6:E241`,
where the menus' attract scene starts its curtain, which the scene engine's
step (`$C3:01E4`) sweeps; then at `$C2:046E`, the sprite-object mode's jump
to its wait for a frame. Before them, the review's fixes: a task's
registered entry takes its row (`scene_task_alone(s, &row)`, the list the
row's), so a task with no row can't reach it; and the fallback measure's
unfinished comparisons (below).

**What it is.**
- The curtain (`scene/curtain.c`): the ragged edge that sweeps across the
  screen to open or close a scene, a byte a line. Each frame every line's
  edge moves 8 to 11 pixels, in runs of 3 to 34 lines that move alike, the
  runs taken from the game's table of random bytes from a place a stirred
  number picks (`scene_draw_random`); the edges are kept $1C0 bytes on and
  sent to the table or the half after it, a frame each; after 31 steps the
  sweep is over. Two of the original's accidents are kept: an edge stopped at
  0 leaves A's high byte at the direct page's, which every later run's length
  takes in (the rest of that frame is one run); and the adds and subtracts
  take in the carry of the line's compare with the end, which carries only
  where the lines wrap round the bank. The scene engine's jump to it
  (`$C3:000E`) is a stop there, so a comparison of the step is the step's own.
- The menus' curtain (`menu/line_effects.c`, `$C6:E241` start, `$C6:E27E`
  step): window 1's right edge line by line on slot 2 (a new line target,
  `VID_LINE_WINDOW1_RIGHT`), from the object's own table, the slot's two runs
  of 112 lines showing the half the step just sent.
- The sprite-object mode's wait for a frame from its start (`$C2:0346`) and
  its jump (`$C2:046E`).

**What shadow mode and fuzzing found.** Fuzzing found both of the curtain's
accidents, a sweep of none clearing the count, and that a table in the
second bank near its end runs the work RAM port on into low RAM (it wrote
over the menus' current object): the guard takes tables clear of low RAM's
first $2000 bytes in the first bank and short of the end in the second.
Through the jump as a port of its own, the stir's wait for the original's
latch was the jump's comparison's, not the step's, and 4% of steps compared
a beam they hadn't waited for. Over the 35 movies, 0 mismatching:
the step 2,097 calls (248 inside an outer step's latch wait, compared as its), its jump 1,535, the menus' start 8 and step 264, the wait 30,714 and its jump 29,964. **Fuzzing** (seed 1): the step 0 of 18,572 of 20,000 (1,428
turned away by the guard), the jump 0 of 20,000, the menus' start 0 of 1,750
and step 0 of 1,860 of 2,000, the wait and its jump 0 of 2,000 each.

**The fallback measure's unfinished comparisons.** A covered stretch ended
only where its comparison finished, its routine returned, or a port was
entered at or above it; a nested comparison that shadow mode set aside
(started inside another's stretch and still running at its point) never told
the measure, so its stretch stayed open and hid everything the original ran
after it until S rose above where it began. The title's first frame's never
did: its stretch, begun at S $06FF, covered the title's main loop for the rest
of every movie (seen by dumping the measure's extents in the old build), which
is why `$7E:3176` read 7,510 and then, once the title's setup ran inside the
start's comparison and the first frame's comparison finished, 45,103: the
loop's pass the frame's interrupt cuts, once a frame, counted as every other
interrupted idle pass is. The runtime now tells the measure that a set-aside
comparison is finished (`settle_nested`), and a run that ends inside a
comparison writes what it hid (`open` lines): every instruction a comparison
covers, its callees' included, is counted on the comparison's own line, and
the `open PORT` lines take part in the ratchet like the other kinds (a new
one, or one that rose, fails `--check`), so a comparison that stops finishing
can't pass for a gain. Over the library 62,881 instructions sit under comparisons still open at a movie's end (16 ports, the battle's frame work's the most), now baseline lines of their own. The reference play's count went from 25.12 M to 52.21 M with the
fix and this chunk's ports: the battle's wait loops run as call-outs' callees
(`$CD:0453` 20.7 M, `$CD:0033` 6.7 M, `$CD:0024` 1.4 M), the race setup's
arrangers and the menus' and field's code after set-aside comparisons. The
endings' small rises at boot-path-2's merge (`$7E:6BE5` +70, `$7E:9ABD` +34,
`$7E:A03D` +88) are the same mechanism the other way about: with the title's
tasks inline the frame list's comparison was set aside 25 times instead of
165, so 140 more frames' original tasks were counted as the list's call-outs'
callees rather than hidden under the frame work's stretch.

**Measured.** The strict run goes on through the title's end, the menus'
attract scene and its curtain, to frame 2663, and stops at `$7E:6B04`, the
setup of the attract sequence's next scene (mode 1). Over 3,000 frames
`--interpreted log` lists 224 addresses (from 417), all in the program at
`$7E` (the attract sequence's scenes 1 and 3).

**Timing.** Calibrated: the new entries. One exception: the jump, a stop with no access of its own, estimated at 0 against the original's 18 clocks.

## Phase 4a, timing: the battle's waits and its transfer from the main thread (`$C1:007E`, `$CF:FCFB`, `$CD:3E3B`-`$CD:3E89`, `$CD:3ECE`-`$CD:3F9D`, `$CD:3E44`, `$CD:0036`, `$CD:044A`, `$CD:04DF`, `$CD:0235`-`$CD:025D`, `$CD:3B5F`)

The second chunk of the timing service's stream (PLATFORM.md, "Timing without
a beam, as built", has the calls, the runtime's wait and the host backend).

**How it was chosen.** By `tools/fallbacks.py --by routine` over the reference
play: after the time warp, the battle's waits were its largest handbacks --
the wait for a line `$CD:3B82` 4.8 M instructions, the pause's turn `$CD:3E44`
4.0 M and its long form `$CD:0036` 2.8 M, the frame wait `$C1:007E` 4.0 M, the
return to the field's `$CF:FCFB` 1.9 M, the start's `$CD:044A` 0.8 M, the
frame work's waits `$CD:3E7D`, `$CD:3E75` and `$CD:3E3B` 0.8 M -- with the
work the waits do each frame, the message's letter `$CD:04DF` 1.3 M, the
script's sound `$CD:3B5F` 1.1 M, and the transfer that waits on them
`$CD:3ED2` 0.6 M; and the message tiles' clear `$CD:0239` 1.8 M, which the
message's start makes. The battle's frame work was 19.9 M of the 60.3 M.

**What it is.** A wait was a call-out to the original's routine, whose
loop ran on the machine (in shadow mode, and as a pass in the shipping
mode), so moving its callers onto `tm_wait_frame` alone would have retired
nothing. The routines became ports (`battle/wait.c`), and what they wait on
became the service's: a wait asks the vblank handler for something (the
frame's update, the handler's frame, the small handler's) and calls
`tm_wait_for_handlers` until it is answered, the pause given its turn on each
pass as the original's loop gives it. That needed the runtime to let a port
wait (`native_wait_loop`, PLATFORM.md). The wait for a line has no game work
in it, so `tm_wait_line` waits in place itself (`native_wait_call`). The
pause (`battle/sound.c`) sits a paused fight out a frame at a time through
the same wait; the letter's typing and the tiles' clear are
`battle/message.c`; the transfer is `battle/uploads.c`, behind the new
`vid_send_vram_and_wait`, which the battlefield's change calls instead of the
original's routine. The vars it named: `battle_alternate_frame_pending`
(`$47`, the small handler's flag), `battle_vram_send_left`, `_last` and
`_size`.

**What shadow mode found.** Registers that flow out of the waits to their
callers through the original's code: the typing leaves the message's speed in
X and a bit of the settings in the carry, which the callers read (`$CD:04DF`'s
entry derives them from the state); the transfer leaves A 1 and the carry its
count's, likewise; the frame work's wait keeps A on the stack while it waits,
so the transfer's A there had to be the original's (scaffolding, its
`leave_what_the_wait_keeps`); and the message tiles' clear leaves the flags the
battle's setup pushes. Comparing the port's X and Y at the wait points, where
the menus' text code leaves its own, found nothing a reader reads: the wait
points compare memory, S, the page and the bank, and the registers where
something reads them. The pause's entry is reached on every pass of the
original's loop and compared on its own each time, which makes the library's
shadow runs about half as long again; the frame work's waits that a port runs
as C are compared in that port's stretch and abandoned on their own, which
`tools/uncompared.py`'s baseline records.

**Fuzzing.** No interrupt comes under `ct_fuzz`, so a wait is compared up to
its first pass (`--stop-at-call` at the loop's head, which `native_wait_loop`
honours outside the runtime), or through it where the count it asks with
wraps to 0; and a sibling the port runs as C (`battle_run`) is a real call
of the original's wait there, which spins until the budget, so a state that
reaches one is skipped. Seed 1, 20,000 states each, compared and mismatched:
the frame wait 0 of 9,985 to its wait and 0 of 20,000 answered at once, the
last frame's 0 of 20,000, the small handler's 0 of 20,000, the pause 0 of
13,176 and its long form 0 of 6,578, the script's sound 0 of 14,870, the
tiles' clear 0 of 20,000 (each form), the handler's wait 0 of 44, the next
frame's 0 of 21, some frames' 0 of 5 and the letter 0 of 97 (the menus' text
guard takes few random texts), the transfer 0 of 108; the callers changed,
the frame wait's tail 0 of 14, the return to the field 0 of 16,509, the
screen's setup 0 of 1,360 and the battlefield's five 0 of 71,296. The
transfer's first run mismatched 4 of 108, a VRAM byte's low bit: the carry
at its waits reached the original's typing of a message's letter, whose
menus' text code reads it on texts the game never sets though liveness has
it dead (`--poison-site` runs: 199 of 6,982 at the typing's call, 0 of
17,684 at the menu's turn; docs/TODO.md). The shipping mode types the letter
as C, which reads no carry, behind the menus' text guard that turns those
texts away; the transfer leaves the original's carry anyway.

**Measured.** Shadow mode over the 35 movies, 0 mismatching: the frame wait
358,694 calls (2 interrupted), the letter 400,276 (806 interrupted: the
vblank handler fell in the typing), the script's sound 357,611 (24), the
transfer 10,657 (8) and its long form 403, the tiles' clear 2,487 (331) and
37, the last frame's 200, the small handler's 78; the frame work's waits
42,666, 37,244 and 17,735 calls, of which 37,244, 31,106 and 16,571 the
original reaches inside an enclosing wait's stretch, compared there as that
port's; the pause's entries 299.7 million calls, 299.2 million compared and 0.5 million interrupted. The
reference play's fallbacks went from 60.3 M instructions to 36.2 M (the
battle's frame work 19.9 M to 1.8 M, the battle's 4.7 M to 0.7 M, its
sprites' 1.9 M to 12 K). The menu's turn `$C1:0012` counts 7% more and the
message call `$CD:0021` 23% more: the measure left an idle loop's pass open
after the loop, until the next pass through the same head with the same S
took back what had run between, and the frame work's wait for the handler,
an idle loop until this chunk, left its pass open across the menu's turn.
`dev/fallbacks.c` closes a pass where its routine returns past the loop's
level now: the library's total rose by 4.6 K instructions, the overworld's
frame wait (`$C2:0346`, `$C2:0454`) taking less back.

**Timing.** Calibrated (and the frame work's waits again once their nested
calls were compared): the waits' `cost_milli` are their passes' (a pass
is a few reads, which the original spends 40 to 100 clocks on), and the
callers whose stretches now take the waits' work inline (the frame wait's
tail, the battle's frame step) moved with them.

## Phase 4a, timing: the battle frame work's remainders: the battle's close, the battlefield put back, the messages (`$CD:0006`-`$CD:0033`, `$CD:01B3`-`$CD:0310`, `$CD:039E`-`$CD:0453`, `$CD:04FD`-`$CD:0534`, `$CD:0C9D`-`$CD:0CF6`, `$CD:0D62`, `$CD:0DB1`-`$CD:0DD8`, `$CD:0E23`-`$CD:0E9D`, `$CD:3B71`)

The third chunk of the timing service's stream, after the battle's waits
(history file, "Phase 4a, timing: the battle's waits and its transfer from the
main thread").

**How it was chosen.** The battle's waits had moved the frame work's loops
into ports, and the fallback measure's fix for unfinished comparisons (the
curtain's section, below) then showed what they waited inside: the callers' call-outs ran the
original's routines whose waits these are, the battlefield put back
`$CD:0453` 20.7 M instructions of the reference play, the enemy's message
`$CD:0033` 6.7 M, the battle's own messages `$CD:0021` 0.2 M and the display's
close `$CD:0006` 0.2 M, with the boss's end `$CD:0DB1`, the colors kept
`$CD:0E23` and the descriptions `$CD:0027`; the battle frame work's 30.9 M of
the 52.2 M. Together with the start settings' copy they were nearly all of
what `burndown.py --uncovered CD` still listed.

**What it is.**
- The battle's close (`battle/closing.c`, `$CD:03B4` through `$CD:0006`):
  the battlefield put back if an effect drew over it, the music faded out
  (`$CD:039E`) unless the event kept the field's song, the window slid away
  (`$CD:0516`; its opening `$CD:04FD` through `$CD:000C` is the same slide the
  other way, with its sound), and on an HP gradient's battlefield the boss's
  end played out (`$CD:0DB1`, `$CD:0DD8`, `$CD:0D62`: the gradient settles,
  two seconds held, the victory's battlefield and song); then the small vblank
  handler fades the picture and the field's music comes back. The window's
  slide test (`$CD:0534`) is a C predicate inside the waits.
- The battlefield put back (`battle/battlefield_restore.c`, `$CD:0453`): the
  battlefield's tiles sent again from the battle's copy, its own layer faded
  back in (`$CD:0E61`, the fade `battlefield_change.c` already had, the other
  way), the screen's settings back to the start's (`$CD:0CF6`, which the start
  took, `$CD:0C9D`/`$CD:0CA1`), a quarter of a second to settle, and row 1's
  last colors back (`$CD:0E23`); `$CD:0E9D` says whether the battlefield has a
  layer of its own.
- The messages (`battle/message.c`): the battle's own (`$CD:01B3` through
  `$CD:0021`), held for the frames the message speed gives or until A; the
  enemy's (`$CD:0295` through `$CD:0033`), which the vblank handler times; the
  naming windows and descriptions typed a letter a frame (`$CD:02CB`,
  `$CD:02D8`, `$CD:0310`), sharing the opening (`$CD:025E`), the setting out
  (`$CD:027F`) and the pause (`$CD:020A`). The sound they and the window play
  is `$CD:3B71` (`battle/sound.c`). Their callers in `$C1` and the frame work
  call them as C (`battle_run`), and the two idle entries the close's music
  waits had (`$CD:042B`, `$CD:0DB7`) are inside the ports now, retired.
- Left out: the screen script runner's hand-back for a byte past the
  language's last command (`$CD:165F`; no script has one); a battle lost on
  the screen effects' battlefield (`$CD:0D93`, `$CD:0DA2`, a call-out from
  `closing.c`; nothing reaches it); the beam wait `$CD:3B82`, which is the
  timing service's `tm_wait_line` already, so an entry there would make the
  service's call recurse into a port; and the jump stubs `$CD:0000`, `$CD:0012`
  and `$CD:0039`.

**What shadow mode and the gate found.** The window's slide test and its stub
(`$CD:0534`, `$CD:0012`) were entries at first; the battle's window wait
(`$C1:003C`, an idle loop) reaches them on every pass, and the measure counted
the passes as their comparisons instead of taking them back, so they are not
registered. The effects check's runs found the script's screen effect
(`$C1:621B`) reading the cell `$53`, which the message calls leave as
scaffolding (the frames held, the naming window's kind): it read all five
settings before putting the given ones over them, so it now reads only the
ones its command doesn't give. The start settings' copy leaves the carry set,
which the battle music's call keeps on the stack. Most of the entries run only
inside their callers' stretches, which compare them there: the enemy's message
is compared alone once in 298 calls, the close and the window's close never
(`tools/uncompared.py`'s baseline).

**Fuzzing.** Seed 1 (`tools/fuzz_routines.toml`), 20,000 states each, each
compared up to its first wait or transfer as the battle's waits are, or to a
sibling that spans frames, which is fuzzed alone: 0 mismatched of 20,000 for
the battlefield put back, the start settings and the start's screen put back,
the fade back in, the layer test, the kept colors, the window's close, the
boss's end and its settle, and both naming windows; the descriptions 0 of
19,999, the window's opening 0 of 9,852, the sound 0 of 9,997, the change to
the victory's battlefield 0 of 5,353 (9,214 turned away by its guard, a
battlefield that doesn't fit), the opening 0 of 3,391 (16,609 a string from
no table of the battle's), the pause 0 of 89 and the music's fade 0 of 38
(the rest a letter being typed that the menus' text guard turns away), and
the setting out none (that guard takes no random text). The three composites, the display's close, the
battle's message and the enemy's, differ at their first sibling's call
(19,985, 15,260 and 19,912 of 20,000) in registers only, A's low byte, X, Y
and the flags the sibling declares dead, with memory equal in every state.
The script's screen effect, which now reads only the settings its command
doesn't give, 0 of 143 (the rest turned away by its guard).

**Measured.** Shadow mode over the 35 movies, 0 mismatching: the colors'
layer test 124,068 calls, the descriptions 6,156 (265 interrupted), the
opening 2,450 (348), the naming window 1,912 (2) and with HP 90, the boss's
settle 1,881, the kept colors 1,138, the start screen put back 523, the
battlefield put back 486, the fade back in 326, the setting out 314, the
enemy's message 298, the sound 113, the start settings 37, the window's opening
37 and close 32, the display's close 32, the pause 33, the battle's messages
17 (15 interrupted: each holds for frames), the music's fade 7, the boss's end
and its change 3. The reference play's fallbacks went from 52.2 M
instructions to 24.0 M (the battle frame work 30.9 M to 2.7 M: what is left
there is `$CD:0024`, `$CD:010A` and `$CD:0154`, which the timers' dispatcher's
call-outs reach). Two `open` lines are the movies' ends: `gato-battle` stops
while the rewards' second message is up (`$CD:0021`, 1,083 instructions), and
in `late-omen-terramutant` the timers' dispatcher's last call is still open
(`$C1:8290`, 11,547 to 32,402 over the library): the enemy's message inside it,
a call-out's callee before, is the dispatcher's own work now.

**Timing.** Calibrated: the new entries, and with `--drifted` the callers
whose stretches took the new ports' work in (the return to the field, the
screen's setup, the timers' dispatcher, the battle's message), and the
script's screen effect, which reads fewer cells now.

## Phase 3, menus and overworld: the world map set up, its scenes' calls and the frame's input (`$C2:0024`-`$C2:015D`, `$C2:03EF`, `$C2:0471`, `$C2:0BF6`, `$C2:23A8`-`$C2:23CE`, `$C2:26D6`-`$C2:2EC0`, `$C2:2ED9`-`$C2:2F4D`, `$C2:5798`, `$C2:7860`-`$C2:7BD0`, `$C2:8002`)

The thirteenth chunk of bank `$C2` (history file, "Phase 3, menus and
overworld: the vehicles, the screen's objects and the calls made where they
go", for the one before), picked as that one was, by what the reference play
still ran as the original in the menus and the overworld: `$C2` at 78.9%
native (47.7 KiB run, 10.1 KiB of it the original's). Three of the largest
were left: the trampoline at `$C2:8002` (2.0 million instructions, the
field's, the battle's and the object mode's vblank handlers' way to the
frame's input, `$C2:84D2`, already a port); the overworld's setup, everything
the sprite-object mode did after its own start stopped at `$C2:0024`, the
scene stream loaders among it (2.1 KiB and 0.6 million instructions); and the
routines the scenes' scripts call that nothing ported.

**What it is.**

- `overworld/setup.c`, the world map set up (`$C2:23A8`) and loaded again
  after the menus or the map view (`$C2:2C93`). Each world map's record
  (`$C6:FD00`, `overworld_data.h`) names a stream a byte in one of ten
  tables (the MD2 resources at `$C6:FDF0`-`$C6:FFE0`, `OverworldStreamTable`):
  the sprites' four pages, the party's sprites and colours (each member's
  block taken from one stream of every character's), the map's seven blocks
  of tiles, the top layer's tiles and tilemap, the palette (kept in low RAM
  with the last sprite palette from the ROM, and sent), the Black Omen's
  sprites and colours over the four maps it hangs over from the story's point
  `$D4`, the tile groups, the maps, the ground's kinds, the regions, the lists
  of places (spread from one stream to their four lists), the scene's script
  and the graphics its script sends as it plays. Then the overworld's own
  state afresh, the camera on the party, both planes drawn and the scene's
  script started. The unpacker (`data/decompress.c`) still takes the machine,
  so the loads reach it through an `Unpacker` the entries supply, which runs
  its entry as the original's call reaches it.
- `menu/display.c`, the mode's own start whole (`$C2:000F`): the cartridge's
  speed (`tm_fast_rom`), the screen and the console's registers (`$C2:0043`,
  `$C2:00F2`: the controller port's lines, the arithmetic unit, the timers),
  its state (`$C2:012F`: the direct page cleared, the shadows of the display),
  its vblank handler (`$C2:0110`) and its upload queue (`$C2:03EF`); with the
  sprite list's free links laid out (`menu/sprites.c`, `$C2:0BF6`) and every
  object slot freed (`menu/objects.c`, `$C2:0471`).
- `menu/object_commands.c`, what the scenes' scripts call: a random step to
  the right (`$C2:79E0`) and a random place in one of two stretches of the
  map (`$C2:7A0C`, `$C2:7A31`, one operation and two rows); the party hidden,
  kept and brought back (`$C2:7860`-`$C2:7888`, `menu_kept_party`); a story
  flag set and one asked about (`$C2:7B44`, `$C2:7BC4`). The location's song
  (`$C2:2F0F`) is `overworld/music.c`'s, the request passed on (`$C2:2ED9`) is
  its entry now, and the sign's label cleared (`$C2:5798`) `sign.c`'s.
- `menu/frame.c`, `$C2:8002`: the frame's input run as its call at `$C2:8006`
  reaches it, so its stop where the controller isn't read yet stays the
  input's own.

The Epoch's screens and the overworld's main loop call the new pieces as C
(the queue reset, the slots freed, the map's tiles and the map loaded again),
so `menu/epoch.c` and `overworld/main_loop.c` lost five call-outs; the main
loop hands a map the loader can't take as it stands (none the game makes) to
the original's call.

**Nested entries count.** The fallback measure files what a registered entry
hands back as its own even inside a port that covers the whole routine in
shadow mode: the palette's send at `$C2:2D47`, entered by falling through,
stopped at `$C2:2D91`, and its rest counted in every movie that set up a map,
until it became the whole routine (`overworld_finish_the_graphics`). A port
that lands around an old stopping entry should make that entry whole too.

**What the gate found.** Shadow mode: the location's song leaves A's high
byte as the region lookup's word did, which the driver's call compares; the
mode's start stops at `$C2:23A8` with its own set (D is read there, the
registers aren't). Fuzzing: the mode's page is cleared through `$EF` (the
first draft cleared `$F0` too, which no state of the game's has set); and
the map view's plane built on a random record pointer unpacked garbage over
the stack, so `menu_epoch_build_map` now asks for the world map's record to
be the one noted (`overworld_map_tiles_run`), which every real call has.

**Numbers.** Over the reference play the code run in place of hand ports fell
from 27.1 KiB and 141.9 million instructions to 24.8 KiB and 139.3
million; `$C2`'s native coverage from 78.9% to 83.8% (47.7 KiB run, the
original's from 10.1 KiB to 7.7 KiB). Every new entry matches in every
movie; `$C2:7860`, `$C2:7888`, `$C2:7A0C` and `$C2:7A31` are reached by no
movie and are fuzzed only (seed 1, `tools/fuzz_routines.toml`; counts in
`docs/ports.tsv`). Calibrated rows: the new entries, the mode's start and
screen, the Epoch's two screens, the map view's plane and graphics, and the
overworld's main loop and its return from the menus, whose call-outs became
C. Timing exceptions (`tools/timing_exceptions.txt`): the set up, the map's
loads and the frames that run them (`worst`), whose time follows the packed
streams, as the field's loads' does.

**Left.** What `C2:----` holds, 5.0 KiB the reference play runs with no call
on record: the menus' screens' code after a task's yield (`$C2:821E`), which
the measure can't attribute to the port that stopped there (TODO.md). The
frame waits (`$C2:0346`, `$C2:046E`, `$C2:0454`), the timing service's. The
scrolling lists' cursor and frame (`$C2:EA53`, `$C2:EA81`), which four
screens' bridges call out to; the equipment screen's preview (`$C2:A5C3`);
the endings' scenes' line effects (`$C6:E241`, `$C6:E27E`), which call the
scene engine's unported `$C3:000E`; and the dactyl's flight, which
`field-65m-bc-dactyl.ctm` (route `dactyl_65m_bc`) reaches now.

## Phase 4a, the shipping mode's fallbacks: the save screen's frame among the slots and the battle's steps (`$C2:CFA2`, `$C1:0003`, `$C1:1FDD`)

The menus' stream's fourteenth chunk (history file, "Phase 3, menus and
overworld: the world map set up, its scenes' calls and the frame's input",
for the one before), chosen by what the shipping mode's fallback measure (the movies and the
routes) still ran as the original rather than by what the reference play
does.

**How it was chosen.** The last full gate's shipping mode ran 56.88 M
instructions of the original's code over the movies, 51.4 M of it hand-backs,
and 13.47 M over the routes. One line was two thirds of it: `C2:D506`, 38.1 M,
the save point's save screen's choosing (`menu_choose_on_save_screen`)
handing the rest of every frame to the original. In the shipping mode
`endings-beyond-time` (33.3 M), `late-lavos` and `late-omen-zeal` (2.4 M each)
drift onto a save point's save screen and sit there for 59,000 frames, and
every one of those frames the original redrew the slot under the cursor
(`$C2:CFA2`): three block moves, each byte an instruction, the slot's
summary drawn when it holds a game and three uploads queued. No movie's
reference play reaches the save point's screen, but the main menu's save
screen jumps to the same routine (`$C2:CEFD`), which `menus-field-menu` and
`menus-end-of-time` reach. The next lines, in order of gain, were the menus'
object scripts' 1000 A.D. world map routines (`$C6:E7FA`-`$C6:EE22`, 7.3 M,
none in any reference play), the menus' tasks' and vblank handler's own code
under `C2:----` (5.6 M) and the battle's steps' dispatch (`C1:0003`, 1.1 M).

**What it is.**
- The save screen's frame among the slots (`menu/save_screen.c`,
  `menu_move_among_the_slots`, `$C2:CFA2`): the cursor's slot noted as the
  one shown and the one the rows' effect marks, a tick where the cursor came
  to it from another, the rows' effect, the slot's tilemap and the era
  label's rows cleared through their accessors (the load screen's frame,
  `$C2:E743`, shares them), the slot's summary drawn where it holds a game by
  `$C2:D36C`'s logic called as C and timed as it (`menu_draw_the_slot_shown`,
  which the load screen's frame and opening and the save point's panel call
  so too), and the three uploads queued in the save screen's order (the
  tilemap's second part before its first, where the load screen's frame
  queues the first before the second; each goes to its own place in video
  memory and the vblank sends the queue whole, so only the queue's records
  in work RAM tell the two orders apart). The save point's choosing
  (`$C2:D506`) calls it as C where its guard's condition holds; the main
  menu's save screen still reaches it through the original's jump. The
  slot's drawing's guard (`slot_is_drawable`) now also asks the summary's
  location to be a place's number, as the summary's reading masks it: past
  that the era's lookup runs off its table into bank `$00`, which fuzzing of
  the choosing on the cartridge's own screen reached as soon as the frame
  was C (631 of 1,981 mismatching, the era label drawn where the original's
  read past the table and drew none); the fuzz rows with random summaries
  keep their locations' high bytes to 0-1.
- The battle's steps (`battle/steps.c`, `battle_run_a_step`, `$C1:0003`):
  the step the accumulator names, through a C table of the ten steps'
  routines, with the asker's accumulator and index registers kept on the
  stack under the step's call (`battle_keeping_the_registers`, a
  `BattleFrame` of five bytes) and given back after. Four steps are ports and
  run as siblings (the frame's work, an action's showing, the target search
  and now the area search); the other six are call-outs to the original. The
  ports that ask for steps (the main loop's, a member's action called off,
  the party standing down, the knocked-out enemies seen to) run it as C
  (`battle_take_step`) where the runtime would run its port, and make the
  original's call otherwise. The asker's X and Y it keeps on the stack under
  the step are declared dead while they stay there (`native_stack_dont_care`,
  docs/PORTING.md) where the asker's call declares the registers so, which
  `tools/dontcare_check.py` checks as the call's.
- The area search as step 7 (`battle/area_search.c`, `battle_find_the_noted_area`,
  `$C1:1FDD`): the search already C (`battle_find_area`), entered with the
  area its asker noted in the search's cells, one body with the targeting's
  search (`$C1:C741`), which notes the shape and asks for the step. Without it the dispatch's
  call-out to the original's search was counted as hand-back inside the
  targeting's port, whose own C does the search (10.5 K instructions of new
  lines in the reference play, gone with the entry).
- The routes' fallback count is over the frames each run kept
  (`tools/movies/ctdrv.py`, `Machine.count_kept_frames`): as a run ends, its
  kept input is played again from its start by a `ct_drive` that counts, so
  the player search's probes, the attempts a restore undid and the frames a
  load replays from a base are no longer counted, and a run resumed from a
  checkpoint is counted from its start (REIMPLEMENTATION.md, "The fallback
  measure"). The replay must end on the run's frame with the run's work RAM,
  or the run fails rather than count another; so do frames run with no
  buttons kept (a probe no restore undid, a state loaded with no input beside
  it). It costs a replay of each route's kept frames, seconds for the short
  routes, under a timeout of its own.
- Left out: the menus' vblank handler's entry and exit (`$C2:840E`-`$C2:8429`,
  `$C2:8466`-`$C2:8471`), written whole and taken back, because the handler
  moves to a stack above the interrupted one and shadow mode takes a stack
  above an entry's for its return (every call mismatched at its first
  call-out); the tasks' loops (`$C2:826C`, `$C2:E91B`), which yield every pass
  and would hold a comparison open for a whole screen; the 1000 A.D. world
  map's Mode 7 routines, which only the shipping mode's drift reaches and
  would rest on fuzzing alone; the main menu's save screen's steps and the
  save itself (`$C2:CEDC`-`$C2:CF92`, 0.2 M); and, as the brief asked, the
  scene program's lines, `C0:0002` and the `$CD` lines (TODO.md for each).

**What shadow mode and fuzzing found.** Over the 35 movies, 0 mismatching:
`menu_move_among_the_slots` 418 calls, all compared; `battle_run_a_step`
29,986, of which 28,260 are compared inside the ports that ask for steps, 714
on their own and 1,009 interrupted (the steps that wait for frames, which
fuzzing compares at the step's call); `battle_find_the_noted_area` 46 (4
interrupted). The frame's work's own calls (`battle_wait_step`, 27,547) are
compared inside the dispatch's, as its sibling; the uncompared lines of both
say so (`tools/uncompared_baseline.txt`). The
don't-care check found that the steps' routines may read the overflow the
asker leaves (the frame's work and an action's showing, by a target set
decided at run time), so the dispatch passes it on untouched rather than
declaring it; and that A's high byte is read after the area search's
return, which its declaration now compares. **Fuzzing** (seed 1,
`tools/fuzz_routines.toml`): the frame among the slots 0 of 4,724 (276 turned
away), the choosing that now runs it 0 of 2,000, the slot's drawing 0 of
1,894 and the load screen's frame 0 of 1,894 with the locations kept to
places; the dispatch 0 of 5,000 compared at each of the ten steps' calls
(`ct_fuzz` takes twelve `--stop-at-call`s now, from eight); the area search
step 0 of 5,000.

**Measured.** The shipping mode over the movies: 10.9 KiB of code and 18.70
M instructions in place of hand ports, from 11.0 KiB and 56.88 M (the save
point's choosing's line 38.1 M to 3,228 instructions, the save itself, which
it still hands back); over the routes, counted over the frames they keep, 6.7
KiB and 5.50 M, from 13.47 M counted over every frame they ran. The
reference play: 19.6 KiB and 22.24 M, from 22.83 M (`C1:0003` 0.32 M to 45
instructions; `C2:----` 3.65 M to 3.38 M). No line rose there; the line
`open C1:0003` is new, what an action's showing's and the frame's work's open
comparisons hid at a movie's end (`open C1:4058`, `open C1:106E`), under the
dispatch's comparison now that they run as its siblings. In the shipping
mode `endings-telepod-lavos` rose at first, from 3.29 M to 8.29 M: the
dispatch's estimate (its value calibrated on its siblings' stretches) ended
a frame of the Lavos shell fight one pass early, the party lost and went
back to the title, and the presses opened New Game's name entry, where the
menus' task loop and vblank entry run every frame (`C2:----` 3.76 M,
`7E:0500` 1.25 M). With the steps' siblings measured on their own stretches
(Timing, below) it plays to its end again, 3.26 M. (The route count's
replay was checked by hand on the `telepod_lavos` route: the same outcome at
the same frame, 134,547, with byte-identical work RAM; every route's count
checks that now.) The review's answers moved the others: over the movies 20.83 M (from 18.70 M),
`endings-bucket-lavos` 0.40 M to 7.00 M, whose battle near frame 15,000 now
runs its passes ahead and goes on to a menu screen and the 1000 A.D. world
map's event; under `--port-timing oracle` both builds play it the same, and
both estimated runs already leave the original's timing before frame 8000.
Over the routes 5.43 M (from 5.50 M).

**Timing.** Calibrated (`--drifted`): the new entries, the load screen's
frame and the choices round it, and the steps' siblings, measured on their
own stretches (`native_time_as_sibling`, "The timing gate"): an action's
showing 9781 to 18,841 (17,617 before the dispatch was a port), whose
worst-stretch exception is its own again, and the dispatch 94,180 to 2367,
which has none now.

## Phase 4a, the shipping mode's fallbacks, chunk 2: the menus' vblank handler whole, and the Day of Lavos's turning world (`$C2:840E`-`$C2:8471`, `$C6:E7FA`-`$C6:EE30`)

The menus' stream's fifteenth chunk (history file, "Phase 4a, the shipping
mode's fallbacks: the save screen's frame among the slots and the battle's
steps", for the one before), again chosen by what the shipping mode still ran
as the original.

**How it was chosen.** The last gate's shipping mode ran 26.27 M instructions
of the original's code over the movies and the routes (20.83 M and 5.43 M).
Its two largest pieces were the menus' vblank handler and the 1000 A.D. world
map's event. The handler's own code was 5.6 M of it, less than the whole `C2:----`
line (7.7 M) it was taken for: its entry (`$C2:840E`-`$C2:8420`, filed under the `$0500` stub's
hand-back, `7E:0500`, 2.38 M), the rest of the entry and the exit after the
stack moves (`$C2:8421`-`$C2:8429`, `$C2:8467`-`$C2:8471`, 2.98 M of the
`C2:----` line) and the exit's first pull, after `menu_nmi_work`'s stop
(`C2:842A`, 0.2 M), each run 198,549 times. The rest of `C2:----` is the
menus' task loop and the records' sprite task (`$C2:826C`, 3.5 M; `$C2:E91B`,
0.6 M), which TODO.md keeps for the tasks as fibers. The world map event's
routines (`C6:E9F5`, `C6:EDE1`, `C6:E8C7`, `C6:E7FA`, 7.25 M) ran only in the
three endings' shipping runs, identically in each: each drifted into losing
the Lavos fight, and a loss to Lavos plays the Day of Lavos.

**The runtime: an interrupt entry's comparison ends at its RTI**
(PORTING.md, "Interrupt handlers"). The handler moves to its own stack above
the interrupted code's, and shadow mode took S rising above an entry's for its
return, so a port of it was compared as returned at the move. An entry flagged
`.interrupt` is now followed to the original's RTI instead, and where S jumps
further in a step than an instruction pushes or pulls the popped region starts
again on the new stack (the stack it left keeps its own), so the bytes between
the two stacks are compared rather than taken as popped. Only flagged entries
see either change. `shadow_test` has a handler that moves to its own stack:
matched with the flag, a mismatch without it, a wrong count and a stray write
between the stacks caught, and the dead bytes it pushes and pulls on the
stack it leaves not compared (without the left region they mismatched; without
the low mark's restart the stray write passed). The fallback measure and the
fuzzer follow the same calls (a held extent in `dev/fallbacks.c`, the popped
region in `dev/fuzz.c`; `ct_fuzz --s` and `--jump`). **Evidence that nothing
else moved:** over main's 35 movies, every (movie, entry) line of the shadow
reports but `menu_nmi_work`'s and the new entries' is identical to main's
gate, 71,540 of them; `menu_nmi_work` has the same 47,347 calls, all matched
there and here.

**What it is.**
- The menus' vblank handler (`menu/display.c`): its entry, `menu_nmi_enter`
  (`$C2:840E`), keeps the interrupted code's registers on its stack, notes the
  stack (`menu_interrupted_stack`, `$7E:1DFE`, new in the map), moves to the
  handler's own at `$1DFD` and its page, and stops where its work's port
  begins; the work, `menu_nmi_work` (`$C2:842A`), now ends with the exit,
  everything put back and the RTI. Two entries rather than one because the
  work's is a port of its own with call-outs at the handler's sites: a whole
  handler port would have set its comparisons aside at its first call. The
  register mechanics are machine helpers (`m_enter_handler_stack`,
  `m_leave_handler_stack`, `runtime/port/machine.h`); the work's guard asks for
  the stack its entry built and an interrupted stack below the handler's page.
- The Day of Lavos's turning world (`menu/turning_world.c`, a new file): the
  1000 A.D. world map drawn as a Mode 7 picture (`menu_object_draw_the_world`,
  `$C6:E7FA` with `$C6:E8C7`; the picture's pixels shared with the Epoch's map
  view in `menu/map_picture.h`), the matrix put on line-effect slots 2-5 and
  the turn set up (`menu_object_start_the_world_turning`, `$C6:E90F`), each
  frame's matrix a line pair at a time in perspective with the plane's centre
  and scroll (`menu_object_turn_the_world`, `$C6:E9F5` with `$C6:EDE1`), and
  the angle moved by the turn the frame hands on in a cell
  (`menu_object_turn_the_world_on`, `$C6:EE23`). The sine and cosine are the
  menus' (`menu_sine`, `menu_data.h`, now the Epoch's too); slots 4 and 5's
  tables and the object's turning fields are new in the map.
- A reference for it: the route `lavos_lost` (`tools/movies/routes/`, sharing
  `telepod_lavos`'s way to the shell) loses the shell's fight from the
  technoplaza fair save and plays the Day of Lavos to the globe;
  `battle-lavos-gameover.ctm` (16,192 frames) is in the library, so shadow
  mode compares the event, and it reaches no refusal.
- Left out: the menus' task loops (above); `$C6:EE31`, which nothing in the
  library calls; and, as the brief asked, the scene program's lines (the
  globe's `7E:6439` and `7E:65DD` among them), the `$CD` lines and `C0:0002`.

**What shadow mode and fuzzing found.** Over the 36 movies, 0 mismatching:
`menu_nmi_enter` and `menu_nmi_work` 48,055 calls each, all compared to the
RTI; `menu_object_turn_the_world` 249 (one interrupted, inside the start),
`menu_object_turn_the_world_on` 234; the drawing and the start once each, both
interrupted (each runs past a vblank), so they rest on fuzzing. Shadow mode
found the product register the divider's remainder shares (the multiply comes
last), and that the start falls into the frame with a 16-bit accumulator,
which the frame's precondition now takes rather than refusing. **Fuzzing**
(seed 1, `tools/fuzz_routines.toml`): the handler's entry 0 of 20,000, and 0 of
20,000 interrupting 8-bit registers; the work 0 of 1,669 to its RTI (331 whose
random callees don't return), from its own stack (`--s 1DF8 --jump`) with an
interrupted stack anywhere below the handler's page (fuzzing first found an
interrupted stack inside the handler's own reading what the ports among the
callees don't push, which the guard now refuses); the drawing 0 of 131 with random planes (169
turned away: planes in low RAM, which the original reads changing under it as
it keeps its cells and return there) and 0 of 300 with the scene's planes;
the start 0 of 2,000; the frame 0 of 20,000, and again with a 16-bit
accumulator, the tilt one of the table's 24 (its guard refuses any other,
which no scene sets: the original's index reads on into the code after the
table); the turn 0 of 2,000.

**Measured.** The shipping mode over the movies: 9.3 KiB of code and
8.07 M instructions in place of hand ports, from 10.9 KiB and 20.83 M (7.99 M
over main's 35 movies; the new movie's own shipping run, which drifts away
from the Day of Lavos too, adds 0.08 M): the handler's 5.6 M and the event's
7.25 M are gone, and `C2:----` (4.75 M) is now the task loops'. Over the
routes, counted over the frames they keep, 5.23 M over main's nine, from
5.43 M, and 6.02 M with `lavos_lost`, which the long routes now run (0.78 M:
the globe's scene program, mostly). The reference play: over main's 35
movies 20.91 M, from 22.24 M (the handler's lines gone: `7E:0500`, `C2:842A`,
and its part of `C2:----`); with the new movie 21.97 M over 36 movies and
20.3 KiB, the baseline taking the movie's lines (the globe's scene program,
`7E:6439` and `7E:65DD`, and the colours' sampling, `C2:6400`, `C2:6453`, that
the event's script calls). No refusal in real play; the native shape runs all
36 movies and reaches all ten routes' outcomes. `--misses` lists nothing over
`menu` and `battle-lavos-gameover`, and `--px-check` reports every frame of
both identical.

**Timing.** Calibrated (`--drifted`, 33 rows: the new entries and those the new movie moved): the
handler's entry at 4791 and its work 3755 to 3113; the frame 12,410, the turn
2000; the drawing and the start, which no stretch of the library completes,
at their calls' ratios (13,034 and 11,857). One exception: the scripts' start
of an object, whose search for a free slot is long where the Day of Lavos
keeps dozens of needles running (`tools/timing_exceptions.txt`; a charge per
slot passed overshot every other scene).

## Phase 4a, the shipping mode's fallbacks, chunk 3: the menus' task loops and the screens' steps, the save screens, and the planet seen from space (`$C2:826C`-`$C2:828E`, `$C2:E91B`-`$C2:E922`, `$C2:CEDC`-`$C2:CFA1`, `$C2:D4BB`-`$C2:D4EF`, `$7E:6439`-`$7E:64BE`, `$7E:6598`-`$7E:663F`)

The menus' stream's sixteenth chunk (history file, "Phase 4a, the shipping
mode's fallbacks, chunk 2", for the one before), again chosen by what the
shipping mode still ran as the original.

**How it was chosen.** The last gate's shipping mode ran 14.08 M instructions
of the original's code over the movies and the routes. `C2:----` was 4.92 M of
it, and `fallbacks.py --report` split it into two loops, each run once a frame
for as long as the menus are open: the menus' main task (`$C2:826C`-`$C2:828E`:
its wait, then the running screen's step looked up through the table at
`$C2:8292` and jumped to; 21 instructions a pass, 204,627 passes, 4.30 M) and
the cursor's task (`$C2:E91B`-`$C2:E921`: the sprites' reset, the wait, a
branch back; 0.62 M), nearly all of both in the three endings' shipping runs,
which drift onto a save point's save screen and sit there. The planet's two
frame tasks (`7E:6439`, 1.61 M, and `7E:65DD`, 1.37 M) came next. Ported, the
main task's turn handed the save screens' overwrite question (`$C2:CF61`, the
step the endings sat on) back to the original 52,750 times (0.42 M), so the
main menu's save screen's steps, the question and the save itself followed,
which leaves no step of any screen to the original. `7E:99B5` and `7E:A03D`
(0.55 M and 0.51 M, the `beyond_time` route only) are another scene's, an
orbit and a sprite list whose callers patch its code, and the `$CD` lines the
battle's; both are left (TODO.md).

**The runtime: dead stack bytes kept to a stop** (PORTING.md, the don't-care
sets). A loop's wait keeps the task's X, Y, P and D on the task's stack for
its return, and the loop goes on into code that loads both index registers
before reading them; but the port's X and Y there are its own (the cursor's
reset leaves the original's last place in them, a step's C its own), and
shadow mode compares the bytes on the stack the task left. A port already
declares such bytes dead with `native_stack_dont_care` while they stay pushed;
the declaration ended with the port's job, before its stop's comparison, and
only covered bytes above the original's S. The bytes a port leaves declared
where it stops now stay uncompared at the stop wherever the original's S has
gone (another task's stack). The battle's steps, the one other user, release
theirs before they stop, so nothing else moves; `shadow_test` has a handler
that leaves a wrong X on the stack it moves from, matched with the
declaration and a mismatch without. The loops' kept X and Y are left
uncompared only as far as the set their stop declares has them, which
`tools/dontcare_check.py` checks at the wait's JSR through the wait's keeping
them, and `tests/test_menu_task_waits.py` ties to the ROM: the wait pushes X
then Y under the return, and neither is read after it at `$C2:826F` or on the
cursor's way round to `$C2:E92D`. A call the original makes as a jump through
a pointer, its return pushed before it (a `PER`), is a call-out of its own
(`native_jump_call_dc`; the menus' `menu_call_jumped` and `menu_run_jumped`),
named by the jump's own site; the check takes one that declares nothing only
where liveness has the site's target read at run time.

**What it is.**
- The menus' two tasks that run all the time (`menu/tasks.c`): the main
  task's turn (`menu_run_the_main_task`, `$C2:826F`, from its start and from
  its wait), the running screen's step and then the wait; its wait alone
  where a step came back through the original's code (`menu_main_task_waits`,
  `$C2:826C`); and the cursor's turn (`$C2:E91B` and `$C2:E921`, one
  function), the sprites' reset and the wait. The wait is the yield's port
  run as its JSR reaches it (`menu_run`), where the runtime would run it
  (`menu_runs_as_port`, `menu/bridge.c`, new), and otherwise the JSR's.
- The screens and their steps (`menu/screens.c`, new): a C table of the
  twelve screens (`MenuScreen`, `menu/menu.h`) and each one's steps, the step
  run as C from the original's jump to it (`$C2:828E`) under the return to the
  wait its `PER` pushes, timed as the sibling's own; where a step's guard refuses, the original's routine
  is called. The step's routine finds its address in A as the jump leaves it
  (the exchange's opening reads its high byte where it refuses); the rest the
  lookup leaves, X, Y, the flags and the two cells it goes through, is
  scaffolding.
- The save screens (`menu/saving.c`): the main menu's save screen's opening,
  refused with a buzz where saving takes a save point or a vehicle is out
  (`menu_open_saving`, `$C2:CEDC`); its choosing of a slot (`$C2:CEFD`); the
  slot chosen, which asks first where it holds a game (`$C2:CF0A`, also where
  a save point's choosing stops); the question whether to save over it and
  the step after it no code moves to (`$C2:CF61`, `$C2:CF92`, which both save
  screens share), with its window opened and put away (`$C2:D4BB`,
  `$C2:D4D5`, as C); the save itself (`menu_save_the_game`, `$C2:CF37`: its
  sound, the slot marked, `saves_save_game`, a frame, the slot's summary read
  back, a frame) and the slot shown after it (`$C2:CF21`). A step's read of
  the cursor is one helper of `menu/cursor.h` (`menu_step_reads_the_cursor`,
  and a form that leaves the flags of the original's test of the buttons,
  which a save point's choosing now takes too), and the slots' labels'
  graphic and its three uploads are named once, in `menu_data.h`. The record between the Epoch's and the
  dactyl's in the world map's vehicles, whose state the opening reads with
  theirs, is new in the map (`overworld_second_vehicle_state`).
- The planet seen from space (`scene/planet.c`, new): the scene program's
  modes 7 and 8, the Day of Lavos's end when the party loses among them. Its
  palette's colours stepped toward the colours a script chose, a step of
  each of red, green and blue every so many frames (`$7E:6439`; the period an
  operand of the step's own code, which the shared endings' routine changes,
  now a variable in the map); two dozen stars twinkling about it, each waiting,
  playing its ten frames four ticks each with the program's sprite for it, and
  put anew elsewhere (`$7E:65DD`), all put out as the scene sets up
  (`$7E:6598`). Both tasks are rows of the frame lists' C table.

**What shadow mode and fuzzing found.** Over the 36 movies, 0
mismatching: the main task's turn 46,419 calls (72 interrupted),
its wait 46,368, the cursor's turn 47,292 and 47,239; the main menu's
save screen's choosing 422, the question 110, the slot shown after a
save 2; the opening, the slot chosen and the save once each, their own
comparisons set aside inside the main task's turn, which reaches every call
they make; the colours' step 4,377, the stars 660, their putting
out 1. Shadow mode found a step that comes back with a 16-bit
accumulator (the item list's), which the turn's wait takes as it is; the
index registers the wait keeps (the runtime change above); and the flags the
steps' own tests leave where a tick saves them on the stack. **Fuzzing** (seed
1, `tools/fuzz_routines.toml`), 0 mismatching: the main task's turn 2,239 of 10,000
(the rest the guard refuses past a screen's last step, or calls past the
budget), its wait and the cursor's turns 2,000 and 20,000 each; the save screens' steps
and the save every one compared but those whose save, the original's call outside the runtime, runs past the budget (`menu_save_in_the_slot` 989 of 2,000), between 1,887 and 2,000 of 2,000 each; the colours' step 20,000 of 20,000 and loosely
1,535 of 20,000, the stars 10,009 of 20,000 (10,029 loosely) and their putting out 10,023 of 20,000 (the rest
entered with the carry set, which the guards refuse). The loose run found the
colours' step reading back what it writes where the colours lie over its
code, its working cells or the stack's top, which its guard now refuses.

**Measured.** The shipping mode over the movies: 8.8 KiB of code and
1.52 M instructions in place of hand ports, from 9.3 KiB and 8.07 M:
`C2:----` from 4.75 M to 0.19 M (what is left is the held cursors' task's
loop, the records' task's return to its start and a step of the save
panel's), the question's 0.42 M and the planet's lines gone. Over the routes,
counted over the frames they keep, 4.80 M from 6.02 M (the planet's scene of
`lavos_lost`, the menus' loops); movies and routes together 6.32 M from 14.08
M, of which `CD:0154`, `CD:010A`, `7E:99B5` and `7E:A03D` are 2.46 M. The
reference play: 19.91 M from 21.97 M over the 36 movies, 19.5 KiB from 20.3
KiB. The baseline takes the gain and three new lines: the main task's turn's
290 instructions where a step stops in the original's code, and the menus'
wait for the frame's uploads (`$C2:81CA`, an idle entry shadow mode leaves to
the original, 49,680 instructions), filed now under the cursor's turn, whose
wait, the frame's last task's, stops there. No refusal in real play; the
native shape runs all 36 movies and reaches all ten routes' outcomes.
`--misses` lists nothing over `menu`, `menus-field-menu` and
`battle-lavos-gameover`, and `--px-check` reports every frame of the three
identical. Nine screens' steps the turn runs as C, each of which makes a
call-out, now have their own comparisons set aside inside the turn's, which
covers them (`tools/uncompared_baseline.txt`).

**Timing.** Calibrated (`--drifted`, 29 rows): the new entries -- the
main task's turn at 554 and its wait at the floor, the cursor's turns 24,747
and 25,865, the question 5,755, the stars 10,350, the colours' step 4,023 once
it charges the working cells it keeps in locals (24 bytes a colour: one
scalar fitted neither the frames that step and the frames that don't) -- the
screens' steps the turn runs, whose stretches take in its lookup and wait as
a sibling's, and the frame list, which runs the planet's tasks. The save
screens' other entries, which no stretch of the library completes on its own,
keep the placeholder. Four exceptions (`tools/timing_exceptions.txt`): the
main task's wait, whose stretch is the wait's; the main menu's and the shop's
how-many steps' vblank tails (1.08% and 1.05%), now that they run inside the
turn; and the twinkling's, whose time follows how many stars wait, twinkle
or are put anew.

## Phase 4a, the shipping mode's fallbacks, chunk 4: the beaten enemies seen off, the Beyond Time ending's night, and the scene program's sine and signed multiply (`$C1:006D`, `$CF:FE20`-`$CF:FE7F`, `$CD:0024`-`$CD:019C`, `$CD:0E05`, `$CD:0C89`, `$7E:9080`-`$7E:90CE`, `$7E:992A`-`$7E:9E79`, `$7E:A03D`-`$7E:A098`, `$7E:33D6`-`$7E:340C`, `$7E:3461`-`$7E:3479`)

The battle's and the scene program's streams together this time (history
file, "Phase 4a, the shipping mode's fallbacks, chunk 3", for the one before),
chosen, as the last three were, by what the shipping mode still ran as the
original.

**How it was chosen.** The last gate's shipping mode ran 6.32 M instructions
of the original's code (the movies 1.52 M, the routes 4.80 M). Its largest
lines were the dissolve of a beaten enemy (`CD:0154` 0.75 M and `CD:010A`
0.65 M, its step and the step's loop over the tiles), the map layers' scroll
a battle on a map's background calls through `$C0:0002` (0.44 M), and the
Beyond Time ending's tasks in the scene program: the shared endings' orbit
(`7E:99B5`, 0.55 M), the program's own list of large sprites (`7E:A03D`,
0.51 M), the flight's view (`7E:9AF3`, `7E:9DBC`, `7E:9ABD`, 0.54 M) and the
Mode 7 pictures' (`7E:9E2B`, 0.16 M). The dissolve is reached only from seeing
a beaten enemy off, so the battle's step 6 came whole with it; the Beyond Time
tasks are one scene, taken whole with the fireworks the shared setup places.
The program's sine and signed multiply, which those tasks, the race's and
every scene's original code call, were the stretch, and came too.

**What it is.**
- The enemies an action beat leave the fight (`battle/beaten_enemies.c`,
  new): the battle's step 6 (`$C1:006D`) and its search from the last enemy
  place to the first (`$CF:FE20`): each beaten enemy is down, its mark and
  drift cleared; a hidden one leaves its place; a drawn one takes the dying
  enemies' palette and is seen off (`$CD:0024`, `$CD:0043`), unless it stays
  on the field after the battle. Seeing off waits for the battlefield to be
  put back, then dissolves the enemy with a sound (`$CD:0093`, `$CD:00A0`:
  two of the 64 places of each tile go clear a frame for 32 frames, the
  stride through the tile its pace's, `$CD:010A`/`$CD:0154` as one step), or
  changes the battlefield where its family has it (`$CD:0E05`), or under an
  enemy's rule dissolves without the sound and then changes the battlefield
  to the rule's (`$CD:006B`). The waits and transfers are the battle's
  bridges (`battle/wait.c`, `battle/uploads.c`), made where the original
  makes them.
- The map layers that scroll by themselves under a battle (`$CD:0C89`): the
  field's own C (`field_scroll_layers_by_themselves`), timed as its routine,
  in place of the long call through `$C0:0002`.
- The Beyond Time ending's night (`scene/beyond_time.c`, new): the flight
  over the world's map in the dark -- the map passing under the camera
  (`$7E:90C1`), the view checked, the flat plane turned or the perspective
  built again where the camera changed (`$7E:9ABD`, `$7E:56B4`; the camera
  kept by `scene_keep_the_camera`, now shared with the race's camera check,
  as is the horizon's split, `scene_horizon_lines`), placed and sent in the
  vblank (`$7E:9AF3`, `$7E:9DBC`, `$7E:5654`) and the horizon set
  (`$7E:9080`); the Epoch circling and swaying (`$7E:992A`, `$7E:997D`); the
  town's lights twinkling (`$7E:9D48`, `$7E:9D45`); the fireworks placed by
  the shared setup, now as C (`$7E:9BB8`), and bursting (`$7E:9BF9`, the
  burst picked at `$7E:9BD8`); the Mode 7 pictures sent (`$7E:9E2B`,
  `$7E:9E19`); and the shared endings' orbit (`$7E:99B5`). "The End" over
  the last scene (`$7E:99FA`) joined `scene/endings.c`.
- The program's own list of large sprites (`scene/sprites.c`,
  `scene_draw_large_sprite_list`, `$7E:A03D`), beside the lists the scene
  engine draws.
- The program's sine and cosine and its signed multiply as entries
  (`scene/math.c`, `$7E:3461`, `$7E:3465`, `$7E:33D6`), which the C callers
  already had; the original's own code, where it still runs, now reaches
  them as ports.

**How the list of large sprites picks its form.** Its callers patch its code.
As the program is loaded, its test at `$7E:A074` leaves out a sprite whose Y
comes to the picture's bottom or below (a branch back to the next sprite);
the fireworks save that word, write two no-operations over it for their
bursts so that every sprite shows however low, and put it back after; they
also write a compare into two bytes no path runs (`$7E:A090`) and then the
load the program has there. The port has the two forms as a type
(`SceneLargeList`): the fireworks call it as C with the form that keeps every
sprite, and the registered entry, which only the original's code reaches,
reads the form from the test's code bytes (`scene_large_list_clip`, named in
the map), where the guard refuses any code but the two the program writes.
The fireworks' saved word and the spare bytes' final load are scaffolding.
The original's form that leaves sprites out goes on with a 16-bit
accumulator once it has left one out, storing the next sprite's bytes as
words; the guard refuses a list that leaves out any sprite but its last, and
that form's one caller (`$7E:9C58`) no showing reaches.

**What shadow mode and fuzzing found.** Over the 36 movies, 0 mismatching:
the dissolve's step 3,008 calls; the step 6 and its search 52 each, 12 of
them compared on their own (no enemy dissolved) and the rest inside the
turn's comparison, as seeing an enemy off and the dissolve (47 each) are,
since they span frames; the map layers' scroll 440,679; the Beyond Time
tasks between 163 (the Epoch swaying) and 15,676 (the orbit) calls, the
fireworks' setup 2, the list of large sprites 3,198 and "The End" 3,211, all
in `endings-beyond-time`; the signed multiply 41,691 and the sine and cosine
223,895 and 97,948, in every movie. The battlefield's change as an enemy
leaves, the lights' twinkle with the camera turned back and the picture sent
with BG2's scroll nothing in the library calls: fuzzing alone checks them
(TODO.md). Shadow mode found the flat view's guard refusing 975 real calls:
the flight's flat view keeps its camera half a screen left of the map's
left edge, where the original's sum across carries into its sum down and
puts the plane's centre a line lower; that is now a named rule
(`left_of_the_map_by_less_than_the_centre`) rather than a refusal, with a
fuzz variant about the edge. The gate's shipping runs with
`--scaffold-reads` found the screen script reading, in three movies' Lavos
fights, what a dissolve's step leaves in the battle's shared cells: they lie
over the screen effect's kept settings, which an effect giving fewer than
five takes up, so those three words are written plainly
(`leave_the_step_over_the_effect_settings`; the overlap named in the map and
in TODO.md). **Fuzzing** (seed 1, `tools/fuzz_routines.toml`), 0 mismatching
everywhere: the battle's entries 20,000 of 20,000 each up to their first
wait or long call (the battlefield's change 19,933, the rest past the
budget), the scroll 20,000; the scene's tasks 20,000 each but the flight's
view sent flat with the carry set (14,958, and 10,029 of 20,000 about the
map's edge), the fireworks (13,264; the guard refuses a firework at no frame
of a burst or a burst's start met early) and the list of large sprites
(7,580 in the fireworks' form; the guard refuses a list whose sprites
don't fit the table from the cursor); the multiply, the sine and the
cosine 20,000 each (the multiply on a channel's page too).

**The review** asked for nine things, all done but one part. The word in the
battle's first shared cells (`$45`), whose high byte the screen scripts'
drift tests, is written plainly (`battle_screen_leftover`): the reviewer
named the screen window's leftover, but the shipping run's read was of what
the layers' scroll leaves there every frame and, at the one drift that read
it, what a layer's nudge had left; the scroll and every screen command that
leaves the word in its logic now write it plainly
(`screen_leave_for_the_drift`), and the few that leave it only in a leaves
helper are in TODO.md. The bursts' frames are read through one accessor returning a
frame's count and list (`scene_firework_bursts`, now in the map) where
offsets had read the next entry's count; the view's check is named for the
turn it also makes (`update_the_view`); the cosine's overflow is one helper
the perspective's builder shares, and the quarter turn one definition
(`scene.h`); loading a battlefield and marking its scripts running is one
operation (`battle_load_and_run_battlefield`); `field_line_screens` has its
four bytes as fields; the drawn enemy and the family's mark are predicates in
`battle.h`, and the gradient's bit is `frame.h`'s. The search's leftovers are
one helper after its loop, but for those the call to see an enemy off must
find, since shadow mode compares memory at the call: they went into that
call's own helper. The part not done: the fireworks' leftover offset stays
scaffolding, because written plainly it would read two cells as a word and
past the places in the logic (items 99 and 103); its baseline lines keep
their reasons. The math lines' reasons name the eight calls that store the
operands.

**Measured.** The shipping mode over the movies: 8.4 KiB of code and
0.89 M instructions in place of hand ports, from 8.8 KiB and 1.52 M; over the
routes, counted over the frames they keep, 0.63 M from 4.80 M (the dissolve's,
the scroll's and the Beyond Time ending's lines); movies and routes
together 1.52 M from 6.32 M, with `CD:0154`, `CD:010A`, `C0:0002`,
`7E:99B5`, `7E:A03D` and the flight's and pictures' lines gone. What leads
now is the battle's long-call return (`C1:0012`, 0.20 M, the music's fade and
the way back to the field), the Mode 7 picture's tiles (`7E:5EF8`, 0.20 M)
and the menus' unported code (0.20 M). The reference play: 13.90 M from
19.91 M over the 36 movies, 18.0 KiB from 19.5 KiB. The baseline takes the
gain, with no new line. No refusal in real play; the native shape runs all
36 movies and reaches all ten routes' outcomes. `--misses` lists nothing
over `endings-beyond-time`, `endings-bucket-lavos`, `gato-battle`,
`battle-magus`, `battle-lavos-gameover`, `late-omen-battles`,
`events-jetbike-race` and `attract-demo`, and `--px-check` reports every
frame of the eight identical. The step 6 and its nested entries have 52
calls or fewer, under the uncompared check's floor, so the uncompared
baseline doesn't change.

**Timing.** Calibrated (`--drifted`, twice): the new entries -- the Beyond
Time tasks between 2,230 (the lights) and 14,339 (the Epoch circling), the
list of large sprites 5,482, the dissolve's step 5,454, the multiply 7,469,
the sine 9,453 and the cosine 11,067 -- and the scroll under a battle, whose
work is now the field's routine's part (18,362 on its own few accesses),
and the endings' picture frame (4,125), which now picks the frame
through the Epoch's helper. Two that nothing calls take their twins' values
(the lights' 2,230, the picture's 4,698); the fireworks' setup, the search
and the dissolve, whose stretches the library has too few of to judge, their
calibrated ones. The step 6 is the odd one: its own accesses are its long
call's few, while the original's time in its stretches is the whole of the
seeing off, its parts at their own rates and a remainder (the dissolve's frame
loop and its fades) that no own access stands for, so its rate is 365,064 per
access. A first calibration made before the dissolve's step wrote its
leftovers plainly drifted it and the knocked-out enemies' step, which carries
it as a part, past the limit; calibrated again from the next gate's shadow logs
(365,064 and 8,414) both hold. The review's changes moved three, calibrated
again (`--drifted`): the field's screen effects and the event command that
runs them, whose screens' values are now written a field at a time (3,484
and 5,212), and the fireworks, which read their bursts' frames through the
map (7,461). No exception.

## Phase 4a, the shipping mode's fallbacks, chunk 5: the battle's menu line and the party's poses, the dactyls on the 65,000,000 B.C. map, the scroll bar's task, and the endings' Mode 7 tiles (`$C1:0012`, `$C1:1B19`-`$C1:1BAA`, `$C1:354D`-`$C1:3713`, `$C1:BD6F`, `$C2:4CC5`-`$C2:5627`, `$C2:6400`, `$C2:65B2`, `$C2:77F2`, `$C2:8BB8`-`$C2:8C33`, `$C2:A5C3`, `$C2:A5E7`, `$7E:5E3C`, `$7E:5EF8`)

The battle's, the menus' and the scene program's streams together again
(history file, "Phase 4a, the shipping mode's fallbacks, chunk 4", for the one
before), chosen, as the last four were, by what the shipping mode still ran as
the original.

**How it was chosen.** The last gate's shipping mode ran 1.52 M instructions
of the original's code (the movies 0.89 M, the routes 0.63 M). Over the movies
its largest lines were the menus' code after a task's wait (`C2:----`, 0.19 M,
most of it the scroll bar's task), the Day of Lavos's turning world sampling
and placing the world map (`C2:6400`, `C2:6453`, `C2:65B2`, 0.20 M, the
scene's script calling them directly), the equipment screen's preview
(`C2:A5E7`, 93 K), the party's poses (`C1:369A`, 79 K, with the opening that
goes into them, `C1:0042`) and the battle menu's turn (`C1:0012`, 52 K); over
the routes the endings' Mode 7 tiles (`7E:5EF8`, 0.20 M) and the menu's turn
(0.15 M). The menu's turn calls the line of members waiting for the menu
(`$C1:1B67`, already C), so the line's two steps came with it; the poses'
victory and defeat are the battle's steps 8 and 9; the dactyls
(`$C2:4CC5`-`$C2:5627`) were the bank's last unported run, which
`field-65m-bc-dactyl.ctm` rides. The list's cursor kept within its rows
(`$C2:EA53`) was left: five call sites in four bridges, each reading registers
it leaves (TODO.md).

**What it is.**
- The party's line for the battle menu (`battle/menu_line.c`, new): a member
  whose turn comes joins the line with the turn's chime (`$C1:1B19`, the
  battle's step 1), the first in line takes the menu at the menu's turn
  (`$C1:1B67`, moved from `frame_step.c`), and a member whose action is
  called off steps out of the line or gives its slot up, the menu handed on
  (`$C1:1BAA`, step 2; `battle_hand_the_menu_on`, which the queued action
  in `menu_input.c` now shares). The menu's turn by the frame wait's long
  call (`$C1:0012`, `$C1:10E3`) is the frame step's turn without its tail.
- The party's poses (`battle/party_poses.c`, new): loaded from the opening's,
  the victory's or the defeat's animation (`$C1:35DD`), stepped every other
  frame for 64 (`$C1:369A`), the waits and the frame work's tail the battle's
  bridges; the opening (`$C1:0042`) goes into them as C; the victory (`$C1:356D`,
  step 8) sees the fight's leftovers off and takes each member's menu away a
  frame apart before its poses, unless the battle is set up without them
  (`BATTLE_SETUP_NO_VICTORY_POSE`); the defeat (`$C1:354D`, step 9).
- A hasted or slowed combatant's turn wait as it starts again (`$C1:BD6F`,
  `battle_restart_turn_wait`, which the members' turns already called as C):
  worked out from the full wait and handed back, and called as C by the
  enemies' turns too.
- The dactyls (`overworld/dactyl.c`, new): the leader's dactyl as the
  vehicle (`$C2:4CC5`), taking off, flown by the pad, coming down where the
  ground is open; the other two in a V behind it (`$C2:5097`), each heading
  for its place every fourth frame by the arctangent (`$C2:229D`), slowing so
  as not to pass it, gliding onto the leader's spot as it lands; and each
  one's second sprite (`$C2:504B`, `$C2:5494`). What the leader's shares
  with the Epoch -- its state's bits, rising, flying a tile at a time on the
  pad's word, steering, coming down where it may, landing -- is one module in
  `vehicles.c` ("In the air"), keyed by `Vehicle`, which both handlers call,
  with what differs (the Epoch's time gate, Black Omen, song and six tiles of
  ground; the dactyl's highest walls, palette and followers) said where it
  differs; the walkers' pieces moved into `party.c` and `terrain.c`.
- The menus: the scroll bar's task (`$C2:8BB8`, and `$C2:8C33` after its
  wait), whose frame (`menu/held_cursors.c`) was C already, a loop of the
  tasks' own (`menu/tasks.c`) that waits for the next frame as the main and
  the cursor's tasks do, the wait run as C; the top layer's fast drift
  (`$C2:77F2`, `drift` taking the velocity); the equipment screen's preview
  (`$C2:A5C3`, `$C2:A5E7`, `menu_equip_try_on`): the item tried on and the
  figures it would change marked up or down, one try-on with the shop's
  (`menu_try_on`, `menu_mark_the_figures_tried_on`, `menu/figures.c`; the
  windows' tables of figures `menu_data.h`'s accessors), called as C by the
  item step's bridge; and the Day of Lavos's two calls into the Epoch's map
  view (`$C2:6400`, `$C2:65B2`), the pieces `epoch.c` already had, now
  registered with the registers the scene's script reads after.
- The scene program: the eight-plane tile expansion (`$7E:5EF8`), one
  function with the title's four-plane one (`scene_expand_plane_tile`), and
  the endings' loop over the Mode 7 picture's 164 tiles (`$7E:5E3C`) as C,
  writing the code patches the program's state keeps.

**How the menu's turn leaves the carry.** The frame wait (`$CD:3E75`) used to
call the original's turn, which runs the menu's frame (`$C1:1153`) and the
cursors' (`$C1:17DD`); the cursors pass the frame's carry on where no cursor
is placed, and the frame's own carry is declared don't-care on library
evidence (its registry note). Run as C, the turn leaves the port's carry, and
the waits' comparison at their return found it differing at 13 of their 43,351
calls in the six movies that fight Lavos or in the Black Omen. Liveness has it
read after the waits, through the same battle messages' frame waits into the
same multiply, so the two waits declare it with the same kind of note (`ct_dev
--poison-return`, re-made by the gate's `--evidence`): 11,814 returns of the
wait for the next frame (8,753 of them to a bridge) and 339 of the wait for
some frames with the carry inverted, and every movie's trace the same. The menu's frame's own note
counted its returns to the original's turn, which is gone, so
`--poison-return` now also inverts a port's registers where a battle bridge
runs it as C and goes on with them (`native_poison_sibling_return`,
`battle/bridge.c`): 54,108 of the frame's returns, every one to a bridge.
A bridge may set the registers again at once (the step dispatch does), so
such a return tests less than one to the original's code: `ct_dev` counts
them apart, and each note says how many of its returns were to a bridge.

**What shadow mode and fuzzing found.** Over the 36 movies, 0 mismatching once
the waits declare the carry: the menu's turn 37,511 calls (26,407 compared on
their own, the rest inside the frame wait's); the step of the poses 3,008, the
calling off 94 and the opening 38, compared on their own; the line's joining
(588), the poses (47), the victory (25) and the defeat (2) span frames or run
inside the step dispatch's stretch, which compares them; the dactyls 313 and
626 calls; the scroll bar's task 9,890 and 8,761; the fast drift 139; the
eight-plane expansion 148 of 164 (16 interrupted); the Day of Lavos's two 1
and 2 (one interrupted). Shadow mode and fuzzing found the poses' member
counter a word (`battle_party_pose_member`), the victory's palette copy
reading A's high byte after the frame's tail (the port goes on as the original
from there where it is set, which the game never does) and the calling off's
carry read by the step's asker; a loose follower's offsets breaking with the
leader's spot off the map (the guard now asks it on), and the turn's joining
reading A's high byte. **Fuzzing** (seed 1, `tools/fuzz_routines.toml`): 0
mismatching everywhere, and against a build of the last commit the same counts
for every entry the chunk changed (the frame wait and its callers, the
opening, the item step, the timers' dispatch, the scroll bar's start, the
title's expansion): the menu's turn 3,000 of 3,000, the joining 3,955 of 5,000
(the rest past the budget), the calling off and the step of the poses 5,000
each, the poses 3,000 up to their first wait; the victory and the defeat 2 of
3,000 each, the rest past the first call-out's budget (TODO.md); the dactyls
5,000, 3,000, 4,939 and 5,000, and the followers loose 10,310 of 20,000 (the
guard refuses the rest); the scroll bar's task 20,000 and 20,000; the fast
drift 2,000; the Day of Lavos's two 300 and 2,000; the eight-plane expansion
2,000, and loose 1,978.

**Measured.** The shipping mode over the movies: 4.6 KiB of code and 0.21 M
instructions in place of hand ports, from 8.4 KiB and 0.89 M; over the routes,
counted over the frames they keep, 0.09 M from 0.63 M (the Mode 7 tiles' and
the menu's turn's lines); movies and routes together 5.6 KiB and 0.30 M from
9.6 KiB and 1.52 M, with `C1:0012`, `C1:369A`, `C1:0042`, `C2:6400`-`C2:65B2`,
`C2:A5E7`, `7E:5EF8` and the dactyls' lines gone and the menus' code after a
wait (`C2:----`) down to 64 K. What leads now is that line, the swept window's
rider (`7E:3A97`, 46 K), the healing's digits (`C2:9093`, 29 K), the list's
cursor (`C2:EA53`, 24 K) and the attract program's frame tasks (`7E:66B0`,
`7E:63D5`, `7E:64BF`). The reference play: 12.82 M from 13.90 M over the 36
movies, 14.0 KiB from 18.0 KiB. The baseline takes the gain, with two new
lines: the scroll bar's task handing back at the menus' wait for the frame's
uploads (`C2:8BB8`, `C2:8C33`, 41 and 2,521 instructions), an idle entry, where
its task is the frame's last to run, as the cursor's task does at `C2:E921`. No refusal
in real play; the native shape runs all 36 movies and reaches all ten routes'
outcomes. `--misses` lists nothing over `field-65m-bc-dactyl`,
`battle-lavos-gameover`, `late-omen-battles`, `gato-battle`,
`menus-shop-equip`, `menus-field-menu`, `field-12kbc-1f4`, `title-start` and
`endings-beyond-time`, and `--px-check` reports every frame of the nine
identical. One new entry is past the uncompared check's floor, the line's
joining (588 calls, all inside the step dispatch's stretch), with its reason
in the baseline.

**After the review.** The review (one per merge) found no file a
translation and eleven lapses, all fixed: the scroll bar's task stopped at
its JSR to the task switch, a port, where it now waits through `tasks.c`'s
loop wait as the main and the cursor's tasks do (its kept flags declared and
tested, `tests/test_menu_task_waits.py`), which took its fallback lines from
1,129 and 8,761 instructions to the idle wait's 41 and 2,521; the following
dactyl's stray way faced before, taken from the map's scroll, is marked
scaffolding (its effects line gone, and ten added for the cells the slots
lie over, each read after a plain write); the equipment screen's try-on indexed its figures from the menus'
zero cell, which its guard and the item step's now refuse unless 0 (a fuzz
variant with the cell random: 1,997 of 2,000 mismatched before, all refused
now); the dactyl's and the Epoch's flight became one module keyed by
`Vehicle`; the poses use `animation.c`'s list reader and kept lists, writing
the lists once and the frames left as scaffolding, and one
`battle_give_the_party_poses` serves the opening, the victory and the
defeat; the try-on is one with the shop's and reads the windows' tables of
figures through accessors; the endings' Mode 7 tiles are a `GameState`
function, the dead Y write gone; the expansion returns nothing and the tile's
size is one definition (`scene_plane_tile_bytes`), its guard also refusing a
tile past low RAM (5 of 2,000 fuzzed states mismatched there before); and
`battle_restart_turn_wait` works the wait out from the full wait and hands it
back, where its callers wrote the cell for it to read. Shadow over the 36
movies, 0 mismatching; `fuzz_compare` against the reviewed commit's build,
seed 1, the same on all 33 of the overworld's entries and 83 of the 84
battle, menu and scene entries the fixes reach, the 84th the zero cell's
variant above; the don't-care check and the effects ratchet clean.

**Timing.** Calibrated (`--drifted`) from a shadow pass over the library: the
new entries -- the dactyls between 5,653 and 8,843, the scroll bar's task
4,530 and 4,690, the menu's turn 3,967, the line's joining 4,414 and calling
off 6,523, the poses 8,410 and their step 16,160, the victory 5,951, and the
eight-plane expansion 33,350 (its own accesses few beside the picture's bytes
it writes) -- and the entries whose work moved into or out of them: the frame
waits (1,971 and 2,492), the transfers and the return to the field that wait
through the turn (3,068, 8,963, and the long transfer 1, nearly all its time
the turn's, a part now), the turn's giving (6,685), the calling off a waiting
member (4,196), the main task (702) and the equipment screen's slot and item
steps (7,499 and 15,992, the item's with the preview inside; after the
review the scroll bar's task 3,245 and 3,448, its wait now a part, the item
step 14,276, the shop's try-on, compare and choosing, the members' tech
waits and turn's end, and the Epoch again). The defeat and
the Day of Lavos's two, with one or two stretches each, take what those
calibrate to (2,464, 30,988 and 4,408); the fast drift calibrates to the
drift's own value (2,766). No exception.

## Phase 4a, the shipping mode's fallbacks, chunk 6: the field's and the battle's line handlers whole, the fallback measure's waits and task switches, the endings' rider and the lists' cursor (`$C0:ECCC`-`$C0:ED13`, `$D1:FE97`-`$D1:FEF3`, `$7E:3A92`-`$7E:3B47`, `$C2:EA53`-`$C2:EA80`)

The field's, the battle's, the menus' and the scene program's streams together
("Phase 4a, the shipping mode's fallbacks, chunk 5", above, for the one
before), chosen this time by what the **reference play** still ran as the
original, the shipping mode's count being down to 0.30 M.

**How it was chosen.** The last gate's reference play ran 12.82 M instructions
of the original's code. Past the sound driver (4.1 M, which PLAN.md keeps for
the audio work) its largest lines were the field's frame wait
(`C0:00BA`, 2.50 M), the battle's line handler (`D1:FE97`, 1.32 M), the menus'
code with no call on record (`C2:----`, 1.45 M), the field's line handler
(`C0:ECCC`, 0.65 M) and the sprite-object mode's frame wait (`C2:046E`,
0.16 M). Taken apart by address, none was code a port lacked:
- `C0:00BA` was the field's frame wait's loop (`$C0:EC71`), an idle entry,
  counted three passes a frame where the measure means to count one (the
  pass that leaves), and the field's line handler's exit (`$C0:ED0F`-`$C0:ED13`,
  1.04 M), which its tail's port, entered after the handler's pushes, left
  to the original from its first pull, where S rose above its entry;
- `C0:ECCC` and `D1:FE97` were the line handlers' waits for their line's
  hblank, idle entries, the pass that leaves each;
- `C2:----` was 1.08 M of the menus' switch into the next task
  (`$C2:8209`-`$C2:8215`), which the scheduler's ports do, counted from the
  switch on because a comparison's extent ended where S rose onto the next
  task's stack.
The shipping leftovers TODO.md listed came after: the swept window's rider
(`$7E:3A97`, 46 K in both counts) and the lists' cursor (`$C2:EA53`). The
healing's digits and `$C2:A518`, `$C2:A7B7`, `$C2:A970` turned out to be C
already (below).

**The measure.** Two fixes to `dev/fallbacks.c`, each making it count what
its definition says (above, "The fallback measure"):
- a wait loop's pass that an interrupt cuts is kept aside while the handler
  runs (the handler's own idle loops have passes of their own) and goes on
  where the handler returns to it, so it is taken back when it comes round,
  as an uncut pass is; the field's frame wait now counts exactly its frames'
  leaving passes (5,494 over `field`, from 16,482);
- a comparison's extent goes on across a switch of stacks (S rising further in
  a step than an instruction pulls) until the runtime says the comparison is
  over, as an interrupt entry's already did.

**The line handlers whole.** The field's (`$C0:ECCC`) and the battle's
(`$D1:FE97`) line handlers were each a head that stopped at the wait for
the line's hblank, an idle entry on `tm_snes_video_status`, and a tail from
the wait's exit. Each is one port now, from the interrupt to its `RTI`, as
the menus' vblank handler became one in chunk 2: the line is let be drawn
through a new timing call, `tm_wait_line_drawn` (PLATFORM.md, "Timing without
a beam, as built"), then the picture ends and the time to vblank goes to the
frame's job. The SNES backend waits through `native_wait_loop` with a new
wake, `NATIVE_WAKE_HBLANK`: in the shipping mode the machine steps where the
port stands to the hblank's start and then lets a pass of the shortest such
loop go by (`IDLE_EXIT_CLOCKS`, as an idle entry's exit does), and where it
found the blank begun later than that (the line's HDMA holding the processor
into it) still the pass's tail after its read, the `AND #` and the `BEQ`
(`POLL_TAIL_CLOCKS`, 24), so the forced blank lands no earlier than the
original's could and the picture's last line stays whole; in shadow mode the original's loop runs and the port goes on at its
exit. A host has nothing to wait for. The field's job on its even frames,
the dialog's pointer and the changed tiles (`$C0:ED15`), and the battle's,
the alternating frame (`$D1:FE4C`), run as C timed as their own, where they
were jumps into their entries through the machine. The handlers keep their
stack, so their comparisons end at the `RTI` without the interrupt flag.
`ct_fuzz --line-handler` raises the line interrupt and puts the line in its
hblank, as a line handler finds the machine (nothing advances time there).

**The rider and the lists' cursor.** The program's sprite list with signed
offsets (`$7E:3A97`, `scene/sprites.c`, `scene_draw_signed_sprite_list`):
each sprite's X and Y a signed offset from the scene's whole-word offsets, its
size a bit of its attributes that isn't kept; a sprite left of the picture
shows with X's 9th bit, one at or past the right edge goes below the
picture, small, its X left as it was, and one above or below the picture goes
below it. The swept window's task draws the rider with it as C; the entry
stays for the program's four other callers, which no movie reaches. The
lists' cursor (`$C2:EA53`, `menu/lists.c`, `menu_keep_the_cursor_on_the_list`):
a list that scrolls leaves the cursor to its screen, which scrolls where the
push took it past the rows, and keeps a held Start repeating fast; a list that
doesn't wraps the cursor round its places. The four screens' steps run it as
C, and what the original leaves its callers, the push or the page in A and
N, is scaffolding worked out from the state (`menu_leave_cursor_kept_on_the_list`).
The fast repeat's request is one function for the three screens that ask it
(`menu_ask_for_a_fast_repeat`).

**What it found and left.** The healing's rising numbers (`$C2:9093`) and the
equipment screen's slides (`$C2:A518`, `$C2:A7B7`, `$C2:A970`) are C, but a
wait for the next frame in the middle of a step is a call-out to the task
switch, whose callee moves to the next task's stack; where that lies above the
yielding task's, the runtime takes it for the callee unwinding past its return
and abandons the port, in both modes, and the original runs the rest of the
step when the task goes on (`menu_raise_amount_kind` abandoned 6 of 6 over
`menus-field-menu`). Keeping such a call-out means knowing when the task will
never go on, or a routine stays suspended for good: TODO.md. The field's and
the sprite-object mode's frame waits stay idle entries, though the battle's
waits wait inside their ports: the main thread is in its wait at nearly every
frame end there, and a port waiting inside would hold a routine suspended
across each, which ct_drive's bases and ct_play's saves can't hold (TODO.md).

**What shadow mode and fuzzing found.** Over the 36 movies, 0 mismatching:
the field's line handler 217,911 calls, the battle's 440,647 (1 interrupted),
the rider's list 112 and the swept window's task 785, the lists' cursor 6,405
on its own inside the screens' steps; its five call sites' stretches compare
the rest. The battle's handler first mismatched at its call to the
alternating frame on A's high byte, which the original's wait leaves 0 and
the port's own wait doesn't: run as C, there is no call to compare. The
rider's list first left the work RAM port's bank byte as 0 where the routine
writes `$7E`. **Fuzzing** (seed 1, `tools/fuzz_routines.toml`), 0 mismatching:
the field's line handler 20,000 (half of them the field's own, the rest the
handler handing back), the battle's 20,000; the rider's list 11,247 of 20,000
and loosely 5,600 (the guard refuses the rest); the lists' cursor 19,982 of
20,000, the rest a list whose last place is before its first, which the
original wraps round for ever and the port ends.

**Measured.** The reference play: 7.33 M from 12.82 M over the 36 movies, 13.6 KiB from 14.0 KiB: the field's frame wait 0.63 M, its leaving passes alone (from 2.50 M), its line handler's and the battle's lines gone (0.65 M and 1.32 M), `C2:----` 0.34 M (from 1.45 M), the sprite-object mode's wait 95 K (from 0.16 M), and the rider's and the lists' cursor's lines gone; the baseline takes the gain with no new line. The shipping mode over the movies:
4.6 KiB of code and 0.18 M instructions in place of hand ports, from 0.21 M; over the routes, counted over the frames they keep, 0.04 M from 0.09 M; together 0.23 M from 0.30 M. What leads now is the menus' steps' rest after a mid-step wait (`C2:----` 64 K, `C2:9093` 29 K, `C2:A518`, `C2:A7B7`, `C2:A970`), the attract program's frame tasks (`7E:66B0`, `7E:63D5`, `7E:64BF`) and the first script step's tail (`7E:3749`).
No refusal in real play; the native shape runs all 36 movies and reaches all
ten routes' outcomes. `--misses` lists nothing and `--px-check` reports every
frame identical over `field`, `gato-battle`, `menus-field-menu`, `menus-shop-equip` and `endings-beyond-time`. The vblank check found the field's forced blank
a line late on 6% of its frames where the hblank was seen behind the line's
HDMA and the whole exit pass added on top; charging only the pass's tail
there, it lands on the reference's line in every frame. The battle's late one behind the masked
frame work keeps its exception, now under `battle_irq`.

**Timing.** Calibrated (`--drifted`): the line handlers (the field's 3,356,
the battle's 4,076), the rider's list 8,904 and the swept window's task
94,416, the lists' cursor 3,188, and the screens' steps that run the cursor
as C (the equipment list's pointing 5,163, its item choice 16,184, the
shop's choices, the items' list and picked item, the techs' members).
One exception (`tools/timing_exceptions.txt`): the swept window's task's
worst stretch, 64,755 clocks once in 785 (the limit 50,468), its window's
work as unevenly timed as before but its stretch no longer split at the
rider's call; and the techs' choosing's vblank-tail exception goes, within
its limit now.

## Phase 4a, the shipping mode's fallbacks, chunk 7: the sound driver's CPU half, its waits for the SPC700 in place (`$C7:0000`-`$C7:0A97`)

The fallbacks' stream (history/reimplementation-chunks.md, "Phase 4a, the
shipping mode's fallbacks, chunk 6", for the one before), chosen by the
reference play: of its 7.33 M instructions run as the original, 4.4 M were
the sound driver's CPU half waiting for the SPC700.

**How it was chosen.** The last gate's reference play had the counter
exchange (`$C7:09DA`, 3.07 M) and the boot's byte loop (`$C7:00B0`, 1.03 M)
first and second, and `C7:----` (0.29 M) sixth. Taken apart: the first two
were the leaving pass of the echo waits the ports stopped at, idle entries,
a pass for each of the 1.5 M messages and 0.5 M boot bytes; `C7:----` was a
song's upload in a menu task run on as the original, after the menus' vblank
handler, which moves to a stack of its own, made the runtime take the
upload's call-out for its callee unwinding (`menus-shop-equip`, 252 K) and
the fallback measure end the extents under it.

**The waits on the SPC700, in place.** The audio service has four calls more
at the protocol level (PLATFORM.md, "The CPU half's waits, as built"): a wait
for an echo, for port 0 to move on, for an echo given up after the original's
count of polls (true where it came), and the exchange that ends a message,
which the original makes by calling `$C7:09DA` or `$C7:09EA`. The SNES backend
waits with the runtime's own machinery: `native_wait_loop`, a new
`native_wait_loop_or_give_up` for a loop with a second exit, and
`native_wait_call`, on a wake for port 0 (`native_wake_port0_shows`,
`_leaves`). In the shipping mode the machine steps a pass of the original's
poll at a time (42 master clocks, 66 for a poll that counts down) and the port
goes on when port 0 answers, or the polls run out; after an exchange it also
lets the routine's return pass (102 clocks, `native_wait_time`): the SPC700
reads a message's last two bytes after it echoes, and without that the next
message's port 3 overwrote every third byte of a song's sequence (found by
comparing sound memory after the title's song, then by the lockstep). The
rest is the port's own time, which has to put a message's bytes where the
original's go: its loads of them through the long pointer, which the port
doesn't make, are charged before each byte (`ACCESSES_A_LOAD`). Without that
the next byte reached port 3 2-3 SPC700 cycles after the SPC700 read the
last in 784,592 of the library's 1.16 M messages; with it, 182, and 8-15
cycles in all but 8,577, as the original's (`ct_dev --apu-port-margins`, a
measure at every echo; PLATFORM.md has the numbers). In shadow mode the
original's loop or routine runs, and the port goes on where the original
leaves it; a wait on the SPC700 doesn't go on after an interrupt
as a wait on the handlers does, and doesn't take the exit the original left
the last wait by (a loop that gives up leaves where it goes round to send
again). Nothing the runtime lacked but these.

**The command as one port.** `game/core/sound_driver.c` is rewritten. Every
wait is the port's, so a command runs whole from `$C7:0004` to the driver's
exit: the chain of entries that went on after each wait, the idle entries
and the call-outs to the exchange are gone, 43 registered entries down to 9.
The logic is C over the game's state through `vars.h` (the effects' set, the
echo's delay and page, the first sample's slot named in `re/ram.toml` for
what they are, and the lists' ends as a variable of their own), the
cartridge through the `GameState` accessors the C driver already used
(`core/sound_data.h`'s `Snes *` duplicates gone), and the cells only the
original's next instructions read (where it reads in the cartridge, its work
word, its index and the song's lists) as scaffolding. The slots are a small
module (a slot's block is its byte; the word's high byte the original keeps
is the guard's alone), the protocol is messages on a `Link`, each step naming
the original's site of its exchange, and the data a resource streamed by its
index (`SoundBytes`, a block, a sequence, a set of effects, a song's
percussion), the original's long pointer derived from it where only shadow
mode reads it. The commands read as what the game asks: a command as it is, a
song (its command, the slots it doesn't use freed, the kept ones moved down,
the missing loaded, its directory entries, tunings, envelopes, the effects
where due, its sequence and percussion), command `$70`'s switch of the
effects and `$71`'s sample. Three routines that wait inside themselves --
a song's command (`$C7:0A39`), a block's upload (`$C7:0655`), the effects'
upload (`$C7:0922`) -- are called as C like the rest, inside the original's
frame for them: shadow mode meets the original at each wait with S and the
stack compared, and the original has the routine's return address there and,
for two of them, the flags it pushed, which no port can derive.
`IN_THE_ORIGINALS_FRAME(g, bytes, ...)` (`game/original_frame.h`,
scaffolding the native shape compiles out) lowers S by the frame's bytes for
the routine's work and declares them dead (`native_frame_enter`,
`native_frame_leave`), so the waits in them meet the original's where it
stands. The three stay registered, for the original's own calls (where the
command's guard turns a state away) and to fuzz each alone; the command's
stretches compare them otherwise (`tools/uncompared_baseline.txt`). The boot is three
entries: the wait for the boot ROM (`$C7:0050`) and the countdown
(`$C7:011B`) stay idle entries, now leaving the flags as the original's pass
does; between them the whole upload is one port. A guard on the command
(`sound_driver_slots_are_kept`) turns away slots the driver never keeps:
blocks past a byte, a length short enough to be a block's number (the
directory's search runs on past the slots into their lengths), a hole among
the slots in use, a block in two slots more than once; real play never makes
one.

**The runtime and the measure.** A call-out under an interrupt whose handler
moves to a stack of its own (the menus' vblank handler) is no longer taken
for the callee unwinding, nor a port's own stretch under one for the port
leaving: `Active.handler_away` holds until S is back where the interrupt was
taken. The command needs the second: a song started in a menu task uploads
within one port now, its stretches cut by that handler, and without the rule
the rest of the upload ran as the original under its caller's handback
(`C2:CBEB`, 89,680 instructions of `$C7` code in `menus-shop-equip`). The
fallback measure keeps the extents under such an interrupt the same way
(`dev/fallbacks.c`, `handler_away`). Both reach past the sound driver: the
healing's rising numbers (`$C2:9093`) are compared past their waits for the
next frame where those fell under the vblank handler, and code the measure
counted as `C2:----` and `FF:----` is filed under the extent it runs in. A wait
on the sound processor meets the original at its loop's head before an exit
(the exit before that is where the last wait on the loop left): over the 36
movies against `main`, no wait's interrupted count rose, so none was entered
past its head. What moved is the sound driver's (the command 103 interrupted
to 262, its stretches the whole command now, and 283 abandoned to none; a
block's upload 430 interrupted to none, its 1,026 calls compared inside the
command's stretches) and about forty menu calls cut by that handler, counted
abandoned now rather than interrupted (neither compares the call). The vblank check's shipping runs play the C sound driver
in lockstep beside the original's, so the waits made in place are checked
where they are made.

**What shadow mode and fuzzing found.** Over the 36 movies, 0 mismatching:
the command 13,974 calls (261 interrupted: a song's upload spans frames and
a vblank cuts one of its stretches; every exchange before it is compared);
the song's command (607 calls), a block's upload (1,026) and the effects'
upload (72) run inside the command's stretches, which compare them, so their
own entries count their calls abandoned; the boot's three entries 37 each;
the sound lockstep identical in every movie, in shadow and in the shipping
mode. **Fuzzing** (seed 1, `tools/fuzz_routines.toml`, `--apu-loopback`), 0
mismatching: the command 6,818 of 20,000 (13,163 refused by the guard, 19
skipped where a wait for port 0 to move on runs out of budget, as the
loopback never moves it), 20,000 with the slots as the title's song leaves
them and 17,742 of 20,000 in its `refusals` variant (2,258 skipped so); the
song's command, the effects' upload (in both variants), the boot, its
upload, its finish and its two waits 20,000 each; a block's upload 19,682 of
20,000; and the healing's raising, whose timing moved, 20,000 and its kind
9,872 (the rest refused by its guard). The loopback confirms every header and wants every
song, so the `refusals` variants have the SPC700 answer otherwise on port 1
now and then (`ct_fuzz --apu-port1-flips`, which flips bit 7 of the run's
first eight reads of port 1 as a random byte of work RAM says): a header it
doesn't confirm (`send_header`'s count again), a song it doesn't want, the
moves it turns down (`forget_every_slot`), which the movies reach seldom or
never. Against `main`'s ports, the block's and the effects' uploads, the
boot and its finish agree (0 and 0); the two idle loops went from 20,000
mismatching (a pass declaring its flags dead, which `ct_fuzz` now refuses) to
0. The command's and the song's command's extents changed (they stopped at
their first wait before), so their counts aren't comparable.

**Measured.** The reference play: 2.83 M from 7.33 M over the 36 movies, 11.0 KiB from
13.6 KiB. The sound driver's lines are gone but the boot's two machine waits
(`C7:0000` 222 and `C7:005F` 74, the passes that leave them): the exchange
(3.07 M), the boot's bytes (1.03 M), `C7:----` (0.29 M), the command's
waits (`C7:0004` 28 K) and the routines' (`C7:0A79`, `0A39`, `0A7B`,
`063D`, `08FF`, 9 K). Outside `$C7` the lines fell by 78 K (2.91 M to
2.83 M), none of it code that stopped running: the shadow runs' original
runs as it was recorded whatever the ports do, so what moved is where the
measure files it. With the measure's fix, the menus' `C2:----` (0.18 M from
0.34 M) is filed under the extents its code runs in (`C2:F657` 69 K,
`C2:E921` 68 K, `C2:821E` 23 K, `C2:83CA` 6 K, `C2:8C33` 3 K, `C2:8A45` 2 K
and five lines under 1 K), and `FF:----` (0.13 M from 0.17 M) the same way:
the record task's callee in bank `$FF` (from `FF:F56B`, 32 K) under the
task's handback, `C2:F657`, and the gate screen's (1.9 K a movie) under
`C2:CE86` in one movie and under its port's comparison in two, the
instructions at those addresses as many as before. The healing's rising numbers are
compared now (`C2:9093`, 29 K, and 14 K more of `menus-field-menu`'s). The
rest, 31 K over the movies that show text and menus, runs under ports'
comparisons where the measure counted it before (in `fair`, the glyphs
`C2:5DC4` draws: 45,468 instructions there before, 45,528 now, 1.4 K more
of them under `menu_text_go_on`'s), and `C3:0E29` rose by 3 K (the attract
demo, `scene_find_divisor`'s skipped work). At the first review the
fall was 28 K, as `C2:CBEB` then held 33 K of the sound driver's own code: a
song's upload in a menu task, run on as the original after the menus'
vblank handler cut the block's comparison, which the rule for a port's own
stretch keeps under the command's now (13 instructions left there). `open
C7:0004` is 102,816 where `open C7:0655` was 7,904: the attract demo ends
during a song's upload, which the command carries out whole, so the
original's upload so far lies under its open comparison, where before the
block being uploaded was a comparison of its own, open since its last
exchange (25,688 at the first review, once that waited in place). The
shipping mode over the movies: 0.14 M from 0.18 M instructions; over the
routes, counted over the frames they keep, 0.04 M as before; together 0.18 M
from 0.23 M (the shipping mode played the CPU half's waits as idle entries
already, so what moved there is the menus' call-outs no longer abandoned
under their vblank handler). No refusal in real play; the native shape runs
all 36 movies and reaches all ten routes' outcomes. The lockstep beside the
vblank check's shipping runs is identical in every movie.

**Timing.** Calibrated (`--drifted`): the boot's upload 6,543, the command
3,251, the song's command 5,576, a block's upload 4,486, and two of the
menus' rows the runtime's rule moved, `menu_play_cursor_sound` 3,089 and
`menu_raise_amount_kind` 9,014. The command charges what the original does
at length and the port in a step (`charge.h`): each byte's load through the
original's long pointer (`ACCESSES_A_LOAD`), which also puts the bytes on the
ports where the original's go (the margin above), and its searches of the
slots and of the song's list, a word a step (`ACCESSES_A_COMPARE`). Its one
rate then fits all its stretches, at most 9,069 clocks off in vblank (the
limit 12,617), and it needs no exception. Nor does `menu_raise_amounts`: the
healing's frames are its kind's (`NATIVE_TIME_AS_SIBLING`, as the original's
call of `$C2:9047` yields between them), and the kind is calibrated over its
264 stretches rather than its first six. One exception is new, the main
menu's opening (`menu_choose_on_main_menu`, worst: 67,208 clocks off, once
in each of two movies), a stretch the rule for a port's own stretch lets be
judged (TODO.md).

## Phase 4a, the shipping mode's fallbacks, chunk 8: the runtime's waits across a task switch and a frame's end, and the scrolling lists' frame (`$C0:EC60`, `$C2:0346`, `$C2:8190`-`$C2:81CF`, `$C2:F657`-`$C2:F699`, `$C2:EA81`-`$C2:EAB9`)

The fallbacks' stream (history/reimplementation-chunks.md, "Phase 4a, the
shipping mode's fallbacks, chunk 7", for the one before), chosen by what the
runtime couldn't hold rather than by a routine: a port that waits for the next
frame where the runtime can't keep it waiting.

**How it was chosen.** The last gate left two kinds of wait to the original
(TODO.md's "What the shipping mode's fallbacks' sixth chunk left"). A menus'
step that waits for the next frame in the middle of its work (a call to the
task switch, `$C2:821E`) was taken for its callee unwinding wherever the next
task's stack lay above the yielding one's, and abandoned: the original ran the
rest of the step though it is C (the healing's rising numbers, the equipment
screen's slides `$C2:A518`, `$C2:A7B7`, `$C2:A970`, most of `C2:----`). And
the frame waits the main thread is in at nearly every frame's end (the
field's `$C0:EC71`, the sprite-object mode's `$C2:0348`, the menus' `$C2:81CA`)
stayed idle entries a port stops at, since a port waiting there would be
suspended at every frame end, where ct_drive takes its bases and ct_play its
quick saves; the reference play counted the pass that leaves each as a
handback (0.63 M, 0.10 M, and 0.09 M under the menus' task ports that stop
there).

**A yield, parked.** `native_yield_call_dc` makes the call to the task switch
with the caller's answer to whether the task can still go on
(`NativeGoesOn`). The runtime takes the routine off the routines in progress
(a parked list beside them, its machine copy with it in shadow mode), so the
other tasks' ports run and finish above nothing of it, and puts it back on
top, in its call-out, when the machine is at the call's return with the
call's S: from there it goes on as from any call-out, compared at its next
point. It asks the answer at every frame's start and stack switch and drops
the routine where it is no (`menu_task_goes_on_from`: the task's record still
waiting with the stack its wait kept, or the task the scheduler is taking back
into it; a task stopped, as all are when the menus close, or started afresh
over the wait, never goes on), counted abandoned; a second yield to the same
return and S drops the first. Parked routines count in `native_suspended`
(`native_parked` alone). The menus make it through `menu_yield` and
`menu_wait_a_frame_from` (`game/menu/bridge.h`), which every screen's local
`yield` helper became. The fallback measure ends a parked port's stretch at the
yield and starts it again where it goes on (`NativeObserver.parked`,
`.resumed`), and an enclosing port's again where the comparisons nested in it
are set aside at its point (a task's loop's head reached from its wait entered
at the same S, which ended its stretch).

**A wait, held.** `native_wait_loop_held` (`tm_wait_for_handlers_held`,
`PF_LOOP_HELD(head, exit, set)`) is a wait whose continuation is the idle entry
at the loop's head: in the shipping mode (and outside the runtime) the port
stops there, as it did, and returns at once; shadow mode waits it through the
original's loop and compares what it does after it, and compares the stop at
the head with the set it declares there, which `tools/dontcare_check.py` checks
as a stop. The field's frame wait (`field_finish_frame`, and the main loop's
next pass after it from `$C0:00BA`), the sprite-object mode's (`$C2:0346`,
`$C2:046E`, its compare's flags declared dead at both entries) and the menus'
wait for the frame's uploads (`menu/tasks.c`: the frame's turns after it, timed
as `$C2:81CF`'s, where its guard would run them) wait so. The records' task
(`$C2:F657`) runs its loop's wait as C as the main task's and the cursor's do,
with an entry where it goes on (`$C2:F699`), rather than stopping at it.

**What comparing the steps' rest found.** The rest of each step after a yield
had never been compared: the runtime abandoned it, in shadow mode as in the
shipping mode. Five ports were wrong there, and the shipping mode now runs
them: the equipment list's opening left A for the cursor's sound as the
port's, where the original's description clears it when it starts typing and
its last load is the cursor's place; the item use's member choice loaded the
helping test's cell only where it refused, where the original loads it either
way and a healing saves the flags; the shop's tick after its list's slide
compared the places as bytes, where the original's accumulator is still
16-bit from the equipment screen's slide, so it compares where the cursor last
moved from with a byte the menus never write (`$82`, named
`menu_byte_the_mode_before_left`), and ticks again as the slide ends
wherever they differ (the shop tells a slide by the list's first row moving
where no page turned, `menu_page_turns`, asked before the move); the slot
erased in the load screen queued its label's record with a spare of 0 where
the original's is the summary's byte after it, which nothing reads (the
spare is scaffolding, as the description's uploads' are);
and the gate screen's start saved P raw. Two were the runtime's: a port's
flags after a yield are its own, and a routine that saves P puts them on the
stack, which shadow mode compared, so the flags of a P a port saves and
restores itself are compared where it restores them (`native_flags_saved`,
through `menu_save_p`); and a task's stack below what its wait keeps, which the
original's work popped, was compared once the wait moved to the main stack,
so the loops' wait declares it dead (`leave_the_task_s_stack_below_uncompared`).

**The tasks as the menus open** (`$C2:8190`, `menu_start_the_tasks` in
`menu/tasks.c`): the records cleared, the main task and the cursor's started
from their heads, then the first frame's wait as C.

**The scrolling lists' frame** (`$C2:EA81`, `menu_frame_the_list` in
`menu/lists.c`): a list's places, the rows it shows and the scroll bar's
track in, the paging's rows and last top, the cursor's last place, how far it
scrolls, the bar's span and its thumb's step out, the step what the CPU's
divider makes of it. The equipment, items, shop and techs screens call it as
C with their counts, rows and tracks, where they left the rows in the menus'
first cell and the track in the divider for the original's; and the bridges
call the routines around it as C too, the shop's list's frame (`$C2:DD40`)
and its list for sale (`$C2:DECC`), the techs' list (`$C2:BB50`) and member
rows (`$C2:BA4F`), which no longer call out.

**The block move's repeats.** The menus' block move stub (`$7E:9690`, a
`MVN` and an `RTL` in work RAM) runs its one instruction again for each byte
it moves, back at the entry with the S it entered with. Shadow mode took each
pass for a new call and nested a comparison of the stub a level a byte, until
the runtime's 32 levels skipped the rest (0.14 M instructions over the
library, 12 K more once the records' task's entry where it goes on stood a
level deeper under the records' draws). It starts no comparison there now
while the innermost comparison is the same routine's from the same S, in its
stretch: the port stands for it (PORTING.md, "Call-outs").

**Numbers.** The reference play, 2.83 M to 1.41 M instructions run as the
original (and 0.16 M under comparisons still open as runs end, 0.10 M of it the
attract demo's sound command as before, the rest the frame waits' last
passes): the frame waits' leaving passes (0.63 M, 0.10 M), the menus' frame
wait's under the tasks' ports (0.09 M), the records' task's hand-back
(0.08 M) and the steps' rest after their waits gone, and with the block
move's repeats no longer nesting, the three routines skipped at the nesting
limit (the stub's 0.14 M, the scene's divisor search `$C3:0E29` 0.34 M, its
resource copy `$C3:099C` 2 K). Some of what the records'
task's hand-back held was other tasks' code on stacks within the measure's
span, and is filed under `C2:----`, `FF:----`, `C2:CE86` and the held cursors'
hand-back now (`tools/fallback_baseline.txt` says which). The shipping mode
over the movies 0.14 M to 0.09 M, the routes 0.04 M as they were.

Shadow over the 36 movies 0 mismatching (the block move stub 39,382 calls,
the divisor search 2,095, each compared once and matched); the parked ports' continuations
compared from where they go on (the records' task where it goes on 11,299
calls, 11,296 matched; the field's frame wait 209,087, 209,044 matched; the
sprite-object mode's 31,765, 31,756; the lists' frame's entry 1,139, all
matched). Fuzz 0 mismatching on every entry this touched (28, among them the
new `menu_record_task_goes_on`, `menu_frame_the_list` and
`menu_start_the_tasks`), and after the review's fixes 133 entries of the
shop, techs, equipment, items, tasks and frame waits, 0 mismatching.
`tests/port/suspend_test.c` has the runtime's cases: a
yield parked and going on in the shipping mode and in shadow mode, a task
dropped for a frame, a task started afresh over its wait, a state saved with
the step parked that the original's code finishes once loaded (in both
modes, shadow mode 0 mismatching), the menus' own answer whether a task goes
on (`menu_task_goes_on_from`, over its records), and a held wait
nothing suspends at any frame end of, whose save state loads into a fresh
runtime and plays on identically. Misses none and px-check identical over the
seven menu movies. Timing: the entries the chunk moved
recalibrated (`--drifted`); four exceptions for three steps whose stretches after their waits run the slots'
window, the list's opening and the list for sale's making inline at those
ports' own rates, and one for the block move stub, whose own calls measure
4.6% slower than the rate the 36 ports that time it inside their stretches
fit (`tools/timing_exceptions.txt`). Gate: suite OK.

## Phase 4a, the shipping mode's fallbacks, chunk 9: six movies into the library, the refusals they found, Lavos falling to the planet, and the Epoch's dive's perspective (`$7E:53BB`-`$7E:5529`, `$7E:55F9`-`$7E:5653`, `$7E:5654`-`$7E:56EA`, `$7E:56EB`-`$7E:576D`, `$7E:5855`, `$7E:801F`-`$7E:8078`, `$7E:81C4`, `$C2:9D5B`-`$C2:9DAE`)

The fallbacks' stream ("Phase 4a, the shipping mode's fallbacks, chunk 8", above, for the one before), chosen by the movies:
five recorded from routes (docs/movies-wanted.md) and the tech sweep, which
waited on a refusal, each reaching code no movie ran.

**The movies.** `menus-sweep` (6424 frames), `menus-save-point` (3270),
`events-epoch-lavos` (7792), `battle-black-tyrano` (49219), `battle-guardian`
(25751) and `battle-tech-sweep` (118090), each made by its route
(tools/movies/routes/) on this branch's build: the routes reproduced the five
recorded on chunk 7's byte for byte, since chunk 8 moved only the shipping
mode's timing. With them the library leaves 40.9 KiB of live code unexecuted
(3.7 KiB unported), against 47.4 KiB without them (movies-wanted.md, Totals).
The tech sweep adds about six minutes of shadow run to the gate.

**What they found.** One port bug: `menu_slide_tech_list` (`$C2:B72F`) left
the sign clear in the P its tick saves, where the original's last load before
the queue's call is the upload record's place (`$FBE3`, sign set); fuzzing the
tick's call (variant `tick`, stopped at the sound driver) found it in 457 of
500 states, and none after. Since chunk 8 the slide's comparisons are set aside
at the main task's point, so the shadow runs no longer see it. And four guards turning away calls real play makes, each
closed by porting what it turned away rather than an allowlist line:

- `scene_build_perspective` (`$7E:357B`, 202 calls in the Epoch's dive). The
  dive puts its own routines at the program's call that starts the lines: from
  outside, the shell curving away (`$7E:801F`), each line's distance read off a
  curve the dive made as it began (`scene_shell_curve`, through the work RAM
  port: the step `$2C80` is a branch to read the next); from the cockpit, the
  shell above and below built in turn (`$7E:8042`), nearer two a line toward
  the top and farther two a line from the middle, the half above counting a
  frame on the camera. perspective.c now plans the lines once (`lines_of`:
  first line, end, first distance, how it steps) for the guard, the build and
  the leaves, so the race's and the endings' lines are the same plan's other
  cases.
- `scene_animate_the_palettes` (`$7E:37FA`, 2): the dive's sequence calls it
  with 8-bit A on a channel's page through work RAM's bank, where the word it
  keeps aside around the animation is two bytes of the race's code
  (`scene_code_under_the_multiplier`).
- `scene_setup_line_effects` (`$7E:36CC`, 2): the dive's table drives color
  math's control and layers as a pair a line, a target the video service had no
  name for: `VID_LINE_MATH_CONTROL_AND_LAYERS`, added (the SNES encoding and
  the observer's state).
- `script_return_from_call` (`$C1:56FB`, 24 in the Black Tyrano's fight, 8 in
  the tech sweep): the return at the top (`$C1:5722`) goes on into the plain
  return where the point is at the top of the screen, as the end unless listed
  and the throw that waits first already did into theirs (`goes_on_into`,
  `script_point_at_the_top`).

Refusals in real play: 0, over the 42 movies.

**Lavos falls to the planet** (scene mode 4, `scene/lavos_falls.c`): the setup
(`$7E:53BB`; Lavos's picture, its map widened to the plane's 128, the planet's
tiles laid out in a list's order, the colours, the scripts, the stars), Lavos
turning (`$7E:56A9`), the star field swinging round on an angle (`$7E:55F9`,
`$7E:5855`), the planet's edge drawn (`$7E:56EB`) and the two routines channel
1's script calls (`$7E:5640`, `$7E:564D`, through script.c's table of routines,
now one per scene file). The flat plane's turn and send (`$7E:56B4`,
`$7E:5654`) were beyond_time.c's; they are `scene/plane.c`'s now, Mode 7's
plane under channel 0's camera, which the Beyond Time ending and Lavos's fall
both use, and which also sends the dive's plane's scroll and centre in the
vblank (`$7E:81C4`). The widening of a plane's map (`$7E:37CA`), its blanking
(`$7E:90A7`), the stars' scatter bridge and the buffers' addresses
(`SCENE_UNPACK_BUFFER`, `SCENE_PLANE_MAP`) moved where every scene shares them.
Not ported: the pixels' spread (`$7E:350E`), which reads the packed bytes by
moving the stack onto them (docs/TODO.md). Shadow mode took S rising above an
entry or a call's return for a return or an unwinding, so the setup was
compared only up to the spread; it now tells a switch onto a stack of the
code's own (more than a page above, `NATIVE_STACK_SWITCH_DISTANCE`) from
either, and waits for S to come back (`note_stack_switch`; docs/PORTING.md,
"Call-outs"; `tests/port/shadow_test.c` has such a callee, abandoned at every
call without it). The setup is now compared to its end inside
`scene_program_start`'s stretch, which found the port's leftover of the work
RAM port pointing into bank `$7E` where the original writes the port's bank
0, since fixed. Fuzzing had found the setup's three resource copies taking the
program's byte places for the copier's entry numbers (300 of 300 states),
which shadow mode, blind past the spread then, hadn't.

**The Epoch's dive** (mode 6) stays the program's but for its perspective's
starts and its plane's place: the rest (docs/TODO.md) mixes whatever registers
the code before left into its random draws and animates line tables that are
bytes of the program's image. One consequence lands here: its sprite drawing
reads the flags the signed sprite list leaves (`$7E:3A97`), so that entry's
don't-care set narrowed to nothing and its wrapper works them out (the count
run down; the last sprite's last sum's overflow and its last test's carry,
which for a sprite past the right edge is a bit of its high-table byte from
before the list, read by the wrapper), and `scene_multiply`'s V is declared
live (the perspective's callers read it through it).

**The menus.** The equipment members' slide's start (`$C2:9D5B`,
`menu_start_sliding_equipment_members`) is a port, its slide's settings one
table as the techs screen's, and the two screens share `MemberListSlide` and
the decision which way a push slides the list (members.h,
`menu_member_slide_for_push`). Both starts run their slide where its own guard
holds and call the original's otherwise (`menu_run_or_call`, menu/bridge.h;
the slide's guard wants room for three uploads, which the start's doesn't
ask). The settings' button rows and window design (`$C2:C20F`-`$C2BC`,
`$C2:C5C7`) were C already; burndown didn't credit them because its reading
took the menus' bridge's `run` setting the PC for a hand-back
(dontcare_check.py's `BRIDGES` lists it now, as the scene bridge's
`run_entry` was), which moved `$C2` from 54.5 to 56.6 KiB counted ported.

**Numbers.** Over the 36 movies chunk 8 measured, the reference play is 1.19 M
instructions run as the original (1.41 M before; no port changed, the drop is
comparisons followed further: the runtime across stack switches, and the
measure where it had dropped them at a callee port's entry after nested ones
were set aside), and the
shipping mode 0.07 M (0.09 M before). Over the five recorded movies the reference play went from 0.68 M (after
the guards; the Epoch alone had refused 0.37 M before them) to 0.49 M. Their
shipping mode is 2.32 M, nearly all of it the dive's (`events-epoch-lavos`, 2.31 M,
1.94 M of it code with no port): it measured 2.32 M before the ports and 2.18 M
after at the same estimates, and the recalibration over the 42 movies moved it
back, as it moved the endings' down, so over the dive it follows the estimates
more than the ports. With the tech sweep, the library's reference play is 1.66 M
and its shipping mode 2.40 M; the routes' shipping mode is 2.37 M, nearly all of
it the Epoch's route and 0.04 M the ten from before, as they were. Burndown: 254.8 KiB of 266.5 KiB to 257.3 KiB (11.7 KiB unported to 9.2, about 2.1 KiB of it the menus' credit above).

Shadow over the 42 movies 0 mismatching: the new entries compare 1,712 turns
and as many flat plane sends (2,687 with the Beyond Time ending's), 828 swings,
564 angles, 538 planet draws, the two routines' 3 calls, 1,619 plane places;
the closed guards' entries 2,909 perspective builds, 85,409 palettes' frames,
16 line effects' setups, 3,898 returns from a call; the equipment slide's
start 2. Fuzz 0 mismatching on every entry this touched (tools/fuzz_routines.toml:
the mode 4 entries, the dive's perspective (`cockpit`, `outside`), palettes
(`channel`) and line effects (`dive`, `cockpit`) variants, the return at the
top (`at_the_top`), the tech list's tick, the slide's start 2000/2000), seed 1.
Misses: none on the movies the ports touch but `events-epoch-lavos`'s, which
are the dive's own interpreted code; px-check identical over `battle-black-tyrano`,
`events-epoch-lavos`, `menus-sweep` and `endings-beyond-time`. Timing: 191 rows recalibrated (`--drifted`, over the 42 movies), the new entries among them, and the signed sprite list again once its flag leaves stopped counting their reads as its time (8,823 to 9,474); eight exceptions where the new movies took an entry's stretches in vblank past the limits' minimum or added one long stretch (the tile animations' load and its jump, the description's typing, the button rows, the effect slots' step, the signed sprite list in the dive, the save point's screen, the rewards shared with the reserve; `tools/timing_exceptions.txt`), and one exception dropped that the recalibration made unneeded. With the setup
compared past the spread, Lavos's fall's setup has a rate of its own (6962,
worked out from the scene start's stretches it runs in, where its work is a
part), and a stretch's room for the routines it attributes time to went from 48
to 128 (`MAX_SEGMENT_PARTS`): the field's frame, followed through a mode on
stacks of its own, reaches more.
Gate: suite OK.

## Phase 4a, the shipping mode's fallbacks, chunk 10: the Epoch's dive into Lavos, and the program's stirred draw (`$7E:78DD`-`$7E:7B9B`, `$7E:79F8`-`$7E:7A67`, `$7E:7BF2`-`$7E:7C2F`, `$7E:7EC7`-`$7E:7F04`, `$7E:7F12`-`$7E:8108`, `$7E:81F1`, `$7E:8205`-`$7E:838A`, `$7E:3443`, `$C3:02DD`, `$7E:38C3`)

The fallbacks' stream (this file, "Phase 4a, the
shipping mode's fallbacks, chunk 9", for the one before), chosen by the
shipping mode: after chunk 9 the Epoch's dive (scene mode 6) was 2.31 M of
the movies' 2.40 M instructions run as the original, nearly all of it the
program's main loop spinning on the dive's frame routine while its picture
waited for the vblank, and the rest the dive's tasks and the stir they draw
from.

**The dive** (`scene/epoch_dive.c`) is the whole mode now: its setup, the
frame's and the vblank's tasks of both views, the views' switches, the
routines its scripts call, and its frame routine, which the main loop's pass
runs as C (attract/main_loop.c takes it beside the race's and the endings'
scalings). The scene cuts between the cockpit -- the shell above and below
on the Mode 7 plane, the panel shaking, its warning lights and gauges'
needles flickering, a band of light ahead that opens at the end -- and the
view from outside, where the Epoch streaks into the shell as a trail of light
whose round head is the Epoch's scaled picture. Three things made it more than
a translation's work, each now named:

- **What the random draws are salted with.** The cockpit's draws go on from
  whatever the code before left: the panel's place across, the task's slot,
  where the sprite table's cursor stands in its group of four after the
  lights are drawn (`scene_sprite_place_in_group`; the guard holds the lights'
  last sprite on the picture, where that is all the drawing leaves), a
  warning light's picture, a gauge's place. The panel's lights draw with the
  data bank on work RAM, so the stir's reads of the beam land on two colours
  of the palette as loaded: `scene_draw_random_off_the_palette` takes them as
  its noise, which is what the program has there.
- **The line tables are the program's own bytes.** The tasks move their
  counts and sources as the panel shakes, the trail moves and the light
  opens; each is a variable in re/ram.toml (`scene_dive_panel_*`,
  `scene_dive_trail_*`, `scene_dive_light_*`), as are the bands of fog the
  setup makes, the trail's circle and window, the panel's lights' list and
  the pictures they take, and the Epoch's frames of animation as it leaves
  (a table of records from its first frame, which the port steps through by
  index; the program's pointer into it is decoded in one place).
- **The frame's task lists are copied whole** by a block move from the
  program (`$7E:7ED1`, `$7E:7EE8`), which the analysis didn't follow, so it
  knew no caller of the tasks they hold and liveness took every register as
  read after them. tools/disasm/derive.py now reads a block move into a task
  list from the program's own image as the tasks it holds.

Where the original's carry moves a pixel, a named function keeps it: the
trail's left edge (`trail_edge`, a pixel further right on each line after one
whose edge is on the screen while the Epoch is left of the screen's edge),
and the shell's wrap a pixel short where it passes its start
(`round_the_shell`). The guards refuse the states where a borrow or a carry
the game never makes would change the sum (the shell passing more than 32767
pixels round, a panel above line 167, the Epoch's point above its picture's
reach), so the rest is written plainly.

**The program's stirred draw** (`$7E:3443`, `$C3:02DD`) is a registered entry
too, for the code that still calls it with its registers; random.c's stir is
split at its noise so the palette's form shares it. Two of the program's
routines moved where scenes share them: the transfer from the scene's call
cells (`$7E:38C3`, `scene_send_from`, which keeps its parameters in the cells
plainly: the multiply carries in the byte after its product, where the count
lies) and the race's buffer clear through the multiplier
(`scene_clear_buffer`). What the script routines leave in the registers (A,
and for the dive's Y and the overflow, which the interpreter hands on to the
sound driver) is a table parallel to each file's routines, which the build
without the original's effects compiles out. Where the scenes repeated one
another, one operation now serves them: a bitmap handed to the scaler
(`scene_scaler_start`, the dive's, the endings' and the race's), the loop over
a picture's tiles (`scene_expand_tiles`, the title's two and the endings'),
the unpacked send in the way a setup chooses (`scene_choose_transfer`,
`scene_send_unpacked_by`), the way into the scaler's rows (bridge.c), and the
regrouping routine's writer, which points the work RAM port at the routine's
place in the program (`scene_matrix_code`) itself rather than taking the
cursor its caller left (it still writes through the port, as the original
does: the timing gate reckons a port write and a work RAM write apart).

**Three of the reference play's handbacks outside the dive**, each the
original's code going on past an entry that stood for the start of it: the
race's tail (`$7E:4BD2`, 228K instructions), where the ride's own code comes
to the engine's pitch every frame and the bikes' motion ran as the original
after it -- the entry is `scene_race_pitch_and_move` now, through the motion
to the routine's return, beside the ride's port that does the same in C --
and at every movie's boot the title loader's loop over the plane's tiles
(`$7E:5E3C`, `scene_expand_the_plane_tiles`, either expansion and its
stride as the loop's code holds them) and the palette made current
(`$7E:38AF`). The dive's draws stir in the scene's first call cell, which
the native shape had read stale (`tools/effects_check.py`'s runs): it has one
name as the stir reads it (`scene_call_stirred`), random.c lists what leaves
something there, and those writers go through its one writer
(`scene_leave_for_the_draws`): the scene scripts' interpreter the routine a
script calls, the palettes' frame task the word it keeps aside (`$7E:37FA`),
the dive's lights a warning light's picture and a gauge's place. The 8.8
divide leaves its dividend there plainly, from C and from its entry alike.

**Numbers.** The shipping mode over the 42 movies is 0.09 M instructions run
as the original (2.40 M before; the dive 2.31 M to 2,134, the 36 older movies
0.07 M as they were), the routes 0.06 M (2.37 M before; the Epoch's route
2.31 M to 2,134). The reference play over the 42 movies is 0.95 M (1.66 M
before; the 36 older movies 1.19 M to 0.89 M, of it the race's tail 228 K and
the title's tiles and palette at boot 82 K; the dive 0.41 M to 9,507, most of that its script's
waits for the sound CPU, which the shipping mode runs as an idle entry). What
the reference play still counts is mostly code no port stands for in the
menus, the field and `$FF` (0.50 M) and the attract demo's race set up inside a
stretch whose comparison is set aside (`$7E:3ECD`-`$7E:3F13`, 0.13 M; the
shipping mode runs it as C). Burndown: 257.3 KiB of 266.5 KiB to 258.8 KiB of
266.7 KiB (the analysis found 0.2 KiB more of the dive once it read the task
lists' block moves).

Shadow over the 42 movies 0 mismatching: the dive's 25 entries compare in
`events-epoch-lavos` (its frame routine's 362,203 passes, 241 interrupted,
and its gathering's 847, 201 interrupted, where the scaler's rows run across
frames; the tasks' 2,717 perspective checks, 1,098 shakes and panel
follows, 737 flickers, 521 and 1,098 passes of the shell, 204 Epochs drawn,
185 trails; the setup inside the program's start, to its end), the stir's
entries 2,950 and 2,948 (the rest inside the stars' and the curtain's latch
waits), the race's tail 4,363, the tile loop 46 and the palette 59. Fuzz 0
mismatching on every new entry (tools/fuzz_routines.toml, seed 1; two new
states, `dive_leaving` and `dive_breaking_through`): 0/300 the setup, 0 on
each task over 61 to 2000 states each compared (the rest the guards': the
list's carry, a place far round the shell), the stir 0/3000 in both forms.
Paths no movie reaches and only fuzzing checks: the Epoch's frame table
going back to its first frame, the light's table once the fog has reached
its brightest, a 16-bit entry into the tile loop (refused: the original's
first expansion then runs 16-bit). Misses: only the dive's two step
instructions the program rewrites into the perspective's loop
(`$7E:3634`, run interpreted in `--generated only`), as before; px-check
identical over `events-epoch-lavos`, `events-jetbike-race` and `title-start`.
Timing: the new entries and five that drifted calibrated (`--drifted`, over
the 42 movies; `scene_program_start` 4441 to 15362 now that the dive's setup
runs as C inside its stretch), no new exception; after the review's fixes
`scene_scale_the_epoch`, `scene_send_the_epoch` (the picture's mark a byte),
`scene_flicker_the_lights` (the gauge left as a byte) and
`scene_race_place_bikes` (the 8.8 divide leaving its dividend) again. Gate:
suite OK.

#### The native program through the field and the title

`ct_native` (PLAYING.md, "The native program") links the whole of `game/`:
the entry wrappers come along but nothing makes the `Snes` they take, and
what they call of the machine is `runtime/port/no_machine.c`, whose list
replaced `ct_native_whole`'s link failure as the measure of what `game/`
asks of a machine. Every service has a host backend (video into the
observer, audio playing the C sound driver, saves), so the program's
progress is a matter of its own C: it now runs the boot and the field --
the first start, location 0, the change to the next, their scripts, frames
and handlers -- and then the title's scene: the scene engine boots it and
unpacks its program, whose data the C reads where the original's code
would, and the program's start, the title's setup, its frames (the NMI's
frame work with both task lists, the sequence scripts' channels) and its end
run as C until the game goes back to the field (`scene_runs`). Left alone the
title gives way to the attract demo, which stops at frame 2093 where it
enters a world map (`$C2:0000`, the menus' sprite-object mode); with
`new-game.ctm`'s presses the field after the title asks for a menu, the
menus run as C through the battle mode's choice and Crono's naming, and the
field comes back and goes on to the world map at frame 3243.

How the ports were made to call each other, by kind:

- **An entry whose work calls nothing on the machine** became a function
  of the game's state, which the entry calls and the program calls too:
  the unpacker (`unpack_stream`, the last file on the transcription
  backlog, rewritten), the location's tile sets, looks and script, the
  map's loading, the vblank handler's sends, the line handler. Where a
  caller ran it through `field_run`, the caller now runs it as C, timed as
  its routine; shadow mode still compares it on its own, the original's
  entry into it starting a nested comparison.
- **An entry whose callees the machine pairs a call at a time** (the budgets'
  turns and frames, the song's calls to the driver, the screen's setup and
  fade in, the main loop's pass, the field's entry) keeps its bridge. The
  routine is one sequence of the game's state that both run, its steps
  callbacks: on the machine a step is called from the original's call, leaves
  the flags the original's callers read and hands what the port doesn't take
  to the original; as C it runs the step, or stops the program there. The
  budgets were first (`draw_queued_frames`, `run_queued_turns`); then the
  field's entry, the location's loading and song, the main loop's pass and
  the frame's work (each a table of steps, the entry and its C in two
  columns), the objects' update (`UpdateSteps`: the move, the view, the
  tick, the rows, the place, the window-only check) and view (`ViewSteps`,
  whose C now checks the view's guard too), and the pause and the menus
  (`MenuSteps`). So shadow mode compares the sequence `ct_native` runs. The
  ones still written twice are TODO.md's ("The native program"). The menus
  a script asks for followed in the same shape: the field's side (the fade
  out, the put-aside, the set-up again, the fade in after a menu,
  `StopSteps`), the menus' mode (`ModeSteps`), its opening (`OpeningCalls`),
  the save slots' reading, the records' loads, the vblank handler's two
  calls, the close (`CloseCalls`), the naming's steps (`NamingSteps`, the
  steps moved out of the bridge into `naming.c`), the sounds and the fades;
  and the tasks' bodies -- the main task's and the cursor's turns, the fade
  in, the typing -- each one function whose waits are callbacks: the
  machine's entry runs a turn, its wait the original's task switch, and the
  coroutine (`core/tasks.c`, one turn of a slot at a time,
  `tasks_take_a_turn`, for `menu/tasks.c`'s scheduler's order) runs through
  them, its wait coming back the next frame.
- **An interpreter's machine runner has a runner as C beside it**
  (`event_runner_as_c`): each command's operation, and for a bridge the C of
  its work where it has one (`EVENT_BRIDGES_AS_C`: the sound commands and
  the screen command so far).
- **Where the C meets a state its port doesn't take, or a step only the
  machine runs yet,** the program stops and names it
  (`program_needs_the_machine`), as a guard on the machine hands the call to
  the original.

#### The native program through the world map

`ct_native` (PLAYING.md, "The native program") stopped where the field
enters a world map: the locations from `$1F0`, which the menus'
sprite-object mode shows (`$C2:0000`). The attract demo met it at frame 2093,
`new-game.ctm`'s presses at frame 3243 (the opening's bells over Guardia).
The world map now runs as C: the mode's start, the map's setup, the
overworld's main loop, the mode's vblank and line handlers and the objects
they run, until the map's scene leaves for the field, whose start takes it to
the location the scene chose. The attract demo's first world map plays
through and the field goes on to frame 2551; the new game's opening plays
through into Crono's house, to frame 4798.

The shapes are the field's and the menus' (the section before this one,
history/reimplementation-chunks.md, "The native program through the field
and the title"), one sequence each that the machine's entry and the program
both run:

- **The mode's start** (`menu_object_mode_starts`) and **the map's setup**
  were functions of the game's state already; the setup's unpacker is a
  callback (`Unpacker`), now with a C one (`overworld_unpacker_as_c`) beside
  the machine's. The setup's guard has its state half
  (`overworld_can_be_set_up`), which the program checks first.
- **The main loop** (`overworld/main_loop.c`): a pass is one function over
  `LoopSteps`, the waits, the map's loads again, the screens the loop opens
  and the registers the original's code reads after a step being callbacks.
  The machine's entry runs a pass from the loop's head and stops where the
  pass ends (the head again, the field's start, the halt); the program loops
  over passes until the scene leaves (`overworld_runs`). The waits' sites
  and frames, and the loads' sites, are table rows, so the don't-care check
  reads them.
- **The vblank handler** (`$C2:015E`): its work is one sequence of six steps
  (the queue, the frame's input, the sprite list emptied, the sprites hidden,
  the objects run, the list drawn), each a call of the original's on the
  machine and C in the program, each with its guard's state half; the
  handler's line twin only returns. Both are rows of `core/program.c`'s table.
- **The objects' loop** (`$C2:051D`) takes a turn per slot through a
  callback: on the machine the original's `JSR` through the slot's handler,
  in the program a row of a C table (`menu/handlers.c`): the command scripts'
  interpreter, the screen's objects (the fades, the palette rows, the tile
  streamer, the mosaic), the layer over the map's drifts and wave, and the
  drifting map. A handler's guard is its machine half
  (`menu_object_handler_runs`) and a state half the row names
  (`menu_object_handler_takes`).
- **The command scripts** run as C on `menu_run_script` with a runner of
  their own: each command decoded by its row and performed where
  `menu_script_runs_here` takes it, and the services as C -- the map's scroll
  and redraw with their guards' state halves, the sound request passed on,
  and the routines a script calls by address, each a row
  (`MENU_SCRIPT_ROUTINES`) beside its entry in its own file: the scenes'
  routines (`object_commands.c`), their line effects, irises and curtain
  (`line_effects.c`) and the map scenes' (`map_scenes.c`). Where an entry's
  work was written into the entry, it moved into a function both call.

**Measured.** Shadow mode over the library: 0 mismatches; the entries whose
work became a sequence compare every call real play makes
(`menu_object_nmi` 35,503 with 3 interrupted, `menu_run_objects` 34,743 with
7, `menu_run_object_script` 392,650 with 20, `overworld_main_loop` 30,652
with 2, `menu_wait_for_frame` 33,776, `menu_wait_frames_sending_the_sound`
380). Refusals in real play: 0; fallbacks unchanged (the reference play 3.5
KiB, 554,128 instructions; the shipping mode 0.6 KiB, 18,033). The census:
259 entries the machine enters, as before -- the machine's entries are the
same, and what moved is that the program reaches them as C.
`tools/machine_joints.py`: 180 stops (181), 129 time tags that take the
machine (132), and the register escapes as they were (24: the scripts'
write of a picture register, which both runners make, moved into
`script.c` beside the test of it). Timing: no row recalibrated;
the frame wait's first draft read the frame count twice and drifted
+47.6%, which reading it once put back. A cartridge word the world map's
setup clears VRAM with (`$C2:26D4`) joined the catalogue of resources
(`menu_vram_clearing_zero`), as the menus' clearing zero did.

What the world map still hands the machine when the program runs it: the
walkers, the vehicles and the sign (the overworld's handlers, which only a
map the party walks runs), the map view and the time dial, and the menus
opened from the map, which go as far as the menus' own C does (TODO.md,
"The native program").

#### The native program through Crono's house to the fair

With the world map as C (the section before this one, "The native program
through the world map"), `new-game.ctm`'s presses took
`ct_native` into Crono's house, where it stopped at frame 4798: an event
command that casts a person whose graphics must be unpacked (`$C0:4590`).
Each step after it was taken as it was met, in the same shape -- one
sequence the machine's entry and the program run, its steps callbacks:

- **The casts of a person and an enemy** (`$C0:4590`, `$C0:46DF`): the
  casting's two halves either side of the unpacking, the unpacker a
  callback (`SetUnpacker`; on the machine the original's from each
  command's call, its sites and the too-far stops a table's rows), now
  bridges with C (`EVENT_BRIDGES_AS_C`).
- **A tile copy** (`$C0:1779`, a request): the screen's recentring a frame
  at a time, the copy, and the layers it changed drawn and sent, each call a
  row of a table (`tile_copy_calls`: the routine and the original's site),
  run by one sequence (`copy_the_tiles`) whose step is a call; on the machine
  the original's calls and the jump to the frame wait the pass goes on
  from, as C the ports' functions with their guards' state halves. The
  recentring and the leader's walk became one sequence each with the
  layers' scroll a callback (`walk.c`), which took the walk off TODO.md's
  list of steps written twice.
- **The world map walked on foot** (`overworld/handlers.c`): the overworld's
  handlers -- the walkers, the vehicles, the places' watcher, the music, the
  sign, the Epoch and the dactyls -- and the routines its scenes call (the
  song of where the party is, the sign's label, the Epoch's scenes), as rows
  of the sprite-object mode's C tables; the walkers' and the Epoch's scripts
  on the interpreter as C. Their guards' state halves moved here from
  `entries.c`, whose guards are now each the handler's machine half and the
  row's state half (`menu_object_handler_takes`).

So `new-game.ctm` plays to its end, and `fair.ctm`'s presses cross the world
map into Leene Square, the fair (location `$005`, where the reference play
of the movie ends), and on to the end of the run. The attract demo still
stops at frame 2551, at a screen effect only the machine runs (its curtain,
its close into a scene or its ripple, `$C0:21E1`).

**Measured.** Shadow mode over the library: 0 mismatches; the entries whose
work became a sequence compare every call real play makes
(`event_cmd_cast_npc` 971, `event_cmd_cast_enemy` 662,
`field_handle_requests` 238,533, `field_recentre_the_screen` 323,
`field_walk_the_leader` 244,514, `overworld_lead` 18,356 with 4
interrupted, `overworld_follow` 36,712, `overworld_start_the_location_song`
32). Refusals in real play: 0; fallbacks unchanged. The census: 259 entries
the machine enters, as before. `tools/machine_joints.py`: 179 stops (180).
Timing: no row recalibrated. The moved guards' reads of slot cells the
menus proper also use as scaffolding are four baseline lines of the same
kind as the ones they moved with.

#### The native program through the field's screen effects

With Crono's house and the fair as C (the section before this one, "The native program
through Crono's house to the fair"), the attract
demo still stopped at frame 2551, on its field after the first world map, at
a screen effect only the machine ran: `$C0:21E1`'s step had C for the
effects drawn in a frame (the holes and ovals, the searchlight, BG3 in a
circle) and handed the machine the three that call on the field's other
work: the curtain's sweep (bank `$C3`'s), the ripple and the field's close
into a scene, which run for frames with BG1 drawn, the effects refreshed,
frames waited and the field put aside between their steps. The C path
didn't check the entry's guard either (an effect past the table's, a
searchlight whose shape starts below the picture).

The step is one sequence now (`move_on_a_step`), its calls to the field's
other work callbacks (`EffectCalls`: the sweep, BG1's drawing, the refresh,
the frame wait, the battle's start, the scene's), each named by the moment of
the close or the ripple it is made at (`Moment`); the machine's callbacks
keep the original's call sites in one table by moment, the program's are the
ports' functions with their guards' state halves (`field_screen_effect_moved_on`
checks the step's own first). What it took with it, each one sequence the
same way:

- **The battle's start** (`$C0:024C`), which the close calls to put the
  field aside: the enemies found, and with none the party brought back
  together through `event/party.c`'s port and a frame of its walk, a
  callback (`field_battle_set_up` as C, with its guard's state half and the
  party's).
- **A frame of the leader's walk alone** (`$C0:00EB`): the walk, the
  refresh and the wait, three callbacks (`field_walked_a_frame`).
- **A layer drawn as at a map's load** (`$C0:75E9` and its two siblings),
  as C with each entry's guard's state half (`field_layer_drawn_at_the_load`,
  which the tile copy's C path now calls too: it checked BG2's half for all
  three).

The scene's callers of the palettes' animation (`scene/screen.c`'s task and
the Epoch's dive's channel call, `scene/rising_palace.c`'s setup) go through
`palette_animated_as_c`, the guard's state half every caller on the field
already checks; and the scene lists' runner as C stops where a task's C
went no further.

So the attract demo's field closes into its scene as C, and `ct_native`
stops at frame 2584, at that scene's setup (the credits' page,
`$7E:6B04`). `fair-companion.ctm` and `gato-battle.ctm` stop at frame
12832, at the fair, on event `$D2` (`$C0:38E8`), which only the machine
runs.

**Measured.** Shadow mode over the library: 0 mismatches
(`field_move_screen_effect_on` 237,748 of 238,260 compared, the rest
abandoned as before; `field_walk_a_frame` 690 of 696, 6 interrupted;
`field_battle_begins` 48, every one interrupted as before: its call-out
waits across frames; `scene_animate_the_palettes` 93,828). No movie reaches
the ripple or the close on the machine, so those paths are checked only by
`ct_native` getting through them. Refusals in real play: 0; fallbacks
unchanged; the census: 259 entries the machine enters, as before.
`tools/machine_joints.py`: 1,055 returns (1,056). Timing: the palettes'
check is a peek, so the scene's task takes none of its time; the step and
the pass's step after it recalibrated (`field_move_screen_effect_on` 3484
to 2901, `field_frame_goes_on_with_the_screen_effect` 2095 to 2525). The C
guards' reads of the curtain's table and the leader's contact are three
baseline lines beside the ones the same cells already have. Fuzzing, seed
1, `tools/fuzz_compare.py` against `main`'s `ct_fuzz`: the four entries'
eight runs (the step's curtain, curtain-call and close-ripple variants among
them) the same as before. Gate: suite OK.

#### The native program to the battles

With the field's screen effects as C (the section before this one, history
file, "The native program through the field's screen effects"), the attract
demo stopped at frame 2584, at the setup of the scene its field closes into
(a page of the credits, `$7E:6B04`), and `fair-companion.ctm` and
`gato-battle.ctm` at frame 12832, at the fair, on event `$D2`. Each stop
after them was taken as it was met, in the same shape:

- **The credits' page's setup**: one sequence (`set_up_the_page`) whose
  unpacking of the letters is a callback, the machine's the original's
  unpacker from the setup's call, the program's the unpacker as C; the scene
  program's setups as C are a table (`c_setups`) beside the machine's.
- **Events `$D2` and `$CF`** (whether a character is in the active party, or
  recruited): what the menus' service answers and where the script goes
  after it are functions both the entry and the program call
  (`among_the_record`, `after_asking`), the entry keeping the original's
  scaffolding around them; as C, bridges in `EVENT_BRIDGES_AS_C`.
- **Events `$E1` and `$DF`** (a change of place made at once): the place
  set, a frame waited, the screen blank and the place changed, shared with
  the machine's two entries either side of its wait; and the field started
  again from its start, which with no machine is the program's to do: a
  request the field's loop takes as its pass ends
  (`program_starts_the_field_again`), as a scene is.

So the attract demo plays on through its credits page and the field after
it to its first battle (frame 3605, `$C0:188C`); `fair-companion.ctm` plays
to its end; `gato-battle.ctm` stops at Gato's battle (frame 19179). The
battle is what every one of them meets next.

**Measured.** Shadow mode over the library: 0 mismatches
(`event_cmd_if_in_the_active_party` 366, `event_cmd_if_recruited` 12,
`event_cmd_go_to_location_now` 42 and `_raising_1e` 38,
`event_go_now_in_the_dark` 42, every call compared;
`scene_credit_pages_set_up` 10, all abandoned under the setups' dispatch as
before). Refusals in real play: 0; fallbacks unchanged; the census: 259
entries the machine enters, as before. `tools/machine_joints.py` unchanged
(1,055 returns, 179 stops, 2,099 scaffolding writes). Timing: no row moved
past its limits. Fuzzing, seed 1, against `main`'s `ct_fuzz`: the six
entries' eight runs the same as before. Gate: suite OK.

#### The native program through the battle

With the credits' page and the changes of place made at once as C (the
history file, "The native program to the battles"), every way `ct_native`
went stopped at the battle's entry, `$C0:188C`: the attract demo at frame
3605, `gato-battle.ctm` at frame 19179. The battle is now a mode of the
program's: the field's request for it (`fight_a_battle`, field/requests.c)
fights it as C (`battle_fought`, battle/from_field.c) and takes the field up
again after it (`field_taken_up_after_battle`, field/battle_return.c), and
the battle's three interrupt handlers are rows of `core/program.c`'s table
(`battle_vblank`, `battle_line`, `battle_closing_vblank`;
battle/program.h).

Every routine on the way is one sequence with two sets of callbacks, as the
field's were (the history file, "The native program through the field and
the title"): the registered entry is a thin wrapper that hands the sequence
`*_on_the_machine` callbacks, which make the original's calls from their
sites with the registers it sets up; the program hands it C callbacks, each
calling the routine's C under `TIMED_AS` once its entry's guard's state half
(a `const GameState *` function the guard calls too) agrees, and otherwise
stopping at `battle_needs_the_machine`. What the original leaves in its
registers for its callers is the machine callbacks' or the wrappers' to set,
worked out from where the sequence went (`leave_cursor_registers`,
battle/menu_window.c), never noted inside the logic. Where the C took a
machine's register (`aim_the_action`'s carry), it is a parameter.

- **The start and the end** (`battle_fought`): the field put aside, the
  battle's setup (`set_up`, battle/opening.c, with its waits for the
  graphics and the window's slide made as loops on the timing service's
  frame), the screen and the display set up, the main loop, and the way
  back: the last frames, the window closed, the display closed and the
  field's music back.
- **The main loop** (`battle_main_loop_runs`, battle/main_loop.c): the
  original's passes as sequences (`next_frame`, `run_pass`, `take_turns`,
  `finish_beaten_enemies`) over one table of calls (`LoopCall`), each
  returning where the original's loop goes on (`LoopGoesOn`). The battle's
  steps (battle/steps.c) have a C column beside the machine's, as the
  field's and the menus' tables do.
- **The frame** (battle/frame.c, nmi.c, wait.c): the vblank's work, the
  frame's dispatch and update, and the waits for the frame, the handler and
  a number of frames, as loops the native host answers a frame at a time.
- **The turns**: the members' (battle/member_turn.c, party_turns.c, the
  menu's line and window, the aim), the enemies' (enemy_turn.c, with each
  action's turn a row of a table naming its routine), the action shown
  (action_showing.c, action_animation.c, scripts.c, screen_script.c), the
  beaten enemies, the rewards, the party's poses, and the battlefield changed
  and put back.
- **The field taken up again** (`$C0:0283`): the party and the enemies put
  back where the battle kept them, the map, the window frame, the tile
  looks, the script and the map's script, the chests and the text area, a
  frame at a time; then the party back together. A game over and the place
  put aside fade out and start the field afresh; the program takes the
  second as a request (`program_starts_the_field_again`) and stops at the
  first.

So `gato-battle.ctm` plays to its end (frame 21600), Gato's battle and the
field after it as C, and the attract demo goes on through its battle to
frame 7519. A screen script's wait for a line (command `$C1`, `$CD:1D93`),
which the attract demo's battle has, is the timing service's wait as C; the
world map's layer shimmering (`$C2:7759`) is a row of `menu/handlers.c`'s
table; and the jet bike race's setup (`$7E:3B48`), the scene the attract
demo goes on to, is one sequence (`set_up_the_race`, scene/race.c) whose
song, unpacking and first script step are callbacks, a C setup in
`scene/program.c`'s table beside the title's and the credits' page's. The
race's frame tasks are next: machine entries in `scene/frame.c`'s table with
no C column. The new guards' state halves read the cells the
original's callers hand them operands in only on the machine
(`operands_fit`, battle/aim.c): the C passes them as values.

**Measured.** Shadow mode over the library: 0 mismatches. The census: over `gato-battle.ctm` the same as before (143 entries the
machine enters, the battle's as entries whose callers are still the
machine's, since ct_dev runs the registered entries); over the library, 278
(the tree's, measured afresh). `tools/machine_joints.py`: 177 stops (179 before) and 80 time tags
that take the machine (129); 503 video calls (497), the C paths' own.
`tools/effects_check.py`: the renamed functions' lines carried over, with
reasons, and none where a guard reads what only scaffolding writes.
Timing: three entries recalibrated (`battle_place_menu_cursors`,
`battle_load_action_animation`, `battle_step_enemy_effects`, whose sequences
read a flag or two more than the shapes before them), and
`battle_frame_dispatch` in `tools/timing_exceptions.txt` (+0.42% against a
limit of 0.38%, +0.36% before). Fuzzing, seed 1, 2,000 states, `tools/fuzz_compare.py` against `main`'s
`ct_fuzz`: the 37 runs of the 28 entries the sequences changed most the same
as before, and `scene_race_set_up` and `menu_object_wave_hdma` the same.
Gate: suite OK.

#### The native program through the attract sequence

With the battle as C (the history file, "The native program through the
battle"), `ct_native` alone stopped at frame 7519, in the attract demo's
jet bike race, set up as C, at its first frame's tasks (`$7E:5221`): the
race's tasks were rows of `scene/frame.c`'s table of the tasks whose entries
take the machine, with no C column. Each has one now, and the next blocker,
the rising palace's setup (`$7E:671F`, mode 3), the scene the attract demo
goes on to, is a C setup beside the race's, so the attract sequence runs as
C from the boot round to the title again and on: `ct_native` alone ran
200,000 frames without stopping.

The table's C column is a `SceneTaskInC` (scene.h): the task's frame on its
list's slot, returning how it ends (`TaskEnd`, as the tasks that are C
throughout return), so the list empties a slot or hands it over as C; one
that needs the machine ends `TASK_LEFT_TO_THE_ORIGINAL` through
`scene_task_needs_the_machine`. The C function takes the routine's plain name
(`scene_race_ride`), and the registered `Snes *` wrapper is `*_entry`.

- **One sequence per task.** Where the task's frame calls the original's
  code on the machine, the work is a `*_with` sequence taking the call as a
  step: the curtain's step (`sweep_curtain_with`, scene/race.c; the palace's
  `sweep_the_curtain_with`), the perspective's build
  (`check_perspective_with`, race_camera.c), and the race's own sound effects
  (`ride_with`, `coast_with`, `wait_to_leave_with`, race_rules.c, through
  `race_send_sound` and a `RaceSends` step: `race_sound_on_the_machine` from
  the race's call of the program's send, `race_sound` the send's port). The
  machine wrapper hands the sequence `*_on_the_machine` steps and sets, from
  the `TaskEnd`, what the list's code reads (the slot handed over in X, A's
  high byte); the rest are the same logic function called by both.
- **The guards' state halves.** The ride's and the coast's room on the lists
  (`ride_lists_fit`, `coast_lists_fit`), the bikes' placing
  (`bikes_placeable`), the drawing (`drawing_fits`), the perspective's lines
  (`scene_perspective_fits`, perspective.c, split from
  `scene_perspective_as_ported`, whose code hashes stay the machine's half)
  and the curtain's table (`scene_curtain_stepped`, curtain.c, the step as
  the scenes' C paths take it) are `const GameState *` functions the guard
  and the C path both call; the other tasks' guards are their code and page
  alone. The list's slot is the curtain stir's salt, as X is on the machine.
- **The palace** (scene/rising_palace.c): its setup is one sequence
  (`set_up_the_palace_with`) whose scripts' first step is the step, a row of
  `scene/program.c`'s C setups, and its curtain's sweep the race's shape.

**Measured.** Shadow mode over the library: 0 mismatches. Fuzzing, seed 1,
`tools/fuzz_compare.py` against the branch point's `ct_fuzz` over the race's
and the palace's entries (and the perspective's build): the 42 runs the
same as before, none mismatching and the guards' refusals unchanged.
Timing: every entry within its limits, none recalibrated. `tools/effects_check.py`: the field's two lines for the
curtain's guard (`sweep_the_curtain`) moved to the shared step
(`scene_curtain_stepped`), which the race and the palace call too; none new.
`ct_native` with the movies' presses goes as far as before
(`menu.ctm`'s stop at the main menu's opening, `$C2:99CE`, is the first
stop left). Gate: suite OK.

#### The native program through the main menu

With the attract sequence as C (the history file, "The native program
through the attract sequence"), the first stop left was the menus the player
opens: `menu.ctm` stopped at frame 7417, the main menu's opening (`$C2:99CE`),
a step of `menu/screens.c`'s table with no C. The chunk followed the movie:
each stop named the next routine, until the movie played to its end (frame
8900; 30,000 frames without a stop). The stops on the way, in order, were
the main menu's opening and choosing, the equipment's opening (`$C2:9C31`),
the items' (`$C2:AA40`), the scroll bar's task (`$C2:8BB8`), the records'
task the techs' panel starts (`$C2:F657`), the techs' opening (`$C2:B35D`)
and the settings' (`$C2:BF3D`).

Every step is one sequence, the shape the rest of the tree has: the
registered entry is a thin wrapper (`*_entry` where the C takes the plain
name) handing a `*_with` sequence its `*_on_the_machine` steps, and the
program hands it C steps. A screen's steps are a struct of callbacks
(`MainMenuSteps`, `EquipmentSteps`, `ItemsSteps`, `TechsSteps`,
`SettingsSteps`): the cursor read, a tick from a named place (an enum the
bridge's table turns into the original's call site), a frame's wait, the
siblings the original reaches by a JSR (the panel, the member card, the
description, the moves among the techs' members, an option's handler), the
way back to the main menu, and the notes where the machine leaves what the
original's tests left. As C the siblings are their C under `TIMED_AS`, the
ticks and the cursor read `menu_tick` and `menu_read_the_cursor_for_a_step`
(menu/cursor.c), a wait `menu_task_waits`, and the way back
`menu_back_to_the_main_menu`; a sibling with no C yet (a list's slide, the
slots' window, the techs' lister, the settings' meters, the exchange's and
saving's panels) stops at `program_needs_the_machine` naming its routine.

- **The guards' state halves.** Each C path checks what its entry's guard
  takes of the state, a `const GameState *` function the guard calls too:
  the main menu's (`menu_main_menu_can_open`, `_can_choose`,
  `_can_come_back`, `_panel_can_show`), the panels'
  (`menu_equipment_panel_can_show` and the items', techs' and settings'),
  the screens' (`menu_equip_can_set_up`, `_can_choose_a_member`,
  `_can_point_at_a_member`; `menu_items_can_open`,
  `_can_point_along_the_list`, `_can_sort`; `menu_techs_can_open`,
  `_can_move_among_members`; `menu_settings_can_open`, `_can_choose`,
  `menu_settings_option_can_be_taken`), the card's
  (`menu_member_card_can_be_drawn`), and the two tasks'
  (`menu_scroll_bar_can_be_drawn`, `menu_records_can_run`). The upload
  queue's room, which three bridges each worked out, is one function
  (`menu_upload_queue_takes`, menu/vram.c).
- **The table.** `menu/screens.c`'s rows have a C column beside the machine
  entry; the naming's row is `menu_naming_step`, which reads the step it is
  on, as the other screens' rows would. `menu/tasks.c`'s coroutines have the
  scroll bar's and the records' bodies: each frame's work timed as its entry,
  then the wait, while its guard's state half holds.
- **The machine's side.** Where the original's code saves P or sets the
  widths between the steps' calls, the wrapper does it round the sequence, a
  wait's table row sets them for the wait (the main menu's panels), or, in
  the items', techs' and settings' sequences, the note at that point does
  (TODO.md, "The menus' steps' notes carry machine work").

**Measured.** Shadow mode over `menu.ctm`: the changed entries 0 mismatched.
Fuzzing, seed 1, `tools/fuzz_compare.py` against the branch point's
`ct_fuzz` over the 31 entries the sequences changed (35 runs, the settings'
with their second variant): every run the same as before, none mismatching
and the guards' refusals unchanged. `tools/effects_check.py`: the eight
reads the bridges' guards and steps made of cells written plainly moved with
them to the shared functions (their baseline lines renamed), and the records'
guard, once a static the check didn't see, has its two lines with why;
`tools/dontcare_check.py`: none. `ct_native` with the other movies' presses
and alone goes as far as before; from a cartridge save,
`saves/menus-field-menu.ctm` stops at the load screen's opening
(`$C2:E613`), the next menus to give a C path. Timing: every entry within
its limits, none recalibrated. Gate: suite OK.

#### The native program from a cartridge save

With the main menu as C ("The native program through the
main menu", above), every power-on movie played to its end in `ct_native`, and every
movie that starts from a cartridge save stopped at frame 1692 on the load
screen's opening (`$C2:E613`), the first step of `menu/screens.c`'s table it
meets with no C. The chunk followed the save movies from there.

**The load screen.** Its four steps (the opening, choosing, which game a New
Game+ carries on from, the erase question's answer), its drawing (`$C2:E7C3`)
and the slot under the cursor shown (`$C2:E743`, save_screen.c) are each one
sequence through `LoadScreenSteps`, the main menu's shape: the cursor read, a
tick from a named place, the slot shown, the screen drawn, the New Game+'s
question, the load and the carrying over, the erase, the sound output's
command, the notes, and the step's end (the fade in, or the menus left for
the game). The machine's answers are `load_screen_bridge.c`'s (each sibling
run as the original's JSR reaches it, the driver's command a call out, the
ends the jumps the steps stop at); the one piece of machine work between
calls, the byte-wide registers the original's code sets after a choice, is a
callback of its own (`byte_wide`), a no-op as C. As C each sibling checks its
entry's guard's state half where the step has moved the cursor or the slot:
the slot shown (`menu_load_screen_slot_can_show`), the drawing
(`menu_load_screen_can_draw`, which peeks save memory), the load
(`menu_save_code_can_run`) and the carrying over (`menu_carry_over_can_run`);
the steps' own are `menu_load_screen_can_choose` and
`menu_erase_can_be_answered`. A slot's erasing (`$C2:E8B7`), which no movie
reaches, stops at `program_needs_the_machine`. The slots' rows' drawing
leaving no slot shown (its count past the last) is a plain write now: the
screens set the slot shown plainly before anything reads it, and the 21
effects-baseline lines that answered the scaffolding write went with it.

**Saving.** The main menu's panel for it (`$C2:D065`), both save screens'
steps (a save point's, `$C2:D4F8` and `$C2:D506`; the main menu's,
`$C2:CEDC` and `$C2:CEFD`; the question before a game is saved over and the
step no code moves to, `$C2:CF61` and `$C2:CF92`), saving in the slot
(`$C2:CF0A`), the save up to its last wait (`$C2:CF37`) and the slots' frame
as the cursor moves among them (`$C2:CFA2`, save_screen.c) are each one
sequence through `SavingSteps` (saving_bridge.c the machine's side): the
panel's waits, the save's sound (a call out on the machine), its frame and
the summary read back, the layout over the panel and the slot saved shown
are steps of their own, each as C checking its entry's guard's state half
where the step has moved the cursor or the slot. The save's last wait,
which the original jumps to so the task goes on from it into the save's
caller, is the machine's frame around the call as before and a frame's wait
as C. The main menu's panels as C take saving's (`menu_show_save_panel`).

**The party exchange.** The main menu's exchange (its opening `$C2:C627`,
the first and second choices `$C2:C65D` and `$C2:C6B9`), the gate's first
choice (`$C2:C7E0`), the screen's layout (`$C2:CAF3`), the cursor's move
(`$C2:C805`) and the panel (`$C2:C9E5`) are each one sequence through
`ExchangeSteps`. The machine's work between the original's calls is steps
of their own: the register widths (`widths`), and the frames of the two
routines the reserve's slide waits inside, which the machine keeps on its
stack round the slide's frames (`slide`); as C both are no-ops. The cursor's
move and the layout are siblings each step re-checks the guard of as C. The
gate's screen's opening (`$C2:C7AC`: its song's task, the swirl, the gate's
sprite on its record's page) stays on the machine, and as C the panel's
gate sprite and the place's song back stop at `program_needs_the_machine`.
The held cursors' tasks (`$C2:89F2`, `$C2:8A45`, `$C2:8AB2`, `$C2:8AF5`:
the cursor held on a list's item, on a member, on a slot, and on each
member) are `menu/tasks.c` coroutines now, each frame's drawing timed as its
entry while its guard's state half holds, as the scroll bar's.

**The game over's start afresh.** After a game over the original goes dark
and jumps to the field's first start (`$C0:000E`) on a fresh stack: the
title, then the load screen again. As C the field's taking up after the
battle blanks the screen and tells the program
(`program_starts_afresh_from_the_title`, core/program.h), and the main
thread, as the field returns, runs the field's power-on start
(`field_power_on`) again, as at power-on, and goes on.

**The field's frames on a host.** The first save movie past the load screen
(`saves/battle-golem-twins-gameover.ctm`) hung: an object holding its frame
(layout 12) answers the frame queue's drawing with "wait" until the next
frame, and the original asks again until the beam ends the frame's time,
which a host's `tm_frame_time_left` never does. The C path's drawing ends
the frame there (`FRAME_HELD_TO_THE_NEXT`, field/frames.c); the machine's
asks as before.

**Measured.** Shadow mode: the save screens' entries compared in
`saves/menus-field-menu.ctm` and `saves/menus-save-point.ctm` (the slots'
frame 504 calls, the question 209, saving in the slot 3, the save itself
abandoned at its last wait as before), and the exchange's and the held
cursors' over the movies that open them (the cursor's move 2,270 calls,
the choices 2,292, the held cursors' tasks 3,898), none mismatched. Fuzzing, seed 1,
`tools/fuzz_compare.py` against the branch point's `ct_fuzz` over the 17
load screen and slot entries the sequences changed (with their
`new_game_plus`, `at-the-sound` and `narrow` variants) and the 13 saving
entries, the 15 of the exchange and the held cursors: every run the same as before, none mismatching and the guards'
refusals unchanged. `tools/effects_check.py`: none new. `ct_native` from a
cartridge save, with each movie's presses: 20 of the 40 save movies play to
their end (none did); the rest stop at a routine a world map's script calls (`$C2:26A8`,
three), the time gate's tunnel (`$FD:DA5F` and kin, four), the gate's
exchange (`$C2:C7AC`, two), the time dial and the map view (`$C2:6A34`,
`$C2:631F`, three), a scene's routine (`$7E:3F59`, two), a step on an
event's tile (`$C0:0C76`, three), the equipment's slots window (`$C2:A71E`),
an item moved on the items' list (`$C2:AAC8`) and a shop (`$C2:D52B`).
The four that lose a battle on purpose go through the game over to the
title and on. The power-on movies and
the attract sequence go as far as before. Timing: one row recalibrated,
`menu_answer_whether_to_save_over` from 5,434 to 4,473 (its save now
reached through the sequence's step). Gate: suite OK.

#### The native program through the field's stops: switches, the turning world and the tunnel

From a cartridge save `ct_native` played 20 of the 40 save movies to their
end (the history file, "The native program from a cartridge save"). Ten of
the rest stopped in the field, the world map or the scenes: three at a step
on an event's tile (`$C0:0C76`), three at a routine a world map's scene calls
(`$C2:26A8`), and four at the time gate's tunnel the field ends into
(`$FD:DA5F` and its kin). The chunk followed each movie from its stop until
it played to its end.

**A switch stepped on, and a chest opened.** The requests' step event
(`$C0:0D45`-`$C0:16DB`) and chest (`$C0:16DC`) are one sequence each
(`answer_step_event_with`, `answer_chest_with`, field/requests.c) through
`StepAndChestSteps`: the switch's sound and the chest's are steps, and so is
what the machine leaves after each answer for the original's later calls (A's
high byte for the chest's sound, the overflow), which as C is nothing. The C
path checks the requests' entry's guard's state half first
(`field_requests_can_be_answered`, split from `field_requests_can_run`), and
each sound its own entry's (the leader one the port reads).

**The Day of Lavos's turning world.** The three movies that lose to Lavos
(and the Beyond Time ending, which shows the same) play the 1000 A.D. world
map's scene that lays the map flat in Mode 7 and turns it. Its object's
script calls seven routines by address, each a port already: all of VRAM
cleared (`$C2:26A8`), the map's tiles loaded (`$C2:27DE`), its colours
sampled and its picture laid out as the map view does (`$C2:6400`,
`$C2:65B2`), the view's backdrop (`$C2:68F4`), and the world drawn, started
turning, turned a frame and its turn handed on (`$C6:E7FA`, `$C6:E90F`,
`$C6:E9F5`, `$C6:EE23`). Each is a row of a module's `MenuScriptRoutines`
table now (`menu_map_view_routines` in menu/epoch.c,
`menu_turning_world_routines` in menu/turning_world.c, and the overworld's
and the scenes' own), with the state half of its entry's guard as the row's
`takes` where the guard has one (the tiles' load, the world's drawing and
turning: each split out of its guard). The script runner clears the carry
before it reads the routine's result, so what the entries leave in X and the
carry is the machine's alone.

**The planet seen from space.** The scene after it ("But... the future
refused to change.", `$7E:6248`) is one sequence through `PlanetSetUpSteps`
(scene/planet.c): a resource unpacked, the scripts' first step and the stars
put out are steps; as C the unpacker, the camera channel's turn and the
stars' port. Its three tasks have C columns in scene/frame.c's table: the
caption (the drawing's guard's state half, `scene_caption_fits`, split out
of scene/captions.c), the wait for a button (one sequence with the send as
its step; as C it checks the list's room for the darkening), and the
countdown, which on the machine jumps to the console's reset and as C asks
the program for it (`program_resets_the_console`, core/program.h): the
scene's loop ends there, and the main thread runs the boot and the field's
power-on start, as at power-on.

**The time gate's tunnel.** The field's event that ends it into scene
`$80`-`$83` goes on to the warp as the program's next step, as to a scene
(`field_scene_starts`); the main thread runs the trip (`warp_runs`,
warp/warp.c): the warp started and the screen turned on, a frame made after
each the vblank handler takes (`warp_vblank`, a row of core/program.c's
handlers now), and at the last the screen dark and the story flags put back,
then the field's start for the place the event set. The pieces are the
machine's entries' own; the loop between them is the original's code on the
machine and `warp_runs` as C (TODO.md, "Two sequences of one routine").

**The screen setup's frame drawing.** An object holding a 12-sprite frame
never draws it, and the original's screen setup (`$C0:B109`), which asks
again until the frame is whole, would ask for ever; as C
(`field_frame_part_drawn`) the drawing ends there, as the frame queue's does
on a host. No movie reaches it: the original would hang.

**Measured.** Shadow mode over the library, none mismatched: the requests'
entry 238,533 calls, the warp's vblank 1,691, the planet's caption 1,098
and its wait for a button 970, the turning world's frame 249 and its turn
handed on 234, the map's tiles 67 (the world's drawing, its start and one
tile load interrupted, each once; the countdown's last frame is the reset,
which no movie's shadow run compares). Fuzzing, seed 1,
`tools/fuzz_compare.py` against the branch point's `ct_fuzz` over the 16
entries the chunk changed (25 runs with their variants: the requests'
switches, chest and step-and-chest among them): every run the same as
before, none mismatching and the guards' refusals unchanged.
`tools/effects_check.py`: none new (seven lines renamed with the turn's
handing on, one for the frame's guard's state half reading the turning
object's tilt, which its start writes plainly). `ct_native` from a
cartridge save: 30 of the 40 save movies play to their end (20 did): the
three switch movies, the four that end through the tunnel
(battle-lavos-gameover, endings-bucket-lavos, endings-telepod-lavos,
events-end-of-time-gate), and the three through the turning world, the
planet and the reset (battle-tech-sweep, endings-beyond-time,
late-lavos); the other ten stop where they did. The power-on movies and
the attract sequence go as far as before. No timing row moved. Gate:
suite OK.

#### The native program through the Epoch's screens and the race's opening

From a cartridge save `ct_native` played 30 of the 40 save movies to their
end (the history file, "The native program through the field's stops").
Five of the rest stopped outside the menus proper: three where the world
map's loop opens the Epoch's time dial or its map view (`$C2:6A34`,
`$C2:631F`; events-epoch-lavos, menus-shop-equip, menus-epoch), and both
race movies at the routine the jet bike race's first script calls to pick
its opening (`$7E:3F59`). The chunk took those stops.

**The two screens, one sequence each.** The map view and the time dial
(`show_the_map_view_with`, `show_the_time_dial_with`, menu/epoch.c) run
their Mode 7 setup, their builders and their loop until the menus' mode
leaves them through `EpochScreenSteps`: VRAM cleared, the map view's marker
glyphs loaded (`$C2:6965`), its plane built from the world map's tiles
(`$C2:63DF`), the gate built (`$C2:7389`) and each of its three streams
unpacked (`$C2:71F9`), the typesetter's start and call, a frame waited, frames
waited while the sound's requests go on, and what starting a script leaves in
A. On the machine each is the original's call at its site (a table of sites
the machine's steps alone read); as C each is the port, behind the state
half of its entry's guard (the map's tiles' load, the slot the dial's
graphics count in, the typesetter's two). The builders the screens call are
sequences too (`load_map_view_graphics_with`, `build_map_view_from`,
`build_the_dial_with`, `load_gate_stream_from`), their registered entries
passing the machine's steps or its unpacker, so the screens and their
entries share one copy. Two loads of A's low byte the old wrappers made
before waits that read only its high byte went: liveness has them dead.

**The screens' objects.** The world map's loop runs each object's handler
as a row of a C table (menu/handlers.c); the screens' seven -- the map
view's cursor and its three markers, the dial, the gate's spin and the
digits -- are rows of `menu_epoch_handlers` now, each with its guard's state
half split out of the machine's guard (`cursor_takes`, `dial_takes`,
`spin_takes`, `marker_takes`, `digit_takes`). The dial's buzz, sent on the
machine from the handler's own call, goes through `au_command` as C.

**The race's opening.** `$7E:3F59` was a port already; it had no row in the
scene script's routine tables, so the program stopped where the race's first
script called it. Its row is in `scene_race_routines` (scene/race_camera.c),
and the opening goes to the calling channel's script, as the original's
store on its direct page does; the guard no longer insists on channel 0. As
a row it leaves A as the original's last load does, the opening it picked:
the interpreter's stop after it reads A's high byte, so the entry no longer
declares A don't-care.

**Measured.** Shadow mode over the library, none mismatched: the dial's
digits 15,365 calls, the dial and the gate's spin 2,195 each (the spin 5
interrupted), the map view's cursor 166 and its markers 104 each, the gate
stream's load 15 and the gate's build 5, the race's pick 2; the two screens
span frames, so their calls are abandoned, as before. Fuzzing, seed 1,
`tools/fuzz_compare.py` against the branch point's `ct_fuzz` over the 14
entries changed: every run the same as before; the race's pick has a
second variant on the second channel's page, which the branch point's guard
refused and which now compares all 20,000 states, none mismatching.
`tools/effects_check.py`: the handlers' state halves read the objects' steps,
periods and the gate window's bank, which their starts write plainly; their
lines are the old guards' (`animates` among them), renamed, with the reasons
the old ones had. `ct_native` from a cartridge save: 35 of the 40 save
movies play to their end (30 did): events-epoch-lavos, menus-shop-equip and
menus-epoch through the dial and the map view, and both race movies; the
other five stop where they did. The power-on movies and the attract sequence
go as far as before. No timing row moved.

### The native program through the menus the player opens

From a cartridge save `ct_native` played 35 of the 40 save movies to their
end (the history file, "The native program through the Epoch's screens and
the race's opening"). Four of the rest stopped in the menus: menus-field-menu
at the equipment's slots window (`$C2:A71E`), menus-sweep at an item moved
(`$C2:AAC8`), and battle-tech-sweep-spekkio and menus-end-of-time at the
End of Time gate's exchange (`$C2:C7AC`); the fifth, menus-shop-inn, at a
shop (`$C2:D52B`). The chunk followed the four through every step they
reach: the equipment's slots and list, the items moved and used with the
healing they show, the techs' list and the members a tech is used on, the
settings' meters, gauge layouts and button rows, and the gate's exchange
with its song and swirl. The shop it left (below, "The native program
through the shops").

**One shape throughout.** Each step, and each routine a step goes on to, is
one `*_with` sequence of the screen's (menu/equipment.c, item_screen.c,
tech_screen.c, settings_screen.c, exchange.c, healing.c, sound.c), asking
what differs through the screen's steps struct: a tick, a frame's wait, a
note (what the original's tests leave, scaffolding), and two the screens
lacked: `registers_at`, the widths and the P a routine saves or sets where
the original's code does, and `run`, a sibling routine the original reaches
by a call, named by an enum. On the machine `run` is the original's call at
its site, from a table only the machine's step reads, and `registers_at` the
width or P change; as C, `run` is the sibling's own sequence or port behind
the state half of its entry's guard (split out of the guard where it was
static), and `registers_at` nothing. The registered entries are thin
wrappers passing the machine's steps; where a C function now takes a
routine's plain name, its wrapper is `<name>_entry` (registry names
unchanged).

**What came with it.** The equipment's member list's slide (`$C2:9D5B`),
its slots window opening and closing (`$C2:A71E`, `$C2:A85C`), the list of
what fits made, drawn, slid in and out (`$C2:A463`, `$C2:A45D`, `$C2:A4AF`,
`$C2:A697`), pointing at a slot and the shoulders' switch (`$C2:9ECD`,
`$C2:A00B`), the card redrawn (`$C2:A21F`, members.c) and back on the slots
(`$C2:A64D`). An item's use (`$C2:B0AB`) and its kinds (a tonic, by chance,
the party's rest, a tab), the member window (`$C2:AF42`), a tab's pointing
and raise (`$C2:B24A`, `$C2:B275`, `$C2:B2A7`), one more used (`$C2:B31C`),
and the item's users closed (`$C2:AC1D`). The healing shown, its numbers'
task and each kind's rise (`$C2:8F55`, `$C2:8F6C`, `$C2:9016`, `$C2:9047`),
the task a C row of menu/tasks.c's table; the sound on the effects' other
voices as C (`$C2:EAF9`). The cursor among the members a healing is used on
(`$C2:B83E`, `$C2:B870`), which the items and the techs share. The techs'
member list slid (`$C2:B568`, `$C2:B5C8`), the lister and its sprite
(`$C2:B9E1`, `$C2:BA09`; one record's sprite load as C, sprites.c), choosing
a tech and its targets (`$C2:B3AE`, `$C2:B48E`) with the list's slide, the
tech described, the targets' window and pointing (`$C2:B72F`, `$C2:BE01`,
`$C2:BE79`, `$C2:B823`). The settings' meters and the design (`$C2:C164`,
`$C2:C0B9`, `$C2:C0D1`, `$C2:C12C`), the gauge's layouts (`$C2:BF9F`) and the
button rows (`$C2:BF6D`). The gate's exchange's opening (`$C2:C7AC`): the
gate's song changed, played in a task and changed back (`$C2:CB8B`,
`$C2:CBBF`, `$C2:CC0E`; the task a row of the tasks' table, its wait for the
song looking at the driver's status once a frame: TODO.md, "The gate's wait
for its song to play"), the
panel's gate sprite, the swirl unpacked and drawn (`$FF:F677`) and the gate
screen started (`$C2:CE9D`) with its frame task as C (`$C2:CECE`), and the
fade in. Past the gate, battle-tech-sweep-spekkio met the event commands
that fill the party's HP and MP (`$F8`-`$FA`, `$C0:3E5C`-`$C0:3E75`): one
sequence, with C bridges in the event engine's table.

A cursor read with the cursor held (the item users' close) is one helper,
`menu_hold_and_read_the_cursor`, whose machine form notes what the read
leaves before the hold, as the original's order has it.

**Measured.** Shadow mode over the library, none mismatched: the
equipment's slot step 724 calls and its item step 633, pointing at a slot
712 and along the list 1,440; an item moved 322 and the users closed 63;
the techs' list step 788, its targets 222 and the cursor among the members
a healing is used on 532; the settings' meter 935, the design 726, the
speeds 218 and 222, the gauge's layouts 160 and the button rows 208; the
gate screen's task 1,213; the steps and routines that wait across frames
(the windows' slides, the item's use and the healing shown, the gate's
opening and its swirl) are abandoned where their frames end, as their
callers' steps are. Fuzzing, seed 1, `tools/fuzz_compare.py` against the
branch point's `ct_fuzz` over the 81 entries the chunk changed (98 runs with
their variants): it found the techs' new tick places missing from the
machine's table of sites (the list's slide, a tech chosen, its targets),
which the movies' runs and the C path never meet; fixed, every run is the
same as before. `tools/effects_check.py`: the bridges' reads that moved into
the sequences keep their lines and reasons under the sequences' names, and
two new state halves read the menus' zero word, which the menus' opening
writes plainly. One timing row moved: `menu_raise_amount_kind` (`$C2:9047`),
its sound now a step, recalibrated from 9102 to 8670. `ct_native` from a
cartridge save: 39 of the 40 save movies play to their end (35 did):
menus-field-menu, menus-sweep, menus-end-of-time and
battle-tech-sweep-spekkio; menus-shop-inn stops at the shop's opening
(`$C2:D52B`), where it did. The power-on movies and the attract sequence go
as far as before.

### The native program through the shops

The last save movie `ct_native` didn't play to its end, menus-shop-inn,
stopped at a shop's opening (`$C2:D52B`): the shop's nine steps had no C
column in `menu/screens.c`'s table. Their bridges (`menu/shop_bridge.c`)
were the machine's own code, with the decisions in `menu/shop.c` and
`shop_equipment.c` between the original's calls.

**The same shape as the menus'.** Each step and each routine a step goes on
to that waits or calls out is one `*_with` sequence in `menu/shop_steps.c`:
the opening and the set-up (`$C2:D52B`, `$C2:DACA`), the panel
(`$C2:DD7C`), the tabs (`$C2:D546`), the goods and the list for sale
(`$C2:D58B`, `$C2:D645`, their row taken, a page turned and the list slid
a row), the quantity (`$C2:D618`, `$C2:D690`, its change by a push), and
the equipping from the shop: the members (`$C2:D6C3`), a member chosen
(`$C2:E0A5`), its slots (`$C2:E0F1`), the shoulders' switch (`$C2:E108`),
a slot (`$C2:D715`) and the list of what fits (`$C2:D778`). `ShopSteps`
has the menus' `note`, `registers_at` and `run`, with a tick, a wait and the
ends (the fade in, the way out, a list's opening, the refusal's buzz); a
note's value is a word, since a few leaves take two bytes of the state
before (the room and the held count of an offer, the members compared, the
total before a push). On the machine `run` is the original's call from a
table of sites and the sounds' loads; as C, each routine's sequence behind
its guard's state half (`menu_shop_can_be_opened` and the rest, split out
of `shop_bridge.c`'s guards, and `menu_figures_can_be_worked_out` out of
the figures' guard). The two lists' openings (`$C2:DC7B`, `$C2:DE98`)
make no call-out and are one C function each, which their entries wrap. The
equipment screen's list of what fits and its move along (`$C2:A463`,
`$C2:9F05`) are the equipment's sequences; their C forms take the plain
names (`menu_make_equipment_list`, `menu_point_along_equipment_list`) and
the machine's wrappers became `*_entry`, as the shop's nine steps did.

**Measured.** `ct_native` from a cartridge save: all 40 save movies play to
their end (39 did), menus-shop-inn and menus-shop-equip among them. The gate
over the library passes, the shop's entries compared in shadow mode with none
mismatched; `tools/effects_check.py`'s lines for the bridges' reads moved
with them into the sequences. Fuzzing, seed 1, `tools/fuzz_compare.py`
over the shop's 28 entries and the equipment's two the shop shares (with
their variants, at most 2,000 states each): none mismatched.

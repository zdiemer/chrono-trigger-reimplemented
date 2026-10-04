# Phase 3, chunk by chunk

The per-chunk record for the three big banks -- the menus, the field and
events, and the battle -- and for the scene program in bank `$7E`, as each
chunk of routines became C. The newest section
stays in docs/RECOMP.md as the shape a new one follows (the field's and the
battle's, at the split; the menus' next chunk will keep its section there until
the one after it lands).

Split out of docs/RECOMP.md, which keeps the mechanisms, the current
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
`--native on --recomp off --misses` over the menus' movies, the interpreted
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
button conditions included. `ct_dev --native on --recomp only --misses`
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
places nor paths (docs/history/recomp-chunks.md). This chunk is the rest: the
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
stays in docs/RECOMP.md as the shape a new one follows (the field's and the
battle's, at the split; the menus' next chunk will keep its section there until
the one after it lands).

Split out of docs/RECOMP.md, which keeps the mechanisms, the current
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
`--native on --recomp off --misses` over the menus' movies, the interpreted
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
button conditions included. `ct_dev --native on --recomp only --misses`
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
places nor paths (docs/history/recomp-chunks.md). This chunk is the rest: the
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

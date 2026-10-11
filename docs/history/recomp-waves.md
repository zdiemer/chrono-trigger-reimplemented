# How the bulk conversion went, wave by wave

The pilot and the first two waves of Phase 3, bank by bank: what each wave
converted, what it found and what it cost. The mechanisms these sections
exercise are described in docs/RECOMP.md; this is the record of using them.

Split out of docs/RECOMP.md, which keeps the mechanisms, the current
phase and an index of these files.

## The $C7 pilot

The last step before PLAN.md's bulk conversion: one subsystem converted through the
workflow to the whole bar, to learn what Phase 3 costs. The subsystem was the
sound driver's CPU half, bank `$C7` (`game/core/sound_driver.c`): 2.6 KiB of
code, half of it ported before in the early style (every instruction's effect
on A, X, Y, P and the stack reproduced: 1066 emulated-register operations by
the count below), the rest inlined into those ports but credited to nothing.

**Where it ended.** `python3 tools/burndown.py` for `$C7`: 1.4 KiB ported and
46% left before, 2.6 KiB and 0% after; the one byte left, `$C7:0003`, is a
`NOP` between two jumps that nothing reaches. 43 registry entries (34 before):
the routines the old ports inlined but nothing credited (`play_song` at
`$C7:01A1` and its stages, `$C7:0734`, `$C7:09FD`, `$C7:0A12`), and the code
after wait loops that ran interpreted (`$C7:011E` after the boot's countdown,
which is an idle entry now, `$C7:08DA`, `$C7:09E5`, `$C7:09F7`, the common
exit `$C7:0192`). The file went from 1788 lines to 1416, plus 134 in
`game/core/sound_data.h`. Every routine is a C function of what it does in
values; the emulated-register operations left, 86, are all in the part of the
file that moves the original's registers in and out (the entries, the idle
passes and the call-out stubs) and none in the logic. Over the 26 movies in
shadow mode nothing mismatches and no call is refused; all 43 entries pass
20000 fuzzed states (PORTING.md's row has the numbers).

**What the bar needed that the gate didn't allow.** A port written as logic
apart from the registers doesn't know the flags, A's high byte after 16-bit
index arithmetic, or the stack bytes of the subroutines it inlines, and shadow
mode compared all three, so the first attempt had to choose between
reproducing them (the draft tool does, with a local per flag) and a narrower
comparison. The comparison was narrowed, to what the original's own execution
shows is dead, and nothing else:

- stack bytes the original pushed and pulled again since the last comparison
  (between the lowest S it reached and its S now) aren't compared, in shadow
  mode and in `ct_fuzz` (`native_copy_popped_stack`). Nothing reads below S.
  Every subroutine in `$C7` saves P with `PHP` and restores it, and every
  call-out's callee does, so without this each inlined subroutine and each
  call-out needed its P byte exact;
- a native routine can declare what is dead where it stops
  (`m_stop_dont_care`), on top of its entry's set: an entry that hands back at
  several wait loops leaves different registers live at each;
- `DC_AH`, A's high byte alone;
- `au_driver_call` takes the call-out's don't-care set, like `m_jsr_dc`.

The first two only ever leave out bytes and registers the original itself
overwrites before reading; the suite's other checks (trace identity with
`--generated only`, timing, vblank, routes) are untouched by them.

**Per routine**, what survived of the draft (`python3 -m tools.generate.show`,
with the pilot's hints and the library's coverage) and what changed:

| Routine | Draft / port lines | Close lines, similarity | What changed from the draft, and why |
|---|--:|--:|---|
| `$C7:0004` command, `play_song`, commands `$70`, `$71` | 895 / 546 | 31, 0.02 | The draft is one function with 49 gotos over the dispatch, `play_song` and its eight stages; the port is about fifteen functions, one per stage, with the slot tables as named word arrays. Its jump table became `command & 2`. The draft's register stubs (ten variants of the stub for `$C7:09DA` alone, one per register signature) became one stub that sets A and names the rest untaken. D is unknown to the draft after the first call-out, so its accesses fall back to `m_dp`; the port names them all |
| `$C7:0655` upload a block | 128 / 121 | 1, 0.08 | The header retry, the two data loops (the one carrying into the next bank chosen once) and the length prefix became shared protocol helpers used by six routines; the pointer set-up that the retry repeats is done once, since it is idempotent |
| `$C7:0A39` send a song | 28 / 50 | 0, 0.19 | Close to the draft; `$C7:09FD`, a call-out in the draft, is a C call |
| `$C7:0922` upload a `$2F00` set | 91 / 101 | 2, 0.13 | The two sets' duplicated loops became one, the set chosen by an accessor |
| `$C7:00B0` boot, next byte | 50 / 34 | 0, 0.25 | The index the boot ROM counts, which the original keeps in A's high byte across the wait (`XBA`), is a parameter and a result |
| `$C7:0643`, `$C7:0614`, `$C7:08FF`, `$C7:0A7B`, `$C7:011E` | 15-28 / 10-56 | 0-1, 0.09-0.23 | Short; mostly the entry's register handling, now in the wrappers |
| `$C7:0734`, `$C7:09FD`, `$C7:0A12` | 17-28 / 13-26 | 0, 0.14-0.23 | Loops the draft left as gotos |

The draft was right everywhere the port could be checked against it; nothing
in it was wrong. The port differs in text almost completely: token similarity
0.02-0.25, 0-7% of the port's lines with a close draft line. (The ground-truth
tool undercounts the port's memory accesses and call-outs here, since it
doesn't resolve file-local names for `RAM_` constants or call-out stubs;
the similarity figures stand.)

**Found on the way**, each a port bug that compiled, looked right and passed
the first checks: an inlined subroutine's P is live across its call-outs
(`$C7:0655` saves P under the return address of the calls it makes, so the
port pushes it); registering `play_song` on its own broke `$C7:0004`'s shadow
comparison (PORTING.md, Call-outs); and three that only fuzzing reached, since
no movie does: an odd boot-table index (the accessor rounded it to a block),
P restored with 8-bit index registers without truncating X and Y, and
`$C7:09DA`'s `SEP #$20` written as setting both widths.

**The asset seam.** `game/core/sound_data.h` holds every read the driver makes
of the ROM: MD2's `sound_block_ptrs` and `song_ptrs` by name
(`sound_block`, `sound_song`), the hand-typed tables by what the driver does
with them, and the bytes of any resource through `sound_data_byte`. The
resource is still located by its ROM address, because the driver keeps its
upload position in work RAM as a long pointer the game can see; a build that
loads assets from files maps that address in this one header.

**Effort.** Reading the subsystem and the drafts took about as long as writing
the port, and deciding how the logic and wrapper split meets shadow mode
(above) took longer than either: it is decided once, now. The port itself,
1550 lines, was written in one pass from the existing port, the listing and
the drafts; after that, two shadow iterations over one movie, one over the
library, and three fuzz iterations. Machine time dominated the wall clock: a
library shadow run is 6 minutes at four at a time, the fuzzing of 43 entries 3
minutes, the full gate with every route most of an hour. Validation found five
bugs; review of the draft against the listing found two more before the first
run (a loop that must run at least once, an 8-bit read written 16-bit).

**Sound.** The driver broke once in exactly this kind of change (a song
upload scrambled when native time opened a gap between APU writes;
PORTING.md, `native_sync`), so the shipping mode's audio was compared with
`tools/audio_compare.py` (4 s segments) against the reference, `--generated
only`, for the pilot's build and for the build before it:

| Movie | Audible segments | Spectrum correlation, median (before / after) | Segments below 0.9 (before / after) | Log-spectral distance, median (before / after) |
|---|--:|--:|--:|--:|
| `new-game` | 25 | 0.9990 / 0.9990 | 5 / 5 | 0.53 / 0.53 dB |
| `field` | 43 | 1.0000 / 1.0000 | 5 / 5 | 0.36 / 0.40 dB |
| `gato-battle` | 81 | 1.0000 / 1.0000 | 9 / 10 | 0.33 / 0.34 dB |
| `events-jetbike-race` | 38 | 0.9970 / 0.9985 | 2 / 2 | 0.59 / 0.46 dB |
| `battle-magus` | 69 | 0.9825 / 0.9775 | 13 / 14 | 1.73 / 1.85 dB |
| `menus-shop-inn` | 38 | 1.0000 / 1.0000 | 6 / 6 | 0.30 / 0.35 dB |

The same songs and sounds at shifted times, as before: a scrambled upload
would put a whole song's segments far below 0.9, and no movie has more than
one segment where one side is silent and the other isn't (the same one before
and after, at the edge of a silence). `battle-magus` is lowest in both builds,
where the fight plays out differently in the shipping mode. With
`--audio direct`, `field` gives 0.9997 and 0.38 dB against the reference
(0.9997 and 0.39 dB before), and the direct runs now interpret 5 instructions
where they interpreted 84856, the code after the wait loops being entries.

## Phase 3, wave 1: bank `$FD`

The first bulk subsystem after the pilot: bank `$FD`'s field graphics, 15.3
KiB of code of which 1.5 KiB was ported (9 entries, most in the early style).
`python3 tools/burndown.py` for `$FD`: 90% left before, 47% after, 8.1 KiB
ported in 50 entries (9 before). Two coherent parts were converted to the bar, with the
bank's existing ports:

- **The field's per-line effects** (`game/field/line_effects.c`, 871 lines):
  the tables the eight line effects read, rebuilt every frame for where the
  dialog window is (`$FD:C2C1` and six builders, two table sets of three
  layouts, with their pieces), the values the setup fills once (`$FD:C124`,
  with the dialog window's shading through the divider, `$FD:D52D`), and the
  slot programming (`$FD:C1EE`, moved here from `field/display.c`). About
  2400 lines of drafts for 5.1 KiB of 65816, most of it straight-line stores
  twice over; the port is one function per layout, the sets and the window's
  position as parameters.
- **Palette and tile animation** (`game/core/palette.c`, `tile_animation.c`):
  the per-frame animation of every mode (`$FD:E39C`), its eight types'
  handlers and fourteen channel modes as two tables of what each changes, the
  location loader (`$FD:E292`) and the clear (`$FD:E275`), and the animated
  tile upload (`$FD:E022`).

The 48 entries added or rewritten match over the library in shadow mode and
pass fuzzing (PORTING.md's rows). What the part taught, for the next waves:

- **Callers' liveness decides a port's size.** The field's main loop reads
  nothing the analysis can rule out after `$FD:C2C1` returns, so X, A's high
  byte, the carry and the overflow flag are live: the carry the window-2
  builder's `LSR` leaves carries into the wave's sixteen `ADC`s, and the exit's
  overflow flag is one particular `SBC`'s. The port returns those four as an
  exit record, traced by hand where the draft traces them mechanically.
  Liveness through the field loop's calls would drop most of it.
- **The burndown doesn't follow call tables.** `JSR (abs,X)` targets are
  credited only when registered, so the six builders are entries of their own
  (which also fuzzes them apart from the dispatcher).
- **Stack bytes a transfer reads.** Shadow mode leaves out popped stack bytes,
  but a DMA whose source is the stack page reads them before they are popped:
  fuzzing sent an animated tile slot there, so `$FD:E022`'s wrapper pushes what
  the original pushes. Likewise the frame copy of palette type 1 keeps its
  pointer in the direct page, which a count of 0 overwrites; the port reads it
  from memory each time, and fuzzing needed `--step-limit` raised to compare
  those states at all (the default skipped them).
- **Inlining a dispatcher's handlers costs its time estimate.** Each palette
  handler fits its own `cost_milli` within 0.3-4.4%, but `palette_animate`,
  which now runs them as C calls rather than call-outs, spends its time on
  twelve delay counts one frame and a fade through the multiplier the next,
  and one scalar misses by 25.9% on average. The vblank check finds no video
  write moved (it writes RAM), so it and the two fades are timing exceptions
  (`tools/timing_exceptions.txt`), as the decompressor is.
- **Declarations the check can't read**: a registry entry whose precondition
  is a macro argument (`LINE_PIECE(..., NEEDS_M8_X16, ...)`) is a finding of
  `tools/dontcare_check.py`; entries are written out.

## Phase 3, wave 1: the battle's graphics banks ($CF, $D1)

The first subsystem converted in bulk after the pilot, chosen because its code
is video-heavy where the pilot's went through the audio service. What that
turned out to mean, and what the workflow cost:

**Where it ended.** `python3 tools/burndown.py`: `$D1` from 87% left to 17%,
`$CF` from 52% to 7% (the whole ROM from 83% to 81%, 44.6K to 51.7K ported).
New: the battle's screen script commands and the frame's palette and window
effects in `$D1` (`battle/screen_*.c`), the nineteen timed effect slots in
`$CF` (`battle/effect_paths.c`), the afterimages and effect sprites the object
drawing had called out to, and the three window-row transfer requests.
Rewritten to the bar: the NMI and IRQ handlers, the tile row upload and the
second vblank handler in `$D1`; the object drawing and motion
(`battle/draw_objects.c`, `battle/object_motion.c`, out of `battle/sprites.c`),
the vblank uploads and the frame update in `$CF`. Every call over the library
matches and every entry is fuzzed (PORTING.md's rows). What is left is in
docs/TODO.md: `$D1:F1B0` and `$D1:F9E7` (below), and two runs in `$CF`
(`$CF:FADD`, `$CF:FD9E`).

**The video service was the smaller part.** Almost none of `$D1`'s "graphics"
code touches a register: it writes tables in work RAM that the per-line
effects and the uploads read (the two window line buffers, the palette's two
copies, the fade tables, the quadrilateral's corners), so it is ordinary
logic under the RAM map. The register writes are in the handlers and the
uploads, which P2 had already converted, so bringing them to the bar was
restructuring around calls that already existed (`vid_enable_line_effects`,
`vid_set_display`, `vid_raster_display`, `vid_upload_vram`,
`vid_upload_palette`, `vid_upload_oam`, the SNES transfer escape for the
described transfer, the timing service's acknowledgements). No service call
was added. Two things no service expresses were found, and both are left in
the original for now rather than escaped: the window quadrilateral rasterizer
`$D1:F9E7` writes the window table through the work RAM port with 16-bit
stores to `$217F`, so the low byte of every edge also lands on APU port 3 (the
P1 leftover in TODO.md, now located); and the battle's screen setup
`$D1:F1B0` sets the line interrupt's line (`$4209`), for which the timing
service has no call yet (PLATFORM.md's sketch has `tm_set_line_handler`,
never built).

**What cost the time** was A's high byte. Liveness has it live after most of
these routines (their callers' later 16-bit loads and `TAX`es take it along),
and no declaration leaves out A's low byte alone, so a port that is otherwise
pure logic still has to return exactly what the original leaves in A, path by
path: the handler that ran last, the flag byte a skipped slot loaded, the
distance the proximity test measured. For the effect slots that is a return
value on every handler. The same byte enters as an index (`TAX` after an
8-bit load), so the entries whose callers always leave it 0 are guarded on
that instead of carrying it through every table lookup; the handful that
genuinely take it (`$D1:EA4B`'s level lookup, `$D1:ECF3`'s first block,
`$D1:EEA5`'s start line, the animated color `$CF:E58A` indexes with what the
party palette cycle leaves) pass it as a parameter. A routine made of
call-outs, like the frame update, also has to set A's low byte before each
call even though no callee reads it, since the call's comparison includes it.

**What shadow mode and fuzzing found** is the pilot's list again: flags a
caller reads that the early ports reproduced by emulating every instruction
and a rewrite has to set on purpose (the IRQ's pushed P under `$FD:FFFD`, the
carry `$D1:FE4C` hands back through `$CE:EE6E`, the overflow of
`$D1:E8D9`'s last address step, the carry the fade tables' last `CPX`
leaves, the frame update's overflow, which its callees pass on), 16-bit
`INC`/`DEC` read as 8-bit (the script pointer's step back, the wipe's count,
the homing jump's arrival count, which carries into the next slot's byte),
16-bit wraps where the original's indexed access runs into bank `$7F` (a
palette offset, the effect slots' anchor offsets, the rotated colors), a
direct-page index that wraps in bank `$00` where the port had used the work
RAM address, and orders of reads and writes that matter where two tables
overlap: the effect chase's cells, and the object drawing, whose sprites,
written far enough on, land on the drawing page's own cells (the object
number, the counts, the base position), which the original reads back. Two
more were copies that read the stack page: the tile row upload's source can
cover the bytes its `PHB : PHA` leave there and its count on the direct page,
and the sprite tile copy the return address of its own `JSR`, so the ports
write those bytes too, as the deferred uploads' port already did. None of it
is reached by a movie.

**The fuzzer's limits showed.** A native loop can't be stopped the way the
interpreter's step limit stops the original, so the object drawing, which on
random RAM can loop forever reading its draw list back from what its own
sprites overwrote, is fuzzed with the part and sprite counts bounded (PORTING.md
says how). And a copy whose destination covers its own return address sends
the original somewhere the port can't follow; the entries that take a
destination from RAM are fuzzed with it kept above the stack.

**The draft tool** was used for its headers and liveness far more than its
text, as in the pilot; with the listing at hand the logic was written from
the listing. Its dispatch-table handlers draft as separate functions, which
is right, but the burndown only credits a handler that is registered, so each
inlined helper and handler also became a (small) registry entry of its own.

## Phase 3, wave 1: the scene engine and scene data (`$C3`, `$C6`)

The scene engine's bank went from 44% left to 25% by `python3 tools/burndown.py`
(11 entries to 28), and the scene data bank from 100% to 68% (25 entries); what
is left in `$C3` is code the beam's counters decide, one routine nothing reaches
and the edge sequence `field_rasterize_quad` inlines, which the tool can't
credit (PORTING.md's rows). The early ports (the NMI, the sprite lists, the
scaler, the multiply) were rewritten to the bar on the way. Found on the way,
about the workflow rather than the code:

- **A stop inside the original's loop is ambiguous.** Shadow mode and
  `ct_fuzz` take a stop as reached the first time the original passes that
  instruction with the same S. The sequence interpreter's loop head, or any
  instruction a command shares, is passed again and again, so a port that runs
  several commands and then stops there (a command it doesn't handle, a script
  that never yields) was compared against the original's first pass. Such a port
  now puts the script back on the command and makes the rest of the run a
  call-out to the original's loop from the entry's own `JSR` site, which leaves
  the stack exactly as the original has it; the divisor loop `$C3:0E29`, whose
  loop head is its entry, hands a loop that never ends back after its first
  pass, where the machine is also after all 256.
- **An entry on a path the port also runs** is the nesting rule of Call-outs in
  PORTING.md again: `$C3:0C28`, where the sound wait goes on, is also where the
  store command's path continues, so the original entering it inside a run
  started a comparison of its own, which took the run's call to the sound
  driver. It isn't an entry; before registering an address in the middle of a
  routine, check that nothing else a port covers runs through it.
- **Chained carries** (`ADC` without `CLC`) run through the scaler's samples,
  the circle's midpoint steps and the sequence commands, and callers read the
  carry and overflow after them. A `bool carry` threaded through the logic
  reproduces them; fuzzing checked every one first time.
- **Open bus in a status byte kept in A**: `$4211` and `$4212` return open bus
  in the bits they don't define, which the original reads as the instruction's
  last operand byte; where A keeps the byte, the port masks those bits.
- **Everything live after an indirect tail jump** (docs/TODO.md): bank C6's
  routines are reached through `JML [$005B]`, so liveness keeps every register
  and flag live after them, and each port reproduces all of them.
- **Fuzzing long native runs**: `ct_fuzz` gives the original four times the
  call-outs' step budget, but not the port's own work, so a port that runs a
  long script natively can be reported as "original never reached" where the
  original only ran out of steps; `--step-limit` fixes it. Pointers into I/O
  space (object slots, script pointers) need `--fill` to stay in RAM, where the
  game keeps them.
- **The draft tool** names a direct page cell by whichever page has that D: the
  menu's page and the scene's are both `$0000`, so the C6 drafts named the menu
  object mode's cells after the scene program's (`RAM_SCENE_FRAME_READY` for
  `$00`). Entries with more than one entry width need `--m`/`--x`.

Effort: a long working day for 53 entries (1.1 KiB of bank C6 among them,
counting the jump-table copies); the sequence interpreter and the circle were
most of the reading, the ambiguous stops most of the debugging, and machine
time (the library in shadow mode, 10 minutes at two at a time; the gate with
every route about an hour) most of the waiting.

## Phase 3, wave 2: the menus' Mode 7 screens (`$C2`)

The first slice of bank `$C2`, taken because it was also P2's last leftover
(PLATFORM.md, "P2 as built"): the Epoch's map view and time dial, twenty-six
routines between `$C2:631F` and `$C2:7758` that only `menus-epoch` and
`endings-beyond-time` reach, and the End of Time's gate screen `$C2:CE9D`.
`python3 tools/burndown.py` for `$C2`: 5.6 KiB ported and 90% left before, 10.3
KiB and 83% after, 52 entries to 81. All of it is in `game/menu/epoch.c`, which
is 2208 lines for 4.7 KiB of 65816. `--vid-check` went from 5506 of 8799 frames
to all of them on `menus-epoch`, and from 8234 to all of them on
`menus-end-of-time`, which closes the menus' part of P2 (PLATFORM.md; what is
left there is the gate screen the event engine opens, which is different code
and reached by neither movie).

**What the two screens are.** Both run in the menus' sprite-object mode (the one
at `$C2:0000`): they blank the screen, build a Mode 7 plane in VRAM, put one
object in a slot to drive it and loop on the frame task until the mode byte at
`$027C` changes. The map view packs the world map's tiles into one colour each
at `$7E:AB25`, builds the plane from the two map layers and turns it under a
cursor walking a fixed path, with markers for the party, the Epoch and the map
it would land on; its one per-line effect is a blue gradient. The time dial
spins a triangular gate with a Mode 7 matrix that changes every line, cut out of
the screen by window 1 whose edges change every line too, and seven digit
objects show the periods.

**Two service calls were added**, both additive:

- `vid_upload_vram_half`, for an upload that writes one byte of each VRAM word
  and leaves the other. A Mode 7 plane keeps its tile numbers in the low bytes
  and its pixels in the high ones, so the game builds and uploads each half on
  its own (`$C2:645C` feeds `$2119` alone, `$C2:65B2` and `$C2:722C` `$2118`);
  `vid_upload_vram` is fixed to whole words.
- `VID_LINE_MODE7_A` through `_D`, a Mode 7 matrix value driven per line on its
  own. The service had `VID_LINE_MODE7_AB` and `_CD`, which are the pair a
  transfer mode 3 channel writes; the dial uses four mode 2 channels instead, one
  per value, so the observer had no target for them and read the whole screen
  wrong from the frame they start.

**What it cost, and where.** The screens are mostly ordinary logic, and shadow
mode's findings were all in the same three kinds:

- **Register widths at each exit.** These routines are object handlers reached
  through `JMP ($0050)` and they hand back at several places, each with `REP` or
  `SEP` in a different state: the drift routines return 16-bit A on their working
  path and 8-bit where the objects are frozen, and the dial's object has six
  returns across three widths. The first shadow run's whole mismatch list was
  `P original 4 native 24`.
- **The dispatch leaves the step in X.** `TDC : LDA $0002,X : ASL A : TAX :
  JMP (table,X)` means a path that doesn't load the slot again returns with X
  at twice the step, which the callers read; and the `ASL` is what sets the
  carry a later call-out is compared on. One `dispatch_step` helper does all of
  it.
- **Index registers are 16 bits wide here, so a `STY`/`LDY` swap moves a word.**
  The gate's edge sort swaps its two corners through Y, so the byte after each
  coordinate moves as well, which is what the original leaves at `$09` and `$0F`.

Two more were plain mistakes the check caught: the map packer reaches the work
RAM port and the multiplier long, because it runs with the data bank on the map
(`m_abs` would have put them in bank `$7F`), and the gradient's second loop runs
its counter all the way round rather than sixteen times, writing 1792 bytes past
the 224 the table reads -- which the game never looks at, and shadow mode
compares.

**The don't-care check found two declarations** that liveness refuses: the sound
command the dial sends reads A's high byte, so the call declares `DC_AL`; and the
gate's setup ends by jumping to the object starter with the address in X, so its
stop declares its own set (`m_stop_at_own`) rather than adding the entry's `DC_X`.

**What fuzzing found**, none of it reached by a movie, and all of the same kind
as the pilot's: a routine that takes its first operand from whichever slot the
caller left in X and the rest from `$4E` (`$C2:6736`, the map's matrix); an `ADC`
after three shifts with no `CLC`, so the last bit shifted out counts
(`$C2:688A`); a period doubled with an 8-bit `ASL`, so one of `$80` or more
indexes from the other end of its own byte (`$C2:745A`); six direction buttons
tested one at a time, down and up in turn, rather than all the downs and then all
the ups; a channel pair programmed the wrong way round (`$C2:6F85` puts C on
`$211D` and D on `$211E`), whose only call in a movie was interrupted both times,
so shadow mode had never compared it; an upload that sets the VRAM port before
the address where every other upload on the screen does it the other way round,
and the address latches through the port; and a count read from memory, which
goes all the way round at 0 where a `for` loop would not run at all. Seven in
28 entries, on a chunk that shadow mode already passed over the whole library.

**Bank `$C6`'s object handlers moved.** Wave 1 ported twenty-four routines of
bank `$C6` into `game/scene/objects.c`; they are the menu object scripts'
command `$4E` handlers and two the Epoch's map screen calls, so they are
`game/menu/object_commands.c` now, with `menu_object_`/`menu_epoch_` names. No
behaviour changed.

## Phase 3, wave 2: the menus' object scripts (`$C2`)

The second slice of bank `$C2`: the instruction set of the menus' object
scripts. Every sprite object of the mode at `$C2:0000` can run a command
script, and `menu_run_object_script` `$C2:0F63` was ported long ago with its 83
handlers left as call-outs. They are ports now (`game/menu/object_script.c`),
with the five routines that start an object and make its per-frame move.
`python3 tools/burndown.py` for `$C2`: 10.3 KiB ported and 83% left before,
13.6 KiB and 77% after, 81 entries to 169. All of it is 1700 lines of C for
3.5 KiB of 65816.

**How the chunk was chosen.** `ct_dev --io-log --native on --generated off` over
`menus-field-menu` and `menus-shop-inn` says that no unported code in `$C2`
touches a PPU, APU or DMA register at all any more: what is left of the bank's
I/O is the multiplier and a handful of `$42xx` writes. So the io-log, which
picked the chunk last time, said only that the display side of the bank is
done; the chunk came from the burndown's largest run instead (`$C2:1037`, 2617
bytes), which turned out to be 80 of the 83 handlers, and from the fact that
they are one thing: a scripting language with an interpreter already ported.

**What the commands are.** A handler hands back a length to move the script on
by, zero to leave the object until the next frame, zero with carry to free its
slot, or a signed byte to branch by -- which the interpreter adds the same way,
so a branch of zero stops the object like any other zero. They reach three
places: a byte of the running object's slot by an offset into it, a byte of low
RAM by a 16-bit address, and their own operand bytes. On top of that they start
objects and scripts, set a sprite's palette and priority, move and scroll the
map, queue VRAM uploads, send the sound driver a command, load a palette row,
copy tilemap rows through the work RAM port, and leave for another location.
`tools/disasm/objscript.py`, which already parsed the scripts for MD4, now
carries the name of each command beside its length, and the ports are
`menu_object_cmd_` plus that name.

**What shadow mode found** was one mistake, and it is the kind a map makes
easier to make than to avoid: the map's origin is not three words `$02` apart,
so the port that saves the party's place before a location change read the
wrong one. Three words `$04` apart was the model that fixed it; what it really
is, two arrays of a word per plane, is in "the menus' map" below. Everything else matched on the first run over eight movies.

**What fuzzing found**, neither reached by a movie: the mode's VRAM upload queue
is longer than a direct page, so a record's offset must not wrap in one -- the
original reaches a record's first byte through the data bank and the rest
through the page, and both run on past `$FF`; the port wrapped it and wrote the
records a page early. And the queue command's second record, `$08` further on,
had the same wrap.

**What fuzzing cost to set up**, which is the part worth writing down. Three
constraints, each for a reason the port's row records: the script pointer has
to lie in an area whose bytes are also constrained, because a command's operands
are slot offsets and 16-bit addresses and an unconstrained one reaches I/O
space, where open bus holds what the interpreter's fetches left and native code
sets nothing; the object slot has to be a real slot for the same reason; and the
slot's position and velocity have to be small, because the map routines a scroll
command calls out to walk a row per tile moved, so a random 32-bit velocity
sends them past any budget. Without the last one, twelve states in five thousand
came back as mismatches rather than as skips, and raising the budget tenfold
moved which ones -- the tell that they were the budget and not the port.

**Which calls shadow mode never compared.** Five of the new entries have
interrupted calls in the library (`menu_object_spawn` 4, `menu_object_start_script`
2, and the two commands that start an object, 2 and 3), so those calls were
counted and not compared; they are the paths fuzzing covers, which is why the
rules now say to fuzz them deliberately.

**One timing exception was added, for two entries.** Starting an object walks
the free slots until it finds one and then copies `$3B` bytes of the caller's
state into it, so the share of a call that is the copy swings with how far the
walk went, which one scalar can't fit; the command that loads a palette row is
almost all one such call, and inherits the spread through the
`NATIVE_TIME_AS` that attributes it. Both drift by 0.01% or less over fourteen
movies, their worst call is 1.2 lines out, and the vblank check finds none of
their writes moved.

**The two dispatchers came up to the bar with the chunk**
(PLAN.md: a subsystem's existing ports are converted with it). `menu_run_objects`
`$C2:051D` and `menu_run_object_script` `$C2:0F63` in `game/menu/objects.c` were
in the early style, moving emulated registers about and spelling the slot and
the script pointer as `$0B30` and `$58`; they read as source now and take their
names from the RAM map.

## Phase 3, wave 2: the menus' map (`$C2`)

The third slice of bank `$C2`: what the mode at `$C2:0000` shows behind its
sprite objects. The two chunks before this one left the mode's object scripts
calling out to three routines that scroll and redraw it (`$C2:0568`,
`$C2:066C`, `$C2:09C5`), which the burndown also had as the bank's largest
unported run; they are ports now, in `game/menu/map.c`, with everything only
they call. `python3 tools/burndown.py` for `$C2`: 13.6 KiB ported and 77% left
before, 15.0 KiB and 75% after, 169 entries to 173. 751 lines of C for 1.4 KiB
of 65816.

**What the map is.** Three planes, each 96 x 64 map tiles of 16 x 16 pixels.
A plane's map is one byte per map tile in work RAM; the byte picks one of 256
tile groups, four background tiles laid out two by two, also in work RAM; and
the group's tiles go to a 64 x 32 tilemap in VRAM, which the SNES holds as two
32 x 32 screens side by side. Three words of bank `$C2` (`$C2:0556`, `$C2:055C`
and `$C2:0562`) say, per plane, where its groups, its map and its tilemap are;
`$C2:055C` is the table the object scripts' write-a-map-byte command already
read, and all three are one accessor now. Scrolling moves the plane's scroll
shadow one 8-pixel tile at a time, carrying its top-left map tile with it, and
whenever a whole tile column or row comes uncovered it builds the 32 tilemap
words that fill it in the mode's upload buffer at `$7E:F000` and queues them
for the next vblank. A redraw is the same work for the whole plane: eighteen
blocks of sixteen map tiles, each two tilemap columns, sent straight to VRAM
with the screen blanked.

**Two things the RAM map had wrong**, both found by reading the routines that
use them:

- `menu_map_origin` was one array of three words `$04` apart at `$7E:00E5`.
  It is two: the planes' columns at `$7E:00E3` and their rows at `$7E:00E7`, a
  word each, indexed by the plane number doubled. The pair the Epoch's screens
  and the change-location command read (`$E5` and `$E9`) is the same pair
  either way, which is why the wrong model passed; but a third plane's column
  would be the first plane's row, and a third plane's tile groups would be the
  first plane's map (`$C2:0556`'s third word is `$C2:055C`'s first), so only
  two of the three planes can be scrolled at all. `$C2:2720`, which puts the
  map where the party is and came with the chunk, writes exactly those two.
- `menu_object_tile_source` at `$7E:00E0` was "a long pointer to sprite tile
  data". It is the mode's upload buffer cursor: the next free bytes of
  `$7E:F000`, which sprite tile columns and map strips are both built in and
  which `menu_object_upload_vram_queue` puts back once the queue is out. The
  routine that reads it, `$C2:0ADC`, was `menu_object_upload_sprite_tiles` in
  `menu/display.c`; its only caller is the redraw, so it is
  `menu_map_upload_block` in `menu/map.c` now, rewritten out of the early style
  (an emulated register for every step of the transfer) at the same time.

**What shadow mode found** was one thing: `$C2:066C`'s callers read X, and the
original leaves the plane number there on a pass that crosses no tile boundary
and the queue record's offset on one that does. The port left the caller's X.
Nothing else mismatched, on the first run over the library.

**What fuzzing found**, neither reached by a movie, and both about what a
caller can see of work the routine does through pointers the caller chose:

- **A queue record's source is read after the record's own bank and port are
  written.** The mode's upload queue is sixteen records at `$7E:0060`, and the
  upload buffer's cursor is right past it at `$7E:00E0`, so a queue that has
  filled up puts a record on top of the cursor; the original reads the cursor
  for the record's source only after storing the record's bank and port, and so
  takes the value those stores left. The port read it first. (The same fix
  had to address the second record a row strip queues as a set of fields `$08`
  further on rather than a record at a tail `$08` further on, so a tail near
  `$FF` doesn't fold the second record onto the first.)
- **The saved data bank is on the stack while the strip is written.** The four
  strip builders put the data bank on `$7E` for their loop and pull it back
  after, and the loop writes through the upload buffer's cursor, so a cursor
  pointing into the stack page takes the saved byte with it and the routine
  comes back with whatever the strip wrote over it. The port had dropped the
  push, since it doesn't need a data bank to address bank `$7E`; it does the
  push and the pull now, and addresses the map through the data bank like the
  original.

**What fuzzing cost to set up**: three constraints, each for a reason, and each
one a statement about how the mode uses these routines. The upload buffer's
cursor is kept as the state has it (`--keep 7E00E0-7E00E2`), because a random
one sends the strip anywhere in bank `$7E` -- over the stack, where the
original loses the return addresses of the subroutines the port inlines and
goes somewhere the port never does, and over I/O space for the redraw's DMA
source, where open bus holds what the interpreter's fetches left and native
code sets nothing. The queue's tail is kept inside the queue
(`--fill 7E005F-7E005F=00-70`), for the same reason at the other end: a tail of
`$FF` puts records at `$7E:015F`, on the stack. And the scroll's step is kept
small (`--fill 7E0001-7E0001=00-08`, and again `F8-FF` for the other
direction), because the step is a count of tiles to move and each tile can
queue a strip, so a step of `$80` walks the buffer's cursor `$2000` bytes on and
round the bank into low RAM -- which the object scripts never ask for, since
the step is the whole part of one frame's move. With those three, 20000 states
each on two seeds for the two scrolls (each direction separately), the block
upload and the map centring, and 5000 for the redraw, none mismatching; and
5000 each of the plane number the routines refuse, which no movie reaches
either.

**The plane number is checked and then spun on.** Both scroll routines and the
redraw take the plane in the mode's first scratch byte and answer a number that
isn't 1, 2 or 3 with a branch to itself (`$C2:059B`, `$C2:069F`, `$C2:0A6A`).
The ports hand those addresses back to the interpreter rather than reproduce
the loop, declaring their own empty set there (`m_stop_at_own`), since the
accumulator and flags the original has at that point are exact and cheap to
keep. The redraw's stop is under the data bank byte it has already pushed,
which is the one piece of SNES mechanics its port keeps.

**What was left at the edge, and why.** The chunk is 1.4 KiB against the 6 KiB
a wave-2 chunk aims at, and the next coherent unit -- the screen's own setup,
`$C2:2720`-`$C2:2D90`, which the burndown puts at `$C2:27DE` (1385 bytes) --
was not taken. Its shape is plain: about fifteen routines that each decompress
one stream into work RAM, picked by a byte of the scene record at `$C6:FD00`
that `$C2:274D` finds from the location (`$1F0`-`$1F7`, the world maps), out of
ten tables of long pointers at `$C6:FDF0`-`$C6:FFFF` that `re/hints.toml`
already types as `lz`. What is missing is what each stream *is*: two of the
fifteen are nameable from this chunk's work (`$C2:295E` loads the first plane's
tile groups to `$7E:3000`, `$C2:298C` its map to `$7E:4000`) and one from MD4
(`$C2:2A16` loads the scene's object scripts to `$7F:0400`), and the rest would
take their names from their destination addresses, which is the transcription
M5 exists to stop. Identifying them is resource typing (MATCHING.md's MD2), not
porting, so that run is worth taking after it rather than before. The two
drivers at the end of it (`$C2:2C1D` and `$C2:2C93`) also run straight through
two entries that already exist in `menu/display.c` (`$C2:2D47`, `$C2:2D70`), so
they belong with that file's loader half, which is its own chunk.

## Phase 3, wave 2: the enemies' minds (`$C1`)

The first chunk of bank `$C1`: how the enemies choose what they do and carry
it out. Their scripts' interpreter -- the choosing `$C1:8C09`/`$C1:AFD2`, the
reactions `$C1:B488`, the 41 conditions, the 23 actions' choosing and turns,
the 57 selectors and the target lists (`$C1:8C09`-`$C1:B092`, `$C1:B488`,
`$C1:B4AA`) -- the target search and the battlefield's layout questions
(`$C1:28B0`-`$C1:2D80`), the direction between two points `$C1:0222` and
the scaled sine `$C1:01F9`, the random numbers `$C1:AF22`/`$C1:AF79`, a
combatant's knock-out in the turn's update (`$C1:B279`, but for a member that
comes back once), and the beaten enemies' finish `$C1:AC5E`. About 11 KiB of
65816. `python3 tools/burndown.py` for `$C1`: the battle
subsystem from 84% left (10.6 KiB ported, 0.5 KiB inlined, 42 entries) to
52% (31.1 KiB, 10.9 KiB, 202 entries). That is more than the chunk: the loop's
entries now reach, without a call-out, the code they run as C and as siblings,
which the tool credits to them (its docstring says which way it is wrong).

**The engine is C on the game's state.** `battle/enemy_scripts.c` is the
choosing: every enemy that has nothing chosen looks through its script's
turn section, block by block, until a block's conditions hold; the reactions
look through the reaction section the same way for the actor. A choosing's
state is a `ScriptChoosing` its starter makes and passes down -- the enemy,
the pair of conditions, the outcome, how far the script has been read, the
block and its actions -- and the original's cells for it are written once,
as scaffolding, where it ends. The conditions (`enemy_conditions.c`) and the
actions' choosing (`enemy_actions.c`) are C tables, a row a command: its
operands' layout, which decodes them into the test's named parameters, how
far the original reads the script for it, and the original's routine it is
timed as, a literal. A condition returns how it came out (a
`ConditionResult`: fails, holds, holds with nothing chosen, chooses and
fails, and whether it took a target) and the block's testing acts on it; a
choice returns how far it read and what it does to the block. The selectors (`targets.c`) a table of
what each picks; the search (`positions.c`) a function of the question and the
three combatants, with the grid, the boxes and the direction as operations of
their own. None of it takes a `Snes *`: the entries' wrappers put the answers
where the original's callers look for them, and the machine side is the
enemy's turn (`enemy_turn.c`), which calls the original's code for what is
still generated -- the effect's working out, the attacks' and techs' setting
up and targeting, an enemy's coming in, the message, the showing -- and runs
its siblings (`battle_run`) where the original makes a JSR: the action's turn
from its C table, the showing.

**The main loop calls its own subsystem's ports as C.** The choosing at the
battle's start and after every action, the timers' order drawn at random, the
damage table's and the numbers' clears, the combatants' low HP and statuses,
the reactions' conditions: C where they were call-outs through ROM addresses.
The enemy's turn, the update of every combatant, the combination check, the
turn itself and the beaten enemies' finish are siblings the loop runs as its
JSRs would. The turn is the port's where its guard holds and the original's
where the turn's place names no combatant (`battle_take_turn`, from the loop
and from the timers' dispatch), since running a sibling skips the entry's
guard.

**The guards follow what the entry takes up.** A script holds only the
commands the tables have, but a place the game never leaves (a fuzzed one)
can land on bytes that aren't commands, which the original dispatches past its
tables into the next table or into code. So each entry's guard looks at what
it would take up from the place: a condition entry at the selector it names
and the actions after it (past the first end marker after each byte the
condition can leave the place on), an action entry at its selectors, a draw
among four at the four, the choosing at the actions it keeps, the enemy's turn
at the action it carries out. Each entry's guard is made with it by the same
macro and knows its command as a constant, and the layouts it checks are the
tables' rows. None refuses a call of the library's (over the 26 movies every entry's calls
match, none refused and none mismatching).

**The review's rework.** The first review rated the three script files a
translation: the engine kept its working state in the original's cells (the
script's place written and read back, the outcome counted in a cell, the
enemy at work a cell used as the loop's variable), read operands by position
with the layout derived again in the guards, called the next step from inside
the handlers, and knew its entries by the machine's PC. They were rewritten as
above. Three cells the ports wrote only as scaffolding turned out to be read
back as game input, and are now kept plainly by every port that writes them:
the long arithmetic's leftover high word `$2E` (the random draw divides with
it: `battle_long_high_word`), the high byte `$0B` of the stat offset the
stat test reads (whatever the last word kept at `$0A` left), and `$0E`,
which an enemy tech's hits are noted as where its targeting doesn't set it
(the tech's number, or whatever an action, a condition or a selection last
left there; every write of it in the scripts and turns is kept). The
native-shape build had read them stale. The re-review added the cells a
candidate past the combatants reads its stats from, wherever the table of
the stat blocks' places points: the conditions that read candidates' stats
keep what they write at `$08`, `$0A` and `$0E` too (the stat test's count
among them), since its doubled walk goes past the list. `tools/fuzz_compare.py` against the
reviewed ports (135 routines, 5,000 states each, seed 1): 133 the same, and
two differ where the walk's stray box read lands on the choosing's cells,
which the rework writes when a choosing ends (writing them before each
condition, as a check, brings both to 0; an accepted divergence, docs/TODO.md).
The rework moves where the time of the conditions, the choices and the
turns' parts falls, so the battle's rows the library measures were
calibrated again (44 of them, only battle's taken); the library is clean
under shadow mode and the gate passes.

**What shadow mode and fuzzing found**, all reproduced rather than tidied:

- The target search's finds store the slot found as a word, so the byte after
  it is cleared (`battle_target_found_high`).
- The side tests (below, to the right, level, the halves and edges) count
  their failure cell down from what the search left in it, rather than set
  it.
- The stat blocks are reached two ways, and the table of their places is
  indexed with the slot doubled in a byte by most routines and in a word by
  the stat tests (`$C1:9130`, `$C1:918E`, `$C1:91F9`, `$C1:95FA`) and the HP
  test `$C1:90BE`; an index past the combatants reads wherever the table's
  word points, into the next bank where it carries.
- The test of a stat's value (`$C1:95FA`) goes on from a candidate to the one
  at twice its slot in the list: it keeps the doubled slot in X for the list
  as well.
- A condition that finds nobody holds without choosing for the side tests,
  fails for others; the side tests set the search's question and searcher
  whether or not a search runs; the side test's search is the third operand
  plus six, in a byte.
- An enemy present in the fight is looked for until the enemies' count, from
  the first, by a 16-bit index: with no enemies it reads on for 256.
- The selector in A (`$C1:AC2C`) is doubled in a byte, so its top bit is
  lost.
- A party member picked at random has the party updated again for each
  draw; the draw among four drops its own return and goes on with the
  choosing's loop at `$C1:8C90`, leaving the script's place on a byte it
  stepped over until the loop puts it back.
- The carry the attacks', techs' and their targeting's setting up read is the
  one the original's code before them leaves: the attack's number compared
  with 1, the marking of the targets from a first target compared with 3, a
  stat change's.
- The turn wait taken away to exactly 0 stays 0; to below it, 1.
- A member that comes back once is the original's from its test of the
  revival byte, with A's high byte and the carry clear.

**Fuzzing.** Every new entry, seed 1, on the `gato-battle` state with the
constraints in `tools/fuzz_routines.toml` (161 routines,
none mismatching: the selectors, the search and the reach tests 20,000
states each (18,700-20,000 compared, a member that would come back turned
away; the selector in A 8,836, where its guard takes the table's), the
conditions 20,000 each (12,900-15,700 compared, the rest turned away as above:
bytes that aren't commands), the actions' choosing and the choosing's entries
16,900-19,900 (all of them for the choosing of every enemy, 5,000), the
actions' turns 20,000 each (8,300-18,700 compared, a message or a tech's
setting up running past the call-out budget), the enemy's turn, the tech's
turn and body and the showing 5,000 each (3,400-4,250 compared), and the
beaten enemies' finish 5,000 without an enemy beaten and 5,000 in a demo
battle, where the battle's step that waits for frames is skipped). The ports rewritten
around it compare with `main`'s with `tools/fuzz_compare.py`: 47 routines,
none differing.

**Timing.** The battle's 89 rows the library measures were calibrated from
the shadow runs (`tools/timing_gate.py --calibrate`), only battle's taken: the
new entries from a default of 3,000, `battle_main_loop_body` from 7,292 to
33,695 (the choosing and the timers' orders drawn run in its time now),
`battle_update_all_combatants` from 11,157 to 17,210 (the knock-out in C).
Every entry is within the limits, with no exception added; the entries the
library doesn't reach keep 3,000. Running the turn inside its caller's stretch
costs shadow mode some comparisons: `battle_give_turn` is compared within the
timers' dispatch and the loop's pass rather than on its own (4,613 of 4,626
calls), and those two are interrupted more often (3,620 of 27,532 passes and
5,704 of 17,952 dispatches against `main`'s 299 and 3,383); fuzzing covers
them, as the predecessor's counts do

**What is left** of the enemies is in TODO.md, "What battle's rewrite left":
the call-outs from an enemy's turn, the timers' kinds' handlers, and the
quirks and the one accepted divergence (narrowing the candidates by the aim,
which the original never leaves where every candidate is marked).


# Taking each subsystem off the machine (Phase 3a)

Subsystem by subsystem, how the ports moved off the emulated SNES: the logic
into functions over the game's own state, the machine into a thin wrapper at
each entry. The rules are in CLAUDE.md and docs/PLAN.md; this is what each
subsystem's move actually took.

Split out of docs/RECOMP.md, which keeps the mechanisms, the current
phase and an index of these files.

## Phase 3a: the scene subsystem off the machine

The first whole subsystem of PLAN.md's Phase 3a, after the circle pilot
(`game/scene/circle.c`): the other ten files of `game/scene/`, 73 entries, taken
together so the vocabulary they share was written once (75 registered
entries in all, the circle's two among them). Nothing was added to
the burndown; what moved is portstyle's two numbers. Before, scene scored 0.20
operations per line with five files on the transcription backlog (`program.c`
0.52, `race.c` 0.34, `display.c` 0.30, `nmi.c` 0.18, `boot.c` 0.17), and 196 of
its 215 functions took a `Snes *`. After, it scores 0.05, no file is on the
backlog, and 143 of 254 functions take the machine: the 74 functions the 75
entries run, 42 guards, and 27 functions that are either shared pieces of the entry
wrappers (setting the widths, the flags a compare leaves, the page the caller
is on) or bridges around call-outs, all listed below. (`portstyle.py`'s own
column says 90% to 54%; its function matcher misses one-line definitions, so
those are counted here by name.)

**The shape.** Each routine's work is a static function of `GameState *` and,
where it touches the host, `Platform *` -- the two handles apart, as decided
before the rewrite began -- with plain C parameters and results. The entry
wrapper turns the machine into those (`state_of`, `m_platform`), reads the
registers the routine takes, calls it, and puts back only what liveness says
the callers read. That last part is what made most wrappers thin: every entry
now declares the largest don't-care set `tools/dontcare_check.py` accepts, which
for the program's display and race entries is everything but a register or two
(the RAM program's callers are in the analysis since the M3 image work), so the
`LDA #$80 : LDX #$0400 ...` register traffic the display ports reproduced to be
compared is gone rather than moved. `scene_setup_mode7_display`, which
reproduced every load the original made on the way to each store, now leaves
A's high byte and the M flag; the eight race stops leave nothing.

**What the accessors couldn't say, and what was added.**

- *Cells on the caller's page.* The multiply and divide helpers work on
  `$F0-$FF` of whichever page D is on: the scene's own, or a sequence channel's
  when a script calls the program (5412 of the race's 19842 multiplies). The map
  has no accessor shape for a variable on a caller-chosen page, so
  `scene_math_scratch` became a 16-byte buffer and `scene/scratch.c` takes the
  page (`ScenePage`: a channel, or the scene's own) and an offset, reaching the
  bytes through `scene_math_scratch_byte` or `scene_channels_math_scratch_byte`.
  The first guard written for these (`D = 0`) turned those 5412 calls away,
  which is how it was found: a guard's refusals show only as a drop in the
  entry's shadow count, so every rewritten entry's count was compared with the
  predecessor's, movie by movie. The map has that shape now, and the hand
  mapping is gone ("Page kinds", below).
- *The multiplier, divider and work RAM port.* These are CPU registers, not a
  platform service (PLATFORM.md, "Not services"), and not variables either;
  `game/cpu_io.h` names them as the game drives them (`hw_set_multiplicand`,
  `hw_multiply_by`, `hw_quotient`, `hw_divide_by`, `hw_port_seek`,
  `hw_port_read`, `hw_port_write`), taking the state because an operand or a
  cursor left for the next routine is state, with `runtime/port/state_io.c` the
  backing. Every subsystem's arithmetic has the same need (`battle/math.c`,
  `menu/epoch.c`).
- *ROM data from logic that has no machine.* `scene/scene_data.h`'s accessors
  took the machine. They take the state now, through `game/rom.h`
  (`rom_read8`/`rom_read16`, backed in `state_io.c`), which only a `*_data.h`
  accessor is meant to call; the menu's two stream-table readers keep the
  machine until `menu/object_commands.c` is rewritten.
- *Buffers the game locates at run time.* 18 new uses of `game/wram.h`, 22 in
  the subsystem: the scaler's source and output rows (the data bank says where,
  `$7F` in the race), the bank `$FE` copies' destination, the tile source
  `$7E:5E54` reads through the data bank, the next step of a task whose page is
  the caller's (`$7E:5DA7`), the race's line-effect descriptor table, the
  sequence scripts' bytes and their store command's target, and the sprites'
  four-byte sources. Each is data a caller points at, not a cell; the map could
  name the script area and the race's descriptor table as fixed buffers of the
  program image, which is the next step for most of them.
- *Byte offsets into a record.* The sprite writers index the OAM buffer by a
  byte offset and a high-table pointer, not a sprite number, and fuzzing puts
  odd offsets there; the core's `oam_buffer` record can't take them, so the map
  has `scene_sprite_table` and `scene_sprite_high_table` over the same bytes
  (`$0700-$08FF`, `$0900-$091F`) for the scene.

**The guards.** A port that names its cells through `game/vars.h` knows them
where the scene keeps them, while the original reaches them through D and the
data bank; so every such entry now says, in its guard, that it runs on the
page and bank the game always enters it with (`scene/guards.c`, and the
predicates `m_bank_reaches_low_ram`, `m_bank_reaches_io` and `m_direct_page`
added to `runtime/port/machine.h` for every subsystem's page guards). With the
movies' per-entry counts compared against the predecessor's, none turns a call
away except the one noted below.

**What shadow mode found**, over the attract sequence and the race while the
files were rewritten, before the library: the task lists' call-outs push P after the load that finds
the task (so its N and Z are the task word's), and carry the loop's compare's
carry from one task into the next (both only when a task runs, which the fuzz
constraints that empty the tables never let happen); and `scene_frame_work`'s
callers read A's high byte, which the 16-bit task list leaves as the frame work
set it before the call. Then the full library, clean.

**Differential fuzzing, and a tool for it.** Every rewritten entry was fuzzed
against the port it replaced with the same state, seed and constraints
(PORTING.md, "Fuzzing"): `tools/fuzz_compare.py` runs two `ct_fuzz` builds side
by side and reports both counts per routine, from constraints kept in
`tools/fuzz_routines.toml` (one entry per registered routine, with the reason
for each constraint; the ones PORTING.md's rows recorded for the scene's
entries, made exact). A routine can also be listed with `variant = "loose"`,
fewer constraints, which is where a reordered access shows: the sprite writers'
loose runs, with the entry offset and the high-table pointer anywhere in low RAM
a list can't run past, caught the first form of the rewrite -- the named table
wrote to bank `$7E` where the original, through a data bank of 0, reaches I/O
space past `$2000` (19569 of 20000 states against 22, until the cursors were
kept in low RAM, where both agree). Every entry's predecessor and rewrite counts
are in its PORTING.md row; all agree.

**What it cost.** Recalibrating the scene's `cost_milli` rows: a port that
reads a byte cell as the word the accessor has, or writes a result once
instead of a step at a time, makes a different number of bus accesses.
Sixteen of the scene's rows moved. The arithmetic moved most, because it
now keeps its rounds in C variables and touches the scratch cells once at
each end: `scene_divide` 2053 to 140438 (32 rounds of shifts and subtracts in
about 40 bus accesses), `scene_divide_8_8` 2372 to 32485, `scene_multiply` 2866
to 6178, `scene_multiply_signed` 3813 to 5708, and `scene_write_matrix_code`
4467 to 19279, whose 5153 port writes are now nearly all its accesses. The
rest moved by a few hundred where the wrappers stopped reproducing dead
registers or an accessor reads a word where the original read a byte
(`scene_frame_work` 3393 to 3006, the display and race rows, the sprite writers,
the task lists). Nothing needed an exception: one scalar fits each of them, since
every one of these does the same work on every call or scales it with the same
loop the original has. The whole suite over the library, with the shadow logs
of the first run calibrated from, then passes; and the per-entry shadow totals
over the 26 movies -- calls, compared, interrupted, skipped -- are identical to
the predecessor's, entry for entry.

**Left on the machine, and why** (the 27): the bridges around call-outs --
the frame work and the NMI (`run_frame_work`, `read_pause_button`,
`update_song`, `run_task_list`, `send_pause_sound`, `can_continue`), the task
lists (`run_tasks`, `call_task`: the tasks are the program's code), and the
sequence interpreter's loop (`run`, `call_program`, `send_sound`, `past_call`,
`machine_as_expected`, `can_continue`), whose commands are logic but whose
program calls and driver sends are the original's; the pieces the entry
wrappers share (`set_m8`, `load_status`, `set_widths16`, `put_flags`,
`compare8`, `caller_page`, `copy_entry`, the two files' `set_vram_address`,
`upload_column_entry`); two guards that other guards call; and the pilot's
`scene_circle_draw`, the C call `menu/object_commands.c` makes into the circle
while that port is on the machine.

**One call the guards can leave to the original.** `scene_find_divisor` reads
its limit through the data bank; in one iteration's run of the race movie one
call came with a data bank that doesn't reach low RAM, where that cell isn't
the scene's, and its guard left it to the original (the suite's runs, and the
predecessor's, had none).

## Phase 3a: the event subsystem off the machine

The second subsystem of PLAN.md's Phase 3a: all of `game/event/`, the event
script's engine (2 entries) and its 185 command handlers, 15 files that were
all on the transcription backlog. Before, event scored 0.46 operations per line,
the worst subsystem in the tree, and all 251 of its functions took a `Snes *`;
its shared vocabulary (`script.h`) was one of instructions -- `iny_tyx_sec_rts`,
`ldx_current_object`, `var_offset_here` -- not of the script language. After,
it scores 0.02 and no file is on the backlog. Counted by name (portstyle's
matcher misses one-line definitions and the macro-generated entries, so its
own column says 29%), 255 of its 507 functions take the machine: the 187
entries (149 generated by one macro, 36 hand-written bridges and the engine's
two), 15 guards, the 8 functions of the shared translation, and 44 bridge
helpers around call-outs, 29 of them in `movement.c` (below).

**The protocol, designed first.** Every handler has the same calling
convention in the original -- entered through the table at `$C0:5D6E` with the
command's offset in Y, returning X = where the script goes on and the carry
set to keep running the object's turn -- so it was written once, as C types,
before any handler (`event/script.h`):

- `EventCommand` is where the command is; operands are read by their position
  in it (`operand(g, c, 1)`, `operand_word`), objects, party members and
  variables through helpers that say what the operand names
  (`object_operand`, `member_object`, `variable_operand`, `var8`/`var16`).
  Script offsets are 16-bit and wrap, as the index registers do.
- `EventStep` is what the interpreter does next: `next_command(c, length,
  ...)`, `end_turn`, `wait_here`, `jump_from` (every jump in the language is
  relative to its jump byte, and a jump keeps its distance in the scratch cell
  as the original does), `branch_unless` for the conditions.
- Every handler is then `EventStep handler(GameState *, const EventCommand *)`,
  named for what the command does, and `EVENT_COMMAND(entry, handler)` makes
  its registered entry: one shared translation (`event/bridge.c`,
  `event_command_run`) reads Y, runs the handler and returns as the original
  does, so `game/registry.c` still gets one function per entry and none of them
  is written by hand. The two handlers that read the platform (the sound
  driver's status) use `EVENT_PLATFORM_COMMAND`.

**Where the protocol didn't fit, and what it carries for it.** The convention
turned out not to be uniform in what callers can read, and liveness settles
what the step has to carry:

- *Y.* The original's Y at return -- where the command's parse of its
  operands stopped, which differs from command to command -- is live: the
  map-setup run of object 0 (`$C0:595C`) returns to callers that pass it to the
  menus' dispatcher at `$C2:8C36`, which stores it at `$04C0`. So `EventStep`
  carries it (`cursor`), each constructor taking where the parse stopped; a few
  commands leave something else there (`$E6` the speed it set, `$7B` the
  object, the bridges what their callee left), and the comparisons' operator
  call runs with 8-bit index registers, which clears its high byte -- shadow
  mode found that one on its first run.
- *V.* The overflow of the commands' additions (the jumps' offset
  arithmetic, the variable arithmetic, the calls' saved-position index, the
  block copy, the on-screen test's subtractions) is live too, conservatively:
  the field loop carries it into code it leaves through JMLs. So the step
  carries it (`overflow`, or kept), computed from the operands.
- *A.* A's high byte looked live after every handler, which would have kept A
  in the protocol. It isn't: the dispatch widens the next command byte with
  `AND #$00FF`, which the liveness analysis took for a read of both bytes. The
  analysis now knows a 16-bit `AND #$00xx` (or `ORA #$FFxx`) sets the high
  byte whatever it held (`tools/disasm/liveness.py`, with a test), and every
  handler entry declares `DC_A | DC_N | DC_Z`. The change only makes the
  analysis more precise: nothing else in `game/` needed a change, and several
  field entries could now add `DC_A` too (not done here).
- *The carry, in one handler.* `$D6` (take a character out of the party)
  leaves the carry of its last comparison, or the one it was entered with when
  it compared none; its entry is a two-line bridge that reads that from the
  machine.

Both are a trace of the original's engine, not of what a command means: a
build without that engine drops `cursor` and `overflow` and keeps `next` and
`go_on`.

**The vocabulary** that replaced `script.h`'s instructions: the protocol
above, `current_object`, `slot_of`, `set_var8`, `memory8`/`memory16`, the
scratch cells as `keep8`/`keep16` (they are still written as the original
writes them), and in the files the commands' own -- `start_mode`,
`end_timed_play`, `priority_from_map`, `place_on_tile`, `callable`,
`preempt`, `queue`, `saved_position_index`, `take_latched`, `start_dialog`,
`take_channels`, `face_the_way`, `start_walking`, `tile_arrival`. ROM reads
go through `event/event_data.h` (the bit masks, the random bytes, the facing
for a direction, the command table), backed by `game/rom.h`.
`tools/disasm/eventscript.py` gives 36 commands the names their ports
established (`if_a_held`, `if_b_pressed`, `set_player_control`,
`set_speed`, `set_solidity`, `set_blocking`, `make_object_uncallable`,
`get_party_leader`, `set_result_from_variable`, `store_location`, ...), and
the entries carry them; the `$16` dispatch's derivation follows the rename.

**What the accessors couldn't say, and what was added.** The map gained 20
entries, and four fields of the location triples, for what the commands touch: the script buffer as a
buffer (`event_script`, to the end of bank `$7F`, since offsets reach it), the
scratch cells (`event_scratch`, `$D9-$E4`), the pointers `$48`-`$4D` keep, the
saved position kept across call-outs, the comparisons' resume offset, the
player-control flag (`$1F`: Start pauses the field only while it is set), the
animation update's scanline limit (`$6B`), the choice lines of a dialog, the
tile copy `$E4`/`$E5` describe, a stored location, the object bytes `$0F01`
and `$1001`, and the flags the event hands the battle (`$2A1F`, bit 6 keeping
the song); `field_direction` and `field_target_object` became words, as the
commands write them, and the location triples gained their fields. The
multiplier and the divider are driven through `game/cpu_io.h`, which gained
`hw_set_dividend`. What the map can't name is the memory commands' 24-bit
pointers and `$4E`'s block copy, 8 uses of `game/wram.h` in `memory.c` (event
had none before, because it had no accessors at all): those are buffers the
script locates, anywhere on the bus.

**The guards, and a hazard they found.** The handlers name the object tables
by slot while the original indexes them with the object doubled through the
data bank, so every entry is guarded (`event_guard`): D on the field page, the
data bank `$00` or `$80` (whose upper half is the `$C0` tables the handlers
read), the running object an even offset below `$100`, the command not in the
script's last 16 bytes (whose operands would reach the I/O register past bank
`$7F`), and per command what
its operands must be -- an object operand even, a party member's object even
(or none, for the calls that check), a comparison's operator in its table, a
bit number whose mask stays in its bank, a dialog string table, a return that
finds a level before the search comes round (`$00` never stops otherwise).
These are the first guards that read work RAM, and the first run found that
they cost time: a guard runs between the original's instructions, and its bus
reads landed in shadow mode's record of the original's run, so the generated
blocks checked against that record were 8 or 16 clocks slow (16209 block
mismatches over three movies, none of them a port's). Guards now look through
`vars_peek` (`runtime/port/vars_ram.h`): the generated accessors read with
`snes_bus_peek` while it is on, taking no time and leaving the bus as it was.
After a call-out, each bridge goes on only if the callee left the machine as
the command runs (widths, D, the data bank: `event_machine_back`) and
otherwise stops at the call's return, declared (`m_stop_at_own`), where the
predecessor returned without declaring it -- which fuzzing counted against it
(below).

**The engine** is a C loop over the protocol with the dispatch as its one
bridge: a turn runs commands until one ends it or the budget is spent, and a
command is run through the original's table with the registers the original
has there (the doubled command byte in A and X, the carry the 16-bit shift
clears), since most handlers are ports registered at their entries and the
rest are still the original's code. It hands a turn back to the original when
a handler leaves it where the port's accessors wouldn't reach what the
original's do: another data bank, an odd running object, or the last script
offset, which reads the first byte past bank `$7F`, an I/O register holding
whatever was last on the bus.

**Movement.** The motion is the field's (`field_direction_to_target`,
`field_object_in_the_way`, `field_start_step`, the walking and standing
animations, the line-up routines), still called out to, so the move commands
are bridges: the decisions (where the target is, whether the object is next
to it, on its tile, lined up) are plain C, and the calls are made with what
the original has at each. Two things made that harder than it looks. The
carry: `field_object_in_the_way` and the line-up routines answer in it, and
`field_direction_to_target` passes on the one it is called with, so declaring
the carry dead at those calls let shadow mode hand the port its own stale
carry back (a declared-dead flag the callee leaves as it found it is the
port's again, as ON mode would have it); the port now computes the carry the
original's tile comparisons leave and calls with it. And the call sites: the
don't-care check reads a call-out's site from a helper's parameters, so the
move-to-tile commands' sites are table rows, one table for the moves that
turn and one for those that keep their facing, rather than offsets from a
base.

**What shadow mode found**, beyond the parse position's high byte: the
carry above (the move-to-tile commands in the Blackbird movie called the
standing animation where the original walked on), and the guards' time. Then
the full library, 26 movies, clean; and the per-entry shadow totals (calls,
matched, mismatched, interrupted, skipped, abandoned) are identical to the
predecessor's for all 798 hand ports, event's 187 among them, so no guard
refuses a call the game makes.

**Differential fuzzing.** Every entry is in `tools/fuzz_routines.toml` twice,
on a field state (`field.ctm`, frame 11400): tightly, with the field's page
and data bank, the running object a slot and the script offset below `$8000`;
and loosely, with the offset anywhere, so operands past `$DFFF` are read
through bank `$80`'s mirror of low RAM and can land on the cells a command
writes. `tools/fuzz_compare.py` takes an entry by address now (`at`), since the
rewrite renamed 36 of them. Seed 1, 20000 states each (2000 for the map setup),
summed by file:

| Files | Variant | Entries | States | Mismatched before | of | Mismatched after | of | Turned away by the guards |
|---|---|--:|--:|--:|--:|--:|--:|--:|
| `animation.c` | tight, loose | 7 | 280000 | 0 | 280000 | 0 | 139804 | 140196 |
| `arithmetic.c` | tight, loose | 24 | 960000 | 0 | 960000 | 0 | 473607 | 486393 |
| `calls.c` | tight, loose | 14 | 560000 | 0 | 560000 | 0 | 181234 | 378766 |
| `dialog.c` | tight, loose | 7 | 280000 | 0 | 280000 | 0 | 139372 | 140628 |
| `engine.c` | tight | 2 | 22000 | 0 | 13510 | 0 | 6769 | 10074 |
| `facing.c` | tight, loose | 12 | 480000 | 0 | 480000 | 0 | 175696 | 304304 |
| `flow.c` | tight, loose | 19 | 760000 | 0 | 760000 | 0 | 265897 | 494103 |
| `input.c` | tight, loose | 17 | 680000 | 0 | 680000 | 0 | 339524 | 340476 |
| `locations.c` | tight, loose | 10 | 400000 | 0 | 400000 | 0 | 188123 | 211877 |
| `memory.c` | tight, loose | 16 | 640000 | 0 | 640000 | 0 | 319552 | 320448 |
| `movement.c` | tight, loose | 20 | 800000 | 4 | 799880 | 0 | 285846 | 514096 |
| `properties.c` | tight, loose | 13 | 520000 | 0 | 520000 | 0 | 239988 | 280012 |
| `screen.c` | tight, loose | 13 | 520000 | 2 | 500279 | 0 | 249732 | 260364 |
| `sound.c` | tight, loose | 6 | 240000 | 1 | 179889 | 0 | 89854 | 120168 |
| `variables.c` | tight, loose | 7 | 280000 | 0 | 280000 | 0 | 90460 | 189540 |
| **all** | | 187 | 7422000 | 7 | 7333558 | 0 | 3185458 | 4191445 |

The rewrite mismatches nowhere. The predecessor's seven are its own: in
`$A0` and `$A1` (and `$9E`/`$9F` on seed 4) a callee that came back with other
widths, where it returned without declaring the stop the fuzzer then counts;
in the loose `$EC`, `$E4` and `$E5` operands read through the low RAM mirror
after the command had written the cells they land on, which the predecessor
read in another order. Getting there found, and the rewrite fixed:

- the comparisons took their operator's bit 7, which the original's 8-bit
  doubling drops;
- `$4E`'s block copy onto the stack changed the data bank the original pulls
  back (the port now keeps it on the stack across the copy too);
- the engine stored a turn's end through the data bank a handler left, and
  read the command byte past the end of the script: both handed back to the
  original now;
- the loose runs' reorderings: the rewrite read a command's operands before
  writing the scratch cells and pointers the original writes between them
  (and C's unspecified argument order hid one), so an operand read through
  the mirror saw the old byte. The handlers now read and keep in the
  original's order: the pointers a byte at a time, the tile and location
  commands an operand after each store;
- a hazard in `ct_fuzz` itself: it asked the guard before pushing the return
  address, so a guard reading an operand on the stack saw the byte the call
  then overwrote. It now asks it as the runtime does (PORTING.md, "Fuzzing").

The guards turn away about half of every run -- the odd running objects,
which the game never makes -- and more for the comparisons (the operator)
and the calls; those are counted in the skipped column, and in them the
original runs. Every run is fast: the whole comparison, both builds and both
variants, took under half an hour at two processes.


**What it cost.** Recalibrating event's `cost_milli` rows (`timing_gate.py
--calibrate`, which now reads a table macro with arguments after the cost, as
`EVENT_CMD`'s guard is): 73 of event's 187 rows moved, the rest being
unreached or unchanged. The rewrite reads bytes as the words the accessors
have and writes an 8-bit variable back as its word, which makes more bus
accesses and a lower cost per access (`var_add` 5969 to 4341, `move_to_tile`
3117 to 2560, the engine's per-frame turn 4150 to 3256, the map setup 4476 to
4173), and makes no stack or register traffic, which makes fewer (`call` 3906
to 4599). Nothing needed an exception: the gate's other limits hold for
every event entry at the calibrated values.

**Left on the machine, and why** (the 44 bridge helpers): the move
commands' calls into the field's motion (29 in `movement.c`), the sound
commands' driver calls, which check what the driver left before going on,
the party routines at `$C2:8004`, `$E4`'s tile copy, `$A8`/`$A9`'s facing,
`$AF`'s call into `$B0`, the engine's dispatch, `$E1`'s hand-off to the
original's location change, and each file's `back`. `field` is next and owns
all the motion routines those bridges call; once they are ports, the move
commands' decisions can call them as C and most of `movement.c` leaves the
machine.

## Phase 3a: the event language, and its scaffolding

A review of the rewrite above found it scoring 0.02 and still reading as the
ROM: one function per opcode (`loop_animation`, `loop_animation_0`,
`loop_animation_1`), operands read by position (`operand(g, c, 1)`), an engine
that found its handlers through the ROM's pointer table and an emulated `JSR`,
and emulation carried through every result -- a parse position as the third
number of all ~136 `next_command(c, 3, 2)`s, the V flag in `with_overflow`,
and 84 writes to scratch cells named for their addresses (`SCRATCH_D9`),
"because other code may read them". `portstyle.py` counts operations on
emulated registers, which none of these are. This change settled what reads
each of them, separated what nothing but the gate reads, and reshaped the
subsystem around a language table.

**The evidence.** `ct_dev --event-reads` (dev/event_reads.h, off by default)
follows what a command leaves beyond X and the carry, on the interpreter
(`--native off --recomp off`, so everything that runs is the original's): Y
and V from the handler's return until something overwrites them, through
pushes and pulls, and into memory when Y is stored; and every byte of the
field page's event cells (`$C1-$E4`) a command writes, until code outside a
command writes it again, each read classed by who makes it (the writing
routine, a routine it calls or that called it, a later command, code outside
any command, an interrupt). The call stack is followed from the calls and the
stack pointer; interrupt handlers are skipped for Y and V (they save and
restore what they touch). It needed one hook the bus didn't have, a work-RAM
observer beside the I/O one (`snes_bus_wram_observer`). Over the 26 movies,
1,546,991 commands:

- *Y* is read in one place: `STY $C0` in the menus' dispatcher (`$C2:8C36`),
  after the openings at map setup (99 times, after 20 different opcodes; 60
  following the port's engine at `$C0:5910`, 39 following `$C0:595C`, still
  the original's). It stores the low byte at `$04C0`, which nothing in the
  library reads afterwards. After the per-frame turns (`$C0:5AB1`) and
  `$C0:597D` nothing reads it at all.
- *V* is never tested after a command. Its only read is a `PLA` at
  `$C7:0A90`, the sound driver's error path discarding a pushed P, 69 times.
  Liveness keeps it live (the field loop leaves through JMLs it can't follow).
- *The event cells* are read by the command that wrote them -- its handler,
  or its own subroutines (the comparisons' operator routines at
  `$C0:6487-$64C1`, the pointer helpers `$C0:67A2`/`$C0:67E3`, each part of
  one C operation now) -- and by nothing else, with two exceptions: `$02`
  reads the high byte of `$DD` a `$96` left, and masks it away; and the
  menus' wave handler at `$C2:77C7` (`menus-end-of-time`, 182 reads) advances
  `$01E2` as a phase it never sets, and so starts from what `$02` or `$B5`
  last left there. That is a reader without a reason: a native menus port
  must give its phase a start (TODO.md).

**The classification**, then: none of the three has a meaning to name. The
parse position, the V flag, the scratch cells, the comparisons' resume offset
(`event_condition_resume`), the pointers `$48`-`$4D` keep, and the saved
position the commands keep, are all scaffolding: kept while the gate is
byte-exact, and marked. What stays real: `$E4`'s saved position, which the
tile copy it calls can overwrite; the turn's budget, which commands the
original runs can spend; and `$D6`'s go-on flag, which is the original's
carry but is what the engine goes on by.

**The mechanism** (`game/original_effects.h`, PORTING.md "Scaffolding"):
`ORIGINAL_EFFECT(expr)`, which the default build evaluates and a build with
`CT_WITHOUT_ORIGINAL_EFFECTS` compiles without evaluating.
`ct_game_native_shape` builds every port that way in every build and in CI.
Event's vocabulary for it is `event/leaves.h`: the record of the Y and V a
command leaves, which whoever runs it owns, and the scratch cells by the order
the original fills them (`KEPT_FIRST` ...), each file saying what its commands
keep there. What is the same every time a command runs -- the parse position,
the operands kept in the first cells, which additions leave their V -- is a
table of its own, `event/leaves.c`, by opcode, beside the language's and not
in it; the build without the original's effects compiles it out, and the
engine and the entries reproduce it around the operation, which never
mentions it. Where it varies with a path it is one marked call on that path
(`$06`'s absent member leaves its parse on the operand; `$E6` leaves the speed
in Y). Arithmetic computes `variable + amount`; the V it leaves is derived
after the operation from the amount and the variable as it is then, not by
ALU helpers in the operations. The multiplier and divider the calls, the
return and the jumps drive are the same: the operations compute the product,
and the hardware is left holding it as scaffolding. A service call-out whose
callee doesn't read some registers now says so, `PF_CALL_DC(site, target,
set)` (`runtime/platform/platform.h`), which `tools/dontcare_check.py` checks
like `m_jsr_dc`: the sound commands declare what the driver doesn't read
instead of loading A, X and the flags for it. `tools/portstyle.py` reports
the count: event had 0 marked before (and 59 uses of names taken from
addresses, 45 of them `SCRATCH_D9`), 75 marked after (and 10 such
names, all `field_setting_1e` and the menus' routine `$0A`, whose meanings
aren't established; TODO.md).

**The language table.** Each file ends with its rows: opcode, operation, how
its operands decode into the parameters the operation reads, by name
(`.animation = OPERAND(1)`, `.animation = CONSTANT(0)`, `.value =
VARIABLE_WORD(1)`, `.target = MEMBER_OBJECT(1)`, `.flags = BIT(1)`,
`.object = SELF` ...), length, jump operand, the guard's extra check, and the
handler's address, which names the command's time and its registered entry:

    { 0xAA, loop_animation, .animation = OPERAND(1), .length = 2, .routine = 0xC02E67 },
    { 0xB3, loop_animation, .animation = CONSTANT(0), .length = 1, .routine = 0xC02E87 },

The parameters are one list in `script.h` (`EVENT_PARAMETERS`: each a type and
a name -- `EventVariable variable`, `Width width`, `EventObject target`,
`WindowPlace window`, `LocationRecord record` ...), from which the row's
fields, the decoded `EventCommand` and the engine's decoding and checking
loops are generated. An operation takes the command and reads what it needs
by name (`c->animation`, `c->self`), with no adapter between the row and it,
and says what happens next (`next_command()`, `end_turn()`, `stay()`,
`take_jump()`, `go_to(at)`); the engine does the jumping. A command that
still calls out to the original's code is a row too, whose operation takes the
machine (`.bridge`), and its operands are named in its row the same way: a
bridge reads them decoded by its row (`event_command_of(s, 0x94).target`),
never by offset. The guard's extra checks get the decoded command too
(`compares_bytes` reads `c->comparison`). The table is where every ported
command is, 185 rows. Opcodes that differ only in how an operand is encoded
became one operation each:

| Family | Opcodes | Operations |
|---|---|--:|
| loop an animation (given, 0, 1) | `$AA $B3 $B4` | 1 |
| play a one-off (2 frames, n frames) | `$AB $B7` | 1 |
| assign a variable (number, variable, story, 1, 0; byte, word) | `$4F-$55 $75-$77` | 1 |
| add, subtract (number, variable, one; byte, word) | `$5B $5D-$61 $71-$73` | 2 |
| set, keep, toggle flags (bit number or mask) | `$63 $64 $67 $69 $6B` | 3 |
| record in the story (number, variable; byte, word; progress) | `$56 $58-$5A` | 1 |
| raise, lower a story flag | `$65 $66` | 1 |
| peek, poke (byte, word; number, variable) | `$48-$4D` | 2 |
| compare (variable with number or variable; byte, word) | `$12-$15` | 1 |
| wait (n, 4, 8, 16, 32 frames) | `$AD $B9-$BD` | 1 |
| buttons held, assignments held, pressed | `$2D-$44` (17) | 3 |
| face (self or an object; a direction, operand, variable) | `$0F $17 $1B $1D $1E $1F $25 $26 $A6 $A7` | 1 |
| where an object stands, which way it faces (object or member) | `$21-$24` | 2 |
| call (object or member; now, when taken, and wait) | `$02-$07` | 3 |
| take calls, refuse them | `$08 $09` | 1 |
| speed (number, variable) | `$89 $8A` | 1 |
| stand on a tile (numbers, centred; variables) | `$8B $8C` | 1 |
| sprite shown, hidden, none (self or an object) | `$7C-$7E $90 $91` | 1 |
| say a line, ask a question (where the window goes) | `$BB $C1 $C2`, `$C0 $C3 $C4` | 2 |
| set the result (variable, story) | `$19 $1C` | 1 |
| where the party is, or a place put aside | `$DC-$DE` | 1 |
| wait for the fade, for the tint | `$F2 $F3` | 1 |

150 of the rows are operations, over 70 functions; 35 are bridges. Each
registered entry is named for its operation, with what tells a family's
opcodes apart (`event_cmd_loop_animation_0`, `event_cmd_wait_frames_16`,
`event_cmd_if_pressed_a`). The subsystem's concepts have files of their own:
the language (`script.h`, `engine.h`, `engine.c`), the scripts' variables and
the story (`variables.h`, `variables.c`, `story.c`), the party (`party.h`,
`party.c`), dialog (`dialog.c`), calls between objects (`calls.c`), the moves
(`movement.c`). Where a command changes a field object's state -- animation,
position, layering, whether calls reach it -- it goes through small local
helpers named for what they do, grouped at the top of each file
(`restart_animation`, `interrupt_loop`, `back_to_loop`, `put_on_tile`,
`put_at`, `relayer`, `set_priorities`, `set_steps_per_move`,
`layer_as_the_map`, `calls_skip`, `take_off_the_map`, `stop_moving`, `turn`,
`aim`, `aim_at_object`, `head`, `set_steps`, `count_move`, `mark_launched`),
which field's object module replaces when it lands (TODO.md lists the
definitions a review found duplicated between them).

**The engine** (`engine.c`) is plain C: a turn, with its budget and its stop
at the script's last offset, keeping where the script stopped; and the map's
openings. It runs each command through a runner the caller gives it; on the
development system that is `event/bridge.c`'s, which runs a row's operation
as C, a row's bridge on the machine -- called directly, with the return
address the original's `JSR` would push, and its time named with
`native_time_as` at the row's handler -- and only what has no row, or a state
the port refuses, through the original's table and an emulated call. A bridge
that calls another port of the subsystem calls it the same way
(`event_call_bridge`: `$AF` runs `$B0`'s, `$B5` and `$B6` the moves toward an
object and a party member), never through its ROM address. The port takes a
command when `event_runs_here` does: the running object one, the command at
least 8 bytes from the buffer's end (so no operand is read through bank
`$80`'s mirror of low RAM, which also lets the rows decode every operand up
front), what the operands' kinds say (an object operand even, a member's
object one, a bit number whose mask stays in its bank) and the row's own
check. After each command it hands the machine the X and carry the command
returns and, as scaffolding, the Y and V the original's handler would have
left. Each row's handler stays registered: an operation's by a generic entry
that runs its row, a bridge's by the bridge itself, each with its row's
guard. `$C0:595C` and `$C0:597D`, still the original's, reach the handlers
through the table, and `ct_fuzz` exercises each alone; the call logs show no
other code reaching a handler.

**What shadow mode found.** Over the library nothing mismatches, the engine's
two entries match on every call, and the per-entry totals of every hand port
but the bridges are identical to `main`'s. The bridges' change shape:
shadow mode runs the original's engine, which calls each handler at its
entry, and compares the port's registered entry for it; a handler with no
call-outs is compared there as before, but a bridge the port's engine calls
directly makes its call-outs as the engine's, so shadow mode abandons the
bridge's own comparison ("started inside the engine's stretch"). 506,381 of 518,396
bridge calls over the library are counted abandoned where they were matched
(`event_cmd_behave` 250,698 of 251,565, the moves toward the party 72,101 of
72,121, ...), still checked at every call-out they make and at the end of the
engine's turn, and fuzzed one by one below. What the review expected to find
in the handlers' totals is here, in the bridges'.

**Differential fuzzing.** `tools/fuzz_compare.py` against `main`'s `ct_fuzz`, every
event entry, seed 1, both variants (372 runs, 7,422,000 states):

| Variant | Entries | States | Mismatched before | of | Mismatched after | of |
|---|--:|--:|--:|--:|--:|--:|
| tight | 187 | 3,722,000 | 0 | 1,595,786 | 0 | 1,595,919 |
| loose | 185 | 3,700,000 | 0 | 1,589,672 | 0 | 1,396,820 |

The loose runs compare 192,852 fewer states: the commands within 8 bytes of
the buffer's end, which the guard now refuses (their operands would come
through bank `$80`'s mirror of low RAM). The engine's runs compare a few more
(6,823 turns against 6,695, 79 openings against 74), since a bridge it calls
directly no longer runs past the fuzzer's budget as a call-out. The first
runs found three real differences, each fixed: `$07` (call a party member and
wait) had taken the looser guard of `$05`/`$06`, where its waiting path goes on
with an empty place's marker (1,262 states); `$E4` kept its saved position in
a local where the tile copy's indexed stores can overwrite the cell (274); and
the turn counted its budget in a local where a command the original runs can
spend it (4 of the engine's).

**What it cost.** The engine spends its time on its commands' work, each
attributed to its row's handler (`native_time_as`, the bridges' as well
now), and the rows run differently from the handlers they replaced. So event's
rows were recalibrated (`timing_gate.py --calibrate`, event's rows only):
over the two reviews 87 of the 185 handlers' `cost_milli` moved -- `yield`
4750 to 2375 and `halt` 4000 to 2000, which touch the bus only to decode,
`var_add` 4341 to 3183, `call` 4599 to 5270 -- the per-frame turn 3256 to
4500, and the openings at map setup 4173 to 5936. Decoding the bridges'
operands by their rows, before their first call-out, moved 22 more, all down
(`move_in_direction` 2968 to 2414, `play_effect` 3373 to 2768,
`jump_to_tile` 2606 to 1519, `move_toward_party_member` 4469 to 4151). The openings' own walk over
the object slots (every object cleared, only some run), and the commands they
hand to the original's table, which they look up, are a mix one scalar can't
fit: 3.15% of calls off by a line or more against the 1% vblank-tail limit, at
map load with the screen blanked, writing RAM. That is an entry in
`tools/timing_exceptions.txt`, and the vblank check finds none of its writes
moved. Every other event entry is within the limits at its calibrated value.

**The second review**, of the language table above, rated `sound.c` and
`movement.c` translations and the rest hand-written or nearly, and found what
reads as the original in the rest; its items became review-port's 20-25.
What changed for each:
- *The C table is the only dispatch.* The bridges became rows; the engine
  reads the ROM's handler table only for what has no row; the time tag is the
  row's; `$AF`, `$B5` and `$B6` call their siblings as C.
- *Scaffolding out of the language's rows* into `event/leaves.c`: the parse
  positions, the kept operands (an enum and an index, not `0x80|n`), the V an
  addition leaves.
- *`sound.c`*: the driver's commands are named in `platform/audio.h`
  (`au_play_effect`, `au_fade`, `AU_FADE_MUSIC` ..., `au_music_fade_left`,
  `au_music_playing`), the waits named for what they wait on, and the registers
  the driver doesn't read declared with `PF_CALL_DC` rather than loaded; what
  it hands back to the command's caller (A's high byte, Y) stays, and says so.
- *`movement.c`*: the decisions return plain values; the carries the field's
  motion reads are computed in the helpers that make those calls, each naming
  the callee (`carry_after_tile_test`: what `$C0:ABA2` passes to `$C0:5B95`);
  the V the turn aside, the screen test and the jump's rise leave is
  scaffolding derived from their operands; each move's operands are decoded
  once into a struct (`TileMove`, `ObjectMove`, `DirectionMove`); `$7B`
  became an operation (`launch`).
- *Cells used as registers*: the saved positions the moves, the sound
  commands, the party routines and `$D6` keep across their calls, `$7A`'s
  height in `event_count`, and `$4E`'s count are locals now, written once as
  scaffolding. Two stay cells, for reasons fuzzing found: `$E4`'s saved
  position, which the tile copy's indexed stores can overwrite (the command
  goes on from what it holds after, as the original's does: 274 of 5,001
  states differed with a local), and the turn's budget
  (`event_commands_left`), which commands the original still runs can spend
  (4 of 6,823 turns differed).
- *The turn loop as plain C* (`event_run_turn`, `event_run_openings`), the
  machine behind a runner the entry wrappers supply.
- *Rows name their parameters* (review-port's item 24, from the review's
  follow-up): the positional `args` column and the adapters that shuffled
  `a->arg[n]` into each operation's parameters went; a row says
  `.animation = OPERAND(1)`, the operation reads `c->animation`, and the
  bridges and the guard's checks read the same decoded command, so no command
  of the subsystem reads an operand by its offset outside the engine. The
  scaffolding table names the parameters it keeps (`KEEP(WORD, amount)`).
- *The rest*: one flow for the call commands (`take_the_call`), the tint's
  channels taken once, `count` folded into `add` (and `subtract`), locations
  through `set_location_as`, calls' places as entries of the table rather than
  the original's byte offsets, `copy_data` decoded into a `DataBlock` (and its
  Y as scaffolding), `$D6`'s carry computed in its bridge from the search the
  logic makes, typed operand kinds, named blocking and solidity bits, the
  waiting mark a bool, entry names that are the operations', a typed tint and sprite priority (the field's
  shadows still hold hardware bytes; TODO.md), the dialog flag named for what
  sets it, and `re/ram.toml`'s event sources brought up to date.

**What liveness could still take down.** V would be declarable dead at every
event entry if the analysis followed the field loop's JMLs (a hint per site,
or JML edges in the runtime's call log): 1.5 million commands never test it.
Y at the openings' end is stored by the menus' dispatcher and never read,
which a ported menus dispatcher that establishes it dead for routine `$0B`
would settle -- but `$C0:595C` and `$C0:597D` must be ported too, since they
reach the handlers' entries. The scratch cells can't be declared at all
(don't-cares are registers and flags); they go when the gate does (TODO.md,
PLAN.md "Retiring the gate").


**The second review, and what merged with it.** A fresh reviewer read the
branch at `8e061e8` against the rubric's 31 items and rated no file
translation. `dialog.c`, `input.c`, `story.c`, `party.h`, `variables.*` and
`event_data.h` rated hand-written. The machine glue and the scaffolding table
were judged thin and confined. The C table is the only dispatch for ported
opcodes; the ROM's table is reached only for opcodes with no row and for rows
whose guard declines. Nothing meaningful sits behind `ORIGINAL_EFFECT`.

The review's "mostly" findings were **accepted for the merge**. The user chose
to merge the improvement and fix these as a follow-up checked by the gate, rather
than hold it for another round. They are listed in TODO.md, "Event's follow-up
after its second review". In short:
- **Fixable inside event:**
  - stale `re/ram.toml` sources naming functions that moved;
  - dead helpers;
  - two setter chains left in `animation.c`;
  - a guard that re-runs the search it guards;
  - an uncommented restart of the openings loop from a RAM cell;
  - `party.c`'s carry-reproducing decision;
  - the script read that bypasses its accessor;
  - packed operands still decoded inside operations;
  - the `.routine` and `.bridge` columns in the language's rows;
  - bridge helpers copied into five files;
  - operations taking one ~80-field `EventCommand` instead of the parameters
    each reads.
- **Waiting on the field's object module and the naming pass:**
  - one field-object vocabulary (the same bit named differently across four
    files, `well_on_screen` written twice);
  - `movement.c`'s families, which can only become operations and rows once
    the field's motion is C;
  - the map's layout-shaped names.
- **The platform:** a color-math vocabulary for `tint_math`.

## Phase 3a: the field subsystem off the machine

The third subsystem of PLAN.md's Phase 3a: all of `game/field/`, 82
registered entries (70 in the table and the twelve deferred uploads one macro
makes), 15 files of which ten were on the transcription backlog (16 files
now: the windows' corner tiles are a file of their own). Before, field scored
0.35 operations per line and 93% of its functions took a `Snes *`
(portstyle's count); `display.c` reached the PPU through a direct page of
`$2100`, `sprites.c` wrote the sprite table through the work RAM port with the
direct page on the port's registers, and the object routines were one
function per ROM routine over `c->a` and `c->x`. After, it scores 0.02, no
file is on the backlog, and portstyle's column says 30%. Counted by name,
148 of its 588 functions take the machine: 98 are the entries' functions and
their guards, and the other 50 are listed at the end.

**Objects as a module.** The object routines were rewritten as a domain
module, `field/object.h`, before the files that use it: the part of the game
an event script, the object update and later `event/` itself talk to. An
object is a `FieldObject` (a slot); its state is kept as four concepts, each
with operations that do their bytes' writes inside:

- *its role* (`ObjectRole`, a kind and a disabled flag; `object_is_party_member`,
  `object_may_run`) and whether it is in view (a `bool`): the party's leader
  and members, other objects, objects that only come and go with the view,
  objects left alone -- one definition where four files had their own copy
  of the state byte's values;
- *its animation* (`field/animation.c`): the four modes a script chooses
  between (`ObjectAnimationMode`: with its movement, a loop, a one-off played a
  number of times, a held frame) and the operations a script uses
  (`object_loop_animation`, `object_play_animation`,
  `object_finish_animation`, `object_show_frame`, `object_stop_animating`,
  `object_hurry_animation`), the movement animation the field picks
  (`object_animate_movement`, `object_stand`), and every frame's tick and the
  frame builder's read (`object_animation_tick`, `object_current_frame`);
- *its motion* (`field/motion.c`): `Facing`, `Direction` (a byte to a turn),
  `MapTile`, `FieldPosition`; `direction_between`, `object_head`,
  `object_start_step` (with its `StartOutcome`), `object_in_the_way`,
  `object_line_up_with_tile`/`_with`, and the step every frame
  (`object_take_step`, `party_member_take_step`);
- *its place on the screen* (`field/object_update.c`):
  `object_update_screen_position`, `object_leave_row`, `object_join_row`.

The map's records don't group that way, which is the point of the module:
`field_object_move_state` holds both the one-off animation's plays
(`animation_count`, the animation's) and whether a move may cross its tile
(`tile_mode`, the motion's); `field_object_animation_mode` holds the mode and
the one-off's animation, while the loop's animation is
`field_object_animation_number` and the frame, its delay and the tables are
five more variables; the role is `field_object_state`'s `state`; and the
position is four bytes (`x_fine`, `x_tile`, `y_fine`, `y_tile`) the module
reads as a `FieldPosition`. The accessors keep the map's names (an awkward
one, `field_object_animation_mode_mode`, included); the module is where the
concepts are named. The operations the scripts' commands need are there, but
`game/event/` still has its own copies of the animation's state machine and
still calls the motion routines as call-outs: moving it onto the module is
event's next change (TODO.md).

No register-shaped value is in the module's signatures. Where an entry's
callers read something the original leaves in a register, the wrapper
derives it from the game's state around the call: the fine position before a
step, the plays before a one-off's frame table ended, the direction a blocked
start last looked in. Where it follows only from a value the routine had on
the way (the last distance the in-the-way check measured, whether reading a
frame started the animation over, the wave's last line's sum, the unpacker's
last run, the rasterizer's last sloped edge), the routine records it in a
file-static record (`in_the_way_leaves`, `frame_read_leaves`, `wave_leaves`,
`unpack_leaves`, `shape_leaves`), written only by a `record_...` helper and
read only by the wrapper or a `leave_...` helper (field/scratch.h). It is the
shape of event's `event_leaves`, and moves onto the scaffolding mechanism when
that is in the tree.

**Calling its own ports as C.** The object update, the sprite table's
building and the vblank handler called the field's own ports through their
ROM addresses, as the original does. They now run them as C, each timed as
the routine it is (`NATIVE_TIME_AS`, so the calibrated estimates still
apply): the update's move, view, tick, rows and screen position; the
building's draws and their placing; the vblank's sprite table, sprite tile
queue, the twelve edge uploads and the line effects' programming. Two rules
decide where an entry is still called by address, and both come from how
shadow mode checks a bridge. Where the state is one the entry's guard would
turn away, the entry is called, so the original runs it
(`field_tick_is_exact`, `field_rows_are_exact`, `field_layer_edges_fit`, the
draw's cursors and records). And where the inlined work would itself call the
original's code (an object coming into view or leaving it, a rebuild of an
object's records, the 24-sprite layout), the entry is called too: shadow mode
compares a bridge's calls with the calls the original makes itself, and a
callee's calls made from the caller don't line up with them. The building's
draws go further: every draw of a frame is made from the same two call sites,
so shadow mode can only match them if the port inlines all of a frame's
draws or none (`every_draw_is_own` walks the rows first, asking of each
object only what its draw's guard would). With those rules the
shadow totals stayed the predecessor's; the first try, which inlined a
rebuild and the view's coming and going, mismatched in 105923 draws.

**The machine, replaced.**

- *The direct page on the registers.* `sprites.c` wrote the sprite table
  through `$2180` with D = `$2100`; it now seeks and writes the work RAM port
  through `game/cpu_io.h` (`hw_port_seek_address`, the new
  `hw_port_seek_bank`, `hw_port_write`), and the table and the records are
  byte buffers in the map (`field_sprite_table`, `field_sprite_records`).
  `field/sprites.c` left the platform lint's `UNCONVERTED` list: `game/` is
  now all behind the services bar three documented files. The four windows'
  corner-tile writers (`field/window_corners.c`), which the original runs
  with the direct page on the PPU's registers, write through the video
  service. An object's sprites are read into a list of `Sprite` (a signed
  screen X, the ninth bit made only by the one function that writes a list
  into the table, `write_sprites`); the lists are an object's, not a range's,
  since a range's would be up to 128 sprites of locals.
- *`$0900`,* the sprite table's high part, is `field_sprite_table`'s last
  `$20` bytes, reached by offset.
- *The arithmetic units.* The velocity is `speed * factor >> 8` and the
  shading's step `40 / 8` in C; the multiplier's and divider's registers,
  which the original leaves holding its last operation, are written once in
  a marked helper (`leave_multiplier`, `leave_shading`); `cpu_io.h` gained
  `hw_remainder`.
- *The rasterizer* (`field/window_shape.c`, `$C3:0011`) was a transcript of
  the edge routine with its stack bytes, over 16-bit A and the flags; it is
  now the scan conversion it is: an edge from its upper end down, the X
  moving a pixel at a time while an error term lasts, a level edge writing
  both ends, and the lines outside the shape copied down from the first.
  The one place the original's rounding shows is one named function,
  `edge_start_error`: an edge running left starts a step behind the mirror of
  one running right, so the two sides of the Black Omen's light cone lean a
  pixel. Its guard keeps the table in bank `$7F` clear of the bank's end.
- *The wave.* The line effects' BG3 wave adds its 16 lines as one running
  sum, so a line whose sum wraps the scroll's range moves the next a pixel,
  and the first line moves a pixel while window 2 reads its second table;
  that is one named function (`fill_wave_scroll`), and `send_scroll`'s entry
  is guarded on the carry it is entered with being that choice (the builders
  always enter it so). The wave's line was kept in `field_direction` from
  part to part; it is a local now, and the cell gets its final value.
- *The flags callers read.* Carries and overflows are derived in the entry
  wrappers from the logic's results, through one set of helpers in
  `field/bridge.h` (`field_add_overflows8`, ...), where five files had their
  own.

**Scaffolding.** The cells the original writes for itself (the field page's
`$C1-$C8` and `$D9-$E6`, the layers' page's `$1D50-$1D6F`) are reached through
`field/scratch.h`, and every write of one is in a small helper per file named
for what it leaves (`leave_screen_origin`, `leave_start`, `leave_shading`,
`leave_wave_line`, ...), written once with the final value where the original
wrote a cell several times on the way; the working state is in locals. Two
cells were named by meaning because code reads them (`field_layer_edge_wrap`,
`field_direction_work`); the rest have no reader but the original's own later
instructions.

**What the map gained**: `field_work`, `field_sprite_graphics`,
`field_sprite_tile_offset`, `field_frame_entry`, the layers' work cells, the
window shape's table, edges, top, right side's start and steps,
`field_animation_queue` as a record, `field_direction_work`,
`field_layer_still`, `field_tile_properties`, the sprite table and records as
buffers; `event_scratch` grew to `$E6`. The field's three open addresses are
closed (43 left in the tree). `game/wram.h` is used in two places: the
rasterizer's table, which the caller locates in bank `$7E` or `$7F`, and the
sprite graphics' build buffer.

**What shadow mode found**, beyond the open-bus order of a long pointer (read
its address before its bank, or a pointer to nothing reads another bus
value): the stale-carry rule again (a call-out's don't-care carry comes back
as the port's own where the callee leaves it, so the object update reads
whether the view step left the object in view from the object), the
unpacker's run bytes, the object rows' links read as the 8-bit `$6D`, and a
guard that refused real sprite draws. Over the 26 movies every entry matches,
no page is refused, and the per-entry totals (calls, mismatched, skipped,
abandoned) are the predecessor's for every one of the 619 entries; so are
matched and interrupted, but for four entries, whose calls an interrupt now
lands in more often, since work that ran in a nested entry's call runs in the
caller's time: `field_update_objects` 6 interrupted of 193199 to 17,
`field_build_sprite_table` 267 of 193483 to 336, `field_draw_object_sprites`
837 of 974055 to 1713, and `field_update_object_sprites` 78 to 855. An
interrupted call is one shadow mode didn't compare, so those are what their
fuzzing (below) covers. No guard refuses a call the game makes: the skipped
counts are the predecessor's.

**Differential fuzzing.** Every entry is in `tools/fuzz_routines.toml`, and
all 66 routines listed there (two with a loose variant) were fuzzed against
the port each replaced (`tools/fuzz_compare.py`, seed 1; 20000 states, 10000
for the rasterizer, 2000 for the update loops, the unpacker and the vblank
handler, 200 for the VRAM queue). The rewrite mismatches nowhere. 65 agree
with the predecessor on every compared state; where the rewrite compares
fewer, the rest are its guards' refusals, counted as skipped (an odd object
or link, a delay or frame table past its bank, a stream that would reach the
layers' page, a table at the end of bank `$7F`, the other carry into sending
the scroll), and each PORTING.md row gives both counts. The one that differs
is `field_position_sprites` loose, with the records anywhere: 3452 of 19995
mismatched before, 0 of 13878 after, the records that would run past bank
`$7F` into bank `$80`'s low RAM and I/O turned away by a guard (the
predecessor diverged there). `field_build_sprite_table` compares nothing on
either side: every state's draws run past the call-out budget. Getting there
found the open-bus order of the frame table's pointer, the rasterizer's
stack clobbered by a table in bank `$7E` (now guarded), and carry-chain
quirks at facing 3's tables and the delay entry, now guards.

**What it cost.** Recalibrating field's `cost_milli` rows moved 60 of
them: the rewrite's bus accesses are the game's reads and writes, where the
transcripts' were every operand of the original's, so ports that read words
where the original read bytes cost less per access (`field_line_up_with_tile`
4865 to 2313) and ports that keep their working state in locals cost more
(`field_compute_layer_edges` 2920 to 11237, `field_direction_to_target` 4731
to 8132, `field_position_sprites` 4002 to 4751, `field_nmi` 4023 to 4520).
The bridges that now run their steps as C are timed as those steps plus their
own loops (`field_update_objects` 4165 to 4309, `field_build_sprite_table`
6699 to 4736). Six entries went onto `tools/timing_exceptions.txt` and
`field_rasterize_quad` gained a third limit: their time follows loops whose index steps the original's time
follows and the rewrite doesn't make (the sprite table's leftover-hiding, the
line-effect builders' bands, the unpacker's runs, the rasterizer's pixel
steps), so one scalar fits their average but not each call. All of them write
RAM the next frame reads, the vblank check finds none of their writes moved,
and the errors are at most two lines where they end in vblank
(`field_build_line_effect_tables` 2252 clocks), but for the sprite table's
building, whose draws now run in its own call (7352); the unpacker runs at a map's load, 2.5 frames a call. The sixth is
the object update itself, whose call is now the whole frame's object work
(30848 clocks on average, where its own stretches between call-outs were 115)
and follows how many objects there are: 3% of its calls reach vblank, off by
at most 1915 clocks there, writing object state the next frame's sprite
table reads.

**The review rubric, two rounds.** Checked against
`.claude/skills/review-port` by reviewers who hadn't seen the change. The
first rated `line_effects.c` a translation (the wave's carry chain threaded
through the builders, the wave's line kept in `field_direction`, the wrappers
working the logic backward) and the rest "mostly". What was changed for it:
the wave's line a local and its carry one function (`fill_wave_scroll`,
above); one definition of `field_display_flags`' bits (`field/field.h`), of
the 4.4 scroll fraction, and of the layers (`FIELD_BG1`-`FIELD_BG3`); the
arithmetic units' results computed in C; the map transition's steps named for
what they send; entries renamed where their words disagreed with the
operations' (`field_leave_object_row`, `field_join_object_row`,
`field_tick_object_animation`); and `field_setting_bc` found to be the
screen's shake, which the map now says (its accessor keeps its name).

The second rated `animation.c` and `motion.c` translations by the threshold
rule (their twin functions passing traces to the wrappers, rubric item 26)
and `sprites.c` a translation (its row counter the original's Y, its ranges
and records addresses and byte offsets, its start in the original's write
order). What was changed for it:

- *Traces out of the signatures* (item 26): the twin functions are gone, the
  wrappers derive what they can from the state, and the rest is recorded as
  above. `StartLeftovers`, `PlaceLeftovers` and the rasterizer's and the
  unpacker's leftovers, which the logic filled as it went, are gone too: the
  `leave_...` helpers take the final values once.
- *A start's sign bytes* (`KeptSteps`) are one named quirk,
  `retry_look_across`: a retry with no horizontal step looks a tile left
  where the first try went left. The in-the-way check's borrow is
  `earlier_slot_measured_nearer`, with what the player sees.
- *The sprite table* is rebuilt around `Sprite` and one serializer (above).
  The building's row counter is a local set on the machine only at a call,
  taking back what the original's draw leaves (the original counts rows in Y,
  and its draws can move it); the ranges are offsets into the table,
  converted to the addresses the original keeps only in their storage
  functions; the records' fields are named in one place. The all-or-none
  walk stays (item 29): shadow mode pairs a bridge's calls with the
  original's in order, so drawing one object as C and calling the next would
  pair that call with the original's draw of the first. It shrank to asking
  of each object what its draw's guard asks; the ranges' cursors, which it
  simulated, can't leave low RAM for the layouts it takes.
- *Aliases past the tables* (item 28) are explicit: taking an object out of
  the middle of its row writes the in-view flag of slot `(link - $80) / 2`
  when none follows, in `leave_stray_in_view_write`; the in-the-way search's
  slots past 64 go through `stray_slot_can_block`; the map transition's third
  destination is its own variable, `field_transition_map_base`, not "the word
  after BG3's"; and the object update's guard now turns away a count past 64,
  which would run its loop into the tables beyond.
- *Loop state and call sites* (items 13, 30, 31): the object update's loop
  runs on locals; its call sites are tables by step and by which update
  (`view_sites`, `tick_sites`, ...); the call-outs are named for what they do
  (`GIVE_PALETTE`, `FREE_SPRITE_RECORDS`, `SEND_STAGED_MAP`, `MENUS_FRAME`,
  ...); a call's return is `site + JSR_SIZE`.
- *Scaffolding out of the logic* (items 9, 13): `field_target_object`'s high
  byte, the port's bank and the current object's high byte at the sprite
  table's start, the queue's Y, the placing's ninth bits, all in named
  helpers; `scratch.h`'s value readers are gone (the tile copies' Y is the Y
  they came with, now that the mirrored copy's guard keeps the port's writes
  off the cell it saves Y in; the shading's overflow follows from its
  constants); the rasterizer's cells go through `scratch.h` like the rest.
- *In-band encodings* (items 5, 10): in `animation.c` the `$FF` "nothing to go
  back to" and the half-pass countdown are confined to storage functions, and
  the public count is in passes; the role is a kind and a disabled flag, in
  view a `bool`, the records' state a struct, a move's tile crossing an enum,
  an object's screen position an `int16_t` pair (nine bits only in storage and
  in the sprite table), a row or queue link a `FieldLink`, and a started-over
  animation's frame `BEFORE_THE_FIRST_FRAME`.
- *Structure*: coming into view calls the animation's and the rows'
  operations; `view_window` is shared by the view and its guard; one
  vocabulary for the dialog window's position in `line_effects.c`; the edge
  runs are named fields (`field/layers.h`), and the twelve uploads are named
  for which run they send (`field_upload_layerN_bottom_row`, `_top_row`,
  `_right_column`, `_left_column`, which were `_tiles`, `_tiles_alt`,
  `_columns` and `_columns_alt`); the windows' corner tiles left `display.c`
  for `window_corners.c` (with entries renamed `field_write_window_corners_N`
  and `field_refresh_window_corners`); the interrupt handlers' register save
  is one helper in `runtime/port/machine.h`.

What a reader can still see of the original: the scaffolding helpers name
the cells they leave by the original's direct-page offsets (`$D9`, `$DB`),
since the cells have no reader but the original; the sprite table is still
written through the work RAM port, until a platform sprite service exists;
and `game/event/` still keeps its own copy of the animation state machine
(TODO.md). Deferred: renaming the map's stuttering fields, and the platform's
sprite and line-effect services.

**Left on the machine, and why** (the 50 functions that are neither an entry
nor a guard): the shared pieces of the guards and the entry wrappers in
`bridge.c` (8: which object the registers name, whether a callee came back as
expected, the page and bank checks the guards compose); the frame wait's
pass (1); the wrapper pieces the line effects' and the tile copies' entries
share (9); the steps the object update runs as C where their entries would
run them, which set the flags the entries leave (4: the move, the view, the
window-only check, and the rows' entries' return); and the bridges around the
original's code: the vblank handler's (`nmi.c`, 8: a frame's transitions,
fade and palette are the original's), the object update's (9) and the view's
(2), and the sprite table's (9: an object's records are rebuilt for 8, 12 and
24 sprites by the original's code).


**The fresh review, and what merged with it.** A reader who hadn't seen this
account read the branch at `4972e5d` against the rubric's 33 items, and rated
no file translation. `window_shape.c`, `window_corners.c`, `scroll.c`, `frame.c`
and `interrupts.c` rated hand-written. `animation.c`'s storage and script
operations reach the shape of the review skill's illustration; its frame-read
half does not yet. No logic function passes a register-shaped struct. The
five `*_leaves` records are written only by their record helpers, and the
object update's stray in-view write is the explicit, marked model the other
aliases should follow.

The "mostly" findings were accepted for the merge, on the same terms as
event's: merge now, fix on main as a follow-up checked by the gate (TODO.md,
"Field's follow-up after its review"). The most important are stray accesses
past a table that are still hidden behind clean accessors: a fifth sprite's Y,
the high-table cursor's spill, map tables declared as their first 256-byte
row and indexed across 64 KiB, and the stray-slot reads. They are no problem
while storage is the original's layout, and a hazard the moment it isn't. The
rest are reading:
- sprite records addressed by offset;
- bridge loops that run on the original's Y or on the current-object cell;
- dispatch that compares ROM addresses;
- the bridge helpers copied into four files;
- two guards that re-run what they guard, which shadow mode's call pairing
  forces;
- `object.h`'s link encoding showing through its interface.

The platform gaps (a sprite service, typed line effects, transfers without
write-order arrays) and the map's renames are deferred, as before.

## Phase 3a: event onto the field's objects

The pass PLAN.md scheduled once event's language and field's objects had both
landed: it finishes what the two subsystems' reviews left (TODO.md's lists,
each item deleted as it was done), moves event's commands onto
`field/object.h`, and names the object tables in the RAM map by what they
hold. Five steps, each committed with shadow mode clean over the library.

**1. The stray accesses past a table, made explicit.** Each is one marked
access, the way `leave_stray_in_view_write` already was, and is listed in
TODO.md for the build without the original's memory:
- an object above the screen places a fifth sprite's Y, in the record after
  its four (`leave_stray_fifth_sprite_y`, `ORIGINAL_EFFECT`);
- a range full enough runs its high table cursor past `field_sprite_table`'s
  $220 bytes, and the original writes the high bits there too
  (`leave_stray_high_bits`, by address through `game/wram.h`, since those
  bytes are no variable's);
- the in-the-way search's slots past 64, which the original reaches with no
  objects or 64 of them (it counts round 128), are the original's: no map has
  either, and the guards of the check and of the moves that set off refuse
  them (`object_search_fits`), where step 1 first read the tables after each
  table by their own names (item 34 of review-port's rubric, "emulating inputs
  no game state produces", which came after);
- the openings' word clear of the last slot's solidity clears the first
  object's falling flag (`object_start_on_the_map`);
- `field_layer_maps`, `field_tile_properties` and the two tile attribute
  tables are tables of rows: `tools/ram_map.py` has a new shape for them
  (`rows`, `row_stride`: the variable is one row, repeated a row stride apart
  with other tables' rows between), whose accessors take a row and a column,
  with tests. The attributes are two variables now, the upper and the lower
  (`field_tile_upper_attributes`, `field_tile_lower_attributes`), and a map's
  tile is one `TileAttributes` (`field/layers.h`), where two files had their
  own.

The object guards take only slots inside the tables (`field_names_object`:
below `$80` doubled, where they took anything below `$100`). One exception the
game makes: a command that names a party member without checking takes an
empty place's marker as object 64 (`$95`'s 52,898 calls over the library
include a scene following a party of two; the member's place is empty). This
step first took the marker as "the absent member" and let the port read the
tables' neighbours through the objects' accessors; the fresh reviews asked for
an explicit path instead, and the guards now leave those calls to the
original (below, "The fresh reviews' must-fix items").

**2. `field/object.h`, the only way to an object's state.** The header gained
the operations event needed, each with its storage in one place
(`field/object.c` for what a script sets, `motion.c` for position and moves,
`animation.c` for the animation, `object_view.c` for the view):
- role, reachability and sprite: `object_take_off_the_map`,
  `object_leave_the_party`, `object_character`, `object_runs_a_script`,
  `object_unreachable`/`object_set_unreachable` (the mark is the top bit of the
  move length, which a walk's step count then carries), `object_set_sprite`
  (`ObjectSprite`), `object_can_be_seen`, `object_start_on_the_map`;
- place and look: `object_tile`, `object_place`, `object_put_on_tile`,
  `object_facing`, `object_set_facing`, `object_turn`, `facing_of_direction`,
  `object_set_layering` (`ObjectLayering`), `object_layer_as_the_map`,
  `object_set_sprite_priorities`, `object_take_own_priorities`;
- what a script sets about moves: `object_set_speed`, `object_set_blocked_by`
  (`ObjectBlocking`), `object_set_solidity` (`ObjectSolidity`),
  `object_set_arrival`/`object_lines_up` (`ObjectArrival`),
  `object_set_move_length`/`object_move_length`;
- moves: `object_start_move` (with a `TileCrossing`), `object_start_walk` (the
  scripts' walks store their step count as the crossing, so an unreachable
  object's 128-step walk crosses on every step), `object_start_one_step`,
  `object_moving`, `object_stop_moving`, `object_mark_scripted_move` and its
  pair, `object_launch`, `object_jump`, `object_land`, `object_velocity`,
  `aim_toward_object`;
- animation: `object_play_animation` takes the half-pass countdown `$B7`
  gives (`$AB` passes two), `object_end_frame_time` (a turn shows at once),
  and the frame read split into a finder and a taker
  (`object_take_next_frame`, below);
- the screen: `tile_well_on_screen`, one test where two files had it.

And the four the review asked for: `FieldLink` is a value whose byte only the
storage functions and the guards see (`field_link_to`, `field_linked_object`,
`field_link_is_object`, `field_same_link`); the in-view flag is one
`ObjectView`, since the row removal's stray write can leave a link in the
first object's flag, which the object update reads as out of view and the
in-the-way check, the party's re-sort and `$27` as in view
(`OVERWRITTEN_BY_A_LINK`); `ObjectRole.kind` is an `ObjectKind`; and
`object_make_current`, the original's calling convention, is in
`field/bridge.h`. The step's start takes a tile's two priorities the other
way round from a script's layering (the original's two routines disagree
about which table is which half), one named function
(`take_priorities_for_the_step`). The object update's steps take the object
as a parameter, and its loop end is marked scaffolding.

**3. Event onto the object model.** No file of `game/event/` reaches an
object table's accessor any more; "the field's objects" sections are gone.
`event/animation.c` is four operations over the field's state machine.
`event/movement.c`'s twenty bridges became five operations and a bridge:

| Operation | Opcodes | What the rows say |
|---|---|---|
| `move_to_tile` | `$96 $97 $A0 $A1 $9A` | walking or sliding (`MoveStyle`), the tile from operands or variables, counted |
| `move_toward` | `$94 $95 $9E $9F $98 $99 $8F` | an object or a member, walking or sliding, counted, until next to it or well on screen (`WalkEnd`) |
| `move_in_direction` | `$92 $9C $9D` | walking or sliding, from operands or variables |
| `jump_to_tile`, `launch`, `follow`, `follow_party_member` | `$7A $7B $B5 $B6` | |
| (bridge) `event_cmd_behave` | `$B0` | the field's state routines, still the original's |

They call the field's motion as C (`direction_between`, `object_in_the_way`,
`object_start_walk`, `object_start_step`, `object_animate_movement`,
`object_stand`, `object_line_up_with*`, `object_head`, `object_jump`), and
the carries they handed each other, `s->cpu.a = facing`, the jumper from X and
the call-site tables are gone with the calls. `$A8`/`$A9` (face toward an
object) are operations too. What the original's handlers leave in Y and V is
now the field's routines' own, recorded as scaffolding where the move ends
(`leave_set_off`, `leave_jump`), from the field's record of what each routine
leaves (`field/scratch.h`: `field_leave_aim`, `field_leave_direction`,
`field_step_start_y`/`_overflow`, `field_walk_overflow`, `field_jump_overflow`,
which the field's own wrappers now use too). Only the final Y and V of a path
matter, since shadow mode compares them where the command returns.

**Operations take the parameters they read** (review-port's item 32): an
operation is `static EventFlow loop_animation(GameState *g, FieldObject self,
uint8_t animation)`, and `EVENT_OPERATION(loop_animation, self, animation)`
after it makes the form the table calls, which hands it those of the decoded
command, in its order; a row says `OPERATION(loop_animation)`. The guards'
checks are the same (`EVENT_CHECK`, `CHECK`). `EventCommand` is the engine's
alone. The rows lost `.routine` (the handler's address is the original's
table's, read by the machine side, `handler_of`, which all 185 rows agreed
with) and `.bridge` (`event.h`'s `EVENT_BRIDGES` list is the table of
bridges); `EventParameter`, which only the scaffolding table used, is
`leaves.c`'s and compiles out with it. New operand kinds decode packed
operands into typed parameters: `OPERAND_BITS`/`OPERAND_WORD_BITS` (a fade's
brightness and rate, a tint's `TintMix` and rate, a call's level and
function, a story flag's page and bit, a question's first and last choice)
and `OPERAND_PLUS_ONE` (`$B7`'s count); solidity, blocking, arrival, layering
and the sprite are the field's types. Two stayed bytes on purpose:
`set_player_control` stores the script's byte, which the field tests as
nonzero, and a location's two words are decoded by `location_of` in the
operation (TODO.md).

**One vocabulary.** The bridges of both subsystems call the original through
`field/bridge.h`: `bridge_call`, `bridge_call_long`, `bridge_back` and
`BridgeMachine` (how the bridge runs: A's width, X's, the direct page, the data
bank), with `JSR_SIZE`; the five copies of `back()` and the four of the field's
call-out helper are gone, and `tools/dontcare_check.py` reads `bridge_call` as
it reads `m_jsr_dc` (with a test). The duplicated definitions went:
`DISABLED`/`CALLS_SKIP` (the object's `disabled` and `unreachable`),
`NO_SPRITE_SHOWN`/`SPRITE_HIDDEN` (`ObjectSprite`), `well_on_screen`
(`tile_well_on_screen`), the three aim helpers (`aim_toward_object`,
`field_leave_aim`), `field_requests`' bits (`field/field.h`: a battle is asked
for with a tile copy, `FIELD_REQUEST_BATTLE | FIELD_REQUEST_TILE_COPY`, which
the main loop's `$C0:1770` runs), the field's effect bits, `link_or_none`
(two guards named for what each checks), `TileAttributes`, `JSR_SIZE`,
`SPRITE_TABLE` (`FIELD_SPRITE_TABLE_ADDRESS`), `LEAVE_ROW`/`JOIN_ROW`
(`FIELD_*_ROW_ENTRY`, with the rows' step shared by the update and the view),
`AXIS_X` (`field/field.h`), the random table and its draw
(`field_draw_random`, which a blocked object's turn and `$7F` share), the
overflow helpers (`field/scratch.h`), and "keep the offset across the call"
(`event_keep_offset`).

**4. The RAM map by meaning.** The object tables' records grouped by where
their bytes sit became variables named for what they hold:
`field_object_animation_mode_mode` is `field_object_animation_mode`,
`..._animation_mode_timed_animation` is `field_object_one_off_animation`,
`field_object_animation_number` is `field_object_looped_animation`,
`field_object_move_state_tile_mode` is `field_object_tile_crossing`,
`..._move_state_animation_count` is `field_object_one_off_half_passes`,
`field_object_state_state` is `field_object_role` (and `_character`
`field_object_character`), `field_object_unlinked` (0 meaning in a row) is
`field_object_row_mark`, `field_setting_bc` is `field_screen_shake` (and event
`$F4`'s operation `shake_screen`, in `tools/disasm/eventscript.py` too), and
the byte of script variable `$A8` window 2 reads is
`field_window_2_second_table`. `field_sprite_frame_data` is its $340 bytes, not
its first record. `tools/ram_map.py` rejects a generated name that says a word
twice, and `check` lists the names still shaped like addresses (seven:
`field_setting_1e`, whose meaning isn't cheap to find, and six of the sound
driver's). Every stale `source` line of event's and field's was fixed: 57
named functions that had moved or been renamed.

**5. The rest of the follow-ups.**
- `sprites.c`: a `SpriteRecord` with storage functions (the record's X, Y,
  offsets and look, and the same sprite in the frame's data), which
  `place_sprites` works on; no `high_of` (the placing entry's guard takes the
  object in X only where it is the current one, which the game always has);
  the table's building loops on a local row, hands the draw its row in Y and
  stops where a draw came back with another; the records' steps are a
  `RecordsStep` over one table of each layout's routines, X set only for the
  calls that read it.
- `animation.c`'s frame read is a finder that changes nothing
  (`find_next_frame`) and a taker (`object_take_next_frame`), over
  `field_data.h`'s typed frame tables (`FieldFrameTable`,
  `field_animation_place`, `field_animation_frame_number`,
  `field_animation_frame_time`), where callers built ROM addresses.
- The object update's call sites are one table of the update's steps, and
  `nmi.c`'s three map-transition sends one table.
- Smaller: `wave_part` returns nothing (the wrapper works out the last
  entry's pointer), the view window's widenings are named, the layers are
  `FIELD_BG1`-`FIELD_BG3`, and the sprite graphics' ROM banks are
  `field_data.h`'s (`field_sprite_graphics_rom_bank`).
- The field's scaffolding helpers are called through `ORIGINAL_EFFECT`, as
  event's are, so the native-shape build compiles them out.
- PORTING.md's "Call-outs" says how a bridge's calls are paired with the
  original's, and the bridges' vocabulary.

**Against the rubric's later items** (review-port 34-38, which grew from
scene's review while this pass ran). The leftover records went where the
wrapper can derive the value (item 35): the in-the-way check returns the
object it measured last as well as whether it was blocked, and the wrapper
measures it again for the distance whose high byte the original leaves in A;
the wave's last carry comes from the table the wave wrote and the offsets'
table; whether the frame read started the animation over from the frame and
entry it left. What the wrapper looks at again it reads under `vars_peek`,
which the cartridge's accessors now honour too (`runtime/port/state_io.c`), so
it costs the port no time. Two records stay, for cells whose values follow the
path taken and are nowhere in the final state (the last run the tile
unpacking repeated, the last sloped edge of a window shape), written through
`ORIGINAL_EFFECT`; `field/scratch.h` says which. The in-the-way search's stray
slots are a guard now (item 34, above). No step is kept in the original's
order for the timing estimate's sake (item 36): the orders this pass kept (the
frame table looked up last, the copy of script data a byte at a time) are
behaviour, a read of open bus and a copy onto itself.

Deferred, each in TODO.md with why: the location's decoding, `nmi.c` sharing
`display.c`'s edge runs (the don't-care check reads a call's site only from
its own file), the staging buffers (a transfer service's), the prediction
guards, and the platform's sprite and line-effect services.

**What shadow mode found.** Over the 26 movies nothing mismatches and no page
is refused. The totals of every hand port are `main`'s but for the commands
that stopped being bridges, whose calls the engine used to make as call-outs
(so shadow mode abandoned them) and now runs as C (so it compares them):

| Entry | Calls | Matched before | Matched after |
|---|---:|---:|---:|
| `event_cmd_move_toward_party_member` | 72,121 | 20 | 72,121 |
| `event_cmd_follow_party_member` | 70,971 | 20 | 70,971 |
| `event_cmd_move_to_tile` | 38,113 | 451 | 38,113 |
| `event_cmd_move_toward_object` | 33,893 | 9 | 33,893 |
| `event_cmd_follow_object` | 33,893 | 9 | 33,893 |
| `event_cmd_move_in_direction_keep_facing` | 7,287 | 6,330 | 7,287 |
| `event_cmd_move_to_tile_keep_facing` | 3,074 | 112 | 3,074 |
| `event_cmd_move_in_direction` | 2,661 | 2,362 | 2,661 |
| `event_cmd_jump_to_tile` | 1,932 | 1,810 | 1,932 |
| `event_cmd_face_toward_member` | 1,543 | 0 | 1,543 |
| `event_cmd_face_toward_object` | 251 | 0 | 251 |
| `event_cmd_move_to_tile_from_variables_keep_facing` | 127 | 0 | 127 |
| `event_cmd_move_to_tile_from_variables` | 100 | 0 | 100 |
| `event_cmd_move_toward_party_member_until_on_screen` | 10 | 0 | 10 |

Every call before that didn't match was abandoned, never compared; none
mismatched, before or after. (The reviews' fixes, below, later handed the
calls on an empty party place to the original: 52,898 each of `$95`'s and
`$B6`'s.) `event_cmd_behave`, the one move still a bridge,
keeps its 250,698 abandoned calls.

The field's motion entries (`field_direction_to_target`, `field_start_step`,
...) keep their totals: shadow mode counts an entry where the original reaches
it, and the original still calls them from its own handlers. Renamed entries
(`event_cmd_set_bc`, `event_cmd_set_solid`, `event_cmd_move_by_variables`)
carry their predecessors' totals under their new names.

**What fuzzing found.** `tools/fuzz_compare.py` over event's and field's 438
runs (seed 1, `main`'s `ct_fuzz` against this one's) found one divergence, in
the rewritten frame read: `field_read_animation_frame` mismatched 517 of 3,326
states in A. The finder looked up the frame table (its bank last) before the
animation's number, and where the table points at nothing that answers (bank
`$3C`, `$4600`), the read returns the last byte on the bus: the bank in the
original, the animation's number in the port. The finder now looks the table up
last, as the original does, and reads a restarting animation's first frame
after putting it back before its start (a table in work RAM can hold the frame
number it rewinds; that took two more states). 0 then. Every other run has the
predecessor's mismatches, none, and three kinds of run compare fewer states,
each for a guard, which the skipped column shows:
- every entry whose operand or X names an object compares about half as many
  (`event_cmd_if_in_view` 5,055 to 2,513, `field_move_object` 10,065 to 5,060,
  `field_line_up_with_object` 5,000 to 1,283 with two objects): the guards take
  slots inside the tables, below `$80`, where they took anything below `$100`;
- `event_cmd_copy_data` 9,987 to 6,139: the operation reads the data through
  the script's accessor, and its guard turns away a block that runs past the
  script's end or has no length (the original's block move takes the whole
  bank then); no script has either;
- the sliding moves toward an object or a member (`$9E`, `$9F`) and
  `field_position_sprites`/`field_load_and_position_sprites` compared 55 to
  156 states before their entries were changed: the first check the object
  faces one of the four facings (the in-the-way check reads a table of four),
  the second take the object in X only where it is the current one, which the
  high table bits are the current object's (the game always places the current
  object). Their entries now fill the facings with the four and make the object
  in X the current one: the moves compare half their predecessor's states,
  the object guard's share (2,498 and 1,317), and the placing all 20,000
  (loose 13,878), before and after.
- the moves that set off and `field_object_in_the_way`, once the search's
  stray slots became a guard, compared a quarter of the states: the fuzzer
  randomizes the map's object count, which sits before the script. Their
  entries now keep it 1 to 63, and they compare their predecessors' states
  (the object guard's half where an operand names an object).

**The timing gate.** Event's and field's `cost_milli` rows were recalibrated
over this pass's shadow runs, 57 rows (the two of menu's that the calibration
also moved were put back). The largest moves: `field_set_velocity` 4,384 to
6,305 (122 calls in three movies), `field_update_object_sprites` 3,228 to
4,353, the window table choices `field_set_window2_table_0`/`_1` 2,567 to
3,208 and 2,617 to 3,271, `field_update_objects` 4,309 to 4,963, and among
event's the rows that stopped calling out (`event_cmd_play_animation` 5,323 to
4,737). Three entries went over the vblank tail's limit and are in
`tools/timing_exceptions.txt`: `event_cmd_move_to_tile` (2.24% of its calls in
vblank off by 1024 clocks or more) and `event_cmd_move_toward_party_member`
(5.76%), whose time follows the field's motion they now run inline as C over
the game's state, with no machine at hand to name those routines' time, and
`field_object_in_the_way` (1.08%), whose time follows how far its search over
the slots goes. All three write object state in work RAM, and the vblank check
finds none of their writes moved.

**The fresh reviews' must-fix items.** A reader who hadn't seen this account
read the branch against the rubric, which the reviews grew to 48 items, and
rated no file translation; `object.h` was credited as a domain module the
rest uses. What they would not accept for a merge was fixed on the branch:

- *Accesses past a table* (item 28). The resumption search of `$00` walked 512
  levels of `event_saved_positions`, which holds eight; it searches the levels
  below the object's to the lowest now, and the return's guard refuses an
  object with nothing to resume there (and any level past the eight), so the
  original runs its wrap. A call's level and its target's are checked to be
  among the eight too. An empty party place no longer becomes object 64: it
  names no object (`member_object`), the commands that allow one decode
  `there` false and don't look, and the guards leave the ones that don't to
  the original (`member_named`), the wait of `$07` included.
- *The Y/V record written from inside the logic* (items 26, 35, 39). The
  operations no longer see the record or the command's offset. `event/leaves.c`
  runs the command (`event_perform_leaving`) and works out around it, from its
  parameters, the state before (a `note`) and after (a `derive`), what the
  original leaves: the calls' V (clear wherever the call reached the places,
  whose index additions can't overflow), the absent member's Y, the return's
  V, the face-toward's direction overflow, both screen tests' V, the jump's Y
  and V, the launch's Y, the change of location's Y and kept offset, the
  question's kept choices, the drift's Y, the follows' kept offset and the
  story flag's operand (table entries), and the jump's leftovers, which the
  engine made in `follow` before. `copy_data`'s Y and V are its bridge's,
  from the block. What stays said where it happens, through three functions of
  `leaves.h` that take no offset: how a move set off (the step's start is the
  field's, and nothing after the move says whether it stepped, turned or
  stayed), whether it looked for objects in the way on its way (which keeps
  the offset in a cell), and the walking animation's overflow while it lines
  up. The engine takes no pointer from the runner, and the two screen-test
  helpers are one (`keep_screen_edges`, beside `field_screen_test_overflow`).
- *The object encoding in `field/object.h`* (item 18). `event_names_object`,
  `slot_of`, the engine's copy of `object_make_current`, `handler_of`'s own
  read of the table and `SCRIPT_SIZE` are gone; `field_names_object`,
  `field_link_names_object`, `object_current`, `object_current_is_one` and
  `object_make_current` are `object.h`'s; the party's link and the dialog's
  speaker are operations (`party_place_left_empty`, `dialog_set_speaker`), the
  character byte's empty marker is `NO_CHARACTER`, and scaffolding keeps an
  object through `field_object_offset`. The engine counts `FieldObject`s. The
  openings keep `event_object_end` once, as scaffolding, and loop to the
  count; they store the current object as a word, as the original's map
  setup does with 16-bit index registers ($C0:58FB), which the review read as
  a slip: `leave_current_as_a_word` says so.
- *`object.h` as the field's whole vocabulary.* `sprites.c` reads no object
  byte: the layout, the range, the records and their state, and the high bits
  are operations, and the records' state has one definition. The battle's
  placement byte has one name, and a true one (`object_let_the_battle_place`:
  $FF leaves the place to the battle). Solidity is a `bool` (decoded by a new
  operand kind, `FLAG`, which the guard holds to 0 or 1), the layering a
  struct the script's byte decodes into (`layering_of_byte`; a byte with bit 6
  set is the original's), and the facing is decoded once, in storage, with a
  guard (`object_facing_is_one`) where a facing past the four would read past
  a table.
- *`animation.c`.* Half passes are the scripts' unit as well as the storage's,
  and the header says so; "which animation is current" is one function (the
  tick times the last one-off through a held frame, the frame builder reads
  the loop's frames, which the one function says); the frame table for a
  facing takes the decoded facing; and the frame read's overflow is worked
  out in `field/leaves.c`. The file then read with no finding of its own, and
  its first half, with `event/animation.c`'s command, replaced the review
  skill's illustration.
- *Window 2's tables* are named for what they are, the two halves of
  `field_window_edge_lines` read a byte a line (`window2_edge_table`), and the
  comments that called them a copy are fixed.
- *The word clear's spill* is one marked helper for every slot
  (`leave_word_clear_spill`). The game doesn't depend on it: a build that
  skips it, run with the ports in the shipping mode over `new-game`, `field`,
  `events-prison-escape` and `late-omen-zeal` (68,422 frames with their map
  loads), left every byte of WRAM, VRAM, CGRAM, OAM and SRAM identical to the
  original's, every frame; the next slot's state is already 0 when the
  openings run.
- *Small ones.* The in-the-way search keeps the object it measured once,
  after the search (`leave_target`). The flag derivations exported from the
  logic files moved to `field/leaves.c`, beside the wrappers, with
  `field/parts.h` naming the few parts of the motion and the animation they
  work from. `cpu_io.h` says the port and the math units are for the
  original's leftovers, not for computing. The walk animation of a move is a
  statement of its own, not inside the expression only scaffolding read
  (item 40).

The rest of the reviews' findings are in TODO.md with why they wait.

**What they moved.** Shadow mode over the 26 movies: nothing mismatches. Three
entries' totals moved, all to the guards on an empty party place:
`event_cmd_move_toward_party_member` and `event_cmd_follow_party_member` hand
52,898 calls each to the original (a scene following a party of two, where the
third place is empty; 19,223 and 18,073 still match), and
`event_cmd_note_tile_member` 2,735 of 38,500. Every other entry's total is the
same, the calls, layering, solidity and facing guards refusing no call the
game makes.

Fuzzing against `main` (seed 1, the 438 runs) found three things on the way,
each fixed in the port: the one "current animation" function first took an
animation mode past the four as looping where the tick takes it as a one-off
(10 of 2,898 states; each reader keeps its own test of the stored mode now),
the noted facing wrote the decoded facing where the original notes whatever
byte is kept (now the guard's), and through those the engine (10 of 6,823).
Then 0 differ. The entries the new guards narrow fill what the guards
require, so they compare again: the party's places and the operand that
names one (the member commands, about 2,000 of 20,000 each, where the random
operand named a place past the party's three before), the facings (the noted
facing, 2,498; the frame read, 4,089), the call levels (1,289, the refusing
flags sharing the rows) and the solidity operand (10,035). The derivations
look at the state without the machine's time (`vars_peek`), so an estimate
still counts only what the operation does, and event's and field's rows were
recalibrated.

## Phase 3a: scene reworked to the review

The first qualitative review (`.claude/skills/review-port/`) rated three of
scene's files translation -- `program.c` emulated the arithmetic unit,
`circle.c` kept its X and Y in RAM cells and chained the carry through every
edge, `bitmap.c` passed a struct that was the CPU (`carry, overflow, x, y,
bank`) -- and most of the rest mostly. This rework takes each of its findings
and regroups the subsystem by the game. Nothing was added to the burndown, and
the behaviour is the predecessor's: the per-entry shadow totals over the 26
movies (calls, matched, mismatched, interrupted, skipped, abandoned, and the
pages each entry was called on) are identical to `main`'s for every hand port,
not only the scene's.

**The files, by the game.** `title.c` (the title screen: the pendulum, the
logo, the ripple, and the loader's blocks), `race.c` (the jet bike race's
display and its engine's pitch), `race_matrix.c` (the race's generated matrix
code), `script.c` (the sequence scripts), `frame.c` (the NMI, pause, the song,
the task lists), `screen.c` (every scene's first screen, the per-frame uploads,
the program's transfer and the line effects), `boot.c` (starting a scene, its
interrupt handlers, the resource copies), `sound.c` (the program's own sends to
the sound driver and its wait for the song's marker), and what the scenes
share: `sprites.c`, `circle.c`, `scaler.c` (was `bitmap.c`), `math.c`.
`scene.h` says where each part of the game is. The endings have no routines of
their own: they are the program's scripts, using the iris, the scaler and the
arithmetic. `display.c`, `display.h`, `race.h`, `sound.h`, `program.c`,
`nmi.c`, `sequence.c`, `matrix_code.c` and `scratch.c` are gone.

**The arithmetic is C** (`math.c`). The 32-bit divide is `n / d` and `n % d`,
the 8.8 divide `(dividend & $FF00) / divisor`, the multiplies `a * b`, the
divisor search a loop of `dividend / divisor` in locals. Where the original's
way of computing them would come out otherwise, what the game gives it decides
between a guard and a named input, measured by guarding each and comparing the
per-entry shadow totals with `main`'s:
- no scene divides by a 32-bit divisor of 0 or with its top bit set, and no
  unsigned product's middle partial sums overflow 16 bits (which would put the
  overflow a byte too low); the guards hand those to the original, and turned
  away none of the library's calls;
- the unsigned multiply carries the top bit of the byte after its product into
  it, and leaves that byte shifted a place for the next: the title's pendulum
  and the race multiply with it set (848 of the 3870 multiplies of every title
  screen, 32936 over the library, which a guard turned away), so it is a named
  input (`carries_into_the_product`, `shift_the_byte_after`) and stays in the
  logic, since the next multiply reads it; the 8.8 divide's divisor less one,
  one of the things left in that byte, stays in the logic with it;
- the 8.8 divide by 0: the endings make it (637 calls in `endings-beyond-time`),
  and the original's long division, compared over every divisor, comes out
  exact for all of them with 0 counting as 65536 -- so the fraction is 0, one
  line, and the bit-serial division is gone;
- the signed multiply adds the product cell's third byte as the caller left it
  (`magnitude_with_leftover`), a named input.
The dividend cells the divides leave shifted out with the caller's carry, the
multiplier and divider as the original leaves them, and the divisor search's
time (below) are scaffolding. The wrappers derive what the callers read: the
8.8 divide's carry is always clear, the signed multiply's X its first partial
product, the search's X and A the divisor after the one found and its quotient.
The search takes what its caller left in the divider -- the dividend
(`cpu_io.h`'s `hw_dividend`, which costs no time) and the quotient of the
division the caller started -- and divides by the divisors after in C.

**The circle** keeps the outline in locals (`Octant`: x, y, the decision, the
near line) and writes its four cells, and the center's and radius's cleared
high bytes, once, as scaffolding (nothing reads the high bytes: the callers
write the cells as bytes). The original's chained carries move pixels, and they
are two named functions: `edge_rounding` (the top line a line higher, the far
lines' right edge a pixel further right unless their left edge is off screen, a
near line's left edge a pixel in when the far line reached the right of the
screen) and `decision_step` (the midpoint step, with `y - x` one less while the
near line's right edge is on screen, and one or two more near the diagonal).
Closing lines is `close_lines(g, c, first_line, lines)`, a plain fill; a whole
table closed runs three bytes past it in the original, a marked write of its
own (`spill_past_table`); and the lines above a circle whose top is on the
second line stay open, because the original's count for one line would run its
block move through the bank. A table that could reach the cells the original
reads back is refused by a guard (`scene_circle_table_is_clear`); no table the
game or the fuzzing makes comes near them. `scene_circle_draw` takes the
`GameState`, which the menus' one call site now passes.

**The scaler** (`scaler.c`) has no CPU struct and no carry chain. The
original never clears the carry between its sums; two of them move what the
player sees, and they are named: a row's samples start 1/256 of a pixel further
on when its source row lies further along the bitmap than the last row's
(`sample_nudge`), and a row's source is picked 1/128 of a row further on when
the call before left the carry set (the entry's carry, `row_nudge`): the
positions are fixed point, a sample's in 1/256ths of a pixel (`PIXEL_UNITS`)
and a row's in 1/128ths of a row (`ROW_UNITS`). The rest
come from sums that wrap past the end of the bank -- a sample's position along
its row, the plane pointer -- and the entry's guard hands those bitmaps to the
original, with a source limit a row's position compares with either way and a
source whose samples stay in the bank; no movie's call is refused. A sample's
position is then `(k + 1) * step + nudge`, and its time the samples it reads.
The wrapper derives the flags it leaves where the bitmap ends from the previous
end, this one's and whether the rows ran out, and the carry at the row loop's
head from the output pointer; the only record left is the scratch the original
packs pixels in, a memory write through `ORIGINAL_EFFECT`. Clearing what the
previous bitmap left and repeating a row are a plain fill and copy, their time
charged as the block move's.

**The script** (`script.c`) is a table of rows, each a mask and match over the
opcode, a handler, and what the port needs of the state to take the command
(a store in low RAM, a video setting a call expresses, a task that is on its
list). Each handler decodes its operands and calls an operation with named
parameters -- `assign(g, ch, reg, value, add)`, `count_loop(g, ch, counter,
limit, back_to, after)`, `store`, `gosub(g, ch, target, return_to)`,
`queue_sound`, `make_video_setting`, `stop` -- and says how the command ends.
The script pointer's moves across the original's code are named: while a routine
of the program runs, the script stays on the routine's address (a routine can
move it) and goes on past it from there; a store to the script's own pointer is
a jump; a sound goes unsent, and unheard, while a song is on its way to the
driver, which takes one message at a time. The operations leave nothing for
the original: each row has a kind (set, add, loop, ...), and after each command
the bridge leaves, through `ORIGINAL_EFFECT`, what the original's code for that
kind leaves -- the word it keeps in its channel's scratch (an assignment its
register's address, any other command itself), and its carry and overflow, from
a table of the flags per kind worked out from the command's register and loop
counter before and after it (`note_flags`). The flags live in P, so a call-out's
are the callee's with no copy. They are left because they are read: the attract
and title programs run their channels at `$7E:54B4` and go straight on to
`$7E:5777`, whose `$7E:3443` calls the random number mixer at `$C3:02DD`, which
starts with a `ROL A`; and `$7E:62F0` reaches the `ADC #$40` at `$7E:65B3` with the
carry the run left, `$7E:3448` between them saving and restoring P. No code in
either program or in bank C3 branches on the overflow, but the liveness
analysis counts it as read, so it is left too. A set's flags are both clear,
not the original's sum of the channel's page and the register's place they
come from, which carries and overflows on no channel's page. The A a turn ends
with is the ending command's kind's: the script's place after a loop or a jump,
1 after the stop, the plain end of turn's handler's place otherwise. The
video settings a script names by their register are a table of the scripts'
encoding. The wait for the music is `scene_music_status`/`scene_music_reached`
(`sound.c`), with its flags derived in the idle entry.

**Tasks and pages.** `SceneTask` and `SceneTaskList` (the vblank tasks at
`$0940`, the frame's at `$0920`) are types with operations (`scene_add_task`,
`scene_remove_task`, `scene_task_listed`). The review expected a task page
kind for `$7E:5DA7`'s "task's next step"; the page report says it is called
only on `$0A00`, channel 0's page, from a script's call, and the cell it
writes is that channel's register 2, where the title keeps BG1's vertical
scroll (`scene_title_start_ripple`). So there is no task page: the routine
writes a channel register, and its guard asks for a channel's page.

**Vocabulary in the map.** The sixteen bytes the program's shared routines
take their values in are `scene_call`, on the page kind `scene_program` (the
scene's own page, or a sequence channel's): a record of named, overlapping
fields, one per user (the operands, product, quotient, remainder, the 8.8
fraction and the divisor less one, the byte after the product, the transfer's
mode and register, the matrix generator's offsets, the script's kept word);
`scene_nmi_stub` and `scene_irq_stub` name the stubs' jump and target, so
installing a handler is two writes and no bit surgery. Named by finding their
readers: `$62`/`$64` are the title logo's X and Y (the sprite offsets its lists
are drawn at, `$7E:5D01`; the review found "cell 1" and "cell 2"); `$4B` is the
race's engine pitch (the sound driver's command `$89`, the effect voices'
pitch; the review found "presumably a parameter"); the words the old
`shift_history` moved are BG3's second row, which in mode 2 are the columns'
vertical offsets: the title's ripple. `scene_flags`' bit 7 has one definition,
named for both its meanings, and the pause, the song on its way, Start held and
the reset allowed are switches with one storage function each (`frame.c`). The
scene's DMA channel and modes, its two banks of work RAM, its tilemap buffer,
its blank tile and its two VRAM stepping settings each have one definition, in
`scene.h`.

**The display** writes the video service's structs: `(VidMode){ .mode = 7 }`,
`VID_LAYER_BIT(...)` sets, `.cy = -64`; `init_video` is one value, the first
screen every scene starts from, applied by one function; the race's line-effect
descriptors decode into a `LineEffectDescriptor`. The transfers through the
SNES-only escape name their registers (`VID_SNES_VRAM_DATA_LOW` and the rest,
added to `platform/video.h`); their per-call `steps` stay (TODO.md). No
assembly is quoted in a comment.

**Entries named for what they do**: 30 renames, among them
`scene_title_ripple_columns` (was `scene_shift_c00_words`),
`scene_run_frame_tasks` and `scene_run_vblank_tasks` (`_950`, `_940`),
`scene_wait_for_music_loop` (`scene_wait_apu_port3_loop`),
`scene_race_wait_for_tilemap_upload_loop` (`scene_wait_39_clear_loop`),
`scene_script_wait_for_sound` (`scene_script_wait_sound_port2`),
`scene_copy_resource` and `_to_upper_ram` (`_to_7e`, `_to_7f`), and the
loaders' blocks by what they put in VRAM (`scene_title_load_sprite_graphics`
for `scene_set_vram_4000`, `scene_race_load_bg3_map` for
`scene_race_set_vram_6800`, ...). The loaders themselves aren't ported
(TODO.md).

**The sprite writers** are one loader with a `clipped` parameter and no
in-band X: a sprite past the right edge is at X 257, and the word `$9801` the
original stores there only feeds the A its callers read. The loader leaves no
register record: a small list's A already has the list's high byte, and the
clipped entry's wrapper works out A's high byte and the overflow after the call
from the list's last entry, which the guard keeps clear of every cell the call
writes. The lists are read from work RAM (`wram_read8`), and the port the
original streams them through is left where its reads would leave it, once,
through `ORIGINAL_EFFECT` (the title's plane tiles, the same). The sprite table
is two views in the map, `scene_sprite_table` (the $200 bytes of entries) and
`scene_sprite_high_table` (its $20), with `SCENE_SPRITE_TABLE` and
`SCENE_SPRITE_HIGH_TABLE` in `scene.h`, and the port's high-table cursor is the
byte's place in the table, 0-31. Their guard, which
TODO.md asked for, takes a cursor whose sprites stay in the sprite table, a
list clear of the cell the original counts it down in (fuzzing found 2 and 15
lists of 20000 read over that cell), and a clipped list clear of the table.
The fuzz runs start the cursor where each frame's first list finds it; the
guard turns away the random lists too long for the table, about half.

**The frame** (`frame.c`) names the controller's buttons and the reset
combination, and installs its handlers through the stub's fields. Its bridge
sets only what the original's code it calls reads: A the pause word the frame's
work tested (0) at a task list's call, the carry the acknowledge's or the
controller's compare where the tasks see it, Y where the list's copy left it
(through `ORIGINAL_EFFECT`); the pause sound's and the song's calls declare the
rest dead, which the don't-care check verified (the driver's entry saves A,
which liveness counts as reading its high byte, so those calls keep it). A pause
word other than 0 or 1, which the original tests as a word in one place and as
a byte in another, is refused by a guard; no movie makes one.

**Fuzzing against the predecessor** (`tools/fuzz_compare.py`, seed 1, every
scene entry and variant in `tools/fuzz_routines.toml`, 87 runs): the counts
agree for every one. The runs that didn't, over the two rounds, were each the
rewrite's to fix: the unsigned multiply's middle sum (7 and 11 states, now
guarded), the scaler's samples read past the bank into its own scratch (24),
sprite lists read over the cell the original counts them down in (2 and 15),
the divisor search taking its first quotient from the dividend rather than from
the division its caller made (211: the fuzzer's states don't keep the two
consistent, the game's do), and a routine a script calls reading the A the
original loads it with (60 and 64, which the call-out's don't-care set had
declared dead: liveness can't see into the program's RAM routines, so A is set
and no longer declared). The third round's two: a small list read from work RAM
must read each sprite's Y before shifting its high-table bits, as the original
does, since a list can lie over the high table (7 and 3 states); and a loop's
flags must be worked out from the limit as it was before the loop ran, since a
script a jump sends into its channel's page can lie over the counter (1). What the guards turn away shows in the skipped column:
the sprite lists too long for the table from where the cursor stands (9907 and
10074 of 20000 in the tight runs, 15320 and 15785 in the loose ones, whose
cursors are anywhere in the table and whose lists anywhere in low RAM), the
single sprites whose high-table byte is past the table (112 of 20000, loose),
8 of 2000 plane tiles over the scratch, the 32-bit divides by 0 or past
`$7FFFFFFF` (10092 and 10017 of 20000) and the products whose middle sum
overflows (7 and 11); everything each accepts matches as it did. The scaler's,
the frame's and the sprites' runs are narrowed to what their guards take, with
the guard named in each entry's `why`. The plane tiles mismatch in 5 of 2000
states, and the frame's work in 1 of 10024, before the rework and after it:
the predecessor's residue, not the rework's.

**Time: charged, not kept in the logic's shape.** The review's item 36: the
first round kept the circle's and the scaler's block moves byte by byte, and
the divisor search dividing through the divider, because that shape is what the
timing estimate charged. Now a port charges the time as scaffolding, through a
general mechanism (`game/charge.h`, backed by `runtime/port/state_io.c` on
`native_charge_as` and `native_uncounted_begin`/`_end` in `native.c`):
`charge_block_move(g, bytes)` charges a block move's time at the block move
stub's calibrated rate, after a plain `wram_fill8`/`wram_fill16`/`wram_copy`
(`wram.h`), whose accesses take none of the port's own time; and
`charge_as(g, routine, accesses)` charges that many accesses of a registered
routine's work, the running port's own included -- the divisor search charges
three a divisor tried. Sixteen of the scene's `cost_milli` rows moved over the
two rounds: most by a few percent, the ones whose work is now C by more
(`scene_multiply` 6178 to 8688, `scene_title_expand_plane_tile` 3723 to 27517,
`scene_scale_bitmap_row` 2877 to 20318, `scene_draw_circle` 3051 to 17017,
`scene_find_divisor` 6952 to 5195). The circle's worst error in vblank is 11085
clocks, which its vblank-tail exception covers with the new figure
(`tools/timing_exceptions.txt`). The whole gate, every route included
(`--long-routes`), passes.

**The second review's follow-ups** (the third round took its must-fix items,
above; these it accepted as later work, each in TODO.md): the scaler's carry
into the leftover's measure, threaded through three functions, a block move
charged as `bytes - 1` without saying why, and `in_scaler_bank`'s two
arguments whose roles its callers mix; the iris's `mode` byte across the
menus' call, which wants an `IrisShape` and a `leave_outside` flag, and a
`count` local that exists only for scaffolding; one `scene_queue_sound` for the
frame's and the script's copies; the controller's carry derived twice; the
button names, which belong in `platform/input.h`; `SCENE_VBLANK_TASKS` against
the map's `scene_early_tasks`; the script's register encoding and its operand
offsets in the adapters; the divisor search's `tried`, and `divide_8_8` as
`fraction_along`; `race_matrix.c`'s accumulators; the bank `$FE` resource
accessor; `init_video`'s sites and computed `PF_AT`, which wait for
`vid_apply_screen`; the transfers' `steps[]`, the platform's; the title's logo
sweep and ripple, the program's code still; and `game/attract/`, which is the
same program and belongs here (the two headers now point at each other).


## Phase 3a: the menus off the machine

`game/menu/` was the second-largest backlog: ten of its twelve files on
`tools/portstyle_baseline.txt`, 0.35 operations a line and 97% of its functions
on the machine. The rewrite takes all of them but `menu/epoch.c` (the Epoch's two
Mode 7 screens and the End of Time's gate, 1,827 lines and a chunk of its own)
to the bar, in the field's order: a domain module written first, then the files
that use it, and each entry's widest don't-care set declared before its wrapper
is written. Nothing was added to the burndown. The subsystem now scores 0.07
operations a line with 40% of its functions on the machine (the entries'
wrappers, the guards, and the bridges round the call-outs); every rewritten file
is under the bar and off the baseline.

**The files, by the game.** The leaves -- `glyphs.c` (a glyph's pixels into
tile graphics, in the eight packings the font uses), `text.c` (the string and
number drawers, the control codes a switch of named operations), `vram.c` (the upload queue
as records), `save_screen.c` (the load screen's slots, a header and a row per
member), `frame.c` and `sound.c` (the waits) -- are plain functions over the RAM
map with their ROM through `menu/menu_data.h`'s accessors. The sprite-object
mode, the half of bank C2 the Epoch's map and the End of Time run on, is a
module: `object.h`/`objects.c` (the sixteen slots: start,
turn round, move, wrap onto the map, and the per-frame run over the slots'
handlers), `sprites.c` (the sprite list the objects draw through, the sprite
script each runs, and the window records' sprites), `wave.c` (the one object
handler that isn't a script: a wave down the screen's lines), and the object
scripts. `display.c` is both modes' screen setup, vblank handler and loaders
through the video service: the shadows sent as named values, the queued uploads
by record, the colour math and window line tables built in C, and the frame's
block move a `wram_copy`. `object_script.h`, and the old `objects.c` that held
the scripts' commands inline, are gone.

**The object scripts are a language** (`script.h`, `script.c`,
`object_script.c`), on `game/event/`'s pattern. `object_script.c` is 33
operations -- `change_byte`, `copy_byte`, `branch_if`, `for_frames`, `move_to`,
`start_handler`, `mark_table_entry`, `scroll_map_for`, `play_song`, ... --
named for what they do to an object or the game, each with typed parameters,
and a table of 83 rows, one an opcode, saying how its operands decode into
them (`SLOT_BYTE(1)`, `CELL(1)`, `OPERAND_WORD(1)`, `CONSTANT(MENU_ACROSS)`).
The families that were a function an opcode are rows of one operation:
`change_byte` 16 opcodes (clear, count up and down, set, set and clear bits,
add and subtract, on a slot byte or a cell), `branch_if` 16, `start_handler` 8,
`copy_byte` 4, `for_frames` 3 (move and animate, animate, wait), `move_to` 2.
`script.c` is the interpreter, as C: decode the row, run it, follow its flow
(next, branch, go to, wait a frame, end), keep the object's place. What reaches
past the object -- scrolling and redrawing the map, the sound queue, a routine a
script names by address -- goes through `ScriptServices`, so the map's port can
move under it. Every handler stays registered (`menu_object_cmd_*`, generated
from one list in `script_bridge.h`), so fuzzing and the ROM's own dispatch still
find each one.

`script_bridge.c` is the machine side, and the only file of the language that
takes a `Snes *`. It runs each command at its handler's time
(`NATIVE_TIME_AS`), pushes the dispatch's return round it as the original's
`JSR` does, and derives what a command leaves for the interpreter's loop -- X,
the carry -- from a table parallel to the language's, through
`ORIGINAL_EFFECT`; the language's rows carry none of it. A routine a script
names gets the registers its handler has at the call: A the address's word (or
its bank), Y the operand's offset, N and Z from A, and the overflow the
interpreter's last `ADC` left, tracked from command to command. A command the
port doesn't take goes back to the original's interpreter at the top of its
loop (`$C2:0F73`) rather than through its table from inside the engine, which
on a random opcode could re-enter the engine from the machine.

**Shadow mode, and what it found.** Each group was committed shadow-clean over
the whole library (26 movies, 0 mismatched). What it found on the way: the
object mode's vblank handler carries on after the sprite list's reset in the
16-bit accumulator that reset leaves, and the port first handed the rest back
in 8 (the NMI's A and P); the wave's phase is the cell at `$01E2` that event's
calls leave (`menu_wave_phase`; TODO.md); the tilemap row copy sets the data
bank to `$7E`; the upload queue's Y is a byte index, so an 8-bit index register
truncates it; the sprite hiding the vblank handler does before the objects' run
leaves Y a byte and the carry and overflow clear, which the handlers read.
Against `main`'s per-entry totals, every menu entry has the same calls, and
every one compared matched, but for these moves, each explained:
- `menu_draw_save_slot`: 1,139 compared of 15,708 (was 15,707): the load
  screen draws its slot as C from `menu_show_save_slot` (`NATIVE_TIME_AS`), so
  the 14,590 inside it are inside that entry's comparison;
- the commands that call out mid-command (`call_routine` 96,028, `sound_18`
  228, `redraw_map` 36, `play_song_unless_flag` 8, `play_song` 1,
  `call_routine_long` 2, and 2,699 of `scroll_map_for`'s 9,093): the
  interpreter runs them as C, so their calls are inside
  `menu_run_object_script`'s comparison rather than compared on their own;
- interrupted calls: `menu_run_object_script` 9 (was 3), `menu_object_nmi` 1
  and `menu_show_save_slot` 1 (were 0): an interrupt landing in a longer or
  shorter native stretch; the paths are the ones the library compares
  elsewhere, and the fuzzing below covers each entry alone;
- `play_song` refuses 8 of its 9 calls, as it did before: the game reaches it
  from `play_song_unless_flag`, not the dispatch, and the interpreter runs
  both as C.

**Fuzzing** (`tools/fuzz_routines.toml`, the menus' section: 196 entries on two
states, the field menu open and the Epoch's map). `tools/fuzz_compare.py`
ran every menu entry, rewritten or not, against `main`'s ports with the same
seed and domains: 197 runs, 12,863 of 430,672 compared states mismatched
before and 12,362 of 258,516 after. All but 105 of the after's are the
predecessors' own, on the same states in `epoch.c`, `map.c`, the irises of
`object_commands.c`, `menu_init_video_memory` and
`menu_window_set_line_effect` (those of `map.c` and the irises are gone since:
below, "After the review"); the 105 are the two accepted divergences
(`menu_draw_text`'s 97 and the load screen's 8, below). Fewer states are
compared after because the guards turn away what the game never makes, which
is the point of them; where that left an entry with few or none, the domain
was reworked so its guard takes the states (the load screen's summaries, both
upload queues' records, the command script and sprite script built in work
RAM, the upload buffer where the game keeps it), and the animating commands
run twenty thousand. Setting it up took a toml section of 197 entries on two
base states, and the 16-area limit of `ct_fuzz` is why the command entries
pack their script pointers as repeated bytes (`$7E:1C1C`, `$7E:1D1D`).

What the fuzzing found, each fixed in the port or refused by a guard:
- the glyph line takes its byte index as a word, as the original does;
- the object queue's records at an end past `$F8` run on past the queue, as
  the original's page-relative writes do (`_unchecked` accessors);
- the number drawer read a table offset the menus never use, its own cells,
  the I/O registers and the stack (the conversion pushes its place in the
  number as it reads each byte): the predecessor's 223 mismatches, now refused;
- a sprite script's jump forward holds the script on the jump: the original
  tests the offset's sign where it means to test the step, so only a jump back
  goes on (the port had it go on either way);
- a sprite script inside the objects' slots changes under its own reading, and
  one with a command past the eight jumps into code: both refused;
- the object mode's vblank handler writes its frame flag through the page, so
  a call-out that comes back on another page or data bank goes to the
  original; the menus' handler the same;
- the map's commands set the step once for both planes, the second plane
  scrolling by what the first's scroll left in the cell;
- the load screen: a slot drawn without its header clears the era label's
  width (the original clears the row count a word wide, over the width after
  it), and a portrait whose row or character the portrait's guard would
  refuse is drawn by the original's; the menus' upload queue refuses sources
  over the top of the stack, where the original saves its registers first.

Two divergences are accepted, on inputs no game state makes: a string's
control code past `$0C`, or one included deeper than four, draws nothing where
the original jumps or recurses (97 of 1,503); and the load screen's member
rows keep their own place where the original reads it back from its cells
after the window list and the portrait, which a list run over random memory
changes (8 of 3,394). The map's strips parted from the original's where the
upload buffer was near the bank's end, in `map.c`'s port before this rewrite
and in its first pass, so the command fuzzing keeps the buffer where the game
has it; the map module's guards now say so.

**The guards refuse only what the game never makes.** Each is a claim about the
game, checked against the library's totals (none refuses a real call but
`play_song`'s, above): numbers read from the I/O registers or open bus, from
the number drawer's own cells or from the stack, or converted from a table
offset the menus never use; uploads from the I/O banks; fills and block moves
over the stack or the move's own stub; a vblank call-out that comes back on
another page or data bank; a wave entered for another slot; a colour math list
without its end; a script outside the ROM's banks and work RAM, a command
naming the interpreter's own cells, a sprite list not as the mode lays it out,
and an upload or map command with the object queue off a record or the upload
buffer out of work RAM; and a sprite script that doesn't read as the language
where a command runs it (every command it can reach one of the eight, outside
the objects' slots, before it stops, holds for good or goes round). That last
one first refused 12,610 of the interpreter's real calls: objects that never
animate have no sprite script at all, and a show or wait of no frames holds its
script for good with the next script's bytes after it. It is now asked only by
the commands that animate, of the script they run, and stops at a hold; it
refuses none. Where the load screen draws a portrait inline and the portrait's
own guard would refuse it, the original's draws it.

**Don't-care declarations.** The entries' sets were widened to what
`tools/dontcare_check.py --list` offered before their wrappers were written,
and the check agrees with every one (0 findings, the strict solve for
call-outs included). The window command lists' call-outs in `save_screen.c` declare the
carry and overflow on a poisoned fuzz run (`dc-evidence`: 500 states, 3,340
call-outs scrambled, 0 mismatched); the idle waits stop with `m_stop_at` at
their loop's head.

**After the review.** A qualitative review by a reader who hadn't seen this
section rated two files translation, both outside the first pass because they
were under the style bar: `map.c` kept its loop counters in direct-page cells,
multiplied a row by 96 with the original's swaps and shifts and queued its
strips with its own copy of the queue's code; `object_commands.c` emulated
`ADC` with a carry-and-overflow struct and named routines by their addresses
(`clear_7f0062`, `step_28`). Both were rewritten:
- `map.c` is a module over the game's state: `scroll_plane(g, plane, axis,
  step)` moves a plane a pixel at a time and builds each strip that comes into
  view from one table of strip shapes (a column down the map, a row along
  it), `redraw_plane` builds and sends the eighteen blocks, the strips are
  queued through `vram.h` (which gained `MenuObjectUpload` and
  `menu_queue_object_uploads`), the plane tables come from `menu_data.h`, and
  a row starts at `row * MAP_COLUMNS`. The original's cells get their final
  values once, as scaffolding; the thin entries read the plane and step cells
  and set X as the scripts read it. Its guards (the page, a plane that
  scrolls, the tiles on the map, the buffer in work RAM above low RAM, room
  in the queue) refuse none of the library's calls.
- the per-line effects and the irises moved to `menu/line_effects.c`, which
  the wave shares: a slot's table set up in one place, the tables' addresses
  named, the irises' copies as loops over lines. `object_commands.c` keeps
  the scene routines, named for what they do (`read_scene_mode`,
  `clear_scene_flag`, `keep_song`, `bob_second_plane`, `accelerate`, `drift`,
  `load_scene_graphics`), as plain C over `object.h`'s place and velocity:
  a sum is `a + b`, and the only carries are the ones the scripts read, derived
  in the wrappers. Every entry declares all but X and the carry dead (the
  liveness analysis agrees), and the story byte at `$7F:0062` has a name in
  the map, if not yet a meaning.
- `epoch.c`'s gate matrix tables have their name back.
Shadow totals over the library are unchanged, entry by entry. Fuzzed against
`main`'s ports, same seed: the map's entries on a domain their guards take
(scroll across 0 of 1,989, down 0 of 1,012, the redraw 0 of 2,000, the block
upload the predecessor's 2 of 1,998); every bank C6 entry 0 mismatched, the
irises on a new domain (their centre and radius on the screen), where the
predecessor's squashed irises mismatched 11 and 9 on radii under 2 that the
guard now refuses. Fuzzing found one ordering: the block upload's entry wrote
its VRAM cell after sending the block, where the original writes it first,
which only a buffer on the direct page can see.

**What is left** is in TODO.md, "What menu's rewrite left": `epoch.c`; what
the review rated "mostly"; the two accepted divergences; the save RAM flag
read through the ROM accessor; and the glyph drawer's font addresses, which
come from the still-original text engine.

**Timing.** The rewritten ports make far fewer bus accesses than their
predecessors, so the menus' measured `cost_milli` rows were recalibrated from
shadow runs over the library (`tools/timing_gate.py --calibrate`), and only
those rows were taken. Two ports do in a step what the original does at a
length that follows the data, and charge the difference (`charge.h`, through
`ORIGINAL_EFFECT`): the number conversion, bit by bit with an add for each bit
set, which left `menu_draw_text` 78,077 clocks off at worst before it was
charged; and the sprite list's drawing, per node and per sprite dropped across,
dropped down or shown. Two entries went into `tools/timing_exceptions.txt`
with their numbers: `menu_draw_sprite_nodes` (2.97% of its 236 calls in vblank
off by a line or more, at most 3,416 clocks, writing the sprite table in RAM)
and `menu_object_cmd_animate_for`, which runs its object's sprite script
inline (1.54%, at most 2,982 clocks).

## Phase 3a: the controller off the machine

`game/core/input.c` was core's one file on `tools/portstyle_baseline.txt`
(0.54 operations a line): the per-frame input work the menus, the field and
battle share, four registered entries -- `input_update` `$C2:84D2`, which saves
every register and calls the other three; `input_read_joypad` `$C2:84EC`;
`input_map_buttons` `$C2:8545`; and `play_time_tick` `$C2:85AE`. It was the
original's instructions with the registers carried through them (the
controller's two bytes shifted and `TSB`ed together, the assignments `ROR`ed
into their byte eight times, the clock's digits walked with X, and the wrapper
calling its three sibling ports through emulated `JSR`s). The rewrite is 0.08
a line, with only the entries' wrappers on the machine.

**The file, by the game.** Buttons have names now, in two orders: the
controller's (`PadBits`, `IN_BUTTON_*` in `platform/input.h`, where scene's
`frame.c` takes its Start and every later subsystem finds them) and the game's
own (`Buttons`, `BUTTON_*` in `core/input.h`, the order of the words it keeps
them in). One table says where each button goes from the one to the other, and
another which of the game's buttons each of the player's eight assignments
stands for; a set goes through the assignments by testing each group whole, and
the direction pad's byte passes through. The reset combination
(`RESET_BUTTONS`) is defined once, in `core/input.h`, for both it and the
scenes. The repeat is one function: buttons held through repeat when the
countdown runs out, and it starts again from the mode's delay. The clock is a
`PlayTime` of six digits, advanced by `count_up` in a chain that stops at the
first digit that doesn't roll over; running past its last digit marks it.

**The entries.** Each declares the widest don't-care set the liveness analysis
accepts: A added to the controller read; A, D and the data bank to the mapping
and to the clock, so neither wrapper sets anything but the widths; and N and Z
to `input_update`, whose closing `PLB` sets them from the caller's data bank.
`input_update` runs the three parts as C, each at its routine's time
(`NATIVE_TIME_AS`), leaves its caller's registers alone rather than saving and
restoring them, and charges the original's pushes and pulls as scaffolding
(`charge_as`). Only where the controller read stops -- at the original's wait
while the console's automatic read is still busy (`$C2:84F9`, the item in
"Timing without a beam") or at the reset (`$C0:FF00`) -- does it build the
frame its saves leave on the stack. Both stops declare their own sets, now
wider (the wait: A's high byte and the overflow; the reset: everything but Y
and the widths). The mapping is guarded to the controller's page, which it has
in all 651,497 of the library's calls, so the guard refuses none.

**Measured.** Every call in every movie matches: 651,497 `input_update`,
686,666 `input_read_joypad`, 651,497 `input_map_buttons` and 686,666
`play_time_tick` (interrupted 3, 1, 2 and 0). Fuzzed against `main`'s ports with
`tools/fuzz_compare.py` (the four new `core` entries in
`tools/fuzz_routines.toml`, on the `menus` state), seeds 1 and 2, 20,000 states
each: 0 mismatched before and after, for all four, none skipped. The four
`cost_milli` rows were recalibrated from the library's shadow runs and only
they were taken; the read and the mapping now make far fewer bus accesses
than the original's instructions, and the clock, which reads and writes all
six digits, more.

## Phase 3a: the rest of core -- palette animation, animated tiles, the block move, the display

After the controller, the rest of `game/core/` was off the baseline but read
as translation: `palette.c` took the machine in every function, reached its
slots as byte offsets through the original's Y, used the hardware multiplier
to compute and re-read its scratch pointer every color; `tile_animation.c`
did offset arithmetic into the slot records and computed its call sites;
`memory.c` was the block move as `m_mvn`; `display.c`'s blanking mixed its
register work into the logic. All four are rewritten; `sound_driver.c` is
left, since it is the original driver's upload protocol and goes when the C
sound driver becomes the default. Core scores 0.03-0.08 operations a line
per file, and only the entries' wrappers and guards take the machine.

**Palette animation** is a module over the slots: an `AnimationType` for what
a slot does (cycle frames in order or from a table, rotate either way, fade
or pulse toward white or black), named operations (`show_frame`,
`rotate_colors`, `fade_step`, `pulse_step`, `restore_colors`), and the
fourteen fade modes as two tables of what each changes (`FadeChannels`: the
channels it scales, those it keeps, and the byte of the working color the two
one-byte modes leave). A channel is scaled in C at the place in a byte where
the original holds it (blue two bits up, green three), since that sets the
rounding the player sees. The location's animations load from the ROM's sets
through a `SetStream`; `palette_data.h` takes the game's state. What the
original leaves for nobody but shadow mode -- its scratch cells, its long
pointer, the multiplier's operands, the work RAM port's address -- is written
once, as scaffolding, from a table parallel to the game's (`FadeLoop`), and
the handlers' X comes from the slot afterwards in the wrappers (`run_end`,
with `palette_animate` noting which slot ran last). The dispatcher and the
fades keep their time per handler and per mode's loop through a new
`TIMED_AS` (charge.h),
and a mode's loop charges per color what the original spends there on its
scratch and the multiplier, which the port does once (the scaffolding's own
accesses uncounted: `charge_uncounted_begin`). The multiplier's first operand,
which a mode's loop is entered with, is read through `hw_multiplicand`
(cpu_io.h). Each entry is guarded to the slots the game makes (a count of at
least 1, the colors in the palette, a rotation's and a frame's count under
128, a frame table in the cartridge; a mode's loop entered with X its slot's
first color): the original's loops take a count of 0 as 65536 and run on past
the palette, which no game state does. `re/ram.toml` gained `frames_bank`.

**Animated tiles** send a slot's current frame through `vid_send_vram`, with
each slot's copy of the original's code named at its call, and the stack
bytes the original's saves leave are no longer written: a guard refuses the
one state that could send them (a slot reading its tiles from the top of the
stack). **The block move** is a copy through `wram.h` charged per byte at the
stub's own rate, with the registers an `MVN` leaves (`m_leave_mvn`); its guard
also refuses a move over the stub itself. **The display**: blanking and
stopping is a function of the platform, and both entries declare what their
callers don't read (A, and the power-on block's direct page and registers at
the boot), so the wrappers set only the data bank and the interrupt mask.

**Measured.** Over the library every call of every entry matches with the
same counts as before, so no guard refuses a call the game makes. Fuzzed
against `main`'s ports (`tools/fuzz_compare.py`, the `core` entries in
`tools/fuzz_routines.toml`), seeds 1 and 2, 20000 states each: the handlers,
modes, loader, clear, the display entries and the tile upload 0 mismatched
before and after (the tile guard refusing 64 and 59 states that read the
stack); `palette_animate` 0 where `main`'s port mismatched 47 and 44 (a frame
slot with a count of 0 over its own return address), refusing the count-0
states; the block move 0 where `main`'s mismatched 14 and 16 (moves over the
stub), refusing 67 and 74. `palette_animate` and `palette_lighten_all` are in `tools/timing_exceptions.txt` for their vblank tail (3.9% and 3.6% of vblank calls off by a line or more, writing the working palette in RAM). The rows of the rewritten entries were
recalibrated, and those whose work is charged at the block move's rate,
which the rewrite moved: `scene_draw_circle`'s two and three of the Epoch's
(`menu_epoch_build_map`, `menu_epoch_build_gate_window`,
`menu_epoch_time_dial_object`); the time dial's setup call keeps a vblank tail
of 1.58% on that account, and is in `tools/timing_exceptions.txt`.

## Phase 3a: the menus' follow-ups and the Epoch

The menus' rewrite left two things (TODO.md, "What menu's rewrite left"): what
its qualitative review rated "mostly" in nine files, and `menu/epoch.c`, the
last menu file on `tools/portstyle_baseline.txt` (0.16 operations a line over
1,827 of its 2,212 lines, every function on the machine). This pass takes both;
`menu/display.c`'s findings went with the platform pass that landed beside
it (PLATFORM.md, "The services' gaps the reviews found, closed").

**The follow-ups.** Each became what the review asked for:
- `menu/script_bridge.h`: every command's entry is named for its operation in
  `object_script.c`, and where one operation serves several opcodes, for what
  the row fixes (`menu_object_cmd_forget_everything`, `..._change_byte_slot_add`,
  `..._branch_if_cell_at_least`, `..._play_effect_on_voices_67`); the bridge's
  per-opcode facts -- what a command leaves in X and the carry, which of the
  original's routines it calls and from where, how many records it queues --
  are one table parallel to the language's, and the scroll's and the sound
  queue's call sites are picked by the row's kind of call, not by opcode
  literals. `tools/disasm/objscript.py` takes the same names.
- `menu/object_script.c`: the look's bits are `object.h`'s (`MENU_LOOK_*`, one
  definition the sprites use too), and `set_look` takes the part it replaces;
  the story flags that hold the layer over the map still, and the sound
  driver's commands (`SOUND_PLAY_SONG`, `SoundEffectVoices` in
  `game/sound/sound.h`), have names; the palette loader's row and hand-over
  flag are fields of the slot (`palette_row`, `palette_hand_over`); and the
  language's parameters are named for what they are (`x`, `y`, `pixels`,
  `fraction`, `scene_script`, `effect`, `pan`, `song`), so no operation takes
  a `word`. The three operations that took the whole command
  (`copy_map_tiles`, the call, the sounds) take their parameters.
- `menu/sprites.c`: the sprite scripts are read by a table of rows (what a
  command does, how it changes a byte, how long it is), decoded once into
  named operands; the interpreter and the well-formedness walk share it.
- `menu/vram.h`: `menu_queue_object_uploads` is the only way into the object
  mode's queue, `MenuObjectUpload`'s port a `MenuVramPort`.
- `menu/save_screen.c`: the portrait takes its row, character and tilemap as
  parameters, read where the rows' command list left them, and the load
  screen's call of the slot's drawing names its return.
- `menu/text.c`: a control code's operands are decoded by its row's layout
  into named fields.
- `menu/map.c`: the scroll returns nothing; its entries derive X from the
  plane's final scroll and the queue's end, and the last strip's working
  values go through one scaffolding record. The redraw takes the platform and
  times the block uploads through `TIMED_AS` (`charge.h`, new: the tag
  `NATIVE_TIME_AS` gives code that has the machine, for code that hasn't).
- `menu/line_effects.c`: the irises' bars and the capsule's gap are line
  fills (`wram_fill16`, the block move charged), the squashed iris walks lines
  from its centre, the slots' tables are named in the RAM map
  (`menu_slot1_line_values` and five more, the sprite-object mode's own), and
  each copy of the starters programs the channel from its own literal site. A
  slot's small table, its channel and its enable bit are operations the time
  dial and the layer's wave now use too (`menu_lay_out_line_slot`,
  `menu_program_line_channel`, `menu_turn_on_line_slots`).
- `menu/object_commands.c`: the routines the world maps' scenes call, by
  purpose (the scene's state, the song, motion, pictures, and now three that
  put objects on the map at random); the Epoch's area check is in
  `epoch.c`; the scene mode's bits and the loaders (`..._load_mode7_graphics`,
  `..._load_second_mode7_palette`) are named for what they are.

**`menu/epoch.c`** is now the Epoch's screens alone, rewritten on the object
mode's module and the line effects: the map view (the world map sampled a
colour a tile, drawn as a picture a pixel a map tile across, laid out on the
plane; a cursor that walks its path turning and zooming it; three markers; the
blue gradient) and the time dial (the gate's window drawn from three turned
corners, the spin's double-buffered cosines and sines, the dial through its
periods, seven digits, the gate's graphics). Its handlers are functions over
the slot's named fields (`angle`, `target_angle`, `turn`, `period`, `zoom_x`,
`path_step`, new in the RAM map) returning what the object did this frame,
from which the entries set the carry and the accumulator's width the
original's path leaves. What the original leaves in the mode's direct-page
cells, the multiplier, the divider and the work RAM port is written once, at
the end, by `leave_*` scaffolding: 26 of the file's cells, worked out from the
predecessor's register-level port. Three things moved out: the four objects
that move the layer over three world maps (`menu/top_layer.c`, new:
`menu_top_layer_slow_drift`, `_drift`, `_wave_drift`, `_wave`, which the
predecessor called the gate's, as the RAM map did the wave's buffer and table,
now `menu_top_layer_wave_lines`, `_at` and `_table`), the three routines that put a scene's objects
on the map at random (`object_commands.c`: `menu_object_place_under_the_view`,
`_under_the_stretch`, `menu_object_give_random_velocity`), and the End of
Time's gate screen, which stays in `epoch.c` as its own small section. The
sibling routines the screens called through their ROM addresses are C calls
now (the object starts, the sprite scripts, the uploads, the window, the
multiply). The four that call out themselves (the map view's graphics and
its build, which call the menus' loader and the world map's, and the gate's
graphics and its stream loader, which call the decompressor) are still called
through the machine, as the original calls them: each is an entry of its own,
which shadow mode compares and the timing gate measures on its own, where run
inline their time would fall into one stretch of the screen's. `epoch.c`
scores 0.02 and is off the baseline.

**The video service's new calls.** The platform pass that landed alongside
(PLATFORM.md, "The services' gaps the reviews found, closed") left
`epoch.c`, `map.c` and `line_effects.c` on the older forms; they are on
`vid_send_vram`, `vid_send_vram_half` and `vid_set_line_slot` now, and
their 25 sites' write orders are in `runtime/platform/snes/encodings.c`.

**Shadow mode.** Over the 26 movies, against `main`'s logs before the pass:
no mismatch, and every menu entry's calls, compared calls and refusals are
the same, entry by entry. Two guards refused real calls on the way and were
narrowed to what the handlers read: the gate matrix's (the slot in X is a new
object at the cursor path's end, not the running one) and the digits' (a
hidden digit's sprite script is whatever its slot held, and it shows nothing
until it is given one; 4,823 of 6,636 calls were refused until the guard
checked the script it would run). The map view's graphics and build, the
gate's graphics and its stream loader went through a stage of running inline,
where shadow mode compares them only inside their screens; they are entries
again, compared on their own.

**Fuzzing.** `tools/fuzz_compare.py` against `main`'s `ct_fuzz`, seed 1, over
the menus' 197 entries: every entry but one has the same counts before and
after or fewer mismatches after. `menu_epoch_load_gate_stream` mismatches 16
of 173 compared states against 17 of 174 before, the same states: with a
random bank the decompressor reads through I/O space, where open bus holds
what the interpreter's fetches left (PORTING.md). The differences fuzzing
found in the rewrite, all fixed in the port: the map matrix takes its sine's
angle from the object in X (the cursor, or the object it has just started)
and the rest from the cursor, as the predecessor had found; the map
picture's tile reads both palettes before drawing either pixel, and the last
tile group it looked at is the last one drawn, not the last on the map; the
path's zooms and the map view's graphics number are read with the bank's
24-bit carry; the gate tilemap's row table is read as a word, its low byte
kept, which matters where the table is pointed at hardware registers; the
dial and its window draw into the tables the dial's first step put in bank
`$7E` or `$7F`. New constraints: the dial's window bank, the row upload's
table in ROM and rows in bank `$7F`, and a slot running for the gate's
graphics (which counts the digits' periods in it).

**Timing.** The rewritten entries were recalibrated (`menu_epoch_*`,
`menu_top_layer_*`, the three placements, `menu_start_gate_screen`), and
nothing else. The inlined siblings are named with `NATIVE_TIME_AS` where
they have the machine and with `TIMED_AS` (`game/charge.h`, new) where the
logic has only the game state; block moves and work RAM fills charge the
original's per-byte time (`charge_block_move`), and the gate window's drawing
charges its per-line accesses. That took `menu_epoch_time_dial_object`'s
vblank tail from 14.24% of its vblank calls off by 1024 clocks or more to
0.53%, and its worst from 45,506 clocks to 4,271, so both its exceptions left
`tools/timing_exceptions.txt`; `menu_epoch_build_gate_window`'s tail went from
88.8% to 2.0% and keeps its exception, its time following the triangle's
lines. The load screen's portrait is timed as before: the check whether it is
the port's to draw stays outside its `NATIVE_TIME_AS`, since inside it charged
the portrait's time to the rows it hands to the original.

## Phase 3a: the battle off the machine

`game/battle/` was the largest backlog: twenty of its twenty-seven files on
`tools/portstyle_baseline.txt`, 3,367 operations in 10,310 lines (0.33 a
line), 99% of its functions on the machine and 53 names shaped like addresses.
The rewrite takes every file to the bar. The subsystem now scores 0.02
operations a line with 31% of its functions on the machine -- the
entries' wrappers, the guards, and the bridges round the call-outs to the
original's code that is still generated -- and none of its files is on the
baseline. It was done in six groups on branches cut from `battle-3a` and
merged back one by one, each shadow-clean over the library before its merge:
the combatants, timers, arithmetic, scripts, main loop, handlers, waits and
sound first (they set the pattern the others follow), then the objects'
drawing and effects' motion, the screen effects, the windows, the combatants'
animation and graphics, the setup, and the frame's display work.

**The foundation.** `battle/battle.h` is the subsystem's vocabulary (the
eleven battle slots, the party and the enemies, where a combatant stands);
`battle/entries.h` holds every registered entry's prototype, which is what the
old `battle.h` was. `battle/bridge.h` is the machine side, and says so: the
guards the entries run under (the battle's page, D = 0 with the data bank at
`$7E`; the sprite drawers' page `$1D00`; a combatant's stat block in X; A's
high byte clear where the original indexes with all of A) and `battle_call` /
`battle_call_long`, the one way a port calls the original's code: the call with
its dead set (which `tools/dontcare_check.py` reads as it reads `m_jsr_dc`),
and a check that the callee came back as the battle runs (8-bit A, 16-bit index
registers, binary, the same page and bank), stopping at the call's return for
the original to carry on where it didn't. `battle_run` does the same for a
sibling port run as the original's `JSR` runs it, its return pushed. Each
group's shared concepts have a header of their own: `combatant.h` (a
combatant's statuses as a set, the kept ones beside them), `timers.h` (the
thirteen timer kinds: run, stopped, followed, run out), `arith.h`, `effects.h`
(the eight effects, the eight anchors, the movers the paths move, the effect
records' encoding), `animation.h`, `window.h` (a tilemap cell, the layouts, a
member's line, digits, the menu's "nobody"), `screen.h`, `frame.h`; and every
ROM read is a named accessor in a `*_data.h` (`sprite_data.h`,
`animation_data.h`, `window_data.h`, `screen_data.h`, `setup_data.h`,
`frame_data.h`), on `game/rom.h`.

**The files, by the game.**
- *The fight* -- `combatants.c` (was `stats.c`: a combatant's statuses followed
  every pass, its low HP noted, its script restarted or its turn stopped as its
  statuses say), `timers.c` (the timers counted down and the run-out ones
  handed to their kinds' handlers through a C table), `main_loop.c` (the
  battle's start, drawing the timers' and the enemies' orders from a pool; a
  pass: the damage cleared, the combatants updated, the battle lost, won or run
  from, then the timers or the turn; the turn: an enemy's script picks its
  action, a member's menu opens or its status picks for it, the party's queued
  actions carried out one at a time, and the enemies it hit react through
  their scripts' reaction blocks; the rewards), `damage.c`, `battlefield.c`
  (the grid rebuilt with the combatants' cells marked), `reset.c` (what
  animates the battlefield put back to nothing at the start: the scripts, the
  animations, the effects and anchors, the trails, the palette animations, the
  rising numbers), `arith.c` (was `math.c`: multiply and divide as C, the
  hardware unit's registers left as scaffolding), `buttons.c`, `scripts.c`
  (the animation script channels, the commands through a C table).
- *The combatants on the battlefield* -- `animation.c` (a combatant's pose
  chosen from its statuses, its animation stepped a frame at a time, three
  combatants a frame by group, the party turning to face its nearest enemy),
  `objects.c` (screen bounds, the animation scheduler's search), `sprites.c`
  (a frame's tile list built into the object's tiles, the palette animations,
  the effect animations' index, the bitmap shapes), `tiles.c` (the
  battlefield's tiles decoded), `draw_objects.c` (the drawing pass: the
  combatants sorted by how far down they stand, then each one's front effects,
  tiles, frame, back effects, shadow and afterimages, a leaf per part
  returning the next free sprite), `object_motion.c` (the damage numbers
  rising, scripted moves, anchors following, trails, the party's colour
  cycles), `effect_paths.c` (the nineteen movers' paths, one operation per kind
  in a C table: line, jump, spiral, orbit, homing jump, flat orbit, flicker,
  chase; the arithmetic plain C), `effect_animation.c`.
- *The screen effects in bank D1* -- `screen_palette.c`, `screen_gradient.c`
  (the HP gradient), `screen_graphics.c` (an effect's graphics loaded),
  `screen_window.c` (the window line table as a concept: the top border, the
  coming and shown buffers, bands, the wipe, waves, nudges, fills through a C
  table), `screen_beam.c` (the beam's two window quadrilaterals),
  `screen_motion.c` (jitter, swing, the effect settings as a C table),
  `screen_setup.c` (the fade tables as rising and falling pairs).
- *The windows* -- `window.c` (the whole battle window redrawn: the members'
  lines by layout, the enemies' names, the command windows and their frames),
  `status.c` (a member's HP and MP a frame, the gauges), `digits.c`,
  `menu_window.c` (the command and tech pages, the cursors over the
  battlefield, whose combination techs are called off, the frames' reset),
  `numbers.c` (the script's number commands), `window_leaves.c` (the carries
  the original's callers read, worked out beside the wrappers).
- *The setup* -- `setup.c` (the two entries and the one call-out left, the
  name converter), `setup_reset.c`, `setup_screen.c`, `setup_names.c`,
  `setup_party.c`, `setup_enemies.c` (an enemy from its record and its
  `EnemyHeader`, the formation, the enemies' rules, the kinds).
- *The frame* -- `frame.c` (the vblank's frame work and the main loop's frame
  update: the dispatch on the battle's mode a C table, the window's shadows,
  the slide as a state -- still, starting in, sliding in, sliding out -- the
  sprites hidden, the blink, the cursors' animated colour, the palette
  rotation, the frame count, a demo's exit, L and R held to run), `scroll.c`
  (the layers' per-line scroll over a `LayerScroll` a layer, the scroll mode's
  dispatch, the screen shake per axis), `uploads.c` (everything the vblank
  sends: the palette, a queued transfer, a requested block, a message's tiles,
  the sprite table, the window's rows, the rotating queued uploads, a
  combatant's built graphics), `display.c` (the display setup, the window's
  line table, the graphics load, the last vblank handler). Every upload and
  line slot is on the value calls (`vid_send_*`, `vid_set_line_slot`), the
  write orders in `runtime/platform/snes/encodings.c`, so no battle file is on
  the lint's `WRITE_ORDER` any more.
- *The handlers and waits* -- `nmi.c` (the vblank handler's frame count,
  message countdown, pause and frame advance, and the line handler's
  alternating-frame setting, on the game's state; the interrupted code's
  registers, direct-page scratch and math operands saved and put back in the
  entries, through `m_save_interrupted_registers_bank_first` and
  `m_block_move`, two additions to `machine.h`), `wait.c` and `sound.c` (the
  waits by what they wait for, the pause's silence and the music's fade on the
  audio service; `tm_beam_line`, the beam's line, is a new timing call).

**The RAM map.** About two hundred battle variables were added or reshaped,
by the groups as they needed them: the stat block's fields, the timers as
blobs by kind, the scripts' variables, marks, point and per-combatant and
per-effect command cells, the direct-page work cells (`battle_work`, `$80-$9D`,
"each routine's own", written only through named leave helpers), the tech
records (sixty, twenty a member), the setup's cells, the sprite drawers' page.
Stray accesses the original makes past a table -- an actor or a turn that
names no combatant, a partner past the three menu slots, Magus's slot (a word),
the grid's marking with the party off the grid, the effects' done flags that a
word increment carries into -- go through `_unchecked` accessors, each variable
saying why.

**Shadow mode.** Every group was merged shadow-clean over the library (26
movies, none mismatched, no page refused, no accessor out of range), and the
per-entry totals equal `main`'s for every battle entry in calls: no guard
refuses a call the game makes. Matched moves only with interrupted, where work
that was a chain of call-outs is now C with fewer comparison points:
`battle_draw_menu_window` 489 to 7,231 interrupted of 104,644 (it runs the
redraw, the party status and the frames' reset inline), the party status 60
to 437, the combination check 896 to 1,373, the combatants' step 10 to 124,
`battle_build_object_graphics_checked` 0 to 786 (it now runs the build as C); and `battle_frame_work`, `battle_frame_update` and
`battle_setup_display` (431,221, 431,221 and 1,174 calls) now run inside the
frame dispatch's own native run through its C table, so the original entering
them starts a nested comparison that is set aside when the dispatch reaches
its next point: the dispatch compares their work.
Those entries are fuzzed alone. What shadow found, each fixed in the port:
- the main loop: the Y the enemies' script start reads (the enemies placed),
  the carry the step to an enemy's stat block leaves before its statuses are
  followed and the one `slot - 3` leaves before an enemy's turn, the members'
  checks as a subroutine whose return sits under its calls, and the flags the
  rewards' callees don't read (`late-omen-zeal` gives an item);
- the menu window's inline frames reset leaves the carry its compare sets;
- the map's extents were too small where the game reads further
  (`battle_effect_sprite_data` to 48 sets, `battle_bitmap_counts`,
  `battle_object_sprite_entries`);
- the effect graphics loader's callers read the carry and overflow the
  decompressor and the transfer pass back (the endings movies).

**Fuzzing** (`tools/fuzz_routines.toml`, the battle section, on one base
state, `gato-battle` at frame 20,000). `tools/fuzz_compare.py` ran every battle
entry against `main`'s ports with the same seed: in the tight constraints no routine differs, and every
mismatch left is on a loose variant, one of the accepted divergences below;
the predecessor's own 225 of 20,000 on `battle_load_graphics` (it wrote its
inlined `JSR`'s return address into the stack) are gone. The per-entry counts
are in PORTING.md's battle row. What it found,
fixed or refused:
- the actor's status and chosen action read past their tables for an actor
  that names no combatant (the chosen action at four times the actor, a byte):
  now read as the original indexes them; a turn's place in the timers' order
  that names no combatant is refused;
- A's random high byte feeding the original's indexes (the party status, the
  cursors, the pose choice, the builds, the effects' paths): guarded, as every
  game call has it clear;
- a tech list entry past a member's twenty records, a slot's member past the
  party, Frog's slot off a member's: refused; a tech record's partner past the
  three slots, Magus's slot and a target entry past the combatants: read as
  the original indexes them;
- the anchor chase leaves its points in the angle's cells when it is already
  close; a wrapped cursor request leaves a partial offset in `$80`; the
  enemies' step leaves its place at each enemy, where a frame list read over
  it sees it;
- BG2's scroll took BG1's extra offset (20,000 of 20,000 on both fills; the
  movies always have the offset 0): only BG1 takes it; the colour rotation
  from colour 0 stores a bank on, at `$7F:2200`, reproduced through the
  working palette's unchecked accessors.
The accepted divergences, on inputs no battle makes, are each written at their
routine: the drawing pass past the sprite buffer's 128 sprites (the order of
writes shows there; 20 of 5,000 random states, `main` 7), and a graphics set
built over the direct page's working cells or the stack (the loose variants
only).

**The guards refuse only what the game never makes**, and each says so at its
definition: the battle's page and bank; A's high byte where the original
indexes with all of A; a combatant in X, in Y, or in the cell the caller
leaves; the turn's place naming a combatant; a divisor over `$8000` (the
original's bit-serial division loses the remainder's top bit); the window's
menu state (members, slots, cursors, targets, tech lists as the menus build
them); the setup's inputs (party characters 0-6, enemy slots empty, at most
three kinds of enemy in view, parts that fit in the eleven slots); the effect
paths' kinds; the drawing pass's sprite places. None refuses a call of the
library's.

**Don't-care declarations.** Every entry's set was widened to what
`tools/dontcare_check.py --list` offered before its wrapper was written, and
each call-out's to its callee's; the check agrees with every one (0 findings).
Where liveness has a register dead only by trusting a target table the game
fills at run time (the turn's `$C1:B762` and `$C1:AC5E`, which reach the
scripts' handlers), the call passes the overflow as the original has it.

**Timing.** The rewritten ports make far fewer bus accesses than their
predecessors (locals where the original re-read its cells, arithmetic in C
where it drove the multiplier, a loop's work in one place), so the battle's
119 measured `cost_milli` rows were recalibrated from shadow runs over the
library (`tools/timing_gate.py --calibrate`), and only those rows were taken.
A port that runs a sibling's work inline names it (`NATIVE_TIME_AS`): the
frame dispatch its three routines, the drawing pass each part, the movers'
loop each path by kind, the menu window the redraw, the party status and the
frames' reset. Three entries went into `tools/timing_exceptions.txt` with their
numbers, all writing RAM, with the vblank check finding no write moved:
`battle_draw_objects`' vblank-worst (25,095 clocks, a sort of the full list),
`battle_effect_combatant_jump`'s vblank tail (4.61%, at most 2,562 clocks) and
the frame dispatch's drift (+0.83%, the table step's share, at `cost_milli`
1); `battle_draw_object_underlays`' exception left, its successor
`battle_draw_in_front` within the limits. The new timing moved the
`bucket_lavos` route's shell fight onto a frame where the route's bot pressed A
on a member's menu before the menu had put its remembered command under the
cursor, so a list other than the one it wanted opened and the bot, not
expecting it, waited for good; `main` passes because its timing lands
elsewhere. The bot now backs out of a list it didn't ask for
(`tools/movies/bot.py`), and the route passes in the shipping mode.

**What is left** is in TODO.md, "What battle's rewrite left": the original's
routines the battle still calls out to, the meanings not worked out, the
quirks reproduced on purpose, and the accepted divergences.


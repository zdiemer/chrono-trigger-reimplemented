# Comparison gate mechanisms retired in phase A

Historical mechanisms, superseded by REIMPLEMENTATION.md, "The gate after the
comparisons, phase A". The tools and ledgers named below describe the old gate.

## Scaffolding

Some of what a port does is there only because the original does it and
something that isn't the game still looks: a scratch cell the original writes
and nothing reads again, which shadow mode compares; a register or flag the
original leaves and liveness can't show dead, so no don't-care can drop it; a
cell or register the original's still-generated code expects filled. That is
scaffolding. It stands while the gate is byte-exact and comes down when the
subsystem retires the gate (PLAN.md, "After the byte-exact gate").
Until then it is written apart from the logic, marked, and never read back by
it:

```c
uint8_t value = draw_random(g);
ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, value));   /* the original keeps it in a scratch cell */
set_variable_byte(g, v, value);
```

- **One mechanism.** `ORIGINAL_EFFECT(expr)` (`game/original_effects.h`)
  evaluates `expr` in the default build. With `CT_WITHOUT_ORIGINAL_EFFECTS`
  defined it compiles the expression and evaluates nothing: that is the
  native build's shape. `ct_game_native_shape` compiles every port that way
  in every build, CI included, so logic that leans on its scaffolding stops
  building; `-DCT_ORIGINAL_EFFECTS=OFF` builds the whole tree that way, which
  is not expected to pass the gate (shadow mode then sees exactly what was
  left out). Clang warns about an assignment in an unevaluated context, so
  what goes inside is a call.
- **Evidence first.** A write is scaffolding when nothing but the command
  that made it, or shadow mode, reads it: establish that over the whole movie
  library (`ct_dev --event-reads` is event's instrument; history/reimplementation-phase3a.md, "The event
  language") and by reading the listing. A value some other code reads for a
  reason has a meaning: name it in `re/ram.toml` and keep it in the logic. A
  reader that reads it without a reason (an uninitialized variable elsewhere)
  goes in docs/TODO.md: the native build has to decide it.
- **Final values, once.** The logic keeps its working state in locals and
  writes each cell the original leaves once, with the value it ends with.
  Reading a scratch cell back as working state is memory used as a register.
- **Where it is static, it is a table of its own.** What a command leaves
  the same way every time (event's parse position in Y, the operand it keeps)
  goes in a table parallel to the language's, by opcode (`event/leaves.c`),
  which the build without the original's effects leaves out; the engine and
  the entries reproduce it around the operation, which never mentions it. It
  is never a column of the language's rows: those say what a command is.
- **Derived where it can be, beside the wrappers.** A register or flag the
  original leaves is worked out after the operation, from its parameters, the
  state before (noted first) and after: event's by opcode, in the same table
  (`event/leaves.c`, `note` and `derive`), the field's routines' in
  `field/leaves.c`. The logic never holds the record it goes into, nor the
  command's offset. What only the path taken knows (how a move's step
  started) is one marked call on that path, through a function of the
  scaffolding's that reads its own record; `event/leaves.h` lists them and
  says why each can't be derived.
- **A working cell is a local.** A cell the original keeps a value in across
  its own calls and reads back itself (a saved position, a count) is a local
  in the port, written to the cell once, marked, where the original writes it.
- **Registers a callee doesn't read are declared, not set.** A call-out
  through a service takes `PF_CALL_DC(site, target, set)`, which the
  don't-care check verifies like `m_jsr_dc`; what the callee does read, or
  hands back to what reads it, is set in one helper that names the reader.
- **A sibling is called as C.** A port that calls another port of its
  subsystem calls its function (`event_call_bridge` for a machine-shaped one),
  never its ROM address through an emulated `JSR`.
- **Flags from their operands.** A V or carry the original's callers can see
  is derived where it is handed back, from the operands and the result
  (`field_add_overflows16(a, b)`), not computed in the logic as an ALU
  would.
- **Counted.** `tools/portstyle.py` reports each subsystem's and file's
  `ORIGINAL_EFFECT` uses, and the identifiers named after an address, beside
  its other two numbers.
- **Checked never read back.** `tools/effects_check.py` finds, in the source,
  every read by the logic of bytes some port writes only as scaffolding
  (inside `ORIGINAL_EFFECT`, `#if ORIGINAL_EFFECTS` code, or a helper they
  call), and `ct_dev --native on --scaffold-reads` catches the same in a
  running game, in order: a port reading a byte whose latest write was
  scaffolding, which the native build would read stale. The gate runs both,
  the second over the vblank check's shipping-mode runs, as a ratchet on
  `tools/effects_baseline.txt`. A new finding is answered before it merges:
  the write is the game's, so make it plain; or the read takes a value
  something else sets (the original's caller, before every call), and its
  line goes in the baseline with the reason after a `#`. `python3
  tools/effects_check.py --new` lists what the baseline doesn't have
  (REIMPLEMENTATION.md, "The tooling pass after Phase 3's first chunks").

- **Run, in the native shape.** `ct_dev_native_shape` and
  `ct_drive_native_shape` are the ports with their scaffolding compiled out,
  and the gate runs them over the movies and routes in the shipping mode
  (tools/suite.py's `native-shape` check; REIMPLEMENTATION.md, "The fallback
  measure"). What the original's still-generated code reads of scaffolding
  is listed by `ct_dev --native on
  --scaffold-reads original` and `tools/effects_check.py --originals`, with
  the source of each write: a cell there is one the native build needs the
  reader ported or the write made plain for, and it is what a failure of the
  native shape is traced through. A failure the gate lets stand is a line in
  `tools/native_shape_exceptions.txt` with its trace.


### The tooling pass after Phase 3's first chunks

The first chunk of each of the three big banks (battle's `$C1`, field and
events' `$C0`, the menus' `$C2`) found gaps that made every chunk slower or let
a bug past the gate. This pass closed the ones that were tools.

**The native shape, checked.** Two of the three chunks wrote a value the game
reads only inside `ORIGINAL_EFFECT`: the battle's random draws divided by the
long arithmetic's leftover high word, and the menus drew a window's inside to
the size the frame's routine left, both written only as scaffolding. The
native-shape build compiles that out and was never run, and shadow mode runs
the default build, so nothing saw either. Two checks look now, one in the
source and one in a running game, and the gate runs both as a ratchet
(`tools/effects_check.py`, `tools/effects_baseline.txt`):

- *In the source.* For every write of a work RAM cell made inside an
  `ORIGINAL_EFFECT`, or in `#if ORIGINAL_EFFECTS` code, or in a function
  called from either (the `leave_*` and `original_cell*` helpers, to any
  depth), the check finds the reads of the same bytes by code outside all of
  that, in any function the game reaches (a registry entry, a table's row, a
  callback, and what they call). A cell is bytes, not a name: constants are
  followed through the helpers' parameters, enum constants and `#define`s, so
  `set_work_byte(g, LEFT_TURNED, 0)` is `battle_work`'s element 0 and a read
  of element 1 isn't reported; a helper's branches are all taken and an
  element past the variable's end drops out, which is what keeps
  `original_cell8`'s two accessors apart; a read and a write of different
  variables meet only where their bytes overlap within one subsystem (or the
  families sharing a page), since work RAM is reused between modes. Reads
  while a guard peeks (`vars_peek`) and in registry guards are the runtime's
  question, not the logic's, and don't count. It is textual (it borrows
  `tools/dontcare_check.py`'s reader of C) and takes two seconds.
- *In a run.* `ct_dev --native on --scaffold-reads` marks every work RAM byte
  with whether its latest write was made inside an `ORIGINAL_EFFECT`
  (`SnesBus.wram_scaffold`; the macro now counts its depth,
  `original_effect_depth`), and the accessors' reads report a marked byte a
  port reads outside one, by port (`runtime/port/scaffold_reads.h`). A write
  the original's code makes clears the mark. That is the question with its
  order: the native build, at that read, would have found what was there
  before the scaffolding's write. The vblank check's shipping-mode runs take
  the flag (they are the library in `--native on` already), so the gate gets
  it for nothing, and `effects_check.py --runs` reads their logs. The one gap
  is `event/leaves.c`, whose runner is `#if ORIGINAL_EFFECTS` code around the
  operation rather than an `ORIGINAL_EFFECT`: the source check sees it, the
  run doesn't mark it. The other way to run the native shape, a
  `-DCT_ORIGINAL_EFFECTS=OFF` `ct_dev` compared with the default build at the
  level of game outcomes, would find less for more: the shipping mode's timing
  already drifts from the reference, so outcomes part for reasons that have
  nothing to do with scaffolding, and a difference names no port. A mark names
  the reading port, the byte and the port whose scaffolding wrote it.

**Which cells count.** The source check reports a read whether or not some
port also writes the cell plainly, and says which (`[plain]` or `[only]`):
the check has no order, and the battle's case was exactly a cell other ports
wrote plainly, read after the one routine that kept it only as scaffolding. A
rule of "flag only cells nothing writes plainly" would have missed it. Nor can
it see the original's still-generated code, which sets most of the operand
cells the entry wrappers read (`[wrapper]` marks a read in a function that
takes the machine: usually the calling convention, sometimes a real input).
So the source check is broad, 131 findings on main before this pass, and the run check is the
precise one: over the library it found 15 port-and-variable pairs, each a read
the native build would get wrong or would have to show it doesn't. One was the
scene's and is fixed: the race's matrix generator left its column step in the
scene's cells only as scaffolding, and the column step's cell is the product's
third byte, which the next signed multiply adds into its product (once in the
jet bike race's 21,120 signed multiplies); the step is written plainly now,
and the multiply reads that byte through a field of its own
(`scene_call.product_third_byte`) rather than the whole product. Three are the
scene circle's, answered below; the rest are battle's, the field's and
events', and the menus' (docs/TODO.md, by subsystem).

Both are ratchets: `--check` fails on a finding not in the baseline, which
holds main's findings, the source findings of the conversion branch still in
flight (so its merge doesn't break on what it already had) and the library's
run findings. A source finding's line names the read's file and function,
what it reads (with the element, where the source names one) and the variable
written as scaffolding, and says `only` where no port writes the cell plainly,
so a plain write turned into scaffolding is a new line rather than the old
one. That is what makes the ratchet hold the battle's first chunk as it was
before its fixes: with the leftover high word, the stat offset's high byte
and the tech's hits kept only as scaffolding again (a scratch copy of main
with the three writes put back inside `ORIGINAL_EFFECT`), the check reports
the reads of the high word (`battle_wide_math[6]` and `[7]`, in `wide_word`,
which the random draw calls) and of the tech's hits (`battle_low_scratch[14]`)
as new; the stat offset's high byte (`[11]`) is a finding on main already,
another routine writing that byte as scaffolding too, and is in the baseline.
Its first cut missed the high word, which the arithmetic writes through a
setter it passes to a helper as a function pointer, now followed. The re-review's later cases (stats read through
the stat blocks' table wherever it points, which can be the direct page's
working cells) are aliasing through a computed address, which the source can't
see and the library's runs don't reach: a build of main with that fix taken
out finds no new run finding. A new finding is answered
before it merges, by making the write plain (it is the game's) or by showing
where the value the read takes comes from and adding its line with the reason
after a `#`. A line nothing produces any more is reported, not failed, since
the baseline carries the branches' lines until they merge; `--prune` drops
them. The scene's fifteen source findings left are answered in the baseline:
the scene program's operand cells (`scene_call`, one record whose fields are
each call's operands and results, so one call's leftovers sit where the next
reads its operands, which the program sets before each call; no run finds
one), and the circle's center and radius, whose reads take the low byte the
callers write, which the scaffolding writes back unchanged while it clears the
high byte (the runs do see those: the circle's jump draws it again without its
callers writing it). The core's one is the block-move stub's own port, which
reads what its caller wrote into the stub. 130 source findings were left on
main then; with the battle's and the menus' first chunks merged, 252 (counted
by element, as the baseline now keys them), and the
battle's chunk added eight run findings of its own (docs/TODO.md).

**A port that spans frames.** The menus' chunk ported the load screen's
opening (`$C2:E613`, which draws the screen through `$C2:E7C3`), which matched
where shadow mode could compare it (every call interrupted) but broke the
`beyond_time` route in the shipping mode: black screen, then a `BRK`. To
reproduce it without the lost port, `ct_dev --as-port BBAAAA` runs a routine
of the original as one hand port would run in the shipping mode: all at once,
the CPU stepped with time stopped and interrupts held, and its bus and
internal clocks owed afterwards at a `cost_milli` of 1000 (`dev/as_port.h`).
The drawing owes about 1.2 frames. The first vblank's NMI was taken at the
port's boundary, correctly; but the menus' handler (`$C2:840E`) moves the
stack up to its own at `$1DFD`, and `pay_debt` took a machine above the
boundary's S for one that had left the boundary, so it paid the remaining
436,911 clocks inside the handler, before its vblank work and with interrupts
held. The next NMI was then taken inside the first handler, whose stack switch
overwrote the S the outer one had saved, and the outer handler returned
through garbage. A debt now remembers that a handler was taken at its
boundary and waits for it wherever the stack goes, for up to a frame, after
which it is paid where the machine is (a handler that never comes back, such
as a task switch); coming back to the boundary clears it. So ports need no
rule for spanning frames. The same path had been delaying the menus' vblank
work on main by up to 11 lines, 12 times in `menus-field-menu`.
`tests/port/debt_test.c` covers a handler on its own stack, one on the
interrupted stack and one that never returns (with the fix taken out it finds
nine nested handlers and the main loop starved). The load screen's port can
come back unchanged; `$C2:E613` ends in the task yield (`JMP $83B2`), where it
stops with `m_stop_at`.

**Fuzzing.** `tools/fuzz_compare.py` runs an entry the `--before` binary
doesn't have on `--after` alone and reports it `new` with its own counts
(before, every new entry was a "before error"), and without `--before` runs
every routine that way, which is how a chunk's new ports go through the same
constraints file. `ct_fuzz`'s areas take a stride (`AREA:STEP`, a field of
each record in a table), a set of values to pick each element from
(`=V,V,...`, as wide as the widest written, so a script place or a record
offset is a real one) and a tie (`=@BBAAAA`, another cell's bytes, applied
after every other area), and it takes 32 areas (as the menus' branch had it).
A port that waits for the next frame through a menu task's yield (`$C2:821E`)
had every state skipped, the yield never coming back on the stack it was
called on; `--stop-at-call BBAAAA` ends each state's comparison at the port's
call-out to that routine instead, comparing the machines at the callee's entry
with the call's don't-care set (`menu_erase_slot` is the case). Running on
past the yield is still out of reach: that is the other task's frame, and the
fuzzer runs one stack.

**Shadow mode's report.** Mismatches are described apart from notes
(abandoned comparisons, timing), each up to its own limit, and mismatches
first, so a run of `abandoned` lines can't push the mismatch the gate fails on
out of the report; `ct_dev --shadow-details N` raises the limit (16, at most
256), and a line says how many more there were. An entry that runs only
inside another port's stretch (the menus' labels in `menu_open`) is compared
on its own where the original enters it, up to its first point: if that point
is the enclosing routine's too, it is compared there and goes on; otherwise it
is set aside at the enclosing routine's point, counted `abandoned`. What it
did to then is timed, so its `cost_milli` is calibrated from those stretches
(`menu_read_save_slots` has its stretches in `menus-field-menu`); what it
isn't is compared past that point, which would need shadow mode to follow two
comparisons waiting on different points of the original at once, the enclosing
one resumed by its callee's return while the nested one waits for a later
instruction: a checker over every active comparison rather than the innermost.
The enclosing comparison covers the stretch, and the shipping mode never calls
the nested entry there, so that is left (docs/TODO.md).

### The fallback measure

The goal is a program with no emulator in it, and four things keep the
original's code running beside the ports: a guard that refuses so the original
runs, a port that stops and hands the rest back, a call-out to code that is
still the original's, and code no port covers. Until this pass nothing measured
how much real play still ran on them, and the native-shape build had only ever
been compiled. Three gate checks look now (tools/suite.py's `fallbacks`,
`refusals` and `native-shape`).

**The measure.** `ct_dev --fallbacks` and `ct_drive --fallbacks`
(`dev/fallbacks.c`) count every instruction the original's code executes in
place of hand ports, interpreted (the CPU's instruction hook) or generated
(`generated_insn_hook`, a test per instruction in `RC_INSN` when it is unset),
by address, by the routine it belongs to, and by why it ran. The runtime
tells the tool when it hands off (`NativeObserver`, `runtime/port/native.h`):
every hand entry at an address refused (with which condition: the P
precondition, the CPU's mode, or the guard), a port entered, called out,
stopped, returned from an interrupt, or was skipped at the nesting limit. The
tool follows the stack from there: a refused call lasts until S rises above
where it was entered, a call-out until its callee returns, a stop until the
port's routine returns; an interrupt opens an extent of its own that an RTI
ends wherever the stack is; and a hand port entered at or above an extent's
level ends it (the original went on to a port without returning: a main loop's
next pass). Each instruction is then `refused`, `handback`, `skipped` or
`unported`, filed under the target of the latest call on record (the port's
entry for what it hands back after a stop).

**Measured where it holds still.** The shipping mode can't carry a ratchet: a
port's estimated time moves every movie's presses to other frames, so a movie
goes somewhere else with every timing change. Running the library with the
native-shape build, whose only difference is timing (the scaffolding's bus
accesses), moved 99 of 1068 (kind, routine, port) keys and doubled the wait
loops' counts. So the ratchet is taken over the **reference play**: the shadow
runs the gate makes anyway, where the original runs everything at its own
timing. There the runtime's same calls mark what a port stands for -- from the
original's arrival at a port's entry until it reaches the port's call-out,
stop or return, the original is running the port's work and isn't counted --
and an idle entry's pass of its wait loop, counted as the original runs it, is
taken back when the loop comes round to its head. The pass that leaves the
wait is taken back too where it ran nothing but the loop's own instructions
and a hand port's entry takes over at the exit, or where it returned having
run only the bytes its entry declares, the loop's and its return's
(`NativeEntry.idle_return_span`: the warp's frame wait, and the scene
scripts' wait for the music, which returns every frame until the music gets
there); any other leaving pass stays counted, since what follows a loop's
exit, or a helper it calls, can be the original's. A pass an interrupt cuts is kept aside while the handler runs
and goes on where the handler returns to the S it was cut at, so it is taken
back too. A comparison's stretch goes on across a switch of stacks (S rising
further in a step than an instruction pulls, as the menus' task switch moves
it) until the runtime says the comparison is over, as an interrupt entry's
does. A comparison the runtime gives up because an interrupt's handler moved
to a stack of its own above the routine's (the menus' vblank handler: shadow
mode can't compare a routine whose stack the handler left) keeps its stretch,
uncompared, where the port's run had ended by then: the handler comes back
to the routine, and what the original runs of it from there is still the
port's, until the routine returns, the original reaches where the port
stopped (from there it is handed back, as at any stop), the handler comes
back to another stack, or the routine's task is switched away from
(`NativeObserver.given_up` says where the port's run ended). A port's
call-out that moves onto a stack of its own and comes back
(`NATIVE_STACK_SWITCH_DISTANCE`, the pixels' spread) stays the call-out's
until it returns, as the runtime waits for it; and where the runtime sets the
comparisons nested in a port's stretch aside at a call the port makes, the
port's stretch goes on from the caller's level, so the callee, itself a port
(the scene program's unpacker), doesn't end it. That count changes only when a port does. It costs the shadow runs
nothing measurable (gato-battle 47 s with it, 49 s without). The shipping
mode's count is still made, over every movie (`--native on --generated on`,
43 s for the library) and the routes (`route_check
--fallbacks`, `$CT_DRIVE_FALLBACKS` through `tools/movies/ctdrv.py`), and
ratcheted on its own, loosely: `tools/fallback_shipping_baseline.txt`, judged
as a subset since its counts wander with timing, fails on a routine it doesn't
have or on more distinct bytes or instructions past the slack in one. It is
the backstop for the reference play's: whatever that measure counts as a
port's that the ports don't run shows here. A route's count is over the frames it kept: as the run
ends, the input it kept is played again from its start by a `ct_drive` that
counts (`Machine.count_kept_frames`), so a search's probes, the attempts a
restore undid and the frames a load replays from a base are left out, and a
run resumed from a checkpoint is counted from its start. The replay is
checked against the run: it must end on the run's frame with the run's work
RAM, or the route fails rather than count another run, and input with frames
no buttons were kept for (a probe no restore undid, a state loaded with no
`.pads` beside it) is an error, not frames of no buttons. The shadow runs use the original sound driver, so the CPU
half's waits for its answers count there and not in the shipping mode.

**The ratchet.** `tools/fallbacks.py --check` against
`tools/fallback_baseline.txt`, a line per routine with its instructions,
distinct bytes and reason. A routine the baseline doesn't have, or more
distinct bytes in one (a new address run in place of a port), fails; so do
more instructions past 5% and 1000. Kinds are summed per routine so that a
port landing doesn't move its callees (what was `unported` becomes `handback`,
same routine); the report keeps them apart. `--update` takes a gain and keeps
the reasons of the lines it keeps.

**A movie ending inside a comparison.** The suite uses
`ct_dev --finish-fallbacks 120`: counts, traces and the saved end state freeze
at the movie's last frame, then at most 120 input-free frames let existing
comparisons finish. A `closed` record cancels only its own file's `open`
instructions, after the exact entry's match count increases by the number
of pending invocations, with no new calls, mismatches, interruptions or
abandonments. Completion is observed between instructions so another call
later in the same frame cannot obscure it. Unresolved comparisons retain
their original counts and ratchet failures; the tail's mismatches still fail
the shadow run. Comparison statistics and instruction diagnostics include
the tail; saved-game, trace and fallback counts retain the movie's cutoff.

The tail also finishes an idle pass cut short by the last frame. Each pass
has an identity preserved while an interrupt holds it aside. Only that
pass reaching its head again, or completing through its declared idle
return span, can certify its counted prefix with `idle-closed` records.
The reader subtracts only matching instruction addresses, lengths, kinds
and owners in the same movie's file; it rejects excess or cross-movie
credits. An abandoned pass, an exit outside the span, or a later pass at
the same address and stack does not certify the prefix. The raw cutoff
counts stay in the file, and the tail contributes no new instructions.

`battle-black-tyrano` ends during scene teardown and field loading: 20
instructions under `scene_program_restart` and 44,292 under `decompress`
were previously reported as new or increased open fallbacks. Both comparisons
match within 11 extra frames. The cutoff saved state is byte-identical to
the reference at frame 49219; the movie still measures 18 fallback
instructions (10 handback, eight unported), with none new or increased.
No fallback baseline was raised to accept the loading call's partial count.
A replay from a reference state before the cutoff checks zero-, one- and
120-frame limits: all three retain identical cutoff files and saved states
before the appended completion records. One extra frame completes only
decompression, so restart's 20 instructions still fail the ratchet. Eleven
complete both. Reader tests also reject closure exceeding its own run's
open count or attempting to cancel another movie's comparison.

**On main**, when this came in, the reference play ran 46.5 KiB of distinct
code and 347 million instructions in place of hand ports, the same in two runs
to the instruction; by kind (a byte can be in two):

| | Reference play (28 movies, shadow) | Shipping mode (28 movies and 3 routes) |
|---|--:|--:|
| refused | 2.4 KiB, 10.3 M | 2.6 KiB, 40.6 M |
| handback | 33.8 KiB, 296.0 M | 36.5 KiB, 1,789 M |
| skipped | 0.0 KiB, 0.3 M | 0 |
| unported | 13.8 KiB, 40.8 M | 11.8 KiB, 134 M |
| all | 46.5 KiB, 347 M | 45.0 KiB, 1,964 M |

By subsystem (the reference play, KiB of code and millions of instructions):
field engine and events 9.8 KiB, 121.8 M; scene program 8.1 KiB, 68.1 M;
battle 8.8 KiB, 36.6 M, its frame work 3.2 KiB, 49.5 M, sprites 0.1 KiB,
4.4 M, NMI 3.3 M; menus, text and overworld 13.8 KiB, 21.8 M; field graphics
3.7 KiB, 31.6 M; scene engine 1.0 KiB, 6.1 M; the sound driver's CPU half
0.9 KiB, 3.8 M. Almost all of it is `handback`: the ports are dispatchers
around code that is still the original's. The largest are the
field frame's call-outs to the beam-coupled waits (`$C0:59D9`, `$C0:5A46`,
`$C0:CA76`; PLATFORM.md, "Timing without a beam"), the scene program's frame
tasks, and the battle frame work's call-outs.

**Refusals as failures** (`tools/fallbacks.py --refusals`,
`tools/refusal_allowlist.txt`). A native build has no original to hand a
refused call to ("After the byte-exact gate", PLAN.md), so a refusal in
real play -- the shadow runs, the movies in the shipping mode, the routes --
fails unless its entry and condition have an allowlist line with a reason.
Each is printed with its count and its first four states (registers, the run,
and for code in work RAM whether its bytes are the image the generated blocks
were made from, `misses_describe`, which is how the code-presence guards that
turn away another program's code are told from gaps). On main, 23 entries
refuse in real play, 7,609 times: four are another program's code at a RAM
address a port was written for (the attract program's), and 19 are gaps,
listed in docs/TODO.md, "Refusals in real play".

Closing one is finding which of the guard's predicates the real state fails
(a print at the guard over the movie that refuses), reading the original on
that state, and making the port take it -- never widening the guard past
what the port does. The last of the library's gaps went that way:
- the equipment list's guard read eight rows even past the list's end,
  where the inventory screen had left `$FF`s; the drawing stops at the end
  and repeats its blank row, so the guard reads no further, and the list's
  making (`$C2:A463`, `$C2:A45D`) asks what the made list will hold rather
  than what the last one left (`battle-unequipped-armor`, 36 calls);
- Robo staying behind at Fiona's house steps as the party's third while the
  place already names who took it: the guard asked the place to name the
  one stepping. The original moves the one in the place, and, waiting, shows
  its pose on that one while the member turns: the port now does the same
  (`field/party_trail.c`), and leaves Y on whichever it posed (120 calls);
- Flea's scripts turn with facing `$1F`, past the facings' table, which runs
  on into the places': the code finds where the subject it numbers stands
  and keeps the facing found last (`script_places.c`, `no_way_found`; four
  calls);
- 600 A.D. Guardia Forest holds all 64 objects, and the battle's search
  starts at the count, one past the tables: the port starts at the last
  object, and the guard leaves to the original only a slot past the tables
  whose bytes (object 0's in the next tables) read as an enemy on the
  screen (`battle_start.c`, `stray_slot_is_an_enemy`).
Each was fuzzed with a variant made for the state (`tools/fuzz_routines.toml`:
`another`, `ended`, `past the facings`, `full map`), 0 mismatches but
`field_battle_begins`' one in about 4,000, which the old constraints show
too (TODO.md).

**The native shape, run.** `ct_dev_native_shape` and `ct_drive_native_shape`
link `ct_game_native_shape`, the ports with `ORIGINAL_EFFECT` compiled out
(now a static library rather than objects only). Nothing can compare them byte
for byte -- the scaffolding is what they leave out, and their timing differs
from the default build's by its accesses -- so the check is the outcomes: every
movie in the shipping mode exits 0 within its time (a hang), with no `halted:`
line (`ct_dev` prints one where the machine ended stopped), `accessor out of
range` or `page refused` line that the default build's run of the same movie
doesn't show; and the routes reach their outcomes (the fast set; all three
with `--long-routes`). A failure is traced through what the original's code
reads of scaffolding: `ct_dev --scaffold-reads original` (the default build's
shipping runs take it) notes each read the original's code makes of a byte
whose latest write was a port's `ORIGINAL_EFFECT`, by the instruction, and
`tools/effects_check.py --originals` gives each the source of the write; the
check writes the list beside its logs. On main, 101 such (byte, instruction)
pairs over the library.

When the check came in, all 28 movies ran to their end in the native shape,
and the fast routes passed, but all three whole routes failed, each traced and
confirmed by making the writes plain in a scratch build and running the route
again. The targeting's port fixed all three (docs/history/reimplementation-chunks.md,
"Phase 3, battle: the targeting"), and `tools/native_shape_exceptions.txt` is
empty:

- **Battle items land on the wrong side** (`beyond_time`, `bucket_lavos`):
  `battle_carry_out_member_action` wrote the acting member's record
  (`$0006`, game/battle/member_turn.c:499) and the item targeting's cell
  (`$000C`, :420) only as scaffolding, and the original's targeting, which
  the port called out to, read them (`$C1:C24A`, `$C1:C57F`). A revive healed an
  enemy instead; the final fights ended in wipes. With both writes plain, both
  routes reach their endings.
- **A walk stops short** (`telepod_lavos`): `object_run_script_from` cleared an
  object's step count only as scaffolding (game/event/calls.c:173), and the
  original's event scheduler reads it (`$C0:59F0`). The walk to the reaper
  statue in the developers' room ended a tile and a half away, and the seventh
  round was missed. With that and the battle's two plain, the route reaches its
  ending.

The movies don't see either: their presses drift in the shipping mode
anyway, and nothing in them checks an outcome.


### Declarations liveness can't prove

`tools/dontcare_check.py` checks a call-out's don't-care set twice: against
liveness as the analysis follows the game, and against liveness that trusts no
target set the machine decides at run time. A callee that reaches a transfer
whose targets are read from work RAM (a jump table or a pointer there, a call
whose operand the program writes) or come from data (a `derive` hint) has its
targets followed as they were argued for the game's own data, and a port that
stores the target itself, as the scene interpreter stores a script's routine
in `$00F0` before calling `$7E:37F3`, can send it anywhere; so can the fuzzer.
The second solve keeps everything live at such a transfer. A set only the
first solve supports is a finding: pass what may be read (usually the port
already has the original's value there, as the scene's calls did), or, where
the declaration is right and only the analysis can't show it, fuzz the
call-out with its declared registers scrambled and note the run beside it.

`ct_fuzz --poison-dc` scrambles, at each call-out the port makes with a
don't-care set, the registers and flags the set names, each to a value other
than the port's, so a callee that reads one runs on something other than the
original's value and the state mismatches (`--poison-site BBAAAA` does it at
the call-outs from one site only, to tell which declaration a mismatch is in).
Without it a port that happens to pass the original's value hides a wrong
declaration: the scene's call to `$7E:37F3` declared Y and V dead, passed the
original's, and matched on every state until they were scrambled (2 of 1390
states then read them, through routines random scripts named). The callee runs
twice from the same machine, first with the port's values on a copy, and what
it left alone is the port's again afterwards, as shadow mode keeps it; the run
reports how many call-outs it scrambled in the states it compared.

The note goes in the comment block just above the statement that makes the
declaration, or on its lines, in exactly this form:

```c
/* dc-evidence: ct_fuzz --poison-dc ROUTINE[:VARIANT], seed S: N states,
   M call-outs scrambled, 0 mismatched */
m_jsl_dc(s, site, target, set);
```

with the run's routine, seed and counts in place of the capitals. `:VARIANT`
names a variant of `tools/fuzz_routines.toml`, run with that
entry's arguments, and `--poison-site BBAAAA` may stand for `--poison-dc`. The
check reads the note: the routine must be listed there and be a registered
entry that reaches the declaration; the run must have compared states,
scrambled at least 100 call-outs (a run whose states mostly stop before the
call, a callee past the budget, says little: narrow the states until they
reach it) and mismatched none. A note on a declaration liveness proves without
one is a finding too. `grep -rn "dc-evidence:" game/` lists every declaration
that rests on fuzzing rather than on the analysis.

Fuzzing can't settle a declaration whose callee runs for frames: a call-out to
a routine that waits on the next frame, again and again, runs past any budget
from every state. Where the one register such a call leaves unproven is the
overflow flag, the evidence is instead the flag followed as far as the
dispatch can reach, and the whole library run with the flag inverted at each
site of the declaration:

```c
/* dc-evidence: tools/v_readers.py BB:AAAA: V dead, its tables' indexes at
   most $FE (what that rests on); ct_dev --poison-set V at each site over the
   library, calls with V inverted: BBAAAA N, BBAAAA N; 0 differing[; a site
   under 100 rests on the static result] */
```

`tools/v_readers.py` solves liveness as strictly as the check does, but
follows each jump table the strict solve gives up on (its targets derived from
data) that carries an `index_reach` hint in `re/hints.toml` to every word its
dispatch can index, whatever the data picks. The hint is argued from the
dispatch, and the tool checks each part against the analysis (and
`tests/test_v_readers.py` against the ROM): `ASL A` with an 8-bit A, `TAX`,
`JSR` or `JMP (abs,X)`, nothing jumping past the `ASL`, 128 words for a doubled
byte, every word code the analysis traced, and A's high byte, which `TAX`
carries into X, known clear at the `ASL` (`tools/disasm/high_byte.py` follows
it through every path and call the analysis traced: a `TDC` clears it on the
battle's direct page `$0000`, which the note's parenthesis says it rests on,
and a 16-bit load or transfer, an `XBA` or a callee that returns with it
unknown make it unknown). A hint that fails any of these is a finding, and the
tool lists why. `ct_dev --index-check BBAAAA` (on the interpreter alone,
`--native off --generated off`) also records X at each dispatch over the library,
which must never pass `$FE`. The tool lists what reads V in the code the
callee reaches: a `BVS` or `BVC`, or a `PHP` whose byte is taken back other
than by `PLP` (liveness follows where it goes). `ct_dev --poison-site BBAAAA`
(with `--native on`) scrambles, at each call-out made from that site, what its
don't-care set names, as `ct_fuzz --poison-dc` does, and `--poison-set V`
narrows that to the flag; it counts, per site, the calls where V was inverted.
The run is every movie in the shipping mode with and without it, each trace,
the picture's hash included, the same outside the stack's page
(`$7E:0600-06FF`: a register pushed and pulled again lies there until
something overwrites it, which is all a scrambled one leaves where nothing
reads it).

The check runs `tools/v_readers.py`'s solve itself: the note must name the
call's target and have V dead into it, and every bounded dispatch must pass
the checks above; the call's own site must be among the sites the note counts,
V in the set and no trace differing; and the call's own site must have had V
inverted at least 100 times. A declaration made at several sites (a helper's
parameter) counts them all, and one the library reaches fewer than 100 times
rests on the static result alone: the note says so ("a site under 100 rests on
the static result"), and then some other site of the note must have made 100.
It speaks for V alone: any other register the strict solve leaves unproven
still needs a fuzz note. `python3 tools/dc_evidence.py --ct-dev
build/main/ct_dev` (or the suite with `--evidence`) makes both runs and the
index check again, over every site the notes name and every bounded dispatch,
prints each site's cited count beside the run's, and exits 1 on a differing
trace, an index past `$FE`, a bounded dispatch the library never ran
(unexercised), or a site under 100 its note doesn't rest on the static result.
**Whoever merges a change that touches `game/battle/`, `runtime/port/`, an
`index_reach` hint or a dc-evidence note runs `tools/suite.py --evidence`
first**: the notes' counts are the library's, and a change there can move them.

Those runs are most of the `--evidence` gate's time (four runs of each of the
library's 84 movies, the index check's on the interpreter), so a movie whose
runs all passed keeps its counts in one store every worktree of the checkout
shares, `build/evidence-cache/dc/<key>.json` in the main checkout
(`tools/evidence_cache.py`), and the next run anywhere takes them from there
when nothing they depend on has changed. The key is the SHA-256 of:
`ct_dev`'s bytes, the ROM's, the movie's text with CRLF made LF (a save the
movie starts from is in it, by the CRC `ct_dev` checks), its frame count, each
run's options (the sites, entries and dispatches it poisons or checks), and the
syntax trees of `tools/dc_evidence.py` and `tools/trace.py`, which judge the
runs, with their docstrings and comments left out. Nothing in it names a path,
so a store shared between worktrees is as sound as one per build directory,
and it is what lets a worktree's first gate, or main's after merging a branch
whose binary has already passed, take the results instead of making all 84
movies again (about 48 minutes at `-j 1`). The binary stands for the C of
`game/`, `runtime/` and `dev/`, the generated C and its generator, the build's
options and the compiler at once: `ct_dev` reads no environment variable and
no file its command line doesn't name, and a MinGW relink of the same objects
differs from the last only in the executable's link time and checksum, which
are zeroed before hashing, so a merge that touches only tools or docs keeps
its binary and pays a few seconds for the stage, while one that rebuilds
`ct_dev` differently makes everything again. The key is the whole binary, not
the entries a declaration names, because a movie's runs poison every site and
entry at once and compare its whole trace in the shipping mode: the result
depends on every routine the movie plays, the time model in `game/registry.c`
included, and nothing records which C a run executed, so a narrower key could
only guess (docs/TODO.md, "Narrow the evidence cache's key"). A result with a failed run, a
differing trace or an index past `$FE` is never stored, so it is made again
every time; the counts are summed over the library and judged against the
notes afresh on every run, cached or not, so a note's edited count is still
checked. `tools/uncompared_evidence.py`'s isolated comparisons keep their
verdicts the same way (`build/evidence-cache/uncompared/` in the same shared
store, keyed on the binary, ROM, movie, frames, the case's options and the
tool's syntax tree). `--no-cache` on either
tool, `--no-evidence-cache` on the suite or `CT_EVIDENCE_CACHE=off` makes every
run again (and still stores what passes). When the main checkout is read-only,
`CT_EVIDENCE_CACHE_DIR` selects a writable store; the tools use their `dc/`
and `uncompared/` subdirectories there, with the same keys and checks.
The unreached ledger's fuzz trials
(`tools/unreached_fuzz.py`) aren't cached: they take half a minute, and their
game states already are, keyed on the binary that made them.
The effect worked out for an enemy's action hands the action's showing
(`$C1:BFA4`) and the battle's step (`$C1:0003`) an overflow flag they only
save and restore across their frame waits (history/reimplementation-chunks.md, "Phase 3, battle chunk 2: an effect worked out"), which is
the case it was made for.

The third form is an entry's own carry after it returns, where liveness
follows it out through the original's callers to a reader whose result
nothing then acts on. Fuzzing compares only up to the entry's return, so it
can't show that; the library can. The note sits in the comment block just
above the entry in `game/registry.c`, beside the reader named:

```c
/* ...the reader, and why nothing acts on what it reads...
 * dc-evidence: ct_dev --poison-return BBAAAA --poison-set C over the library:
 * N returns (B to a bridge), C inverted M; 0 differing */
```

`ct_dev --poison-return BBAAAA` (with `--native on`) scrambles, wherever the
port registered there returns to the original's code in the shipping mode, or
to a battle bridge that ran it as C in place of a call and goes on with its
registers (a sibling, `battle/bridge.c`), the registers its entry declares
don't-care (only those `--poison-set` names), and prints how many returns it
made, how many of them were to a bridge, and how many had the carry
inverted. A return to a bridge is counted apart because the bridge may set
the registers again at once (the battle's step dispatch does, `steps.c`), so
it tests less than a return to the original's code; the note says how many of
its returns were such. The check takes the note where
liveness has the carry alone read after the entry returns, the entry named,
C in the set, no trace differing and at least 100 returns inverted;
`tools/dc_evidence.py` makes the run again beside the other two, and compares
its trace with the unscrambled one's. The menu's frame (`$C1:1153`) was the
case: its carry runs out through the frame step into a battle message's frame
waits, and the first multiply of the party's statuses cleared after the
message takes it into a product's high word the next pass clears
(history/reimplementation-chunks.md, "Phase 3, battle: the player choosing").
Since the menu's turn is C, the frame's returns are all to bridges (its note
says so), and the
frame waits that the turn's carry leaves through (`$CD:3E75`, `$CD:3E3B`)
carry notes of their own (history/reimplementation-chunks.md "Phase 4a, the shipping
mode's fallbacks, chunk 5").


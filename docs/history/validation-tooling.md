# Validation tooling: development accounts

These are historical accounts moved from REIMPLEMENTATION.md. Counts, registry
entries and pending work describe their original revisions; current mechanisms
and retirement status remain in [REIMPLEMENTATION.md](../REIMPLEMENTATION.md).

### Before the bulk: liveness, the don't-care check, the draft's hand-backs

The pilot left three things to do once rather than in every subsystem (PLAN.md,
Phase 3): liveness that sees through a saved P, a check that every don't-care
set a port declares is one the original agrees with, and the draft tool's
remaining gaps. They were done in that order, since each needed the one before.

**Liveness through PHP and PLP.** `tools/disasm/liveness.py` counted `PHP` as
reading every flag, and Chrono Trigger's subroutines save P at entry and
restore it before returning, so nearly every routine "read" N, V, Z and C, and
a draft carried all four through every routine that called one. Saving P
reads nothing by itself: a flag is live at a `PHP` exactly when it is live
after the `PLP` that pulls the byte. So for each `PHP` a walk forward counts
the bytes pushed and pulled above its byte; where every path reaches a `PLP`
that pulls exactly it, the `PHP` reads what is live after those `PLP`s, and
otherwise it stays a read of every flag:

- **S changes**: `TCS`, `TXS`, `XCE`, a push whose width the analysis doesn't
  know, or paths that meet with different depths. `LDX #const : TXS` (the
  mode loaders' new stack) and `CLC : XCE` are understood: a byte under a new
  stack is never read again.
- **Read otherwise**: the byte is pulled by something else or with other
  bytes, a return pulls it, a stack-relative access reaches it, or `TSC`/`TSX`
  copies S while it is on the stack.
- **A callee reaches it**: every routine has a `reach`, how far above its entry
  S it (or what it calls) pulls or reads the stack; 2609 of 3136 routines reach
  exactly a JSR's return address, 262 a JSL's, 228 can't be bounded (tasks that
  switch stacks, code in RAM). A call made while the byte is on the stack, to a
  routine that reaches that far, may read it.
- **Control leaves** with the byte on the stack: a return, a tail jump, an
  indirect jump, a flow out of the traced code. A call that doesn't return
  just ends the path.

Of the 669 `PHP`s the analysis traces (counted once per context; 399
addresses): 566 paired (84.6%, 321 addresses), none where S changes, 13 read
otherwise (5 addresses: the menus' task switch at `$C2:821E` copies S, and
`$C7:0A39`'s refused path pulls its P byte with a `PLA`), 87 where a callee
reaches the byte (71: calls into the menus' task switch, and the sound driver's
`$C7:0A39`, whose refused path drops its caller's frame and leaves through the
driver's common exit), 3 where control leaves (2). Routines whose entry reads
all four flags went from 424 of 3136 to 164; flag bits read at entries, summed,
from 4336 to 3671; flag bits live after returns from 5400 to 5141. The
generated blocks don't use liveness (M4's dead flags aren't built), so `python3
-m tools.generate` writes byte-identical C before and after (all 65 files), and
the gate passed unchanged (`suite OK` with `--long-routes`; trace identity,
`sst_generated_runner` and `generated_bus_diff` 0 failures). What changes is the
drafts, checked by `draftcheck` (below). The draft of a routine that starts
with `PHP` now pushes the machine's P as it is (a `PHP` reached only along a
straight line from the entry through instructions that leave P alone, where the
machine's P is still the original's), so the flags don't enter the draft at
all.

**The don't-care check** (`tools/dontcare_check.py`). Shadow mode doesn't
compare what a port declares dead, so a wrong declaration hides a difference
and ships a wrong register. Each declaration is a claim about the original at
the point `runtime/port/native.c` applies it, and the check verifies it against
liveness there:

| Declaration | Where shadow mode leaves it out | What liveness must say |
|---|---|---|
| a registry entry's `.dont_care` | where the port finishes: at its return, and at every stop | dead after the routine returns (over every routine the entry is part of: `$C1:1C55` is also the fall-through end of `$C1:1C4A`), and dead at each stop the port declares |
| `m_stop_at(s, at, set)`, `m_stop_dont_care(s, at, set)` | at the stop, with the entry's set | `set` and the entry's dead at `at`; or, when `at` is a return instruction the port carries out last, dead after that return |
| `m_jsr_dc`, `m_jsl_dc`, `au_driver_call` | at the callee's entry | not read by the callee before it returns (`Liveness.callee_reads`) |

That needed three runtime changes, so the claims mean what the check verifies:

- **Stops say where.** `m_stop_dont_care` takes the stop's address, and
  `m_stop_at(s, at, set)` sets PC and declares in one call. Shadow mode (and
  `ct_fuzz`) report a port that stops somewhere other than the address it
  declared, and one whose entry has a don't-care set that finishes without
  either returning (`m_rts`, `m_rtl`, `m_rti` now tell the runtime) or
  declaring its stop: the entry's set applies there, so the check has to know
  the address. 24 entries stop somewhere; each stop is now an `m_stop_at`.
- **A call-out's set means what the callee reads, and no more.** When a
  callee returns, shadow mode reloaded the port's machine from the original's,
  so a register the port declared don't-care at the call and the callee
  carried through came back as the original's, while in the shipping mode it
  comes back as the port's. Now a declared register the callee left as it
  found it is the port's own again after the reload (`keep_untouched`): if the
  port relies on it later, shadow mode sees what the shipping mode would. The
  claim at the call is then only that the callee doesn't read the set, which
  is `machine.h`'s documented meaning.
- **Liveness splits A.** `DC_AH` claims A's high byte alone, so liveness has
  `A` (the low byte) and `AH` apart: an 8-bit accumulator reads and writes the
  low byte, `XBA` swaps the two bytes' liveness, `TAX` with 8-bit index
  registers reads the low byte. `DC_A` is dead only when both are.

The declarations are read from the source, not run: registry entries through
the file's macros, the rest with their arguments evaluated from constants,
the function's parameters (every call of it in the same file is followed, so
the sound driver's `stop_at(s, loop, dead)` and `call_driver(...)` helpers
resolve per call), locals, the rows of a table in the file (the wait table's
address and dead set stay paired), and both branches of a condition decided
at run time. A call-out whose target is computed (a jump table's) takes its
targets from the original's call at the site. A declaration the check can't
read is itself a finding. It is a row of `tools/suite.py`'s `CHECKS`
(`--no-dontcare`), fed the call logs the shadow runs just wrote, and takes
about 20 seconds; `--tree` checks another tree, such as a `draftcheck` one.
Call-outs are also held to a second solve that trusts no target set the machine
picks at run time, which doubles the solve ("Call-outs into targets the machine
picks", below).

**What it found on the ports**, and what was done. Every finding was either
liveness being too conservative, fixed where that was sound, or a declaration
wider than the original allows, narrowed:

| Finding | Kind | Resolution |
|---|---|---|
| the sound driver's boot stops (`$C7:00AB`, `$C7:00DB`) declare `DC_AH`; liveness had A's high byte live | liveness | the value there is overwritten on every path after the `XBA`s that move it; with `XBA` swapping the bytes' liveness, it is dead |
| `$C7:0922`'s entry and the stop at `$C7:08FA` declare X and Y; liveness had them read | liveness | `$C7:09DA` carries X and Y back to callers that read them, and a call counted every caller's readers. Liveness is now solved as a function of what callers read (a summary per routine, the value at each instruction packed per register the caller may read), so a call carries only its own caller's through |
| the call-outs through `au_driver_call` declare X and Y (loop counters the port keeps in C) and `$C7:01AE`'s call to `$C7:0A39` the flags | semantics | the callee reads none of them; what the caller does after the call is the port's, now compared as the shipping mode would run it (above). `$C7:0A39`'s `PHP` byte is pulled by a `PLA` whose A is dead: pairing is now for every push, and a pull into another register reads what was pushed only where that register (or N and Z) is live |
| `menu_nmi_work`'s `DC_C` checked at its return | declaration | it never returns: it stops at `$C2:8466`, or after a call-out that changed the widths; the three stops are declared, and the carry is dead at each |
| `battle_build_object_graphics` (`$C1:1C55`) declares X and C | declaration | live after `$C1:1C4A`'s callers resume, which it returns to when `$C1:1C4A` falls into it; narrowed to N and Z |
| `input_read_joypad` declares Y and V | declaration | live at its stops (the joypad wait at `$C2:84F9`, the reset at `$C0:FF00`); narrowed. V is `$4212`'s hblank bit from a `BIT` that native time can't reproduce, and dead after the return: that is now declared at the `RTS` (`$C2:8544`) alone |
| `event_run_object_script` X and C, `field_object_in_the_way` X, `menu_run_objects` Y | declaration | live at the stops each makes after a call-out changes the widths (`$C0:5AB4`, `$C0:5BE6`, `$C2:0541`); narrowed |

Over the 26 movies in shadow mode the narrowed ports still match everywhere:
the early ports reproduce every register they declared, so none of the wrong
declarations was hiding a difference on the paths the movies reach. The one
exception proves the mechanism: with V taken out of `input_read_joypad`'s
set, 416 calls mismatched at the return on V, the hblank bit; declared at the
`RTS`, none do. The tree as this work left it: 0 findings over 284 entries with
a don't-care set, 56 stop and 45 call-out declarations (a value per helper call
and table row). The counts have grown with every wave since; the check still
reports 0.

The liveness those fixes left, from the same analysis (3136 routines):

| | Before | PHP pairing | Now |
|---|--:|--:|--:|
| routines whose entry reads all four flags | 424 | 164 | 123 |
| flag bits read at entries, summed | 4336 | 3671 | 2409 |
| flag bits live after returns, summed | 5400 | 5141 | 3968 |
| routines X is live after | 1635 | 1635 | 1032 |
| routines A's low byte is live after (A whole before) | 2553 | 2553 | 511 |
| routines with something dead after return | 2663 | 2743 | 2820 |

Pushes of every register pair the same way now (1980 in contexts): 1784
paired (1140 addresses), 3 where S changes, 60 read otherwise, 120 where a
callee reaches the byte, 13 where control leaves. The solve takes 15 s.

**The draft tool's fixes**, in the pilot's order, each on the machinery above:

- **Liveness at hand-backs.** A draft stops with `m_stop_at(s, at, set)`,
  `set` being what liveness has dead at `at`, and writes back only what is live
  there (and A's low byte wherever its high byte is, since no set leaves out
  the low byte alone). Its registry line declares what is dead everywhere it
  finishes, after the return and at every stop, and a return declares the
  rest at the return instruction, so a draft suggests declarations the check
  accepts: `tools/dontcare_check.py --tree` on the `draftcheck` tree finds
  nothing over 335 entries, 113 stops and 516 call-outs.
- **Call-out stubs set what the callee reads** (`callee_reads`) and name the
  rest don't-care, rather than everything any caller reads after the callee;
  and a `PLP` that pulls the byte the routine's first `PHP` pushed restores the
  caller's P, so it no longer makes the call "change" every register. Fewer
  inputs and read-backs mean fewer stub signatures (`$C7:0004`: 16 to 11).
- **D kept across call-outs that leave it alone.** A callee's summary of D and
  DB now takes only the returns that come back to its caller (at the depth it
  was entered with): `$C7:0A39`'s refused path, which pulls its caller's frame
  and returns further up, no longer makes D unknown after the call, and the
  rest of `$C7:0004`'s draft names its direct page (135 `m_dp` accesses to
  none).
- **The ground truth reads a port the way the check does.** Its file-local
  names (`COUNTER` for `RAM_ABS(RAM_SOUND_DRIVER_UPLOAD_COUNTER)`) resolve
  through the file's macros, a helper's parameters count as addresses only
  where the helper reads or writes through them, and call-outs are followed
  through the file's stubs to their targets.

Drafts before and after (`python3 -m tools.generate.show`, the library's coverage
and call logs):

| Routine | Flag assignments | Flag parameters | Call-out stubs | `m_dp` accesses | Logic lines | Stops declared |
|---|--:|--:|--:|--:|--:|--:|
| `$C7:0004` (m8 x16) | 140 → 11 | 4 → 1 | 16 → 11 | 135 → 0 | 893 → 690 | 0 → 7 |
| `$C7:0655` | 15 → 2 | 4 → 0 | 3 → 3 | 43 → 0 | 128 → 90 | - |
| `$C7:0A39` | 5 → 0 | 4 → 0 | 1 → 1 | - | 26 → 19 | 0 → 1 |
| `$C2:015E` | 2 → 0 | 2 → 2 | 6 → 6 | - | 115 → 109 | - |
| `$C1:423A` | 7 → 6 | 1 → 1 | 2 → 2 | - | 112 → 109 | - |
| `$C0:62DE`, `$C0:5FB6` | unchanged | | | | 33, 79 | |

The ground truth on the `$C7` port, which it undercounted:

| Port | Accesses in both / port (before) | Named in the port (before) | Call-outs in both / port (before) |
|---|--:|--:|--:|
| `$C7:0004` | 23 / 23 (0 / 43) | 97 of 97 (0 of 62) | 5 / 5 (0 / 0) |
| `$C7:0655` | 8 / 8 (0 / 5) | 13 of 13 (0 of 5) | 2 / 2 (0 / 0) |
| `$C7:0922` | 5 / 7 (0 / 4) | 11 of 11 (0 of 4) | 2 / 2 (0 / 0) |
| `$C7:0A39` | 2 / 6 (0 / 1) | 13 of 13 (0 of 4) | 0 / 0 (0 / 0) |

The draft reaches every address the port's `$C7:0004` does and calls what it
calls; where the counts still differ, the port inlines a stage the draft calls
out (`$C7:09FD`, `$C7:0734`) or reads a table through the driver's own
accessors. Its other ports read as before (`$C1:423A`, `$C0:62DE`,
`$C2:015E`, `$C0:5FB6` unchanged).

`draftcheck` over the library, with every fix: 188 drafts registered in place
of the ports (134 left as they are), 182 called, 53.1 million calls,
none mismatching, 84824 interrupted. It exports the committed tree by default;
`--rev` takes another (`git stash create` gives one of uncommitted work).

### Between the waves: the validator

Wave 1's reports named five places where shadow mode and the don't-care check
made a port reproduce what the original leaves dead, or broke on a port shaped
the natural way. Each is fixed in `runtime/port/native.c` and
`tools/dontcare_check.py`, under one rule: anything that lets shadow mode
compare less is shown sound (from liveness, or from the original's own
execution), and a deliberately wrong port is shown still caught. The runtime
side is tested on a synthetic program (`tests/port/shadow_test.c`, in CI), the
check's on synthetic code (`tests/test_dontcare_check.py`), and each on the
ROM below.

**A's low byte alone: `DC_AL`.** Liveness has A's two bytes apart, but only
the high byte could be declared by itself (`DC_AH`), so a routine whose callers
read A's high byte (a later 16-bit load or `TAX` takes it along) had to return
the exact low byte an 8-bit routine leaves, on every path: the battle's effect
handlers returned "what the original leaves in A" as a value, and the frame
update set A's low byte before each of its call-outs although no callee reads
it. `DC_AL` (bit 10) leaves out the low byte alone, at a return, a stop and a
call-out; with it every register byte and flag liveness tracks can be declared
on its own. Sound for the same reason as every declaration: the check maps
`DC_AL` to liveness's low-byte bit and verifies it where shadow mode applies
it, and at a call-out a low byte the callee left as it found it goes back to
being the port's own (`keep_untouched`), so a port that relies on the value it
declared dead there is compared as the shipping mode would run it. Shown: the
synthetic program's routine that returns a wrong low byte under `DC_AL`
matches and one with a wrong high byte doesn't ("A's high byte original 12
native 99"); one that declares the low byte dead at a call-out and then stores
it is caught at its return; on the ROM, `DC_AL` on `battle_sine_scale`'s entry
(its callers read the result) and on the call to `$CD:3E8E` (which pushes the
bank A holds) are findings. `--list` now says, for every declaration, what
else liveness has dead there (`could add`): 312 declarations could add
`DC_AL` before the ports below adopted it, 291 after (295 once merged with
the fuzz and timing pass's new entries).

**A stop's own set: `m_stop_at_own`.** An entry's set applied at its returns
and at every stop it made, so one early exit to code that reads a register the
callers don't shrank the whole entry's set: the frame update's demo exit to the
reset code at `$C0:FF00`, which reads Y and V, kept both out of a set that is
otherwise dead after the return, and `input_read_joypad` declared V at its
`RTS` as a stop to get around it. `m_stop_at_own(s, at, set)` (and
`native_own_dont_care_at_stop`) makes `set` the stop's alone: shadow mode and
`ct_fuzz` leave out exactly that set there, and the check verifies it alone at
`at`; the entry's set then applies only at returns and at stops that aren't
their own, and is checked at each (`native_finish_dont_care` is the one rule
both runtimes use). Nothing is compared less than it is declared, and every
set is checked where it applies. Shown: on the synthetic program, a wrong Y at
an own stop is caught and the same stop declared with `m_stop_at`, which adds
the entry's `DC_Y`, hides it; on the ROM, `scene_run_sequence` with D added to
its entry (dead after its return, read at the sound wait) and a wrong D at the
wait matches all 32854 calls of `endings-beyond-time` as a shared stop, which
the check rejects ("where liveness has D read"), and mismatches 5142 as its own
stop, which the check accepts; an own set with V at `$C0:FF00` is rejected.

**Call-outs compare exactly what the callee reads.** With `DC_AL`, a call-out's
set can be the complement of `Liveness.callee_reads` bit for bit, and the check
verifies it there as before. Making the runtime leave out what no callee reads
by itself, from a liveness table built with the generated code, was considered
and not done: it would put the ROM's analysis into the runtime (and make what
is compared depend on the build's call logs), and it would move the claim out
of the source, where the declaration is reviewed and `--list` shows what else
could be declared.

**Stops and calls at a loop's head.** Shadow mode took a stop or call-out as
reached the first time the original passed its instruction with the same S, so
a port that ran several passes of an interpreter loop and stopped at its head
was compared against the first, and ports worked around it (the scene
sequence interpreter handed the rest of a run back as a call-out from its
entry's own `JSR` site, which is compared at the same first pass and was only
right when no command had run in that stretch). Now every pass is compared,
and the first where the machines are the same counts (`reach_point`). It can't
accept a wrong pass: the comparison covers everything the original's code can
observe except what is dead there (the declared sets, checked against
liveness, and the stack below S) and what changes with time rather than code
(HDMA's registers, the SPC700's port clears, the OAM reload), which the first
pass left out as well. What the original does from a pass is decided by the
machine at that pass, so from a pass where the two are the same the original
goes on exactly as the shipping mode goes on from where the port stopped:
whichever pass it is, what follows is what the original did. A port that is
the same at no pass is reported with the first pass's difference, and the
last's, once the original leaves the point's frame, when an interrupt comes
(its handler runs on the original only, so a later pass can't be compared:
reported as a mismatch, as the first pass's difference was before), after
65536 passes, or when the run ends with it still waiting (`native_failed`).
So nothing the old rule reported as a mismatch passes now unless a later pass
matched exactly. A failed pass costs a comparison per later pass until then.
`ct_fuzz` does the same over the original's passes. Shown: on the synthetic
program, a stop after two passes of a loop and a call-out from a loop's head
after two inlined passes match (under the old runtime, every call mismatched,
"X original 0 native 2"), a stop whose memory matches no pass is caught ("and
at 2 later passes, then the original left"), and in a loop longer than a
frame a port matching no pass is reported at the NMI. On the ROM,
`scene_run_sequence` made to stop at its loop's head `$C3:0B03` after one
command: in `title-start` 235 of its 1500 calls mismatch under the first-pass
rule and none now; the same with the script pointer one byte off, 238 mismatch
("... at the last: WRAM[0A20] original 3B native 3C").
`scene_run_sequence` stopped at `$C3:0B03` in place of the workaround, until
the fuzzer as it is after the fuzz and timing pass found 4793 of 20000 states
where no pass matched, and the comparison was right not to take one: a command
the port gives up on (one no script has) has already written the scratch cell
`$F0` and changed the flags, which the original does only once it runs the
command, so at the loop's head, on every pass, the port's machine is one the
original never has. The port went back to the call-out (which the fuzzer
compares only at the return, after the original has run the command itself);
it now hands such a command to the original's code for that command, entered
as its dispatch enters it (REIMPLEMENTATION.md, "Phase 3, the scene program: the staff
roll").

**An entry on another port's path.** An entry the original reached while it
was catching up with a native routine started a comparison of its own, nested,
and only the innermost comparison was checked, so one still running when the
original got to the point the enclosing routine waited for took that point
for itself (the same call, typically), and the enclosing comparison failed:
the $C7 pilot's `play_song`, and `$C3:0C28`, where the scenes' sound wait goes
on and the store command's path runs through. The runtime handles it rather
than the check rejecting it: at each step the outermost routine waiting at the
original's point comes first (`settle_nested`). The nested comparisons are
checked there first, innermost first, as before, and one that finishes or goes
on (its own point is here too, or its callee unwound past it) is dealt with as
it was; any other is set aside, counted as `abandoned` with a note naming the
enclosing routine, and the enclosing one is compared (a difference a set-aside
one had shown stays a mismatch, and one waiting for a later pass is reported
then). That compares less only
for a call the shipping mode never makes (there the enclosing routine runs
natively and the entry isn't called at all), and the comparison it gives way
to covers everything the stretch did. Nothing that matched before is set
aside: nested comparisons that end inside the stretch, the common case (a
helper the port inlines and the burndown credits as an entry), are compared as
they were, and those that outlasted it made the enclosing one fail. Over the
library the shadow runs' counts are the same as under the old rules for every
entry (calls, 64558726 matched, 64158 interrupted, 489 abandoned, none
mismatched), so both this and the loop heads change nothing that passed. A
first version didn't start an entry inside another's stretch at all, which is
the shipping mode's rule, but that dropped 17.3 million of those comparisons
(the battle's per-object drawing routines and the menus' glyph and number
routines, all inlined by their callers) and moved four entries' timing past
the timing gate's limits, so it was replaced. A check could only reject such a
registration by knowing which of the original's instructions each port stands
for, which the burndown only estimates, and it would forbid the registrations
porters want. Shown: on the synthetic program, an entry registered in the
middle of another's path took the outer one's call-out (all 250 of the outer's
calls mismatched, "original returned without the call"); now both match, the
inner one set aside once per pass inside the outer's stretch and compared at
its own calls, and a callee the outer inlines, registered as well, is still
compared at every call (none abandoned). On the ROM, `$C3:0C28` registered for
the demonstration: under the old rule `events-jetbike-race` reports
`scene_run_sequence: original returned without the call to C70004`, the scene
agent's failure, and `endings-beyond-time` 8 such (2 of `scene_run_sequence`,
6 of the new entry's own); now none, and the new entry is compared at 26 of
its 35 calls there (1 of 2 in the race), the rest set aside where they
outlasted a run's call.

**A reader fix.** The check read a table's struct with two fields in one
declaration (`uint16_t flag, copy;`, `battle_run_queued_scripts`' table) as one
field, which shifted every column after it, so a declaration taken from such a
table would be checked with the wrong address or set; it now reads every
declarator (`struct_fields`, tested on synthetic code).

**The ports that adopted them**, each changed only as far as the declarations
go, fuzzed again and matching over the library (PORTING.md's rows):
`battle_frame_update` declares Y and V dead after its return, its demo exit
its own set, and its call-outs `DC_AL | DC_X | DC_Y | DC_FLAGS`, so the low
byte it set before each call is gone; the fifteen effect handlers, called by
themselves, and `battle_place_formation` declare `DC_AL` and no longer
reproduce what the original leaves in A's low byte; `input_read_joypad`
declares Y and V after its return and its two stops their own sets, in place
of its stop at the `RTS`. (`scene_run_sequence`'s stop at its loop's head was
taken back, above.)

### The tooling pass after wave 1: the analysis

Wave 1's reports (above) named what the analysis side cost the ports:
liveness keeping every register live after bank C6's routines and carry and
overflow live after the field loop's callees, a burndown that credits nothing
a port inlines, and drafts that name scratch cells after unrelated variables.
Before wave 2 takes on the three big banks, each was fixed where it could be
made precise without claiming anything the original doesn't show.

**Liveness through indirect jumps** (`tools/disasm/liveness.py`). A routine an
indirect `JMP`/`JML` reaches counted everything as read after it returns, and
the jump itself used only a hint's targets, not the ones derived from data.
Now the jump goes to every target the analysis follows; a `JMP (abs)` after a
`PER` is a call that returns to the `PER`'s address, any other a tail jump
whose targets return for the routine making it, so their callers' liveness is
that routine's. That holds only if nothing else reaches them, so a routine
keeps everything live where some site that reaches it has a target set nobody
argued complete (neither a `derive` nor a `bound` hint: a movie-only list could
have other targets from anywhere). All eight indirect jump sites in the ROM
are argued, so all 176 routines they reach are now precise, none kept live;
the count is `Liveness.indirect_entries`. After the C6 routines the menu object
scripts reach, X and the carry are what is read.

**The field loop's carry and overflow.** They were the analysis being coarse,
not the loop reading them. Following the reads back from the loop found two
causes. The sound driver's entry `$C7:0004` saves P and X (and the rest) and
calls `$C7:0A39`, which on its refused path pulls its own return address and
runs the entry's exit: its saves "reached by a callee" counted as reads of
everything, for every routine that sends a sound command. A push a callee
reaches is now followed into the callee: where every path either returns
normally or pulls the byte with a pull of its size and then only pulls the
rest of the caller's frame and returns as the caller does, the push reads what
is live after that pull, which is what the caller's callers read. A return
below the depth a routine was entered at is a return through its caller's
frame, and its liveness is carried apart from an ordinary return's (packed
values have a second set of segments for it), so what follows the escape isn't
merged into the callee's other returns. The second cause was the analysis
walking every context's width variants at an instruction: `$FF:F9C4` runs its
`PHX` with 8-bit X from one caller and 16-bit from another, which read as
depths that disagree, so its reach was unbounded and every push around a call
to it read everything. Each context now runs the widths it was traced with.
What stays: the overflow flag, which the menus' entry `$C2:800E` saves with
`PHP` and then copies S into `$0920` for a stack of its own (`TSX`), which no
pairing follows (docs/TODO.md). `RTI` now reads no flag (it pulls P).

**What turned out wrong, and was fixed.** Three places where liveness was *less*
careful than the code: a `JSR (abs,X)` whose table is filled at run time
(count 0, every target derived from data, as the scene program's task lists at
`$7E:3998`, `$7E:39CF` and `$7E:37F6` are) called nothing at all, so the task
loop read nothing its tasks read and the tasks' callers' liveness never
reached them; the derived and hinted targets of any call table were left out
of what callers read after them; and a `PER` then `JMP (abs)` was taken for a
tail jump, dropping the code after it (it happened to return to an `RTS`). The
task loop's register saves, which tasks leave through `JMP $39A7` without
returning, were "paired" because the tasks weren't known; they are now "a
callee reaches it", and a return through a caller's frame counts what is read
after it. With the tasks known, five declarations in `game/` were wider than
the original allows, and were narrowed, the ports changed as little as that
needed: the scene sprite lists `$C3:0D0E`/`$C3:0D5E` now leave Y (the next
free entry) and A (the loop's last value) as the original does, and
`$C3:0DBC` Y, since the scene task loop carries them into the next task, and a
task reached the field's start (through the reset), whose menu call reads Y;
`$C3:094C`'s Y was already the original's and is no longer declared; the
sequence interpreter's call to `$7E:37F3` passes its carry, which a task's
chained `ADC` reads.

| | Before | After |
|---|--:|--:|
| routines everything is live after (all 3136) | 209 | 34 |
| of them: entered by an indirect jump | 175 | 0 |
| register and flag bits live after returns, summed | 14677 (flags 3968) | 11731 (2192) |
| bits read at entries, summed | 11895 (flags 2409) | 10255 (1324) |
| routines A's low byte / X / the carry / the overflow is live after | 511 / 1032 / 1296 / 1884 | 298 / 850 / 998 / 800 |
| routines with something dead after return | 2820 | 3092 |
| bank C6 (41 routines): everything live after | 34 | 0 |
| bank C6: A / Y / any flag live after (routines) | 35 / 37 / 36 | 1 / 0 / 35 (the carry) |
| `$FD:C2C1`: live after it returns | AH X V C DB D | AH V DB D |
| `PHP`s paired / a callee reaches it (per context) | 567 / 87 | 579 / 75 |
| pushes paired through a callee that leaves the caller's frame | - | 12 (`$C7:0140`-`$C7:0149`) |

(Pushes of any register counted per context moved from paired to "a callee
reaches it" in 145 places, every one around the scene task loops' calls, which
were paired only because the tasks weren't known, and the other way in 63.)

**Why it is sound.** Everything the check now accepts that it rejected before
is dead on every path the analysis knows the original takes: a tail jump's
targets return exactly where the routine making the jump returns, and are
reached from nowhere else unless a site whose targets aren't argued complete
reaches them (then nothing changes); a pull in a callee is followed only where
every path from it pulls the rest of the frame and returns as the caller would,
so what is live after it is what the caller's callers read; an instruction runs
in a context only with the widths the analysis traced there; and `RTI` replaces
P. The assumptions are the ones liveness already made for `JSR` targets (that
the analysis sees every caller; the call logs back it up for the movies'
paths). Checked three ways: `tools/dontcare_check.py` finds nothing on
`game/` as the pass left it (417 entries, 67 stops, 73 call-outs); a
declaration the new liveness allows and the old one refused (`menu_object_read_mode` declaring
`DC_A | DC_Y | DC_N | DC_V | DC_Z`) passes, and one that is wrong (the same with
`DC_X`, which its callers read) is still a finding; and the drafts, which are
made from liveness, match the original over the library (below).

**Drafts before and after.** `tools.generate.draftcheck` over the library (26
movies, shadow mode), with the committed tools and with these: 194 drafts
registered before, 197 after (three entries the analysis enters with several
widths now pick one, below); 166 and 169 called, 39.09 and 39.10 million calls,
none mismatching either time (84819 and 84841 interrupted), and the don't-care
check finds nothing in the drafts' own declarations (471 entries, 124 stops,
519 call-outs). The drafts the fixes were for: the C6 routines' headers went
from "callers read A X Y N V Z C" to "X C" (`$C6:E237` 3 flag assignments to 1,
registering with `DC_A | DC_Y | DC_N | DC_V | DC_Z` where it had 0), and
`$FD:C2C1`'s from "A X V C" to "A V", its logic losing the carry (12 mentions to
none) and X from its result. The gate passes (`suite OK` with
`--long-routes`), and the strict player builds warning-free.

**The burndown** (`tools/burndown.py`) credited a port with its routine's code
only, so every helper a port inlined had to be a registry entry to count, and
it read the registry with a pattern that missed its macro rows (the 180 event
commands, the field uploads). It now reads the entries as the don't-care
check does, and follows a call into its callee where the port inlines it:
where the port makes no call-out at that call, by the call-outs its functions
make, read through the file's helpers, tables and parameters the way the
don't-care check reads declarations. The original makes that call during the
port's call and the port doesn't, so the port does its work, which shadow
mode would otherwise show. It doesn't over-credit where the reading can't be
trusted: a port with a call-out (or a PC it sets) the check can't read, or
one whose site reads as something that isn't a call (a table whose struct
declared two fields in one declaration came out shifted, which the
validator's pass has since fixed: "Between the waves: the validator"),
credits no callee, and neither does a call past a place the port hands back
(an `m_stop_at`, a PC it sets), since the original may be making it; 22 ports
credit none for those reasons, among them the two that would otherwise claim
50 KiB between them (`battle_run_queued_scripts`, `battle_main_loop_body`).
Idle entries credit none either. Per bank (the wave-1 banks and the three big
ones):

| Bank | Before | The macro rows | And inlining (now) |
|---|--:|--:|--:|
| `$C0` | 9.6K, 84% left | 18.3K, 69% | 18.7K, 68% |
| `$C1` | 10.0K, 84% | 10.0K, 84% | 10.7K, 83% |
| `$C2` | 5.2K, 91% | 5.2K, 91% | 5.6K, 90% |
| `$C3` | 3.0K, 25% | 3.0K, 23% | 3.5K, 10% |
| `$C6` | 1.1K, 68% | 1.1K, 68% | 1.1K, 68% |
| `$CF` | 6.3K, 7% | 6.3K, 7% | 6.4K, 7% |
| `$D1` | 4.8K, 17% | 4.8K, 17% | 4.8K, 17% |
| `$FD` | 8.1K, 47% | 8.1K, 47% | 8.1K, 47% |
| total | 60.1K, 78% | 69.0K, 74% | 71.6K, 73% |

`$C0` moves most, and not from inlining: 197 of its 249 entries are macro rows
the old pattern didn't see. `$C3` drops from 25% to 10% left with the edge
sequence `field_rasterize_quad` inlines (`$C3:0F35`), the frame work's
controller check (`$C3:0A8E`) and song update (`$C3:09A4`) credited; its
middle column also counts the targets of `$C3:0F32`'s indirect jump, which the
walk now follows. `$FD`, `$CF` and `$D1` barely move because wave 1 had
already registered what their ports inline, to get it counted.

**Draft names** (`tools/generate/names.py`). A routine's subsystem was its
bank's, and banks `$C3`, `$C6` and `$FD` serve several (and `$FD` had none,
so any subsystem's name was taken). In those banks it is now the one every
caller comes back to, through the call sites, call logs and followed indirect
jumps (none where they differ or there is no caller, and then only `core`
names it). A direct-page access is named only by a variable the map puts on a
page with that D: of the routine's own subsystem, or of any where the page is
its own subsystem's (field code takes the event engine's cells on the field's
page, event commands on the field's page don't take the field's). Any other
cell gets a plain name, `SCRATCH_01DB`, which the draft defines and undefines.
Over the drafts of every registered routine (194 before, 197 after): 1016
distinct names before, 990 after, 261 scratch names, and 713 addresses left as
numbers before against 466 after. The two that went wrong: `event_cmd_call`
(`$C0:5FB6`) wrote `RAM_FIELD_SPRITE_RANGE_NEXT` for its `$DB`-`$DF` and
`0x7E01D9`/`0x7E01E1`/`0x7E01E3` for the rest, and now has `SCRATCH_01D9` to
`SCRATCH_01E3`; `menu_object_read_mode` (`$C6:E237`) wrote
`RAM_SCENE_FRAME_READY` for the menu's `$00` and a number for `$7E:1BA9`, and
now has `SCRATCH_0000` and `RAM_MENU_SCENE_MODE_BITS` (its subsystem is the
menus', its callers' ). Nine drafts lost names: `$FD`'s
`battle_clear_b328_table` a menu name it had taken for want of a subsystem,
and the C6 irises and motion routines and `$C3:0DF3` (which nothing calls) the
scene's circle and scaler cells, which they use on the menu's page
(docs/TODO.md). A routine the analysis enters with several widths is drafted
with the registry precondition's, then the widths the coverage saw it run with;
if several remain, `show` prints one draft for each rather than stopping.

### Unreached ownership at the scene-continuation merge

The scene-continuation merge transfers `$7E:7441-$7E:74DC` from
`attract_frame_pass` to `scene_turn_map`, and `$7E:31BE` from
the former `attract_vblank_start_loop` to `scene_frame_wait_done` (both
comparisons have since retired with the scene program). Re-deriving both
trees' exact reach shows that every retained audit range is a subset of
the original full-library range; none gains bytes. The former 156-byte
callback exclusion leaves this ledger because its separately registered
scene port owns it now (its comparisons are in `docs/ports.tsv`). This is
an ownership correction using the existing library record, not an absence
inferred from the sample. The retained safety entries keep their registry
contracts apart from calibrated costs; added scene RAM fields do not move
the sign-refresh cells, and the warp continuations do not change the circle
builder or its radius selection. Each refreshed row records that review;
the merge gate replays the unchanged fuzz byte obligations against the new
implementation fingerprints.

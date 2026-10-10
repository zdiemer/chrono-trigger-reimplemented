# Porting routines

Ported code replaces original routines one at a time while the rest of the game
keeps running on the interpreter. Each port is validated call by call against
the original, independent of timing.

## Layout

Game code is organized by subsystem, the way the game's own programmers would
have split it, not by where the routines ended up in the ROM. The ROM address
is recorded in each routine's comment. Names use the subsystem as a prefix
(`field_nmi`, `menu_wait_for_frame_loop`). When porting a routine, place it
by what it does; move routines when their purpose becomes clearer.

| Path | What |
|---|---|
| `game/core/` | System code shared by every mode (display and interrupt control, the palette and tile animations, the sound driver interface and its data accessors, the block move stub the menus and save code write to RAM) |
| `game/data/` | Data handling (the decompressor) |
| `game/event/` | Event scripts: the language (`script.h`: how a command decodes into an operation's typed parameters and what happens next; each file ends with its opcodes' rows), the engine that runs it (`engine.c`), the scaffolding it reproduces for the gate (`leaves.h`), the original's machine -- entries, guards, registers (`bridge.c`, `entries.c`), the ROM tables (`event_data.h`), and the operations by what they do (`flow.c`: jumps, conditions, waits; `calls.c`: objects calling each other; `variables.h`/`variables.c`: the scripts' variables; `story.c`: the story memory and its flags; `party.h`/`party.c`: the party; `dialog.c`: lines, questions, answers; `animation.c`, `facing.c`, `properties.c`, `movement.c`: objects; `input.c`: the controller; `memory.c`: memory by address; `screen.c`, `sound.c`, `locations.c`: the screen, sound, places and battles) |
| `game/field/` | Field mode: interrupt handlers, frame sync, objects and their motion, the per-frame object update loop, the sprite table, sprite tile graphics, the layers' scroll arithmetic, the per-line effects' tables and the quadrilateral rasterizer in bank C3, the screen effects the windows cut (`screen_effects.c`) and the layer work scripts leave the main loop, the credits among it (`script_layers.c`). The objects are a module, `field/object.h` (their role, animation, motion and place on the screen), implemented in `object.c`, `animation.c`, `motion.c`, `object_view.c` and `object_update.c`; `bridge.h`/`bridge.c` hold the guards and the pieces the entry wrappers share, `scratch.h` the cells the original writes for itself, and `leaves.c` (with `parts.h`) what the field's routines leave the original's code, worked out beside the wrappers. |
| `game/menu/` | Menu mode: frame sync, font glyphs, text and numbers, the VRAM upload queue, the save file screen, the save code that checks, saves and loads a slot (`saves.c`), the End of Time's gate screen (`gate_screen.c`), the digits two screens build (`digits.c`), sprite objects with their scripts (the commands in bank C6 among them), the map they scroll and redraw, the Epoch's map view and time dial, the equipment screen (its steps, its windows, the member list and card) and the items screen, with the scrolling lists they share (`lists.c`) |
| `game/overworld/` | The world map walked on foot, which runs in the sprite-object mode at $C2:0000 (`overworld.h` says how): the leader's walk and the map's scroll with it (`walk.c`), the followers along its trail with their search for walls (`followers.c`), the party's shared vocabulary -- who is in it, a walker's facing and animations, the vehicles it boards, the trail (`party.c`), the ground and the regions (`terrain.c`), the places' lists and their watcher (`places.c`), the regions' music and the mode's sound request (`music.c`), the place's sign (`sign.c`), the main loop and its round trips to the menus and the Epoch's screens (`main_loop.c`), and the registered entries and their guards (`entries.c`) |
| `game/battle/` | Battle mode: setup of a battle's tables, the main loop, display synchronization, the NMI handler, drawing battle objects' sprites, scroll tables, timers, per-frame work buffers, the window tilemap, the menu window and combatant animations (so far) |
| `game/attract/` | Attract sequence and title screen code the game runs from RAM |
| `game/scene/` | Scenes the game decompresses to `$7E:3000` and runs from RAM (the attract sequence and title screen, the jet bike race, the return to the title after a game over): the bank C3 routines they call (the boot, the interrupt handlers, the sequence scripts, sprite lists, bitmap scaling, the circle window table, the race's generated code, arithmetic) and, guarded, routines of the RAM program itself (divides, multiply, task lists, wait loops) |
| `game/registry.c` | Table registering each routine: address, name, entry precondition on P, don't-care set, and time cost in ON mode |
| `runtime/port/machine.h` | Helpers for touching the machine the way the original does: bus reads and writes, stack, returns, flags |
| `runtime/platform/` | Platform services (video, audio, timing, input, saves) that ports call instead of writing hardware registers, with the SNES backend performing the original's writes ([PLATFORM.md](PLATFORM.md)) |
| `runtime/port/native.c` | Registry, interception, call-outs, shadow validation, reporting |
| `runtime/port/fiber.c` | Coroutines native routines run on, so call-outs can suspend them |

## Modes

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --frames 7200 --movie oracle/movies/new-game.ctm --native shadow
build/main/ct_dev --rom roms/ct-usa.sfc --frames 7200 --movie oracle/movies/new-game.ctm --native on
```

- **`off`:** the interpreter runs everything.
- **`shadow`:** the interpreter still runs everything. On each entry to a
  registered routine, the machine is cloned and the native routine runs on the
  clone. When the original returns, the two machines must match exactly:
  registers and flags, WRAM (including stack bytes), SRAM, VRAM, CGRAM, OAM,
  PPU and CPU I/O register state, and the CPU-to-APU ports. Timing isn't
  compared, and neither is state that changes with time on its own: HDMA's
  per-channel line counters and table addresses, and the PPU registers enabled
  HDMA channels write, CPU-to-APU ports the SPC700 cleared (its driver
  writing `$F1` bits 4-5) while the original ran, and the OAM address when a
  vblank (which reloads it) passed with NMI disabled. Calls interrupted by NMI/IRQ
  are counted but not compared. Registers and flags in the routine's
  don't-care set (below) aren't compared, nor, where a port stops at an
  instruction of the original, those it declares dead there (`m_stop_at(s,
  at, set)`, which also sets PC, or `m_stop_dont_care(s, at, set)`, `at` being
  the instruction, or the return instruction the port carries out last when
  the set is dead after a return). Shadow mode reports a port that stops
  anywhere but the address it declared, and one whose entry has a don't-care
  set that finishes without returning or declaring where: that set applies at
  every stop, on top of the stop's own, except one declared with
  `m_stop_at_own(s, at, set)`, where `set` alone applies. That is for an early
  exit to code that reads what the routine's callers don't (the battle frame
  update's demo exit to the reset at `$C0:FF00`, which reads Y and V): the
  entry keeps the larger set for its returns. Every declaration is a claim about the original, and
  `tools/dontcare_check.py` checks each against liveness where it applies (the
  gate runs it; RECOMP.md, "Before the bulk"). Nor are the stack bytes the original pushed and pulled
  again since the last comparison, between the lowest S it reached and its S
  now: nothing reads below S, so a port needn't rebuild the frames of the
  subroutines it inlines or the P bytes a callee saves and restores (the $C7
  pilot, below; bytes still on the stack, such as a frame live under a
  call-out's return address, are compared as before). `DC_AH` in a don't-care
  set leaves out A's high byte only, for code that works on an 8-bit A and
  leaves the high byte to whatever last set it, and `DC_AL` A's low byte only,
  for code whose callers read A's high byte (a later 16-bit load or `TAX` takes
  it along) but not the low byte an 8-bit routine leaves; liveness has the two
  bytes apart, so every register byte and flag it tracks can be declared on its
  own. `DC_WIDTH` leaves out the accumulator's width (P's M bit), for an
  entry whose callers set it again with `REP` or `SEP` before anything depends
  on it (the sprite-object mode's objects' loop after every handler): liveness
  checks it apart (`Liveness.width_read_after_return`, which walks each
  caller's resume point to the first `REP`/`SEP` of bit `$20` and counts any
  instruction whose effect depends on M, `PHP`, a call or a return carried up
  as a read), and only an entry's own set may carry it, at its returns; a stop
  or a call-out declaring it is refused, `m_stop_at_own` included. `python3 tools/dontcare_check.py --list` says, for each declaration,
  what else liveness has dead there (`could add`). Any mismatch prints the first
  differing field and makes `ct_dev` exit nonzero.
- **Idle entries** stand in for one pass of a busy-wait loop (the game's
  wait-for-next-frame loops) instead of a routine. The native function runs
  the loop body and either leaves PC at the loop head or sets it to the loop's
  exit. In `on` mode a waiting pass skips ahead to the next point where
  `$4212`'s blanking bits can change (hblank end, hblank start, or the end of
  the line), so interrupts still land and loops polling those bits still see
  every change. Loops polling something else, like the APU ports, give
  `idle_clocks` instead: a waiting pass then takes the loop's own time per
  pass, since skipping ahead would change when the loop sees the other
  processor's answer. A loop that waits for a change leaves on a pass that
  takes `IDLE_EXIT_CLOCKS` (54, the shortest such loop's pass), since the
  original sees a change on its next read, not at the change. `shadow` mode
  skips idle entries.
- **Guards** cover code the game decompresses or copies into RAM, where the
  same address holds different code and data at different times. An entry's
  `guard` checks, with `m_code_matches`, that the bytes at the entry hash to
  the code the port was written for. Only the 64-bit hash is kept, never the
  code. If the guard fails, the interpreter runs whatever is there.
- **`on`:** registered routines run natively in place of the original. A native
  routine finishes instantly, so the time the original would have taken is
  charged afterwards, one scanline at a time, so frame boundaries and line
  events still happen. An interrupt that comes due meanwhile is taken at the
  instruction boundary where the port stopped, and the rest of the time is paid
  when the handler returns there, wherever the handler moves the stack in the
  meantime (RECOMP.md, "The timing gate", and "The tooling pass after Phase
  3's first chunks"; a port that spans frames, below). Each call's
  cost is the time of its DMA transfers, as it is, plus the rest of its own
  bus-access clocks times the routine's `cost_milli`/1000 (or, for the clocks
  it attributes to another routine it runs inline, that routine's; Writing a
  port, step 7), so cost tracks data size. That is an estimate, and any error shifts everything
  after it, so `on` mode with hand ports drifts from `off` mode. Generated
  blocks don't: their time is exact to the instruction (below).
- **Syncing with the SPC700.** Charging a routine's time after it stops is wrong
  where another processor watches the timing of its I/O. The sound driver
  writes parameters to APU ports 1-3 and then calls out to a subroutine that
  writes port 0. Charged afterwards, the time opened a gap between the two,
  the SPC700 read the ports inside it, and uploaded songs came out scrambled:
  no music in `on` mode, while `shadow` (which doesn't model timing) matched.
  A port calls `native_sync(s)` before such I/O. In `on` mode the routine
  suspends there until the time it has taken so far has passed, then continues;
  an interrupt that comes due meanwhile is taken, and returns to the suspended
  port.
  `shadow` mode ignores it. Compare `on` against `off` audio with `ct_dev --wav`
  when a port talks to the APU. `ct_dev --exclude PREFIX` leaves out the
  routines whose names start with PREFIX, for narrowing down which port causes
  a difference; `--include PREFIX` keeps only the ports whose names start with
  it (generated blocks stay). A port's time stays an estimate and ports don't
  carry cycle costs; `--timing-report` in shadow mode measures each entry's
  estimate error per call and the `cost_milli` its totals imply, and the timing
  gate in `tools/suite.py` bounds it (RECOMP.md, "The timing gate").
- **Generated blocks.** A build given the ROM also registers C blocks
  generated from it ([RECOMP.md](RECOMP.md)) behind the ported routines: a port
  takes precedence at its address, and shadow mode checks the blocks too.
  `--recomp off` leaves them out. In `on` mode a block's time passes per
  instruction, I/O sees the time the interpreter's would, and interrupts land
  between the same instructions (RECOMP.md, "Exact timing"): with
  `--recomp only`, which leaves the hand ports out, `on` mode produces the same
  traces as `off` mode, frame for frame. Since M3 the code the game runs from
  RAM has guarded blocks too, so a guarded hand port for RAM code competes with
  a block generated from the same image (the port wins where its guard holds).

## Call-outs

A port calls code that isn't ported yet with `m_jsr`/`m_jsl`, giving the
original call site. The call doesn't run inside the native routine. The
routine runs on its own coroutine (`runtime/port/fiber.c`); a call-out pushes
the same return address the JSR/JSL would, points PC at the callee, and
suspends. The system loop then runs the callee like any other code: time
passes, interrupts and frames happen, and a ported callee runs natively. When
the callee returns to the call's return address with the stack as it was, the
routine resumes. So a port may call code that waits for frames (event command
handlers, fades), and ports can nest.

In `shadow` mode the native routine runs on the clone up to its first
call-out. The machines are compared when the original reaches the same call
(same target and stack pointer), as well as at the return. Where the original
passes that point more than once (a call or a stop inside a loop, such as an
interpreter's dispatch), every pass is compared and the first where the
machines are the same counts, so a port can run any number of passes before it
calls out or stops; a port that is the same at no pass is reported with the
first pass's difference once the original leaves the point's frame or an
interrupt comes (RECOMP.md, "Between the waves: the validator"). While the callee
runs, only the original machine moves. When it returns, the clone is reloaded
from it, and the native routine continues to the next call or its return,
except that a register the call declared don't-care and the callee left as it
found it stays the native routine's own, as the shipping mode would have it:
the callee carried the native value through. A
port is thus checked at every call it makes, not just at the end. That makes
`shadow` strict about registers passed to callees; `m_jsr_dc`/`m_jsl_dc` name
registers and flags a callee doesn't read (before it returns: what it only
carries back is the port's business, and compared later). An interrupt in the stretch before a
checkpoint skips that one comparison, and later stretches are still checked.

A callee's frame that is still on the stack at a call-out (the P a subroutine
saves at entry, under the return address of a call it makes) is compared at
that call, so a port that inlines the callee pushes it. A callee the port
inlines, or any address on the stretch of the original a port stands in for,
can also be a registry entry. The original entering it there starts a nested
comparison, which is compared on its own if it ends inside the stretch; if it
is still running when the original gets to the point the enclosing routine
waits for (typically the same call), it is set aside, counted as `abandoned`
with a note, and the enclosing routine is compared. The shipping mode never
calls it there (the enclosing routine runs natively), and the enclosing
comparison covers what it would have done. (Before, the nested comparison took
that point for itself and the enclosing one reported "original returned
without the call": `play_song` at `$C7:01A1` in the $C7 pilot, `$C3:0C28` in
the scenes.)

**How a bridge's calls are paired with the original's decides what it can
run inline.** Shadow mode matches a port's call-outs to the calls the original
makes, in order and by site. So a port that runs another routine's work as C
(the field's object update running its steps, the sprite table's building
running its draws) can do so only where that work makes no call-outs of its
own: the callee's calls, made from the caller, don't line up with the calls the
original makes from inside the callee. And where several calls come from one
shared site (every draw of a frame is made from the same two), they must all
be inlined or none, or a call the port makes would be paired with the
original's call for another object. The field rewrite learned this from
105,923 mismatched draws (RECOMP.md, "Phase 3a: the field subsystem off the
machine"); `every_draw_is_own` and `field_view_changes` exist for it. A call is
paired by its stack too, so a port that runs a step the original reaches
through a table's `JSR` makes the step's call-outs under that `JSR`'s return
(`field_run_table_step`): the screen effects' curtain, called a frame higher
than the original's, never paired, and the attract demo's curtains read as 31
calls the original "returned without". Fuzzing missed it, since its callees
return either way; `ct_fuzz --stop-at-call` compares the machines, the stack
pointer included, at the call. Two
practical notes from the same work: the report describes 16 mismatches and,
apart from them, 16 notes (abandoned comparisons, timing), mismatches first
(`ct_dev --shadow-details N` for more), so a count is still the thing to
watch; and timing calibration rewrites every measured row, so a rewrite puts
back the rows of subsystems it didn't touch.

**The bridges' vocabulary** is `game/field/bridge.h`'s, for the field's
bridges and the event commands': `bridge_call(s, site, routine, dont_care,
machine)` (and `bridge_call_long`) make the call with its don't-care set and
go on only where the callee came back as `machine` says the bridge runs (8-bit
A, the index registers' width, the direct page and the data bank); otherwise
they stop at the call's return, where the original carries on. `bridge_back`
is the same test after a call made another way (a service's call-out, a
sibling bridge called as the original's JSR would). `tools/dontcare_check.py`
reads `bridge_call` as it reads `m_jsr_dc`, with the arguments where it is
called.

If a callee unwinds past its return address (discarding its caller's frame),
the native routine is abandoned, since the original's continuation never runs
either. The report counts these.

A port doesn't have to run to the end. It can stop at any instruction boundary
in the original by setting PC (and PB) there and returning. The interpreter
carries on from there, and `shadow` checks that the original reaches that
point with the same stack pointer, at the first pass where the machines are
the same (a loop's head is fine). That covers routines that end by jumping
rather than returning, and states a port doesn't handle yet.

Save states can't capture a routine suspended in a call-out; `ct_dev` warns
when `--save-state` is used while one is.

### What shadow mode costs

Every entry to a ported routine, and every return from one of its call-outs,
brings a copy of the machine up to date and later compares it with the
original, so a movie makes millions of them (`gato-battle` 1.1 million in its
first 10000 frames). Copying the whole 289 KB machine and comparing 200 KB of
memory byte by byte each time was most of a shadow run: a sampling profile of
that stretch put 74% of the time in the comparison, 8% in the copy and 2% in
the renderer. What it does now:

- **Copies are kept.** `native.c` keeps one machine copy per nesting level and
  refreshes it with `snes_clone_since` rather than allocating a new one per
  call.
- **Only written pages move.** The bus and the PPU stamp each 256-byte page of
  work RAM and VRAM with the machine's write epoch when they write it
  (`SNES_TRACK_PAGE_BITS`, `bus.h`). A refresh copies the pages either machine
  wrote since the copy's last refresh, and everything outside those two
  memories (90 KB, most of it sound RAM); the comparison looks at the same
  pages, and all of SRAM, CGRAM and OAM. Nothing else writes those memories
  while a movie runs; what changes them another way calls
  `snes_mark_all_written`, which counts every page as written so the next
  refresh and comparison take all of them. A state load does it itself
  (RUNTIME.md, "Save states"), as do the ZSNES import and `ct_drive`'s pokes.
  `native_compare`, which `ct_fuzz` uses on machines it fills directly, still
  compares everything.
- **`memcmp` before the byte loop,** which now only runs to describe a
  difference.
- **No drawing in the suite's runs.** `ct_dev` draws only when something reads
  the pixels (RUNTIME.md, Renderer).

Checked: a build that also made the full copy and the full comparison at every
refresh and compare, and stopped on any difference, ran the whole suite (113
million refreshes and as many comparisons) without one. From a state: a
`gato-battle` shadow run saved at frame 10000 and one resumed from that state
to frame 21600 add up to the straight 21600-frame run, routine by routine
(calls, matches, interruptions, skips; 289 routines called after the load, none
mismatched), except one generated block in flight at the cut, and their
coverage and call files together are the straight run's. The suite's 26 shadow
reports, coverage files and call files are byte-identical before and after, as
are `new-game`'s `--native off` and `--recomp only` traces, framebuffer hashes
included.

CPU time of single shadow runs with the suite's options, the old and new
builds running side by side:

| Movie | Frames | Before | After |
|---|---|---|---|
| `gato-battle` | 21600 | 222 s | 30 s |
| `saves/battle-magus` | 18000 | 176 s | 29 s |
| `saves/late-omen-zeal` | 36922 | 351 s | 47 s |
| `saves/endings-bucket-lavos` | 196000 | 1742 s | 285 s |

Per frame the endings cost what the other movies do (1.3-1.6 ms now, 9-10 ms
before); they take longest because they are longest, the three together
434000 of the library's 763000 frames, and most of that the fights and credits
they exist to cover. The whole suite at `-j 2` went from 84 minutes to 9-11,
on a machine other work was also using (the old build's run shared it with
more). `suite.py` now starts the longest movies first, so a long one started
late doesn't run on alone at the end. What remains is the shadow machinery
itself: recording every bus access and replaying the generated blocks against
it, the interception at every instruction, and the copying and comparison per
call, besides running the machine.

## From a draft to a port

The workflow for converting generated code into ports (PLAN.md, Phase 3). The
draft tool and how good its drafts are is [RECOMP.md](RECOMP.md), "Cleanup into
hand ports (M5)".

1. **Pick.** `python3 tools/burndown.py --uncovered C1 --top 20` lists the
   largest runs of a bank no port covers, with the routine each starts in; pick
   by subsystem, cheapest and best understood first, next to ports that already
   exist. A helper or handler a port inlines (no call-out at its call) is
   credited to that port without a registry entry of its own, and a port's
   code ends where it hands back (its tool's docstring says how it reads that).
2. **Draft.**

   ```sh
   python3 -m tools.recomp.show C1:423A C1:44B0 C1:4512 > <scratch dir outside the repo>/drafts.c
   ```

   Ask for every routine of a chunk at once: the analysis is solved once for
   the whole run and is nearly all of its time, so four drafts cost the same 27
   seconds as one and seventeen cost about the same again.

   It reads `traces/coverage-*.txt`, `traces/saves/cov-*.txt` and the matching
   call logs (`--coverage`/`--calls` to change them, `--root` to look for them
   in another checkout), so refresh those first when the movies reach code they
   didn't (step 1 of "Writing a port"). Where the analysis enters the routine
   with more than one set of widths, the registry entry's precondition and then
   the widths the coverage saw it run with pick one; if several are left it
   prints a draft for each (named `_m8_x16` and so on), and `--m`/`--x` choose.
   `--d`/`--db` give D or DB when you know them and the tool doesn't, `--page`
   a page with no fixed D, and `--asm` shows each instruction as a comment
   above its statements. The draft is ROM-derived: keep it outside the
   repository.
3. **Read the header.** It says what the routine is (a script command's handler
   is named after the command), the widths, D and DB it assumes and why, what
   callers read after it returns, where it hands back to the interpreter (wait
   loops, other ports' entries), which I/O registers it touches and which
   machine state it sets. Those are the parts a port has to decide about.
   Work RAM is named from the map for the routine's subsystem (in the banks
   several share, `$C3`, `$C6`, `$FD`, the one its callers work for), and a
   direct-page cell only by a variable the map puts on that page: the others
   are `SCRATCH_01DB`-style names the draft defines, the direct page being
   where each mode keeps its scratch. Name the ones that mean something in
   `re/ram.toml`, with their `page`.
4. **Check the draft, if in doubt.** `python3 -m tools.recomp.draftcheck --out
   <scratch> C1:423A` registers the draft in a scratch tree outside the
   repository (a registry entry's name works too, and replaces that port);
   built there, `ct_dev --native shadow` checks it like a port, so a question
   about what the original does is answered before any of it is rewritten.
5. **Write the port** in `game/<subsystem>/` from the draft's logic function:
   name it and its locals by what they hold, give the parameters and results
   their meaning, turn register writes into platform calls
   ([PLATFORM.md](PLATFORM.md)), fold call-outs to routines you port as well
   into C calls, drop the flags and registers the callers turn out not to need
   (the header's don't-care set is what liveness proved), replace the stubs
   with the call-outs the port still makes, and add what you learned to
   `re/ram.toml`. Keep the wrapper's shape: the logic apart, the registers at
   the edge (CLAUDE.md, "Writing a port from generated code"). The draft's
   `// $BB:AAAA` stays; nothing else mechanical does. Code ported from the
   sound CPU (`game/sound/`, [SOUND.md](SOUND.md)) has no bank: it carries
   `// SPC $AAAA` instead, with several addresses where one function stands
   for several of the original's routines, and its command and event tables
   name each row's handler the same way.
6. **Register, validate, fuzz, calibrate**: steps 4-7 of "Writing a port"
   below. The header's registry line is the starting point, with its guard if
   the draft assumed D or DB from its callers.

`python3 -m tools.recomp.groundtruth BB:AAAA` compares a draft with the port of
the same routine; it is how the tool is measured, and a quick way to see what a
port changed from the mechanical version.

What the $C7 pilot added (RECOMP.md, "The $C7 pilot", has the measurements):

- **Use the draft as a reference, not as text to edit.** What carried over was
  its header (widths, D and DB, what callers read, what each callee takes), its
  RAM names and its control flow; the logic was written from what the routine
  does. The draft keeps flags and stack bytes exact because shadow mode used to
  compare them; with the rules in Modes above, a port leaves out flags nobody
  reads, the frames of callees it inlines, and the P bytes a callee saves and
  restores, and says so: `m_jsr_dc` or `au_driver_call` with what the callee
  doesn't take, `m_stop_at` with what is dead where it stops, the entry's
  don't-care set for what is dead wherever it finishes, and `m_stop_dont_care`
  at the return instruction for what is dead after the return alone. Write
  them as constants, or from a helper's parameters, locals or a table in the
  file, so `tools/dontcare_check.py` can read them.
- **Decide per exit.** A routine that stops at several instructions of the
  original (the sound driver's wait loops) leaves different registers live at
  each; a table of the exits with the registers each needs and the ones it
  leaves dead is shorter than the draft's per-path code.
- **Keep what the game can see**: scratch cells the original writes on the way
  (index arithmetic, list pointers) are compared like any other work RAM, so
  the port writes them; and indexing the way the original does, 16-bit
  wrap-around and odd offsets included, since fuzzing reaches them (an
  accessor takes the original's index, not a cleaned-up one).
- **A frame that is live across a call-out is pushed** (see Call-outs), and a
  callee the port inlines isn't registered on its own if it contains a
  call-out the port makes.
- **Fuzz entries guarded on a page with it set**: `--d` and `--db`.

## Writing a port

**The bar, before the steps.** A port is a reimplementation of a routine, not a
translation of it. The gate measures observable behaviour; nothing requires the
port to reach that behaviour the way the ROM does, and a good one usually
doesn't. The test to apply to a finished port is whether someone who has never
seen the ROM can read it and say what the game does.

The failure mode has a shape, and most of `game/` written before this bar
existed has it: a straight line of machine operations with the original's
variables spelled as addresses.

```c
/* Transcription -- passes the gate, decompiles nothing. */
m_lda16(s, 0xE000);
m_write16(s, m_abs(s, 0x0740), c->a);
m_ldx(s, 0x0740);
m_ldy(s, 0x0742);
m_lda16(s, 0x013D);
m_mvn(s, 0x7E, 0x7E);
```

```c
/* The same work, reimplemented: the fill is one named operation on a named
 * buffer, and the reader never simulates a register. */
oam_hide_sprites(s, MENU_FIRST_FREE_SPRITE, SPRITES);
```

Getting there means writing the vocabulary the port needs rather than working
without it: a local helper in the port's own file, a shared one in the
subsystem's header, a service call in `runtime/platform/` when it is the host,
and a named accessor (`sound_data.h`, `scene_data.h`, `screen_data.h`,
`field_data.h`, `palette_data.h`) when it is ROM data. `m_*` and the `Cpu`
struct belong in a port's thin entry wrapper and in what has no better
expression yet -- not in its body. A port that can't be expressed through a
service is a finding: add the call (additively) or document the escape, as
PLATFORM.md's "as built" sections do.

**The game's own state is a value, not an address.** A port's logic takes a
`GameState *` and reads and writes the game's variables through `game/vars.h`,
generated from `re/ram.toml` (DISASSEMBLY.md, "The RAM map"): `scene_circle_radius(g)`,
`set_field_object_facing(g, slot, v)`. It never names an address, a stride or a
`Snes *`; the machine appears only in the registered entry's wrapper, which
turns the `Snes *` into a `GameState *` with `state_of` and translates the
original's registers, flags and direct page. `game/scene/circle.c` is the
smallest example of the whole shape. The accessors are backed by emulated work
RAM today, so a port that uses them writes exactly the bytes it wrote before and
shadow mode still compares them.

`python3 tools/portstyle.py` measures this, per file and per subsystem, and the
gate holds new and rewritten ports to it. `.claude/skills/port-routine/` is this
workflow packaged for an agent: the same bar with worked examples, the
self-check to run before reporting done, and the worktree rules a wave needs.

1. Refresh the analysis with runtime call edges, so liveness knows every caller:

   ```sh
   build/main/ct_dev --rom roms/ct-usa.sfc --frames 7200 --movie oracle/movies/new-game.ctm        --coverage traces/coverage.txt --calls traces/calls.txt
   python3 -m tools.disasm --coverage traces/coverage.txt --calls traces/calls.txt
   ```

2. Start from the draft (`python3 -m tools.recomp.show BB:AAAA`, above), and
   read the original with `python3 -m tools.disasm.linear BB:AAAA` and the
   listing where the draft leaves a question. `disasm/out/functions.json` has
   the M/X widths it's entered with, and `disasm/out/liveness.json`
   `dead_after_return`: the registers and flags no caller reads after it
   returns, `dont_care` the same as a registry entry writes it (the draft's
   header says both).
3. Write readable C with the same observable effects: memory writes (including
   stack bytes from push/pull), I/O writes, and every register and flag that
   isn't dead after return. Use the data bank and direct page as the original
   does (`m_abs`, `m_dp`), not hard-coded banks. Spell work RAM with the
   names in `game/ram.h` (`RAM_ABS(name)` where the original reaches it through
   the data bank, `name_DP` through the direct page), and add the variables the
   routine establishes to `re/ram.toml` ([DISASSEMBLY.md](DISASSEMBLY.md), "The
   RAM map"). Reach PPU, APU, DMA, HDMA and
   CPU registers (other than the multiplier and divider) through the platform
   services (`m_platform(s)`, [PLATFORM.md](PLATFORM.md)); `tests/test_platform_lint.py`
   checks it. Call routines that aren't
   ported yet with `m_jsr`/`m_jsl`, giving the original call site. Registers
   and flags must be exactly what the original has at that point (see
   Call-outs).
4. Register it in `game/registry.c` with its entry precondition (for example
   8-bit A and binary mode) and its don't-care set, so calls under other
   conditions fall back to the interpreter. A guard function must only peek at
   memory: it runs on the real machine, and a bus read there costs time and
   sets open bus. A guard that read `$CD02` through the bus made shadow runs of
   the fight drift from interpreter-only runs within a few hundred frames; with
   peeks the two produce identical traces.
5. Run `--native shadow` over the **whole** movie library. Every call must
   match. Then run `--native on` and compare frames with the interpreter-only
   run. A subset is for iterating, never for landing, and that holds for a
   rewrite of a working port as much as for a new one: a wave-2 rewrite of the
   object dispatcher passed eight movies and then failed on the full library,
   because it no longer left the address it stopped at in A. A shadow run's
   `interrupted` count is not noise either: those calls were not compared, so
   an entry with a non-zero count has paths only fuzzing will check.
6. Fuzz it with `ct_fuzz` (below), especially paths the movies don't reach.
   This is part of done, not optional: generated code is correct on every path
   by construction, a hand port only on the paths the movies and fuzzing reach.
   Record the states and counts in the port's table row, as the rows below do.
7. Set `cost_milli` from the measurement: run the suite (its shadow runs carry
   `--timing-report`), then `python3 tools/timing_gate.py
   build/suite/shadow-*.log --calibrate game/registry.c` sets every measured
   entry to the value its totals imply (the original's time over the port's
   bus clocks, DMA left out of both). The suite's timing check then holds the
   port to the gate's limits; a port whose time follows its data in a way one
   scalar can't may need an entry in `tools/timing_exceptions.txt`, with the
   reason its error doesn't reach the budget the limit stands for.

   A port that runs other routines' work as C instead of calling out to them
   (a dispatcher running its handlers, a routine inlining its subroutines)
   spends its time on whichever of them ran, which one scalar can't fit.
   Name the routine whose work each part is, by its address:

   ```c
   NATIVE_TIME_AS(s, 0xCFEDB8, draw_underlays(s));   /* $CF:EDB8's time, not the pass's */
   int was = native_time_as(s, type_handlers[type]);   /* or around several statements */
   x = run_type(s, slot, type);
   native_time_end(s, was);
   ```

   The bus clocks in between are then estimated with the `cost_milli` of the
   hand port registered at that address, which shadow mode calibrates on that
   routine's own calls (the original runs it as a routine, so each of its
   calls is measured by itself), and the rest with the entry's own. The routine
   must be registered as a hand port; the timing report names an address that
   isn't, and the gate fails on it (a literal one is also caught by
   `tests/test_timing_gate.py`). The address is a tag, not a time: nothing in
   `game/` says how long anything takes. `--calibrate` works out the handlers
   first and the dispatcher's own part from what they leave (RECOMP.md, "The
   timing gate", "Dispatchers").

   A port that does in one plain step what the original does at length -- a
   block move done as a fill, a search done in locals -- must not keep the
   original's shape so that it gets timed. It charges the time instead, as
   scaffolding (`game/charge.h`, backed by `runtime/port/state_io.c` and
   `native_charge_as`): `ORIGINAL_EFFECT(charge_block_move(g, bytes))` after a
   plain `wram_fill8`/`wram_fill16`/`wram_copy` (`game/wram.h`, whose accesses
   take none of the port's own time), estimated at the block move stub's
   calibrated rate; or `ORIGINAL_EFFECT(charge_as(g, 0xBBAAAA, accesses))`, that
   many memory accesses of a registered routine's work, the running port's own
   included (the scene's divisor search charges its passes that way). A native
   build has none of it.

Liveness is only as good as the call graph. Functions called from code the
analysis can't see (RAM code, unresolved indirect jumps) keep everything live,
which is why step 1 feeds in the runtime call log. Rerun it when movies reach
new parts of the game, before relaxing a routine further.

## Scaffolding

Some of what a port does is there only because the original does it and
something that isn't the game still looks: a scratch cell the original writes
and nothing reads again, which shadow mode compares; a register or flag the
original leaves and liveness can't show dead, so no don't-care can drop it; a
cell or register the original's still-generated code expects filled. That is
scaffolding. It stands while the gate is byte-exact and comes down when the
subsystem retires the gate (PLAN.md, "Retiring the gate, deliberately").
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
  library (`ct_dev --event-reads` is event's instrument; RECOMP.md, "The event
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
  (RECOMP.md, "The tooling pass after Phase 3's first chunks").

- **Run, in the native shape.** `ct_dev_native_shape` and
  `ct_drive_native_shape` are the ports with their scaffolding compiled out,
  and the gate runs them over the movies and routes in the shipping mode
  (tools/suite.py's `native-shape` check; RECOMP.md, "The fallback
  measure"). What the original's still-generated code reads of scaffolding
  is listed by `ct_dev --native on
  --scaffold-reads original` and `tools/effects_check.py --originals`, with
  the source of each write: a cell there is one the native build needs the
  reader ported or the write made plain for, and it is what a failure of the
  native shape is traced through. A failure the gate lets stand is a line in
  `tools/native_shape_exceptions.txt` with its trace.

## A port that spans frames

A routine whose original takes longer than a frame (the load screen's drawing,
`$C2:E7C3`, about 1.2 frames) is ported like any other. In the shipping mode
its work is done at once and its time owed at the boundary where it stopped;
the interrupts that fall due while the time is paid are taken there, each
handler seeing the finished work (a timing difference, as with any port), and
the debt waits for a handler wherever it moves the stack (RECOMP.md, "The
tooling pass after Phase 3's first chunks"). What such a port must still do
is stop where the original yields: a task switch or a wait for the next frame
partway through is a stop (`m_stop_at` at the yield's address) or a call-out,
never something run through. Shadow mode compares none of its calls when every
one is interrupted, so fuzz it. To see what the shipping mode does with a
routine as one port before writing it, run the original that way:

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --movie <movie> --frames N --native on --as-port C2E7C3 --shot-every 100 --shot-dir <dir>
```

`--as-port BBAAAA[,...]` (`dev/as_port.h`) runs the routine all at once with
time stopped and interrupts held, and owes its bus and internal clocks after
it, as a port would with a `cost_milli` of 1000. It gives up on a routine that
yields (2 million instructions without a return).

## Fuzzing

Shadow mode only checks the states the movies reach. `ct_fuzz` checks a
registered routine against the original on random states:

**Rewriting a port needs the predecessor's numbers, not just good ones.** A
rewrite can reorder memory accesses in a way no movie exposes: the accessors
pilot hoisted a table's bank read out of a loop, which moved it before a write
that used to precede it -- identical for every table the game actually uses,
different when the table overlaps the cell being read. Shadow mode over all 26
movies saw nothing, because the library never aims a buffer at its own cells.
What found it was fuzzing the rewrite and the port it replaced with the **same
seed and the same constraints** and comparing the counts: 262 against 256. An
absolute count says nothing on its own, since the predecessor has residual
mismatches of its own (here, the original returning into its own output); what
matters is whether the rewrite's are the same ones. So a rewrite records both
counts in its row, and the comparison is part of its definition of done.

`tools/fuzz_compare.py` does it for a whole subsystem at once:

```sh
# a scratch build of main, outside the repo, for the predecessor
python3 tools/fuzz_compare.py --before <scratch>/build/main/ct_fuzz --after build/main/ct_fuzz \
    --ct-dev build/main/ct_dev --subsystem scene -j 3 [--routine 'scene_race_*'] [--variant loose]
```

It runs both builds on every routine with the same state, seed and
constraints, at most `-j` processes at a time, prints each routine's two counts
(and the states each skipped, the guard's among them, since a guard the
rewrite added turns states away rather than mismatching), and exits non-zero
when any mismatch count differs. An entry the `--before` binary doesn't have
is run on `--after` alone and reported `new` with its own counts, which don't
change the verdict; without `--before`, every routine is run that way, which
is how a chunk of new ports goes through the constraints file. The routines and their constraints are in
`tools/fuzz_routines.toml`: one entry per registered routine, with its
`ct_fuzz` options and the reason for each, and the base states named by movie
and frame, which it makes with `--ct-dev` the first time and keeps in
`traces/fuzz/`. A routine listed again with `variant = "loose"` and fewer
constraints is where a reordered access shows, since that is where buffers and
cursors land on each other; it is also where a rewrite that names a buffer
finds out how far the name reaches (the scene's sprite writers, RECOMP.md,
"Phase 3a: the scene subsystem off the machine"). A subsystem's rewrite adds its
entries from the constraints its rows record, which makes them exact.

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --frames 11400 --movie oracle/movies/field.ctm --save-state traces/field.state
build/main/ct_fuzz --rom roms/ct-usa.sfc --load-state traces/field.state --routine event_cmd_if_var_const \
  --iterations 50000 --range Y=0000-7FFF
```

Each iteration randomizes A, X, Y, the arithmetic flags and all of work RAM
except the stack page, applies the entry precondition, and calls the routine
as a JSR would (`--long` for JSL, `--interrupt` for interrupt handlers, which return with RTI). The entry's
guard is asked with the return address pushed, as the runtime asks it, since a
guard that reads work RAM can read the stack (the event handlers' operands can
land there). The original runs on the interpreter, and the
native routine on a copy with call-outs interpreted in place. The machines are
compared where the native routine stopped: normally the return, or the
instruction where it handed back to the interpreter, at the first pass of the
original through it where they are the same (as in shadow mode, a loop's head
is fine; a port the same at no pass is reported with the first pass's
difference). A mismatching state is described in full (the first 10, `--details N` for more): every register and
flag that differs, then every run of differing bytes in work RAM, SRAM, VRAM,
CGRAM, OAM and the I/O registers, grouped, with the original's and the port's
bytes side by side:

```
iteration 453: WRAM[1DC7] original 73 native 42
  WRAM 7E1DC7-7E1DCD: 7 byte(s), 2 differ
      original 73 FC 99 21 08 00 C0
        native 42                BD
  (2 difference(s) in all, 1 memory range(s))
```

**Budgets.** Random RAM sends loops into places they never end, the
original's and the port's alike, so each side's work on a state is bounded,
both counted the same way: in bus accesses, the port's own and every
instruction the interpreter runs for its call-outs, and the original's. The
port may do `--budget` of them (default 2000000); past that it is abandoned
wherever it is, in a call-out or in its own C loop (it runs on its own fiber,
and the bus counts its accesses: nothing else can stop a loop in C). The
original may do the budget plus 16 times what the port did: it fetches every
instruction, and loops through what a port does in a few accesses (a fade over
65536 colors takes it about ten times the port's). So a port that runs long
natively is still compared at the default. The outcomes the report counts:

- the port ran past its budget in its own code, and the original returns within
  a quarter of it: **a finding**, counted with the mismatches ("the port ran
  past N bus accesses; the original returned after M"). A correct port makes
  fewer accesses than the original, so it would have finished;
- the port ran past its budget and the original runs long too: skipped;
- a call-out ran past the budget: skipped (random RAM sends callees into
  endless loops). A call-out that never returns on its stack by design, a
  menu task's yield (`$C2:821E`), would skip every state: `--stop-at-call
  BBAAAA` ends each state's comparison at the port's call-out to that routine,
  comparing the machines at the callee's entry with the call's don't-care set;
- the port returned and the original didn't within its budget: skipped (and
  reported as "original never reached" when the port stopped somewhere other
  than the return).

`--step-limit N`, the old spelling, is `--budget 4N`.

Keep random values inside what the game can produce when the difference
matters. Past the end of a data area, indexed reads reach I/O space, where open
bus holds the last byte on the data bus. The interpreter's instruction fetches
set it and native code doesn't, so those reads differ in ways real game data
never exercises. `--range` limits a register (script offsets, for example), and
`--keep` leaves a WRAM range as it is in the base state. `--fill BBAAAA-BBAAAA=lo-hi` gives a WRAM range random bytes within limits, for example a pointer's bank byte in script operands.

An area need not be every byte of its range, nor a fill a range of bytes:

- `AREA:STEP` takes the elements that start every STEP bytes from the area's
  first, for one field of each record in a table (`7E1100-7E11FF:10=00-07`);
- `=V,V,...` picks each element from the values listed, as wide as the widest
  written (two hex digits a byte, four a word, low byte first), so a script's
  place or a record's offset is one of the real ones
  (`[7E006D]+2,2=0010,0200,0350`);
- `=@BBAAAA` copies the bytes at BBAAAA, as the state has them once every
  other area is applied (ties go last), for two cells the game keeps equal
  (`7E0042-7E0042=@7E0040`).

A command takes up to 32 areas.

Both also take an area at a pointer the state holds, for the fields of an
object slot or a record wherever the random state puts it:
`[BBAAAA]+OFF,COUNT` is COUNT bytes at the 16-bit pointer in the cell at
BBAAAA, plus OFF (`-OFF` back), in the cell's bank (wrapping in it), or the
bank a `BB:` before the bracket names; `[X]`, `[Y]` and `[D]` take the
pointer from a register. Hex throughout. Areas at pointers are applied last,
after the absolute ones and the registers, in order, so the pointer can be
filled first and the fields it reaches after. The scene engine's irises read
the current object's slot at `$4E`; where their first fuzzing pinned the slot
at `$0B30` to keep its fields in range, the slot can now be anywhere:

```sh
build/main/ct_fuzz --rom roms/ct-usa.sfc --load-state traces/field.state --routine menu_object_squashed_iris_1 \
  --d 0000 --db 7E --fill 7E004F-7E004F=0B-1A --fill '[7E004E]+15,1=00-00' --fill '[7E004E]+19,1=00-00' \
  --fill '[7E004E]+22,1=02-FF' --fill '[7E004E]+23,1=00-00'
```

(the slot in `$0Bxx`-`$1Axx`, its X, Y and radius's high bytes 0 and the
radius 2 or more: 5000 of 5000 match; with the slot random and its fields
not, half mismatch, on field values outside what that first fuzzing kept
them to).

`--db BB` sets the data bank, which otherwise comes from the base state; entries
guarded on a data bank that reaches the I/O registers need it when the state's
is `$7E`. `--d HHHH` sets the direct page the same way, for entries guarded on
their subsystem's page. `--widths M,X` (8 or 16 each) sets the widths the
routine is entered with, for an entry whose callers come both ways: the
base state otherwise fixes them, and a fuzz that never enters 8 bits wide
never sees what a wide port leaves in the index registers' high bytes (the
menus' services, found by the New Game+ movie). The comparison leaves out what shadow mode does: the
routine's don't-care set, what it declares dead where it stops, and the stack
bytes the original pushed and pulled again; and a stop somewhere the routine
didn't declare is a mismatch, as in shadow mode.

The SPC700 doesn't run in `ct_fuzz`, so a routine waiting for the sound driver's
echo on an APU port would never finish; `--apu-loopback` makes the ports read
back what the CPU last wrote to them, which ends every echo wait at once. Idle
entries can be fuzzed too: the original runs one pass of the loop, until it is
back at the loop head or at the exit the native pass took.

Event commands index object tables by the current object's slot in `$6D`; a
random slot reaches I/O space the same way, so fuzz them with
`--keep 7E006D-7E006E` as well.

Passing so far: the event control flow commands (50000 states each, two
seeds, `--range Y=0000-7FFF`), the event movement commands (10000 each, also
keeping `$6D`), `display_blank_and_stop_interrupts`,
`field_install_nmi`, `field_install_irq` and `field_update_object_visibility`
(20000 each). The three 16-bit and variable-vs-variable conditionals
(`$13`-`$15`), which no movie reaches yet, are validated only this way. Not
fuzzed: `decompress`, whose random source and destination pointers make it
overwrite its own stack and variables (it needs generated streams), and
`field_nmi`, which returns with RTI.

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
`--native off --recomp off`) also records X at each dispatch over the library,
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
The effect worked out for an enemy's action hands the action's showing
(`$C1:BFA4`) and the battle's step (`$C1:0003`) an overflow flag they only
save and restore across their frame waits (RECOMP.md, "Phase 3, battle chunk 2: an effect worked out"), which is
the case it was made for.

The third form is an entry's own carry after it returns, where liveness
follows it out through the original's callers to a reader whose result
nothing then acts on. Fuzzing compares only up to the entry's return, so it
can't show that; the library can. The note sits in the comment block just
above the entry in `game/registry.c`, beside the reader named:

```c
/* ...the reader, and why nothing acts on what it reads...
 * dc-evidence: ct_dev --poison-return BBAAAA --poison-set C over the library:
 * N returns, C inverted M; 0 differing */
```

`ct_dev --poison-return BBAAAA` (with `--native on`) scrambles, wherever the
port registered there returns to the original's code in the shipping mode, the
registers its entry declares don't-care (only those `--poison-set` names), and
prints how many returns had the carry inverted. The check takes the note where
liveness has the carry alone read after the entry returns, the entry named,
C in the set, no trace differing and at least 100 returns inverted;
`tools/dc_evidence.py` makes the run again beside the other two, and compares
its trace with the unscrambled one's. The menu's frame (`$C1:1153`) was the
case: its carry runs out through the frame step into a battle message's frame
waits, and the first multiply of the party's statuses cleared after the
message takes it into a product's high word the next pass clears
(RECOMP.md, "Phase 3, battle: the player choosing").

## Status

Every registered port has a row in [ports.tsv](ports.tsv): the routine, its
address, and what validating it found -- the shadow counts, the fuzz runs, and
each rewrite it has been through. It is a ledger, not prose, and it is long, so
it lives as a file to `grep` rather than a table to read:

```sh
grep -P '^field_install_nmi\t' docs/ports.tsv       # one port's history
awk -F'\t' '$3 ~ /interrupted/' docs/ports.tsv      # what the library never compared
```

A new port appends its row in the same commit as the port, in the same form:
the entry's name as `game/registry.c` registers it, its `$BB:AAAA` address, and
one sentence per validation -- the calls shadow mode compared, the fuzz run and
its counts, and a note for any rewrite since.

### What validating the first ports showed

The validator catches injected bugs: a wrong flag and a missing stack write
were each reported with the exact field. The first decompressor draft failed
all 54 calls on one detail, flags set by the final `PLD`/`PLB`, and the report
pointed straight at it.

With all five running natively over the new-game movie, 5108 of 7200 frames
are identical to the interpreter-only run at the same frame. Almost all of the
rest (2088) are the same frames one frame earlier, from approximating the
decompressor's time cost.

`on` mode's time accounting is approximate for hand ports, so input can land on
different frames than in an interpreter-only run. The new-game movie still
plays identically. In the field movie, where walking is sensitive to that, 8658
of 11400 frames also appear in the interpreter-only run. Use shadow mode, not
`on` mode, to judge correctness. (Generated blocks alone are exact; see
RECOMP.md for what making hand ports' time more exact did and didn't change.)

A first attempt at the event script loop that runs each object's init script
(`$C0:58DE`) predated suspending call-outs. Back then a call-out interpreted
its callee on the spot with no time passing, so a handler waiting for a frame
spun forever. With call-outs running in the system loop, the same port matches
on every call. Shadow mode caught its one bug at the first call-out: `ASL A`'s
result wasn't stored back into A before calling a handler.

The object visibility port first failed 7108 of its calls on HDMA's line
counter and then 41 on HDMA-written PPU registers: state that moves with time
while the original runs. Both are now excluded from comparison for all
routines.

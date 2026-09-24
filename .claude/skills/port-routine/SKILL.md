---
name: port-routine
description: Convert generated C from the ROM into hand-written ports, or rewrite an existing transliterated port to the reimplementation bar. Use for any Phase 3/3a porting chunk in game/ — a subsystem, a bank slice, or a single routine.
---

# Porting a routine to the reimplementation bar

You are writing a **reimplementation**, not a translation. The gate measures
observable behaviour; nothing requires your port to reach that behaviour the way
the ROM does, and a good one usually doesn't.

**Two tests to apply to your own work:**

1. Could someone who has never seen the ROM read this file and say what the game
   does?
2. Could this logic run on a machine that is not an emulated SNES? A function
   taking a `Snes *` cannot, however well it reads — and 98% of `game/` does,
   including files that score well on style. The machine belongs in a thin
   wrapper at the registered entry; the logic belongs in functions that take the
   game's own state and plain C values. Game state is reached through the typed
   accessors generated from `re/ram.toml`, never by address.

Read `CLAUDE.md`'s "Writing a port from generated code" and `docs/PORTING.md`'s
"Writing a port" before writing code. This skill is the workflow around them; the
docs are the source of truth and win any disagreement.

## The trap, concretely

Every port is pulled toward machine terms by a real force: chaining the machine
helpers gives you the flags for free, and the gate compares flags. Writing plain
C means also establishing which flags are dead. Take that extra step — it is the
whole difference between a decompilation and a transcript.

```c
/* Transliteration. A 32-bit add, as the 65816 does it. Passes the gate. */
m_set_flag(s, CPU_FLAG_C, false);
m_write16(s, at(s, to, slot),
          m_lda16(s, m_adc16(s, m_read16(s, at(s, to, slot)),
                             m_read16(s, at(s, from, slot)))));
m_write16(s, at(s, to + 2, slot), m_lda16(s, m_adc16(s, ...)));
```

```c
/* The same add, reimplemented. The flags it doesn't set are declared dead. */
slot_write32(s, slot, to, slot_read32(s, slot, to) + slot_read32(s, slot, from));
```

```c
/* Transliteration: a fill, spelled as the instructions that do it. */
m_lda16(s, 0xE000);
m_write16(s, m_abs(s, 0x0740), c->a);
m_ldx(s, 0x0740); m_ldy(s, 0x0742); m_lda16(s, 0x013D); m_mvn(s, 0x7E, 0x7E);
```

```c
/* Reimplemented: one named operation on a named buffer. */
oam_hide_sprites(s, MENU_FIRST_FREE_SPRITE, SPRITES);
```

(`slot_write32` and `oam_hide_sprites` are sketches, not existing functions:
they are the kind of thing you write as you go. See "write the vocabulary".)

Two real files from the same week, written to the same rules:
`game/menu/map.c` has **1** such construct, `game/menu/object_script.c` has
**68**. Both pass the gate. Only one is a decompilation.

## What "write the vocabulary" means

When a port has no good way to say something, the abstraction is missing — write
it, don't work around it:

- local to the routine → a static helper in the port's own file;
- shared in the subsystem → the subsystem's header;
- it touches the host (video, audio, timing, input, saves) → a call in
  `runtime/platform/`, added additively. A lint test enforces that no PPU, DMA,
  HDMA or APU register number appears in `game/`;
- it reads ROM data → a named accessor (`sound_data.h`, `scene_data.h`,
  `screen_data.h`, `field_data.h`, `palette_data.h`), never
  `m_read8(s, 0xD15E10 + i)` mid-routine. Where MD2 typed the resource
  (docs/MATCHING.md), name the accessor after that type.

`m_*` and the `Cpu` struct belong in a port's thin entry wrapper and in what
genuinely has no better expression yet — not in its body. A port that can't be
expressed through a service is a **finding**: add the call, or use the documented
SNES escape and write down why, as docs/PLATFORM.md's "as built" sections do.

## The shape to aim for

`.claude/skills/review-port/SKILL.md`, "What hand-written looks like", has a
real file, annotated: field objects' animation (`game/field/animation.c`) as a
small module, with the original's in-band encodings confined to its storage
functions, operations named for what the game does, and a script command whose
opcode family is table rows. Read it before you write anything; it is the same
concept as the "before" examples below.

## The second trap: memory mechanics instead of register mechanics

Taking the machine out of a port is not enough. The event rewrite scored 0.02
ops per line with no `Snes *` in its logic, and it **still read as mechanical**,
because the accessors took the addresses out and left the original's structure
in. This came from `game/event/animation.c`:

```c
/* Translation with better names: four stores, in the original's order. */
static void start_mode(GameState *g, unsigned object, uint8_t mode) {
    set_field_object_animation_mode_mode(g, object, mode);
    set_field_object_frame_delay(g, object, 0);
    set_field_object_move_state_animation_count(g, object, 0);
    set_field_object_animation_frame(g, object, 0);
}

// $C0:2E67
/* $AA: loop animation (operand 1). */
static EventStep loop_animation(GameState *g, const EventCommand *c)
{ loop(g, operand(g, c, 1)); return next_command(c, 2, 1); }

// $C0:2E87
/* $B3: loop animation 0. */
static EventStep loop_animation_0(GameState *g, const EventCommand *c)
{ loop(g, 0); return next_command(c, 1, 0); }
```

What makes that mechanical, and what to do instead:

- **The accessors are the storage, not the vocabulary.** Model the game's concept:
  an object's animation is a state machine (still, looping, playing a one-off
  for N frames then going back, holding a frame). Write it as a module with
  operations in game terms, such as `object_loop_animation(g, obj, anim)` and
  `object_play_animation(g, obj, anim, frames)`, and call those. The per-byte
  writes the gate still needs go **inside** the operations, in one place per
  concept, and that is where the storage moves when the gate retires. A
  sentinel the original stores in-band (`$FF` for "nothing to go back to")
  becomes a named state inside the implementation.
- **One function per ROM routine is a smell.** `loop_animation_0` exists only
  because the engine dispatched through the ROM's handler table with an emulated
  `JSR`, so every opcode had to be its own function reachable by address. `$B3`
  is a one-byte short form of `$AA` with the animation fixed. Likewise
  `play_animation`/`play_animation_for` (`$AB`/`$B7`) are one operation with the
  frame count fixed or read. **An interpreter dispatches through a C table**:
  each opcode family is one operation, with table rows saying how each opcode's
  operands decode. The table decodes operands into typed, named parameters
  (`animation`, `frames`), not `operand(g, c, 1)`, and the engine passes what
  every handler needs (the running object) instead of each looking it up.
  Inlined handlers keep their time with `NATIVE_TIME_AS` and stay registered, so
  each can still be fuzzed alone.
- **Comments say what a function is for, in the game's terms.** "$B3: loop
  animation 0" restates the encoding. "$AB: play animation (operand 1) once
  through a countdown of 2" restates the mechanics. Say what the game is doing:
  "Play a one-off animation on the object (a gesture, a reaction) and hold the
  script until it finishes; the object then goes back to what it was looping."
  The original's mechanics belong in a comment only where the port deliberately
  reproduces them, with why. A file's header says what part of the game it is
  and how its pieces fit together.

The same trap has two more forms, both found in `game/scene/` by the first
qualitative review:
- **Emulating the arithmetic unit.** `add_carry`, `rotate_left`, a divide as 32
  rounds of rotate-and-subtract. Write `n / d` and `a * b`. Derive any carry,
  overflow or leftover the callers read in the wrapper, from the results.
- **Using RAM cells as registers.** A loop that stores its counter in a cell and
  reads it back. Keep the loop's state in locals, and write the cells' final
  values once, as scaffolding.

The pilot `circle.c` did both, while having no `Snes *` in its logic.

**portstyle can't see any of this.** Its operation count and machine coupling
measure syntax. Read your own file before reporting. If a function is a run of
setters in the original's order, if there's a function per opcode that differs
only in operands, or if a comment restates a name or an encoding, it isn't done.

## Scaffolding is marked, not mixed in

A scratch write the original makes, a register or flag its callers might
read, a hardware register it leaves holding a product: when only shadow mode
or the original's still-generated code looks at it, it is scaffolding, and it
goes through `ORIGINAL_EFFECT(...)` (`game/original_effects.h`; docs/PORTING.md,
"Scaffolding"). Find the reader first -- evidence over the movie library, not
"other code may read it". Keep working state in locals and write the cell's
final value once; never read a scratch cell back. Where the value is the same
every time for a command (a parse position), put it in a table parallel to the
command table (`event/leaves.c`), never in the language's rows and never an
argument every return carries. Derive a flag from the
operands where it is handed back, not with ALU helpers in the logic. The
native-shape build (`ct_game_native_shape`) compiles your logic with all of it
switched off, and must still build; `portstyle.py` counts what you marked.

An index past a variable's count, bytes, rows or columns is an alias of
whatever follows it in the original's memory: `ct_dev` counts it (`accessor
out of range`) and the gate fails. Where the gate needs the original's stray
access, make it a marked scaffolding function that calls the variable's
`_unchecked` pair (declare `unchecked = "<why>"` on the variable in
`re/ram.toml`; `field/sprites.c`'s `leave_stray_fifth_sprite_y` is the
example). Otherwise guard the state away, and if the movies make it, it is
unported work (below). `ct_fuzz` lists the accessors random states drive out
of range: a guard is usually missing there.

A dispatcher over opcodes is a C table of rows -- operation, how each operand
decodes into a typed parameter, length, jump -- calling operations as C, with
`NATIVE_TIME_AS` naming each handler's address. Opcodes that differ only in
operand encoding (a short form, a byte and a word form, a constant and a
variable form) are rows of one operation, never separate functions. Keep the
handlers registered too (a generic entry per opcode), so fuzzing and callers
that reach them by address still find them. The table is the only dispatch: a
command that still calls out to the original's code is a row too, whose
operation takes the machine (a bridge), and the ROM's own table is only the
fallback for what has no row. Operations take named, typed parameters in the
order the row lists them, and the running object; each is named for what it
does in the game, and its registered entries share its name. `game/event/` is
the example.

## Workflow

1. **Scope the chunk** from the call graph: the routines named in the task, plus
   everything only they call. Stop where the call graph leaves the subsystem, and
   say in your report what you left at the edge and why.
2. **Draft every routine of the chunk in one run.** The analysis is solved once
   and is nearly all the time, so four drafts cost the same 27 seconds as one:
   ```sh
   python3 -m tools.recomp.show C2:0568 C2:066C C2:09C5 > <scratch>/drafts.c
   ```
   The draft is a **reference, not a starting text** — the pilot carried over
   0–7% of its lines. Read the original beside it with
   `python3 -m tools.disasm.linear BB:AAAA`.
3. **Write the port**, to the bar above. Name work RAM from `game/ram.h` and add
   what you learn to `re/ram.toml` (`python3 tools/ram_map.py coverage --open
   --subsystem S`, then `python3 tools/ram_map.py header`).
   A routine that runs on whichever direct page its caller has reaches its cells
   through a **page kind**: an accessor taking which instance, and a generated
   `<page>_page_of(d, ...)` for the entry wrapper (DISASSEMBLY.md, "The
   accessors"). Declare a page kind's instances from evidence
   (`ct_dev --page-report` over the library), and read the code before trusting
   it: D at entry is not the page a routine uses when it sets D itself. The
   joypad reader is entered with eight different D values and sets D = `$0000`
   before touching anything, so its "caller's page" turned out to be one fixed
   page.
4. **Declare what the original leaves dead** rather than reproducing it. This is
   what buys you plain C. `python3 tools/dontcare_check.py --list` shows what
   each declaration could still add; the gate verifies every one against
   liveness, so a declaration is a checked claim, not a shortcut.
5. **Validate, in order** (see "Checking yourself" below).
6. **Document and commit** (see "Finishing").

## Checking yourself — before you report done

```sh
python3 tools/portstyle.py --subsystem <your subsystem>   # both bars, measured
python3 tools/portstyle.py --check                        # the gate's ratchet
```

A new or rewritten file over **0.10** operations per line has transliterated the
assembly. Fix it before reporting, not after. The report's "on machine" column
is the second bar: every function but the entry wrappers should be off it. If you rewrote a file that was on
`tools/portstyle_baseline.txt`, delete its line — the list only shrinks.

Then the real validation:

```sh
cmake -S . -B build/main -G Ninja -DCT_ROM=roms/ct-usa.sfc && cmake --build build/main
# 1. shadow over the movie library: 0 mismatches
# 2. --native on
# 3. fuzz every entry you wrote or rewrote:
build/main/ct_fuzz --load-state <state> --routine <name>
# 4. the full gate, which must print "suite OK":
python3 tools/suite.py --ct-dev build/main/ct_dev -j 3 --long-routes
python3 -m unittest discover tests
```

An `interrupted` count in a shadow run is **not noise**: those calls were never
compared, so they are exactly what fuzzing is for. Record in your PORTING.md row
which ones the library never reached.

### Rewriting an existing port: fuzz the predecessor too

A rewrite can **reorder memory accesses invisibly**. Hoisting a read out of a
loop can move it before a write that used to precede it — identical on every
input the game produces, different when a buffer overlaps the cell being read.
Shadow mode over the whole library will not see it, because the movies never
construct that overlap.

What catches it is a differential count: fuzz **your rewrite and the port it
replaces** with the *same seed and the same constraints*, and compare the
mismatch counts. An absolute count means nothing on its own — the predecessor
may have its own residual mismatches, and what matters is whether yours are the
same ones. The pilot found a real reordering this way (262 against 256); it
found nothing without the comparison.

```sh
git stash        # or a scratch build of the predecessor's tree
build/main/ct_fuzz --load-state <state> --routine <name> --seed 1 ...   # before
# then the same command against your rewrite, same seed, same --fill/--keep
```

For a whole subsystem, `tools/fuzz_compare.py` does this for every routine
listed in `tools/fuzz_routines.toml` (add your subsystem's entries there, with
the constraints its PORTING.md rows record, and a `variant = "loose"` entry
where a rewrite names a buffer or reorders reads): build `main` in a scratch
tree outside the repo, then
`python3 tools/fuzz_compare.py --before <scratch>/build/main/ct_fuzz --after build/main/ct_fuzz --ct-dev build/main/ct_dev --subsystem <s> -j 3`.
A guard you add turns states away rather than mismatching, so read its skipped
column as well; and compare the per-entry shadow totals over the library with
the predecessor's, which is where a guard that refuses real calls shows.
Declare each entry's largest don't-care set first (`tools/dontcare_check.py
--list`): it is what takes the register traffic out of the wrappers.

**A guard may refuse only what the game never produces.** A guard hands a
call to the original, so while the emulator is there it is always correct. But
in a native build a refused call has nowhere to go (PLAN.md, "A guard becomes
an assertion"). So:
- a guard refusing states the game never makes (random fuzz states, inputs no
  movie reaches) is the tool for item 34;
- a guard refusing **calls the game really makes** is an unported path. Count
  those calls from the shadow totals (calls handed to the original, by entry),
  report them, and put them in `docs/TODO.md` as work to finish.

The absent party member is the example: 52,898 real calls, correctly refused,
and still owed a port.

**When the counts disagree, fix the port, not the fuzz.** If your rewrite
mismatches where its predecessor didn't, narrowing the fuzz domain until the
counts agree only stops the tool from looking; the rewrite still diverges on
those inputs. There are two honest answers:
- **A guard** that refuses the states where the port would diverge. The
  original then runs them, the port stays exact everywhere it accepts, and the
  refusals show in the skipped column.
- **An accepted divergence** (target 4), written in the port at the entry's
  `// $BB:AAAA`, saying which inputs diverge and why no game state reaches them.

Either way, the narrowed `fuzz_routines.toml` entry's `why` names the guard or
the comment. Your report says which of the two you chose, for every routine
whose domain you narrowed. The scene rewrite's sprite writers are the example:
with their cursors free, the named sprite table wrote bank `$7E` where the
original, through a data bank of 0, reaches I/O past `$2000` (19,569 mismatches
against 22). The loose runs were narrowed to low RAM, and the only record of
that was in the docs.

Expect to **recalibrate your own `cost_milli` rows**, too: any rewrite that
changes how many bus accesses a port makes moves its drift, and the limits are
tight (±0.38%). Sequence the work so only the final code pays for a
`--long-routes` run.

## Working safely

- **Your own worktree and branch**, cut from `main`:
  `git worktree add -b <branch> .worktrees/<branch> main`. Never write to the
  main checkout (reading is fine).
- **Never link the main checkout's gitignored directories** (`roms/`, `traces/`)
  into your worktree. A worktree removal deletes through such a link, and it has
  destroyed the ROM once. Stage your own ROM with `python3 tools/rom.py stage
  <path>`, or point `-DCT_ROM` at the main checkout's copy (read-only use).
- **One directory of `game/` per agent.** Shared files (`game/registry.c`,
  `re/ram.toml`, the exception lists) are edited **in place, in your subsystem's
  part**, never reorganised. Calibration rewrites every measured `cost_milli`;
  commit only your subsystem's rows.
- **At most 3 heavy processes** (builds, movies, fuzz runs) at once, unless your
  brief gives you a different number.
- **Stop your own processes by PID, never by image name.** Other agents run
  `ct_dev` and `ct_fuzz` on the same machine: `taskkill /IM ct_dev.exe` (or
  `pkill ct_dev`) kills theirs too, silently cutting short a shadow run whose
  result they will then trust. Keep the PIDs you start, and kill only those.
- **Never commit ROM bytes or anything derived from them**: drafts, traces,
  listings with data bytes, extracted assets, save states. Keep them in scratch.

## Finishing

In the same commit as the work:

- `docs/RECOMP.md`: a new "as built" section in the style of the ones before
  yours — how the chunk was chosen, what it is, what shadow mode found, what
  fuzzing found and what it cost to set up, any exception and why.
- `docs/PORTING.md`: a row per entry, with the fuzz run recorded.
- `docs/PLAN.md`: refresh the burndown table and the Phase 3a table if you
  emptied backlog files.
- `docs/TODO.md`: remove what you closed, add what you opened. **Delete finished
  work rather than marking it done** — no "(done)", no strikethrough.

Commit to your branch; do not push or merge. **No `Co-Authored-By` and no
`Claude-Session` trailers** — this repo's rule.

**Your work is reviewed before it merges, by a reader who hasn't seen your
report** (`.claude/skills/review-port/`). The reader judges whether each file
reads as a hand reimplementation or as a translation, against a concrete rubric.
Read that rubric before you start, not after: every item on it is something a
port in this repo has already done wrong. A file rated "translation" doesn't
merge.

## Report

What you ported (entries and bytes), the burndown and portstyle scores before
and after, what shadow mode and fuzzing each found, any exception and why, what
you left at the chunk's edge, and **where the workflow and the tools still
hurt** — that last answer decides whether the next thing is another chunk or a
tooling pass.

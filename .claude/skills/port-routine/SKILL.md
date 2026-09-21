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
- **At most 3 heavy processes** (builds, movies, fuzz runs) at once.
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

## Report

What you ported (entries and bytes), the burndown and portstyle scores before
and after, what shadow mode and fuzzing each found, any exception and why, what
you left at the chunk's edge, and **where the workflow and the tools still
hurt** — that last answer decides whether the next thing is another chunk or a
tooling pass.

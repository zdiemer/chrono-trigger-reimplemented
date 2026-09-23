---
name: review-port
description: Critical qualitative review of hand-written ports in game/, judging whether they read as a hand reimplementation of Chrono Trigger or as a translation of its assembly. Use before a Phase 3/3a subsystem merges, or to audit a subsystem already in the tree.
---

# Reviewing a port: does it read as hand-written?

The gate proves a port **behaves** like the original. It says nothing about
whether it **reads** like a game someone wrote in C, and neither does
`tools/portstyle.py`, which measures syntax. Ports have passed both while still
being translations: the event rewrite scored 0.02 operations per line with no
`Snes *` in its logic, and a reader still saw the ROM through it. This review is
the check that does see that. It is **critical by design**. Its job is to find
where the code still reads as a translation, not to confirm that it doesn't.

## How to review

- **Read every file of the subsystem in full.** Not excerpts, not a grep. The
  failures this review exists for only show in the reading.
- **You are not given the author's report, and don't look for it** (RECOMP.md's
  section for the change, commit messages). Judge the code as a reader meets it.
  You may read the rest of the repo for context: other subsystems, the headers
  a file includes, `re/ram.toml`, CLAUDE.md's porting rules.
- **Don't run the gate or rebuild anything.** Behaviour is the gate's job.
  Yours is reading.

**The test, per function:** from its name, its signature and its comment alone,
can you say in one sentence what it does *in the game* — to the party, an
object, the map, the story, the screen? And could a programmer who set out to
write this game in C, never having seen the ROM, plausibly have written it this
way? If either answer is no, it is a finding.

**The test, per file:** does it read as a part of the game with its own
concepts (objects that animate, walk and face each other; a party; story
flags; a screen that fades), or as a list of routines operating on tables of
bytes?

## What reads as a translation

Each of these is a finding. Every example here comes from this repo.

1. **Setter chains in the original's order.** Four `set_field_object_*` calls
   in a row is the original's stores with names on them. A hand-written version
   calls one operation of a concept (`object_loop_animation(obj, anim)`) that
   does those stores inside.
2. **One function per ROM routine or per opcode**, where they differ only in how
   an operand is encoded. `loop_animation`, `loop_animation_0` and
   `loop_animation_1`, or `play_animation` and `play_animation_for`, are one
   operation each. An interpreter that dispatches through the ROM's pointer
   table and an emulated `JSR`, instead of a C table, is the usual cause.
3. **Positional or offset access to data that has meaning.** `operand(g, c, 1)`
   for "the animation"; a byte offset into a record; `at + 2`. A hand-written
   version decodes once into named, typed values.
4. **Names that come from addresses or from the original's memory layout.**
   `SCRATCH_D9`, `sub_C0xxxx`, `cmd_6C`, `set_1c80`. Also names that file a
   value under whatever sits next to it: `field_object_move_state_animation_count`
   files an animation countdown under "move state"; `field_object_animation_mode_mode`
   stutters.
5. **Magic numbers and in-band encodings.** `& 0x80` meaning "disabled", `$FF`
   stored in an animation number to mean "nothing to go back to", a count of 2
   with no name. These want an enum, a bool or a named state.
6. **The original's registers and flags in the logic.** A result carrying the
   original's Y or V flag, a "parse position" threaded through every return,
   carry-chained arithmetic, width checks. These belong in the entry wrapper or
   a bridge, or in marked scaffolding, not in what a function means.
7. **Control flow that mirrors the original's branches.** Flags that exist to
   reproduce a `BRA`; duplicated tails where the original jumped into shared
   code; an early `return` exactly where the original had an `RTS`, when the
   logic has a natural shape of its own.
8. **Comments that don't say what a function is for.** They restate the name
   ("$B3: loop animation 0"), the encoding, or narrate the mechanics ("once
   through a countdown of 2", "LDX $6D"). A good one says what the game is
   doing and why: "Play a one-off animation on the object (a gesture, a
   reaction) and hold the script until it finishes." Also a finding: a file
   without a header saying what part of the game it is and how its pieces fit
   together.
9. **Scaffolding mixed into the logic.** A write that exists only because
   still-generated code or shadow mode reads it, inline and unmarked among the
   writes that are the game's.
10. **The wrong types.** `uint8_t` where the value is a bool, an enum, a state,
    a slot handle or a count with a unit. Types are part of the vocabulary.
11. **The wrong level.** A file whose functions operate on tables and bytes when
    the game concept exists (or should) one level up.

## What is legitimately machine-shaped

Don't flag these, but **do** check that each is thin, isolated and says why it
exists:

- a registered entry's wrapper, which translates the original's registers to
  the logic and back;
- guards;
- bridges around a call to still-generated code;
- a deliberate reproduction of an original quirk the gate or the game can see
  (a wraparound, an odd order, a scratch write), with a comment saying why;
- marked scaffolding (docs/PORTING.md);
- `// $BB:AAAA` origin comments.

A wrapper that has grown logic, or a "deliberate reproduction" with no comment
saying why, **is** a finding.

## Report

1. **A verdict per file:**
   - **hand-written:** a reader would not guess it was ported;
   - **mostly:** reads as hand-written with specific lapses;
   - **translation:** the original's structure shows through the whole.
2. **Findings**, most important first. Each finding gives `file:line`, which
   smell it is (by number above, or say it's new), and what a hand-written
   version would do instead, concretely, in a sentence or a few lines of code.
   Don't pad: three findings that matter beat thirty nits. Group repeated
   instances ("the same in 14 handlers: ...").
3. **The five changes that would do the most** for how the subsystem reads.
4. **Anything the rubric missed.** New ways the code still reads as a
   translation belong in this skill.

**What merging needs:** no file rated **translation**, and every **mostly**
finding either fixed or accepted by the user with a reason, recorded in the
subsystem's RECOMP.md section. The reviewer doesn't fix anything; the finding
goes back to the author, or to a follow-up.

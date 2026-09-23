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
12. **The arithmetic unit emulated in the logic.** An add-with-carry or a
    rotate-left written as C functions (`add_carry`, `rotate_left`, `adc`/`sbc`),
    a divide done as 32 rounds of rotate-and-subtract, a multiply as partial
    products with a chained carry. A hand-written version writes
    `quotient = n / d`, `product = a * b`, and any carry, overflow or leftover
    the callers read is derived *in the wrapper* from those results. Where the
    original's rounding really moves a pixel, that is one named function
    (`edge_rounding(...)`) with a comment saying what the player sees, not the
    shape of the whole routine. A struct of `carry, overflow, x, y, bank` passed
    through the logic is the CPU; name it that way in the finding.
13. **Memory used as registers.** A loop counter or temporary kept in a RAM cell
    and read back (`set_scene_circle_count(g, x); ... x = scene_circle_count(g);`),
    or bytes shifted in place in scratch. CLAUDE.md requires writing the cells the
    original leaves, but only their *final values*, once, in marked scaffolding.
    The working state belongs in locals.
14. **Semantic values built by decoding the original's literal bytes.**
    `vid_mode_decode(0x07)`, `vid_set_screens(pf, at, 0x13, 0x04, ...)`,
    `vid_windows_decode(&w, field, 0x00)`. The decoders exist for bytes the game
    computes at run time; a constant fed to one is the register value with a
    function around it. Write the struct's fields and the named constants
    (`(VidMode){ .mode = 7 }`, `VID_LAYER_BIT(VID_BG1)`, `.cy = -64`). A setup
    routine that is 25 register-group writes in the original's order is smell 1
    on the display side: build the state as one value and apply it, or add the
    service call that would.
15. **Assembly quoted in comments.** `/* STX $2102 : STZ $4374 : STA $420B */`
    is the most blatant form of smell 8. Also a finding: a per-call array
    spelling out the original's write order for a transfer the service already
    models.
16. **Stop-point fragments.** Entries that exist only because a routine is
    half-ported, named for the address or constant at the stop
    (`scene_set_vram_6400`, `scene_race_set_vram_0000_again`). Port the routine
    they're fragments of (`load_title_graphics`), or at least name each for what
    it holds.
17. **Admitted ignorance.** "presumably", "a history of values, presumably",
    "what reads the pair isn't ported". This always fails the one-sentence test
    and is always a finding. Its fix has an owner: find the reader and name the
    value, or record it in `docs/TODO.md`.
18. **One field, several private definitions.** The same bit defined in
    several files, under different names or meanings (`SCENE_FLAG_LINE_IRQ` and
    `SCENE_FLAG_TITLE_AGAIN`, both `0x80` of `scene_flags`). One definition, in
    the subsystem's header, named for everything it means.
19. **Branch-shaped compares.** `!((uint16_t)(x - LIMIT) & 0x8000)` is a
    BMI/BPL spelled out. Write the comparison the logic means (`(int16_t)(x -
    LIMIT) < 0`, or `x < LIMIT` where the range allows it), with a comment only
    where wraparound really changes the result.
20. **Scaffolding as table data.** `ORIGINAL_EFFECT` compiles out *writes*, not
    *columns*. A language's row struct carrying the original's Y, scratch
    layout or flags (`.parse`, `.keeps = 0x80|n`, `.overflow`) is smell 9 even
    when every use is marked. The original's leftovers go in a parallel table
    that compiles out with the mechanism.
21. **Two vocabularies, or dead address columns.** Registered entry names that
    disagree with the operation names for the same command (`reset_animation`
    and `stop_animating`), or a column holding ROM addresses that no code reads.
22. **Calling a sibling port through its ROM address.** An emulated `JSR` into
    your own subsystem's port, instead of a C call.
23. **The scaffolding switch used as a value.** `return ORIGINAL_EFFECTS &&
    overflows16(...)`: the switch has leaked into what a function means.
24. **Adapters that reorder positional arguments.** A dispatch table whose rows
    list operands in one order while the operation takes them in another
    (`(arg[2], arg[0], arg[1], arg[3])`). This is smell 3 moved into the
    interpreter; decode into a named struct.
25. **Stale provenance.** `re/ram.toml` `source` lines and header declarations
    naming files or functions that no longer exist.

## What is legitimately machine-shaped

Don't flag these, but **do** check that each is thin, isolated and says why it
exists:

- a registered entry's wrapper, which translates the original's registers to
  the logic and back;
- guards;
- bridges around a call to still-generated code;
- a deliberate reproduction of an original quirk the gate or the game can see
  (a wraparound, an odd order, a scratch write), with a comment saying why;
- marked scaffolding: writes and values that exist only for still-generated
  code or shadow mode, going through the scaffolding mechanism
  (docs/PORTING.md). Until that exists in the tree, the convention is one
  helper per file with a comment saying the write is for the original's code
  or shadow mode. A scaffolding write inline among the logic's own writes is
  smell 9;
- `// $BB:AAAA` origin comments, and `PF_AT(...)` / `PF_CALL(...)` origin tags
  on service calls.

A wrapper that has grown logic, or a "deliberate reproduction" with no comment
saying why, **is** a finding. So is a bridge that builds fake register state
beyond what its call needs (`c->a = 0xFFFF; c->x = 0x0940; ...` "the registers
the block move leaves").

## The subsystem as a whole

Per-file verdicts can all pass while the subsystem is still organised by ROM
location ("the RAM program", "bank C3 routines") instead of by the game. So also
answer:
- **Where would a reader look for each part of the game this subsystem
  covers** (the title screen, the race, the endings, the script language)?
  If the answer is "several files, none named for it", that's a finding.
- **What are the subsystem's concepts** (tasks, scripts, objects, the frame)?
  Is each one a type or module with operations, or only implied by how bytes
  are used?

## Report

1. **A verdict per file:**
   - **hand-written:** a reader would not guess it was ported;
   - **mostly:** reads as hand-written with specific lapses;
   - **translation:** the original's structure shows through the whole.
     Any logic function that emulates the arithmetic unit (smell 12) or passes
     a register-shaped struct through the logic rates its file **translation**,
     however good the header is. Otherwise, the share of logic functions that
     fail the one-sentence test decides between mostly and translation.
   Then a paragraph on the subsystem as a whole (above).
2. **Findings**, most important first. Each finding gives `file:line`, which
   smell it is (by number above, or say it's new), and what a hand-written
   version would do instead, concretely, in a sentence or a few lines of code.
   Don't pad: three findings that matter beat thirty nits. Group repeated
   instances ("the same in 14 handlers: ...").
3. **The five changes that would do the most** for how the subsystem reads.
   Where a finding is really a gap in a platform service or the RAM map (a
   service with no call for what the port needs, a decoder only reachable
   through literal bytes), say so: it goes to the platform or the map, not
   to the port.
4. **Anything the rubric missed.** New ways the code still reads as a
   translation belong in this skill.

**What merging needs:** no file rated **translation**, and every **mostly**
finding either fixed or accepted by the user with a reason, recorded in the
subsystem's RECOMP.md section. The reviewer doesn't fix anything; the finding
goes back to the author, or to a follow-up.

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
- **Rate every file under the subsystem's directory**, whether or not the
  branch touched it. A file that is neither rewritten nor on
  `tools/portstyle_baseline.txt` is not thereby a pass: `portstyle.py` counts
  machine operations, and a file can emulate an ALU in plain C (a
  `Sum{carry, overflow}` threaded through the logic, a multiply by shifts) or
  keep its loop counters in direct-page cells without tripping it.

**The test, per function:** from its name, its signature and its comment alone,
can you say in one sentence what it does *in the game* — to the party, an
object, the map, the story, the screen? And could a programmer who set out to
write this game in C, never having seen the ROM, plausibly have written it this
way? If either answer is no, it is a finding.

**The test, per file:** does it read as a part of the game with its own
concepts (objects that animate, walk and face each other; a party; story
flags; a screen that fades), or as a list of routines operating on tables of
bytes?

## What hand-written looks like

A real file first, so there is a picture of the target and not only a list of
ways to miss it: the first half of `game/field/animation.c`, field objects'
animation (the concept the rubric's worst examples come from; compare smells
1, 2 and 5 below), as it passed its review, and the script command that uses
it from `game/event/animation.c`. Read the rest of the file in the tree: the
tick, the frame queue and the frame read follow the same shape.

```c
/* Field objects: how an object animates.
 *
 * Everything on the map shows one of four things at any moment
 * (ObjectAnimationMode): it animates with its movement, standing, walking or
 * running by how fast it goes; it loops an animation a script chose (an idle
 * bob, a torch flickering); it plays a one-off animation (a nod, a jump of
 * surprise) a number of times and then goes back to what it was doing; or it
 * holds a single frame a script chose. Scripts start and stop these, the
 * object's movement picks the stand, walk and run animations
 * (object_animate_movement), and the object update advances the animation
 * once a frame (object_animation_tick): when the frame's time is up the
 * object is queued for the frame builder, which draws the new frame's tiles
 * and takes the new frame from this file (object_take_next_frame).
 *
 * An animation is four entries a facing in the object's frame table, frame
 * numbers ending in an end marker, and beside it a table of how long each
 * frame shows, ending in 0. Reaching the end of the times starts the
 * animation over, or for a one-off counts a pass.
 *
 * A one-off is counted in half passes, which is how the scripts give its
 * length too (event/animation.c): the end of its times takes a whole pass
 * and the end of its frame table, which an animation whose frame table is
 * shorter than its times reaches first, half of one.
 *
 * The state is kept in the object tables, and the original encodes part of it
 * in-band: while a one-off plays or a frame is held, "was it looping an
 * animation before?" is the looped animation's number $FF. That encoding
 * lives in this file's storage functions and nowhere else. */
#include "field/field.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/field_data.h"
#include "field/parts.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The end markers of an animation's two tables. */
enum { END_OF_FRAMES = 0xFF, END_OF_TIMES = 0 };

/* ---- Storage: the only code that knows how the state sits in the tables ------------------ */

ObjectAnimationMode object_animation_mode(const GameState *g, FieldObject obj)
{
    return (ObjectAnimationMode)field_object_animation_mode(g, obj);
}

static void set_mode(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    set_field_object_animation_mode(g, obj, (uint8_t)mode);
}

/* The animation it loops, or animates its movement with. */
static uint8_t looped_animation(const GameState *g, FieldObject obj) { return field_object_looped_animation(g, obj); }
static void set_looped_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_field_object_looped_animation(g, obj, animation);
}

/* The original's in-band marker, in place of the looped animation: nothing to
 * go back to but the object's movement. */
enum { NOTHING_TO_GO_BACK_TO = 0xFF };

static void remember_nothing_to_go_back_to(GameState *g, FieldObject obj)
{
    set_looped_animation(g, obj, NOTHING_TO_GO_BACK_TO);
}

static bool has_loop_to_go_back_to(const GameState *g, FieldObject obj)
{
    return looped_animation(g, obj) != NOTHING_TO_GO_BACK_TO;
}

static uint8_t one_off_animation(const GameState *g, FieldObject obj)
{
    return field_object_one_off_animation(g, obj);
}
static void set_one_off_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_field_object_one_off_animation(g, obj, animation);
}

/* The frame a script holds. */
static void set_held_frame(GameState *g, FieldObject obj, uint8_t frame) { set_field_object_static_frame(g, obj, frame); }

/* A one-off's half passes left (above). The last half pass is the one-off
 * played out, holding its frame; none is no one-off. */
enum { NO_ONE_OFF = 0, PLAYED_OUT = 1, HALF_PASSES_PER_PASS = 2 };

static uint8_t half_passes_left(const GameState *g, FieldObject obj)
{
    return field_object_one_off_half_passes(g, obj);
}
static void set_half_passes_left(GameState *g, FieldObject obj, uint8_t n)
{
    set_field_object_one_off_half_passes(g, obj, n);
}

/* Where the object is in its animation: a frame number, and how long it has
 * left on it. An animation that starts over is put a frame before its first,
 * so that its next frame is the first (the frame number wraps). */
enum { BEFORE_THE_FIRST_FRAME = 0xFF };

void object_restart_animation(GameState *g, FieldObject obj)
{
    set_field_object_animation_frame(g, obj, 0);
    object_end_frame_time(g, obj);
}

void object_end_frame_time(GameState *g, FieldObject obj) { set_field_object_frame_delay(g, obj, 0); }

/* ---- What a script does with it ------------------------------------------------------- */

/* Leave what it was doing for a one-off animation or a held frame. An object
 * animating with its movement has nothing to go back to but its movement; a
 * looping one goes back to its loop. */
static void interrupt_with(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    object_restart_animation(g, obj);
    if (object_animation_mode(g, obj) == ANIMATE_WITH_MOVEMENT)
        remember_nothing_to_go_back_to(g, obj);
    set_mode(g, obj, mode);
}

/* Start the object animating in a mode from the first frame, dropping any
 * one-off it was playing. */
static void start_afresh(GameState *g, FieldObject obj, ObjectAnimationMode mode)
{
    set_mode(g, obj, mode);
    set_half_passes_left(g, obj, NO_ONE_OFF);
    object_restart_animation(g, obj);
}

/* Loop an animation from its first frame, dropping whatever the object was
 * doing. */
void object_loop_animation(GameState *g, FieldObject obj, uint8_t animation)
{
    set_looped_animation(g, obj, animation);
    start_afresh(g, obj, ANIMATE_LOOP);
}

/* Stop any animation a script gave the object: it stands, and walks and runs
 * again as it moves. */
void object_stop_animating(GameState *g, FieldObject obj)
{
    set_looped_animation(g, obj, ANIMATION_STAND);
    start_afresh(g, obj, ANIMATE_WITH_MOVEMENT);
}

/* Play a one-off animation (a gesture, a reaction) for `half_passes` half
 * passes; then it holds its last frame until the script that started it
 * finishes it. The count runs down two a pass and one where the frame table
 * ends first, and the one-off is played out on its last half pass. */
void object_play_animation(GameState *g, FieldObject obj, uint8_t animation, uint8_t half_passes)
{
    set_one_off_animation(g, obj, animation);
    interrupt_with(g, obj, ANIMATE_PLAY);
    set_half_passes_left(g, obj, half_passes);
}

bool object_playing_animation(const GameState *g, FieldObject obj, uint8_t animation)
{
    return half_passes_left(g, obj) != NO_ONE_OFF && animation == one_off_animation(g, obj);
}

bool object_animation_played(const GameState *g, FieldObject obj) { return half_passes_left(g, obj) == PLAYED_OUT; }

/* A one-off is over: back to the loop the object had, or to animating with its
 * movement, standing. */
void object_finish_animation(GameState *g, FieldObject obj)
{
    set_half_passes_left(g, obj, NO_ONE_OFF);
    object_restart_animation(g, obj);
    if (has_loop_to_go_back_to(g, obj)) {
        set_mode(g, obj, ANIMATE_LOOP);
    } else {
        set_looped_animation(g, obj, ANIMATION_STAND);
        set_mode(g, obj, ANIMATE_WITH_MOVEMENT);
    }
}

/* Hold one frame a script chose, until the object moves again. */
void object_show_frame(GameState *g, FieldObject obj, uint8_t frame)
{
    set_held_frame(g, obj, frame);
    interrupt_with(g, obj, ANIMATE_SHOW_FRAME);
}

/* An object that leaves view while playing a one-off animation is cut to its
 * last pass, so the script waiting on it isn't held up by what nobody sees. */
void object_hurry_animation(GameState *g, FieldObject obj)
{
    if (half_passes_left(g, obj) != NO_ONE_OFF)
        set_half_passes_left(g, obj, HALF_PASSES_PER_PASS);
}
```

And a script command using it. One operation serves an opcode family,
because the table (not a function per opcode) says how each opcode's operands
decode into the operation's parameters:

```c
// $C0:2E9A
/* Play a one-off animation on the object (a gesture, a reaction) and hold the
 * script until it has played; the object then goes back to what it was
 * looping. Met again while that animation still plays, the command waits on.
 * How long it plays is counted in half passes (field/object.h): the short
 * form plays it once through. */
static EventFlow play_animation(GameState *g, FieldObject self, uint8_t animation, uint8_t half_passes)
{
    if (object_animation_played(g, self)) {
        object_finish_animation(g, self);
        return next_command();
    }
    if (!object_playing_animation(g, self, animation))
        object_play_animation(g, self, animation, half_passes);
    return stay();
}
EVENT_OPERATION(play_animation, self, animation, half_passes)

/* The long form gives how long the one-off plays as its half passes less
 * one. */
EVENT_OPCODES(event_animation_opcodes,
    { 0xAA, OPERATION(loop_animation), .animation = OPERAND(1), .length = 2 },
    { 0xB3, OPERATION(loop_animation), .animation = CONSTANT(0), .length = 1 },
    { 0xB4, OPERATION(loop_animation), .animation = CONSTANT(1), .length = 1 },
    { 0xAE, OPERATION(stop_animating), .length = 1 },
    { 0xAB, OPERATION(play_animation), .animation = OPERAND(1), .half_passes = CONSTANT(2), .length = 2 },
    { 0xB7, OPERATION(play_animation), .animation = OPERAND(1), .half_passes = OPERAND_PLUS_ONE(2), .length = 3 },
    { 0xAC, OPERATION(hold_frame), .frame = OPERAND(1), .length = 2 });
```

Why this reads as hand-written:

- **The header teaches the concept before any code.** A reader who has never
  seen the ROM learns the four things an object can be doing, who changes
  them, who advances them, and how long a one-off lasts, in the unit the
  scripts give it in.
- **The original's encoding is confined and named.** `$FF` appears once, as
  `NOTHING_TO_GO_BACK_TO`, inside the storage functions at the top, and the
  frame number's wrap once, as `BEFORE_THE_FIRST_FRAME`. Everything below them
  reads in the game's terms (`has_loop_to_go_back_to`, `half_passes_left`),
  and nothing outside the file sees the bytes: the rest of the game calls
  `field/object.h`.
- **Operations are named for what the game does**, and each comment says what
  the player sees, not how the original stored it.
- **There is still a setter sequence in each operation, and that's fine.** It
  sits inside the operation that means it, in one place per concept, not
  scattered through every caller in the original's order. It is also where
  the storage changes when the gate retires.
- **The gate is still satisfied.** The operations write the same bytes the
  original writes, so shadow mode compares them as before. Nothing in the
  shape needs a machine.
- **Short forms are table rows**, not functions. `$B3` and `$B4` are the same
  operation as `$AA`, and `$AB` the same as `$B7` with its count fixed; the
  reader sees that at a glance.
- **What's absent is as telling:** no `Snes *`, no `m_*`, no register or flag
  values carried through, no positional `operand(g, c, 1)`, no `SCRATCH_D9`, no
  "presumably".

A registered entry around such code stays the thin shape the rubric describes:
it reads the original's registers into these parameters, calls the operation,
and derives any register a caller reads from the state afterwards, beside the
other derivations (`game/field/leaves.c`). Anything it can't derive is
recorded through the scaffolding mechanism, in one marked line.

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
    (`(VidMode){ .mode = 7 }`, `VID_BG1_BIT | VID_OBJ_BIT`, `.cy = -64`). A setup
    routine that is 25 register-group writes in the original's order is smell 1
    on the display side: build the state as one `VidScreen` and apply it
    (`vid_apply_screen`), or add the service call that would.
15. **Assembly quoted in comments.** `/* STX $2102 : STZ $4374 : STA $420B */`
    is the most blatant form of smell 8. Also a finding: a per-call array
    spelling out the original's write order for a transfer the service already
    models (`VID_STEP_*`, `VidSnesXfer`, a DMA channel or mode); the site's
    order belongs in `runtime/platform/snes/encodings.c`, and the lint allows it
    in `game/` only in the files it lists as not moved yet.
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
26. **Traces as out-parameters, with twin functions.** A public logic function
    that wraps a private one taking `SomethingTrace *` or `bool *x_for_wrapper`
    only so the entry wrapper can rebuild registers (`FrameTrace { plays_counted
    /* A */, started_over /* V */ }`). This is smell 6 moved into a parameter, and
    it counts as a register-shaped struct for the verdict threshold. Wrappers
    derive registers from the state after the call. Anything they can't derive
    is recorded through the scaffolding mechanism, not through the logic's
    signature.
27. **A domain module with no callers.** A header of good operations beside
    code that still reaches the same bytes directly. Grep for each operation's
    callers; don't only read the header.
28. **Out-of-range indices that alias the next table.** Slot 64 of a 64-slot
    accessor, "the word after BG3's", a loop running to `& 0x7F` over a
    64-entry table. It works only because of the original's memory layout, it
    hides behind a clean accessor name, and it breaks the moment storage goes
    native. A deliberate stray write the gate needs is an explicit, marked
    scaffolding write, through the variable's `_unchecked` accessors (the map
    says why) or `game/wram.h`. The gate now finds the accidental ones the
    movies reach (`accessor out of range`, docs/DISASSEMBLY.md "The
    accessors"), and `ct_fuzz` lists those random states reach; what neither
    reaches is still the reviewer's to read for.
29. **Guards that duplicate the logic.** A guard that re-runs the routine to
    predict it (`field_view_unchanged`, `draws_as_c`) is shadow mode's structure
    leaking into the file's shape.
30. **Call-site arithmetic.** `first + 3`, `site + 3`, "the party's copy of the
    loop": the ROM's code layout used as control data. Name each target for
    what it does.
31. **Registers or RAM cells as a bridge's own loop variables.** A row counter
    in `s->cpu.y`, or `field_current_object` as the loop index. A bridge's own
    loop runs on locals and sets the machine only at the call.
32. **Operations that take a bag.** An operation whose signature is
    `(GameState *g, const EventCommand *c)`, where `EventCommand` holds every
    parameter of the language (about 80 fields). Its fields are named, but the
    signature no longer says what the operation reads. Take the parameters
    explicitly, as `event/animation.c` does (`play_animation(g, self, animation, half_passes)`),
    or a small struct per operation family.
33. **Constants that type nothing.** Enums declared so a comment can mention
    them (`IN_OTHERS_WAY`, `STOPPED_BY_*`) while the parameter stays `uint8_t`.
    The type is the vocabulary.
34. **Emulating inputs no game state produces.** A bit-serial division kept
    "for a divisor of 0, or one with its top bit set, which no scene uses". The
    remedy is a guard that refuses those inputs, so the original runs them;
    the logic keeps only the arithmetic the game actually needs (`n / d`). An
    emulation with a comment explaining that nothing reaches it is still an
    emulation.
35. **Leftover records that carry the CPU.** A file-static `*_leaves` record is
    acceptable for a value the wrapper can't derive from the inputs and the
    final state. A register-shaped record (carry, overflow, A) that the logic
    writes all the way through, even inside `ORIGINAL_EFFECT`, is item 26
    moved into a global: the signatures look clean and the CPU is still
    threaded through. For each field of a leftover record, the review asks
    whether the wrapper could derive it.
36. **Timing shaping the logic.** A loop kept byte by byte, or a step kept in
    the original's order, because that shape is what the timing estimate
    charges. Time is charged through scaffolding (`NATIVE_TIME_AS`, or a
    block-move charge of N bytes in one call), never by keeping a shape.
37. **Hardware helpers used as the computation.** The divider, the
    multiplier or the work RAM port used to *compute* (`quotient =
    hw_quotient_by(...)`, a list read through the port with a word's high byte
    "dropped"). Compute in C, and leave the hardware's final state as
    scaffolding.
38. **Machine counting conventions as parameters.** A block move's "count
    minus one", or "A + 1 bytes", passed into logic as a parameter. Say how
    many bytes, or which range, in the game's terms.
39. **A scaffolding global that doubles as a parameter.** A record the machine
    wrapper sets and the logic reads (the command's offset out of
    `event_leaves->at`), so the "portable" code only works under the wrapper.
    Pass the value explicitly.
40. **Game work inside an expression only scaffolding reads.** `bool animated =
    style == WALKING && object_animate_movement(...)`, where the result goes only
    into `ORIGINAL_EFFECT`: the walk starts as a side effect of scaffolding. Do
    the work as a statement of its own.
41. **Comments that contradict the code.** "A wandering townsperson" on a bridge
    that only serves party members; "its copy" for a table that is the other
    half of another; `NO_BATTLE_PLACE` and `BATTLE_PLACES_IT` for the same byte.
    A wrong comment is worse than none.
42. **Enum values used as indices into tables of ROM addresses**
    (`sites[role.kind]`). Name the table's rows by role.
43. **The same decision written several times in one file**, and scaffolding
    helpers duplicated across files. This is item 18 for logic and for
    scaffolding, not only for bit definitions.
44. **Flag derivations exported from a logic file.** `field_step_start_overflow`
    and the like, living in `motion.c` and re-running its internals so a
    wrapper can rebuild a flag. Derivations belong beside the wrappers, so the
    logic file holds only the game.
45. **Locals and return values that exist only for scaffolding.** A return
    value the game ignores, or locals that only shape a `leave_*` call. This is
    item 26 in the return-value form.
46. **One buffer read as two tables through an offset**, with no named view.
    Name the view, or declare the second table in the map.
47. **Service headers that invite a smell.** A header ports rely on whose
    contract says to "stream bytes through" the port, or to program write
    order. The review also checks the headers, not only the ports that follow
    them.
48. **Aliasing artifacts promoted into domain types.** An enum value that exists
    because a stray write lands there (`OVERWRITTEN_BY_A_LINK`). Model it only
    if the game can observe it, and say so where the type is defined.
49. **Handler identity as a type tag.** `row->run == run_loop` used to tell
    rows apart. The table is missing a column; give it a kind.
50. **Leftover round-trips that do nothing.** A leftover field that writes back
    what the register already held (`a_high = list >> 8` restored into A's high
    byte). Check each leftover field for identity with the input, as well as for
    whether the wrapper could derive it.
51. **Origin tags computed or tabled.** `PF_AT(0x7E3124 + 8 * bg)`,
    `map_sites[bg]`. `PF_AT`/`PF_CALL` tags are legitimate as literals at the
    call they describe, not as arithmetic on the ROM's code layout.
52. **Counting conventions in scaffolding too.** `charge_block_move(g,
    bytes - 1)` with no reason given is item 38 inside scaffolding.
53. **Fixed-point claims without a named unit.** A comment saying "1/256 of a
    row" that can't be checked, and was wrong (it was 1/128). Wherever a comment
    states a fraction, name the unit as a constant.
54. **One program split across subsystem directories**, with neither header
    pointing at the other (`game/scene/` and `game/attract/`).
55. **Generic operand names forced by a language-wide parameter list.** An
    interpreter whose table decodes into one shared set of parameters (`word`,
    `second_word`, `first`) lets every operation take them without saying what
    they are. The signature looks explicit and isn't: `place(g, o, x, y)`, not
    `place(g, o, word, second_word)`.
56. **A machine idiom's side effect used as the computation.** An overlapping
    block move that copies a buffer onto itself one byte on, so its first byte
    repeats (`repeat_forward(g, row, row + 1, n)`), stands for "fill these
    lines with X". Write the fill; keep the idiom only where the gate needs
    its exact bytes, and say so.

57. **A quirk parameter named for one of its uses.** A flag that is the add's
    carry at one call and the subtraction's no-borrow at another, named and
    commented for the first. Check every call site of such a parameter
    against its name and comment.
58. **A declaration wider than its meaning.** A word accessor for a cell of
    which one byte means something forces `& 0xFF00` and `(uint8_t)` into
    the port. Narrow it in `re/ram.toml` (a map fix, not the port's).
59. **A bridge's runs between call-outs left in the `Snes *` body.** A
    bridge may call out mid-body, but the stretches without call-outs (a
    screen's setup, a table's fill) are logic, and belong in `GameState`/
    `Platform` functions the bridge calls.

60. **A member of a finer enum passed to select a coarser case.** A callee
    that only branches on a family (`slot_fits(s, ROTATE_DOWN)` covering both
    rotations, `FADE_TO_WHITE` standing for every fade) reads as if it cared
    which member. Give the coarser case its own type.

61. **A call-out that is really a port.** Check every call-out's target
    against `game/registry.c`: a list headed "the original's routines" can
    quietly include the subsystem's own ports, called through the machine
    where C would do.
62. **Register setup the call's own declaration marks dead.** A carry set
    before a call whose don't-care set includes `DC_C`.
63. **An increment where the value is a bool or a state.** `x + 1` written to
    a cell the guards keep at 0 is `true`, or a named step.
64. **Data accessors that take byte offsets.** `setup_rule(at)`,
    `layout * 14`: item 3 moved into the asset seam, where it looks
    legitimate. Accessors take the index and return a record.
65. **One resource's layout written in several files.** A VRAM, CGRAM or
    buffer place as a literal in two files (item 18 is bits, 43 decisions).
66. **Porting status in the game's vocabulary.** "Which are still the
    original's" in a type's meaning, or an API like `..._filled_here()`:
    what is ported belongs to the guards and entries, not to the logic.
67. **A leaves file not used everywhere.** Where a subsystem derives
    registers from state in one place, a register derived inline in a bridge
    elsewhere is a finding.

68. **A value of one enum used as another's.** `ObjectKind place =
    object_role(g, self).kind` passed as a party place, working only because
    `ROLE_PARTY_1 == 1`. One concept, one type, and a conversion that says so.
69. **Scaffolding written through an accessor named for something else.**
    Marked, but the name still lies about what the cell holds then
    (`set_field_own_tile_x` receiving a table address). Name the cell's
    second use in the map, or use the scaffolding's own writer.
70. **Script data patched by offset.** A command overwriting another script's
    operands (`PUSH_SCRIPT + PUSH_TILE + 1`) is item 3 aimed at code: one
    named operation per patch, beside where the script is defined.

71. **Game state written only in scaffolding.** A cell the logic reads back
    (a quirk the game can see) written only inside `ORIGINAL_EFFECT`: the
    native-shape build then behaves differently. Grep every scaffolding
    cell for readers in logic; a value something reads is game state,
    written as a plain statement and named as such. This is a bug, not style.
72. **Guards that know their entry by `s->cpu.pc`.** Scanning a table's ROM
    address column for the PC is handler identity by machine position, and
    makes routine columns in logic tables look necessary. Generate a guard
    per entry with its kind as a constant.
73. **A handler that calls the continuation.** A condition that itself
    "chooses the block's actions", as the original's `JSR` did, instead of
    returning a result the interpreter's loop acts on.
74. **A header that admits its state is the original's cells** ("the state
    the choosing keeps is the original's, in its cells"). Not a disclosure
    that excuses item 13; a flag to check it.

75. **Logic re-run inside `ORIGINAL_EFFECT` for the cells it records.** Item
    40 turned round: the function then serves the game and the scaffolding,
    and its signature grows to carry the leftover. Scaffolding works its
    values out in a leaves helper from the state, never by re-running logic.
76. **A header naming vocabulary nobody wrote.** A comment pointing at a type
    ("walk.c, `TileShape`") where the code kept a bare `uint8_t`.
77. **A map array whose indices are different variables.**
    `field_chests[0]` the game's list, `[1]` the location's: item 42 in the
    RAM map. Split them there.

78. **A don't-care that rests on a new analysis hint.** When a change adds a
    hint to the liveness analysis (`re/hints.toml`), check the hint against
    the ROM: which registers the routine saves, restores, reads and leaves
    to whatever runs next. A wrong hint kills a live register everywhere the
    routine is called, and every declaration that leans on it passes the
    check. Ask for a test that ties the hint to the disassembly.
79. **A file named for something it doesn't contain.** `save_screen.c`
    holding the slots' summaries while the load screen lives elsewhere.
80. **A shared mechanism whose header states a rule its callers break.**
    `menu_run`'s header says pure-C siblings are called as C; a caller runs
    one through an emulated `JSR`. Item 41 aimed at a bridge's contract.

**Re-reviews are scoped.** After a round that fixes files rated translation, the
fresh review reads those files (and anything their fixes touched), not the
whole subsystem again. The files that passed stand, and their remaining "mostly"
findings are follow-ups. This keeps the loop convergent as the rubric grows;
it never lets a file rated translation merge.

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

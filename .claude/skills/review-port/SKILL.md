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

- **Run the mechanical items first, and don't read for them:**
  ```sh
  python3 tools/portsmell.py --files    # the rubric items a machine can settle
  python3 tools/portsmell.py --list     # which items those are
  ```
  Thirteen of the items below are decidable from the source -- quoted assembly, a
  "presumably", a sibling port called through its ROM address, a `Trace *`
  out-parameter, an enum nothing takes as a type, a flag noted inside a game
  branch, a return frame pushed by hand -- and `tools/portsmell.py`
  owns them. Its findings are yours to report, with the item's number, but they
  are not yours to hunt for: the items it owns are one line each below, pointing
  here. Spend the reading on the items that need a reader.
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
   an operand is encoded (`loop_animation`, `loop_animation_0`,
   `loop_animation_1`): machine-checked, `portsmell.py twin-functions`. What it
   can't see is the cause -- an interpreter dispatching through the ROM's
   pointer table and an emulated `JSR` instead of a C table -- so read the table.
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
12. **The arithmetic unit emulated in the logic.** A hand-written version writes
    `quotient = n / d`, `product = a * b`, and any carry, overflow or leftover
    the callers read is derived *in the wrapper* from those results. Named
    emulations (`add_carry`, `rotate_left`, `adc`/`sbc`) are machine-checked,
    `portsmell.py cpu-arithmetic`; an unnamed one -- a divide as 32 rounds of
    rotate-and-subtract, a multiply as partial products with a chained carry, a
    struct of `carry, overflow, x, y, bank` threaded through the logic -- is
    yours to see, and rates its file **translation**. Where the original's
    rounding really moves a pixel, that is one named function
    (`edge_rounding(...)`) with a comment saying what the player sees, not the
    shape of the whole routine.
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
    is the most blatant form of smell 8: machine-checked, `portsmell.py
    asm-in-comments` (a quoted sequence, or a write to a hardware register; one
    instruction named in prose is the provenance CLAUDE.md asks for). Still
    yours: a per-call array spelling out the original's write order for a
    transfer the service already models (`VID_STEP_*`, `VidSnesXfer`, a DMA
    channel or mode); that order belongs in
    `runtime/platform/snes/encodings.c`.
16. **Stop-point fragments.** Entries that exist only because a routine is
    half-ported, named for the address or constant at the stop
    (`scene_set_vram_6400`, `scene_race_set_vram_0000_again`). Port the routine
    they're fragments of (`load_title_graphics`), or at least name each for what
    it holds.
17. **Admitted ignorance.** "presumably", "what reads the pair isn't ported":
    always a finding, and machine-checked, `portsmell.py admitted-ignorance`. Its
    fix has an owner -- find the reader and name the value, or record it in
    `docs/TODO.md`.
18. **One field, several private definitions.** The same bit defined in
    several files, under different names or meanings (`SCENE_FLAG_LINE_IRQ` and
    `SCENE_FLAG_TITLE_AGAIN`, both `0x80` of `scene_flags`). One definition, in
    the subsystem's header, named for everything it means.
19. **Branch-shaped compares.** `!((uint16_t)(x - LIMIT) & 0x8000)` is a
    BMI/BPL spelled out: machine-checked, `portsmell.py sign-bit-compare`. Write
    the comparison the logic means (`(int16_t)(x - LIMIT) < 0`, or `x < LIMIT`
    where the range allows it), with a comment only where wraparound really
    changes the result.
20. **Scaffolding as table data.** `ORIGINAL_EFFECT` compiles out *writes*, not
    *columns*. A language's row struct carrying the original's Y, scratch
    layout or flags (`.parse`, `.keeps = 0x80|n`, `.overflow`) is smell 9 even
    when every use is marked. The original's leftovers go in a parallel table
    that compiles out with the mechanism.
21. **Two vocabularies, or dead address columns.** Registered entry names that
    disagree with the operation names for the same command (`reset_animation`
    and `stop_animating`), or a column holding ROM addresses that no code reads.
22. **Calling a sibling port through its ROM address.** An emulated `JSR` into
    your own subsystem's port instead of a C call: machine-checked, `portsmell.py
    sibling-through-machine`, which reads every call-out's target against
    `game/registry.c` (item 61 is the same check from the call-out's side).
23. **The scaffolding switch used as a value.** `return ORIGINAL_EFFECTS &&
    overflows16(...)`: the switch has leaked into what a function means.
    Machine-checked, `portsmell.py scaffolding-as-value`.
24. **Adapters that reorder positional arguments.** A dispatch table whose rows
    list operands in one order while the operation takes them in another
    (`(arg[2], arg[0], arg[1], arg[3])`). This is smell 3 moved into the
    interpreter; decode into a named struct.
25. **Stale provenance.** `re/ram.toml` `source` lines and header declarations
    naming files or functions that no longer exist. The file half is
    machine-checked, `portsmell.py stale-provenance`; a named *function* that is
    gone is still yours.
26. **Traces as out-parameters, with twin functions.** Machine-checked in its
    named form, `portsmell.py register-out-params`. A public logic function
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
    The type is the vocabulary. Machine-checked, `portsmell.py
    enum-types-nothing`, for an enum nothing takes as a type at all.
34. **Emulating inputs no game state produces.** A bit-serial division kept
    "for a divisor of 0, or one with its top bit set, which no scene uses". The
    remedy is a guard that refuses those inputs, so the original runs them;
    the logic keeps only the arithmetic the game actually needs (`n / d`). An
    emulation with a comment explaining that nothing reaches it is still an
    emulation. The same holds for a carry or borrow that is constant over
    every input the game produces (a subtraction that can't borrow on any
    real camera): the guard refuses the rest and the sum is written plainly,
    rather than the bit worked out and handed on.
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

57. **A quirk parameter or constant named for one of its uses.** A flag that is the add's
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

61. **A call-out that is really a port.** Machine-checked with item 22,
    `portsmell.py sibling-through-machine`, which reads every call-out's target
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
    per entry with its kind as a constant. Machine-checked, `portsmell.py
    pc-identity`, which reads the PC but not the ports that *set* it (how an
    idle loop's port says where the machine resumes).
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

81. **A guard replaced by scaffolding that reaches game state.** Where a port
    stops refusing rare real inputs and reproduces the original's quirk only
    under `ORIGINAL_EFFECT` (reads past a table), the two builds may diverge
    on game-visible state. That is allowed only as the absent party member
    and the walk's search past the tables do it: the logic stays typed in the
    game's terms, the quirk's effect is applied by one marked line (never a
    local the logic branches on), the library is shown not to reach a
    visible difference, and the divergence is listed in docs/TODO.md.

82. **A table keyed by which original routine runs.** Rows that reproduce
    the differences between near-copies of a routine (`y_rules(layout,
    reload)`) are item 7 moved into data. Each row that differs must say
    what differs on screen, or be shown unreachable and dropped.
83. **One routine's quirk promoted into a platform service.** A service body
    that carries a game's rule (`vid_oam_y` holding one layout's hiding rule
    while its siblings live in the port). The platform keeps the plain
    encoding; the game's rule stays in the port with its siblings.
84. **Result fields that only say which registers are defined.** `placed`,
    `rebuilt`, `overflow`: plain bools with game-sounding names that exist
    to tell a wrapper which flags to set. Items 26 and 45 in their most
    easily missed form; the wrapper asks the state instead.

85. **The original's call stack mirrored in the logic.** A return frame
    pushed by hand outside a bridge is machine-checked, `portsmell.py
    return-address-pushed`. What needs a reader is the frame kept another
    way: an enum of `JSR` sites with a "stopped" check after each call, so
    that a call-out can rebuild the original's frames. It pins the logic to
    the ROM's call graph. Port the call-outs that need it, or have the guard
    refuse what reaches them.
86. **A CPU-flag parameter on a shared service whose only effect is a
    leftover cell** (`battle_multiply_words_carrying(..., carry)`). Once the
    service writes the cell plainly the flag looks like game input.
87. **Scaffolding promoted to game state without naming it.** Item 71's fix
    is to write a value plainly, but the reader must say what the value is in
    the game's terms; a run of "kept plainly" across unrelated files to
    satisfy one reader is the original's register traffic by another name.

88. **An out-parameter dressed as an outcome enum.** Game-sounding members
    that only separate cases the game's own result merges. The check: if
    every member maps onto the same game-state value, it is the registers.
    The tell-tales are a `(void)local;` in an entry whose only other use is
    inside `ORIGINAL_EFFECT`, and public `void` twins of a private function
    that returns the struct.
89. **Cells as the calling convention between two C functions.** A ported
    caller writes cells and a ported callee reads them back as its
    parameters (`menu_set_label(g, menu_load_destination(g))`). Pass them.
90. **The same test written two ways on one field** (`l == 2` and
    `(l & 0x0F) == 2`) with no comment saying why.

91. **Twin functions that differ only in the order they read cells**, kept
    for the open-bus value on an input no game state produces. Item 34 in
    the form of read order: a guard refuses the input and one function takes
    the difference as a parameter.
92. **A hardware cursor as the parameter between two ported functions.** Item
    89 with a register (the work RAM port's address) in place of a cell. Pass
    the destination; leave the cursor's final place as scaffolding.

93. **Flag notes interleaved with the logic's arithmetic.** A note of a flag
    inside a loop or under a game branch is machine-checked, `portsmell.py
    flag-note-in-logic`. What needs a reader is the straight-line form: a
    note beside each add pins the logic to the original's order of
    operations. The test: would reordering two independent adds in a formula
    break the gate? The legitimate form is one leaves helper that works the
    flag out from the final state (`event/contact.c`).

94. **A leaves derivation that decodes state differently from the logic.** A
    helper that works a register out must read the state through the logic
    file's own storage functions, never re-decode the layout with raw
    accessors, or it quietly diverges at the edges (a tile map's column 63
    read as BG1's column 0 in one file and BG2's in the other).
95. **Origin tags passed into a logic helper's signature.** A drawing
    function that takes a table of sites and an index only to tag its writes
    (`redraw_tile(..., RedrawSites at, unsigned n)`): item 45 for tags. Tag
    once, with a literal, where the branch starts its uploads.

96. **A justification that contradicts what shadow mode compares.** A comment
    keeping scaffolding or a register derivation "because shadow mode
    compares X" where docs/PORTING.md says it doesn't. The code may be right
    while its stated reason is wrong, which misleads whoever next decides
    whether it can go. Check the claim against PORTING.md.

97. **Timing fudge dressed as block-move bytes.** `charge_block_move(g, N)`
    where N is a cycle estimate turned into "bytes", not a real fill or copy,
    gets round "a tag, never a number". A port charges only the bytes it
    actually fills or copies; calibration and the timing exceptions take the
    rest.
98. **Aliasing guards that list offenders instead of stating the invariant.**
    A guard that exists because the port keeps a cell in hand checks "nothing
    the call reads or writes touches that cell" on every path, not a list of
    the known ways in.
99. **Two cells read as one machine word.** `(lo | hi << 8) << 1 & 0xFF00`
    reproduces a 16-bit load spanning two variables (item 46's sibling). Read
    each in its own terms.

100. **A dispatch column inside the game model.** A table that says what the
     game does (a `TimerEffect` per status) with the ROM's routine addresses
     in a column beside it makes the ROM's layout look like part of the
     game. Keep routine and entry columns in a parallel dispatch table beside
     the entries.
101. **An inlined subroutine's frame, chosen by a game condition.** Outside
     a bridge, machine-checked, `portsmell.py return-address-pushed`. In a
     bridge (`m_push16(... POISON ? A : B)`) it is acceptable, but prefer
     registering the inlined subroutine as its own entry so `battle_run` or
     its sibling supplies the frame.

102. **A sibling port reached through the machine only so its guard runs.**
     The code gives another reason; the real one is that a C call would skip
     the entry's guard. Call it as C behind a helper that tests the guard
     itself and calls out where it refuses (`event_call_bridge`, the field's
     palette give-up), under a pushed frame where the sibling's call-outs
     need one.

103. **A sentinel the port handles by reaching past the tables in plain
     logic.** A guard dropped so that a "nobody" (`$FF`, one past the list)
     flows through as a bare byte and reads or writes stray cells identically
     in both builds. It isn't item 81's build divergence, so it is easy to
     argue away, but it still needs a typed "nobody", the stray effects in
     one marked place (or the guard back), and a docs/TODO.md entry: native
     storage breaks it.

104. **One decision through two bridge mechanisms in one file.** One sibling
     run through `battle_run` (its guard, then a call-out, the turn carrying
     on in C) and another's guard asked with the whole routine stopping where
     it refuses. Use one: the helper that tests the guard and calls out
     keeps the rest of the routine C either way.

105. **A scaffolding note run only on the original's path through a branch.**
     Machine-checked for flags, `portsmell.py flag-note-in-logic`; a cell's
     note under a game branch (`if (!(moves & X)) ORIGINAL_EFFECT(bit_test(...))`)
     still needs a reader: item 7 inside scaffolding.
106. **A game-sounding bool that is really the carry of an unsigned add.**
     `past_the_top = was + step > 0xFFFF` for a negative step. If the name
     matches the carry only on the inputs that occur, it is the register:
     write the game test (`step < 0`).

107. **A register derived right after a call-out that already leaves it.**
     Before writing a register back after a call-out, check whether the
     callee is still the original's; if it is, the machine already holds its
     value and the derivation is a round trip (item 50).
108. **An operation in game vocabulary observed only by shadow mode's compare
     at the next call-out.** Something written just before a call and always
     overwritten after it is scaffolding however it is named (item 87's
     sibling): put it in `ORIGINAL_EFFECT`, or name the reader while the
     call waits.
109. **A cell "kept plainly" whose only later writer is a sibling piece of the
     same ported routine.** Item 108 when the overwrite is C, not a call-out:
     a piece with its own registered entry looks as though someone might call
     it alone. List the ROM callers of the piece's routine; where the only one
     is a ported routine that overwrites the cell, the keep is scaffolding,
     and the routine's last writer of each surviving cell keeps it plainly,
     named where the routine is described.
110. **One table read through a record named for another use.** The same
     bytes read two ways (a modifier record's routine number read as a stat
     block's offset) want two named views, each with its own accessor.
111. **A multi-byte field carried in pieces.** A 24- or 32-bit value read as
     a word and a byte, added with a hand-made carry and capped a byte and a
     word at a time: give the record a wide accessor, add in one expression,
     and put the original's way of capping in one named function whose
     comment says how it differs from a plain minimum.
112. **Scaffolding written on every pass of a loop.** Where only the loop's
     last value is observed, work the final values out after it and write
     them once (a leaves helper), rather than mirroring the original's cell
     updates pass by pass.
113. **A guard that exists only because the logic loops for ever on an
     input.** Where the game never produces the input, make the loop end on
     it (equivalent on every input the game produces) and drop the guard's
     condition; fuzzing then counts the original's endless run as skipped.

114. **A named game cell that other routines' scaffolding also writes.** A
     cell the logic reads under one name (`battle_jump_lift`) that another
     port's `ORIGINAL_EFFECT` leave writes at its offset (`LEFT_WAY_BACK`):
     the native shape then reads it stale. Check every leave's offset against
     the map's named cells; a candidate for `tools/effects_check.py`.
115. **A register leftover made into vocabulary.** A logic header exporting a
     query whose answer is only what a register held when the original
     returned (`menu_equip_item_described_on_opening`, X left pointing at a
     place): it is a register derivation and belongs in the subsystem's
     leaves, worked out from the state, not named as a game concept.
116. **One parameter list for a whole language.** An interpreter whose table
     decodes every command into one shared set of fields (`value`, `which`,
     `kind`) has items 55 and 10 at once: the names can't say what each
     command's operand is, and no enum can be the type. Each row's
     parameters are named for that command, and the enums it takes are the
     types.
117. **A table declared from below its start.** A map entry whose base is
     moved down so the original's index needs no subtraction (`menu_tech_parts`
     from `$7E:1607`) makes the indices below the real start look valid,
     aliasing whatever lies there without a `reuses`, and the accessor's
     range check can't catch them. Declare the table where it starts and
     index it by what the game counts.
118. **A service result in the hardware's layout.** A call added to a
     portable service (`runtime/platform/*.h`) whose result keeps the SNES
     register's bits (`tm_picture_status`: `$213E` low, `$213F` high, bit 15
     the field) so a wrapper can rebuild A. The service answers the game's
     question (`tm_odd_frame`); what the original's register held is
     scaffolding the SNES backend hands the wrapper, never part of the
     portable interface.
119. **A carry explained for one sign only.** A kept carry from adding a
     step that can be negative (a two's-complement slope) means a carry for
     one sign and a no-borrow for the other, so it is set on nearly every
     line in one direction. A comment that calls it an occasional rounding
     misleads; say what it does for both signs, and name the step for it.

**Items 75 and 88 together.** When the wrapper needs to tell apart cases the
game merges (to derive a flag) and working them out from the state would mean
re-running the logic: first do the port-routine skill's third trap (name the
reader, or prove it dead with `--poison-dc`). If the flag survives, a status
enum is acceptable when every member is a real game condition and everything
that derives registers from it lives outside the logic file (the subsystem's
leaves), never in the logic's own header.

**Re-reviews are scoped.** After a round that fixes files rated translation, the
fresh review reads those files (and anything their fixes touched), not the
whole subsystem again. The files that passed stand, and their remaining "mostly"
findings are follow-ups. This keeps the loop convergent as the rubric grows;
it never lets a file rated translation merge.

**One review per merge, not per chunk.** A review's cost is mostly fixed: this
rubric, the subsystem's files, and the reading that finds what the gate can't.
Paying it per chunk pays it two or three times for the same subsystem. The gate
runs per chunk, because behaviour has to hold at every commit; the review runs
once on the accumulated diff, before the subsystem merges. So:

- A chunk lands on its convert branch when the gate passes. No review yet.
- Two or three chunks in, or when the stream's next chunk would touch files the
  last ones wrote, review the branch whole: the files those chunks wrote or
  rewrote, plus every file of the subsystem the per-file rating covers.
- Review before merging to `main`, never after. A chunk whose review is still
  owed is a chunk whose files can still be rewritten cheaply.

Batching does not weaken the bar: it is the same reading of the same files, done
when there is more to read. What it drops is re-reading the rubric and the
subsystem's headers for each chunk in a run.

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

# Held-loop conversions

### Restartable C loops

The warp's `warp_next_frame` now runs the trip as a C loop. Its frame end
calls `native_poll_held`: the live fiber waits in place, while the saved
machine remains at the existing hold with the return frame identifying the
kind of trip. After waking, the loop reconstructs that kind from the frame
and checks the continuation's full contract before making another picture.
The idle contract requires native mode, M8, the warp's page, an I/O bank and
its unchanged zero cell; the frame also requires X16 and a valid return.
Neither row constrains decimal mode. Refusal stops at `$FD:EAED` without
consuming the return or the frame. The calibrated rates stay unchanged.

`native_poll_held` only accepts the running root continuation's own hold
number and entry address, with no timing scope open. The caller promises
that no local from before the wait is needed after it: all such values are
reconstructed from saved state. Its read-only predicate wakes on either
completion or a refused idle contract; the loop then checks the complete
frame contract. Polls use the same interrupt and status-change boundaries
as the old idle continuation.

`native_suspended` excludes this one reconstructible frame only while the
machine rests at its checkpoint's PC and S. Other active or parked callers
still count, including a caller waiting on another task's stack. Unpaid
time still prevents an exact save. No fiber or callback pointer is saved:
a fresh runtime uses the existing hold number and aliases to enter the C
loop, and an older state with no hold named can still enter by address.
Both warp entry wrappers and their held rows remain for that compatibility;
`machine_joints.py` therefore still counts them. The live trip no longer
redispatches those continuations for each frame or idle poll.

The suspension test compares the C loop's state and pace with the former
held continuations, reloads old, new and unnamed-hold states into fresh
runtimes, and checks the saved caller frame. Its task-switch cases also run
with a checkpoint wait: an enclosing caller stays suspended until its own
stack returns, and a genuine restart discards it. Seven rejected warp
contracts leave the frame and return intact.

Across the 37-movie gate sample, the warp's frame-continuation entries fall
from 414 to two and its idle-continuation passes from 257,754 to 1,572: each
trip enters the C loop once, after its initial compatibility wait. Every
other held continuation's count is unchanged. The machine-entry census
stays at 698,313 after the console conversion. The source counter retains
143 held rows and 505 emulated returns; the explicit refusal adds one stop
(104 to 105), and the polling predicate adds one machine-taking function
(2,001 to 2,002). Timing estimates and baselines are untouched.

`events-end-of-time-gate`, loaded from its cartridge save, supplies the real
save/load check. Frame 4,000 rests at `$FD:EAED`, hold 2, S `$06FD`, with no
suspended-routine or owed-time warning. The old and new builds serialize
every field identically there. Old and new mid-wait saves loaded into fresh
new runtimes both reach frame 4,200 with every serialized field identical
to uninterrupted play, including the C sound driver and save RAM. Both
200-frame traces also match the old build's reload, including the framebuffer.

The sampled gate with `--evidence` ends `suite OK`: 37 movies and both fast
routes pass, with no real-play refusals or fallback instructions. The
suspension, state, cartridge-save, debt, time-parts and shadow tests pass,
as do the 28 targeted Python checks and the port-style and smell ratchets.

## Field shared frame wait

The 37-movie census counts 142,734 entries into the field wait instead of
60,007,940 polling dispatches. Every other held continuation and C-call count
is unchanged. Total held dispatches fall from 159,848,871 to 99,983,665;
machine entries remain zero.

The floor-switch movie, loaded from its cartridge save, rests at frame
2,800 at `$C0:EC71`, hold 5, S `$06FD`, with no suspended caller or unpaid
time. Old and new builds serialize every field identically there and at
frame 3,000. Old and new mid-wait saves loaded into fresh runtimes reach
that same final state, including sound and save RAM; both 200-frame traces
match the preceding build's reload. Suspension, state, debt, timing-scope
and cartridge-save tests pass, as do 28 targeted Python checks.

## Field and menu loop measurements

The field wait's 37-movie census falls from 60,007,940 polling dispatches to
142,734 entries into its C loop. The floor-switch movie, loaded from its
cartridge save, rests at frame 2,800 at `$C0:EC71`, hold 5, S `$06FD`, with
no suspended caller or unpaid time. Old and new builds serialize every
field identically there and at frame 3,000. Old and new mid-wait saves
loaded into fresh runtimes reach that same final state, including sound and
save RAM; both 200-frame traces match the preceding build's reload.

The menu upload wait's sampled dispatches fall from 48,112,971 to 117,478;
the sprite-object frame wait's fall from 1,281,651 to 3,130. Other held
continuations and C-call counts are unchanged by the menu step. Across
these conversions total held dispatches fall from 159,848,871 to
50,709,651, with zero machine entries on the 37-movie sample.

At frame 4,000, `menus-field-menu` rests in the upload wait with hold 10 and
S `$09FF`; `menus-epoch` rests in the frame wait with hold 14 and S `$06FB`.
Both start from cartridge saves. Old and new states reload into fresh
runtimes and reach frame 4,200 with every serialized field identical to
uninterrupted play and matching 200-frame traces. The equipment-and-save
movie also ends with identical serialized state and save RAM. The sampled
gate ends `suite OK`; style and smell checks have no new findings.

## Scene wait named before its C loop

The scene program's main loop is the first held wait (`HOLD_SCENE_MAIN_LOOP`):
the first frame (`first_frame_on_the_machine`) holds it, and its two passes
(`attract_wait_loop`, the empty one, idle; `attract_frame_pass`, a picture's
scaling, a port) are its continuations. These are the measures over
attract-demo and title-start:

| Measure | Before | Now |
|---|---|---|
| census, `$7E:3023` entered by the machine | 4,470,854 wait passes and 103 frame passes (100 by stop, 3 by return) | 0 |
| census, held runs | none | 4,470,854 and 103 |
| census, all entries by the machine | 9,285,097 | 4,814,140 |
| attract-demo, driven a frame at a time in `ct_drive --native on`: frame ends with a routine suspended | 12,329 of 22,500, in 56 runs, the longest 2,142 | the same frames |

The wait passes match one for one, because the continuations are the same
functions as the idle entry and the port. What changed is how the runtime
reaches them: by the wait's number, with no address lookup. `tools/machine_joints.py` counts the two rows apart, as
held waits' continuations: they are still machine wrappers, a joint until the
loop runs with no machine under it. A `ct_play` quick save taken at attract-demo's
frame 12,000 (PC `$7E:3023`, wait 1 named), loaded, and played 500 frames ends
with the same state and the same picture as the run that didn't stop.
The program then retired the gate (REIMPLEMENTATION.md, "The scene program's
teardown"): its continuations are `scene_program_waits` and
`scene_program_next_pass` (`game/scene/program.c`), `.retired`, and the pass
makes the call the setup named and, once a script ends the scene, the
program's end, by a return rather than a stop.

## Overworld wait named before its C loop

The overworld's main loop is the third (`HOLD_OVERWORLD_FRAME`), and the
first whose wait's code other loops share: its head calls the sprite-object
mode's wait for the next frame (`$C2:23CF` calls `$C2:046E`, which jumps to
`$C2:0346`), whose loop (`$C2:0348`) the Epoch's screens and the loop's own
fades wait in too. So the wait is held at the shared loop, and its
continuations (`overworld_next_frame`, the frame counted: the wait's return,
the mode's arm and the wait again; `overworld_waits_for_the_frame`, idle)
take it only where the wait's return on the stack is the loop's pass
(`$C2:23D2`); the mode's own idle entry there is found only by the address
path, where no wait is named. The halt the loop can end in, a loop with the
interrupts off, holds a wait of its own (`HOLD_OVERWORLD_HALT`). Over the
four world-map movies (`field-65m-bc`, `-dactyl`, `-dactyl-flight`,
`field-12kbc-1f4`) and `menus-epoch`, shadow mode compared
`overworld_next_frame` 5,463 times, every one matched, and the shipping mode
played each frame for frame as before (`tools/trace.py diff`: work RAM, VRAM,
CGRAM, OAM, save RAM); on `field-65m-bc-dactyl` the machine's wait-loop
passes went from 377,674 to 214,949, the other 162,393 now the held wait's.
The overworld then retired the gate, and its three continuations are
`.retired` (REIMPLEMENTATION.md, "The overworld's teardown").

## Scene loop measurements

On the 37-movie sample, scene idle dispatches fall from 39,271,842 to 59,
and race upload dispatches from 8,881 to 2,170. Every other held count and
C-call count is unchanged. Total held dispatches fall from 50,709,651 to
11,431,157; machine entries remain zero.

Attract-demo's frame 12,000 rests at `$7E:3023`, hold 1, S `$06FF`.
The jet-bike race, loaded from its cartridge save, rests at frame 7,810 at
`$7E:5211`, hold 9, S `$06FA`. Both checkpoints have no suspended caller or
unpaid time. The preceding build and the loop build serialize every field
identically there, and at the next exact checkpoints (12,200 and 8,140).
Old and new saves loaded into fresh runtimes reach those same final states,
including sound and save RAM; the 200- and 330-frame traces match.
Suspension tests cover complementary guards at one head, including
mid-frame saves in the wait and its interrupt, changed contracts and
separate task stacks. They assert that normal wakes stay named holds and
that a ready continuation still enforces stricter index-width and decimal
preconditions than its idle loop. The sampled gate ends `suite OK`, with
zero real-play guard refusals and fallback instructions; the style and
smell ratchets have no new findings.

## Source boundary inventory corrected

The earlier source counter reported 103 stops, five address call-outs and
two sibling lookups after the scene and overworld loops. Counting the
actual APIs instead gives 111, 14 and nine, without changing any game code.
Eight `m_hold_at_own` sites were omitted. Four `call_out_*` name hits were
already checked C helpers (two definitions and their two uses), while
`m_jsr`/`m_jsl` wrappers were omitted. Address-specific and held-continuation
lookup APIs add seven previously omitted registry dependencies. The
corrected patterns, comments/strings/declarations and macro forwarding are
covered by eight tests. These are measurement corrections, not new machine
joints or claimed conversions.
Applying the corrected patterns to the sources before the scene and world
loop conversions gives the same 111/14/9 counts. The conversions reduce
held continuations from 139 to 136; they add no machine boundary.

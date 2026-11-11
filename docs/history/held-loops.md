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

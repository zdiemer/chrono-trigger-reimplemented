# The first console hand-offs

The measurements before the frame driver took the interrupts and the field
game-over restart became a direct call. The current mechanism is described
in REIMPLEMENTATION.md and RUNTIME.md.

## The console's direct hand-offs

The shipping console calls its next step as C: reset calls the boot, the
boot calls the field's first start, and each interrupt vector calls its RAM
stub. `core/console.c` keeps a shared checked tail-call boundary. It moves to
the original entry, passes the caller's time with `native_sync_boundary`,
then checks native CPU mode, the callee's P mask and its guard before a
`TIMED_AS` call. An empty vector still yields at that boundary, preserving
the driver's minimum step and interrupt poll. A refusal stops at the
original entry with the original don't-care set and stack intact. Shadow
mode retains the separate comparisons; no registry rows, wrappers or
timing estimates change.

The boot has no P restriction; the field's first start takes M8/X16. The
stubs take any P in native mode, including decimal, and require a jump or
return installed in the corresponding RAM vector. None of these four
callees has an additional page, bank or code-image guard. The suspension
test exercises both vectors at all 256 status bytes, with valid return
stubs, invalid stubs and emulation-mode refusals, checking the destination
and the complete interrupt frame.

Over the 37 movies in `tools/gate_movies.txt`, the census's machine entries
fall from 1,396,661 at seven distinct entries to 698,313 at four. The two
RAM stubs account for 698,272 removed entries. Every held continuation's
pass count is unchanged (160,105,465 in total). The remaining entries are
the two interrupt vectors, power-on reset, and the field's first start
from game-over. `machine_joints.py` and its per-file report count 107 to
104 stop sites: four hand-offs share one refusal stop. Registered entries
(13), held continuations (143), address call-outs (5), registry sibling
lookups (2) and emulated returns (505) are unchanged. The boundary helper
adds one machine-taking function (2,000 to 2,001).

The sampled gate with `--evidence` ends `suite OK`: no fallbacks or refusals,
both fast routes reach their outcomes, and all strict/native outcome
ratchets pass. The suspension test and all 595 Python tests pass; the style
and smell checks have no new findings.


## The frame driver's interrupt calls

In shipping mode the frame driver calls the console's vblank and line
vectors as C continuations. `frontend/natives.c` supplies their functions to
`native_attach_frame_interrupts`, which selects the enabled hand entries
once by function. Each interrupt selects one of those two continuations by
kind; it does not search the registry by the machine's PC. Entry filtering,
shadow mode and the oracle timing mode retain their former paths.

Interrupt recognition, entry time and the return frame still belong to the
machine. After an entry sequence, `snes_run_frame` records which interrupt
was taken and calls its continuation on the next step. Before starting its
fiber, the runtime pays debt and handles interrupted or parked callers as
it does for an ordinary entry. It checks the original destination and the
entry's complete contract, including native mode, P mask and guard. A
refusal leaves that destination and return frame for ordinary dispatch.
The console's checked stub and handler calls remain unchanged, including
their estimates and dispatch boundaries.

`snes.frame_interrupt` saves the pending kind, never a function or fiber
pointer. Loading preserves the destination host's callback; cloning drops
it. A state with no such field starts with no event queued and enters by
the retained vector row. A host without the callback similarly uses the
ordinary path. No new suspended frame exists between interrupt entry and
the host's call; once a handler starts, the existing suspension and debt
rules still decide whether an exact save is possible.

The suspension test compares every serialized field over eight frames with
and without the host calls, with line and vblank interrupts, unpaid port
time and a nested NMI. States captured mid-frame before either handler
reload into fresh runtimes, both with the queued kind and with only the
legacy destination, and finish identically. Eight invalid contracts or
destinations preserve the interrupt frame. Task-switch tests also take a
host interrupt while a caller is yielding. The state test checks the new
field's range and excludes the hook from serialization.

Over the 37-movie gate sample, machine entries fall from 698,313 at four
entries to 41 at two: 37 power-on resets and four field-first-start stops.
The only changed C-call counts are the 461,909 vblank and 236,363 line
vectors. All 159,848,871 held-continuation dispatches are unchanged by entry.
The source joints remain unchanged, including 105 stops and 143 held rows;
compatibility wrappers and returns remain. A real warp save at frame 4,000
has every existing field identical to the preceding build. Old and new
mid-wait saves reload to frame 4,200 identically to uninterrupted play,
including save RAM and the C sound driver; both 200-frame traces match the
preceding build, including the framebuffer.

The sampled gate with `--evidence` ends `suite OK`, with no real-play
refusals or fallback instructions. The suspension, state, debt, timing-scope,
shadow and cartridge-save tests pass, as do the 28 targeted Python checks.

## Driver power-on

Binding reset to the frame driver removed the sample's final 37 machine
entries, without moving any of its 159,848,871 held dispatches. Source
counts at that step were 143 held rows and 105 stops. Old states without
the driver event field resumed their saved address without queuing reset.
State and suspension tests passed, and the sampled gate ended `suite OK`.

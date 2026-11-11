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

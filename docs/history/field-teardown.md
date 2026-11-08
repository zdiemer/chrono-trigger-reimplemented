# Field and event teardown preparation

## Initial audit at 7f7c7f46

The audit below measured the remaining machine entries. Its interpretation
of outcome exceptions as blockers was superseded: retirement relies on the
protected identities, while retaining the recorded debts. Still-gated callers
may keep documented machine forms and scratch writes.

- **Field and event teardown: replacement evidence still blocks retirement.**
  Audit of `7f7c7f46`, after the held waits, with an independent Release build.
  Neither subsystem's comparison can retire yet. The field's main loop is
  held, so the former whole-screen suspended-loop obstacle is not the reason
  to keep it. The remaining machine calls need explicit callers and
  continuations, and the replacement observations still leave gameplay and
  drawing differences unresolved. Keep both subsystems' registry rows,
  guards and scaffolding until those preconditions are closed.

  **Fresh census and execution evidence.** Six movies, 39,953 frames:
  `field-floor-switch` (3,501), `field-factory-switch` (8,387),
  `field-northern-ruins-switch` (3,811), `field-12kbc-1f4` (2,953),
  `events-end-of-time-gate` (5,600), and `events-luccas-mother` (15,701).
  Run `tools/census.py --ct-dev build/main/ct_dev.exe --rom <rom>` with
  those `--movies`, `-j 1 --entries --keep build/field-teardown-census`.
  Shipping mode enters 96 entries by the machine, 1,928,243 times;
  771 entries run only as C. Of these, field has 20 machine-entered entries,
  150,357 times (111,718 calls, 38,513 stops, 79 returns, 47 wait passes),
  and event has 3, 81,903 times (81,899 calls, 4 returns).
  The loop head `$C0:0097`, backedge `$C0:00BD` and frame wait `$C0:EC71`
  each have **zero machine entries**; their held continuations run 35,
  18,426 and 8,597,898 times respectively.

  The remaining boundaries are not all the same problem:
  - The interrupt stubs hand off to `field_nmi` 19,280 times and
    `field_irq` 19,198 times. `core/console.c` can call their machine forms
    with their full contracts, as it already does the object-mode handler;
    these counts do not require a loop suspended across frames.
  - `field_run_queued_turns` calls `event_run_object_script` by address
    81,899 times. Event therefore is not already a subsystem whose callers
    are all C calls. Preserve a turn's nonreturning location change and
    battle handoff when replacing that bridge, not just its normal return.
  - `$C0:EC60` still has 742 entries (728 calls, 14 returns), and screen
    startup `$C0:0B38` has 44 initial machine passes. Event's immediate
    location change reaches `$C0:3B71` and its continuation `$C0:3B76`
    three times each in `events-luccas-mother`; neither is held.
    `event_party_back_together` returns from the shared wait once.
    Name these callers' continuations before removing their machine forms;
    preserve a saveable frame boundary and arbitrary callers' returns.
  - Still-gated menu callers return into `field_enter` 11 times: 5 from
    `menu_wait_for_frame_loop`, 6 from `menu_run_the_tasks`. Boot stops at
    `field_first_start` 6 times. The retired scene and warp continuations
    also call field frame work and its wait through the machine. These are
    observed callers, not an exhaustive inventory of battle/menu bridges.

  `tools/fallbacks.py --by routine` on fresh shipping runs of the same six
  movies reports **0 instructions**, 0 refusals and 0 open instructions;
  every run reports 0 generated blocks reached. No generated code touches
  field/event state in these runs; this is sample evidence, not a fresh
  whole-library claim. `tools/burndown.py --by subsystem` reports 59.4 KiB
  code / 59.2 KiB ported for field/events and 12.5 / 12.5 KiB for field
  graphics (both round to 0% left). That rounding is not proof of coverage.
  `tools/unreached.py --check` passes: 463 entries, 84 library movies.
  Its field inventory has 202 entries, 2,229 unreached bytes, 61 fuzz rows
  and 7 safe rows (92 bytes); event has 250 entries, 1,490 unreached bytes,
  65 fuzz rows and 10 safe rows (72 bytes). Neither has an open row.
  This checks the existing ledger, not a fresh replay of its fuzz recipes.
  Keep the bounds, immutable-resource arguments and explicit native failures
  required by "Keep the unreached ledger's preconditions" above.

  **Replacement evidence is not ready.** The existing outcome ledger
  records all six movies in both modes; recording missing rows is not the
  gap. Its protected identities are in `tools/outcomes_baseline.txt`:

  | Movie | Strict pictures / places | Native pictures / places |
  |---|---:|---:|
  | field-floor-switch | 8 / 5 | 7 / 5 |
  | field-factory-switch | 11 / 6 | 8 / 6 |
  | field-northern-ruins-switch | 9 / 6 | 7 / 6 |
  | field-12kbc-1f4 | 6 / 6 | 5 / 6 |
  | events-end-of-time-gate | 9 / 5 | 4 / 4 |
  | events-luccas-mother | 34 / 13 | 25 / 13 |

  All six protect both end digests in strict mode; native protects all six
  save-RAM digests and five saved-game digests, excluding End of Time.
  All twelve movie/mode pairs still except picture differences, and native
  End of Time also excepts saved-game and place differences. These are
  inventories of existing protection, not fresh outcome replays or claims
  of equivalence. Resolve the differences needed to protect the field's
  drawing and event progression before dropping their comparisons.
  The fresh `--scaffold-reads` run also reproduces **984 reads**, from frame
  12,470, of the object-row removal's scaffolding at `$7E:0F00` in
  `events-luccas-mother`. Decide and test that live in-view alias before
  deleting the write ("What only the original's memory has, past the object
  tables"). Event additionally cannot discard the scratch value feeding
  the still-gated menus' wave phase ("The menus' wave phase starts from
  event's scratch cell"). Retiring events first does not avoid these shared
  state and observation requirements.

  **Measures remain unchanged by this documentation-only audit.**
  `tools/machine_joints.py`: 2,136 registered entries, 13 held continuations,
  993 emulated returns, 99 stops, 8 call-outs, 3 registry sibling lookups,
  8 machine time tags, 24 register escapes, 551 video calls and 1,844
  scaffolding writes. In `game/field/` alone: 135 returns, 29 stops,
  2 call-outs, 1 sibling lookup, 1 register escape, 72 video calls and
  152 scaffolding writes; in `game/event/`: 12 returns, 8 stops and
  114 scaffolding writes. The census above is likewise the before/after
  value: no runtime code changed. Local evidence is in
  `build/field-teardown-census.txt`, `build/field-teardown-census/`,
  `build/field-teardown-fallbacks.txt`, `build/field-teardown-fallbacks/`,
  `build/field-teardown-unreached.txt` and
  `build/field-teardown-joints-summary.txt`. The sampled gate was not run:
  this stops at the failed retirement preconditions, with no gate pass claimed.


## The held waits before retirement

The following measurements describe the preparation merged before this teardown.

### The field's held waits

The field's waits are named before its teardown; every registry row stays.
`HOLD_FIELD_FRAME` holds the shared frame wait at `$C0:EC71`, and
`HOLD_FIELD_SCREEN` the picture's startup wait at `$C0:0B38`. The first
frame is held too, the main loop's full entry contract is checked before
its direct call, and the retained `field_main_loop_goes_round` compares
the same next-pass implementation as the held continuation. Its registry
row names `field_frame` as its `shadow_body`: shadow mode recognizes the
head reached at the same stack depth inside a returning pass as part of
that comparison. The two entries keep distinct wrappers, without reading
the PC to tell them apart; independently entered frames still compare.
Its don't-care
set is the loop head's existing set:
A's high byte and overflow remain live. The runtime preserves a hold across
an idle return into another of that wait's continuations, with the new S;
address lookup prefers retained rows over their held aliases.

**One frame wait, not one per caller.** The return already on the emulated
stack identifies who waited. The idle pass only reads the pending-frame
byte and returns when the vblank handler has taken it. Two return addresses
are main-loop continuations: `$C0:00BD`, the next complete pass, and
`$C0:00A7`, the walk after a request. Other returns resume their caller as
before. The four movies below exercise the full loop, requests, fades,
menus, an event party regroup and a battle return. Their census keeps the
same 406 machine calls/returns into the wait's setup (`$C0:EC60`); its
2.18 million polling passes, and the main-loop passes, no longer enter by
address. A fade or event command waiting through a call-out can still be
suspended; the held wait does not invent a continuation for that caller.

**Timing and contracts.** Before calling the loop head or its frame-end
wait directly, the caller keeps the preceding dispatch boundary and
checks `native_hand_entry_at`, including the full P mask, widths, page,
bank and code guard. Refusal stops at the original entry. Inline work keeps
the original entries' estimates; no estimate changes. The next-pass wrapper
uses `NATIVE_TIME_AS_SIBLING` for the field pass: its timing error belongs to
`field_frame`, including the inlined screen effects, rather than the loop
backedge. This changes report ownership only. In the Death Peak movie its
110,607-clock worst stretch remains covered by `field_frame`'s existing
exception; no exception is added for the wrapper.
The walk continuation uses its retained wrapper and cost without an extra
continuation charge. The screen wait keeps the existing vblank predicate
and idle exit time; its handler-enable continuation keeps the original
width and I/O-bank contract.

The backedge makes no accesses: `tm_sync` alone would skip its empty
stretch, losing the driver's minimum step and interrupt poll at the old
stop. `native_sync_boundary` yields even that empty stretch; the frame-end
wait still uses `tm_sync` to settle the pass's work. Without the empty
boundary, `attract-demo` first diverged from main at frame 2,727 and its
second demo cycle later slipped four frames. Keeping it restores the first
5,000 frames byte for byte and all 22,500 frames' existing strict outcome
expectations, without changing their exceptions. `suspend_test` also
compares an empty backedge with its direct continuation for eight frames,
including the vblank interrupts, with no ROM.
The full demo trace has no divergence outside the stack page, including
every framebuffer hash. Its first remaining difference, frame 5,843, is
two bytes at `$7E:06F8` and `$7E:06FA` below both runs' S (`$06FD`); both
are waiting at `$C0:EC71` with P `$20`. These are stack residue left by the
different dispatch paths. The four field traces below remain entirely equal.

**Measured against main (`bfd75c49`).** Shipping mode over `field-floor-switch` (3,501
frames), `field-factory-switch` (8,387), `field-northern-ruins-switch`
(3,811) and `field-12kbc-1f4` (2,953):

| Census measure | Before | Held waits |
|---|---:|---:|
| `$C0:0097`, machine entries | 4,132 | 0 |
| `$C0:00BD`, machine entries | 4,114 | 0 |
| `$C0:EC71`, machine polling passes | 2,182,034 | 0 |
| `$C0:0B38`, machine entries/passes | 6,723 | 25 |
| All machine entries | 3,525,734 | 1,324,628 |

The named frame wait runs 2,182,293 idle passes, 4,114 complete-loop
continuations, 18 first passes and 2 request continuations. Screen startup
has 6,698 held idle passes and 25 handler-enable continuations; its 25
remaining address entries are the timing service's initial calls.
`tools/trace.py diff --framebuffer` finds no divergence over all 18,652
frames in WRAM, VRAM, SRAM, CGRAM, OAM or the framebuffer hashes.
At floor-switch frame 2,800, `ct_drive` saves at
`$C0:EC71` with wait 5 named and nothing suspended. A slot reload and a
fresh process loading the file reach frame 3,000 with every state field and
the PNG identical to uninterrupted play; replayed, inexact and lost loads
all stay zero. `suspend_test` covers the returned stack depth, save/load and
the retained row's shadow comparison independently of the ROM, including
a deliberately wrong inline body that must fail that comparison.

The floor-switch shadow run compares 682 returning passes, 4 first passes
and 779 frame-wait setups, all matching and none interrupted. Fuzz seed 1
compares the frame-wait setup on 5,000 arbitrary states, all matching.
With the registry available, the loop's recipe preserves the resident
field's object/script graph and randomizes registers, flags and the wait
byte (clear, set and wrapping): 2,000 matches, no skips or mismatches.
Loose-RAM trials reach existing field-pass failures too (TODO.md); they are
not counted as successful validation. Wrong-page and wrong-bank refusal
trials with the registry compare 1,000 each, all matching. The final 34
focused Python checks for fallback accounting, gate integration, the port
subset and platform access pass, as do `suspend_test`, `state_test`,
`shadow_test`, `debt_test` and `time_parts_test`.

The 37-movie sampled shadow gate is clean: 147,080 returning passes,
221 first passes and 153,854 wait setups compare, all matching. Another
1,586 returning passes, 1 first pass and 28 wait setups are interrupted;
those calls are not comparisons, and the coherent-state fuzz recipe does
not close the loose-RAM follow-up in TODO.md. The sampled timing check
passes with the existing exceptions and unchanged estimates.

The complete sampled gate with `-j 1 --evidence`, without long routes,
ends `suite OK`: all 37 movies pass both strict and native outcome ratchets,
and the evidence stage passes its 84 library replays. Shipping fallbacks
and real-play guard refusals are zero. The existing outcome differences
remain covered by their unchanged ratchets; this does not claim that every
native outcome equals the reference.

**Fallback accounting.** Both idle rows declare the return span
`$C0:EC71-$C0:EC76`, so the measure recognizes the final pass as the idle
port's work, just as it recognizes the passes that keep waiting. Movie
cutoff prefixes are not waived: the bounded tail certifies only
the exact idle passes it completes ("A movie ending inside a comparison",
above). `shadow_test` checks loop and return completions, an interrupted
pass, an exit outside the return span, and a later pass that cannot close
an earlier one. The reader and gate integration tests pass (24 tests).
Runtime handoffs settle an already-completed idle return before discarding
its tracking, as the next instruction hook would: Golem Twins' game-over
replay exposed that ordering. Six targeted shadow replays pass the fallback
ratchet without idle allowances; floor-switch and save-point certify their
partial prefixes in one extra frame.
Over the complete sample, 28 cutoff idle instructions are certified; the
fallback ratchet reports no new or increased routine after those certificates.
The unreached-path audit replays its recipes and passes for 463 entries over
the 84-movie inventory. Its existing location-error argument keeps the same
guard and six bytes: only `field_enter`'s source fingerprint is refreshed,
with the reason that a successful entry now names the frame hold.

Three movies end inside a battle called by the field pass: `battle-menu-buttons`,
`battle-magus` and `late-omen-terramutant`. Their field comparisons remain
open after the bounded 120-frame tail. Restoring only the old backedge
wrapper for those replays measures 6 instructions each under `field_frame`;
the returning wrapper measures the same work plus its backedge, 7 each.
`fallback_baseline.txt` therefore keeps 21 unresolved instructions under
`open C0:00BD`, with that reason. The old `open C0:0097` allowance awaits
the batched library update; it is not pruned on this sample. Shipping-mode
fallbacks over the sample and its two fast routes are zero.

`tools/machine_joints.py`: registered entries remain 2,136; held
continuations 7 to 13; emulated-stack returns 994 to 993; stops 97 to 99
(the two direct-call refusal paths); all other measures unchanged, including
1,844 scaffolding writes. This is preparation for teardown, not retirement.



## The callers before retirement

### The field's callers and held continuations

The console's interrupt stubs call `field_nmi` and `field_irq` directly,
through the same contract check as the sprite-object handler. The check
sees the handler's PC and the interrupted stack, and keeps every registry
precondition; a refusal stops at the handler's original entry. The queued
script turns likewise call `event_run_object_script` directly, with the
call's return pushed before its full contract is checked. A turn that
changes location or enters a battle can leave without returning; its caller
does not continue the queue in that case. Both calls keep their existing
calibrated time tags. A former call-out settles its caller's time after the
call frame is pushed, and its callee's time before resuming the caller;
those stack accesses remain charged to the caller. These hand-offs preceded
the events' retirement below; the field's comparison rows remain for its
separate teardown.

The fades, menu returns, tile-copy stages, battle returns, scene close and
ripple now resume from named held continuations. Picture startup returns
through its held caller at `$C0:28A4`, and immediate location changes and
restarts use `HOLD_FIELD_LOCATION` and `HOLD_FIELD_RESTART`. Changing location
resets the stack, ends the old comparison, then starts the new field as a
held continuation with `field_enter`'s full contract. None of these waits
keeps its field caller suspended across a frame. Battle and menu tasks still
keep their own machine interfaces. The runtime indexes all held rows instead
of silently keeping only sixteen; `suspend_test` checks saving and restoring
a wait after more than sixteen preceding held rows.

On the same six movies and 39,953 frames as the initial inventory, the census
falls from 96 machine-entered entries / 1,928,243 calls to 85 / 1,807,034.
Events fall from 3 / 81,903 to **zero**. Field falls from 20 / 150,357 to
12 / 111,049; its remaining entries are ordinary calls and the retained
mode interfaces, not wait-loop passes. The shared wait's setup `$C0:EC60`,
screen startup `$C0:0B38` and location wait `$C0:3B71` have zero machine
entries. All protected identities hold in the sampled movies plus
`events-luccas-mother`, in strict and native modes. The changed existing
outcome exceptions carry their attribution and retain every protected match.

The reference fallback ledger records two changes of comparison owner.
The race win movie's four known wait/gather instructions are now attributed
to `scene_boot`; three battle movies each leave their battle-call instruction
under `field_requested_battle_starts`, still open after the 120-frame tail.
These were already the race's unported cutoff and the returning field
pass's open stretch. Shipping runs still execute zero original instructions.
The old library-wide rows remain until the batched ledger update.

A row-tail removal's effect on visibility is game state, not scaffolding.
During Lara's rescue it leaves object zero present to collision and script
conditions but excluded from normal object updates. `ObjectView` names this
state `PRESENT_WITHOUT_UPDATES`; `object_apply_row_tail_visibility` owns the
storage rule. A predecessor of object zero clears visibility instead, and
other admitted end markers select other objects. A subsequent ordinary
visibility update replaces the state. The write remains byte-exact for the
callers still compared, and now survives the build without scaffolding.
`field_test`, linked against that build and requiring no ROM, covers these
cases and removal from the middle of a row without a visibility change.
The original six-movie inventory is in history/field-teardown.md. This is
preparation for the field's retirement: its byte-exact comparisons still apply.



## Held-path evidence before retirement

The held-continuation preparation also closes the screen effects' unreached
paths: nine cases of 200 matched trials cover all 283 bytes previously
claimed by two whole-call close/ripple recipes. Each begins at its own held
boundary. The settling counter and circle center survive those boundaries
as state. The recipes keep the line-effects selector within its existing
guard's three layouts; a selector beyond that table runs unrelated original
code and reads registers the valid layouts leave dead.

The empty-battle return at `$C0:0281` has 150 matched trials (50 refused by
its existing guard), and the stored-place fade at `$C0:02E8` has 200. With
the after-battle entry's 913 matches, those held-boundary proofs preserve
all 60 bytes claimed by the previous whole-call recipes. The full fresh
unreached check still covers 213 entries and the 84-movie library union.
Its replay selects 70 cited recipes and 23 additional plain recipes; the
restart idle wait's five bytes retain their reviewed timing-escape argument.



## The event-only boundary

Against the audit's 2,136 registry entries and 13 held continuations,
`machine_joints.py` now counts 1,886 and 69 respectively; stack returns fall
from 993 to 991 and scaffolding writes from 1,844 to 1,837. The six protected
movies' preparation census entered no event routine through the machine,
down from three entries and 81,903 entries by address in the audit.

The sampled gate with evidence passes all 37 movies and both fast routes.
The extra Lara movie also passes the strict and native outcome checks.



## The field unreached audit

**The field, inventoried ahead of its teardown.** The field joined the audit
before its paths were all checked, so the ledger takes a fourth kind of
row for it: `open`, bytes neither compared nor argued, which `unreached.py`
accepts only for a subsystem in its `OPEN` list and `--check` lists entry by
entry as what that subsystem's teardown waits on. They are a ratchet, so lost
evidence can't turn into them: `--record` adds no open byte the ledger it
replaces didn't have (only a first inventory, `--admit-open`, does), and
`--check` fails past the subsystem's cap in `OPEN`. A recipe that mismatches
or reports a diagnostic fails the record rather than leaving its bytes open,
unless `[failing]` in `tools/unreached_cases.toml` names it with its reason,
as does a recipe the previous ledger cited with no fresh clean result. A
teardown moves its subsystem from `OPEN` to `RETIRED`, which fails the check
while it has an open row.
Of its 202 entries, 51 have code no movie runs. The field's own recipes
(`tools/fuzz_routines.toml`) executed 1,598 of those 2,229 bytes as they
stood; targeted cases took 348 more. Most of what was left sits behind the
field's frame wait (`$C0:EC60`), which spins until the vblank handler clears
its count and so never returned under `ct_fuzz`: `--tick-at C0EC71=7E0152`
stands in for the handler on both machines, the count wrapping to zero, and
the ripple, the field's close into a scene, the fade after a battle and a
battle request that finds no enemy run to their ends. The rest took a
state the recipes didn't make: the window design sent at a vblank, a
choice's pointer at its bounds, the view's edges clamped, a dialog's
speaker leaving view, every sprite block given. Six arguments are
reviewed instead: a vertical line-up arm and the 8-sprite layout's carry
past 255 in the screen's upper half, both dead by arithmetic; a tile set's
eighth block and BG2 drifting left, which no location's set or map has; a
location load error only a word the entry's guard refuses reaches; and a
party place's slot held by other colours, which the guard turns away. The
216 bytes the first inventory left open were closed, and what fuzzing found
on the way fixed. The original files a party place's object joining its
slot's sharers by the older sharer's number where the slot's belongs
(`$C0:71FE-$C0:7210`): the port does too (`join_party_place_sharers`), so
`field_take_palette:unreached_party_shares` went from 897 of 1,000
mismatching to none. The map's unpacking and the set-up after a menu had
mismatched on their plain recipes since the unpacker became C (the data
subsystem's teardown): their random place of the location's record named
other bytes as a map or a tile set, whose output ran past its bank onto low
RAM, the stack and the unpacker's own cells, which the original reads back
as it goes and the C keeps in locals. The game keeps only a place where a
record starts, so the map's, the script's and the set-up's guards take only
that (`location_record_is_one`) and their recipes fill 32 records. A step's
recipe read the map's tables past their 64 columns (`accessor out of
range`) for a random repeat mask, which the recipes now give as maps make
it: every 16, 32 or 64 tiles each way. The rest took targeted cases: the
dialog read on at each break, placed for the field's message and setting
each break from text made for it in work RAM; a gathering cut short after a
menu; a one-off animation's end; and the leader nudged around another level
into a shut way, which a tile half one level and half another makes about
once in 20,000 trials (that case runs 100,000).



## Validation follow-ups at retirement

- **The field's arbitrary-state fuzzing and battle boundary.** Preserve
  empty dispatch boundaries too (`native_sync_boundary`): settling only
  accrued accesses skips the driver's minimum step and interrupt poll.
  Three battle movies retain one battle call each under the held battle
  start's open comparison (`open C0:18A7`, three instructions). The older
  whole-library `open C0:00BD` allowance awaits the batched ledger update;
  shipping mode executes neither as original code.
  Loose-RAM fuzzing of the whole pass with `--with-registry` needs
  follow-up: from the `field` recipe's frame-11,400 state, seed 1, 2,000
  trials, `--d 0100 --db 00`, `field_main_loop_goes_round` matches 1,363,
  mismatches at trials 113, 260 and 1396, and skips 634 call-outs past the
  budget. All three trials also fail through the existing `field_frame`
  path (which additionally fails trial 1744); the first two differ in A's
  high byte, the third also in object cells. Both runs reach out-of-range
  character names, event scripts and line-effect tables. Determine the
  accepted state invariants and fix the responsible callees/guards; the
  coherent-field continuation recipe is not evidence for arbitrary RAM.
  The sampled gate also interrupts 1,586 returning field passes, 1 first
  pass and 28 frame-wait setups; those are not successful comparisons.
  Keep their caller and object/script states in scope when closing this
  fuzzing gap.
- **What the field's unreached review left.** The `unreached_party_shares`
  recipe fills sharers only up to `$16` where the guard takes up to `$25`: extend
  it to `$24` so the whole accepted range of the sprite-block owner table is
  compared. `unreached_fuzz.py --ledger` silently drops an inventoried entry
  whose name has no recipe; it should report or fail on one. REIMPLEMENTATION.md
  should give the count of plain recipes `--ledger` replays beside the cited
  ones. The two new `location_record_is_one` guards repeat the map's
  `vars_peek` boilerplate: one shared helper.
